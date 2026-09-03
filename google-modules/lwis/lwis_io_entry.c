// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS I/O Entry Implementation
 *
 * Copyright (c) 2021 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-ioentry: " fmt

#include <linux/delay.h>
#include <linux/preempt.h>

#include "lwis_io_buffer.h"
#include "lwis_io_entry.h"
#include "lwis_util.h"

/* Allow 20us range in usleep */
#define USLEEP_RANGE_DELTA_SHORT 2
#define USLEEP_RANGE_DELTA 20

/* Retrict to udelay with 10us */
#define UDELAY_BOUND_TIME 10

int lwis_io_entry_poll(struct lwis_device *lwis_dev, struct lwis_io_entry *entry, bool is_short)
{
	uint64_t val, start;
	uint64_t timeout_ms = entry->read_assert.timeout_ms;
	int ret = 0;
	int64_t process_time_ms = 0;

	/* Only read and check once if in_hardirq() */
	if (in_hardirq())
		timeout_ms = 0;

	/* Read until getting the expected value or timeout */
	val = ~entry->read_assert.val;
	start = ktime_to_ms(lwis_get_time());
	while (val != entry->read_assert.val) {
		ret = lwis_io_entry_read_assert(lwis_dev, entry);
		if (ret == 0)
			break;

		if (ktime_to_ms(lwis_get_time()) - start > timeout_ms) {
			ret = lwis_io_entry_read_assert(lwis_dev, entry);
			if (ret == 0)
				break;

			dev_err(lwis_dev->dev, "Polling timed out: block %d offset 0x%llx\n",
				entry->read_assert.bid, entry->read_assert.offset);
			return -ETIMEDOUT;
		}

		if (is_short) {
			/* Sleep for 10us */
			usleep_range(10, 10 + USLEEP_RANGE_DELTA_SHORT);
		} else {
			/* Sleep for 1ms */
			usleep_range(1000, 1000 + USLEEP_RANGE_DELTA);
		}
	}

	process_time_ms = ktime_to_ms(lwis_get_time()) - start;

	if (process_time_ms > DEFAULT_POLLING_TIMEOUT_MS)
		dev_info(lwis_dev->dev, "IO entry polling processed %lld ms", process_time_ms);

	return ret;
}

int lwis_io_entry_read_assert(struct lwis_device *lwis_dev, struct lwis_io_entry *entry)
{
	uint64_t val;
	int ret = 0;

	ret = lwis_device_single_register_read(lwis_dev, entry->read_assert.bid,
					       entry->read_assert.offset, &val,
					       lwis_dev->native_read_value_bitwidth);
	if (ret) {
		dev_err(lwis_dev->dev, "Failed to read registers: block %d offset 0x%llx\n",
			entry->read_assert.bid, entry->read_assert.offset);
		return ret;
	}
	if ((val & entry->read_assert.mask) == (entry->read_assert.val & entry->read_assert.mask))
		return 0;

	return -EINVAL;
}

int lwis_io_entry_wait(struct lwis_device *lwis_dev, struct lwis_io_entry *entry)
{
	if (entry->wait_us == 0)
		return 0;

	if (entry->wait_us <= UDELAY_BOUND_TIME) {
		udelay(entry->wait_us);
		return 0;
	}
	if (entry->wait_us <= MAX_WAIT_TIME) {
		usleep_range(entry->wait_us, entry->wait_us + USLEEP_RANGE_DELTA);
		return 0;
	}
	dev_warn(lwis_dev->dev, "Sleep time should be within 0us ~ %dus\n", MAX_WAIT_TIME);
	return -EINVAL;
}

void *lwis_io_entry_result(void *read_buf, struct lwis_io_entry *entry, int reg_value_bytewidth,
			   bool is_periodic)
{
	struct lwis_io_result *io_result;
	size_t result_struct_size;

	if (entry->type != LWIS_IO_ENTRY_READ && entry->type != LWIS_IO_ENTRY_READ_V2 &&
	    entry->type != LWIS_IO_ENTRY_READ_BATCH && entry->type != LWIS_IO_ENTRY_READ_BATCH_V2) {
		return read_buf;
	}

	if (is_periodic) {
		/* Point to the inner io_result struct within the periodic struct */
		io_result = &((struct lwis_periodic_io_result *)read_buf)->io_result;
		result_struct_size = sizeof(struct lwis_periodic_io_result);
	} else {
		io_result = (struct lwis_io_result *)read_buf;
		result_struct_size = sizeof(struct lwis_io_result);
	}

	if (entry->type == LWIS_IO_ENTRY_READ || entry->type == LWIS_IO_ENTRY_READ_V2)
		memcpy(io_result->values, &entry->rw.val, reg_value_bytewidth);
	return (uint8_t *)read_buf + result_struct_size + io_result->num_value_bytes;
}

static bool is_type_batch_compatible(struct lwis_io_entry *entry)
{
	return entry->type == LWIS_IO_ENTRY_WRITE || entry->type == LWIS_IO_ENTRY_WRITE_V2 ||
	       entry->type == LWIS_IO_ENTRY_WRITE_BATCH ||
	       entry->type == LWIS_IO_ENTRY_WRITE_BATCH_V2 || entry->type == LWIS_IO_ENTRY_READ ||
	       entry->type == LWIS_IO_ENTRY_READ_V2 || entry->type == LWIS_IO_ENTRY_READ_BATCH ||
	       entry->type == LWIS_IO_ENTRY_READ_BATCH_V2;
}

int lwis_io_entry_process(struct lwis_device *lwis_dev, struct lwis_io_entry *entry)
{
	if (is_type_batch_compatible(entry) || entry->type == LWIS_IO_ENTRY_MODIFY)
		return lwis_dev->vops.register_io(lwis_dev, entry);
	else if (entry->type == LWIS_IO_ENTRY_POLL)
		return lwis_io_entry_poll(lwis_dev, entry, /*is_short=*/false);
	else if (entry->type == LWIS_IO_ENTRY_POLL_SHORT)
		return lwis_io_entry_poll(lwis_dev, entry, /*is_short=*/true);
	else if (entry->type == LWIS_IO_ENTRY_WAIT)
		return lwis_io_entry_wait(lwis_dev, entry);
	else if (entry->type == LWIS_IO_ENTRY_READ_ASSERT)
		return lwis_io_entry_read_assert(lwis_dev, entry);
	else if (entry->type == LWIS_IO_ENTRY_WRITE_TO_BUFFER)
		return lwis_io_buffer_write(lwis_dev, entry);
	else if (entry->type == LWIS_IO_ENTRY_IGNORE)
		return 0;

	dev_err(lwis_dev->dev, "Unrecognized io_entry command\n");
	return -EINVAL;
}

int lwis_io_entries_process(struct lwis_device *lwis_dev, struct lwis_io_entry *io_entries,
			    int start_idx, int end_idx, bool skip_err)
{
	int i = start_idx, ret = 0;

	while (i < end_idx) {
		int batch_start_idx = i;
		bool is_batch_compatible = lwis_dev->vops.batch_register_io &&
					   is_type_batch_compatible(&io_entries[i]);
		if (is_batch_compatible && (end_idx - i) > 1) {
			i++;

			while (i < end_idx && is_type_batch_compatible(&io_entries[i]))
				i++;

			int batch_size = i - batch_start_idx;

			if (batch_size > 1) {
				ret = lwis_dev->vops.batch_register_io(
					lwis_dev, &io_entries[batch_start_idx], batch_size);
				if (ret)
					return ret;
				continue;
			}
		}

		ret = lwis_io_entry_process(lwis_dev, &io_entries[i]);

		if (ret) {
			if (skip_err) {
				dev_warn(
					lwis_dev->dev,
					"IO entry processing failed at index %d, skipping error and running next command\n",
					i);
				ret = 0; /* Clear error to continue */
				i++;
				continue;
			}
			return ret;
		}
		i++;
	}

	return 0;
}

uint8_t *lwis_io_entry_prepare_results(struct lwis_device *lwis_dev,
				       struct lwis_io_entry *io_entries, int start_idx, int end_idx,
				       uint8_t *read_buf, bool is_periodic)
{
	int i;
	struct lwis_io_result *io_result;
	struct lwis_io_entry *entry;
	size_t result_struct_size;
	const int reg_value_bytewidth = lwis_dev->native_read_value_bitwidth / 8;

	for (i = start_idx; i < end_idx; i++) {
		entry = &io_entries[i];

		if (is_periodic) {
			/* Point to the inner io_result struct within the periodic struct */
			io_result = &((struct lwis_periodic_io_result *)read_buf)->io_result;
			result_struct_size = sizeof(struct lwis_periodic_io_result);
		} else {
			io_result = (struct lwis_io_result *)read_buf;
			result_struct_size = sizeof(struct lwis_io_result);
		}

		if (entry->type == LWIS_IO_ENTRY_READ || entry->type == LWIS_IO_ENTRY_READ_V2) {
			io_result->bid = entry->rw.bid;
			io_result->offset = entry->rw.offset;
			io_result->num_value_bytes = reg_value_bytewidth;
		} else if (entry->type == LWIS_IO_ENTRY_READ_BATCH ||
			   entry->type == LWIS_IO_ENTRY_READ_BATCH_V2) {
			io_result->bid = entry->rw_batch.bid;
			io_result->offset = entry->rw_batch.offset;
			io_result->num_value_bytes = entry->rw_batch.size_in_bytes;
			entry->rw_batch.buf = io_result->values;
		} else {
			/* Not a read entry, so skip to the next one. */
			continue;
		}

		/* If periodic, set the timestamp. */
		if (is_periodic)
			((struct lwis_periodic_io_result *)read_buf)->timestamp_ns =
				ktime_to_ns(lwis_get_time());

		/* Advance the buffer pointer. */
		read_buf += result_struct_size + io_result->num_value_bytes;
	}

	return read_buf;
}
