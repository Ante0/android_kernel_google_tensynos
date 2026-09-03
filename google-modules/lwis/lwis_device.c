// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Base Device Driver
 *
 * Copyright 2018 Google LLC.
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-dev: " fmt

#include "lwis_device.h"

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rculist.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include "lwis_buffer.h"
#include "lwis_bus_manager.h"
#include "lwis_clock.h"
#include "lwis_commands.h"
#include "lwis_allocator.h"
#include "lwis_debug.h"
#include "lwis_device.h"
#include "lwis_device_dpm.h"
#include "lwis_device_i2c_v2.h"
#include "lwis_device_i3c_proxy.h"
#include "lwis_device_pwrseq.h"
#include "lwis_device_slc.h"
#include "lwis_device_spi.h"
#include "lwis_device_test.h"
#include "lwis_device_top.h"
#include "lwis_dt.h"
#include "lwis_event.h"
#include "lwis_fence.h"
#include "lwis_gpio.h"
#include "lwis_ioctl.h"
#include "lwis_periodic_io.h"
#include "lwis_pinctrl.h"
#include "lwis_platform.h"
#include "lwis_pwrseq.h"
#include "lwis_trace.h"
#include "lwis_transaction.h"
#include "lwis_util.h"
#include "lwis_version.h"

#ifdef CONFIG_OF
#include "lwis_dt.h"
#endif

#define LWIS_CLASS_NAME "lwis"
#define LWIS_DEVICE_NAME "lwis"
#define LWIS_MAX_DEVICES (1U << MINORBITS)

/* Define this to help debug power sequence */
#undef LWIS_PWR_SEQ_DEBUG

bool lwis_reg_io_debug;
module_param(lwis_reg_io_debug, bool, 0644);
MODULE_PARM_DESC(lwis_reg_io_debug,
		 "Enable detailed register IO logging at runtime (default: false)");

/* Global declaration for core lwis structure */
static struct lwis_core core;

static int lwis_open(struct inode *node, struct file *fp);
static int lwis_release(struct inode *node, struct file *fp);
static long lwis_ioctl(struct file *fp, unsigned int type, unsigned long param);
static unsigned int lwis_poll(struct file *fp, poll_table *wait);
static ssize_t lwis_read(struct file *fp, char __user *user_buf, size_t count, loff_t *pos);

static const struct file_operations lwis_fops = {
	.owner = THIS_MODULE,
	.open = lwis_open,
	.release = lwis_release,
	.unlocked_ioctl = lwis_ioctl,
	.poll = lwis_poll,
	.read = lwis_read,
};

static void free_otp_settings(struct lwis_otp_config *otp_config)
{
	kfree(otp_config->settings);
	otp_config->settings = NULL;
	otp_config->setting_count = 0;
}

/*
 * transaction_work_func:
 * Function to be called by transaction worker thread to direct it the correct client
 * to process their queues
 */
static void transaction_work_func(struct kthread_work *work)
{
	struct lwis_client *client = container_of(work, struct lwis_client, transaction_work);

	lwis_process_worker_queue(client);
}

static void io_bus_process_work_func(struct kthread_work *work)
{
	/* io_bus_work stores the context of the lwis_client submitting the transfer request */
	struct lwis_client *client = container_of(work, struct lwis_client, io_bus_work);

	lwis_bus_manager_process_worker_queue(client);
}

/*
 *  lwis_open: Opening an instance of a LWIS device
 */
static int lwis_open(struct inode *node, struct file *fp)
{
	struct lwis_device *lwis_dev;
	struct lwis_client *lwis_client;
	unsigned long flags;

	/* Making sure the minor number associated with fp exists */
	mutex_lock(&core.lock);
	lwis_dev = idr_find(core.idr, iminor(node));
	mutex_unlock(&core.lock);
	if (!lwis_dev) {
		pr_err("No device %d found\n", iminor(node));
		return -ENODEV;
	}
	dev_info(lwis_dev->dev, "Opening instance %d\n", iminor(node));

	lwis_client = kzalloc(sizeof(struct lwis_client), GFP_KERNEL);
	if (!lwis_client)
		return -ENOMEM;

	lwis_client->lwis_dev = lwis_dev;
	kref_init(&lwis_client->kref);
	/* Initialize locks */
	mutex_init(&lwis_client->lock);
	spin_lock_init(&lwis_client->periodic_io_lock);
	spin_lock_init(&lwis_client->event_lock);
	spin_lock_init(&lwis_client->flush_lock);

	/* Empty hash table for client event states */
	hash_init(lwis_client->event_states);

	/* The event queue itself is a linked list */
	INIT_LIST_HEAD(&lwis_client->event_queue);
	INIT_LIST_HEAD(&lwis_client->error_event_queue);

	/* Initialize the wait queue for the event queue */
	init_waitqueue_head(&lwis_client->event_wait_queue);

	/* Initialize the release fences list */
	INIT_LIST_HEAD(&lwis_client->release_fences);

	/* Empty hash table for client allocated buffers */
	hash_init(lwis_client->allocated_buffers);

	/* Empty hash table for client enrolled buffers */
	hash_init(lwis_client->enrolled_buffers);

	/* Initialize the allocator */
	lwis_allocator_init(lwis_client->lwis_dev);

	kthread_init_work(&lwis_client->transaction_work, transaction_work_func);
	kthread_init_work(&lwis_client->io_bus_work, io_bus_process_work_func);

	/* Start transaction processor task */
	lwis_transaction_init(lwis_client);

	/* Start periodic io processor task */
	lwis_periodic_io_init(lwis_client);

	memset(&lwis_client->debug_info, 0, sizeof(lwis_client->debug_info));

	spin_lock_irqsave(&lwis_dev->lock, flags);
	list_add_rcu(&lwis_client->node, &lwis_dev->clients);
	spin_unlock_irqrestore(&lwis_dev->lock, flags);

	/* Storing the client handle in fp private_data for easy access */
	fp->private_data = lwis_client;

	spin_lock_irqsave(&lwis_client->flush_lock, flags);
	lwis_client->flush_state = NOT_FLUSHING;
	spin_unlock_irqrestore(&lwis_client->flush_lock, flags);

	if (lwis_bus_manager_connect_client(lwis_client)) {
		dev_err(lwis_dev->dev, "Failed to connect lwis client to bus manager\n");
		spin_lock_irqsave(&lwis_dev->lock, flags);
		list_del_rcu(&lwis_client->node);
		spin_unlock_irqrestore(&lwis_dev->lock, flags);
		lwis_client_put(lwis_client);
		return -EINVAL;
	}

	lwis_client->is_enabled = false;
	lwis_client->is_suspended = false;
	return 0;
}

/* Cleans client contents to a reset state. This will hold the LWIS client mutex. */
static int cleanup_client(struct lwis_client *lwis_client)
{
	unsigned long flags;

	/* Let's lock the mutex while we're cleaning up to avoid other parts
	 * of the code from acquiring dangling pointers to the client or any
	 * of the client event states that are about to be freed
	 */
	mutex_lock(&lwis_client->lock);

	/* Set flush_state early to block new transactions and events */
	spin_lock_irqsave(&lwis_client->flush_lock, flags);
	lwis_client->flush_state = FLUSHING;
	spin_unlock_irqrestore(&lwis_client->flush_lock, flags);

	/* Clear event states for this client */
	lwis_client_event_states_clear(lwis_client);

	/* Clear the event queues */
	lwis_client_event_queue_clear(lwis_client);
	lwis_client_error_event_queue_clear(lwis_client);

	/* Clean up all periodic io state for the client */
	lwis_periodic_io_client_cleanup(lwis_client);

	mutex_unlock(&lwis_client->lock);
	lwis_transaction_client_flush(lwis_client);
	mutex_lock(&lwis_client->lock);

	/* Cancel all pending transactions for the client and destory workqueue*/
	lwis_transaction_clear(lwis_client);

	/* Run cleanup transactions. */
	lwis_transaction_client_cleanup(lwis_client);

	/* Disenroll and clear the table of allocated and enrolled buffers */
	lwis_client_allocated_buffers_clear(lwis_client);
	lwis_client_enrolled_buffers_clear(lwis_client);

	mutex_unlock(&lwis_client->lock);

	return 0;
}

/* Calling this requires holding lwis_dev->lock. */
static inline bool check_client_exists(const struct lwis_device *lwis_dev,
				       const struct lwis_client *lwis_client)
{
	struct lwis_client *p;

	/*
	 * This function is called with lwis_dev->lock held. Since we hold the
	 * writer lock, the list cannot change under us, so it is safe to use
	 * the standard list_for_each_entry() iterator even though the list is
	 * RCU-protected.
	 */
	list_for_each_entry(p, &lwis_dev->clients, node) {
		if (lwis_client == p)
			return true;
	}
	return false;
}

static void lwis_client_release(struct kref *kref)
{
	struct lwis_client *client = container_of(kref, struct lwis_client, kref);

	kfree_rcu(client, rcu);
}

struct lwis_client *lwis_client_get(struct lwis_client *client)
{
	if (client)
		kref_get(&client->kref);
	return client;
}

void lwis_client_put(struct lwis_client *client)
{
	if (client)
		kref_put(&client->kref, lwis_client_release);
}

/* Release client and deletes its entry from the device's client list,
 * this assumes that LWIS device still exists and will hold LWIS device
 * and LWIS client locks.
 */
static int release_client(struct lwis_client *lwis_client)
{
	struct lwis_device *lwis_dev = lwis_client->lwis_dev;
	struct lwis_fence_pending_signal *pending_fence, *temp;
	int rc = 0;
	unsigned long flags;

	/* Take this lwis_client off the list of active clients first */
	spin_lock_irqsave(&lwis_dev->lock, flags);
	if (check_client_exists(lwis_dev, lwis_client)) {
		list_del_rcu(&lwis_client->node);
	} else {
		dev_err(lwis_dev->dev,
			"Trying to release a client tied to this device, but the entry was not found on the clients list.");
	}
	spin_unlock_irqrestore(&lwis_dev->lock, flags);

	/* Ensure all RCU readers (e.g. interrupt handlers) have finished accessing this client */
	synchronize_rcu();

	/* Signal release fences */
	spin_lock_irqsave(&lwis_client->flush_lock, flags);
	list_for_each_entry_safe(pending_fence, temp, &lwis_client->release_fences, node) {
		lwis_dma_fence_signal_with_status(pending_fence->fence, 0);
		dma_fence_put(pending_fence->fence);
		list_del(&pending_fence->node);
		lwis_allocator_free(lwis_client->lwis_dev, pending_fence);
	}
	spin_unlock_irqrestore(&lwis_client->flush_lock, flags);

	rc = cleanup_client(lwis_client);
	if (rc)
		return rc;

	lwis_bus_manager_disconnect_client(lwis_client);

	/*
	 * It is safe to destroy the top device worker thread when top
	 * client is released since the top device worker doesn't need
	 * to exist by default.
	 */
	if (lwis_client->lwis_dev->type == DEVICE_TYPE_TOP)
		lwis_stop_top_device_worker(lwis_client);

	lwis_client_put(lwis_client);

	return 0;
}

/*
 *  lwis_release: Closing an instance of a LWIS device
 */
static int lwis_release(struct inode *node, struct file *fp)
{
	struct lwis_client *lwis_client = fp->private_data;
	struct lwis_device *lwis_dev = lwis_client->lwis_dev;
	struct lwis_i2c_device *i2c_dev = NULL;
	int rc = 0;
	int __maybe_unused i;
	bool is_client_enabled = lwis_client->is_enabled;
	bool is_client_suspended = READ_ONCE(lwis_client->is_suspended);

	dev_info(lwis_dev->dev, "Closing instance %d\n", iminor(node));

	rc = release_client(lwis_client);

	mutex_lock(&lwis_dev->client_lock);

	/* Update suspend count based on the released client's state */
	if (is_client_suspended) {
		if (lwis_dev->suspend_count > 0) {
			lwis_dev->suspend_count--;
			dev_dbg(lwis_dev->dev, "Client was suspended, suspend_count now %d\n",
				lwis_dev->suspend_count);
		} else
			dev_warn(lwis_dev->dev,
				 "Client suspended but device suspend_count is 0!\n");
	}

	/*
	 * Handle the power_up_to_suspend case: suspend if not already suspended.
	 * lwis_dev->enabled - lwis_dev->suspend_count represents the total
	 * number of awake clients. If this value is 1, it means the client
	 * currently being released is the last awake client. We should only
	 * execute the hardware suspend sequence if no other awake clients exist.
	 */
	if (lwis_dev->power_up_to_suspend && is_client_enabled && !is_client_suspended &&
	    lwis_dev->enabled - lwis_dev->suspend_count == 1) {
		if (lwis_dev->type == DEVICE_TYPE_I2C)
			i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);

		if (lwis_dev->vops.device_suspend) {
			lwis_bus_manager_lock_bus(lwis_dev);
			rc = lwis_dev->vops.device_suspend(lwis_dev);
			lwis_bus_manager_unlock_bus(lwis_dev);
			if (rc)
				dev_err(lwis_dev->dev, "vops.device_suspend failed: %d\n", rc);
		}
		if (i2c_dev)
			mutex_lock(i2c_dev->group_i2c_lock);
		rc = lwis_dev_process_power_sequence(lwis_dev, lwis_dev->suspend_sequence,
						     /*set_active=*/false,
						     /*skip_error=*/false);
		if (i2c_dev)
			mutex_unlock(i2c_dev->group_i2c_lock);

		if (rc)
			dev_err(lwis_dev->dev, "Suspend sequence failed: %d\n", rc);
	}

	/* Update enabled count based on the released client's state */
	if (is_client_enabled) {
		if (lwis_dev->enabled > 0) {
			lwis_dev->enabled--;
			dev_dbg(lwis_dev->dev, "Client was enabled, enabled count now %d\n",
				lwis_dev->enabled);
		} else
			dev_warn(lwis_dev->dev, "Client enabled but device enabled count is 0!\n");
	}

	if (is_client_enabled && lwis_dev->enabled == 0) {
		dev_info(lwis_dev->dev, "Last client closed, powering down.\n");
		lwis_debug_crash_info_dump(lwis_dev);

		for (i = 0; i < lwis_dev->bts_block_num; i++) {
			if (lwis_dev->bts_indexes[i] != BTS_UNSUPPORTED) {
				lwis_platform_update_bts(lwis_dev, i, /*bw_peak=*/0,
							 /*bw_read=*/0, /*bw_write=*/0,
							 /*bw_rt=*/0);
			}
		}
		/* remove voted qos */
		lwis_platform_remove_qos(lwis_dev);
		/* Release device event states if no more client is using */
		lwis_device_event_states_clear(lwis_dev);

		rc = lwis_dev_power_down_locked(lwis_dev, /*error_handling=*/false);
		if (rc)
			dev_err(lwis_dev->dev, "Power down failed: %d\n", rc);
	}

	/* Call device type specific close routines. */
	if (is_client_enabled && lwis_dev->enabled == 0) {
		if (lwis_dev->vops.close != NULL)
			lwis_dev->vops.close(lwis_dev);
	}

	mutex_unlock(&lwis_dev->client_lock);

	lwis_allocator_release(lwis_dev);

	return rc;
}

/*
 *  lwis_ioctl: I/O control function on a LWIS device
 *
 *  List of IOCTL types are defined in lwis_commands.h
 */
static long lwis_ioctl(struct file *fp, unsigned int type, unsigned long param)
{
	struct lwis_client *lwis_client;
	struct lwis_device *lwis_dev;

	lwis_client = fp->private_data;
	if (!lwis_client) {
		pr_err("Cannot find client instance\n");
		return -ENODEV;
	}

	lwis_dev = lwis_client->lwis_dev;
	if (!lwis_dev) {
		pr_err("Cannot find device instance\n");
		return -ENODEV;
	}

	return lwis_ioctl_handler(lwis_client, type, param);
}
/*
 *  lwis_poll: Event queue status function of LWIS
 *
 */
static unsigned int lwis_poll(struct file *fp, poll_table *wait)
{
	unsigned int mask = 0;
	struct lwis_client *lwis_client;

	lwis_client = fp->private_data;
	if (!lwis_client) {
		pr_err("Cannot find client instance\n");
		return POLLERR;
	}

	/* Add our wait queue to the poll table */
	poll_wait(fp, &lwis_client->event_wait_queue, wait);

	/* Check if we have anything in the event lists */
	if (lwis_client_error_event_peek_front(lwis_client, NULL) == 0)
		mask |= POLLERR;
	else if (lwis_client_event_peek_front(lwis_client, NULL) == 0)
		mask |= POLLIN;

	return mask;
}

static ssize_t lwis_read(struct file *fp, char __user *user_buf, size_t count, loff_t *pos)
{
	int ret = 0;
	/* Buffer to store information */
	const size_t buffer_size = 8192;
	char *buffer = kzalloc(buffer_size, GFP_KERNEL);

	if (!buffer)
		return -ENOMEM;

	lwis_get_feature_flags(buffer, buffer_size);

	ret = simple_read_from_buffer(user_buf, count, pos, buffer, strlen(buffer));

	kfree(buffer);

	return ret;
}

static int base_setup(struct lwis_device *lwis_dev)
{
	int ret = 0;

#ifdef CONFIG_OF
	/* Parse device tree for device configurations */
	ret = lwis_base_parse_dt(lwis_dev);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			pr_err("Failed to parse device tree\n");
	}
#else
	/* Non-device-tree init: Save for future implementation */
	ret = -EINVAL;
#endif

	return ret;
}

/*
 *  assign_top_to_other: Assign top device to the devices probed before.
 */
static void assign_top_to_other(struct lwis_device *top_dev)
{
	struct lwis_device *lwis_dev;

	mutex_lock(&core.lock);
	list_for_each_entry(lwis_dev, &core.lwis_dev_list, dev_list) {
		lwis_dev->top_dev = top_dev;
	}
	mutex_unlock(&core.lock);
}

static bool need_to_power_up(struct lwis_device *lwis_dev)
{
	int i;

	if (lwis_dev->power_seq_handler == NULL)
		return true;

	mutex_lock(&core.lock);
	for (i = 0; i < MAX_UNIFIED_POWER_DEVICE; i++) {
		if (core.unified_dev_pwr_map[i].dev_node_seq == NULL)
			break;

		if (core.unified_dev_pwr_map[i].dev_node_seq == lwis_dev->power_seq_handler) {
			if (core.unified_dev_pwr_map[i].count == 0)
				break;

			mutex_unlock(&core.lock);
#ifdef LWIS_PWR_SEQ_DEBUG
			dev_info(lwis_dev->dev, "%s: Already power up\n", __func__);
#endif
			return false;
		}
	}
	mutex_unlock(&core.lock);
#ifdef LWIS_PWR_SEQ_DEBUG
	dev_info(lwis_dev->dev, "%s: Need power up\n", __func__);
#endif
	return true;
}

static bool need_to_power_down(struct lwis_device *lwis_dev)
{
	int i;

	if (lwis_dev->power_seq_handler == NULL)
		return true;

	mutex_lock(&core.lock);
	for (i = 0; i < MAX_UNIFIED_POWER_DEVICE; i++) {
		if (core.unified_dev_pwr_map[i].dev_node_seq == NULL)
			break;

		if (core.unified_dev_pwr_map[i].dev_node_seq == lwis_dev->power_seq_handler) {
			if (core.unified_dev_pwr_map[i].count == 1)
				break;

			mutex_unlock(&core.lock);
#ifdef LWIS_PWR_SEQ_DEBUG
			dev_info(lwis_dev->dev, "%s: No need power down\n", __func__);
#endif
			return false;
		}
	}
	mutex_unlock(&core.lock);
#ifdef LWIS_PWR_SEQ_DEBUG
	dev_info(lwis_dev->dev, "%s: Ready to power down\n", __func__);
#endif
	return true;
}

static int increase_unified_power_count(struct lwis_device *lwis_dev)
{
	int i;

	if (lwis_dev->power_seq_handler == NULL)
		return 0;

	mutex_lock(&core.lock);
	for (i = 0; i < MAX_UNIFIED_POWER_DEVICE; i++) {
		if (core.unified_dev_pwr_map[i].dev_node_seq == NULL)
			break;

		if (core.unified_dev_pwr_map[i].dev_node_seq == lwis_dev->power_seq_handler) {
			core.unified_dev_pwr_map[i].count++;
			if (core.unified_dev_pwr_map[i].count == 1)
				core.unified_dev_pwr_map[i].hold_dev = lwis_dev;
			mutex_unlock(&core.lock);
#ifdef LWIS_PWR_SEQ_DEBUG
			dev_info(lwis_dev->dev, "%s: power counter = %d\n", __func__,
				 core.unified_dev_pwr_map[i].count);
#endif
			return 0;
		}
	}
	if (i >= MAX_UNIFIED_POWER_DEVICE) {
		dev_err(lwis_dev->dev, "Unified power sequence map overflow\n");
		mutex_unlock(&core.lock);
		return -EOVERFLOW;
	}

	core.unified_dev_pwr_map[i].dev_node_seq = lwis_dev->power_seq_handler;
	core.unified_dev_pwr_map[i].hold_dev = lwis_dev;
	core.unified_dev_pwr_map[i].count++;
	mutex_unlock(&core.lock);

#ifdef LWIS_PWR_SEQ_DEBUG
	dev_info(lwis_dev->dev, "%s: power counter = %d\n", __func__,
		 core.unified_dev_pwr_map[i].count);
#endif
	return 0;
}

static int decrease_unified_power_count(struct lwis_device *lwis_dev)
{
	int i;

	if (lwis_dev->power_seq_handler == NULL)
		return 0;

	mutex_lock(&core.lock);
	for (i = 0; i < MAX_UNIFIED_POWER_DEVICE; i++) {
		if (core.unified_dev_pwr_map[i].dev_node_seq == NULL)
			break;

		if (core.unified_dev_pwr_map[i].dev_node_seq == lwis_dev->power_seq_handler) {
			if (core.unified_dev_pwr_map[i].count > 0) {
				core.unified_dev_pwr_map[i].count--;
				if (core.unified_dev_pwr_map[i].count == 0)
					core.unified_dev_pwr_map[i].hold_dev = NULL;
			}
			mutex_unlock(&core.lock);
#ifdef LWIS_PWR_SEQ_DEBUG
			dev_info(lwis_dev->dev, "%s: power counter = %d\n", __func__,
				 core.unified_dev_pwr_map[i].count);
#endif
			return 0;
		}
	}
	mutex_unlock(&core.lock);
	dev_err(lwis_dev->dev, "Unified power sequence not found\n");
	return -ENODEV;
}

static struct lwis_device *get_power_down_dev_locked(struct lwis_device *lwis_dev)
{
	int i;

	if (lwis_dev->power_seq_handler == NULL)
		return lwis_dev;

	for (i = 0; i < MAX_UNIFIED_POWER_DEVICE; i++) {
		if (core.unified_dev_pwr_map[i].dev_node_seq == NULL)
			break;

		if (core.unified_dev_pwr_map[i].dev_node_seq == lwis_dev->power_seq_handler) {
#ifdef LWIS_PWR_SEQ_DEBUG
			dev_info(lwis_dev->dev, "%s: power dev = %s\n", __func__,
				 core.unified_dev_pwr_map[i].hold_dev->name);
#endif
			return core.unified_dev_pwr_map[i].hold_dev;
		}
	}
	if (i >= MAX_UNIFIED_POWER_DEVICE) {
		dev_err(lwis_dev->dev, "Unified power sequence not found\n");
		return lwis_dev;
	}

	return lwis_dev;
}

static bool is_transaction_worker_active(struct lwis_client *client)
{
	struct lwis_device *lwis_dev;
	struct lwis_top_device *top_dev;

	if (!client->lwis_dev->transaction_worker_thread)
		return false;

	/*
	 * Return true for all device types except Top device since the worker
	 * thread will be active till the device exists.
	 */
	if (client->lwis_dev->type != DEVICE_TYPE_TOP)
		return true;

	/*
	 * For top device, the thread is scheduled and runs only if a transaction is submitted
	 * on the top device. Once the usecase is finished and the client is destroyed, the thread
	 * is stopped. In this scenario, we want to avoid subsequent usecases from flushing an
	 * inactive thread in order to avoid infinite wait or failure due to timeouts.
	 */
	lwis_dev = client->lwis_dev;
	top_dev = container_of(lwis_dev, struct lwis_top_device, base_dev);
	return top_dev->transaction_worker_active;
}

void lwis_queue_device_worker(struct lwis_client *client)
{
	if (client->lwis_dev->bus_manager) {
		kthread_queue_work(&client->lwis_dev->bus_manager->bus_worker,
				   &client->io_bus_work);
	} else {
		if (is_transaction_worker_active(client)) {
			kthread_queue_work(&client->lwis_dev->transaction_worker,
					   &client->transaction_work);
		}
	}
}

void lwis_flush_device_worker(struct lwis_client *client)
{
	if (client->lwis_dev->bus_manager) {
		lwis_bus_manager_flush_worker(client->lwis_dev);
	} else {
		if (is_transaction_worker_active(client))
			kthread_flush_worker(&client->lwis_dev->transaction_worker);
	}
}

static bool handle_deactivate_shared_gpio(struct lwis_device *lwis_dev, char *gpio_name,
					  struct lwis_gpios_info *gpios_info)
{
	struct lwis_device *lwis_dev_it;

	/* Look up if gpio is already acquired by another device */
	mutex_lock(&core.lock);
	list_for_each_entry(lwis_dev_it, &core.lwis_dev_list, dev_list) {
		if ((lwis_dev->id != lwis_dev_it->id) && lwis_dev_it->enabled) {
			struct lwis_device *power_dev = get_power_down_dev_locked(lwis_dev_it);
			struct lwis_gpios_info *gpios_info_it =
				lwis_gpios_get_info_by_name(&power_dev->gpios_list, gpio_name);

			if (IS_ERR_OR_NULL(gpios_info_it))
				continue;

			if (gpios_info_it->id == gpios_info->id && gpios_info_it->gpios == NULL) {
				dev_info(lwis_dev->dev, "Handover shared GPIO to %s\n",
					 power_dev->name);
				gpios_info_it->gpios = gpios_info->gpios;
				gpios_info_it->hold_dev = gpios_info->hold_dev;
				gpios_info->gpios = NULL;
				mutex_unlock(&core.lock);
				return true;
			}
		}
	}
	mutex_unlock(&core.lock);

	return false;
}

static int handle_regulator_state(struct lwis_device *lwis_dev,
				  struct lwis_device_power_sequence_info *seq_info, bool set_active)
{
	int ret;

	if (set_active)
		ret = lwis_regulator_enable(&lwis_dev->regulator_list, seq_info->name);
	else
		ret = lwis_regulator_disable(&lwis_dev->regulator_list, seq_info->name);
	if (ret)
		dev_err(lwis_dev->dev, "Error set regulators (%d)\n", ret);

	return ret;
}

static int handle_pwrseq_state(struct lwis_device *lwis_dev,
			       struct lwis_device_power_sequence_info *seq_info, bool set_active)
{
	int ret;

	if (set_active)
		ret = lwis_pwrseq_enable(&lwis_dev->pwrseq_list, seq_info->name);
	else
		ret = lwis_pwrseq_disable(&lwis_dev->pwrseq_list, seq_info->name);
	if (ret)
		dev_err(lwis_dev->dev, "Error set pwrseqs %s (%d)\n", seq_info->name, ret);

	return ret;
}

static int activate_gpio(struct lwis_device *lwis_dev,
			 struct lwis_device_power_sequence_info *seq_info,
			 struct lwis_gpios_info *gpios_info)
{
	struct gpio_descs *gpios;
	int ret;

	gpios = lwis_gpio_list_get(lwis_dev->k_dev, seq_info->name);
	if (IS_ERR_OR_NULL(gpios)) {
		gpios_info->gpios = NULL;
		ret = PTR_ERR(gpios);
		if (ret == -EBUSY && gpios_info->is_shared) {
			dev_warn(lwis_dev->dev, "Shared gpios %s requested by another device\n",
				 seq_info->name);
			return 0;
		}
		dev_err(lwis_dev->dev, "Failed to obtain gpio list (%d)\n", ret);
		return ret;
	}

	gpios_info->hold_dev = lwis_dev->k_dev;
	gpios_info->gpios = gpios;

	if (gpios_info->is_pulse) {
		ret = lwis_gpio_list_set_output_value(gpios_info->gpios, 0);
		if (ret)
			goto error_gpio;

		usleep_range(1000, 1500);
	}

	ret = lwis_gpio_list_set_output_value(gpios_info->gpios, 1);
error_gpio:
	if (ret)
		dev_err(lwis_dev->dev, "Error activating GPIO pins (%d)\n", ret);

	return ret;
}

static int deactivate_gpio(struct lwis_device *lwis_dev,
			   struct lwis_device_power_sequence_info *seq_info,
			   struct lwis_gpios_info *gpios_info)
{
	int ret;

	if (gpios_info->gpios == NULL) {
		if (gpios_info->is_shared)
			return 0;

		dev_err(lwis_dev->dev, "No %s gpios defined\n", seq_info->name);
		return -ENODEV;
	}

	if (gpios_info->is_shared &&
	    handle_deactivate_shared_gpio(lwis_dev, seq_info->name, gpios_info))
		return 0;

	if (gpios_info->is_pulse) {
		ret = lwis_gpio_list_set_output_value(gpios_info->gpios, 1);
		if (ret)
			goto error_gpio;

		usleep_range(1000, 1500);
	}

	ret = lwis_gpio_list_set_output_value(gpios_info->gpios, 0);
error_gpio:
	if (ret)
		dev_err(lwis_dev->dev, "Error deactivating GPIO pins (%d)\n", ret);

	/* Release "ownership" of the GPIO pins */
	lwis_gpio_list_put(gpios_info->gpios, gpios_info->hold_dev);
	gpios_info->gpios = NULL;
	return ret;
}

static int handle_gpio_state(struct lwis_device *lwis_dev,
			     struct lwis_device_power_sequence_info *seq_info, bool set_active)
{
	struct lwis_gpios_info *gpios_info;

	gpios_info = lwis_gpios_get_info_by_name(&lwis_dev->gpios_list, seq_info->name);
	if (IS_ERR_OR_NULL(gpios_info)) {
		dev_err(lwis_dev->dev, "Get %s gpios info failed\n", seq_info->name);
		return PTR_ERR(gpios_info);
	}

	if (set_active)
		return activate_gpio(lwis_dev, seq_info, gpios_info);

	return deactivate_gpio(lwis_dev, seq_info, gpios_info);
}

static int handle_controller_state(struct lwis_device *lwis_dev,
				   struct lwis_device_power_sequence_info *seq_info,
				   bool set_active)
{
	int ret;

	if (lwis_dev->controller_dev == NULL || !pm_runtime_enabled(lwis_dev->controller_dev)) {
		dev_err(lwis_dev->dev, "Controller pm_runtime not enabled.\n");
		return 0;
	}

	if (set_active)
		ret = pm_runtime_resume_and_get(lwis_dev->controller_dev);
	else
		ret = pm_runtime_put_sync_suspend(lwis_dev->controller_dev);
	if (ret) {
		if (ret == -EAGAIN) {
			dev_warn(
				lwis_dev->dev,
				"Error in suspending master device power. Usage counter is not zero.\n");
			return 0;
		}
		dev_err(lwis_dev->dev, "Error in controller_dev runtime suspend/resume (%d)\n",
			ret);
	}

	return ret;
}

int lwis_dev_process_power_sequence(struct lwis_device *lwis_dev,
				    struct lwis_device_power_sequence_list *list, bool set_active,
				    bool skip_error)
{
	int ret = 0;
	int last_error = 0;
	int i;

	if (lwis_dev == NULL) {
		pr_err("lwis_dev is NULL\n");
		return -ENODEV;
	}

	if (list == NULL || list->count == 0) {
		dev_err(lwis_dev->dev, "No power_up_sequence defined\n");
		return -EINVAL;
	}

	for (i = 0; i < list->count; ++i) {
#ifdef LWIS_PWR_SEQ_DEBUG
		dev_info(lwis_dev->dev, "%s: %d - type:%s name:%s delay_us:%d", __func__, i,
			 list->seq_info[i].type, list->seq_info[i].name,
			 list->seq_info[i].delay_us);
#endif
		if (strcmp(list->seq_info[i].type, "regulator") == 0)
			ret = handle_regulator_state(lwis_dev, &list->seq_info[i], set_active);
		else if (strcmp(list->seq_info[i].type, "gpio") == 0)
			ret = handle_gpio_state(lwis_dev, &list->seq_info[i], set_active);
		else if (strcmp(list->seq_info[i].type, "pinctrl") == 0)
			ret = lwis_pinctrl_set_state(lwis_dev, list->seq_info[i].name);
		else if (strcmp(list->seq_info[i].type, "controller") == 0)
			ret = handle_controller_state(lwis_dev, &list->seq_info[i], set_active);
		else if (strcmp(list->seq_info[i].type, "regulator_mode") == 0)
			ret = lwis_regulator_set_mode(&lwis_dev->regulator_list,
						      list->seq_info[i].name);
		else if (strcmp(list->seq_info[i].type, "pwrseq") == 0)
			ret = handle_pwrseq_state(lwis_dev, &list->seq_info[i], set_active);

		if (ret) {
			if (!skip_error)
				return ret;
			last_error = ret;
		}

		usleep_range(list->seq_info[i].delay_us, list->seq_info[i].delay_us);
	}

	if (last_error)
		return last_error;

	return ret;
}

/*
 * Power up a LWIS device, should be called when lwis_dev->enabled is 0
 * lwis_dev->client_lock should be held before this function.
 */
int lwis_dev_power_up_locked(struct lwis_device *lwis_dev)
{
	int ret;
	struct lwis_i2c_device *i2c_dev = NULL;

	if (lwis_dev->type == DEVICE_TYPE_I2C) {
		i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);
		if (i2c_dev->group_i2c_lock == NULL) {
			dev_err(lwis_dev->dev, "group_i2c_lock is NULL. Abort power up.\n");
			return -EINVAL;
		}
	}

	/* Let's do the platform-specific enable call */
	ret = lwis_platform_device_enable(lwis_dev);
	if (ret) {
		dev_err(lwis_dev->dev, "Platform-specific device enable fail: %d\n", ret);
		goto error_power_up;
	}

	if (lwis_dev->clocks) {
		/* Enable clocks */
		ret = lwis_clock_enable_all(lwis_dev->clocks);
		if (ret) {
			dev_err(lwis_dev->dev, "Error enabling clocks (%d)\n", ret);
			goto error_power_up;
		}
	}

	if (lwis_dev->type == DEVICE_TYPE_I2C)
		mutex_lock(i2c_dev->group_i2c_lock);

	if (lwis_dev->power_up_sequence) {
		if (need_to_power_up(lwis_dev)) {
			LWIS_ATRACE_FUNC_BEGIN(lwis_dev, "lwis_dev_process_power_sequence");
			ret = lwis_dev_process_power_sequence(lwis_dev, lwis_dev->power_up_sequence,
							      /*set_active=*/true,
							      /*skip_error=*/false);
			LWIS_ATRACE_FUNC_END(lwis_dev, "lwis_dev_process_power_sequence");
			if (ret) {
				dev_err(lwis_dev->dev,
					"Error lwis_dev_process_power_sequence (%d)\n", ret);
				if (lwis_dev->type == DEVICE_TYPE_I2C)
					mutex_unlock(i2c_dev->group_i2c_lock);
				goto error_power_up;
			}
		}
		increase_unified_power_count(lwis_dev);
	} else if (lwis_dev->type == DEVICE_TYPE_I2C) {
		dev_err(lwis_dev->dev, "No power up sequence defined!\n");
		ret = -EINVAL;
		mutex_unlock(i2c_dev->group_i2c_lock);
		goto error_power_up;
	}
	if (lwis_dev->type == DEVICE_TYPE_I2C)
		mutex_unlock(i2c_dev->group_i2c_lock);

	if (lwis_dev->phys) {
		/* Power on the PHY */
		ret = lwis_phy_set_power_all(lwis_dev->phys,
					     /* power_on = */ true);
		if (ret) {
			dev_err(lwis_dev->dev, "Error powering on PHY\n");
			goto error_power_up;
		}
	}

	if (lwis_dev->vops.device_enable) {
		ret = lwis_dev->vops.device_enable(lwis_dev);
		if (ret) {
			dev_err(lwis_dev->dev, "Error executing device enable function\n");
			goto error_power_up;
		}
	}

	lwis_platform_set_device_state(lwis_dev, /*camera_up=*/true);
	/* Set initial suspend state based on DT */
	lwis_dev->suspend_count = lwis_dev->power_up_to_suspend ? 1 : 0;
	dev_dbg(lwis_dev->dev, "power up, suspend_count now %d\n", lwis_dev->suspend_count);
	return 0;

	/* Error handling */
error_power_up:
	lwis_dev_power_down_locked(lwis_dev, /*error_handling=*/true);
	return ret;
}

/*
 * Power down a LWIS device, should be called when lwis_dev->enabled become 0
 * lwis_dev->client_lock should be held before this function.
 */
int lwis_dev_power_down_locked(struct lwis_device *lwis_dev, bool error_handling)
{
	int ret;
	int last_error = 0;
	struct lwis_i2c_device *i2c_dev = NULL;

	/*
	 * Device power down triggered by error handling has
	 * no influence on camera state.
	 */
	if (!error_handling)
		lwis_platform_set_device_state(lwis_dev, /*camera_up=*/false);

	if (lwis_dev->type == DEVICE_TYPE_I2C) {
		i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);
		if (i2c_dev->group_i2c_lock == NULL) {
			dev_err(lwis_dev->dev, "group_i2c_lock is NULL. Abort power up.\n");
			return -EINVAL;
		}
	}

	if (lwis_dev->vops.device_disable) {
		ret = lwis_dev->vops.device_disable(lwis_dev);
		if (ret) {
			dev_err(lwis_dev->dev, "Error executing device disable function\n");
			last_error = ret;
		}
	}

	if (lwis_dev->phys) {
		/* Power on the PHY */
		ret = lwis_phy_set_power_all(lwis_dev->phys,
					     /* power_on = */ false);
		if (ret) {
			dev_err(lwis_dev->dev, "Error powering off PHY\n");
			last_error = ret;
		}
	}

	if (lwis_dev->type == DEVICE_TYPE_I2C)
		mutex_lock(i2c_dev->group_i2c_lock);

	if (error_handling && lwis_dev->suspend_sequence) {
		dev_dbg(lwis_dev->dev,
			"Running suspend sequence during error handling power down\n");
		lwis_dev_process_power_sequence(lwis_dev, lwis_dev->suspend_sequence,
						/*set_active=*/false, /*skip_error=*/true);
	}

	if (lwis_dev->power_down_sequence) {
		if (need_to_power_down(lwis_dev)) {
			struct lwis_device *power_dev;

			mutex_lock(&core.lock);
			power_dev = get_power_down_dev_locked(lwis_dev);
			mutex_unlock(&core.lock);
			LWIS_ATRACE_FUNC_BEGIN(lwis_dev, "lwis_dev_process_power_sequence");
			ret = lwis_dev_process_power_sequence(power_dev,
							      power_dev->power_down_sequence,
							      /*set_active=*/false,
							      /*skip_error=*/true);
			LWIS_ATRACE_FUNC_END(lwis_dev, "lwis_dev_process_power_sequence");
			if (ret) {
				dev_err(lwis_dev->dev,
					"Error lwis_dev_process_power_sequence (%d)\n", ret);
				last_error = ret;
			}
		}
		decrease_unified_power_count(lwis_dev);
	} else if (lwis_dev->type == DEVICE_TYPE_I2C) {
		dev_err(lwis_dev->dev, "No power down sequence defined!\n");
		last_error = -EINVAL;
	}
	if (lwis_dev->type == DEVICE_TYPE_I2C)
		mutex_unlock(i2c_dev->group_i2c_lock);

	/* Disable all clocks */
	if (lwis_dev->clocks)
		lwis_clock_disable_all(lwis_dev->clocks);

	/* Let's do the platform-specific disable call */
	ret = lwis_platform_device_disable(lwis_dev);
	if (ret) {
		dev_err(lwis_dev->dev, "Platform-specific device disable fail: %d\n", ret);
		last_error = ret;
	}

	/* Device is powered down, reset suspend count */
	lwis_dev->suspend_count = 0;
	dev_dbg(lwis_dev->dev, "power down, suspend_count now %d\n", lwis_dev->suspend_count);

	lwis_allocator_purge_pools_locked(lwis_dev);

	if (last_error)
		return last_error;

	return ret;
}

/*
 *  lwis_dev_power_seq_list_alloc:
 *  Allocate an instance of the lwis_device_power_sequence_info
 *  and initialize the data structures according to the number of
 *  lwis_device_power_sequence_info specified.
 */
struct lwis_device_power_sequence_list *lwis_dev_power_seq_list_alloc(int count)
{
	struct lwis_device_power_sequence_list *list;

	/* No need to allocate if count is invalid */
	if (count <= 0)
		return ERR_PTR(-EINVAL);

	list = kmalloc(sizeof(struct lwis_device_power_sequence_list), GFP_KERNEL);
	if (!list)
		return ERR_PTR(-ENOMEM);

	list->seq_info =
		kmalloc_array(count, sizeof(struct lwis_device_power_sequence_info), GFP_KERNEL);
	if (!list->seq_info) {
		kfree(list);
		return ERR_PTR(-ENOMEM);
	}

	list->count = count;

	return list;
}

/*
 *  lwis_dev_power_seq_list_free: Deallocate the
 *  lwis_device_power_sequence_info structure.
 */
void lwis_dev_power_seq_list_free(struct lwis_device_power_sequence_list *list)
{
	if (!list)
		return;

	kfree(list->seq_info);
	kfree(list);
}

/*
 *  lwis_dev_power_seq_list_print:
 *  Print lwis_device_power_sequence_list content
 */
void lwis_dev_power_seq_list_print(struct lwis_device_power_sequence_list *list)
{
	for (int i = 0; i < list->count; ++i) {
		pr_info("type:%s  name:%s  delay_us:%d\n", list->seq_info[i].type,
			list->seq_info[i].name, list->seq_info[i].delay_us);
	}
}

static struct lwis_device *find_top_dev(void)
{
	struct lwis_device *lwis_dev;

	mutex_lock(&core.lock);
	list_for_each_entry(lwis_dev, &core.lwis_dev_list, dev_list) {
		if (lwis_dev->type == DEVICE_TYPE_TOP) {
			mutex_unlock(&core.lock);
			return lwis_dev;
		}
	}
	mutex_unlock(&core.lock);
	return NULL;
}

static void event_heartbeat_timer(struct timer_list *t)
{
	struct lwis_device *lwis_dev = from_timer(lwis_dev, t, heartbeat_timer);
	int64_t event_id = LWIS_EVENT_ID_HEARTBEAT | (int64_t)lwis_dev->id
							     << LWIS_EVENT_ID_EVENT_CODE_LEN;

	lwis_device_event_emit(lwis_dev, event_id, NULL, 0);

	mod_timer(t, jiffies + msecs_to_jiffies(LWIS_HEARTBEAT_EVENT_INTERVAL_MS));
}

/*
 *  lwis_find_dev_by_id: Find LWIS device by id.
 */
struct lwis_device *lwis_find_dev_by_id(int dev_id)
{
	struct lwis_device *lwis_dev;

	mutex_lock(&core.lock);
	list_for_each_entry(lwis_dev, &core.lwis_dev_list, dev_list) {
		if (lwis_dev->id == dev_id) {
			mutex_unlock(&core.lock);
			return lwis_dev;
		}
	}
	mutex_unlock(&core.lock);
	return NULL;
}

/*
 *  lwis_i2c_i3c_dev_is_in_use: Check i2c/i3c device is in use.
 */
bool lwis_i2c_i3c_dev_is_in_use(struct lwis_device *lwis_dev)
{
	struct lwis_device *lwis_dev_it;
	struct lwis_i2c_device *i2c_i3c_dev;

	if (lwis_dev->type != DEVICE_TYPE_I2C && lwis_dev->type != DEVICE_TYPE_I3C) {
		dev_err(lwis_dev->dev, "It's not i2c/i3c device\n");
		return false;
	}

	i2c_i3c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);
	mutex_lock(&core.lock);
	list_for_each_entry(lwis_dev_it, &core.lwis_dev_list, dev_list) {
		if (lwis_dev_it->type == DEVICE_TYPE_I2C || lwis_dev_it->type == DEVICE_TYPE_I3C) {
			struct lwis_i2c_device *i2c_i3c_dev_it =
				container_of(lwis_dev_it, struct lwis_i2c_device, base_dev);
			/* Look up if i2c bus are still in use by other device*/
			if ((i2c_i3c_dev_it->state_pinctrl == i2c_i3c_dev->state_pinctrl) &&
			    (i2c_i3c_dev_it != i2c_i3c_dev) && lwis_dev_it->enabled) {
				mutex_unlock(&core.lock);
				return true;
			}
		}
	}
	mutex_unlock(&core.lock);
	return false;
}

void lwis_device_info_dump(const char *name, void (*func)(struct lwis_device *))
{
	struct lwis_device *lwis_dev_it;

	pr_info("LWIS Device Info Dump: %s\n\n", name);

	mutex_lock(&core.lock);
	list_for_each_entry(lwis_dev_it, &core.lwis_dev_list, dev_list) {
		func(lwis_dev_it);
	}
	mutex_unlock(&core.lock);
}

void lwis_save_register_io_info_with_size(struct lwis_device *lwis_dev,
					  struct lwis_io_entry *io_entry, int access_size)
{
	unsigned long flags;
	u64 timestamp = ktime_to_ns(lwis_get_time());

	if (!access_size) {
		access_size = (io_entry->type == LWIS_IO_ENTRY_WRITE ||
			       io_entry->type == LWIS_IO_ENTRY_WRITE_V2 ||
			       io_entry->type == LWIS_IO_ENTRY_WRITE_BATCH ||
			       io_entry->type == LWIS_IO_ENTRY_WRITE_BATCH_V2) ?
				      lwis_dev->native_write_value_bitwidth :
				      lwis_dev->native_read_value_bitwidth;
	}

	if (lwis_reg_io_debug) {
		if (lwis_dev->dev) {
			switch (io_entry->type) {
			case LWIS_IO_ENTRY_READ:
			case LWIS_IO_ENTRY_READ_V2:
				dev_info(lwis_dev->dev,
					 "Register IO: READ device:%s bid:%d offset:0x%llx\n",
					 lwis_dev->name, io_entry->rw.bid, io_entry->rw.offset);
				break;
			case LWIS_IO_ENTRY_WRITE:
			case LWIS_IO_ENTRY_WRITE_V2:
				dev_info(
					lwis_dev->dev,
					"Register IO: WRITE device:%s bid:%d offset:0x%llx val:0x%llx\n",
					lwis_dev->name, io_entry->rw.bid, io_entry->rw.offset,
					io_entry->rw.val);
				break;
			case LWIS_IO_ENTRY_READ_BATCH:
			case LWIS_IO_ENTRY_READ_BATCH_V2:
				dev_info(
					lwis_dev->dev,
					"Register IO: READ_BATCH device:%s bid:%d offset:0x%llx size:%llu\n",
					lwis_dev->name, io_entry->rw_batch.bid,
					io_entry->rw_batch.offset,
					(unsigned long long)io_entry->rw_batch.size_in_bytes);
				break;
			case LWIS_IO_ENTRY_WRITE_BATCH:
			case LWIS_IO_ENTRY_WRITE_BATCH_V2:
				dev_info(
					lwis_dev->dev,
					"Register IO: WRITE_BATCH device:%s bid:%d offset:0x%llx size:%llu\n",
					lwis_dev->name, io_entry->rw_batch.bid,
					io_entry->rw_batch.offset,
					(unsigned long long)io_entry->rw_batch.size_in_bytes);
				break;
			case LWIS_IO_ENTRY_MODIFY:
				dev_info(
					lwis_dev->dev,
					"Register IO: MODIFY device:%s bid:%d offset:0x%llx val:0x%llx mask:0x%llx\n",
					lwis_dev->name, io_entry->mod.bid, io_entry->mod.offset,
					io_entry->mod.val, io_entry->mod.val_mask);
				break;
			case LWIS_IO_ENTRY_POLL:
			case LWIS_IO_ENTRY_POLL_SHORT:
				dev_info(
					lwis_dev->dev,
					"Register IO: POLL device:%s bid:%d offset:0x%llx val:0x%llx mask:0x%llx timeout:%llu\n",
					lwis_dev->name, io_entry->read_assert.bid,
					io_entry->read_assert.offset, io_entry->read_assert.val,
					io_entry->read_assert.mask,
					io_entry->read_assert.timeout_ms);
				break;
			case LWIS_IO_ENTRY_READ_ASSERT:
				dev_info(
					lwis_dev->dev,
					"Register IO: READ_ASSERT device:%s bid:%d offset:0x%llx val:0x%llx mask:0x%llx\n",
					lwis_dev->name, io_entry->read_assert.bid,
					io_entry->read_assert.offset, io_entry->read_assert.val,
					io_entry->read_assert.mask);
				break;
			case LWIS_IO_ENTRY_WAIT:
				dev_info(lwis_dev->dev,
					 "Register IO: WAIT device:%s duration:%lluus\n",
					 lwis_dev->name, io_entry->wait_us);
				break;
			default:
				dev_info(lwis_dev->dev, "Register IO: OTHER device:%s type:%d\n",
					 lwis_dev->name, io_entry->type);
				break;
			}
		} else {
			pr_info("Register IO: device:%s (no dev) type:%d\n", lwis_dev->name,
				io_entry->type);
		}
	}

	spin_lock_irqsave(&lwis_dev->lock, flags);
	lwis_dev->debug_info.io_entry_hist[lwis_dev->debug_info.cur_io_entry_hist_idx].io_entry =
		*io_entry;
	lwis_dev->debug_info.io_entry_hist[lwis_dev->debug_info.cur_io_entry_hist_idx].access_size =
		access_size;
	lwis_dev->debug_info.io_entry_hist[lwis_dev->debug_info.cur_io_entry_hist_idx]
		.start_timestamp = timestamp;
	lwis_dev->debug_info.cur_io_entry_hist_idx =
		(lwis_dev->debug_info.cur_io_entry_hist_idx + 1) % IO_ENTRY_DEBUG_HISTORY_SIZE;
	spin_unlock_irqrestore(&lwis_dev->lock, flags);
}

void lwis_save_register_io_info(struct lwis_device *lwis_dev, struct lwis_io_entry *io_entry)
{
	lwis_save_register_io_info_with_size(lwis_dev, io_entry, 0);
}

static int ioreg_dev_update_platform_top_dev(struct lwis_device *lwis_dev)
{
	static bool qos_box_probed;
	int ret = 0;
	struct lwis_device *other_dev;

	/*
	 * Note: Populate platform top dev may failed until qos box is probed.
	 * Use qos_box_probed to track it.
	 */
	mutex_lock(&core.lock);
	if (!qos_box_probed) {
		/* populate platform top dev from other probed ioreg device */
		list_for_each_entry(other_dev, &core.lwis_dev_list, dev_list) {
			if (other_dev->type != DEVICE_TYPE_IOREG)
				continue;

			ret = lwis_platform_update_top_dev(lwis_dev->top_dev, other_dev,
							   &qos_box_probed);
			if (ret < 0) {
				mutex_unlock(&core.lock);
				if (ret == -ENOMEM || qos_box_probed)
					return ret;

				dev_warn(lwis_dev->dev, "Qos Box is not available, try later");
				return 0;
			}
		}
	} else {
		ret = lwis_platform_update_top_dev(lwis_dev->top_dev, lwis_dev, &qos_box_probed);
	}
	mutex_unlock(&core.lock);

	return ret;
}

/*
 *  lwis_base_probe: Create a device instance for each of the LWIS device.
 */
int lwis_base_probe(struct lwis_device *lwis_dev)
{
	int ret = 0;

	/* Allocate a minor number to this device */
	mutex_lock(&core.lock);
	ret = idr_alloc(core.idr, lwis_dev, 0, LWIS_MAX_DEVICES, GFP_KERNEL);
	mutex_unlock(&core.lock);
	if (ret >= 0) {
		lwis_dev->id = ret;
	} else {
		dev_err(lwis_dev->k_dev, "Unable to allocate minor ID (%d)\n", ret);
		return ret;
	}

	/* Initialize enabled state */
	lwis_dev->enabled = 0;
	lwis_dev->suspend_count = 0;
	lwis_dev->clock_family = CLOCK_FAMILY_INVALID;

	/* Initialize client mutex */
	mutex_init(&lwis_dev->client_lock);

	/* Initialize an empty list of clients */
	INIT_LIST_HEAD(&lwis_dev->clients);

	/* Initialize the allocator */
	lwis_allocator_init(lwis_dev);

	/* Initialize an empty list for gpio info nodes */
	INIT_LIST_HEAD(&lwis_dev->gpios_list);

	/* Initialize an empty list for regulator info nodes */
	INIT_LIST_HEAD(&lwis_dev->regulator_list);

	/* Initialize an empty list for pwrseq info nodes */
	INIT_LIST_HEAD(&lwis_dev->pwrseq_list);

	/* Initialize event state hash table */
	hash_init(lwis_dev->event_states);

	/* Initialize the spinlock */
	spin_lock_init(&lwis_dev->lock);
	spin_lock_init(&lwis_dev->allocator_lock);

	if (lwis_dev->type == DEVICE_TYPE_TOP) {
		lwis_dev->top_dev = lwis_dev;
		/* Assign top device to the devices probed before */
		assign_top_to_other(lwis_dev);
	} else {
		lwis_dev->top_dev = find_top_dev();
		if (lwis_dev->top_dev == NULL)
			pr_warn("Top device not probed yet");
	}

	/* Add this instance to the device list */
	mutex_lock(&core.lock);
	list_add(&lwis_dev->dev_list, &core.lwis_dev_list);
	mutex_unlock(&core.lock);

	ret = base_setup(lwis_dev);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			pr_err("Error initializing LWIS device\n");
		goto error_init;
	}

	/* Upon success initialization, create device for this instance */
	lwis_dev->dev = device_create(core.dev_class, NULL, MKDEV(core.device_major, lwis_dev->id),
				      lwis_dev, LWIS_DEVICE_NAME "-%s", lwis_dev->name);
	if (IS_ERR_OR_NULL(lwis_dev->dev)) {
		pr_err("Failed to create device\n");
		ret = PTR_ERR(lwis_dev->dev);
		goto error_init;
	}

	/* Call platform-specific probe function */
	lwis_platform_probe(lwis_dev);

	if (lwis_dev->type == DEVICE_TYPE_IOREG) {
		ret = ioreg_dev_update_platform_top_dev(lwis_dev);
		if (ret)
			goto error_init;
	}

	lwis_device_debugfs_setup(lwis_dev, core.dbg_root);

	timer_setup(&lwis_dev->heartbeat_timer, event_heartbeat_timer, 0);

	dev_info(lwis_dev->dev, "Base Probe: Success\n");

	return ret;

	/* Error conditions */
error_init:
	lwis_base_unprobe(lwis_dev);
	mutex_lock(&core.lock);
	idr_remove(core.idr, lwis_dev->id);
	mutex_unlock(&core.lock);
	return ret;
}

/*
 *  lwis_base_unprobe: Cleanup a device instance
 */
void lwis_base_unprobe(struct lwis_device *unprobe_lwis_dev)
{
	struct lwis_device *lwis_dev = NULL;
	struct lwis_device *it, *temp;

	mutex_lock(&core.lock);
	list_for_each_entry_safe(it, temp, &core.lwis_dev_list, dev_list) {
		if (it == unprobe_lwis_dev) {
			list_del(&it->dev_list);
			lwis_dev = it;
			break;
		}
	}
	mutex_unlock(&core.lock);

	if (!lwis_dev)
		return;

	pr_debug("Destroy device %s id %d\n", lwis_dev->name, lwis_dev->id);
	lwis_device_debugfs_cleanup(lwis_dev);
	/* Destroy the allocator */
	lwis_allocator_destroy(lwis_dev);
	/* Release device clock list */
	if (lwis_dev->clocks) {
		lwis_clock_list_free(lwis_dev->clocks);
		lwis_dev->clocks = NULL;
	}
	/* Release device interrupt list */
	if (lwis_dev->irqs) {
		lwis_interrupt_list_free(lwis_dev->irqs);
		lwis_dev->irqs = NULL;
	}
	/* Release device regulator list */
	if (!list_empty(&lwis_dev->regulator_list)) {
		lwis_regulator_put_all(&lwis_dev->regulator_list);
		lwis_regulator_list_free(&lwis_dev->regulator_list);
	}
	/* Release device pwrseq list */
	if (!list_empty(&lwis_dev->pwrseq_list))
		lwis_pwrseq_list_free(&lwis_dev->pwrseq_list);
	/* Release device phy list */
	if (lwis_dev->phys) {
		lwis_phy_list_free(lwis_dev->phys);
		lwis_dev->phys = NULL;
	}
	/* Release device power sequence list */
	if (lwis_dev->power_up_sequence) {
		lwis_dev_power_seq_list_free(lwis_dev->power_up_sequence);
		lwis_dev->power_up_sequence = NULL;
	}
	if (lwis_dev->power_down_sequence) {
		lwis_dev_power_seq_list_free(lwis_dev->power_down_sequence);
		lwis_dev->power_down_sequence = NULL;
	}
	/* Release device gpio list */
	if (!list_empty(&lwis_dev->gpios_list))
		lwis_gpios_list_free(&lwis_dev->gpios_list);

	/* Release device gpio info irq list */
	if (lwis_dev->irq_gpios_info.irq_list) {
		lwis_interrupt_list_free(lwis_dev->irq_gpios_info.irq_list);
		lwis_dev->irq_gpios_info.irq_list = NULL;
	}
	if (lwis_dev->irq_gpios_info.gpios) {
		lwis_gpio_list_put(lwis_dev->irq_gpios_info.gpios, lwis_dev->k_dev);
		lwis_dev->irq_gpios_info.gpios = NULL;
	}

	lwis_bus_manager_disconnect_device(lwis_dev);

	/* Call platform-specific probe function */
	lwis_platform_unprobe(lwis_dev);

	/* Destroy device */
	if (!IS_ERR_OR_NULL(lwis_dev->dev))
		device_destroy(core.dev_class, MKDEV(core.device_major, lwis_dev->id));

	if (timer_pending(&lwis_dev->heartbeat_timer))
		del_timer(&lwis_dev->heartbeat_timer);
}

/*
 *  lwis_register_base_device: Create device class and device major number to
 *  the class of LWIS devices.
 *
 *  This is called once when the core LWIS driver is initialized.
 */
static int __init lwis_register_base_device(void)
{
	int ret = 0;

	/* Allocate ID management instance for device minor numbers */
	core.idr = kzalloc(sizeof(struct idr), GFP_KERNEL);
	if (!core.idr)
		return -ENOMEM;

	mutex_lock(&core.lock);

	idr_init(core.idr);

	/* Acquire device major number and allocate the range to minor numbers
	 * to the device
	 */
	ret = alloc_chrdev_region(&core.lwis_devt, 0, LWIS_MAX_DEVICES, LWIS_DEVICE_NAME);
	if (ret) {
		pr_err("Error in allocating chrdev region\n");
		goto error_chrdev_alloc;
	}

	core.device_major = MAJOR(core.lwis_devt);

	/* Create a device class*/
	core.dev_class = class_create(LWIS_CLASS_NAME);
	if (IS_ERR_OR_NULL(core.dev_class)) {
		pr_err("Failed to create device class\n");
		ret = PTR_ERR(core.dev_class);
		goto error_class_create;
	}

	/* Allocate a character device */
	core.chr_dev = cdev_alloc();
	if (!core.chr_dev) {
		pr_err("Failed to allocate cdev\n");
		ret = -ENOMEM;
		goto error_cdev_alloc;
	}

	core.chr_dev->ops = &lwis_fops;

	ret = cdev_add(core.chr_dev, core.lwis_devt, LWIS_MAX_DEVICES);
	if (ret) {
		pr_err("Failed to add cdev\n");
		goto error_cdev_alloc;
	}

	INIT_LIST_HEAD(&core.lwis_dev_list);

#ifdef CONFIG_DEBUG_FS
	/* Create DebugFS directory for LWIS, if avaiable */
	core.dbg_root = debugfs_create_dir("lwis", NULL);
	if (IS_ERR_OR_NULL(core.dbg_root)) {
		/* No need to return error as this is just informational that
		 * DebugFS is not present
		 */
		pr_info("Failed to create DebugFS dir - DebugFS not present?");
		core.dbg_root = NULL;
	}
#endif

	mutex_unlock(&core.lock);

	return ret;

	/* Error conditions */
error_cdev_alloc:
	class_destroy(core.dev_class);
	core.dev_class = NULL;
error_class_create:
	unregister_chrdev_region(core.lwis_devt, LWIS_MAX_DEVICES);
error_chrdev_alloc:
	mutex_unlock(&core.lock);
	kfree(core.idr);
	core.idr = NULL;

	return ret;
}

static void lwis_unregister_base_device(void)
{
	mutex_lock(&core.lock);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove(core.dbg_root);
	core.dbg_root = NULL;
#endif

	cdev_del(core.chr_dev);
	core.chr_dev = NULL;

	class_destroy(core.dev_class);
	core.dev_class = NULL;

	unregister_chrdev_region(core.lwis_devt, LWIS_MAX_DEVICES);

	kfree(core.idr);
	core.idr = NULL;

	mutex_unlock(&core.lock);
}

/*
 *  lwis_base_device_init: Called during subsys_initcall routines.
 */
static int __init lwis_base_device_init(void)
{
	int ret = 0;

	pr_info("LWIS device initialization\n");

	/* Initialize the core struct */
	memset(&core, 0, sizeof(struct lwis_core));
	mutex_init(&core.lock);

	ret = lwis_register_base_device();
	if (ret) {
		pr_err("Failed to register LWIS base (%d)\n", ret);
		return ret;
	}

	ret = lwis_top_device_init();
	if (ret) {
		pr_err("Failed to lwis_top_device_init (%d)\n", ret);
		goto top_failure;
	}

	ret = lwis_pwrseq_device_init();
	if (ret) {
		pr_err("Failed to lwis_pwrseq_device_init (%d)\n", ret);
		goto pwrseq_failure;
	}

	ret = lwis_ioreg_device_init();
	if (ret) {
		pr_err("Failed to lwis_ioreg_device_init (%d)\n", ret);
		goto ioreg_failure;
	}

	ret = lwis_i2c_device_init();
	if (ret) {
		pr_err("Failed to lwis_i2c_device_init (%d)\n", ret);
		goto i2c_failure;
	}

	ret = lwis_slc_device_init();
	if (ret) {
		pr_err("Failed to lwis_slc_device_init (%d)\n", ret);
		goto slc_failure;
	}

	ret = lwis_dpm_device_init();
	if (ret) {
		pr_err("Failed to lwis_dpm_device_init (%d)\n", ret);
		goto dpm_failure;
	}

	ret = lwis_test_device_init();
	if (ret) {
		pr_err("Failed to lwis_test_device_init (%d)\n", ret);
		goto test_failure;
	}

	ret = lwis_spi_device_init();
	if (ret) {
		pr_err("Failed to lwis_spi_device_init (%d)\n", ret);
		goto spi_failure;
	}

	ret = lwis_i2c_device_v2_init();
	if (ret) {
		pr_err("Failed to lwis_i2c_device_v2_init (%d)\n", ret);
		goto i2c_v2_failure;
	}

	ret = lwis_i3c_proxy_device_init();
	if (ret) {
		pr_err("Failed to lwis_i3c_proxy_device_init (%d)\n", ret);
		goto i3c_proxy_failure;
	}

	return 0;

i3c_proxy_failure:
	lwis_i2c_device_v2_deinit();
i2c_v2_failure:
	lwis_spi_device_deinit();
spi_failure:
	lwis_test_device_deinit();
test_failure:
	lwis_dpm_device_deinit();
dpm_failure:
	lwis_slc_device_deinit();
slc_failure:
	lwis_i2c_device_deinit();
i2c_failure:
	lwis_ioreg_device_deinit();
ioreg_failure:
	lwis_pwrseq_device_deinit();
pwrseq_failure:
	lwis_top_device_deinit();
top_failure:
	lwis_unregister_base_device();
	return ret;
}

struct file *lwis_open_lwis_client_from_fd(struct lwis_device *lwis_dev, int fd)
{
	struct file *fp;

	fp = fget(fd);
	if (IS_ERR(fp)) {
		dev_err(lwis_dev->dev, "Error opening file pointer with fd %d\n", fd);
		return fp;
	}

	if (fp->f_op != &lwis_fops) {
		dev_err(lwis_dev->dev, "File for fd %d is not a lwis_client.\n", fd);
		fput(fp);
		return ERR_PTR(-EINVAL);
	}

	return fp;
}

/*
 *  lwis_base_device_deinit: Called when driver is unloaded.
 */
static void __exit lwis_driver_exit(void)
{
	struct lwis_device *lwis_dev, *temp;
	struct lwis_client *client, *client_temp;

	pr_info("%s Clean up LWIS devices.\n", __func__);
	list_for_each_entry_safe(lwis_dev, temp, &core.lwis_dev_list, dev_list) {
		pr_info("Destroy device %s id %d\n", lwis_dev->name, lwis_dev->id);
		lwis_device_debugfs_cleanup(lwis_dev);
		/* Disable lwis device events */
		lwis_device_event_enable(lwis_dev, LWIS_EVENT_ID_HEARTBEAT, false);
		if (lwis_dev->type == DEVICE_TYPE_I2C) {
			struct lwis_i2c_device *i2c_dev;

			i2c_dev = container_of(lwis_dev, struct lwis_i2c_device, base_dev);
			i2c_unregister_device(i2c_dev->client);
			free_otp_settings(&i2c_dev->i3c_otp_config);
			free_otp_settings(&i2c_dev->i2c_otp_config);
		}
		/* Relase each client registered with dev */
		list_for_each_entry_safe(client, client_temp, &lwis_dev->clients, node) {
			if (release_client(client))
				pr_info("Failed to release client.");
		}
		pm_runtime_disable(lwis_dev->k_dev);
		/* Release device clock list */
		if (lwis_dev->clocks)
			lwis_clock_list_free(lwis_dev->clocks);

		/* Release device interrupt list */
		if (lwis_dev->irqs)
			lwis_interrupt_list_free(lwis_dev->irqs);

		/* Release device regulator list */
		if (!list_empty(&lwis_dev->regulator_list)) {
			lwis_regulator_put_all(&lwis_dev->regulator_list);
			lwis_regulator_list_free(&lwis_dev->regulator_list);
		}
		/* Release device pwrseq list */
		if (!list_empty(&lwis_dev->pwrseq_list))
			lwis_pwrseq_list_free(&lwis_dev->pwrseq_list);
		/* Release device phy list */
		if (lwis_dev->phys)
			lwis_phy_list_free(lwis_dev->phys);

		/* Release device power sequence list */
		if (lwis_dev->power_up_sequence)
			lwis_dev_power_seq_list_free(lwis_dev->power_up_sequence);

		if (lwis_dev->power_down_sequence)
			lwis_dev_power_seq_list_free(lwis_dev->power_down_sequence);

		/* Release device gpio list */
		if (!list_empty(&lwis_dev->gpios_list))
			lwis_gpios_list_free(&lwis_dev->gpios_list);

		/* Release device gpio info irq list */
		if (lwis_dev->irq_gpios_info.irq_list)
			lwis_interrupt_list_free(lwis_dev->irq_gpios_info.irq_list);

		if (lwis_dev->irq_gpios_info.gpios)
			lwis_gpio_list_put(lwis_dev->irq_gpios_info.gpios, lwis_dev->k_dev);

		/* Release event subscription components */
		if (lwis_dev->type == DEVICE_TYPE_TOP) {
			struct lwis_top_device *top_dev;

			top_dev = container_of(lwis_dev, struct lwis_top_device, base_dev);
			top_dev->subscribe_ops.release(lwis_dev);
		}

		lwis_bus_manager_disconnect_device(lwis_dev);
		/* Call platform-specific probe function */
		lwis_platform_unprobe(lwis_dev);

		/* Destroy device */
		device_destroy(core.dev_class, MKDEV(core.device_major, lwis_dev->id));
		list_del(&lwis_dev->dev_list);
	}

	/* Deinit device classes */
	lwis_i3c_proxy_device_deinit();
	lwis_i2c_device_v2_deinit();
	lwis_spi_device_deinit();
	lwis_test_device_deinit();
	lwis_dpm_device_deinit();
	lwis_slc_device_deinit();
	lwis_i2c_device_deinit();
	lwis_ioreg_device_deinit();
	lwis_pwrseq_device_deinit();
	lwis_top_device_deinit();

	/* Unregister base lwis device */
	lwis_unregister_base_device();
}

void lwis_process_worker_queue(struct lwis_client *client)
{
	lwis_process_transactions_in_queue(client, /*process_high_priority_transaction=*/false);
	lwis_process_periodic_io_in_queue(client);
}

subsys_initcall(lwis_base_device_init);
module_exit(lwis_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Google-ACMA");
MODULE_DESCRIPTION("LWIS Base Device Driver");
MODULE_IMPORT_NS(DMA_BUF);
