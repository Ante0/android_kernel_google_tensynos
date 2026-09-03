/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2026 Google LLC.
 *
 * Google firmware Modem UART protocol header.
 *
 * This header is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT.
 */

#ifndef __MODEM_UART_SERVICE_H
#define __MODEM_UART_SERVICE_H

#ifdef __linux__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/* Valid values for the TYPE field. */
enum gdmc_mba_modem_uart_op_type {
	GDMC_MBA_MODEM_UART_START = 0,
	GDMC_MBA_MODEM_UART_STOP = 1,
	GDMC_MBA_MODEM_UART_CLEAR = 2,
	GDMC_MBA_MODEM_UART_GET_TAIL_OFFSET = 3,
};

struct gdmc_mba_modem_uart_msg {
	/* Non-queue mode header and data */
	uint32_t header;

	union {
		/* Reused for STOP, CLEAR */
		struct {
			uint32_t rsvd[3];
		} send_cmd_req;

		struct {
			uint32_t rsvd[3];
		} get_cmd_res;

		struct {
			uint32_t pa_low;
			uint32_t pa_high;
			uint32_t size;
		} send_start_req;

		struct {
			uint32_t rsvd[3];
		} get_start_res;

		struct {
			uint32_t rsvd[3];
		} get_tail_offset_req;

		struct {
			uint32_t offset;
			uint32_t rsvd[2];
		} get_tail_offset_res;
	} payload;
};

_Static_assert(sizeof(struct gdmc_mba_modem_uart_msg) == 4 * sizeof(uint32_t),
	       "gdmc_mba_modem_uart_msg size");

#endif /* __MODEM_UART_SERVICE_H */
