/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_IRM_REG_H
#define _GOOGLE_IRM_REG_H

#include <linux/kconfig.h>

/* clang-format off */

#if IS_ENABLED(CONFIG_SOC_LGA)

#include "google_irm_reg_lga.h"

#elif IS_ENABLED(CONFIG_SOC_MBU)

#include "google_irm_reg_mbu.h"

#endif

#define SIZE_DVFS_REQ_REG		(DVFS_REQ_TRIG + 0x4)

/* clang-format on */

#endif /* _GOOGLE_IRM_REG_H */
