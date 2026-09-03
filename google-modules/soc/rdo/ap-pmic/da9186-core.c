// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2023 Google LLC
// mfd core driver for Morro PMIC DA9186 & DA9187.
#include <linux/err.h>
#include <linux/mailbox_client.h>
#include <linux/mfd/core.h>
#include <linux/of_device.h>

#include <ap-pmic/da9186.h>
#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>

/* Mailbox timeout in ms */
#define MBOX_CLIENT_TX_TOUT (3000)
#define MBOX_RESPONSE_TOUT (6000)
/*
 * Mailbox send request max attempts, perform retry until total attempts is
 * equal to this value. This value should always be greater than 1.
 */
#define MAILBOX_MAX_ATTEMPTS (3)

/* Mailbox response payload struct, should sync with remote driver. */
struct mailbox_resp_payload {
	__le32 status;
	__le32 data_1;
	__le32 data_2;
} __packed;

/**
 * struct da9186_mfd_context - A single connection to PMIC registers via CPM.
 * @dev: The device.
 */
struct da9186_mfd_context {
	struct device *dev;
};

static const struct mfd_cell da9186_mfd_devices[] = {
	{
		.name = "da9186-regulator",
		.of_compatible = "google,da9186-regulator",
	},
	{
		.name = "da9186-rtc",
		.of_compatible = "google,da9186-rtc",
	},
	{
		.name = "da9186-gpio",
		.of_compatible = "google,da9186-gpio",
	},
	{
		.name = "da9186-vgpio",
		.of_compatible = "google,da9186-vgpio",
	},
};

static void _inc_attempt_count(struct device *dev, int *attempts, int ret)
{
	if (attempts && (*attempts > 0)) {
		dev_err(dev, "MB retry. attempt: %d/%d. Ret: %d\n", *attempts,
			MAILBOX_MAX_ATTEMPTS, ret);
		++(*attempts);
	}
}

int pmic_mfd_mbox_request(struct device *dev, struct pmic_mfd_mbox *mbox)
{
	int ret;

	mbox->client = cpm_iface_request_client(dev, 0, NULL, NULL);
	if (IS_ERR(mbox->client)) {
		ret = PTR_ERR(mbox->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request cpm mailbox client. Err: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pmic_mfd_mbox_request);

void pmic_mfd_mbox_release(struct pmic_mfd_mbox *mbox)
{
	cpm_iface_free_client(mbox->client);
}
EXPORT_SYMBOL_GPL(pmic_mfd_mbox_release);

/*
 * Send the request command with data, blocking until the response is back.
 * Returns: 0 on success or a negative error code on failure or timeout.
 * The mailbox server response data (CPM) will be returned by *resp_data,
 * In send only case, set the resp_data = NULL will discard the response data.
 */
int pmic_mfd_mbox_send_req_blocking_read(struct device *dev,
					 struct pmic_mfd_mbox *mbox,
					 u8 mbox_dst, u8 target, u8 cmd,
					 u16 id_or_addr,
					 struct mailbox_data req_data,
					 struct mailbox_data *resp_data)
{
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	int attempts;
	int ret;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MBOX_CLIENT_TX_TOUT;
	cpm_req.dst_id = mbox_dst;

	req_msg.payload[0] = cpu_to_le32(FIELD_PREP(MB_QUEUE_PMIC_REQ_TARGET_MASK, target) |
				       FIELD_PREP(MB_QUEUE_PMIC_REQ_COMMAND_MASK, cmd) |
				       FIELD_PREP(MB_QUEUE_PMIC_REQ_ADDR_MASK, id_or_addr));
	req_msg.payload[1] = cpu_to_le32(req_data.data[0]);
	req_msg.payload[2] = cpu_to_le32(req_data.data[1]);

	if (IS_ERR_OR_NULL(mbox->client)) {
		dev_err(dev, "Mailbox not initialized\n");
		return -ENODEV;
	}

	dev_dbg(dev,
		"Send msg to CPM with cmd: %u  addr: %u  data[0]: %#x [1]: %#x\n",
		cmd, id_or_addr, req_msg.payload[0], req_msg.payload[1]);

	for (attempts = 1; attempts <= MAILBOX_MAX_ATTEMPTS;
	     _inc_attempt_count(dev, &attempts, ret)) {
		struct mailbox_resp_payload *resp_payload;
		u32 status, data_1, data_2;

		ret = cpm_send_message(mbox->client, &cpm_req);
		if (ret)
			continue;

		/* Store received payload */
		resp_payload = (struct mailbox_resp_payload *)&resp_msg.payload[0];
		status = FIELD_GET(MB_QUEUE_PMIC_RSP_STATUS_MASK,
				  le32_to_cpu(resp_payload->status));
		data_1 = le32_to_cpu(resp_payload->data_1);
		data_2 = le32_to_cpu(resp_payload->data_2);

		dev_dbg(dev,
			"CPM response with status: %u  data 1: %u  data 2: %u\n",
			status, resp_payload->data_1, resp_payload->data_1);

		/* Validate the response status from CPM. */
		if (unlikely(status != MB_RSP_STS_OK)) {
			dev_err(dev, "Mailbox response status failure: %u\n",
				status);
			ret = -EPIPE;
			continue;
		}
		/* Write-back the response value. */
		if (resp_data) {
			resp_data->data[0] = data_1;
			resp_data->data[1] = data_2;
		}
		/* Return 0 for success. */
		ret = 0;
		break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(pmic_mfd_mbox_send_req_blocking_read);

/*
 * Send the request command with data, blocking until the response is back.
 * Discard the unused response data.
 * Returns: 0 on success or a negative error code on failure or timeout.
 */
int pmic_mfd_mbox_send_req_blocking(struct device *dev,
				    struct pmic_mfd_mbox *mbox,
				    u8 mbox_dst,
				    u8 target, u8 cmd, u16 id_or_addr,
				    struct mailbox_data req_data)
{
	return pmic_mfd_mbox_send_req_blocking_read(dev, mbox, mbox_dst,
						    target, cmd, id_or_addr,
						    req_data, NULL);
}
EXPORT_SYMBOL_GPL(pmic_mfd_mbox_send_req_blocking);

static int da9186_mfd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da9186_mfd_context *da9186_mfd;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "must be instantiated using device tree\n");
		return -ENODEV;
	}

	da9186_mfd = devm_kzalloc(dev, sizeof(struct da9186_mfd_context),
				  GFP_KERNEL);
	if (!da9186_mfd)
		return -ENOMEM;

	platform_set_drvdata(pdev, da9186_mfd);
	da9186_mfd->dev = dev;

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, da9186_mfd_devices,
				   ARRAY_SIZE(da9186_mfd_devices), NULL, 0,
				   NULL);
	if (ret)
		return ret;

	dev_dbg(dev, "DA9186 MFD driver probe done\n");

	return 0;
}

static const struct of_device_id da9186_mfd_of_match[] = {
	{ .compatible = "google,da9186mfd" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, da9186_mfd_of_match);

static struct platform_driver da9186_mfd_driver = {
	.probe = da9186_mfd_probe,
	.driver = {
		.name = "da9186-mfd",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(da9186_mfd_of_match),
	},
};
module_platform_driver(da9186_mfd_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("DA9186 PMIC multi-function core driver");
MODULE_LICENSE("GPL");
