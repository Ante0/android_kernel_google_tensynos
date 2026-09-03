/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Vamsidhar Reddy Gaddam <gvamsi@google.com>
 */

#pragma once

#include <linux/kfifo.h>
#include <linux/workqueue_types.h>

struct pixel_gpu_device;

enum gpu_uevent_type {
	GPU_UEVENT_TYPE_NONE,
	GPU_UEVENT_TYPE_KMD_ERROR,
	GPU_UEVENT_TYPE_MAX
};


enum gpu_uevent_info {
	GPU_UEVENT_INFO_NONE,
	GPU_UEVENT_INFO_FW_PAGEFAULT,
	GPU_UEVENT_INFO_HOST_WDG_FW_ERROR,
	GPU_UEVENT_INFO_GUILTY_LOCKUP,
	GPU_UEVENT_INFO_MAX
};

struct gpu_uevent {
	enum gpu_uevent_type type;
	enum gpu_uevent_info info;
	uid_t uid;
};

struct gpu_uevent_ctx {
	unsigned long last_uevent_ts[GPU_UEVENT_TYPE_MAX];
	DECLARE_KFIFO_PTR(evts_fifo, struct gpu_uevent);
	spinlock_t lock;
	struct work_struct gpu_uevent_work;
};


void gpu_uevent_send(struct pixel_gpu_device *pixel_dev, const struct gpu_uevent *evt);
void gpu_uevent_kmd_error_send(struct pixel_gpu_device *pixel_dev, const enum gpu_uevent_info info);

void gpu_uevent_term(struct pixel_gpu_device *pixel_dev);
int gpu_uevent_init(struct pixel_gpu_device *pixel_dev);
