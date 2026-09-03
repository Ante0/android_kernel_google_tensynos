/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020 Google LLC
 */

/* This header is internal only.
 *
 * Public APIs are in //private/google-modules/soc/gs/include/linux/gsa/
 *
 * Include via //private/google-modules/soc/gs:gs_soc_headers
 */

#ifndef __LINUX_GSA_PRIV_H
#define __LINUX_GSA_PRIV_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>
#include "gsa_tz.h"

enum gsa_suspend_hint_state {
	GSA_SUSPEND_HINT_UNKNOWN,
	GSA_SUSPEND_HINT_SUPPORTED,
	GSA_SUSPEND_HINT_NOT_SUPPORTED,
};

struct gsa_cdev {
	dev_t device_num;
	struct cdev cdev;
	struct device *device;
};

struct gsa_dev_state {
	struct device *dev;
	struct gsa_mbox *mb;
	dma_addr_t bb_da;
	void *bb_va;
	size_t bb_sz;
	struct mutex bb_lock; /* protects access to bounce buffer */
	int32_t rom_patch;
	enum gsa_suspend_hint_state suspend_hint;
	struct gsa_tz_chan_ctx aoc_srv;
	struct gsa_tz_chan_ctx tpu_srv;
	struct gsa_tz_chan_ctx dsp_srv;
	struct gsa_log *log;
	struct gsa_cdev cdev_node;
	struct gsa_cdev cdev_pm_stats_node;
#if IS_ENABLED(CONFIG_GSA_DEBUGFS)
	struct dentry *debugfs_dir;
#endif
};

int gsa_send_cmd(struct device *dev, u32 cmd, u32 *req, u32 req_argc, u32 *rsp, u32 rsp_argc);

int gsa_send_simple_cmd(struct device *dev, u32 cmd);

int gsa_send_one_arg_cmd(struct device *dev, u32 cmd, u32 arg);

ssize_t gsa_get_gsa_version(struct device *gsa, char *buf);

int gsa_pm_stat_open(struct device *dev, struct gsa_dev_state *s, struct file *filp);
ssize_t gsa_pm_stat_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos);
int gsa_pm_state_release(struct inode *inode, struct file *filp);

int gsa_set_ipc_irq_handler(struct device *gsa,
			    void (*handler)(void *),
			    void *handler_args);

#endif /* __LINUX_GSA_PRIV_H */
