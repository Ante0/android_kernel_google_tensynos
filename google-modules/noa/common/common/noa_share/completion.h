/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header that align API between NOA mode and Kernel mode
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __LINUX_PORT_NOA_SHARE_COMPLETION__
#define __LINUX_PORT_NOA_SHARE_COMPLETION__

static inline void deinit_completion(struct completion *x)
{
	(void)x;
}

#endif /* __LINUX_PORT_NOA_SHARE_COMPLETION__ */
