// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for the Google GSA core.
 *
 * Copyright (C) 2024 Google LLC
 */
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>

#include "gsa_tz.h"
#include "tzprot-ipc.h"

struct tzprot_dev_state {
	struct device *dev;
	struct gsa_tz_chan_ctx prot_srv;
};

int trusty_protect_ip_bulk(struct device *dev, uint32_t dev_enable_mask,
	uint32_t dev_disable_mask)
{
	int rc;
	const uint32_t BULK_MODE = BIT(31);
	struct platform_device *pdev = to_platform_device(dev);
	struct tzprot_dev_state *s = platform_get_drvdata(pdev);
	struct media_prot_req req;
	struct media_prot_rsp rsp;

	req.cmd = MEDIA_PROT_CMD_SET_IP_PROT;
	req.set_ip_prot_req.dev_enable_mask = BULK_MODE | dev_enable_mask;
	req.set_ip_prot_req.dev_disable_mask = BULK_MODE | dev_disable_mask;

	rc = gsa_tz_chan_msg_xchg(&s->prot_srv, &req, sizeof(req),
				  &rsp, sizeof(rsp));
	if (rc < 0 || rc < MIN_MEDIA_PROT_RSP)
		return -EIO;

	if (rsp.cmd != (req.cmd | MEDIA_PROT_CMD_RESP)) {
		return -EIO;
	}

	return rsp.err;
}
EXPORT_SYMBOL_GPL(trusty_protect_ip_bulk);

int trusty_protect_ip(struct device *dev, uint32_t prot_id, bool enable)
{
	return trusty_protect_ip_bulk(dev, enable ? 1 << prot_id : 0,
					   enable ? 0 : 1 << prot_id);
}
EXPORT_SYMBOL_GPL(trusty_protect_ip);

int trusty_get_histogram(struct device *dev, uint8_t hist_id, uint8_t channel_mask,
			 uint16_t *luma_arr, uint8_t arr_size)
{
	int rc;
	uint8_t i;
	uint8_t res_idx;

	struct platform_device *pdev = to_platform_device(dev);
	struct tzprot_dev_state *s = platform_get_drvdata(pdev);
	struct media_prot_req req = { 0 };
	struct media_prot_rsp rsp;

	req.cmd = MEDIA_PROT_CMD_GET_HISTOGRAM;
	req.get_hist_luma_req.hist_id = hist_id;
	req.get_hist_luma_req.channel_mask = channel_mask;

	rc = gsa_tz_chan_msg_xchg(&s->prot_srv, &req, sizeof(req), &rsp, sizeof(rsp));

	if (rc != sizeof(rsp))
		return -EIO;

	if (rsp.cmd != (req.cmd | MEDIA_PROT_CMD_RESP))
		return -EIO;

	if (rsp.err != 0)
		return rsp.err;

	for (i = 0, res_idx = 0; i < MAX_HIST_CHANNELS; i++) {
		if ((channel_mask & BIT(i)) && (res_idx < arr_size)) {
			luma_arr[res_idx] = rsp.get_hist_luma_rsp.chan_luma[i];
			res_idx += 1;
		}
	}

	return rsp.err;
}
EXPORT_SYMBOL_GPL(trusty_get_histogram);

static int tzprot_probe(struct platform_device *pdev)
{
	struct tzprot_dev_state *s;
	struct device *dev = &pdev->dev;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->dev = dev;
	platform_set_drvdata(pdev, s);

	gsa_tz_chan_ctx_init(&s->prot_srv, TZPROT_PORT, dev);
	return 0;
}

static void tzprot_remove(struct platform_device *pdev)
{
	struct tzprot_dev_state *s = platform_get_drvdata(pdev);

	/* close connection to tz services */
	gsa_tz_chan_close(&s->prot_srv);
}

static const struct of_device_id tzprot_of_match[] = {
	{ .compatible = "google,gsoc-tzprot-v1", },
	{},
};
MODULE_DEVICE_TABLE(of, tzprot_of_match);

static struct platform_driver tzprot_driver = {
	.probe = tzprot_probe,
	.remove = tzprot_remove,
	.driver	= {
		.name = "tzprot",
		.of_match_table = tzprot_of_match,
	},
};

static int __init tzprot_driver_init(void)
{
	return platform_driver_register(&tzprot_driver);
}

static void __exit tzprot_driver_exit(void)
{
	platform_driver_unregister(&tzprot_driver);
}

MODULE_DESCRIPTION("Google TZPROT platform driver");
MODULE_LICENSE("GPL v2");
module_init(tzprot_driver_init);
module_exit(tzprot_driver_exit);
