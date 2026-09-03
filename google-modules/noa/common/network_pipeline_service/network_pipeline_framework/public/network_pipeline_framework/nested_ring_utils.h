/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Nested Ring Utility Functions.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_UTILS_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_UTILS_H

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/io.h>
#else /* linux */
#include <cerrno>
#include <cstdint>

#include "common/defs.h"
#include "linux_port/io.h"
#endif /* linux */

static inline uintptr_t noa_readptr(const uintptr_t *addr)
{
#ifdef CONFIG_64BIT
	return readq(addr);
#else /* CONFIG_64BIT */
	return readl(addr);
#endif /* CONFIG_64BIT */
}
static inline void noa_writeptr(uintptr_t value, uintptr_t *addr)
{
#ifdef CONFIG_64BIT
	writeq(value, addr);
#else /* CONFIG_64BIT */
	writel(value, addr);
#endif /* CONFIG_64BIT */
}

#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_UTILS_H */
