// SPDX-License-Identifier: GPL-2.0-only
/*
 * Edge TPU client management.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/workqueue.h>

#include <gcip/gcip-event.h>
#include <gcip/gcip-pm.h>

#include "edgetpu-client.h"
#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-firmware.h"
#include "edgetpu-ikv.h"
#include "edgetpu-internal.h"
#include "edgetpu-mmu.h"
#include "edgetpu-pm.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"

/*
 * Exited clients holding wakelocks for at least this number of seconds total are held preserved
 * for power accounting.
 */
#define WAKELOCK_TOTAL_PRESERVE_THRESHOLD_SEC	(60 * 5)
/*
 * Exited clients that exited at least this many seconds ago may be expunged if they do not meet
 * the wakelock time preservation requirement above.
 */
#define EXIT_TIME_PRESERVE_THRESHOLD_SEC (60 * 15)

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

	scoped_guard(mutex, &client->group_lock) {
		if (client->group)
			edgetpu_group_close_and_detach_mailbox(client->group);
	}

	release_wakelocks(client, count);
	edgetpu_client_put(client);
}

struct edgetpu_client *edgetpu_client_add(struct edgetpu_dev *etdev)
{
	struct edgetpu_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->client_id = atomic_add_return(1, &next_client_id);
	client->etdev = etdev;
	edgetpu_wakelock_init(client);
	client->tgid = task_tgid_nr(current);
	edgetpu_client_update_name(client, client->tgid);
	mutex_init(&client->group_lock);
	/* equivalent to edgetpu_client_get() */
	refcount_set(&client->count, 1);
	mutex_init(&client->limited_interface_lock);
	INIT_WORK(&client->force_wakelock_release_work, edgetpu_force_wakelock_release_worker);

	scoped_guard(mutex, &etdev->clients_lock)
		list_add_tail(&client->client_list_node, &etdev->clients);

	edgetpu_eventlog_event(client->etdev, EVENTLOG_EVENT_CLIENT_CREATE, client);

	return client;
}

void edgetpu_exited_client_destroy_all(struct edgetpu_dev *etdev)
{
	struct edgetpu_exited_client *xclient;
	struct edgetpu_exited_client *xc;

	list_for_each_entry_safe(xclient, xc, &etdev->exited_clients, exited_clients) {
		list_del(&xclient->exited_clients);
		kfree(xclient);
	}
}

/* etdev->clients_lock is held by caller */
static void exited_client_create(struct edgetpu_client *client)
{
	struct edgetpu_exited_client *xclient = NULL;
	struct edgetpu_exited_client *xc;
	struct timespec64 curr_ts;

	/*
	 * Look for an old existing entry that isn't important to keep for wakelock held triage
	 * that we can re-use for the new entry.
	 */
	ktime_get_ts64(&curr_ts);
	list_for_each_entry(xc, &client->etdev->exited_clients, exited_clients) {
		if (xc->wakelock_total_time < WAKELOCK_TOTAL_PRESERVE_THRESHOLD_SEC) {
			struct timespec64 exit_ago_ts = timespec64_sub(curr_ts, xc->exit_ts);

			if (exit_ago_ts.tv_sec > EXIT_TIME_PRESERVE_THRESHOLD_SEC) {
				xclient = xc;
				list_del(&xclient->exited_clients);
				break;
			}
		}
	}

	if (!xclient) {
		xclient = kmalloc(sizeof(*xclient), GFP_KERNEL);
		if (!xclient)
			return;
	}

	xclient->client_id = client->client_id;
	xclient->tgid = client->tgid;
	strscpy(xclient->name, client->name, sizeof(xclient->name));
	xclient->wakelock_total_time = client->wakelock.total_acquired_time.tv_sec;
	ktime_get_ts64(&xclient->exit_ts);
	list_add_tail(&xclient->exited_clients, &client->etdev->exited_clients);
}

void edgetpu_client_remove(struct edgetpu_client *client)
{
	struct edgetpu_dev *etdev = client->etdev;
	uint wakelock_count;

	edgetpu_eventlog_event(client->etdev, EVENTLOG_EVENT_CLIENT_REMOVE, client);

	/*
	 * Not checked with lock held, but an exiting client shouldn't be changing wakelock state,
	 * and the warn log doesn't have to be rigorously consistent.
	 */
	if (client->wakelock.req_count) {
		etdev_warn(etdev, "client %s exiting with wakelock held", client->name);
		edgetpu_client_log_state(client);
	}

	scoped_guard(mutex, &etdev->clients_lock) {
		/* remove the client from the device list */
		list_del(&client->client_list_node);
		exited_client_create(client);
	}

	edgetpu_client_destroy_group(client);

	/* Cleanup external mailbox/secure client stuff. */
	edgetpu_ext_client_remove(client);

	gcip_event_mgr_unset_by_owner(etdev->event_mgr, client);

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

	return 0;

err_revert_group:
	scoped_guard(mutex, &client->group_lock)
		client->group = NULL;
err_disband:
	edgetpu_device_group_disband(group);

	return ret;
}

void edgetpu_client_destroy_group(struct edgetpu_client *client)
{
	struct edgetpu_device_group *group = client->group;

	/* Not checked under lock, but unlikely to change at this point and not a critical check. */
	if (group && atomic_read(&group->rsp_mgr->ikv_credits) <
	    EDGETPU_NUM_VII_CREDITS_PER_CLIENT) {
		etdev_warn(client->etdev,
			   "client %s deactivating with pending VII commands",
			   client->name);
		edgetpu_client_log_state(client);
		edgetpu_firmware_log_state(client->etdev);
	}

	scoped_guard(mutex, &client->group_lock) {
		group = client->group;
		client->group = NULL;
	}

	if (!group)
		return;

	edgetpu_dev_unregister_group(client->etdev, group);
	edgetpu_device_group_disband(group);
}

void edgetpu_client_log_state(struct edgetpu_client *client)
{
	struct edgetpu_dev *etdev = client->etdev;
	struct edgetpu_device_group *group = client->group;
	struct edgetpu_iommu_domain *etdomain;

	etdev_info(etdev,
		   "client %s: tgid=%d wl=%c inact=%c trim=%c",
		   client->name, client->tgid,
		   client->wakelock.req_count ? 'y' : 'n',
		   client->inactive_count ? 'y' : 'n',
		   client->trim_enabled ? 'y' : 'n');
	if (!group)
		return;

	down_read(&group->lock);
	etdomain = edgetpu_group_domain_locked(group);
	etdev_info(etdev, "vcid=%u pasid=%d stat=%u err=%#x cmdpend=%d cancel=%d",
		   group->vcid, edgetpu_mmu_domain_detached(etdomain) ? -1 : etdomain->pasid,
		   group->status, group->fatal_errors,
		   EDGETPU_NUM_VII_CREDITS_PER_CLIENT - atomic_read(&group->rsp_mgr->ikv_credits),
		   group->rsp_mgr->cancel_reason);
	up_read(&group->lock);
}
