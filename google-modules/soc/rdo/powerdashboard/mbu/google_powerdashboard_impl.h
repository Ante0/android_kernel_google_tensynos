/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */
#ifndef __GOOGLE_POWERDASHBOARD_H__
#define __GOOGLE_POWERDASHBOARD_H__

#include "../google_powerdashboard_iface.h"

#define MBA_CLIENT_TX_TIMEOUT 3000
#define READ_TIME 3

#define MEDIUM_PF_STATE_CNT 16
#define LARGE_PF_STATE_CNT 24

/* For MBU, the global timestamp counter is clocked at around 38.4 */
#define GTC_TICKS_PER_MS 38400

/*
 * XMacro is a very powerful pattern
 * that enables us to enumerate the code generation for a list of values.
 * It allows us to skip the static code repetition
 */
#define MBU_SOC_POWER_STATE_X_MACRO_TABLE(X_MACRO)           \
	X_MACRO(SOC_POWER_STATE_MISSION, mission)                 \
	X_MACRO(SOC_POWER_STATE_DORMANT, dormant)                 \
	X_MACRO(SOC_POWER_STATE_DORMANT_SUSPEND, dormant_suspend) \
	X_MACRO(SOC_POWER_STATE_AMBIENT_MC_ON, ambient_mc_on)                 \
	X_MACRO(SOC_POWER_STATE_AMBIENT_OD_AF, ambient_od_af)           \
	X_MACRO(SOC_POWER_STATE_AMBIENT_SUSPEND, ambient_suspend)

#define GENERATE_ENUM_SIMPLE(a, b) a,
/*
 * Xmacro is used by defining the Xmacro definition before using the table macro
 * and then using it. That creates a foreach-like mechanism
 * Don't forget to undef afterwards
 */
enum soc_power_state {
	MBU_SOC_POWER_STATE_X_MACRO_TABLE(GENERATE_ENUM_SIMPLE)
	SOC_POWER_STATE_NUM,
};

/* Thermal Data */
enum pd_thermal_throttle_level {
	PD_THERM_DVFS,
	PD_THERM_DFS,
	PD_THERM_THERM_TRIP,
	PD_THERM_LVL_NUM,
};

enum pd_power_state {
	PD_POWER_STATE_OFF,
	PD_POWER_STATE_ON,
	PD_POWER_STATE_NUM,
};

/*PWRBLK, PWRBLK_NAME, PSM_CNT*/
#define MBU_PWRBLK_X_MACRO_TABLE(X_MACRO)   \
	X_MACRO(AOSS_AMBSS, aoss_ambss, 1) \
	X_MACRO(AOSS_AONSS, aoss_aonss, 1) \
	X_MACRO(AOSS_PG, aoss_pg, 1)       \
	X_MACRO(AURDSP, aurdsp, 1)         \
	X_MACRO(BMSM, bmsm, 1)             \
	X_MACRO(CODEC_3P, codec_3p, 1)     \
	X_MACRO(CPUACC, cpuacc, 1)         \
	X_MACRO(DPU, dpu, 6)               \
	X_MACRO(FABDISP, fabdisp, 1)       \
	X_MACRO(FABHBW, fabhbw, 1)         \
	X_MACRO(FABMED, fabmed, 1)         \
	X_MACRO(FABSTBY, fabstby, 1)       \
	X_MACRO(FABSYSS, fabsyss, 1)       \
	X_MACRO(G2D, g2d, 4)               \
	X_MACRO(GCV, gcv, 1)               \
	X_MACRO(GMC0, gmc0, 1)             \
	X_MACRO(GMC1, gmc1, 1)             \
	X_MACRO(GMC2, gmc2, 1)             \
	X_MACRO(GMC3, gmc3, 1)             \
	X_MACRO(GPCA, gpca, 1)             \
	X_MACRO(GPCM_AMB, gpcm_amb, 1)     \
	X_MACRO(GPCM_INFRA, gpcm_infra, 1) \
	X_MACRO(GPU, gpu, 1)               \
	X_MACRO(GSA, gsa, 1)               \
	X_MACRO(HSIO_N, hsio_n, 3)         \
	X_MACRO(HSIO_S, hsio_s, 3)         \
	X_MACRO(ISPBE, ispbe, 4)           \
	X_MACRO(ISPFE, ispfe, 3)           \
	X_MACRO(LSIO_E, lsio_e, 4)         \
	X_MACRO(LSIO_S, lsio_s, 4)         \
	X_MACRO(MEMSS, memss, 5)           \
	X_MACRO(PCIE, pcie, 1)             \
	X_MACRO(TPU, tpu, 1)

#define PD_PWRBLK_ID(a) PWRBLK_##a##_ID
#define GENERATE_ENUM_PWRBLK(a, b, c) PD_PWRBLK_ID(a),
enum pd_pwrblk_id {
	MBU_PWRBLK_X_MACRO_TABLE(GENERATE_ENUM_PWRBLK) PWRBLK_NUM_IDS,
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

enum pd_lpb_sswrp_ids {
	LPB_AOSS_AMBSS_ID,
	LPB_AOSS_AONSS_ID,
	LPB_AOSS_PG_ID,
	LPB_AURDSP_ID,
	LPB_BMSM_ID,
	LPB_CODEC_3P_ID,
	LPB_CPU_ID,
	LPB_CPUACC_ID,
	LPB_DPU_ID,
	LPB_FABDISP_ID,
	LPB_FABHBW_ID,
	LPB_FABMED_ID,
	LPB_FABSTBY_ID,
	LPB_FABSYSS_ID,
	LPB_G2D_ID,
	LPB_GCV_ID,
	LPB_GMC0_ID,
	LPB_GMC1_ID,
	LPB_GMC2_ID,
	LPB_GMC3_ID,
	LPB_GPCA_ID,
	LPB_GPCM_AMB_ID,
	LPB_GPCM_INFRA_ID,
	LPB_GPU_ID,
	LPB_GSA_ID,
	LPB_HSIO_N_ID,
	LPB_HSIO_S_ID,
	LPB_ISPBE_ID,
	LPB_ISPFE_ID,
	LPB_LSIO_E_ID,
	LPB_LSIO_S_ID,
	LPB_MEMSS_ID,
	LPB_PCIE_ID,
	LPB_TPU_ID,
	LPB_NUM_SSWRPS,
};

/*LPCM, LPCM_NAME */
#define MBU_MEDIUM_LPCM_X_MACRO_TABLE(X_MACRO)    \
	X_MACRO(GSM, gsm)               \
	X_MACRO(AOSS_AONSS, aoss_aonss) \
	X_MACRO(AOSS_AMBSS, aoss_ambss) \
	X_MACRO(PCIE, pcie)             \
	X_MACRO(FABDISP, fabdisp)       \
	X_MACRO(FABHBW, fabhbw)         \
	X_MACRO(MEMSS, memss)           \
	X_MACRO(FABMED, fabmed)         \
	X_MACRO(FABSTBY, fabstby)       \
	X_MACRO(FABSYSS, fabsyss)       \
	X_MACRO(CODEC_3P, codec_3p)     \
	X_MACRO(G2D, g2d)               \
	X_MACRO(GCV, gcv)               \
	X_MACRO(ISPBE, ispbe)           \
	X_MACRO(ISPFE, ispfe)           \
	X_MACRO(CPUACC, cpuacc)         \
	X_MACRO(DPU, dpu)               \
	X_MACRO(GMC0, gmc0)             \
	X_MACRO(GPCM_AMB, gpcm_amb)     \
	X_MACRO(GPCM_INFRA, gpcm_infra) \
	X_MACRO(GPU, gpu)               \
	X_MACRO(TPU, tpu)               \
	X_MACRO(AURDSP, aurdsp)

#define MBU_LARGE_LPCM_X_MACRO_TABLE(X_MACRO)    \
	X_MACRO(CPU0, cpu0)                  \
	X_MACRO(CPU1, cpu1)                  \
	X_MACRO(CPU2, cpu2)                  \
	X_MACRO(DSU, dsu)                    \

#define PD_LPCM_ID(a) LPCM_##a##_ID
#define GENERATE_ENUM_LPCM(a, b) PD_LPCM_ID(a),
enum pd_lpcm_id { MBU_MEDIUM_LPCM_X_MACRO_TABLE(GENERATE_ENUM_LPCM) \
	MBU_LARGE_LPCM_X_MACRO_TABLE(GENERATE_ENUM_LPCM) LPCM_NUM_SSWRPS };

#define PD_LARGE_LPCM_ID(a) LARGE_LPCM_##a##_ID
#define GENERATE_ENUM_LARGE_LPCM(a, b) PD_LARGE_LPCM_ID(a),
enum pd_large_lpcm_id { MBU_LARGE_LPCM_X_MACRO_TABLE(GENERATE_ENUM_LARGE_LPCM) LARGE_LPCM_NUM_SSWRPS };

/* SOC Specific Sections */
enum pd_blocker_triplet {
	PD_BLOCKER_AMB_OD_AF_AOSS_AOSS_AMBSS,
	PD_BLOCKER_AMB_OD_AF_AOSS_AOSS_PG,
	PD_BLOCKER_AMB_OD_AF_AOSS_MC_ON,
	PD_BLOCKER_AMB_OD_AF_AOSS_FABSTBY,
	PD_BLOCKER_AMB_OD_AF_AOSS_PCIE,
	PD_BLOCKER_AMB_OD_AF_GSA_GSA,
	PD_BLOCKER_AMB_OD_AF_GSA_AURDSP,
	PD_BLOCKER_AMB_OD_AF_GSA_BMSM,
	PD_BLOCKER_AMB_OD_AF_GSA_MC_ON,
	PD_BLOCKER_AMB_OD_AF_GSA_TPU,
	PD_BLOCKER_AMB_SUS_AOSS_AOSS_AMBSS,
	PD_BLOCKER_AMB_SUS_AOSS_AOSS_PG,
	PD_BLOCKER_AMB_SUS_AOSS_MC_ON,
	PD_BLOCKER_AMB_SUS_AOSS_FABSTBY,
	PD_BLOCKER_AMB_SUS_AOSS_PCIE,
	PD_BLOCKER_AMB_SUS_GSA_GSA,
	PD_BLOCKER_AMB_SUS_GSA_AURDSP,
	PD_BLOCKER_AMB_SUS_GSA_BMSM,
	PD_BLOCKER_AMB_SUS_GSA_MC_ON,
	PD_BLOCKER_AMB_SUS_GSA_TPU,
	PD_BLOCKER_AMB_MC_ON_AOSS_AOSS_AMBSS,
	PD_BLOCKER_AMB_MC_ON_AOSS_AOSS_PG,
	PD_BLOCKER_AMB_MC_ON_AOSS_MC_ON,
	PD_BLOCKER_AMB_MC_ON_AOSS_FABSTBY,
	PD_BLOCKER_AMB_MC_ON_AOSS_PCIE,
	PD_BLOCKER_AMB_MC_ON_GSA_GSA,
	PD_BLOCKER_AMB_MC_ON_GSA_AURDSP,
	PD_BLOCKER_AMB_MC_ON_GSA_BMSM,
	PD_BLOCKER_AMB_MC_ON_GSA_MC_ON,
	PD_BLOCKER_AMB_MC_ON_GSA_TPU,
	PD_BLOCKER_DORM_SUS_AOSS_AOSS_AMBSS,
	PD_BLOCKER_DORM_SUS_AOSS_AOSS_PG,
	PD_BLOCKER_DORM_SUS_AOSS_MC_ON,
	PD_BLOCKER_DORM_SUS_AOSS_FABSTBY,
	PD_BLOCKER_DORM_SUS_AOSS_PCIE,
	PD_BLOCKER_DORM_SUS_GSA_GSA,
	PD_BLOCKER_DORM_SUS_GSA_AURDSP,
	PD_BLOCKER_DORM_SUS_GSA_BMSM,
	PD_BLOCKER_DORM_SUS_GSA_MC_ON,
	PD_BLOCKER_DORM_SUS_GSA_TPU,
	PD_BLOCKER_DORM_SUS_VM1_AURDSP,
	PD_BLOCKER_DORM_SUS_VM1_CODEC_3P,
	PD_BLOCKER_DORM_SUS_VM1_DPU,
	PD_BLOCKER_DORM_SUS_VM1_G2D,
	PD_BLOCKER_DORM_SUS_VM1_GCV,
	PD_BLOCKER_DORM_SUS_VM1_GPCM_AMB,
	PD_BLOCKER_DORM_SUS_VM1_GPU,
	PD_BLOCKER_DORM_SUS_VM1_HSIO_N,
	PD_BLOCKER_DORM_SUS_VM1_HSIO_S,
	PD_BLOCKER_DORM_SUS_VM1_ISPBE,
	PD_BLOCKER_DORM_SUS_VM1_ISPFE,
	PD_BLOCKER_DORM_SUS_VM1_LSIO_E,
	PD_BLOCKER_DORM_SUS_VM1_LSIO_S,
	PD_BLOCKER_DORM_SUS_VM1_PCIE,
	PD_BLOCKER_DORM_SUS_VM1_TPU,
	PD_BLOCKER_DORM_SUS_VM1_AOSS_PG,
	PD_BLOCKER_DORM_SUS_VM1_AOSS_AMBSS,
	PD_BLOCKER_COUNT
};

enum pd_blocker_client {
	PD_BLOCKER_CLIENT_AOSS,
	PD_BLOCKER_CLIENT_CPM,
	PD_BLOCKER_CLIENT_DPA,
	PD_BLOCKER_CLIENT_GDMC,
	PD_BLOCKER_CLIENT_CPUSS,
	PD_BLOCKER_CLIENT_GSA,
	PD_BLOCKER_CLIENT_HYP,
	PD_BLOCKER_CLIENT_THERMAL_MEAS,
	PD_BLOCKER_CLIENT_THERMAL_SVC,
	PD_BLOCKER_CLIENT_TZ,
	PD_BLOCKER_CLIENT_VM1,
	PD_BLOCKER_CLIENT_VM2,
	PD_BLOCKER_CLIENT_VM3,
	PD_BLOCKER_CLIENT_VM4,
	PD_BLOCKER_CLIENT_POWER_DASH,
	PD_BLOCKER_CLIENT_DVFSMON,
	PD_BLOCKER_CLIENT_CPM_TP,
	PD_BLOCKER_CLIENT_TEST,
	PD_BLOCKER_CLIENT_COUNT
};

struct pd_power_state_blocker_stats {
	u64 last_blocked_ts;
	u64 time_blocked;
	bool is_currently_blocking;
} __packed;

struct pd_power_state_blockers_section {
	struct pd_section_header header;
	u64 total_time_blocked[SOC_POWER_STATE_NUM];
	struct pd_power_state_blocker_stats triplet_stats[PD_BLOCKER_COUNT];
	struct pd_power_state_blocker_stats
		simplified_stats[SOC_POWER_STATE_NUM][PD_BLOCKER_CLIENT_COUNT];
} __packed;

enum pd_voter {
	PD_VOTER_AOSS,
	PD_VOTER_GSA,
	PD_VOTER_COUNT
};

enum pd_vote {
	PD_VOTE_FABSTBY,
	PD_VOTE_DRAM,
	PD_VOTE_COUNT
};

struct pd_fabric_acg_apg_res {
	u32 fabmed_acc_val_acg;
	u32 fabmed_acc_val_apg;
	u32 fabdisp_acc_val_acg;
	u32 fabdisp_acc_val_apg;
	u32 fabstby_acc_val_acg;
	u32 fabstby_acc_val_apg;
	u32 fabsyss_acc_val_acg;
	u32 fabsyss_acc_val_apg;
	u32 fabhbw_acc_val_acg;
	u32 fabhbw_acc_val_apg;
	u32 memss01_acc_val_acg;
	u32 memss01_acc_val_apg;
	u32 memss23_acc_val_acg;
	u32 memss23_acc_val_apg;
} __packed;

struct pd_gmc_acg_apg_res {
	u32 gmc0_acc_val_acg;
	u32 gmc0_acc_val_apg;
	u32 gmc1_acc_val_acg;
	u32 gmc1_acc_val_apg;
	u32 gmc2_acc_val_acg;
	u32 gmc2_acc_val_apg;
	u32 gmc3_acc_val_acg;
	u32 gmc3_acc_val_apg;
} __packed;

struct pd_acg_apg_csr_res_section {
	struct pd_section_header header;
	struct pd_fabric_acg_apg_res *fabric_acg_apg_res;
	struct pd_gmc_acg_apg_res *gmc_acg_apg_res;
} __packed;

#endif // __GOOGLE_POWERDASHBOARD_H__
