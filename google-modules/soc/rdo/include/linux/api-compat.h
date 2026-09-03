/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024, Google LLC
 */

#ifndef _API_COMPAT_H
#define _API_COMPAT_H

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define assign_str_wrp(dst, src) __assign_str(dst)
#else
#define assign_str_wrp(dst, src) __assign_str(dst, src)
#endif

#endif /* _API_COMPAT_H */
