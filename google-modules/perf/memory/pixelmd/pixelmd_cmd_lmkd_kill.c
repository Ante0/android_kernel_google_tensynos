// SPDX-License-Identifier: GPL-2.0

#include "pixelmd_cmd_lmkd_kill.h"

#include <linux/errno.h>
#include <linux/oom.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <trace/hooks/signal.h>
#include <linux/cred.h>

#include "pixelmd_events.h"

/*
 * From system/core/libcutils/include/private/android_filesystem_config.h
 */
#define AID_LMKD 1069

static void vh_do_send_sig_info(void *data, int sig, struct task_struct *killer,
				struct task_struct *dst)
{
	struct pixelmd_lmkd_kill_event event = { 0 };

	// We're interested only in SIGKILLs from LMKD.
	if (sig != SIGKILL || from_kuid(&init_user_ns, task_uid(killer)) != AID_LMKD)
		return;

	event.pid = task_tgid_nr(dst);
	event.uid = from_kuid(&init_user_ns, task_uid(dst));

	/*
	 * As in other places in the kernel (e.g. oom_score_adj_read() from base.c)
	 * we don't use any locks to read the oom_score_adj value from a valid task
	 * pointer.
	 */
	event.oom_score_adj = dst->signal->oom_score_adj;

	pixelmd_write_event(PIXELMD_SOURCE_LMKD, PIXELMD_EVENT_LMKD_KILL, &event, sizeof(event));
}

int pixelmd_lmkd_kill_register_hooks(void)
{
	return register_trace_android_vh_do_send_sig_info(vh_do_send_sig_info, NULL);
}

void pixelmd_lmkd_kill_unregister_hooks(void)
{
	unregister_trace_android_vh_do_send_sig_info(vh_do_send_sig_info, NULL);
}

/*
 * VH_MM module also uses "trace/events/android_vendor_lmk.h", so if it's not
 * enabled, then probably the file is not there.
 */
#if IS_ENABLED(CONFIG_VH_MM)
#include <trace/events/android_vendor_lmk.h>
#endif

/* A copy of the enum from
 * private/google-modules/soc/gs/drivers/soc/google/vh/include/pixel_mm_hint.h
 *
 * Remove once we can include that file directly.
 */
enum mm_kill_reason { MM_PA_KILL, MM_PIXELMD_KILL };

long pixelmd_cmd_lmkd_kill(void __user *param)
{
	__u16 min_oom_score_adj;

	if (copy_from_user(&min_oom_score_adj, param, sizeof(min_oom_score_adj)))
		return -EFAULT;

	if (min_oom_score_adj < 0 || min_oom_score_adj > OOM_SCORE_ADJ_MAX)
		return -EINVAL;

#if IS_ENABLED(CONFIG_VH_MM)
	trace_android_trigger_vendor_lmk_kill(MM_PIXELMD_KILL, min_oom_score_adj);
#endif
	return 0;
}
