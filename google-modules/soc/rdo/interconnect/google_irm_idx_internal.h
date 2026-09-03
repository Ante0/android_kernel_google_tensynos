/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_IRM_IDX_INTERNAL_H
#define _GOOGLE_IRM_IDX_INTERNAL_H

#include <linux/bitops.h>

#include <interconnect/google_irm_idx.h>

#define IRM_TYPE_GMC		BIT(31)
#define IRM_TYPE_GSLC		BIT(30)

#define IRM_GMC_GSLC_MASK	(BIT(31) | BIT(30))
#define IRM_IDX_MASK		0xFFFF

#endif /* _GOOGLE_IRM_IDX_INTERNAL_H */
