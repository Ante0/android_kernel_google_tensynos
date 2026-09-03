/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform device driver header for GSLC.
 *
 * Copyright (C) 2021 Google LLC
 */
#ifndef __GSLC_PLATFORM_H__
#define __GSLC_PLATFORM_H__

#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <soc/google/pt.h>

#include <soc/google/goog_mba_q_xport.h>
#include <soc/google/goog_mba_cpm_iface.h>
#define MBOX_SERVICE_ID_GSLC 4

#define PT_PTID_MAX 64

struct gslc_cpm_mba {
	struct mutex mutex; /* CPM MBA struct mutex */
	struct cpm_iface_client *client;
	int remote_ch; /* Remote channel on CPM */
};

/* Debug info */
struct gslc_dbg {
	struct dentry *base_dir; /* Debugfs base directory */
	u32 reg_offset; /* Register address offset */
	u8 pid; /* Partition ID */
	u8 cfg; /* Partition configuration */
	u8 priority; /* Partition priority */
	u8 ovr_valid; /* Overrides valid */
	u32 size; /* Partition size in 1KB granularity */
	u32 overrides; /* Partition overrides */
};

struct gslc_pid_data {
	u32 size;
	u8 priority;
	int cfg;
	void *data;
	void (*resize)(void *data, size_t size);
};

struct gslc_dev {
	struct device *dev; /* platform bus device */
	void __iomem *csr_base; /* IO remapped CSR base */
	struct gslc_dbg *dbg; /* Debug related info */
	struct gslc_cpm_mba *cpm_mba; /* CPM mailbox data */
	struct gslc_pid_data pid_data[PT_PTID_MAX]; /* PID data */
	struct pt_driver *pt_driver; /* Pt driver handle */
	spinlock_t pid_lock; /* Spinlock to protect the PID data */
};

#endif /* __GSLC_PLATFORM_H__ */
