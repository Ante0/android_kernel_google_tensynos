/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _HYP_ARM_SMMU_V3_PKVM_COMMON_TELEMETRY_H
#define _HYP_ARM_SMMU_V3_PKVM_COMMON_TELEMETRY_H

/* Denotes the maximum number of SMMU devices whose telemetry can be collected. */
#define MAX_SMMU_DEVICE 20

/* Make a reasonable guess for max number of domains. */
#define MAX_SMMU_DOMAIN 100

extern struct hyp_shared_arm_smmu_telemetry *kvm_nvhe_sym(kvm_hyp_shared_arm_smmu_telemetry);
#define kvm_hyp_shared_arm_smmu_telemetry kvm_nvhe_sym(kvm_hyp_shared_arm_smmu_telemetry)

/*
 * Common page/block sizes used in Armv8-A translation regimes:
 * 4K, 16K, 64K (granules), and block sizes 2M, 32M, 512M, 1G & 16G.
 * Use this enum to index into the array of map counters.
 */
enum pgsize_idx {
	IDX_4K,
	IDX_16K,
	IDX_64K,
	IDX_2M,
	IDX_32M,
	IDX_512M,
	IDX_1G,
	IDX_16G,
	IDX_MAX,
};

enum ptw_status {
	PTW_COMPLETE,
	PTW_INCOMPLETE,
	PTW_RETRY,
	PTW_ERROR_NODEV,
	PTW_ERROR_INVAL,
};

enum ptw_hvc_res_idx {
	PTW_RET_STATUS_HVC,
	PTW_RET_IOVA_NEXT,
	PTW_RET_STATUS_WALK,
};

#define PTW_VERSION_INPROGRESS_BIT BIT(63)
#define PTW_VERSION_MASK           GENMASK_ULL(62, 0)

enum ptw_hvc_arg_idx {
	PTW_ARG_HVC_NUM,
	PTW_ARG_DOMAIN_ID,
	PTW_ARG_IOVA_START,
};

struct map_counters_by_page_size {
	u32 counters[IDX_MAX];
	/* Tracks page table version to detect concurrent modifications during walks */
	u64 pgt_version;
	u32 pgtables_used;
	u32 empty_pgtables;
};

/**
 * struct hyp_atomic_pages_telemetry - Holds telemetry for atomic pages usage
 * @alloc_reqs:  Total number of allocation requests.
 * @free_reqs:   Total number of free requests.
 * @pages_in_use: Current number of pages allocated from the pool.
 * @max_pages_used: Maximum number of pages used from the pool since boot.
 */
struct hyp_atomic_pages_telemetry {
	u64 alloc_reqs;
	u64 free_reqs;
	s64 pages_in_use;
	s64 max_pages_used;
};

/**
 * struct hyp_stage2_telemetry - Holds stage 2 telemetry data
 */
struct hyp_stage2_telemetry {
	u64 num_s2_tlb_invalidates;
	u64 prot_mem_usage;
	u64 max_prot_mem_usage;
	struct map_counters_by_page_size s2_map_counters;
	struct hyp_atomic_pages_telemetry s2_atomic_pages;
};

/**
 * struct hyp_arm_smmu_domain_telemetry - Holds per-domain telemetry data (hyp side)
 */
struct hyp_arm_smmu_domain_telemetry {
	struct map_counters_by_page_size map_counters;
};

/**
 * struct hyp_cmdq_telemetry - Holds data for cmdq telemetry
 * @sync_cmd_max_timer_tick: Single max cmd sync time, in timer counts (needs to be converted to
 *                           realtime)
 * @sync_cmd_total_timer_tick: Total cmd sync time for all cmd sync, in timer counts
 * @sync_cmd_cnt: Total number of cmd sync count issued
 */
struct hyp_cmdq_telemetry {
	u64 sync_cmd_max_timer_tick;
	u64 sync_cmd_total_timer_tick;
	u64 sync_cmd_cnt;
};

/**
 * struct hyp_arm_smmu_device_telemetry - Holds per-device telemetry data (hyp side)
 */
struct hyp_arm_smmu_device_telemetry {
	struct hyp_cmdq_telemetry cmdq_tel;
};

/**
 * struct hyp_shared_arm_smmu_telemetry - This is shared structure across host and hypervisor. This
 *                                        structure would hold all the device telemetry, domain
 *                                        telemetry, stage-2 telemetry - meaning everything that
 *                                        can be captured from hypervisor. Note that, this structure
 *                                        itself holds all telemetry data and on hypervisor side,
 *                                        there won't be any need to allocate memory dynamically.
 *
 * @enabled - A global enable/disable knob for telemetry
 * @cur_s1_pgtable_usage - Current number of pages holding S1 page tables
 * @max_s1_pgtable_usage - Max S1 page tables since boot
 * @arch_timer_rate - Freq of arch timer in Hz
 * @hs2t - A struct holding all stage 2 telemetry data
 * @hyp_dev_tel_arr - Array holding device telemetry structures
 * @hyp_dom_tel_arr - Array holding domain telemetry structures
 */
struct hyp_shared_arm_smmu_telemetry {
	int enabled;
	int cur_s1_pgtable_usage;
	int max_s1_pgtable_usage;
	u32 arch_timer_rate;
	struct hyp_stage2_telemetry hs2t;
	struct hyp_arm_smmu_device_telemetry hyp_dev_tel_arr[MAX_SMMU_DEVICE];
	struct hyp_arm_smmu_domain_telemetry hyp_dom_tel_arr[MAX_SMMU_DOMAIN];
};

#endif /* _HYP_ARM_SMMU_V3_PKVM_COMMON_TELEMETRY_H */
