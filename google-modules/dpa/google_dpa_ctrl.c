// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/export.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>

#ifdef USE_PIXEL_CPM
#include <soc/google/goog_mba_cpm_iface.h>
#include <soc/google/goog_cpm_service_ids.h>
#endif

#include "google_dpa_ctrl_internal.h"
#include "google_dpa_internal.h"

#ifdef USE_PIXEL_CPM
// TODO(b/400972856): Move service ids to CPM headers after merging
//                    to main branch.
// DPA ID is not defined in the main branch kernel.
// Pick a far away ID to avoid collision.
#define APC_COMMON_SERVICE_ID_DPA (0x36)

// DPA ID is not defined in the main branch CPM
// includes/interfaces/protocols/mba/include/interfaces/protocols/mba/cpm/common/service_ids.h
#define CPM_COMMON_DPA_SERVICE (0x36)
enum {
	CPM_DPA_CMD_NOTIFY_DPA_POWER_ON = 1,
	CPM_DPA_CMD_NOTIFY_DPA_POWER_OFF = 2,
	CPM_DPA_CMD_PCIE_OWNER_SWITCH = 13,
};

#define CPM_SEND_TIMEOUT_MS (3000)
#define CPM_RESPONSE_TIMEOUT_MS (3000)

// cpm/google/platform/gsp/service/dpa/mbu/include/dpa_ipc.h
enum {
	CPM_DPA_PCIE_OWNER_DPA = 0,
	CPM_DPA_PCIE_OWNER_AP = 1,
};
#endif

struct google_dpa_ctrl {
	struct google_dpa *dpa;
	/*
	 * init/deinit is called by dpa, but set_data_path will be called
	 * from external governors or sysfs, use a mutex to protect the
	 * control flow.
	 */
	struct mutex mutex;
	enum dpa_state state;
	enum dpa_data_path data_path;
	enum dpa_pcie_ownership pcie_ownership;
	struct list_head client_list;

#ifdef USE_PIXEL_CPM
	struct cpm_iface_client *cpm_client;
#endif
};

// A client might register to DPA controller before the controller
// initialization, data must be initialized before executing
// google_dpa_ctrl_init().
static struct google_dpa_ctrl dpa_ctrl = {
	.dpa = NULL,
	.mutex = __MUTEX_INITIALIZER(dpa_ctrl.mutex),
	.state = NOA_STATE_UNAVAILABLE,
	.data_path = NOA_DATA_PATH_DIRECT,
	.pcie_ownership = NOA_PCIE_OWNERSHIP_APC,
	.client_list = LIST_HEAD_INIT(dpa_ctrl.client_list),
};

static void notify_client_sync_locked(void (*work_func)(struct work_struct *work))
{
	struct dpa_client *client;

	list_for_each_entry(client, &dpa_ctrl.client_list, list) {
		INIT_WORK(&client->event_work, work_func);
		schedule_work(&client->event_work);
	}
	list_for_each_entry(client, &dpa_ctrl.client_list, list) {
		flush_work(&client->event_work);
	}
}

static void notify_state_changed_work(struct work_struct *work)
{
	struct dpa_client *client = container_of(work, struct dpa_client, event_work);

	client->callbacks.on_state_changed(google_dpa_ctrl_get_state(), client->context);
}

static void google_dpa_ctrl_set_state_locked(enum dpa_state new_state)
{
	if (dpa_ctrl.state == new_state) {
		dev_warn(dpa_ctrl.dpa->dev, "new state %d is the same as old state\n", new_state);
		return;
	}

	dpa_ctrl.state = new_state;
	notify_client_sync_locked(notify_state_changed_work);
}

void google_dpa_ctrl_set_state(enum dpa_state new_state)
{
	mutex_lock(&dpa_ctrl.mutex);

	google_dpa_ctrl_set_state_locked(new_state);

	mutex_unlock(&dpa_ctrl.mutex);
}

enum dpa_state google_dpa_ctrl_get_state(void)
{
	return dpa_ctrl.state;
}

static int google_dpa_ctrl_switch_path_locked(enum dpa_data_path new_data_path)
{
	dev_info(dpa_ctrl.dpa->dev, "Switch data path to %d", new_data_path);
	// TODO: Switch NOA data path
	return 0;
}

static void notify_data_path_changed_work_internal(struct work_struct *work, enum dpa_action action) {
	struct dpa_client *client = container_of(work, struct dpa_client, event_work);

	if (!client->callbacks.on_data_path_changed) {
		return;
	}

	client->callbacks.on_data_path_changed(google_dpa_ctrl_get_data_path(),
					       action, client->context);
}

static void notify_data_path_changed_work(struct work_struct *work)
{
	notify_data_path_changed_work_internal(work, NOA_ACTION_UPDATE);
}

static void notify_service_pre_switch_work(struct work_struct *work)
{
	notify_data_path_changed_work_internal(work, NOA_ACTION_SERVICE_PRE_SWITCH);
}

static void notify_device_pre_switch_work(struct work_struct *work)
{
	notify_data_path_changed_work_internal(work, NOA_ACTION_DEVICE_PRE_SWITCH);
}

static void notify_device_post_switch_work(struct work_struct *work)
{
	notify_data_path_changed_work_internal(work, NOA_ACTION_DEVICE_POST_SWITCH);
}

static void notify_service_post_switch_work(struct work_struct *work)
{
	notify_data_path_changed_work_internal(work, NOA_ACTION_SERVICE_POST_SWITCH);
}

static int google_dpa_ctrl_set_data_path_locked(enum dpa_data_path new_data_path)
{
	if (dpa_ctrl.data_path == new_data_path) {
		dev_warn(dpa_ctrl.dpa->dev, "new data_path %d is the same as old state\n",
			 new_data_path);
		return 0;
	}

	if (google_dpa_ctrl_get_state() != NOA_STATE_READY) {
		dev_warn(dpa_ctrl.dpa->dev, "DPA is not ready yet.\n");
		return -EBUSY;
	}

	dpa_ctrl.data_path = new_data_path;

	notify_client_sync_locked(notify_service_pre_switch_work);

	notify_client_sync_locked(notify_device_pre_switch_work);

	google_dpa_ctrl_switch_path_locked(new_data_path);

	notify_client_sync_locked(notify_device_post_switch_work);

	notify_client_sync_locked(notify_service_post_switch_work);

	return 0;
}

int google_dpa_ctrl_set_data_path(enum dpa_data_path new_data_path)
{
	mutex_lock(&dpa_ctrl.mutex);

	int ret = google_dpa_ctrl_set_data_path_locked(new_data_path);

	mutex_unlock(&dpa_ctrl.mutex);
	return ret;
}

enum dpa_data_path google_dpa_ctrl_get_data_path(void)
{
	return dpa_ctrl.data_path;
}

enum dpa_pcie_ownership google_dpa_ctrl_get_pcie_ownership(void)
{
	return dpa_ctrl.pcie_ownership;
}

static void notify_pcie_ownership_changed_internal(struct work_struct *work, enum dpa_action action) {
	struct dpa_client *client = container_of(work, struct dpa_client, event_work);

	if (!client->callbacks.on_pcie_ownership_changed) {
		return;
	}

	client->callbacks.on_pcie_ownership_changed(google_dpa_ctrl_get_pcie_ownership(),
						    action, client->context);
}

static void notify_pcie_ownership_changed_work(struct work_struct *work)
{
	notify_pcie_ownership_changed_internal(work, NOA_ACTION_UPDATE);
}

static void notify_pcie_ownership_pre_switch_work(struct work_struct *work)
{
	notify_pcie_ownership_changed_internal(work, NOA_ACTION_PCIE_OWNERSHIP_PRE_SWITCH);
}

static void notify_pcie_ownership_post_switch_work(struct work_struct *work)
{
	notify_pcie_ownership_changed_internal(work, NOA_ACTION_PCIE_OWNERSHIP_POST_SWITCH);
}

#ifdef USE_PIXEL_CPM
static int google_dpa_ctrl_cpm_send_message(u32 cmd, u32 arg)
{
	if (!dpa_ctrl.cpm_client) {
		dev_warn(dpa_ctrl.dpa->dev, "No CPM client to handle PCIe ownership switch.");
		return -EIO;
	}

	struct cpm_iface_req cpm_req = { 0 };
	struct cpm_iface_payload req_msg = { 0 };
	struct cpm_iface_payload resp_msg = { 0 };

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = CPM_SEND_TIMEOUT_MS;
	cpm_req.dst_id = CPM_COMMON_DPA_SERVICE;

	req_msg.payload[0] = cmd;
	req_msg.payload[1] = arg;

	return cpm_send_message(dpa_ctrl.cpm_client, &cpm_req);
}
#endif

static int google_dpa_ctrl_switch_pcie_ownership_locked(enum dpa_pcie_ownership new_pcie_ownership)
{
	dev_info(dpa_ctrl.dpa->dev, "Switch PCIe ownership to %d", new_pcie_ownership);
#ifdef USE_PIXEL_CPM
	u32 owner = new_pcie_ownership == NOA_PCIE_OWNERSHIP_APC ? CPM_DPA_PCIE_OWNER_AP :
								   CPM_DPA_PCIE_OWNER_DPA;
	return google_dpa_ctrl_cpm_send_message(CPM_DPA_CMD_PCIE_OWNER_SWITCH, owner);
#else
	return 0;
#endif
}

static int google_dpa_ctrl_set_pcie_ownership_locked(enum dpa_pcie_ownership new_pcie_ownership)
{
	if (dpa_ctrl.pcie_ownership == new_pcie_ownership) {
		dev_warn(dpa_ctrl.dpa->dev, "new pcie_ownership %d is the same as old state\n",
			 new_pcie_ownership);
		return 0;
	}

	if (google_dpa_ctrl_get_state() != NOA_STATE_READY) {
		dev_warn(dpa_ctrl.dpa->dev, "DPA is not ready yet.\n");
		return -EBUSY;
	}

	dpa_ctrl.pcie_ownership = new_pcie_ownership;

	notify_client_sync_locked(notify_pcie_ownership_pre_switch_work);

	google_dpa_ctrl_switch_pcie_ownership_locked(new_pcie_ownership);

	notify_client_sync_locked(notify_pcie_ownership_post_switch_work);

	return 0;
}

int google_dpa_ctrl_set_pcie_ownership(enum dpa_pcie_ownership new_pcie_ownership)
{
	mutex_lock(&dpa_ctrl.mutex);

	int ret = google_dpa_ctrl_set_pcie_ownership_locked(new_pcie_ownership);

	mutex_unlock(&dpa_ctrl.mutex);
	return ret;
}

int google_dpa_ctrl_init(struct google_dpa *dpa)
{
	int ret = 0;

	mutex_lock(&dpa_ctrl.mutex);
	dpa_ctrl.dpa = dpa;

	if (google_dpa_ctrl_get_state() != NOA_STATE_UNAVAILABLE) {
		ret = -EBUSY;
		goto init_unlock;
	}

#ifdef USE_PIXEL_CPM
	dpa_ctrl.cpm_client =
		cpm_iface_request_client(dpa->dev, CPM_COMMON_DPA_SERVICE, NULL, &dpa_ctrl);
	if (!dpa_ctrl.cpm_client) {
		dev_warn(dpa_ctrl.dpa->dev, "Cannot get CPM client.");
	}

	// Note DPA is on to activate CPM DPA service.
	google_dpa_ctrl_cpm_send_message(CPM_DPA_CMD_NOTIFY_DPA_POWER_ON, 0);
#endif

init_unlock:
	mutex_unlock(&dpa_ctrl.mutex);

	return ret;
}

int google_dpa_ctrl_deinit(struct google_dpa *dpa)
{
	int ret = 0;
	enum dpa_state dpa_state = NOA_STATE_UNAVAILABLE;

	if (!dpa)
		return -EINVAL;

	if (dpa != dpa_ctrl.dpa)
		return -EINVAL;

	mutex_lock(&dpa_ctrl.mutex);

	dpa_state = google_dpa_ctrl_get_state();

	if (dpa_state != NOA_STATE_READY && dpa_state != NOA_STATE_CRASH) {
		goto deinit_unlock;
		ret = -EBUSY;
	}

	google_dpa_ctrl_set_data_path_locked(NOA_DATA_PATH_DIRECT);

	google_dpa_ctrl_set_state_locked(NOA_STATE_UNAVAILABLE);

#ifdef USE_PIXEL_CPM
	// Note DPA is off to deactivate CPM DPA service.
	google_dpa_ctrl_cpm_send_message(CPM_DPA_CMD_NOTIFY_DPA_POWER_OFF, 0);
#endif

	dpa_ctrl.dpa = NULL;

deinit_unlock:
	mutex_unlock(&dpa_ctrl.mutex);
	return ret;
}

struct dpa_client *google_dpa_ctrl_register(const char *client_name,
					    struct dpa_callbacks *callbacks, void *context)
{
	struct dpa_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;

	client->name = kstrdup(client_name, GFP_KERNEL);
	if (!client->name) {
		kfree(client);
		return NULL;
	}

	mutex_lock(&dpa_ctrl.mutex);

	if (!dpa_ctrl.dpa)
		goto fail_unlock;

	get_device(dpa_ctrl.dpa->dev);

	INIT_LIST_HEAD(&client->list);

	client->callbacks.on_state_changed = callbacks->on_state_changed;
	client->callbacks.on_data_path_changed = callbacks->on_data_path_changed;
	client->callbacks.on_pcie_ownership_changed = callbacks->on_pcie_ownership_changed;
	client->context = context;

	list_add_tail(&client->list, &dpa_ctrl.client_list);

	// Tell the client current state.
	INIT_WORK(&client->event_work, notify_state_changed_work);
	schedule_work(&client->event_work);
	flush_work(&client->event_work);

	INIT_WORK(&client->event_work, notify_data_path_changed_work);
	schedule_work(&client->event_work);
	flush_work(&client->event_work);

	INIT_WORK(&client->event_work, notify_pcie_ownership_changed_work);
	schedule_work(&client->event_work);
	flush_work(&client->event_work);

	mutex_unlock(&dpa_ctrl.mutex);

	return client;

fail_unlock:
	mutex_unlock(&dpa_ctrl.mutex);
	kfree(client->name);
	kfree(client);
	return NULL;
}
EXPORT_SYMBOL_GPL(google_dpa_ctrl_register);

int google_dpa_ctrl_unregister(struct dpa_client *client)
{
	mutex_lock(&dpa_ctrl.mutex);
	cancel_work_sync(&client->event_work);
	list_del(&client->list);
	kfree(client->name);
	client->name = NULL;
	kfree(client);
	mutex_unlock(&dpa_ctrl.mutex);

	put_device(dpa_ctrl.dpa->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpa_ctrl_unregister);
