/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023, Google LLC
 */

#ifndef __GOOG_GDMC_REGDUMP_SERVICE_H
#define __GOOG_GDMC_REGDUMP_SERVICE_H

/*
 * SERVICE_ID: GDMC_MBA_SERVICE_ID_GET_REGDUMP
 *
 * This message is used for the AP to read register dump data from GDMC.
 *
 * NOTE: This message does not trigger the actual register dump. If there is
 *       no valid register dump at the time of the request, then no data is
 *       copied and the buffer entries are marked invalid.
 *
 * Request:
 * The ID field in the header identifies the subsystem/core for which dump
 * data is being requested. AP must also pass the physical address and size
 * of a pre-allocated buffer where the register dump should be copied.
 * NOTE: this buffer must currently be located within the first 1GB of DRAM.
 *
 * Word 0: Non-queue mode header and data
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | ID       |
 * Word 1: Bits 31-0 of SOC physical address
 * Word 2: Bits 63-32 of SOC physical address
 * Word 3: Size
 *
 * Response:
 * GDMC copies the register dump into the provided buffer and writes the
 * number of bytes copied into the size field. If the buffer is not large
 * enough, then the dump is truncated. If a valid dump does not exist,
 * then an error response is returned.
 *
 * The contents of the dump will vary depending on the architecture of the
 * target (see structures below) and may be an array of dumps if the target
 * contains multiple cores.
 *
 * Word 0: Non-queue mode header and data (unchanged)
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | ID       |
 * Word 1: (unchanged)
 * Word 2: (unchanged)
 * Word 3: Number of bytes copied to buffer
 *
 * SERVICE_ID: GDMC_MBA_SERVICE_ID_GET_REGDUMP
 */

enum gdmc_mba_regdump_id {
	GDMC_MBA_REGDUMP_ID_APC = 0,
	GDMC_MBA_REGDUMP_ID_AOC = 1,
	GDMC_MBA_REGDUMP_ID_CPM = 2,
	GDMC_MBA_REGDUMP_ID_PPU = 3,
	GDMC_MBA_REGDUMP_ID_GDMC = 4,
	GDMC_MBA_REGDUMP_ID_DPA = 5,
};

/*
 * gdmc_mba_cmd_get_regdump_msg - Structure of regdump message
 * @header:			Non-queue mode header and data
 * @pa_low:			SOC physical address of dump buffer (bits 31:0)
 * @pa_high:			SOC physical address of dump buffer (bits 63:32)
 * @size:			Size of dump buffer
 */
struct gdmc_mba_cmd_get_regdump_msg {
	u32 header;
	u32 pa_low;
	u32 pa_high;
	u32 size;
};

#define GDMC_MBA_AARCH32_REGDUMP_FLAG_VALID            BIT(0)
#define GDMC_MBA_AARCH32_REGDUMP_FLAG_POWER_STATE      BIT(1)
#define GDMC_MBA_AARCH32_REGDUMP_FLAG_HALTED           BIT(2)

/*
 * gdmc_mba_aarch32_register_dump - register dump for aarch32 AoC processor
 * @flags:		Bit 0 -> boolean value indicating whether the dump is valid
 *			Bit 1 -> boolean value indicating whether the core power is on
 *			Bit 2 -> boolean value indicating whether the core is halted
 *			Bits 31:3 -> reserved
 * @gprs:		General-purposed registers from R0 to R12
 * @sp:			Stack pointer
 * @lr:			Link register
 * @pc:			Program counter
 * @cpsr:		Current program status register
 * @spsr:		Saved program status register
 *			(invalid if PE is in User/System mode)
 * @dfsr:		Data fault status register
 * @dfar:		Data fault address register
 * @ifsr:		Instruction fault status register
 * @ifar:		Instruction fault address register
 */
struct gdmc_mba_aarch32_register_dump {
	u32 flags;
	u32 gprs[13];
	u32 sp;
	u32 lr;
	u32 pc;
	u32 cpsr;
	u32 spsr;
	u32 dfsr;
	u32 dfar;
	u32 ifsr;
	u32 ifar;
};

/*
 * gdmc_mba_cortex_m_register_dump - register dump for M55 processor
 * @valid:          bit 0 -> boolean value indicating whether the dump is valid
 *                  bits 31:1 -> reserved
 * @gprs:           General-purposed registers from R0 to R12
 * @sp:             Stack pointer
 * @lr:             Link register
 * @pc:             Program counter
 * @xpsr_eapsr:     Exception program status register
 * @psp:            program stack pointer
 * @msp:            main stack pointer
 * @curr_state:     current state: {CONTROL[7:0], FAULTMASK[7:0], BASEPRI[7:0], PRMASK[7:0]}
 * @psplim:         program stack limit
 * @msplim:         main stack limit
 * @vpr:            vector prediction register
 * @cfsr:           configurable fault status register
 * @mmfar:          mem manage fault address register
 * @bfar:           bus fault address register
 * @afsr:           auxiliary fault status register
 * @hfsr:           hard fault status register
 */
struct gdmc_mba_cortex_m_register_dump {
	u32 valid;
	u32 gprs[13];
	u32 sp;
	u32 lr;
	u32 pc;
	u32 xpsr_eapsr;
	u32 psp;
	u32 msp;
	u32 curr_state;
	u32 psplim;
	u32 msplim;
	u32 vpr;
	u32 cfsr;
	u32 mmfar;
	u32 bfar;
	u32 afsr;
	u32 hfsr;
};

/*
 * google_mba_dpa_register_dump - register dump for DPA (NCP/NEP processors)
 */
struct gdmc_mba_dpa_register_dump {
	struct gdmc_mba_cortex_m_register_dump ncp_regs;
	struct gdmc_mba_cortex_m_register_dump nep_regs;
	// TODO(b/377638729): Remove valid flags as ncp_regs/nep_regs have valid flag
	bool ncp_valid;
	bool nep_valid;
};

#endif /* __GOOG_GDMC_REGDUMP_SERVICE_H */
