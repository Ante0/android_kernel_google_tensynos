/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/sched/clock.h>
#include "sched.h"
#include "sched_events.h"
#include "drivers/android/binder_internal.h"
#include "drivers/android/binder/rust_binder.h"
#include <asm/atomic.h>

#define UCLAMP_STATS_SLOTS  21
#define UCLAMP_STATS_STEP   (100 / (UCLAMP_STATS_SLOTS - 1))
#define DEF_UTIL_THRESHOLD  1280
#define DEF_UTIL_POST_INIT_SCALE  512
#define DEF_THERMAL_CAP_MARGIN  1536
#define C1_EXIT_LATENCY     1
#define THREAD_PRIORITY_TOP_APP_BOOST 110
#define THREAD_PRIORITY_BACKGROUND    130
#define THREAD_PRIORITY_LOWEST        139
#define LIST_QUEUED         0xa5a55a5a
#define LIST_NOT_QUEUED     0x5a5aa5a5
#define LIB_PATH_LENGTH 512
#define SCHED_AUTO_UCLAMP_MAX_TASK	0
#define SCHED_AUTO_UCLAMP_MAX_THERMAL	1
#define SCHED_AUTO_UCLAMP_MAX_ST	2
#define SCHED_AUTO_UCLAMP_MAX_NUM_TYPES	3

#if IS_ENABLED(CONFIG_TICK_DRIVEN_LATGOV)
#define CORE_CYCLE_INDEX	PERF_CYCLE_IDX
#define CORE_STALL_INDEX	PERF_STALL_BACKEND_MEM_IDX
#define CORE_INST_INDEX		PERF_INST_IDX
#define CORE_MEM_RD_INST_INDEX	PERF_MEM_RD_INST_IDX
#define CORE_MEM_WR_INST_INDEX	PERF_MEM_WR_INST_IDX
#else
#define CORE_CYCLE_INDEX	CYCLE_IDX
#define CORE_STALL_INDEX	STALL_IDX
#define CORE_INST_INDEX		INST_IDX
#define CORE_MEM_RD_INST_INDEX	MEM_RD_INST_IDX
#define CORE_MEM_WR_INST_INDEX	MEM_WR_INST_IDX
#endif

#define PTICK_PERIOD_US		(USEC_PER_MSEC * 1.025)
#define PTICK_PERIOD_NS		(NSEC_PER_MSEC * 1.025)

/*
 * For cpu running normal tasks, its uclamp.min will be 0 and uclamp.max will be 1024,
 * and the sum will be 1024. We use this as index that cpu is not running important tasks.
 */
#define DEFAULT_IMPRATANCE_THRESHOLD	1024
#define MAX_CPU_IMPORTANCE		(DEFAULT_IMPRATANCE_THRESHOLD << 1)

/*
 * Sets uclamp_max to the task based on the most efficient point of the CPU the
 * task is currently running on.
 */
#define AUTO_UCLAMP_MAX_MAGIC		-2

#define AUTO_UCLAMP_MAX_FLAG_TASK	BIT(0)
#define AUTO_UCLAMP_MAX_FLAG_ST		BIT(1)

#define UCLAMP_BUCKET_DELTA DIV_ROUND_CLOSEST(SCHED_CAPACITY_SCALE, UCLAMP_BUCKETS)

/*
 * Bit definition for sched qos features.
 */
#define SCHED_QOS_RAMPUP_MULTIPLIER_BIT	0
#define SCHED_QOS_PREFER_HIGH_CAP_BIT	1
#define SCHED_QOS_AUTO_UCLAMP_MAX_BIT	2
#define SCHED_QOS_PREEMPT_WAKEUP_BIT	3
#define SCHED_QOS_ADPF_BIT		4
#define SCHED_QOS_PREFER_IDLE_BIT	5
#define SCHED_QOS_PREFER_FIT_BIT	6
#define SCHED_QOS_BOOST_PRIO_BIT	7
#define SCHED_QOS_TAG_NICE_BIT		8
#define SCHED_QOS_AUTO_ADPF_BIT		9

#if IS_ENABLED(CONFIG_PIXEL_EM_VOLTAGE_SCALING)
#define VOLTAGE_SCALING_THRESHOLD	10
#endif

/* Iterate thr' all leaf cfs_rq's on a runqueue */
#define for_each_leaf_cfs_rq_safe(rq, cfs_rq, pos)			\
	list_for_each_entry_safe(cfs_rq, pos, &rq->leaf_cfs_rq_list,	\
				 leaf_cfs_rq_list)

#define get_bucket_id(__val)								      \
		min_t(unsigned int,							      \
		      __val / DIV_ROUND_CLOSEST(SCHED_CAPACITY_SCALE, UCLAMP_BUCKETS),	      \
		      UCLAMP_BUCKETS - 1)

extern unsigned int
	sched_auto_uclamp_max[SCHED_AUTO_UCLAMP_MAX_NUM_TYPES][CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int thermal_cap_margin[CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int sched_capacity_margin[CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int sched_auto_fits_capacity[CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int sched_dvfs_headroom[CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int sched_per_cpu_iowait_boost_max_value[CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int sched_memory_capacity[CONFIG_VH_SCHED_MAX_CPU_NR];
extern unsigned int sched_per_task_iowait_boost_max_value;
extern unsigned int vendor_sched_adpf_rampup_multiplier;
extern unsigned int vendor_sched_overloaded_nr_running_threshold;
extern int vendor_sched_task_placement_retry_count;
extern int vendor_sched_task_placement_retry_delay_us;

extern int pixel_cpu_num;
extern int pixel_cluster_num;
extern int *pixel_cluster_start_cpu;
extern int *pixel_cluster_cpu_num;
extern int *pixel_cpu_to_cluster;
extern int *pixel_cluster_enabled;
extern unsigned int *pixel_cpd_exit_latency;

extern unsigned int vh_sched_max_load_balance_interval;
extern unsigned int vh_sched_min_granularity_ns;

extern char boost_at_fork_task_name[LIB_PATH_LENGTH];
extern raw_spinlock_t boost_at_fork_task_name_lock;
extern unsigned long vendor_sched_boost_at_fork_value;
extern unsigned long vendor_sched_boost_at_fork_duration;

extern unsigned int auto_uclamp_max_st_util_threshold;

extern bool in_suspend_resume;
extern unsigned int vendor_sched_suspend_resume_boost;

extern const char *GRP_NAME[VG_MAX];
extern unsigned int sched_group_tracker_rate_limit;

DECLARE_STATIC_KEY_FALSE(auto_migration_margins_enable);
DECLARE_STATIC_KEY_FALSE(auto_dvfs_headroom_enable);
DECLARE_STATIC_KEY_FALSE(response_time_ms_fix_enable);
DECLARE_STATIC_KEY_FALSE(per_task_memory_aware_enable);
DECLARE_STATIC_KEY_FALSE(eas_fork_exec_enable);
DECLARE_STATIC_KEY_FALSE(enable_ptick);

unsigned long approximate_util_avg(unsigned long util, u64 delta);
u64 approximate_runtime(unsigned long util);
inline void __reset_task_affinity(struct task_struct *p, const struct cpumask *in_mask);
bool should_boost_at_fork(struct task_struct *p);
bool is_vcpu_task(struct task_struct *p);

extern bool update_auto_max_uclamp_st(struct task_struct *p);

extern int read_perf_event_local(int cpu, unsigned int event_id, u64 *count);

extern int __update_load_avg_mem_pressure(u64 now, struct cfs_rq *cfs_rq, struct sched_entity *se);
extern void attach_memory_util(struct cfs_rq *cfs_rq, struct sched_entity *se);

extern inline void check_pushable_task(struct task_struct *p, struct rq *rq);
extern inline void fair_queue_pushable_tasks(struct rq *rq);
extern void fair_remove_pushable_task(struct rq *rq, struct task_struct *p);
extern inline void fair_add_pushable_task(struct rq *rq, struct task_struct *p);

#define cpu_overutilized(cap, max, cpu)	\
		((cap) * sched_capacity_margin[cpu] > (max) << SCHED_CAPACITY_SHIFT)

#define cap_scale(v, s) ((v)*(s) >> SCHED_CAPACITY_SHIFT)

static inline void update_auto_fits_capacity(void)
{
	int cpu;
	unsigned int tick;

	if (static_branch_likely(&enable_ptick))
		tick = PTICK_PERIOD_US;
	else
		tick = TICK_USEC;

	for (cpu = 0; cpu < pixel_cpu_num; cpu++) {
		u64 limit = approximate_runtime(arch_scale_cpu_capacity(cpu)) * USEC_PER_MSEC;
		if (static_branch_likely(&auto_dvfs_headroom_enable))
			limit -= tick;
		else
			limit = cap_scale(limit - tick, arch_scale_cpu_capacity(cpu));
		sched_auto_fits_capacity[cpu] = approximate_util_avg(0, limit);
	}
}

/*
 * The util will fit the capacity if it has enough headroom to grow within the
 * next tick - which is when any load balancing activity happens to do the
 * correction.
 *
 * If util stays within the capacity before tick has elapsed, then it should be
 * fine. If not, then a correction action must happen shortly after it starts
 * running, hence we treat it as !fit.
 *
 * Make sure to take invariance into account so tasks don't linger on smaller
 * cores which needs to run more on to achieve same utilization compared to big
 * core.
 */
static inline bool fits_capacity(unsigned long util, unsigned long capacity, int cpu)
{
	if (static_branch_likely(&auto_migration_margins_enable))
		return util < sched_auto_fits_capacity[cpu];
	else
		return !cpu_overutilized(util, capacity, cpu);
}

#define lsub_positive(_ptr, _val) do {				\
	typeof(_ptr) ptr = (_ptr);				\
	*ptr -= min_t(typeof(*ptr), *ptr, _val);		\
} while (0)

#define sub_positive(_ptr, _val) do {				\
	typeof(_ptr) ptr = (_ptr);				\
	typeof(*ptr) val = (_val);				\
	typeof(*ptr) res, var = READ_ONCE(*ptr);		\
	res = var - val;					\
	if (res > var)						\
		res = 0;					\
	WRITE_ONCE(*ptr, res);					\
} while (0)

#define __container_of(ptr, type, member) ({			\
	void *__mptr = (void *)(ptr);				\
	((type *)(__mptr - offsetof(type, member))); })

#define remove_from_vendor_group_list(__node, __group) do {			\
	unsigned long irqflags;							\
	raw_spin_lock_irqsave(&vendor_group_list[__group].lock, irqflags);	\
	if (__node == vendor_group_list[__group].cur_iterator)			\
		vendor_group_list[__group].cur_iterator = (__node)->prev;	\
	list_del_init(__node);							\
	raw_spin_unlock_irqrestore(&vendor_group_list[__group].lock, irqflags);	\
} while (0)

#define add_to_vendor_group_list(__node, __group) do {				\
	unsigned long irqflags;							\
	raw_spin_lock_irqsave(&vendor_group_list[__group].lock, irqflags);	\
	list_add_tail(__node, &vendor_group_list[__group].list);		\
	raw_spin_unlock_irqrestore(&vendor_group_list[__group].lock, irqflags);	\
} while (0)

struct vendor_group_property {
	bool prefer_idle;
	bool prefer_high_cap;
	bool auto_uclamp_max;
	bool auto_prefer_fit;
#if !IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
	unsigned int group_throttle;
#endif
	cpumask_t group_cfs_skip_mask;
	cpumask_t preferred_idle_mask_low;
	cpumask_t preferred_idle_mask_mid;
	cpumask_t preferred_idle_mask_high;
	unsigned int uclamp_min_on_nice_low_value;
	unsigned int uclamp_min_on_nice_mid_value;
	unsigned int uclamp_min_on_nice_high_value;
	unsigned int uclamp_max_on_nice_low_value;
	unsigned int uclamp_max_on_nice_mid_value;
	unsigned int uclamp_max_on_nice_high_value;
	unsigned int uclamp_min_on_nice_low_prio;
	unsigned int uclamp_min_on_nice_mid_prio;
	unsigned int uclamp_min_on_nice_high_prio;
	unsigned int uclamp_max_on_nice_low_prio;
	unsigned int uclamp_max_on_nice_mid_prio;
	unsigned int uclamp_max_on_nice_high_prio;
	bool uclamp_min_on_nice_enable;
	bool uclamp_max_on_nice_enable;
#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
	enum utilization_group ug;
#endif
	struct uclamp_se uc_req[UCLAMP_CNT];
	unsigned int rampup_multiplier;

	bool qos_adpf_enable;
	bool qos_prefer_idle_enable;
	bool qos_prefer_fit_enable;
	bool qos_boost_prio_enable;
	bool qos_preempt_wakeup_enable;
	bool qos_auto_uclamp_max_enable;
	bool qos_prefer_high_cap_enable;
	bool qos_rampup_multiplier_enable;
	bool qos_tag_nice_enable;

	bool disable_sched_setaffinity;
	bool disable_sched_setaffinity_mask;
	bool use_batch_policy;
};

#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
struct vendor_util_group_property {
#if IS_ENABLED(CONFIG_USE_GROUP_THROTTLE)
	unsigned int group_throttle;
#endif
	struct uclamp_se uc_req[UCLAMP_CNT];
};
#endif

struct uclamp_stats {
	spinlock_t lock;
	bool last_min_in_effect;
	bool last_max_in_effect;
	unsigned int last_uclamp_min_index;
	unsigned int last_uclamp_max_index;
	unsigned int last_util_diff_min_index;
	unsigned int last_util_diff_max_index;
	u64 util_diff_min[UCLAMP_STATS_SLOTS];
	u64 util_diff_max[UCLAMP_STATS_SLOTS];
	u64 total_time;
	u64 last_update_time;
	u64 time_in_state_min[UCLAMP_STATS_SLOTS];
	u64 time_in_state_max[UCLAMP_STATS_SLOTS];
	u64 effect_time_in_state_min[UCLAMP_STATS_SLOTS];
	u64 effect_time_in_state_max[UCLAMP_STATS_SLOTS];
};

#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
struct vendor_cfs_util {
	raw_spinlock_t lock;
	struct sched_avg avg;
	unsigned long util_removed;
	unsigned long util_est;
};
#endif

struct vendor_group_list {
	struct list_head list;
	raw_spinlock_t lock;
	struct list_head *cur_iterator;
	struct mutex iter_mutex;
};

unsigned long apply_dvfs_headroom(unsigned long util, int cpu, bool tapered);
unsigned long map_util_freq_pixel_mod(unsigned long util, unsigned long freq,
				      unsigned long cap, int cpu);
void check_migrate_rt_task(struct rq *rq, struct task_struct *p);
void rvh_uclamp_eff_get_pixel_mod(void *data, struct task_struct *p, enum uclamp_id clamp_id,
				  struct uclamp_se *uclamp_max, struct uclamp_se *uclamp_eff,
				  int *ret);

enum vendor_group_attribute {
	VTA_TASK_GROUP,
	VTA_PROC_GROUP,
};

enum VENDOR_TUNABLE_TYPE {
	SCHED_CAPACITY_MARGIN,
	SCHED_AUTO_UCLAMP_MAX,
	SCHED_DVFS_HEADROOM,
	SCHED_IOWAIT_BOOST_MAX,
	SCHED_THERMAL_CAP_MARGIN,
	SCHED_MAX_UCLAMP_ST,
	SCHED_MEMORY_CAPACITY,
};

enum cpu_util_type {
	FREQUENCY_UTIL,
	ENERGY_UTIL,
};

#if !IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
struct vendor_task_group_struct {
	enum vendor_group group;
};

ANDROID_VENDOR_CHECK_SIZE_ALIGN(u64 android_vendor_data1[4], struct vendor_task_group_struct t);
#endif

extern bool vendor_sched_reduce_prefer_idle;
extern bool vendor_sched_auto_prefer_idle;
extern bool vendor_sched_auto_latency_sensitive_nice;
extern bool vendor_sched_auto_latency_sensitive_affinity;
extern bool vendor_sched_ptick_auto_spread;
extern struct vendor_group_property vg[VG_MAX];

DECLARE_STATIC_KEY_FALSE(uclamp_min_filter_enable);
DECLARE_STATIC_KEY_FALSE(uclamp_max_filter_enable);

DECLARE_STATIC_KEY_FALSE(tapered_dvfs_headroom_enable);

DECLARE_STATIC_KEY_FALSE(enqueue_dequeue_ready);

DECLARE_STATIC_KEY_FALSE(skip_inefficient_opps_enable);
DECLARE_STATIC_KEY_FALSE(use_em_for_freq_mapping);

DECLARE_STATIC_KEY_FALSE(update_freq_on_idle_enable);

DECLARE_STATIC_KEY_FALSE(dsulat_fast_switch_enable);
DECLARE_STATIC_KEY_FALSE(memlat_fast_switch_enable);

/*
 * Any governor that relies on util signal to drive DVFS, must populate these
 * percpu dvfs_update_delay variables.
 *
 * It should describe the rate/delay at which the governor sends DVFS freq
 * update to the hardware in us.
 */
DECLARE_PER_CPU(u64, dvfs_update_delay);

#define SCHED_PIXEL_FORCE_UPDATE		BIT(8)

/*****************************************************************************/
/*                       Upstream Code Section                               */
/*****************************************************************************/
/*
 * This part of code is copied from Android common GKI kernel and unmodified.
 * Any change for these functions in upstream GKI would require extensive review
 * to make proper adjustment in vendor hook.
 */
#define UTIL_EST_MARGIN (SCHED_CAPACITY_SCALE / 100)

extern struct uclamp_se uclamp_default[UCLAMP_CNT];

void set_next_buddy(struct sched_entity *se);

static inline unsigned long task_util(struct task_struct *p)
{
	return READ_ONCE(p->se.avg.util_avg);
}

static inline unsigned long task_runnable(struct task_struct *p)
{
	return READ_ONCE(p->se.avg.runnable_avg);
}

static inline unsigned long _task_util_est(struct task_struct *p)
{
	return READ_ONCE(p->se.avg.util_est) & ~UTIL_AVG_UNCHANGED;
}

static inline unsigned long task_util_est(struct task_struct *p)
{
	return max(task_util(p), _task_util_est(p));
}

static inline unsigned long capacity_of(int cpu)
{
	return cpu_rq(cpu)->cpu_capacity;
}

extern inline void uclamp_rq_inc_id(struct rq *rq, struct task_struct *p,
				    enum uclamp_id clamp_id);
extern inline void uclamp_rq_dec_id(struct rq *rq, struct task_struct *p,
				    enum uclamp_id clamp_id);

static inline void
uclamp_update_active_locked(struct task_struct *p, enum uclamp_id clamp_id)
{
	struct rq *rq = task_rq(p);

	lockdep_assert_rq_held(rq);

	if (!uclamp_is_used())
		return;

	/*
	 * Setting the clamp bucket is serialized by task_rq_lock().
	 * If the task is not yet RUNNABLE and its task_struct is not
	 * affecting a valid clamp bucket, the next time it's enqueued,
	 * it will already see the updated clamp bucket value.
	 */
	if (p->uclamp[clamp_id].active) {
		uclamp_rq_dec_id(rq, p, clamp_id);
		uclamp_rq_inc_id(rq, p, clamp_id);

		if (clamp_id == UCLAMP_MAX && rq->uclamp_flags & UCLAMP_FLAG_IDLE)
			rq->uclamp_flags &= ~UCLAMP_FLAG_IDLE;
	}
}

static inline void
uclamp_update_active(struct task_struct *p, enum uclamp_id clamp_id)
{
	struct rq_flags rf;
	struct rq *rq;

	if (!uclamp_is_used())
		return;

	/*
	 * Lock the task and the rq where the task is (or was) queued.
	 *
	 * We might lock the (previous) rq of a !RUNNABLE task, but that's the
	 * price to pay to safely serialize util_{min,max} updates with
	 * enqueues, dequeues and migration operations.
	 * This is the same locking schema used by __set_cpus_allowed_ptr().
	 */
	rq = task_rq_lock(p, &rf);

	/*
	 * Setting the clamp bucket is serialized by task_rq_lock().
	 * If the task is not yet RUNNABLE and its task_struct is not
	 * affecting a valid clamp bucket, the next time it's enqueued,
	 * it will already see the updated clamp bucket value.
	 */
	if (p->uclamp[clamp_id].active) {
		uclamp_rq_dec_id(rq, p, clamp_id);
		uclamp_rq_inc_id(rq, p, clamp_id);

		if (clamp_id == UCLAMP_MAX && rq->uclamp_flags & UCLAMP_FLAG_IDLE)
			rq->uclamp_flags &= ~UCLAMP_FLAG_IDLE;
	}

	task_rq_unlock(rq, p, &rf);
}

static inline int util_fits_cpu(unsigned long util,
				unsigned long uclamp_min,
				unsigned long uclamp_max,
				int cpu)
{
	unsigned long capacity_orig, capacity_orig_thermal;
	unsigned long capacity = capacity_of(cpu);
	bool fits, uclamp_max_fits;

	/*
	 * Check if the real util fits without any uclamp boost/cap applied.
	 */
	fits = fits_capacity(util, capacity, cpu);

	if (!uclamp_is_used())
		return fits;

	/*
	 * We must use arch_scale_cpu_capacity() for comparing against uclamp_min and
	 * uclamp_max. We only care about capacity pressure (by using
	 * capacity_of()) for comparing against the real util.
	 *
	 * If a task is boosted to 1024 for example, we don't want a tiny
	 * pressure to skew the check whether it fits a CPU or not.
	 *
	 * Similarly if a task is capped to arch_scale_cpu_capacity(little_cpu), it
	 * should fit a little cpu even if there's some pressure.
	 *
	 * Only exception is for thermal pressure since it has a direct impact
	 * on available OPP of the system.
	 *
	 * We honour it for uclamp_min only as a drop in performance level
	 * could result in not getting the requested minimum performance level.
	 *
	 * For uclamp_max, we can tolerate a drop in performance level as the
	 * goal is to cap the task. So it's okay if it's getting less.
	 *
	 * In case of capacity inversion, which is not handled yet, we should
	 * honour the inverted capacity for both uclamp_min and uclamp_max all
	 * the time.
	 */
	capacity_orig = arch_scale_cpu_capacity(cpu);
	capacity_orig_thermal = capacity_orig - arch_scale_hw_pressure(cpu);

	/*
	 * We want to force a task to fit a cpu as implied by uclamp_max.
	 * But we do have some corner cases to cater for..
	 *
	 *
	 *                                 C=z
	 *   |                             ___
	 *   |                  C=y       |   |
	 *   |_ _ _ _ _ _ _ _ _ ___ _ _ _ | _ | _ _ _ _ _  uclamp_max
	 *   |      C=x        |   |      |   |
	 *   |      ___        |   |      |   |
	 *   |     |   |       |   |      |   |    (util somewhere in this region)
	 *   |     |   |       |   |      |   |
	 *   |     |   |       |   |      |   |
	 *   +----------------------------------------
	 *         cpu0        cpu1       cpu2
	 *
	 *   In the above example if a task is capped to a specific performance
	 *   point, y, then when:
	 *
	 *   * util = 80% of x then it does not fit on cpu0 and should migrate
	 *     to cpu1
	 *   * util = 80% of y then it is forced to fit on cpu1 to honour
	 *     uclamp_max request.
	 *
	 *   which is what we're enforcing here. A task always fits if
	 *   uclamp_max <= capacity_orig. But when uclamp_max > capacity_orig,
	 *   the normal upmigration rules should withhold still.
	 *
	 *   Only exception is when we are on max capacity, then we need to be
	 *   careful not to block overutilized state. This is so because:
	 *
	 *     1. There's no concept of capping at max_capacity! We can't go
	 *        beyond this performance level anyway.
	 *     2. The system is being saturated when we're operating near
	 *        max capacity, it doesn't make sense to block overutilized.
	 */
	uclamp_max_fits = (capacity_orig == SCHED_CAPACITY_SCALE) && (uclamp_max == SCHED_CAPACITY_SCALE);
	uclamp_max_fits = !uclamp_max_fits && (uclamp_max <= capacity_orig);
	uclamp_max_fits = uclamp_max_fits &&
			  (uclamp_max <= sched_auto_uclamp_max[SCHED_AUTO_UCLAMP_MAX_THERMAL][cpu]);
	fits = fits || uclamp_max_fits;

	/*
	 *
	 *                                 C=z
	 *   |                             ___       (region a, capped, util >= uclamp_max)
	 *   |                  C=y       |   |
	 *   |_ _ _ _ _ _ _ _ _ ___ _ _ _ | _ | _ _ _ _ _ uclamp_max
	 *   |      C=x        |   |      |   |
	 *   |      ___        |   |      |   |      (region b, uclamp_min <= util <= uclamp_max)
	 *   |_ _ _|_ _|_ _ _ _| _ | _ _ _| _ | _ _ _ _ _ uclamp_min
	 *   |     |   |       |   |      |   |
	 *   |     |   |       |   |      |   |      (region c, boosted, util < uclamp_min)
	 *   +----------------------------------------
	 *         cpu0        cpu1       cpu2
	 *
	 * a) If util > uclamp_max, then we're capped, we don't care about
	 *    actual fitness value here. We only care if uclamp_max fits
	 *    capacity without taking margin/pressure into account.
	 *    See comment above.
	 *
	 * b) If uclamp_min <= util <= uclamp_max, then the normal
	 *    fits_capacity() rules apply. Except we need to ensure that we
	 *    enforce we remain within uclamp_max, see comment above.
	 *
	 * c) If util < uclamp_min, then we are boosted. Same as (b) but we
	 *    need to take into account the boosted value fits the CPU without
	 *    taking margin/pressure into account.
	 *
	 * Cases (a) and (b) are handled in the 'fits' variable already. We
	 * just need to consider an extra check for case (c) after ensuring we
	 * handle the case uclamp_min > uclamp_max.
	 */
	uclamp_min = min(uclamp_min, uclamp_max);
	if (util < uclamp_min && capacity_orig != SCHED_CAPACITY_SCALE)
		fits = fits && (uclamp_min <= capacity_orig_thermal);

	return fits;
}

static inline unsigned long
uclamp_eff_value_pixel_mod(struct task_struct *p, enum uclamp_id clamp_id)
{
	struct uclamp_se uc_max = uclamp_default[clamp_id];
	struct uclamp_se uc_eff;
	int ret;

	/* Task currently refcounted: use back-annotated (effective) value */
	if (p->uclamp[clamp_id].active)
		return (unsigned long)p->uclamp[clamp_id].value;

	// This function will always return uc_eff
	rvh_uclamp_eff_get_pixel_mod(NULL, p, clamp_id, &uc_max, &uc_eff, &ret);

	return (unsigned long)uc_eff.value;
}

static inline bool uclamp_boosted_pixel_mod(struct task_struct *p)
{
	return uclamp_eff_value_pixel_mod(p, UCLAMP_MIN) > 0;
}

/*****************************************************************************/
/*                       New Code Section                                    */
/*****************************************************************************/
/*
 * This part of code is new for this kernel, which are mostly helper functions.
 */
#if !IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
static inline struct vendor_task_group_struct *get_vendor_task_group_struct(struct task_group *tg)
{
	return (struct vendor_task_group_struct *)tg->android_vendor_data1;
}
#endif

struct vendor_rq_struct {
	raw_spinlock_t lock;
	unsigned long util_removed;
	unsigned long iowait_boost;
	atomic_t num_adpf_tasks;
	struct hrtimer *ptick_timer;
	struct plist_head pushable_tasks;
};

ANDROID_VENDOR_CHECK_SIZE_ALIGN(u64 android_oem_data1[16], struct vendor_rq_struct t);

static inline struct vendor_task_struct_h *get_vendor_task_struct_h(struct task_struct *p)
{
	return (struct vendor_task_struct_h *)p->android_vendor_data1;
}

inline void _init_vendor_task_data(struct task_struct *t);

static inline struct vendor_task_struct *get_vendor_task_struct(struct task_struct *p)
{
	struct vendor_task_struct_h *vph = (struct vendor_task_struct_h *)p->android_vendor_data1;

	if (likely(vph->body))
		return vph->body;

	_init_vendor_task_data(p);

	return vph->body;
}

static inline struct vendor_inheritance_struct *get_vendor_inheritance_struct(struct task_struct *p)
{
	return &get_vendor_task_struct(p)->vi;
}

static inline int get_vendor_group(struct task_struct *p)
{
       return get_vendor_task_struct(p)->group;
}

static inline void set_vendor_group(struct task_struct *p,  enum vendor_group group)
{
	get_vendor_task_struct(p)->group = group;
}

static inline void set_vendor_task_struct_private(struct vendor_task_struct *vp, unsigned long val)
{
	WARN_ON(vp->private != 0);
	vp->private = val;
}

static inline unsigned long get_and_reset_vendor_task_struct_private(struct vendor_task_struct *vp)
{
	unsigned long val = vp->private;

	vp->private = 0;
	return val;
}
static inline struct vendor_rq_struct *get_vendor_rq_struct(struct rq *rq)
{
	return (struct vendor_rq_struct *)rq->android_oem_data1;
}

static inline bool get_vendor_boost(struct task_struct *p)
{
	return get_vendor_task_struct(p)->vendor_boost;
}

static inline void set_vendor_boost(struct task_struct *p, bool boost)
{
	get_vendor_task_struct(p)->vendor_boost = boost;
}

static inline bool is_highpri_workqueue(struct task_struct *p)
{
	return (p->flags & PF_WQ_WORKER) && task_nice(p) == MIN_NICE;
}

static inline bool __is_adpf(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	bool is_adpf;

	is_adpf = vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_ADPF_BIT);
	is_adpf |= vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_AUTO_ADPF_BIT);

	return is_adpf;
}

static inline bool get_adpf(struct task_struct *p, bool inherited)
{
	/*
	 * task pi_lock is not guaranteed to be held and we must be careful what we access
	 * during these checks.
	 */
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	struct vendor_inheritance_struct *vi = get_vendor_inheritance_struct(p);
	bool is_adpf;

	is_adpf = __is_adpf(p) || is_highpri_workqueue(p);

	if (inherited)
		is_adpf |= vi->adpf;

	return is_adpf && vg[vp->group].qos_adpf_enable;
}

static inline void update_adpf_counter(struct task_struct *p, bool old_adpf);

static inline void __set_auto_adpf_locked(struct task_struct *p, bool val)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	bool old_adpf;

	old_adpf = __is_adpf(p);

	if (val)
		set_bit(SCHED_QOS_AUTO_ADPF_BIT, &vp->sched_qos_user_defined_flag);
	else
		clear_bit(SCHED_QOS_AUTO_ADPF_BIT, &vp->sched_qos_user_defined_flag);

	update_adpf_counter(p, old_adpf);
}

static inline void set_auto_adpf(struct task_struct *p, bool val)
{
	struct rq_flags rf;
	struct rq *rq;

	rq = task_rq_lock(p, &rf);
	__set_auto_adpf_locked(p, val);
	task_rq_unlock(rq, p, &rf);
}

static inline void __set_adpf_locked(struct task_struct *p, bool val)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	bool old_adpf;

	old_adpf = __is_adpf(p);

	if (val)
		set_bit(SCHED_QOS_ADPF_BIT, &vp->sched_qos_user_defined_flag);
	else
		clear_bit(SCHED_QOS_ADPF_BIT, &vp->sched_qos_user_defined_flag);

	update_adpf_counter(p, old_adpf);
}

static inline void set_adpf(struct task_struct *p, bool val)
{
	struct rq_flags rf;
	struct rq *rq;

	rq = task_rq_lock(p, &rf);
	__set_adpf_locked(p, val);
	task_rq_unlock(rq, p, &rf);
}

static inline bool is_binder_task(struct task_struct *p)
{
	return get_vendor_task_struct(p)->is_binder_task;
}

static inline bool should_auto_prefer_idle(struct task_struct *p, int group)
{
	if (group == VG_TOPAPP) {
		/*
		 * Binder Task
		 */
		if (is_binder_task(p))
			return true;
		/*
		 * Task of prio <= 120 and with possitive wake_q_count
		 */
		if (p->prio <= DEFAULT_PRIO && p->wake_q_count)
			return true;
		/*
		 * Task of prio <= 120 and waked up by another process
		 */
		if (current) {
			if (p->prio <= DEFAULT_PRIO && current->tgid != p->tgid)
				return true;
		}
	} else if (group == VG_FOREGROUND) {
		/*
		 * Binder Task
		 */
		if (is_binder_task(p))
			return true;
	}

	return false;
}

static inline bool should_auto_latency_sensitive(struct task_struct *p,
						 const struct cpumask *affinity,
						 const long *nice)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	if (vp->group == VG_TOPAPP) {
		if (vendor_sched_auto_latency_sensitive_nice) {
			if (vp->orig_prio < DEFAULT_PRIO)
				return true;

			if (nice && NICE_TO_PRIO(*nice) < DEFAULT_PRIO)
				return true;
		}

		if (vendor_sched_auto_latency_sensitive_affinity) {
			struct cpumask mask = {};

			mask.bits[0] = (1 << pixel_cluster_start_cpu[1]) - 1;

			if (!affinity)
				affinity = p->cpus_ptr;

			if (!cpumask_intersects(affinity, &mask))
				return true;
		}
	}

	return false;
}

static inline bool should_ptick_auto_spread(struct task_struct *p, int group)
{
	if (!vendor_sched_ptick_auto_spread)
		return false;

	return should_auto_prefer_idle(p, group);
}

static inline bool get_prefer_idle(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	struct vendor_inheritance_struct *vi = get_vendor_inheritance_struct(p);

	/* Ignore prefer idle for push lb, ie: task is already running */
	if (current == p)
		return false;

	/* Let task prefer idle if it is in the duration of boost at fork. */
	if (unlikely(vp->boost_at_fork_start_ns)) {
		if (vp->boost_at_fork_start_ns + vendor_sched_boost_at_fork_duration >=
		    sched_clock())
			return true;

		vp->boost_at_fork_start_ns = 0;
	}

	// Always perfer idle for tasks with prefer_idle set explicitly.
	// In auto_prefer_idle case, only allow high prio tasks of the prefer_idle group,
	// or high prio task with wake_q_count value greater than 0 in top-app.
	if ((vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_PREFER_IDLE_BIT) || vi->prefer_idle) &&
	    vg[vp->group].qos_prefer_idle_enable)
		return true;
	else if (vendor_sched_auto_prefer_idle)
		return should_auto_prefer_idle(p, vp->group);
	else if (vendor_sched_reduce_prefer_idle)
		return (vg[vp->group].prefer_idle && p->prio <= DEFAULT_PRIO &&
			uclamp_eff_value_pixel_mod(p, UCLAMP_MAX) == SCHED_CAPACITY_SCALE);
	else
		return vg[vp->group].prefer_idle;
}

static inline bool get_prefer_fit(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	struct vendor_inheritance_struct *vi = get_vendor_inheritance_struct(p);

	return (vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_PREFER_FIT_BIT) ||
	       vi->prefer_fit) && vg[vp->group].qos_prefer_fit_enable;
}

static inline bool get_boost_prio(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	return (vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_BOOST_PRIO_BIT) &&
		vg[vp->group].qos_boost_prio_enable);
}

static inline bool get_preempt_wakeup(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	struct vendor_inheritance_struct *vi = get_vendor_inheritance_struct(p);

	return (vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_PREEMPT_WAKEUP_BIT) ||
	       vi->preempt_wakeup) && vg[vp->group].qos_preempt_wakeup_enable;
}

static inline bool get_auto_uclamp_max_task(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	return vg[vp->group].auto_uclamp_max ||
	       (vp->auto_uclamp_max_flags & AUTO_UCLAMP_MAX_FLAG_TASK) ||
	       (vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_AUTO_UCLAMP_MAX_BIT) &&
		vg[vp->group].qos_auto_uclamp_max_enable);
}

static inline bool get_power_efficiency(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	return vp->sched_qos_profile == SCHED_QOS_POWER_EFFICIENCY;
}

static inline unsigned int get_rampup_multiplier(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	bool rampup_qos_user_defined =
		vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_RAMPUP_MULTIPLIER_BIT);

	if (!vg[vp->group].qos_rampup_multiplier_enable)
		return vg[vp->group].rampup_multiplier;

	if (get_adpf(p, true))
		return rampup_qos_user_defined
			? max(vp->rampup_multiplier, vendor_sched_adpf_rampup_multiplier)
			: vendor_sched_adpf_rampup_multiplier;
	else
		return rampup_qos_user_defined
			? vp->rampup_multiplier
			: vg[vp->group].rampup_multiplier;
}

static inline void _update_prefer_high_cap(struct task_struct *p, bool req)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	struct vendor_inheritance_struct *vi = get_vendor_inheritance_struct(p);

	vp->prefer_high_cap = req ||
		vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_PREFER_HIGH_CAP_BIT) ||
		vi->prefer_high_cap || vg[vp->group].prefer_high_cap;
}

static inline bool get_prefer_high_cap(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	return vp->prefer_high_cap && vg[vp->group].qos_prefer_high_cap_enable;
}

static inline bool get_tag_nice(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	return (vp->sched_qos_user_defined_flag & BIT(SCHED_QOS_TAG_NICE_BIT) &&
		vg[vp->group].qos_tag_nice_enable);
}

static inline unsigned long get_mem_stall_pressure(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	return min(vp->mp_stats.mp.mem_stall_pressure_avg, 1024);
}

static inline unsigned long get_mem_access_pressure(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);

	return min(vp->mp_stats.mp.mem_access_pressure_avg, 1024);
}

static inline unsigned long get_mem_pressure(struct task_struct *p)
{
	return max(get_mem_stall_pressure(p), get_mem_access_pressure(p));
}

static inline bool memory_pressure_fits_cpu(struct task_struct *p, int cpu)
{
	if (get_rampup_multiplier(p) == 0)
		return true;

	return get_mem_pressure(p) <= sched_memory_capacity[cpu];
}

static inline void init_vendor_inheritance_struct(struct vendor_inheritance_struct *vi)
{
	int i;

	for (i = 0; i < VI_MAX; i++) {
		vi->uclamp[i][UCLAMP_MIN] = uclamp_none(UCLAMP_MIN);
		vi->uclamp[i][UCLAMP_MAX] = uclamp_none(UCLAMP_MAX);
	}
	vi->adpf = 0;
	vi->prefer_idle = 0;
	vi->prefer_high_cap = 0;
	vi->prefer_fit = 0;
	vi->preempt_wakeup = 0;
}

/*
 * Returns true if task has privilege false otherwise.
 */
static inline bool check_cred(struct task_struct *p)
{
	const struct cred *cred, *tcred;
	bool ret = true;

	cred = current_cred();
	tcred = get_task_cred(p);
	if (!uid_eq(cred->euid, GLOBAL_ROOT_UID) &&
	    !uid_eq(cred->euid, tcred->uid) &&
	    !uid_eq(cred->euid, tcred->suid) &&
	    !ns_capable(tcred->user_ns, CAP_SYS_NICE)) {
		ret = false;
	}
	put_cred(tcred);
	return ret;
}

static inline bool check_cap(struct task_struct *p, int cap)
{
	const struct cred *cred;
	bool ret = true;

	cred = get_task_cred(p);
	if (!uid_eq(cred->euid, GLOBAL_ROOT_UID) &&
	    !ns_capable(cred->user_ns, cap)) {
		ret = false;
	}
	put_cred(cred);
	return ret;
}

static inline void init_vendor_task_struct_h(struct task_struct *p)
{
	struct vendor_task_struct_h *vph = (struct vendor_task_struct_h *)p->android_vendor_data1;

	INIT_LIST_HEAD(&vph->node);
	vph->body = android_task_vendor_data(p);
}

static inline void init_vendor_task_struct(struct vendor_task_struct *v_tsk)
{
	/* Guarantee everything is not random first, just in case */
	memset(v_tsk, 0, sizeof(struct vendor_task_struct));

	/* Then explicitly set what we expect init value to be */
	raw_spin_lock_init(&v_tsk->lock);
	v_tsk->group = VG_SYSTEM;
	v_tsk->group_tracked = VG_INVALID;
	v_tsk->last_notified_group = VG_INVALID;
	v_tsk->group_tracked_ctx_count = 0;
	v_tsk->queued_to_list = LIST_NOT_QUEUED;
	v_tsk->runnable_start_ns = -1;
	v_tsk->rampup_multiplier = 1;
	v_tsk->sched_qos_profile = SCHED_QOS_NONE;
	init_vendor_inheritance_struct(&v_tsk->vi);
}

extern u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se);
extern unsigned int sysctl_sched_uclamp_min_filter_us;
extern unsigned int sysctl_sched_uclamp_max_filter_divider;
extern unsigned int sysctl_sched_uclamp_min_filter_rt;
extern unsigned int sysctl_sched_uclamp_max_filter_rt;

/*
 * Check if we can ignore uclamp_min requirement of a task. The goal is to
 * prevent small transient tasks from boosting frequency unnecessarily.
 *
 * Returns true if a task can finish its work within a specific threshold.
 *
 * We look at the immediate history of how long the task ran previously.
 * Converting task util_avg into runtime is not trivial and expensive
 * operations.
 */
static inline bool uclamp_can_ignore_uclamp_min(struct rq *rq,
						struct task_struct *p)
{
	struct cpufreq_policy *policy;
	struct sched_entity *se;
	unsigned long runtime;

	if (SCHED_WARN_ON(!uclamp_is_used()))
		return false;

	if (!static_branch_likely(&uclamp_min_filter_enable))
		return false;

	if (task_on_rq_migrating(p))
		return false;

	if (get_adpf(p, true))
		return false;

	if (p->in_iowait && uclamp_boosted_pixel_mod(p))
		return false;

	if (rt_task(p))
		return task_util(p) < sysctl_sched_uclamp_min_filter_rt;

	/*
	 * Based on previous runtime, we check that runtime is sufficiently
	 * larger than a threshold
	 *
	 *
	 *	runtime >= sysctl_sched_uclamp_min_filter_us
	 *
	 * There are 2 caveats:
	 *
	 * 1- When a task migrates on big.LITTLE system, the runtime will not
	 *    be representative then. But this would be one time off error.
	 *
	 * 2. runtime is not frequency invariant. See comment in
	 *    uclamp_can_ignore_uclamp_max()
	 *
	 */
	se = &p->se;
	runtime = se->sum_exec_runtime - se->prev_sum_exec_runtime;
	if (!runtime)
		return false;

	/*
	 * XXX: This can explode if the governor changes in the wrong moment.
	 * We need to create per cpu variables and access those instead. This
	 * will be addressed in the future.
	 */
	policy = cpufreq_cpu_get_raw(cpu_of(rq));
	if (!policy)
		return false;

	if (runtime >= sysctl_sched_uclamp_min_filter_us * 1000)
		return false;

	return true;
}

/*
 * Check if we can ignore uclamp_max requirement of a task. The goal is to
 * prevent small transient tasks that share the rq with other tasks that are
 * capped to lift the capping easily/unnecessarily, hence increase power
 * consumption.
 *
 * Returns true if a task can finish its work within a sched_slice() / divider.
 *
 * We look at the immediate history of how long the task ran previously.
 * Converting task util_avg into runtime or sched_slice() into capacity is not
 * trivial and is an expensive operations. In practice this simple approach
 * proved effective to address the common source of noise. If a task suddenly
 * becomes a busy task, we should detect that and lift the capping at tick, see
 * task_tick_uclamp().
 */
static inline bool uclamp_can_ignore_uclamp_max(struct rq *rq,
						struct task_struct *p)
{
	unsigned long uclamp_max, util;
	unsigned long runtime, slice;
	struct sched_entity *se;
	struct cfs_rq *cfs_rq;
	bool is_rt = rt_task(p);

	if (SCHED_WARN_ON(!uclamp_is_used()))
		return false;

	if (!static_branch_likely(&uclamp_max_filter_enable))
		return false;

	if (task_on_rq_migrating(p))
		return false;

	if (get_adpf(p, true))
		return false;

	if (p->in_iowait && uclamp_boosted_pixel_mod(p))
		return false;

	/*
	 * If util has crossed uclamp_max threshold, then we have to ensure
	 * this is always enforced.
	 */
	util = is_rt ? task_util(p) : task_util_est(p);
	uclamp_max = uclamp_eff_value_pixel_mod(p, UCLAMP_MAX);
	if (util >= uclamp_max)
		return false;

	if (is_rt)
		return util < sysctl_sched_uclamp_max_filter_rt;

	/*
	 * Based on previous runtime, we check the allowed sched_slice() of the
	 * task is large enough for this task to run without preemption.
	 *
	 *
	 *	runtime < sched_slice() / divider
	 *
	 * ==>
	 *
	 *	runtime * divider < sched_slice()
	 *
	 * There are 2 caveats:
	 *
	 * 1- When a task migrates on big.LITTLE system, the runtime will not
	 *    be representative then (not capacity invariant). But this would
	 *    be one time off error.
	 *
	 * 2. runtime is not frequency invariant either. If the
	 *    divider >= fmax/fmin we should be okay in general because that's
	 *    the worst case scenario of how much the runtime will be stretched
	 *    due to it being capped to minimum frequency but the rq should run
	 *    at max. The rule here is that the task should finish its work
	 *    within its sched_slice(). Without this runtime scaling there's a
	 *    small opportunity for the task to ping-pong between capped and
	 *    uncapped state.
	 *
	 */
	se = &p->se;

	runtime = se->sum_exec_runtime - se->prev_sum_exec_runtime;
	if (!runtime)
		return false;

	cfs_rq = cfs_rq_of(se);
	slice = sched_slice(cfs_rq, se);
	runtime *= sysctl_sched_uclamp_max_filter_divider;

	if (runtime >= slice)
		return false;

	return true;
}

static inline void uclamp_set_ignore_uclamp_min(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	vp->uclamp_filter.uclamp_min_ignored = 1;
}
static inline void uclamp_reset_ignore_uclamp_min(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	vp->uclamp_filter.uclamp_min_ignored = 0;
}
static inline void uclamp_set_ignore_uclamp_max(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	vp->uclamp_filter.uclamp_max_ignored = 1;
}
static inline void uclamp_reset_ignore_uclamp_max(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	vp->uclamp_filter.uclamp_max_ignored = 0;
}

static inline bool uclamp_is_ignore_uclamp_min(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	return vp->uclamp_filter.uclamp_min_ignored;
}
static inline bool uclamp_is_ignore_uclamp_max(struct task_struct *p)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	return vp->uclamp_filter.uclamp_max_ignored;
}

static inline bool apply_uclamp_filters(struct rq *rq, struct task_struct *p, int flags)
{
	unsigned long rq_uclamp_min = rq->uclamp[UCLAMP_MIN].value;
	unsigned long rq_uclamp_max = rq->uclamp[UCLAMP_MAX].value;
	bool force_cpufreq_update;

	/* Only inc the delayed task which being woken up. */
	if (p->se.sched_delayed && !(flags & ENQUEUE_DELAYED))
		return false;

	/*
	 * We can't ignore uclamp_min or uclamp_max individually without side
	 * effects due to the way UCLAMP_FLAG_IDLE Is handled. It'll cause
	 * confusions and spit out warnings due to imbalances.
	 *
	 * If one of them needs to be ignored, then we assume the other must be
	 * ignored too.
	 *
	 * This should keep some implicit assumptions about how these values
	 * are inc/dec and how the flag is handled correct.
	 */
	if (uclamp_can_ignore_uclamp_min(rq, p) ||
	    uclamp_can_ignore_uclamp_max(rq, p)) {

		uclamp_set_ignore_uclamp_min(p);
		uclamp_set_ignore_uclamp_max(p);

		/* GKI has incremented it already, undo that */
		uclamp_rq_dec_id(rq, p, UCLAMP_MIN);
		uclamp_rq_dec_id(rq, p, UCLAMP_MAX);
	}

	/*
	 * Force cpufreq update if we filtered and the new rq eff value is
	 * smaller than it was at func entry.
	 */
	force_cpufreq_update = rq_uclamp_min > rq->uclamp[UCLAMP_MIN].value;
	force_cpufreq_update |= rq_uclamp_max > rq->uclamp[UCLAMP_MAX].value;

	return force_cpufreq_update;
}

void check_cancel_protect_slice(struct cfs_rq *cfs_rq, struct sched_entity *pse,
				struct sched_entity *se);

static inline void inc_adpf_counter(struct task_struct *p, struct rq *rq)
{
	struct vendor_rq_struct *vrq;

	if (rt_task(p))
		return;

	vrq = get_vendor_rq_struct(rq);

	atomic_inc(&vrq->num_adpf_tasks);
	/*
	 * Tell the scheduler that this tasks really wants to run next
	 */
	if (!p->se.sched_delayed)
		set_next_buddy(&p->se);

	if (trace_clock_set_rate_enabled()) {
		char trace_name[] = { 'a', 'd', 'p', 'f', '_', 'c', 'p', 'u', '0', '\0' };
		struct vendor_rq_struct *vrq = get_vendor_rq_struct(task_rq(p));

		trace_name[8] = '0' + task_rq(p)->cpu;
		trace_clock_set_rate(trace_name, atomic_read(&vrq->num_adpf_tasks),
			raw_smp_processor_id());
	}
}

static inline void dec_adpf_counter(struct task_struct *p, struct rq *rq)
{
	struct vendor_rq_struct *vrq = get_vendor_rq_struct(rq);

	if (rt_task(p))
		return;

	vrq = get_vendor_rq_struct(rq);

	/*
	 * An enqueue could have happened before our dequeue hook was
	 * registered, which can lead to imbalance.
	 *
	 * Make sure to never go below 0.
	 */
	atomic_dec_if_positive(&vrq->num_adpf_tasks);

	if (trace_clock_set_rate_enabled()) {
		char trace_name[] = { 'a', 'd', 'p', 'f', '_', 'c', 'p', 'u', '0', '\0' };
		struct vendor_rq_struct *vrq = get_vendor_rq_struct(task_rq(p));

		trace_name[8] = '0' + task_rq(p)->cpu;
		trace_clock_set_rate(trace_name, atomic_read(&vrq->num_adpf_tasks),
			raw_smp_processor_id());
	}
}

static inline void update_adpf_counter(struct task_struct *p, bool old_adpf)
{
	lockdep_assert_rq_held(task_rq(p));

	if (task_on_rq_queued(p)) {
		if (old_adpf && !get_adpf(p, true))
			dec_adpf_counter(p, task_rq(p));
		else if (!old_adpf && get_adpf(p, true))
			inc_adpf_counter(p, task_rq(p));
	}
}

extern int vendor_sched_ug_bg_auto_prio;

#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
extern struct vendor_cfs_util vendor_cfs_util[UG_MAX][CONFIG_VH_SCHED_MAX_CPU_NR];
static inline enum utilization_group get_utilization_group(struct task_struct *p, int group)
{
	if (vg[group].ug == UG_AUTO) {
		// Always consider task prio >= vendor_sched_ug_bg_auto_prio as background util
		if (p->prio >= vendor_sched_ug_bg_auto_prio)
			return UG_BG;

		return UG_FG;
	}

	return vg[group].ug;
}
#endif

/*
 * Counter the impact of utilization invariance which can slow down ramp-up
 * time when tasks become suddenly busy.
 *
 * It is only enabled when auto_dvfs_headroom is enabled.
 */
static inline void __update_util_est_invariance(struct rq *rq,
						struct task_struct *p,
						bool update_cfs_rq)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	unsigned long se_enqueued, cfs_rq_enqueued, new_util_est;
	unsigned long util = task_util(p);
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se = &p->se;
	unsigned int rampup_multiplier;
	int __maybe_unused group;
	u64 dequeue_time_ns;
	u64 delta_exec;
	bool do_update;
#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
	unsigned long irqflags;
#endif

	if (!static_branch_likely(&auto_dvfs_headroom_enable))
		return;

	if (!sched_feat(UTIL_EST))
		return;

	if (!fair_policy(p->policy) && !idle_policy(p->policy))
		return;

	rampup_multiplier = get_rampup_multiplier(p);

	if (unlikely(!rampup_multiplier))
		return;

	delta_exec = (se->sum_exec_runtime - vp->prev_sum_exec_runtime)/1000;
	delta_exec *= rampup_multiplier;

	vp->delta_exec += delta_exec;
	vp->prev_sum_exec_runtime = se->sum_exec_runtime;

	dequeue_time_ns = sched_clock() - vp->last_dequeue;
	new_util_est = approximate_util_avg(vp->util_enqueued, vp->delta_exec);

	/* Is the task util increasing? */
	do_update = util > vp->util_dequeued + UTIL_EST_MARGIN;

	/*
	 * Due to invariance util can be stuck at the same value for extended
	 * period of time. Check if this is the case and try to rampup quickly
	 * if it is. To avoid triggering the logic against higher util values
	 * that naturally can linger, check if new_util_est has actually grown
	 * too.
	 */
	do_update |= util == vp->prev_util &&
		dequeue_time_ns >= NSEC_PER_MSEC && new_util_est > vp->util_dequeued + UTIL_EST_MARGIN;

	if (util != vp->prev_util) {
		vp->last_dequeue = sched_clock();
		vp->prev_util = util;
	}

	if (!do_update)
		return;

	se_enqueued = READ_ONCE(se->avg.util_est) & ~UTIL_AVG_UNCHANGED;
	cfs_rq_enqueued = READ_ONCE(cfs_rq->avg.util_est);

	if (update_cfs_rq) {
#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
		group = get_utilization_group(p, get_vendor_group(p));
		raw_spin_lock_irqsave(&vendor_cfs_util[group][rq->cpu].lock, irqflags);
		lsub_positive(&vendor_cfs_util[group][rq->cpu].util_est, se_enqueued);
#endif
		lsub_positive(&cfs_rq_enqueued, se_enqueued);
	}

	WRITE_ONCE(se->avg.util_est, new_util_est);
	trace_sched_util_est_se_tp(se);

	if (update_cfs_rq) {
		cfs_rq_enqueued += new_util_est;
		WRITE_ONCE(cfs_rq->avg.util_est, cfs_rq_enqueued);
		trace_sched_util_est_cfs_tp(cfs_rq);

#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
		vendor_cfs_util[group][rq->cpu].util_est += new_util_est;
		raw_spin_unlock_irqrestore(&vendor_cfs_util[group][rq->cpu].lock, irqflags);
#endif
	}

	/* Send a cpufreq update to reflect the new changes */
	if (static_key_enabled(&enable_ptick))
		cpufreq_update_util(rq, 0);
}

static inline void send_trace_sched_group_tracker(struct task_struct *p, bool is_ctx)
{
	struct vendor_task_struct *vp = get_vendor_task_struct(p);
	int rate_limit = 0;

	if (!trace_sched_group_tracker_enabled()) {
		vp->group_tracked = VG_INVALID;
		vp->group_tracked_ctx_count = 0;
		return;
	}

	if (is_ctx) {
		vp->group_tracked_ctx_count++;
		rate_limit = vp->group_tracked_ctx_count % sched_group_tracker_rate_limit;
	}

	if (vp->group_tracked == vp->group && rate_limit)
		return;

	vp->group_tracked = vp->group;
	vp->group_tracked_ctx_count = 0;
	trace_sched_group_tracker(p, GRP_NAME[vp->group], vp->group);
}

void init_uclamp_stats(void);
int create_procfs_node(void);
#if IS_ENABLED(CONFIG_VH_SCHED) && IS_ENABLED(CONFIG_PIXEL_EM)
void vh_arch_set_freq_scale_pixel_mod(void *data,
				      const struct cpumask *cpus,
				      unsigned long freq,
				      unsigned long max,
				      unsigned long *scale);
#endif
void rvh_set_iowait_pixel_mod(void *data, struct task_struct *p, struct rq *rq,
			      int *should_iowait_boost);
void rvh_select_task_rq_rt_pixel_mod(void *data, struct task_struct *p, int prev_cpu,
				     int sd_flag, int wake_flags, int *new_cpu);
void vh_scheduler_tick_pixel_mod_locked(struct rq *rq);
void vh_scheduler_tick_pixel_mod_nolock(struct rq *rq);
void vh_scheduler_tick_pixel_mod(void *data, struct rq *rq);
void vh_sched_switch_pixel_mod(void *data, bool preempt, struct task_struct *prev,
			       struct task_struct *next, unsigned int prev_state);
void rvh_cpu_overutilized_pixel_mod(void *data, int cpu, int *overutilized);
void rvh_util_est_update_pixel_mod(void *data, struct cfs_rq *cfs_rq, struct task_struct *p,
				   bool task_sleep, int *ret);
#if !IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
void rvh_cpu_cgroup_online_pixel_mod(void *data, struct cgroup_subsys_state *css);
#endif
void rvh_post_init_entity_util_avg_pixel_mod(void *data, struct sched_entity *se);
void rvh_check_preempt_wakeup_fair_pixel_mod(void *data, struct rq *rq,
					     struct task_struct *p, bool *preempt,
					     bool *nopreempt, int wake_flags,
					     struct sched_entity *se,
					     struct sched_entity *pse);
void vh_sched_uclamp_validate_pixel_mod(void *data, struct task_struct *tsk,
					const struct sched_attr *attr,
					int *ret, bool *done);
void vh_sched_setscheduler_uclamp_pixel_mod(void *data, struct task_struct *tsk,
					    int clamp_id, unsigned int value);
void vh_dup_task_struct_pixel_mod(void *data, struct task_struct *tsk,
				  struct task_struct *orig);
void vh_exit_check_pixel_mod(void *data, struct task_struct *tsk);
void rvh_select_task_rq_fair_pixel_mod(void *data, struct task_struct *p, int prev_cpu,
				       int sd_flag, int wake_flags, int *target_cpu);
void init_vendor_group_data(void);
void init_pixel_locks(void);
void rvh_update_rt_rq_load_avg_pixel_mod(void *data, u64 now, struct rq *rq,
					 struct task_struct *p, int running);
void rvh_set_task_cpu_pixel_mod(void *data, struct task_struct *p, unsigned int new_cpu);
void rvh_enqueue_task_pixel_mod(void *data, struct rq *rq, struct task_struct *p, int flags);
void rvh_dequeue_task_pixel_mod(void *data, struct rq *rq, struct task_struct *p, int flags);
void rvh_after_enqueue_task_pixel_mod(void *data, struct rq *rq, struct task_struct *p,
				      int flags);
void rvh_after_dequeue_task_pixel_mod(void *data, struct rq *rq, struct task_struct *p,
				      int flags, bool *results);
void rvh_enqueue_task_fair_pixel_mod(void *data, struct rq *rq, struct task_struct *p,
				     int flags);
void rvh_dequeue_task_fair_pixel_mod(void *data, struct rq *rq, struct task_struct *p,
				     int flags);
void vh_binder_set_priority_pixel_mod(void *data, struct binder_transaction *t,
				      struct task_struct *task);
void vh_binder_restore_priority_pixel_mod(void *data, struct binder_transaction *t,
					  struct task_struct *task);
void vh_binder_looper_state_registered_pixel_mod(void *data, struct binder_thread *thread,
				       struct binder_proc *proc);
void vh_binder_looper_exited_pixel_mod(void *data, struct binder_thread *thread,
				       struct binder_proc *proc);
void vh_binder_proc_transaction_finish(void *data, struct binder_proc *proc,
				       struct binder_transaction *t,
				       struct task_struct *binder_th_task,
				       bool pending_async, bool sync);
void vh_rust_binder_set_priority_pixel_mod(void *data,
					   rust_binder_transaction t,
					   struct task_struct *task);
void vh_rust_binder_restore_priority_pixel_mod(void *data, struct task_struct *task);
void vh_rust_binder_looper_entry_mod(void *data, rust_binder_thread thread,
				     unsigned int looper_flags);
void rvh_rtmutex_prepare_setprio_pixel_mod(void *data, struct task_struct *p,
					   struct task_struct *pi_task);
void vh_dump_throttled_rt_tasks_mod(void *data, int cpu, u64 clock, ktime_t rt_period,
				    u64 rt_runtime, s64 rt_period_timer_expires);
void vh_alter_futex_plist_add_pixel_mod(void *data, struct plist_node *q_list,
				     struct plist_head *hb_chain, bool *already_on_hb);
void vh_lock_task_fork_pixel_mod(void *data, struct task_struct *p);
#if IS_ENABLED(CONFIG_RVH_SCHED_LIB)
void rvh_sched_setaffinity_mod(void *data, struct task_struct *task,
			       const struct cpumask *in_mask, int *res);
void rvh_set_cpus_allowed_ptr_mod(void *data, struct task_struct *task,
				  struct affinity_context *ctx, bool *skip_user_ptr);
#endif /* IS_ENABLED(CONFIG_RVH_SCHED_LIB) */
void rvh_set_cpus_allowed_by_task(void *data, const struct cpumask *cpu_valid_mask,
				  const struct cpumask *new_mask, struct task_struct *p,
				  unsigned int *dest_cpu);
void sched_newidle_balance_pixel_mod(void *data, struct rq *this_rq, struct rq_flags *rf,
				     int *pulled_task, int *done);
void rvh_can_migrate_task_pixel_mod(void *data, struct task_struct *p, int dst_cpu,
				    int *can_migrate);
void rvh_attach_entity_load_avg_pixel_mod(void *data, struct cfs_rq *cfs_rq,
					  struct sched_entity *se);
void rvh_update_load_avg_pixel_mod(void *data, u64 now, struct cfs_rq *cfs_rq,
				   struct sched_entity *se);
#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
void rvh_detach_entity_load_avg_pixel_mod(void *data, struct cfs_rq *cfs_rq,
					  struct sched_entity *se);
void rvh_remove_entity_load_avg_pixel_mod(void *data, struct cfs_rq *cfs_rq,
					  struct sched_entity *se);
void rvh_update_blocked_fair_pixel_mod(void *data, struct rq *rq);
#endif
void android_vh_use_amu_fie_pixel_mod(void *data, bool *use_amu_fie);
void rvh_set_user_nice_locked_pixel_mod(void *data, struct task_struct *p, long *nice,
					bool *allowed);
void rvh_setscheduler_prio_pixel_mod(void *data, struct task_struct *p);
void rvh_find_lowest_rq_pixel_mod(void *data, struct task_struct *sched_ctx,
				  struct task_struct *exec_ctx, struct cpumask *lowest_mask,
				  int ret, int *cpu);
void rvh_util_fits_cpu_pixel_mod(void *data, unsigned long util, unsigned long uclamp_min,
				 unsigned long uclamp_max, int cpu, bool *fits, bool *done);
void rvh_task_fits_cpu_pixel_mod(void *data, struct task_struct *p, unsigned long util,
				 unsigned long uclamp_min, unsigned long uclamp_max, int cpu,
				 bool *fits, bool *done);
void rvh_update_load_sum_pixel_mod(void *data, int *force_update);
void rvh_try_to_wake_up_success_pixel_mod(void *data, struct task_struct *task);
int pmu_poll_init(void);
#if IS_ENABLED(CONFIG_CAL_IF) || IS_ENABLED(CONFIG_GS_DOMAIN_IDLE)
void set_cluster_enabled_cb(int cluster, int enabled);
#endif
void vh_sched_resume_end(void *data, void *unused);
void rvh_set_task_comm_pixel_mod(void *data, struct task_struct *p, bool exec);
void android_rvh_build_perf_domains_pixel_mod(void *data, bool *eas_check);
void update_thermal_freq_cap(unsigned int cpu);
#if IS_ENABLED(CONFIG_PIXEL_EM_FREQUENCY_SCALING)
void reset_scaling_freq(int cpu);
#endif
unsigned long cpu_util(int cpu);
unsigned long task_util(struct task_struct *p);
int cpu_is_idle(int cpu);
int sched_cpu_idle(int cpu);
void ___update_load_avg(struct sched_avg *sa, unsigned long load);
int get_cluster_enabled(int cluster);
#if IS_ENABLED(CONFIG_UCLAMP_STATS)
void update_uclamp_stats(int cpu, u64 time);
#endif
int find_energy_efficient_cpu(struct task_struct *p, int prev_cpu,
			      cpumask_t *valid_mask);
void initialize_vendor_group_property(void);
struct vendor_group_property *get_vendor_group_property(enum vendor_group group);
#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
void migrate_vendor_group_util(struct task_struct *p, unsigned int old, unsigned int new);
struct vendor_util_group_property *get_vendor_util_group_property(enum utilization_group group);
#endif
void update_task_prio(struct task_struct *p, struct vendor_task_struct *vp, bool val, int prio);
unsigned long schedutil_cpu_util_pixel_mod(int cpu, unsigned long util_cfs,
					   unsigned long max, enum cpu_util_type type,
					   struct task_struct *p);
unsigned int map_scaling_freq(int cpu, unsigned int freq);
#if IS_ENABLED(CONFIG_UCLAMP_STATS)
void reset_uclamp_stats(void);
#endif
int pmu_poll_enable(void);
void pmu_poll_disable(void);
int set_prefer_idle_task_name(void);

#define VENDOR_SCHED_NETLINK_DELAY_MS 10
#define VENDOR_CMDLINE_LEN 256

/* Generic Netlink subsystem interface */
int vh_sched_netlink_init(void);
enum vendor_sched_cmd {
	VENDOR_SCHED_CMD_UNSPEC,
	VENDOR_SCHED_CMD_GROUP_MIGRATION,
	VENDOR_SCHED_CMD_TASK_FORK,    /* Thread Fork Notification Command */
	VENDOR_SCHED_CMD_TASK_EXIT,    /* Thread Exit Notification Command */
	VENDOR_SCHED_CMD_TASK_RENAME,  /* Thread Rename Notification Command */
	__VENDOR_SCHED_CMD_MAX,
};
#define VENDOR_SCHED_CMD_MAX (__VENDOR_SCHED_CMD_MAX - 1)

void send_netlink_notification(struct task_struct *p, struct work_struct *work, enum vendor_sched_cmd cmd);
void queue_delayed_notification(struct task_struct *p, enum vendor_sched_cmd cmd, int old_group, int new_group);
