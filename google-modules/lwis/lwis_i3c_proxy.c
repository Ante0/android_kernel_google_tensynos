// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS I3C Proxy Interface With I2C
 *
 * Copyright (c) 2024 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-i3c: " fmt

#include "lwis_allocator.h"
#include "lwis_i3c_proxy.h"
#include "lwis_trace.h"
#include "lwis_util.h"

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i3c/device.h>

/* Max bit width for register and data that is supported by this
 * driver currently
 */
#define MIN_OFFSET_BITS 8
#define MAX_OFFSET_BITS 16
#define MIN_DATA_BITS 8
#define MAX_DATA_BITS 32

/* I2C/I3C target address bytes is 7-bit + 1 ack bit */
#define TARGET_ADDR_BYTES 1

/* Context required for xfer */
struct xfer_context {
	uint8_t wbuf[4];
	uint8_t lwbuf[8];
	uint8_t rbuf[4];
	uint8_t *buf;
};

static inline int get_xfer_count(struct lwis_io_entry *entry, int entries_count)
{
	int i, cnt = 0;

	for (i = 0; i < entries_count; ++i) {
		if (entry->type == LWIS_IO_ENTRY_READ || entry->type == LWIS_IO_ENTRY_READ_BATCH)
			cnt += 2;
		else if (entry->type == LWIS_IO_ENTRY_WRITE ||
			 entry->type == LWIS_IO_ENTRY_WRITE_BATCH)
			cnt++;
	}
	return cnt;
}

static inline bool check_bitwidth(const int bitwidth, const int min, const int max)
{
	return (bitwidth >= min) && (bitwidth <= max) && ((bitwidth % 8) == 0);
}

static inline void release_xfer_context(struct lwis_device *lwis_dev, struct xfer_context *context)
{
	lwis_allocator_free(lwis_dev, context->buf);
}

static inline int get_xfers_len(struct i3c_priv_xfer *xfers, int nxfers)
{
	int i, total_len = 0;

	for (i = 0; i < nxfers; ++i)
		total_len += (xfers[i].len + TARGET_ADDR_BYTES);

	return total_len;
}

static int setup_i3c_read(struct lwis_i2c_device *i3c_dev, uint64_t offset,
			  struct xfer_context *context, struct i3c_priv_xfer *xfers)
{
	unsigned int offset_bits;
	unsigned int offset_bytes;
	uint64_t offset_overflow_value;
	unsigned int value_bits;
	unsigned int value_bytes;

	offset_bits = i3c_dev->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i3c_dev->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	value_bits = i3c_dev->base_dev.native_read_value_bitwidth;
	value_bytes = value_bits / BITS_PER_BYTE;
	if (!check_bitwidth(value_bits, MIN_DATA_BITS, MAX_DATA_BITS)) {
		dev_err(i3c_dev->base_dev.dev, "Invalid value bitwidth %d\n", value_bits);
		return -EINVAL;
	}

	offset_overflow_value = 1 << offset_bits;
	if (offset >= offset_overflow_value) {
		dev_err(i3c_dev->base_dev.dev, "Max offset is %d bits\n", offset_bits - 1);
		return -EINVAL;
	}

	lwis_value_to_be_buf(offset, context->wbuf, offset_bytes);
	xfers[0].rnw = false;
	xfers[0].len = offset_bytes;
	xfers[0].data.out = context->wbuf;

	xfers[1].rnw = true;
	xfers[1].len = value_bytes;
	xfers[1].data.in = context->rbuf;

	return 0;
}

static int setup_i3c_write(struct lwis_i2c_device *i3c_dev, uint64_t offset, uint64_t value,
			   struct xfer_context *context, struct i3c_priv_xfer *xfers)
{
	unsigned int offset_bits;
	unsigned int offset_bytes;
	uint64_t offset_overflow_value;
	unsigned int value_bits;
	unsigned int value_bytes;
	uint64_t value_overflow_value;
	uint8_t *wbuf;

	if (i3c_dev->base_dev.is_read_only) {
		dev_err(i3c_dev->base_dev.dev, "Device is read only\n");
		return -EPERM;
	}

	offset_bits = i3c_dev->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i3c_dev->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	value_bits = i3c_dev->base_dev.native_write_value_bitwidth;
	value_bytes = value_bits / BITS_PER_BYTE;
	if (!check_bitwidth(value_bits, MIN_DATA_BITS, MAX_DATA_BITS)) {
		dev_err(i3c_dev->base_dev.dev, "Invalid value bitwidth %d\n", value_bits);
		return -EINVAL;
	}

	offset_overflow_value = 1 << offset_bits;
	if (offset >= offset_overflow_value) {
		dev_err(i3c_dev->base_dev.dev, "Max offset is %d bits\n", offset_bits - 1);
		return -EINVAL;
	}

	value_overflow_value = 1 << value_bits;
	if (value >= value_overflow_value) {
		dev_err(i3c_dev->base_dev.dev, "Max value is %d bits\n", value_bits);
		return -EINVAL;
	}

	wbuf = context->lwbuf;
	lwis_value_to_be_buf(offset, wbuf, offset_bytes);
	lwis_value_to_be_buf(value, wbuf + offset_bytes, value_bytes);
	xfers[0].rnw = false;
	xfers[0].len = offset_bytes + value_bytes;
	xfers[0].data.out = wbuf;

	return 0;
}

static int setup_i3c_read_batch(struct lwis_i2c_device *i3c_dev, uint64_t offset, uint8_t *read_buf,
				int read_buf_size, struct xfer_context *context,
				struct i3c_priv_xfer *xfers)
{
	unsigned int offset_bits;
	unsigned int offset_bytes;
	uint64_t offset_overflow_value;

	offset_bits = i3c_dev->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i3c_dev->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	offset_overflow_value = 1 << offset_bits;
	if (offset >= offset_overflow_value) {
		dev_err(i3c_dev->base_dev.dev, "Max offset is %d bits\n", offset_bits - 1);
		return -EINVAL;
	}

	lwis_value_to_be_buf(offset, context->wbuf, offset_bytes);
	xfers[0].rnw = false;
	xfers[0].len = offset_bytes;
	xfers[0].data.out = context->wbuf;

	xfers[1].rnw = true;
	xfers[1].len = read_buf_size;
	xfers[1].data.in = read_buf;

	return 0;
}

static int setup_i3c_write_batch(struct lwis_i2c_device *i3c_dev, uint64_t offset,
				 uint8_t *write_buf, int write_buf_size,
				 struct xfer_context *context, struct i3c_priv_xfer *xfers)
{
	unsigned int offset_bits;
	unsigned int offset_bytes;
	uint64_t offset_overflow_value;
	int msg_bytes;

	if (i3c_dev->base_dev.is_read_only) {
		dev_err(i3c_dev->base_dev.dev, "Device is read only\n");
		return -EPERM;
	}

	offset_bits = i3c_dev->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i3c_dev->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	offset_overflow_value = 1 << offset_bits;
	if (offset >= offset_overflow_value) {
		dev_err(i3c_dev->base_dev.dev, "Max offset is %d bits\n", offset_bits - 1);
		return -EINVAL;
	}

	msg_bytes = offset_bytes + write_buf_size;
	context->buf = lwis_allocator_allocate(&i3c_dev->base_dev, msg_bytes, GFP_KERNEL);

	if (!context->buf) {
		dev_err(i3c_dev->base_dev.dev, "Failed to allocate memory for I3C buffer\n");
		return -ENOMEM;
	}

	lwis_value_to_be_buf(offset, context->buf, offset_bytes);
	memcpy(context->buf + offset_bytes, write_buf, write_buf_size);
	xfers[0].rnw = false;
	xfers[0].len = msg_bytes;
	xfers[0].data.out = context->buf;

	return 0;
}

static int setup_i3c_xfer(struct lwis_i2c_device *i3c_dev, struct lwis_io_entry *entry,
			  struct xfer_context *context, struct i3c_priv_xfer *xfers)
{
	if (entry->type == LWIS_IO_ENTRY_READ) {
		return setup_i3c_read(i3c_dev, entry->rw.offset, context, xfers);
	} else if (entry->type == LWIS_IO_ENTRY_WRITE) {
		return setup_i3c_write(i3c_dev, entry->rw.offset, entry->rw.val, context, xfers);
	} else if (entry->type == LWIS_IO_ENTRY_READ_BATCH) {
		return setup_i3c_read_batch(i3c_dev, entry->rw_batch.offset, entry->rw_batch.buf,
					    entry->rw_batch.size_in_bytes, context, xfers);
	} else if (entry->type == LWIS_IO_ENTRY_WRITE_BATCH) {
		return setup_i3c_write_batch(i3c_dev, entry->rw_batch.offset, entry->rw_batch.buf,
					     entry->rw_batch.size_in_bytes, context, xfers);
	} else {
		dev_err(i3c_dev->base_dev.dev, "Invalid IO entry type: %d\n", entry->type);
		return -EINVAL;
	}
}

static void parse_i3c_result(struct lwis_io_entry *entry, struct xfer_context *context,
			     struct i3c_priv_xfer *xfers)
{
	if (entry->type == LWIS_IO_ENTRY_READ) {
		/* for i3c_read, xfers[1].data.in = context->rbuf */
		*(&entry->rw.val) = lwis_be_buf_to_value(context->rbuf, xfers[1].len);
	}
}

int lwis_i3c_io_entry_mod(struct lwis_i2c_device *i3c_dev, struct lwis_io_entry *entry)
{
	int ret;
	uint64_t reg_value;
	struct i3c_priv_xfer xfers[2];
	struct xfer_context context = { .wbuf = { 0 }, .lwbuf = { 0 }, .rbuf = { 0 }, .buf = NULL };
	char trace_name[LWIS_MAX_NAME_STRING_LEN];

	if (!entry) {
		dev_err(i3c_dev->base_dev.dev, "IO entry is NULL.\n");
		return -EINVAL;
	}

	ret = setup_i3c_read(i3c_dev, entry->mod.offset, &context, xfers);
	if (ret) {
		dev_err(i3c_dev->base_dev.dev, "failed to setup xfer ret: %d\n", ret);
		return ret;
	}

	scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "i3c_mod_read_%s", i3c_dev->base_dev.name);
	LWIS_ATRACE_FUNC_INT_BEGIN(&(i3c_dev->base_dev), trace_name, get_xfers_len(xfers, 2));
	ret = i3c_device_do_priv_xfers(i3c_dev->i3c, xfers, 2);
	LWIS_ATRACE_FUNC_INT_END(&(i3c_dev->base_dev), trace_name, get_xfers_len(xfers, 2));
	if (ret)
		return ret;

	reg_value = lwis_be_buf_to_value(context.rbuf, xfers[1].len);
	reg_value &= ~entry->mod.val_mask;
	reg_value |= entry->mod.val_mask & entry->mod.val;

	ret = setup_i3c_write(i3c_dev, entry->mod.offset, reg_value, &context, xfers);

	if (ret) {
		dev_err(i3c_dev->base_dev.dev, "failed to setup xfer ret: %d\n", ret);
		return ret;
	}

	/* Reuse xfer_context for modification write */
	scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "i3c_mod_write_%s", i3c_dev->base_dev.name);
	LWIS_ATRACE_FUNC_INT_BEGIN(&(i3c_dev->base_dev), trace_name, get_xfers_len(xfers, 1));
	ret = i3c_device_do_priv_xfers(i3c_dev->i3c, xfers, 1);
	LWIS_ATRACE_FUNC_INT_END(&(i3c_dev->base_dev), trace_name, get_xfers_len(xfers, 1));

	return ret;
}

int lwis_i3c_io_entry_rw(struct lwis_i2c_device *i3c_dev, struct lwis_io_entry *entry)
{
	struct i3c_priv_xfer xfers[2];
	struct xfer_context context = { .wbuf = { 0 }, .lwbuf = { 0 }, .rbuf = { 0 }, .buf = NULL };
	int ret, nxfers;
	char trace_name[LWIS_MAX_NAME_STRING_LEN];

	if (!entry) {
		dev_err(i3c_dev->base_dev.dev, "IO entry is NULL.\n");
		return -EINVAL;
	}

	nxfers = get_xfer_count(entry, /*entries_count=*/1);

	ret = setup_i3c_xfer(i3c_dev, entry, &context, xfers);

	if (ret) {
		dev_err(i3c_dev->base_dev.dev, "failed to setup xfer ret: %d\n", ret);
		release_xfer_context(&i3c_dev->base_dev, &context);
		return ret;
	}

	scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "i3c_xfer_%s", i3c_dev->base_dev.name);
	LWIS_ATRACE_FUNC_INT_BEGIN(&(i3c_dev->base_dev), trace_name, get_xfers_len(xfers, nxfers));
	ret = i3c_device_do_priv_xfers(i3c_dev->i3c, xfers, nxfers);
	LWIS_ATRACE_FUNC_INT_END(&(i3c_dev->base_dev), trace_name, get_xfers_len(xfers, nxfers));

	parse_i3c_result(entry, &context, xfers);

	release_xfer_context(&i3c_dev->base_dev, &context);
	return ret;
}

int lwis_i3c_io_entries_rw(struct lwis_i2c_device *i3c_dev, struct lwis_io_entry *entries,
			   int entries_cnt)
{
	struct i3c_priv_xfer *xfers;
	struct xfer_context *contexts;
	int ret = 0;
	int nxfers;
	int i;
	int xfers_cursor;
	char trace_name[LWIS_MAX_NAME_STRING_LEN];

	if (!entries) {
		dev_err(i3c_dev->base_dev.dev, "IO entries are NULL.\n");
		return -EINVAL;
	}

	nxfers = get_xfer_count(entries, entries_cnt);

	xfers = lwis_allocator_allocate(&i3c_dev->base_dev, nxfers * sizeof(struct i3c_priv_xfer),
					GFP_KERNEL);
	if (!xfers)
		return -ENOMEM;
	/* It zeroes the members inside the i3c_priv_xfer struct */
	memset(xfers, 0, nxfers * sizeof(struct i3c_priv_xfer));

	contexts = lwis_allocator_allocate(&i3c_dev->base_dev,
					   entries_cnt * sizeof(struct xfer_context), GFP_KERNEL);
	if (!contexts) {
		lwis_allocator_free(&i3c_dev->base_dev, xfers);
		return -ENOMEM;
	}
	/* It zeroes the members inside the xfer_context struct */
	memset(contexts, 0, entries_cnt * sizeof(struct xfer_context));

	for (i = 0, xfers_cursor = 0; i < entries_cnt; ++i) {
		ret = setup_i3c_xfer(i3c_dev, &entries[i], &contexts[i], &xfers[xfers_cursor]);
		if (ret)
			goto err_release;

		xfers_cursor += get_xfer_count(&entries[i], /*entries_count=*/1);
	}

	scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "i3c_xfer_%s", i3c_dev->base_dev.name);
	LWIS_ATRACE_FUNC_INT_BEGIN(&(i3c_dev->base_dev), trace_name, get_xfers_len(xfers, nxfers));
	ret = i3c_device_do_priv_xfers(i3c_dev->i3c, xfers, nxfers);
	LWIS_ATRACE_FUNC_INT_END(&(i3c_dev->base_dev), trace_name, get_xfers_len(xfers, nxfers));

	for (i = 0, xfers_cursor = 0; i < entries_cnt; ++i) {
		parse_i3c_result(&entries[i], &contexts[i], &xfers[xfers_cursor]);

		xfers_cursor += get_xfer_count(&entries[i], /*entries_count=*/1);
	}

err_release:
	for (i = 0; i < entries_cnt; ++i)
		release_xfer_context(&i3c_dev->base_dev, &contexts[i]);
	lwis_allocator_free(&i3c_dev->base_dev, xfers);
	lwis_allocator_free(&i3c_dev->base_dev, contexts);

	return ret;
}
