/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2024, MediaTek Inc.
 */

#ifndef __MTK_FRC_H__
#define __MTK_FRC_H__

#include "mtk_dev.h"
#include "mtk_pm.h"

struct mtk_md_frc_rec {
	u64 cur_host_ts;
	u32 cur_dev_us;
	u64 host_ts_diff;
	u32 dev_us_diff;
};

struct mtk_md_frc {
	unsigned long flag;
	atomic_t allow_sync;
	u64 curr_host_ts_local;
	u64 curr_host_ts;
	u32 curr_md_frc;
	u32 raw_md_frc;
	u32 l0_count;
	struct completion frc_complete;
	struct mtk_md_dev *mdev;
	struct mtk_md_frc_rec __rcu *frc_record;
	struct mtk_pm_entity pm_entity;
	struct work_struct sync_work;
	struct delayed_work clr_work;
};

#define FRC_SYNC_PERIOD 10
#define MAX_SYNC_PERIOD 90ULL
#define CUR_DIFF_SEC 180ULL

/**
 * mtk_frc_get_host_ts_by_dev_us() - Get corresponding host timestamp by input MD FRC.
 * @mdev: Device instance.
 * @md_frc: input MD FRC.
 * @host_ts: output host timestamp.
 *
 * Return:
 * * 0 - indicates success.
 * * other value - indicates failure.
 */
int mtk_frc_get_host_ts_by_dev_us(struct mtk_md_dev *mdev, u32 md_frc, u64 *host_ts);

/**
 * mtk_frc_get_dev_us_by_host_ts() - Get corresponding MD FRC by input host timestamp.
 * @mdev: Device instance.
 * @host_ts: input host timestamp.
 * @md_frc: output MD FRC.
 *
 * Return:
 * * 0 - indicates success.
 * * other value - indicates failure.
 */
int mtk_frc_get_dev_us_by_host_ts(struct mtk_md_dev *mdev, u64 host_ts, u32 *md_frc);

/**
 * mtk_frc_get_host_dur_by_dev_dur() - Get corresponding host duration by input MD duration.
 * @mdev: Device instance.
 * @md_dur: input MD duration.
 * @host_dur: output host duration.
 *
 * Return:
 * * 0 - indicates success.
 * * other value - indicates failure.
 */
int mtk_frc_get_host_dur_by_dev_dur(struct mtk_md_dev *mdev, u32 md_dur, u64 *host_dur);

int mtk_frc_sync_init(struct mtk_md_dev *mdev);
int mtk_frc_sync_exit(struct mtk_md_dev *mdev);

/**
 * mtk_frc_check_and_sync() - Check allow_sync flag to decide whether to start MD FRC sync.
 * @mdev: Device instance.
 */
void mtk_frc_check_and_sync(struct mtk_md_dev *mdev);

#endif
