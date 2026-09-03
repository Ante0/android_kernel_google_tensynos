// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Google Inc.
 *
 */

#include "noa_md_wrapper.h"
#include "noa_md_wrapper_t900.h"
#include "noa_md.h"
#include "noa_md_trace.h"
#include "t900/noa_md_mtk_priv.h"

struct noa_md_wpr_mtk_t900_dev *md_wpr_t900_dev = NULL;

const char *noa_md_wpr_t900_cmd_to_str(enum noa_md_wpr_mtk_t900_drv_cmd cmd)
{
	switch (cmd) {
	case NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT:
		return "DPMAIF_SW_INIT";
	case NOA_MD_WPR_T900_CMD_DPMAIF_SW_RESET:
		return "DPMAIF_SW_RESET";
	case NOA_MD_WPR_T900_CMD_DPMAIF_SW_EXIT:
		return "DPMAIF_SW_EXIT";
	case NOA_MD_WPR_T900_CMD_DPMAIF_START:
		return "DPMAIF_START";
	case NOA_MD_WPR_T900_CMD_DPMAIF_STOP:
		return "DPMAIF_STOP";
	case NOA_MD_WPR_T900_CMD_DPMAIF_STATUS_SYNC:
		return "DPMAIF_STATUS_SYNC";
	case NOA_MD_WPR_T900_CMD_CLDMA_INIT:
		return "CLDMA_INIT";
	case NOA_MD_WPR_T900_CMD_CLDMA_EXIT:
		return "CLDMA_EXIT";
	case NOA_MD_WPR_T900_CMD_CLDMA_DEV_INIT:
		return "CLDMA_DEV_INIT";
	case NOA_MD_WPR_T900_CMD_CLDMA_DEV_EXIT:
		return "CLDMA_DEV_EXIT";
	case NOA_MD_WPR_T900_CMD_CLDMA_OPEN:
		return "CLDMA_OPEN";
	case NOA_MD_WPR_T900_CMD_CLDMA_CLOSE:
		return "CLDMA_CLOSE";
	case NOA_MD_WPR_T900_CMD_WWAN_INIT:
		return "WWAN_INIT";
	case NOA_MD_WPR_T900_CMD_WWAN_SETUP:
		return "WWAN_SETUP";
	case NOA_MD_WPR_T900_CMD_WWAN_OPEN:
		return "WWAN_OPEN";
	case NOA_MD_WPR_T900_CMD_WWAN_STOP:
		return "WWAN_STOP";
	case NOA_MD_WPR_T900_CMD_WWAN_EXIT:
		return "WWAN_EXIT";
	case NOA_MD_WPR_T900_CMD_NETDEV_UPDATE:
		return "NETDEV_UPDATE";
	case NOA_MD_WPR_T900_CMD_PCIE_PROBE_DONE:
		return "PCIE_PROBE_DONE";
#if IS_ENABLED(CONFIG_DEBUG_FS)
	case NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT_SCRIPT:
		return "DPMAIF_SW_INIT_SCRIPT";
#endif
	default:
		return "UNKNOWN_CMD";
	}
}
EXPORT_SYMBOL_GPL(noa_md_wpr_t900_cmd_to_str);

struct mtk_pm_entity noa_t900_pm_entity = {
#if IS_ENABLED(CONFIG_ENABLE_T900_NOA_SUPPORT)
	.user = MTK_USER_NOA,  // User ID for NOA
#endif
	.param = NULL,		 // Parameter, temporarily set to NULL
	.suspend = noa_md_wpr_t900_suspend,
	.suspend_late = noa_md_wpr_t900_suspend_late,
	.resume_early = noa_md_wpr_t900_resume_early,
	.resume = noa_md_wpr_t900_resume,
};

// T900 initialization function
static int noa_md_wpr_t900_init(void *data)
{
	// Update initialization work of mtk_t900 modem
	NOA_MD_WPR_INFO("initialized");
	return 0;
}

// T900 exit function
static int noa_md_wpr_t900_exit(void *data)
{
	// Update exit work of mtk_t900 modem
	NOA_MD_WPR_INFO("exit");
	return 0;
}

// Update network device information
static int noa_md_wpr_t900_update_netdev(struct net_device_info *info)
{
	// Implement the logic of network device information transmission
	NOA_MD_WPR_INFO("net device updated");
	return 0;
}

// Start queue
static int noa_md_wpr_t900_start_queue(enum netdev_drv_dir dir)
{
	// Implement logic here
	NOA_MD_WPR_INFO("queue started, dir: %d", dir);
	return 0;
}

// Stop queue
static int noa_md_wpr_t900_stop_queue(enum netdev_drv_dir dir)
{
	// Implement logic here
	NOA_MD_WPR_INFO("queue stopped, dir: %d", dir);
	return 0;
}

// Transmit TX data
static int noa_md_wpr_t900_tx_data(void *data_blk, struct sk_buff *skb, u64 data)
{
	int ret = 0;
	// Implement logic here
	NOA_MD_WPR_DATA_LIMIT(
		"data transmitted, data_blk=[0x%pK]", data_blk);
	// Call NOA modem TX func
	ret = noa_md_tx_wwan_data(data_blk, skb);
	return ret;
}

// Receive RX data
static int noa_md_wpr_t900_rx_data(void *data)
{
	// Implement logic here
	NOA_MD_WPR_DATA_LIMIT("data received");
	return 0;
}

// Handle data interrupts for the MediaTek T900 modem.
int noa_md_wpr_t900_data_irq_handle(void *data)
{
	// NOA md data rx step 3: Create work_data structure to store interrupt
	// information
	struct noa_md_wpr_dpmaif_event_work_data *work_data;
	struct noa_md_wpr_irq_data *info = (struct noa_md_wpr_irq_data*)data;

	// Check for valid input data
	if (!info || !info->intr_info) {
		NOA_MD_WPR_ERROR("Invalid interrupt info pointer");
		return -EINVAL;
	}

	struct dpmaif_drv_intr_info *intr_info =
		(struct dpmaif_drv_intr_info*)info->intr_info;
	enum dpmaif_drv_intr_type intr_type = intr_info->intr_types[info->index];
	unsigned int q_mask = intr_info->intr_queues[info->index];

	// NOA md data rx step 4: Check whether the interrupt type is supported,
	// if not, return an error
	if (intr_type >= DPMAIF_INTR_UL_MIN &&
		intr_type <= DPMAIF_INTR_UL_MAX) {
		// TODO: Add check for TX enable
	} else if (intr_type >= DPMAIF_INTR_DL_MIN &&
		intr_type <= DPMAIF_INTR_DL_MAX) {
		// TODO: Add check for RX enable
	} else {
		NOA_MD_WPR_ERROR("Unsupported type=[%d]", intr_type);
		return -EOPNOTSUPP;
	}

	// NOA md data rx step 5: Configure work_data structure
	work_data = kmalloc(
		sizeof(struct noa_md_wpr_dpmaif_event_work_data), GFP_ATOMIC);

	if (!work_data) {
		NOA_MD_WPR_ERROR("Failed to allocate memory for work data");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&work_data->work, noa_md_wpr_t900_event_work);
	work_data->intr_type = intr_type;
	work_data->q_mask = q_mask;

	if (intr_type >= DPMAIF_INTR_UL_MIN && intr_type <= DPMAIF_INTR_UL_MAX) {
		// TX
		queue_delayed_work(
			md_wpr_dev->noa_md_tx_event_workqueue, &work_data->work, 0);
	} else if (
		intr_type >= DPMAIF_INTR_DL_MIN && intr_type <= DPMAIF_INTR_DL_MAX) {
		// RX
		// NOA md data rx step 6: Queue the work into the RX event workqueue,
		// which will be processed later by
		// noa_md_wpr_t900_event_work
		queue_delayed_work(
			md_wpr_dev->noa_md_rx_event_workqueue, &work_data->work, 0);
	}

	return 0;
}

// Work function to handle DPMAIF events
void noa_md_wpr_t900_event_work(struct work_struct *work)
{
	if (!md_wpr_dev) {
		NOA_MD_WPR_ERROR("md_wpr_dev is null");
	}
	// NOA md data rx step 7: Get the interrupt information from the
	// work structure
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct noa_md_wpr_dpmaif_event_work_data *work_data =
		container_of(
			delayed_work, struct noa_md_wpr_dpmaif_event_work_data, work);
		NOA_MD_WPR_DATA_LIMIT(
			"intr_type=[%d], q_mask=[%d]",
			(int)work_data->intr_type, work_data->q_mask);
	// NOA md data rx step 8: Call noa_md_data_irq_handle to handle the
	// interrupt
	if(md_wpr_dev->ops->modem_event_irq_handle_func) {
		struct dpmaif_irq_data irq_data = {
			.intr_type = work_data->intr_type,
			.q_mask = work_data->q_mask
		};
		int ret = md_wpr_dev->ops->modem_event_irq_handle_func(
			(void *)&irq_data);
		if (ret != 0) {
			NOA_MD_WPR_ERROR("ret=[%d]", ret);
		}
	}
	kfree(work_data);
}

/**
 * __noa_md_wpr_t900_feature_cmd() - Core implementation for feature commands.
 * @cmd:  The command identifier.
 * @data: The data associated with the command.
 *
 * This function contains the actual logic for handling commands. It is called
 * by the public wrapper function and can be called directly from debugfs
 * to bypass the command gate.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int __noa_md_wpr_t900_feature_cmd(enum noa_md_wpr_mtk_t900_drv_cmd cmd,
	void *data)
{
	CHECK_PTR_OR_RETURN_ERR(data, -EINVAL);

	if (unlikely(cmd < 0 || cmd >= NOA_MD_WPR_T900_CMD_MAX)) {
		NOA_MD_WPR_ERROR("Invalid command ID: %d", cmd);
		return -EINVAL;
	}

	NOA_MD_WPR_DEBUG(
		"feature command %d executed, data=[0x%pK]", cmd, data);

	switch (cmd) {  // The existing switch-case logic goes here
		case NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT:
			if (md_wpr_t900_dev && md_wpr_t900_dev->pm_entity_register) {
				struct mtk_dpmaif_ctlb *dcb = (struct mtk_dpmaif_ctlb *)data;
				NOA_MD_WPR_INFO("pm_entity_register");
				md_wpr_t900_dev->pm_entity_register(DCB_TO_MDEV(dcb), &noa_t900_pm_entity);
			} else {
				NOA_MD_WPR_ERROR("md_wpr_t900_dev is null");
			}

			noa_md_dpmaif_sw_init(data);
			break;
		case NOA_MD_WPR_T900_CMD_DPMAIF_SW_RESET:
			noa_md_dpmaif_sw_reset(data);
			break;
		case NOA_MD_WPR_T900_CMD_DPMAIF_SW_EXIT:
			if (md_wpr_t900_dev && md_wpr_t900_dev->pm_entity_unregister) {
				struct mtk_dpmaif_ctlb *dcb = (struct mtk_dpmaif_ctlb *)data;
				NOA_MD_WPR_INFO("pm_entity_unregister");
				md_wpr_t900_dev->pm_entity_unregister(DCB_TO_MDEV(dcb), &noa_t900_pm_entity);
			} else {
				NOA_MD_WPR_ERROR("md_wpr_t900_dev is null");
			}

			noa_md_dpmaif_sw_exit(data);
			break;
		case NOA_MD_WPR_T900_CMD_DPMAIF_START:
			noa_md_dpmaif_start(data);
			break;
		case NOA_MD_WPR_T900_CMD_DPMAIF_STOP:
			noa_md_dpmaif_stop(data);
			break;
		case NOA_MD_WPR_T900_CMD_DPMAIF_STATUS_SYNC:
			noa_md_dpmaif_stats_sync(data);
			break;
		case NOA_MD_WPR_T900_CMD_CLDMA_INIT:
			noa_md_cldma_init(data);
			break;
		case NOA_MD_WPR_T900_CMD_CLDMA_EXIT:
			noa_md_cldma_exit(data);
			break;
		case NOA_MD_WPR_T900_CMD_CLDMA_DEV_INIT:
			noa_md_cldma_dev_init(data);
			break;
		case NOA_MD_WPR_T900_CMD_CLDMA_DEV_EXIT:
			noa_md_cldma_dev_exit(data);
			break;
		case NOA_MD_WPR_T900_CMD_CLDMA_OPEN:
			noa_md_cldma_open(data);
			break;
		case NOA_MD_WPR_T900_CMD_CLDMA_CLOSE:
			noa_md_cldma_close(data);
			break;
		case NOA_MD_WPR_T900_CMD_WWAN_INIT:
			noa_md_wwan_init(data);
			break;
		case NOA_MD_WPR_T900_CMD_WWAN_SETUP:
			noa_md_wwan_setup(data);
			break;
		case NOA_MD_WPR_T900_CMD_WWAN_OPEN:
			noa_md_wwan_open(data);
			break;
		case NOA_MD_WPR_T900_CMD_WWAN_STOP:
			noa_md_wwan_stop(data);
			break;
		case NOA_MD_WPR_T900_CMD_WWAN_EXIT:
			noa_md_wwan_exit(data);
			break;
		case NOA_MD_WPR_T900_CMD_NETDEV_UPDATE:
			noa_md_netdev_update(data);
			break;
		case NOA_MD_WPR_T900_CMD_PCIE_PROBE_DONE:
			noa_md_pcie_probe_done(data);
			break;
#if IS_ENABLED(CONFIG_DEBUG_FS)
		case NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT_SCRIPT:
			if (md_wpr_t900_dev && md_wpr_t900_dev->pm_entity_register) {
				struct mtk_dpmaif_ctlb *dcb = (struct mtk_dpmaif_ctlb *)data;
				NOA_MD_WPR_INFO("pm_entity_register");
				md_wpr_t900_dev->pm_entity_register(DCB_TO_MDEV(dcb), &noa_t900_pm_entity);
			} else {
				NOA_MD_WPR_ERROR("md_wpr_t900_dev is null");
			}

			noa_md_dpmaif_sw_init_script(data);
			break;
#endif
		default:
			NOA_MD_WPR_ERROR("Unsupported feature command %d", cmd);
			return -EOPNOTSUPP;
	}
	return 0;
}

/**
 * noa_md_wpr_t900_feature_cmd() - Public wrapper for feature commands.
 * @cmd:  The command identifier.
 * @data: The data associated with the command.
 *
 * This function acts as a gatekeeper. It checks if a command is disabled
 * for normal driver calls. If the command is gated, it can only be
 * triggered via the debugfs interface.
 *
 * Return: 0 on success, -EPERM if gated, or other negative error codes.
 */
int noa_md_wpr_t900_feature_cmd(int cmd, void *data)
{
	if (md_wpr_t900_dev && cmd < NOA_MD_WPR_T900_CMD_MAX &&
		!md_wpr_t900_dev->drv_cmd_enabled[cmd]) {
		NOA_MD_WPR_ERROR("Command %d is disabled for driver calls.", cmd);
		return -EPERM;
	}
	return __noa_md_wpr_t900_feature_cmd(
		(enum noa_md_wpr_mtk_t900_drv_cmd)cmd, data);
}

int noa_md_wpr_t900_wwan_notify(void *evt_dat)
{
	if (!evt_dat) {
		return -EINVAL;
	}
	struct noa_md_wpr_mtk_t900_evt_data *t900_evt_data = evt_dat;
	int evt = t900_evt_data->evt;
	// TODO: Implement wwan_notify event handle
	switch (evt) {
	case DATA_EVT_TX_START:
		NOA_MD_WPR_DATA_LIMIT(
			"DATA_EVT_TX_START, DATA_EVENT=%d", evt);
		break;
	case DATA_EVT_TX_STOP:
		NOA_MD_WPR_DATA_LIMIT(
			"DATA_EVT_TX_STOP, DATA_EVENT=%d", evt);
		break;
	case DATA_EVT_RX_START:
		NOA_MD_WPR_DATA_LIMIT(
			"DATA_EVT_RX_START, DATA_EVENT=%d", evt);
		break;
	case DATA_EVT_RX_STOP:
		NOA_MD_WPR_DATA_LIMIT(
			"DATA_EVT_RX_STOP, DATA_EVENT=%d", evt);
		break;
#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
	case DATA_EVT_RX_FLUSH:
		NOA_MD_WPR_DATA_LIMIT(
			"DATA_EVT_RX_FLUSH, DATA_EVENT=%d", evt);
		break;
#endif
	case DATA_EVT_REG_DEV:
		NOA_MD_WPR_DATA_LIMIT(
			"DATA_EVT_REG_DEV, DATA_EVENT=%d", evt);
		break;
	case DATA_EVT_UNREG_DEV:
		NOA_MD_WPR_DATA_LIMIT(
			"DATA_EVT_UNREG_DEV, DATA_EVENT=%d", evt);
		break;
	case DATA_EVT_DUMP:
		NOA_MD_WPR_DATA_LIMIT(
			"DATA_EVT_DUMP, DATA_EVENT=%d", evt);
		break;
	default:
		NOA_MD_WPR_ERROR(
			"Invalid parameter, DATA_EVENT=%d", evt);
		break;
	}

	noa_md_wwan_data_event(evt_dat);

	return 0;
}

int noa_md_wpr_t900_suspend(
		struct mtk_md_dev *mdev, void *data, bool is_runtime)
{
	ENSURE_NOA_MD_WPR_PM_READY(ENOMEM);

	struct noa_md_wpr_mtk_t900_pm_data pm_dat = {
		.mdev = mdev,
		.data = data,
		.is_runtime = is_runtime
	};
	md_wpr_dev->pm_ops->suspend(&pm_dat);
	return 0;
}

int noa_md_wpr_t900_suspend_late(
		struct mtk_md_dev *mdev, void *data, bool is_runtime)
{
	ENSURE_NOA_MD_WPR_PM_READY(ENOMEM);

	struct noa_md_wpr_mtk_t900_pm_data pm_dat = {
		.mdev = mdev,
		.data = data,
		.is_runtime = is_runtime
	};
	md_wpr_dev->pm_ops->suspend_late(&pm_dat);
	return 0;
}

int noa_md_wpr_t900_resume_early(
		struct mtk_md_dev *mdev, void *data, bool is_runtime, bool link_ready)
{
	ENSURE_NOA_MD_WPR_PM_READY(ENOMEM);

	struct noa_md_wpr_mtk_t900_pm_data pm_dat = {
		.mdev = mdev,
		.data = data,
		.is_runtime = is_runtime,
		.link_ready = link_ready
	};
	md_wpr_dev->pm_ops->resume_early(&pm_dat);
	return 0;
}

int noa_md_wpr_t900_resume(
		struct mtk_md_dev *mdev, void *data, bool is_runtime, bool link_ready)
{
	ENSURE_NOA_MD_WPR_PM_READY(ENOMEM);

	struct noa_md_wpr_mtk_t900_pm_data pm_dat = {
		.mdev = mdev,
		.data = data,
		.is_runtime = is_runtime,
		.link_ready = link_ready
	};
	md_wpr_dev->pm_ops->resume(&pm_dat);
	return 0;
}

int noa_md_wpr_t900_set_pm_entity_register(int (*pm_entity_register)(
		struct mtk_md_dev *mdev, struct mtk_pm_entity *md_entity))
{
	if (!md_wpr_t900_dev) {
		NOA_MD_WPR_ERROR("md_wpr_t900_dev is null");
		return -ENOMEM;
	}
	md_wpr_t900_dev->pm_entity_register = pm_entity_register;
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_t900_set_pm_entity_register);

int noa_md_wpr_t900_set_pm_entity_unregister(int (*pm_entity_unregister)(
		struct mtk_md_dev *mdev, struct mtk_pm_entity *md_entity))
{
	if (!md_wpr_t900_dev) {
		NOA_MD_WPR_ERROR("md_wpr_t900_dev is null");
		return -ENOMEM;
	}
	md_wpr_t900_dev->pm_entity_unregister = pm_entity_unregister;
	return 0;
}
EXPORT_SYMBOL(noa_md_wpr_t900_set_pm_entity_unregister);

// Initialize the driver operations structure for the T900 modem
struct noa_md_drv_ops noa_md_drv_ops_t900 =
{
	.init = noa_md_wpr_t900_init,
	.exit = noa_md_wpr_t900_exit,
	.update_netdev = noa_md_wpr_t900_update_netdev,
	.start_queue = noa_md_wpr_t900_start_queue,
	.stop_queue = noa_md_wpr_t900_stop_queue,
	.tx_data = noa_md_wpr_t900_tx_data,
	.rx_data = noa_md_wpr_t900_rx_data,
	.data_irq_handle = noa_md_wpr_t900_data_irq_handle,
	.feature_cmd = noa_md_wpr_t900_feature_cmd,
	.wwan_notify = noa_md_wpr_t900_wwan_notify
};

struct noa_md_drv_ops* noa_md_wpr_t900_get_drv_ops(void)
{
	return &noa_md_drv_ops_t900;
}

int noa_md_wpr_mtk_t900_init(void)
{
	struct noa_md_wpr_mtk_t900_dev *dev = NULL;
	dev = kzalloc(sizeof(struct noa_md_wpr_mtk_t900_dev), GFP_KERNEL);
	if (!dev) {
		NOA_MD_WPR_ERROR("dev is null");
		return -ENOMEM;
	}
	md_wpr_t900_dev = dev;

	memset(md_wpr_t900_dev->drv_cmd_enabled, true,
		sizeof(md_wpr_t900_dev->drv_cmd_enabled));

	return 0;
}

void noa_md_wpr_mtk_t900_exit(void)
{
	if (md_wpr_t900_dev) {
		kfree(md_wpr_t900_dev);
	}
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * noa_md_wpr_t900_set_cmd_enabled() - Sets the gate for a specific command.
 * @cmd_id:   The command ID to configure.
 * @enabled: True to enable the command for driver calls,
 * false to disable it.
 *
 * This function is for debugfs use only.
 *
 * Return: 0 on success, or -EINVAL for an invalid command ID.
 */
int noa_md_wpr_t900_set_cmd_enabled(int cmd_id, bool enabled)
{
	if (!md_wpr_t900_dev) {
		NOA_MD_WPR_ERROR("dev is null");
		return -ENODEV;
	}

	if (cmd_id < 0 || cmd_id >= NOA_MD_WPR_T900_CMD_MAX) {
		NOA_MD_WPR_ERROR("invalid command ID");
		return -EINVAL;
	}

	md_wpr_t900_dev->drv_cmd_enabled[cmd_id] = enabled;
	return 0;
}

/**
 * noa_md_wpr_t900_is_cmd_enabled() - Gets the gate status for a command.
 * @cmd_id: The command ID to query.
 *
 * Return: 1 if the gate is closed (disabled), 0 if open (enabled),
 * or a negative error code on failure.
 */
int noa_md_wpr_t900_is_cmd_enabled(int cmd_id)
{
	if (!md_wpr_t900_dev) {
		NOA_MD_WPR_ERROR("dev is null");
		return -ENODEV;
	}

	if (cmd_id < 0 || cmd_id >= NOA_MD_WPR_T900_CMD_MAX) {
		NOA_MD_WPR_ERROR("invalid command ID");
		return -EINVAL;
	}

	return md_wpr_t900_dev->drv_cmd_enabled[cmd_id];
}

#endif /* CONFIG_DEBUG_FS */
