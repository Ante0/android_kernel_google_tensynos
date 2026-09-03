// SPDX-License-Identifier: GPL-2.0

#include <linux/platform_data/sscoredump.h>
#include <uapi/misc/crashinfo.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include "sscd.h"

#define SSCD_MAX_MSG_LEN (1 << 18) // 256 KiB buffer
#define SSCD_RATELIMIT_MS (300000U) // 5min

static void sscd_release(struct device *dev)
{
	(void)dev;
}

static struct sscd_platform_data sscd_pdata;
static struct platform_device sscd_dev = { .name = "powervr",
					   .driver_override = SSCD_NAME,
					   .id = -1,
					   .dev = { .platform_data = &sscd_pdata,
					   .release = sscd_release,
					   } };

enum {
	DEBUG_DATA,
	NUM_SEGMENTS
} sscd_segs;

static void _DumpDebugDataWrapper(void *pvPriv, const IMG_CHAR *pFmt, ...)
{
	va_list vaArgs;
	struct sscd_segment *pSeg = (struct sscd_segment *)pvPriv;
	IMG_UINT32 bufRem;
	IMG_INT32 fmtLen;

	va_start(vaArgs, pFmt);

	if (unlikely(pSeg->size > SSCD_MAX_MSG_LEN)) {
		pr_warn_once("SSCD: Buffer size exceeded limit.");
		pSeg->size = SSCD_MAX_MSG_LEN;
		va_end(vaArgs);
		return;
	}

	bufRem = SSCD_MAX_MSG_LEN - pSeg->size;

	if (bufRem > 0) {
		fmtLen = vsnprintf(&pSeg->addr[pSeg->size], bufRem, pFmt, vaArgs);

		if (unlikely(fmtLen < 0)) {
			pr_warn("SSCD: Unexpected output error!");
			va_end(vaArgs);
			return;
		}

		if (fmtLen < ((IMG_INT32)bufRem - 1)) {
			/* New line added after each instance of logging
			 * Provides better readability
			 * This also replaces the ending null character
			 * so we will need to re-insert it.
			 */
			*((char *)pSeg->addr + pSeg->size + fmtLen) = '\n';
			pSeg->size += fmtLen + 1;
			*((char *)pSeg->addr + pSeg->size) = 0;
		} else {
			pSeg->size = SSCD_MAX_MSG_LEN;
			pr_info("SSCD buffer is full. Dump Data might be TRUNCATED!\n");
		}
	}

	va_end(vaArgs);
}

static void get_debug_data(struct pixel_gpu_device *pixel_dev, struct sscd_segment *seg)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = pixel_dev->dev_config->psDevNode;

	if (psDeviceNode != NULL)
		PVRSRVDebugRequest(psDeviceNode, DEBUG_REQUEST_VERBOSITY_MAX,
						_DumpDebugDataWrapper, seg);
}

/**
 * gpu_sscd_dump() - Intiates and reports a subsystem core-dump of the GPU.
 */
void gpu_sscd_dump(struct pixel_gpu_device *pixel_dev, PVRSRV_ROBUSTNESS_NOTIFY_DATA *error)
{
	struct task_struct *task;
	struct sscd_segment segs[NUM_SEGMENTS];
	char sscd_title[CRASHINFO_REASON_SIZE] = {0};
	struct sscd_platform_data *pdata = dev_get_platdata(&sscd_dev.dev);
	char fault_info[32] = {0};
	char reset_reason[64] = {0};
	char pid_tid_string[16] = {0};
	char process_thread_group_name[(TASK_COMM_LEN * 2) + 5] = {0};
	static atomic_long_t last_dump_ts = ATOMIC_LONG_INIT(0);
	unsigned long last_dump_ts_val, current_ts = jiffies;
	bool suppress_dump = false;

	if (pixel_dev == NULL || pixel_dev->dev_config == NULL)
		return;

	last_dump_ts_val = atomic_long_read(&last_dump_ts);
	if (!last_dump_ts_val || time_after(current_ts,
				last_dump_ts_val + msecs_to_jiffies(SSCD_RATELIMIT_MS))) {
		if (atomic_long_cmpxchg(&last_dump_ts, last_dump_ts_val,
					current_ts) != last_dump_ts_val) {
			suppress_dump = true;
		}
	} else {
		suppress_dump = true;
	}

	if (suppress_dump) {
		dev_info(pixel_dev->dev, "pixel: skipping powervr subsystem core dump");
		return;
	}

	if (error->eResetReason == RGX_CONTEXT_RESET_REASON_GUILTY_LOCKUP)
		snprintf(fault_info,
			 ARRAY_SIZE(fault_info),
			 "[%s]:",
			 rgxfwif_dm_str(
				error->uErrData.sGuiltyLockupData.eDM));
	if (error->eResetReason == RGX_CONTEXT_RESET_REASON_FW_PAGEFAULT)
		snprintf(fault_info,
			 ARRAY_SIZE(fault_info),
			 "[addr=0x%llx]:",
			 error->uErrData.sFwPFErrData.sFWFaultAddr.uiAddr);

	snprintf(pid_tid_string,
		 ARRAY_SIZE(pid_tid_string),
		 "[%u]:",
		 error->pid);
	snprintf(reset_reason,
		 ARRAY_SIZE(reset_reason),
		 "[%s]",
		 rgx_context_reset_reason_str(error->eResetReason));

	rcu_read_lock();

	task = find_task_by_vpid(error->pid);
	if (task) {
		pid_t thread_group_id = task->tgid;
		struct task_struct *parent_task = find_task_by_vpid(thread_group_id);
		char process_name[TASK_COMM_LEN] = {0};

		get_task_comm(process_name, task);

		if (thread_group_id != error->pid && parent_task) {
			char thread_group_name[TASK_COMM_LEN] = {0};

			get_task_comm(thread_group_name, parent_task);

			snprintf(pid_tid_string,
				 ARRAY_SIZE(pid_tid_string),
				 "[%u-%u]:",
				 thread_group_id,
				 error->pid);
			snprintf(process_thread_group_name,
				 ARRAY_SIZE(process_thread_group_name),
				 "[%s-%s]:",
				 thread_group_name,
				 process_name);
		} else
			snprintf(process_thread_group_name,
				 ARRAY_SIZE(process_thread_group_name),
				 "[%s]:",
				 process_name);
	}

	rcu_read_unlock();

	snprintf(sscd_title,
		 ARRAY_SIZE(sscd_title),
		 "%s%s%s%s",
		 pid_tid_string,
		 process_thread_group_name,
		 fault_info,
		 reset_reason);

	dev_info(pixel_dev->dev, "PowerVR subsystem core dump in progress");
	if (!pdata->sscd_report) {
		dev_warn(pixel_dev->dev, "Failed to report core dump, sscd_report was NULL");
		return;
	}

	memset(segs, 0, sizeof(segs));

	segs[DEBUG_DATA].addr = kcalloc(SSCD_MAX_MSG_LEN, sizeof(IMG_CHAR), GFP_KERNEL);
	if (!segs[DEBUG_DATA].addr)
		return;

	get_debug_data(pixel_dev, &segs[DEBUG_DATA]);

	pdata->sscd_report(&sscd_dev, segs, NUM_SEGMENTS, SSCD_FLAGS_ELFARM64HDR, sscd_title);

	kfree(segs[DEBUG_DATA].addr);
}

/**
 * gpu_sscd_init() - Registers the SSCD platform device and inits a firmware trace buffer.
 */
int gpu_sscd_init(struct pixel_gpu_device *pixel_dev)
{
	int ret;
	ret = platform_device_register(&sscd_dev);
	return ret;
}

/**
 * gpu_sscd_deinit() - Unregisters the SSCD platform device.
 */
void gpu_sscd_deinit(struct pixel_gpu_device *pixel_dev)
{
	platform_device_unregister(&sscd_dev);
}
