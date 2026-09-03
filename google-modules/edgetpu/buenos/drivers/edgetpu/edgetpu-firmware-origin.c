// SPDX-License-Identifier: GPL-2.0-only
/*
 * Edge TPU firmware loader utility for Origin.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/types.h>

#include "edgetpu-config.h"
#include "edgetpu-firmware.h"
#include "edgetpu-internal.h"

void edgetpu_firmware_reset_cpu_ns(struct edgetpu_dev *etdev, bool assert_reset)
{
	const int top_reset = 0x1;
	int i;

	for (i = 0; i < EDGETPU_NUM_CORES; i++)
		edgetpu_dev_write_32_sync(etdev, EDGETPU_REG_RESET_CONTROL + i * 8,
					  assert_reset ? top_reset : 0x0);
}
