/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __SSCOREDUMP_H__
#define __SSCOREDUMP_H__

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/platform_data/sscoredump.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <uapi/misc/crashinfo.h>

/**
 * struct sscd_device - internal state container for sscd devices.
 * This state container holds most of the runtime variable data
 * for a sscd device.
 */
struct sscd_device {
	/**
	 * device related
	 * @node: common device list
	 * @rx_lock: protects access to structure
	 * @enabled: coredump enabled/disabled
	 * @opened: tracks active client
	 */
	struct device            dev;
	struct cdev              chrdev;
	struct device            *parent_dev;
	struct list_head         node;
	struct mutex             rx_lock; /* access to structure */

	bool                     enabled;
	atomic_t                 opened;
	u32                      read_timeout;
	wait_queue_head_t        read_wait_q;
	struct completion        read_completion;

	/**
	 * subsystem report data (includes extra segments for internal data)
	 * @report_active: active crash data
	 * @segs: crash segment data
	 * @nsegs: number of segments
	 * @read_offset: read offset within crash data
	 */
	atomic_t                 report_active;
	u32                      report_flags;
	struct crashinfo_img_hdr crash_hdr;
	struct sscd_segment      *segs;
	u16                      nsegs;
	loff_t                   read_offset;

	/**
	 * stats
	 */
	time64_t                 report_time;
	u64                      report_count; /* number of reports */
	u64                      read_count; /* number of read reports */
};

enum sscd_report_level {
	REPORT_CRASHINFO_ONLY = 0,
	REPORT_ALL
};

#if IS_ENABLED(CONFIG_KUNIT)
void free_report(struct sscd_device *sdev);
int create_report(struct sscd_device *sdev, struct sscd_segment *segs,
		  u16 nsegs, u64 flags, const char *crash_info);
ssize_t sdev_enabled_show(struct device *dev,
			  struct device_attribute *attr, char *buf);
ssize_t sdev_enabled_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count);
int sscoredump_probe(struct platform_device *pdev);
void sscoredump_remove(struct platform_device *pdev);
#endif

#endif /* __SSCOREDUMP_H__ */
