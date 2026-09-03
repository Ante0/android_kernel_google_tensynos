/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_EXCEPT_H__
#define __MTK_EXCEPT_H__

#include "mtk_dev.h"
#include "mtk_pci.h"

#define MTK_EXCEPTION_SUPPORT_ONLINE_DBG	BIT(2)
#define MTK_EXCEPTION_CONFIG_OFFSET		(5)
#define MTK_EXCEPTION_CONFIG_INFO		(7)
#define MTK_EXCEPTION_AEE_CONFIG		(3)

enum mtk_except_evt {
	EXCEPTION_LINK_ERR,
	EXCEPTION_RGU,
	EXCEPTION_REBOOTINT,
	EXCEPTION_AER_DETECTED,
	EXCEPTION_MAX
};

enum mtk_aee_type {
	MTK_AEE_TYPE_INVALID = 0,
	MTK_AEE_TYPE_AEE_OFF,
	MTK_AEE_TYPE_AEE_MINI,
	MTK_AEE_TYPE_AEE_FULL,
};

struct mtk_md_except {
	struct mtk_md_dev *mdev;
	unsigned long flag;
	int check_cnt;
	int pci_ext_irq_id;
	u32 dev_pin_cap;
	u32 config_info;
	enum mtk_reset_type type;
	/* exception_lock: lock to protect type and flag */
	spinlock_t exception_lock;
	struct timer_list guard_timer;
	struct timer_list check_link_timer;
};

int mtk_exception_report_evt(struct mtk_md_dev *mdev, enum mtk_except_evt evt);
void mtk_exception_start(struct mtk_md_dev *mdev);
void mtk_exception_stop(struct mtk_md_dev *mdev);
int mtk_exception_init(struct mtk_md_dev *mdev);
int mtk_exception_exit(struct mtk_md_dev *mdev);

#endif /* __MTK_EXCEPT_H__ */
