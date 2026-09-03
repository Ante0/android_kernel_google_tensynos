// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 *
 * This file implements OS-specific backends for NOA IPC.
 */

#include "noa_ipc/noa_ipc_os.h"

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <soc/google/google_dpa_doorbell.h>

#include "google_dpa_internal.h"

// Memory
void NoaIpcFlushDCache(volatile void *addr, s32 dsize)
{
	flush_kernel_vmap_range((void *)addr, dsize);
}

void NoaIpcInvalidateDCache(volatile void *addr, s32 dsize)
{
	invalidate_kernel_vmap_range((void *)addr, dsize);
}

// Notifier Data
struct linux_notifier {
	u32 mailbox_id;
	u32 doorbell_num;
	struct google_dpa_doorbell *doorbell;
	void *context;

	OnNotified on_notified;
	void *data;
};

enum mailbox_client {
	NCP_APC_RPC,
	NEP_APC_RPC,
	MAILBOX_CLIENT_NUM,
};

static const char *const google_dpa_mailbox_name[] = {
	[NCP_APC_RPC] = "ncp_doorbell_0",
	[NEP_APC_RPC] = "nep_doorbell_2",
};

void NoaIpcNotifierInit(void **notifier_data, void *init_data, bool is_source, bool is_sink,
			void *context)
{
	u32 mailbox_info = *(int *)init_data;
	u32 mailbox_id = mailbox_info & 0xFF;
	u32 doorbell_num = (mailbox_info >> 16) & 0xFF;
	struct google_dpa *dpa = (struct google_dpa *)context;
	struct google_dpa_doorbell *doorbell = NULL;

	if (mailbox_id >= MAILBOX_CLIENT_NUM)
		return;

	doorbell = google_dpa_get_doorbell(dpa->dev, google_dpa_mailbox_name[mailbox_id]);
	if (!doorbell) {
		dev_err(dpa->dev, "Cannot get doorbell for IPC");
		return;
	}

	struct linux_notifier *notifier = kmalloc(sizeof(struct linux_notifier), GFP_KERNEL);
	notifier->mailbox_id = mailbox_id;
	notifier->doorbell_num = doorbell_num;
	notifier->doorbell = doorbell;
	notifier->context = context;

	notifier->on_notified = NULL;
	notifier->data = NULL;

	*notifier_data = notifier;
}

void NoaIpcNotifierDeinit(void *notifier_data)
{
	if (!notifier_data)
		return;
	struct linux_notifier *notifier = (struct linux_notifier *)notifier_data;
	google_dpa_doorbell_disable_doorbell(notifier->doorbell, notifier->doorbell_num);
	kfree(notifier_data);
}

void NoaIpcNotify(void *notifier_data)
{
	struct linux_notifier *notifier = (struct linux_notifier *)notifier_data;
	google_dpa_doorbell_ring_mcu(notifier->doorbell, notifier->doorbell_num);
}

static void notifier_callback(void *context)
{
	struct linux_notifier *notifier = (struct linux_notifier *)context;

	notifier->on_notified(notifier->data);
}

void NoaIpcRegisterNotificationHandler(void *notifier_data, OnNotified callback, void *data)
{
	struct linux_notifier *notifier = (struct linux_notifier *)notifier_data;
	notifier->on_notified = callback;
	notifier->data = data;

	google_dpa_doorbell_enable_doorbell(notifier->doorbell, notifier->doorbell_num,
					    notifier_callback, notifier);
}

void NoaIpcSignalInit(void **signal)
{
	struct completion *comp_ptr = kmalloc(sizeof(struct completion), GFP_KERNEL);
	if (!comp_ptr)
		return;

	init_completion(comp_ptr);
	*signal = comp_ptr;
}

s32 NoaIpcWaitFor(void *signal)
{
	return wait_for_completion_interruptible(signal);
}

void NoaIpcComplete(void *signal)
{
	complete((struct completion *)signal);
}

void NoaIpcSignalDeinit(void *signal)
{
	kfree(signal);
}

// Thread
bool NoaIpcYield(void)
{
	cond_resched();
	if (current->flags & PF_KTHREAD)
		return !kthread_should_stop();
	return !signal_pending(current);
}

// Memory Allocation
void *NoaIpcAllocate(size_t size)
{
	return kmalloc(size, GFP_KERNEL);
}

void NoaIpcFree(void *ptr)
{
	kfree(ptr);
}

void *OsIoRemap(u32 noa_addr, u32 len, void *context)
{
	struct google_dpa *dpa = (struct google_dpa *)context;
	bool is_iomem;

	return google_dpa_da_to_va_internal(dpa, &dpa->ncp, noa_addr, 0, &is_iomem);
}

void OsIoUnmap(void *os_addr, void *context)
{
}
