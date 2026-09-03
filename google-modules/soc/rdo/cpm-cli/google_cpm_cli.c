// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2023 Google LLC
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <soc/google/goog_mba_q_xport.h>
#include <soc/google/goog_mba_cpm_iface.h>
#define CPM_SOURCE_ID_CPM_CLI 0x9

#define MBA_CLIENT_TX_TOUT 3000
#define MBA_REQUEST_TIMEOUT 3000

struct google_cpm_cli {
	struct device *dev;
	struct dentry *debugfs_root;
	struct mutex msg_lock; /* userspace access lock */
	int lmf_per_direction_buffer_size;

	struct cpm_iface_client *cpm_client;

	u32 remote_ch;

	void __iomem *shared_memory_base;
	u32 cpm_sram_base;
	u32 lmf_buffer_offset;
};

static void google_cpm_cli_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct google_cpm_cli *cpm_cli = priv_data;
	struct device *dev = cpm_cli->dev;
	struct cpm_iface_payload *resp_msg = msg;

	dev_info(dev, "rx callback msg 0x%x\n", resp_msg->payload[0]);
	dev_dbg(dev, "rx callback type 0x%x\n", goog_mba_q_xport_get_type(msg));
}

static void google_cpm_cli_get_lmf_offset(struct google_cpm_cli *cpm_cli, u32 lmf_buffer_offset)
{
	cpm_cli->lmf_buffer_offset = lmf_buffer_offset;
	cpm_cli->shared_memory_base = devm_ioremap(cpm_cli->dev,
						   cpm_cli->cpm_sram_base +
						   cpm_cli->lmf_buffer_offset,
						   cpm_cli->lmf_per_direction_buffer_size);
}

static int google_cpm_cli_request_lmf_offset(struct google_cpm_cli *cpm_cli)
{
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req client_req = {
		.msg_type = REQUEST_MSG,
		.req_msg = &req_msg,
		.resp_msg = &resp_msg,
		.dst_id = cpm_cli->remote_ch,
		.tout_ms = MBA_CLIENT_TX_TOUT,
	};
	int ret;

	req_msg.payload[0] = 0;
	req_msg.payload[1] = 0;
	req_msg.payload[2] = 0;

	ret = cpm_send_message(cpm_cli->cpm_client, &client_req);
	if (ret < 0) {
		dev_err(cpm_cli->dev, "Send message failed ret (%d)\n", ret);
		return ret;
	}
	google_cpm_cli_get_lmf_offset(cpm_cli, resp_msg.payload[0]);

	return 0;
}

static int google_cpm_cli_send_cmd_len(struct google_cpm_cli *cpm_cli, u32 cmd_len)
{
	int ret;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req client_req = {
		.msg_type = REQUEST_MSG,
		.req_msg = &req_msg,
		.resp_msg = &resp_msg,
		.dst_id = cpm_cli->remote_ch,
		.tout_ms = MBA_CLIENT_TX_TOUT,
	};

	req_msg.payload[0] = cmd_len;
	req_msg.payload[1] = 0;
	req_msg.payload[2] = 0;

	ret = cpm_send_message(cpm_cli->cpm_client, &client_req);
	if (ret < 0) {
		dev_err(cpm_cli->dev, "Send message failed ret (%d)\n", ret);
		return ret;
	}

	return 0;
}

/* Copy the command to CPM SRAM and send the command length via mailbox */
static ssize_t google_cpm_cli_debugfs_cli_write(struct file *filp, const char __user *ubuf,
						size_t count, loff_t *ppos)
{
	struct google_cpm_cli *cpm_cli = filp->private_data;
	ssize_t buf_size;
	char *write_buf;
	int ret;

	/* Empty string or just a newline */
	if (count <= 1) {
		ret = -EINVAL;
		dev_err(cpm_cli->dev, "Empty string was passed.\n");
		return ret;
	}
	if (count > cpm_cli->lmf_per_direction_buffer_size) {
		ret = -EINVAL;
		dev_err(cpm_cli->dev, "User's input is too long.\n");
		return ret;
	}

	ret = mutex_lock_interruptible(&cpm_cli->msg_lock);
	if (ret)
		return ret;

	write_buf = devm_kzalloc(cpm_cli->dev, cpm_cli->lmf_per_direction_buffer_size,
				 GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	buf_size = simple_write_to_buffer(write_buf, count, ppos, ubuf, count);
	if (buf_size < 0) {
		ret = buf_size;
		goto cpm_cli_debugfs_cli_write_out;
	}

	memcpy_toio(cpm_cli->shared_memory_base, write_buf, count);
	ret = google_cpm_cli_send_cmd_len(cpm_cli, count);

cpm_cli_debugfs_cli_write_out:
	devm_kfree(cpm_cli->dev, write_buf);
	mutex_unlock(&cpm_cli->msg_lock);
	return ret == 0 ? count : ret;
}

static const struct file_operations cpm_cli_debugfs_cli_fops = {
	.open = simple_open,
	.write = google_cpm_cli_debugfs_cli_write,
};

static int google_cpm_cli_request_mailbox(struct device *dev, struct google_cpm_cli *cpm_cli)
{
	struct cpm_iface_client *cpm_client;

	cpm_client = cpm_iface_request_client(dev, CPM_SOURCE_ID_CPM_CLI,
					      google_cpm_cli_rx_callback, cpm_cli);
	if (IS_ERR(cpm_client))
		return PTR_ERR(cpm_client);

	cpm_cli->cpm_client = cpm_client;

	return 0;
}

static inline void google_cpm_cli_release_mailbox(struct google_cpm_cli *cpm_cli)
{
	if (cpm_cli->cpm_client)
		cpm_iface_free_client(cpm_cli->cpm_client);
}

static void google_cpm_cli_remove(struct platform_device *pdev)
{
	struct google_cpm_cli *cpm_cli = platform_get_drvdata(pdev);

	google_cpm_cli_release_mailbox(cpm_cli);
	debugfs_remove_recursive(cpm_cli->debugfs_root);
}

static int google_cpm_cli_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_cpm_cli *cpm_cli;
	struct device_node *np = dev->of_node;
	int ret;

	cpm_cli = devm_kzalloc(dev, sizeof(*cpm_cli), GFP_KERNEL);
	if (!cpm_cli)
		return -ENOMEM;
	cpm_cli->dev = dev;
	platform_set_drvdata(pdev, cpm_cli);

	ret = google_cpm_cli_request_mailbox(dev, cpm_cli);
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "CPM interface is not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request mailbox channel err %d.\n", ret);
		goto probe_exit;
	}

	if (of_property_read_u32(np, "mba-dest-channel", &cpm_cli->remote_ch)) {
		ret = -EINVAL;
		dev_err(cpm_cli->dev, "Failed to read mba-dest-channel.\n");
		goto probe_exit;
	}

	if (of_property_read_u32(np, "cpm-sram-base", &cpm_cli->cpm_sram_base)) {
		ret = -EINVAL;
		dev_err(cpm_cli->dev, "Failed to read cpm-sram-base.\n");
		goto probe_exit;
	}

	if (of_property_read_u32(np, "lmf-per-direction-buffer-size",
				 &cpm_cli->lmf_per_direction_buffer_size)) {
		ret = -EINVAL;
		dev_err(cpm_cli->dev, "Failed to read lmf-per-direction-buffer-size.\n");
		goto probe_exit;
	}

	mutex_init(&cpm_cli->msg_lock);
	cpm_cli->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	debugfs_create_file("cli", 0220, cpm_cli->debugfs_root, cpm_cli,
			    &cpm_cli_debugfs_cli_fops);

	ret = google_cpm_cli_request_lmf_offset(cpm_cli);

	return ret;

probe_exit:
	google_cpm_cli_remove(pdev);
	return ret;
}

static const struct of_device_id google_cpm_cli_of_match_table[] = {
	{ .compatible = "google,cpm-cli", },
	{},
};
MODULE_DEVICE_TABLE(of, google_cpm_cli_of_match_table);

struct platform_driver google_cpm_cli_driver = {
	.driver = {
		.name = "google-cpm-cli",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_cpm_cli_of_match_table),
	},
	.probe  = google_cpm_cli_probe,
	.remove = google_cpm_cli_remove,
};

module_platform_driver(google_cpm_cli_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google cpm-cli driver");
MODULE_LICENSE("GPL");
