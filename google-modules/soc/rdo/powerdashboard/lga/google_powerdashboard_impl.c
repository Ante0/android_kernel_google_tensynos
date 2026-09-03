// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <dt-bindings/ip-idle/google,lga.h>

#include "google_powerdashboard_impl.h"
#include "../google_powerdashboard_iface.h"
#include "../google_powerdashboard_helper.h"

static const char *const thermal_throttle_names[] = {
	[PD_THERM_DVFS] = "THERMAL_DVFS",
	[PD_THERM_DFS] = "THERMAL_DFS",
	[PD_THERM_THERM_TRIP] = "THERMAL_TRIP",
};
static_assert(ARRAY_SIZE(thermal_throttle_names) == PD_THERM_LVL_NUM);

static const struct pd_attr_str pd_thermal_throttle_names = {
	.values = thermal_throttle_names,
	.count = PD_THERM_LVL_NUM,
};

static const char *const platform_power_state_names[] = {
	[SOC_POWER_STATE_MISSION] = "MISSION",
	[SOC_POWER_STATE_DORMANT] = "DORMANT",
	[SOC_POWER_STATE_DORMANT_SUSPEND] = "DORMANT_SUSPEND",
	[SOC_POWER_STATE_AMBIENT] = "AMBIENT",
	[SOC_POWER_STATE_AMBIENT_SEC_ACC] = "AMBIENT_SEC_ACC",
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
	[PD_POWER_STATE_CG] = "STATE_CG",
	[PD_POWER_STATE_PG] = "STATE_PG",
	[PD_POWER_STATE_ON] = "STATE_ON",
};
static_assert(ARRAY_SIZE(pwrblk_power_state_names) == PD_POWER_STATE_NUM);

static const struct pd_attr_str pd_pwrblk_power_state_names = {
	.values = pwrblk_power_state_names,
	.count = PD_POWER_STATE_NUM
};

static const char *const rail_names[] = {
	[PD_RAIL_VDD_CPU] = "VDD_CPU",	   [PD_RAIL_VDD_CPU1] = "VDD_CPU1",
	[PD_RAIL_VDD_CPU2] = "VDD_CPU2",   [PD_RAIL_VDD_STBY] = "VDD_STBY",
	[PD_RAIL_VDD_AMB] = "VDD_AMB",	   [PD_RAIL_VDD_INFRA] = "VDD_INFRA",
	[PD_RAIL_VDD_MM] = "VDD_MM",	   [PD_RAIL_VDD_GMC] = "VDD_GMC",
	[PD_RAIL_VDD_GPU] = "VDD_GPU",	   [PD_RAIL_VDD_AOC] = "VDD_AOC",
	[PD_RAIL_VDD_TPU] = "VDD_TPU",	   [PD_RAIL_VDD_AUR] = "VDD_AUR",
	[PD_RAIL_VDD_HSION] = "VDD_HSION", [PD_RAIL_VDD_HSIOS] = "VDD_HSIOS",
};
static_assert(ARRAY_SIZE(rail_names) == PD_RAIL_NUM);

static const struct pd_attr_str pd_rail_names = { .values = rail_names,
						  .count = PD_RAIL_NUM };

static const char *const bucks_ldo_names[] = {
	[PD_BUCK_2M] = "BUCK_2M", [PD_BUCK_3M] = "BUCK_3M",
	[PD_BUCK_4M] = "BUCK_4M", [PD_BUCK_7M] = "BUCK_7M",
	[PD_LDO_2S] = "LDO_2S",
};
static_assert(ARRAY_SIZE(bucks_ldo_names) == PD_BUCK_LDO_NUM);

static const struct pd_attr_str pd_bucks_ldo_names = {
	.values = bucks_ldo_names,
	.count = PD_BUCK_LDO_NUM
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

static const char *const sswrp_names[LPB_NUM_SSWRPS] = {
	[LPB_AOC_ID] = "AOC",
	[LPB_AURORA_ID] = "SSWRP AURORA",
	[LPB_CODEC_3P_ID] = "SSWRP Codec 3P",
	[LPB_CPU_ID] = "SSWRP CPU",
	[LPB_CPUACC_ID] = "SSWRP CPUACC",
	[LPB_DPU_ID] = "SSWRP DPU",
	[LPB_EH_ID] = "SSWRP EH",
	[LPB_FABHBW_ID] = "SSWRP FABHBW",
	[LPB_FABMED_ID] = "SSWRP FABMED",
	[LPB_FABRT_ID] = "SSWRP FABRT",
	[LPB_FABSTBY_ID] = "SSWRP FABSTBY",
	[LPB_FABSYSS_ID] = "SSWRP FABSYSS",
	[LPB_G2D_ID] = "SSWRP G2D",
	[LPB_GMC0_ID] = "SSWRP GMC0",
	[LPB_GMC1_ID] = "SSWRP GMC1",
	[LPB_GMC2_ID] = "SSWRP GMC2",
	[LPB_GMC3_ID] = "SSWRP GMC3",
	[LPB_GPCA_ID] = "SSWRP GPCA",
	[LPB_GPCM_ID] = "SSWRP GPCM",
	[LPB_GPDMA_ID] = "SSWRP GPDMA",
	[LPB_GPU_ID] = "SSWRP GPU",
	[LPB_GSA_ID] = "GSA",
	[LPB_GSW_ID] = "SSWRP GSW",
	[LPB_HSIO_N_ID] = "SSWRP HSIO_N",
	[LPB_HSIO_S_ID] = "SSWRP HSIO_S",
	[LPB_INF_TCU_ID] = "SSWRP INF_TCU",
	[LPB_ISPBE_ID] = "SSWRP ISPBE",
	[LPB_ISPFE_ID] = "SSWRP ISPFE",
	[LPB_LSIO_E_ID] = "SSWRP LSIO_E",
	[LPB_LSIO_N_ID] = "SSWRP LSIO_N",
	[LPB_LSIO_S_ID] = "SSWRP LSIO_S",
	[LPB_MEMSS_ID] = "SSWRP MEMSS",
	[LPB_TPU_ID] = "SSWRP TPU",
};
static_assert(ARRAY_SIZE(sswrp_names) == LPB_NUM_SSWRPS);

static const struct pd_attr_str pd_sswrp_names = { .values = sswrp_names,
						   .count = LPB_NUM_SSWRPS };

static const u8 precondition_blocker_lpb_ids[SOC_POWER_PRE_LPB_NUM_SSWRPS] = {
	[SOC_POWER_PRE_FABSTBY_AOC_ID] = LPB_AOC_ID,
	[SOC_POWER_PRE_DRAM_AOC_ID] = LPB_AOC_ID,
	[SOC_POWER_PRE_FABSTBY_GSA_ID] = LPB_GSA_ID,
	[SOC_POWER_PRE_DRAM_GSA_ID] = LPB_GSA_ID,
	[SOC_POWER_PRE_LPB_AURORA_ID] = LPB_AURORA_ID,
	[SOC_POWER_PRE_LPB_CODEC_3P_ID] = LPB_CODEC_3P_ID,
	[SOC_POWER_PRE_LPB_DPU_ID] = LPB_DPU_ID,
	[SOC_POWER_PRE_LPB_EH_ID] = LPB_EH_ID,
	[SOC_POWER_PRE_LPB_G2D_ID] = LPB_G2D_ID,
	[SOC_POWER_PRE_LPB_GPDMA_ID] = LPB_GPDMA_ID,
	[SOC_POWER_PRE_LPB_GPU_ID] = LPB_GPU_ID,
	[SOC_POWER_PRE_LPB_GSW_ID] = LPB_GSW_ID,
	[SOC_POWER_PRE_LPB_HSIO_N_ID] = LPB_HSIO_N_ID,
	[SOC_POWER_PRE_LPB_HSIO_S_ID] = LPB_HSIO_S_ID,
	[SOC_POWER_PRE_LPB_ISPBE_ID] = LPB_ISPBE_ID,
	[SOC_POWER_PRE_LPB_ISPFE_ID] = LPB_ISPFE_ID,
	[SOC_POWER_PRE_LPB_LSIO_E_ID] = LPB_LSIO_E_ID,
	[SOC_POWER_PRE_LPB_LSIO_N_ID] = LPB_LSIO_N_ID,
	[SOC_POWER_PRE_LPB_LSIO_S_ID] = LPB_LSIO_S_ID,
	[SOC_POWER_PRE_LPB_TPU_ID] = LPB_TPU_ID,
};
static_assert(ARRAY_SIZE(precondition_blocker_lpb_ids) ==
	      SOC_POWER_PRE_LPB_NUM_SSWRPS);

static const struct pd_attr_u8 pd_precondition_blocker_lpb_ids = {
	.values = precondition_blocker_lpb_ids,
	.count = SOC_POWER_PRE_LPB_NUM_SSWRPS
};

static const char *const ip_idle_id_names[] = {
	[IP_IDLE_IDX_RESERVED] = "Reserved",
	[IP_IDLE_IDX_GPDMA_0] = "Kernel GPDMA 0",
	[IP_IDLE_IDX_GPDMA_1] = "Kernel GPDMA 1",
	[IP_IDLE_IDX_GPDMA_2] = "Kernel GPDMA 2",
	[IP_IDLE_IDX_GPDMA_3] = "Kernel GPDMA 3",
	[IP_IDLE_IDX_UFS] = "Kernel UFS",
	[IP_IDLE_IDX_DPU] = "Kernel DPU",
	[IP_IDLE_IDX_CODEC_3P] = "Kernel Codec 3P",
	[IP_IDLE_IDX_PCIE_WIFI] = "Kernel PCIe WiFi",
	[IP_IDLE_IDX_PCIE_MODEM] = "Kernel PCIe Modem",
	[IP_IDLE_IDX_USB] = "Kernel USB",
	[IP_IDLE_IDX_EH] = "Kernel EH",
};
static_assert(ARRAY_SIZE(ip_idle_id_names) == IP_IDLE_IDX_NUM);

static const struct pd_attr_str pd_ip_idle_id_names = {
	.values = ip_idle_id_names,
	.count = IP_IDLE_IDX_NUM
};

static struct pd_residency_state thermal_residency[PD_THERM_LVL_NUM];
static u32 tmss_data[TMSS_NUM_PROBES * TMSS_BUFF_SIZE];
static struct pd_thermal_section thermal_section = { .thermal_residency =
							     thermal_residency,
						     .tmss_data = tmss_data};

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

#define REPEAT_ASSIGN_1(lhs, rhs) {.lhs = rhs##_1 },
#define REPEAT_ASSIGN_2(lhs, rhs) REPEAT_ASSIGN_1(lhs, rhs) { .lhs = rhs##_2 },
#define REPEAT_ASSIGN_3(lhs, rhs) REPEAT_ASSIGN_2(lhs, rhs) { .lhs = rhs##_3 },
#define REPEAT_ASSIGN_4(lhs, rhs) REPEAT_ASSIGN_3(lhs, rhs) { .lhs = rhs##_4 },
#define REPEAT_ASSIGN_5(lhs, rhs) REPEAT_ASSIGN_4(lhs, rhs) { .lhs = rhs##_5 },
#define REPEAT_ASSIGN_6(lhs, rhs) REPEAT_ASSIGN_5(lhs, rhs) { .lhs = rhs##_6 },
#define REPEAT_ASSIGN_7(lhs, rhs) REPEAT_ASSIGN_6(lhs, rhs) { .lhs = rhs##_7 },
#define REPEAT_ASSIGN_8(lhs, rhs) REPEAT_ASSIGN_7(lhs, rhs) { .lhs = rhs##_8 },
#define REPEAT_ASSIGN_9(lhs, rhs) REPEAT_ASSIGN_8(lhs, rhs) { .lhs = rhs##_9 },

#define DECLARE_PB(a, b, c)                                            \
	REPEAT_ARR_##c(pd_residency_time_state, power_state_res##b,    \
		       PD_POWER_STATE_NUM);                            \
	static struct pd_pwrblk_power_state pb_##b[c] = {              \
		REPEAT_ASSIGN_##c(power_state_res, power_state_res##b) \
	};
#define ASSIGN_PB(a, b, c) pb_##b,

LGA_PWRBLK_X_MACRO_TABLE(DECLARE_PB)
static struct pd_pwrblk_power_state *pwrblk_power_states[PWRBLK_NUM_IDS] = {
	LGA_PWRBLK_X_MACRO_TABLE(ASSIGN_PB)
};

static struct pd_pwrblk_section pwrblk_section = {
	.pwrblk_power_states = pwrblk_power_states
};

#define DECLARE_LPCM(a, b)                                               \
	static struct pd_residency_state pf_state_res_##b[PF_STATE_CNT]; \
	static struct pd_lpcm_res lpcm_##b[PWRBLK_SINGLE_LPCM] = {       \
		{ .pf_state_res = pf_state_res_##b }                     \
	};
#define ASSIGN_LPCM(a, b) lpcm_##b,

LGA_LPCM_X_MACRO_TABLE(DECLARE_LPCM)
static struct pd_lpcm_res *lpcm_residencies[LPCM_NUM_SSWRPS] = {
	LGA_LPCM_X_MACRO_TABLE(ASSIGN_LPCM)
};

static struct pd_lpcm_section lpcm_section = { .lpcm_residencies =
						       lpcm_residencies };

static u32 curr_voltage[PD_RAIL_NUM];
static struct pd_rail_section rail_section = { .curr_voltage = curr_voltage };

static struct pd_clavs_section clavs_section;

static struct pd_buck_ldo_data buck_ldo_data[PD_BUCK_LDO_NUM];
static struct pd_curr_volt_section curr_volt_section = {
	.buck_ldo_data = buck_ldo_data
};

static u32 ppu[PD_PPU_CNT];
static struct pd_apc_power_section apc_power_section = { .ppu = ppu };

static struct pd_power_state_blockers_section power_state_blockers_section;

static struct pd_fabric_acg_apg_res fabric_acg_apg_res;
static struct pd_gmc_acg_apg_res gmc_acg_apg_res;

static struct pd_acg_apg_csr_res_section acg_apg_csr_res_section = {
	.fabric_acg_apg_res = &fabric_acg_apg_res,
	.gmc_acg_apg_res = &gmc_acg_apg_res,
};

#define BASE_SIZE HEADER_OFFSET
#define THERMAL_SECTION_SIZE (BASE_SIZE + sizeof(thermal_residency) + sizeof(tmss_data))
#define PLATFORM_POWER_SECTION_SIZE  (BASE_SIZE + (sizeof(plat_power_res)) + 1)

#define GET_COUNT(a, b, c) + (c)
#define PSM_COUNT (LGA_PWRBLK_X_MACRO_TABLE(GET_COUNT))
#define PB_SIZE                                                                \
	((1 + (sizeof(struct pd_residency_time_state) * PD_POWER_STATE_NUM)) * \
	 PSM_COUNT)
#define PWRBLK_SECTION_SIZE (BASE_SIZE + PB_SIZE)

#define LPCM_RES_SIZE                                               \
	((2 + (sizeof(struct pd_residency_state) * PF_STATE_CNT)) * \
	 LPCM_NUM_SSWRPS)
#define LPCM_SECTION_SIZE (BASE_SIZE + LPCM_RES_SIZE)

#define RAIL_SECTION_SIZE (BASE_SIZE + sizeof(curr_voltage))
#define CLAVS_SECTION_SIZE (BASE_SIZE + 2)
#define CURR_VOLT_SECTION_SIZE (BASE_SIZE + 4 + sizeof(buck_ldo_data))
#define APC_POWER_SECTION_SIZE (BASE_SIZE + sizeof(ppu))
#define CPM_M55_SECTION_SIZE (BASE_SIZE + sizeof(power_state_res) + 1)
#define POWER_STATE_BLOCKERS_SECTION_SIZE sizeof(power_state_blockers_section)
#define ACG_APG_CSR_RES_SECTION_SIZE	\
	(BASE_SIZE + sizeof(fabric_acg_apg_res) + sizeof(gmc_acg_apg_res))

static void __iomem *section_bases[PD_SECTION_NUM];
static const size_t section_sizes[PD_SECTION_NUM] = {
	[PD_THERMAL] = THERMAL_SECTION_SIZE,
	[PD_PLATFORM_POWER] = PLATFORM_POWER_SECTION_SIZE,
	[PD_PWRBLK] = PWRBLK_SECTION_SIZE,
	[PD_LPCM] = LPCM_SECTION_SIZE,
	[PD_RAIL] = RAIL_SECTION_SIZE,
	[PD_CLAVS] = CLAVS_SECTION_SIZE,
	[PD_CURR_VOLT] = CURR_VOLT_SECTION_SIZE,
	[PD_APC_POWER] = APC_POWER_SECTION_SIZE,
	[PD_CPM_M55] = CPM_M55_SECTION_SIZE,
	[PD_POWER_STATE_BLOCKERS] = POWER_STATE_BLOCKERS_SECTION_SIZE,
	[PD_ACG_APG_CSR_RES] = ACG_APG_CSR_RES_SECTION_SIZE,
};

static const void *section_ptrs[PD_SECTION_NUM] = {
	[PD_THERMAL] = &thermal_section,
	[PD_PLATFORM_POWER] = &platform_power_section,
	[PD_PWRBLK] = &pwrblk_section,
	[PD_LPCM] = &lpcm_section,
	[PD_RAIL] = &rail_section,
	[PD_CLAVS] = &clavs_section,
	[PD_CURR_VOLT] = &curr_volt_section,
	[PD_APC_POWER] = &apc_power_section,
	[PD_CPM_M55] = &cpm_m55_power_section,
	[PD_POWER_STATE_BLOCKERS] = &power_state_blockers_section,
	[PD_ACG_APG_CSR_RES] = &acg_apg_csr_res_section,
};

#define HEADER_COPY(name, data)                                       \
	name##_section.header.size = *(u64 *)(data);                    \
	name##_section.header.version = *(u32 *)((data) + sizeof(u64)); \
	size_t offset = HEADER_OFFSET;

static void thermal_copy(void *section_data)
{
	HEADER_COPY(thermal, section_data);
	memcpy(thermal_residency, section_data + offset,
	       sizeof(thermal_residency));
	offset += sizeof(thermal_residency);
	memcpy(tmss_data, section_data + offset, sizeof(tmss_data));
}

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
	LGA_PWRBLK_X_MACRO_TABLE(ASSIGN_PSM_COUNT)
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
			offset += PD_POWER_STATE_NUM * sizeof(struct pd_residency_time_state);
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

		for (int j = 0; j < PWRBLK_SINGLE_LPCM; ++j) {
			lpcm[j].curr_pf_state = *(u8 *)(section_data + offset);
			offset += 1;
			lpcm[j].num_pf_states = *(u8 *)(section_data + offset);
			offset += 1;
			memcpy(lpcm[j].pf_state_res, section_data + offset,
			       PF_STATE_CNT *
				       sizeof(struct pd_residency_state));
			offset += PF_STATE_CNT *
				  sizeof(struct pd_residency_state);
		}
	}
}

static void rail_copy(void *section_data)
{
	HEADER_COPY(rail, section_data);
	memcpy(curr_voltage, section_data + offset, sizeof(curr_voltage));
}

static void clavs_copy(void *section_data)
{
	HEADER_COPY(clavs, section_data);
	clavs_section.gpu_current = *(u16 *)(section_data + offset);
}

static void curr_volt_copy(void *section_data)
{
	HEADER_COPY(curr_volt, section_data);
	curr_volt_section.vsys_droop_count = *(u16 *)(section_data + offset);
	offset += 2;
	memcpy(buck_ldo_data, section_data + offset, sizeof(buck_ldo_data));
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
	power_state_blockers_section =
		*(struct pd_power_state_blockers_section *)section_data;
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
	return true;
}

static section_copy_func section_copy_funcs[PD_SECTION_NUM] = {
	[PD_THERMAL] = &thermal_copy,
	[PD_PLATFORM_POWER] = &platform_power_copy,
	[PD_PWRBLK] = &pwrblk_copy,
	[PD_LPCM] = &lpcm_copy,
	[PD_RAIL] = &rail_copy,
	[PD_CLAVS] = &clavs_copy,
	[PD_CURR_VOLT] = &curr_volt_copy,
	[PD_APC_POWER] = &apc_power_copy,
	[PD_CPM_M55] = &cpm_m55_copy,
	[PD_POWER_STATE_BLOCKERS] = &power_state_blockers_copy,
	[PD_ACG_APG_CSR_RES] = &acg_apg_csr_res_copy,
};

const struct google_powerdashboard_iface powerdashboard_iface = {
	.sections = {
		.thermal_section = &thermal_section,
		.platform_power_section = &platform_power_section,
		.pwrblk_section = &pwrblk_section,
		.lpcm_section = &lpcm_section,
		.rail_section = &rail_section,
		.clavs_section = &clavs_section,
		.curr_volt_section = &curr_volt_section,
		.apc_power_section = &apc_power_section,
		.cpm_m55_power_section = &cpm_m55_power_section,
		.power_state_blockers_section = &power_state_blockers_section,
		.vote_section = NULL,
		.acg_apg_csr_res_section = &acg_apg_csr_res_section,
		.section_ptrs = section_ptrs,
		.section_bases = (void __iomem **)section_bases,
		.section_sizes = (size_t *)section_sizes,
		.section_copy_funcs = section_copy_funcs,
	},
	.attrs = {
		.thermal_throttle_names = &pd_thermal_throttle_names,
		.platform_power_state_names = &pd_platform_power_state_names,
		.pwrblk_power_state_names = &pd_pwrblk_power_state_names,
		.rail_names = &pd_rail_names,
		.bucks_ldo_names = &pd_bucks_ldo_names,
		.apc_power_ppu_names = &pd_apc_power_ppu_names,
		.cpm_m55_power_state_names = &pd_cpm_m55_power_state_names,
		.sswrp_names = &pd_sswrp_names,
		.ip_idle_id_names = &pd_ip_idle_id_names,
		.blocker_names = NULL,
		.voter_names = NULL,
		.voted_resource_names = NULL,
		.precondition_blocker_lpb_ids = &pd_precondition_blocker_lpb_ids,
	},
};

const struct google_powerdashboard_constants powerdashboard_constants = {
	.mba_client_tx_timeout = MBA_CLIENT_TX_TIMEOUT,
	.read_time = READ_TIME,
	.pf_state_cnt = PF_STATE_CNT,
	.tmss_num_probes = TMSS_NUM_PROBES,
	.tmss_buff_size = TMSS_BUFF_SIZE,
	.gtc_ticks_per_ms = GTC_TICKS_PER_MS,
	.soc_pwr_state_num_kernel_blockers = SOC_PWR_STATE_NUM_KERNEL_BLOCKERS,
	.ip_idle_idx_num = IP_IDLE_IDX_NUM,
	.soc_power_state_dormant_suspend = SOC_POWER_STATE_DORMANT_SUSPEND,
};
