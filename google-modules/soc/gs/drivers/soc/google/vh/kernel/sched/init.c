// SPDX-License-Identifier: GPL-2.0-only
/* init.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2020 Google LLC
 */

#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <linux/cpufreq.h>
#include <linux/futex.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <trace/hooks/binder.h>
#include "drivers/android/binder/rust_binder.h"
#include <trace/hooks/rust_binder.h>
#include <trace/hooks/cgroup.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/suspend.h>
#include <trace/hooks/topology.h>
#include <trace/hooks/cpufreq.h>
#include <trace/hooks/futex.h>
#include <perf/core/gs_domain_idle.h>
#include <soc/google/cal-if.h>

#include "pixel_em.h"
#include "sched_priv.h"

extern struct cpufreq_governor sched_pixel_gov;
extern bool wait_for_init;

int pixel_cpu_num;
int pixel_cluster_num;
int *pixel_cluster_start_cpu;
int *pixel_cluster_cpu_num;
int *pixel_cpu_to_cluster;
int *pixel_cluster_enabled;
unsigned int *pixel_cpd_exit_latency;
bool pixel_cpu_init = false;

EXPORT_SYMBOL_GPL(pixel_cpu_num);
EXPORT_SYMBOL_GPL(pixel_cluster_num);
EXPORT_SYMBOL_GPL(pixel_cluster_start_cpu);
EXPORT_SYMBOL_GPL(pixel_cpu_init);

#define REGISTER_TRACE_VH(__func, __callback) \
	ret = register_trace_android_vh_##__func(__callback, NULL); \
	if (ret) \
		pr_err("VH registration failed "#__func"\n");

#define REGISTER_TRACE_RVH(__func, __callback) \
	ret = register_trace_android_rvh_##__func(__callback, NULL); \
	if (ret) \
		pr_err("RVH registration failed "#__func"\n");

DEFINE_STATIC_KEY_FALSE(enqueue_dequeue_ready);

#if IS_ENABLED(CONFIG_VH_PRIO_INHERITANCE)
/*
 * @tsk: Remote task we want to access its info
 * @saved_nice: Pointer to save the old nice value if we inherited a new one.
 * @prio_inherited: Returns whether we performed prio inheritance or not
 *
 * This function helps promote current prio to that of @tsk.
 *
 * We only do such for CFS tasks. Used to handle priority inversion when
 * holding mmap_sem in GKI.
 *
 * Returns true when inheritance was performed and saved_nice was updated.
 * False if no inheritance was necessary.
 */
static void vh_prio_inheritance(void *data, struct task_struct *tsk,
				int *saved_nice, bool *prio_inherited)
{
	int current_nice = task_nice(current);
	int target_nice;

	*prio_inherited = false;

	if (tsk == current)
		return;

	if (dl_task(current) || rt_task(current))
		return;

	if (dl_task(tsk) || rt_task(tsk))
		target_nice = 0;
	else
		target_nice = task_nice(tsk);

	/* Only promote, don't demote */
	if (current_nice <= target_nice)
		return;

	*saved_nice = current_nice;
	set_user_nice(current, target_nice);

	*prio_inherited = true;
}

static void vh_prio_restore(void *data, int nice)
{
	set_user_nice(current, nice);
}
#endif

static enum hrtimer_restart ptick(struct hrtimer *timer)
{
	struct rq *rq = cpu_rq(smp_processor_id());
	struct vendor_rq_struct *vrq = get_vendor_rq_struct(rq);
	struct rq_flags rf;

	/* The timer must be pinned to this_cpu, hotplug can mess it up though.*/
	if (vrq->ptick_timer != timer)
		return HRTIMER_NORESTART;

	rq_lock(rq, &rf);
	/* Update stats first */
	update_rq_clock(rq);
	rq->donor->sched_class->task_tick(rq, rq->donor, 1);
	vh_scheduler_tick_pixel_mod_locked(rq);

	/* Then check if we need to push any tasks */
	if (fair_policy(rq->donor->policy))
		check_pushable_task(rq->donor, rq);
	rq_unlock(rq, &rf);

	/* TODO: maybe we better off raise a softirq and do the work there */
	vh_scheduler_tick_pixel_mod_nolock(rq);

	hrtimer_forward_now(timer, ns_to_ktime(PTICK_PERIOD_NS));

	return HRTIMER_RESTART;
}

static void ptick_init(struct rq *rq)
{
	struct vendor_rq_struct *vrq = get_vendor_rq_struct(rq);

	vrq->ptick_timer = kcalloc(1, sizeof(struct hrtimer), GFP_ATOMIC);
	if (WARN_ON(!vrq->ptick_timer))
		return;

	hrtimer_init(vrq->ptick_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_HARD);
	vrq->ptick_timer->function = ptick;
}

static void init_vendor_rq(void)
{
	int i;
	struct vendor_rq_struct *vrq;

	for (i = 0; i < pixel_cpu_num; i++) {
		vrq = get_vendor_rq_struct(cpu_rq(i));
		raw_spin_lock_init(&vrq->lock);
		vrq->util_removed = 0;
		vrq->iowait_boost = 0;
		atomic_set(&vrq->num_adpf_tasks, 0);
		ptick_init(cpu_rq(i));
		plist_head_init(&vrq->pushable_tasks);
	}
}

inline void _init_vendor_task_data(struct task_struct *t)
{
	struct vendor_task_struct *v_tsk;

	init_vendor_task_struct_h(t);
	v_tsk = get_vendor_task_struct(t);
	init_vendor_task_struct(v_tsk);
	v_tsk->orig_prio = t->static_prio;
	v_tsk->orig_policy = t->policy;
	v_tsk->prev_sum_exec_runtime = t->se.prev_sum_exec_runtime;
}

static int init_vendor_task_data(void *data)
{
	struct task_struct *p, *t;
	int cpu;

	for_each_possible_cpu(cpu) {
		_init_vendor_task_data(cpu_rq(cpu)->idle);
	}

	rcu_read_lock();
	for_each_process_thread(p, t) {
		_init_vendor_task_data(t);
	}
	rcu_read_unlock();

	/* our module can start handling the initialization now */
	wait_for_init = false;

	return 0;
}

static int init_pixel_cpu(void)
{
	int i, j = 0;
	unsigned long cur_capacity = 0;

	pixel_cpu_num = cpumask_weight(cpu_possible_mask);

	if (!pixel_cpu_num)
		return -EPROBE_DEFER;

	for_each_possible_cpu(i) {
		if (arch_scale_cpu_capacity(i) > cur_capacity) {
			cur_capacity = arch_scale_cpu_capacity(i);
			pixel_cluster_num++;
		}
	}

	pixel_cpu_to_cluster  = kcalloc(pixel_cpu_num, sizeof(int), GFP_KERNEL);
	if (!pixel_cpu_to_cluster)
		return -ENOMEM;

	pixel_cluster_start_cpu = kcalloc(pixel_cluster_num, sizeof(int), GFP_KERNEL);
	if (!pixel_cluster_start_cpu)
		goto out_no_pixel_cluster_start_cpu;

	pixel_cluster_cpu_num = kcalloc(pixel_cluster_num, sizeof(int), GFP_KERNEL);
	if (!pixel_cluster_cpu_num)
		goto out_no_pixel_cluster_cpu_num;

	pixel_cluster_enabled = kmalloc_array(pixel_cluster_num, sizeof(int), GFP_KERNEL);
	if (!pixel_cluster_enabled)
		goto out_no_pixel_cluster_enabled;

	pixel_cpd_exit_latency = kcalloc(pixel_cluster_num, sizeof(int), GFP_KERNEL);
	if (!pixel_cpd_exit_latency)
		goto out_no_pixel_cpd_exit_latency;

	cur_capacity = 0;
	for_each_possible_cpu(i) {
		if (arch_scale_cpu_capacity(i) > cur_capacity) {
			pixel_cluster_start_cpu[j++] = i;
			cur_capacity = arch_scale_cpu_capacity(i);
		}

		pixel_cluster_cpu_num[j - 1]++;
		pixel_cpu_to_cluster[i] = j - 1;
	}

	for (i = 0; i < pixel_cluster_num; i++) {
		pixel_cluster_enabled[i] = 1;
		pixel_cpd_exit_latency[i] = UINT_MAX - pixel_cluster_num + i;
	}

	pixel_cpu_init = true;

#if IS_ENABLED(CONFIG_CAL_IF) || IS_ENABLED(CONFIG_GS_DOMAIN_IDLE)
	register_set_cluster_enabled_cb(set_cluster_enabled_cb);
#endif

	register_arch_set_freq_scale_cb(vh_arch_set_freq_scale_pixel_mod);
	register_update_thermal_freq_cap_cb(update_thermal_freq_cap);
#if IS_ENABLED(CONFIG_PIXEL_EM_FREQUENCY_SCALING)
	register_reset_scaling_freq_cb(reset_scaling_freq);
#endif

	return 0;

out_no_pixel_cpd_exit_latency:
	kfree(pixel_cluster_enabled);
out_no_pixel_cluster_enabled:
	kfree(pixel_cluster_cpu_num);
out_no_pixel_cluster_cpu_num:
	kfree(pixel_cluster_start_cpu);
out_no_pixel_cluster_start_cpu:
	kfree(pixel_cpu_to_cluster);

	return -ENOMEM;
}

static void rvh_get_nohz_timer_target_pixel_mod(void *data, int *cpu, bool *done)
{
	if (*cpu >= pixel_cluster_start_cpu[pixel_cluster_num - 1]) {
		/*
		 * When on the big cluster, target the last CPU of the little
		 * cluster (cluster 0).
		 */
		*cpu = pixel_cluster_start_cpu[0] + pixel_cluster_cpu_num[0] - 1;
		if (!cpumask_test_cpu(*cpu, cpu_online_mask))
			*cpu = cpumask_first(cpu_online_mask);
		*done = true;
	}
}

static void init_sched_params(void)
{
	vh_sched_max_load_balance_interval = max_load_balance_interval;
	vh_sched_min_granularity_ns = sysctl_sched_base_slice;
}

static int vh_sched_pm_notify(struct notifier_block *nb,
			       unsigned long mode, void *_unused)
{
	switch (mode) {
	case PM_SUSPEND_PREPARE:
		in_suspend_resume = true;
		break;

	case PM_POST_SUSPEND:
		in_suspend_resume = false;
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block vh_sched_pm_nb = {
	.notifier_call = vh_sched_pm_notify,
};

static int vh_sched_init(void)
{
	int ret;

	init_sched_params();

	ret = init_pixel_cpu();
	if (ret) {
		pr_err("[%s] pixel cpu init failed\n", __func__);
		return ret;
	}

	ret = pmu_poll_init();
	if (ret) {
		pr_err("[%s] pmu poll init failed\n", __func__);
		return ret;
	}

#if IS_ENABLED(CONFIG_UCLAMP_STATS)
	init_uclamp_stats();
#endif

	update_auto_fits_capacity();

	ret = create_procfs_node();
	if (ret) {
		pr_err("[%s] creating procfs nodes failed\n", __func__);
		return ret;
	}

	ret = vh_sched_netlink_init();
	if (ret) {
		pr_err("[%s] registering generic netlink failed\n", __func__);
		return ret;
	}

	init_vendor_rq();

	init_vendor_group_data();

	init_pixel_locks();

	register_pm_notifier(&vh_sched_pm_nb);

	/*
	 * We must register this first but it won't do anything until we
	 * initialize vendor task data for all currently running tasks.
	 *
	 * We can't call this directly in init_vendor_task_data() as it'll hold
	 * a mutex and the context in stop_machine is atomic.
	 *
	 * init_vendor_task_data() should set a flag to enable this function to
	 * work as soon as we have initialized the task data.
	 */

	REGISTER_TRACE_VH(dup_task_struct, vh_dup_task_struct_pixel_mod);
	REGISTER_TRACE_VH(exit_check, vh_exit_check_pixel_mod);
	REGISTER_TRACE_VH(lock_task_fork, vh_lock_task_fork_pixel_mod);

	/*
	 * Heavy handed, but necessary. We want to initialize our private data
	 * structure for every task running in the system now. And register
	 * a hook to ensure we initialize them for future ones via
	 * dup_task_struct() vh.
	 *
	 * stop_machine provides atomic way to guarantee this without races.
	 */
	ret = stop_machine(init_vendor_task_data, NULL, cpumask_of(raw_smp_processor_id()));
	if (ret)
		pr_err("[%s] stop_machine failed\n", __func__);

	REGISTER_TRACE_RVH(enqueue_task, rvh_enqueue_task_pixel_mod);
	REGISTER_TRACE_RVH(dequeue_task, rvh_dequeue_task_pixel_mod);
	REGISTER_TRACE_RVH(after_enqueue_task, rvh_after_enqueue_task_pixel_mod);
	REGISTER_TRACE_RVH(after_dequeue_task, rvh_after_dequeue_task_pixel_mod);
	REGISTER_TRACE_RVH(can_migrate_task, rvh_can_migrate_task_pixel_mod);
	REGISTER_TRACE_RVH(enqueue_task_fair, rvh_enqueue_task_fair_pixel_mod);
	REGISTER_TRACE_RVH(dequeue_task_fair, rvh_dequeue_task_fair_pixel_mod);

	if (ret)
		return ret;

	static_branch_enable(&enqueue_dequeue_ready);

#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
	REGISTER_TRACE_RVH(attach_entity_load_avg, rvh_attach_entity_load_avg_pixel_mod);
	REGISTER_TRACE_RVH(detach_entity_load_avg, rvh_detach_entity_load_avg_pixel_mod);
	REGISTER_TRACE_RVH(update_load_avg, rvh_update_load_avg_pixel_mod);
	REGISTER_TRACE_RVH(remove_entity_load_avg, rvh_remove_entity_load_avg_pixel_mod);
	REGISTER_TRACE_RVH(update_blocked_fair, rvh_update_blocked_fair_pixel_mod);
#endif

	REGISTER_TRACE_RVH(rtmutex_prepare_setprio, rvh_rtmutex_prepare_setprio_pixel_mod);
	REGISTER_TRACE_RVH(update_rt_rq_load_avg, rvh_update_rt_rq_load_avg_pixel_mod);
	REGISTER_TRACE_RVH(set_task_cpu, rvh_set_task_cpu_pixel_mod);
	REGISTER_TRACE_RVH(set_iowait, rvh_set_iowait_pixel_mod);
	REGISTER_TRACE_RVH(select_task_rq_rt, rvh_select_task_rq_rt_pixel_mod);
	REGISTER_TRACE_VH(scheduler_tick, vh_scheduler_tick_pixel_mod);

	ret = register_trace_sched_switch(vh_sched_switch_pixel_mod, NULL);
	if (ret)
		return ret;

	REGISTER_TRACE_RVH(cpu_overutilized, rvh_cpu_overutilized_pixel_mod);
	REGISTER_TRACE_RVH(uclamp_eff_get, rvh_uclamp_eff_get_pixel_mod);


	REGISTER_TRACE_RVH(util_est_update, rvh_util_est_update_pixel_mod);
#if !IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
	REGISTER_TRACE_RVH(cpu_cgroup_online, rvh_cpu_cgroup_online_pixel_mod);
#endif

	REGISTER_TRACE_RVH(sched_newidle_balance, sched_newidle_balance_pixel_mod);
	REGISTER_TRACE_RVH(post_init_entity_util_avg, rvh_post_init_entity_util_avg_pixel_mod);
	REGISTER_TRACE_RVH(check_preempt_wakeup_fair, rvh_check_preempt_wakeup_fair_pixel_mod);
	REGISTER_TRACE_RVH(select_task_rq_fair, rvh_select_task_rq_fair_pixel_mod);
	REGISTER_TRACE_RVH(set_cpus_allowed_by_task, rvh_set_cpus_allowed_by_task);

#if IS_ENABLED(CONFIG_VH_SCHED) && IS_ENABLED(CONFIG_PIXEL_EM)
	REGISTER_TRACE_VH(arch_set_freq_scale, vh_arch_set_freq_scale_pixel_mod);
#endif

	REGISTER_TRACE_VH(uclamp_validate, vh_sched_uclamp_validate_pixel_mod);
	REGISTER_TRACE_VH(setscheduler_uclamp, vh_sched_setscheduler_uclamp_pixel_mod);

	ret = cpufreq_register_governor(&sched_pixel_gov);
	if (ret)
		pr_err("[%s] cpufreq governor register failed\n", __func__);

	REGISTER_TRACE_VH(dump_throttled_rt_tasks, vh_dump_throttled_rt_tasks_mod);

#if IS_ENABLED(CONFIG_RVH_SCHED_LIB)
	REGISTER_TRACE_RVH(sched_setaffinity, rvh_sched_setaffinity_mod);
	REGISTER_TRACE_RVH(set_cpus_allowed_ptr, rvh_set_cpus_allowed_ptr_mod);
#endif /* IS_ENABLED(CONFIG_RVH_SCHED_LIB) */

	REGISTER_TRACE_VH(binder_set_priority, vh_binder_set_priority_pixel_mod);
	REGISTER_TRACE_VH(binder_looper_state_registered, vh_binder_looper_state_registered_pixel_mod);
	REGISTER_TRACE_VH(binder_looper_exited, vh_binder_looper_exited_pixel_mod);
	REGISTER_TRACE_VH(binder_restore_priority, vh_binder_restore_priority_pixel_mod);
	REGISTER_TRACE_VH(binder_proc_transaction_finish, vh_binder_proc_transaction_finish);
	REGISTER_TRACE_VH(rust_binder_set_priority, vh_rust_binder_set_priority_pixel_mod);
	REGISTER_TRACE_VH(rust_binder_restore_priority, vh_rust_binder_restore_priority_pixel_mod);
	REGISTER_TRACE_VH(rust_binder_looper_entry, vh_rust_binder_looper_entry_mod);

	REGISTER_TRACE_VH(use_amu_fie, android_vh_use_amu_fie_pixel_mod);
	REGISTER_TRACE_RVH(set_user_nice_locked, rvh_set_user_nice_locked_pixel_mod);
	REGISTER_TRACE_RVH(setscheduler, rvh_setscheduler_prio_pixel_mod);
	REGISTER_TRACE_RVH(setscheduler_prio, rvh_setscheduler_prio_pixel_mod);
	REGISTER_TRACE_RVH(find_lowest_rq, rvh_find_lowest_rq_pixel_mod);

#if IS_ENABLED(CONFIG_VH_PRIO_INHERITANCE)
	REGISTER_TRACE_VH(prio_inheritance, vh_prio_inheritance);
	REGISTER_TRACE_VH(prio_restore, vh_prio_restore);
#endif

	REGISTER_TRACE_RVH(util_fits_cpu, rvh_util_fits_cpu_pixel_mod);
	REGISTER_TRACE_RVH(set_task_comm, rvh_set_task_comm_pixel_mod);
	REGISTER_TRACE_VH(resume_end, vh_sched_resume_end);
	REGISTER_TRACE_RVH(try_to_wake_up_success, rvh_try_to_wake_up_success_pixel_mod);
	REGISTER_TRACE_RVH(build_perf_domains, android_rvh_build_perf_domains_pixel_mod);
	REGISTER_TRACE_RVH(task_fits_cpu, rvh_task_fits_cpu_pixel_mod);

	REGISTER_TRACE_RVH(update_load_sum, rvh_update_load_sum_pixel_mod);

	REGISTER_TRACE_RVH(get_nohz_timer_target, rvh_get_nohz_timer_target_pixel_mod);

	ret = register_trace_android_vh_alter_futex_plist_add(
			vh_alter_futex_plist_add_pixel_mod, NULL);
	if (ret)
		return ret;

	// Disable TTWU_QUEUE.
	sysctl_sched_features &= ~(1UL << __SCHED_FEAT_TTWU_QUEUE);
	static_key_disable(&sched_feat_keys[__SCHED_FEAT_TTWU_QUEUE]);

	// Enable NEXT_BUDDY
	sysctl_sched_features |= (1UL << __SCHED_FEAT_NEXT_BUDDY);
	static_key_enable(&sched_feat_keys[__SCHED_FEAT_NEXT_BUDDY]);

	pr_info("vh_sched driver initialized! :D\n");
	return 0;
}

module_init(vh_sched_init);
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: pixel_em");
