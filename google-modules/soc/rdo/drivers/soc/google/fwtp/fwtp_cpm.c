// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 *
 * CPM firmware tracepoint driver.
 */

#include <linux/debugfs.h>
#include <linux/devm-helpers.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include <soc/google/goog_mba_cpm_iface.h>
#include <soc/google/goog_cpm_service_ids.h>

#include "cpm_decoder/cpm_trace.h"
#include "cpm_decoder/cpm_tracepoint_decoder.h"
#include "fwtp.h"
#include "fwtp_entry.h"
#include "fwtp_protocol.h"

/*******************************************************************************
 * Data structures and defs.
 ******************************************************************************/

/* Mailbox request timeout time in milliseconds. */
#define FWTP_MBA_REQ_TIMEOUT_MS 3000

/* Set of FWTP devices. */
enum fwtp_cpm_dev_id_t {
	FWTP_CPM_DEV_ID_CPM = 0,
	FWTP_CPM_DEV_ID_CAP,
	FWTP_CPM_DEV_ID_VFA,
	FWTP_CPM_DEV_NUM
};

/**
 * struct fwtp_cpm_dev - Structure representing a CPM FWTP device.
 *
 * @dev: Kernel device record.
 * @fwtp_dev_list: List of FWTP sub-devices.
 * @cpm_client: Mailbox interface client to CPM.
 * @mba_channel: CPM mailbox channel.
 * @reserved_mem_initialized: If true, device reserved memory has been
 *                            initialized.
 * @dma_size: Size of DMA memory.
 * @dma_base: Base address of DMA memory.
 * @dma_base_phys: Physical base address of DMA memory.
 * @ring_notify_work: Work item for tracepoint ring notifications.
 * @debugfs_root: Root debugfs directory of device.
 */
struct fwtp_cpm_dev {
	struct device *dev;
	struct fwtp_dev fwtp_dev_list[FWTP_CPM_DEV_NUM];
	struct cpm_iface_client *cpm_client;
	u32 mba_channel;
	bool reserved_mem_initialized;
	size_t dma_size;
	void *dma_base;
	dma_addr_t dma_base_phys;
	struct work_struct ring_notify_work;
	struct dentry *debugfs_root;
};

/**
 * struct cpm_mba_fwtp_msg - CPM mailbox FWTP message.
 *
 * @mba_header: Mailbox header.
 * @msg_phys_addr_lo: Lower 32-bits of message physical address.
 * @msg_phys_addr_hi: Upper 32-bits of message physical address.
 * @msg_buffer_size: Size of message buffer.
 * @msg_data_size: Size of message data.
 *
 * FWTP messages are stored in shared memory. This mailbox message specifies the
 * address and size of the FWTP message.
 */
struct cpm_mba_fwtp_msg {
	u32 mba_header;
	u32 msg_phys_addr_lo;
	u32 msg_phys_addr_hi;
	u16 msg_buffer_size;
	u16 msg_data_size;
};

/* Number of bytes in tracepoint buffer at which to send notification. */
/* TODO: b/413142700 - Compute byte count based on ring buffer size. */
#define FWTP_CPM_NOTIFY_BYTE_COUNT 0x4000

/* Set of CPM tracepoint rings that may be requested. */
enum tracepoint_request_t {
	TRACEPOINT_REQUEST_INFO = 0,
	TRACEPOINT_REQUEST_ERROR,
	TRACEPOINT_REQUEST_DRAM,
	TRACEPOINT_REQUEST_CAP_DRAM,
	TRACEPOINT_REQUEST_VFA_DRAM,
	NUM_TRACEPOINT_REQUESTS
};

/* Set of CPM string tables. */
enum cpm_string_table_num_t {
	CPM_STRING_TABLE_CPM = 0,
	CPM_STRING_TABLE_CAP,
	CPM_STRING_TABLE_VFA
};

/*******************************************************************************
 * Globals.
 ******************************************************************************/

/* CPM FWTP device to use with ftrace. */
struct fwtp_cpm_dev *ftrace_fwtp_cpm_dev;

/*******************************************************************************
 * Internal prototypes.
 ******************************************************************************/

static void fwtp_cpm_remove(struct platform_device *pdev);

/*******************************************************************************
 * Internal functions.
 ******************************************************************************/

/**
 * fwtp_cpm_subscribe - Subscribes or unsubscribes to CPM tracepoints.
 *
 * @fwtp_dev: FWTP device.
 * @subscribe: If true, subscribe; otherwise, unsubscribe.
 */
static int fwtp_cpm_subscribe(struct fwtp_dev *fwtp_dev, bool subscribe)
{
	fwtp_error_code_t err;

	/* Subscribe or unsubscribe to the tracepoint rings. */
	err = fwtp_ipc_client_subscribe(&(fwtp_dev->fwtp_ipc_client), subscribe,
					fwtp_dev->notify_byte_count);
	if (err != kFwtpOk)
		return -EIO;

	return 0;
}

/**
 * fwtp_cpm_tracepoint_decoder_enable_cb - Callback to enable CPM tracepoints.
 *
 * @ctx: Callback context. Points to an FWTP device.
 * @enable: If true, enable CPM tracepoints; otherwise, disable them.
 */
static int fwtp_cpm_tracepoint_decoder_enable_cb(void *ctx, bool enable)
{
	struct fwtp_dev *fwtp_dev = ctx;

	return fwtp_cpm_subscribe(fwtp_dev, enable);
}

/*******************************************************************************
 * FWTP interface and printer functions.
 ******************************************************************************/

/**
 * fwtp_cpm_send_message - Sends a message through the interface.
 *
 * @fwtp_if: The firmware tracepoint interface through which to send message.
 * @msg_buffer: Buffer containing the message.
 * @msg_buffer_size: Size of the message buffer.
 * @tx_msg_data_size: Size of the message data to transmit.
 * @rx_msg_data_size: Size of the received message data.
 *
 * Sends the message contained in the buffer @msg_buffer. The size of the
 * message data to transmit is specified by @tx_msg_data_size.
 *
 * Any received response message is placed in the message buffer @msg_buffer.
 * The size of the data in the received message is returned in
 * @rx_msg_data_size. This will not be larger than the buffer size
 * @msg_buffer_size.
 *
 * Return: kFwtpOk on success, non-zero error code on error.
 */
static fwtp_error_code_t fwtp_cpm_send_message(struct fwtp_if *fwtp_if,
					       void *msg_buffer,
					       u16 msg_buffer_size,
					       u16 tx_msg_data_size,
					       u16 *rx_msg_data_size)
{
	struct fwtp_cpm_dev *fwtp_cpm_dev;
	struct cpm_mba_fwtp_msg cpm_mba_msg;
	struct cpm_iface_req cpm_req;
	u16 local_rx_msg_data_size;
	int ret;

	/* Get the CPM FWTP device record. */
	fwtp_cpm_dev = (struct fwtp_cpm_dev *)fwtp_if->send_message_ctx;

	/* Validate the message size. */
	if (tx_msg_data_size > msg_buffer_size) {
		dev_err(fwtp_cpm_dev->dev,
			"FWTP message data size %u too big for message buffer size %u.\n",
			tx_msg_data_size, msg_buffer_size);
		return kFwtpErrBadLen;
	}
	if (msg_buffer_size > fwtp_cpm_dev->dma_size) {
		dev_err(fwtp_cpm_dev->dev,
			"FWTP message of size %d too big for DMA buffer of size %zu.\n",
			(int)msg_buffer_size, fwtp_cpm_dev->dma_size);
		return kFwtpErrBadLen;
	}

	/* Encapsulate the FWTP message into a CPM FWTP message. */
	memcpy(fwtp_cpm_dev->dma_base, msg_buffer, tx_msg_data_size);
	cpm_mba_msg.msg_phys_addr_lo = fwtp_cpm_dev->dma_base_phys &
				       0xFFFFFFFFUL;
	cpm_mba_msg.msg_phys_addr_hi = fwtp_cpm_dev->dma_base_phys >> 32;
	cpm_mba_msg.msg_buffer_size = msg_buffer_size;
	cpm_mba_msg.msg_data_size = tx_msg_data_size;

	/* Ensure the FWTP message is visible to CPM before sending. */
	mb();

	/* Send the message through the interface. */
	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = (struct cpm_iface_payload *)&cpm_mba_msg;
	cpm_req.resp_msg = (struct cpm_iface_payload *)&cpm_mba_msg;
	cpm_req.dst_id = fwtp_cpm_dev->mba_channel;
	cpm_req.tout_ms = FWTP_MBA_REQ_TIMEOUT_MS;
	ret = cpm_send_message(fwtp_cpm_dev->cpm_client, &cpm_req);
	if (ret < 0) {
		dev_err(fwtp_cpm_dev->dev,
			"Failed to send message with error %d\n", ret);
		return kFwtpErrIo;
	}

	/* Return the received response message. */
	local_rx_msg_data_size = cpm_mba_msg.msg_data_size;
	if (local_rx_msg_data_size > msg_buffer_size) {
		dev_err(fwtp_cpm_dev->dev,
			"Received message of size %d too big for message buffer of size %d.\n",
			(int)local_rx_msg_data_size, (int)msg_buffer_size);
		return kFwtpErrIo;
	}
	memcpy(msg_buffer, fwtp_cpm_dev->dma_base, local_rx_msg_data_size);
	*rx_msg_data_size = local_rx_msg_data_size;

	return 0;
}

/**
 * fwtp_cpm_printer_post_process - Performs post-processing on CPM tracepoints.
 *
 * Runs CPM tracepoint post-processing using the decoded tracepoint with the
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
static void fwtp_cpm_printer_post_process(
	struct fwtp_printer_ctx *printer_ctx, unsigned int type, u64 timestamp,
	u32 str_id, const char *str, struct fwtp_data_item_list *data_items)
{
	uint8_t *p_starting_next_data_item;
	u32 data;
	int data_item_size;
	u64 boottime_timestamp;

	/*
	 * Pass the tracepoint string and data item to the CPM tracepoint
	 * decoder. Reset the data items list.
	 */
	p_starting_next_data_item = data_items->p_next_data_item;
	data = 0;
	data_item_size =
		fwtp_get_next_data_item(data_items, &data, sizeof(data));
	if (data_item_size > 0) {
		boottime_timestamp = fwtp_dev_get_boottime_timestamp(
			timestamp, printer_ctx->timestamp_hz);
		cpm_tracepoint_decode(str_id, data, boottime_timestamp);
	}
	data_items->p_next_data_item = p_starting_next_data_item;

	/* Continue post-processing the tracepoint. */
	fwtp_dev_printer_post_process(printer_ctx, type, timestamp, str_id, str,
				      data_items);
}

/**
 * fwtp_cpm_handle_ring_notify_work - Handles FWTP ring notifications as a work
 * item.
 *
 * @work: Ring notification work item.
 */
static void fwtp_cpm_handle_ring_notify_work(struct work_struct *work)
{
	struct fwtp_cpm_dev *fwtp_cpm_dev =
		container_of(work, struct fwtp_cpm_dev, ring_notify_work);
	int i;

	/* Print all the rings that have new tracepoints. */
	for (i = 0; i < FWTP_CPM_DEV_NUM; i++) {
		struct fwtp_dev *fwtp_dev = &(fwtp_cpm_dev->fwtp_dev_list[i]);
		struct fwtp_printer_ctx *printer_ctx = &(fwtp_dev->printer_ctx);
		uint8_t decode_buffer[FWTP_DECODE_BUFFER_SIZE];

		/*
		 * Initialize the printer context for use with the FWTP IPC
		 * client.
		 */
		fwtp_ipc_client_printer_ctx_init(&(fwtp_dev->fwtp_ipc_client),
						 printer_ctx);

		/*
		 * Print the ring using an intermediate buffer. The buffer is
		 * needed because when the AP reads tracepoint data items, it
		 * may issue unaligned reads which are not allowed for CPM
		 * memory.
		 */
		if (fwtp_dev->memio_ring) {
			fwtp_print_ring_entries_with_decode_buffer(
				printer_ctx, fwtp_dev->memio_ring, 0,
				decode_buffer, sizeof(decode_buffer));
		}
	}
}

/**
 * fwtp_cpm_handle_ring_notify - Handles FWTP ring notifications.
 *
 * Handles the FWTP ring notification message specified by msg_base for the CPM
 * FWTP device specified by fwtp_cpm_dev.
 *
 * @fwtp_cpm_dev: CPM FWTP device receiving the message.
 * @msg_base: FWTP ring notification message.
 */
static void fwtp_cpm_handle_ring_notify(struct fwtp_cpm_dev *fwtp_cpm_dev,
					struct fwtp_msg_base *msg_base)
{
	int i;

	/* Update the ring tail offset. */
	struct fwtp_msg_ring_notify *ring_notify =
		container_of(msg_base, struct fwtp_msg_ring_notify, base);
	for (i = 0; i < FWTP_CPM_DEV_NUM; i++) {
		struct fwtp_dev *fwtp_dev = &fwtp_cpm_dev->fwtp_dev_list[i];
		struct fwtp_ipc_client *fwtp_ipc_client =
			&(fwtp_dev->fwtp_ipc_client);

		if (ring_notify->ring_num ==
		    fwtp_ipc_client->client_ring_list[0].ring_num) {
			if (fwtp_dev->memio_ring)
				fwtp_dev->memio_ring->tail_offset =
					ring_notify->tail_offset;
			break;
		}
	}

	/* Handle ring notification as a work item. */
	schedule_work(&fwtp_cpm_dev->ring_notify_work);
}

/**
 * fwtp_cpm_host_cb - Callback invoked for CPM-initiated transactions.
 *
 * @context: Context value provided by the CPM host. 0 for oneway request,
 *           otherwise client need to respond. (see below)
 * @msg: Pointer to the message payload received from the CPM host.
 * @priv_data: Client's private data (registered via
 *             `cpm_iface_request_client`).
 */
static void fwtp_cpm_host_cb(u32 context, void *msg, void *priv_data)
{
	struct fwtp_cpm_dev *fwtp_cpm_dev = priv_data;
	struct cpm_iface_payload *cpm_payload = msg;
	struct fwtp_msg_base *msg_base =
		(struct fwtp_msg_base *)cpm_payload->payload;

	/* Dispatch handling of the message. */
	switch (msg_base->type) {
	case kFwtpMsgTypeRingNotify:
		fwtp_cpm_handle_ring_notify(fwtp_cpm_dev, msg_base);
		break;
	default:
		break;
	}
}

/*******************************************************************************
 * Debugfs functions.
 ******************************************************************************/

/**
 * fwtp_cpm_debugfs_write_subscribe - Handle write.
 *
 * @data: Pointer to FWTP device.
 * @val: Write value.
 *
 * Handles a debugfs write operation to enable or disable tracepoint ring
 * subscriptions.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_cpm_debugfs_write_subscribe(void *data, u64 val)
{
	struct fwtp_dev *fwtp_dev = data;
	struct fwtp_ipc_client *fwtp_ipc_client;
	const bool enable = (val != 0);

	/* Initialize or exit CPM tracepoint decoder callbacks. */
	if (fwtp_dev->printer_ctx.post_process ==
	    fwtp_cpm_printer_post_process) {
		if (enable) {
			/* Initialize tracepoint decoder clients. */
			fwtp_ipc_client = &(fwtp_dev->fwtp_ipc_client);
			client_init_callbacks(
				fwtp_ipc_client->string_table,
				fwtp_ipc_client->string_table_size,
				fwtp_ipc_client->string_table_offset);
		} else {
			client_exit_callbacks();
		}
	}

	return fwtp_cpm_subscribe(fwtp_dev, enable);
}

/* Define the CPM FWTP driver subscription debugfs interface. */
DEFINE_DEBUGFS_ATTRIBUTE(fwtp_cpm_debugfs_fops_subscribe, NULL,
			 fwtp_cpm_debugfs_write_subscribe, "%llu\n");

/**
 * fwtp_cpm_debugfs_read_enable_log - Handle read.
 *
 * @data: Pointer to FWTP device.
 * @val: Read value on return.
 *
 * Handles a debugfs read operation for publishing tracepoints to the kernel
 * log.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_cpm_debugfs_read_enable_log(void *data, u64 *val)
{
	struct fwtp_dev *fwtp_dev = data;

	*val = fwtp_dev->log_enabled ? 1 : 0;
	return 0;
}

/**
 * fwtp_cpm_debugfs_write_enable_log - Handle write.
 *
 * @data: Pointer to FWTP device.
 * @val: Write value.
 *
 * Handles a debugfs write operation for publishing tracepoints to the kernel
 * log.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_cpm_debugfs_write_enable_log(void *data, u64 val)
{
	struct fwtp_dev *fwtp_dev = data;

	fwtp_dev->log_enabled = (val != 0);
	return 0;
}

/*
 * Define the CPM FWTP driver debug interface to enable publishing tracepoints
 * to the kernel log.
 */
DEFINE_DEBUGFS_ATTRIBUTE(fwtp_cpm_debugfs_fops_enable_log,
			 fwtp_cpm_debugfs_read_enable_log,
			 fwtp_cpm_debugfs_write_enable_log, "%llu\n");

/**
 * fwtp_cpm_debugfs_read_enable_ftrace - Handle read.
 *
 * @data: Pointer to FWTP device.
 * @val: Read value on return.
 *
 * Handles a debugfs read operation for publishing tracepoints to ftrace.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_cpm_debugfs_read_enable_ftrace(void *data, u64 *val)
{
	struct fwtp_dev *fwtp_dev = data;

	*val = fwtp_dev->ftrace_enabled ? 1 : 0;
	return 0;
}

/**
 * fwtp_cpm_debugfs_write_enable_ftrace - Handle write.
 *
 * @data: Pointer to FWTP device.
 * @val: Write value.
 *
 * Handles a debugfs write operation for publishing tracepoints to ftrace.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int fwtp_cpm_debugfs_write_enable_ftrace(void *data, u64 val)
{
	struct fwtp_dev *fwtp_dev = data;

	fwtp_dev->ftrace_enabled = (val != 0);

	return 0;
}

/*
 * Define the CPM FWTP driver debug interface to enable publishing tracepoints
 * to ftrace.
 */
DEFINE_DEBUGFS_ATTRIBUTE(fwtp_cpm_debugfs_fops_enable_ftrace,
			 fwtp_cpm_debugfs_read_enable_ftrace,
			 fwtp_cpm_debugfs_write_enable_ftrace, "%llu\n");

/*******************************************************************************
 * Platform driver functions.
 ******************************************************************************/

/**
 * fwtp_cpm_init_dev - Initialize a CPM FWTP sub-device.
 *
 * @fwtp_cpm_dev: CPM FWTP device.
 * @tracepoint_name: Name of sub-device tracepoints.
 * @dev_num: Sub-device number.
 * @ring_num: Sub-device ring number.
 * @string_table_num: Sub-device string table number.
 */
static int fwtp_cpm_init_dev(struct fwtp_cpm_dev *fwtp_cpm_dev,
			     const char *tracepoint_name, int dev_num,
			     int ring_num, int string_table_num)
{
	struct fwtp_dev *fwtp_dev;
	struct dentry *root_subdev_debugfs;
	struct dentry *dentry;
	int ret;

	/* Create the root sub-device debugfs directory. */
	root_subdev_debugfs =
		debugfs_create_dir(tracepoint_name, fwtp_cpm_dev->debugfs_root);
	if (IS_ERR(root_subdev_debugfs)) {
		dev_err(fwtp_cpm_dev->dev,
			"Failed to create a debugfs directory for \"%s\".\n",
			tracepoint_name);
		return PTR_ERR(root_subdev_debugfs);
	}

	/* Configure and initialize the FWTP sub-device. */
	fwtp_dev = &(fwtp_cpm_dev->fwtp_dev_list[dev_num]);
	fwtp_dev->dev = fwtp_cpm_dev->dev;
	fwtp_dev->root_debugfs = root_subdev_debugfs;
	fwtp_dev->fwtp_ipc_client.fwtp_if.send_message = fwtp_cpm_send_message;
	fwtp_dev->fwtp_ipc_client.fwtp_if.send_message_ctx = fwtp_cpm_dev;
	fwtp_dev->fwtp_ipc_client.client_ring_list[0].ring_num = ring_num;
	fwtp_dev->fwtp_ipc_client.client_ring_count = 1;
	fwtp_dev->fwtp_ipc_client.string_table_num = string_table_num;
	fwtp_dev->printer_ctx.name = tracepoint_name;
	fwtp_dev->ftrace_enabled = true;
	fwtp_dev->notify_byte_count = FWTP_CPM_NOTIFY_BYTE_COUNT;
	ret = fwtp_dev_init(fwtp_dev);
	if (ret) {
		dev_err(fwtp_dev->dev,
			"Failed to initialize the FWTP sub-device with error %d.\n",
			ret);
		return ret;
	}

	/* Get the tracepoint ring with mem I/O access. */
	fwtp_dev->memio_ring = devm_kzalloc(
		fwtp_cpm_dev->dev, sizeof(struct tracepoint_ring), GFP_KERNEL);
	if (!fwtp_dev->memio_ring)
		return -ENOMEM;

	ret = fwtp_dev_get_memio_ring(
		fwtp_dev,
		fwtp_dev->fwtp_ipc_client.client_ring_list[0].ring_num,
		fwtp_dev->memio_ring);
	if (ret) {
		dev_err(fwtp_dev->dev,
			"Failed to get tracepoint ring with error %d.\n", ret);
		return ret;
	}

	/* Create the various device debugfs files. */
	dentry = debugfs_create_file("request_subscribe", 0220,
				     root_subdev_debugfs, fwtp_dev,
				     &fwtp_cpm_debugfs_fops_subscribe);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create a subscription debugfs file.\n");
		return PTR_ERR(dentry);
	}
	dentry = debugfs_create_file("enable_log", 0220, root_subdev_debugfs,
				     fwtp_dev,
				     &fwtp_cpm_debugfs_fops_enable_log);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create an enable kernel log debugfs file.\n");
		return PTR_ERR(dentry);
	}

	dentry = debugfs_create_file("enable_ftrace", 0220, root_subdev_debugfs,
				     fwtp_dev,
				     &fwtp_cpm_debugfs_fops_enable_ftrace);
	if (IS_ERR(dentry)) {
		dev_err(fwtp_dev->dev,
			"Failed to create an enable ftrace debugfs file.\n");
		return PTR_ERR(dentry);
	}

	return 0;
}

/**
 * fwtp_cpm_init_dev_list - Initialize configured list of CPM FWTP sub-devices.
 *
 * @fwtp_cpm_dev: CPM FWTP device.
 */
static int fwtp_cpm_init_dev_list(struct fwtp_cpm_dev *fwtp_cpm_dev)
{
	struct device *dev = fwtp_cpm_dev->dev;
	int sub_dev_count;
	int i;
	int ret;

	/* Get the number of FWTP sub-devices. */
	ret = of_property_count_strings(dev->of_node, "tracepoint-list");
	if (ret < 0) {
		dev_err(dev,
			"Failed to get tracepoint sub-device count with error %d.\n",
			ret);
		return ret;
	}
	sub_dev_count = ret;

	/* Initialize all sub-devices. */
	for (i = 0; i < sub_dev_count; i++) {
		const char *sub_dev_name;

		/* Get the sub-device name. */
		ret = of_property_read_string_index(
			dev->of_node, "tracepoint-list", i, &sub_dev_name);
		if (ret) {
			dev_err(dev,
				"Failed to get sub-device index %d name with error %d.\n",
				i, ret);
			return ret;
		}

		/* Initialize the sub-device. */
		if (strcmp(sub_dev_name, "cpm") == 0) {
			ret = fwtp_cpm_init_dev(fwtp_cpm_dev, "cpm",
						FWTP_CPM_DEV_ID_CPM,
						TRACEPOINT_REQUEST_DRAM,
						CPM_STRING_TABLE_CPM);
		} else if (strcmp(sub_dev_name, "cap") == 0) {
			ret = fwtp_cpm_init_dev(fwtp_cpm_dev, "cap",
						FWTP_CPM_DEV_ID_CAP,
						TRACEPOINT_REQUEST_CAP_DRAM,
						CPM_STRING_TABLE_CAP);
		} else if (strcmp(sub_dev_name, "vfa") == 0) {
			ret = fwtp_cpm_init_dev(fwtp_cpm_dev, "vfa",
						FWTP_CPM_DEV_ID_VFA,
						TRACEPOINT_REQUEST_VFA_DRAM,
						CPM_STRING_TABLE_VFA);
		} else {
			dev_warn(dev, "Sub-device name %s unrecognized.\n",
				 sub_dev_name);
			ret = 0;
		}
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * fwtp_cpm_probe - Probe CPM firmware tracepoint devices.
 *
 * @pdev: The platform device to probe.
 */
static int fwtp_cpm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwtp_cpm_dev *fwtp_cpm_dev;
	struct cpm_iface_client *cpm_client;
	struct fwtp_dev *fwtp_dev;
	struct fwtp_ipc_client *fwtp_ipc_client;
	struct device_node *dma_reserved_mem_node = NULL;
	struct reserved_mem *dma_reserved_mem;
	int ret;

	/* Log the start of probing. */
	dev_dbg(dev, "Probing CPM FWTP device.\n");

	/* Create a CPM FWTP device record. */
	fwtp_cpm_dev = devm_kzalloc(dev, sizeof(*fwtp_cpm_dev), GFP_KERNEL);
	if (!fwtp_cpm_dev) {
		ret = -ENOMEM;
		goto out;
	}
	platform_set_drvdata(pdev, fwtp_cpm_dev);
	ftrace_fwtp_cpm_dev = fwtp_cpm_dev;
	fwtp_cpm_dev->dev = dev;

	/* Create a work item to handle tracepoint ring notifications. */
	ret = devm_work_autocancel(dev, &fwtp_cpm_dev->ring_notify_work,
				   fwtp_cpm_handle_ring_notify_work);
	if (ret) {
		dev_err(dev, "Failed to create a work item with error %d.\n",
			ret);
		goto out;
	}

	/* Get a CPM mailbox interface client. */
	cpm_client = cpm_iface_request_client(dev, CPM_COMMON_FWTP_SERVICE,
					      fwtp_cpm_host_cb, fwtp_cpm_dev);
	if (IS_ERR(cpm_client)) {
		ret = PTR_ERR(cpm_client);
		dev_err(dev,
			"Failed to get a CPM mailbox interface handle with error %d.\n",
			ret);
		goto out;
	}
	fwtp_cpm_dev->cpm_client = cpm_client;

	/* Get the CPM mailbox channel. */
	ret = of_property_read_u32(dev->of_node, "mba-dest-channel",
				   &fwtp_cpm_dev->mba_channel);
	if (ret < 0) {
		dev_err(dev, "Failed to read mba-dest-channel.\n");
		goto out;
	}

	/* Get DMA memory. */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(36));
	if (ret) {
		dev_err(dev,
			"Failed to set DMA mask to 36 bits with error %d.\n",
			ret);
		goto out;
	}
	dma_reserved_mem_node =
		of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!dma_reserved_mem_node) {
		dev_err(dev, "Failed to get DMA memory region node.\n");
		ret = -ENODEV;
		goto out;
	}
	dma_reserved_mem = of_reserved_mem_lookup(dma_reserved_mem_node);
	if (!dma_reserved_mem) {
		dev_err(dev, "Failed to acquire DMA reserved memory.\n");
		ret = -ENODEV;
		goto out;
	}
	fwtp_cpm_dev->dma_size = dma_reserved_mem->size;
	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		dev_err(dev,
			"Failed to assign device memory regions with error %d.\n",
			ret);
		goto out;
	}
	fwtp_cpm_dev->reserved_mem_initialized = true;
	fwtp_cpm_dev->dma_base =
		dma_alloc_coherent(dev, fwtp_cpm_dev->dma_size,
				   &fwtp_cpm_dev->dma_base_phys, GFP_KERNEL);
	if (!fwtp_cpm_dev->dma_base) {
		dev_err(dev, "Failed to allocate DMA memory.\n");
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Create the debugfs directory used to configure logging of CPM
	 * tracepoints.
	 */
	fwtp_cpm_dev->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(fwtp_cpm_dev->debugfs_root)) {
		dev_err(dev, "Failed to create a debugfs directory.\n");
		ret = PTR_ERR(fwtp_cpm_dev->debugfs_root);
		goto out;
	}

	/* Initialize CPM FWTP sub-devices. */
	ret = fwtp_cpm_init_dev_list(fwtp_cpm_dev);
	if (ret)
		goto out;

	/* Set up CPM tracepoint post-processing. */
	fwtp_dev = &(fwtp_cpm_dev->fwtp_dev_list[FWTP_CPM_DEV_ID_CPM]);
	fwtp_dev->printer_ctx.post_process = fwtp_cpm_printer_post_process;

	/* Initialize the CPM tracepoint decoder. */
	initialize_cpm_tracepoint_decoder();
	fwtp_ipc_client = &(fwtp_dev->fwtp_ipc_client);
	cpm_tracepoint_decoder_set_string_table(
		fwtp_ipc_client->string_table,
		fwtp_ipc_client->string_table_size,
		fwtp_ipc_client->string_table_offset);
	cpm_tracepoint_decoder_set_enable_cb(
		fwtp_cpm_tracepoint_decoder_enable_cb, fwtp_dev);

out:
	/* Clean up. */
	if (dma_reserved_mem_node)
		of_node_put(dma_reserved_mem_node);

	/* Clean up on error. */
	if (ret)
		fwtp_cpm_remove(pdev);

	return ret;
}

/**
 * fwtp_cpm_remove - Removes a CPM FWTP device.
 *
 * @pdev: The platform device to remove.
 */
static void fwtp_cpm_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwtp_cpm_dev *fwtp_cpm_dev;
	int i;

	/*
	 * Get the CPM FWTP device record and remove it from the platform
	 * device.
	 */
	fwtp_cpm_dev = platform_get_drvdata(pdev);
	if (!fwtp_cpm_dev)
		return;
	platform_set_drvdata(pdev, NULL);
	ftrace_fwtp_cpm_dev = NULL;

	/* Deinitialize the CPM tracepoint decoder. */
	cpm_tracepoint_decoder_set_enable_cb(NULL, NULL);
	cpm_tracepoint_decoder_set_string_table(NULL, 0, 0);

	/*
	 * Remove the debugfs directory used to configure publishing of CPM
	 * tracepoints.
	 */
	debugfs_remove_recursive(fwtp_cpm_dev->debugfs_root);

	/* Free DMA memory. */
	if (fwtp_cpm_dev->dma_base)
		dma_free_coherent(dev, fwtp_cpm_dev->dma_size,
				  fwtp_cpm_dev->dma_base,
				  fwtp_cpm_dev->dma_base_phys);

	/* Release the device reserved memory. */
	if (fwtp_cpm_dev->reserved_mem_initialized)
		of_reserved_mem_device_release(dev);

	/* Free the CPM mailbox interface client. */
	if (fwtp_cpm_dev->cpm_client)
		cpm_iface_free_client(fwtp_cpm_dev->cpm_client);

	/* Deinitialize the FWTP sub-devices. */
	for (i = 0; i < FWTP_CPM_DEV_NUM; i++)
		fwtp_dev_deinit(&(fwtp_cpm_dev->fwtp_dev_list[i]));

	/* Log removal. */
	dev_dbg(dev, "Removed CPM FWTP device.\n");
}

/*******************************************************************************
 * Platform device configuration.
 ******************************************************************************/

/* Device tree match table. */
static const struct of_device_id fwtp_cpm_of_match_table[] = {
	{ .compatible = "google,fwtp-cpm" },
	{},
};
MODULE_DEVICE_TABLE(of, fwtp_cpm_of_match_table);

/* Platform driver configuration. */
static struct platform_driver fwtp_cpm_driver = {
	.probe = fwtp_cpm_probe,
	.remove = fwtp_cpm_remove,
	.driver = {
		.name = "fwtp-cpm",
		.owner = THIS_MODULE,
		.of_match_table = fwtp_cpm_of_match_table,
	},
};
module_platform_driver(fwtp_cpm_driver);

/*******************************************************************************
 * Module info.
 ******************************************************************************/

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("CPM firmware tracepoint");
MODULE_LICENSE("GPL");
