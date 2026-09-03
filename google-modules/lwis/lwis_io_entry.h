/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS I/O Entry Implementation
 *
 * Copyright (c) 2021 Google, LLC
 */

#ifndef LWIS_IO_ENTRY_H_
#define LWIS_IO_ENTRY_H_

#include "lwis_commands.h"
#include "lwis_device.h"

/* Maximum value of sleep time in us */
#define MAX_WAIT_TIME 1000000
/* Default value of polling timeout */
#define DEFAULT_POLLING_TIMEOUT_MS 5

/*
 * lwis_io_entry_poll:
 * Polls a register for a specified time or until it reaches the expected value.
 */
int lwis_io_entry_poll(struct lwis_device *lwis_dev, struct lwis_io_entry *entry, bool is_short);

/*
 * lwis_io_entry_read_assert:
 * Returns error if a register's value is not as expected.
 */
int lwis_io_entry_read_assert(struct lwis_device *lwis_dev, struct lwis_io_entry *entry);

/*
 * lwis_io_entry_wait:
 * Waits for a settling time to meet the devices to function properly.
 */
int lwis_io_entry_wait(struct lwis_device *lwis_dev, struct lwis_io_entry *entry);

/*
 * lwis_io_entry_result:
 * Process a read entry and update the read buffer.
 */
void *lwis_io_entry_result(void *read_buf, struct lwis_io_entry *entry, int reg_value_bytewidth,
			   bool is_periodic);

/**
 * lwis_io_entry_process() - Process a single LWIS I/O entry.
 * @lwis_dev: The LWIS device to process the entry for.
 * @entry:    The I/O entry to process.
 *
 * This function processes a single I/O entry for the specified LWIS device.
 * It examines the entry type and dispatches it to the appropriate handler
 * function.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int lwis_io_entry_process(struct lwis_device *lwis_dev, struct lwis_io_entry *entry);

/**
 * lwis_io_entries_process() - Processes a batch of LWIS IO entries.
 * @lwis_dev: LWIS device.
 * @io_entries: Array of I/O entries.
 * @start_idx: The starting index to process.
 * @end_idx: The ending index to process.
 * @skip_err: If true, skip errors and continue processing.
 *
 * This function manages the batch processing of LWIS IO entries,
 * checking for compatibility, starting batches, and calling the
 * batch_register_io virtual operation.
 *
 * @return 0 on success, otherwise a negative error code.
 */
int lwis_io_entries_process(struct lwis_device *lwis_dev, struct lwis_io_entry *io_entries,
			    int start_idx, int end_idx, bool skip_err);

/**
 * lwis_io_entry_prepare_results() - Prepare the result buffer for read entries.
 * @lwis_dev: The LWIS device.
 * @io_entries: Array of I/O entries.
 * @start_idx: The starting index to process.
 * @end_idx: The ending index to process.
 * @read_buf: The buffer to populate with results.
 * @is_periodic: True if the results are for periodic I/O (includes timestamp).
 *
 * This function iterates through a list of I/O entries and prepares the
 * result buffer for read operations. It handles both regular and batch reads,
 * and can optionally add timestamps for periodic I/O.
 *
 * Return: A pointer to the end of the populated buffer area.
 */
uint8_t *lwis_io_entry_prepare_results(struct lwis_device *lwis_dev,
				       struct lwis_io_entry *io_entries, int start_idx, int end_idx,
				       uint8_t *read_buf, bool is_periodic);

#endif /* LWIS_IO_ENTRY_H_ */
