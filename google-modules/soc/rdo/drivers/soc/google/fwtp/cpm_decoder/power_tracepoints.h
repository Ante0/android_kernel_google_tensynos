/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Google LLC */

#ifndef _POWER_TRACEPOINTS_H
#define _POWER_TRACEPOINTS_H

#include "cpm_tracepoint_decoder.h"

#define POWER_TRACEPOINTS_LIST(X)                                              \
	X(syspm_ev_func_call_start, "spmEvS func_call %d",                     \
	  power_payload_handler)                                               \
	X(syspm_ev_func_call_end, "spmEvE func_call %d",                       \
	  power_payload_handler)                                               \
	X(syspm_ev_trans_start, "spmEvS transition %d", power_payload_handler) \
	X(syspm_ev_trans_end, "spmEvE transition %d", power_payload_handler)   \
	X(syspm_ev_res_fsm_req_start, "spmEvS res_fsm_req %d",                 \
	  power_payload_handler)                                               \
	X(syspm_ev_res_fsm_req_end, "spmEvE res_fsm_req %d",                   \
	  power_payload_handler)                                               \
	X(syspm_ev_vote_added_hook_start, "spmEvS vote_added_hook %d",         \
	  power_payload_handler)                                               \
	X(syspm_ev_vote_added_hook_end, "spmEvE vote_added_hook %d",           \
	  power_payload_handler)                                               \
	X(syspm_ev_sync_event_hook_start, "spmEvS sync_event_hook %d",         \
	  power_payload_handler)                                               \
	X(syspm_ev_sync_event_hook_end, "spmEvE sync_event_hook %d",           \
	  power_payload_handler)                                               \
	X(syspm_ev_res_driver_req_start, "spmEvS res_driver_req %d",           \
	  power_payload_handler)                                               \
	X(syspm_ev_res_driver_req_end, "spmEvE res_driver_req %d",             \
	  power_payload_handler)                                               \
	X(syspm_ev_fsm_pre_start, "spmEvS fsm_pre %d", power_payload_handler)  \
	X(syspm_ev_fsm_pre_end, "spmEvE fsm_pre %d", power_payload_handler)    \
	X(syspm_ev_fsm_execute_start, "spmEvS fsm_execute %d",                 \
	  power_payload_handler)                                               \
	X(syspm_ev_fsm_execute_end, "spmEvE fsm_execute %d",                   \
	  power_payload_handler)                                               \
	X(syspm_ev_fsm_post_start, "spmEvS fsm_post %d",                       \
	  power_payload_handler)                                               \
	X(syspm_ev_fsm_post_end, "spmEvE fsm_post %d", power_payload_handler)  \
	X(syspm_ev_fsm_complete_start, "spmEvS fsm_complete %d",               \
	  power_payload_handler)                                               \
	X(syspm_ev_fsm_complete_end, "spmEvE fsm_complete %d",                 \
	  power_payload_handler)                                               \
	X(syspm_ev_lpb_set_on_start, "spmEvS lpb_set_sswrp_on %d",             \
	  power_payload_handler)                                               \
	X(syspm_ev_lpb_set_on_end, "spmEvE lpb_set_sswrp_on %d",               \
	  power_payload_handler)                                               \
	X(syspm_ev_update_rail_state_start, "spmEvS update_rail_state %d",     \
	  power_payload_handler)                                               \
	X(syspm_ev_update_rail_state_end, "spmEvE update_rail_state %d",       \
	  power_payload_handler)                                               \
	X(syspm_ev_writel_set_sswrp_on_start, "spmEvS writel_set_sswrp_on %d", \
	  power_payload_handler)                                               \
	X(syspm_ev_writel_set_sswrp_on_end, "spmEvE writel_set_sswrp_on %d",   \
	  power_payload_handler)                                               \
	X(syspm_ev_writel_set_sswrp_off_start,                                 \
	  "spmEvS writel_set_sswrp_off %d", power_payload_handler)             \
	X(syspm_ev_writel_set_sswrp_off_end, "spmEvE writel_set_sswrp_off %d", \
	  power_payload_handler)                                               \
	X(syspm_ev_lpb_vote_volt_mgr_rail_start,                               \
	  "spmEvS lpb_vote_volt_mgr_rail %d", power_payload_handler)           \
	X(syspm_ev_lpb_vote_volt_mgr_rail_end,                                 \
	  "spmEvE lpb_vote_volt_mgr_rail %d", power_payload_handler)           \
	X(syspm_ev_volt_mgr_lpb_vote_rail_start,                               \
	  "spmEvS volt_mgr_lpb_vote_rail %d", power_payload_handler)           \
	X(syspm_ev_volt_mgr_lpb_vote_rail_end,                                 \
	  "spmEvE volt_mgr_lpb_vote_rail %d", power_payload_handler)           \
	X(syspm_ev_sre_restore_ac_start, "spmEvS sre_restore_ac %d",           \
	  power_payload_handler)                                               \
	X(syspm_ev_sre_restore_ac_end, "spmEvE sre_restore_ac %d",             \
	  power_payload_handler)                                               \
	X(syspm_ev_sre_restore_post_interrupt_start,                           \
	  "spmEvS sre_restore_post_interrupt %d", power_payload_handler)       \
	X(syspm_ev_sre_restore_post_interrupt_end,                             \
	  "spmEvE sre_restore_post_interrupt %d", power_payload_handler)       \
	X(syspm_ev_lpcm_apply_sswrp_patches_start,                             \
	  "spmEvS lpcm_apply_sswrp_patches %d", power_payload_handler)         \
	X(syspm_ev_lpcm_apply_sswrp_patches_end,                               \
	  "spmEvE lpcm_apply_sswrp_patches %d", power_payload_handler)         \
	X(syspm_ev_hw_mitigation_platform_init_start,                          \
	  "spmEvS hw_mitigation_platform_init %d", power_payload_handler)      \
	X(syspm_ev_hw_mitigation_platform_init_end,                            \
	  "spmEvE hw_mitigation_platform_init %d", power_payload_handler)      \
	X(syspm_ev_cpm_init_sswrp_tunables_start,                              \
	  "spmEvS cpm_init_sswrp_tunables %d", power_payload_handler)          \
	X(syspm_ev_cpm_init_sswrp_tunables_end,                                \
	  "spmEvE cpm_init_sswrp_tunables %d", power_payload_handler)          \
	X(syspm_ev_writel_cpm_init_sswrp_tunables_start,                       \
	  "spmEvS writel_cpm_init_sswrp_tunables %d", power_payload_handler)   \
	X(syspm_ev_writel_cpm_init_sswrp_tunables_end,                         \
	  "spmEvE writel_cpm_init_sswrp_tunables %d", power_payload_handler)   \
	X(syspm_ev_lpcm_resume_residency_start,                                \
	  "spmEvS lpcm_resume_residency %d", power_payload_handler)            \
	X(syspm_ev_lpcm_resume_residency_end,                                  \
	  "spmEvE lpcm_resume_residency %d", power_payload_handler)            \
	X(syspm_ev_lpcm_resume_residency_tracking_start,                       \
	  "spmEvS lpcm_resume_residency_tracking %d", power_payload_handler)   \
	X(syspm_ev_lpcm_resume_residency_tracking_end,                         \
	  "spmEvE lpcm_resume_residency_tracking %d", power_payload_handler)   \
	X(syspm_ev_lpcm_reset_counter_tracking_start,                          \
	  "spmEvS lpcm_reset_counter_tracking %d", power_payload_handler)      \
	X(syspm_ev_lpcm_reset_counter_tracking_end,                            \
	  "spmEvE lpcm_reset_counter_tracking %d", power_payload_handler)      \
	X(syspm_ev_restore_sswrp_pf_state_start,                               \
	  "spmEvS restore_sswrp_pf_state %d", power_payload_handler)           \
	X(syspm_ev_restore_sswrp_pf_state_end,                                 \
	  "spmEvE restore_sswrp_pf_state %d", power_payload_handler)           \
	X(syspm_ev_lpcm_get_cached_op_lvl_start,                               \
	  "spmEvS lpcm_get_cached_op_lvl %d", power_payload_handler)           \
	X(syspm_ev_lpcm_get_cached_op_lvl_end,                                 \
	  "spmEvE lpcm_get_cached_op_lvl %d", power_payload_handler)           \
	X(syspm_ev_lpcm_get_cached_min_max_op_lvl_start,                       \
	  "spmEvS lpcm_get_cached_min_max_op_lvl %d", power_payload_handler)   \
	X(syspm_ev_lpcm_get_cached_min_max_op_lvl_end,                         \
	  "spmEvE lpcm_get_cached_min_max_op_lvl %d", power_payload_handler)   \
	X(syspm_ev_lpcm_set_op_lvl_sync_start,                                 \
	  "spmEvS lpcm_set_op_lvl_sync %d", power_payload_handler)             \
	X(syspm_ev_lpcm_set_op_lvl_sync_end, "spmEvE lpcm_set_op_lvl_sync %d", \
	  power_payload_handler)                                               \
	X(syspm_ev_lpcm_set_op_clamp_sync_start,                               \
	  "spmEvS lpcm_set_op_clamp_sync %d", power_payload_handler)           \
	X(syspm_ev_lpcm_set_op_clamp_sync_end,                                 \
	  "spmEvE lpcm_set_op_clamp_sync %d", power_payload_handler)           \
	X(syspm_ev_platform_on_event_fixes_start,                              \
	  "spmEvS platform_on_event_fixes %d", power_payload_handler)          \
	X(syspm_ev_platform_on_event_fixes_end,                                \
	  "spmEvE platform_on_event_fixes %d", power_payload_handler)          \
	X(syspm_ev_lpb_ipc_publish_event_hook_start,                           \
	  "spmEvS lpb_ipc_publish_event_hook %d", power_payload_handler)       \
	X(syspm_ev_lpb_ipc_publish_event_hook_end,                             \
	  "spmEvE lpb_ipc_publish_event_hook %d", power_payload_handler)       \
	X(syspm_ev_platform_off_event_fixes_start,                             \
	  "spmEvS platform_off_event_fixes %d", power_payload_handler)         \
	X(syspm_ev_platform_off_event_fixes_end,                               \
	  "spmEvE platform_off_event_fixes %d", power_payload_handler)         \
	X(syspm_ev_reset_lpcm_trigger_ready_interrupts_start,                  \
	  "spmEvS reset_lpcm_trigger_ready_interrupts %d",                     \
	  power_payload_handler)                                               \
	X(syspm_ev_reset_lpcm_trigger_ready_interrupts_end,                    \
	  "spmEvE reset_lpcm_trigger_ready_interrupts %d",                     \
	  power_payload_handler)

#define DECLARE_POWER_TRACEPOINT_EXTERN(name, ...) \
	extern struct client_tracepoint name;

POWER_TRACEPOINTS_LIST(DECLARE_POWER_TRACEPOINT_EXTERN)

#undef DECLARE_POWER_TRACEPOINT_EXTERN

#endif /* _POWER_TRACEPOINTS_H */
