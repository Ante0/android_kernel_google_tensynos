/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header file for Destiny.
 *
 * Copyright (C) 2023 Google LLC
 */

#ifndef __MOBILE_SOC_DESTINY_H__
#define __MOBILE_SOC_DESTINY_H__

#include <linux/devfreq.h>
#include <linux/pm_qos.h>
#include <linux/workqueue.h>

#include <gcip/gcip-memory.h>

#include "gxp-internal.h"

struct dsu_devfreq {
	struct devfreq *df;
	struct dev_pm_qos_request qos_req;
	/* The worker to asynchronously reset freq for DSU. */
	struct work_struct dsu_reset_work;
};

struct gxp_soc_data {
	struct gcip_memory gem_regs;
	struct dsu_devfreq dsu_df;
};

#endif /* __MOBILE_SOC_DESTINY_H__ */
