/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __MAX77779_H
#define __MAX77779_H

#include <linux/platform_device.h>
#include "bcl.h"

#define MAX77779_VIMON_NV_PRE_LSB 78122
#define MAX77779_VIMON_NA_PRE_LSB 781250

int max77779_ifpmic_setup(struct bcl_device *bcl_dev, struct platform_device *pdev);
int max77779_adjust_batoilo_lvl(struct bcl_device *bcl_dev, u8 lower_enable, u8 set_batoilo1_lvl,
				u8 set_batoilo2_lvl);
int evt_cnt_rd_and_clr(struct bcl_device *bcl_dev, int idx, bool update_evt_cnt);
int google_bcl_max77779_vimon_init(struct bcl_device *bcl_dev);


#endif /* __MAX77779_H */
