// SPDX-License-Identifier: GPL-2.0-only
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/rwlock.h>
#include <linux/suspend.h>
#include <linux/timekeeping.h>

#include <soc/google/google_gtc.h>
#include <soc/google/google_timestamp_sync.h>

#define TIMESTAMP_SYNC_BUFFER_SIZE 10

/**
 * struct timestamp_sync_snapshot - Structure to store time snapshots.
 *
 * @gtc_tick: Global Timestamp Counter (GTC) counter/tick value.
 * @systime_snapshot: Contains kernel clock times (real, boot, mono raw)
 *                    and the related clock source counter value.
 *
 * This structure is internally used to maintain a snapshot of different kernel
 * clock times alongside the GTC counter/tick value. It facilitates
 * synchronization between various clock sources by storing their respective
 * timestamps in a coherent manner.
 */
struct timestamp_sync_snapshot {
	u64 gtc_tick;
	struct system_time_snapshot systime_snapshot;
};

struct timestamp_sync_ring_buffer {
	rwlock_t lock;
	struct timestamp_sync_snapshot buffer[TIMESTAMP_SYNC_BUFFER_SIZE];
	unsigned int latest_idx;
	unsigned int count;
};

static struct timestamp_sync_ring_buffer ts_buffer;

u64 goog_gtc_ticks_to_boottime(u64 gtc_tick)
{
	u64 gtc_nsec_offset;
	u64 snapshot_gtc_tick;
	ktime_t snapshot_boot;
	struct timestamp_sync_snapshot temp_snapshot;
	unsigned long flags;

	read_lock_irqsave(&ts_buffer.lock, flags);

	if (ts_buffer.count == 0) {
		read_unlock_irqrestore(&ts_buffer.lock, flags);
		return 0;
	}
	temp_snapshot = ts_buffer.buffer[ts_buffer.latest_idx];

	read_unlock_irqrestore(&ts_buffer.lock, flags);

	snapshot_gtc_tick = temp_snapshot.gtc_tick;
	snapshot_boot = temp_snapshot.systime_snapshot.boot;

	/*
	 * Addressing negative GTC offset handling:
	 * Due to the snapshot updates, the GTC tick value of the snapshot may be
	 * ahead of the user provided GTC tick value, and leading to a negative
	 * GTC offset.
	 *
	 * The underlying help function goog_gtc_ticks_to_ns() takes unsigned
	 * division, so doesn't support negative GTC tick inputs, and will lead to
	 * incorrect large GTC nanosecond offset from the negative GTC tick offset.
	 *
	 * To address this, we apply goog_gtc_ticks_to_ns() to both the snapshot
	 * and the user provided GTC tick values, and then subtract the result to
	 * get the GTC nanosecond offset. While the resulting GTC nanosecond
	 * offset may be negative, the unsigned type can hold bit representation
	 * correctly.
	 */
	gtc_nsec_offset = goog_gtc_ticks_to_ns(gtc_tick) -
			  goog_gtc_ticks_to_ns(snapshot_gtc_tick);

	return (u64)ktime_to_ns(snapshot_boot) + gtc_nsec_offset;
}
EXPORT_SYMBOL_GPL(goog_gtc_ticks_to_boottime);

void goog_timestamp_sync_snapshot_update(void)
{
	u64 gtc_tick_before, gtc_tick_after;
	unsigned long flags;
	unsigned int next_idx;
	struct timestamp_sync_snapshot *snapshot;

	write_lock_irqsave(&ts_buffer.lock, flags);

	next_idx = (ts_buffer.latest_idx + 1) % TIMESTAMP_SYNC_BUFFER_SIZE;
	snapshot = &ts_buffer.buffer[next_idx];

	/*
	 * We take two readings of GTC counter value and averaging them to
	 * provide a closer approximation of the GTC counter value at the
	 * monent the system time snapshot is taken.
	 */
	gtc_tick_before = goog_gtc_get_counter();
	ktime_get_snapshot(&snapshot->systime_snapshot);
	gtc_tick_after = goog_gtc_get_counter();

	snapshot->gtc_tick = (gtc_tick_before + gtc_tick_after) >> 1;

	ts_buffer.latest_idx = next_idx;
	if (ts_buffer.count < TIMESTAMP_SYNC_BUFFER_SIZE)
		ts_buffer.count++;

	write_unlock_irqrestore(&ts_buffer.lock, flags);
}
EXPORT_SYMBOL_GPL(goog_timestamp_sync_snapshot_update);

static void __timestamp_sync_snapshot_to_data(const struct timestamp_sync_snapshot *snapshot,
					      struct timestamp_snapshot_data *data)
{
	struct timespec64 ts;
	u64 local_gtc_nsec;

	/* rtc_ktime_to_tm isn't used here because it rounds up the nanosecond portion */
	ts = ktime_to_timespec64(snapshot->systime_snapshot.real);
	local_gtc_nsec = goog_gtc_ticks_to_ns(snapshot->gtc_tick);

	rtc_time64_to_tm(ts.tv_sec, &data->tm);
	data->tv_nsec = ts.tv_nsec;

	data->gtc_sec = div64_u64_rem(local_gtc_nsec, NSEC_PER_SEC, &data->gtc_nsec);
	data->mono_raw = snapshot->systime_snapshot.raw;
}

int goog_timestamp_sync_get_nth_snapshot(unsigned int n, struct timestamp_snapshot_data *data)
{
	unsigned long flags;
	struct timestamp_sync_snapshot temp_snapshot;
	unsigned int index;

	read_lock_irqsave(&ts_buffer.lock, flags);

	if (n >= ts_buffer.count) {
		read_unlock_irqrestore(&ts_buffer.lock, flags);
		return -EINVAL;
	}

	index = (ts_buffer.latest_idx + TIMESTAMP_SYNC_BUFFER_SIZE - n) %
		TIMESTAMP_SYNC_BUFFER_SIZE;
	temp_snapshot = ts_buffer.buffer[index];

	read_unlock_irqrestore(&ts_buffer.lock, flags);

	__timestamp_sync_snapshot_to_data(&temp_snapshot, data);

	return 0;
}
EXPORT_SYMBOL_GPL(goog_timestamp_sync_get_nth_snapshot);

int goog_timestamp_sync_get_all_snapshots(struct timestamp_snapshot_data *data_array,
					  unsigned int array_size)
{
	unsigned long flags;
	unsigned int i, count_to_copy, src_idx;
	struct timestamp_sync_snapshot local_snapshots[TIMESTAMP_SYNC_BUFFER_SIZE];

	if (!data_array || array_size == 0)
		return 0;

	read_lock_irqsave(&ts_buffer.lock, flags);

	count_to_copy = min(ts_buffer.count, array_size);
	if (count_to_copy == 0) {
		read_unlock_irqrestore(&ts_buffer.lock, flags);
		return 0;
	}

	src_idx = ts_buffer.latest_idx;
	for (i = 0; i < count_to_copy; ++i) {
		local_snapshots[i] = ts_buffer.buffer[src_idx];
		src_idx = src_idx ? src_idx - 1 : TIMESTAMP_SYNC_BUFFER_SIZE - 1;
	}

	read_unlock_irqrestore(&ts_buffer.lock, flags);

	for (i = 0; i < count_to_copy; ++i)
		__timestamp_sync_snapshot_to_data(&local_snapshots[i], &data_array[i]);

	return count_to_copy;
}
EXPORT_SYMBOL_GPL(goog_timestamp_sync_get_all_snapshots);

static void timestamp_sync_suspend_snapshot(char *annotation)
{
	struct timestamp_snapshot_data data;
	int err;

	goog_timestamp_sync_snapshot_update();

	err = goog_timestamp_sync_get_nth_snapshot(0, &data);
	if (unlikely(err)) {
		pr_err("Fail to get timestamp sync snapshot. err:%d\n", err);
		return;
	}

	pr_info("PM: suspend %s %d-%02d-%02d %02d:%02d:%02d.%09lu UTC, GTC: %llu.%llu\n",
		annotation, data.tm.tm_year + 1900, data.tm.tm_mon + 1, data.tm.tm_mday,
		data.tm.tm_hour, data.tm.tm_min, data.tm.tm_sec, data.tv_nsec,
		data.gtc_sec, data.gtc_nsec);
}

static int timestamp_sync_pm_notifier_handler(
		struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		timestamp_sync_suspend_snapshot("entry");
		break;
	case PM_POST_SUSPEND:
		timestamp_sync_suspend_snapshot("exit");
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block timestamp_sync_pm_notifier_block = {
	.notifier_call = timestamp_sync_pm_notifier_handler,
};

static int __init timestamp_sync_init(void)
{
	struct timestamp_snapshot_data data;
	int err;

	rwlock_init(&ts_buffer.lock);

	goog_timestamp_sync_snapshot_update();
	err = goog_timestamp_sync_get_nth_snapshot(0, &data);
	if (unlikely(err)) {
		pr_err("Fail to get timestamp sync snapshot. err:%d\n", err);
		return err;
	}

	pr_info("%s: kernel initialized timestamp: %d-%02d-%02d %02d:%02d:%02d.%09lu UTC, GTC: %llu.%llu\n",
		__func__, data.tm.tm_year + 1900, data.tm.tm_mon + 1, data.tm.tm_mday,
		data.tm.tm_hour, data.tm.tm_min, data.tm.tm_sec, data.tv_nsec,
		data.gtc_sec, data.gtc_nsec);

	register_pm_notifier(&timestamp_sync_pm_notifier_block);

	return 0;
}

static void __exit timestamp_sync_exit(void)
{
	unregister_pm_notifier(&timestamp_sync_pm_notifier_block);
}

module_init(timestamp_sync_init);
module_exit(timestamp_sync_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google timestamp synchronization driver");
MODULE_LICENSE("GPL");
