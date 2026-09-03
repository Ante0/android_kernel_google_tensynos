/* SPDX-License-Identifier: GPL */
/*
 * Google Touch Interface - log
 *
 * Copyright 2026 Google LLC.
 */

#ifndef _GTI_LOG_H_
#define _GTI_LOG_H_

#include <linux/printk.h>

#define GOOG_LOG_NAME(gti) ((gti && gti->dev) ? dev_name(gti->dev) : "gti")
#define GOOG_DBG(gti, fmt, args...) pr_debug("%s: " fmt, GOOG_LOG_NAME(gti), ##args)
#define GOOG_INFO(gti, fmt, args...) pr_info("%s: " fmt, GOOG_LOG_NAME(gti), ##args)
#define GOOG_WARN(gti, fmt, args...) pr_warn("%s: " fmt, GOOG_LOG_NAME(gti), ##args)
#define GOOG_ERR(gti, fmt, args...) pr_err("%s: " fmt, GOOG_LOG_NAME(gti), ##args)
#define GOOG_LOGD(gti, fmt, args...) GOOG_DBG(gti, "%s: " fmt, __func__, ##args)
#define GOOG_LOGI(gti, fmt, args...) GOOG_INFO(gti, "%s: " fmt, __func__, ##args)
#define GOOG_LOGW(gti, fmt, args...) GOOG_WARN(gti, "%s: " fmt, __func__, ##args)
#define GOOG_LOGE(gti, fmt, args...) GOOG_ERR(gti, "%s: " fmt, __func__, ##args)

#endif // _GTI_LOG_H_
