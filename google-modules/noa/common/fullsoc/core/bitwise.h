/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM Bitwise Operations
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_CORE_BITWISE_H__
#define __LVM_CORE_BITWISE_H__

#include <linux/io.h>

#define setbits(addr, set)		writel(readl(addr) |  (set), (addr))
#define clrbits(addr, clr)		writel(readl(addr) & ~(clr), (addr))
#define clrsetbits(addr, clr, set)	writel((readl(addr) & ~(clr)) | (set), (addr))

#endif  /* __LVM_CORE_BITWISE_H__ */
