/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2022 Google LLC
 */

/* This header is internal only.
 *
 * Public APIs are in //private/google-modules/soc/gs/include/linux/gsa/
 *
 * Include via //private/google-modules/soc/gs:gs_soc_headers
 */

#ifndef __LINUX_GSA_LOG_H
#define __LINUX_GSA_LOG_H

#define GSA_LOG_SIZE ((CONFIG_GSA_LOG_REGION_SIZE) / 2)

struct gsa_log;

struct gsa_log *gsa_log_init(struct platform_device *pdev);

ssize_t gsa_log_read(struct gsa_log *log, bool intermediate, char *buf, size_t sz);

#endif /* __LINUX_GSA_LOG_H */
