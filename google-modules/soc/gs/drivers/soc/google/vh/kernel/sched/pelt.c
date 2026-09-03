// SPDX-License-Identifier: GPL-2.0-only
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <kernel/sched/pelt.h>
#include <linux/moduleparam.h>
#include <trace/events/power.h>
#include <trace/hooks/systrace.h>

#if IS_ENABLED(CONFIG_SOC_GS101) || IS_ENABLED(CONFIG_SOC_GS201) || IS_ENABLED(CONFIG_SOC_ZUMA)
#include <performance/gs_perf_mon/gs_perf_mon.h>
#else
#include <perf/core/gs_perf_mon.h>
#endif

#include "sched_priv.h"

#define LOAD_AVG_MAX 47742

/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 */
static u64 decay_load(u64 val, u64 n)
{
	unsigned int local_n;

	if (unlikely(n > LOAD_AVG_PERIOD * 63))
		return 0;

	/* after bounds checking we can collapse to 32-bit */
	local_n = n;

	/*
	 * As y^PERIOD = 1/2, we can combine
	 *    y^n = 1/2^(n/PERIOD) * y^(n%PERIOD)
	 * With a look-up table which covers y^n (n<PERIOD)
	 *
	 * To achieve constant time decay_load.
	 */
	if (unlikely(local_n >= LOAD_AVG_PERIOD)) {
		val >>= local_n / LOAD_AVG_PERIOD;
		local_n %= LOAD_AVG_PERIOD;
	}

	val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32);
	return val;
}

static u32 __accumulate_pelt_segments(u64 periods, u32 d1, u32 d3)
{
	u32 c1, c2, c3 = d3; /* y^0 == 1 */

	/*
	 * c1 = d1 y^p
	 */
	c1 = decay_load((u64)d1, periods);

	/*
	 *            p-1
	 * c2 = 1024 \Sum y^n
	 *            n=1
	 *
	 *              inf        inf
	 *    = 1024 ( \Sum y^n - \Sum y^n - y^0 )
	 *              n=0        n=p
	 */
	c2 = LOAD_AVG_MAX - decay_load(LOAD_AVG_MAX, periods) - 1024;

	return c1 + c2 + c3;
}

/*
 * Accumulate the three separate parts of the sum; d1 the remainder
 * of the last (incomplete) period, d2 the span of full periods and d3
 * the remainder of the (incomplete) current period.
 *
 *           d1          d2           d3
 *           ^           ^            ^
 *           |           |            |
 *         |<->|<----------------->|<--->|
 * ... |---x---|------| ... |------|-----x (now)
 *
 *                           p-1
 * u' = (u + d1) y^p + 1024 \Sum y^n + d3 y^0
 *                           n=1
 *
 *    = u y^p +					(Step 1)
 *
 *                     p-1
 *      d1 y^p + 1024 \Sum y^n + d3 y^0		(Step 2)
 *                     n=1
 */
static __always_inline u32
accumulate_sum(u64 delta, struct sched_avg *sa,
	       unsigned long load, unsigned long runnable, int running)
{
	u32 contrib = (u32)delta; /* p == 0 -> delta < 1024 */
	u64 periods;

	delta += sa->period_contrib;
	periods = delta / 1024; /* A period is 1024us (~1ms) */

	/*
	 * Step 1: decay old *_sum if we crossed period boundaries.
	 */
	if (periods) {
		sa->load_sum = decay_load(sa->load_sum, periods);
		sa->runnable_sum =
			decay_load(sa->runnable_sum, periods);
		sa->util_sum = decay_load((u64)(sa->util_sum), periods);

		/*
		 * Step 2
		 */
		delta %= 1024;
		if (load) {
			/*
			 * This relies on the:
			 *
			 * if (!load)
			 *	runnable = running = 0;
			 *
			 * clause from ___update_load_sum(); this results in
			 * the below usage of @contrib to disappear entirely,
			 * so no point in calculating it.
			 */
			contrib = __accumulate_pelt_segments(periods,
					1024 - sa->period_contrib, delta);
		}
	}
	sa->period_contrib = delta;

	if (load)
		sa->load_sum += load * contrib;
	if (runnable)
		sa->runnable_sum += runnable * contrib << SCHED_CAPACITY_SHIFT;
	if (running)
		sa->util_sum += contrib << SCHED_CAPACITY_SHIFT;

	return periods;
}

/*
 * When syncing *_avg with *_sum, we must take into account the current
 * position in the PELT segment otherwise the remaining part of the segment
 * will be considered as idle time whereas it's not yet elapsed and this will
 * generate unwanted oscillation in the range [1002..1024[.
 *
 * The max value of *_sum varies with the position in the time segment and is
 * equals to :
 *
 *   LOAD_AVG_MAX*y + sa->period_contrib
 *
 * which can be simplified into:
 *
 *   LOAD_AVG_MAX - 1024 + sa->period_contrib
 *
 * because LOAD_AVG_MAX*y == LOAD_AVG_MAX-1024
 *
 * The same care must be taken when a sched entity is added, updated or
 * removed from a cfs_rq and we need to update sched_avg. Scheduler entities
 * and the cfs rq, to which they are attached, have the same position in the
 * time segment because they use the same clock. This means that we can use
 * the period_contrib of cfs_rq when updating the sched_avg of a sched_entity
 * if it's more convenient.
 */
void
___update_load_avg(struct sched_avg *sa, unsigned long load)
{
	u32 divider = get_pelt_divider(sa);

	/*
	 * Step 2: update *_avg.
	 */
	sa->load_avg = div_u64(load * sa->load_sum, divider);
	sa->runnable_avg = div_u64(sa->runnable_sum, divider);
	WRITE_ONCE(sa->util_avg, sa->util_sum / divider);
}

/*
 * Approximate the new util_avg value assuming an entity has continued to run
 * for @delta us.
 */
unsigned long approximate_util_avg(unsigned long util, u64 delta)
{
	struct sched_avg sa = {
		.util_sum = util * PELT_MIN_DIVIDER,
		.util_avg = util,
	};

	if (unlikely(!delta))
		return util;

	accumulate_sum(delta, &sa, 1, 0, 1);
	___update_load_avg(&sa, 0);

	return sa.util_avg;
}

/*
 * Approximate the required amount of runtime in ms required to reach @util.
 */
u64 approximate_runtime(unsigned long util)
{
	struct sched_avg sa = {};
	u64 delta = static_branch_likely(&enable_ptick) ? PTICK_PERIOD_US : TICK_USEC;
	u64 runtime = 0;

	if (unlikely(!util))
		return runtime;

	while (sa.util_avg < util) {
		accumulate_sum(delta, &sa, 1, 0, 1);
		___update_load_avg(&sa, 0);
		runtime++;
	}

	if (static_branch_likely(&enable_ptick))
		return runtime * (unsigned int)(PTICK_PERIOD_US/USEC_PER_MSEC);
	else
		return runtime * (TICK_USEC/USEC_PER_MSEC);
}

static inline u64
scale_mem_stall_preasure(u64 delta, struct vendor_task_struct *vp, int cpu)
{
	u64 cycle_delta = vp->mp_stats.mp.current_cycle - vp->mp_stats.mp.last_cycle;
	u64 stall_delta = vp->mp_stats.mp.current_stall - vp->mp_stats.mp.last_stall;
	int cluster = pixel_cpu_to_cluster[cpu];

	/* This does happen occasionally for unknown reason. */
	if (cycle_delta >= stall_delta)
		return delta * stall_delta / cycle_delta;

	/* Fallback approach if cycle_delta < stall_delta */
	if (vp->mp_stats.pmu_stats.cycle[cluster])
		return delta * vp->mp_stats.pmu_stats.stall[cluster] /
			vp->mp_stats.pmu_stats.cycle[cluster];

	return 0;
}

static inline u64
scale_mem_access_preasure(u64 delta, struct vendor_task_struct *vp)
{
	u64 inst_delta = vp->mp_stats.mp.current_inst - vp->mp_stats.mp.last_inst;
	u64 mem_rd_inst_delta = vp->mp_stats.mp.current_mem_rd_inst -
				vp->mp_stats.mp.last_mem_rd_inst;
	u64 mem_wr_inst_delta = vp->mp_stats.mp.current_mem_wr_inst -
				vp->mp_stats.mp.last_mem_wr_inst;
	u64 mem_access_inst_delta = mem_rd_inst_delta + mem_wr_inst_delta;

	/* This does happen occasionally for unknown reason. */
	if (inst_delta >= mem_access_inst_delta)
		return delta * mem_access_inst_delta / inst_delta;

	/* Fallback approach if inst_delta < mem_access_inst_delta */
	if (vp->mp_stats.pmu_stats.total_inst)
		return delta * (vp->mp_stats.pmu_stats.total_mem_rd_inst +
			vp->mp_stats.pmu_stats.total_mem_wr_inst) /
			vp->mp_stats.pmu_stats.total_inst;

	return 0;
}

static __always_inline u32
accumulate_sum_mp(u64 delta, struct vendor_task_struct *vp, int on_rq, int running, int cpu)
{
	u32 contrib = (u32)delta;
	u32 stall_contrib, access_contrib;
	u64 periods;

	delta += vp->mp_stats.mp.period_contrib;
	periods = delta / 1024;

	if (periods) {
		vp->mp_stats.mp.mem_stall_pressure_sum =
			decay_load((u64)(vp->mp_stats.mp.mem_stall_pressure_sum), periods);

		vp->mp_stats.mp.mem_access_pressure_sum =
			decay_load((u64)(vp->mp_stats.mp.mem_access_pressure_sum), periods);

		delta %= 1024;
		if (on_rq) {
			contrib = __accumulate_pelt_segments(periods,
					1024 - vp->mp_stats.mp.period_contrib, delta);
		}
	}
	vp->mp_stats.mp.period_contrib = delta;

	if (running) {
		/* Only do the scale down if it is running. */
		stall_contrib = scale_mem_stall_preasure(contrib, vp, cpu);
		vp->mp_stats.mp.mem_stall_pressure_sum += stall_contrib << SCHED_CAPACITY_SHIFT;

		access_contrib = scale_mem_access_preasure(contrib, vp);
		vp->mp_stats.mp.mem_access_pressure_sum += access_contrib << SCHED_CAPACITY_SHIFT;
	}

	return periods;
}

static int
___update_mem_pressure(u64 now, struct sched_entity *se, int cpu, int on_rq, int running)
{
	u64 delta;
	u64 cycle = 0, stall = 0, inst = 0, mem_rd_inst = 0, mem_wr_inst = 0;
	struct task_struct *p = task_of(se);
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	int ret = 0;

	raw_spin_lock(&vp->lock);

	if (cpu == raw_smp_processor_id() &&
	    !read_perf_event_local(cpu, CORE_STALL_INDEX, &stall) &&
	    !read_perf_event_local(cpu, CORE_CYCLE_INDEX, &cycle) &&
	    !read_perf_event_local(cpu, CORE_INST_INDEX, &inst) &&
	    !read_perf_event_local(cpu, CORE_MEM_RD_INST_INDEX, &mem_rd_inst) &&
	    !read_perf_event_local(cpu, CORE_MEM_WR_INST_INDEX, &mem_wr_inst)) {
		if (likely(cycle >= stall)) {
			vp->mp_stats.mp.last_cycle = vp->mp_stats.mp.current_cycle;
			vp->mp_stats.mp.last_stall = vp->mp_stats.mp.current_stall;
			vp->mp_stats.mp.current_cycle = cycle;
			vp->mp_stats.mp.current_stall = stall;
		}

		if (likely(inst >= (mem_rd_inst + mem_wr_inst))) {
			vp->mp_stats.mp.last_inst = vp->mp_stats.mp.current_inst;
			vp->mp_stats.mp.last_mem_rd_inst = vp->mp_stats.mp.current_mem_rd_inst;
			vp->mp_stats.mp.last_mem_wr_inst = vp->mp_stats.mp.current_mem_wr_inst;
			vp->mp_stats.mp.current_inst = inst;
			vp->mp_stats.mp.current_mem_rd_inst = mem_rd_inst;
			vp->mp_stats.mp.current_mem_wr_inst = mem_wr_inst;
		}
	}

	delta = now - vp->mp_stats.mp.last_update_time;

	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 */
	if ((s64)delta < 0) {
		vp->mp_stats.mp.last_update_time = now;
		goto out;
	}

	delta >>= 10;
	if (!delta)
		goto out;

	vp->mp_stats.mp.last_update_time += delta << 10;

	if (!on_rq)
		running = 0;

	if (!accumulate_sum_mp(delta, vp, on_rq, running, cpu))
		goto out;

	ret = 1;
out:
	raw_spin_unlock(&vp->lock);

	return ret;
}

static inline u32 get_pelt_divider_mp(struct vendor_task_struct *vp)
{
	return PELT_MIN_DIVIDER + vp->mp_stats.mp.period_contrib;
}

static void
___update_memory_pressure_avg(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	u32 divider = get_pelt_divider_mp(vp);

	raw_spin_lock(&vp->lock);
	WRITE_ONCE(vp->mp_stats.mp.mem_stall_pressure_avg,
		   vp->mp_stats.mp.mem_stall_pressure_sum / divider);
	WRITE_ONCE(vp->mp_stats.mp.mem_access_pressure_avg,
		   vp->mp_stats.mp.mem_access_pressure_sum / divider);
	raw_spin_unlock(&vp->lock);
}

int __update_load_avg_mem_pressure(u64 now, struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct task_struct *p = task_of(se);

	if (unlikely(is_idle_task(p) || p->flags & PF_EXITING))
		return 0;

	if (___update_mem_pressure(now, se, cfs_rq->rq->cpu, !!se->on_rq, cfs_rq->curr == se)) {
		___update_memory_pressure_avg(se);
		trace_per_task_memory_pressure(task_of(se), get_mem_stall_pressure(task_of(se)),
					       get_mem_access_pressure(task_of(se)));
		return 1;
	}

	return 0;
}

void attach_memory_util(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	u32 divider = get_pelt_divider(&cfs_rq->avg);
	struct task_struct *p = task_of(se);
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	raw_spin_lock(&vp->lock);
	vp->mp_stats.mp.last_update_time = cfs_rq->avg.last_update_time;
	vp->mp_stats.mp.period_contrib = cfs_rq->avg.period_contrib;
	vp->mp_stats.mp.mem_stall_pressure_sum = vp->mp_stats.mp.mem_stall_pressure_avg * divider;
	vp->mp_stats.mp.mem_access_pressure_sum = vp->mp_stats.mp.mem_access_pressure_avg * divider;
	raw_spin_unlock(&vp->lock);
}
