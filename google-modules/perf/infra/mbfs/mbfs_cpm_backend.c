// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <soc/google/goog_mba_cpm_iface.h>

#include "mbfs_backend.h"

#define DEV_ERR(dev, fmt, ...) dev_err(dev, "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DEV_INFO(dev, fmt, ...) dev_info(dev, "%s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DEV_DBG(dev, fmt, ...) dev_dbg(dev, fmt, ##__VA_ARGS__)

#define MAILBOX_RECEIVE_TIMEOUT_MS 10000

struct comm_info {
	void *cpm_client;
	u32 cpm_service_idx;
	struct device *dev;
};

static struct comm_info comm;
static bool initialized;

static int cpm_submit_request(const struct mbfs_request *request, struct mbfs_response *response)
{
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload qst = { 0 };
	struct cpm_iface_payload ans = { 0 };
	int err = 0;
	struct device *dev = comm.dev;

	memcpy(qst.payload, request, sizeof(qst.payload));
	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &qst;
	cpm_req.resp_msg = &ans;
	cpm_req.tout_ms = MAILBOX_RECEIVE_TIMEOUT_MS;
	cpm_req.dst_id = comm.cpm_service_idx;

	DEV_DBG(dev, "mbfs CPM request: %x %x %x\n", qst.payload[0], qst.payload[1],
		qst.payload[2]);
	err = cpm_send_message(comm.cpm_client, &cpm_req);
	if (err) {
		DEV_ERR(dev, "mbfs cpm_send_message() failed, err = %d\n", err);
		// notify the users of a potential issue and prompt them to investigate
		BUG_ON(err == -ETIME);
		return -ECOMM;
	}
	memcpy(response, ans.payload, sizeof(ans.payload));
	DEV_DBG(dev, "mbfs CPM response: %x %x %x\n", ans.payload[0], ans.payload[1],
		ans.payload[2]);

	return 0;
}

static struct mbfs_client_backend cpm_backend = {
	.name = "cpm",
	.submit_request = cpm_submit_request,
};

static void *init_cpm_client(struct device *dev)
{
	int err;
	struct cpm_iface_client *client;

	client = cpm_iface_request_client(dev, 0, NULL, NULL);
	if (IS_ERR(client)) {
		err = PTR_ERR(client);
		if (err == -EPROBE_DEFER)
			DEV_ERR(dev, "cpm interface not ready; Try again later\n");
		else
			DEV_ERR(dev, "Failed to request cpm client: err = %d\n", err);
		return client;
	}
	return client;
}

static inline void remove_cpm_client(void *client)
{
	cpm_iface_free_client(client);
}

static int mbfs_cpm_backend_device_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;

	DEV_INFO(dev, "Probing device\n");
	/* Device initialization here */
	if (initialized) {
		DEV_ERR(dev, "A device exists and this driver supports only one device\n");
		return -EEXIST;
	}

	ret = of_property_read_u32(dev->of_node, "service-idx", &comm.cpm_service_idx);
	if (ret) {
		DEV_ERR(dev, "Error parsing service-idx property: ret = %d\n", ret);
		return ret;
	}
	comm.cpm_client = init_cpm_client(dev);
	if ((IS_ERR(comm.cpm_client))) {
		ret = PTR_ERR(comm.cpm_client);
		DEV_ERR(dev, "Error calling init_cpm_client: ret = %d\n", ret);
		return ret;
	}
	comm.dev = dev;
	cpm_backend.dev = dev;
	ret = register_mbfs_backend(&cpm_backend);
	if (ret) {
		DEV_ERR(dev, "Error calling register_mbfs_backend: ret = %d\n", ret);
		return ret;
	}
	initialized = true;

	return 0;
}

/* Platform driver remove function */
static void mbfs_cpm_backend_device_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	DEV_INFO(dev, "Removing device\n");
	unregister_mbfs_backend(&cpm_backend);
	remove_cpm_client(comm.cpm_client);
	initialized = false;
}

static const struct of_device_id mbfs_cpm_backend_of_match[] = {
	{
		.compatible = "google,mbfs_cpm_backend",
	},
	{},
};

static struct platform_driver mbfs_cpm_backend_device_driver = {
	.probe = mbfs_cpm_backend_device_probe,
	.remove = mbfs_cpm_backend_device_remove,
	.driver = {
		.name = "mbfs_cpm_backend_drv",
		.owner = THIS_MODULE,
		.of_match_table = mbfs_cpm_backend_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
#ifdef MBFS_DEV
module_platform_driver(mbfs_cpm_backend_device_driver);
#else
builtin_platform_driver(mbfs_cpm_backend_device_driver);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yong Zhao <yozhao@google.com>");
MODULE_DESCRIPTION("CPM backend driver for CPMFS");
MODULE_DEVICE_TABLE(of, mbfs_cpm_backend_of_match);
