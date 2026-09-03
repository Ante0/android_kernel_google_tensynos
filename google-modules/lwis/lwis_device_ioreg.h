/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS I/O Mapped Device Driver
 *
 * Copyright (c) 2018 Google, LLC
 */

#ifndef LWIS_DEVICE_IOREG_H_
#define LWIS_DEVICE_IOREG_H_

#include <linux/types.h>

#include "lwis_bus_manager.h"
#include "lwis_device.h"

struct lwis_ioreg {
	phys_addr_t start;
	int size;
	void __iomem *base;
	const char *name;
	uint64_t offset;
};

struct lwis_ioreg_list {
	struct lwis_ioreg *block;
	int count;
};

struct lwis_ioreg_valid_range {
	u32 block_id;
	u32 start_addr;
	u32 size;
};

struct lwis_ioreg_valid_range_list {
	struct lwis_ioreg_valid_range *ranges;
	int count;
};

/*
 *  struct lwis_ioreg_device
 *  "Derived" lwis_device struct, with added IOREG related elements.
 */
struct lwis_ioreg_device {
	struct lwis_device base_dev;
	struct lwis_ioreg_list reg_list;
	struct lwis_ioreg_valid_range_list reg_valid_range_list;
	/* Device priority for bus manager processing order */
	int device_priority;
	/* Group handle for devices that are managed together */
	u32 device_group;
	/* Used only by specific platforms for aggregation purpose */
	int32_t sswrap_key;
};

int lwis_ioreg_device_init(void);
int lwis_ioreg_device_deinit(void);

/* Print lwis_ioreg_valid_range_list content */
void lwis_ioreg_device_valid_range_list_print(struct lwis_ioreg_device *ioreg_dev);

#endif /* LWIS_DEVICE_IOREG_H_ */
