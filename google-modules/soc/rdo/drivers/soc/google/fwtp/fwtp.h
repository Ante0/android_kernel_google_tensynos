/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint services header.
 */

#ifndef __FWTP_H
#define __FWTP_H

#include "fwtp_ipc_client.h"

/* FWTP printer data buffer size to hold at least 8 32-bit data items. */
#define FWTP_PRINTER_DATA_BUFFER_SIZE (8 * (sizeof(struct fwtp_data_item) + 4))

/* Size of buffer to use for printing tracepoints. */
#define FWTP_PRINTER_BUFFER_SIZE 128

/* Size of tracepoint decode buffer. */
#define FWTP_DECODE_BUFFER_SIZE 128

/* String used to select all filters. */
#define FWTP_SELECT_ALL_FILTERS_STRING "all"

/**
 * struct fwtp_dev_svc - Structure used to manage FWTP device services.
 *
 * @dev_list: List of FWTP devices.
 * @genl_family_registered: True if FWTP Generic Netlink family has been
 *                          registered.
 */
struct fwtp_dev_svc {
	struct list_head dev_list;
	bool genl_family_registered;
};

/**
 * struct fwtp_dev - Structure representing an FWTP kernel device.
 *
 * @list_entry: Entry in the FWTP device list.
 * @in_list: If true, the FWTP device is in the FWTP device list.
 * @fwtp_ipc_client: FWTP IPC client record.
 * @dev: Kernel device record.
 * @root_debugfs: Root device debugfs directory.
 * @memio_ring: Tracepoint ring with mem I/O access (may be NULL).
 * @printer_ctx: Tracepoint printer context.
 * @decoder: Optional tracepoint decoder.
 * @printer_data_buffer: Buffer used for tracepoint data items.
 * @printer_buffer: Buffer used for printing tracepoints. It should be big
 *                  enough to hold one line of output.
 * @log_enabled: If true, tracepoints will be published to the kernel log.
 * @ftrace_enabled: If true, tracepoints will be published to ftrace.
 * @notify_byte_count: Threshold byte count for notification.
 * @prev_boottime_timestamp: Previous timestamp for monotonic boottime.
 */
struct fwtp_dev {
	struct list_head list_entry;
	bool in_list;
	struct fwtp_ipc_client fwtp_ipc_client;
	struct device *dev;
	struct dentry *root_debugfs;
	struct tracepoint_ring *memio_ring;
	struct fwtp_printer_ctx printer_ctx;
	struct fwtp_decoder *decoder;
	u8 printer_data_buffer[FWTP_PRINTER_DATA_BUFFER_SIZE];
	char printer_buffer[FWTP_PRINTER_BUFFER_SIZE];
	bool log_enabled;
	bool ftrace_enabled;
	u32 notify_byte_count;
	u64 prev_boottime_timestamp;
};

int fwtp_dev_init(struct fwtp_dev *fwtp_dev);
void fwtp_dev_deinit(struct fwtp_dev *fwtp_dev);
int fwtp_dev_get_memio_ring(struct fwtp_dev *fwtp_dev, int ring_num,
			    struct tracepoint_ring *ring);
u64 fwtp_dev_get_boottime_timestamp(u64 timestamp, u32 timestamp_hz);
void fwtp_dev_printer_post_process(struct fwtp_printer_ctx *printer_ctx,
				   unsigned int type, u64 timestamp, u32 str_id,
				   const char *str,
				   struct fwtp_data_item_list *data_items);

#endif /* __FWTP_H */
