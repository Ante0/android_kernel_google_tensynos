/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM Print Formats
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_CORE_PRINT_H__
#define __LVM_CORE_PRINT_H__
#define DEBUG

#include <linux/printk.h>

#define LVM_DBG(fmt, ...)		pr_debug(fmt, ##__VA_ARGS__)
#define LVM_INFO(fmt, ...)		pr_info(fmt, ##__VA_ARGS__)
#define LVM_NOTICE(fmt, ...)		pr_notice(fmt, ##__VA_ARGS__)
#define LVM_WARN(fmt, ...)		pr_warn(fmt, ##__VA_ARGS__)
#define LVM_ERR(fmt, ...)		pr_err(fmt, ##__VA_ARGS__)

#endif  /* __LVM_CORE_PRINT_H__ */
