/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PIXELMD_CLIENT_H_
#define PIXELMD_CLIENT_H_

#include "pixelmd_api.h"

#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/kfifo.h>
#include <linux/bitmap.h>
#include <linux/list.h>

/*
 * Struct that's associated with each opened device fd.
 */
struct pixelmd_client {
	/* Guarded by 'pixelmd_clients.lock' */
	struct list_head node;

	/* Doesn't need a lock - operations are atomic */
	DECLARE_BITMAP(enabled_sources, PIXELMD_NUM_SOURCES);

	/* 'event_fifo' readers use 'event_read_lock', writers use 'event_write_lock' */
	struct kfifo event_fifo;
	spinlock_t event_write_lock;
	struct mutex event_read_lock;
	wait_queue_head_t event_wq;
};

/* Creates a new client. Returns ERR_PTR() on error. */
struct pixelmd_client *__must_check pixelmd_client_create(void);

/* Destroys the client. */
void pixelmd_client_destroy(struct pixelmd_client *client);

/* Marks the source as enabled (or disabled) according to the flag. */
void pixelmd_client_enable_source(struct pixelmd_client *client, enum pixelmd_source source,
				  bool enable);

struct pixelmd_clients {
	/*
	 * This lock is acquired by pixelmd_write_event(), which can be called
	 * from an atomic context (via a vendor hook). Thus everyone must make
	 * sure that interrupts are disabled while the lock is held.
	 */
	spinlock_t lock;
	struct list_head list;

	/* Tracks how many clients have a source enabled. */
	atomic_t source_enable_counts[PIXELMD_NUM_SOURCES];
};

extern struct pixelmd_clients pixelmd_clients;

extern const struct file_operations pixelmd_device_fops;

long pixelmd_client_ioctl(struct pixelmd_client *client, unsigned int cmd, void __user *param);

#endif /* PIXELMD_CLIENT_H_ */
