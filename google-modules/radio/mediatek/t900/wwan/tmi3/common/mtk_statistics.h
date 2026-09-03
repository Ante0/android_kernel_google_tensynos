/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2024, MediaTek Inc.
 */

#ifndef __MTK_STATISTICS_H__
#define __MTK_STATISTICS_H__

#include <linux/delay.h>
#include <linux/pci.h>

#include "mtk_dev.h"
#include "mtk_memlog.h"

#define MTK_STATS_INFO_LEN 128
#define MTK_STATS_INTERVAL_MIN 1
#define MTK_STATS_BUF_CGF_INDEX 11
#define MTK_STATS_DEBUG_MASK_DEFAULT 0

struct mtk_statistics_cfg {
	int ctrl_type_base;
	int data_type_base;
	int sys_type_base;
	int stats_type_cnt;
};

struct mtk_statistics;

struct mtk_statistics_type {
	struct work_struct stats_work;
	struct mtk_statistics *stats;
	struct delayed_work bit_restore_work;
	u32 interval;
	void *data;
	ssize_t (*stats_cb)(struct mtk_md_dev *mdev, void *data, char *buf);
};

struct mtk_statistics {
	struct mtk_md_dev *mdev;
	struct workqueue_struct *stats_wq;
	atomic_t stats_attr_in_use;
	atomic_t stats_debug_bitmap;
	atomic_t stats_debug_refcnt;
	bool stats_debug_is_enabled;
	int stats_attr_type;
	struct mtk_statistics_type stats_types[];
};

int mtk_stats_chk_and_proc(struct mtk_md_dev *mdev, unsigned long types);
int mtk_stats_register_cb(struct mtk_md_dev *mdev, int type, u32 interval,
			  ssize_t (*cb)(struct mtk_md_dev *mdev, void *data, char *buf),
			  void *data);
int mtk_stats_unregister_cb(struct mtk_md_dev *mdev, int type);
int mtk_stats_init(struct mtk_md_dev *mdev);
int mtk_stats_init_late(struct mtk_md_dev *mdev);
int mtk_stats_exit(struct mtk_md_dev *mdev);
int mtk_stats_exit_early(struct mtk_md_dev *mdev);

#endif
