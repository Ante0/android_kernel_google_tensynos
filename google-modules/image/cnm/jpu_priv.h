/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC.
 *
 * Author: Anastasia Young <anastasiayoung@google.com>
 */

#ifndef _JPU_PRIV_H_
#define _JPU_PRIV_H_

#include <interconnect/google_icc_helper.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/pm_qos.h>
#include <linux/reset.h>

#define READ_JPU_REGISTER(core, addr)	readl((core)->base + (addr))
#define WRITE_JPU_REGISTER(core, addr, val) \
	writel((u32)(val), (core)->base + (addr))

struct jpu_dmabuf_list {
	struct list_head mappings;
	struct mutex lock;
};

struct jpu_devfreq {
	struct devfreq *df;
	struct dev_pm_qos_request qos_req;
};

enum jpu_power_status {
	POWER_OFF_RELEASED,
	POWER_ON,
	POWER_OFF_SLEEP,
};

struct jpu_core {
	struct class *_class;
	struct cdev cdev;
	struct device *svc_dev;
	struct device *dev;
	struct device *pd_dev;
	dev_t devno;
	int irq;
	struct jpu_devfreq dev_freq;
	struct google_icc_path *c3p_icc_path;
	struct google_icc_path *jpu_icc_path;
	struct clk *clock;
	unsigned int regs_size;
	phys_addr_t paddr;
	void __iomem *base;
	struct mutex lock;
	struct jpu_dmabuf_list dmabuf_list;
	wait_queue_head_t wq;
	struct kfifo intr_pending_q;
	spinlock_t kfifo_lock;
	enum jpu_power_status power_status;
	int inst_open_count;
	struct mutex inst_count_lock;
	struct reset_control *jpu_reset;
};

struct jpu_bandwidth_info {
	// read constraints in MBytes
	int read_avg_bw;
	int read_peak_bw;
	// write constraints in MBytes
	int write_avg_bw;
	int write_peak_bw;
};

#endif //_JPU_PRIV_H_
