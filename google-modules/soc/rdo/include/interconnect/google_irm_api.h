/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_IRM_API_H
#define _GOOGLE_IRM_API_H

#include <linux/device.h>
#include <linux/types.h>

#include "google_irm_idx.h"
#include "google_irm_reg.h"

#if IS_ENABLED(CONFIG_GOOGLE_IRM)

u32 irm_register_read(struct device *dev, u32 client_idx, u32 reg_offset);
int irm_register_write(struct device *dev, u32 client_idx, u32 reg_offset, u32 val);
bool irm_probing_completed(void);

#else

u32 irm_register_read(struct device *dev, u32 client_idx, u32 reg_offset)
{
	return 0;
}

int irm_register_write(struct device *dev, u32 client_idx, u32 reg_offset, u32 val)
{
	return 0;
}

static inline bool irm_probing_completed(void)
{
	return true;
}

#endif /* CONFIG_GOOGLE_IRM */

#endif /* _GOOGLE_IRM_API_H */
