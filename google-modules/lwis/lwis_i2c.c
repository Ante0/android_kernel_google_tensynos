// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS I2C Interface
 *
 * Copyright (c) 2018 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-i2c: " fmt

#include "lwis_allocator.h"
#include "lwis_i2c.h"
#include "lwis_trace.h"
#include "lwis_util.h"

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#define I2C_DEVICE_NAME "LWIS_I2C"

/* Max bit width for register and data that is supported by this
 * driver currently
 */
#define MIN_OFFSET_BITS 8
#define MAX_OFFSET_BITS 16
#define MIN_DATA_BITS 8
#define MAX_DATA_BITS 32

/* I2C target address bytes is 7-bit + 1 ack bit */
#define I2C_TARGET_ADDR_BYTES 1

/* Context required for i2c_msg */
struct i2c_msg_context {
	uint8_t wbuf[4];
	uint8_t lwbuf[8];
	uint8_t rbuf[4];
	uint8_t *buf;
};

static inline int get_msg_count(struct lwis_io_entry *entry, int entries_count)
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

static inline int get_msgs_len(struct i2c_msg *msgs, int nmsgs)
{
	int i, total_len = 0;

	for (i = 0; i < nmsgs; ++i)
		total_len += (msgs[i].len + I2C_TARGET_ADDR_BYTES);

	return total_len;
}

static inline void release_i2c_msg_context(struct lwis_device *lwis_dev,
					   struct i2c_msg_context *context)
{
	lwis_allocator_free(lwis_dev, context->buf);
}

static inline bool check_bitwidth(const int bitwidth, const int min, const int max)
{
	return (bitwidth >= min) && (bitwidth <= max) && ((bitwidth % 8) == 0);
}

static int perform_write_transfer(struct i2c_client *client, struct i2c_msg *msg, uint64_t offset,
				  int offset_size_bytes, int value_size_bytes, uint64_t value,
				  struct lwis_device *lwis_dev)
{
	int ret = 0;
	u8 *buf = msg->buf;

	const int num_msg = 1;
	char trace_name[LWIS_MAX_NAME_STRING_LEN];

	scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "i2c_write_%s", lwis_dev->name);

	lwis_value_to_be_buf(offset, buf, offset_size_bytes);
	lwis_value_to_be_buf(value, buf + offset_size_bytes, value_size_bytes);
	LWIS_ATRACE_FUNC_INT_BEGIN(lwis_dev, trace_name,
				   msg[0].len + num_msg * I2C_TARGET_ADDR_BYTES);
	ret = i2c_transfer(client->adapter, msg, num_msg);
	LWIS_ATRACE_FUNC_INT_END(lwis_dev, trace_name,
				 msg[0].len + num_msg * I2C_TARGET_ADDR_BYTES);
	return (ret == num_msg) ? 0 : ret;
}

int lwis_i2c_set_state(struct lwis_i2c_device *i2c, const char *state_str)
{
	int ret;
	struct pinctrl_state *state;
	const char *state_to_set;

	if (!i2c || !i2c->state_pinctrl) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}

	state_to_set = i2c->pinctrl_default_state_only ? "default" : state_str;

	state = pinctrl_lookup_state(i2c->state_pinctrl, state_to_set);
	if (IS_ERR_OR_NULL(state)) {
		dev_err(i2c->base_dev.dev, "State %s not found (%ld)\n", state_str, PTR_ERR(state));
		return PTR_ERR(state);
	}

	ret = pinctrl_select_state(i2c->state_pinctrl, state);
	if (ret) {
		dev_err(i2c->base_dev.dev, "Error selecting state %s (%d)\n", state_str, ret);
		return ret;
	}

	return 0;
}

static int setup_i2c_read(struct lwis_i2c_device *i2c, uint64_t offset,
			  struct i2c_msg_context *context, struct i2c_msg *msg)
{
	struct i2c_client *client;
	unsigned int offset_bits;
	unsigned int offset_bytes;
	unsigned int value_bits;
	unsigned int value_bytes;

	if (!i2c || !i2c->client) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}
	client = i2c->client;

	offset_bits = i2c->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i2c->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	value_bits = i2c->base_dev.native_read_value_bitwidth;
	value_bytes = value_bits / BITS_PER_BYTE;
	if (!check_bitwidth(value_bits, MIN_DATA_BITS, MAX_DATA_BITS)) {
		dev_err(i2c->base_dev.dev, "Invalid value bitwidth %d\n", value_bits);
		return -EINVAL;
	}

	lwis_value_to_be_buf(offset, context->wbuf, offset_bytes);
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = offset_bytes;
	msg[0].buf = context->wbuf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = value_bytes;
	msg[1].buf = context->rbuf;

	return 0;
}

static int setup_i2c_write(struct lwis_i2c_device *i2c, uint64_t offset, uint64_t value,
			   struct i2c_msg_context *context, struct i2c_msg *msg)
{
	uint8_t *wbuf;
	struct i2c_client *client;
	unsigned int offset_bits;
	unsigned int value_bits;
	unsigned int offset_bytes;
	unsigned int value_bytes;

	if (!i2c || !i2c->client) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}
	client = i2c->client;

	if (i2c->base_dev.is_read_only) {
		dev_err(i2c->base_dev.dev, "Device is read only\n");
		return -EPERM;
	}

	offset_bits = i2c->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i2c->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	value_bits = i2c->base_dev.native_write_value_bitwidth;
	value_bytes = value_bits / BITS_PER_BYTE;
	if (!check_bitwidth(value_bits, MIN_DATA_BITS, MAX_DATA_BITS)) {
		dev_err(i2c->base_dev.dev, "Invalid value bitwidth %d\n", value_bits);
		return -EINVAL;
	}

	wbuf = context->lwbuf;
	lwis_value_to_be_buf(offset, wbuf, offset_bytes);
	lwis_value_to_be_buf(value, wbuf + offset_bytes, value_bytes);
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = offset_bytes + value_bytes;
	msg[0].buf = wbuf;

	return 0;
}

static int setup_i2c_read_batch(struct lwis_i2c_device *i2c, uint64_t start_offset,
				uint8_t *read_buf, int read_buf_size,
				struct i2c_msg_context *context, struct i2c_msg *msg)
{
	struct i2c_client *client;
	unsigned int offset_bits;
	unsigned int offset_bytes;

	if (!i2c || !i2c->client) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}
	client = i2c->client;

	offset_bits = i2c->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i2c->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	lwis_value_to_be_buf(start_offset, context->wbuf, offset_bytes);
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = offset_bytes;
	msg[0].buf = context->wbuf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = read_buf_size;
	msg[1].buf = read_buf;

	return 0;
}

static int setup_i2c_write_batch(struct lwis_i2c_device *i2c, uint64_t start_offset,
				 uint8_t *write_buf, int write_buf_size,
				 struct i2c_msg_context *context, struct i2c_msg *msg)
{
	struct i2c_client *client;
	unsigned int offset_bits;
	unsigned int offset_bytes;
	int msg_bytes;

	if (!i2c || !i2c->client) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}
	client = i2c->client;

	if (i2c->base_dev.is_read_only) {
		dev_err(i2c->base_dev.dev, "Device is read only\n");
		return -EPERM;
	}

	offset_bits = i2c->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i2c->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	msg_bytes = offset_bytes + write_buf_size;
	context->buf = lwis_allocator_allocate(&i2c->base_dev, msg_bytes, GFP_KERNEL);
	if (!context->buf) {
		dev_err(i2c->base_dev.dev, "Failed to allocate memory for I2C buffer\n");
		return -ENOMEM;
	}

	lwis_value_to_be_buf(start_offset, context->buf, offset_bytes);
	memcpy(context->buf + offset_bytes, write_buf, write_buf_size);
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = msg_bytes;
	msg[0].buf = context->buf;

	return 0;
}

int lwis_i2c_write(struct lwis_i2c_device *i2c, uint64_t offset, uint64_t value)
{
	int ret;
	u8 *buf;
	struct i2c_client *client;
	struct i2c_msg msg;
	unsigned int offset_bits;
	unsigned int value_bits;
	unsigned int offset_bytes;
	unsigned int value_bytes;
	int msg_bytes;

	if (!i2c || !i2c->client) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}
	client = i2c->client;

	if (i2c->base_dev.is_read_only) {
		dev_err(i2c->base_dev.dev, "Device is read only\n");
		return -EPERM;
	}

	offset_bits = i2c->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(i2c->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	value_bits = i2c->base_dev.native_write_value_bitwidth;
	value_bytes = value_bits / BITS_PER_BYTE;
	if (!check_bitwidth(value_bits, MIN_DATA_BITS, MAX_DATA_BITS)) {
		dev_err(i2c->base_dev.dev, "Invalid value bitwidth %d\n", value_bits);
		return -EINVAL;
	}

	msg_bytes = offset_bytes + value_bytes;
	buf = kmalloc(msg_bytes, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = msg_bytes;

	ret = perform_write_transfer(client, &msg, offset, offset_bytes, value_bytes, value,
				     &i2c->base_dev);
	if (ret) {
		dev_err(i2c->base_dev.dev, "I2C Write failed: Offset 0x%llx Value 0x%llx (%d)\n",
			offset, value, ret);
	}

	kfree(buf);
	return ret;
}

static void parse_i2c_result(struct lwis_io_entry *entry, struct i2c_msg_context *context,
			     struct i2c_msg *msg)
{
	if (entry->type == LWIS_IO_ENTRY_READ) {
		/* for i2c_read, msg[1].buf = context->rbuf */
		*(&entry->rw.val) = lwis_be_buf_to_value(context->rbuf, msg[1].len);
	}
}

int lwis_i2c_io_entry_mod(struct lwis_i2c_device *i2c, struct lwis_io_entry *entry)
{
	int ret;
	uint64_t reg_value;
	struct i2c_client *client;
	struct i2c_msg msg[2];
	struct i2c_msg_context context = {
		.wbuf = { 0 }, .lwbuf = { 0 }, .rbuf = { 0 }, .buf = NULL
	};

	if (!i2c || !i2c->client) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}
	client = i2c->client;

	if (!entry) {
		dev_err(i2c->base_dev.dev, "IO entry is NULL.\n");
		return -EINVAL;
	}

	ret = setup_i2c_read(i2c, entry->mod.offset, &context, msg);
	if (ret) {
		release_i2c_msg_context(&i2c->base_dev, &context);
		return ret;
	}

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 2) {
		release_i2c_msg_context(&i2c->base_dev, &context);
		if (ret >= 0)
			ret = -EIO;
		return ret;
	}

	reg_value = lwis_be_buf_to_value(context.rbuf, msg[1].len);
	reg_value &= ~entry->mod.val_mask;
	reg_value |= entry->mod.val_mask & entry->mod.val;
	release_i2c_msg_context(&i2c->base_dev, &context);
	return lwis_i2c_write(i2c, entry->mod.offset, reg_value);
}

static int setup_i2c_xfer(struct lwis_i2c_device *i2c_dev, struct lwis_io_entry *entry,
			  struct i2c_msg_context *context, struct i2c_msg *msgs)
{
	if (entry->type == LWIS_IO_ENTRY_READ) {
		return setup_i2c_read(i2c_dev, entry->rw.offset, context, msgs);
	} else if (entry->type == LWIS_IO_ENTRY_WRITE) {
		return setup_i2c_write(i2c_dev, entry->rw.offset, entry->rw.val, context, msgs);
	} else if (entry->type == LWIS_IO_ENTRY_READ_BATCH) {
		return setup_i2c_read_batch(i2c_dev, entry->rw_batch.offset, entry->rw_batch.buf,
					    entry->rw_batch.size_in_bytes, context, msgs);
	} else if (entry->type == LWIS_IO_ENTRY_WRITE_BATCH) {
		return setup_i2c_write_batch(i2c_dev, entry->rw_batch.offset, entry->rw_batch.buf,
					     entry->rw_batch.size_in_bytes, context, msgs);
	} else {
		dev_err(i2c_dev->base_dev.dev, "Invalid IO entry type: %d\n", entry->type);
		return -EINVAL;
	}
}

int lwis_i2c_io_entry_rw(struct lwis_i2c_device *i2c, struct lwis_io_entry *entry)
{
	int ret, nmsgs;
	struct i2c_msg msg[2];
	struct i2c_client *client;
	struct i2c_msg_context context = {
		.wbuf = { 0 }, .lwbuf = { 0 }, .rbuf = { 0 }, .buf = NULL
	};
	char trace_name[LWIS_MAX_NAME_STRING_LEN];

	if (!i2c || !i2c->client) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}

	if (!entry) {
		dev_err(i2c->base_dev.dev, "IO entry is NULL.\n");
		return -EINVAL;
	}

	client = i2c->client;

	nmsgs = get_msg_count(entry, /*entries_count=*/1);

	ret = setup_i2c_xfer(i2c, entry, &context, msg);
	if (ret) {
		dev_err(i2c->base_dev.dev, "failed to setup msg ret: %d\n", ret);
		release_i2c_msg_context(&i2c->base_dev, &context);
		return ret;
	}

	scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "i2c_msg_%s", i2c->base_dev.name);
	LWIS_ATRACE_FUNC_INT_BEGIN(&(i2c->base_dev), trace_name, get_msgs_len(msg, nmsgs));
	ret = i2c_transfer(client->adapter, msg, nmsgs);
	LWIS_ATRACE_FUNC_INT_END(&(i2c->base_dev), trace_name, get_msgs_len(msg, nmsgs));
	if (ret < nmsgs) {
		release_i2c_msg_context(&i2c->base_dev, &context);
		dev_err(i2c->base_dev.dev, "less msg received : %d, expected: %d.\n", ret, nmsgs);
		if (ret >= 0)
			ret = -EIO;
		return ret;
	}

	parse_i2c_result(entry, &context, msg);
	release_i2c_msg_context(&i2c->base_dev, &context);
	return 0;
}

int lwis_i2c_io_entries_rw(struct lwis_i2c_device *i2c, struct lwis_io_entry *entries,
			   int entries_cnt)
{
	struct i2c_msg *msgs;
	struct i2c_msg_context *contexts;
	struct i2c_client *client;
	int ret = 0;
	int nmsgs;
	int i;
	int msgs_cursor;
	char trace_name[LWIS_MAX_NAME_STRING_LEN];

	if (!i2c || !i2c->client) {
		pr_err("Cannot find i2c instance\n");
		return -ENODEV;
	}

	if (!entries) {
		dev_err(i2c->base_dev.dev, "IO entry is NULL.\n");
		return -EINVAL;
	}
	client = i2c->client;

	nmsgs = get_msg_count(entries, entries_cnt);

	msgs = lwis_allocator_allocate(&i2c->base_dev, nmsgs * sizeof(struct i2c_msg), GFP_KERNEL);
	if (!msgs)
		return -ENOMEM;

	/* It zeroes the members inside the i2c_msg struct */
	memset(msgs, 0, nmsgs * sizeof(struct i2c_msg));

	/* kcalloc already clear all contexts */
	contexts = lwis_allocator_allocate(
		&i2c->base_dev, entries_cnt * sizeof(struct i2c_msg_context), GFP_KERNEL);
	if (!contexts) {
		lwis_allocator_free(&i2c->base_dev, msgs);
		return -ENOMEM;
	}

	/* It zeroes the members inside the i2c_msg_context struct */
	memset(contexts, 0, entries_cnt * sizeof(struct i2c_msg_context));

	for (i = 0, msgs_cursor = 0; i < entries_cnt; ++i) {
		ret = setup_i2c_xfer(i2c, &entries[i], &contexts[i], &msgs[msgs_cursor]);
		if (ret)
			goto err_release;

		msgs_cursor += get_msg_count(&entries[i], /*entries_count=*/1);
	}

	scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "i2c_msg_%s", i2c->base_dev.name);
	LWIS_ATRACE_FUNC_INT_BEGIN(&(i2c->base_dev), trace_name, get_msgs_len(msgs, nmsgs));
	ret = i2c_transfer(client->adapter, msgs, nmsgs);
	LWIS_ATRACE_FUNC_INT_END(&(i2c->base_dev), trace_name, get_msgs_len(msgs, nmsgs));

	if (ret < nmsgs) {
		dev_err(i2c->base_dev.dev, "less msg received : %d, expected: %d.\n", ret, nmsgs);
		if (ret >= 0)
			ret = -EIO;
		goto err_release;
	} else {
		ret = 0;
	}

	for (i = 0, msgs_cursor = 0; i < entries_cnt; ++i) {
		parse_i2c_result(&entries[i], &contexts[i], &msgs[msgs_cursor]);
		msgs_cursor += get_msg_count(&entries[i], /*entries_count=*/1);
	}

err_release:
	for (i = 0; i < entries_cnt; ++i)
		release_i2c_msg_context(&i2c->base_dev, &contexts[i]);
	lwis_allocator_free(&i2c->base_dev, msgs);
	lwis_allocator_free(&i2c->base_dev, contexts);

	return ret;
}
