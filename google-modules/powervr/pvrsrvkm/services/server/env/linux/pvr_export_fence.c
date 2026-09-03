/*
 * @File
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/spinlock_types.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/bug.h>

#include "pvr_export_fence.h"
#include "osfunc_common.h"
#include "services_kernel_client.h"

#if defined(SUPPORT_FASTPATH_FENCE)
#include "pvr_sync_fpf_token.h"
#include "sys_fpf_protocol.h"
#endif

/*
 * Export fence update state types:
 * CREATED: The export fence has been allocated but has not been resolved as an
 *          update fence.
 * RESOLVED: The export fence is prepared to be used as an update fence.
 * ROLLED_BACK: An error occurred after being resolved. It cannot be finalised
 *              before being resolved again.
 * FINALISED: The export fence is being used as an update fence and will not be
 *            rolled back!
 *
 *
 *   ┌───────────┐      ┌────────────┐     ┌─────────────┐
 *   │           │      │            │     │             │
 *   │  Created  ├──────►  Resolved  ├─────►  Finalised  │
 *   │           │      │            │     │             │
 *   └───────────┘      └───▲─────┬──┘     └─────────────┘
 *                          │     │
 *                          │     │
 *                     ┌────┴─────▼────┐
 *                     │               │
 *                     │  Rolled Back  │
 *                     │               │
 *                     └───────────────┘
 */
enum export_fence_update_resolve_state {
	EXPORT_FENCE_UPDATE_RESOLVE_STATE_INVALID,
	EXPORT_FENCE_UPDATE_RESOLVE_STATE_CREATED,
	EXPORT_FENCE_UPDATE_RESOLVE_STATE_RESOLVED,
	EXPORT_FENCE_UPDATE_RESOLVE_STATE_ROLLED_BACK,
	EXPORT_FENCE_UPDATE_RESOLVE_STATE_FINALISED
};

struct pvr_exp_fence_context {
	struct kref kref;
	char context_name[32];
	char driver_name[32];
	atomic_t hwperf_seqno;
	atomic_t fence_count;

	void *cmd_complete_handle;

	/* lock for signal and fence lists */
	spinlock_t list_lock;
	struct list_head signal_list;
	struct list_head fence_list;
};

struct pvr_exp_fence {
	struct dma_fence base;
	struct pvr_exp_fence_context *fence_context;
	/* dma_fence context, needs to be unique for each fence */
	u64 context;
	PSYNC_CHECKPOINT checkpoint_handle;
	/* Resolve state of the export fence */
	atomic_t resolve_state;
	/* dma_fence fd (needed for hwperf) */
	int fd;
	/* Lock for the dma fence */
	spinlock_t lock;
#if defined(SUPPORT_FASTPATH_FENCE)
	unsigned int token : 16;
	unsigned int token_valid : 1;
#endif
	/* fence will point to the dma_fence in base */
	struct dma_fence *fence;
	struct list_head fence_head;
	struct list_head signal_head;

};

#if defined(SUPPORT_FASTPATH_FENCE)
static struct fpf_token_generator_context *g_token_context;
#endif

#define PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile, fmt, ...) \
	do {                                                             \
		if (pfnDumpDebugPrintf)                                  \
			pfnDumpDebugPrintf(pvDumpDebugFile, fmt,         \
					   ## __VA_ARGS__);              \
		else                                                     \
			pr_err(fmt "\n", ## __VA_ARGS__);                \
	} while (0)


#if defined(SUPPORT_FASTPATH_FENCE)
enum PVRSRV_ERROR_TAG pvr_exp_fence_module_init(void)
{
	int err = pvr_sync_fpf_instantiate_token_generator(&g_token_context);
	if (err)
	{
		pr_err("err %d", err);
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	return PVRSRV_OK;
}

void pvr_exp_fence_module_deinit(void)
{
	pvr_sync_fpf_destroy_token_generator(g_token_context);
}
#endif

static inline bool
pvr_exp_fence_sync_is_signaled(struct pvr_exp_fence *exp_fence, u32 fence_sync_flags)
{
	bool signalled = false;

	if (exp_fence->checkpoint_handle) {
		signalled = SyncCheckpointIsSignalled(exp_fence->checkpoint_handle,
						fence_sync_flags);
	}

	return signalled;
}

static inline bool
pvr_exp_fence_sync_is_finalised(struct pvr_exp_fence *exp_fence)
{
	return atomic_read(&exp_fence->resolve_state) == EXPORT_FENCE_UPDATE_RESOLVE_STATE_FINALISED;
}

static inline void
pvr_exp_fence_set_state(struct pvr_exp_fence *exp_fence,
			       enum export_fence_update_resolve_state new_state)
{
	atomic_set(&exp_fence->resolve_state, new_state);
}

static inline enum PVRSRV_ERROR_TAG
pvr_exp_fence_transition_state(struct pvr_exp_fence *exp_fence,
			       enum export_fence_update_resolve_state current_state,
			       enum export_fence_update_resolve_state new_state)
{
	enum export_fence_update_resolve_state prev_state;

	prev_state = atomic_cmpxchg(&exp_fence->resolve_state, current_state, new_state);
	if (prev_state != current_state)
	{
		pr_err("%s: Export fence in invalid state for transition. "
		       "States - Current: %u, Prev: %u New: %u",
		       __func__,
		       current_state,
		       prev_state,
		       new_state);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return PVRSRV_OK;
}

const char *pvr_exp_fence_context_name(struct pvr_exp_fence_context *fctx)
{
	return fctx->context_name;
}

static inline void pvr_exp_fence_context_value_str(struct pvr_exp_fence_context *fctx,
				    char *str, int size)
{
	snprintf(str, size, "%d", atomic_read(&fctx->hwperf_seqno));
}

static void
pvr_exp_fence_context_fences_dump(struct pvr_exp_fence_context *fctx,
				  DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				  void *pvDumpDebugFile)
{
	unsigned long flags;
	char value[128];

	spin_lock_irqsave(&fctx->list_lock, flags);
	pvr_exp_fence_context_value_str(fctx, value, sizeof(value));
	PVR_DUMPDEBUG_LOG(pfnDumpDebugPrintf, pvDumpDebugFile,
			 "exp_fence_ctx: @%s", value);
	spin_unlock_irqrestore(&fctx->list_lock, flags);
}

static inline unsigned
pvr_exp_fence_context_hwperf_seqno_next(struct pvr_exp_fence_context *fence_context)
{
	if (fence_context)
		return atomic_inc_return(&fence_context->hwperf_seqno) - 1;
	else
		return 0xfeedface;
}

static void
pvr_exp_fence_context_signal_fences(void *data)
{
	struct pvr_exp_fence_context *fctx = (struct pvr_exp_fence_context *)data;
	struct pvr_exp_fence *pvr_exp_fence, *tmp;
	unsigned long fence_ctx_flags;

	LIST_HEAD(signal_list);

	/*
	 * We can't call fence_signal while holding the lock as we can end up
	 * in a situation whereby pvr_fence_foreign_signal_sync, which also
	 * takes the list lock, ends up being called as a result of the
	 * fence_signal below, i.e. fence_signal(fence) -> fence->callback()
	 *  -> fence_signal(foreign_fence) -> foreign_fence->callback() where
	 * the foreign_fence callback is pvr_fence_foreign_signal_sync.
	 *
	 * So extract the items we intend to signal and add them to their own
	 * queue.
	 */
	spin_lock_irqsave(&fctx->list_lock, fence_ctx_flags);
	list_for_each_entry_safe(pvr_exp_fence, tmp, &fctx->signal_list, signal_head) {

		/* We check here if the export fence has been placed into the finalised state before checking
		 * if it has been signalled. This is because we need to know that the checkpoint will not be
		 * removed mid check by a rollback. We also know that a fence that hasn't been finalised cannot
		 * have been signalled because it won't have been added to a workload at that point.
		 */
		if (pvr_exp_fence_sync_is_finalised(pvr_exp_fence) &&
		    pvr_exp_fence_sync_is_signaled(pvr_exp_fence, PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT)) {
			list_move_tail(&pvr_exp_fence->signal_head, &signal_list);
		}
	}
	spin_unlock_irqrestore(&fctx->list_lock, fence_ctx_flags);

	list_for_each_entry_safe(pvr_exp_fence, tmp, &signal_list, signal_head) {
		list_del(&pvr_exp_fence->signal_head);
		dma_fence_signal(pvr_exp_fence->fence);
		dma_fence_put(pvr_exp_fence->fence);
	}
}

static const char *pvr_exp_fence_get_driver_name(struct dma_fence *fence)
{
	struct pvr_exp_fence *pvr_exp_fence = to_pvr_exp_fence(fence);

	if (pvr_exp_fence && pvr_exp_fence->fence_context)
		return pvr_exp_fence->fence_context->driver_name;
	else
		return "***NO_DRIVER***";
}

static const char *pvr_exp_fence_get_timeline_name(struct dma_fence *fence)
{
	struct pvr_exp_fence *pvr_exp_fence = to_pvr_exp_fence(fence);

	if (pvr_exp_fence && pvr_exp_fence->fence_context)
		return pvr_exp_fence_context_name(pvr_exp_fence->fence_context);
	else
		return "***NO_TIMELINE***";
}

static void pvr_exp_fence_value_str(struct dma_fence *fence, char *str, int size)
{
	snprintf(str, size, "%llu", (u64) fence->seqno);
}

static void pvr_exp_fence_timeline_value_str(struct dma_fence *fence,
					    char *str, int size)
{
	struct pvr_exp_fence *pvr_exp_fence = to_pvr_exp_fence(fence);

	if (pvr_exp_fence && pvr_exp_fence->fence_context)
		pvr_exp_fence_context_value_str(pvr_exp_fence->fence_context, str, size);
}

static bool pvr_exp_fence_enable_signaling(struct dma_fence *fence)
{
	struct pvr_exp_fence *exp_fence = to_pvr_exp_fence(fence);

	if (!exp_fence)
		return false;

	/* We must not take the exp_fence lock in this function.
	 * It can be called by dma_fence_add_callback() which already holds the lock,
	 * Waiting on the fence from UM can trigger this.
	 * We can also check for finalised state before checking the checkpoint as we
	 * know it cannot have been signalled if it hasn't been finalised.
	 * We add this fence to the signal list when we know it is possible to signal it,
	 * this state is represented by finalised.
	 */
	if (pvr_exp_fence_sync_is_finalised(exp_fence) &&
	    pvr_exp_fence_sync_is_signaled(exp_fence, PVRSRV_FENCE_FLAG_SUPPRESS_HWP_PKT))
		return false;

	return true;
}

static void pvr_exp_fence_context_destroy_kref(struct kref *kref)
{
	struct pvr_exp_fence_context *fence_context =
		container_of(kref, struct pvr_exp_fence_context, kref);
	unsigned int fence_count;

	if (WARN_ON(!list_empty_careful(&fence_context->fence_list)))
		pvr_exp_fence_context_fences_dump(fence_context, NULL, NULL);

	PVRSRVUnregisterCmdCompleteNotify(fence_context->cmd_complete_handle);

	fence_count = atomic_read(&fence_context->fence_count);
	if (WARN_ON(fence_count))
		pr_debug("%s context has %u fence(s) remaining\n",
			 fence_context->context_name, fence_count);

	kfree(fence_context);
}

static void pvr_exp_fence_release(struct dma_fence *fence)
{
	struct pvr_exp_fence *pvr_exp_fence = to_pvr_exp_fence(fence);
	unsigned long flags;

	if (pvr_exp_fence) {
		if (pvr_exp_fence->fence_context) {
			spin_lock_irqsave(&pvr_exp_fence->fence_context->list_lock, flags);
			list_del(&pvr_exp_fence->fence_head);
			atomic_dec(&pvr_exp_fence->fence_context->fence_count);
			spin_unlock_irqrestore(&pvr_exp_fence->fence_context->list_lock, flags);

			kref_put(&pvr_exp_fence->fence_context->kref,
				 pvr_exp_fence_context_destroy_kref);
		}

		/* No need to lock access here since release is only called once the
		 * last reference has been dropped by dma_fence_put()
		 */
		if (pvr_exp_fence->checkpoint_handle) {
			SyncCheckpointFree(pvr_exp_fence->checkpoint_handle);
			pvr_exp_fence->checkpoint_handle = NULL;
		}

#if defined(SUPPORT_FASTPATH_FENCE)
		if (pvr_exp_fence->token_valid)
		{
			pvr_sync_fpf_release_token(g_token_context, pvr_exp_fence->token);
		}
#endif

		kfree(pvr_exp_fence);
	}
}

static const struct dma_fence_ops pvr_exp_fence_ops = {
	.get_driver_name = pvr_exp_fence_get_driver_name,
	.get_timeline_name = pvr_exp_fence_get_timeline_name,
	.enable_signaling = pvr_exp_fence_enable_signaling,
	.wait = dma_fence_default_wait,
	.release = pvr_exp_fence_release,
};
static const struct pvr_fence_print_ops print_ops = {
	.fence_value_str = pvr_exp_fence_value_str,
	.timeline_value_str = pvr_exp_fence_timeline_value_str,
};

struct pvr_exp_fence_context *
pvr_exp_fence_context_create(const char *context_name, const char *driver_name)
{
	struct pvr_exp_fence_context *fence_context;
	PVRSRV_ERROR srv_err;

	fence_context = kzalloc(sizeof(*fence_context), GFP_KERNEL);
	if (!fence_context)
		return NULL;

	OSStringSafeCopy(fence_context->context_name, context_name,
		sizeof(fence_context->context_name));
	OSStringSafeCopy(fence_context->driver_name, driver_name,
		sizeof(fence_context->driver_name));
	atomic_set(&fence_context->hwperf_seqno, 0);
	atomic_set(&fence_context->fence_count, 0);
	kref_init(&fence_context->kref);

	spin_lock_init(&fence_context->list_lock);
	INIT_LIST_HEAD(&fence_context->signal_list);
	INIT_LIST_HEAD(&fence_context->fence_list);

	srv_err = PVRSRVRegisterCmdCompleteNotify(&fence_context->cmd_complete_handle,
				pvr_exp_fence_context_signal_fences,
				fence_context);
	if (srv_err != PVRSRV_OK) {
		pr_err("%s: failed to register command complete callback (%s)\n",
		       __func__, PVRSRVGetErrorString(srv_err));
		kfree(fence_context);
		return NULL;
	}

	return fence_context;
}

void pvr_exp_fence_context_destroy(struct pvr_exp_fence_context *fence_context)
{
	if (fence_context) {
		kref_put(&fence_context->kref, pvr_exp_fence_context_destroy_kref);
	}
}

struct dma_fence *
pvr_exp_fence_create(struct pvr_exp_fence_context *fence_context, int fd, u64 *sync_pt_idx)
{
	struct pvr_exp_fence *pvr_exp_fence;
	unsigned long flags;
	unsigned int hwperf_seqno;
	struct pvr_exp_fence_context *pfence_context = fence_context;

	if (WARN_ON(!fence_context))
		return NULL;

	pvr_exp_fence = kzalloc(sizeof(*pvr_exp_fence), GFP_KERNEL);
	if (WARN_ON(!pvr_exp_fence))
		return NULL;

	spin_lock_init(&pvr_exp_fence->lock);

	INIT_LIST_HEAD(&pvr_exp_fence->fence_head);
	INIT_LIST_HEAD(&pvr_exp_fence->signal_head);

	atomic_inc(&pfence_context->fence_count);
	kref_get(&pfence_context->kref);
	pvr_exp_fence->context = dma_fence_context_alloc(1);
	pvr_exp_fence->fd = fd;
	pvr_exp_fence->fence_context = pfence_context;

	/* No sync checkpoint is assigned until attached to a kick */
	pvr_exp_fence->checkpoint_handle = NULL;
	pvr_exp_fence_set_state(pvr_exp_fence, EXPORT_FENCE_UPDATE_RESOLVE_STATE_CREATED);

	/* Since context moved to the fence this is no longer used by the dma fence
	 * but it's still used for HWPerf events to show per PVR context fence
	 * "progression". */
	hwperf_seqno = pvr_exp_fence_context_hwperf_seqno_next(pfence_context);

	pvr_exp_fence->fence = &pvr_exp_fence->base;

	/* Each fence gets a new dma_fence context (pvr_exp_fence::context) therefore
	 * the `seqno` is always 0 since no other fence will be created against
	 * that context. */
	dma_fence_init(&pvr_exp_fence->base, &pvr_exp_fence_ops,
	               &pvr_exp_fence->lock, pvr_exp_fence->context, 0);

	spin_lock_irqsave(&fence_context->list_lock, flags);
	list_add_tail(&pvr_exp_fence->fence_head, &fence_context->fence_list);
	spin_unlock_irqrestore(&fence_context->list_lock, flags);

	*sync_pt_idx = hwperf_seqno;

	return &pvr_exp_fence->base;
}

#if defined(SUPPORT_FASTPATH_FENCE)
struct dma_fence *
pvr_exp_fence_create_with_fp_token(struct pvr_exp_fence_context *fence_context,
                                   int fd,
                                   u64 *sync_pt_idx)
{
	struct dma_fence *fence;
	struct pvr_exp_fence *exp_fence;
	PVRSRV_FAST_PATH_FENCE_TOKEN token;
	int err = 0;

	fence = pvr_exp_fence_create(fence_context, fd, sync_pt_idx);
	if (fence == NULL)
	{
		return NULL;
	}

	err = pvr_sync_fpf_acquire_token(g_token_context, &token);
	if (err != 0)
	{
		goto err_acquire_token;
	}

	fence->flags |= ((RGX_FPF_DMA_FENCE_USERBITS_TOKEN_VALID_BIT | token) << DMA_FENCE_FLAG_USER_BITS);

	exp_fence = to_pvr_exp_fence(fence);
	exp_fence->token_valid = true;
	exp_fence->token = token;

	return fence;

err_acquire_token:
	dma_fence_put(fence);

	return NULL;
}
#endif

enum PVRSRV_ERROR_TAG
pvr_exp_fence_assign_checkpoint(PVRSRV_FENCE fence_to_resolve,
                                struct dma_fence *fence,
                                PSYNC_CHECKPOINT_CONTEXT checkpoint_context,
                                PSYNC_CHECKPOINT *assigned_checkpoint)
{
	struct SYNC_CHECKPOINT_TAG *new_sync_checkpoint;
	struct pvr_exp_fence *pvr_exp_fence;
	PVRSRV_FENCE export_fence_fd = fence_to_resolve;
	enum export_fence_update_resolve_state prev_update_state =
		EXPORT_FENCE_UPDATE_RESOLVE_STATE_INVALID;
	PVRSRV_ERROR err = PVRSRV_OK;
	unsigned long flags;

	pvr_exp_fence = to_pvr_exp_fence(fence);
	if (!pvr_exp_fence) {
		pr_err("%s: Invalid fence_to_resolve\n", __func__);
		err = PVRSRV_ERROR_INVALID_PARAMS;
		goto out;
	}

	spin_lock_irqsave(&pvr_exp_fence->lock, flags);
#if defined(SUPPORT_FASTPATH_FENCE)
	if (pvr_exp_fence->token_valid)
	{
		if (RGX_FPF_DMA_FENCE_USERBITS_TOKEN_CHECK_ACK(pvr_exp_fence->fence->flags >> DMA_FENCE_FLAG_USER_BITS) == 0)
		{
			pr_err("%s: APU did not acknowledge the token in this fence.\n", __func__);
			err = PVRSRV_ERROR_FPF_TOKEN_NOT_ACKNOWLEDGED;
			spin_unlock_irqrestore(&pvr_exp_fence->lock, flags);
			goto out;
		}
	}
#endif

	prev_update_state = atomic_read(&pvr_exp_fence->resolve_state);

	if (prev_update_state == EXPORT_FENCE_UPDATE_RESOLVE_STATE_CREATED ||
	    prev_update_state == EXPORT_FENCE_UPDATE_RESOLVE_STATE_ROLLED_BACK) {
		/* Set now so we can block incoming calls */
		pvr_exp_fence_set_state(pvr_exp_fence,
		                        EXPORT_FENCE_UPDATE_RESOLVE_STATE_RESOLVED);
	} else {
		pr_err("%s: Invalid fence state: %u\n", __func__, prev_update_state);
		err = PVRSRV_ERROR_INVALID_PARAMS;
		spin_unlock_irqrestore(&pvr_exp_fence->lock, flags);
		goto out;
	}

	spin_unlock_irqrestore(&pvr_exp_fence->lock, flags);

	/* Ensure fd assigned to export fence when it was created is passed
	 * to SyncCheckpointAlloc() (so correct fd is used in HWPerf event)
	 * as the export fence may only be part of the check fence.
	 */
	if (pvr_exp_fence->fd != PVRSRV_NO_FENCE) {
		export_fence_fd = pvr_exp_fence->fd;
	}

	/* Allocate the checkpoint, must be outside of spinlock since
	 * the underlying allocation here could sleep
	 */
	err = SyncCheckpointAlloc(checkpoint_context,
	                          PVRSRV_NO_TIMELINE,
	                          export_fence_fd,
	                          fence->ops->get_timeline_name(fence),
	                          &new_sync_checkpoint);
	if (unlikely(err != PVRSRV_OK)) {
		pr_err("%s: SyncCheckpointAlloc() failed (err%d)\n", __func__, err);
		*assigned_checkpoint = NULL;

		/* Revert back to previous state */
		WARN_ON(prev_update_state == EXPORT_FENCE_UPDATE_RESOLVE_STATE_INVALID);
		pvr_exp_fence_set_state(pvr_exp_fence, prev_update_state);

		goto out;
	}

#if defined(SUPPORT_FASTPATH_FENCE)
	if (pvr_exp_fence->token_valid)
	{
		SyncCheckpointInjectFFToken(new_sync_checkpoint, pvr_exp_fence->token);
	}
#endif

	/* Take a reference for the exposed sync checkpoint. Callers of this function
	 * are required to unref when finished. */
	SyncCheckpointTakeRef(new_sync_checkpoint);

	pvr_exp_fence->checkpoint_handle = new_sync_checkpoint;
	*assigned_checkpoint = new_sync_checkpoint;

	return 0;

out:
	return err;
}

enum PVRSRV_ERROR_TAG
pvr_exp_fence_rollback(struct dma_fence *fence)
{
	struct pvr_exp_fence *pvr_exp_fence;
	PVRSRV_ERROR err;
	unsigned long flags;

	pvr_exp_fence = to_pvr_exp_fence(fence);
	if (!pvr_exp_fence) {
		pr_err("%s: Invalid fence\n", __func__);
		err = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	spin_lock_irqsave(&pvr_exp_fence->lock, flags);

	err = pvr_exp_fence_transition_state(pvr_exp_fence,
	                                     EXPORT_FENCE_UPDATE_RESOLVE_STATE_RESOLVED,
	                                     EXPORT_FENCE_UPDATE_RESOLVE_STATE_ROLLED_BACK);
	if (err != PVRSRV_OK)
	{
		spin_unlock_irqrestore(&pvr_exp_fence->lock, flags);
		goto err_out;
	}

	if (pvr_exp_fence->checkpoint_handle != NULL)
	{
		/* The export fence is holding the last reference to the sync checkpoint.
		 * The sync checkpoint is about to be freed, so detach it from the export
		 * fence before releasing the lock. */
		PSYNC_CHECKPOINT checkpoint = pvr_exp_fence->checkpoint_handle;
		pvr_exp_fence->checkpoint_handle = NULL;

		spin_unlock_irqrestore(&pvr_exp_fence->lock, flags);
		/* Drop the reference on the checkpoint. This must be done outside of the
		 * spinlock to prevent lock ups. */
		SyncCheckpointFree(checkpoint);
	}
	else
	{
		spin_unlock_irqrestore(&pvr_exp_fence->lock, flags);
	}

	return PVRSRV_OK;

err_out:
	return err;
}

enum PVRSRV_ERROR_TAG
pvr_exp_fence_finalise(struct dma_fence *fence)
{
	struct pvr_exp_fence *pvr_exp_fence;
	PVRSRV_ERROR err = PVRSRV_OK;
	unsigned long flags;

	pvr_exp_fence = to_pvr_exp_fence(fence);
	if (!pvr_exp_fence) {
		pr_err("%s: Invalid fence\n", __func__);
		err = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_out;
	}

	err = pvr_exp_fence_transition_state(pvr_exp_fence,
	                                     EXPORT_FENCE_UPDATE_RESOLVE_STATE_RESOLVED,
	                                     EXPORT_FENCE_UPDATE_RESOLVE_STATE_FINALISED);
	if (err != PVRSRV_OK)
	{
		goto err_out;
	}

	dma_fence_get(&pvr_exp_fence->base);

	spin_lock_irqsave(&pvr_exp_fence->fence_context->list_lock, flags);
	list_add_tail(&pvr_exp_fence->signal_head, &pvr_exp_fence->fence_context->signal_list);
	spin_unlock_irqrestore(&pvr_exp_fence->fence_context->list_lock, flags);

err_out:
	return err;
}

bool pvr_is_exp_fence(const struct dma_fence *fence)
{
	return (fence->ops == &pvr_exp_fence_ops);
}

struct pvr_exp_fence *to_pvr_exp_fence(struct dma_fence *fence)
{
	if (pvr_is_exp_fence(fence))
		return container_of(fence, struct pvr_exp_fence, base);

	return NULL;
}

struct SYNC_CHECKPOINT_TAG *
pvr_exp_fence_get_checkpoint(struct pvr_exp_fence *export_fence)
{
	unsigned long flags;
	struct SYNC_CHECKPOINT_TAG *checkpoint = NULL;

	if (export_fence) {
		spin_lock_irqsave(&export_fence->lock, flags);
		if (export_fence->checkpoint_handle) {
			checkpoint = export_fence->checkpoint_handle;
			SyncCheckpointTakeRef(checkpoint);
		}
		spin_unlock_irqrestore(&export_fence->lock, flags);
	}

	return checkpoint;
}

const struct pvr_fence_print_ops *get_pvr_exp_fence_print_ops(const struct dma_fence *fence)
{
	if (pvr_is_exp_fence(fence))
		return &print_ops;
	return NULL;
}
