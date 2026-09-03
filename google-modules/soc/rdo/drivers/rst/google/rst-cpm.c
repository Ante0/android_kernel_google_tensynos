// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver to control Google SoC LPCM resets via CPM.
 * Copyright (C) 2023 Google LLC.
 */

#include <linux/container_of.h>
#include <linux/debugfs.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

#include "rst-cpm.h"

static int goog_cpm_rst_init_mailbox(struct device *dev,
				     struct goog_cpm_rst_mbox *cpm_mbox)
{
	int ret;

	cpm_mbox->client = cpm_iface_request_client(dev, 0, NULL, NULL);
	if (IS_ERR(cpm_mbox->client)) {
		ret = PTR_ERR(cpm_mbox->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request cpm client ret %d\n", ret);
		return ret;
	}

	cpm_mbox->send_msg_and_block = cpm_send_message;
	return 0;
}

static int goog_cpm_rst_mba_check_ret(struct device *dev, int ret)
{
	switch (ret) {
	case NO_ERROR:
		return 0;
	case ERR_LPCM_NOT_SUPPORTED:
		dev_err(dev, "the received request_id is invalid\n");
		return -EPROTO;
	case ERR_LPCM_INVALID_ARGS:
		dev_err(dev, "invalid lpcm_id or rst_id\n");
		return -EINVAL;
	case ERR_LPCM_TIMED_OUT:
		dev_err(dev, "operation timed out\n");
		return -ETIMEDOUT;
	case ERR_LPCM_GENERIC:
		dev_err(dev, "internal error occurred\n");
		return -ESERVERFAULT;
	default:
		dev_err(dev, "unknown error response from CPM\n");
		return -EPROTO;
	}
}

static int goog_cpm_rst_mba_resp_hdl(struct goog_cpm_rst *cpm_rst, const struct cpm_msg *msg)
{
	struct device *dev = cpm_rst->rcdev.dev;
	int dst = msg->cpm_req.dst_id;
	int ret = msg->resp_msg.payload[0];

	dev_dbg(dev, "got resp from %u, data %d.\n", dst, ret);

	return goog_cpm_rst_mba_check_ret(dev, ret);
}

void goog_cpm_init_payload(struct goog_cpm_rst *cpm_rst, struct cpm_msg *msg,
			   unsigned long rst_id, int op_id)
{
	struct cpm_iface_req *cpm_req = &msg->cpm_req;
	struct cpm_iface_payload *req_msg = &msg->req_msg;
	struct cpm_iface_payload *resp_msg = &msg->resp_msg;
	struct goog_cpm_lpcm_service_req *lpcm_req;

	cpm_req->msg_type = REQUEST_MSG;
	cpm_req->req_msg = req_msg;
	cpm_req->resp_msg = resp_msg;
	cpm_req->tout_ms = MAILBOX_SEND_TIMEOUT_MS;
	cpm_req->dst_id = LPCM_REMOTE_CHANNEL;

	lpcm_req = (struct goog_cpm_lpcm_service_req *)req_msg->payload;
	lpcm_req->req_id = LPCM_CMD_SET_RST;
	lpcm_req->lpcm_id = cpm_rst->lpcm_id;
	lpcm_req->rst_id = rst_id;
	lpcm_req->op_id = op_id;
}
EXPORT_SYMBOL_GPL(goog_cpm_init_payload);

int goog_cpm_rst_send_mba_mail(struct goog_cpm_rst *cpm_rst, unsigned long rst_id, int op_id)
{
	struct device *dev = cpm_rst->rcdev.dev;
	struct goog_cpm_rst_mbox *cpm_mbox = cpm_rst->cpm_mbox;
	struct cpm_iface_client *client = cpm_mbox->client;
	struct cpm_msg msg;
	struct cpm_iface_req *cpm_req = &msg.cpm_req;
	struct cpm_iface_payload *req_msg = &msg.req_msg;
	int ret;

	goog_cpm_init_payload(cpm_rst, &msg, rst_id, op_id);

	dev_dbg(dev, "lpcm_id %d, rst_id %lu: send mba mail: [%u, %u, %u]\n",
		cpm_rst->lpcm_id, rst_id, req_msg->payload[0], req_msg->payload[1],
		req_msg->payload[2]);

	ret = cpm_rst->cpm_mbox->send_msg_and_block(client, cpm_req);
	if (ret < 0) {
		dev_err(dev,
			"lpcm_id %d, rst_id %lu: failed to send request to CPM, ret=%d\n",
			cpm_rst->lpcm_id, rst_id, ret);
		return ret;
	}

	return goog_cpm_rst_mba_resp_hdl(cpm_rst, &msg);
}
EXPORT_SYMBOL_GPL(goog_cpm_rst_send_mba_mail);

static int goog_cpm_rst_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct goog_cpm_rst *goog_cpm_rst = to_goog_cpm_rst(rcdev);

	dev_dbg(rcdev->dev, "assert reset id=%lu of lpcm %u\n", id,
		goog_cpm_rst->lpcm_id);
	return goog_cpm_rst_send_mba_mail(goog_cpm_rst, id, ASSERT_OP_ID);
}

static int goog_cpm_rst_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	struct goog_cpm_rst *goog_cpm_rst = to_goog_cpm_rst(rcdev);

	dev_dbg(rcdev->dev, "deassert reset id=%lu of lpcm %u\n", id,
		goog_cpm_rst->lpcm_id);
	return goog_cpm_rst_send_mba_mail(goog_cpm_rst, id, DEASSERT_OP_ID);
}

static const struct reset_control_ops goog_cpm_rst_ops = {
	.assert = goog_cpm_rst_assert,
	.deassert = goog_cpm_rst_deassert,
};

#ifdef CONFIG_DEBUG_FS
static int goog_cpm_rst_debugfs_assert_set(void *data, u64 rst_id)
{
	struct goog_cpm_rst *goog_cpm_rst = (struct goog_cpm_rst *)data;

	return goog_cpm_rst_assert(&goog_cpm_rst->rcdev, rst_id);
}

static int goog_cpm_rst_debugfs_deassert_set(void *data, u64 rst_id)
{
	struct goog_cpm_rst *goog_cpm_rst = (struct goog_cpm_rst *)data;

	return goog_cpm_rst_deassert(&goog_cpm_rst->rcdev, rst_id);
}

DEFINE_DEBUGFS_ATTRIBUTE(goog_cpm_rst_assert_debugfs_fops, NULL,
			 goog_cpm_rst_debugfs_assert_set, "%llu\n");

DEFINE_DEBUGFS_ATTRIBUTE(goog_cpm_rst_deassert_debugfs_fops, NULL,
			 goog_cpm_rst_debugfs_deassert_set, "%llu\n");

static void goog_cpm_rst_add_debugfs(struct goog_cpm_rst *goog_cpm_rst,
				     struct dentry *debugfs_root)
{
	struct dentry *d = debugfs_create_dir(goog_cpm_rst->rcdev.of_node->name,
					      debugfs_root);

	debugfs_create_file("assert", 0220, d, goog_cpm_rst,
			    &goog_cpm_rst_assert_debugfs_fops);
	debugfs_create_file("deassert", 0220, d, goog_cpm_rst,
			    &goog_cpm_rst_deassert_debugfs_fops);
}
#endif

static int goog_cpm_rst_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct goog_cpm_rst_controller *rc;
	int child_count, i, ret;

	rc = devm_kzalloc(dev, sizeof(*rc), GFP_KERNEL);

	if (!rc)
		return -ENOMEM;

	rc->dev = dev;

	ret = goog_cpm_rst_init_mailbox(dev, &rc->cpm_mbox);
	if (ret < 0)
		return ret;
	platform_set_drvdata(pdev, rc);

	child_count = of_get_child_count(np);
	rc->resets =
		devm_kcalloc(dev, child_count, sizeof(*rc->resets), GFP_KERNEL);

	i = 0;
	for_each_available_child_of_node(np, child_np) {
		ret = of_property_read_u32(child_np, "reset-num",
					   &rc->resets[i].rcdev.nr_resets);
		if (ret < 0) {
			dev_err(dev, "failed to read reset-num\n");
			of_node_put(child_np);
			return -EINVAL;
		}

		ret = of_property_read_u32(child_np, "lpcm-id",
					   &rc->resets[i].lpcm_id);
		if (ret < 0) {
			dev_err(dev, "failed to read lpcm-id\n");
			of_node_put(child_np);
			return -EINVAL;
		}

		rc->resets[i].rcdev.dev = dev;
		rc->resets[i].rcdev.owner = THIS_MODULE;
		rc->resets[i].rcdev.of_node = child_np;
		rc->resets[i].rcdev.ops = &goog_cpm_rst_ops;
		rc->resets[i].cpm_mbox = &rc->cpm_mbox;

		ret = devm_reset_controller_register(dev, &rc->resets[i].rcdev);

		if (ret < 0) {
			dev_err(dev,
				"failed to register resets of lpcm_id=%d\n",
				rc->resets[i].lpcm_id);
			return ret;
		}
		i++;
	}

#ifdef CONFIG_DEBUG_FS
	rc->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	for (i = 0; i < child_count; i++)
		goog_cpm_rst_add_debugfs(&rc->resets[i], rc->debugfs_root);
#endif
	return 0;
};

static inline void goog_cpm_rst_mbox_free(struct goog_cpm_rst_mbox *mbox)
{
	cpm_iface_free_client(mbox->client);
}

static void goog_cpm_rst_remove(struct platform_device *pdev)
{
	struct goog_cpm_rst_controller *rc = platform_get_drvdata(pdev);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(rc->debugfs_root);
#endif
	goog_cpm_rst_mbox_free(&rc->cpm_mbox);
}

static const struct of_device_id goog_cpm_rst_of_match_table[] = {
	{ .compatible = "google,cpm-rst" },
	{},
};

static struct platform_driver goog_cpm_rst_driver = {
	.probe = goog_cpm_rst_probe,
	.remove = goog_cpm_rst_remove,
	.driver = {
		.name = "goog_cpm_rst",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goog_cpm_rst_of_match_table),
	},
};

module_platform_driver(goog_cpm_rst_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google CPM Reset Driver");
MODULE_LICENSE("GPL");
