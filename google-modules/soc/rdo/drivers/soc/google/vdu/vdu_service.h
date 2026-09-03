/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware VDU protocol header.
 *
 * This header is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT.
 */

#ifndef __VDU_SERVICE_H
#define __VDU_SERVICE_H

#ifdef __linux__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#ifndef __packed
#if LK
#include <lk/compiler.h>
#define __packed __PACKED
#else
#define __packed
#endif
#endif

#define GDMC_MBA_VDU_NONCE_LEN 16
#define GDMC_MBA_VDU_CHIPID_LEN 20
#define GDMC_MBA_VDU_DIRECTIVE_MAX_SIZE (0x7D0) // gRoot MSG RAM size
#define GDMC_MBA_VDU_VECTOR_LEN 16

/*
 * SERVICE_ID: GDMC_MBA_SERVICE_ID_VDU
 *
 * This message is used to communicate with BMSM core to use Volatile Debug Unlock (VDU)
 *
 * The TYPE field in the header determines type of operation to be executed against BMSM.
 * Each operation has a different interpretation for remaining mailbox message payload.
 *
 * ==================================================
 * ===== TYPE: GDMC_MBA_VDU_RETRIEVE_NONCE ==========
 * ==================================================
 *
 * This operation is used to trigger gRoot nonce generation for VDU, or read existing one from the
 * cache in GDMC.
 *
 * Request:
 * AP must pass physical address and available size of the buffer where nonce & chip-id should be
 * stored.
 * NOTE: Buffer must have size of at least sizeof(struct gdmc_mba_vdu_msg_nonce_buffer).
 * NOTE: It is recommended to use `struct gdmc_mba_vdu_msg_nonce_buffer` in the caller code.
 * NOTE: Buffer must be placed in the first gigabyte of DRAM (SoC range: 0x80000000 - 0xBFFFFFFF)
 *
 * Word 0: Non-queue mode header and data
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: Bits 31-0 of SOC physical address of the buffer
 * Word 2: Bits 63-32 of SOC physical address of the buffer
 * Word 3: Buffer size
 *
 * Response:
 * GDMC triggers "generate nonce" request to gRoot and stores the result in the internal GDMC cache.
 * If nonce has been already generated, existing value from the internal GDMC cache will be used.
 *
 * Nonce and chip-id values are placed in the buffer one after another.
 * |       Buffer       |
 * |  nonce  |  chipid  |
 *
 * Word 0: Non-queue mode header and data (unchanged)
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: (unchanged)
 * Word 2: (unchanged)
 * Word 3: (unchanged)
 *
 * ==================================================
 * ===== TYPE: GDMC_MBA_VDU_PROCESS_DIRECTIVE =======
 * ==================================================
 *
 * This operation is used to send VDU directive to gRoot. Supported only on LGA.
 *
 * Request:
 * AP must pass physical SoC address of the buffer where VDU directive is stored and size of
 * grant/directive components.
 * NOTE: Buffer must be placed in the first gigabyte of DRAM (SoC range: 0x80000000 - 0xBFFFFFFF)
 *
 * VDU directive consists of two components (grant and delegate) which are expected to be placed
 * one after another in the buffer
 * |        Buffer        |
 * |  grant  |  delegate  |
 *
 * Word 0: Non-queue mode header and data
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: Bits 31-0 of SOC physical address of the buffer
 * Word 2: Bits 63-32 of SOC physical address of the buffer
 * Word 3: Size of grant and directive components in the buffer
 *  bit [ 31 - 16       | 15 - 0     ]
 *      | delegate_size | grant_size ]
 *
 * Response:
 * GDMC passes provided VDU directive to gRoot to process and returns status code.
 *
 * Word 0: Non-queue mode header and data (unchanged)
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: (unchanged)
 * Word 2: (unchanged)
 * Word 3: (unchanged)
 *
 * ==================================================
 * ===== TYPE: GDMC_MBA_VDU_GET_TIMER =======
 * ==================================================
 *
 * This operation is used to read GROOT_TIMER register and parse it.
 *
 * Request:
 *
 * Word 0: Non-queue mode header and data
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: (reserved)
 * Word 2: (reserved)
 * Word 3: (reserved)
 *
 * Response:
 * GDMC reads GROOT_TIMER register from BMSM, parses it and provides result in mailbox response
 * payload.
 *
 * Word 0: Non-queue mode header and data (unchanged)
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: Status of VDU enablement (1 - enabled, 0 - disabled)
 * Word 2: Remaining minutes of VDU enablement
 * Word 3: (reserved)
 *
 * ==================================================
 * ===== TYPE: GDMC_MBA_VDU_GET_VECTOR =======
 * ==================================================
 *
 * This operation is used to read DTA_AUTH_VECTOR registers.
 *
 * Request:
 * AP must pass physical SoC address of the buffer where result will be stored.
 * NOTE: Buffer must have size of at least sizeof(gdmc_mba_vdu_msg_vector_buffer).
 * NOTE: It is recommended to use `gdmc_mba_vdu_msg_vector_buffer` in the caller code.
 * NOTE: Buffer must be placed in the first gigabyte of DRAM (SoC range: 0x80000000 - 0xBFFFFFFF *
 * Word 0: Non-queue mode header and data
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: Bits 31-0 of SOC physical address of the buffer
 * Word 2: Bits 63-32 of SOC physical address of the buffer
 * Word 3: Available buffer size
 *
 * Response:
 * GDMC reads DTA_AUTH_VECTOR registers from BMSM, and stores result in the provided buffer.
 *
 * Word 0: Non-queue mode header and data (unchanged)
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: (unchanged)
 * Word 2: (unchanged)
 * Word 3: (unchanged)
 *
 * ==================================================
 * ===== TYPE: GDMC_MBA_VDU_PROCESS_GRANT =====
 * ==================================================
 *
 * This operation is used to send VDU grant to gRoot. Supported on MBU+.
 *
 * Request:
 * AP must pass physical SoC address of the buffer where VDU grant is stored and size of it
 * NOTE: Buffer must be placed in the first gigabyte of DRAM (SoC range: 0x80000000 - 0xBFFFFFFF)
 *
 * Word 0: Non-queue mode header and data
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: Bits 31-0 of SOC physical address of the buffer
 * Word 2: Bits 63-32 of SOC physical address of the buffer
 * Word 3: Size of grant in the buffer
 *
 * Response:
 * GDMC passes provided VDU grant to gRoot to process and returns status code.
 *
 * Word 0: Non-queue mode header and data (unchanged)
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: (unchanged)
 * Word 2: (unchanged)
 * Word 3: (unchanged)
 *
 * ==================================================
 * ===== TYPE: GDMC_MBA_VDU_PROCESS_DELEGATE =====
 * ==================================================
 *
 * This operation is used to send VDU delegate to gRoot. Supported on MBU+.
 *
 * Request:
 * AP must pass physical SoC address of the buffer where VDU delegate is stored and size of it
 * NOTE: Buffer must be placed in the first gigabyte of DRAM (SoC range: 0x80000000 - 0xBFFFFFFF)
 *
 * Word 0: Non-queue mode header and data
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: Bits 31-0 of SOC physical address of the buffer
 * Word 2: Bits 63-32 of SOC physical address of the buffer
 * Word 3: Size of delegate in the buffer
 *
 * Response:
 * GDMC passes provided VDU delegate to gRoot to process and returns status code.
 *
 * Word 0: Non-queue mode header and data (unchanged)
 *  bit [ 31 - 16       | 15-0     ]
 *      | common header | TYPE   |
 * Word 1: (unchanged)
 * Word 2: (unchanged)
 * Word 3: (unchanged)
 *
*/

/* Valid values for the TYPE field. */
enum gdmc_mba_vdu_op_type {
	GDMC_MBA_VDU_RETRIEVE_NONCE = 0,
	GDMC_MBA_VDU_PROCESS_DIRECTIVE = 1,
	GDMC_MBA_VDU_GET_TIMER = 2,
	GDMC_MBA_VDU_GET_VECTOR = 3,
	GDMC_MBA_VDU_PROCESS_GRANT = 4,
	GDMC_MBA_VDU_PROCESS_DELEGATE = 5,
};

struct gdmc_mba_vdu_msg {
	/* Non-queue mode header and data */
	uint32_t header;

	union {
		/* Reused for RETRIEVE_NONCE, GET_VECTOR, PROCESS_GRANT, PROCESS_DELEGATE */
		struct {
			uint32_t pa_low;
			uint32_t pa_high;
			uint32_t size;
		} send_buffer_req;

		struct {
			uint32_t rsvd[3];
		} send_buffer_res;

		struct {
			uint32_t pa_low;
			uint32_t pa_high;
			struct {
				uint16_t grant;
				uint16_t delegate;
			} size;
		} process_directive_req;

		struct {
			uint32_t rsvd[3];
		} process_directive_res;

		struct {
			uint32_t rsvd[3];
		} get_timer_req;

		struct {
			uint32_t status;
			uint32_t remaining_mins;
			uint32_t rsvd[1];
		} get_timer_rsp;
	} payload;
};

_Static_assert(sizeof(struct gdmc_mba_vdu_msg) == 4 * sizeof(uint32_t),
	       "gdmc_mba_vdu_msg size");

/* Buffer used to store nonce and chipid results */
struct gdmc_mba_vdu_msg_nonce_buffer {
	uint8_t nonce[GDMC_MBA_VDU_NONCE_LEN];
	uint8_t chipid[GDMC_MBA_VDU_CHIPID_LEN];
} __packed;

_Static_assert(sizeof(struct gdmc_mba_vdu_msg_nonce_buffer) ==
		       (GDMC_MBA_VDU_NONCE_LEN + GDMC_MBA_VDU_CHIPID_LEN),
	       "gdmc_mba_vdu_msg_nonce_buffer size");

/* Buffer used to store DTA vector */
struct gdmc_mba_vdu_msg_vector_buffer {
	uint8_t data[GDMC_MBA_VDU_VECTOR_LEN];
} __packed;

_Static_assert(sizeof(struct gdmc_mba_vdu_msg_vector_buffer) ==
		       (GDMC_MBA_VDU_VECTOR_LEN),
	       "gdmc_mba_vdu_msg_vector_buffer size");

#endif /* __VDU_SERVICE_H */
