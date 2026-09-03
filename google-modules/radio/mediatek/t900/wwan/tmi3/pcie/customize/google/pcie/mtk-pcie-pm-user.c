// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include "linux/debugfs.h"
#include "linux/export.h"
#include "linux/stddef.h"
#include "linux/wait.h"
#include "mtk-pcie-pm-user-internal.h"
#include "mtk_debug.h"
#include "mtk_debugfs.h"
#include "radio-utils.h"
#include <linux/pm_runtime.h>

static struct mtk_md_dev *mdev;
static struct wait_queue_head *pci_busy_wq;
static struct mtk_pci_user_pm_ops user_pm_ops;

static const char *const user_id_names[MTK_PCI_USER_MAX] = {
	[MTK_PCI_USER_AUDIO] = "AUDIO",
	[MTK_PCI_USER_GNSS] = "GNSS",
	[MTK_PCI_USER_NOA] = "NOA",
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
static void mtk_pci_user_pm_dbgfs_init(struct radio_google *goog);
static void mtk_pci_user_pm_dbgfs_exit(struct radio_google *goog);
#endif /* CONFIG_DEBUG_FS */

static void mtk_pci_rpm_stats_init(struct mtk_pci_user_pm *user_pm);
static void mtk_pci_spm_vote_manager_init(struct mtk_pci_user_pm *user_pm);
static void record_rpm_error(enum mtk_pci_user_id user_id, enum rpm_error_type type);

// TODO: b/473984596 - Workaround to lock DS during VoLTE/VoNR.
void mtk_pci_user_register_pm_ops(struct mtk_pci_user_pm_ops *ops)
{
	if (ops) {
		user_pm_ops = *ops;
		LOG_INFO("PM ops registered successfully\n");
	}
}
EXPORT_SYMBOL_GPL(mtk_pci_user_register_pm_ops);

int mtk_pci_user_register_busy_wq(struct wait_queue_head *wq)
{
	pci_busy_wq = wq;
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_pci_user_register_busy_wq);

void mtk_pci_user_unregister_busy_wq(void)
{
	pci_busy_wq = NULL;
}
EXPORT_SYMBOL_GPL(mtk_pci_user_unregister_busy_wq);

int mtk_pci_user_init(struct radio_google *goog)
{
	struct mtk_pci_user_pm *user_pm;

	if (!goog)
		return -ENODEV;

	user_pm = devm_kzalloc(goog->mdev->dev, sizeof(*user_pm), GFP_KERNEL);
	if (!user_pm)
		return -ENOMEM;

	user_pm->goog = goog;
	goog->mtk_pci_user_pm = user_pm;

	mtk_pci_rpm_stats_init(user_pm);
	mtk_pci_spm_vote_manager_init(user_pm);

	user_pm->prev_spm_decision = false;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	mtk_pci_user_pm_dbgfs_init(goog);
#endif /* CONFIG_DEBUG_FS */

	mdev = goog->mdev;

	return 0;
}

void mtk_pci_user_exit(struct radio_google *goog)
{
	struct mtk_pci_user_pm *user_pm;

	if (!goog)
		return;

	user_pm = goog->mtk_pci_user_pm;

	if (!user_pm)
		return;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	mtk_pci_user_pm_dbgfs_exit(goog);
#endif /* CONFIG_DEBUG_FS */
}

static void mtk_pci_rpm_stats_init(struct mtk_pci_user_pm *user_pm)
{
	if (!user_pm)
		return;

	spin_lock_init(&user_pm->rpm_error_history.lock);
	user_pm->rpm_error_history.current_idx = 0;
	memset(user_pm->rpm_error_history.entries, 0, sizeof(user_pm->rpm_error_history.entries));

	for (int i = 0; i < MTK_PCI_USER_MAX; i++) {
		atomic_set(&user_pm->rpm_stats[i].get_count, 0);
		atomic_set(&user_pm->rpm_stats[i].put_count, 0);
		atomic_set(&user_pm->rpm_stats[i].active_count, 0);
	}
}

int mtk_pci_user_rpm_get(enum mtk_pci_user_id user_id)
{
	struct radio_google *goog;
	struct mtk_pci_user_pm *user_pm;

	int ret, status;

	if (user_id >= MTK_PCI_USER_MAX)
		return -EINVAL;

	if (!mdev)
		return -EINVAL;

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	atomic_inc(&user_pm->rpm_stats[user_id].get_count);
	ret = pm_runtime_resume_and_get(mdev->dev);
	if (ret)
		return ret;

	atomic_inc(&user_pm->rpm_stats[user_id].active_count);

	LOG_INFO("User: %d, get ret: %d\n", user_id, ret);

	// TODO: b/473984596 - Workaround to lock DS during VoLTE/VoNR.
	if (user_id == MTK_PCI_USER_AUDIO) {
		LOG_INFO("Lock DS for Audio\n");
		if (user_pm_ops.lock && user_pm_ops.wait_complete) {
			user_pm_ops.lock(mdev, MTK_USER_CTRL);
			status = user_pm_ops.wait_complete(mdev, MTK_USER_CTRL);

			if (unlikely(status < 0))
				LOG_ERR("Failed to wait ds_lock\n");
		} else {
			LOG_ERR("Lock and wait not implemented\n");
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_pci_user_rpm_get);

int mtk_pci_user_rpm_put(enum mtk_pci_user_id user_id)
{
	struct radio_google *goog;
	struct mtk_pci_user_pm *user_pm;
	int active_count, get_total, put_total;
	int ret;

	if (user_id >= MTK_PCI_USER_MAX)
		return -EINVAL;

	if (!mdev)
		return -EINVAL;

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	// TODO: b/473984596 - Workaround to lock DS during VoLTE/VoNR.
	if (user_id == MTK_PCI_USER_AUDIO) {
		LOG_INFO("Unlock DS for Audio\n");
		if (user_pm_ops.unlock)
			user_pm_ops.unlock(mdev, MTK_USER_CTRL);
		else
			LOG_ERR("Unlock not implemented\n");
	}

	/* Use sync for put for consistency and immediate effect */
	ret = pm_runtime_put_sync(mdev->dev);

	/* runtime PM usage counter of @dev remains decremented in all cases,
	 * even if it returns an error code.
	 */
	put_total = atomic_inc_return(&user_pm->rpm_stats[user_id].put_count);
	active_count = atomic_dec_return(&user_pm->rpm_stats[user_id].active_count);
	get_total = atomic_read(&user_pm->rpm_stats[user_id].get_count);

	if (active_count < 0) {
		LOG_ERR("RPM underflow detected for user %d! (get:%d, put:%d)\n", user_id,
			get_total, put_total);

		record_rpm_error(user_id, RPM_ERROR_UNDERFLOW);

		WARN_ON_ONCE(1);

	} else if (active_count == 0) {
		if (pci_busy_wq)
			wake_up(pci_busy_wq);
	}

	LOG_INFO("User: %d, put ret: %d\n", user_id, ret);

	/*
	 * Don't return error code from pm_runtime_put_sync(). There's not
	 * much a caller can do about it and pm_runtime_put_sync() is documented
	 * to decrement the counter even if an error was returned.
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_pci_user_rpm_put);

static void mtk_pci_spm_vote_manager_init(struct mtk_pci_user_pm *user_pm)
{
	if (!user_pm)
		return;

	atomic_set(&user_pm->spm_vote_manager.history_idx, -1);
	memset(user_pm->spm_vote_manager.history, 0, sizeof(user_pm->spm_vote_manager.history));
}

bool mtk_pci_user_pm_any_active(void)
{
	struct radio_google *goog;
	struct mtk_pci_user_pm *user_pm;
	int i;

	if (!mdev)
		return true;

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	for (i = 0; i < MTK_PCI_USER_MAX; i++) {
		bool user_vote = (atomic_read(&user_pm->rpm_stats[i].active_count) != 0);

		if (user_vote)
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(mtk_pci_user_pm_any_active);

bool mtk_pci_user_spm_eval_busy_state(bool is_suspend)
{
	struct radio_google *goog;
	struct mtk_pci_user_pm *user_pm;
	bool final_veto = false;
	int idx, i;

	if (!mdev)
		return false;

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	if (!is_suspend) {
		LOG_INFO("System Resume Final Decision: %s\n",
			 user_pm->prev_spm_decision ? "VETO (Keep L1.2)" : "OK (Allow L2)");
		return user_pm->prev_spm_decision;
	}

	/* Get the next slot in our circular history buffer */
	idx = atomic_inc_return(&user_pm->spm_vote_manager.history_idx) % SPM_HISTORY_SIZE;

	struct spm_history_entry *entry = &user_pm->spm_vote_manager.history[idx];

	/* Get a real-world timestamp that matches dmesg */
	ktime_get_boottime_ts64(&entry->timestamp);

	for (i = 0; i < MTK_PCI_USER_MAX; i++) {
		bool user_vote = (atomic_read(&user_pm->rpm_stats[i].active_count) != 0);

		entry->results[i] = user_vote;
		if (user_vote)
			final_veto = true;
	}

	entry->final_decision = final_veto;
	LOG_INFO("System Suspend Final Decision: %s\n",
		 final_veto ? "VETO (Keep L1.2)" : "OK (Allow L2)");

	user_pm->prev_spm_decision = final_veto;

	return final_veto;
}
EXPORT_SYMBOL_GPL(mtk_pci_user_spm_eval_busy_state);

void mtk_pci_user_dump_active_users(void)
{
	struct radio_google *goog;
	struct mtk_pci_user_pm *user_pm;
	int i;
	bool found_any = false;

	if (!mdev)
		return;

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	for (i = 0; i < MTK_PCI_USER_MAX; i++) {
		int active = atomic_read(&user_pm->rpm_stats[i].active_count);
		const char *name = user_id_names[i] ? user_id_names[i] : "UNKNOWN";

		if (active != 0) {
			found_any = true;
			LOG_ERR("[User %d - %s] is ACTIVE (Count: %d | Total Get: %d, Put: %d)\n",
				i, name, active, atomic_read(&user_pm->rpm_stats[i].get_count),
				atomic_read(&user_pm->rpm_stats[i].put_count));
		}
	}

	if (!found_any)
		LOG_ERR("No active users found\n");
}
EXPORT_SYMBOL_GPL(mtk_pci_user_dump_active_users);

static void record_rpm_error(enum mtk_pci_user_id user_id, enum rpm_error_type type)
{
	struct radio_google *goog;
	struct mtk_pci_user_pm *user_pm;
	unsigned long flags;
	int idx;
	struct rpm_error_entry *entry;

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	spin_lock_irqsave(&user_pm->rpm_error_history.lock, flags);

	idx = user_pm->rpm_error_history.current_idx;
	entry = &user_pm->rpm_error_history.entries[idx];

	ktime_get_boottime_ts64(&entry->timestamp);
	entry->user_id = user_id;
	entry->error_type = type;
	entry->get_count = atomic_read(&user_pm->rpm_stats[user_id].get_count);
	entry->put_count = atomic_read(&user_pm->rpm_stats[user_id].put_count);
	entry->kernel_usage_count = atomic_read(&mdev->dev->power.usage_count);

	user_pm->rpm_error_history.current_idx = (idx + 1) % RPM_ERROR_HISTORY_SIZE;

	spin_unlock_irqrestore(&user_pm->rpm_error_history.lock, flags);
}

#ifdef CONFIG_DEBUG_FS
static int mtk_pci_user_rpm_stats_show(struct seq_file *s, void *v)
{
	/* seq_file put private_data in s->private */
	struct mtk_md_dev *mdev = s->private;
	struct radio_google *goog;
	struct mtk_pci_user_pm *user_pm;
	int i;

	if (!mdev) {
		seq_puts(s, "ERROR: mdev context is NULL\n");
		return 0;
	}

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	seq_puts(s, "--- RPM User Vote Stats ---\n");
	for (i = 0; i < MTK_PCI_USER_MAX; i++) {
		seq_printf(s, "User %d: get_count = %d, put_count = %d\n", i,
			   atomic_read(&user_pm->rpm_stats[i].get_count),
			   atomic_read(&user_pm->rpm_stats[i].put_count));
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mtk_pci_user_rpm_stats);

/* Helper to convert error enum to string */
static const char *rpm_err_to_str(enum rpm_error_type type)
{
	switch (type) {
	case RPM_ERROR_UNDERFLOW:
		return "UNDERFLOW";
	case RPM_ERROR_LEAK_RESET:
		return "LEAK_RESET";
	default:
		return "UNKNOWN";
	}
}

static int rpm_error_history_show(struct seq_file *s, void *v)
{
	struct mtk_md_dev *mdev = s->private;
	struct mtk_pci_user_pm *user_pm;
	struct radio_google *goog;
	struct rpm_error_history *err_hist;
	int i, start_idx;
	unsigned long flags;

	if (!mdev)
		return 0;

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	err_hist = &user_pm->rpm_error_history;

	seq_puts(s, "--- RPM Error/Event History ---\n");
	seq_puts(s, "(Newest events are first)\n\n");

	spin_lock_irqsave(&err_hist->lock, flags);
	start_idx = err_hist->current_idx;

	for (i = 0; i < RPM_ERROR_HISTORY_SIZE; i++) {
		/* Print circular buffer from newest to oldest */
		int idx = (start_idx - 1 - i + RPM_ERROR_HISTORY_SIZE) % RPM_ERROR_HISTORY_SIZE;
		struct rpm_error_entry *entry = &err_hist->entries[idx];

		if (entry->timestamp.tv_sec == 0)
			continue;

		seq_printf(s, "[%5lld.%06ld] User %d, Event: %s\n",
			   (long long)entry->timestamp.tv_sec, entry->timestamp.tv_nsec / 1000,
			   entry->user_id, rpm_err_to_str(entry->error_type));
		seq_printf(s, "  -> Counts at event time: get=%d, put=%d, kernel_usage=%d\n",
			   entry->get_count, entry->put_count, entry->kernel_usage_count);
	}
	spin_unlock_irqrestore(&err_hist->lock, flags);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rpm_error_history);

static int mtk_pci_user_spm_stats_show(struct seq_file *s, void *v)
{
	int i, j;
	int current_idx;
	struct mtk_md_dev *mdev = s->private;
	struct radio_google *goog;
	struct mtk_pci_user_pm *user_pm;

	if (!mdev) {
		seq_puts(s, "ERROR: mdev context is NULL\n");
		return 0;
	}

	goog = mdev->google;
	user_pm = goog->mtk_pci_user_pm;

	seq_puts(s, "Vote History (Last 16 Suspend Events):\n");

	current_idx = atomic_read(&user_pm->spm_vote_manager.history_idx);

	for (i = 0; i < SPM_HISTORY_SIZE; i++) {
		/* Print circular buffer from newest to oldest */
		int idx = (current_idx - i + SPM_HISTORY_SIZE) % SPM_HISTORY_SIZE;
		struct spm_history_entry *entry = &user_pm->spm_vote_manager.history[idx];

		/* If timestamp is 0, it's an empty entry */
		if (entry->timestamp.tv_sec == 0)
			continue;

		seq_printf(s, "[%5lld.%06ld] Final Decision: %s\n",
			   (long long)entry->timestamp.tv_sec,
			   entry->timestamp.tv_nsec / 1000, /* nsec to usec */
			   entry->final_decision ? "VETO (Keep L0/L1.2)" : "OK (Allow L2)");

		for (j = 0; j < MTK_PCI_USER_MAX; j++) {
			seq_printf(s, "  - User %d: Voted to %s\n", j,
				   entry->results[j] ? "VETO" : "Allow");
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mtk_pci_user_spm_stats);

static void mtk_pci_user_pm_dbgfs_init(struct radio_google *goog)
{
	struct dentry *parent_dir, *pm_user_dir;

	if (!goog || !goog->mdev)
		return;

	parent_dir = mtk_get_dev_dentry(goog->mdev);
	if (!parent_dir)
		return;

	/* Create pm_user dir */
	pm_user_dir = debugfs_create_dir("pm_user", parent_dir);
	if (IS_ERR_OR_NULL(pm_user_dir))
		return;

	/* Save dentry for _exit */
	goog->mtk_pci_user_pm->dentry = pm_user_dir;

	/* Create rpm_stats under the dir */
	debugfs_create_file("rpm_stats", 0444, pm_user_dir, goog->mdev,
			    &mtk_pci_user_rpm_stats_fops);

	/* Create rpm_error_history under the dir */
	debugfs_create_file("rpm_error_history", 0444, pm_user_dir, goog->mdev,
			    &rpm_error_history_fops);

	/* Create spm_stats under the dir */
	debugfs_create_file("spm_vote_stats", 0444, pm_user_dir, goog->mdev,
			    &mtk_pci_user_spm_stats_fops);
}

/*
 * mtk_pci_spm_vote_manager_exit - Cleanup the subsystem.
 */
static void mtk_pci_user_pm_dbgfs_exit(struct radio_google *goog)
{
	if (goog && goog->mtk_pci_user_pm && goog->mtk_pci_user_pm->dentry)
		debugfs_remove_recursive(goog->mtk_pci_user_pm->dentry);
}

#endif /* CONFIG_DEBUG_FS */
