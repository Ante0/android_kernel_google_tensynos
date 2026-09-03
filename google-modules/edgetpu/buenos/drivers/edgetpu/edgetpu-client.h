/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Edge TPU client structures and helper functions.
 *
 * Copyright (C) 2026 Google LLC
 */

#ifndef __EDGETPU_CLIENT_H__
#define __EDGETPU_CLIENT_H__

#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "edgetpu-internal.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"

/**
 * struct edgetpu_client - Edge TPU client state.
 * @client_id: Unique ID number of this client.
 * @tgid: Thread group ID of the client process.
 * @name: Name of the client.
 * @runtime_identified_client: True if client has been identified.
 * @count: Reference count.
 * @group_lock: Protects @group.
 * @group: The virtual device group this client created.
 * @etdev: The device opened by this client.
 * @wakelock: Per-client request to keep device active.
 * @limited_interface_lock: Protects @limited_interface.
 * @limited_interface: Pointer to the limited interface to this client, if any.
 * @trim_enabled: Pixel trim currently enabled/disabled for this client.
 * @inactive_count: Times client has been reported inactive by firmware.
 * @force_wakelock_release_work: Worker for async forced wakelock release.
 * @client_list_node: Node for list of clients under `edgetpu_dev`.
 *
 * @group can be NULL if the client has not created a group.
 * Once the group is set it will never change until client teardown when the last fd is closed.
 * Racing group creation and disbandment are not allowed.
 */
struct edgetpu_client {
	uint client_id;
	pid_t tgid;
	char name[40];
	/* TODO(b/489208801): Remove when EDGETPU_IDENTIFY_CLIENT in use for all targets. */
	bool runtime_identified_client;
	refcount_t count;
	struct mutex group_lock;
	struct edgetpu_device_group *group;
	struct edgetpu_dev *etdev;
	struct edgetpu_wakelock wakelock;
	struct mutex limited_interface_lock;
	struct file *limited_interface;
	bool trim_enabled;
	uint inactive_count;
	struct work_struct force_wakelock_release_work;
	struct list_head client_list_node;
};

/**
 * struct edgetpu_exited_client - state preserved from exited clients for bookeeping/triage.
 * @client_id: Unique ID number of this client.
 * @tgid: Thread group ID of the client process.
 * @name: Name of the client.
 * @total_wakelock_time: Total time client held TPU wakelocks in seconds.
 * @exited_clients: Node for list of exited clients.
 * @exit_ts: Timestamp of client exit (real time clock).
 */
struct edgetpu_exited_client {
	uint client_id;
	pid_t tgid;
	char name[40];
	/* TPU wakelock total acquire time in seconds. */
	unsigned long wakelock_total_time;
	struct list_head exited_clients;
	struct timespec64 exit_ts;
};

/**
 * edgetpu_client_add() - Adds current thread as new TPU client.
 * @etdev: The Edge TPU device.
 *
 * Return: A pointer to the newly created client, or an ERR_PTR on failure.
 */
struct edgetpu_client *edgetpu_client_add(struct edgetpu_dev *etdev);

/**
 * edgetpu_client_remove() - Removes TPU client.
 * @client: The client to remove.
 */
void edgetpu_client_remove(struct edgetpu_client *client);

/**
 * edgetpu_client_get() - Increases reference count of client.
 * @client: The client to get.
 *
 * Return: The client pointer.
 */
struct edgetpu_client *edgetpu_client_get(struct edgetpu_client *client);

/**
 * edgetpu_client_put() - Decreases reference count and free if zero.
 * @client: The client to put.
 */
void edgetpu_client_put(struct edgetpu_client *client);

/**
 * edgetpu_client_trim_enable() - Enables/disables Pixel trim for client.
 * @client: The client to update.
 * @enable: Non-zero enables, zero disables.
 */
void edgetpu_client_trim_enable(struct edgetpu_client *client, u32 enable);

/**
 * edgetpu_client_update_name() - Sets client name based on task ID.
 * @client: The client to update.
 * @task_id: Task ID of the process.
 */
void edgetpu_client_update_name(struct edgetpu_client *client, pid_t task_id);

/**
 * edgetpu_client_set_tgid() - Sets client tgid and update client name.
 * @client: The client to update.
 * @task_id: Task ID of the process.
 */
void edgetpu_client_set_tgid(struct edgetpu_client *client, pid_t task_id);

/**
 * edgetpu_client_force_release_wakelocks_async() - Forces release wakelocks.
 * @client: The client to release wakelocks for.
 */
void edgetpu_client_force_release_wakelocks_async(struct edgetpu_client *client);

/**
 * edgetpu_client_create_group - Creates a device group for the client.
 * @client: The client to create a group for.
 * @attr: Attributes for the mailbox queue.
 *
 * Return: 0 on success, negative error code on failure.
 */
int edgetpu_client_create_group(struct edgetpu_client *client,
				const struct edgetpu_mailbox_attr *attr);

/**
 * edgetpu_client_destroy_group - Destroys the client's device group.
 * @client: The client whose group to destroy.
 */
void edgetpu_client_destroy_group(struct edgetpu_client *client);

/**
 * edgetpu_exited_client_destroy_all - Destroys all exited client entries at device remove time.
 * @etdev: the edgetpu device being removed.
 */
void edgetpu_exited_client_destroy_all(struct edgetpu_dev *etdev);

/**
 * edgetpu_client_log_state - Log client state info for debugging use.
 * @client: The client to be logged.
 */
void edgetpu_client_log_state(struct edgetpu_client *client);

#endif /* __EDGETPU_CLIENT_H__ */
