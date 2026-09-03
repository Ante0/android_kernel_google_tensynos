/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CPM mailbox header for GSLC.
 *
 * Copyright (C) 2021 Google LLC.
 */
#ifndef __GSLC_CPM_MBA_H__
#define __GSLC_CPM_MBA_H__

#include <linux/types.h>

#include "gslc_platform.h"

/* Defines and enums match with the GSLC driver on CPM */
/* GSLC command defines in the first message of the mailbox payload */
#define GSLC_MBA_CMD_MASK GENMASK(31, 24)

/* GSLC mailbox commands. (Using non zero values to be explicit) */
enum gslc_mba_cmds {
	CMD_ID_PARTITION_ENABLE = 1, /* Partition enable command */
	CMD_ID_PARTITION_DISABLE, /* Partition disable command */
	CMD_ID_PARTITION_MUTATE, /* Partition mutate command */
	CMD_ID_MAX,
};

struct gslc_mba_raw_msg {
	__u32 raw_data[GOOG_MBA_PAYLOAD_SIZE];
} __packed;

int gslc_cpm_mba_init(struct gslc_dev *gslc_dev);
void gslc_cpm_mba_deinit(struct gslc_dev *gslc_dev);
int gslc_cpm_mba_send_req_blocking(struct gslc_dev *gslc_dev,
				   const struct gslc_mba_raw_msg *req,
				   struct gslc_mba_raw_msg *resp);

#endif /* __GSLC_CPM_MBA_H__ */
