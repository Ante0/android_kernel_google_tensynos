/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wrapper to abstract #includes for systems regardless of whether they have GSA support
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __AOC_GSA_H__
#define __AOC_GSA_H__

#include <linux/device.h>
#include <linux/types.h>

enum gsa_aoc_state {
	GSA_AOC_STATE_INACTIVE = 0,
	GSA_AOC_STATE_LOADED,
	GSA_AOC_STATE_RUNNING,
};

enum gsa_aoc_cmd {
	GSA_AOC_GET_STATE = 0,
	GSA_AOC_START = 1,
	GSA_AOC_SHUTDOWN = 4,
	GSA_AOC_RELEASE_RESET = 5,
	GSA_AOC_RESET = 6,
};

static inline int gsa_load_aoc_fw_image(struct device *gsa,
			  dma_addr_t img_meta,
			  phys_addr_t img_body)
{
	return -ENODEV;
}

static inline int gsa_send_aoc_cmd(struct device *gsa, enum gsa_aoc_cmd arg)
{
	return -ENODEV;
}

static inline int gsa_unload_aoc_fw_image(struct device *gsa)
{
	return -ENODEV;
}

#endif /* __AOC_GSA_H__ */
