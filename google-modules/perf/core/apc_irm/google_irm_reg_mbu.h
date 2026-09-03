/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_IRM_REG_MBU_H
#define _GOOGLE_IRM_REG_MBU_H

#define DVFS_REQ_RD_BW_AVG_GMC 0x0
#define DVFS_REQ_WR_BW_AVG_GMC 0x4
/*
 * DVFS_REQ_RD_BW_VCDIST_GMC and DVFS_REQ_WR_BW_VCDIST_GMC in hw spec. It is repurposed to
 * real time bandwidth.
 */
#define DVFS_REQ_RD_BW_RT_GMC 0x8
#define DVFS_REQ_WR_BW_RT_GMC 0xc
#define DVFS_REQ_RD_BW_PEAK_GMC 0x10
#define DVFS_REQ_WR_BW_PEAK_GMC 0x14
/* DVFS_REQ_LATENCY_GMC is unused in practice. */
#define DVFS_REQ_LATENCY_GMC 0x18
/*
 * DVFS_REQ_MIN_CLAMP_GMC is repurposed to frequency min clamp.
 */
#define DVFS_REQ_MIN_CLAMP_GMC 0x1c
/* DVFS_REQ_MAX_CLAMP_GMC is unused in practice. */
#define DVFS_REQ_MAX_CLAMP_GMC 0x20
#define DVFS_REQ_TRIG 0x24

#endif /* _GOOGLE_IRM_REG_MBU_H */
