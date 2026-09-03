/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for Destiny SoCs.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __MOBILE_SOC_DESTINY_H__
#define __MOBILE_SOC_DESTINY_H__

#include <linux/kernel.h>
#include <linux/compiler_types.h>
#include <linux/types.h>

#include "edgetpu-pixel-trim.h"

/* SoC data for Destiny platforms */
struct edgetpu_soc_data {
	/* WDT IRQ numbers */
	int *wdt_irq;
	/* LPB_SSWRP_TPU_CSRS, for reading power status. */
	void __iomem *lpb_sswrp_csrs;
	/* LPCM_LPM_TPU_CSRS, for reading power status. */
	void __iomem *lpcm_lpm_csrs;
	/* If true, bcl_mitigation_config is to be sent to firmware at each power up. */
	bool bcl_mitigation_valid;
	/* BCL mitigation config values to send to firmware. */
	struct edgetpu_kci_bcl_mitigation_config bcl_mitigation;
	struct edgetpu_pixel_trim edgetpu_pixel_trim;
};

#endif /* __MOBILE_SOC_DESTINY_H__ */
