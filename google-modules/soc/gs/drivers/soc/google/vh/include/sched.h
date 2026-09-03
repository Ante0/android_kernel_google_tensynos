/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VH_SCHED_H
#define _VH_SCHED_H

#define ANDROID_VENDOR_CHECK_SIZE_ALIGN(_orig, _new)				\
		static_assert(sizeof(struct{_new;}) <= sizeof(struct{_orig;}),	\
			       __FILE__ ":" __stringify(__LINE__) ": "		\
			       __stringify(_new)				\
			       " is larger than "				\
			       __stringify(_orig) );				\
		static_assert(__alignof__(struct{_new;}) <= __alignof__(struct{_orig;}),	\
			       __FILE__ ":" __stringify(__LINE__) ": "		\
			       __stringify(_orig)				\
			       " is not aligned the same as "			\
			       __stringify(_new) );

// Maximum size: u64[2] for ANDROID_VENDOR_DATA_ARRAY(1, 2) in task_struct
#if IS_ENABLED(CONFIG_USE_VENDOR_GROUP_UTIL)
enum utilization_group {
	UG_BG = 0,
	UG_FG,
	UG_AUTO,
	UG_MAX = UG_AUTO,
};
#endif

enum vendor_group {
	VG_INVALID = -1,
	VG_SYSTEM = 0,
	VG_TOPAPP,
	VG_FOREGROUND,
	VG_CAMERA,
	VG_CAMERA_POWER,
	VG_BACKGROUND,
	VG_SYSTEM_BACKGROUND,
	VG_NNAPI_HAL,
	VG_RT,
	VG_DEX2OAT,
	VG_OTA,
	VG_SF,
	VG_FOREGROUND_WINDOW,
	VG_MAX,
};

enum vendor_inheritnace_t {
	VI_BINDER = 0,
	VI_RTMUTEX,
	VI_MAX,
};

enum vendor_sched_qos {
	SCHED_QOS_NONE,
	SCHED_QOS_POWER_EFFICIENCY,
	SCHED_QOS_SENSITIVE_STANDARD,
	SCHED_QOS_SENSITIVE_HIGH,
	SCHED_QOS_SENSITIVE_EXTREME,
	SCHED_QOS_MAX,
};

struct pmu_stats_struct {
	u64 last_cycle[CONFIG_VH_SCHED_MAX_CLUSTER_NR];
	u64 last_stall[CONFIG_VH_SCHED_MAX_CLUSTER_NR];
	u64 last_inst[CONFIG_VH_SCHED_MAX_CLUSTER_NR];
	u64 cycle[CONFIG_VH_SCHED_MAX_CLUSTER_NR];
	u64 stall[CONFIG_VH_SCHED_MAX_CLUSTER_NR];
	u64 inst[CONFIG_VH_SCHED_MAX_CLUSTER_NR];
	u64 last_mem_rd_inst;
	u64 last_mem_wr_inst;
	u64 total_mem_rd_inst;
	u64 total_mem_wr_inst;
	u64 total_inst;
};

struct mem_pressure_struct {
	u64				last_update_time;
	u64				last_cycle;
	u64				last_stall;
	u64				last_inst;
	u64				last_mem_rd_inst;
	u64				last_mem_wr_inst;
	u64				current_cycle;
	u64				current_stall;
	u64				current_inst;
	u64				current_mem_rd_inst;
	u64				current_mem_wr_inst;
	u64				mem_stall_pressure_sum;
	u64				mem_access_pressure_sum;
	unsigned long			mem_stall_pressure_avg;
	unsigned long			mem_access_pressure_avg;
	u32				period_contrib;
};

struct mem_pressure_stats {
	struct pmu_stats_struct pmu_stats;
	struct mem_pressure_struct mp;
};

struct vendor_inheritance_struct {
	uint16_t uclamp[VI_MAX][UCLAMP_CNT];
	uint8_t adpf;
	uint8_t prefer_idle;
	uint8_t prefer_fit;
	uint8_t prefer_high_cap;
	uint8_t preempt_wakeup;
};

struct uclamp_filter {
	bool uclamp_min_ignored : 1;
	bool uclamp_max_ignored : 1;
};

/*
 * Always remember to initialize any new fields added here in
 * init_vendor_task_struct() or you'll find newly forked tasks inheriting
 * random states from the parent.
 */
struct vendor_task_struct {
	raw_spinlock_t lock;
	enum vendor_group group;
	enum vendor_group group_tracked;	/* cache of last sent sched_group event value */
	enum vendor_group last_notified_group;	/* cache of last sent Generic Netlink event value */
	short group_tracked_ctx_count;		/* count of ctx switches for rate limiting the trace event  */
	unsigned long direct_reclaim_ts;
	int queued_to_list;
	bool prefer_high_cap;
	int auto_uclamp_max_flags;	// Relative to cpu instead of absolute
	struct uclamp_filter uclamp_filter;
	int orig_prio;
	int tag_nice;
	int orig_policy;		/* Protected by task_rq_lock() */
	unsigned long iowait_boost;
	bool is_binder_task;
	bool in_feec;
	bool vendor_boost;
	unsigned int state_dequeued;

	/* parameters for inheritance */
	struct vendor_inheritance_struct vi;

	u64 runnable_start_ns;
	u64 prev_sum_exec_runtime;
	u64 delta_exec;
	u64 last_dequeue;
	unsigned long util_enqueued;
	unsigned long util_dequeued;
	unsigned long util_dequeued_candidate;
	unsigned long prev_util_dequeued;
	unsigned long prev_util;
	bool ignore_util_est_update;

	/* sched qos attributes */
	unsigned int rampup_multiplier;
	enum vendor_sched_qos sched_qos_profile;
	unsigned long sched_qos_user_defined_flag;
	unsigned long prev_sched_qos_user_defined_flag;

	/*
	 * A general field for time measurement in the same process context.
	 * Be careful it should be used for stackwise, use the wrapper
	 * functions to access this field:
	 * - sched_set_vendor_task_struct_private
	 * - sched_get_and_reset_vendor_task_struct_private
	 */
	unsigned long private;

	// ADPF scheduler hint value.
	int adpf_adj;
	// Definition of real_cap: the current cpu_cap that a task was actually running on.
	u64 real_cap_avg;
	// The total durtion for real_cap calculation.
	u64 real_cap_total_ns;
	// Last updated timestamp of real_cap calculation.
	u64 real_cap_update_ns;

	/* For boost at fork */
	u64 boost_at_fork_start_ns;

	/* For per-task memory aware scheduling */
	struct mem_pressure_stats mp_stats;
};
ANDROID_VENDOR_CHECK_SIZE_ALIGN(u64 android_vendor_data1[98], struct vendor_task_struct t);

/* head of vendor_task_struct */
struct vendor_task_struct_h {
	struct list_head node;
	struct vendor_task_struct *body;
};
ANDROID_VENDOR_CHECK_SIZE_ALIGN(u64 android_vendor_data1[6], struct vendor_task_struct_h t);

#if IS_ENABLED(CONFIG_VH_SCHED)
int sched_thermal_freq_cap(unsigned int cpu, unsigned int freq);
struct vendor_task_struct *sched_get_vendor_task_struct(struct task_struct *p);
void sched_set_vendor_task_struct_private(struct vendor_task_struct *vp, unsigned long val);
unsigned long sched_get_and_reset_vendor_task_struct_private(struct vendor_task_struct *vp);
#else
static inline int sched_thermal_freq_cap(unsigned int cpu, unsigned int freq) { return 0; }
#endif

void update_task_real_cap(struct task_struct *p);

#endif
