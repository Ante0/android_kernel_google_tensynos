/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_IO_H
#define _GOOGLE_DPA_IO_H

#include <linux/types.h>

/*
 * memcpy_toio() uses writeq() but writeq() does not work when copying from APC
 * to NCP/NEP's TCM/SRAM.
 */
void google_dpa_memcpy_toio(void __iomem *to, const void *from, size_t count);

/*
 * memset_io() uses writeq() but writeq() does not work when copying from APC
 * to NCP/NEP's TCM/SRAM.
 */
void google_dpa_memset_io(void __iomem *dst, int c, size_t count);

/*
 * memcpy_from() uses readq() but readq() does not work when copying from
 * NCP/NEP's TCM/SRAM to AP.
 */
void google_dpa_memcpy_fromio(void *to, const void __iomem *from, size_t count);

#endif /* _GOOGLE_DPA_IO_H */
