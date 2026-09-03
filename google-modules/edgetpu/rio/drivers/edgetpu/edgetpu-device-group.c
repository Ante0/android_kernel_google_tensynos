// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implements utilities for virtual device group of EdgeTPU.
 *
 * Copyright (C) 2019-2026 Google LLC
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/eventfd.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/pm_runtime.h>
#include <linux/refcount.h>
#include <linux/rwsem.h>
#include <linux/scatterlist.h>
#include <linux/sched/mm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time64.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#include <gcip/gcip-dma-fence.h>
#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-iommu.h>
#include <gcip/gcip-memory.h>

#include <iif/iif-dma-fence.h>

#include "edgetpu-client.h"
#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-dmabuf.h"
#include "edgetpu-firmware.h"
#include "edgetpu-iif.h"
#include "edgetpu-ikv.h"
#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mapping.h"
#include "edgetpu-mmu.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-vii-packet.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"

/* Param id_type passed to get_group_by_id/get_group_by_id_locked */
enum id_type {
	EDGETPU_ID_TYPE_CLIENT_ID,
	EDGETPU_ID_TYPE_VCID,
};

/*
 * Return the group with @id of the given @type for device @etdev, with a reference held on the
 * group (must call edgetpu_device_group_put when done), or NULL if no group with that @id is found.
 *
 * Caller holds etdev->groups_lock.
 */
static struct edgetpu_device_group *get_group_by_id_locked(struct edgetpu_dev *etdev, u32 id,
							   enum id_type type)
{
	struct edgetpu_device_group *group = NULL;
	struct edgetpu_device_group *tgroup;
	struct edgetpu_list_group *g;
	u32 tgroup_id;
	struct edgetpu_iommu_domain *etdomain __maybe_unused;

	etdev_for_each_group(etdev, g, tgroup) {
		switch (type) {
		case EDGETPU_ID_TYPE_CLIENT_ID:
			down_write(&tgroup->lock);
			etdomain = edgetpu_group_domain_locked(tgroup);
			if (!etdomain)
				tgroup_id = IOMMU_PASID_INVALID;
			else
				tgroup_id = etdomain->pasid;
			up_write(&tgroup->lock);
			break;
		case EDGETPU_ID_TYPE_VCID:
			tgroup_id = tgroup->vcid;
			break;
		}
		if (tgroup_id == id) {
			group = edgetpu_device_group_get(tgroup);
			break;
		}
	}
	return group;
}

/*
 * Return the group with @id of the given @type for device @etdev, with a reference held on the
 * group (must call edgetpu_device_group_put when done), or NULL if no group with that @id is found.
 */
static struct edgetpu_device_group *get_group_by_id(struct edgetpu_dev *etdev, u32 id,
						    enum id_type type)
{
	struct edgetpu_device_group *group;

	mutex_lock(&etdev->groups_lock);
	group = get_group_by_id_locked(etdev, id, type);
	mutex_unlock(&etdev->groups_lock);
	return group;
}

static int edgetpu_group_activate_external_mailbox(struct edgetpu_device_group *group)
{
	if (!group->ext_mailbox)
		return 0;
	edgetpu_mailbox_reinit_external_mailbox(group);
	return edgetpu_mailbox_activate_external_mailbox(group);
}

/*
 * Activates the VII mailbox @group owns.
 *
 * Caller holds group->lock for writing.
 */
static int edgetpu_group_activate(struct edgetpu_device_group *group)
{
	struct edgetpu_iommu_domain *etdomain;
	int ret;

	if (edgetpu_group_mailbox_detached_locked(group))
		return 0;

	/* Activate the mailbox whose index == the assigned PASID */
	etdomain = edgetpu_group_domain_locked(group);
	edgetpu_soc_activate_context(group->etdev, etdomain->pasid);
	ret = edgetpu_ikv_activate_client(group->etdev->etikv, etdomain->pasid,
					  group->mbox_attr.client_priv, group->vcid,
					  !group->activated);
	if (ret) {
		etdev_err(group->etdev, "client %s activate mailbox (pasid %d vcid %d) failed: %d",
			  group->client->name, etdomain->pasid, group->vcid, ret);
	} else {
		group->activated = true;
		edgetpu_sw_wdt_inc_active_ref(group->etdev);
	}
	atomic_inc(&group->etdev->job_count);
	return ret;
}

static void edgetpu_group_deactivate_external_mailbox(struct edgetpu_device_group *group)
{
	edgetpu_mailbox_deactivate_external_mailbox(group);
	edgetpu_mailbox_disable_external_mailbox(group);
}

/*
 * Deactivates the VII mailbox @group owns.
 *
 * Caller holds group->lock for writing.
 */
static void edgetpu_group_deactivate(struct edgetpu_device_group *group)
{
	struct edgetpu_iommu_domain *etdomain;

	if (edgetpu_group_mailbox_detached_locked(group))
		return;
	edgetpu_sw_wdt_dec_active_ref(group->etdev);
	etdomain = edgetpu_group_domain_locked(group);

	if (atomic_read(&group->rsp_mgr->pending_count)) {
		etdev_warn(group->etdev, "client %s deactivating with pending VII commands",
			   group->client->name);
		edgetpu_firmware_log_state(group->etdev);
	}

	edgetpu_ikv_deactivate_client(group->etdev->etikv, etdomain->pasid);
	/*
	 * Deactivate the context to prevent speculative accesses from being issued to a disabled
	 * context.
	 */
	edgetpu_soc_deactivate_context(group->etdev, etdomain->pasid);
}

/*
 * Handle KCI chores for device group disband.
 *
 * send KCI CLOSE_DEVICE to the device (and GET_USAGE to update usage stats).
 *
 * Caller holds group->lock for writing.
 */
static void edgetpu_device_group_kci_deactivate(struct edgetpu_device_group *group)
{
	edgetpu_kci_update_usage_async(group->etdev->etkci);
	if (pm_runtime_get_if_active(group->etdev->dev) > 0) {
		edgetpu_group_deactivate(group);
		pm_runtime_put(group->etdev->dev);
	}
}

/*
 * Activates the group/client, including allocate vmbox, etc. KCI interactions with firmware.
 *
 * Caller holds group->lock for writing.
 */
static int edgetpu_device_group_kci_activate(struct edgetpu_device_group *group)
{
	return edgetpu_group_activate(group);
}

static inline bool is_ready_or_errored(struct edgetpu_device_group *group)
{
	return edgetpu_device_group_is_ready(group) || edgetpu_device_group_is_errored(group);
}

int edgetpu_group_set_eventfd(struct edgetpu_device_group *group, uint event_id, int eventfd)
{
	struct eventfd_ctx *ctx = eventfd_ctx_fdget(eventfd);
	ulong flags;

	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (event_id >= EDGETPU_EVENT_COUNT) {
		eventfd_ctx_put(ctx);
		return -EINVAL;
	}

	write_lock_irqsave(&group->events.lock, flags);
	if (group->events.eventfds[event_id])
		eventfd_ctx_put(group->events.eventfds[event_id]);
	group->events.eventfds[event_id] = ctx;
	write_unlock_irqrestore(&group->events.lock, flags);
	return 0;
}

void edgetpu_group_unset_eventfd(struct edgetpu_device_group *group, uint event_id)
{
	ulong flags;

	if (event_id >= EDGETPU_EVENT_COUNT)
		return;

	write_lock_irqsave(&group->events.lock, flags);
	if (group->events.eventfds[event_id])
		eventfd_ctx_put(group->events.eventfds[event_id]);
	group->events.eventfds[event_id] = NULL;
	write_unlock_irqrestore(&group->events.lock, flags);
}

static void edgetpu_group_clear_events(struct edgetpu_device_group *group)
{
	int event_id;
	ulong flags;

	write_lock_irqsave(&group->events.lock, flags);
	for (event_id = 0; event_id < EDGETPU_EVENT_COUNT; event_id++) {
		if (group->events.eventfds[event_id])
			eventfd_ctx_put(group->events.eventfds[event_id]);
		group->events.eventfds[event_id] = NULL;
	}
	write_unlock_irqrestore(&group->events.lock, flags);
}

struct pending_command_task {
	struct list_head list_entry;
	struct task_struct *task;
};

static void edgetpu_group_clear_pending_commands(struct edgetpu_device_group *group)
{
	struct list_head *cur, *nxt;
	struct pending_command_task *pending_task;
	unsigned long flags;

	spin_lock_irqsave(&group->pending_cmd_tasks_lock, flags);
	group->is_clearing_pending_commands = true;
	spin_unlock_irqrestore(&group->pending_cmd_tasks_lock, flags);

	/*
	 * With @group->lock held for writing and @group->is_clearing_pending_commands set, there
	 * will be no more additions or deletions from @group->pending_cmd_tasks respectively so it
	 * can be iterated over without holding @group->pending_cmd_tasks.
	 */
	list_for_each_safe(cur, nxt, &group->pending_cmd_tasks) {
		pending_task = container_of(cur, struct pending_command_task, list_entry);
		/*
		 * kthread_stop() will wake the task and wait for it to exit.
		 * If the task is already waiting on a dma_fence, this will interrupt the wait
		 * and cause the task to exit immediately.
		 *
		 * If the task has not started waiting on its fence by the time this call occurs,
		 * then this call will have to wait for the fence to timeout before it returns.
		 */
		kthread_stop(pending_task->task);
		list_del(&pending_task->list_entry);
		kfree(pending_task);
	}
}

void edgetpu_group_notify(struct edgetpu_device_group *group, uint event_id)
{
	unsigned long flags;

	if (event_id >= EDGETPU_EVENT_COUNT)
		return;

	read_lock_irqsave(&group->events.lock, flags);
	if (group->events.eventfds[event_id])
		eventfd_signal(group->events.eventfds[event_id]);
	read_unlock_irqrestore(&group->events.lock, flags);
}

/*
 * Releases all resources the group allocated and mark the group as disbanded.
 *
 * release VII mailboxes, buffer mappings, etc.
 *
 * The lock of group must be held for writing.
 */
static void edgetpu_device_group_release(struct edgetpu_device_group *group)
{
	lockdep_assert_held(&group->lock);

	edgetpu_group_clear_events(group);
	if (is_ready_or_errored(group)) {
		edgetpu_group_clear_pending_commands(group);
		edgetpu_device_group_kci_deactivate(group);
		/*
		 * Mappings cannot be cleared until the device_group has been closed via KCI.
		 * This ensures firmware will not attempt to access any resources freed by the
		 * command's `release_callback` or memory which has been unmapped.
		 */
		edgetpu_mappings_clear_group(group);
		edgetpu_mailbox_external_disable_free_locked(group);
	}
	/* etdomain is freed after group->lock is dropped to avoid deadlock b/348298955. */

	gcip_dma_fence_manager_destroy(group->gfence_mgr);

	edgetpu_ikv_rsp_mgr_disband(group->rsp_mgr);

	/* Put the refcount of the client held in edgetpu_device_group_create(). */
	edgetpu_client_put(group->client);

	group->status = EDGETPU_DEVICE_GROUP_DISBANDED;
}

/**
 * edgetpu_dev_vcid_alloc() - Allocates a virtual context ID (VCID) for a group.
 * @etdev: The EdgeTPU device.
 * @mbox_attr: Mailbox attributes of the device group.
 *
 * Context: Caller must hold &etdev->groups_lock.
 * Return: The allocated VCID (>= 0) on success, or a negative errno otherwise.
 */
static int edgetpu_dev_vcid_alloc(struct edgetpu_dev *etdev,
				  const struct edgetpu_mailbox_attr *mbox_attr)
{
	u32 vcid_pool;
	int vcid;

	lockdep_assert_held(&etdev->groups_lock);

	if (etdev->group_create_lockout)
		return -EAGAIN;

	vcid_pool = etdev->vcid_pool;
	if (mbox_attr->partition_type_high == EDGETPU_PARTITION_EXTRA)
		vcid_pool &= BIT(EDGETPU_VCID_EXTRA_PARTITION_HIGH);
	else if (mbox_attr->partition_type == EDGETPU_PARTITION_EXTRA)
		vcid_pool &= BIT(EDGETPU_VCID_EXTRA_PARTITION);
	else
		vcid_pool &= ~(BIT(EDGETPU_VCID_EXTRA_PARTITION) |
			       BIT(EDGETPU_VCID_EXTRA_PARTITION_HIGH));

	if (!vcid_pool) {
		if (mbox_attr->partition_type_high == EDGETPU_PARTITION_EXTRA) {
			struct edgetpu_device_group *claim_group = get_group_by_id_locked(
				etdev, EDGETPU_VCID_EXTRA_PARTITION_HIGH, EDGETPU_ID_TYPE_VCID);

			etdev_err(
				etdev,
				"error creating new client: extra high partition already claimed");
			if (claim_group) {
				struct edgetpu_client *claim_client = claim_group->client;

				etdev_err(etdev, "by client %s tgid %d", claim_client->name,
					  claim_client->tgid);
				edgetpu_device_group_put(claim_group);
			}
		} else {
			etdev_err(etdev, "%s client slot unavailable (%u active groups)",
				  mbox_attr->partition_type == EDGETPU_PARTITION_EXTRA ? "extra" :
											 "normal",
				  etdev->n_groups);
		}
		return -EBUSY;
	}

	vcid = ffs(vcid_pool) - 1;
	etdev->vcid_pool &= ~BIT(vcid);

	return vcid;
}

/**
 * edgetpu_dev_vcid_free() - Frees a virtual context ID (VCID) allocated for a group.
 * @etdev: The EdgeTPU device.
 * @vcid: The virtual context ID.
 *
 * Context: Caller must hold &etdev->groups_lock.
 */
static void edgetpu_dev_vcid_free(struct edgetpu_dev *etdev, int vcid)
{
	lockdep_assert_held(&etdev->groups_lock);

	etdev->vcid_pool |= BIT(vcid);
}

/**
 * edgetpu_dev_register_group() - Registers a group to the device's group list.
 * @etdev: The EdgeTPU device.
 * @group: The device group to register.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int edgetpu_dev_register_group(struct edgetpu_dev *etdev, struct edgetpu_device_group *group)
{
	struct edgetpu_list_group *l;
	int vcid;
	int ret;

	l = kmalloc(sizeof(*l), GFP_KERNEL);
	if (!l)
		return -ENOMEM;

	scoped_guard(mutex, &etdev->groups_lock) {
		if (etdev->group_create_lockout) {
			ret = -EAGAIN;
			goto error_free;
		}

		vcid = edgetpu_dev_vcid_alloc(etdev, &group->mbox_attr);
		if (vcid < 0) {
			ret = vcid;
			goto error_free;
		}

		group->vcid = vcid;

		l->grp = edgetpu_device_group_get(group);
		list_add_tail(&l->list, &etdev->groups);
		etdev->n_groups++;
	}

	return 0;

error_free:
	kfree(l);

	return ret;
}

/**
 * edgetpu_dev_unregister_group() - Unregisters a group from the device's group list.
 * @etdev: The EdgeTPU device.
 * @group: The device group to unregister.
 */
void edgetpu_dev_unregister_group(struct edgetpu_dev *etdev, struct edgetpu_device_group *group)
{
	struct edgetpu_list_group *l;

	guard(mutex)(&etdev->groups_lock);

	list_for_each_entry(l, &etdev->groups, list) {
		if (l->grp == group) {
			edgetpu_dev_vcid_free(etdev, group->vcid);
			list_del(&l->list);
			/* Put the refcount of the group held in edgetpu_dev_register_group(). */
			edgetpu_device_group_put(l->grp);
			kfree(l);
			etdev->n_groups--;
			break;
		}
	}
}

void edgetpu_device_group_put(struct edgetpu_device_group *group)
{
	if (!group)
		return;
	if (refcount_dec_and_test(&group->ref_count))
		kfree(group);
}

/* caller must hold @etdev->groups_lock. */
static bool edgetpu_in_any_group_locked(struct edgetpu_dev *etdev)
{
	return etdev->n_groups;
}

void edgetpu_device_group_disband(struct edgetpu_device_group *group)
{
	struct edgetpu_iommu_domain *etdomain;

	down_write(&group->lock);
	edgetpu_device_group_release(group);
	etdomain = group->etdomain;
	group->etdomain = NULL;
	up_write(&group->lock);

	/* Now that group->lock is released, free the etdomain b/348298955 */
	if (etdomain) {
		edgetpu_mmu_detach_domain(group->etdev, etdomain);
		edgetpu_mmu_free_domain(group->etdev, etdomain);
	}

	edgetpu_dev_unregister_group(group->etdev, group);

	/* Put the refcount of the group held in edgetpu_device_group_create(). */
	edgetpu_device_group_put(group);
}

int edgetpu_device_group_finish_setup(struct edgetpu_device_group *group)
{
	struct edgetpu_client *client = group->client;
	int ret = 0;

	edgetpu_wakelock_lock(client);
	down_write(&group->lock);
	if (!group->mailbox_detachable) {
		ret = edgetpu_mmu_attach_domain(group->etdev, group->etdomain);
		if (ret) {
			etdev_err(group->etdev, "group setup attach domain failed: %d", ret);
			goto err_unlock;
		}
	}

	if (edgetpu_wakelock_count_locked(group->client)) {
		ret = edgetpu_group_attach_mailbox_locked(group);
		if (ret) {
			etdev_err(group->etdev, "group setup attach mailbox failed: %d", ret);
			goto err_detach_mmu_domain;
		}
	}

	/* send KCI only if the device is powered on */
	if (edgetpu_wakelock_count_locked(group->client)) {
		ret = edgetpu_device_group_kci_activate(group);
		if (ret)
			goto err_remove_detach_mailbox;
	}

	group->status = EDGETPU_DEVICE_GROUP_READY;
	up_write(&group->lock);
	edgetpu_wakelock_unlock(client);
	return 0;

err_remove_detach_mailbox:
	if (edgetpu_wakelock_count_locked(group->client))
		edgetpu_group_detach_mailbox_locked(group);

err_detach_mmu_domain:
	if (!group->mailbox_detachable)
		edgetpu_mmu_detach_domain(group->etdev, group->etdomain);
err_unlock:
	up_write(&group->lock);
	edgetpu_wakelock_unlock(client);
	return ret;
}

struct edgetpu_device_group *edgetpu_device_group_create(struct edgetpu_client *client,
							 const struct edgetpu_mailbox_attr *attr)
{
	int ret;
	struct edgetpu_device_group *group;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	refcount_set(&group->ref_count, 1);
	group->status = EDGETPU_DEVICE_GROUP_INITIALIZING;
	group->etdev = client->etdev;
	group->client = edgetpu_client_get(client);
	atomic_set(&group->available_vii_credits, EDGETPU_NUM_VII_CREDITS_PER_CLIENT);
	init_rwsem(&group->lock);
	mutex_init(&group->pin_user_pages_lock);
	mutex_init(&group->vii_lock);
	rwlock_init(&group->events.lock);
	INIT_LIST_HEAD(&group->dma_fence_list);
	mutex_init(&group->dma_fence_lock);
	edgetpu_mapping_init(&group->host_mappings);
	edgetpu_mapping_init(&group->dmabuf_mappings);
	group->mbox_attr = *attr;
	INIT_LIST_HEAD(&group->pending_cmd_tasks);
	spin_lock_init(&group->pending_cmd_tasks_lock);
	group->is_clearing_pending_commands = false;
#if HAS_DETACHABLE_IOMMU_DOMAINS
	if (attr->priority & EDGETPU_PRIORITY_DETACHABLE)
		group->mailbox_detachable = true;
#endif

	group->rsp_mgr = edgetpu_ikv_rsp_mgr_create(group);
	if (IS_ERR(group->rsp_mgr)) {
		ret = PTR_ERR(group->rsp_mgr);
		goto err_put_client;
	}

	group->gfence_mgr =
		gcip_dma_fence_manager_create(group->etdev->dev, "edgetpu", client->name);
	if (IS_ERR(group->gfence_mgr)) {
		ret = PTR_ERR(group->gfence_mgr);
		goto err_put_rsp_mgr;
	}

	group->etdomain = edgetpu_mmu_alloc_domain(group->etdev);
	if (!group->etdomain) {
		ret = -ENOMEM;
		goto err_destroy_gfence_mgr;
	}

	return group;

err_destroy_gfence_mgr:
	gcip_dma_fence_manager_destroy(group->gfence_mgr);
err_put_rsp_mgr:
	edgetpu_ikv_rsp_mgr_disband(group->rsp_mgr);
err_put_client:
	edgetpu_client_put(client);
	kfree(group);

	etdev_err(client->etdev, "client %s group create failed: %d", client->name, ret);

	return ERR_PTR(ret);
}

bool edgetpu_in_any_group(struct edgetpu_dev *etdev)
{
	bool ret;

	mutex_lock(&etdev->groups_lock);
	ret = edgetpu_in_any_group_locked(etdev);
	mutex_unlock(&etdev->groups_lock);
	return ret;
}

bool edgetpu_set_group_create_lockout(struct edgetpu_dev *etdev, bool lockout)
{
	bool ret = true;

	mutex_lock(&etdev->groups_lock);
	if (lockout && edgetpu_in_any_group_locked(etdev))
		ret = false;
	else
		etdev->group_create_lockout = lockout;
	mutex_unlock(&etdev->groups_lock);
	return ret;
}

/*
 * Unmap a mapping specified by @map. Unmaps from IOMMU and unpins pages,
 * frees mapping node, which is invalid upon return.
 *
 * Caller locks group->host_mappings.
 */
static void buffer_mapping_destroy(struct edgetpu_mapping *map)
{
	struct edgetpu_device_group *group = map->priv;

	gcip_mapping_unmap(map->gcip_mapping);
	edgetpu_device_group_put(group);
	kfree(map);
}

static void edgetpu_host_map_show(struct edgetpu_mapping *map, struct seq_file *s)
{
	struct scatterlist *sg;
	int i;
	size_t cur_offset = 0;
	enum gcip_map_debug_flags map_debug_flags = map->gcip_mapping->map_debug_flags;
	unsigned long attrs = GCIP_MAP_FLAGS_GET_DMA_ATTR(map->gcip_mapping->gcip_map_flags);

	if (map->trimmed || !map->gcip_mapping->sgt) {
		seq_printf(s, "  %pad %lu %s %#llx - %c%c%c%c%c%c%c%c\n",
			   &map->gcip_mapping->device_address, map->gcip_mapping->size,
			   edgetpu_dma_dir_rw_s(map->gcip_mapping->dir), map->host_addr,
			   map_debug_flags & GCIP_MAP_DEBUG_COW ? 'c' : '.',
			   map_debug_flags & GCIP_MAP_DEBUG_OVRRD_RDDIR ? 'o' : '.',
			   map_debug_flags & GCIP_MAP_DEBUG_VMA_NF ? 'n' : '.',
			   map_debug_flags & GCIP_MAP_DEBUG_ASSUME_RDONLY ? 'a' : '.',
			   GCIP_MAP_FLAGS_GET_DMA_COHERENT(map->gcip_mapping->gcip_map_flags) ?
			   'C' : '.',
			   attrs & DMA_ATTR_SKIP_CPU_SYNC ? 'S' : '.',
			   /* trimmed */ 'T', map->mapped_by_limited ? 'L' : '.');
		return;
	}

	/* Only 1 entry per mapped segment is shown, with the phys addr of the 1st segment. */
	for_each_sg(map->gcip_mapping->sgt->sgl, sg, map->gcip_mapping->sgt->nents, i) {
		dma_addr_t phys_addr = sg_phys(sg);
		dma_addr_t dma_addr = sg_dma_address(sg);

		seq_printf(s, "  %pad %lu %s %#llx %pap %c%c%c%c%c%c%c%c\n", &dma_addr,
			   DIV_ROUND_UP(sg_dma_len(sg), PAGE_SIZE),
			   edgetpu_dma_dir_rw_s(map->gcip_mapping->dir),
			   map->host_addr + cur_offset, &phys_addr,
			   map_debug_flags & GCIP_MAP_DEBUG_COW ? 'c' : '.',
			   map_debug_flags & GCIP_MAP_DEBUG_OVRRD_RDDIR ? 'o' : '.',
			   map_debug_flags & GCIP_MAP_DEBUG_VMA_NF ? 'n' : '.',
			   map_debug_flags & GCIP_MAP_DEBUG_ASSUME_RDONLY ? 'a' : '.',
			   GCIP_MAP_FLAGS_GET_DMA_COHERENT(map->gcip_mapping->gcip_map_flags) ?
			   'C' : '.',
			   attrs & DMA_ATTR_SKIP_CPU_SYNC ? 'S' : '.',
			   map->flags & EDGETPU_MAP_TRIMMABLE ? 't' : '.',
			   map->mapped_by_limited ? 'L' : '.');
		cur_offset += sg_dma_len(sg);
	}
}

size_t edgetpu_group_mappings_total_size(struct edgetpu_device_group *group, bool restrict32,
					 bool cow_only)
{
	size_t ret = edgetpu_mappings_total_size(&group->host_mappings, restrict32, cow_only);

	/* dmabuf is never COW. */
	if (!cow_only)
		ret += edgetpu_mappings_total_size(&group->dmabuf_mappings, restrict32, false);
	return ret;
}

/*
 * Performs DMA sync of the mapping with region [offset, offset + size).
 *
 * Caller holds @host_mappings lock, to prevent @map being modified / removed by other processes.
 */
static int group_sync_host_map(struct edgetpu_device_group *group, struct edgetpu_mapping *map,
			       u64 offset, u64 size, bool for_cpu)
{
	if (map->trimmed) {
		etdev_err(group->etdev, "sync requested for trimmed buffer");
		return -EINVAL;
	}

	/* In the future buffers can have no sgt even when not "trimmed"; check this now. */
	if (!map->gcip_mapping->sgt)
		return 0;

	return gcip_mapping_buffer_sync(map->gcip_mapping, group->etdev->dev, offset, size,
					for_cpu);
}

int edgetpu_group_remap_buffers(struct edgetpu_client *client)
{
	struct edgetpu_device_group *group = client->group;
	struct edgetpu_mapping *map;
	int ret = 0;
	int fail_ct = 0;

	client->trim_enabled = false;

	if (!group)
		return 0;

	edgetpu_mapping_lock(&group->host_mappings);
	/*
	 * We hold the group's host mappings lock and have set trim disabled for the client above.
	 * Any previous trim operations on this client, which acquire the same lock, are complete
	 * by now. Subsequent trim activity will skip this client, until trim is re-enabled for it.
	 */
	list_for_each_entry(map, &group->host_mappings.trimmable_mappings, trimmable_list) {
		if (map->trimmed) {
			int remap_ret = gcip_mapping_buffer_remap(map->gcip_mapping,
								  &group->pin_user_pages_lock);

			if (remap_ret) {
				ret = remap_ret;
				fail_ct++;
			} else {
				map->trimmed = false;
			}
		}
	}
	edgetpu_mapping_unlock(&group->host_mappings);
	edgetpu_eventlog_event(client->etdev, EVENTLOG_EVENT_CLIENT_REMAP_DONE, client);

	if (ret)
		etdev_err(client->etdev, "client %s remap trimmed buffers: %d failed (%d)\n",
			  client->name, fail_ct, ret);

	return ret;
}

static void edgetpu_group_trim_buffers(struct edgetpu_device_group *group)
{
	struct edgetpu_mapping *map;
	bool trimmed = false;

	edgetpu_mapping_lock(&group->host_mappings);
	if (group->client->trim_enabled) {
		list_for_each_entry(map, &group->host_mappings.trimmable_mappings, trimmable_list) {
			gcip_mapping_buffer_trim(map->gcip_mapping);
			map->trimmed = true;
			trimmed = true;
		}
	}
	edgetpu_mapping_unlock(&group->host_mappings);

	if (trimmed)
		edgetpu_eventlog_event(group->etdev, EVENTLOG_EVENT_CLIENT_TRIM_DONE,
				       group->client);
}

void edgetpu_trim_buffers(struct edgetpu_dev *etdev)
{
	struct edgetpu_list_group *g;
	struct edgetpu_device_group *group;

	mutex_lock(&etdev->groups_lock);
	etdev_for_each_group(etdev, g, group) {
		edgetpu_group_trim_buffers(group);
	}
	mutex_unlock(&etdev->groups_lock);
}

void edgetpu_device_group_log_map_error(struct edgetpu_device_group *group, size_t size,
					edgetpu_map_flag_t flags, int errorval)
{
	bool restrict32 = !(flags & EDGETPU_MAP_CPU_NONACCESSIBLE);
	size_t total = edgetpu_group_mappings_total_size(group, restrict32, false);

	etdev_err(group->etdev, "map %zuB (%d-bit) failed: %d (already mapped %zuB)", size,
		  restrict32 ? 32 : 36, errorval, total);
}

/**
 * buffer_mapping_create() - Maps the buffer and creates the corresponding mapping object.
 * @group: The group that the buffer belongs to.
 * @host_addr: The memory address of the buffer.
 * @size: The size of the buffer.
 * @flags: The flags used to map the buffer.
 *
 * Return: The pointer of the target mapping object or an error pointer on failure.
 */
static struct edgetpu_mapping *buffer_mapping_create(struct edgetpu_device_group *group,
						     u64 host_addr, u64 size,
						     edgetpu_map_flag_t flags, bool limited)
{
	int ret = -EINVAL;
	struct edgetpu_mapping *map = NULL;
	struct edgetpu_iommu_domain *etdomain;
	unsigned long dma_attrs = map_to_dma_attr(flags);
	u64 gcip_map_flags;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map) {
		ret = -ENOMEM;
		goto err_ret;
	}

	map->host_addr = host_addr;
	map->priv = edgetpu_device_group_get(group);
	map->release = buffer_mapping_destroy;
	map->show = edgetpu_host_map_show;
	map->flags = flags;
	map->mapped_by_limited = limited;

	down_read(&group->lock);
	etdomain = edgetpu_group_domain_locked(group);
	if (!edgetpu_device_group_is_ready(group)) {
		ret = edgetpu_group_errno(group);
		up_read(&group->lock);
		goto err_free_map;
	}
	gcip_map_flags = edgetpu_mappings_encode_gcip_map_flags(flags, dma_attrs, true);
	map->gcip_mapping = gcip_mapping_buffer_map(etdomain->gdomain, host_addr, size,
						    gcip_map_flags, &group->pin_user_pages_lock);
	up_read(&group->lock);
	if (IS_ERR(map->gcip_mapping)) {
		ret = PTR_ERR(map->gcip_mapping);
		edgetpu_device_group_log_map_error(group, size, flags, ret);
		goto err_free_map;
	}

	return map;

err_free_map:
	kfree(map);
	edgetpu_device_group_put(group);
err_ret:
	return ERR_PTR(ret);
}

int edgetpu_device_group_map(struct edgetpu_device_group *group, struct edgetpu_map_ioctl *arg,
			     bool limited)
{
	int ret;
	struct edgetpu_mapping *map;
	tpu_addr_t tpu_addr;

	/* Establishing new TPU mappings sets client to "not OK to trim" state. */
	group->client->trim_enabled = false;
	/* Coherent mappings imply no CMO needed. */
	if (arg->flags & EDGETPU_MAP_COHERENT)
		arg->flags |= EDGETPU_MAP_SKIP_CPU_SYNC;
	map = buffer_mapping_create(group, arg->host_address, arg->size, arg->flags, limited);
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		return ret;
	}

	/*
	 * @map can be freed (by another thread) once it's added to the mappings, record the address
	 * before that.
	 */
	tpu_addr = map->gcip_mapping->device_address;
	ret = edgetpu_mapping_add(&group->host_mappings, map);
	if (ret)
		goto err_destroy_mapping;

	arg->device_address = tpu_addr;

	return 0;

err_destroy_mapping:
	buffer_mapping_destroy(map);

	return ret;
}

int edgetpu_device_group_unmap(struct edgetpu_device_group *group, tpu_addr_t tpu_addr,
			       edgetpu_map_flag_t flags, bool limited)
{
	struct edgetpu_mapping *map;

	edgetpu_mapping_lock(&group->host_mappings);
	map = edgetpu_mapping_find_locked(&group->host_mappings, tpu_addr, limited);
	if (!map) {
		edgetpu_mapping_unlock(&group->host_mappings);
		etdev_err(group->etdev, "unmap client %s iova %pad not found", group->client->name,
			  &tpu_addr);
		return -EINVAL;
	}

	edgetpu_mapping_unlink(&group->host_mappings, map);
	buffer_mapping_destroy(map);
	edgetpu_mapping_unlock(&group->host_mappings);
	return 0;
}

int edgetpu_device_group_sync_buffer(struct edgetpu_device_group *group,
				     const struct edgetpu_sync_ioctl *arg)
{
	struct edgetpu_mapping *map;
	int ret = 0;
	tpu_addr_t tpu_addr = arg->device_address;
	/*
	 * Sync operations don't care the data correctness of prefetch by TPU CPU if they mean to
	 * sync FROM_DEVICE only, so @dir here doesn't need to be wrapped with host_dma_dir().
	 */
	enum dma_data_direction dir = arg->flags & EDGETPU_MAP_DIR_MASK;

	if (!valid_dma_direction(dir))
		return -EINVAL;
	/* invalid if size == 0 or overflow */
	if (arg->offset + arg->size <= arg->offset)
		return -EINVAL;

	down_read(&group->lock);
	if (!edgetpu_device_group_is_ready(group)) {
		ret = edgetpu_group_errno(group);
		goto unlock_group;
	}

	edgetpu_mapping_lock(&group->host_mappings);
	map = edgetpu_mapping_find_locked(&group->host_mappings, tpu_addr, false);
	if (!map) {
		ret = -EINVAL;
		goto unlock_mapping;
	}

	ret = group_sync_host_map(group, map, arg->offset, arg->size,
				  arg->flags & EDGETPU_SYNC_FOR_CPU);
unlock_mapping:
	edgetpu_mapping_unlock(&group->host_mappings);
unlock_group:
	up_read(&group->lock);
	return ret;
}

void edgetpu_mappings_clear_group(struct edgetpu_device_group *group)
{
	edgetpu_mapping_clear(&group->host_mappings);
	edgetpu_mapping_clear(&group->dmabuf_mappings);
}

void edgetpu_group_mappings_show(struct edgetpu_device_group *group, struct seq_file *s)
{
	struct edgetpu_iommu_domain *etdomain = edgetpu_group_domain_locked(group);

	seq_printf(s, "client %s", group->client->name);
	switch (group->status) {
	case EDGETPU_DEVICE_GROUP_INITIALIZING:
	case EDGETPU_DEVICE_GROUP_READY:
		break;
	case EDGETPU_DEVICE_GROUP_ERRORED:
		seq_puts(s, " (errored)");
		break;
	case EDGETPU_DEVICE_GROUP_DISBANDED:
		seq_puts(s, ": disbanded\n");
		return;
	}

	if (edgetpu_mmu_domain_detached(etdomain))
		seq_puts(s, " pasid detached:\n");
	else
		seq_printf(s, " pasid %u:\n", etdomain->pasid);

	if (group->host_mappings.count) {
		seq_printf(s, "host buffer mappings (%zd):\n", group->host_mappings.count);
		edgetpu_mappings_show(&group->host_mappings, s);
	}
	if (group->dmabuf_mappings.count) {
		seq_printf(s, "dma-buf buffer mappings (%zd):\n", group->dmabuf_mappings.count);
		edgetpu_mappings_show(&group->dmabuf_mappings, s);
	}
}

int edgetpu_device_group_send_vii_command(struct edgetpu_device_group *group, void *cmd,
					  struct gcip_fence_array *in_fence_array,
					  struct gcip_fence_array *out_fence_array,
					  struct iif_fence *iif_dma_fence,
					  struct edgetpu_ikv_additional_info *additional_info,
					  void (*release_callback)(void *), void *release_data)
{
	struct edgetpu_dev *etdev = group->etdev;
	struct edgetpu_iommu_domain *etdomain;
	int ret = edgetpu_pm_get_if_powered(etdev, true);

	if (ret) {
		etdev_err(etdev, "Unable to send VII command, TPU block is off");
		return ret;
	}

	down_read(&group->lock);
	mutex_lock(&group->vii_lock);
	if (!edgetpu_device_group_is_ready(group) || edgetpu_device_group_is_errored(group)) {
		etdev_err(etdev, "Unable to send VII command, device group is %s",
			  edgetpu_device_group_is_errored(group) ? "errored" : "disbanded");
		ret = -EINVAL;
		goto unlock_group;
	}

	etdomain = edgetpu_group_domain_locked(group);
	if (!etdomain) {
		etdev_err(etdev, "Unable to send VII command, device group has no domain");
		ret = -EINVAL;
		goto unlock_group;
	}

	if (!atomic_add_unless(&group->available_vii_credits, -1, 0)) {
		ret = -EBUSY;
		goto unlock_group;
	}

	edgetpu_vii_command_set_client_id(cmd, etdomain->pasid);
	ret = edgetpu_ikv_send_cmd(etdev->etikv, cmd, group, in_fence_array, out_fence_array,
				   iif_dma_fence, additional_info, release_callback, release_data);
	/* Refund credit if command failed to send. */
	if (ret)
		atomic_inc(&group->available_vii_credits);

unlock_group:
	mutex_unlock(&group->vii_lock);
	up_read(&group->lock);
	edgetpu_pm_put(etdev);
	return ret;
}

int edgetpu_device_group_get_vii_response(struct edgetpu_device_group *group, void *resp)
{
	struct edgetpu_ikv_response *ikv_resp;
	struct edgetpu_ikv_rsp_mgr *rsp_mgr = group->rsp_mgr;
	unsigned long flags;
	int ret = 0;

	down_read(&group->lock);
	mutex_lock(&group->vii_lock);
	if (!edgetpu_device_group_is_ready(group) || edgetpu_device_group_is_errored(group)) {
		ret = -EINVAL;
		goto unlock_group;
	}

	spin_lock_irqsave(&rsp_mgr->list_lock, flags);

	if (list_empty(&rsp_mgr->rslt_list)) {
		ret = -ENOENT;
		spin_unlock_irqrestore(&rsp_mgr->list_lock, flags);
		goto unlock_group;
	}

	ikv_resp = list_first_entry(&rsp_mgr->rslt_list, typeof(struct edgetpu_ikv_response),
				    list_entry);
	list_del(&ikv_resp->list_entry);

	spin_unlock_irqrestore(&rsp_mgr->list_lock, flags);

	memcpy(resp, ikv_resp->resp, edgetpu_vii_response_packet_size());
	gcip_mailbox_awaiter_put(&ikv_resp->gcip_awaiter);

unlock_group:
	mutex_unlock(&group->vii_lock);
	up_read(&group->lock);
	return ret;
}

/*
 * Set @group status as errored, set the error mask, and notify the runtime of
 * the fatal error event on the group.
 */
void edgetpu_group_fatal_error_notify(struct edgetpu_device_group *group, uint error_mask)
{
	etdev_warn(group->etdev, "notify client %s error %#x", group->client->name, error_mask);
	down_write(&group->lock);
	/* Only non-disbanded groups may have handshake with the FW, mark them as errored. */
	if (edgetpu_device_group_is_ready(group))
		group->status = EDGETPU_DEVICE_GROUP_ERRORED;
	group->fatal_errors |= error_mask;

	/*
	 * If the firmware is not able to return error responses, cancel all pending commands.
	 * Intentionally call this function while holding @group->lock to prevent any possible race
	 * conditions between canceling commands and the runtime submitting commands.
	 */
	if (edgetpu_firmware_is_not_responding(error_mask))
		edgetpu_ikv_cancel(group, error_mask);

	up_write(&group->lock);
	edgetpu_group_notify(group, EDGETPU_EVENT_FATAL_ERROR);
}

/*
 * For each group active on @etdev: set the group status as errored, set the
 * error mask, and notify the runtime of the fatal error event.
 */
void edgetpu_fatal_error_notify(struct edgetpu_dev *etdev, uint error_mask)
{
	struct edgetpu_device_group *group;
	struct edgetpu_list_group *g;
	bool powered_on;

	/*
	 * Flushing ikv responses and reinitializing iif mailbox requires the TPU to be powered on.
	 */
	powered_on = pm_runtime_get_if_active(etdev->dev) > 0;

	/* Consume all arrived responses first before each group cancels pending commands. */
	if (powered_on && edgetpu_firmware_is_not_responding(error_mask))
		edgetpu_ikv_flush_responses(etdev->etikv);

	mutex_lock(&etdev->groups_lock);
	etdev_for_each_group(etdev, g, group) {
		if (edgetpu_device_group_is_disbanded(group))
			continue;
		edgetpu_group_fatal_error_notify(group, error_mask);
	}
	mutex_unlock(&etdev->groups_lock);

	if (powered_on) {
		/* Flush any pending IIF signals. */
		edgetpu_iif_reinit_mailbox(etdev->etiif);
		pm_runtime_put(etdev->dev);
	}
}

uint edgetpu_group_get_fatal_errors(struct edgetpu_device_group *group)
{
	uint fatal_errors;

	down_write(&group->lock);
	fatal_errors = edgetpu_group_get_fatal_errors_locked(group);
	up_write(&group->lock);
	return fatal_errors;
}

void edgetpu_group_detach_mailbox_locked(struct edgetpu_device_group *group)
{
	if (edgetpu_group_mailbox_detached_locked(group))
		return;

	if (group->mailbox_detachable)
		edgetpu_mmu_detach_domain(group->etdev, group->etdomain);

	group->mailbox_attached = false;
}

void edgetpu_group_close_and_detach_mailbox(struct edgetpu_device_group *group)
{
	down_write(&group->lock);
	/*
	 * Only a non-disbanded group may have mailbox attached.
	 *
	 * Detaching mailbox for an errored group is also fine.
	 */
	if (is_ready_or_errored(group)) {
		edgetpu_group_deactivate(group);
		/*
		 * TODO(b/312575591) Flush pending reverse KCI traffic before detaching the mailbox.
		 * This is necessary since detaching the mailbox may change the group's domain's
		 * PASID, which some rKCI commands use to identify a client.
		 *
		 * The group must be unlocked in case the rKCI handlers need the lock. This is safe
		 * because this thread continues to hold the owning `client`'s lock, preventing any
		 * other threads from trying to reattach the mailbox via the
		 * EDGETPU_ACQUIRE_WAKE_LOCK ioctls.
		 */
		up_write(&group->lock);
		edgetpu_kci_flush_rkci(group->etdev);
		down_write(&group->lock);
		edgetpu_group_detach_mailbox_locked(group);
		edgetpu_group_deactivate_external_mailbox(group);
	}
	up_write(&group->lock);
}

int edgetpu_group_attach_mailbox_locked(struct edgetpu_device_group *group)
{
	int ret;

	if (!edgetpu_group_mailbox_detached_locked(group))
		return 0;

	if (group->mailbox_detachable) {
		ret = edgetpu_mmu_attach_domain(group->etdev, group->etdomain);
		if (ret)
			return ret;
	}

	group->mailbox_attached = true;

	return 0;
}

int edgetpu_group_attach_and_open_mailbox(struct edgetpu_device_group *group)
{
	int ret = 0;

	down_write(&group->lock);
	/*
	 * Only attaching mailbox for groups in "ready" status.
	 * Don't attach mailbox for errored groups.
	 */
	if (!edgetpu_device_group_is_ready(group))
		goto out_unlock;
	ret = edgetpu_group_attach_mailbox_locked(group);
	if (ret)
		goto out_unlock;
	ret = edgetpu_group_activate(group);
	if (ret)
		goto error_detach;
	ret = edgetpu_group_activate_external_mailbox(group);
	if (!ret)
		goto out_unlock;

	edgetpu_group_deactivate(group);
error_detach:
	edgetpu_group_detach_mailbox_locked(group);
out_unlock:
	up_write(&group->lock);
	return ret;
}

void edgetpu_handle_client_fatal_error_notify(struct edgetpu_dev *etdev, u32 client_id)
{
	u32 client_pasid = FIELD_GET(CLIENT_ID_PASID, client_id);
	u32 client_realm = FIELD_GET(CLIENT_ID_REALM, client_id);
	u32 client_vm = FIELD_GET(CLIENT_ID_VM, client_id);
	struct edgetpu_device_group *group;

	etdev_err(etdev, "firmware reported fatal error for realm %u vm %u pasid %u", client_realm,
		  client_vm, client_pasid);
	if (client_realm != CLIENT_REALM_NS)
		return;
	group = get_group_by_id(etdev, client_id, EDGETPU_ID_TYPE_CLIENT_ID);
	if (!group) {
		etdev_warn(etdev, "pasid %u group not found", client_pasid);
		return;
	}
	edgetpu_group_fatal_error_notify(group, EDGETPU_ERROR_CLIENT_CONTEXT_CRASH);
	edgetpu_device_group_put(group);
}

void edgetpu_handle_client_inactivity_timeout(struct edgetpu_dev *etdev, u32 fw_client_id)
{
	u32 client_pasid = FIELD_GET(CLIENT_ID_PASID, fw_client_id);
	u32 client_realm = FIELD_GET(CLIENT_ID_REALM, fw_client_id);
	u32 client_vm = FIELD_GET(CLIENT_ID_VM, fw_client_id);
	struct edgetpu_device_group *group;
	struct edgetpu_client *client;
	struct timespec64 wake_duration;

	etdev_warn(etdev, "firmware reported inactivity timeout for realm %u vm %u pasid %u",
		   client_realm, client_vm, client_pasid);
	if (client_realm != CLIENT_REALM_NS)
		return;
	group = get_group_by_id(etdev, fw_client_id, EDGETPU_ID_TYPE_CLIENT_ID);
	if (!group) {
		etdev_warn(etdev, "inactive pasid %u group not found", client_pasid);
		return;
	}

	client = group->client;
	wake_duration.tv_sec = 0;

	if (client->wakelock.req_count) {
		ktime_get_ts64(&wake_duration);
		wake_duration =
			timespec64_sub(wake_duration, client->wakelock.current_acquire_timestamp);
		etdev_err(etdev,
			  "%sinactive client %s tgid %d holding TPU wakelock for %lds\n",
			  client->inactive_count ? "repeat " : "", client->name, client->tgid,
			  (unsigned long)wake_duration.tv_sec);

		/*
		 * If already sent one "holding wakelock and inactive" error then client isn't
		 * responding, force release its wakelocks.
		 */
		if (group->fatal_errors & EDGETPU_ERROR_CLIENT_INACTIVITY_TIMEOUT)
			edgetpu_client_force_release_wakelocks_async(client);
		edgetpu_group_fatal_error_notify(group, EDGETPU_ERROR_CLIENT_INACTIVITY_TIMEOUT);

		/* Generate one debug dump on the first repeat notification. */
		if (client->inactive_count == 1) {
			int ret = edgetpu_pm_get(etdev);

			if (!ret) {
				edgetpu_debug_dump(etdev, DUMP_REASON_CLIENT_ZOMBIE);
				edgetpu_pm_put(etdev);
			}
		}
	} else {
		etdev_warn(etdev, "%sinactive client %s tgid %d\n",
			   client->inactive_count ? "repeat " : "", client->name, client->tgid);
	}
	client->inactive_count++;
	edgetpu_device_group_put(group);
}

void edgetpu_handle_job_lockup(struct edgetpu_dev *etdev, u16 vcid)
{
	struct edgetpu_device_group *group;

	etdev_err(etdev, "firmware-detected job lockup on VCID %u", vcid);
	group = get_group_by_id(etdev, vcid, EDGETPU_ID_TYPE_VCID);
	if (!group) {
		etdev_warn(etdev, "VCID %u group not found", vcid);
		return;
	}
	edgetpu_group_fatal_error_notify(group, EDGETPU_ERROR_RUNTIME_TIMEOUT);
	edgetpu_device_group_put(group);
}

int edgetpu_device_group_track_fence_task(struct edgetpu_device_group *group,
					  struct task_struct *task)
{
	struct pending_command_task *pending_task;
	unsigned long flags;

	pending_task = kzalloc(sizeof(*pending_task), GFP_KERNEL);
	if (!pending_task)
		return -ENOMEM;

	pending_task->task = task;

	spin_lock_irqsave(&group->pending_cmd_tasks_lock, flags);
	list_add_tail(&pending_task->list_entry, &group->pending_cmd_tasks);
	spin_unlock_irqrestore(&group->pending_cmd_tasks_lock, flags);

	return 0;
}

void edgetpu_device_group_untrack_fence_task(struct edgetpu_device_group *group,
					     struct task_struct *task)
{
	struct list_head *cur, *nxt;
	struct pending_command_task *pending_task;
	unsigned long flags;

	spin_lock_irqsave(&group->pending_cmd_tasks_lock, flags);

	if (group->is_clearing_pending_commands) {
		spin_unlock_irqrestore(&group->pending_cmd_tasks_lock, flags);
		/*
		 * Wait until the release handler has requested this task stop so it doesn't
		 * disappear out from under the release handler.
		 */
		while (!kthread_should_stop())
			msleep(20);
		return;
	}

	list_for_each_safe(cur, nxt, &group->pending_cmd_tasks) {
		pending_task = container_of(cur, struct pending_command_task, list_entry);
		if (pending_task->task == task) {
			list_del(&pending_task->list_entry);
			kfree(pending_task);
			goto out;
		}
	}

	etdev_err(group->etdev, "Attempt to untrack task which was not being tracked");

out:
	spin_unlock_irqrestore(&group->pending_cmd_tasks_lock, flags);
}

/*
 * Return the group with pasid @pasid for device @etdev, with a reference held on the group, or
 * NULL if no group with that pasid is found.
 */
static struct edgetpu_device_group *get_group_by_pasid(struct edgetpu_dev *etdev, uint pasid)
{
	struct edgetpu_device_group *group = NULL;
	struct edgetpu_device_group *tgroup;
	struct edgetpu_list_group *g;

	if (pasid == IOMMU_PASID_INVALID || !pasid)
		return NULL;

	mutex_lock(&etdev->groups_lock);
	etdev_for_each_group(etdev, g, tgroup) {
		if (tgroup->etdomain && tgroup->etdomain->pasid == pasid) {
			group = edgetpu_device_group_get(tgroup);
			break;
		}
	}
	mutex_unlock(&etdev->groups_lock);
	return group;
}

/*
 * Currently always returns a negative error to indicate caller should proceed with fault error
 * processing.
 */
int edgetpu_device_group_handle_fault(struct edgetpu_dev *etdev, u64 iova, uint pasid, bool write)
{
	struct edgetpu_device_group *group;
	struct edgetpu_mapping *map;
	bool dmabuf = false;

	group = get_group_by_pasid(etdev, pasid);
	if (!group)
		return -EIO;

	if (!group->iommu_fault) {
		edgetpu_eventlog_event(etdev, EVENTLOG_EVENT_CLIENT_ACCESS_FAULT, group->client);
		group->iommu_fault = true;
	}

	edgetpu_mapping_lock(&group->host_mappings);
	map = edgetpu_mapping_find_iova_range(&group->host_mappings, iova);
	if (!map) {
		edgetpu_mapping_unlock(&group->host_mappings);
		dmabuf = true;
		edgetpu_mapping_lock(&group->dmabuf_mappings);
		map = edgetpu_mapping_find_iova_range(&group->dmabuf_mappings, iova);
	}
	if (!map) {
		edgetpu_mapping_unlock(&group->dmabuf_mappings);
		if (EDGETPU_REPORT_PAGE_FAULT_ERRORS)
			etdev_warn(etdev,
				   "fault client=%s iova=%#llx pasid=%u write=%u not mapped\n",
				   group->client->name, iova, pasid, write);
		edgetpu_device_group_put(group);
		return -EIO;
	}

	if (map->trimmed)
		etdev_warn(etdev, "fault on trimmed buffer client=%s iova=%#llx pasid=%u\n",
			   group->client->name, iova, pasid);
	else
		etdev_warn(etdev,
			   "fault client=%s iova=%#llx pasid=%u write=%u mapped %s dbgf=%#x\n",
			   group->client->name, iova, pasid, write,
			   edgetpu_dma_dir_rw_s(map->gcip_mapping->dir),
			   map->gcip_mapping->map_debug_flags);
	if (dmabuf)
		edgetpu_mapping_unlock(&group->dmabuf_mappings);
	else
		edgetpu_mapping_unlock(&group->host_mappings);
	edgetpu_device_group_put(group);
	return -EIO;
}
