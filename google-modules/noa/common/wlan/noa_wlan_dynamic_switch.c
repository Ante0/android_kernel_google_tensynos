#include "noa_wlan_dynamic_switch.h"
#include "noa_wlan_hw.h"
#include "noa_wlan_client.h"
#include "google_plat_internal.h"
#include "noa.h"

struct noa_wlan_dynamic_switch_ops {
	void (*svc_pre_switch)(struct noa_wlan_switch_manager *);
	void (*svc_post_switch)(struct noa_wlan_switch_manager *);
	void (*dev_pre_switch)(struct noa_wlan_switch_manager *);
	void (*dev_post_switch)(struct noa_wlan_switch_manager *);
};

static void noa_wlan_service_pre_switch(const struct noa_wlan_dynamic_switch_ops *ops,
					struct noa_wlan_switch_manager *manager)
{
	if (ops && ops->svc_pre_switch) {
		ops->svc_pre_switch(manager);
	}
}

static void noa_wlan_service_post_switch(const struct noa_wlan_dynamic_switch_ops *ops,
					 struct noa_wlan_switch_manager *manager)
{
	if (ops && ops->svc_post_switch) {
		ops->svc_post_switch(manager);
	}
}

static void noa_wlan_device_pre_switch(const struct noa_wlan_dynamic_switch_ops *ops,
				       struct noa_wlan_switch_manager *manager)
{
	if (ops && ops->dev_pre_switch) {
		ops->dev_pre_switch(manager);
	}
}

static void noa_wlan_device_post_switch(const struct noa_wlan_dynamic_switch_ops *ops,
					struct noa_wlan_switch_manager *manager)
{
	if (ops && ops->dev_post_switch) {
		ops->dev_post_switch(manager);
	}
}

static void platform_bus_init(struct noa_wlan_switch_manager *manager)
{
	struct platform_bus_ops **current_plat_ops =
		(struct platform_bus_ops **)manager->current_plat_ops;
	void *vendor_pbus = manager->vendor_bus;
	void *vendor_pdev = manager->vendor_dev;

	if (current_plat_ops && *current_plat_ops && (*current_plat_ops)->init) {
		(*current_plat_ops)->init(vendor_pdev, vendor_pbus);
	}
}

static void platform_bus_stop(struct noa_wlan_switch_manager *manager)
{
	struct platform_bus_ops **current_plat_ops =
		(struct platform_bus_ops **)manager->current_plat_ops;
	void *vendor_pbus = manager->vendor_bus;

	if (current_plat_ops && *current_plat_ops && (*current_plat_ops)->stop) {
		(*current_plat_ops)->stop(vendor_pbus);
	}
}

static void platform_bus_start(struct noa_wlan_switch_manager *manager)
{
	struct platform_bus_ops **current_plat_ops =
		(struct platform_bus_ops **)manager->current_plat_ops;
	void *vendor_pbus = manager->vendor_bus;

	if (current_plat_ops && *current_plat_ops && (*current_plat_ops)->start) {
		(*current_plat_ops)->start(vendor_pbus);
	}
}

static void platform_bus_request_irq(struct noa_wlan_switch_manager *manager)
{
	struct platform_bus_ops **current_plat_ops =
		(struct platform_bus_ops **)manager->current_plat_ops;
	void *vendor_pbus = manager->vendor_bus;

	if (current_plat_ops && *current_plat_ops && (*current_plat_ops)->request_irq) {
		(*current_plat_ops)->request_irq(vendor_pbus);
	}
}

static void platform_bus_exit(struct noa_wlan_switch_manager *manager)
{
	struct platform_bus_ops **current_plat_ops =
		(struct platform_bus_ops **)manager->current_plat_ops;
	void *vendor_pbus = manager->vendor_bus;

	if (current_plat_ops && *current_plat_ops && (*current_plat_ops)->exit) {
		(*current_plat_ops)->exit(vendor_pbus);
	}
}

static const char *get_event_name(enum noa_wlan_dynamic_switch_event event)
{
#define CASE_EVENT_NAME(event)                                                                     \
	case NOA_WLAN_##event:                                                                     \
		return #event
	switch (event) {
		CASE_EVENT_NAME(SERVICE_PRE_SWITCH);
		CASE_EVENT_NAME(SERVICE_POST_SWITCH);
		CASE_EVENT_NAME(DEVICE_PRE_SWITCH);
		CASE_EVENT_NAME(DEVICE_POST_SWITCH);
	default:
		break;
	}

	return "Unknown";
}

static const char *get_state_name(enum noa_wlan_dynamic_switch_state state)
{
#define CASE_STATE_NAME(state)                                                                     \
	case NOA_WLAN_##state:                                                                     \
		return #state
	switch (state) {
		CASE_STATE_NAME(DATA_PATH_OPERATING);
		CASE_STATE_NAME(SERVICE_PRE_SWITCHED);
		CASE_STATE_NAME(SERVICE_POST_SWITCHED);
		CASE_STATE_NAME(DEVICE_PRE_SWITCHED);
		CASE_STATE_NAME(DEVICE_POST_SWITCHED);
	default:
		break;
	}

	return "Unknown";
}

static struct noa_wlan_client *get_noa_wlan_client(struct noa_wlan_switch_manager *manager)
{
	return (struct noa_wlan_client *)(manager->noa_wlan_client);
}

static struct platform_bus_ops *get_dpa_bus_ops(struct noa_wlan_switch_manager *manager)
{
	return (struct platform_bus_ops *)(manager->dpa_plat_ops);
}

static struct platform_bus_ops *get_vendor_bus_ops(struct noa_wlan_switch_manager *manager)
{
	return (struct platform_bus_ops *)(manager->vendor_plat_ops);
}

static enum noa_wlan_dynamic_switch_state
get_dynamic_switch_state(const struct noa_wlan_switch_manager *manager)
{
	return manager->state;
}

static void set_dynamic_switch_state(struct noa_wlan_switch_manager *manager,
				     enum noa_wlan_dynamic_switch_state state)
{
	manager->state = state;
}

static void set_dp_mode(struct noa_wlan_switch_manager *manager,
			enum noa_wlan_data_path_mode dp_mode)
{
	manager->dp_mode = dp_mode;
	noa_wlan_enable_set(dp_mode == NOA_WLAN_DATA_PATH_OFFLOAD_MODE);
}

static void set_current_plat_ops(struct noa_wlan_switch_manager *manager,
				 struct platform_bus_ops *ops)
{
	if (manager->current_plat_ops) {
		*(manager->current_plat_ops) = (void *)ops;
	}
}

int noa_wlan_dynamic_switch_init(struct noa_wlan_switch_manager *manager,
				 struct noa_wlan_dynamic_switch_init_params *params)
{
	memset(manager, 0, sizeof(struct noa_wlan_switch_manager));
	manager->vendor_plat_ops = params->vendor_plat_ops;
	manager->dpa_plat_ops = params->dpa_plat_ops;
	manager->current_plat_ops = params->current_plat_ops;
	manager->vendor_bus = params->vendor_bus;
	manager->vendor_dev = params->vendor_dev;
	manager->dp_mode = params->dp_mode;
	manager->state = NOA_WLAN_DATA_PATH_OPERATING;
	manager->dpa_ssr_policy = NOA_WLAN_DATA_PATH_BYPASS_MODE;
	set_current_plat_ops(manager,
			     get_noa_wlan_dp_mode(manager) == NOA_WLAN_DATA_PATH_OFFLOAD_MODE ?
				     get_dpa_bus_ops(manager) :
				     get_vendor_bus_ops(manager));

	noa_wlan_hw_dynamic_switch_init(manager);
	return 0;
}

void noa_wlan_dynamic_switch_deinit(struct noa_wlan_switch_manager *manager)
{
	noa_wlan_hw_dynamic_switch_deinit(manager);
	memset(manager, 0, sizeof(struct noa_wlan_switch_manager));
}

static void noa_wlan_bypass_mode_svc_pre_switch_handler(struct noa_wlan_switch_manager *manager)
{
	// Nothing to do.
}

static void noa_wlan_bypass_mode_dev_pre_switch_handler(struct noa_wlan_switch_manager *manager)
{
	struct platform_bus_ops *vendor_bus_ops = get_vendor_bus_ops(manager);

	// DPA plat ops
	platform_bus_stop(manager);
	platform_bus_exit(manager);
	set_current_plat_ops(manager, vendor_bus_ops);
}

static void noa_wlan_bypass_mode_dev_post_switch_handler(struct noa_wlan_switch_manager *manager)
{
	// vendor plat ops
	platform_bus_init(manager);
	platform_bus_start(manager);
	platform_bus_request_irq(manager);

	set_dp_mode(manager, NOA_WLAN_DATA_PATH_BYPASS_MODE);
}

static void noa_wlan_bypass_mode_svc_post_switch_handler(struct noa_wlan_switch_manager *manager)
{
	// Nothing to do.
}

static void noa_wlan_offload_mode_svc_pre_switch_handler(struct noa_wlan_switch_manager *manager)
{
	// Nothing to do.
}

static void noa_wlan_offload_mode_dev_pre_switch_handler(struct noa_wlan_switch_manager *manager)
{
	struct platform_bus_ops *dpa_bus_ops = get_dpa_bus_ops(manager);

	// vendor plat ops
	platform_bus_stop(manager);
	platform_bus_exit(manager);
	set_current_plat_ops(manager, dpa_bus_ops);
}

static void noa_wlan_offload_mode_dev_post_switch_handler(struct noa_wlan_switch_manager *manager)
{
	// DPA plat ops
	platform_bus_init(manager);
	platform_bus_start(manager);
	platform_bus_request_irq(manager);

	set_dp_mode(manager, NOA_WLAN_DATA_PATH_OFFLOAD_MODE);
}

static void noa_wlan_offload_mode_svc_post_switch_handler(struct noa_wlan_switch_manager *manager)
{
	// Nothing to do.
}

int noa_wlan_dynamic_switch_event_handler(struct noa_wlan_switch_manager *manager,
					  enum noa_wlan_dynamic_switch_event event)
{
	static const struct noa_wlan_dynamic_switch_ops noa_wlan_switch_to_offload_mode_ops = {
		.svc_pre_switch = noa_wlan_offload_mode_svc_pre_switch_handler,
		.svc_post_switch = noa_wlan_offload_mode_svc_post_switch_handler,
		.dev_pre_switch = noa_wlan_offload_mode_dev_pre_switch_handler,
		.dev_post_switch = noa_wlan_offload_mode_dev_post_switch_handler,
	};
	static const struct noa_wlan_dynamic_switch_ops noa_wlan_switch_to_bypass_mode_ops = {
		.svc_pre_switch = noa_wlan_bypass_mode_svc_pre_switch_handler,
		.svc_post_switch = noa_wlan_bypass_mode_svc_post_switch_handler,
		.dev_pre_switch = noa_wlan_bypass_mode_dev_pre_switch_handler,
		.dev_post_switch = noa_wlan_bypass_mode_dev_post_switch_handler,
	};
	enum noa_wlan_dynamic_switch_state current_state = get_dynamic_switch_state(manager);
	enum noa_wlan_data_path_mode dp_mode = get_noa_wlan_dp_mode(manager);
	/**
     * The opposite mode switch operation must be obtained to switch to
     * opposite mode.
     */
	const struct noa_wlan_dynamic_switch_ops *switch_ops =
		dp_mode == NOA_WLAN_DATA_PATH_BYPASS_MODE ? &noa_wlan_switch_to_offload_mode_ops :
							    &noa_wlan_switch_to_bypass_mode_ops;

	switch (event) {
	case NOA_WLAN_SERVICE_PRE_SWITCH:
		if (current_state != NOA_WLAN_DATA_PATH_OPERATING) {
			pr_err("Receive an invalid event: %s (mode: %s/state: %s)",
			       get_event_name(event), get_data_path_mode_name(dp_mode),
			       get_state_name(current_state));
		}
		noa_wlan_service_pre_switch(switch_ops, manager);
		set_dynamic_switch_state(manager, NOA_WLAN_SERVICE_PRE_SWITCHED);
		break;
	case NOA_WLAN_SERVICE_POST_SWITCH:
		if (current_state != NOA_WLAN_DEVICE_POST_SWITCHED) {
			pr_err("Receive an invalid event: %s (mode: %s/state: %s)",
			       get_event_name(event), get_data_path_mode_name(dp_mode),
			       get_state_name(current_state));
		}
		noa_wlan_service_post_switch(switch_ops, manager);
		set_dynamic_switch_state(manager, NOA_WLAN_DATA_PATH_OPERATING);
		break;
	case NOA_WLAN_DEVICE_PRE_SWITCH:
		if (current_state != NOA_WLAN_SERVICE_PRE_SWITCHED) {
			pr_err("Receive an invalid event: %s (mode: %s/state: %s)",
			       get_event_name(event), get_data_path_mode_name(dp_mode),
			       get_state_name(current_state));
		}
		noa_wlan_device_pre_switch(switch_ops, manager);
		set_dynamic_switch_state(manager, NOA_WLAN_DEVICE_PRE_SWITCHED);
		break;
	case NOA_WLAN_DEVICE_POST_SWITCH:
		if (current_state != NOA_WLAN_DEVICE_PRE_SWITCHED) {
			pr_err("Receive an invalid event: %s (mode: %s/state: %s)",
			       get_event_name(event), get_data_path_mode_name(dp_mode),
			       get_state_name(current_state));
		}
		noa_wlan_device_post_switch(switch_ops, manager);
		set_dynamic_switch_state(manager, NOA_WLAN_DEVICE_POST_SWITCHED);
		break;
	default:
		pr_err("Receive an unknown event: %s(%u)", get_event_name(event), event);
		break;
	}

	return 0;
}

void noa_wlan_pre_reset_dpa(struct noa_wlan_switch_manager *manager)
{
	struct noa_wlan_client *client = get_noa_wlan_client(manager);

	if (get_noa_wlan_dp_mode(manager) == NOA_WLAN_DATA_PATH_BYPASS_MODE) {
		pr_info("%s(): Current mode is BYPASS, no need to stop DPA bus", __func__);
		return;
	}

	if (client != NULL) {
		client->dpa_crash_state = true;
	}

	// DPA plat ops
	platform_bus_stop(manager);
	platform_bus_exit(manager);
}

void noa_wlan_post_reset_dpa(struct noa_wlan_switch_manager *manager)
{
	struct noa_wlan_client *client;
	struct platform_bus_ops *vendor_bus_ops = get_vendor_bus_ops(manager);
	struct platform_bus_ops *dpa_bus_ops = get_dpa_bus_ops(manager);

	if (get_noa_wlan_dp_mode(manager) == NOA_WLAN_DATA_PATH_BYPASS_MODE) {
		return;
	}

	if (manager->dpa_ssr_policy == NOA_WLAN_DATA_PATH_OFFLOAD_MODE) {
		pr_info("%s(): DPA SSR policy is OFFLOAD mode", __func__);
		set_current_plat_ops(manager, dpa_bus_ops);
	} else if (manager->dpa_ssr_policy == NOA_WLAN_DATA_PATH_BYPASS_MODE) {
		pr_info("%s(): DPA SSR policy is BYPASS mode", __func__);
		set_current_plat_ops(manager, vendor_bus_ops);
	} else {
		pr_err("%s(): Invalid DPA SSR policy %d", __func__, manager->dpa_ssr_policy);
		return;
	}

	platform_bus_init(manager);
	platform_bus_start(manager);
	platform_bus_request_irq(manager);

	client = get_noa_wlan_client(manager);
	if (client != NULL) {
		client->dpa_crash_state = false;
	}

	if (manager->dpa_ssr_policy == NOA_WLAN_DATA_PATH_OFFLOAD_MODE) {
		set_dp_mode(manager, NOA_WLAN_DATA_PATH_OFFLOAD_MODE);
	} else if (manager->dpa_ssr_policy == NOA_WLAN_DATA_PATH_BYPASS_MODE) {
		set_dp_mode(manager, NOA_WLAN_DATA_PATH_BYPASS_MODE);
	}
}
