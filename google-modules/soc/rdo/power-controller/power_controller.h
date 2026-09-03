/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024-2025 Google LLC */

#ifndef POWER_CONTROLLER_H
#define POWER_CONTROLLER_H

#include <linux/pm_domain.h>
#include <linux/types.h>

#include <soc/google/goog_mba_cpm_iface.h>
#include <soc/google/goog_power_controller.h>
#include <perf/mbfs.h>

#include "gpu_core_logic.h"
#include "latency/pd_latency_profile.h"

struct power_domain {
	struct power_controller *power_controller;
	struct generic_pm_domain genpd;
	const char *name;
	bool is_top_pd; /* Do not use, only populated for legacy platforms */
	bool use_smc;
	bool boot_stay_on;
	atomic_t state;
	u32 pm_resource_id;
	union mbfs_client_handle syspm_handle;

	union {
		struct gpu_core_logic_pd gpu_core_logic;
		struct {
			struct completion cpm_resp_done;
			/*
			 * sswrp id that CPM uses internally has different
			 * value depending on .is_top_pd:
			 * - for top power domain, it is cpm_lpb_sswrp_id.
			 * - for sub power domain, it is cpm_lpcm_sswrp_id.
			 */
			union {
				u32 cpm_lpcm_sswrp_id;
				u32 cpm_lpb_sswrp_id;
			};
			u32 cpm_lpcm_subdomain_id;
		};
	};
};

struct pc_privdata {
	atomic_t initialized;
	struct power_domain *pds;
	u32 pd_count;
};

struct power_controller {
	struct device *dev;
	struct cpm_iface_client *client;
	struct dentry *debugfs_root;
	struct latency_profile *latency;
	union mbfs_client_handle syspm_root;
	struct pc_privdata *privdata;
};

#endif /* POWER_CONTROLLER_H */
