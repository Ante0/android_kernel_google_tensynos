// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2025 Google LLC.
 *
 */
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <soc/google/google_dpa.h>
#include <soc/google/google_dpa_crash_dump.h>
#include <soc/google/google_dpa_doorbell.h>
#include <soc/google/google_dpa_ctrl.h>
#include <soc/google/google_dpa_ring_service_proxy.h>
#include <common/ring_id.h>
#include <common/noa_ring_id.h>
#include <common/wlan_ring_id.h>
#include "noa_wlan_client.h"
#include "wlan_debug_controller/wlan_debug_controller_client.h"
#include "noa_wlan_hw.h"
#include "google_plat.h"
#include "noa_wlan_dynamic_switch.h"
#include "ring_service/ring_mgmt/ring_manager.h"
#include "wlan_rpc_service/noa_wlan_cmd_dispatch.h"

#define NCP_DOORBELL_NAME "ncp_doorbell_1"
#define NEP_DOORBELL_NAME "nep_doorbell_0"

static struct noa_wlan_manager {
	struct device *dev;
	struct google_dpa *dpa;
	struct dpa_client *registered_dpa_client;
	struct noa_wlan_client *client;
	struct google_dpa_doorbell *ncp_doorbell;
	struct google_dpa_doorbell *nep_doorbell;
	struct noa_wlan_switch_manager *switch_manager;
	struct device *ncp_doorbell_dev;
	struct device *nep_doorbell_dev;
	enum dpa_state dpa_state;
	struct google_dpa_mem_dump shared_info_dump_data;
} wlan_mgmt;

static void noa_wlan_fw_assert_isr(void *context) __attribute__((used));

static struct noa_wlan_manager *get_wlan_manager(void)
{
	return &wlan_mgmt;
}

static void noa_wlan_isr_doorbell(void *data)
{
	struct noa_wlan_client *client = (struct noa_wlan_client *)data;

	client->nep_apc_intr_cnt += 1;
	/* clear interrupt status first */
	if (client->ops && client->ops->rx_isr)
		client->ops->rx_isr(0);
}

static void noa_wlan_event_isr_doorbell(void *data)
{
	(void)data;
	// TODO(b/421036310): Use doorbell to receive WLAN event from NCP WiFi.
}

static void noa_wlan_packet_sniffer_full_isr(void *data)
{
	struct noa_wlan_client *client = (struct noa_wlan_client *)data;
	struct noa_wlan_entry *dbg_entry = (struct noa_wlan_entry *)(client->dbg_entry);
	struct wlan_debug_controller_client *dbg_client;
	struct wlan_dbg_entry *dbg_comp_entry;
	struct wlan_debug_component_client *dbg_comp_client;

	if (dbg_entry) {
		dbg_client = (struct wlan_debug_controller_client *)(dbg_entry->priv);
	} else {
		return;
	}

	dbg_comp_entry = dbg_client->component_entries[DBG_PACKET_SNIFFER];
	dbg_comp_client = (struct wlan_debug_component_client *)(dbg_comp_entry->priv);

	if (dbg_client && dbg_client->ops[DBG_PACKET_SNIFFER]->notify_driver) {
		dbg_client->ops[DBG_PACKET_SNIFFER]->notify_driver(dbg_comp_client, NULL);
	}
}

static int noa_wlan_get_doorbell(struct noa_wlan_client *client)
{
	struct device *dpa_dev;
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();

	if (!client)
		return -EINVAL;

	dpa_dev = client->dpa_dev;
	wlan_manager->ncp_doorbell = google_dpa_get_doorbell(dpa_dev, NCP_DOORBELL_NAME);
	if (!wlan_manager->ncp_doorbell) {
		dev_err(client->dev, "%s(): failed to get ncp doorbell\n", __func__);
		return -EINVAL;
	}
	wlan_manager->ncp_doorbell_dev = google_dpa_get_doorbell_dev(wlan_manager->ncp_doorbell);

	wlan_manager->nep_doorbell = google_dpa_get_doorbell(dpa_dev, NEP_DOORBELL_NAME);
	if (!wlan_manager->nep_doorbell) {
		dev_err(client->dev, "%s(): failed to get nep doorbell\n", __func__);
		return -EINVAL;
	}
	wlan_manager->nep_doorbell_dev = google_dpa_get_doorbell_dev(wlan_manager->nep_doorbell);
	return 0;
}

int noa_wlan_hw_set(struct noa_wlan_client *client)
{
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();
	struct device *dev = google_dpa_get_dpa_dev(wlan_manager->dpa);

	if (!dev) {
		dev_err(client->dev, "%s(): get dpa_dev fail!\n", __func__);
		return -EINVAL;
	}
	client->dpa_dev = dev;
	noa_wlan_get_doorbell(client);

	wlan_manager->client = client;

	pr_info("%s(): enable NEP doorbell", __func__);

	//TODO: (b/435140335)DPA doorbell runtime put and get should follow the WiFi runtime PM.
	pm_runtime_get_sync(wlan_manager->nep_doorbell_dev);
	pm_runtime_get_sync(wlan_manager->ncp_doorbell_dev);
	// Register doorbell for receiving WLAN events
	google_dpa_doorbell_enable_doorbell(wlan_manager->ncp_doorbell, DOORBELL_RX_EVENT,
					    noa_wlan_event_isr_doorbell, client);
	google_dpa_doorbell_enable_doorbell(wlan_manager->ncp_doorbell, DOORBELL_FW_TRAP_EVENT,
					    noa_wlan_fw_assert_isr, client);
	google_dpa_doorbell_enable_doorbell(wlan_manager->ncp_doorbell,
					    DOORBELL_PACKET_SNIFFER_FULL_EVENT,
					    noa_wlan_packet_sniffer_full_isr, client);

	// Register doorbell for WLAN control path & data path
	return google_dpa_doorbell_enable_doorbell(wlan_manager->nep_doorbell, 0,
						   noa_wlan_isr_doorbell, client);
}

void noa_wlan_hw_reset(struct noa_wlan_client *client)
{
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();

	google_dpa_doorbell_disable_doorbell(wlan_manager->nep_doorbell, 0);
	google_dpa_doorbell_disable_doorbell(wlan_manager->ncp_doorbell, 0);
	//TODO: (b/435140335)DPA doorbell runtime put and get should follow the WiFi runtime PM.
	pm_runtime_put(wlan_manager->nep_doorbell_dev);
	pm_runtime_put(wlan_manager->ncp_doorbell_dev);
	wlan_manager->nep_doorbell = NULL;
	wlan_manager->ncp_doorbell = NULL;
	client->dpa_dev = NULL;
	wlan_manager->client = NULL;
}

void noa_wlan_hw_nep_rings_input_activate(bool activate)
{
	u32 ring_id = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
					   kNoaWlanRingTxData);
	if (activate) {
		google_dpa_ring_service_rpc_event_activate(ring_id, kNoaRingNepInput);
	} else {
		google_dpa_ring_service_rpc_event_deactivate(ring_id, kNoaRingNepInput);
	}
}

void noa_wlan_hw_nep_rings_output_activate(bool activate)
{
	u32 ring_id = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost,
					   kNoaWlanRingRxData);
	if (activate) {
		google_dpa_ring_service_rpc_event_activate(ring_id, kNoaRingNepOutput);
	} else {
		google_dpa_ring_service_rpc_event_deactivate(ring_id, kNoaRingNepOutput);
	}
}

int noa_wlan_hw_nep_ring_reg_get(uint8_t interface, uint8_t flow, uint8_t category,
				 uint8_t direction, struct noa_ring_regs *regs)
{
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();

	int ret = NoaDpaRingSharedRegsGet(wlan_manager->dpa, regs, interface, flow, category,
					  direction);
	if (ret) {
		dev_err(wlan_manager->dev, "%s(): NoaDpaRingSharedRegsGet fail\n", __func__);
		return -EINVAL;
	}

	return 0;
}

int noa_wlan_hw_nep_ring_reg_query(struct noa_wlan_client *client, int type,
				   struct noa_ring_regs *regs)
{
	switch (type) {
	case NOA_RING_TYPE_CONSUMER:
		noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost,
					     kNoaWlanRingRxData, kNoaRingNepOutput, regs);
		break;
	case NOA_RING_TYPE_PRODUCER:
		noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
					     kNoaWlanRingTxData, kNoaRingNepInput, regs);
		break;
	default:
		dev_err(client->dev, "%s(): Unsupported ring type: %d\n", __func__, type);
		return -EINVAL;
	}

	return 0;
}

int noa_wlan_hw_ringbell_nep(struct noa_wlan_client *client)
{
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();

	// Call mcu_atomic_safe since this function is using in TX tasklet.
	google_dpa_doorbell_ring_mcu_atomic_safe(wlan_manager->nep_doorbell, 0);
	return 0;
}

int noa_wlan_hw_ringbell_ncp(struct noa_wlan_client *client, enum APC2NCP_DOORBELL_NUM doorbell_num)
{
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();

	if (doorbell_num >= APC2NCP_DOORBELL_NUM_START && doorbell_num < APC2NCP_DOORBELL_NUM_END)
		google_dpa_doorbell_ring_mcu_atomic_safe(wlan_manager->ncp_doorbell, doorbell_num);

	return 0;
}

static const char *get_dpa_action_name(enum dpa_action action)
{
#define DPA_ACTION_NAME(action)                                                                    \
	case action:                                                                               \
		return #action

	switch (action) {
		DPA_ACTION_NAME(NOA_ACTION_SERVICE_PRE_SWITCH);
		DPA_ACTION_NAME(NOA_ACTION_DEVICE_PRE_SWITCH);
		DPA_ACTION_NAME(NOA_ACTION_DEVICE_POST_SWITCH);
		DPA_ACTION_NAME(NOA_ACTION_SERVICE_POST_SWITCH);
	default:
		return "Unknown DPA Action";
	}
}

static const char *get_dpa_data_path_name(enum dpa_data_path data_path)
{
#define DPA_DATA_PATH_NAME(data_path)                                                              \
	case data_path:                                                                            \
		return #data_path

	switch (data_path) {
		DPA_DATA_PATH_NAME(NOA_DATA_PATH_DIRECT);
		DPA_DATA_PATH_NAME(NOA_DATA_PATH_OFFLOAD);
	default:
		return "Unknown DPA Data Path";
	}
}

static void noa_wlan_on_data_path_change(enum dpa_data_path desired_data_path,
					 enum dpa_action action, void *context)
{
	struct noa_wlan_manager *wlan_manager = (struct noa_wlan_manager *)context;
	struct noa_wlan_switch_manager *manager = wlan_manager->switch_manager;
	struct device *noa_wlan_dev = wlan_manager->dev;
	enum noa_wlan_data_path_mode current_dp_mode = get_noa_wlan_dp_mode(manager);

	dev_info(noa_wlan_dev, "Received data path event: data path: %s(%d), action: %s(%d)\n",
		 get_dpa_data_path_name(desired_data_path), desired_data_path,
		 get_dpa_action_name(action), action);

	if ((current_dp_mode == NOA_WLAN_DATA_PATH_BYPASS_MODE &&
	     desired_data_path == NOA_DATA_PATH_DIRECT) ||
	    (current_dp_mode == NOA_WLAN_DATA_PATH_OFFLOAD_MODE &&
	     desired_data_path == NOA_DATA_PATH_OFFLOAD)) {
		// The desired WLAN data path is already configured.
		dev_info(noa_wlan_dev, "Ignoring event, data path is already set to %s",
			 get_dpa_data_path_name(desired_data_path));
		return;
	}

	switch (action) {
	case NOA_ACTION_SERVICE_PRE_SWITCH:
		noa_wlan_dynamic_switch_event_handler(manager, NOA_WLAN_SERVICE_PRE_SWITCH);
		break;
	case NOA_ACTION_DEVICE_PRE_SWITCH:
		noa_wlan_dynamic_switch_event_handler(manager, NOA_WLAN_DEVICE_PRE_SWITCH);
		break;
	case NOA_ACTION_DEVICE_POST_SWITCH:
		noa_wlan_dynamic_switch_event_handler(manager, NOA_WLAN_DEVICE_POST_SWITCH);
		break;
	case NOA_ACTION_SERVICE_POST_SWITCH:
		noa_wlan_dynamic_switch_event_handler(manager, NOA_WLAN_SERVICE_POST_SWITCH);
		break;
	default:
		break;
	}
}

static void noa_wlan_fw_assert_isr(void *context)
{
	struct noa_wlan_manager *wlan_manager = (struct noa_wlan_manager *)context;
	struct noa_wlan_switch_manager *manager = wlan_manager->switch_manager;

	printk("%s(): %d, manager %p, wlan_mgmt %p\n", __func__, __LINE__, manager, wlan_manager);

	pr_info("%s(): NOA WiFi FW assert, DPA pre-reset start.", __func__);
	noa_wlan_pre_reset_dpa(manager);
	pr_info("%s(): NOA WiFi FW assert, DPA post-reset start.", __func__);
	// By default, the post reset will turn off the OFFLOAD mode and change to BYPASS mode.
	noa_wlan_post_reset_dpa(manager);
}

static const char *get_dpa_state_name(enum dpa_state state)
{
#define DPA_STATE_NAME(state)                                                                      \
	case state:                                                                                \
		return #state

	switch (state) {
		DPA_STATE_NAME(NOA_STATE_UNAVAILABLE);
		DPA_STATE_NAME(NOA_STATE_READY);
		DPA_STATE_NAME(NOA_STATE_CRASH);
		DPA_STATE_NAME(NOA_STATE_COUNT);
	default:
		return "Unknown DPA State";
	}
}

static void noa_wlan_on_state_change(enum dpa_state state, void *context)
{
	struct noa_wlan_manager *wlan_manager = (struct noa_wlan_manager *)context;
	struct noa_wlan_switch_manager *manager = wlan_manager->switch_manager;
	enum dpa_state last_state = wlan_manager->dpa_state;
	struct device *noa_wlan_dev = wlan_manager->dev;

	if (last_state != state) {
		dev_info(noa_wlan_dev, "DPA state changed: %s(%d) -> %s(%d)\n",
			 get_dpa_state_name(last_state), last_state, get_dpa_state_name(state),
			 state);
	} else {
		dev_info(noa_wlan_dev, "Ignoring redundant DPA state notification: %s(%d)\n",
			 get_dpa_state_name(state), state);
	}

	if (state <= NOA_STATE_UNAVAILABLE || state >= NOA_STATE_COUNT)
		return;

	switch (state) {
	case NOA_STATE_READY:
		if (last_state == NOA_STATE_CRASH) {
			wlan_manager->dpa_state = state;
			pr_info("%s(): state crash post reset start", __func__);
			noa_wlan_post_reset_dpa(manager);
		}
		break;
	case NOA_STATE_CRASH:
		if (last_state != NOA_STATE_CRASH) {
			wlan_manager->dpa_state = state;
			pr_info("%s(): state crash pre reset start", __func__);
			noa_wlan_pre_reset_dpa(manager);
		}
		break;
	default:
		return;
	}

	return;
}

static void noa_wlan_pcie_ownership_pre_switch(struct noa_wlan_client *client,
					       enum dpa_pcie_ownership owner)
{
	int ret = 0;
	// Update ownership to sMem
	if (!noa_wlan_cfg_update_pci_ownership(client, owner))
		return;

	if (owner == NOA_PCIE_OWNERSHIP_DPA) {
		// Save PCIe related state
		noa_wlan_client_pci_dev_state_sync(client, true);
		// TODO - b/443189086: D3/DS handshake & control ring preparation
		ret = noa_wlan_fw_request_doorbell_sync(DOORBELL_PCIE_OWNERSHIP_SWITCH);
		if (ret < 0) {
			pr_err("%s(): Failed to do PCIe ownership switch to DPA, ret=%d\n",
			       __func__, ret);
		}
	} else if (owner == NOA_PCIE_OWNERSHIP_APC) {
		ret = noa_wlan_fw_request_doorbell_sync(DOORBELL_PCIE_OWNERSHIP_SWITCH);
		if (ret < 0) {
			pr_err("%s(): Failed to do PCIe ownership switch to APC, ret=%d\n",
			       __func__, ret);
		} else {
			// TODO - b/443189086: D3/DS handshake & control ring preparation
			// Restore PCIe related state
			noa_wlan_client_pci_dev_state_sync(client, false);
			// TODO - b/439968187: Use PCIe exposed API to get link status
		}
	}

	return;
}

static void noa_wlan_pcie_ownership_post_switch(struct noa_wlan_client *client)
{
	// Nothing to do for now.
}

static void noa_wlan_on_pcie_ownership_changed(enum dpa_pcie_ownership owner,
					       enum dpa_action action, void *context)
{
	struct noa_wlan_manager *wlan_manager = (struct noa_wlan_manager *)context;
	struct noa_wlan_client *client = wlan_manager->client;

	switch (action) {
	case NOA_ACTION_PCIE_OWNERSHIP_PRE_SWITCH:
		noa_wlan_pcie_ownership_pre_switch(client, owner);
		break;
	case NOA_ACTION_PCIE_OWNERSHIP_POST_SWITCH:
		noa_wlan_pcie_ownership_post_switch(client);
		break;
	default:
		return;
	}

	return;
}

static struct dpa_callbacks dpa_callbacks = {
	.on_data_path_changed = noa_wlan_on_data_path_change,
	.on_state_changed = noa_wlan_on_state_change,
	.on_pcie_ownership_changed = noa_wlan_on_pcie_ownership_changed,
};

static void noa_wlan_manager_dpa_destroy(void)
{
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();

	if (wlan_manager->registered_dpa_client) {
		google_dpa_ctrl_unregister(wlan_manager->registered_dpa_client);
	}
	if (wlan_manager->dpa) {
		google_dpa_put(wlan_manager->dpa);
	}
}

static int noa_wlan_probe(struct platform_device *pdev)
{
	int ret;
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();
	struct device *dev = &pdev->dev;
	struct google_dpa *dpa;

	platform_set_drvdata(pdev, wlan_manager);

	dpa = google_dpa_get(dev);
	if (IS_ERR(dpa)) {
		ret = PTR_ERR(dpa);
		dev_err_probe(dev, ret, "Failed to get dpa structure.");
		goto out;
	}

	wlan_manager->dev = dev;
	wlan_manager->dpa = dpa;
	wlan_manager->registered_dpa_client =
		google_dpa_ctrl_register("noa_wlan", &dpa_callbacks, (void *)wlan_manager);

	if (!wlan_manager->registered_dpa_client) {
		ret = -ENOMEM;
		dev_err_probe(dev, ret, "Failed to register ring manager client on dpa");
		goto out;
	}

	ret = 0;
out:
	if (ret) {
		noa_wlan_manager_dpa_destroy();
	}
	return ret;
}

static void noa_wlan_remove(struct platform_device *pdev)
{
	noa_wlan_manager_dpa_destroy();
}

static const struct of_device_id noa_wlan_of_match[] = { {
								 .compatible = "google,dpa",
							 },
							 { /* sentinel */ } };
MODULE_DEVICE_TABLE(of, noa_wlan_of_match);

static struct platform_driver noa_wlan_driver = {
    .probe = noa_wlan_probe,
    .remove = noa_wlan_remove,
    .driver = {
        .name = "dpa_wlan",
	.owner = THIS_MODULE,
        .of_match_table = noa_wlan_of_match,
    },
};

int noa_wlan_hw_dynamic_switch_init(void *priv)
{
	struct noa_wlan_switch_manager *manager = priv;
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();

	wlan_manager->switch_manager = manager;
	return platform_driver_register(&noa_wlan_driver);
}

void noa_wlan_hw_dynamic_switch_deinit(void *priv)
{
	struct noa_wlan_switch_manager *manager = priv;
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();

	platform_driver_unregister(&noa_wlan_driver);
	memset(manager, 0, sizeof(struct noa_wlan_switch_manager));
	wlan_manager->switch_manager = NULL;
}

int noa_wlan_hw_crash_dump_register(struct noa_wlan_client *client)
{
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();
	struct noa_wlan_cfg_space *cfg = &client->cfg;

	if (!wlan_manager->dpa) {
		return -EINVAL;
	}

	google_dpa_init_crash_dump_data(&wlan_manager->shared_info_dump_data,
					GOOGLE_DPA_SF_NCP_BIT | GOOGLE_DPA_SF_NEP_BIT, cfg->base_va,
					(u64)cfg->base_pa, (u64)cfg->base_va, cfg->size);
	return google_dpa_register_crash_dump_data(wlan_manager->dpa,
						   &wlan_manager->shared_info_dump_data);
}

void noa_wlan_hw_crash_dump_unregister(void)
{
	struct noa_wlan_manager *wlan_manager = get_wlan_manager();
	if (wlan_manager->dpa) {
		google_dpa_unregister_crash_dump_data(wlan_manager->dpa,
						      &wlan_manager->shared_info_dump_data);
	}
}
