/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */
#ifndef __GOOGLE_POWERDASHBOARD_H__
#define __GOOGLE_POWERDASHBOARD_H__

#include <linux/types.h>
#include "../google_powerdashboard_iface.h"

#define MBA_CLIENT_TX_TIMEOUT 3000
#define READ_TIME 3

#define PF_STATE_CNT 24
#define TMSS_NUM_PROBES 23
#define TMSS_BUFF_SIZE 4
/* For LGA, the global timestamp counter is clocked at around 38.4 */
#define GTC_TICKS_PER_MS 38400

#define GENERATE_ENUM_SIMPLE(a, b) a,

/*
 * XMacro is a very powerful pattern
 * that enables us to enumerate the code generation for a list of values.
 * It allows us to skip the static code repetition
 */
#define LGA_SOC_POWER_STATE_X_MACRO_TABLE(X_MACRO)                    \
	X_MACRO(SOC_POWER_STATE_MISSION, mission)                 \
	X_MACRO(SOC_POWER_STATE_DORMANT, dormant)                 \
	X_MACRO(SOC_POWER_STATE_DORMANT_SUSPEND, dormant_suspend) \
	X_MACRO(SOC_POWER_STATE_AMBIENT, ambient)                 \
	X_MACRO(SOC_POWER_STATE_AMBIENT_SEC_ACC, ambient_sec_acc) \
	X_MACRO(SOC_POWER_STATE_AMBIENT_SUSPEND, ambient_suspend)

/* Firmware allocates memory for 16 kernel blockers */
#define SOC_PWR_STATE_NUM_KERNEL_BLOCKERS 16

/*
 * Xmacro is used by defining the Xmacro definition before using the table macro
 * and then using it. That creates a foreach-like mechanism
 * Don't forget to undef afterwards
 */
enum soc_power_state {
	LGA_SOC_POWER_STATE_X_MACRO_TABLE(GENERATE_ENUM_SIMPLE)
	SOC_POWER_STATE_NUM,
};

/* Thermal Data */
enum pd_thermal_throttle_level {
	PD_THERM_DVFS,
	PD_THERM_DFS,
	PD_THERM_THERM_TRIP,
	PD_THERM_LVL_NUM,
};

/* PWRBLK Power Data */
#define PWRBLK_SINGLE_LPCM 1
#define PWRBLK_DOUBLE_LPCM 2

enum pd_power_state {
	PD_POWER_STATE_OFF,
	PD_POWER_STATE_CG,
	PD_POWER_STATE_PG,
	PD_POWER_STATE_ON,
	PD_POWER_STATE_NUM,
};

/*PWRBLK, PWRBLK_NAME, PSM_CNT*/
#define LGA_PWRBLK_X_MACRO_TABLE(X_MACRO) \
	X_MACRO(AURORA, aurora, 1)        \
	X_MACRO(CODEC_3P, codec_3p, 1)    \
	X_MACRO(CPUACC, cpuacc, 1)        \
	X_MACRO(DPU, dpu, 6)              \
	X_MACRO(EH, eh, 2)                \
	X_MACRO(FABHBW, fabhbw, 1)        \
	X_MACRO(FABMED, fabmed, 1)        \
	X_MACRO(FABRT, fabrt, 1)          \
	X_MACRO(FABSTBY, fabstby, 1)      \
	X_MACRO(FABSYSS, fabsyss, 1)      \
	X_MACRO(G2D, g2d, 2)              \
	X_MACRO(GMC0, gmc0, 1)            \
	X_MACRO(GMC1, gmc1, 1)            \
	X_MACRO(GMC2, gmc2, 1)            \
	X_MACRO(GMC3, gmc3, 1)            \
	X_MACRO(GPCA, gpca, 1)            \
	X_MACRO(GPCM, gpcm, 1)            \
	X_MACRO(GPDMA, gpdma, 1)          \
	X_MACRO(GPU, gpu, 1)              \
	X_MACRO(GSA, gsa, 1)              \
	X_MACRO(GSW, gsw, 2)              \
	X_MACRO(HSIO_N, hsio_n, 8)        \
	X_MACRO(HSIO_S, hsio_s, 6)        \
	X_MACRO(INF_TCU, inf_tcu, 1)      \
	X_MACRO(ISPBE, ispbe, 2)          \
	X_MACRO(ISPFE, ispfe, 4)          \
	X_MACRO(LSIO_E, lsio_e, 2)        \
	X_MACRO(LSIO_N, lsio_n, 2)        \
	X_MACRO(LSIO_S, lsio_s, 2)        \
	X_MACRO(MEMSS, memss, 5)          \
	X_MACRO(TPU, tpu, 1)              \
	X_MACRO(CPM, cpm, 1)

#define PD_PWRBLK_ID(a) PWRBLK_##a##_ID
#define GENERATE_ENUM_PWRBLK(a, b, c) PD_PWRBLK_ID(a),
enum pd_pwrblk_id {
	LGA_PWRBLK_X_MACRO_TABLE(GENERATE_ENUM_PWRBLK) PWRBLK_NUM_IDS,
};

/* Power Domain (Rail) Data */
enum pd_rails {
	PD_RAIL_VDD_CPU,
	PD_RAIL_VDD_CPU1,
	PD_RAIL_VDD_CPU2,
	PD_RAIL_VDD_STBY,
	PD_RAIL_VDD_AMB,
	PD_RAIL_VDD_INFRA,
	PD_RAIL_VDD_MM,
	PD_RAIL_VDD_GMC,
	PD_RAIL_VDD_GPU,
	PD_RAIL_VDD_AOC,
	PD_RAIL_VDD_TPU,
	PD_RAIL_VDD_AUR,
	PD_RAIL_VDD_HSION,
	PD_RAIL_VDD_HSIOS,
	PD_RAIL_NUM,
};

/* Current Voltage Data */
enum pd_bucks_ldo {
	PD_BUCK_2M,
	PD_BUCK_3M,
	PD_BUCK_4M,
	PD_BUCK_7M,
	PD_LDO_2S,
	PD_BUCK_LDO_NUM,
};

/* APC Power Data */
enum dsu_ppu {
	PD_CORE_0,
	PD_CORE_1,
	PD_CORE_2,
	PD_CORE_3,
	PD_CORE_4,
	PD_CORE_5,
	PD_CORE_6,
	PD_CORE_7,
	PD_CLUSTER,
	PD_PPU_CNT,
};

/* CPM M55 Power */
enum pd_cpm_m55_state {
	PD_CPM_M55_STATE_ACTIVE,
	PD_CPM_M55_STATE_WFI,
	PD_CPM_M55_STATE_PG,
	PD_CPM_M55_NUM_STATES,
};

/* SoC Power State Blocker Stats */
enum pd_lpb_sswrp_ids {
	LPB_AOC_ID,
	LPB_AURORA_ID,
	LPB_CODEC_3P_ID,
	LPB_CPU_ID,
	LPB_CPUACC_ID,
	LPB_DPU_ID,
	LPB_EH_ID,
	LPB_FABHBW_ID,
	LPB_FABMED_ID,
	LPB_FABRT_ID,
	LPB_FABSTBY_ID,
	LPB_FABSYSS_ID,
	LPB_G2D_ID,
	LPB_GMC0_ID,
	LPB_GMC1_ID,
	LPB_GMC2_ID,
	LPB_GMC3_ID,
	LPB_GPCA_ID,
	LPB_GPCM_ID,
	LPB_GPDMA_ID,
	LPB_GPU_ID,
	LPB_GSA_ID,
	LPB_GSW_ID,
	LPB_HSIO_N_ID,
	LPB_HSIO_S_ID,
	LPB_INF_TCU_ID,
	LPB_ISPBE_ID,
	LPB_ISPFE_ID,
	LPB_LSIO_E_ID,
	LPB_LSIO_N_ID,
	LPB_LSIO_S_ID,
	LPB_MEMSS_ID,
	LPB_TPU_ID,
	LPB_NUM_SSWRPS,
};

enum pd_soc_power_state_pre_blocker_lpb_id {
	SOC_POWER_PRE_FABSTBY_AOC_ID,
	SOC_POWER_PRE_FABSTBY_GSA_ID,
	SOC_POWER_PRE_DRAM_AOC_ID,
	SOC_POWER_PRE_DRAM_GSA_ID,
	SOC_POWER_PRE_LPB_AURORA_ID,
	SOC_POWER_PRE_LPB_CODEC_3P_ID,
	SOC_POWER_PRE_LPB_DPU_ID,
	SOC_POWER_PRE_LPB_EH_ID,
	SOC_POWER_PRE_LPB_G2D_ID,
	SOC_POWER_PRE_LPB_GPDMA_ID,
	SOC_POWER_PRE_LPB_GPU_ID,
	SOC_POWER_PRE_LPB_GSW_ID,
	SOC_POWER_PRE_LPB_HSIO_N_ID,
	SOC_POWER_PRE_LPB_HSIO_S_ID,
	SOC_POWER_PRE_LPB_ISPBE_ID,
	SOC_POWER_PRE_LPB_ISPFE_ID,
	SOC_POWER_PRE_LPB_LSIO_E_ID,
	SOC_POWER_PRE_LPB_LSIO_N_ID,
	SOC_POWER_PRE_LPB_LSIO_S_ID,
	SOC_POWER_PRE_LPB_TPU_ID,
	SOC_POWER_PRE_LPB_NUM_SSWRPS,
};

enum blocker_modes {
	BLOCKER_SSWRP,
	BLOCKER_FABSTBY_VOTE,
	BLOCKER_DRAM_VOTE,
};

#define LGA_LPCM_X_MACRO_TABLE(X_MACRO) \
	X_MACRO(CODEC_3P, codec_3p)     \
	X_MACRO(CPUACC, cpuacc)         \
	X_MACRO(DPU, dpu)               \
	X_MACRO(EH, eh)                 \
	X_MACRO(FABHBW, fabhbw)         \
	X_MACRO(FABMED, fabmed)         \
	X_MACRO(FABRT, fabrt)           \
	X_MACRO(FABSTBY, fabstby)       \
	X_MACRO(FABSYSS, fabsyss)       \
	X_MACRO(G2D, g2d)               \
	X_MACRO(GMC0, gmc0)             \
	X_MACRO(GMC1, gmc1)             \
	X_MACRO(GMC2, gmc2)             \
	X_MACRO(GMC3, gmc3)             \
	X_MACRO(GPCA, gpca)             \
	X_MACRO(GPCM, gpcm)             \
	X_MACRO(GPDMA, gpdma)           \
	X_MACRO(GSA, gsa)               \
	X_MACRO(GSW, gsw)               \
	X_MACRO(HSIO_N, hsio_n)         \
	X_MACRO(HSIO_S, hsio_s)         \
	X_MACRO(INF_TCU, inf_tcu)       \
	X_MACRO(ISPBE, ispbe)           \
	X_MACRO(ISPFE, ispfe)           \
	X_MACRO(LSIO_E, lsio_e)         \
	X_MACRO(LSIO_N, lsio_n)         \
	X_MACRO(LSIO_S, lsio_s)         \
	X_MACRO(MEMSS, memss)           \
	X_MACRO(CPM, cpm)

#define PD_LPCM_ID(a) LPCM_##a##_ID
#define GENERATE_ENUM_LPCM(a, b) PD_LPCM_ID(a),
enum pd_lpcm_id { LGA_LPCM_X_MACRO_TABLE(GENERATE_ENUM_LPCM) LPCM_NUM_SSWRPS };

/* SOC Specific Sections */

struct pd_power_state_blocker_stats {
	u64 last_blocked_ts;
	u64 time_blocked;
	u32 blocked_count;
	u32 is_currently_blocking;
} __packed;

struct pd_power_state_sswrp_blockers {
	u64 total_blocked_count;
	u64 total_time_blocked;
	struct pd_power_state_blocker_stats blocker_stats[SOC_POWER_PRE_LPB_NUM_SSWRPS];
} __packed;

struct pd_power_state_kernel_blockers {
	u64 total_blocked_count;
	struct pd_power_state_blocker_stats blocker_stats[SOC_PWR_STATE_NUM_KERNEL_BLOCKERS];
} __packed;

struct pd_power_state_blockers_section {
	struct pd_section_header header;
	u32 _reserved;
	struct pd_power_state_kernel_blockers kernel_blockers;
	struct pd_power_state_sswrp_blockers sswrp_blockers[SOC_POWER_STATE_NUM];
} __packed;

struct pd_fabric_acg_apg_res {
	u32 fabmed_acc_val_acg;
	u32 fabrt_acc_val_acg;
	u32 fabstby_acc_val_acg;
	u32 fabsyss_acc_val_acg;
	u32 fabhbw_acc_val_acg;
	u32 fabmem_acc_val_acg;
	u32 gslc01_acc_val_acg;
	u32 gslc01_acc_val_apg;
	u32 gslc23_acc_val_acg;
	u32 gslc23_acc_val_apg;
} __packed;

struct pd_gmc_acg_apg_res {
	u32 gmc_acc_val_acg;
	u32 gmc_acc_val_apg;
} __packed;

struct pd_acg_apg_csr_res_section {
	struct pd_section_header header;
	struct pd_fabric_acg_apg_res *fabric_acg_apg_res;
	struct pd_gmc_acg_apg_res *gmc_acg_apg_res;
} __packed;

#endif // __GOOGLE_POWERDASHBOARD_H__
