// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ganymede power management implementations.
 *
 * Copyright (C) 2024 Google LLC
 */

#include "gxp-internal.h"
#include "gxp-pm.h"

#define PMU_AUR_STATUS_ON 0x2

static const struct gxp_pm_ops gxp_pm_ops;

void gxp_pm_chip_set_ops(struct gxp_power_manager *mgr)
{
	mgr->ops = &gxp_pm_ops;
}

void gxp_pm_chip_init(struct gxp_dev *gxp)
{
}

void gxp_pm_chip_exit(struct gxp_dev *gxp)
{
}

bool gxp_pm_is_blk_down(struct gxp_dev *gxp)
{
	return gxp->power_mgr->aur_status ?
		       (readl(gxp->power_mgr->aur_status) != PMU_AUR_STATUS_ON) :
		       gxp->power_mgr->curr_state == AUR_OFF;
}
