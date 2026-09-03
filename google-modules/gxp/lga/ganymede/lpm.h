/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Ganymede LPM chip-dependent settings.
 *
 * Copyright (C) 2022-2024 Google LLC
 */

#ifndef __GANYMEDE_LPM_H__
#define __GANYMEDE_LPM_H__

#include <linux/bitops.h>
#include <linux/types.h>

enum gxp_lpm_psm {
	LPM_PSM_TOP,
	LPM_PSM_CORE0,
	LPM_PSM_CORE1,
	LPM_PSM_CORE2,
	LPM_PSM_MCU,
	LPM_NUM_PSMS,
};

#define CORE_TO_PSM(core) (LPM_PSM_CORE0 + (core))

enum lpm_psm_base {
	GXP_REG_LPM_PSM_0 = 0xB000,
	GXP_REG_LPM_PSM_1 = 0xC000,
	GXP_REG_LPM_PSM_2 = 0xD000,
	GXP_REG_LPM_PSM_3 = 0xE000,
	GXP_REG_LPM_PSM_4 = 0xF000,
};

/* LPM Registers */
#define LPM_VERSION_OFFSET 0x0
#define TRIGGER_CSR_START_OFFSET 0x4
#define IMEM_START_OFFSET 0x4
#define LPM_CONFIG_OFFSET 0x8
#define PSM_DESCRIPTOR_OFFSET 0x10
#define EVENTS_EN_OFFSET 0x100
#define EVENTS_INV_OFFSET 0x140
#define FUNCTION_SELECT_OFFSET 0x180
#define TRIGGER_STATUS_OFFSET 0x184
#define EVENT_STATUS_OFFSET 0x188
#define OPS_OFFSET 0x800
#define PSM_DESCRIPTOR_BASE(_x_) ((_x_) << 2)
#define PSM_DESCRIPTOR_COUNT 5
#define EVENTS_EN_BASE(_x_) ((_x_) << 2)
#define EVENTS_EN_COUNT 16
#define EVENTS_INV_BASE(_x_) ((_x_) << 2)
#define EVENTS_INV_COUNT 16
#define OPS_BASE(_x_) ((_x_) << 2)
#define OPS_COUNT 128
#define PSM_COUNT 5
#define PSM_STATE_TABLE_BASE(_x_) ((_x_) << 8)
#define PSM_STATE_TABLE_COUNT 6
#define PSM_TRANS_BASE(_x_) ((_x_) << 5)
#define PSM_TRANS_COUNT 4
#define PSM_DMEM_BASE(_x_) ((_x_) << 2)
#define PSM_DATA_COUNT 32
#define PSM_NEXT_STATE_OFFSET 0x0
#define PSM_SEQ_ADDR_OFFSET 0x4
#define PSM_TIMER_VAL_OFFSET 0x8
#define PSM_TIMER_EN_OFFSET 0xC
#define PSM_TRIGGER_NUM_OFFSET 0x10
#define PSM_TRIGGER_EN_OFFSET 0x14
#define PSM_ENABLE_STATE_OFFSET 0x60
#define PSM_DATA_OFFSET 0x600
#define PSM_DEBUG_CFG_OFFSET 0x68C
#define PSM_BREAK_ADDR_OFFSET 0x694
#define PSM_GPIN_LO_RD_OFFSET 0x6A0
#define PSM_GPIN_HI_RD_OFFSET 0x6A4
#define PSM_GPOUT_LO_WRT_OFFSET 0x6A8
#define PSM_GPOUT_LO_RD_OFFSET 0x6B0
#define PSM_GPOUT_HI_RD_OFFSET 0x6B4
#define PSM_DEBUG_STATUS_OFFSET 0x6B8

#define PSM0_BASE_OFFSET 0xB000
#define PSM0_CFG_OFFSET 0xB29C
#define PSM0_START_OFFSET 0xB2A0
#define PSM0_STATUS_OFFSET 0xB2A4

#define PSM_SIZE 0x1000
#define PSM_STATE0 0x60
#define PSM_STATE_TABLE_SZ 0x64
#define PSM_CFG_OFFSET 0x1C8
#define PSM_START_OFFSET 0x1CC
#define PSM_STATUS_OFFSET 0x1D0

#define PSM4_CFG_OFFSET 0xf1d8
#define PSM4_START_OFFSET 0xf1dc
#define PSM4_STATUS_OFFSET 0xf1e0

/* LCM register offsets */
#define MCU_CLK_GATE_CTRL_MODE 0x19680
#define CLK_GATE_SW_CTRL 0x0
#define CLK_GATE_HW_CTRL 0x1

#define MCU_CLK_GATE_RST_VAL 0x1968c
#define CLK_GATE_RST_ASSERT 0x0
#define CLK_GATE_RST_DEASSERT 0x1

/* LPM PFSM register offsets */
#define PFSM_CONFIG 0xf324
#define PFSM_CONFIG_MODE_MASK 3
#define PFSM_CONFIG_MODE_SW_PFSM 1
#define PFSM_CONFIG_MODE_SW_SEQ 2
#define PFSM_CONFIG_SW_PFS_MASK 0x7c
#define PFSM_CONFIG_SW_PFS_TRGT_SHIFT 2

#define PFSM_START 0xf328

#define PFSM_STATUS 0x11008
#define PFSM_STATUS_STATE_MASK 0x1f
#define PFSM_STATUS_VALID BIT(5)
#define PFSM_STATUS_SEQ_STATUS BIT(6)
#define PFSM_STATUS_PFSM_ERR BIT(8)

/* LPM Powerbus */
#define VFA_SND_REQ 0x10000
#define VFA_SND_TDATA 0x10004
#define VFA_RSP_TDATA 0x10008
#define VFA_RSP_RDY 0x1008C

#define REQ_OPCODE 0
#define RSP_OPCODE 1

#define AUR_RAIL_ID 5

#define REQ_RSP_MSG_OPCODE_SHIFT 12
#define REQ_RSP_MSG_RAIL_ID_SHIFT 9
#define REQ_RSP_MSG_STATUS_SHIFT 8
#define REQ_RSP_MSG_OPLEVEL_MASK 0xff

static inline u32 gxp_lpm_psm_get_status_offset(enum gxp_lpm_psm psm)
{
	switch (psm) {
	case LPM_PSM_TOP:
		return PSM0_STATUS_OFFSET;
	case LPM_PSM_CORE0:
	case LPM_PSM_CORE1:
	case LPM_PSM_CORE2:
		return PSM0_BASE_OFFSET + (PSM_SIZE * psm) + PSM_STATUS_OFFSET;
	case LPM_PSM_MCU:
		return PSM4_STATUS_OFFSET;
	default:
		return 0;
	}
}

static inline u32 gxp_lpm_psm_get_start_offset(enum gxp_lpm_psm psm)
{
	switch (psm) {
	case LPM_PSM_TOP:
		return PSM0_START_OFFSET;
	case LPM_PSM_CORE0:
	case LPM_PSM_CORE1:
	case LPM_PSM_CORE2:
		return PSM0_BASE_OFFSET + (PSM_SIZE * psm) + PSM_START_OFFSET;
	case LPM_PSM_MCU:
		return PSM4_START_OFFSET;
	default:
		return 0;
	}
}

static inline u32 gxp_lpm_psm_get_cfg_offset(enum gxp_lpm_psm psm)
{
	switch (psm) {
	case LPM_PSM_TOP:
		return PSM0_CFG_OFFSET;
	case LPM_PSM_CORE0:
	case LPM_PSM_CORE1:
	case LPM_PSM_CORE2:
		return PSM0_BASE_OFFSET + (PSM_SIZE * psm) + PSM_CFG_OFFSET;
	case LPM_PSM_MCU:
		return PSM4_CFG_OFFSET;
	default:
		return 0;
	}
}

static inline u32 gxp_lpm_psm_get_state_offset(enum gxp_lpm_psm psm, uint state)
{
	if (psm >= LPM_NUM_PSMS || state > 3)
		return 0;

	return PSM0_BASE_OFFSET + (PSM_SIZE * psm) + PSM_STATE0 + (PSM_STATE_TABLE_SZ * state);
}

static inline u32 gxp_lpm_psm_get_debug_cfg_offset(enum gxp_lpm_psm psm)
{
	if (psm >= LPM_NUM_PSMS)
		return 0;
	return PSM0_BASE_OFFSET + (PSM_SIZE * psm) + PSM_DEBUG_CFG_OFFSET;
}

static inline u32 gxp_lpm_psm_get_gpin_lo_rd_offset(enum gxp_lpm_psm psm)
{
	if (psm >= LPM_NUM_PSMS)
		return 0;
	return PSM0_BASE_OFFSET + (PSM_SIZE * psm) + PSM_GPIN_LO_RD_OFFSET;
}

static inline u32 gxp_lpm_psm_get_gpout_lo_wrt_offset(enum gxp_lpm_psm psm)
{
	if (psm >= LPM_NUM_PSMS)
		return 0;
	return PSM0_BASE_OFFSET + (PSM_SIZE * psm) + PSM_GPOUT_LO_WRT_OFFSET;
}

static inline u32 gxp_lpm_psm_get_gpout_lo_rd_offset(enum gxp_lpm_psm psm)
{
	if (psm >= LPM_NUM_PSMS)
		return 0;
	return PSM0_BASE_OFFSET + (PSM_SIZE * psm) + PSM_GPOUT_LO_RD_OFFSET;
}

#endif /* __GANYMEDE_LPM_H__ */
