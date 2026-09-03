/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS PWRSEQ Device Driver
 *
 * Copyright (c) 2025 Google, LLC
 */

#ifndef LWIS_DEVICE_PWRSEQ_H_
#define LWIS_DEVICE_PWRSEQ_H_

#include <linux/pwrseq/provider.h>
#include <linux/pwrseq/consumer.h>

#include "lwis_device.h"

struct lwis_pwrseq_device {
	struct lwis_device base_dev;
	char target_name[LWIS_MAX_NAME_STRING_LEN];
	struct pwrseq_device *pwrseq;
};

/*
 * Wrapper for devm_pwrseq_get() to workaround matching issue.
 */
struct pwrseq_desc *lwis_devm_pwrseq_get(struct device *dev, const char *target);

int lwis_pwrseq_device_init(void);
int lwis_pwrseq_device_deinit(void);

#endif /* LWIS_DEVICE_PWRSEQ_H_ */
