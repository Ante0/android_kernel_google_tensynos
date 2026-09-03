// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Google Inc.
 *
 */
#ifndef __NOA_MD_WRAPPER_T900_H__
#define __NOA_MD_WRAPPER_T900_H__

#include "mtk_pm.h"

#define DPMAIF_INTR_UL_MIN DPMAIF_INTR_MIN
#define DPMAIF_INTR_UL_MAX DPMAIF_INTR_UL_LEN_ERR
#define DPMAIF_INTR_DL_MIN DPMAIF_INTR_DL_LEGACY_DONE
#define DPMAIF_INTR_DL_MAX DPMAIF_INTR_DL_DONE

/**
 * enum noa_md_wpr_mtk_t900_drv_cmd - T900 driver commands for the NOA framework.
 *
 * This enumeration provides a single, stable entry point for the T900 driver,
 * used by the NOA framework to execute functional commands. It serves as a
 * clear API boundary that isolates the NOA framework from the T900 driver's
 * implementation, thus enhancing stability.
 *
 * The commands are specific to data path modules, primarily mtk_dpmaif and
 * mtk_wwan.
 *
 * @NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT: Initialize DPMAIF software components.
 * @NOA_MD_WPR_T900_CMD_DPMAIF_SW_RESET: Perform a software reset on DPMAIF.
 * @NOA_MD_WPR_T900_CMD_DPMAIF_SW_EXIT: De-initialize DPMAIF software components.
 * @NOA_MD_WPR_T900_CMD_DPMAIF_START: Start the DPMAIF data path.
 * @NOA_MD_WPR_T900_CMD_DPMAIF_STOP: Stop the DPMAIF data path.
 * @NOA_MD_WPR_T900_CMD_DPMAIF_STATUS_SYNC: Synchronize DPMAIF status.
 * @NOA_MD_WPR_T900_CMD_CLDMA_INIT: Initialize the CLDMA controller.
 * @NOA_MD_WPR_T900_CMD_CLDMA_EXIT: Exit/de-initialize the CLDMA controller.
 * @NOA_MD_WPR_T900_CMD_CLDMA_DEV_INIT: Initialize a specific CLDMA device.
 * @NOA_MD_WPR_T900_CMD_CLDMA_DEV_EXIT: Exit a specific CLDMA device.
 * @NOA_MD_WPR_T900_CMD_CLDMA_OPEN: Open/start CLDMA channels.
 * @NOA_MD_WPR_T900_CMD_CLDMA_CLOSE: Close/stop CLDMA channels.
 * @NOA_MD_WPR_T900_CMD_WWAN_INIT: Initialize WWAN components.
 * @NOA_MD_WPR_T900_CMD_WWAN_SETUP: Configure the WWAN data path and resources.
 * @NOA_MD_WPR_T900_CMD_WWAN_OPEN: Open/start the WWAN data path.
 * @NOA_MD_WPR_T900_CMD_WWAN_STOP: Stop/close the WWAN data path.
 * @NOA_MD_WPR_T900_CMD_WWAN_EXIT: De-initialize WWAN components.
 * @NOA_MD_WPR_T900_CMD_NETDEV_UPDATE: Notify of a network device state update.
 * @NOA_MD_WPR_T900_CMD_MAX: The number of commands, not a valid command.
 * @NOA_MD_WPR_T900_CMD_PCIE_PROBE_DONE: Configure pcie bar and data rings.
 * @NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT_SCRIPT: Initialize DPMAIF software components for script.
*/
enum noa_md_wpr_mtk_t900_drv_cmd {
	NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT,
	NOA_MD_WPR_T900_CMD_DPMAIF_SW_RESET,
	NOA_MD_WPR_T900_CMD_DPMAIF_SW_EXIT,
	NOA_MD_WPR_T900_CMD_DPMAIF_START,
	NOA_MD_WPR_T900_CMD_DPMAIF_STOP,
	NOA_MD_WPR_T900_CMD_DPMAIF_STATUS_SYNC,
	NOA_MD_WPR_T900_CMD_CLDMA_INIT,
	NOA_MD_WPR_T900_CMD_CLDMA_EXIT,
	NOA_MD_WPR_T900_CMD_CLDMA_DEV_INIT,
	NOA_MD_WPR_T900_CMD_CLDMA_DEV_EXIT,
	NOA_MD_WPR_T900_CMD_CLDMA_OPEN,
	NOA_MD_WPR_T900_CMD_CLDMA_CLOSE,
	NOA_MD_WPR_T900_CMD_WWAN_INIT,
	NOA_MD_WPR_T900_CMD_WWAN_SETUP,
	NOA_MD_WPR_T900_CMD_WWAN_OPEN,
	NOA_MD_WPR_T900_CMD_WWAN_STOP,
	NOA_MD_WPR_T900_CMD_WWAN_EXIT,
	NOA_MD_WPR_T900_CMD_NETDEV_UPDATE,
	NOA_MD_WPR_T900_CMD_PCIE_PROBE_DONE,
#if IS_ENABLED(CONFIG_DEBUG_FS)
	NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT_SCRIPT,
#endif
	NOA_MD_WPR_T900_CMD_MAX,
};

/**
 * struct noa_md_wpr_mtk_t900_dev - T900 wrapper device structure
 * @pm_entity_register:     Function pointer to register a PM entity.
 * @pm_entity_unregister:   Function pointer to unregister a PM entity.
 * @drv_cmd_enabled:        (Debugfs only) An array to control command execution.
 * If false for a command, it is "gated" and can only be called
 * via the debugfs interface, not by the normal driver path.
 */
struct noa_md_wpr_mtk_t900_dev {
	int (*pm_entity_register)(
		struct mtk_md_dev *mdev, struct mtk_pm_entity *md_entity);
	int (*pm_entity_unregister)(
		struct mtk_md_dev *mdev, struct mtk_pm_entity *md_entity);
	bool drv_cmd_enabled[NOA_MD_WPR_T900_CMD_MAX];
};

struct noa_md_wpr_mtk_t900_evt_data {
	int evt;
	struct mtk_data_blk *data_blk;
	void *data;
};

struct noa_md_wpr_mtk_t900_pm_data {
	struct mtk_md_dev *mdev;
	void *data;
	bool is_runtime;
	bool link_ready;
};

int noa_md_wpr_t900_data_irq_handle(void *data);
void noa_md_wpr_t900_event_work(struct work_struct *work);
int noa_md_wpr_t900_feature_cmd(int cmd, void *data);
int noa_md_wpr_t900_wwan_notify(void *evt_dat);
struct noa_md_drv_ops* noa_md_wpr_t900_get_drv_ops(void);
const char *noa_md_wpr_t900_cmd_to_str(enum noa_md_wpr_mtk_t900_drv_cmd cmd);

// PM
int noa_md_wpr_t900_suspend(struct mtk_md_dev *mdev, void *data, bool is_runtime);
int noa_md_wpr_t900_suspend_late(struct mtk_md_dev *mdev, void *data, bool is_runtime);
int noa_md_wpr_t900_resume_early(struct mtk_md_dev *mdev, void *data, bool is_runtime, bool link_ready);
int noa_md_wpr_t900_resume(struct mtk_md_dev *mdev, void *data, bool is_runtime, bool link_ready);
int noa_md_wpr_t900_set_pm_entity_register(
	int (*pm_entity_register)(
		struct mtk_md_dev *mdev, struct mtk_pm_entity *md_entity));
int noa_md_wpr_t900_set_pm_entity_unregister(
	int (*pm_entity_unregister)(
		struct mtk_md_dev *mdev, struct mtk_pm_entity *md_entity));
int noa_md_wpr_mtk_t900_init(void);
void noa_md_wpr_mtk_t900_exit(void);

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * noa_md_wpr_t900_set_cmd_enabled() - Sets the enabled state for a command.
 * @cmd_id:  The command ID to configure.
 * @enabled: True to enable the command for driver calls,
 * false to disable it (making it debugfs-only).
 *
 * This function is for debugfs use only.
 *
 * Return: 0 on success, or -EINVAL for an invalid command ID.
 */
int noa_md_wpr_t900_set_cmd_enabled(int cmd_id, bool enabled);

/**
 * noa_md_wpr_t900_is_cmd_enabled() - Gets the enabled status for a command.
 * @cmd_id: The command ID to query.
 *
 * Return: True if the command is enabled for driver calls, false otherwise.
 * Returns false on error.
 */
int noa_md_wpr_t900_is_cmd_enabled(int cmd_id);
#else
static inline int noa_md_wpr_t900_set_cmd_enabled(int cmd_id, bool enabled)
{
	return 0;
}

static inline int noa_md_wpr_t900_is_cmd_enabled(int cmd_id)
{
	return 1;
}
#endif /* CONFIG_DEBUG_FS */

#endif // __NOA_MD_WRAPPER_T900_H__
