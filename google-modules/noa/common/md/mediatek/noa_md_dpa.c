/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD DPA Driver
 *
 * Copyright 2025 Google LLC.
 */
#include <linux/of.h>
#include <linux/platform_device.h>

/* NOA related header */
#include "common/modem_ring_id.h"

/* NOA modem related header */
#include "noa_md.h"
#include "noa_md_dpa.h"
#include "noa_md_trace.h"
#include "noa_md_wwan_notifier.h"

/* DPA related header */
#include "soc/google/google_dpa.h"
#include "soc/google/google_dpa_ctrl.h"
#include "soc/google/google_dpa_doorbell.h"

/* Doorbells names for communication with DPA */
#define MD_DOORBELL_NAME_NEP "nep_doorbell_1"
#define MD_DOORBELL_NAME_NCP "ncp_doorbell_2"

struct noa_md_dpa_isr_data {
	struct noa_md_dpa *dpa_res;
	enum noa_md_dpa_doorbell_type type;
	int id;
};

/**
 * noa_md_dpa_master_isr() - Master ISR for both NCP and NEP doorbells.
 * @data: Private data passed from enable_doorbell, points to our helper
 * struct.
 *
 * This function is the first-level interrupt handler. It unpacks the context,
 * finds the appropriate client-registered callback, and dispatches the call.
 */
static void noa_md_dpa_master_isr(void *data)
{
	struct noa_md_dpa_isr_data *isr_data = data;
	struct noa_md_dpa *dpa_res;
	struct noa_md_dpa_isr_handler *handler = NULL;
	noa_md_dpa_isr_callback_t client_callback = NULL;
	void *client_resource = NULL;
	unsigned long flags;

	CHECK_PTR_OR_RETURN(isr_data);
	CHECK_PTR_OR_RETURN(isr_data->dpa_res);

	dpa_res = isr_data->dpa_res;

	spin_lock_irqsave(&dpa_res->lock, flags);

	if (isr_data->type == NOA_MD_DPA_NCP_DOORBELL) {
		handler = &dpa_res->ncp_isr_handlers[isr_data->id];
	} else if (isr_data->type == NOA_MD_DPA_NEP_DOORBELL) {
		handler = &dpa_res->nep_isr_handlers[isr_data->id];
	} else {
		NOA_MD_ERROR_LIMIT("Invalid type:%d", isr_data->type);
		spin_unlock_irqrestore(&dpa_res->lock, flags);
		return;
	}

	if (handler && handler->callback) {
		client_callback = handler->callback;
		client_resource = handler->resource;
	}

	spin_unlock_irqrestore(&dpa_res->lock, flags);

	if (client_callback)
		client_callback(isr_data->id, client_resource);
}

/**
 * noa_md_dpa_disable_resources() - Disable doorbells and release resources.
 * @dpa_res: Pointer to the NOA MD DPA resource structure.
 *
 * This function is called when the DPA state becomes UNAVAILABLE or CRASH.
 */
static void noa_md_dpa_disable_resources(struct noa_md_dpa *dpa_res)
{
	unsigned long ncp_doorbells_to_disable;
	unsigned long nep_doorbells_to_disable;
	struct google_dpa_doorbell *ncp_doorbell_ref;
	struct google_dpa_doorbell *nep_doorbell_ref;
	struct noa_md_dpa_isr_data *isr_data_to_free;
	unsigned long flags;

	spin_lock_irqsave(&dpa_res->lock, flags);
	ncp_doorbells_to_disable = dpa_res->enabled_ncp_doorbells;
	nep_doorbells_to_disable = dpa_res->enabled_nep_doorbells;
	ncp_doorbell_ref = dpa_res->ncp_doorbell;
	nep_doorbell_ref = dpa_res->nep_doorbell;
	isr_data_to_free = dpa_res->isr_data;

	dpa_res->enabled_ncp_doorbells = 0;
	dpa_res->enabled_nep_doorbells = 0;
	dpa_res->ncp_doorbell = NULL;
	dpa_res->nep_doorbell = NULL;
	dpa_res->dpa_dev = NULL;
	dpa_res->isr_data = NULL;
	spin_unlock_irqrestore(&dpa_res->lock, flags);

	// Disable all doorbells
	if (ncp_doorbell_ref) {
		for (int i = 0; i < NOA_MD_DPA_ISR_ID_MAX; i++) {
			if (test_bit(i, &ncp_doorbells_to_disable))
				google_dpa_doorbell_disable_doorbell(ncp_doorbell_ref, i);
		}
	}

	if (nep_doorbell_ref) {
		for (int i = 0; i < NOA_MD_DPA_ISR_ID_MAX; i++) {
			if (test_bit(i, &nep_doorbells_to_disable))
				google_dpa_doorbell_disable_doorbell(nep_doorbell_ref, i);
		}
	}

	if (isr_data_to_free) {
		kfree(isr_data_to_free);
	}

	NOA_MD_INFO("DPA resources disabled.");
}

/**
 * noa_md_dpa_enable_resources() - Get DPA resources and enable doorbells.
 * @dpa_res: Pointer to the NOA MD DPA resource structure.
 *
 * This function is called when the DPA state becomes READY.
 */
static int noa_md_dpa_enable_resources(struct noa_md_dpa *dpa_res)
{
	struct device *dpa_dev;
	struct google_dpa_doorbell *ncp_doorbell;
	struct google_dpa_doorbell *nep_doorbell;
	struct noa_md_dpa_isr_data *isr_ncp_data_array;
	struct noa_md_dpa_isr_data *isr_nep_data_array;
	int isr_data_count;
	unsigned long flags;
	int ret = 0;

	dpa_dev = google_dpa_get_dpa_dev(dpa_res->dpa);
	CHECK_PTR_OR_RETURN_ERR(dpa_dev, PTR_ERR(dpa_dev));

	ncp_doorbell = google_dpa_get_doorbell(dpa_dev, MD_DOORBELL_NAME_NCP);
	CHECK_PTR_OR_RETURN_ERR(ncp_doorbell, PTR_ERR(ncp_doorbell));

	nep_doorbell = google_dpa_get_doorbell(dpa_dev, MD_DOORBELL_NAME_NEP);
	CHECK_PTR_OR_RETURN_ERR(nep_doorbell, PTR_ERR(nep_doorbell));

	isr_data_count = NOA_MD_DPA_DOORBELL_TYPE_MAX * NOA_MD_DPA_ISR_ID_MAX;
	dpa_res->isr_data = kcalloc(isr_data_count, sizeof(*dpa_res->isr_data), GFP_KERNEL);
	CHECK_PTR_OR_RETURN_ERR(dpa_res->isr_data, -ENOMEM);

	spin_lock_irqsave(&dpa_res->lock, flags);
	dpa_res->dpa_dev = dpa_dev;
	dpa_res->ncp_doorbell = ncp_doorbell;
	dpa_res->nep_doorbell = nep_doorbell;
	spin_unlock_irqrestore(&dpa_res->lock, flags);

	isr_ncp_data_array =
		&dpa_res->isr_data[NOA_MD_DPA_NCP_DOORBELL * NOA_MD_DPA_ISR_ID_MAX];
	isr_nep_data_array =
		&dpa_res->isr_data[NOA_MD_DPA_NEP_DOORBELL * NOA_MD_DPA_ISR_ID_MAX];

	for (int ring_type = 0; ring_type < NOA_MD_DPA_ISR_ID_MAX; ring_type++) {
		struct noa_md_dpa_isr_data *ncp_data = &isr_ncp_data_array[ring_type];
		ncp_data->dpa_res = dpa_res;
		ncp_data->type = NOA_MD_DPA_NCP_DOORBELL;
		ncp_data->id = ring_type;

		ret = google_dpa_doorbell_enable_doorbell(
			dpa_res->ncp_doorbell, ring_type,
			noa_md_dpa_master_isr, ncp_data);
		if (ret) {
			NOA_MD_ERROR("Failed to enable ncp doorbell for ring_type %d, err: %d",
				ring_type, ret);
			goto err_disable_resources;
		}

		set_bit(ring_type, &dpa_res->enabled_ncp_doorbells);

		// TODO(b/429057140): Request NEP to support multiple id
		// Currently NEP only support one id, so we need to disable
		// the multiple id support for NEP.
		// Following code will be unmarked when NEP support multiple id.
		/*
		struct noa_md_dpa_isr_data *nep_data = &isr_nep_data_array[ring_type];
		nep_data->dpa_res = dpa_res;
		nep_data->type = NOA_MD_DPA_NEP_DOORBELL;
		nep_data->id = ring_type;
		ret = google_dpa_doorbell_enable_doorbell(
			dpa_res->nep_doorbell, ring_type,
			noa_md_dpa_master_isr, nep_data);
		if (ret) {
			NOA_MD_ERROR(
				"Failed to enable nep doorbell for ring_type %d, err: %d",
				ring_type, ret);
			goto err_disable_resources;
		}
		set_bit(ring_type, &dpa_res->enabled_nep_doorbells);
		*/
	}

	// TODO(b/429057140): Enable NEP idx 0 due to NEP only support one id
	struct noa_md_dpa_isr_data *nep_data = &isr_nep_data_array[0];
	nep_data->dpa_res = dpa_res;
	nep_data->type = NOA_MD_DPA_NEP_DOORBELL;
	nep_data->id = 0;
	ret = google_dpa_doorbell_enable_doorbell(
			dpa_res->nep_doorbell, 0,
			noa_md_dpa_master_isr, nep_data);
	if (ret) {
		NOA_MD_ERROR(
			"Failed to enable nep doorbell for ring_type 0, err: %d", ret);
		goto err_disable_resources;
	}
	set_bit(0, &dpa_res->enabled_nep_doorbells);

	NOA_MD_INFO("DPA resources enabled successfully.");
	return 0;

err_disable_resources:
	noa_md_dpa_disable_resources(dpa_res);
	return ret;
}

/**
 * __get_dpa_state_string() - Convert DPA state enum to a human-readable
 * string.
 * @state: The DPA state enumeration.
 *
 * Return: A string representing the DPA state.
 */
static const char *__get_dpa_state_string(enum dpa_state state)
{
	switch (state) {
	case NOA_STATE_UNAVAILABLE:
		return "NOA_STATE_UNAVAILABLE";
	case NOA_STATE_READY:
		return "NOA_STATE_READY";
	case NOA_STATE_CRASH:
		return "NOA_STATE_CRASH";
	case NOA_STATE_COUNT:
		return "NOA_STATE_COUNT";
	default:
		NOA_MD_ERROR("unknown state:%d", state);
		return "Unknown DPA State";
	}
}

/**
 * noa_md_dpa_on_state_change() - Callback for DPA state changes.
 * @state: The new DPA state.
 * @context: Client-provided context, not used here.
 *
 * This function is invoked by the DPA controller when the DPA's state
 * changes.
 */
static void noa_md_dpa_on_state_change(enum dpa_state state, void *context)
{
	struct noa_md_dpa *dpa_res = (struct noa_md_dpa *)context;
	struct noa_md_dpa_state_client *state_client, *state_tmp;
	LIST_HEAD(dispatch_list);
	unsigned long flags;
	int ret = 0;
	bool ncp_doorbell_enabled = false;
	bool nep_doorbell_enabled = false;
	bool is_doorbell_enabled = false;

	NOA_MD_INFO("DPA state change event received: %s(%d)",
		__get_dpa_state_string(state), state);

	CHECK_PTR_OR_RETURN(dpa_res);

	spin_lock_irqsave(&dpa_res->lock, flags);
	dpa_res->state = state;

	list_splice_init(&dpa_res->state_client_list, &dispatch_list);

	if (dpa_res->ncp_doorbell)
		ncp_doorbell_enabled = true;
	if (dpa_res->nep_doorbell)
		nep_doorbell_enabled = true;
	if (ncp_doorbell_enabled || nep_doorbell_enabled)
		is_doorbell_enabled = true;
	spin_unlock_irqrestore(&dpa_res->lock, flags);

	if ((ncp_doorbell_enabled && !nep_doorbell_enabled) ||
		(!ncp_doorbell_enabled && nep_doorbell_enabled)) {
		NOA_MD_ERROR("Inconsistent doorbell state, NCP:%d, NEP:%d",
			ncp_doorbell_enabled, nep_doorbell_enabled);
	}

	switch (state) {
	case NOA_STATE_READY:
		if (!is_doorbell_enabled) {
			ret = noa_md_dpa_enable_resources(dpa_res);
			if (ret) {
				NOA_MD_ERROR("Failed to enable DPA resources, err:%d", ret);
			}
		} else {
			NOA_MD_ERROR(
				"Redundant READY: resources already enabled");
		}
		break;
	case NOA_STATE_UNAVAILABLE:
	case NOA_STATE_CRASH:
		noa_md_wwan_notifier_reset();
		if (is_doorbell_enabled) {
			noa_md_dpa_disable_resources(dpa_res);
		} else {
			NOA_MD_ERROR(
				"Redundant UNAVAILABLE/CRASH: resources already disabled");
		}
		break;
	default:
		break;
	}


	list_for_each_entry_safe(state_client, state_tmp, &dispatch_list, node) {
		state_client->callback(state, state_client->context);
	}

	ret = noa_md_wwan_notifier_sync_on_ready();
	if (ret) {
		NOA_MD_ERROR("Failed to sync WWAN notifier: %d", ret);
	}

	if (!list_empty(&dispatch_list)) {
		spin_lock_irqsave(&dpa_res->lock, flags);
		list_splice(&dispatch_list, &dpa_res->state_client_list);
		spin_unlock_irqrestore(&dpa_res->lock, flags);
	}
}

struct noa_md_dpa *noa_md_dpa_get_dpa_context(void)
{
	return md_dev.dpa_res;
}

struct noa_md_dpa_state_client *noa_md_dpa_register_state_client(
	noa_md_dpa_state_callback_t callback, void *context)
{
	struct noa_md_dpa *dpa_res = noa_md_dpa_get_dpa_context();
	struct noa_md_dpa_state_client *client;
	unsigned long flags;

	CHECK_PTR_OR_RETURN_ERR(callback, NULL);
	CHECK_PTR_OR_RETURN_ERR(dpa_res, NULL);

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	CHECK_PTR_OR_RETURN_ERR(client, NULL);
	if (!client)
		return NULL;

	client->callback = callback;
	client->context = context;

	spin_lock_irqsave(&dpa_res->lock, flags);
	list_add_tail(&client->node, &dpa_res->state_client_list);
	spin_unlock_irqrestore(&dpa_res->lock, flags);

	if (dpa_res->state == NOA_STATE_READY) {
		callback(dpa_res->state, context);
	}

	NOA_MD_INFO("Registered %ps, client: 0x%p", callback, client);
	return client;
}
EXPORT_SYMBOL_GPL(noa_md_dpa_register_state_client);


void noa_md_dpa_unregister_state_client(struct noa_md_dpa_state_client *client)
{
	struct noa_md_dpa *dpa_res = noa_md_dpa_get_dpa_context();
	unsigned long flags;

	CHECK_PTR_OR_RETURN(client);
	CHECK_PTR_OR_RETURN(dpa_res);

	spin_lock_irqsave(&dpa_res->lock, flags);
	list_del(&client->node);
	spin_unlock_irqrestore(&dpa_res->lock, flags);

	kfree(client);
}
EXPORT_SYMBOL_GPL(noa_md_dpa_unregister_state_client);

int noa_md_dpa_register_isr(
	enum noa_md_dpa_doorbell_type type, int id,
	noa_md_dpa_isr_callback_t callback, void *resource)
{
	struct noa_md_dpa *dpa_res = noa_md_dpa_get_dpa_context();
	struct noa_md_dpa_isr_handler *handler_array;
	unsigned long flags;

	CHECK_PTR_OR_RETURN_ERR(dpa_res, -ENODEV);
	CHECK_PTR_OR_RETURN_ERR(callback, -ENODEV);

	NOA_MD_INFO("type:%d, id:%d", type, id);
	CHECK_TRUE_OR_RETURN_ERR(type >= NOA_MD_DPA_DOORBELL_TYPE_MAX, -EINVAL);
	CHECK_TRUE_OR_RETURN_ERR(id >= NOA_MD_DPA_ISR_ID_MAX, -EINVAL);

	spin_lock_irqsave(&dpa_res->lock, flags);

	handler_array = (type == NOA_MD_DPA_NCP_DOORBELL) ?
	dpa_res->ncp_isr_handlers : dpa_res->nep_isr_handlers;

	if (handler_array[id].callback) {
		spin_unlock_irqrestore(&dpa_res->lock, flags);
		NOA_MD_ERROR("ISR for type %d, id %u already registered", type, id);
		return -EBUSY;
	}

	handler_array[id].callback = callback;
	handler_array[id].resource = resource;

	spin_unlock_irqrestore(&dpa_res->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(noa_md_dpa_register_isr);

int noa_md_dpa_unregister_isr(enum noa_md_dpa_doorbell_type type, int id)
{
	struct noa_md_dpa *dpa_res = noa_md_dpa_get_dpa_context();
	struct noa_md_dpa_isr_handler *handler_array;
	unsigned long flags;

	CHECK_PTR_OR_RETURN_ERR(dpa_res, -ENODEV);
	CHECK_TRUE_OR_RETURN_ERR(type >= NOA_MD_DPA_DOORBELL_TYPE_MAX, -EINVAL);
	CHECK_TRUE_OR_RETURN_ERR(id >= NOA_MD_DPA_ISR_ID_MAX, -EINVAL);

	spin_lock_irqsave(&dpa_res->lock, flags);

	handler_array = (type == NOA_MD_DPA_NCP_DOORBELL) ?
	dpa_res->ncp_isr_handlers : dpa_res->nep_isr_handlers;

	handler_array[id].callback = NULL;
	handler_array[id].resource = NULL;

	spin_unlock_irqrestore(&dpa_res->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(noa_md_dpa_unregister_isr);

/**
 * noa_md_dpa_notify_ncp() - Ring a doorbell to notify ncp by doorbell id.
 * @id: The doorbell id.
 *
 * This function is called to ring a doorbell to notify ncp by doorbell id.
 */
void noa_md_dpa_notify_ncp(int id)
{
	struct noa_md_dpa *dpa_res = noa_md_dpa_get_dpa_context();
	enum dpa_state state;

	CHECK_PTR_OR_RETURN(dpa_res);
	CHECK_PTR_OR_RETURN(dpa_res->ncp_doorbell);

	state = dpa_res->state;
	CHECK_TRUE_OR_RETURN(state != NOA_STATE_READY);

	NOA_MD_DATA("notify ncp for id:%d, dpa_state:%s(%d)", id,
		__get_dpa_state_string(state), state);
	google_dpa_doorbell_ring_mcu(dpa_res->ncp_doorbell, id);
}

static struct dpa_callbacks dpa_callbacks = {
	.on_data_path_changed = NULL,
	.on_state_changed = noa_md_dpa_on_state_change,
};

/**
 * noa_md_dpa_destroy() - Clean up and release all DPA-related resources.
 *
 * This function unregisters the DPA client and releases the reference to the
 * DPA device. It's called during driver unload or in the probe error path.
 */
static void noa_md_dpa_destroy(struct platform_device *pdev)
{
	struct noa_md_dpa *dpa_res = platform_get_drvdata(pdev);
	struct dpa_client *client_to_unregister;
	struct google_dpa *dpa_to_put;
	struct noa_md_dpa_state_client *state_client, *state_tmp;
	LIST_HEAD(dispatch_list);
	unsigned long flags;

	CHECK_PTR_OR_RETURN(dpa_res);

	noa_md_dpa_disable_resources(dpa_res);

	spin_lock_irqsave(&dpa_res->lock, flags);

	client_to_unregister = dpa_res->registered_dpa_client;
	dpa_to_put = dpa_res->dpa;

	dpa_res->registered_dpa_client = NULL;
	dpa_res->dpa = NULL;

	list_splice_init(&dpa_res->state_client_list, &dispatch_list);
	spin_unlock_irqrestore(&dpa_res->lock, flags);

	if (client_to_unregister) {
		google_dpa_ctrl_unregister(client_to_unregister);
	}

	if (dpa_to_put) {
		google_dpa_put(dpa_to_put);
	}

	list_for_each_entry_safe(state_client, state_tmp, &dispatch_list, node) {
		NOA_MD_INFO("Forcibly unregistering a stale state client during teardown");
		list_del(&state_client->node);
		kfree(state_client);
	}
}

/**
 * noa_md_dpa_probe() - Probe function for the DPA platform device.
 * @pdev: Pointer to the platform_device structure.
 *
 * This function is called by the kernel when a device matching the driver's
 * compatibility string is found. It acquires DPA resources like the DPA
 * device handle and doorbells, and registers a client with the DPA controller.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_dpa_probe(struct platform_device *pdev)
{
	struct noa_md_dpa *dpa_res = noa_md_dpa_get_dpa_context();
	struct device *dev;
	struct google_dpa *dpa;
	struct dpa_client *dpa_client;
	int ret = -ENODEV;
	unsigned long flags;

	CHECK_PTR_OR_GOTO_ERR(dpa_res, out_err);

	dev = &pdev->dev;
	CHECK_PTR_OR_GOTO_ERR(dev, out_err);

	platform_set_drvdata(pdev, dpa_res);

	dpa = google_dpa_get(dev);
	CHECK_PTR_OR_GOTO_ERR(dpa, out_err);

	spin_lock_irqsave(&dpa_res->lock, flags);
	dpa_res->dev = dev;
	dpa_res->dpa = dpa;
	spin_unlock_irqrestore(&dpa_res->lock, flags);

	// Register a client to get state change notifications.
	dpa_client = google_dpa_ctrl_register(
		"noa_md", &dpa_callbacks, (void *)dpa_res);
	CHECK_PTR_OR_GOTO_ERR(dpa_client, out_destroy);

	spin_lock_irqsave(&dpa_res->lock, flags);
	dpa_res->registered_dpa_client = dpa_client;
	spin_unlock_irqrestore(&dpa_res->lock, flags);

	return 0;

out_destroy:
	noa_md_dpa_destroy(pdev);
out_err:
	return ret;
}

static const struct of_device_id noa_md_dpa_of_match[] = {
	{.compatible = "google,dpa",},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, noa_md_dpa_of_match);

static struct platform_driver noa_md_dpa_driver = {
	.probe = noa_md_dpa_probe,
	.remove = noa_md_dpa_destroy,
	.driver = {
		.name = "dpa_md",
		.owner = THIS_MODULE,
		.of_match_table = noa_md_dpa_of_match,
	},
};

/**
 * noa_md_dpa_init() - Initialize DPA resources for NOA MD.
 * @p_noa_dev: Pointer to the NOA modem device structure (&struct noa_md_dev).
 *
 * Allocates the DPA context structure within @p_noa_dev and registers the
 * DPA platform driver (&noa_md_dpa_driver).
 *
 * Context: Kernel initialization path.
 * Return:
 * * %0: On success.
 * * %-ENOMEM: If memory allocation for DPA context fails.
 * * Other negative error codes from platform_driver_register().
 */
int noa_md_dpa_init(struct noa_md_dev *p_noa_dev)
{
	int ret = 0;
	NOA_MD_INFO("enter");
	p_noa_dev->dpa_res = kzalloc(sizeof(*p_noa_dev->dpa_res), GFP_KERNEL);
	if (!p_noa_dev->dpa_res) {
		NOA_MD_ERROR("Failed to allocate dpa res");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&p_noa_dev->dpa_res->state_client_list);

	spin_lock_init(&p_noa_dev->dpa_res->lock);

	ret = platform_driver_register(&noa_md_dpa_driver);
	NOA_MD_INFO("platform_driver_register:%d", ret);
	if (ret) {
		kfree(p_noa_dev->dpa_res);
		p_noa_dev->dpa_res = NULL;
	}
	return ret;
}

/**
 * noa_md_dpa_release() - Release DPA resources for NOA MD.
 * @p_noa_dev: Pointer to the NOA modem device structure (&struct noa_md_dev).
 *
 * Unregisters the DPA platform driver and frees the DPA context structure
 * stored in @p_noa_dev.
 *
 * Context: Kernel module exit path.
 */
void noa_md_dpa_release(struct noa_md_dev *p_noa_dev)
{
	NOA_MD_INFO("platform_driver_unregister");
	platform_driver_unregister(&noa_md_dpa_driver);
	if (p_noa_dev->dpa_res) {
		kfree(p_noa_dev->dpa_res);
		p_noa_dev->dpa_res = NULL;
	}
}
