// SPDX-License-Identifier: GPL-2.0-only
/*
 * Edge TPU client management.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <asm/current.h>
#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <gcip/gcip-firmware.h>

#include "edgetpu-client.h"
#include "edgetpu-device-group.h"
#include "edgetpu-internal.h"
#include "edgetpu-pm.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"

static atomic_t next_client_id = ATOMIC_INIT(0);

/**
 * release_wakelocks() - Releases @count wake locks for @client.
 * @client: The client whose wakelocks to release.
 * @count: Number of wakelocks to release.
 *
 * Does not cleanup mailbox etc. state. Caller may have modified
 * wakelock.req_count and may not be holding the wakelock lock.
 */
static void release_wakelocks(struct edgetpu_client *client, uint count)
{
	enum gcip_pm_flags gcip_pm_flags = client->wakelock.suspendable ? GCIP_PM_SUSPENDABLE : 0;

	while (count--)
		edgetpu_pm_put_flags(client->etdev, gcip_pm_flags);
}

/**
 * edgetpu_force_wakelock_release_worker() - Worker to handle forced wakelock release.
 * @work: The work structure embedded in the client.
 */
static void edgetpu_force_wakelock_release_worker(struct work_struct *work)
{
	struct edgetpu_client *client =
		container_of(work, struct edgetpu_client, force_wakelock_release_work);
	uint count;

	edgetpu_wakelock_lock(client);
	count = edgetpu_wakelock_count_locked(client);

	/* If already released then bail. */
	if (!count) {
		edgetpu_wakelock_unlock(client);
		edgetpu_client_put(client);
		return;
	}

	etdev_warn(client->etdev, "force releasing wakelocks for client %s", client->name);
	/* ioctls will no longer touch the wakelock count when this is found to be true. */
	client->wakelock.force_released = true;
	client->wakelock.req_count = 0;
	edgetpu_wakelock_unlock(client);
	mutex_lock(&client->group_lock);
	if (client->group)
		edgetpu_group_close_and_detach_mailbox(client->group);
	mutex_unlock(&client->group_lock);
	release_wakelocks(client, count);
	edgetpu_client_put(client);
}

struct edgetpu_client *edgetpu_client_add(struct edgetpu_dev *etdev)
{
	struct edgetpu_client *client;
	struct edgetpu_list_device_client *l = kmalloc(sizeof(*l), GFP_KERNEL);

	if (!l)
		return ERR_PTR(-ENOMEM);
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		kfree(l);
		return ERR_PTR(-ENOMEM);
	}
	client->client_id = atomic_add_return(1, &next_client_id);
	client->etdev = etdev;
	edgetpu_wakelock_init(client);
	client->tgid = task_tgid_nr(current);
	edgetpu_client_update_name(client, client->tgid);
	mutex_init(&client->group_lock);
	/* equivalent to edgetpu_client_get() */
	refcount_set(&client->count, 1);
	client->perdie_events = 0;
	mutex_init(&client->limited_interface_lock);
	INIT_WORK(&client->force_wakelock_release_work, edgetpu_force_wakelock_release_worker);
	mutex_lock(&etdev->clients_lock);
	l->client = client;
	list_add_tail(&l->list, &etdev->clients);
	mutex_unlock(&etdev->clients_lock);
	edgetpu_eventlog_event(client->etdev, EVENTLOG_EVENT_CLIENT_CREATE, client);
	return client;
}

void edgetpu_client_remove(struct edgetpu_client *client)
{
	struct edgetpu_dev *etdev = client->etdev;
	struct edgetpu_list_device_client *lc;
	uint wakelock_count;

	edgetpu_eventlog_event(client->etdev, EVENTLOG_EVENT_CLIENT_REMOVE, client);

	mutex_lock(&etdev->clients_lock);
	/* remove the client from the device list */
	for_each_list_device_client(etdev, lc) {
		if (lc->client == client) {
			list_del(&lc->list);
			kfree(lc);
			break;
		}
	}
	mutex_unlock(&etdev->clients_lock);

	edgetpu_client_destroy_group(client);

	/* Cleanup external mailbox/secure client stuff. */
	edgetpu_ext_client_remove(client);

	/* Clean up all the per die event fds registered by the client */
	if (client->perdie_events &
	    BIT(perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_LOGS_AVAILABLE)))
		edgetpu_telemetry_unset_event(etdev, etdev->telemetry_log);
	if (client->perdie_events &
	    BIT(perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_TRACES_AVAILABLE)))
		edgetpu_telemetry_unset_event(etdev, etdev->telemetry_trace);

	edgetpu_wakelock_lock(client);
	wakelock_count = client->wakelock.req_count;
	client->wakelock.req_count = 0;
	edgetpu_wakelock_unlock(client);
	release_wakelocks(client, wakelock_count);
	edgetpu_wakelock_destroy(client);
	edgetpu_client_put(client);
}

struct edgetpu_client *edgetpu_client_get(struct edgetpu_client *client)
{
	WARN_ON_ONCE(!refcount_inc_not_zero(&client->count));
	return client;
}

void edgetpu_client_put(struct edgetpu_client *client)
{
	if (!client)
		return;
	if (refcount_dec_and_test(&client->count))
		kfree(client);
}

void edgetpu_client_trim_enable(struct edgetpu_client *client, u32 enable)
{
	client->trim_enabled = enable;
}

void edgetpu_client_update_name(struct edgetpu_client *client, pid_t task_id)
{
	struct task_struct *tsk;

	rcu_read_lock();
	tsk = find_task_by_vpid(task_id);
	if (tsk)
		snprintf(client->name, sizeof(client->name), "%s.%u", tsk->comm, client->client_id);
	else
		snprintf(client->name, sizeof(client->name), "?.%u", client->client_id);
	rcu_read_unlock();
}

void edgetpu_client_set_tgid(struct edgetpu_client *client, pid_t task_id)
{
	client->runtime_identified_client = true;
	client->tgid = task_id;
	edgetpu_client_update_name(client, task_id);
}

void edgetpu_client_force_release_wakelocks_async(struct edgetpu_client *client)
{
	/*
	 * Hold a ref on the client, released by edgetpu_force_wakelock_release_worker (or if
	 * the worker is already scheduled then release the ref here, the worker will release one
	 * ref held by the previous schedule).
	 */
	edgetpu_client_get(client);
	if (!schedule_work(&client->force_wakelock_release_work))
		edgetpu_client_put(client);
}

int edgetpu_client_create_group(struct edgetpu_client *client,
				const struct edgetpu_mailbox_attr *attr)
{
	struct edgetpu_device_group *group;
	int ret;

	/* No need to take the lock as it is just a quick check for early return. */
	if (client->group)
		return -EINVAL;

	group = edgetpu_device_group_create(client, attr);
	if (IS_ERR(group))
		return PTR_ERR(group);

	scoped_guard(mutex, &client->group_lock) {
		if (client->group) {
			ret = -EINVAL;
			goto err_disband;
		}
		client->group = group;
	}

	ret = edgetpu_dev_register_group(client->etdev, group);
	if (ret)
		goto err_revert_group;

	ret = edgetpu_device_group_finish_setup(group);
	if (ret)
		goto err_unregister_group;

	return 0;

err_unregister_group:
	edgetpu_dev_unregister_group(client->etdev, group);
err_revert_group:
	scoped_guard(mutex, &client->group_lock)
		client->group = NULL;
err_disband:
	edgetpu_device_group_disband(group);

	return ret;
}

void edgetpu_client_destroy_group(struct edgetpu_client *client)
{
	struct edgetpu_device_group *group;

	scoped_guard(mutex, &client->group_lock) {
		group = client->group;
		client->group = NULL;
	}

	if (group)
		edgetpu_device_group_disband(group);
}
