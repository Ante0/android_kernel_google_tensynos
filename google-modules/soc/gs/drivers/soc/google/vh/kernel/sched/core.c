// SPDX-License-Identifier: GPL-2.0-only
/* core.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2021 Google LLC
 */

#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>

#if IS_ENABLED(CONFIG_SOC_GS101) || IS_ENABLED(CONFIG_SOC_GS201) || IS_ENABLED(CONFIG_SOC_ZUMA)
#include <performance/gs_perf_mon/gs_perf_mon.h>
#else
#include <perf/core/gs_perf_mon.h>
#endif

#include "sched_priv.h"

#include "governor_memlat.h"

extern inline void update_misfit_status(struct task_struct *p, struct rq *rq);

struct vendor_group_list vendor_group_list[VG_MAX];

/*
 * Ignore uclamp_min for CFS tasks if
 *
 *	runtime >= sysctl_sched_uclamp_min_filter_us
 */
unsigned int sysctl_sched_uclamp_min_filter_us = 1000;

/*
 * Ignore uclamp_max for CFS tasks if
 *
 *	runtime < sched_slice() / divider
 */
unsigned int sysctl_sched_uclamp_max_filter_divider = 4;

/*
 * Ignore uclamp_min for RT tasks if
 *
 *	task_util(p) < sysctl_sched_uclamp_min_filter_rt
 */
unsigned int sysctl_sched_uclamp_min_filter_rt = 50;

/*
 * Ignore uclamp_max for RT tasks if
 *
 *	task_util(p) < sysctl_sched_uclamp_max_filter_rt
 */
unsigned int sysctl_sched_uclamp_max_filter_rt = 100;

/*
 * Enable and disable uclamp min/max filters at runtime
 */
DEFINE_STATIC_KEY_FALSE(uclamp_min_filter_enable);
DEFINE_STATIC_KEY_FALSE(uclamp_max_filter_enable);

DEFINE_STATIC_KEY_FALSE(tapered_dvfs_headroom_enable);
DEFINE_STATIC_KEY_FALSE(auto_dvfs_headroom_enable);
DEFINE_STATIC_KEY_FALSE(auto_migration_margins_enable);
DEFINE_STATIC_KEY_FALSE(response_time_ms_fix_enable);

DEFINE_STATIC_KEY_FALSE(skip_inefficient_opps_enable);
DEFINE_STATIC_KEY_FALSE(use_em_for_freq_mapping);

DEFINE_STATIC_KEY_FALSE(per_task_memory_aware_enable);

DEFINE_STATIC_KEY_FALSE(eas_fork_exec_enable);

DEFINE_STATIC_KEY_FALSE(update_freq_on_idle_enable);

DEFINE_STATIC_KEY_FALSE(enable_ptick);

DEFINE_STATIC_KEY_FALSE(dsulat_fast_switch_enable);
DEFINE_STATIC_KEY_FALSE(memlat_fast_switch_enable);

#define vi_set_adpf(vi, type, value) \
    do { \
        if (value) { \
            (vi)->adpf |= (1 << (type)); \
        } else { \
            (vi)->adpf &= ~(1 << (type)); \
        } \
    } while (0)

#define vi_set_prefer_idle(vi, type, value) \
    do { \
        if (value) { \
            (vi)->prefer_idle |= (1 << (type)); \
        } else { \
            (vi)->prefer_idle &= ~(1 << (type)); \
        } \
    } while (0)

#define vi_set_prefer_fit(vi, type, value) \
    do { \
        if (value) { \
            (vi)->prefer_fit |= (1 << (type)); \
        } else { \
            (vi)->prefer_fit &= ~(1 << (type)); \
        } \
    } while (0)

#define vi_set_prefer_high_cap(vi, type, value) \
    do { \
        if (value) { \
            (vi)->prefer_high_cap |= (1 << (type)); \
        } else { \
            (vi)->prefer_high_cap &= ~(1 << (type)); \
        } \
    } while (0)

#define vi_set_preempt_wakeup(vi, type, value) \
    do { \
        if (value) { \
            (vi)->preempt_wakeup |= (1 << (type)); \
        } else { \
            (vi)->preempt_wakeup &= ~(1 << (type)); \
        } \
    } while (0)

/*****************************************************************************/
/*                       New Code Section                                    */
/*****************************************************************************/
/*
 * This part of code is new for this kernel, which are mostly helper functions.
 */

/*****************************************************************************/
/*                       Modified Code Section                               */
/*****************************************************************************/
/*
 * This part of code is vendor hook functions, which modify or extend the original
 * functions.
 */
#if IS_ENABLED(CONFIG_UCLAMP_TASK)
static inline void task_tick_uclamp(struct rq *rq, struct task_struct *curr)
{
	bool can_ignore;
	bool is_ignored;
	bool reset_filters = false;

	if (!uclamp_is_used())
		return;

	lockdep_assert_rq_held(rq);

	/*
	 * Condition might have changed since we enqueued the task.
	 */

	can_ignore = uclamp_can_ignore_uclamp_max(rq, curr);
	is_ignored = uclamp_is_ignore_uclamp_max(curr);

	if (is_ignored && !can_ignore)
		reset_filters = true;

	can_ignore = uclamp_can_ignore_uclamp_min(rq, curr);
	is_ignored = uclamp_is_ignore_uclamp_min(curr);

	if (is_ignored && !can_ignore)
		reset_filters = true;

	if (reset_filters) {
		uclamp_reset_ignore_uclamp_min(curr);
		uclamp_reset_ignore_uclamp_max(curr);

		uclamp_rq_inc_id(rq, curr, UCLAMP_MIN);
		uclamp_rq_inc_id(rq, curr, UCLAMP_MAX);
	}

	/* Reset clamp idle holding when there is one RUNNABLE task */
	if (reset_filters && rq->uclamp_flags & UCLAMP_FLAG_IDLE)
		rq->uclamp_flags &= ~UCLAMP_FLAG_IDLE;
}
#else
static inline void task_tick_uclamp(struct rq *rq, struct task_struct *curr) {}
#endif

void vh_scheduler_tick_pixel_mod_locked(struct rq *rq)
{
	bool uclamp_st_updated = false;

	lockdep_assert_rq_held(rq);

	task_tick_uclamp(rq, rq->curr);
	__update_util_est_invariance(rq, rq->curr, true);
	uclamp_st_updated = update_auto_max_uclamp_st(rq->curr);

	if (uclamp_st_updated) {
		/* rq clock is already updated */
		rq->clock_update_flags = RQCF_UPDATED;
		/* freq may have been updated in entity tick, so use force update here */
		cpufreq_update_util(rq, SCHED_PIXEL_FORCE_UPDATE);
	}
}

void vh_scheduler_tick_pixel_mod_nolock(struct rq *rq)
{
	/* Check if an RT task needs to move to a better fitting CPU */
	check_migrate_rt_task(rq, rq->curr);

	/* Should be really done when capacity change */
	update_auto_fits_capacity();

	/*
	 * Update our performance counters for profiling per
	 * cpu performance data. Also checks if we need to wake
	 * up a helper thread to update memory frequencies.
	 */
	gs_perf_mon_tick_update_counters();
}

void vh_scheduler_tick_pixel_mod(void *data, struct rq *rq)
{
	struct rq_flags rf;

	/* Our ptick handles everything now, skip if enabled */
	if (static_key_enabled(&enable_ptick)) {
		/*
		 * Tickle the perf counters if there are no tasks running to
		 * help trigger any potential necessary update
		 */
		if (!rq->nr_running)
			gs_perf_mon_tick_update_counters();
		return;
	}

	rq_lock(rq, &rf);
	vh_scheduler_tick_pixel_mod_locked(rq);
	rq_unlock(rq, &rf);

	vh_scheduler_tick_pixel_mod_nolock(rq);
}

static void reset_task_pmu(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	raw_spin_lock(&vp->lock);
	memset(&vp->mp_stats.pmu_stats, 0, sizeof(struct pmu_stats_struct));
	raw_spin_unlock(&vp->lock);
}

static void finish_task_pmu(int cpu, struct task_struct *p, u64 cycle, u64 stall, u64 inst,
			    u64 mem_rd_inst, u64 mem_wr_inst)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	s64 cycle_delta, stall_delta, inst_delta, mem_rd_inst_delta, mem_wr_inst_delta;
	u64 last_cycle = vp->mp_stats.pmu_stats.last_cycle[pixel_cpu_to_cluster[cpu]];
	u64 last_stall = vp->mp_stats.pmu_stats.last_stall[pixel_cpu_to_cluster[cpu]];
	u64 last_inst = vp->mp_stats.pmu_stats.last_inst[pixel_cpu_to_cluster[cpu]];
	u64 last_mem_rd_inst = vp->mp_stats.pmu_stats.last_mem_rd_inst;
	u64 last_mem_wr_inst = vp->mp_stats.pmu_stats.last_mem_wr_inst;

	/* not prepared yet */
	if (last_cycle == 0)
		return;

	/* cycle should not be the same, but stall, inst, and mem_xx_inst could be. */
	if (unlikely(cycle <= last_cycle || stall < last_stall || inst < last_inst ||
		     mem_rd_inst < last_mem_rd_inst || mem_wr_inst < last_mem_wr_inst))
		return;

	cycle_delta = cycle - last_cycle;
	stall_delta = stall - last_stall;
	inst_delta = inst - last_inst;
	mem_rd_inst_delta = mem_rd_inst - last_mem_rd_inst;
	mem_wr_inst_delta = mem_wr_inst - last_mem_wr_inst;

	/* possibly overflow, reset the stats */
	if (unlikely(cycle_delta < 0)) {
		reset_task_pmu(p);
		return;
	}

	/* cycle delta should always >= other delta */
	if (unlikely(cycle_delta < stall_delta || cycle_delta < inst_delta))
		return;

	raw_spin_lock(&vp->lock);
	vp->mp_stats.pmu_stats.cycle[pixel_cpu_to_cluster[cpu]] += cycle_delta;
	vp->mp_stats.pmu_stats.stall[pixel_cpu_to_cluster[cpu]] += stall_delta;
	vp->mp_stats.pmu_stats.inst[pixel_cpu_to_cluster[cpu]] += inst_delta;
	vp->mp_stats.pmu_stats.total_mem_rd_inst += mem_rd_inst_delta;
	vp->mp_stats.pmu_stats.total_mem_wr_inst += mem_wr_inst_delta;
	vp->mp_stats.pmu_stats.total_inst += inst_delta;
	raw_spin_unlock(&vp->lock);
	trace_per_task_pmu_stats(p, cpu, cycle_delta, stall_delta, inst_delta,
				 mem_rd_inst_delta + mem_wr_inst_delta);
}

static void prepare_task_pmu(int cpu, struct task_struct *p, u64 cycle, u64 stall, u64 inst,
			     u64 mem_rd_inst, u64 mem_wr_inst)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	raw_spin_lock(&vp->lock);
	vp->mp_stats.pmu_stats.last_cycle[pixel_cpu_to_cluster[cpu]] = cycle;
	vp->mp_stats.pmu_stats.last_stall[pixel_cpu_to_cluster[cpu]] = stall;
	vp->mp_stats.pmu_stats.last_inst[pixel_cpu_to_cluster[cpu]] = inst;
	vp->mp_stats.pmu_stats.last_mem_rd_inst = mem_rd_inst;
	vp->mp_stats.pmu_stats.last_mem_wr_inst = mem_wr_inst;
	raw_spin_unlock(&vp->lock);
}

static void update_task_pmu(int cpu, struct task_struct *prev, struct task_struct *next)
{
	u64 cycle, stall, inst, mem_rd_inst, mem_wr_inst;

	if (read_perf_event_local(cpu, CORE_INST_INDEX, &inst) ||
	    read_perf_event_local(cpu, CORE_STALL_INDEX, &stall) ||
	    read_perf_event_local(cpu, CORE_CYCLE_INDEX, &cycle) ||
	    read_perf_event_local(cpu, CORE_MEM_RD_INST_INDEX, &mem_rd_inst) ||
	    read_perf_event_local(cpu, CORE_MEM_WR_INST_INDEX, &mem_wr_inst))
		return;

	if (!is_idle_task(prev) && !(prev->flags & PF_EXITING))
		finish_task_pmu(cpu, prev, cycle, stall, inst, mem_rd_inst, mem_wr_inst);
	if (!is_idle_task(next) && !(next->flags & PF_EXITING))
		prepare_task_pmu(cpu, next, cycle, stall, inst, mem_rd_inst, mem_wr_inst);
}

static inline void ptick_clear(struct rq *rq)
{
	struct vendor_rq_struct *vrq = get_vendor_rq_struct(rq);

	if (!vrq->ptick_timer)
		return;

	if (hrtimer_active(vrq->ptick_timer))
		hrtimer_cancel(vrq->ptick_timer);
}

static inline void ptick_start(struct rq *rq)
{
	struct vendor_rq_struct *vrq = get_vendor_rq_struct(rq);

	if (!vrq->ptick_timer)
		return;

	hrtimer_start(vrq->ptick_timer, ns_to_ktime(PTICK_PERIOD_NS), HRTIMER_MODE_REL_PINNED_HARD);
}

void vh_sched_switch_pixel_mod(void *data, bool preempt, struct task_struct *prev,
			       struct task_struct *next, unsigned int prev_state)
{
	bool force_cpufreq_update = false;
	struct rq *rq = task_rq(prev);

	if (static_branch_likely(&update_freq_on_idle_enable) && !in_suspend_resume)
		force_cpufreq_update = is_idle_task(next) || is_idle_task(prev);

	if (task_is_running(prev)) {
		__update_util_est_invariance(rq, prev, rq->nr_running > 1);
		force_cpufreq_update |= update_auto_max_uclamp_st(prev);
	}

	force_cpufreq_update |= update_auto_max_uclamp_st(next);

	if (force_cpufreq_update) {
		/* freq may have been updated in set/put task, so use force update here */
		cpufreq_update_util(rq, SCHED_PIXEL_FORCE_UPDATE);
	}

	if (static_key_enabled(&per_task_memory_aware_enable))
		update_task_pmu(cpu_of(rq), prev, next);

	if (is_idle_task(next))
		ptick_clear(rq);

	if (static_key_enabled(&enable_ptick)) {
		if (fair_policy(prev->policy))
			fair_add_pushable_task(rq, prev);
		if (fair_policy(next->policy)) {
			fair_remove_pushable_task(rq, next);
			fair_queue_pushable_tasks(rq);
		}

		if (is_idle_task(prev))
			ptick_start(rq);
	}

	send_trace_sched_group_tracker(next, true);
}

void rvh_enqueue_task_pixel_mod(void *data, struct rq *rq, struct task_struct *p, int flags)
{
	struct vendor_task_struct_h *vph = get_vendor_task_struct_h(p);
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	unsigned long irqflags;
	int group;

	if (!static_branch_unlikely(&enqueue_dequeue_ready))
		return;

	if (static_branch_likely(&auto_dvfs_headroom_enable)) {
		if (!get_rampup_multiplier(p)) {
			p->se.avg.util_est = 0;
		}
	}

	raw_spin_lock_irqsave(&vp->lock, irqflags);
	if (vp->queued_to_list == LIST_NOT_QUEUED) {
		group = get_vendor_group(p);
		add_to_vendor_group_list(&vph->node, group);
		vp->queued_to_list = LIST_QUEUED;
	}
	raw_spin_unlock_irqrestore(&vp->lock, irqflags);

	/*
	 * uclamp filter for RT tasks. CFS tasks are handled in
	 * enqueue_task_fair() where we need cfs_rqs to be updated before we
	 * can read sched_slice()
	 */
	if (uclamp_is_used() && rt_task(p) && p->sched_class->uclamp_enabled)
		apply_uclamp_filters(rq, p, flags);
}

void rvh_dequeue_task_pixel_mod(void *data, struct rq *rq, struct task_struct *p, int flags)
{
	struct vendor_task_struct_h *vph = get_vendor_task_struct_h(p);
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	unsigned long irqflags;
	int group;

	if (!static_branch_unlikely(&enqueue_dequeue_ready))
		return;

#if IS_ENABLED(CONFIG_UCLAMP_STATS)
	if (rq->nr_running == 1)
		update_uclamp_stats(rq->cpu, rq_clock(rq));
#endif

	raw_spin_lock_irqsave(&vp->lock, irqflags);
	if (vp->queued_to_list == LIST_QUEUED) {
		group = get_vendor_group(p);
		remove_from_vendor_group_list(&vph->node, group);
		vp->queued_to_list = LIST_NOT_QUEUED;
	}
	raw_spin_unlock_irqrestore(&vp->lock, irqflags);

	/*
	 * Reset uclamp filter flags unconditionally for both RT and CFS.
	 */
	if (uclamp_is_used()) {
		uclamp_reset_ignore_uclamp_max(p);
		uclamp_reset_ignore_uclamp_min(p);
	}
}

void rvh_after_enqueue_task_pixel_mod(void *data, struct rq *rq, struct task_struct *p,
				      int flags)
{
	if (p->prio < MAX_RT_PRIO)
		return;

	update_misfit_status(p, rq);
}

void rvh_after_dequeue_task_pixel_mod(void *data, struct rq *rq, struct task_struct *p,
				      int flags, bool *results)
{
	if (!rq->dl.dl_nr_running) {
		/*
		 * Workaround upstream bug due to dl_server causing bw to
		 * always be positive even if there are no dl tasks.
		 */
		rq->dl.running_bw = 0;
	}
}

void rvh_set_cpus_allowed_by_task(void *data, const struct cpumask *cpu_valid_mask,
	const struct cpumask *new_mask, struct task_struct *p, unsigned int *dest_cpu)
{
	cpumask_t valid_mask;
	int best_energy_cpu = -1;

	cpumask_and(&valid_mask, cpu_valid_mask, new_mask);

	/* find a cpu again for the running/runnable/waking tasks
	 * if their current cpu are not allowed
	 */
	if ((p->on_cpu || p->__state == TASK_WAKING || task_on_rq_queued(p)) &&
		!cpumask_test_cpu(task_cpu(p), new_mask)) {
		best_energy_cpu = find_energy_efficient_cpu(p, task_cpu(p), &valid_mask);

		if (best_energy_cpu != -1)
			*dest_cpu = best_energy_cpu;
	}

	trace_set_cpus_allowed_by_task(p, &valid_mask, *dest_cpu);

	return;
}

static void set_adpf_inheritance(struct task_struct *p, unsigned int type, int val)
{
	struct vendor_inheritance_struct *vi = get_vendor_inheritance_struct(p);
	bool old_adpf = get_adpf(p, true);

	lockdep_assert_held(&p->pi_lock);

	vi_set_adpf(vi, type, val);

	update_adpf_counter(p, old_adpf);
}

static void set_performance_inheritance(struct task_struct *p, struct task_struct *pi_task,
	unsigned int type)
{
	struct vendor_inheritance_struct *vi = get_vendor_inheritance_struct(p);
	struct rq_flags rf;
	struct rq *rq;

	/* RT mutex in GKI path already held pi_lock so we only hold rq lock in vendor hook */
	if (type == VI_RTMUTEX)
		rq = __task_rq_lock(p, &rf);
	else
		rq = task_rq_lock(p, &rf);

	lockdep_assert_held(&p->pi_lock);

	if (pi_task) {
		unsigned long p_util = task_util(p);
		unsigned long p_uclamp_min = uclamp_eff_value_pixel_mod(p, UCLAMP_MIN);
		unsigned long p_uclamp_max = uclamp_eff_value_pixel_mod(p, UCLAMP_MAX);
		unsigned long pi_util = task_util(pi_task);
		unsigned long pi_uclamp_min = uclamp_eff_value_pixel_mod(pi_task, UCLAMP_MIN);
		unsigned long pi_uclamp_max = uclamp_eff_value_pixel_mod(pi_task, UCLAMP_MAX);

		/*
		 * Take task's util into consideration first to do full
		 * performance inheritance.
		 *
		 * If pi_uclamp_min = 612 but pi_util is 812, then setting
		 * p_uclamp_min to 612 is not enough as the task will still run
		 * slower.
		 *
		 * Or if pi_uclamp_min is 0 but pi_util is 800 while p_util is
		 * 100, then pi_task could wait for longer to acquire the lock
		 * because the performance of p is too low.
		 */
		p_uclamp_min = clamp(p_util, p_uclamp_min, p_uclamp_max);
		pi_uclamp_min = clamp(pi_util, pi_uclamp_min, pi_uclamp_max);

		/* Inherit unclamp_min/max if they're inverted */

		if (p_uclamp_min < pi_uclamp_min)
			vi->uclamp[type][UCLAMP_MIN] = pi_uclamp_min;

		if (p_uclamp_max < pi_uclamp_max || pi_uclamp_min > p_uclamp_max)
			vi->uclamp[type][UCLAMP_MAX] = pi_uclamp_max;

		if (!!get_adpf(pi_task, true))
			set_adpf_inheritance(p, type, 1);

		if (!!get_prefer_idle(pi_task))
			vi_set_prefer_idle(vi, type, 1);

		if (!!get_prefer_fit(pi_task))
			vi_set_prefer_fit(vi, type, 1);

		if (!!get_preempt_wakeup(pi_task))
			vi_set_preempt_wakeup(vi, type, 1);

		if (get_prefer_high_cap(pi_task) ||
			task_cpu(pi_task) >= pixel_cluster_start_cpu[1])
			vi_set_prefer_high_cap(vi, type, 1);
	} else {
		vi->uclamp[type][UCLAMP_MIN] = uclamp_none(UCLAMP_MIN);
		vi->uclamp[type][UCLAMP_MAX] = uclamp_none(UCLAMP_MAX);

		set_adpf_inheritance(p, type, 0);

		vi_set_prefer_idle(vi, type, 0);

		vi_set_prefer_fit(vi, type, 0);

		vi_set_preempt_wakeup(vi, type, 0);

		vi_set_prefer_high_cap(vi, type, 0);
	}

	if (type == VI_RTMUTEX)
		__task_rq_unlock(rq, &rf);
	else
		task_rq_unlock(rq, p, &rf);
}

void vh_binder_set_priority_pixel_mod(void *data, struct binder_transaction *t,
	struct task_struct *to_task)
{
	struct task_struct *from_task = NULL;

	if (t->flags & TF_ONE_WAY)
		return;

	spin_lock(&t->lock);
	if (t->from)
		from_task = get_task_struct(t->from->task);
	spin_unlock(&t->lock);

	set_performance_inheritance(to_task, from_task, VI_BINDER);
	if (from_task)
		put_task_struct(from_task);
}

void vh_binder_restore_priority_pixel_mod(void *data, struct binder_transaction *t,
	struct task_struct *p)
{
	set_performance_inheritance(p, NULL, VI_BINDER);
}

void vh_binder_proc_transaction_finish(void *data, struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task,
		bool pending_async, bool sync)
{
	if (binder_th_task) {
		struct vendor_task_struct *vp = get_vendor_task_struct(binder_th_task);

		if (vp->is_binder_task && proc->default_priority.prio < NICE_TO_PRIO(0) &&
		    proc->default_priority.prio >= NICE_TO_PRIO(-20))
			proc->default_priority.prio = NICE_TO_PRIO(0);
	}
}

void vh_rust_binder_set_priority_pixel_mod(void *data,
					   rust_binder_transaction t,
					   struct task_struct *to_task)
{
	rust_binder_thread from_thread = rust_binder_transaction_from_thread(t);
	struct task_struct *from_task;

	if (rust_binder_transaction_flags(t) & TF_ONE_WAY)
		return;

	rcu_read_lock();
	from_task = rust_binder_thread_task(from_thread);
	if (from_task)
		from_task = tryget_task_struct(from_task);
	rcu_read_unlock();

	set_performance_inheritance(to_task, from_task, VI_BINDER);
	if (from_task)
		put_task_struct(from_task);
}

void vh_rust_binder_restore_priority_pixel_mod(void *data, struct task_struct *task)
{
	set_performance_inheritance(task, NULL, VI_BINDER);
}

static inline void set_binder_task_state(struct task_struct *task, bool is_binder)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(task);

	vp->is_binder_task = is_binder;
}

void vh_binder_looper_state_registered_pixel_mod(void *data, struct binder_thread *thread,
						 struct binder_proc *proc)
{
	set_binder_task_state(thread->task, true);
}

void vh_binder_looper_exited_pixel_mod(void *data, struct binder_thread *thread,
				       struct binder_proc *proc)
{
	set_binder_task_state(thread->task, false);
}

void vh_rust_binder_looper_entry_mod(void *data, rust_binder_thread thread,
				     unsigned int looper_flags)
{
	struct task_struct *task;

	rcu_read_lock();
	task = rust_binder_thread_task(thread);
	if (task) {
		if (looper_flags & RB_LOOPER_EXITED)
			set_binder_task_state(task, false);
		else if (looper_flags & (RB_LOOPER_REGISTERED | RB_LOOPER_ENTERED))
			set_binder_task_state(task, true);
	}
	rcu_read_unlock();
}

void rvh_rtmutex_prepare_setprio_pixel_mod(void *data, struct task_struct *p,
	struct task_struct *pi_task)
{
	set_performance_inheritance(p, pi_task, VI_RTMUTEX);
}

void rvh_try_to_wake_up_success_pixel_mod(void *data, struct task_struct *p)
{
	trace_sched_wakeup_task_attr(p, p->cpus_ptr, task_util_est(p),
				     uclamp_eff_value_pixel_mod(p, UCLAMP_MIN),
				     p->se.vruntime);
}

void set_cluster_enabled_cb(int cluster, int enabled)
{
	pixel_cluster_enabled[cluster] = enabled;
}

int get_cluster_enabled(int cluster)
{
	return pixel_cluster_enabled[cluster];
}
