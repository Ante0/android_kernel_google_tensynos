// SPDX-License-Identifier: GPL-2.0

#include "debug.h"

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/timekeeping.h>

#include <pvrsrvkm/services/server/devices/rgxpower.h>
#include <customer/volcanic/customer_dvfs.h>

#include "sysconfig.h"
#include "mba.h"
#include "sscd.h"

#define DEFINE_RO_DEBUGFS_ATTRIBUTE(NAME)            \
	static const struct file_operations NAME = { \
		.owner = THIS_MODULE,                \
		.read = on_debugfs_read,             \
		.open = simple_open,                 \
	}

#if defined(SUPPORT_LINUX_DVFS)
static int set_util_off_period_ms(void *data, u64 val)
{
	struct pixel_gpu_device *pixel_dev = data;
	PPVRSRV_DEVICE_NODE psDeviceNode = pixel_dev->dev_config->psDevNode;
	IMG_DVFS_DEVICE *psDVFSDevice =
		&psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;

	psDVFSDevice->off_period_ms = val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(fops_util_off_period_ms, NULL, set_util_off_period_ms, "%llu\n");
#endif

static int set_mba(void *data, u64 val)
{
	struct pixel_gpu_device *pixel_dev = data;
	PPVRSRV_DEVICE_NODE dev_node = pixel_dev->dev_config->psDevNode;
	int err;

	PVRSRVPowerLockWrite(dev_node);

	/* wake up the GPU */
	err = PVRSRVSetDevicePowerStateKM(dev_node, PVRSRV_DEV_POWER_STATE_ON,
					  PVRSRV_POWER_FLAGS_NONE);
	PVR_LOG_GOTO_IF_ERROR(err, "failed to wake the GPU", exit_unlock);

	mba_signal(pixel_dev, (u32)val);

exit_unlock:
	PVRSRVPowerUnlockWrite(dev_node);
	return err;
}
DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(fops_mba, NULL, set_mba, "%llu\n");

static int set_fw_dvfs(void *data, u64 freq)
{
	struct pixel_gpu_device *pixel_dev = data;
	PPVRSRV_DEVICE_NODE dev_node = pixel_dev->dev_config->psDevNode;
	PVRSRV_RGXDEV_INFO *info = dev_node->pvDevice;
	int err;

	PVRSRVPowerLockWrite(dev_node);

	/* wake up the GPU */
	err = PVRSRVSetDevicePowerStateKM(dev_node, PVRSRV_DEV_POWER_STATE_ON,
					  PVRSRV_POWER_FLAGS_NONE);
	PVR_LOG_GOTO_IF_ERROR(err, "failed to wake the GPU", exit_unlock);

	err = pixel_fw_dvfs_set_rate(info, (u32)freq);
	PVR_LOG_IF_ERROR(err, "failed to submit FW command");

exit_unlock:
	PVRSRVPowerUnlockWrite(dev_node);
	return err;
}
DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(fops_fw_dvfs, NULL, set_fw_dvfs, "%llu\n");

static ssize_t on_debugfs_read(struct file *file, char __user *buf, size_t len,
			       loff_t *ppos)
{
	struct dentry *dentry = file->f_path.dentry;
	struct pixel_gpu_debug_node *node = file->private_data;
	int result;

	result = debugfs_file_get(dentry);
	if (unlikely(result)) {
		return result;
	}

	result = node->read(node->parent, buf, len, ppos);

	debugfs_file_put(dentry);

	return result;
}

#define STATS_LINE_EXPR(EXPR) ktime_to_ms((EXPR).time_spent), (EXPR).transitions

#define GENPD_STATS_LINE(BASE_VAR, DOMAIN, STATE) \
	STATS_LINE_EXPR((BASE_VAR)[DOMAIN].notification_stats[STATE])

static int on_read_genpd_stats_summary(struct pixel_gpu_debug_info *debug,
				       char __user *buf, size_t len,
				       loff_t *ppos)
{
#define GENPD_STATS_BUF_SIZE 1024 // Enough to hold a max-size table
	struct pixel_gpu_debug_pd_stats pd_stats_copy[PIXEL_GPU_PM_DOMAIN_COUNT];
	struct pixel_gpu_debug_state_stats sum[PIXEL_GPU_PM_DOMAIN_COUNT];
	ktime_t current_time;
	char str[GENPD_STATS_BUF_SIZE];
	size_t size;

	mutex_lock(&debug->genpd_stats_mutex);
	memcpy(&pd_stats_copy, &debug->pd_stats, sizeof(pd_stats_copy));
	mutex_unlock(&debug->genpd_stats_mutex);

	memset(&sum, 0, sizeof(sum));

	current_time = ktime_get();

	for (int pd = PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD;
	     pd < PIXEL_GPU_PM_DOMAIN_COUNT; ++pd) {
		struct pixel_gpu_debug_pd_stats *current_pd =
			&pd_stats_copy[pd];
		struct pixel_gpu_debug_state_stats *notification_stats =
			current_pd->notification_stats;
		ktime_t *time_spent =
			&notification_stats[current_pd->last_notification]
				 .time_spent;

		// Count the time since the last transition when displaying the output,
		// otherwise the user would have to wait for a transition to get up-to-date
		// time data.
		*time_spent = ktime_add(*time_spent,
					ktime_sub(current_time,
						  current_pd->last_transition));

		for (int state = GENPD_NOTIFY_PRE_OFF;
		     state < (GENPD_NOTIFY_ON + 1); ++state) {
			sum[pd].time_spent =
				ktime_add(sum[pd].time_spent,
					  notification_stats[state].time_spent);
			sum[pd].transitions +=
				notification_stats[state].transitions;
		}
	}

	size = scnprintf(
		str, GENPD_STATS_BUF_SIZE,
		"pd                notification        time_spent_ms          transitions\n"
		"\n"
		"sswrp_gpu_pd      pre_on       %20lld %20lld\n"
		"                  on           %20lld %20lld\n"
		"                  pre_off      %20lld %20lld\n"
		"                  off          %20lld %20lld\n"
		"                  (sum)        %20lld %20lld\n"
		"\n"
		"gpu_core_logic_pd pre_on       %20lld %20lld\n"
		"                  on           %20lld %20lld\n"
		"                  pre_off      %20lld %20lld\n"
		"                  off          %20lld %20lld\n"
		"                  (sum)        %20lld %20lld\n",
		GENPD_STATS_LINE(pd_stats_copy,
				 PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD,
				 GENPD_NOTIFY_PRE_ON),
		GENPD_STATS_LINE(pd_stats_copy,
				 PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD,
				 GENPD_NOTIFY_ON),
		GENPD_STATS_LINE(pd_stats_copy,
				 PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD,
				 GENPD_NOTIFY_PRE_OFF),
		GENPD_STATS_LINE(pd_stats_copy,
				 PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD,
				 GENPD_NOTIFY_OFF),
		STATS_LINE_EXPR(sum[PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD]),
		GENPD_STATS_LINE(pd_stats_copy,
				 PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD,
				 GENPD_NOTIFY_PRE_ON),
		GENPD_STATS_LINE(pd_stats_copy,
				 PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD,
				 GENPD_NOTIFY_ON),
		GENPD_STATS_LINE(pd_stats_copy,
				 PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD,
				 GENPD_NOTIFY_PRE_OFF),
		GENPD_STATS_LINE(pd_stats_copy,
				 PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD,
				 GENPD_NOTIFY_OFF),
		STATS_LINE_EXPR(sum[PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD]));
	return simple_read_from_buffer(buf, len, ppos, str, size);
}

DEFINE_RO_DEBUGFS_ATTRIBUTE(fops_genpd_stats_summary);

static ssize_t trigger_uevent_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)file->private_data;
	struct gpu_uevent evt = { 0 };
	char str[8] = { 0 };

	if (count >= sizeof(str))
		return -EINVAL;

	if (copy_from_user(str, ubuf, count))
		return -EINVAL;

	str[count] = '\0';

	if (sscanf(str, "%u %u", &evt.type, &evt.info) != 2)
		return -EINVAL;

	evt.uid = OSGetCurrentApplicationUID();

	gpu_uevent_send(pixel_dev, &evt);

	return count;
}

static const struct file_operations fops_trigger_uevent = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = trigger_uevent_write,
	.llseek = default_llseek,
};

static ssize_t trigger_core_dump(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)file->private_data;
	PVRSRV_ROBUSTNESS_NOTIFY_DATA error = {
		.eResetReason = RGX_CONTEXT_RESET_REASON_NONE,
		.pid = current->pid,
		.uErrData = {0},
	};

	gpu_sscd_dump(pixel_dev, &error);

	return count;
}

static const struct file_operations fops_trigger_core_dump = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = trigger_core_dump,
	.llseek = default_llseek,
};

int pixel_gpu_debug_init(struct pixel_gpu_device *pixel_dev)
{
	struct pixel_gpu_debug_info *debug = &pixel_dev->debug;
	struct dentry *power;

	debug->root = debugfs_create_dir(pixel_dev->dev->of_node->name, NULL);
	power = debugfs_create_dir("pm", debug->root);

	mutex_init(&debug->genpd_stats_mutex);
	debug->genpd_stats_node = (struct pixel_gpu_debug_node){
		.parent = debug,
		.read = on_read_genpd_stats_summary,
	};
	debugfs_create_file("genpd_stats_summary", MAY_READ, power,
			    &debug->genpd_stats_node,
			    &fops_genpd_stats_summary);

	debugfs_create_file("fw_dvfs", MAY_WRITE, power, pixel_dev, &fops_fw_dvfs);

	debugfs_create_file("mba", MAY_WRITE, debug->root, pixel_dev, &fops_mba);

	debugfs_create_file("trigger_uevent", MAY_WRITE, debug->root,
				pixel_dev, &fops_trigger_uevent);

	debugfs_create_file("trigger_core_dump", MAY_WRITE, debug->root,
				pixel_dev, &fops_trigger_core_dump);

#if defined(SUPPORT_LINUX_DVFS)
	debugfs_create_file("util_off_period_ms", MAY_WRITE, debug->root,
				pixel_dev, &fops_util_off_period_ms);
#endif
	return 0;
}

void pixel_gpu_debug_deinit(struct pixel_gpu_device *pixel_dev)
{
	struct pixel_gpu_debug_info *debug = &pixel_dev->debug;

	debugfs_remove_recursive(debug->root);
}

void pixel_gpu_debug_update_genpd_state(struct pixel_gpu_device *pixel_dev,
					enum pixel_gpu_pm_domain domain,
					enum genpd_notication notification)
{
	struct pixel_gpu_debug_info *debug = &pixel_dev->debug;
	struct pixel_gpu_debug_pd_stats *pd_stats = &debug->pd_stats[domain];
	ktime_t current_time;
	ktime_t elapsed_time;
	ktime_t *time_spent =
		&pd_stats->notification_stats[pd_stats->last_notification]
			 .time_spent;

	mutex_lock(&debug->genpd_stats_mutex);
	current_time = ktime_get();
	elapsed_time = ktime_sub(current_time, pd_stats->last_transition);

	pd_stats->last_transition = current_time;
	pd_stats->last_notification = notification;

	*time_spent = ktime_add(*time_spent, elapsed_time);
	++pd_stats->notification_stats[notification].transitions;
	mutex_unlock(&debug->genpd_stats_mutex);
}
