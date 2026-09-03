// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 *
 * This file implements OS-specific backends for Pigweed RPC.
 */

#include "pw_rpc_c/pw_rpc_os.h"

#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>

void PwRpcLockInit(PwRpcLock *lock)
{
	struct mutex *mutex_ptr = kmalloc(sizeof(*mutex_ptr), GFP_KERNEL);

	if (!mutex_ptr)
		return;

	mutex_init(mutex_ptr);
	*lock = mutex_ptr;
}

s32 PwRpcLockAcquire(PwRpcLock lock)
{
	return mutex_lock_interruptible(lock);
}

void PwRpcLockRelease(PwRpcLock lock)
{
	mutex_unlock(lock);
}

void PwRpcLockDeinit(PwRpcLock lock)
{
	kfree(lock);
}

void *PwRpcAllocate(size_t size)
{
	return kmalloc(size, GFP_KERNEL);
}

void PwRpcFree(void *ptr)
{
	kfree(ptr);
}
