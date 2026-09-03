// SPDX-License-Identifier: GPL-2.0

#include "gpu_uevent.h"
#include "sysconfig.h"

#define GPU_UEVENT_RATELIMIT_MS (1200000U) /* 20min */

static inline const char *gpu_uevent_type_str(enum gpu_uevent_type type)
{
	switch (type) {
	case GPU_UEVENT_TYPE_NONE: return "GPU_UEVENT_TYPE=NONE";
	case GPU_UEVENT_TYPE_KMD_ERROR: return "GPU_UEVENT_TYPE=KMD_ERROR";
	case GPU_UEVENT_TYPE_MAX: return "GPU_UEVENT_TYPE=MAX";
	}
	return "UNKNOWN";
}

static inline const char *gpu_uevent_info_str(enum gpu_uevent_info info)
{
	switch (info) {
	case GPU_UEVENT_INFO_NONE: return "GPU_UEVENT_INFO=NONE";
	case GPU_UEVENT_INFO_FW_PAGEFAULT: return "GPU_UEVENT_INFO=FW_PAGEFAULT";
	case GPU_UEVENT_INFO_HOST_WDG_FW_ERROR: return "GPU_UEVENT_INFO=HOST_WDG_FW_ERROR";
	case GPU_UEVENT_INFO_GUILTY_LOCKUP: return "GPU_UEVENT_INFO=GUILTY_LOCKUP";
	case GPU_UEVENT_INFO_MAX: return "GPU_UEVENT_INFO=MAX";
	}
	return "UNKNOWN";
}

static bool gpu_uevent_check_valid(const struct gpu_uevent *evt)
{
	switch (evt->type) {
	case GPU_UEVENT_TYPE_KMD_ERROR:
	switch (evt->info) {
	case GPU_UEVENT_INFO_FW_PAGEFAULT:
	case GPU_UEVENT_INFO_HOST_WDG_FW_ERROR:
	case GPU_UEVENT_INFO_GUILTY_LOCKUP:
		return true;
	default:
		return false;
	}
	break;
	default:
	break;
	}

	return false;
}

static void gpu_uevent_send_worker(struct work_struct *data)
{
	struct pixel_gpu_device *pixel_dev = container_of(data, struct pixel_gpu_device,
	gpu_uevent_ctx.gpu_uevent_work);
	struct gpu_uevent_ctx *gpu_uevent_ctx = &pixel_dev->gpu_uevent_ctx;
	enum uevent_env_idx {
	ENV_IDX_TYPE,
	ENV_IDX_INFO,
	ENV_IDX_UID,
	ENV_IDX_NULL,
	ENV_IDX_MAX
	};
	char *env[ENV_IDX_MAX] = {0};
	char uid_str[64];
	unsigned long flags, current_ts = jiffies;
	bool suppress_uevent = false;
	struct gpu_uevent evt = {0};

	if (!kfifo_initialized(&gpu_uevent_ctx->evts_fifo))
		return;

	spin_lock_irqsave(&gpu_uevent_ctx->lock, flags);

	if (kfifo_out(&gpu_uevent_ctx->evts_fifo, &evt, 1 /* nelems */) &&
		gpu_uevent_check_valid(&evt) &&
		time_after(current_ts, gpu_uevent_ctx->last_uevent_ts[evt.type]
		+ msecs_to_jiffies(GPU_UEVENT_RATELIMIT_MS))) {
		gpu_uevent_ctx->last_uevent_ts[evt.type] = current_ts;
	} else {
		suppress_uevent = true;
	}

	spin_unlock_irqrestore(&gpu_uevent_ctx->lock, flags);

	if (suppress_uevent)
		return;

	env[ENV_IDX_TYPE] = (char *) gpu_uevent_type_str(evt.type);
	env[ENV_IDX_INFO] = (char *) gpu_uevent_info_str(evt.info);
	if (evt.info == GPU_UEVENT_INFO_GUILTY_LOCKUP) {
		snprintf(uid_str, sizeof(uid_str), "GPU_UEVENT_UID=%u", evt.uid);
		env[ENV_IDX_UID] = uid_str;
	}

	env[ENV_IDX_NULL] = NULL;

	kobject_uevent_env(&pixel_dev->dev->kobj, KOBJ_CHANGE, env);
}

void gpu_uevent_send(struct pixel_gpu_device *pixel_dev, const struct gpu_uevent *evt)
{
	struct gpu_uevent_ctx *gpu_uevent_ctx = &pixel_dev->gpu_uevent_ctx;
	unsigned long flags, current_ts = jiffies;
	bool suppress_uevent = false;

	if (!gpu_uevent_check_valid(evt)) {
		dev_err(pixel_dev->dev,
			"unrecognized uevent type=%u info=%u", evt->type, evt->info);
		return;
	}

	if (!kfifo_initialized(&gpu_uevent_ctx->evts_fifo))
		return;

	spin_lock_irqsave(&gpu_uevent_ctx->lock, flags);

	if (time_after(current_ts, gpu_uevent_ctx->last_uevent_ts[evt->type]
		+ msecs_to_jiffies(GPU_UEVENT_RATELIMIT_MS))) {
		if (!kfifo_avail(&gpu_uevent_ctx->evts_fifo)) {
			/* Drop the oldest unprocessed uevent if the queue is full. */
			kfifo_skip(&gpu_uevent_ctx->evts_fifo);
		}

		kfifo_in(&gpu_uevent_ctx->evts_fifo, evt, 1 /* nelems */);
	} else {
		suppress_uevent = true;
	}

	spin_unlock_irqrestore(&gpu_uevent_ctx->lock, flags);

	if (suppress_uevent)
		return;

	schedule_work(&gpu_uevent_ctx->gpu_uevent_work);
}

void gpu_uevent_kmd_error_send(struct pixel_gpu_device *pixel_dev, const enum gpu_uevent_info info)
{
	const struct gpu_uevent evt = {
		.type = GPU_UEVENT_TYPE_KMD_ERROR,
		.info = info
	};

	gpu_uevent_send(pixel_dev, &evt);
}

void gpu_uevent_term(struct pixel_gpu_device *pixel_dev)
{
	struct gpu_uevent_ctx *gpu_uevent_ctx = &pixel_dev->gpu_uevent_ctx;

	cancel_work_sync(&gpu_uevent_ctx->gpu_uevent_work);
	if (kfifo_initialized(&gpu_uevent_ctx->evts_fifo))
		kfifo_free(&gpu_uevent_ctx->evts_fifo);
}

int gpu_uevent_init(struct pixel_gpu_device *pixel_dev)
{
	struct gpu_uevent_ctx *gpu_uevent_ctx = &pixel_dev->gpu_uevent_ctx;

	memset(&pixel_dev->gpu_uevent_ctx, 0, sizeof(struct gpu_uevent_ctx));
	spin_lock_init(&gpu_uevent_ctx->lock);
	INIT_WORK(&gpu_uevent_ctx->gpu_uevent_work, gpu_uevent_send_worker);

	if (kfifo_alloc(&gpu_uevent_ctx->evts_fifo, 4 /* nelems */, GFP_KERNEL))
		return -ENOMEM;

	return 0;
}
