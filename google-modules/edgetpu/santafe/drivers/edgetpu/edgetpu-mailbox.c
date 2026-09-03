/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Utility functions of mailbox protocol for Edge TPU ML accelerator.
 *
 * Copyright (C) 2019-2026 Google LLC
 */

#include <asm/page.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mmzone.h> /* MAX_ORDER_NR_PAGES */
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <gcip/gcip-memory.h>

#include "edgetpu-client.h"
#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-dt-mailbox-adapter.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-pm.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"

void edgetpu_mailbox_set_cmd_queue_tail(struct edgetpu_mailbox *mailbox, u32 value)
{
	mailbox->cmd_queue_tail = value;
	EDGETPU_MAILBOX_CMD_QUEUE_WRITE_SYNC(mailbox, tail, value);
}

void edgetpu_mailbox_set_resp_queue_head(struct edgetpu_mailbox *mailbox, u32 value)
{
	mailbox->resp_queue_head = value;
	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, head, value);
}

void edgetpu_mailbox_inc_cmd_queue_tail(struct edgetpu_mailbox *mailbox, u32 inc)
{
	u32 new_tail;

	new_tail = gcip_circ_queue_inc(mailbox->cmd_queue_tail, inc, mailbox->cmd_queue_size,
				       CIRC_QUEUE_WRAP_BIT);
	edgetpu_mailbox_set_cmd_queue_tail(mailbox, new_tail);
}

void edgetpu_mailbox_inc_resp_queue_head(struct edgetpu_mailbox *mailbox, u32 inc)
{
	u32 new_head;

	new_head = gcip_circ_queue_inc(mailbox->resp_queue_head, inc, mailbox->resp_queue_size,
				       CIRC_QUEUE_WRAP_BIT);
	edgetpu_mailbox_set_resp_queue_head(mailbox, new_head);
}

/*
 * The queue size of edgetpu_mailbox_attr has units in KB, convert it to use the
 * element size here.
 *
 * Returns a negative errno on error, or the converted size.
 */
static int convert_runtime_queue_size_to_fw(u32 queue_size, u32 element_size)
{
	const u32 runtime_unit = 1024;
	u32 ret;

	/* zero size is not allowed */
	if (queue_size == 0 || element_size == 0)
		return -EINVAL;
	/* A quick check to prevent the queue allocation failure. */
	if (queue_size > (MAX_ORDER_NR_PAGES << PAGE_SHIFT) / runtime_unit)
		return -ENOMEM;
	/*
	 * Kernel doesn't care whether queue_size * runtime_unit is a multiple
	 * of element_size.
	 */
	ret = queue_size * runtime_unit / element_size;
	/* hardware limitation */
	if (ret == 0 || ret > CIRC_QUEUE_MAX_SIZE(CIRC_QUEUE_WRAP_BIT))
		return -EINVAL;
	return ret;
}

int edgetpu_mailbox_validate_attr(const struct edgetpu_mailbox_attr *attr)
{
	int size;

	size = convert_runtime_queue_size_to_fw(attr->cmd_queue_size, attr->sizeof_cmd);
	if (size < 0)
		return size;
	size = convert_runtime_queue_size_to_fw(attr->resp_queue_size, attr->sizeof_resp);
	if (size < 0)
		return size;
	return 0;
}

static int edgetpu_mailbox_alloc_queue(struct edgetpu_dev *etdev, u32 queue_size, u32 unit,
				       struct gcip_memory *mem)
{
	u32 size = unit * queue_size;

	/* Align queue size to page size for TPU MMU map. */
	size = __ALIGN_KERNEL(size, PAGE_SIZE);
	return edgetpu_iremap_alloc(etdev, size, mem);
}

static void edgetpu_mailbox_free_queue(struct edgetpu_dev *etdev, struct gcip_memory *mem)
{
	if (!mem->virt_addr)
		return;

	edgetpu_iremap_free(etdev, mem);
}

/*
 * Creates a mailbox manager, one edgetpu device has one manager.
 */
struct edgetpu_mailbox_manager *
edgetpu_mailbox_create_mgr(struct edgetpu_dev *etdev,
			   const struct edgetpu_mailbox_manager_desc *desc)
{
	struct edgetpu_mailbox_manager *mgr;

	mgr = devm_kzalloc(etdev->dev, sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return ERR_PTR(-ENOMEM);

	mgr->etdev = etdev;
	mgr->num_ext_mailbox = desc->num_ext_mailbox;
	mgr->ext_index_from = desc->ext_mailbox_start;
	mgr->ext_index_to = mgr->ext_index_from + desc->num_ext_mailbox;

	mgr->ext_mailboxes = devm_kcalloc(etdev->dev, mgr->num_ext_mailbox,
					  sizeof(*mgr->ext_mailboxes), GFP_KERNEL);
	if (!mgr->ext_mailboxes)
		return ERR_PTR(-ENOMEM);
	rwlock_init(&mgr->ext_mailboxes_lock);
	mutex_init(&mgr->open_devices.lock);

	return mgr;
}

/* All requested external mailboxes will be disabled and freed. */
void edgetpu_mailbox_remove_ext_mailboxes(struct edgetpu_mailbox_manager *mgr, bool hwaccessok)
{
	uint i;
	unsigned long flags;

	if (IS_ERR_OR_NULL(mgr))
		return;
	write_lock_irqsave(&mgr->ext_mailboxes_lock, flags);
	for (i = 0; i < mgr->num_ext_mailbox; i++) {
		struct edgetpu_mailbox *mailbox = mgr->ext_mailboxes[i];

		if (mailbox) {
			/* Leave mailbox CSRs alone if not known powered up. */
			if (hwaccessok)
				edgetpu_mailbox_disable(mailbox);
			kfree(mailbox);
			mgr->ext_mailboxes[i] = NULL;
		}
	}
	write_unlock_irqrestore(&mgr->ext_mailboxes_lock, flags);
}

void edgetpu_mailbox_reset_ext_mailboxes(struct edgetpu_mailbox_manager *mgr)
{
	uint i;
	unsigned long flags;

	write_lock_irqsave(&mgr->ext_mailboxes_lock, flags);
	/* Reset all the allocated external mailboxes. */
	for (i = 0; i < mgr->num_ext_mailbox; i++) {
		struct edgetpu_mailbox *mbox = mgr->ext_mailboxes[i];

		if (!mbox)
			continue;
		edgetpu_mailbox_reset(mbox);
		edgetpu_mailbox_disable(mbox);
		edgetpu_mailbox_init_doorbells(mbox);
	}
	write_unlock_irqrestore(&mgr->ext_mailboxes_lock, flags);
}

static void edgetpu_mailbox_init_external_mailbox(struct edgetpu_external_mailbox *ext_mailbox)
{
	struct edgetpu_mailbox_attr attr;
	struct edgetpu_mailbox *mailbox;
	struct edgetpu_mailbox_descriptor *desc;
	uint i;

	attr = ext_mailbox->attr;

	for (i = 0; i < ext_mailbox->count; i++) {
		desc = &ext_mailbox->descriptors[i];
		mailbox = desc->mailbox;
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, priority, attr.priority);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_tail_doorbell_enable,
					      attr.cmdq_tail_doorbell);
		edgetpu_mailbox_set_queue(mailbox, GCIP_MAILBOX_CMD_QUEUE,
					  desc->cmd_queue_mem.dma_addr, attr.cmd_queue_size);
		edgetpu_mailbox_set_queue(mailbox, GCIP_MAILBOX_RESP_QUEUE,
					  desc->resp_queue_mem.dma_addr, attr.resp_queue_size);
		edgetpu_mailbox_enable(mailbox);
	}
}

void edgetpu_mailbox_reinit_external_mailbox(struct edgetpu_device_group *group)
{
	struct edgetpu_external_mailbox *ext_mailbox = group->ext_mailbox;

	if (!ext_mailbox)
		return;

	etdev_dbg(group->etdev, "Restoring external attached %d mailboxes\n", ext_mailbox->count);
	edgetpu_mailbox_init_external_mailbox(ext_mailbox);
}

void edgetpu_mailbox_restore_active_ext_mailbox_queues(struct edgetpu_dev *etdev)
{
	struct edgetpu_device_group *group;
	struct edgetpu_device_group **groups;
	size_t i, n = 0;

	mutex_lock(&etdev->groups_lock);
	groups = kmalloc_array(etdev->n_groups, sizeof(*groups), GFP_KERNEL);
	if (unlikely(!groups)) {
		/*
		 * Either the runtime is misbehaving (creates tons of groups),
		 * or the system is indeed OOM - we give up this restore
		 * process, which makes the runtime unable to communicate with
		 * the device through VII.
		 */
		mutex_unlock(&etdev->groups_lock);
		return;
	}
	/*
	 * Fetch the groups into an array to restore the VII without holding
	 * etdev->groups_lock. To prevent the potential deadlock that
	 * edgetpu_device_group_add() holds group->lock then etdev->groups_lock.
	 */
	list_for_each_entry(group, &etdev->groups, group_list_node) {
		/*
		 * Quick skip without holding group->lock.
		 * Disbanded groups can never go back to the normal state.
		 */
		if (edgetpu_device_group_is_disbanded(group))
			continue;
		/*
		 * Increase the group reference to prevent the group being
		 * released after we release groups_lock.
		 */
		groups[n++] = edgetpu_device_group_get(group);
	}
	mutex_unlock(&etdev->groups_lock);

	/*
	 * We are not holding @etdev->groups_lock, what may race is:
	 *   1. The group is disbanding and being removed from @etdev.
	 *   2. A new group is added to @etdev and is not yet ready.
	 *   3. A new group is added to @etdev and just became ready.
	 *
	 * For (1.) the group will be marked as DISBANDED, so we check whether
	 * the group is READY before performing re-init.
	 * For (2.), the same check also skips groups still in INITIALIZING state.
	 * For (3.), this re-init is redundant but isn't harmful.  We hold the PM lock and the
	 * racing client must wait for us to release the PM lock before adding a new power up
	 * request / accessing hardware.
	 *
	 * A new group being added that is not captured in groups[] will initialize external mailbox
	 * as usual.
	 */
	for (i = 0; i < n; i++) {
		group = groups[i];
		down_write(&group->lock);
		if (edgetpu_group_ready_and_attached(group)) {
			edgetpu_mailbox_reinit_external_mailbox(group);
		}
		up_write(&group->lock);
		edgetpu_device_group_put(group);
	}
	kfree(groups);
}

static int edgetpu_mailbox_activate_bulk(struct edgetpu_dev *etdev, u32 mailbox_map,
					 u32 client_priv, s16 vcid, bool first_open)
{
	struct edgetpu_handshake *eh = &etdev->mailbox_manager->open_devices;
	int ret = 0;

	mutex_lock(&eh->lock);
	if (mailbox_map & ~eh->fw_state)
		ret = edgetpu_kci_open_device(etdev->etkci, mailbox_map & ~eh->fw_state,
					      client_priv, vcid, first_open);
	if (!ret) {
		eh->state |= mailbox_map;
		eh->fw_state |= mailbox_map;
	}
	mutex_unlock(&eh->lock);
	/*
	 * We are observing OPEN_DEVICE KCI fails while other KCIs (usage update / shutdown) still
	 * succeed and no firmware crash is reported. Kick off the firmware restart when we are
	 * facing this and hope this can rescue the device from the bad state.
	 */
	if (ret == -ETIMEDOUT)
		edgetpu_watchdog_bite(etdev);
	return ret;
}

static int edgetpu_mailbox_deactivate_bulk(struct edgetpu_dev *etdev, u32 mailbox_map)
{
	struct edgetpu_handshake *eh = &etdev->mailbox_manager->open_devices;
	int ret = 0;

	mutex_lock(&eh->lock);
	if (mailbox_map & eh->fw_state)
		ret = edgetpu_kci_close_device(etdev->etkci, mailbox_map & eh->fw_state);

	if (!ret) {
		eh->state &= ~mailbox_map;
		eh->fw_state &= ~mailbox_map;
	}

	mutex_unlock(&eh->lock);
	return ret;
}

void edgetpu_handshake_clear_fw_state(struct edgetpu_handshake *eh)
{
	mutex_lock(&eh->lock);
	eh->fw_state = 0;
	mutex_unlock(&eh->lock);
}

static int edgetpu_mailbox_external_alloc_queue_batch(struct edgetpu_external_mailbox *ext_mailbox)
{
	int ret, i;
	struct edgetpu_mailbox_attr attr;
	struct edgetpu_mailbox_descriptor *desc;
	struct edgetpu_dev *etdev = ext_mailbox->etdev;

	attr = ext_mailbox->attr;

	for (i = 0; i < ext_mailbox->count; i++) {
		desc = &ext_mailbox->descriptors[i];
		ret = edgetpu_mailbox_alloc_queue(etdev, attr.cmd_queue_size, attr.sizeof_cmd,
						  &desc->cmd_queue_mem);
		if (ret)
			goto undo;

		ret = edgetpu_mailbox_alloc_queue(etdev, attr.resp_queue_size, attr.sizeof_resp,
						  &desc->resp_queue_mem);
		if (ret) {
			edgetpu_mailbox_free_queue(etdev, &desc->cmd_queue_mem);
			goto undo;
		}
	}
	return 0;
undo:
	while (i--) {
		desc = &ext_mailbox->descriptors[i];
		edgetpu_mailbox_free_queue(etdev, &desc->cmd_queue_mem);
		edgetpu_mailbox_free_queue(etdev, &desc->resp_queue_mem);
	}
	return ret;
}

static void edgetpu_mailbox_external_free_queue_batch(struct edgetpu_external_mailbox *ext_mailbox)
{
	u32 i;
	struct edgetpu_mailbox_descriptor *desc;
	struct edgetpu_dev *etdev = ext_mailbox->etdev;

	for (i = 0; i < ext_mailbox->count; i++) {
		desc = &ext_mailbox->descriptors[i];
		edgetpu_mailbox_free_queue(etdev, &desc->cmd_queue_mem);
		edgetpu_mailbox_free_queue(etdev, &desc->resp_queue_mem);
	}
}

/*
 * Checks if the indexes given for external mailboxes are in range of mailbox
 * manager(@mgr) managing the external mailboxes.
 */
static bool edgetpu_mailbox_external_check_range(struct edgetpu_mailbox_manager *mgr,
						 const int start, const int end)
{
	return (start <= end) && (mgr->ext_index_from <= start && mgr->ext_index_to > end);
}

/*
 * Allocates external mailboxes according to @ext_mailbox_req object and
 * associate it with @group.
 *
 * Caller should hold @group->lock for writing.
 */
static int edgetpu_mailbox_external_alloc(struct edgetpu_device_group *group,
					  struct edgetpu_external_mailbox_req *ext_mailbox_req)
{
	u32 i, j = 0, bmap, start, end;
	struct edgetpu_mailbox_manager *mgr = group->etdev->mailbox_manager;
#if !EDGETPU_USE_CMF
	void __iomem *csr_base;
#endif
	struct edgetpu_mailbox *mailbox;
	int ret = 0, count;
	struct edgetpu_external_mailbox *ext_mailbox;
	struct edgetpu_mailbox_attr attr;
	unsigned long flags;

	if (!edgetpu_device_group_is_ready(group))
		return -EINVAL;

	if (group->ext_mailbox)
		return -EEXIST;

	if (!ext_mailbox_req)
		return -EINVAL;

	ret = edgetpu_mailbox_validate_attr(&ext_mailbox_req->attr);
	if (ret)
		return ret;

	attr = ext_mailbox_req->attr;

	if (!edgetpu_mailbox_external_check_range(mgr, ext_mailbox_req->start,
						  ext_mailbox_req->end))
		return -ERANGE;

	ext_mailbox = kzalloc(sizeof(*ext_mailbox), GFP_KERNEL);
	if (!ext_mailbox)
		return -ENOMEM;

	bmap = ext_mailbox_req->mbox_map;
	count = __sw_hweight32(bmap);

	ext_mailbox->descriptors =
		kcalloc(count, sizeof(struct edgetpu_mailbox_descriptor), GFP_KERNEL);
	if (!ext_mailbox->descriptors) {
		kfree(ext_mailbox);
		return -ENOMEM;
	}

	ext_mailbox->attr = attr;
	ext_mailbox->etdev = group->etdev;
	ext_mailbox->mbox_type = ext_mailbox_req->mbox_type;

	start = ext_mailbox_req->start;
	end = ext_mailbox_req->end;

	write_lock_irqsave(&mgr->ext_mailboxes_lock, flags);
	while (bmap) {
		i = ffs(bmap) + start - 1;
		if (i > end) {
			ret = -EINVAL;
			goto unlock;
		}
		if (mgr->ext_mailboxes[i - mgr->ext_index_from]) {
			ret = -EBUSY;
			goto unlock;
		}
		bmap = bmap & (bmap - 1);
	}

	bmap = ext_mailbox_req->mbox_map;
	while (bmap) {
		i = ffs(bmap) + start - 1;
#if EDGETPU_USE_CMF
		/* TODO(b/517784282): Update once we determine where external metadata should be */
		mailbox = ERR_PTR(-ENOMEM);
#else
		csr_base = edgetpu_mailbox_get_ext_csr_base(mgr->etdev, i);
		/* External mailboxes do not have doorbells to the AP. */
		mailbox = edgetpu_mailbox_alloc(mgr->etdev, csr_base, /*irq=*/0, i,
						/*msi_enabled=*/false);
#endif /* EDGETPU_USE_CMF */
		if (!IS_ERR(mailbox)) {
			mgr->ext_mailboxes[i - mgr->ext_index_from] = mailbox;
			ext_mailbox->descriptors[j++].mailbox = mailbox;
		} else {
			ret = PTR_ERR(mailbox);
			goto release;
		}
		bmap = bmap & (bmap - 1);
	}

	ext_mailbox->count = j;

	ret = edgetpu_mailbox_external_alloc_queue_batch(ext_mailbox);
	if (ret)
		goto release;
	write_unlock_irqrestore(&mgr->ext_mailboxes_lock, flags);

	for (i = 0; i < count; i++) {
		mailbox = ext_mailbox->descriptors[i].mailbox;
		mailbox->internal.group = edgetpu_device_group_get(group);
	}
	group->ext_mailbox = ext_mailbox;
	return 0;
release:
	while (j--) {
		mailbox = ext_mailbox->descriptors[j].mailbox;
		mgr->ext_mailboxes[mailbox->mailbox_id - mgr->ext_index_from] = NULL;
		kfree(mailbox);
	}
unlock:
	write_unlock_irqrestore(&mgr->ext_mailboxes_lock, flags);
	kfree(ext_mailbox->descriptors);
	kfree(ext_mailbox);
	return ret;
}

/* Caller must hold @group->lock for writing. */
static void edgetpu_mailbox_external_free(struct edgetpu_device_group *group)
{
	struct edgetpu_mailbox_manager *mgr;
	struct edgetpu_mailbox *mailbox;
	struct edgetpu_external_mailbox *ext_mailbox;
	u32 i;

	ext_mailbox = group->ext_mailbox;
	if (!ext_mailbox)
		return;

	mgr = ext_mailbox->etdev->mailbox_manager;

	edgetpu_mailbox_external_free_queue_batch(ext_mailbox);

	for (i = 0; i < ext_mailbox->count; i++) {
		mailbox = ext_mailbox->descriptors[i].mailbox;
		edgetpu_device_group_put(mailbox->internal.group);
		mgr->ext_mailboxes[mailbox->mailbox_id - mgr->ext_index_from] = NULL;
		kfree(mailbox);
	}

	kfree(ext_mailbox->descriptors);
	kfree(ext_mailbox);
	group->ext_mailbox = NULL;
}

static int edgetpu_mailbox_external_alloc_enable(struct edgetpu_client *client,
						 struct edgetpu_external_mailbox_req *req)
{
	int ret = 0;
	struct edgetpu_device_group *group;

	mutex_lock(&client->group_lock);
	if (!client->group) {
		mutex_unlock(&client->group_lock);
		return -EINVAL;
	}
	group = edgetpu_device_group_get(client->group);
	mutex_unlock(&client->group_lock);

	if (edgetpu_pm_get_if_powered(group->etdev, true)) {
		down_write(&group->lock);
		ret = edgetpu_mailbox_external_alloc(group, req);
		up_write(&group->lock);
		goto out;
	} else {
		down_write(&group->lock);
		ret = edgetpu_mailbox_external_alloc(group, req);
		if (ret) {
			up_write(&group->lock);
			goto err;
		}
		edgetpu_mailbox_init_external_mailbox(group->ext_mailbox);
		ret = edgetpu_mailbox_activate_external_mailbox(group);
		up_write(&group->lock);
		edgetpu_pm_put(group->etdev);
		goto out;
	}
err:
	edgetpu_pm_put(group->etdev);
out:
	edgetpu_device_group_put(group);
	return ret;
}

static int edgetpu_mailbox_external_disable_free(struct edgetpu_client *client)
{
	struct edgetpu_device_group *group;

	mutex_lock(&client->group_lock);
	if (!client->group) {
		mutex_unlock(&client->group_lock);
		return -EINVAL;
	}
	group = edgetpu_device_group_get(client->group);
	mutex_unlock(&client->group_lock);

	if (edgetpu_pm_get_if_powered(group->etdev, true)) {
		down_write(&group->lock);
		edgetpu_mailbox_external_free(group);
		up_write(&group->lock);
	} else {
		down_write(&group->lock);
		edgetpu_mailbox_external_disable_free_locked(group);
		up_write(&group->lock);
		edgetpu_pm_put(group->etdev);
	}

	edgetpu_device_group_put(group);
	return 0;
}

void edgetpu_mailbox_external_disable_free_locked(struct edgetpu_device_group *group)
{
	if (pm_runtime_get_if_active(group->etdev->dev) > 0) {
		int ret;

		ret = edgetpu_mailbox_deactivate_external_mailbox(group);
		if (!ret)
			edgetpu_mailbox_disable_external_mailbox(group);
		pm_runtime_put(group->etdev->dev);
	}
	edgetpu_mailbox_external_free(group);
}

static int edgetpu_mailbox_external_enable_by_id(struct edgetpu_client *client, int mailbox_id,
						 u32 client_priv)
{
	int ret;

	if (!edgetpu_wakelock_lock(client)) {
		etdev_err(client->etdev, "Enabling mailbox %d needs wakelock acquired\n",
			  mailbox_id);
		edgetpu_wakelock_unlock(client);
		return -EAGAIN;
	}

	etdev_dbg(client->etdev, "Enabling mailbox: %d\n", mailbox_id);

	ret = edgetpu_mailbox_activate_bulk(client->etdev, BIT(mailbox_id), client_priv, -1, false);
	if (ret)
		etdev_err(client->etdev, "client %s activate mailbox %d failed: %d", client->name,
			  mailbox_id, ret);
	else
		edgetpu_wakelock_inc_event_locked(client, EDGETPU_WAKELOCK_EVENT_EXT_MAILBOX);
	edgetpu_wakelock_unlock(client);
	return ret;
}

static int edgetpu_mailbox_external_disable_by_id(struct edgetpu_client *client, int mailbox_id)
{
	int ret;

	/*
	 * A successful enable_ext() increases the wakelock event which prevents wakelock being
	 * released, so theoretically the check fail here can only happen when enable_ext() is
	 * failed or not called before.
	 */
	if (!edgetpu_wakelock_lock(client)) {
		etdev_err(client->etdev, "Disabling mailbox %d needs wakelock acquired\n",
			  mailbox_id);
		edgetpu_wakelock_unlock(client);
		return -EAGAIN;
	}

	etdev_dbg(client->etdev, "Disabling mailbox: %d\n", mailbox_id);

	ret = edgetpu_mailbox_deactivate_bulk(client->etdev, BIT(mailbox_id));
	if (ret)
		etdev_err(client->etdev, "client %s deactivate mailbox %d failed: %d", client->name,
			  mailbox_id, ret);
	edgetpu_wakelock_dec_event_locked(client, EDGETPU_WAKELOCK_EVENT_EXT_MAILBOX);
	edgetpu_wakelock_unlock(client);
	return ret;
}

int edgetpu_mailbox_activate_external_mailbox(struct edgetpu_device_group *group)
{
	struct edgetpu_external_mailbox *ext_mailbox = group->ext_mailbox;
	uint vcid = group->vcid;
	u32 mbox_map = 0, i;
	int ret;

	if (!ext_mailbox)
		return -ENOENT;

	for (i = 0; i < ext_mailbox->count; i++)
		mbox_map |= BIT(ext_mailbox->descriptors[i].mailbox->mailbox_id);

	ret = edgetpu_mailbox_activate_bulk(ext_mailbox->etdev, mbox_map,
					    group->mbox_attr.client_priv, vcid, false);

	if (ret)
		etdev_err(group->etdev, "client %s activate external mailbox failed: %d",
			  group->client->name, ret);
	return ret;
}

void edgetpu_mailbox_disable_external_mailbox(struct edgetpu_device_group *group)
{
	u32 i;
	struct edgetpu_external_mailbox *ext_mailbox = group->ext_mailbox;

	if (!ext_mailbox)
		return;

	for (i = 0; i < ext_mailbox->count; i++)
		edgetpu_mailbox_disable(ext_mailbox->descriptors[i].mailbox);
}

int edgetpu_mailbox_deactivate_external_mailbox(struct edgetpu_device_group *group)
{
	u32 i, mbox_map = 0;
	struct edgetpu_external_mailbox *ext_mailbox = group->ext_mailbox;
	int ret;

	if (!ext_mailbox)
		return 0;

	for (i = 0; i < ext_mailbox->count; i++)
		mbox_map |= BIT(ext_mailbox->descriptors[i].mailbox->mailbox_id);

	ret = edgetpu_mailbox_deactivate_bulk(ext_mailbox->etdev, mbox_map);
	if (ret)
		etdev_err(ext_mailbox->etdev,
			  "client %s deactivate external mailbox map %#x failed: %d",
			  group->client->name, mbox_map, ret);
	return ret;
}

int edgetpu_mailbox_enable_ext(struct edgetpu_client *client, int mailbox_id,
			       struct edgetpu_external_mailbox_req *ext_mailbox_req,
			       u32 client_priv)
{
	if (mailbox_id == EDGETPU_MAILBOX_ID_USE_ASSOC)
		return edgetpu_mailbox_external_alloc_enable(client, ext_mailbox_req);
	else
		return edgetpu_mailbox_external_enable_by_id(client, mailbox_id, client_priv);
}

int edgetpu_mailbox_disable_ext(struct edgetpu_client *client, int mailbox_id)
{
	if (mailbox_id == EDGETPU_MAILBOX_ID_USE_ASSOC)
		return edgetpu_mailbox_external_disable_free(client);
	else
		return edgetpu_mailbox_external_disable_by_id(client, mailbox_id);
}
