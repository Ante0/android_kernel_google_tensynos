// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/align.h>
#include <linux/io.h>

#include "google_dpa_io.h"

void google_dpa_memcpy_toio(void __iomem *to, const void *from, size_t count)
{
	__io_bw();

	while (count && !IS_ALIGNED((unsigned long)to, 4)) {
		writeb_relaxed(*(u8 *)from, to);
		from++;
		to++;
		count--;
	}

	while (count >= 4) {
		writel_relaxed(*(u32 *)from, to);
		from += 4;
		to += 4;
		count -= 4;
	}

	while (count) {
		writeb_relaxed(*(u8 *)from, to);
		from++;
		to++;
		count--;
	}

	__io_aw();
}

void google_dpa_memset_io(void __iomem *dst, int c, size_t count)
{
	u32 val = (u32)c;

	val |= val << 8;
	val |= val << 16;

	__io_bw();

	while (count && !IS_ALIGNED((unsigned long)dst, 4)) {
		writeb_relaxed(c, dst);
		dst++;
		count--;
	}

	while (count >= 4) {
		writel_relaxed(val, dst);
		dst += 4;
		count -= 4;
	}

	while (count) {
		writeb_relaxed(c, dst);
		dst++;
		count--;
	}

	__io_aw();
}

void google_dpa_memcpy_fromio(void *to, const void __iomem *from, size_t count)
{
	while (count && !IS_ALIGNED((unsigned long)from, 4)) {
		*(u8 *)to = readb_relaxed(from);
		from++;
		to++;
		count--;
	}

	while (count >= 4) {
		*(u32 *)to = readl_relaxed(from);
		from += 4;
		to += 4;
		count -= 4;
	}

	while (count) {
		*(u8 *)to = readb_relaxed(from);
		from++;
		to++;
		count--;
	}
}
