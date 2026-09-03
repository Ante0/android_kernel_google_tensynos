/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2024 Google LLC.
 *
 */

#pragma once

#include <linux/arm-smccc.h>

/* For EL3 debug cmd */
#define SIP_SVD_GS_DEBUG_CMD		(0x82000612)

/* Debug commands */
#define CMD_ASSERT			0x0
#define CMD_PANIC			0x1
#define CMD_ECC				0xecc

static inline unsigned long google_smc(unsigned long cmd, unsigned long arg0,
				       unsigned long arg1, unsigned long arg2)
{
	struct arm_smccc_res res;

	arm_smccc_smc(cmd, arg0, arg1, arg2, 0, 0, 0, 0, &res);
	return (unsigned long)res.a0;
}
