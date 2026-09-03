/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _ZRAM_VH_H_
#define _ZRAM_VH_H_

#include "zram_drv.h"

#if IS_ENABLED(CONFIG_ZRAM_GS_VENDOR_HOOK)
int zram_vh_init(struct zram *zram);
int zram_vh_deinit(struct zram *zram);
#else
static inline int zram_vh_init(struct zram *zram) { return 0; }
static inline int zram_vh_deinit(struct zram *zram) { return 0; }
#endif

#endif /* _ZRAM_VH_H_ */
