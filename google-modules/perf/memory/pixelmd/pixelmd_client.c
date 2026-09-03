// SPDX-License-Identifier: GPL-2.0

#include "pixelmd_client.h"

#include "pixelmd_api.h"
#include <linux/atomic.h>
#include <linux/cleanup.h>
#include <linux/compiler_attributes.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

struct pixelmd_clients pixelmd_clients = {
	.lock = __SPIN_LOCK_UNLOCKED(pixelmd_clients.lock),
	.list = LIST_HEAD_INIT(pixelmd_clients.list),
};
EXPORT_SYMBOL_GPL(pixelmd_clients);

struct pixelmd_client *__must_check pixelmd_client_create(void)
{
	int ret;
	struct pixelmd_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	ret = kfifo_alloc(&client->event_fifo, 8192, GFP_KERNEL);
	if (ret < 0)
		goto free_client;

	spin_lock_init(&client->event_write_lock);
	mutex_init(&client->event_read_lock);
	init_waitqueue_head(&client->event_wq);

	/*
	 * Add the client to the list. The lock is also taken by pixelmd_write_event(),
	 * which can be called from an atomic context (e.g. a vendor hook), so we need
	 * to disable IRQs to avoid a deadlock.
	 */
	scoped_guard(spinlock_irqsave, &pixelmd_clients.lock) {
		list_add_tail(&client->node, &pixelmd_clients.list);
	}

	return client;

free_client:
	kfree(client);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(pixelmd_client_create);

void pixelmd_client_destroy(struct pixelmd_client *client)
{
	int source;

	/*
	 * Remove the client from the list first, before freeing it. We're disabling
	 * IRQs to avoid self-deadlock in case pixelmd_write_event() interrupts us.
	 */
	scoped_guard(spinlock_irqsave, &pixelmd_clients.lock) {
		list_del(&client->node);
	}

	// Disable all sources for the client.
	for (source = 0; source != PIXELMD_NUM_SOURCES; source++)
		pixelmd_client_enable_source(client, source, false);

	kfifo_free(&client->event_fifo);
	kfree(client);
}
EXPORT_SYMBOL_GPL(pixelmd_client_destroy);

void pixelmd_client_enable_source(struct pixelmd_client *client, enum pixelmd_source source,
				  bool enable)
{
	/*
	 * We take the lock to make sure the 'pixelmd_clients.source_enable_counts'
	 * counter is accurate. We disable IRQs because this lock is also taken by
	 * pixelmd_write_event(), which can be called from an atomic context.
	 */
	scoped_guard(spinlock_irqsave, &pixelmd_clients.lock) {
		bool currently_enabled = test_bit(source, client->enabled_sources);

		if (enable && !currently_enabled) {
			atomic_inc(&pixelmd_clients.source_enable_counts[source]);
			set_bit(source, client->enabled_sources);
		} else if (!enable && currently_enabled) {
			clear_bit(source, client->enabled_sources);
			atomic_dec(&pixelmd_clients.source_enable_counts[source]);
		}
	}
}
EXPORT_SYMBOL_GPL(pixelmd_client_enable_source);

static int device_fop_open(struct inode *inode, struct file *file)
{
	struct pixelmd_client *client;

	client = pixelmd_client_create();
	if (IS_ERR(client))
		return PTR_ERR(client);

	file->private_data = client;

	return 0;
}

static int device_fop_release(struct inode *inode, struct file *file)
{
	struct pixelmd_client *client = file->private_data;

	file->private_data = NULL;

	pixelmd_client_destroy(client);

	return 0;
}

static ssize_t device_fop_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int ret;
	unsigned int copied;
	struct pixelmd_client *client = file->private_data;

	// We loop here until we have data in event_fifo (or an error occurs).
	while (true) {
		if (mutex_lock_interruptible(&client->event_read_lock))
			return -ERESTARTSYS;

		// Locked the mutex, break the loop if we have data.
		if (!kfifo_is_empty(&client->event_fifo))
			break;

		if (file->f_flags & O_NONBLOCK) {
			mutex_unlock(&client->event_read_lock);
			return -EAGAIN;
		}

		// We're going to wait for more data, unlock.
		mutex_unlock(&client->event_read_lock);

		ret = wait_event_interruptible(client->event_wq,
					       !kfifo_is_empty(&client->event_fifo));
		if (ret)
			return ret;
	}

	// At this point the mutex is locked, and event_fifo is not empty.
	ret = kfifo_to_user(&client->event_fifo, buf, count, &copied);

	mutex_unlock(&client->event_read_lock);
	return ret ? ret : copied;
}

static __poll_t device_fop_poll(struct file *file, poll_table *wait)
{
	struct pixelmd_client *client = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &client->event_wq, wait);

	if (!kfifo_is_empty(&client->event_fifo))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static long device_fop_ioctl(struct file *file, unsigned int type, unsigned long param)
{
	struct pixelmd_client *client = file->private_data;

	return pixelmd_client_ioctl(client, type, (void __user *)param);
}

const struct file_operations pixelmd_device_fops = {
	.owner = THIS_MODULE,
	.open = device_fop_open,
	.release = device_fop_release,
	.read = device_fop_read,
	.poll = device_fop_poll,
	.unlocked_ioctl = device_fop_ioctl,
};
