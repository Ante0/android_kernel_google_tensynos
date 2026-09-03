// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2021 Google LLC
#include <linux/bits.h>
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include <soc/google/goog_mba_q_xport.h>
#include <soc/google/goog_mba_cpm_iface.h>
#define CPM_SOURCE_ID_CPMLOG 0x1

struct google_cpmlog {
	struct device *dev;
	struct dentry *debugfs_root;

	/* Lock to protect DMA memory region */
	struct mutex dma_lock;
	dma_addr_t dma_phys;
	void *dma_virt;
	u32 dma_size;

	struct cpm_iface_client *cpm_client;

	u32 remote_ch;
};

/* this structure should sync with remote log driver */
struct log_resp_payload {
	__le32 count;
	__le32 status;
	__le32 unused_2;
} __packed;

/* this structure should sync with remote log driver */
struct cpm_log_entry {
	__le32 len;
	__le32 systicks;
	char data[];
} __packed;

static void google_cpm_log_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct google_cpmlog *cpmlog = priv_data;
	struct cpm_iface_payload *msg_payload = msg;

	dev_dbg(cpmlog->dev, "rx callback msg 0x%x\n", msg_payload->payload[0]);
	dev_dbg(cpmlog->dev, "rx callback type 0x%x\n",
		goog_mba_q_xport_get_type(&msg_payload->header));
}

static void
cpm_log_print_received_log(struct google_cpmlog *cpmlog, u32 *payload)
{
	struct log_resp_payload *log_resp;
	struct cpm_log_entry *log_entry;
	void *memory_iter = cpmlog->dma_virt;
	void *memory_end;
	u32 len;
	u32 remaining_bytes;
	u32 payload_count;
	u32 status;

	log_resp = (struct log_resp_payload *)payload;
	payload_count = le32_to_cpu(log_resp->count);
	status = le32_to_cpu(log_resp->status);

	if (status) {
		/* Status code is most likely a negative value. */
		dev_err(cpmlog->dev, "Received err from CPM (code: %d).\n",
			(int32_t)status);
		return;
	}

	if (!payload_count) {
		/*
		 * Warning as there should be at least one CPM log on the CPM
		 * side, for receiving the CPM log request.
		 */
		dev_warn(cpmlog->dev, "Received no CPM log.\n");
		return;
	}

	if (payload_count > cpmlog->dma_size) {
		dev_warn(cpmlog->dev, "Log size (%u) too large, change it to dma size.\n",
			 payload_count);
		payload_count = cpmlog->dma_size;
	}
	memory_end = cpmlog->dma_virt + payload_count;

	while (memory_iter < memory_end) {
		remaining_bytes = memory_end - memory_iter;
		if (remaining_bytes < sizeof(struct cpm_log_entry))  {
			dev_warn(cpmlog->dev, "Wrong remaining size (%u).\n",
				 remaining_bytes);
			break;
		}
		log_entry = memory_iter;
		len = le32_to_cpu(log_entry->len);
		/* 4-byte alignment is guaranteed by the remote log driver. */
		if (unlikely(len % 4 != 0 || len + sizeof(log_entry) > remaining_bytes)) {
			dev_warn(cpmlog->dev, "Wrong length (%u) of entry. Remaining size (%u).\n",
				 len, remaining_bytes);
			dev_warn(cpmlog->dev, "Remaining log will be dropped.\n");
			break;
		}

		/*
		 * The log string is guaranteed to be NULL terminated on CPM side.
		 * We add another NULL termination in case that the memory is corrupted.
		 * It won't override information in the original log.
		 */
		log_entry->data[len - 1] = '\0';
		dev_info(cpmlog->dev, "%d: %s", le32_to_cpu(log_entry->systicks),
			 log_entry->data);
		memory_iter += len + sizeof(struct cpm_log_entry);
	}
}

static int cpm_log_debugfs_request_write(void *data, u64 val)
{
	struct google_cpmlog *cpmlog = data;
	int ret;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req client_req = {
		.msg_type = REQUEST_MSG,
		.req_msg = &req_msg,
		.resp_msg = &resp_msg,
		.dst_id = cpmlog->remote_ch,
		.tout_ms = 3000,
	};

	req_msg.payload[0] = (u32)cpmlog->dma_phys;
	req_msg.payload[1] = cpmlog->dma_size;
	req_msg.payload[2] = 0;

	ret = mutex_lock_interruptible(&cpmlog->dma_lock);
	if (ret) {
		dev_err(cpmlog->dev, "Interrupted while acquiring lock.\n");
		return ret;
	}

	ret = cpm_send_message(cpmlog->cpm_client, &client_req);
	if (ret)
		goto unlock_mutex;

	cpm_log_print_received_log(cpmlog, resp_msg.payload);

unlock_mutex:
	mutex_unlock(&cpmlog->dma_lock);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(cpm_log_debugfs_request_fops, NULL,
			 cpm_log_debugfs_request_write, "%llu\n");

static int google_cpm_log_request_mailbox(struct device *dev, struct google_cpmlog *cpmlog)
{
	cpmlog->cpm_client = cpm_iface_request_client(dev, CPM_SOURCE_ID_CPMLOG,
						      google_cpm_log_rx_callback, cpmlog);
	if (IS_ERR(cpmlog->cpm_client))
		return PTR_ERR(cpmlog->cpm_client);

	return 0;
}

static int google_cpm_log_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dma_region, *node = dev->of_node;
	struct reserved_mem *rmem;
	struct google_cpmlog *cpmlog;
	int ret;

	cpmlog = devm_kzalloc(dev, sizeof(*cpmlog), GFP_KERNEL);
	if (!cpmlog)
		return -ENOMEM;
	cpmlog->dev = dev;
	platform_set_drvdata(pdev, cpmlog);

	ret = of_property_read_u32(node, "mba-dest-channel",
				   &cpmlog->remote_ch);
	if (ret < 0) {
		dev_err(dev, "Failed to read mba-dest-channel.\n");
		return ret;
	}

	dma_region = of_parse_phandle(node, "memory-region", 0);
	if (!dma_region) {
		dev_err(dev, "Failed to get memory-region\n");
		return -ENODEV;
	}

	rmem = of_reserved_mem_lookup(dma_region);
	if (!rmem) {
		dev_err(dev, "Failed to get reserved mem of node name %s\n",
			node->name);
		return -ENODEV;
	}

	if (rmem->size >= BIT_ULL(32)) {
		dev_err(dev, "DMA size must not exceed 32-bit: %llu\n",
			rmem->size);
		return -EINVAL;
	}

	cpmlog->dma_size = (u32)rmem->size;

	ret = of_reserved_mem_device_init(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to get reserved memory region.\n");
		return ret;
	}

	/* TODO(b/201487692) Change the mask to fit RDO SMMU design. */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		dev_err(dev, "Failed to set dma mask to 32 bits.\n");
		goto release_reserved_region;
	}

	cpmlog->dma_virt = dma_alloc_coherent(dev, cpmlog->dma_size,
					      &cpmlog->dma_phys, GFP_KERNEL);
	if (!cpmlog->dma_virt) {
		ret = -ENOMEM;
		goto release_reserved_region;
	}

	/* TODO(b/283179225): Workaround for the CPM address limitation */
	if (cpmlog->dma_size + cpmlog->dma_phys >= 0xA0000000) {
		dev_err(dev,
			"CPM can't handle the address over 0xA0000000 now on gem5.\n");
		ret = -ENOMEM;
		goto free_dma;
	}

	mutex_init(&cpmlog->dma_lock);

	ret = google_cpm_log_request_mailbox(dev, cpmlog);
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "CPM interface is not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request mailbox channel err %d.\n", ret);
		goto free_dma;
	}

	cpmlog->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	debugfs_create_file("request", 0220, cpmlog->debugfs_root,
			    cpmlog, &cpm_log_debugfs_request_fops);

	return 0;

free_dma:
	dma_free_coherent(dev, cpmlog->dma_size, cpmlog->dma_virt,
			  cpmlog->dma_phys);
release_reserved_region:
	of_reserved_mem_device_release(dev);

	return ret;
}

static inline void google_cpmlog_mbox_free(struct google_cpmlog *cpmlog)
{
	if (!IS_ERR_OR_NULL(cpmlog->cpm_client))
		cpm_iface_free_client(cpmlog->cpm_client);
}

static void google_cpm_log_remove(struct platform_device *pdev)
{
	struct google_cpmlog *cpmlog = platform_get_drvdata(pdev);

	dma_free_coherent(cpmlog->dev, cpmlog->dma_size, cpmlog->dma_virt,
			  cpmlog->dma_phys);
	of_reserved_mem_device_release(cpmlog->dev);
	google_cpmlog_mbox_free(cpmlog);
	debugfs_remove_recursive(cpmlog->debugfs_root);
}

#ifdef CONFIG_PM_SLEEP
static int google_cpm_log_suspend(struct device *dev)
{
	struct google_cpmlog *cpmlog = dev_get_drvdata(dev);

	dev_dbg(cpmlog->dev, "Suspend callback called.\n");
	/* Make sure there is no ongoing request to CPM before suspending */
	mutex_lock(&cpmlog->dma_lock);
	return 0;
}

static int google_cpm_log_resume(struct device *dev)
{
	struct google_cpmlog *cpmlog = dev_get_drvdata(dev);

	dev_dbg(cpmlog->dev, "Resume callback called.\n");
	mutex_unlock(&cpmlog->dma_lock);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id google_cpm_log_of_match_table[] = {
	{ .compatible = "google,cpm-log" },
	{},
};
MODULE_DEVICE_TABLE(of, google_cpm_log_of_match_table);

static const struct dev_pm_ops google_cpm_log_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(google_cpm_log_suspend, google_cpm_log_resume)
};

struct platform_driver google_cpm_log_driver = {
	.driver = {
		.name = "google-cpm-log",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_cpm_log_of_match_table),
		.pm = &google_cpm_log_pm_ops,
	},
	.probe  = google_cpm_log_probe,
	.remove = google_cpm_log_remove,
};

module_platform_driver(google_cpm_log_driver);
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("google cpm log driver");
MODULE_LICENSE("GPL");
