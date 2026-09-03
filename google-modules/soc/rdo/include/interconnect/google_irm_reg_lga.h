/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_IRM_REG_LGA_H
#define _GOOGLE_IRM_REG_LGA_H

#include <linux/kconfig.h>

/* clang-format off */

#define DVFS_REQ_RD_BW_AVG_GMC		0x0
#define DVFS_REQ_WR_BW_AVG_GMC		0x4
/*
 * DVFS_REQ_RD_BW_VCDIST_GMC and DVFS_REQ_WR_BW_VCDIST_GMC in hw spec. It is repurposed to
 * real time bandwidth.
 */
#define DVFS_REQ_RD_BW_RT_GMC           0x8
#define DVFS_REQ_WR_BW_RT_GMC           0xc
#define DVFS_REQ_RD_BW_PEAK_GMC		0x10
#define DVFS_REQ_WR_BW_PEAK_GMC		0x14
#define DVFS_REQ_LATENCY_GMC		0x18
/*
 * DVFS_REQ_MIN_CLAMP_GMC in hw spec. It is repurposed to frequency min clamp. It is not used by
 * ICC, but by other irm clients such as devfreq.
 */
#define DVFS_REQ_MIN_CLAMP_GMC          0x1c
#define DVFS_REQ_RD_BW_AVG_GSLC		0x20
#define DVFS_REQ_WR_BW_AVG_GSLC		0x24
/*
 * DVFS_REQ_RD_BW_VCDIST_GSLC and DVFS_REQ_WR_BW_VCDIST_GSLC in hw spec. It is repurposed to
 * real time bandwidth.
 */
#define DVFS_REQ_RD_BW_RT_GSLC          0x28
#define DVFS_REQ_WR_BW_RT_GSLC          0x2c
#define DVFS_REQ_RD_BW_PEAK_GSLC	0x30
#define DVFS_REQ_WR_BW_PEAK_GSLC	0x34
#define DVFS_REQ_LATENCY_GSLC		0x38
/* DVFS_REQ_LTV_GSLC is unused in practice. */
#define DVFS_REQ_LTV_GSLC		0x3c
#define DVFS_REQ_TRIG			0x40

/* clang-format on */

#endif /* _GOOGLE_IRM_REG_LGA_H */
