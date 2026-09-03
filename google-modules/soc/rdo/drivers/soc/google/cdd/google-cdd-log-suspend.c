// SPDX-License-Identifier: GPL-2.0
/*
 * google-cdd-log-suspend.c - use to diagnose suspend/resume perforance
 *
 * Copyright 2024 Google LLC
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/clock.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <trace/events/power.h>

#include "google-cdd-local.h"

#define GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_H_MASK		(0xFFFFU)
#define GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_H_SHIFT		(32)
#define GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_L_MASK		(0xFFFFFFFFU)
#define GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_L_SHIFT		(0)

#define GOOGLE_CDD_SUSPEND_DIAG_GET_DELTA_TIME_H(delta) \
	(((delta) >> GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_H_SHIFT) & \
	 GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_H_MASK)

#define GOOGLE_CDD_SUSPEND_DIAG_GET_DELTA_TIME_L(delta) \
	(((delta) >> GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_L_SHIFT) & \
	 GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_L_MASK)

#define GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME(delta_h, delta_l) \
	((((uint64_t)(delta_h) & GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_H_MASK) << \
	  GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_H_SHIFT) | \
	 (((uint64_t)(delta_l) & GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_L_MASK) << \
	  GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME_L_SHIFT))

enum google_cdd_suspend_diag_item_index {
	SR_SYNC_FILESYSTEMS_ID = 0,
	SR_FREEZE_PROCESSES_ID,
	SR_SUSPEND_ENTER_ID,
	SR_DPM_PREPARE_ID,
	SR_DPM_SUSPEND_ID,
	SR_DPM_SUSPEND_LATE_ID,
	SR_DPM_SUSPEND_NOIRQ_ID,
	SR_CPU_OFF_ID,
	SR_SYSCORE_SUSPEND_ID,
	SR_MACHINE_SUSPEND_ID,
	SR_SYSCORE_RESUME_ID,
	SR_CPU_ON_ID,
	SR_DPM_RESUME_NOIRQ_ID,
	SR_DPM_RESUME_EARLY_ID,
	SR_DPM_RESUME_ID,
	SR_DPM_COMPLETE_ID,
	SR_RESUME_CONSOLE_ID,
	SR_THAW_PROCESSES_ID,
};

struct google_cdd_suspend_diag_item {
	const char *action;
	uint64_t timeout;
	bool enabled;
};

static struct google_cdd_suspend_diag_item google_cdd_suspend_diag_items[] = {
	[SR_SYNC_FILESYSTEMS_ID]	= {"sync_filesystems", 3 * NSEC_PER_SEC, false},
	[SR_FREEZE_PROCESSES_ID]	= {"freeze_processes", NSEC_PER_SEC, false},
	[SR_SUSPEND_ENTER_ID]		= {"suspend_enter", NSEC_PER_SEC, false},
	[SR_DPM_PREPARE_ID]		= {"dpm_prepare", NSEC_PER_SEC, true},
	[SR_DPM_SUSPEND_ID]		= {"dpm_suspend", NSEC_PER_SEC, true},
	[SR_DPM_SUSPEND_LATE_ID]	= {"dpm_suspend_late", NSEC_PER_SEC, true},
	[SR_DPM_SUSPEND_NOIRQ_ID]	= {"dpm_suspend_noirq", NSEC_PER_SEC, true},
	[SR_CPU_OFF_ID]			= {"cpu_off", NSEC_PER_SEC, false},
	[SR_SYSCORE_SUSPEND_ID]		= {"syscore_suspend", NSEC_PER_SEC, false},
	[SR_MACHINE_SUSPEND_ID]		= {"machine_suspend", NSEC_PER_SEC, false},
	[SR_SYSCORE_RESUME_ID]		= {"syscore_resume", NSEC_PER_SEC, false},
	[SR_CPU_ON_ID]			= {"cpu_on", NSEC_PER_SEC, false},
	[SR_DPM_RESUME_NOIRQ_ID]	= {"dpm_resume_noirq", NSEC_PER_SEC, true},
	[SR_DPM_RESUME_EARLY_ID]	= {"dpm_resume_early", NSEC_PER_SEC, true},
	[SR_DPM_RESUME_ID]		= {"dpm_resume", NSEC_PER_SEC, true},
	[SR_DPM_COMPLETE_ID]		= {"dpm_complete", NSEC_PER_SEC, true},
	[SR_RESUME_CONSOLE_ID]		= {"resume_console", NSEC_PER_SEC, false},
	[SR_THAW_PROCESSES_ID]		= {"thaw_processes", NSEC_PER_SEC, false},
};

struct google_cdd_suspend_diag_info {
	uint32_t enable;
	uint32_t force_panic;
	uint64_t last_index;
	uint64_t curr_index;
	uint64_t timeout;
	char action[32];
} __packed;

static struct google_cdd_suspend_diag_info google_cdd_suspend_diag_inst;

void *google_cdd_suspend_diag_get_info(void)
{
	return (void *)&google_cdd_suspend_diag_inst;
}
EXPORT_SYMBOL_GPL(google_cdd_suspend_diag_get_info);

static void google_cdd_suspend_diag_handle_suspend_resume
(struct google_cdd_log *cdd_log, uint64_t last_idx, uint64_t curr_idx)
{
	uint64_t idx = (last_idx + 1) % ARRAY_SIZE(cdd_log->suspend);
	bool has_dev_pm_cb = (idx != curr_idx);
	uint64_t delta_time = 0;
	int i;

	if (!has_dev_pm_cb) {
		delta_time = cdd_log->suspend[curr_idx].time - cdd_log->suspend[last_idx].time;
	} else {
		/*
		 * dev_pm_cb have been run by multi cores between last_idx and curr_idx
		 * so we can't use cdd_log->suspend[curr_idx].time -
		 * cdd_log->suspend[last_idx].time directly to determine delta time
		 */
		while (idx != curr_idx) {
			delta_time += GOOGLE_CDD_SUSPEND_DIAG_DELTA_TIME(
					cdd_log->suspend[idx].delta_time_h,
					cdd_log->suspend[idx].delta_time_l);
			idx = (idx + 1) % ARRAY_SIZE(cdd_log->suspend);
		}
	}

	for (i = 0; i < ARRAY_SIZE(google_cdd_suspend_diag_items); i++) {
		if (!strcmp(cdd_log->suspend[curr_idx].log,
			google_cdd_suspend_diag_items[i].action))
			break;
	}

	if (i == ARRAY_SIZE(google_cdd_suspend_diag_items))
		return;

	if (delta_time < google_cdd_suspend_diag_items[i].timeout)
		return;

	if (strlen(google_cdd_suspend_diag_inst.action) == 0 &&
			google_cdd_suspend_diag_items[i].enabled)
		goto crash;

	if (strcmp(google_cdd_suspend_diag_inst.action, cdd_log->suspend[curr_idx].log))
		return;

crash:
	google_cdd_suspend_diag_inst.force_panic = 0x1;
	google_cdd_suspend_diag_inst.timeout = google_cdd_suspend_diag_items[i].timeout;
	panic("%s: %s%s(%llu) to %s%s(%llu) %stook %llu.%llu s\n", __func__,
	      cdd_log->suspend[last_idx].log ? cdd_log->suspend[last_idx].log : "",
	      cdd_log->suspend[last_idx].en == CDD_FLAG_IN ? " IN" : " OUT", last_idx,
	      cdd_log->suspend[curr_idx].log ? cdd_log->suspend[curr_idx].log : "",
	      cdd_log->suspend[curr_idx].en == CDD_FLAG_IN ? " IN" : " OUT", curr_idx,
	      has_dev_pm_cb ? "callbacks " : "",
	      delta_time / NSEC_PER_SEC, (delta_time % NSEC_PER_SEC) / USEC_PER_SEC);
}

static void google_cdd_suspend_diag_suspend_resume(void *google_cdd_log, const char *action,
		bool start, uint64_t curr_index)
{
	google_cdd_suspend_diag_inst.curr_index = curr_index;

	if (!google_cdd_suspend_diag_inst.enable)
		return;

	if (start || !action)
		goto backup;

	google_cdd_suspend_diag_handle_suspend_resume(google_cdd_log,
						 google_cdd_suspend_diag_inst.last_index,
						 google_cdd_suspend_diag_inst.curr_index);

backup:
	google_cdd_suspend_diag_inst.last_index = google_cdd_suspend_diag_inst.curr_index;
}

static bool google_cdd_suspend_diag_dev_pm_cb_end(void *google_cdd_log, uint64_t first_log_idx,
		uint64_t last_log_idx, struct device *dev)
{
	uint64_t i;
	struct google_cdd_log *cdd_log = google_cdd_log;
	uint64_t end_time;
	uint64_t delta_time;

	if (!google_cdd_suspend_diag_inst.enable)
		return false;

	end_time = local_clock();

	i = last_log_idx;
	while (i != first_log_idx) {
		if (dev && end_time >= cdd_log->suspend[i].time &&
			cdd_log->suspend[i].dev == dev_name(dev)) {
			delta_time = end_time - cdd_log->suspend[i].time;
			cdd_log->suspend[i].delta_time_h =
				GOOGLE_CDD_SUSPEND_DIAG_GET_DELTA_TIME_H(delta_time);
			cdd_log->suspend[i].delta_time_l =
				GOOGLE_CDD_SUSPEND_DIAG_GET_DELTA_TIME_L(delta_time);
			break;
		}
		i = (i - 1) % ARRAY_SIZE(cdd_log->suspend);
	}

	return true;
}

static unsigned long google_cdd_suspend(const char *log, struct device *dev, int event, int en)
{
	unsigned long i = atomic_fetch_inc(&cdd_log_misc.suspend_log_idx) %
		ARRAY_SIZE(cdd_log->suspend);

	cdd_log->suspend[i].time = local_clock();
	if (log && dev && dev->driver && dev->driver->name && strlen(dev->driver->name))
		cdd_log->suspend[i].log = dev->driver->name;
	else
		cdd_log->suspend[i].log = log;
	cdd_log->suspend[i].event = event;
	cdd_log->suspend[i].dev = dev ? dev_name(dev) : "";
	cdd_log->suspend[i].core = raw_smp_processor_id();
	cdd_log->suspend[i].en = en;
	cdd_log->suspend[i].delta_time_h = 0x0;
	cdd_log->suspend[i].delta_time_l = 0x0;

	return i;
}

static void google_cdd_suspend_resume(void *ignore, const char *action, int event, bool start)
{
	unsigned long curr_index =
		google_cdd_suspend(action, NULL, event, start ? CDD_FLAG_IN : CDD_FLAG_OUT);

	google_cdd_suspend_diag_suspend_resume(cdd_log, action, start, curr_index);
}

static void google_cdd_dev_pm_cb_start(void *ignore, struct device *dev, const char *info,
		int event)
{
	uint64_t predict_full_log_idx =
		(atomic_read(&cdd_log_misc.suspend_log_idx) + 1) % ARRAY_SIZE(cdd_log->suspend);
	if (google_cdd_suspend_diag_inst.enable &&
	    predict_full_log_idx == google_cdd_suspend_diag_inst.last_index) {
		dev_err(dev, "Ring buffer for %s diag is full\n",
			cdd_log->suspend[google_cdd_suspend_diag_inst.last_index].log);
	}

	google_cdd_suspend(info, dev, event, CDD_FLAG_IN);
}

static void google_cdd_dev_pm_cb_end(void *ignore, struct device *dev, int error)
{
	uint64_t predict_full_log_idx =
		(atomic_read(&cdd_log_misc.suspend_log_idx) + 1) % ARRAY_SIZE(cdd_log->suspend);
	uint64_t first_log_idx =
		atomic_read(&cdd_log_misc.suspend_log_idx) % ARRAY_SIZE(cdd_log->suspend);
	uint64_t last_log_idx =
		(atomic_read(&cdd_log_misc.suspend_log_idx) - 1) % ARRAY_SIZE(cdd_log->suspend);

	if (google_cdd_suspend_diag_inst.enable &&
	    predict_full_log_idx == google_cdd_suspend_diag_inst.last_index) {
		dev_err(dev, "Ring buffer for %s diag is full\n",
			cdd_log->suspend[google_cdd_suspend_diag_inst.last_index].log);
	}

	if (!google_cdd_suspend_diag_dev_pm_cb_end(cdd_log, first_log_idx, last_log_idx, dev))
		google_cdd_suspend(NULL, dev, error, CDD_FLAG_OUT);
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	uint64_t val;
	int ret;

	ret = kstrtoul(buf, 10, (unsigned long *)&val);

	if (!ret)
		google_cdd_suspend_diag_inst.enable = val;

	return count;
}

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%sable\n",
			 !!google_cdd_suspend_diag_inst.enable ? "en" : "dis");
}

static struct kobj_attribute google_cdd_suspend_diag_attr_enable = __ATTR_RW_MODE(enable, 0660);

static ssize_t timeout_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			     size_t count)
{
	int i;
	char item_action[32];
	char *item_timeout, *space;
	int action_size;
	uint64_t val;

	/*
	 * Extract buf before the first space.
	 */
	space = strchr(buf, ' ');
	if (!space) {
		pr_warn("invalid parameters format in buffer [%s]\n", buf);
		return -EINVAL;
	}

	action_size = space - buf + 1;
	item_timeout = space + 1;
	if (action_size > sizeof(item_action)) {
		pr_warn("invalid action parameter in buffer [%s]\n", buf);
		return -EINVAL;
	}
	strscpy(item_action, buf, action_size);

	if (kstrtoll(item_timeout, 10, &val)) {
		pr_warn("invalid timeout parameter in buffer [%s]\n", buf);
		return -EINVAL;
	}

	if (!strcmp(item_action, "all")) {
		for (i = 0; i < ARRAY_SIZE(google_cdd_suspend_diag_items); i++)
			google_cdd_suspend_diag_items[i].timeout = val;
		return count;
	}

	for (i = 0; i < ARRAY_SIZE(google_cdd_suspend_diag_items); i++) {
		if (!strcmp(item_action, google_cdd_suspend_diag_items[i].action)) {
			google_cdd_suspend_diag_items[i].timeout = val;
			return count;
		}
	}

	pr_warn("item action doesn't exist in default list [%s]\n", item_action);
	return -EEXIST;
}

static ssize_t timeout_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int i;
	ssize_t size = 0;

	for (i = 0; i < ARRAY_SIZE(google_cdd_suspend_diag_items); i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size, "%s: %llu(ns)\n",
				  google_cdd_suspend_diag_items[i].action,
				  google_cdd_suspend_diag_items[i].timeout);
	}

	return size;
}

static struct kobj_attribute google_cdd_suspend_diag_attr_timeout = __ATTR_RW_MODE(timeout, 0660);

static ssize_t action_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	char *newline = NULL;

	strscpy(google_cdd_suspend_diag_inst.action, buf,
			sizeof(google_cdd_suspend_diag_inst.action));
	newline = strchr(google_cdd_suspend_diag_inst.action, '\n');
	if (newline)
		*newline = '\0';

	return count;
}

static ssize_t action_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", google_cdd_suspend_diag_inst.action);
}

static struct kobj_attribute google_cdd_suspend_diag_attr_action = __ATTR_RW_MODE(action, 0660);

static struct attribute *google_cdd_suspend_diag_attrs[] = {
	&google_cdd_suspend_diag_attr_enable.attr,
	&google_cdd_suspend_diag_attr_timeout.attr,
	&google_cdd_suspend_diag_attr_action.attr,
	NULL
};

static const struct attribute_group google_cdd_suspend_diag_attr_group = {
	.attrs = google_cdd_suspend_diag_attrs,
	.name = "suspend_diag"
};

int google_cdd_suspend_init(void)
{
	struct kobject *google_cdd_suspend_diag_kobj;

	register_trace_suspend_resume(google_cdd_suspend_resume, NULL);
	register_trace_device_pm_callback_start(google_cdd_dev_pm_cb_start, NULL);
	register_trace_device_pm_callback_end(google_cdd_dev_pm_cb_end, NULL);

	google_cdd_suspend_diag_kobj = kobject_create_and_add("google_cdd", kernel_kobj);
	if (!google_cdd_suspend_diag_kobj) {
		pr_err("Failed to create google_cdd folder\n");
		return -EINVAL;
	}

	if (sysfs_create_group(google_cdd_suspend_diag_kobj, &google_cdd_suspend_diag_attr_group)) {
		pr_err("Failed to create files in ../google_cdd/suspend_diag\n");
		kobject_put(google_cdd_suspend_diag_kobj);
		return -EINVAL;
	}

	return 0;
}
