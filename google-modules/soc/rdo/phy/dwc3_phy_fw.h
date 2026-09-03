/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */
#include <linux/types.h>

#ifndef _DWC3_PHY_FW_H
#define _DWC3_PHY_FW_H

struct fw_patch_entry {
	u32 addr;
	u16 val;
};

#endif
