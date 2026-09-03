/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef _GS_REG_DUMP_H_
#define _GS_REG_DUMP_H_

#include <drm/drm_print.h>

#define REG_DUMP_MAX_LINE_BYTE_LEN 16U
#define REG_DUMP_OFFSET_ALIGNMENT 4
#define REG_DUMP_REG_BYTES 4

struct regmap_access_table;

int gs_reg_dump_with_skips(const char *desc, const volatile void __iomem *base, u32 offset,
			   u32 size, struct drm_printer *p,
			   const struct regmap_access_table *regmap_access_table);

int gs_reg_dump(const char *desc, const volatile void __iomem *base, u32 offset, u32 size,
		struct drm_printer *p);
#endif // _GS_REG_DUMP_H_
