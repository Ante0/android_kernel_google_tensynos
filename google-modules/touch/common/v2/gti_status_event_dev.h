/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Google Touch Interface Status Event Device  for Pixel devices.
 *
 * Copyright 2026 Google LLC.
 */

#ifndef _GTI_STATUS_EVENT_DEV_H_
#define _GTI_STATUS_EVENT_DEV_H_

#include <linux/cdev.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include "uapi/input/gti_status_event.h"

/* Forward declaration */
struct goog_touch_interface;

#define GTI_STATUS_FIFO_SIZE 16

#define IS_POWER_OF_2(x) ((x & (x - 1)) == 0)

static_assert(IS_POWER_OF_2(GTI_STATUS_FIFO_SIZE), "The fifo size must be 2^N!");

struct gti_status_event_dev {
	struct cdev cdev;
	struct class *class;
	struct device *device;
	dev_t dev_num;

	struct kfifo fifo;
	wait_queue_head_t waitq;
	spinlock_t fifo_lock;

	atomic_t available;
};

struct gti_status_event {
	size_t size;
	u16 type;
	u8 data[118];
} __packed;

static_assert(IS_POWER_OF_2(sizeof(struct gti_status_event)), "Event struct size must be 2^N!");

int gti_status_send_invalid_gesture_event(struct gti_status_event_dev *dev,
					  struct gti_status_invalid_gesture_event *event);
int gti_status_event_probe(struct goog_touch_interface *gti);
void gti_status_event_remove(struct goog_touch_interface *gti);

#endif
