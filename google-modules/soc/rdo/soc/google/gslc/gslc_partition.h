/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Partitioning header for GSLC.
 *
 * Copyright (C) 2021 Google LLC.
 */
#ifndef __GSLC_PARTITION_H__
#define __GSLC_PARTITION_H__

#include <linux/types.h>

#include "gslc_platform.h"

/* Partition enable command struct */
struct gslc_partition_en_req {
	// Payload [0]
	__u8 cfg; /* Partition type */
	__u8 priority; /* Partition priority */
	__u8 ovr_valid; /* Override valid */
	__u8 cmd; /* Mailbox command */

	// Payload [1]
	__u32 size; /* Partition size in 1KB granularity */

	// Payload [2]
	__u32 overrides;
} __packed;

/* Partition disable command struct */
struct gslc_partition_dis_req {
	__u8 pid; /* Partition ID */
	__u16 reserved; /* Reserved */
	__u8 cmd; /* Command ID */

	__u32 reserved1[2];
} __packed;

/* Partition mutate command struct */
struct gslc_partition_mutate_req {
	// Payload [0]
	__u8 cfg; /* Partition type */
	__u8 priority; /* Partition priority */
	__u8 pid; /* Partition ID */
	__u8 cmd; /* Mailbox command */

	// Payload [1]
	__u32 size; /* Partition size in 1KB granularity */

	// Payload [2]
	__u32 reserved;
} __packed;

/* Partition commands response struct */
struct gslc_partition_resp {
	__u8 pid; /* Partition ID */
	__u16 reserved; /* Reserved */
	__u8 cmd; /* Command ID */

	__u32 reserved1[2]; /* Reserved */
} __packed;

int gslc_client_partition_req(struct gslc_dev *gslc_dev,
			      const struct gslc_mba_raw_msg *req);

#endif /* __GSLC_PARTITION_H__ */
