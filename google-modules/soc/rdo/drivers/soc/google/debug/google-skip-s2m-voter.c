// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 *
 * google-skip-s2m-voter driver.
 */

#include <linux/keydebug.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_mba_nq_xport.h>

struct google_skip_s2m_voter_dev {
	struct device *dev;
	struct gdmc_iface *gdmc_iface;
	struct mutex lock;
};

struct google_skip_s2m_voter_dev skip_s2m_voter_dev;

static int google_skip_s2m_vote(bool vote)
{
	u32 payload[GDMC_MBA_DHUB_PAYLOAD_SIZE] = { 0 };
	int ret;

	mutex_lock(&skip_s2m_voter_dev.lock);

	if (!skip_s2m_voter_dev.gdmc_iface) {
		ret = -ENODEV;
		goto out_unlock;
	}
	dev_dbg(skip_s2m_voter_dev.dev, "Skip s2m vote: %s\n", vote ? "upvote" : "downvote");

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_CRASH_RESET);
	goog_mba_nq_xport_set_oneway(payload, true);
	goog_mba_nq_xport_set_data(payload, GDMC_MBA_CRASH_RESET_CMD_SET_SKIP_S2M_VOTE);

	payload[1] = (u32)vote;

	ret = gdmc_send_message(skip_s2m_voter_dev.gdmc_iface, payload);
	if (ret < 0)
		dev_err(skip_s2m_voter_dev.dev, "Failed to gdmc_send_message (ret:%d)\n", ret);

out_unlock:
	mutex_unlock(&skip_s2m_voter_dev.lock);
	return ret;
}

static int google_skip_s2m_voter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gdmc_iface *gdmc_iface;

	skip_s2m_voter_dev.dev = dev;
	mutex_init(&skip_s2m_voter_dev.lock);

	/* Get a GDMC mailbox interface handle */
	gdmc_iface = gdmc_iface_get(dev);
	if (IS_ERR(gdmc_iface)) {
		dev_err(&pdev->dev, "failed to get gdmc interface %ld\n",
			PTR_ERR(gdmc_iface));
		return PTR_ERR(gdmc_iface);
	}
	skip_s2m_voter_dev.gdmc_iface = gdmc_iface;

	/* Register skip S2M vote function for keydebug */
	keydebug_register_skip_s2m_vote_op(google_skip_s2m_vote);

	return 0;
}

static void google_skip_s2m_voter_remove(struct platform_device *pdev)
{
	/* Unregister skip S2M vote function for keydebug */
	keydebug_register_skip_s2m_vote_op(NULL);

	mutex_lock(&skip_s2m_voter_dev.lock);
	if (skip_s2m_voter_dev.gdmc_iface) {
		gdmc_iface_put(skip_s2m_voter_dev.gdmc_iface);
		skip_s2m_voter_dev.gdmc_iface = NULL;
	}
	mutex_unlock(&skip_s2m_voter_dev.lock);
	mutex_destroy(&skip_s2m_voter_dev.lock);

	dev_dbg(skip_s2m_voter_dev.dev, "Removed Google skip S2M voter device\n");
}

static const struct of_device_id google_skip_s2m_voter_dt_match[] = {
	{
		.compatible = "google,skip-s2m-voter",
	},
	{},
};
MODULE_DEVICE_TABLE(of, google_skip_s2m_voter_dt_match);

static struct platform_driver google_skip_s2m_voter_driver = {
	.probe = google_skip_s2m_voter_probe,
	.remove = google_skip_s2m_voter_remove,
	.driver = {
		.name = "google-skip-s2m-voter",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_skip_s2m_voter_dt_match),
	},
};
module_platform_driver(google_skip_s2m_voter_driver);

MODULE_AUTHOR("Yu-Yu Chen <yuyulydia@google.com>");
MODULE_DESCRIPTION("Google Skip S2M Voter");
MODULE_LICENSE("GPL");
