/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_DEV_H__
#define __MTK_DEV_H__

#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "mtk_utility.h"

#define MTK_DEV_STR_LEN 16

enum mtk_user_id {
	MTK_USER_MIN,
	MTK_USER_CTRL,
	MTK_USER_DATA,
	MTK_USER_PM,
	MTK_USER_EXCEPT,
	MTK_USER_DEVLINK,
	MTK_USER_HW,
	MTK_USER_FRC,
	MTK_USER_STATS,
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	MTK_USER_GOOGLE,
#endif
	MTK_USER_MAX
};

enum mtk_reinit_type {
	REINIT_TYPE_RESUME	= 0,
	REINIT_TYPE_EXP		= 1
};

enum mtk_dev_evt_h2d {
	DEV_EVT_H2D_EXCEPT_ACK	= BIT(0),
	DEV_EVT_H2D_EXCEPT_CLEARQ_ACK	= BIT(1),
	DEV_EVT_H2D_DEVICE_RESET	= BIT(2),
	DEV_EVT_H2D_MD_REBOOT_ACK	= BIT(3),
	DEV_EVT_H2D_TRM_NOTIFY		= BIT(4),
	DEV_EVT_H2D_MAX			= BIT(5)
};

enum mtk_dev_evt_d2h {
	DEV_EVT_D2H_EXCEPT_INIT	= BIT(0),
	DEV_EVT_D2H_EXCEPT_INIT_DONE	= BIT(1),
	DEV_EVT_D2H_EXCEPT_CLEARQ_DONE	= BIT(2),
	DEV_EVT_D2H_EXCEPT_ALLQ_RESET	= BIT(3),
	DEV_EVT_D2H_BOOT_FLOW_SYNC	= BIT(4),
	DEV_EVT_D2H_ASYNC_HS_NOTIFY_SAP = BIT(5),
	DEV_EVT_D2H_ASYNC_HS_NOTIFY_MD	= BIT(6),
	DEV_EVT_D2H_MD_REBOOT		= BIT(7),
	DEV_EVT_D2H_MD_POWER_OFF	= BIT(8),
	DEV_EVT_D2H_GNSS_ENABLE		= BIT(9),
	DEV_EVT_D2H_GNSS_DISABLE	= BIT(10),
	DEV_EVT_D2H_MAX			= BIT(11)
};

enum mtk_dev_log_type {
	MTK_DEV_LOG_BROM_SRAM = 0,
	MTK_DEV_LOG_BROM_SRAM_ORIGIN,
	MTK_DEV_LOG_PL_SRAM,
	MTK_DEV_LOG_PL_DRAM,
	MTK_DEV_LOG_ATF_DRAM,
	MTK_DEV_LOG_HOST_RECVED

};

struct mtk_md_dev;

struct mtk_dev_ops {
	u32 (*get_dev_state)(struct mtk_md_dev *mdev);
	void (*ack_dev_state)(struct mtk_md_dev *mdev, u32 state);
	u32 (*get_dev_cfg)(struct mtk_md_dev *mdev);
	int (*register_dev_evt)(struct mtk_md_dev *mdev, u32 dev_evt,
				int (*evt_cb)(u32 status, void *data), void *data);
	void (*unregister_dev_evt)(struct mtk_md_dev *mdev, u32 dev_evt);
	void (*mask_dev_evt)(struct mtk_md_dev *mdev, u32 dev_evt);
	void (*unmask_dev_evt)(struct mtk_md_dev *mdev, u32 dev_evt);
	void (*clear_dev_evt)(struct mtk_md_dev *mdev, u32 dev_evt);
	int (*send_dev_evt)(struct mtk_md_dev *mdev, u32 dev_evt);
	int (*reinit)(struct mtk_md_dev *mdev, enum mtk_reinit_type type);
	int (*get_dev_log)(struct mtk_md_dev *mdev,
			   void *buf, size_t count, enum mtk_dev_log_type type);
	int (*get_log_region_size)(struct mtk_md_dev *mdev, enum mtk_dev_log_type type);
	int (*dbg_dump)(struct mtk_md_dev *mdev);
};

struct mtk_utility_cfg {
	struct mtk_memlog_cfg *memlog_cfg;
	struct mtk_statistics_cfg *stats_cfg;
};

/* mtk_md_dev defines the structure of MTK modem device */
struct mtk_md_dev {
	struct device *dev;
	const struct mtk_dev_ops *dev_ops;
	void *hw_priv;
	u32 hw_ver;
	char dev_str[MTK_DEV_STR_LEN];
	void *fsm;
	void *ctrl_blk;
	void *data_blk;
	void *devlink;
	struct dentry *dev_dentry; /* For debug */
	void *bm_ctrl;
	void *memlog;
	struct mtk_utility_cfg *utility_cfg;
	struct mtk_statistics *stats;
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	void *google;
#endif
};

struct mtk_utility_cfg_desc {
	u32 hw_ver;
	struct mtk_utility_cfg *utility_cfg;
};

#define utility_cfg_name(NAME)	mtk_utility_cfg_##NAME

int mtk_dev_dump(struct mtk_md_dev *mdev);

struct mtk_md_dev *mtk_dev_alloc(struct device *pdev, const struct mtk_dev_ops *dev_ops);

void mtk_dev_free(struct mtk_md_dev *mdev);

void mtk_dev_except(struct mtk_md_dev *mdev);

static inline u32 mtk_dev_get_dev_state(struct mtk_md_dev *mdev)
{
	return mdev->dev_ops->get_dev_state(mdev);
}

static inline void mtk_dev_ack_dev_state(struct mtk_md_dev *mdev, u32 state)
{
	return mdev->dev_ops->ack_dev_state(mdev, state);
}

static inline u32 mtk_dev_get_dev_cfg(struct mtk_md_dev *mdev)
{
	return mdev->dev_ops->get_dev_cfg(mdev);
}

static inline int mtk_dev_register_dev_evt(struct mtk_md_dev *mdev, u32 dev_evt,
					   int (*evt_cb)(u32 status, void *data), void *data)
{
	return mdev->dev_ops->register_dev_evt(mdev, dev_evt, evt_cb, data);
}

static inline void mtk_dev_unregister_dev_evt(struct mtk_md_dev *mdev, u32 dev_evt)
{
	mdev->dev_ops->unregister_dev_evt(mdev, dev_evt);
}

static inline void mtk_dev_mask_dev_evt(struct mtk_md_dev *mdev, u32 dev_evt)
{
	mdev->dev_ops->mask_dev_evt(mdev, dev_evt);
}

static inline void mtk_dev_unmask_dev_evt(struct mtk_md_dev *mdev, u32 dev_evt)
{
	mdev->dev_ops->unmask_dev_evt(mdev, dev_evt);
}

static inline void mtk_dev_clear_dev_evt(struct mtk_md_dev *mdev, u32 dev_evt)
{
	mdev->dev_ops->clear_dev_evt(mdev, dev_evt);
}

static inline int mtk_dev_send_dev_evt(struct mtk_md_dev *mdev, u32 dev_evt)
{
	return mdev->dev_ops->send_dev_evt(mdev, dev_evt);
}

static inline int mtk_dev_reinit(struct mtk_md_dev *mdev, enum mtk_reinit_type type)
{
	return mdev->dev_ops->reinit(mdev, type);
}

static inline int mtk_dev_dbg_dump(struct mtk_md_dev *mdev)
{
	return mdev->dev_ops->dbg_dump(mdev);
}

static inline int mtk_dev_get_dev_log(struct mtk_md_dev *mdev,
				      void *buf, size_t count, enum mtk_dev_log_type type)
{
	return mdev->dev_ops->get_dev_log(mdev, buf, count, type);
}

static inline int mtk_dev_get_log_region_size(struct mtk_md_dev *mdev, enum mtk_dev_log_type type)
{
	return mdev->dev_ops->get_log_region_size(mdev, type);
}

#endif /* __MTK_DEV_H__ */
