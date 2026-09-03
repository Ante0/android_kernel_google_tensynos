/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __RADIO_UTILS_H__
#define __RADIO_UTILS_H__

#define TAG "radio_google"

#define LOG_DEBUG(fmt, ...) pr_debug("%s: %s: " pr_fmt(fmt), TAG, __func__, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) pr_info("%s: %s: " pr_fmt(fmt), TAG, __func__, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) pr_warn("%s: %s: " pr_fmt(fmt), TAG, __func__, ##__VA_ARGS__)

#define LOG_ERR(fmt, ...) pr_err("%s: %s: " pr_fmt(fmt), TAG, __func__, ##__VA_ARGS__)

#define CALLER (__builtin_return_address(0))

#endif /* __RADIO_UTILS_H__ */
