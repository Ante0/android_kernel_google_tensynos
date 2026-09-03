/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */
#ifndef __POWER_CONTROLLER_MAPPINGS_H__
#define __POWER_CONTROLLER_MAPPINGS_H__

#include <linux/types.h>

#define NOT_SUPPORTED 50

struct cpm_mappings {
	const u32 *sswrp_to_lpb_ids;
	const u32 *sswrp_to_lpcm_ids;
};

extern const struct cpm_mappings lga_cpm_mappings;
extern const struct cpm_mappings mbu_cpm_mappings;

#endif  /* __POWER_CONTROLLER_MAPPINGS_H__ */
