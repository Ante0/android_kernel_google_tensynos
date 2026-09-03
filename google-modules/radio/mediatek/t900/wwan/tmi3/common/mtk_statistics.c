// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2024, MediaTek Inc.
 */

#include <linux/device.h>
#include <linux/rtc.h>
#include <linux/sched/clock.h>

#include "mtk_debug.h"
#include "mtk_fsm.h"
#include "mtk_statistics.h"

#ifdef CONFIG_TX00_UT_STATS
#include "ut_statistics_fake.h"
#endif

#define TAG				"STATS"
#define STATISTICS_LOG_FORMAT_TYPE	"[0.0](0)"
#define STATS_DEBUG_REMOVE_DELAY_MS	20
#define STATS_DEBUG_REMOVE_RETRY	10
#define STATS_ATTR_REMOVE_DELAY_MS	20
#define STATS_ATTR_REMOVE_RETRY	10

static unsigned long mtk_stats_debug_mask = MTK_STATS_DEBUG_MASK_DEFAULT;

static void mtk_stats_restore_bitmap(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_statistics_type *type;
	struct mtk_statistics *stats;

	type = container_of(dwork, struct mtk_statistics_type, bit_restore_work);
	stats = type->stats;
	atomic_or(BIT(type - stats->stats_types), &stats->stats_debug_bitmap);
}

static void mtk_stats_cb_work(struct work_struct *work)
{
	struct mtk_statistics_type *type =
		container_of(work, struct mtk_statistics_type, stats_work);
	struct mtk_statistics *stats = type->stats;

	if (type->stats_cb)
		type->stats_cb(stats->mdev, type->data, NULL);

	queue_delayed_work(system_wq, &type->bit_restore_work, type->interval * HZ);
}

/**
 * mtk_stats_register_cb - Register a callback for statistics
 * @mdev: Pointer to the device structure
 * @type: Type of statistics to register
 * @interval: Minimum interval between callback invocations in seconds
 * @cb: Callback function to be registered
 * @data: Pointer to user-defined data to be passed to the callback function
 *
 * Registers a callback function for the specified type of statistics.
 * The callback will be invoked by mtk_stats_chk_and_proc, but only if
 * the specified interval has passed since the last invocation.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int mtk_stats_register_cb(struct mtk_md_dev *mdev, int type, u32 interval,
			  ssize_t (*cb)(struct mtk_md_dev *mdev, void *data, char *buf),
			  void *data)
{
	struct mtk_statistics *stats;

	if (!mdev || !mdev->stats || !mdev->utility_cfg)
		return -EINVAL;

	stats = mdev->stats;

	if (type >= mdev->utility_cfg->stats_cfg->stats_type_cnt || type < 0) {
		MTK_ERR(mdev, "Invalid statistics type\n");
		return -EINVAL;
	}

	if (stats->stats_types[type].stats_cb) {
		MTK_ERR(mdev, "Statistics callback already registered for type %d\n", type);
		return -EEXIST;
	}
	stats->stats_types[type].stats_cb = cb;
	stats->stats_types[type].interval = interval;
	stats->stats_types[type].stats = stats;
	stats->stats_types[type].data = data;
	INIT_WORK(&stats->stats_types[type].stats_work, mtk_stats_cb_work);
	INIT_DELAYED_WORK(&stats->stats_types[type].bit_restore_work,
			  mtk_stats_restore_bitmap);
	MTK_INFO(mdev, "Register stats cb: type=%d, interval=%d, cb=%ps\n", type, interval, cb);

	return 0;
}
EXPORT_SYMBOL(mtk_stats_register_cb);

int mtk_stats_unregister_cb(struct mtk_md_dev *mdev, int type)
{
	struct mtk_statistics *stats;

	if (!mdev || !mdev->stats || !mdev->utility_cfg)
		return -EINVAL;

	stats = mdev->stats;
	if (type >= mdev->utility_cfg->stats_cfg->stats_type_cnt || type < 0) {
		MTK_ERR(mdev, "Invalid statistics type\n");
		return -EINVAL;
	}
	flush_work(&stats->stats_types[type].stats_work);
	cancel_delayed_work_sync(&stats->stats_types[type].bit_restore_work);
	stats->stats_types[type].stats_cb = NULL;
	stats->stats_types[type].data = NULL;
	MTK_INFO(mdev, "Unegister statistics cb: type=%d\n", type);

	return 0;
}
EXPORT_SYMBOL(mtk_stats_unregister_cb);

/**
 * mtk_stats_chk_and_proc - Check and process statistics
 * @mdev: Pointer to the device structure
 * @types: Bitmap of statistics types to process
 *
 * This function checks the statistics debug bitmap and queues work for
 * each enabled statistics type.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int mtk_stats_chk_and_proc(struct mtk_md_dev *mdev, unsigned long types)
{
	struct mtk_statistics *stats = mdev->stats;
	unsigned long ori;
	int ret = 0;
	int type;

	/* Increment stats_debug_refcnt to ensure that all events that pass the check */
	/* are queued before flush work in mtk_stats_method_debug_config. */
	atomic_inc(&stats->stats_debug_refcnt);
	if (!stats->stats_debug_is_enabled) {
		ret = -ENODEV;
		goto out;
	}

	types &= ~mtk_stats_debug_mask;

	/* This operation sets the bits in stats_debug_bitmap to 0 for the specified types, */
	/* and returns the original value of stats_debug_bitmap before the bits were cleared. */
	ori = atomic_fetch_and(~types, &stats->stats_debug_bitmap);

	/* Iterate through the ori (original bitmap) and queue work for each enabled type. */
	ori &= types;
	while (ori) {
		type = fls(ori) - 1;
		ori &= ~BIT(type);
		queue_work(stats->stats_wq, &stats->stats_types[type].stats_work);
	}
out:
	atomic_dec(&stats->stats_debug_refcnt);
	return ret;
}
EXPORT_SYMBOL(mtk_stats_chk_and_proc);

static ssize_t stats_header_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	struct timespec64 ts64 = {0};
	u64 ts_nsec, rem_nsec;
	struct rtc_time rt;
	ssize_t size;

	size = sprintf(buf, STATISTICS_LOG_FORMAT_TYPE);
	if (size < 0)
		MTK_ERR(mdev, "STATISTICS_LOG_FORMAT_TYPE sprintf failed!\n");

	size += snprintf(buf + size, MTK_DFLT_HEADER_LEN,
			"\n==========%s%s 0x%x==========\n",
			"REGION_", TAG, MTK_MEMLOG_F_EVENT);
	if (size < 0)
		MTK_ERR(mdev, "MTK_DFLT_HEADER_LEN sprintf failed!\n");

	ktime_get_real_ts64(&ts64);
	ts64.tv_sec = ts64.tv_sec - (time64_t)sys_tz.tz_minuteswest * 60;
	rtc_time64_to_tm(ts64.tv_sec, &rt);
	ts_nsec = local_clock();
	rem_nsec = do_div(ts_nsec, 1000000000);
	size += snprintf(buf + size, MTK_DFLT_HEADER_LEN,
			"[%d-%02d-%02d %02d:%02d:%02d.%03d],[%5lu.%06lu]\n",
			rt.tm_year + 1900, rt.tm_mon + 1, rt.tm_mday,
			rt.tm_hour, rt.tm_min, rt.tm_sec, (unsigned int)ts64.tv_nsec / 1000,
			(unsigned long)ts_nsec, (unsigned long)rem_nsec / 1000);
	if (size < 0)
		MTK_ERR(mdev, "snprintf failed!\n");

	return size;
}
static DEVICE_ATTR_RO(stats_header);

static ssize_t stats_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	struct mtk_statistics *stats = mdev->stats;
	ssize_t size = 0;
	int type;

	type = stats->stats_attr_type;

	if (atomic_cmpxchg(&stats->stats_attr_in_use, 0, 1))
		return -EBUSY;

	if (stats->stats_types[type].stats_cb) {
		size = stats->stats_types[type].stats_cb(mdev, stats->stats_types[type].data, buf);
	} else {
		size = snprintf(buf, MTK_STATS_INFO_LEN,
				"Requested statistics not supported, stats_attr_type=%d", type);
		if (size < 0)
			MTK_ERR(mdev, "snprintf failed!\n");
	}

	atomic_set(&stats->stats_attr_in_use, 0);

	return size;
}

static ssize_t stats_type_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	struct mtk_statistics *stats = mdev->stats;
	int ret;
	int id;

	ret = kstrtoint(buf, 10, &id);
	if (ret < 0)
		return ret;

	if (id >= mdev->utility_cfg->stats_cfg->stats_type_cnt || id < 0)
		return -EINVAL;
	stats->stats_attr_type = id;

	return count;
}
static DEVICE_ATTR_RW(stats_type);

static ssize_t stats_interval_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	struct mtk_statistics *stats = mdev->stats;
	int interval;
	int ret;

	ret = kstrtoint(buf, 10, &interval);
	if (ret < 0)
		return ret;

	if (interval < MTK_STATS_INTERVAL_MIN)
		return -EINVAL;
	MTK_INFO(mdev, "interval of stats_type %d change %d->%d\n", stats->stats_attr_type,
		 stats->stats_types[stats->stats_attr_type].interval, interval);
	stats->stats_types[stats->stats_attr_type].interval = interval;

	return count;
}
static DEVICE_ATTR_WO(stats_interval);

static void mtk_stats_method_debug_config(struct mtk_md_dev *mdev, bool enable)
{
	struct mtk_statistics *stats = mdev->stats;
	struct mtk_statistics_type *s_type;
	int type, value, retries = 0;

	if (enable) {
		atomic_set(&stats->stats_debug_bitmap, 0xFFFFFFFF);
		stats->stats_debug_is_enabled = true;
	} else {
		stats->stats_debug_is_enabled = false;
		do {
			value = atomic_read(&mdev->stats->stats_debug_refcnt);
			msleep(STATS_DEBUG_REMOVE_DELAY_MS);
			retries++;
			if (retries > STATS_DEBUG_REMOVE_RETRY) {
				MTK_ERR(mdev, "Failed to wait stats_debug, refcnt = %d\n", value);
				break;
			}
		} while (value);
		flush_workqueue(stats->stats_wq);
		for (type = 0; type < mdev->utility_cfg->stats_cfg->stats_type_cnt; type++) {
			if (stats->stats_types[type].stats_cb) {
				s_type = &stats->stats_types[type];
				cancel_delayed_work_sync(&s_type->bit_restore_work);
			}
		}
	}
}

static void mtk_stats_method_attr_config(struct mtk_md_dev *mdev, bool enable)
{
	static bool current_state;
	int ret, retries = 0;

	/* If the state has not changed, return immediately */
	if (enable == current_state)
		return;

	if (enable) {
		current_state = true;
		atomic_set(&mdev->stats->stats_attr_in_use, 0);
		ret = device_create_file(mdev->dev, &dev_attr_stats_header);
		if (ret)
			MTK_ERR(mdev, "Failed to create stats_header attribute\n");
		ret = device_create_file(mdev->dev, &dev_attr_stats_type);
		if (ret)
			MTK_ERR(mdev, "Failed to create stats_type attribute\n");
		ret = device_create_file(mdev->dev, &dev_attr_stats_interval);
		if (ret)
			MTK_ERR(mdev, "Failed to create stats_interval attribute\n");
	} else {
		current_state = false;
		device_remove_file(mdev->dev, &dev_attr_stats_interval);
		device_remove_file(mdev->dev, &dev_attr_stats_type);
		device_remove_file(mdev->dev, &dev_attr_stats_header);

		/* Ensure that any thread already in stats_type_show completes */
		/* before proceeding to free the structures, preventing null pointer dereference. */
		while (atomic_cmpxchg(&mdev->stats->stats_attr_in_use, 0, 1) != 0) {
			msleep(STATS_ATTR_REMOVE_DELAY_MS);
			retries++;
			if (retries > STATS_ATTR_REMOVE_RETRY) {
				MTK_ERR(mdev, "Failed to remove stats_type attribute\n");
				break;
			}
		}
	}
}

static void mtk_stats_fsm_handler(struct mtk_fsm_param *param, void *data)
{
	struct mtk_md_dev *mdev = data;

	switch (param->to) {
	case FSM_STATE_READY:
		mtk_stats_method_debug_config(mdev, true);
		mtk_stats_method_attr_config(mdev, true);
		break;
	case FSM_STATE_OFF:
		mtk_stats_method_attr_config(mdev, false);
		mtk_stats_method_debug_config(mdev, false);
		break;
	default:
		break;
	}
}

int mtk_stats_init(struct mtk_md_dev *mdev)
{
	struct mtk_statistics_cfg *stats_cfg;
	struct mtk_statistics *stats;

	if (!mdev || !mdev->utility_cfg)
		return -EINVAL;

	stats_cfg = mdev->utility_cfg->stats_cfg;

	stats = devm_kzalloc(mdev->dev, sizeof(*stats) +
			     sizeof(struct mtk_statistics_type) * stats_cfg->stats_type_cnt,
			     GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	stats->mdev = mdev;
	mdev->stats = stats;

	stats->stats_wq = alloc_workqueue("stats_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
	if (!stats->stats_wq) {
		devm_kfree(mdev->dev, stats);
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(mtk_stats_init);

int mtk_stats_init_late(struct mtk_md_dev *mdev)
{
	struct mtk_statistics *stats;
	int ret;

	if (!mdev || !mdev->stats)
		return -EINVAL;

	stats = mdev->stats;
	ret = mtk_fsm_notifier_register(mdev, MTK_USER_STATS,
					mtk_stats_fsm_handler, mdev, FSM_PRIO_0, true);
	if (ret)
		MTK_ERR(mdev, "Failed to register stats fsm notifier\n");

	return ret;
}
EXPORT_SYMBOL(mtk_stats_init_late);

int mtk_stats_exit_early(struct mtk_md_dev *mdev)
{
	if (!mdev)
		return -EINVAL;

	mtk_fsm_notifier_unregister(mdev, MTK_USER_STATS);

	return 0;
}
EXPORT_SYMBOL(mtk_stats_exit_early);

int mtk_stats_exit(struct mtk_md_dev *mdev)
{
	struct mtk_statistics *stats;

	if (!mdev || !mdev->stats)
		return -EINVAL;

	stats = mdev->stats;
	if (stats->stats_wq)
		destroy_workqueue(stats->stats_wq);

	devm_kfree(mdev->dev, stats);

	return 0;
}
EXPORT_SYMBOL(mtk_stats_exit);

module_param(mtk_stats_debug_mask, ulong, 0644);
MODULE_PARM_DESC(mtk_stats_debug_mask, "Disables the set types' stats debug to tmi_log.");
