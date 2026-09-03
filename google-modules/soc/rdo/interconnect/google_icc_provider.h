/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_ICC_PROVIDER_H
#define _GOOGLE_ICC_PROVIDER_H

#include <linux/platform_device.h>
#include <linux/types.h>

u32 google_icc_get_num_vc(void);

int google_icc_platform_probe(struct platform_device *pdev);
void google_icc_platform_remove(struct platform_device *pdev);

#endif /* _GOOGLE_ICC_PROVIDER_H */
