// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC.
 *
 * Google GDMC firmware tracepoint driver.
 */

#include <linux/debugfs.h>
#include <linux/devm-helpers.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_mba_nq_xport.h>

#include "fwtp.h"
#include "fwtp_decode.h"
#include "fwtp_protocol.h"

/*******************************************************************************
 * Data structures and defs.
 ******************************************************************************/

/**
 * struct fwtp_google_gdmc_dev - Structure representing a Google GDMC firmware
 * tracepoint device.
 *
 * @base: Base FWTP kernel device record.
 * @gdmc_iface: Mailbox interface to GDMC.
 * @ring_notify_work: Work item for ring notifications.
 * @registered_gdmc_host_cb: If true, a callback has been registered to handle
 *                           GDMC-initiated requests.
 * @reserved_mem_initialized: If true, device reserved memory has been
 *                            initialized.
 * @dma_size: Size of DMA memory.
 * @dma_base: Base address of DMA memory.
 * @dma_base_phys: Physical base address of DMA memory.
 * @debugfs_root: Root debugfs directory of device.
 * @decoder: Tracepoint decoder instance for this device.
 */
struct fwtp_google_gdmc_dev {
	struct fwtp_dev base;
	struct gdmc_iface *gdmc_iface;
	struct work_struct ring_notify_work;
	bool registered_gdmc_host_cb;
	bool reserved_mem_initialized;
	size_t dma_size;
	void *dma_base;
	dma_addr_t dma_base_phys;
	struct dentry *debugfs_root;
	struct fwtp_decoder decoder;
};

/**
 * struct gdmc_mba_fwtp_msg - GDMC mailbox FWTP message.
 *
 * @mba_header: Mailbox header containing mailbox service ID.
 * @msg_phys_addr_lo: Lower 32-bits of message physical address.
 * @msg_phys_addr_hi: Upper 32-bits of message physical address.
 * @msg_buffer_size: Size of message buffer.
 * @msg_data_size: Size of message data.
 *
 * FWTP messages are stored in shared memory. This mailbox message specifies the
 * address and size of the FWTP message.
 */
struct gdmc_mba_fwtp_msg {
	u32 mba_header;
	u32 msg_phys_addr_lo;
	u32 msg_phys_addr_hi;
	u16 msg_buffer_size;
	u16 msg_data_size;
};

/* Number of bytes in tracepoint buffer at which to send notification. */
/* TODO: b/413142700 - Compute byte count based on ring buffer size. */
#define FWTP_GOOGLE_GDMC_NOTIFY_BYTE_COUNT 1024

/*******************************************************************************
 * Internal prototypes.
 ******************************************************************************/

static fwtp_error_code_t fwtp_google_gdmc_send_message(struct fwtp_if *fwtp_if,
						       void *msg_buffer,
						       u16 msg_buffer_size,
						       u16 tx_msg_data_size,
						       u16 *rx_msg_data_size);
static int fwtp_google_gdmc_probe(struct platform_device *pdev);
static void fwtp_google_gdmc_remove(struct platform_device *pdev);

/*******************************************************************************
 * GDMC tracepoint post-processing.
 ******************************************************************************/

static enum tracepoint_handle test_string_handler(const char *tp_string,
						  u32 payload, u64 timestamp)
{
	pr_info("%s received: Timestamp: %llu, Payload: %u\n", tp_string,
		timestamp, payload);
	return CLIENT_TP_HANDLING_COMPLETE;
}

static struct client_tracepoint test_string_tp = {
	.enabled = true,
	.tp_string = "Test string",
	.handler = test_string_handler,
};

static struct client_tracepoint *gdmc_tracepoints[] = {
	&test_string_tp,
};

/*******************************************************************************
 * Firmware tracepoint interface functions.
 ******************************************************************************/

/**
 * fwtp_google_gdmc_send_message - Sends a message through the interface.
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
static fwtp_error_code_t fwtp_google_gdmc_send_message(struct fwtp_if *fwtp_if,
						       void *msg_buffer,
						       u16 msg_buffer_size,
						       u16 tx_msg_data_size,
						       u16 *rx_msg_data_size)
{
	struct fwtp_dev *fwtp_dev;
	struct fwtp_google_gdmc_dev *fwtp_google_gdmc_dev;
	struct gdmc_mba_fwtp_msg gdmc_mba_fwtp_msg;
	uint16_t local_rx_msg_data_size;
	int ret;

	/* Get the Google GDMC firmware tracepoint device record. */
	fwtp_dev =
		container_of(fwtp_if, struct fwtp_dev, fwtp_ipc_client.fwtp_if);
	fwtp_google_gdmc_dev =
		container_of(fwtp_dev, struct fwtp_google_gdmc_dev, base);

	/* Validate the message size. */
	if (tx_msg_data_size > msg_buffer_size) {
		dev_err(fwtp_google_gdmc_dev->base.dev,
			"FWTP message data size %d too big for message buffer size %d.\n",
			(int)tx_msg_data_size, (int)msg_buffer_size);
		return kFwtpErrBadLen;
	}
	if (msg_buffer_size > fwtp_google_gdmc_dev->dma_size) {
		dev_err(fwtp_google_gdmc_dev->base.dev,
			"FWTP message of size %d too big for DMA buffer of size %zu.\n",
			(int)msg_buffer_size, fwtp_google_gdmc_dev->dma_size);
		return kFwtpErrBadLen;
	}

	/* Encapsulate the FWTP message into a GDMC FWTP message. */
	goog_mba_nq_xport_set_service_id(&(gdmc_mba_fwtp_msg.mba_header),
					 GDMC_MBA_SERVICE_ID_FWTP);
	memcpy(fwtp_google_gdmc_dev->dma_base, msg_buffer, tx_msg_data_size);
	gdmc_mba_fwtp_msg.msg_phys_addr_lo =
		fwtp_google_gdmc_dev->dma_base_phys & 0xFFFFFFFFUL;
	gdmc_mba_fwtp_msg.msg_phys_addr_hi =
		fwtp_google_gdmc_dev->dma_base_phys >> 32;
	gdmc_mba_fwtp_msg.msg_buffer_size = msg_buffer_size;
	gdmc_mba_fwtp_msg.msg_data_size = tx_msg_data_size;

	/* Ensure the FWTP message is visible to GDMC before sending. */
	mb();

	/* Send the message through the interface. */
	ret = gdmc_send_message(fwtp_google_gdmc_dev->gdmc_iface,
				&gdmc_mba_fwtp_msg);
	if (ret < 0) {
		dev_err(fwtp_google_gdmc_dev->base.dev,
			"Failed to send message with error %d\n", ret);
		return kFwtpErrIo;
	}

	/* Return the received response message. */
	local_rx_msg_data_size = gdmc_mba_fwtp_msg.msg_data_size;
	if (local_rx_msg_data_size > msg_buffer_size) {
		dev_err(fwtp_google_gdmc_dev->base.dev,
			"Received message of size %d too big for message buffer of size %d.\n",
			(int)local_rx_msg_data_size, (int)msg_buffer_size);
		return kFwtpErrIo;
	}
	memcpy(msg_buffer, fwtp_google_gdmc_dev->dma_base,
	       local_rx_msg_data_size);
	*rx_msg_data_size = local_rx_msg_data_size;

	return kFwtpOk;
}

/**
 * fwtp_google_gdmc_host_cb - Callback invoked for GDMC-initiated transactions.
 *
 * @msg: Pointer to the message payload received from the GDMC host.
 * @priv_data: Client's private data (registered via `gdmc_register_host_cb`).
 */
static void fwtp_google_gdmc_host_cb(void *msg, void *priv_data)
{
	struct fwtp_google_gdmc_dev *fwtp_google_gdmc_dev = priv_data;
	struct fwtp_msg_base *msg_base =
		(struct fwtp_msg_base *)&((u32 *)msg)[1];

	switch (msg_base->type) {
	case kFwtpMsgTypeRingNotify:
		schedule_work(&fwtp_google_gdmc_dev->ring_notify_work);
		break;
	default:
		break;
	}
}

/**
 * fwtp_google_gdmc_handle_ring_notify - Handles GDMC FWTP ring notifications.
 *
 * @work: Ring notification work item.
 */
static void fwtp_google_gdmc_handle_ring_notify(struct work_struct *work)
{
	struct fwtp_google_gdmc_dev *fwtp_google_gdmc_dev = container_of(
		work, struct fwtp_google_gdmc_dev, ring_notify_work);
	struct fwtp_dev *fwtp_dev = &(fwtp_google_gdmc_dev->base);

	fwtp_ipc_client_print_tracepoints(&(fwtp_dev->fwtp_ipc_client),
					  &(fwtp_dev->printer_ctx), false);
}

/*******************************************************************************
 * Debugfs functions.
 ******************************************************************************/

/**
 * google_gdmc_tracepoint_debugfs_write_tracepoint_poll - Handle write.
 *
 * @data: Pointer to GDMC firmware tracepoint driver.
 * @val: Write value.
 *
 * Handles a debugfs write operation by logging GDMC tracepoints.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int google_gdmc_tracepoint_debugfs_write_tracepoint_poll(void *data,
								u64 val)
{
	struct fwtp_google_gdmc_dev *fwtp_google_gdmc_dev = data;
	struct fwtp_dev *fwtp_dev = &(fwtp_google_gdmc_dev->base);

	fwtp_ipc_client_print_tracepoints(&(fwtp_dev->fwtp_ipc_client),
					  &(fwtp_dev->printer_ctx), false);

	return 0;
}

/* Define the GDMC firmware tracepoint driver polling debugfs interface. */
DEFINE_DEBUGFS_ATTRIBUTE(google_gdmc_tracepoint_debugfs_fops_tracepoint_poll,
			 NULL,
			 google_gdmc_tracepoint_debugfs_write_tracepoint_poll,
			 "%llu\n");

/**
 * google_gdmc_tracepoint_debugfs_write_tracepoint_subscribe - Handle write.
 *
 * @data: Pointer to GDMC firmware tracepoint driver.
 * @val: Write value.
 *
 * Handles a debugfs write operation to enable or disable GDMC tracepoint
 * subscriptions.
 *
 * Return: 0 on success, non-zero error code on error.
 */
static int google_gdmc_tracepoint_debugfs_write_tracepoint_subscribe(void *data,
								     u64 val)
{
	struct fwtp_google_gdmc_dev *fwtp_google_gdmc_dev = data;
	struct fwtp_dev *fwtp_dev = &(fwtp_google_gdmc_dev->base);
	bool enable = (val != 0);
	fwtp_error_code_t err;

	/* Enable or disable tracepoint decoding. */
	fwtp_decoder_enable(&(fwtp_google_gdmc_dev->decoder), fwtp_dev, enable);

	/* Subscribe or unsubscribe to GDMC tracepoints. */
	err = fwtp_ipc_client_subscribe(&(fwtp_dev->fwtp_ipc_client), enable,
					fwtp_dev->notify_byte_count);
	if (err != kFwtpOk)
		return -EIO;

	return 0;
}

/* Define the GDMC firmware tracepoint driver subscription debugfs interface. */
DEFINE_DEBUGFS_ATTRIBUTE(
	google_gdmc_tracepoint_debugfs_fops_tracepoint_subscribe, NULL,
	google_gdmc_tracepoint_debugfs_write_tracepoint_subscribe, "%llu\n");

/*******************************************************************************
 * Platform driver functions.
 ******************************************************************************/

/**
 * fwtp_google_gdmc_probe - Probes Google GDMC firmware tracepoint devices.
 *
 * @pdev: The platform device to probe.
 */
static int fwtp_google_gdmc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwtp_google_gdmc_dev *fwtp_google_gdmc_dev;
	struct gdmc_iface *gdmc_iface;
	struct fwtp_dev *fwtp_dev;
	struct device_node *dma_reserved_mem_node = NULL;
	struct reserved_mem *dma_reserved_mem;
	struct dentry *dentry;
	int ret = 0;

	/* Log the start of probing. */
	dev_dbg(dev, "Probing Google GDMC firmware tracepoint device.\n");

	/* Create a Google GDMC firmware tracepoint device record. */
	fwtp_google_gdmc_dev =
		devm_kzalloc(dev, sizeof(*fwtp_google_gdmc_dev), GFP_KERNEL);
	if (!fwtp_google_gdmc_dev) {
		dev_err(dev,
			"Failed to create a Google GDMC firmware tracepoint device record\n");
		ret = -ENOMEM;
		goto out;
	}
	platform_set_drvdata(pdev, fwtp_google_gdmc_dev);

	/* Create a work item to handle tracepoint ring notifications. */
	ret = devm_work_autocancel(dev, &fwtp_google_gdmc_dev->ring_notify_work,
				   fwtp_google_gdmc_handle_ring_notify);
	if (ret) {
		dev_err(dev, "Failed to create a work item with error %d.\n",
			ret);
		goto out;
	}

	/* Get a GDMC mailbox interface handle. */
	gdmc_iface = gdmc_iface_get(dev);
	if (IS_ERR(gdmc_iface)) {
		ret = PTR_ERR(gdmc_iface);
		dev_err(dev,
			"Failed to get a GDMC mailbox interface handle with error %d.\n",
			ret);
		goto out;
	}
	fwtp_google_gdmc_dev->gdmc_iface = gdmc_iface;

	/* Register a callback function to handle GDMC-initiated requests. */
	ret = gdmc_register_host_cb(gdmc_iface, APC_CRITICAL_GDMC_FWTP_SERVICE,
				    fwtp_google_gdmc_host_cb,
				    fwtp_google_gdmc_dev);
	if (ret) {
		dev_err(dev,
			"Failed to register GDMC host callback with error %d.\n",
			ret);
		goto out;
	}
	fwtp_google_gdmc_dev->registered_gdmc_host_cb = true;

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
	fwtp_google_gdmc_dev->dma_size = dma_reserved_mem->size;
	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		dev_err(dev,
			"Failed to assign device memory regions with error %d.\n",
			ret);
		goto out;
	}
	fwtp_google_gdmc_dev->reserved_mem_initialized = true;
	fwtp_google_gdmc_dev->dma_base = dma_alloc_coherent(
		dev, fwtp_google_gdmc_dev->dma_size,
		&fwtp_google_gdmc_dev->dma_base_phys, GFP_KERNEL);
	if (!fwtp_google_gdmc_dev->dma_base) {
		dev_err(dev, "Failed to allocate DMA memory.\n");
		ret = -ENOMEM;
		goto out;
	}
	fwtp_google_gdmc_dev->debugfs_root =
		debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(fwtp_google_gdmc_dev->debugfs_root)) {
		dev_err(dev,
			"Failed to create a debugfs directory for GDMC.\n");
		ret = PTR_ERR(fwtp_google_gdmc_dev->debugfs_root);
		goto out;
	}

	/* Initialize the base FWTP device. */
	fwtp_dev = &(fwtp_google_gdmc_dev->base);
	fwtp_dev->dev = dev;
	fwtp_dev->fwtp_ipc_client.fwtp_if.send_message =
		fwtp_google_gdmc_send_message;
	fwtp_dev->fwtp_ipc_client.client_ring_list[0].ring_num = 0;
	fwtp_dev->fwtp_ipc_client.client_ring_list[1].ring_num = 1;
	fwtp_dev->fwtp_ipc_client.client_ring_count = 2;
	fwtp_dev->printer_ctx.name = "gdmc";
	fwtp_dev->log_enabled = true;
	fwtp_dev->ftrace_enabled = true;
	fwtp_dev->root_debugfs = fwtp_google_gdmc_dev->debugfs_root;
	fwtp_dev->notify_byte_count = FWTP_GOOGLE_GDMC_NOTIFY_BYTE_COUNT;
	ret = fwtp_dev_init(fwtp_dev);
	if (ret) {
		dev_err(dev,
			"Failed to initialize the base FWTP device with error %d.\n",
			ret);
		goto out;
	}

	dentry = debugfs_create_file(
		"request_poll", 0220, fwtp_google_gdmc_dev->debugfs_root,
		fwtp_google_gdmc_dev,
		&google_gdmc_tracepoint_debugfs_fops_tracepoint_poll);
	if (IS_ERR(dentry)) {
		dev_err(dev, "Failed to create request_poll debugfs file.\n");
		ret = PTR_ERR(dentry);
		goto out;
	}
	dentry = debugfs_create_file(
		"request_subscribe", 0220, fwtp_google_gdmc_dev->debugfs_root,
		fwtp_google_gdmc_dev,
		&google_gdmc_tracepoint_debugfs_fops_tracepoint_subscribe);
	if (IS_ERR(dentry)) {
		dev_err(dev,
			"Failed to create request_subscribe debugfs file.\n");
		ret = PTR_ERR(dentry);
		goto out;
	}

	/* Initialize tracepoint decoder. */
	ret = fwtp_decoder_init(&(fwtp_google_gdmc_dev->decoder),
				gdmc_tracepoints, ARRAY_SIZE(gdmc_tracepoints));
	if (ret) {
		dev_err(dev,
			"Failed to initialize tracepoint decoder with error %d.\n",
			ret);
		goto out;
	}
	fwtp_dev->decoder = &(fwtp_google_gdmc_dev->decoder);

out:
	/* Clean up. */
	if (dma_reserved_mem_node)
		of_node_put(dma_reserved_mem_node);

	/* Clean up on error. */
	if (ret)
		fwtp_google_gdmc_remove(pdev);

	return ret;
}

/**
 * fwtp_google_gdmc_remove - Removes a Google GDMC firmware tracepoint device.
 *
 * @pdev: The platform device to remove.
 */
static void fwtp_google_gdmc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwtp_google_gdmc_dev *fwtp_google_gdmc_dev;

	/*
	 * Get the Google GDMC firmware tracepoint device record and remove it
	 * from the platform device.
	 */
	fwtp_google_gdmc_dev = platform_get_drvdata(pdev);
	if (!fwtp_google_gdmc_dev)
		return;
	platform_set_drvdata(pdev, NULL);

	/* Disable the decoder. */
	if (fwtp_google_gdmc_dev->base.decoder)
		fwtp_decoder_enable(fwtp_google_gdmc_dev->base.decoder,
				    &(fwtp_google_gdmc_dev->base), false);

	debugfs_remove_recursive(fwtp_google_gdmc_dev->debugfs_root);

	/* Free DMA memory. */
	if (fwtp_google_gdmc_dev->dma_base)
		dma_free_coherent(dev, fwtp_google_gdmc_dev->dma_size,
				  fwtp_google_gdmc_dev->dma_base,
				  fwtp_google_gdmc_dev->dma_base_phys);

	/* Release the device reserved memory. */
	if (fwtp_google_gdmc_dev->reserved_mem_initialized)
		of_reserved_mem_device_release(dev);

	/* Unregister the GDMC host callback. */
	if (fwtp_google_gdmc_dev->registered_gdmc_host_cb) {
		gdmc_unregister_host_cb(fwtp_google_gdmc_dev->gdmc_iface,
					APC_CRITICAL_GDMC_FWTP_SERVICE);
	}

	/* Release the GDMC mailbox interface handle. */
	if (fwtp_google_gdmc_dev->gdmc_iface)
		gdmc_iface_put(fwtp_google_gdmc_dev->gdmc_iface);

	/* Deinitialize the base FWTP device. */
	fwtp_dev_deinit(&(fwtp_google_gdmc_dev->base));

	/* Log removal. */
	dev_dbg(dev, "Removed Google GDMC firmware tracepoint device.\n");
}

/*******************************************************************************
 * Platform device configuration.
 ******************************************************************************/

/* Device tree match table. */
static const struct of_device_id fwtp_google_gdmc_of_match_table[] = {
	{ .compatible = "google,fwtp-gdmc" },
	{},
};
MODULE_DEVICE_TABLE(of, fwtp_google_gdmc_of_match_table);

/* Platform driver configuration. */
static struct platform_driver fwtp_google_gdmc_driver = {
	.probe = fwtp_google_gdmc_probe,
	.remove = fwtp_google_gdmc_remove,
	.driver = {
		.name = "fwtp-google-gdmc",
		.owner = THIS_MODULE,
		.of_match_table = fwtp_google_gdmc_of_match_table,
	},
};
module_platform_driver(fwtp_google_gdmc_driver);

/*******************************************************************************
 * Module info.
 ******************************************************************************/
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GDMC firmware tracepoint");
MODULE_LICENSE("GPL");
