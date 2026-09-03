// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 */

#ifndef GOOGLE_CDD_LOG_H
#define GOOGLE_CDD_LOG_H

#include <linux/clk-provider.h>
#include <soc/google/google-cdd.h>

#define CDD_NR_CPUS			CONFIG_GOOGLE_CDD_NR_CPUS
#define CDD_DOMAIN_NUM			CONFIG_GOOGLE_CDD_FREQ_DOMAIN_NUM
#define CDD_LOG_MAX_NUM			CONFIG_GOOGLE_CDD_LOG_ITEM_SIZE

#define TASK_COMM_LEN			16

struct task_log {
	unsigned long long time;
	struct task_struct *task;
	char task_comm[TASK_COMM_LEN];
	unsigned long se_exec_start;
	int pid;
};

struct work_log {
	unsigned long long time;
	work_func_t fn;
	int en;
};

struct cpuidle_log {
	unsigned long long time;
	char *modes;
	unsigned int state;
	u32 num_online_cpus;
	int delta;
	int en;
};

struct suspend_log {
	unsigned long long time;
	const char *log;
	const char *dev;
	int en;
	int event;
	short core;
	unsigned short delta_time_h;
	unsigned int delta_time_l;
};

struct irq_log {
	unsigned long long time;
	int irq;
	void *fn;
	struct irq_desc *desc;
	int en;
};

struct freq_log {
	const char *freq_name;
	unsigned int *old_freq;
	unsigned int *target_freq;
};

enum cdd_log_item_indx {
	CDD_LOG_TASK_ID,
	CDD_LOG_WORK_ID,
	CDD_LOG_CPUIDLE_ID,
	CDD_LOG_SUSPEND_ID,
	CDD_LOG_IRQ_ID,
	CDD_LOG_FREQ_ID,
};

struct google_cdd_log {
	struct task_log task[CDD_NR_CPUS][CDD_LOG_MAX_NUM];
	struct work_log work[CDD_NR_CPUS][CDD_LOG_MAX_NUM];
	struct cpuidle_log cpuidle[CDD_NR_CPUS][CDD_LOG_MAX_NUM];
	struct suspend_log suspend[CDD_LOG_MAX_NUM * 4];
	struct irq_log irq[CDD_NR_CPUS][CDD_LOG_MAX_NUM * 4];
	struct freq_log freq[CDD_DOMAIN_NUM];
};

/* This buffer is intentionally large to accommodate logs from S/R callback diagnostics */
static_assert(ARRAY_SIZE((((struct google_cdd_log *)0)->suspend)) >= SZ_4K,
	      "suspend log buffer must be at least 4096 units");

struct google_cdd_log_misc {
	atomic_t task_log_idx[CDD_NR_CPUS];
	atomic_t work_log_idx[CDD_NR_CPUS];
	atomic_t cpuidle_log_idx[CDD_NR_CPUS];
	atomic_t suspend_log_idx;
	atomic_t irq_log_idx[CDD_NR_CPUS];
	atomic_t freq_log_idx;
};

void *google_cdd_suspend_diag_get_info(void);
#endif
