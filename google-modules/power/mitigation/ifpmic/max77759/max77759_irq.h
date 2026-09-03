/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __MAX77759_H
#define __MAX77759_H

#include <linux/platform_device.h>
#include "bcl.h"

#define BATOILO_DET_SHIFT 2
#define UVLO2_DET_SHIFT 1
#define UVLO1_DET_SHIFT 0
#define CHG_CNFG_17_AICL_MASK 0xF8
#define CHG_CNFG_17_DET_MASK 0x7

int max77759_ifpmic_setup(struct bcl_device *bcl_dev, struct platform_device *pdev);
int google_bcl_max77759_vimon_init(struct bcl_device *bcl_dev);
static inline int evt_cnt_rd_and_clr(struct bcl_device *bcl_dev, int idx, bool update_evt_cnt) {
    return 0;
}

#endif /* __MAX77759_H */
