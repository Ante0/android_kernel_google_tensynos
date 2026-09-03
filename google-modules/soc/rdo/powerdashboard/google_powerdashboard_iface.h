/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */
#ifndef __GOOGLE_POWERDASHBOARD_IFACE_H__
#define __GOOGLE_POWERDASHBOARD_IFACE_H__

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/dcache.h>

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
#include <soc/google/goog_mba_cpm_iface.h>
#else
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#endif

#define HEADER_OFFSET 12

#if IS_ENABLED(CONFIG_SOC_LGA)
enum pd_section {
	PD_THERMAL,
	PD_PLATFORM_POWER,
	PD_PWRBLK,
	PD_LPCM,
	PD_RAIL,
	PD_CLAVS,
	PD_CURR_VOLT,
	PD_APC_POWER,
	PD_CPM_M55,
	PD_POWER_STATE_BLOCKERS,
	PD_ACG_APG_CSR_RES,
	PD_VOTE,
	PD_SECTION_NUM,
};
#define PD_OLD_SECTION_NUM PD_SECTION_NUM
#else
enum pd_section {
	PD_PLATFORM_POWER,
	PD_PWRBLK,
	PD_LPCM,
	PD_APC_POWER,
	PD_CPM_M55,
	PD_POWER_STATE_BLOCKERS,
	PD_VOTE,
	PD_ACG_APG_CSR_RES,
	PD_SECTION_NUM,
};
#endif

enum powerdashboard_mba_mgs_type {
	POWER_DASH_GET_SECT_ADDR,
	POWER_DASH_SET_SAMPLING_RATE,
	POWER_DASH_START_SAMPLING,
	POWER_DASH_STOP_SAMPLING,
	POWER_DASH_FORCE_REFRESH,
};

/* Generic container for residency */
struct pd_residency_state {
#if IS_ENABLED(CONFIG_SOC_LGA)
	u64 entry_count;
#else
	u32 entry_count;
#endif
	u64 time_in_state;
} __packed;

/* Generic container for residency with entry/exit timestamps */
struct pd_residency_time_state {
#if IS_ENABLED(CONFIG_SOC_LGA)
	u64 entry_count;
#else
	u32 entry_count;
#endif
	u64 time_in_state;
	u64 last_entry_ts;
	u64 last_exit_ts;
} __packed;

#define HEADER_OFFSET 12
struct pd_section_header {
	u64 size;
	u32 version;
} __packed;
static_assert(sizeof(struct pd_section_header) == HEADER_OFFSET);

struct pd_attr_str {
	const char *const *const values;
	size_t count;
};

struct pd_attr_u8 {
	const u8 *values;
	size_t count;
};

struct pd_pwrblk_power_state {
	struct pd_residency_time_state *power_state_res;
	u8 curr_power_state;
} __packed;

#if IS_ENABLED(CONFIG_SOC_LGA)
struct pd_lpcm_res {
	u8 curr_pf_state;
	u8 num_pf_states;
	struct pd_residency_state *pf_state_res;
} __packed;
#else
struct pd_lpcm_res {
	u8 pwrblk;
	u8 curr_pf_state;
	u8 num_pf_states;
	struct pd_residency_time_state *pf_state_res;
} __packed;
#endif

struct pd_buck_ldo_data {
	u32 pre_ocp_count;
	u32 soft_ocp_count;
} __packed;

struct pd_thermal_section {
	struct pd_section_header header;
	struct pd_residency_state *thermal_residency;
	u32 *tmss_data;
} __packed;

struct pd_platform_power_section {
	struct pd_section_header header;
	struct pd_residency_time_state *plat_power_res;
	u8 curr_power_state;
} __packed;

struct pd_pwrblk_section {
	struct pd_section_header header;
	struct pd_pwrblk_power_state **pwrblk_power_states;
} __packed;

struct pd_lpcm_section {
	struct pd_section_header header;
	struct pd_lpcm_res **lpcm_residencies;
} __packed;

struct pd_rail_section {
	struct pd_section_header header;
	u32 *curr_voltage;
} __packed;

struct pd_clavs_section {
	struct pd_section_header header;
	u16 gpu_current;
} __packed;

struct pd_curr_volt_section {
	struct pd_section_header header;
	u32 vsys_droop_count; /* (battery level pre-uvlo) */
	struct pd_buck_ldo_data *buck_ldo_data;
} __packed;

struct pd_apc_power_section {
	struct pd_section_header header;
	u32 *ppu;
} __packed;

struct pd_cpm_m55_power_section {
	struct pd_section_header header;
	struct pd_residency_time_state *power_state_res;
	u8 current_state;
} __packed;

struct pd_vote_section {
	struct pd_section_header header;
	struct pd_residency_time_state *vote_res;
} __packed;

typedef void (*section_copy_func)(void *);

struct google_powerdashboard_sections {
	struct pd_thermal_section *thermal_section;
	struct pd_platform_power_section *platform_power_section;
	struct pd_pwrblk_section *pwrblk_section;
	struct pd_lpcm_section *lpcm_section;
	struct pd_rail_section *rail_section;
	struct pd_clavs_section *clavs_section;
	struct pd_curr_volt_section *curr_volt_section;
	struct pd_apc_power_section *apc_power_section;
	struct pd_cpm_m55_power_section *cpm_m55_power_section;
	struct pd_vote_section *vote_section;

	/* The sections that have truly different underlying type between devices.
	 * Needs to be polymorphic.
	 */
	void *power_state_blockers_section;
	void *acg_apg_csr_res_section;

	const void **section_ptrs;

	void __iomem **section_bases;
	size_t *section_sizes;

	section_copy_func *section_copy_funcs;
};

struct google_powerdashboard_constants {
	u16 mba_client_tx_timeout;
	u16 gtc_ticks_per_ms;
	u8 read_time;
	u8 pf_state_cnt;
	u8 tmss_num_probes;
	u8 tmss_buff_size;
	u8 soc_pwr_state_num_kernel_blockers;
	u8 ip_idle_idx_num;
	u8 soc_power_state_dormant_suspend;
};

struct google_powerdashboard_attributes {
	const struct pd_attr_str *thermal_throttle_names;
	const struct pd_attr_str *platform_power_state_names;
	const struct pd_attr_str *pwrblk_power_state_names;
	const struct pd_attr_str *rail_names;
	const struct pd_attr_str *bucks_ldo_names;
	const struct pd_attr_str *apc_power_ppu_names;
	const struct pd_attr_str *cpm_m55_power_state_names;
	const struct pd_attr_str *sswrp_names;
	const struct pd_attr_str *ip_idle_id_names;
	const struct pd_attr_str *blocker_client_names;
	const struct pd_attr_str *blocker_names;
	const struct pd_attr_str *voter_names;
	const struct pd_attr_str *voted_resource_names;
	const struct pd_attr_u8 *precondition_blocker_lpb_ids;
};

struct google_powerdashboard_iface {
	struct google_powerdashboard_sections sections;
	const struct google_powerdashboard_attributes attrs;
};

extern const struct google_powerdashboard_iface powerdashboard_iface;
extern const struct google_powerdashboard_constants powerdashboard_constants;
extern struct attribute *google_powerdashboard_sswrp_attrs[];
extern struct attribute *google_powerdashboard_power_state_attrs[];
extern struct attribute *google_powerdashboard_soc_specific_attrs[];

struct google_powerdashboard {
	struct device *dev;
	struct dentry *debugfs_root;
#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
	struct cpm_iface_client *client;
#else
	struct mbox_client client;
	struct mbox_chan *channel;
#endif
	u32 remote_ch;

	struct mutex config_lock; /* configuration lock */
	u32 sampling_rate; /* in ms */

	bool cpm_sampling;
};

#endif // __GOOGLE_POWERDASHBOARD_IFACE_H__
