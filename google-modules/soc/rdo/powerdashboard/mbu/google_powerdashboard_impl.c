// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <dvfs-helper/google_dvfs_helper.h>

#include "google_powerdashboard_impl.h"
#include "../google_powerdashboard_iface.h"
#include "../google_powerdashboard_helper.h"

static const char *const platform_power_state_names[] = {
	[SOC_POWER_STATE_MISSION] = "MISSION",
	[SOC_POWER_STATE_DORMANT] = "DORMANT",
	[SOC_POWER_STATE_DORMANT_SUSPEND] = "DORMANT_SUSPEND",
	[SOC_POWER_STATE_AMBIENT_MC_ON] = "AMBIENT_MC_ON",
	[SOC_POWER_STATE_AMBIENT_OD_AF] = "AMBIENT_OD_AF",
	[SOC_POWER_STATE_AMBIENT_SUSPEND] = "AMBIENT_SUSPEND",
};
static_assert(ARRAY_SIZE(platform_power_state_names) ==
	      SOC_POWER_STATE_NUM);

static const struct pd_attr_str pd_platform_power_state_names = {
	.values = platform_power_state_names,
	.count = SOC_POWER_STATE_NUM
};

static const char *const pwrblk_power_state_names[] = {
	[PD_POWER_STATE_OFF] = "STATE_OFF",
	[PD_POWER_STATE_ON] = "STATE_ON",
};
static_assert(ARRAY_SIZE(pwrblk_power_state_names) == PD_POWER_STATE_NUM);

static const struct pd_attr_str pd_pwrblk_power_state_names = {
	.values = pwrblk_power_state_names,
	.count = PD_POWER_STATE_NUM
};

static const char *const apc_power_ppu_names[] = {
	[PD_CORE_0] = "CORE_0",	  [PD_CORE_1] = "CORE_1",
	[PD_CORE_2] = "CORE_2",	  [PD_CORE_3] = "CORE_3",
	[PD_CORE_4] = "CORE_4",	  [PD_CORE_5] = "CORE_5",
	[PD_CORE_6] = "CORE_6",	  [PD_CORE_7] = "CORE_7",
	[PD_CLUSTER] = "CLUSTER",
};
static_assert(ARRAY_SIZE(apc_power_ppu_names) == PD_PPU_CNT);

static const struct pd_attr_str pd_apc_power_ppu_names = {
	.values = apc_power_ppu_names,
	.count = PD_PPU_CNT
};

static const char *const cpm_m55_power_state_names[] = {
	[PD_CPM_M55_STATE_ACTIVE] = "ACTIVE",
	[PD_CPM_M55_STATE_WFI] = "WFI",
	[PD_CPM_M55_STATE_PG] = "PG",
};
static_assert(ARRAY_SIZE(cpm_m55_power_state_names) == PD_CPM_M55_NUM_STATES);

static const struct pd_attr_str pd_cpm_m55_power_state_names = {
	.values = cpm_m55_power_state_names,
	.count = PD_CPM_M55_NUM_STATES
};

static const char *const sswrp_names[] = {
	[LPB_AOSS_AMBSS_ID] = "SSWRP AOSS AMBSS",
	[LPB_AOSS_AONSS_ID] = "SSWRP AOSS AONSS",
	[LPB_AOSS_PG_ID] = "SSWRP AOSS PG",
	[LPB_AURDSP_ID] = "SSWRP AURDSP",
	[LPB_BMSM_ID] = "SSWRP BMSM",
	[LPB_CODEC_3P_ID] = "SSWRP Codec 3P",
	[LPB_CPU_ID] = "SSWRP CPU",
	[LPB_CPUACC_ID] = "SSWRP CPUACC",
	[LPB_DPU_ID] = "SSWRP DPU",
	[LPB_FABDISP_ID] = "SSWRP FABDISP",
	[LPB_FABHBW_ID] = "SSWRP FABHBW",
	[LPB_FABMED_ID] = "SSWRP FABMED",
	[LPB_FABSTBY_ID] = "SSWRP FABSTBY",
	[LPB_FABSYSS_ID] = "SSWRP FABSYSS",
	[LPB_G2D_ID] = "SSWRP G2D",
	[LPB_GCV_ID] = "SSWRP GCV",
	[LPB_GMC0_ID] = "SSWRP GMC0",
	[LPB_GMC1_ID] = "SSWRP GMC1",
	[LPB_GMC2_ID] = "SSWRP GMC2",
	[LPB_GMC3_ID] = "SSWRP GMC3",
	[LPB_GPCA_ID] = "SSWRP GPCA",
	[LPB_GPCM_AMB_ID] = "SSWRP GPCM AMB",
	[LPB_GPCM_INFRA_ID] = "SSWRP GPCM INFRA",
	[LPB_GPU_ID] = "SSWRP GPU",
	[LPB_GSA_ID] = "FABSTBY GSA",
	[LPB_HSIO_N_ID] = "SSWRP HSIO_N",
	[LPB_HSIO_S_ID] = "SSWRP HSIO_S",
	[LPB_ISPBE_ID] = "SSWRP ISPBE",
	[LPB_ISPFE_ID] = "SSWRP ISPFE",
	[LPB_LSIO_E_ID] = "SSWRP LSIO_E",
	[LPB_LSIO_S_ID] = "SSWRP LSIO_S",
	[LPB_MEMSS_ID] = "SSWRP MEMSS",
	[LPB_PCIE_ID] = "SSWRP PCIE",
	[LPB_TPU_ID] = "SSWRP TPU",
};
static_assert(ARRAY_SIZE(sswrp_names) == LPB_NUM_SSWRPS);

static const struct pd_attr_str pd_sswrp_names = {
	.values = sswrp_names,
	.count = LPB_NUM_SSWRPS
};

static const char *const blocker_client_names[] = {
	[PD_BLOCKER_CLIENT_AOSS] = "AOSS",
	[PD_BLOCKER_CLIENT_CPM] = "CPM",
	[PD_BLOCKER_CLIENT_DPA] = "DPA",
	[PD_BLOCKER_CLIENT_GDMC] = "GDMC",
	[PD_BLOCKER_CLIENT_CPUSS] = "CPUSS",
	[PD_BLOCKER_CLIENT_GSA] = "GSA",
	[PD_BLOCKER_CLIENT_HYP] = "HYP",
	[PD_BLOCKER_CLIENT_THERMAL_MEAS] = "THERMAL_MEAS",
	[PD_BLOCKER_CLIENT_THERMAL_SVC] = "THERMAL_SVC",
	[PD_BLOCKER_CLIENT_TZ] = "TZ",
	[PD_BLOCKER_CLIENT_VM1] = "VM1",
	[PD_BLOCKER_CLIENT_VM2] = "VM2",
	[PD_BLOCKER_CLIENT_VM3] = "VM3",
	[PD_BLOCKER_CLIENT_VM4] = "VM4",
	[PD_BLOCKER_CLIENT_POWER_DASH] = "POWER_DASH",
	[PD_BLOCKER_CLIENT_DVFSMON] = "DVFSMON",
	[PD_BLOCKER_CLIENT_CPM_TP] = "CPM_TP",
	[PD_BLOCKER_CLIENT_TEST] = "TEST",
};
static_assert(ARRAY_SIZE(blocker_client_names) == PD_BLOCKER_CLIENT_COUNT);

static const struct pd_attr_str pd_blocker_client_names = {
	.values = blocker_client_names,
	.count = PD_BLOCKER_CLIENT_COUNT,
};

static const char *const blocker_names[] = {
	[PD_BLOCKER_AMB_OD_AF_AOSS_AOSS_AMBSS] = "AMB-OD-AF AOSS AOSS_AMBSS",
	[PD_BLOCKER_AMB_OD_AF_AOSS_AOSS_PG] = "AMB-OD-AF AOSS AOSS_PG",
	[PD_BLOCKER_AMB_OD_AF_AOSS_MC_ON] = "AMB-OD-AF AOSS MC_ON",
	[PD_BLOCKER_AMB_OD_AF_AOSS_FABSTBY] = "AMB-OD-AF AOSS FABSTBY",
	[PD_BLOCKER_AMB_OD_AF_AOSS_PCIE] = "AMB-OD-AF AOSS PCIE",
	[PD_BLOCKER_AMB_OD_AF_GSA_GSA] = "AMB-OD-AF GSA GSA",
	[PD_BLOCKER_AMB_OD_AF_GSA_AURDSP] = "AMB-OD-AF GSA AURDSP",
	[PD_BLOCKER_AMB_OD_AF_GSA_BMSM] = "AMB-OD-AF GSA BMSM",
	[PD_BLOCKER_AMB_OD_AF_GSA_MC_ON] = "AMB-OD-AF GSA MC_ON",
	[PD_BLOCKER_AMB_OD_AF_GSA_TPU] = "AMB-OD-AF GSA TPU",
	[PD_BLOCKER_AMB_SUS_AOSS_AOSS_AMBSS] = "AMB-SUS AOSS AOSS_AMBSS",
	[PD_BLOCKER_AMB_SUS_AOSS_AOSS_PG] = "AMB-SUS AOSS AOSS_PG",
	[PD_BLOCKER_AMB_SUS_AOSS_MC_ON] = "AMB-SUS AOSS MC_ON",
	[PD_BLOCKER_AMB_SUS_AOSS_FABSTBY] = "AMB-SUS AOSS FABSTBY",
	[PD_BLOCKER_AMB_SUS_AOSS_PCIE] = "AMB-SUS AOSS PCIE",
	[PD_BLOCKER_AMB_SUS_GSA_GSA] = "AMB-SUS GSA GSA",
	[PD_BLOCKER_AMB_SUS_GSA_AURDSP] = "AMB-SUS GSA AURDSP",
	[PD_BLOCKER_AMB_SUS_GSA_BMSM] = "AMB-SUS GSA BMSM",
	[PD_BLOCKER_AMB_SUS_GSA_MC_ON] = "AMB-SUS GSA MC_ON",
	[PD_BLOCKER_AMB_SUS_GSA_TPU] = "AMB-SUS GSA TPU",
	[PD_BLOCKER_AMB_MC_ON_AOSS_AOSS_AMBSS] = "AMB-MC-ON AOSS AOSS_AMBSS",
	[PD_BLOCKER_AMB_MC_ON_AOSS_AOSS_PG] = "AMB-MC-ON AOSS AOSS_PG",
	[PD_BLOCKER_AMB_MC_ON_AOSS_MC_ON] = "AMB-MC-ON AOSS MC_ON",
	[PD_BLOCKER_AMB_MC_ON_AOSS_FABSTBY] = "AMB-MC-ON AOSS FABSTBY",
	[PD_BLOCKER_AMB_MC_ON_AOSS_PCIE] = "AMB-MC-ON AOSS PCIE",
	[PD_BLOCKER_AMB_MC_ON_GSA_GSA] = "AMB-MC-ON GSA GSA",
	[PD_BLOCKER_AMB_MC_ON_GSA_AURDSP] = "AMB-MC-ON GSA AURDSP",
	[PD_BLOCKER_AMB_MC_ON_GSA_BMSM] = "AMB-MC-ON GSA BMSM",
	[PD_BLOCKER_AMB_MC_ON_GSA_MC_ON] = "AMB-MC-ON GSA MC_ON",
	[PD_BLOCKER_AMB_MC_ON_GSA_TPU] = "AMB-MC-ON GSA TPU",
	[PD_BLOCKER_DORM_SUS_AOSS_AOSS_AMBSS] = "DORM-SUS AOSS AOSS_AMBSS",
	[PD_BLOCKER_DORM_SUS_AOSS_AOSS_PG] = "DORM-SUS AOSS AOSS_PG",
	[PD_BLOCKER_DORM_SUS_AOSS_MC_ON] = "DORM-SUS AOSS MC_ON",
	[PD_BLOCKER_DORM_SUS_AOSS_FABSTBY] = "DORM-SUS AOSS FABSTBY",
	[PD_BLOCKER_DORM_SUS_AOSS_PCIE] = "DORM-SUS AOSS PCIE",
	[PD_BLOCKER_DORM_SUS_GSA_GSA] = "DORM-SUS GSA GSA",
	[PD_BLOCKER_DORM_SUS_GSA_AURDSP] = "DORM-SUS GSA AURDSP",
	[PD_BLOCKER_DORM_SUS_GSA_BMSM] = "DORM-SUS GSA BMSM",
	[PD_BLOCKER_DORM_SUS_GSA_MC_ON] = "DORM-SUS GSA MC_ON",
	[PD_BLOCKER_DORM_SUS_GSA_TPU] = "DORM-SUS GSA TPU",
	[PD_BLOCKER_DORM_SUS_VM1_AURDSP] = "DORM-SUS VM1 AURDSP",
	[PD_BLOCKER_DORM_SUS_VM1_CODEC_3P] = "DORM-SUS VM1 CODEC_3P",
	[PD_BLOCKER_DORM_SUS_VM1_DPU] = "DORM-SUS VM1 DPU",
	[PD_BLOCKER_DORM_SUS_VM1_G2D] = "DORM-SUS VM1 G2D",
	[PD_BLOCKER_DORM_SUS_VM1_GCV] = "DORM-SUS VM1 GCV",
	[PD_BLOCKER_DORM_SUS_VM1_GPCM_AMB] = "DORM-SUS VM1 GPCM_AMB",
	[PD_BLOCKER_DORM_SUS_VM1_GPU] = "DORM-SUS VM1 GPU",
	[PD_BLOCKER_DORM_SUS_VM1_HSIO_N] = "DORM-SUS VM1 HSIO_N",
	[PD_BLOCKER_DORM_SUS_VM1_HSIO_S] = "DORM-SUS VM1 HSIO_S",
	[PD_BLOCKER_DORM_SUS_VM1_ISPBE] = "DORM-SUS VM1 ISPBE",
	[PD_BLOCKER_DORM_SUS_VM1_ISPFE] = "DORM-SUS VM1 ISPFE",
	[PD_BLOCKER_DORM_SUS_VM1_LSIO_E] = "DORM-SUS VM1 LSIO_E",
	[PD_BLOCKER_DORM_SUS_VM1_LSIO_S] = "DORM-SUS VM1 LSIO_S",
	[PD_BLOCKER_DORM_SUS_VM1_PCIE] = "DORM-SUS VM1 PCIE",
	[PD_BLOCKER_DORM_SUS_VM1_TPU] = "DORM-SUS VM1 TPU",
	[PD_BLOCKER_DORM_SUS_VM1_AOSS_PG] = "DORM-SUS VM1 AOSS_PG",
	[PD_BLOCKER_DORM_SUS_VM1_AOSS_AMBSS] = "DORM-SUS VM1 AOSS_AMBSS",
};
static_assert(ARRAY_SIZE(blocker_names) == PD_BLOCKER_COUNT);

static const struct pd_attr_str pd_blocker_names = {
	.values = blocker_names,
	.count = PD_BLOCKER_COUNT
};

static const char *const voter_names[] = {
	[PD_VOTER_AOSS] = "AOSS",
	[PD_VOTER_GSA] = "GSA",
};
static_assert(ARRAY_SIZE(voter_names) == PD_VOTER_COUNT);

static const struct pd_attr_str pd_voter_names = {
	.values = voter_names,
	.count = PD_VOTER_COUNT,
};

static const char *const voted_resource_names[] = {
	[PD_VOTE_FABSTBY] = "FABSTBY",
	[PD_VOTE_DRAM] = "DRAM",
};
static_assert(ARRAY_SIZE(voted_resource_names) == PD_VOTE_COUNT);

static const struct pd_attr_str pd_voted_resource_names = {
	.values = voted_resource_names,
	.count = PD_VOTE_COUNT,
};

static struct pd_residency_time_state power_state_res[PD_CPM_M55_NUM_STATES];
static struct pd_cpm_m55_power_section cpm_m55_power_section = {
	.power_state_res = power_state_res
};

static struct pd_residency_time_state plat_power_res[SOC_POWER_STATE_NUM];
static struct pd_platform_power_section platform_power_section = {
	.plat_power_res = plat_power_res
};

#define REPEAT_ARR_1(type, x, size) static struct type x##_1[size]
#define REPEAT_ARR_2(type, x, size)  \
	REPEAT_ARR_1(type, x, size); \
	static struct type x##_2[size]
#define REPEAT_ARR_3(type, x, size)  \
	REPEAT_ARR_2(type, x, size); \
	static struct type x##_3[size]
#define REPEAT_ARR_4(type, x, size)  \
	REPEAT_ARR_3(type, x, size); \
	static struct type x##_4[size]
#define REPEAT_ARR_5(type, x, size)  \
	REPEAT_ARR_4(type, x, size); \
	static struct type x##_5[size]
#define REPEAT_ARR_6(type, x, size)  \
	REPEAT_ARR_5(type, x, size); \
	static struct type x##_6[size]
#define REPEAT_ARR_7(type, x, size)  \
	REPEAT_ARR_6(type, x, size); \
	static struct type x##_7[size]
#define REPEAT_ARR_8(type, x, size)  \
	REPEAT_ARR_7(type, x, size); \
	static struct type x##_8[size]
#define REPEAT_ARR_9(type, x, size)  \
	REPEAT_ARR_8(type, x, size); \
	static struct type x##_9[size]

#define REPEAT_ASSIGN_1(lhs, rhs) { .lhs = rhs##_1 },
#define REPEAT_ASSIGN_2(lhs, rhs) REPEAT_ASSIGN_1(lhs, rhs) { .lhs = rhs##_2 },
#define REPEAT_ASSIGN_3(lhs, rhs) REPEAT_ASSIGN_2(lhs, rhs) { .lhs = rhs##_3 },
#define REPEAT_ASSIGN_4(lhs, rhs) REPEAT_ASSIGN_3(lhs, rhs) { .lhs = rhs##_4 },
#define REPEAT_ASSIGN_5(lhs, rhs) REPEAT_ASSIGN_4(lhs, rhs) { .lhs = rhs##_5 },
#define REPEAT_ASSIGN_6(lhs, rhs) REPEAT_ASSIGN_5(lhs, rhs) { .lhs = rhs##_6 },
#define REPEAT_ASSIGN_7(lhs, rhs) REPEAT_ASSIGN_6(lhs, rhs) { .lhs = rhs##_7 },
#define REPEAT_ASSIGN_8(lhs, rhs) REPEAT_ASSIGN_7(lhs, rhs) { .lhs = rhs##_8 },
#define REPEAT_ASSIGN_9(lhs, rhs) REPEAT_ASSIGN_8(lhs, rhs) { .lhs = rhs##_9 },

#define DECLARE_PB(a, b, c)                                                \
	REPEAT_ARR_##c(pd_residency_time_state, power_state_res##b,            \
		       PD_POWER_STATE_NUM); static struct pd_pwrblk_power_state \
		pb_##b[c] = { REPEAT_ASSIGN_##c(power_state_res,               \
						power_state_res##b) };
#define ASSIGN_PB(a, b, c) pb_##b,

MBU_PWRBLK_X_MACRO_TABLE(DECLARE_PB)
static struct pd_pwrblk_power_state *pwrblk_power_states[PWRBLK_NUM_IDS] = {
	MBU_PWRBLK_X_MACRO_TABLE(ASSIGN_PB)
};

static struct pd_pwrblk_section pwrblk_section = {
	.pwrblk_power_states = pwrblk_power_states
};

#define DECLARE_MEDIUM_LPCM(_, b)                                              \
	static struct pd_residency_time_state pf_state_res_##b[MEDIUM_PF_STATE_CNT];   \
	static struct pd_lpcm_res lpcm_##b = { \
		.pf_state_res = pf_state_res_##b              \
	};

#define DECLARE_LARGE_LPCM(_, b)                                                  \
	static struct pd_residency_time_state pf_state_res_##b[LARGE_PF_STATE_CNT]; \
	static struct pd_lpcm_res lpcm_##b = { \
		.pf_state_res = pf_state_res_##b              \
	};

#define ASSIGN_LPCM(_, b) &lpcm_##b,

MBU_MEDIUM_LPCM_X_MACRO_TABLE(DECLARE_MEDIUM_LPCM)
MBU_LARGE_LPCM_X_MACRO_TABLE(DECLARE_LARGE_LPCM)
static struct pd_lpcm_res *lpcm_residencies[LPCM_NUM_SSWRPS] = {
	MBU_MEDIUM_LPCM_X_MACRO_TABLE(ASSIGN_LPCM)
	MBU_LARGE_LPCM_X_MACRO_TABLE(ASSIGN_LPCM)
};

static struct pd_lpcm_section lpcm_section = { .lpcm_residencies =
						       lpcm_residencies };

static u32 ppu[PD_PPU_CNT];
static struct pd_apc_power_section apc_power_section = { .ppu = ppu };

static struct pd_power_state_blockers_section power_state_blockers_section;

static struct pd_residency_time_state vote_res[PD_VOTER_COUNT][PD_VOTE_COUNT];
static struct pd_vote_section vote_section = { .vote_res = &vote_res[0][0] };

static struct pd_fabric_acg_apg_res fabric_acg_apg_res;
static struct pd_gmc_acg_apg_res gmc_acg_apg_res;

static struct pd_acg_apg_csr_res_section acg_apg_csr_res_section = {
	.fabric_acg_apg_res = &fabric_acg_apg_res,
	.gmc_acg_apg_res = &gmc_acg_apg_res,
};

#define BASE_SIZE HEADER_OFFSET

#define PLATFORM_POWER_SECTION_SIZE (BASE_SIZE + (sizeof(plat_power_res)) + 1)

#define GET_COUNT(a, b, c) + (c)

#define PSM_COUNT (MBU_PWRBLK_X_MACRO_TABLE(GET_COUNT))
#define PB_SIZE                                                                \
	((1 + (sizeof(struct pd_residency_time_state) * PD_POWER_STATE_NUM)) * \
	 PSM_COUNT)
#define PWRBLK_SECTION_SIZE (BASE_SIZE + PB_SIZE)

#define LPCM_RES_SIZE \
	((3 + (sizeof(struct pd_residency_time_state) * MEDIUM_PF_STATE_CNT)) \
	* (LPCM_NUM_SSWRPS - LARGE_LPCM_NUM_SSWRPS)) + \
	((3 + (sizeof(struct pd_residency_time_state) * LARGE_PF_STATE_CNT)) \
	* LARGE_LPCM_NUM_SSWRPS)
#define LPCM_SECTION_SIZE (BASE_SIZE + LPCM_RES_SIZE)

#define APC_POWER_SECTION_SIZE (BASE_SIZE + sizeof(ppu))
#define CPM_M55_SECTION_SIZE (BASE_SIZE + sizeof(power_state_res) + 1)
#define POWER_STATE_BLOCKERS_SECTION_SIZE sizeof(power_state_blockers_section)
#define VOTE_SECTION_SIZE (BASE_SIZE + sizeof(vote_res))
#define ACG_APG_CSR_RES_SECTION_SIZE \
	(BASE_SIZE + sizeof(fabric_acg_apg_res) + sizeof(gmc_acg_apg_res))

static void __iomem *section_bases[PD_SECTION_NUM];
static const size_t section_sizes[PD_SECTION_NUM] = {
	[PD_PLATFORM_POWER] = PLATFORM_POWER_SECTION_SIZE,
	[PD_PWRBLK] = PWRBLK_SECTION_SIZE,
	[PD_LPCM] = LPCM_SECTION_SIZE,
	[PD_APC_POWER] = APC_POWER_SECTION_SIZE,
	[PD_CPM_M55] = CPM_M55_SECTION_SIZE,
	[PD_POWER_STATE_BLOCKERS] = POWER_STATE_BLOCKERS_SECTION_SIZE,
	[PD_VOTE] = VOTE_SECTION_SIZE,
	[PD_ACG_APG_CSR_RES] = ACG_APG_CSR_RES_SECTION_SIZE,
};

static const void *section_ptrs[PD_SECTION_NUM] = {
	[PD_PLATFORM_POWER] = &platform_power_section,
	[PD_PWRBLK] = &pwrblk_section,
	[PD_LPCM] = &lpcm_section,
	[PD_APC_POWER] = &apc_power_section,
	[PD_CPM_M55] = &cpm_m55_power_section,
	[PD_POWER_STATE_BLOCKERS] = &power_state_blockers_section,
	[PD_VOTE] = &vote_section,
	[PD_ACG_APG_CSR_RES] = &acg_apg_csr_res_section,
};

#define HEADER_COPY(name, data)                                   \
	name##_section.header.size = *(u64 *)(data);                    \
	name##_section.header.version = *(u32 *)((data) + sizeof(u64)); \
	size_t offset = HEADER_OFFSET;

static void platform_power_copy(void *section_data)
{
	HEADER_COPY(platform_power, section_data);
	memcpy(plat_power_res, section_data + offset, sizeof(plat_power_res));
	offset += sizeof(plat_power_res);
	platform_power_section.curr_power_state =
		*(u8 *)(section_data + offset);
}

#define ASSIGN_PSM_COUNT(a, b, c)[PD_PWRBLK_ID(a)] = c,
static size_t pwrblk_psm_counts[] = {
	MBU_PWRBLK_X_MACRO_TABLE(ASSIGN_PSM_COUNT)
};

static void pwrblk_copy(void *section_data)
{
	HEADER_COPY(pwrblk, section_data);
	for (int i = 0; i < PWRBLK_NUM_IDS; ++i) {
		struct pd_pwrblk_power_state *pb = pwrblk_power_states[i];

		for (int j = 0; j < pwrblk_psm_counts[i]; ++j) {
			memcpy(pb[j].power_state_res, section_data + offset,
			       PD_POWER_STATE_NUM *
				       sizeof(struct pd_residency_time_state));
			offset += PD_POWER_STATE_NUM *
				  sizeof(struct pd_residency_time_state);
			pb[j].curr_power_state = *(u8 *)(section_data + offset);
			offset += 1;
		}
	}
}

static void lpcm_copy(void *section_data)
{
	HEADER_COPY(lpcm, section_data)
	for (int i = 0; i < LPCM_NUM_SSWRPS; ++i) {
		struct pd_lpcm_res *lpcm = lpcm_residencies[i];
		lpcm->pwrblk = *(u8 *)(section_data + offset);
		offset += 1;
		lpcm->curr_pf_state = *(u8 *)(section_data + offset);
		offset += 1;
		lpcm->num_pf_states = *(u8 *)(section_data + offset);
		offset += 1;
		int pf_state_cnt;

		if (i < LPCM_NUM_SSWRPS - LARGE_LPCM_NUM_SSWRPS)
			pf_state_cnt = MEDIUM_PF_STATE_CNT;
		else
			pf_state_cnt = LARGE_PF_STATE_CNT;

		memcpy(lpcm->pf_state_res, section_data + offset,
				pf_state_cnt *
					sizeof(struct pd_residency_time_state));
		offset += pf_state_cnt *
				sizeof(struct pd_residency_time_state);
	}
}

static void apc_power_copy(void *section_data)
{
	HEADER_COPY(apc_power, section_data);
	memcpy(ppu, section_data + offset, sizeof(ppu));
}

static void cpm_m55_copy(void *section_data)
{
	HEADER_COPY(cpm_m55_power, section_data);
	memcpy(power_state_res, section_data + offset, sizeof(power_state_res));
	offset += sizeof(power_state_res);
	cpm_m55_power_section.current_state = *(u8 *)(section_data + offset);
}

static void power_state_blockers_copy(void *section_data)
{
	power_state_blockers_section = *(struct pd_power_state_blockers_section *)section_data;
}

static void vote_copy(void *section_data)
{
	HEADER_COPY(vote, section_data);
	memcpy(vote_res, section_data + offset, sizeof(vote_res));
}

static void acg_apg_csr_res_copy(void *section_data)
{
	HEADER_COPY(acg_apg_csr_res, section_data);
	memcpy(&fabric_acg_apg_res, section_data + offset,
	       sizeof(struct pd_fabric_acg_apg_res));
	offset += sizeof(fabric_acg_apg_res);
	memcpy(&gmc_acg_apg_res, section_data + offset,
	       sizeof(struct pd_gmc_acg_apg_res));
}

bool plat_dvfs_helper_ready(void)
{
	return dvfs_helper_ready();
}

static section_copy_func section_copy_funcs[PD_SECTION_NUM] = {
	[PD_PLATFORM_POWER] = &platform_power_copy,
	[PD_PWRBLK] = &pwrblk_copy,
	[PD_LPCM] = &lpcm_copy,
	[PD_APC_POWER] = &apc_power_copy,
	[PD_CPM_M55] = &cpm_m55_copy,
	[PD_POWER_STATE_BLOCKERS] = &power_state_blockers_copy,
	[PD_VOTE] = &vote_copy,
	[PD_ACG_APG_CSR_RES] = &acg_apg_csr_res_copy,
};

const struct google_powerdashboard_iface powerdashboard_iface = {
	.sections = {
		.thermal_section = NULL,
		.platform_power_section = &platform_power_section,
		.pwrblk_section = &pwrblk_section,
		.lpcm_section = &lpcm_section,
		.rail_section = NULL,
		.clavs_section = NULL,
		.curr_volt_section = NULL,
		.apc_power_section = &apc_power_section,
		.cpm_m55_power_section = &cpm_m55_power_section,
		.power_state_blockers_section = &power_state_blockers_section,
		.vote_section = &vote_section,
		.acg_apg_csr_res_section = &acg_apg_csr_res_section,
		.section_ptrs = section_ptrs,
		.section_bases = (void __iomem **)section_bases,
		.section_sizes = (size_t *)section_sizes,
		.section_copy_funcs = section_copy_funcs,
	},
	.attrs = {
		.thermal_throttle_names = NULL,
		.platform_power_state_names = &pd_platform_power_state_names,
		.pwrblk_power_state_names = &pd_pwrblk_power_state_names,
		.rail_names = NULL,
		.bucks_ldo_names = NULL,
		.apc_power_ppu_names = &pd_apc_power_ppu_names,
		.cpm_m55_power_state_names = &pd_cpm_m55_power_state_names,
		.sswrp_names = &pd_sswrp_names,
		.ip_idle_id_names = NULL,
		.blocker_client_names = &pd_blocker_client_names,
		.blocker_names = &pd_blocker_names,
		.voter_names = &pd_voter_names,
		.voted_resource_names = &pd_voted_resource_names,
		.precondition_blocker_lpb_ids = NULL,
	},
};

const struct google_powerdashboard_constants powerdashboard_constants = {
	.mba_client_tx_timeout = MBA_CLIENT_TX_TIMEOUT,
	.read_time = READ_TIME,
	.pf_state_cnt = MEDIUM_PF_STATE_CNT,
	.tmss_num_probes = 0,
	.tmss_buff_size = 0,
	.gtc_ticks_per_ms = GTC_TICKS_PER_MS,
	.soc_pwr_state_num_kernel_blockers = 0,
	.ip_idle_idx_num = 0,
	.soc_power_state_dormant_suspend = SOC_POWER_STATE_DORMANT_SUSPEND,
};

