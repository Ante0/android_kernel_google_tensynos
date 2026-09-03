// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint services source.
 */

#include <linux/debugfs.h>
#include <linux/math.h>
#include <linux/mutex.h>
#include <net/genetlink.h>

#include "fwtp.h"
#include "fwtp_decode.h"
#include "fwtp_entry.h"
#define CREATE_TRACE_POINTS
#include "fwtp_ftrace.h"
#include "fwtp_protocol.h"
#include "soc/google/google_gtc.h"
#include "soc/google/google_timestamp_sync.h"

/*******************************************************************************
 * Data structures and defs.
 ******************************************************************************/

/* FWTP device service record. */
static struct fwtp_dev_svc fwtp_dev_svc = {
	.dev_list = LIST_HEAD_INIT(fwtp_dev_svc.dev_list),
};

/* Define the FWTP Generic Netlink family. */
/* TODO: b/489522286 - Support Netlink operations. */
static struct genl_family fwtp_genl_family = {
	.name = "FWTP",
	.version = 1,
	.maxattr = 0,
	.netnsok = true,
	.module = THIS_MODULE,
	.ops = NULL,
	.n_ops = 0,
};

/*******************************************************************************
 * Internal FWTP kernel device services.
 ******************************************************************************/

/**
 * fwtp_dev_append_output - Appends string to kernel log and ftrace.
 *
 * @printer_ctx: Printer context.
 * @str: String to append to output.
 */
static void fwtp_dev_append_output(struct fwtp_printer_ctx *printer_ctx,
				   const char *str)
{
	struct fwtp_dev *fwtp_dev = printer_ctx->append_output_ctx;

	if (fwtp_dev->log_enabled)
		dev_info(fwtp_dev->dev, "%s", str);
	if (fwtp_dev->ftrace_enabled)
		trace_fwtp(str);
}

/**
 * fwtp_seq_file_append_output - Appends output to a seq_file.
 *
 * @printer_ctx: Printer context containing the seq_file.
 * @str: String to append.
 */
static void fwtp_seq_file_append_output(struct fwtp_printer_ctx *printer_ctx,
					const char *str)
{
	struct seq_file *seq_file = printer_ctx->append_output_ctx;

	seq_puts(seq_file, str);
}

/**
 * fwtp_dev_fetch_ring_info - Fetches ring info via IPC.
 *
 * @fwtp_dev: FWTP kernel device for which to get ring info.
 * @ring_num: Ring number to get.
 * @info: Pointer to the ring info message structure to populate.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_dev_fetch_ring_info(struct fwtp_dev *fwtp_dev, int ring_num,
				    struct fwtp_msg_get_ring_info *info)
{
	u16 rx_msg_data_size;
	fwtp_error_code_t err;

	/* Send a get ring info message. */
	info->base.type = kFwtpMsgTypeGetRingInfo;
	info->ring_num = ring_num;
	err = fwtp_dev->fwtp_ipc_client.fwtp_if.send_message(
		&(fwtp_dev->fwtp_ipc_client.fwtp_if), info, sizeof(*info),
		sizeof(*info), &rx_msg_data_size);
	if (err != kFwtpOk) {
		dev_err(fwtp_dev->dev,
			"Failed to send a get ring info message with error %d.\n",
			err);
		return -EIO;
	}
	if (rx_msg_data_size < sizeof(struct fwtp_msg_get_ring_info)) {
		dev_err(fwtp_dev->dev,
			"Received get ring info response size %d too short.\n",
			rx_msg_data_size);
		return -EFAULT;
	}
	if (info->base.error != kFwtpOk) {
		dev_err(fwtp_dev->dev,
			"Get ring info request failed with error %d.\n",
			info->base.error);
		return -EFAULT;
	}

	return 0;
}

/*******************************************************************************
 * Debugfs functions.
 ******************************************************************************/

/**
 * fwtp_debugfs_filter_list_show - Handle debugfs attribute show.
 *
 * @seq_file: Sequence file to use to show the attribute.
 * @private: Private data.
 *
 * Handles a debugfs show attribute operation to show the tracepoint filter
 * list.
 *
 * The FWTP device is contained in the file private data.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_debugfs_filter_list_show(struct seq_file *seq_file,
					 void *private)
{
	struct fwtp_dev *fwtp_dev = seq_file->private;
	const int filter_count = fwtp_dev->fwtp_ipc_client.filter_count;
	char **filter_name_table = fwtp_dev->fwtp_ipc_client.filter_name_table;

	/* Print the filter names. */
	for (int i = 0; i < filter_count; ++i)
		seq_printf(seq_file, "%s\n", filter_name_table[i]);

	return 0;
}

/* Define the FWTP debugfs interface to show the filter list. */
DEFINE_SHOW_ATTRIBUTE(fwtp_debugfs_filter_list);

/**
 * fwtp_debugfs_filter_config_write - Write the tracepoint filter config.
 *
 * @file: File to write.
 * @buf: Buffer containing data to write.
 * @count: Count of the number of bytes to write.
 * @p_position: Position in file to write.
 * @blocking: If true, configure filters to block; otherwise, configure filters
 *            to not block.
 *
 * Writes the configuration of the tracepoint filters as specified by blocking.
 * The list of filters to configure is specified in the write buffer and is
 * composed of a comma separated list of filter names to configure.
 *
 * The FWTP device is contained in the file private data.
 */
static ssize_t fwtp_debugfs_filter_config_write(struct file *file,
						const char __user *buf,
						size_t count,
						loff_t *p_position,
						bool blocking)
{
	/* Dynamic memory to clean up before returning. */
	char *filter_config_str = NULL;
	struct fwtp_msg_set_filter_config *msg_set_filter_config = NULL;

	struct fwtp_dev *fwtp_dev = file->private_data;
	char *p_filter_config_str;
	char *p_filter_name_list;
	int filter_name_count;
	struct fwtp_msg_filter_config_entry *config_list;
	size_t msg_size;
	int config_count;
	uint16_t rx_msg_data_size;
	bool select_all_filters = false;
	fwtp_error_code_t err;
	ssize_t ret;

	/* Get the null-terminated filter configuration string. */
	filter_config_str = kvmalloc(count + 1, GFP_KERNEL);
	if (!filter_config_str) {
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(filter_config_str, buf, count)) {
		ret = -EFAULT;
		goto out;
	}
	filter_config_str[count] = '\0';

	/*
	 * Convert the filter configuration string to a list of filter name
	 * strings and count the number of filter names. strsep will add a null
	 * terminating character after each filter name string.
	 *
	 * Check if all filter names should be selected.
	 */
	p_filter_config_str = filter_config_str;
	filter_name_count = 0;
	while (p_filter_config_str) {
		const char *filter_name;

		/* Convert the next filter name. */
		filter_name = strsep(&p_filter_config_str, ",\n");
		filter_name_count++;

		/* If selecting all filter names, exit loop. */
		if (strcmp(filter_name, FWTP_SELECT_ALL_FILTERS_STRING) == 0) {
			select_all_filters = true;
			filter_name_count =
				fwtp_dev->fwtp_ipc_client.filter_count;
			break;
		}
	}

	/* Allocate the set filter config message buffer. */
	msg_size = struct_size(msg_set_filter_config, config_list,
			       filter_name_count);
	msg_set_filter_config = kvmalloc(msg_size, GFP_KERNEL);
	if (!msg_set_filter_config) {
		ret = -ENOMEM;
		goto out;
	}
	config_list = msg_set_filter_config->config_list;

	/* Add the filter configuration to the set filter config message. */
	config_count = 0;
	p_filter_name_list = filter_config_str;
	while (config_count < filter_name_count) {
		char *filter_name;
		int filter_index;

		/* Get the index of the next filter. */
		if (select_all_filters) {
			filter_index = config_count;
		} else {
			/* Look up the index of the next filter name. */
			filter_name = p_filter_name_list;
			p_filter_name_list += strlen(filter_name) + 1;
			filter_index = fwtp_ipc_client_get_filter_index(
				&(fwtp_dev->fwtp_ipc_client), filter_name);
		}

		/*
		 * Add the filter configuration to the set filter config
		 * message.
		 */
		if (filter_index >= 0) {
			config_list[config_count].filter_index = filter_index;
			config_list[config_count].blocking = blocking;
			config_count++;
		}

		/*
		 * Stop adding filters if the end of the filter name list has
		 * been reached.
		 */
		if (p_filter_name_list >= (filter_config_str + count))
			break;
	}

	/* Send a set filter config message. */
	msg_set_filter_config->base.type = kFwtpMsgTypeSetFilterConfig;
	msg_set_filter_config->config_count = config_count;
	err = fwtp_dev->fwtp_ipc_client.fwtp_if.send_message(
		&(fwtp_dev->fwtp_ipc_client.fwtp_if), msg_set_filter_config,
		msg_size, msg_size, &rx_msg_data_size);
	if (err != kFwtpOk) {
		dev_err(fwtp_dev->dev,
			"Failed to send a set filter config message with error %d.\n",
			err);
		ret = -EIO;
		goto out;
	}
	if (rx_msg_data_size < sizeof(struct fwtp_msg_base)) {
		dev_err(fwtp_dev->dev,
			"Received set filter config response size %d too short.\n",
			rx_msg_data_size);
		ret = -EPROTO;
		goto out;
	}
	if (msg_set_filter_config->base.error != kFwtpOk) {
		dev_err(fwtp_dev->dev,
			"Set filter config request failed with error %d.\n",
			msg_set_filter_config->base.error);
		ret = -EPROTO;
		goto out;
	}

	/* All bytes were written. */
	ret = count;

out:
	/* Clean up. */
	kvfree(filter_config_str);
	kvfree(msg_set_filter_config);

	return ret;
}

/**
 * fwtp_debugfs_filter_block_write - Write the tracepoint filter config to
 *                                   block.
 *
 * @file: File to write.
 * @buf: Buffer containing data to write.
 * @count: Count of the number of bytes to write.
 * @p_position: Position in file to write.
 *
 * Writes the configuration of the tracepoint filters to block tracepoints. The
 * list of filters to configure is specified in the write buffer and is composed
 * of a comma separated list of filter names to configure.
 *
 * The FWTP device is contained in the file private data.
 */
static ssize_t fwtp_debugfs_filter_block_write(struct file *file,
					       const char __user *buf,
					       size_t count, loff_t *p_position)
{
	return fwtp_debugfs_filter_config_write(file, buf, count, p_position,
						true);
}

/*
 * Define the FWTP debugfs interface to configure the filters to block
 * tracepoints.
 */
static const struct file_operations fwtp_debugfs_filter_block_fops = {
	.open = simple_open,
	.write = fwtp_debugfs_filter_block_write,
};

/**
 * fwtp_debugfs_filter_unblock_write - Write the tracepoint filter config to
 *                                     not block.
 *
 * @file: File to write.
 * @buf: Buffer containing data to write.
 * @count: Count of the number of bytes to write.
 * @p_position: Position in file to write.
 *
 * Writes the configuration of the tracepoint filters to not block tracepoints.
 * The list of filters to configure is specified in the write buffer and is
 * composed of a comma separated list of filter names to configure.
 *
 * The FWTP device is contained in the file private data.
 */
static ssize_t fwtp_debugfs_filter_unblock_write(struct file *file,
						 const char __user *buf,
						 size_t count,
						 loff_t *p_position)
{
	return fwtp_debugfs_filter_config_write(file, buf, count, p_position,
						false);
}

/*
 * Define the FWTP debugfs interface to configure the filters to not block
 * tracepoints.
 */
static const struct file_operations fwtp_debugfs_filter_unblock_fops = {
	.open = simple_open,
	.write = fwtp_debugfs_filter_unblock_write,
};

/**
 * fwtp_debugfs_add_tracepoint_write - Handle write.
 *
 * @data: Pointer to FWTP device.
 * @val: Write value.
 *
 * Handles a debugfs write operation to add tracepoint in FWTP device.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_debugfs_add_tracepoint_write(void *data, u64 val)
{
	struct fwtp_dev *fwtp_dev = data;

	struct fwtp_msg_add_tracepoint msg_add_tracepoint;
	uint16_t rx_msg_data_size;
	fwtp_error_code_t err;

	/* Set up a message to add a tracepoint to ring zero. */
	msg_add_tracepoint.base.type = kFwtpMsgTypeAddTracepoint;
	msg_add_tracepoint.ring_num = 0;
	msg_add_tracepoint.payload = (u32)val;

	err = fwtp_dev->fwtp_ipc_client.fwtp_if.send_message(
		&(fwtp_dev->fwtp_ipc_client.fwtp_if), &msg_add_tracepoint,
		sizeof(msg_add_tracepoint), sizeof(msg_add_tracepoint),
		&rx_msg_data_size);
	if (err != kFwtpOk) {
		dev_err(fwtp_dev->dev,
			"Failed to send an add tracepoint message with error %d.\n",
			err);
		return -EIO;
	}
	if (rx_msg_data_size < sizeof(struct fwtp_msg_base)) {
		dev_err(fwtp_dev->dev,
			"Received add tracepoint response size %d too short.\n",
			rx_msg_data_size);
		return -EPROTO;
	}
	if (msg_add_tracepoint.base.error != kFwtpOk) {
		dev_err(fwtp_dev->dev,
			"Add tracepoint request failed with error %d.\n",
			msg_add_tracepoint.base.error);
		return -EPROTO;
	}

	return 0;
}

/* Define the FWTP add tracepoint debugfs interface. */
DEFINE_DEBUGFS_ATTRIBUTE(fwtp_debugfs_add_tracepoint_fops, NULL,
			 fwtp_debugfs_add_tracepoint_write, "%llu\n");

/**
 * fwtp_debugfs_trace_show_memio - Handle seq_file show for the trace node via
 *                                 memory I/O.
 *
 * @fwtp_dev: FWTP kernel device.
 * @printer_ctx: Printer context to use.
 *
 * Prints the entire contents of the FWTP memory ring buffer.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_debugfs_trace_show_memio(struct fwtp_dev *fwtp_dev,
					 struct fwtp_printer_ctx *printer_ctx)
{
	uint8_t decode_buffer[FWTP_DECODE_BUFFER_SIZE];
	int err;

	/* Print all tracepoints in all client rings. */
	for (int i = 0; i < fwtp_dev->fwtp_ipc_client.client_ring_count; i++) {
		struct fwtp_ipc_client_ring *client_ring =
			&(fwtp_dev->fwtp_ipc_client.client_ring_list[i]);
		struct fwtp_msg_get_ring_info msg_get_ring_info = { 0 };
		struct tracepoint_ring trace_ring;

		/* Fetch the latest ring info. */
		err = fwtp_dev_fetch_ring_info(fwtp_dev, client_ring->ring_num,
					       &msg_get_ring_info);
		if (err)
			return err;

		/* Set up a ring from which to print. */
		trace_ring.version = msg_get_ring_info.version;
		trace_ring.timestamp_hz = msg_get_ring_info.timestamp_hz;
		trace_ring.size = msg_get_ring_info.ring_buffer_size;
		trace_ring.head_offset = 0;
		trace_ring.tail_offset = msg_get_ring_info.tail_offset;
		trace_ring.buffer = fwtp_dev->memio_ring->buffer;

		/* Print the tracepoints. */
		fwtp_print_ring_entries_with_decode_buffer(
			printer_ctx, &trace_ring, 0, decode_buffer,
			sizeof(decode_buffer));
	}

	return 0;
}

/**
 * fwtp_debugfs_trace_show_ipc - Handle seq_file show for the trace node via
 *                               IPC.
 *
 * @fwtp_dev: FWTP kernel device.
 * @printer_ctx: Printer context to use.
 *
 * Prints the entire contents of the FWTP tracepoint buffer using IPC to fetch
 * the tracepoints.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_debugfs_trace_show_ipc(struct fwtp_dev *fwtp_dev,
				       struct fwtp_printer_ctx *printer_ctx)
{
	fwtp_error_code_t err;

	err = fwtp_ipc_client_print_tracepoints(&(fwtp_dev->fwtp_ipc_client),
						printer_ctx, true);
	if (err != kFwtpOk)
		return -EIO;

	return 0;
}

/**
 * fwtp_debugfs_trace_show - Handle seq_file show for the trace node.
 *
 * @seq_file: Sequence file to use to show the attribute.
 * @private: Private data.
 *
 * Prints the entire contents of the FWTP memory ring buffer.
 *
 * CPM, CAP and VFA have DRAM buffers. To show their content, we mimic how
 * initial subscribe request for them is handled: message is sent to firmware to
 * update tail offset and initial notification is sent. This results in full
 * contents of DRAM buffers to be printed. For trace_show, we query firmware for
 * ring info to get recent tail offset and then print its contents.
 *
 * GDMC does not have DRAM buffer. For it, we use
 * fwtp_ipc_client_print_tracepoints function, based on retrieving tracepoint
 * buffer content from firmware via IPC. Flow is similar to how notification
 * request is handled for GDMC.
 *
 * We do not share state with "streaming" side of kernel driver, to minimize
 * data races. In particular, from global state, we use only constant data
 * populated during probing: DRAM buffer address, string table info, fwtp_if IPC
 * interface (in particular send_message pointer). Other then that, everything
 * is local to given read invocation: printer context, ring copy, decode buffer
 * and head_offset.
 *
 * This approach takes the same synchronization compromises as the streaming
 * part: some traces may be lost due to SRAM buffer overruns, some prefix of
 * displayed tracepoints may be overwritten if firmware is fast enough with
 * tracepoint generation. As firmware is pushing data to the DRAM buffers, we
 * would need to disable/enable tracepoint generation there or block the
 * firmware - infeasible given the timescales of tracepoint formatting in kernel
 * and rate of CPM/CAP tracepoint generation.
 */
static int fwtp_debugfs_trace_show(struct seq_file *seq_file, void *private)
{
	struct fwtp_dev *fwtp_dev = seq_file->private;
	struct fwtp_printer_ctx printer_ctx = { 0 };

	/* Initialize printer context. */
	printer_ctx.append_output = fwtp_seq_file_append_output;
	printer_ctx.append_output_ctx = seq_file;
	fwtp_ipc_client_printer_ctx_init(&(fwtp_dev->fwtp_ipc_client),
					 &printer_ctx);

	if (fwtp_dev->memio_ring)
		return fwtp_debugfs_trace_show_memio(fwtp_dev, &printer_ctx);
	else
		return fwtp_debugfs_trace_show_ipc(fwtp_dev, &printer_ctx);
}

DEFINE_SHOW_ATTRIBUTE(fwtp_debugfs_trace);

/**
 * fwtp_debugfs_notify_bytes_read - Handle debugfs read.
 *
 * @data: Pointer to FWTP device.
 * @val: Pointer to read value.
 *
 * Handles a debugfs read operation to get the tracepoint notification byte
 * count.
 *
 * Return: 0 on success.
 */
static int fwtp_debugfs_notify_bytes_read(void *data, u64 *val)
{
	struct fwtp_dev *fwtp_dev = data;

	*val = fwtp_dev->notify_byte_count;

	return 0;
}

/**
 * fwtp_debugfs_notify_bytes_write - Handle debugfs write.
 *
 * @data: Pointer to FWTP device.
 * @val: Write value.
 *
 * Handles a debugfs write operation to set the tracepoint notification byte
 * count.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_debugfs_notify_bytes_write(void *data, u64 val)
{
	struct fwtp_dev *fwtp_dev = data;

	if (val > U32_MAX)
		return -EINVAL;

	fwtp_dev->notify_byte_count = (u32)val;

	return 0;
}

/* Define the FWTP notify bytes debugfs interface. */
DEFINE_DEBUGFS_ATTRIBUTE(fwtp_debugfs_notify_bytes_fops,
			 fwtp_debugfs_notify_bytes_read,
			 fwtp_debugfs_notify_bytes_write, "%llu\n");

/*******************************************************************************
 * Internal FWTP kernel device services.
 ******************************************************************************/

/**
 * fwtp_dev_print_slice_string - Prints a tracepoint slice string.
 *
 * Prints a tracepoint slice string using the printf format string specified by
 * fmt_str using data from data_items. The tracepoint slice string is printed to
 * the buffer specified by slice_str and slice_str_size.
 *
 * @slice_str: Buffer in which to write slice string.
 * @slice_str_size: Size of slice string buffer.
 * @fmt_str: Slice printf format string.
 * @data_items: Slice tracepoint data items.
 */
static void fwtp_dev_print_slice_string(char *slice_str, size_t slice_str_size,
					const char *fmt_str,
					struct fwtp_data_item_list *data_items)
{
	char *conv_spec;
	bool fmt_uses_one_32bit_arg = false;

	/*
	 * Determine whether the format string uses one, and only one, 32-bit
	 * argument.
	 *
	 * TODO: b/483736307 - Support more than just a single 32-bit argument.
	 */
	conv_spec = strchr(fmt_str, '%');
	if (conv_spec && !strchr(conv_spec + 1, '%')) {
		char conv_char = conv_spec[1];

		if (strchr("diouxX", conv_char))
			fmt_uses_one_32bit_arg = true;
	}

	/* Print the slice string with or without a single 32-bit argument. */
	if (fmt_uses_one_32bit_arg) {
		u32 data = 0;

		fwtp_get_next_data_item(data_items, &data, sizeof(data));
		snprintf(slice_str, slice_str_size, fmt_str, data);
	} else {
		strscpy(slice_str, fmt_str, slice_str_size);
	}
}

/*******************************************************************************
 * External FWTP kernel device services.
 ******************************************************************************/

/**
 * fwtp_dev_init - Initializes an FWTP kernel device.
 *
 * @fwtp_dev: FWTP kernel device to initialize.
 *
 * Return: 0 on success, non-zero error code on error.
 */
int fwtp_dev_init(struct fwtp_dev *fwtp_dev)
{
	int rv;
	fwtp_error_code_t err;
	struct dentry *dentry;

	/*
	 * Set up printer context. Tracepoint lines shouldn't have new-lines
	 * with ftrace.
	 */
	fwtp_dev->printer_ctx.append_output = fwtp_dev_append_output;
	fwtp_dev->printer_ctx.append_output_ctx = fwtp_dev;
	fwtp_dev->printer_ctx.post_process = fwtp_dev_printer_post_process;
	fwtp_dev->printer_ctx.data_items.data_buffer =
		fwtp_dev->printer_data_buffer;
	fwtp_dev->printer_ctx.data_items.data_buffer_size =
		sizeof(fwtp_dev->printer_data_buffer);
	fwtp_dev->printer_ctx.output_buffer = fwtp_dev->printer_buffer;
	fwtp_dev->printer_ctx.output_buffer_size =
		sizeof(fwtp_dev->printer_buffer);
	fwtp_dev->printer_ctx.dont_append_new_line = true;

	/* Set up the FWTP interface platform. */
	fwtp_dev->fwtp_ipc_client.fwtp_if.platform.dev = fwtp_dev->dev;

	/* Create the various device debugfs files. */
	dentry = debugfs_create_file("filter_list", 0440,
				     fwtp_dev->root_debugfs, fwtp_dev,
				     &fwtp_debugfs_filter_list_fops);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create filter list debugfs file.\n");
		return PTR_ERR(dentry);
	}
	dentry = debugfs_create_file("filter_block", 0220,
				     fwtp_dev->root_debugfs, fwtp_dev,
				     &fwtp_debugfs_filter_block_fops);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create filter block debugfs file.\n");
		return PTR_ERR(dentry);
	}
	dentry = debugfs_create_file("filter_unblock", 0220,
				     fwtp_dev->root_debugfs, fwtp_dev,
				     &fwtp_debugfs_filter_unblock_fops);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create filter unblock debugfs file.\n");
		return PTR_ERR(dentry);
	}
	dentry = debugfs_create_file("add_tracepoint", 0220,
				     fwtp_dev->root_debugfs, fwtp_dev,
				     &fwtp_debugfs_add_tracepoint_fops);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create add tracepoint debugfs file.\n");
		return PTR_ERR(dentry);
	}
	dentry = debugfs_create_file("trace", 0444, fwtp_dev->root_debugfs,
				     fwtp_dev, &fwtp_debugfs_trace_fops);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create trace debugfs file.\n");
		return PTR_ERR(dentry);
	}
	dentry = debugfs_create_file("notify_bytes", 0660,
				     fwtp_dev->root_debugfs, fwtp_dev,
				     &fwtp_debugfs_notify_bytes_fops);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create notify_bytes debugfs file.\n");
		return PTR_ERR(dentry);
	}

	/* Register the FWTP interface. */
	err = fwtp_ipc_client_register(&(fwtp_dev->fwtp_ipc_client));
	if (err != kFwtpOk)
		return -EFAULT;

	/* Get the filter list. */
	err = fwtp_ipc_client_get_filter_list(&(fwtp_dev->fwtp_ipc_client));
	if (err != kFwtpOk)
		return -EFAULT;

	/* Add device to device list. */
	list_add(&fwtp_dev->list_entry, &fwtp_dev_svc.dev_list);
	fwtp_dev->in_list = true;

	/* Register the FWTP Generic Netlink family. */
	if (!fwtp_dev_svc.genl_family_registered) {
		rv = genl_register_family(&fwtp_genl_family);
		if (rv) {
			dev_err(fwtp_dev->dev,
				"Failed to register FWTP Generic Netlink family with error %d.\n",
				rv);
			return rv;
		}
		fwtp_dev_svc.genl_family_registered = true;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fwtp_dev_init);

/**
 * fwtp_dev_deinit - Deinitializes an FWTP kernel device.
 *
 * @fwtp_dev: FWTP kernel device to deinitialize.
 */
void fwtp_dev_deinit(struct fwtp_dev *fwtp_dev)
{
	/* Remove device from device list. */
	if (fwtp_dev->in_list) {
		list_del(&fwtp_dev->list_entry);
		fwtp_dev->in_list = false;
	}

	/*
	 * Unregister the FWTP Generic Netlink family if no devices are in list.
	 */
	if (list_empty(&fwtp_dev_svc.dev_list) &&
	    fwtp_dev_svc.genl_family_registered) {
		genl_unregister_family(&fwtp_genl_family);
		fwtp_dev_svc.genl_family_registered = false;
	}

	/* Unregister the FWTP interface. */
	fwtp_ipc_client_unregister(&(fwtp_dev->fwtp_ipc_client));
}
EXPORT_SYMBOL_GPL(fwtp_dev_deinit);

/**
 * fwtp_dev_get_memio_ring - Gets an FWTP ring with mem I/O access.
 *
 * Returns in ring an FWTP ring with the ring number specified by ring_num for
 * the FWTP kernel device specified by fwtp_dev. The ring buffer is set up for
 * memory I/O to access a remote tracepoint ring.
 *
 * The ring record will be filled in with the ring info. If available, the ring
 * record will include a buffer address that may be used to directly read the
 * ring buffer; if that's not available, the ring buffer will be NULL.
 *
 * @fwtp_dev: FWTP kernel device for which to get ring.
 * @ring_num: Ring number to get.
 * @ring: Returned ring.
 */
int fwtp_dev_get_memio_ring(struct fwtp_dev *fwtp_dev, int ring_num,
			    struct tracepoint_ring *ring)
{
	struct fwtp_msg_get_ring_info msg_get_ring_info = { 0 };
	int ret;

	ret = fwtp_dev_fetch_ring_info(fwtp_dev, ring_num, &msg_get_ring_info);
	if (ret)
		return ret;

	/* Get the ring info from the response. */
	ring->version = msg_get_ring_info.version;
	ring->timestamp_hz = msg_get_ring_info.timestamp_hz;
	ring->size = msg_get_ring_info.ring_buffer_size;
	ring->tail_offset = msg_get_ring_info.tail_offset;
	if (msg_get_ring_info.buffer_soc_addr) {
		ring->buffer = devm_ioremap(fwtp_dev->dev,
					    msg_get_ring_info.buffer_soc_addr,
					    msg_get_ring_info.ring_buffer_size);
	} else {
		ring->buffer = NULL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fwtp_dev_get_memio_ring);

/**
 * fwtp_dev_get_boottime_timestamp - Returns a given timestamp converted to boot
 *                                   time ticks.
 *
 * On some subsystems (e.g., LGA GDMC), the tracepoint timestamps don't use the
 * GTC clock. In these cases, timestamp_hz will be different than the GTC tick
 * Hz. This function converts the given tracepoint timestamp to GTC ticks using
 * the following formula:
 *
 *   gtc_timestamp = timestamp * gtc_hz / timestamp_hz
 *
 * @timestamp: Timestamp to convert.
 * @timestamp_hz: Timestamp tick frequency in Hz.
 *
 * Return: Positive boot time on success, 0 if timestamp is from before boot.
 */
u64 fwtp_dev_get_boottime_timestamp(u64 timestamp, u32 timestamp_hz)
{
	u64 gtc_hz = goog_gtc_get_freq_hz();
	u64 gtc_timestamp;
	u64 boottime_timestamp;

	/*
	 * Convert the timestamp to GTC ticks. If the timestamp and GTC tick
	 * frequencies are the same, or the tick frequency is not given, use the
	 * unmodified timestamp as GTC ticks.
	 */
	if ((timestamp_hz == gtc_hz) || !timestamp_hz)
		gtc_timestamp = timestamp;
	else
		gtc_timestamp = mult_frac(timestamp, gtc_hz, timestamp_hz);

	/* Convert the GTC time to boot time. */
	boottime_timestamp = goog_gtc_ticks_to_boottime(gtc_timestamp);

	/* If goog_gtc_ticks_to_boottime underflowed, return 0. */
	if (boottime_timestamp > S64_MAX)
		return 0;

	return boottime_timestamp;
}
EXPORT_SYMBOL_GPL(fwtp_dev_get_boottime_timestamp);

/**
 * fwtp_dev_printer_post_process - Performs post-processing on FWTP tracepoints.
 *
 * Runs FWTP tracepoint post-processing using the decoded tracepoint with the
 * type specified by type, timestamp specified by timestamp, title string
 * specified by str_id and str, and data items specified by data_items. This
 * function doesn't modify the tracepoint or printer output, but it may collect
 * information from the tracepoint for other uses (e.g., generating a SEM
 * report). The printer context is specified by printer_ctx.
 *
 * @printer_ctx: Printer context.
 * @type: Tracepoint type.
 * @timestamp: Tracepoint timestamp.
 * @str_id: Tracepoint title string ID.
 * @str: Tracepoint title string.
 * @data_items: Tracepoint data items.
 */
void fwtp_dev_printer_post_process(struct fwtp_printer_ctx *printer_ctx,
				   unsigned int type, u64 timestamp, u32 str_id,
				   const char *str,
				   struct fwtp_data_item_list *data_items)
{
	u64 boottime_timestamp = fwtp_dev_get_boottime_timestamp(
		timestamp, printer_ctx->timestamp_hz);
	struct fwtp_dev *fwtp_dev =
		container_of(printer_ctx, struct fwtp_dev, printer_ctx);
	const char *category = printer_ctx->name ?: "fwtp";
	char slice_str[FWTP_PRINTER_BUFFER_SIZE];
	u32 data = 0;

	/* Drop traces from before kernel boot. */
	if (boottime_timestamp == 0)
		return;

	/* Ensure the boot time is monotonically increasing. */
	boottime_timestamp =
		max(boottime_timestamp, fwtp_dev->prev_boottime_timestamp);
	fwtp_dev->prev_boottime_timestamp = boottime_timestamp;

	/* Decode tracepoint. */
	if (fwtp_dev->decoder) {
		uint8_t *p_starting_next_data_item =
			data_items->p_next_data_item;
		int data_item_size = fwtp_get_next_data_item(data_items, &data,
							     sizeof(data));
		if (data_item_size > 0 && data_item_size <= sizeof(data)) {
			fwtp_decode_tracepoint(fwtp_dev, str_id, data,
					       boottime_timestamp);
		}
		/* Reset data_items. */
		data_items->p_next_data_item = p_starting_next_data_item;
	}

	/* Post-process tracepoints for Perfetto. */
	switch (type) {
	case FWTP_LL_ENTRY_TYPE_TRACE_COUNTER:
		data = 0;
		fwtp_get_next_data_item(data_items, &data, sizeof(data));
		trace_fwtp_perfetto_counter(boottime_timestamp, 0, category,
					    str, data);
		break;
	case FWTP_LL_ENTRY_TYPE_TRACE_BEGIN:
		fwtp_dev_print_slice_string(slice_str, sizeof(slice_str), str,
					    data_items);
		trace_fwtp_perfetto_slice(boottime_timestamp, 0, category,
					  slice_str, true);
		break;
	case FWTP_LL_ENTRY_TYPE_TRACE_END:
		fwtp_dev_print_slice_string(slice_str, sizeof(slice_str), str,
					    data_items);
		trace_fwtp_perfetto_slice(boottime_timestamp, 0, category,
					  slice_str, false);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(fwtp_dev_printer_post_process);

/* Module info. */
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google firmware tracepoint");
MODULE_LICENSE("GPL");
