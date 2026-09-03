/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA WWAN Wrapper header file
 *
 * Copyright (c) 2024 Google Inc.
 *
 */

#ifndef __NOA_MD_WRAPPER_H__
#define __NOA_MD_WRAPPER_H__

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "t900/noa_md_mtk_priv_fullsoc.h"
#else
#include "mtk_dpmaif_drv.h"
#endif

enum noa_md_wpr_sync_duration {
	NOA_MD_WPR_SYNC_DPMAIF_TO_NOA = 0,
	NOA_MD_WPR_SYNC_NOA_TO_DPMAIF,
};

// Define the netdev_drv_dir enumeration type, the direction of data transfer
enum netdev_drv_dir {
	NETDEV_DRV_DIR_TX,
	NETDEV_DRV_DIR_RX
};

enum noa_md_wpr_drv_cmd {
	MOA_MD_WPR_CUSTOM_WWAN_NOTIFY,
	MOA_MD_WPR_CUSTOM_FEATURE
};

// Data container for mtk data commands
struct noa_md_wpr_custom_cmd {
	int cmd;
	void *data;
};

// Data container for mtk data notify
struct noa_md_wpr_custom_evt_data {
	int evt;
	struct mtk_data_blk *data_blk;
	void *data;
};

// Store data for DPMAIF event work items
struct noa_md_wpr_dpmaif_event_work_data {
	struct delayed_work work;
	enum dpmaif_drv_intr_type intr_type;
	unsigned int q_mask;
};

// Data structure of network device information,
// send from mtk_wwan or mtk_wwan_legacy
struct net_device_info {
	// TODO: implement feature
};

// The definition of pm_ops structure
struct noa_md_wpr_pm_ops {
	int (*suspend)(void *data);
	int (*suspend_late)(void *data);
	int (*resume_early)(void *data);
	int (*resume)(void *data);
};

// Define noa_md_drv_ops structure
struct noa_md_drv_ops {
	int (*init)(void *data);
	int (*exit)(void *data);
	int (*update_netdev)(struct net_device_info *info);
	int (*start_queue)(enum netdev_drv_dir dir);
	int (*stop_queue)(enum netdev_drv_dir dir);
	int (*tx_data)(void *dcb, struct sk_buff *skb, u64 data);
	int (*rx_data)(void *data);
	int (*data_irq_handle)(void *data);
	int (*feature_cmd)(int cmd, void *data);
	int (*modem_event_irq_handle_func)(void *data);
	int (*wwan_notify)(void *evt);
};

// Stores information related to dpmaif irq interrupts
struct noa_md_wpr_irq_data {
	int index;
	void *intr_info;
	void *dcb;
};

// Wrapper_device structure
struct noa_md_wpr_dev {
	dev_t dev_num;
	struct class *cls;
	struct device *dev;
	// DPMAIF event workqueue
	struct workqueue_struct *noa_md_tx_event_workqueue;
	struct workqueue_struct *noa_md_rx_event_workqueue;
	// Modem driver's operations
	struct noa_md_drv_ops *ops;
	// Modem driver's pm operations
	struct noa_md_wpr_pm_ops *pm_ops;
	// Control parameters
	int enabled;
};

extern struct noa_md_wpr_dev *md_wpr_dev;

// Function declaration
int noa_md_wpr_module_init(void *data);
int noa_md_wpr_module_exit(void *data);
int noa_md_wpr_update_netdev(struct net_device_info *info);
int noa_md_wpr_start_queue(enum netdev_drv_dir dir);
int noa_md_wpr_stop_queue(enum netdev_drv_dir dir);
int noa_md_wpr_tx_data(void *dcb, struct sk_buff *skb, u64 data);
int noa_md_wpr_rx_data(void *data);
int noa_md_wpr_feature_cmd(enum noa_md_wpr_drv_cmd cmd, void *data);
int noa_md_wpr_custom_feature_cmd(int cmd, void *data);
int noa_md_wpr_custom_data_event(void *evt_dat);
int noa_md_wpr_data_irq_handle(void *data);
int noa_md_wpr_set_modem_irq_handler(
	int (*noa_md_data_irq_handle_ptr)(void*));
struct noa_md_wpr_dev * noa_md_wpr_get_wpr_dev(void);

// Feature control
bool noa_md_wpr_is_noa_enable(void);
bool noa_md_wpr_is_noa_tx_enable(void);
bool noa_md_wpr_is_noa_rx_enable(void);
bool noa_md_wpr_is_noa_unified_desc_enable(void);

// PM
int noa_md_wpr_pm_suspend(void *data);
int noa_md_wpr_pm_suspend_late(void *data);
int noa_md_wpr_pm_resume_early(void *data);
int noa_md_wpr_pm_resume(void *data);
int noa_md_wpr_init(void);
void noa_md_wpr_exit(void);

#define ENSURE_NOA_MD_WPR_READY(error_code) \
do { \
	if (!md_wpr_dev) { \
		NOA_MD_WRAPPER_ERROR_LIMIT("md_wpr_dev is null"); \
		return -error_code; \
	} \
	if (!md_wpr_dev->ops) { \
		NOA_MD_WRAPPER_ERROR_LIMIT("ops is null"); \
		return -error_code; \
	} \
} while (0)

#define ENSURE_NOA_MD_WPR_PM_READY(error_code) \
do { \
	if (!md_wpr_dev) { \
		NOA_MD_WRAPPER_ERROR_LIMIT("md_wpr_dev is null"); \
		return -error_code; \
	} \
	if (!md_wpr_dev->pm_ops) { \
		NOA_MD_WRAPPER_ERROR_LIMIT("pm_ops is null"); \
		return -error_code; \
	} \
} while (0)

#define ENSURE_NOA_MD_WPR_ENABLE(error_code) \
do { \
	if (!md_wpr_dev->enabled) { \
		NOA_MD_WRAPPER_ERROR_LIMIT("Not enabled"); \
		return -error_code; \
	} \
} while (0)

#define ENSURE_NOA_ENABLE(error_code) \
do { \
	if (!md_dev.feature_ctrl.enabled) { \
		NOA_MD_WRAPPER_ERROR_LIMIT("NOA not enabled"); \
		return -error_code; \
	} \
} while (0)

#endif // __NOA_MD_WRAPPER_H__
