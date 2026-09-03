/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/debugfs.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/pm_domain.h>

#include "pm_domain.h"

struct pixel_gpu_debug_info;

typedef int (*pixel_gpu_debug_reader_func)(struct pixel_gpu_debug_info *debug,
					   char __user *buf, size_t len,
					   loff_t *ppos);

struct pixel_gpu_debug_node {
	struct pixel_gpu_debug_info *parent;
	pixel_gpu_debug_reader_func read;
};

struct pixel_gpu_debug_state_stats {
	ktime_t time_spent;
	uint64_t transitions;
};

struct pixel_gpu_debug_pd_stats {
	ktime_t last_transition;
	enum genpd_notication last_notification;
	struct pixel_gpu_debug_state_stats
		notification_stats[GENPD_NOTIFY_ON + 1];
};

struct pixel_gpu_debug_info {
	struct dentry *root;

	struct mutex genpd_stats_mutex;
	struct pixel_gpu_debug_node genpd_stats_node;
	struct pixel_gpu_debug_pd_stats pd_stats[PIXEL_GPU_PM_DOMAIN_COUNT];
};

struct pixel_gpu_device;

/**
 * pixel_gpu_debug_init() - Create debugfs nodes, store their bindings in
 *                          &struct pixel_gpu_debug_info (inside &struct
 *                          pixel_gpu_device), and initialize debugfs data with
 *                          defaults assuming the GPU has just been probed and
 *                          turned off.
 * @pixel_dev: The pixel GPU device, containing a zero-initialized instance of
 *             &struct pixel_gpu_debug_info.
 *
 * Returns 0 on success
 */
int pixel_gpu_debug_init(struct pixel_gpu_device *pixel_dev);
void pixel_gpu_debug_deinit(struct pixel_gpu_device *pixel_dev);

void pixel_gpu_debug_update_genpd_state(struct pixel_gpu_device *pixel_dev,
					enum pixel_gpu_pm_domain domain,
					enum genpd_notication notification);
