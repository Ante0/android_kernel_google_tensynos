/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __MTK_PCIE_PM_USER_INTERNAL__
#define __MTK_PCIE_PM_USER_INTERNAL__
#include "../common/radio-google.h"
#include "mtk-pcie-pm-user.h"
#include "mtk-pcie.h"

#define SPM_HISTORY_SIZE 16
#define RPM_ERROR_HISTORY_SIZE 16

struct mtk_pci_user_rpm_stats {
	atomic_t get_count;
	atomic_t put_count;
	atomic_t active_count;
};

struct spm_history_entry {
	struct timespec64 timestamp;
	bool results[MTK_PCI_USER_MAX];
	bool final_decision; /* The overall outcome (true means vetoed) */
};

/* The main manager for this subsystem */
struct spm_vote_manager {
	/* History Table */
	struct spm_history_entry history[SPM_HISTORY_SIZE];
	atomic_t history_idx; /* A circular buffer index */

	/* Debugfs */
	struct dentry *dentry;
};

enum rpm_error_type {
	RPM_ERROR_NONE,
	RPM_ERROR_UNDERFLOW,
	RPM_ERROR_LEAK_RESET,
};

struct rpm_error_entry {
	struct timespec64 timestamp;
	enum mtk_pci_user_id user_id;
	enum rpm_error_type error_type;
	int get_count;
	int put_count;
	int kernel_usage_count;
};

struct rpm_error_history {
	spinlock_t lock; /* Protects entries and current_idx */
	struct rpm_error_entry entries[RPM_ERROR_HISTORY_SIZE];
	int current_idx;
};

struct mtk_pci_user_pm {
	struct radio_google *goog;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dentry;
#endif /* CONFIG_DEBUG_FS */
	/* RPM */
	struct mtk_pci_user_rpm_stats rpm_stats[MTK_PCI_USER_MAX];
	struct rpm_error_history rpm_error_history;
	/* SPM */
	struct spm_vote_manager spm_vote_manager;
	bool prev_spm_decision;
};

int mtk_pci_user_init(struct radio_google *radio_google);

void mtk_pci_user_exit(struct radio_google *goog);

bool mtk_pci_user_pm_any_active(void);

void mtk_pci_user_dump_active_users(void);

/**
 * mtk_pci_user_spm_eval_busy_state - Evaluate busy state and update SPM history
 *
 * NOTE: This function is STATEFUL. It advances the internal vote history index
 * and toggles the suspend/resume state machine.
 *
 * It should only be called once per Suspend/Resume transition.
 * For stateless polling, use mtk_pci_user_pm_any_active() instead.
 *
 * Return: true if busy (VETO), false if idle (ALLOW).
 */
bool mtk_pci_user_spm_eval_busy_state(bool is_suspend);

/*
 * mtk_pci_user_register_busy_wq - Register the wq for pci user active
 *
 * This function registers the wait queue used in
 * mtk_pcimsg_wait_pci_user_inactive
 */
int mtk_pci_user_register_busy_wq(struct wait_queue_head *wq);

/*
 * mtk_pci_user_unregister_busy_wq - Unregister the wq for pci user active
 *
 * This function unregisters the wait queue used in
 * mtk_pcimsg_wait_pci_user_inactive
 *
 */
void mtk_pci_user_unregister_busy_wq(void);
#endif /* __MTK_PCIE_PM_USER_INTERNAL__ */
