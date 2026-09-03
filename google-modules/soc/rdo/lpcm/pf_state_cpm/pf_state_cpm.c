// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for controlling
 * LPCM's power frequency states (Local Power Clock Manager)
 * via CPM.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/dev_printk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/clk-provider.h>
#include <linux/container_of.h>
#include <linux/mailbox_client.h>

#include <soc/google/goog_mba_cpm_iface.h>
#include <dvfs-helper/google_dvfs_helper.h>

#define MAILBOX_SEND_TIMEOUT_MS 10000
#define MAILBOX_RECEIVE_TIMEOUT_MS 10000
#define INVALID_PF_STATE -1
#define UNUSED_PF_STATE -1
#define MAX_PF_STATE_NUM 32
#define LPCM_SET_PF_STATE_REQ_ID 0
#define LPCM_GET_PF_STATE_REQ_ID 3
#define PF_STATE_BITFIELD GENMASK(23, 16)
#define LPCM_ID_BITFIELD GENMASK(15, 8)
#define REQ_ID_BITFIELD GENMASK(7, 0)

#define NO_ERROR (0)
#define ERR_ALREADY_STARTED (-6)
#define ERR_RECURSE_TOO_DEEP (-23)
#define ERR_NOT_SUPPORTED (-24)
#define ERR_NOT_ALLOWED (-17)
#define ERR_INVALID_ARGS (-8)
#define ERR_TIMED_OUT (-13)
#define ERR_GENERIC (-1)

#define to_pf_state_clk(_hw) container_of(_hw, struct pf_state_clk, hw)

struct pf_state_cpm {
	struct device *dev;
	struct cpm_iface_client *client;

	u32 remote_ch;
};

struct pf_state_clk {
	struct device *dev;
	struct clk_hw hw;
	struct pf_state_cpm *pf_state_cpm;
	const char *name;

	u32 lpcm_id;
	struct dvfs_domain_info *dvfs_helper_info;
	struct completion set_rate_complete;
	int err;
};

struct cpm_set_rate_resp_payload {
	u32 error_code;
	u32 unused_1;
	u32 unused_2;
} __packed;

struct cpm_get_rate_resp_payload {
	u32 error_code;
	u8 lpcm_id;
	u8 pf_state;
} __packed;

static void set_rate_mba_resp_process(struct pf_state_clk *pf_state_clk,
				      struct cpm_set_rate_resp_payload *resp_payload)
{
	struct device *dev = pf_state_clk->dev;
	u32 error_code = le32_to_cpu(resp_payload->error_code);
	const char *name = pf_state_clk->name;

	switch (error_code) {
	case NO_ERROR:
		dev_dbg(dev, "%s: success!\n", name);
		pf_state_clk->err = 0;
		break;
	case ERR_ALREADY_STARTED:
		dev_err(dev, "%s: An operation is already in progress on LPCM. Try again later\n",
			name);
		pf_state_clk->err = -EAGAIN;
		break;
	case ERR_NOT_SUPPORTED:
		dev_err(dev, "%s: The received request is invalid\n", name);
		pf_state_clk->err = -EINVAL;
		break;
	case ERR_NOT_ALLOWED:
		dev_err(dev, "%s: Attempt to modify LPCM state is not allowed\n", name);
		pf_state_clk->err = -ESERVERFAULT;
		break;
	case ERR_INVALID_ARGS:
		dev_err(dev, "%s: Invalid op lvl provided for LPCM\n", name);
		pf_state_clk->err = -EINVAL;
		break;
	case ERR_TIMED_OUT:
		dev_err(dev, "%s: Operation timed out.\n", name);
		pf_state_clk->err = -EAGAIN;
		break;
	case ERR_GENERIC:
		dev_err(dev, "%s: Internal error occurred\n", name);
		pf_state_clk->err = -ESERVERFAULT;
		break;
	default:
		dev_err(dev, "%s: Unknown error response.\n", name);
		pf_state_clk->err = -ESERVERFAULT;
		break;
	}
}

static int send_mba_mail_set_rate(struct pf_state_clk *pf_state_clk,
				  u32 pf_state)
{
	int ret;
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_set_rate_resp_payload *resp_data;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MAILBOX_RECEIVE_TIMEOUT_MS;
	cpm_req.dst_id = pf_state_clk->pf_state_cpm->remote_ch;
	req_msg.payload[0] =
		FIELD_PREP(REQ_ID_BITFIELD, LPCM_SET_PF_STATE_REQ_ID) |
		FIELD_PREP(LPCM_ID_BITFIELD, pf_state_clk->lpcm_id) |
		FIELD_PREP(PF_STATE_BITFIELD, pf_state);

	ret = cpm_send_message(pf_state_clk->pf_state_cpm->client, &cpm_req);
	if (ret) {
		dev_err(pf_state_clk->dev, "send message failed ret (%d)\n",
			ret);
		return ret;
	}

	resp_data = (struct cpm_set_rate_resp_payload *)&resp_msg.payload[0];
	set_rate_mba_resp_process(pf_state_clk, resp_data);

	return 0;
}

static unsigned long
get_rate_mba_resp_process(struct pf_state_clk *pf_state_clk,
			  struct cpm_get_rate_resp_payload *resp_payload)
{
	struct device *dev = pf_state_clk->dev;
	int error_code = resp_payload->error_code;
	int pf_state = resp_payload->pf_state;
	const char *name = pf_state_clk->name;
	unsigned long long freq;

	dev_dbg(dev, "%s: got resp from %u, data %d %d\n", name,
		pf_state_clk->pf_state_cpm->remote_ch, error_code, pf_state);
	if (error_code != NO_ERROR) {
		dev_err(dev, "%s: Failed to get rate of clock. error_code = %d\n",
			name, error_code);
		return 0;
	}

	freq = dvfs_helper_lvl_to_freq_floor(pf_state_clk->dvfs_helper_info, pf_state);

	if (!freq)
		dev_err(dev, "Error while getting freq from pf state");

	return freq;
}

static unsigned long send_mba_mail_get_rate(struct pf_state_clk *pf_state_clk)
{
	int ret;
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_get_rate_resp_payload *resp_data;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MAILBOX_RECEIVE_TIMEOUT_MS;
	cpm_req.dst_id = pf_state_clk->pf_state_cpm->remote_ch;

	req_msg.payload[0] =
		FIELD_PREP(REQ_ID_BITFIELD, LPCM_GET_PF_STATE_REQ_ID) |
		FIELD_PREP(LPCM_ID_BITFIELD, pf_state_clk->lpcm_id);
	ret = cpm_send_message(pf_state_clk->pf_state_cpm->client, &cpm_req);
	if (ret < 0) {
		dev_err(pf_state_clk->dev, "send message failed ret (%d)",
			ret);
		return 0;
	}

	resp_data = (struct cpm_get_rate_resp_payload *)&resp_msg.payload[0];
	return get_rate_mba_resp_process(pf_state_clk, resp_data);
}

static int set_rate(struct clk_hw *hw, unsigned long rate,
		    unsigned long parent_rate)
{
	int pf_state;
	struct pf_state_clk *pf_state_clk;

	pf_state_clk = to_pf_state_clk(hw);
	pf_state = dvfs_helper_freq_to_lvl_exact(pf_state_clk->dvfs_helper_info, rate);

	if (pf_state < 0)
		return pf_state;

	return send_mba_mail_set_rate(pf_state_clk, pf_state);
}

static long round_rate(struct clk_hw *hw, unsigned long rate,
		       unsigned long *parent_rate)
{
	struct pf_state_clk *pf_state_clk = to_pf_state_clk(hw);
	long long freq;

	freq = dvfs_helper_round_rate(pf_state_clk->dvfs_helper_info, rate);

	if (freq <= 0) {
		dev_err(pf_state_clk->dev, "Error while rounding rate: %lu", rate);
		return freq;
	}

	return (long)freq;
}

static unsigned long recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct pf_state_clk *pf_state_clk;

	pf_state_clk = to_pf_state_clk(hw);
	return send_mba_mail_get_rate(pf_state_clk);
}

static const struct clk_ops pf_state_cpm_clk_ops = {
	.set_rate = set_rate,
	.round_rate = round_rate,
	.recalc_rate = recalc_rate,
};

static int pf_state_init_mbox(struct device *dev, struct pf_state_cpm *mbox)
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

static void pf_state_mbox_free(struct pf_state_cpm *mbox)
{
	cpm_iface_free_client(mbox->client);
}

static struct clk_hw *register_lpcm_clk_hw(struct device *dev,
					   struct pf_state_cpm *pf_state_cpm,
					   struct device_node *node)
{
	int err;
	struct pf_state_clk *pf_state_clk;
	struct clk_init_data init = { 0 };
	const char *name = node->name;

	pf_state_clk = devm_kzalloc(dev, sizeof(*pf_state_clk), GFP_KERNEL);

	pf_state_clk->name = name;
	err = of_property_read_u32(node, "lpcm-id", &pf_state_clk->lpcm_id);
	if (err < 0) {
		dev_err(dev, "%s: failed to read lpcm-id\n", name);
		return ERR_PTR(err);
	}

	pf_state_clk->pf_state_cpm = pf_state_cpm;
	err = dvfs_helper_get_domain_info(dev, node, &pf_state_clk->dvfs_helper_info);

	if (err) {
		dev_err(dev, "Failed to get DVFS helper info");
		return ERR_PTR(err);
	}

	init.name = node->name;
	init.ops = &pf_state_cpm_clk_ops;
	/*
	 * pf_state clocks belongs to power domain and can change their frequency
	 * back to default value if the power domain is switched off/on.
	 * Add CLK_GET_RATE_NOCACHE flag to ensure we do not rely on invalid cached
	 * value when setting or getting frequency of those clocks.
	 */
	init.flags = CLK_GET_RATE_NOCACHE;
	pf_state_clk->hw.init = &init;
	pf_state_clk->dev = dev;
	init_completion(&pf_state_clk->set_rate_complete);

	err = devm_clk_hw_register(dev, &pf_state_clk->hw);
	if (err) {
		dev_err(dev, "%s: fail to register clk hw: %d\n", name, err);
		return ERR_PTR(err);
	}

	err = of_clk_add_hw_provider(node, of_clk_hw_simple_get,
				     &pf_state_clk->hw);

	if (err) {
		dev_err(dev, "%s: fail to expose clk hw: %d\n", name, err);
		return ERR_PTR(err);
	}
	return &pf_state_clk->hw;
}

static void pf_state_cpm_remove(struct platform_device *pdev)
{
	struct pf_state_cpm *pf_state_cpm = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct device_node *child_node, *np = dev->of_node;

	pf_state_mbox_free(pf_state_cpm);
	for_each_available_child_of_node(np, child_node) {
		of_clk_del_provider(child_node);
	}
}

static int pf_state_cpm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child_node;
	struct clk_hw *clk_hw;
	struct pf_state_cpm *pf_state_cpm;
	int ret;

	pf_state_cpm = devm_kzalloc(dev, sizeof(*pf_state_cpm), GFP_KERNEL);
	if (!pf_state_cpm)
		return -ENOMEM;

	pf_state_cpm->dev = dev;

	ret = of_property_read_u32(np, "mba-dest-channel",
				   &pf_state_cpm->remote_ch);
	if (ret < 0) {
		dev_err(dev, "failed to read mba-dest-channel\n");
		return ret;
	}
	platform_set_drvdata(pdev, pf_state_cpm);

	ret = pf_state_init_mbox(dev, pf_state_cpm);
	if (ret)
		return ret;

	for_each_available_child_of_node(np, child_node) {
		clk_hw = register_lpcm_clk_hw(dev, pf_state_cpm, child_node);
		if (IS_ERR(clk_hw)) {
			pf_state_cpm_remove(pdev);
			return PTR_ERR(clk_hw);
		}
	}

	return 0;
};

static const struct of_device_id pf_state_cpm_of_match_table[] = {
	{ .compatible = "google,pf-state-cpm" },
	{},
};

static struct platform_driver pf_state_cpm_driver = {
	.probe = pf_state_cpm_probe,
	.remove = pf_state_cpm_remove,
	.driver = {
		.name = "google-pf-state-cpm",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(pf_state_cpm_of_match_table),
	},
};

module_platform_driver(pf_state_cpm_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("google pf state cpm");
MODULE_LICENSE("GPL");
