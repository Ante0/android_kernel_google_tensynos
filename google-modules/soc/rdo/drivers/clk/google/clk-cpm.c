// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver to control Google SoC LPCM clocks via CPM.
 * This covers clocks that are not supported by PFSMs and PSMs.
 * Copyright (C) 2023 Google LLC.
 */

#include <linux/arm-smccc.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/sort.h>

#include <dt-bindings/lpcm/clock.h>

#include <aoss-ssr-notifier/aoss_ssr_notifier.h>
#include <clk/clk-cpm.h>
#include <soc/google/goog_mba_cpm_iface.h>

#define MAILBOX_TIMEOUT_EMULATION_MULTIPLIER 10
#define MAILBOX_SEND_TIMEOUT_MS 1000
#define LPCM_REMOTE_CHANNEL 0x8
#define LPCM_CMD_SET_LCM_CLK 0x2
#define OP_ID_BITFIELD GENMASK(31, 24)
#define CLOCK_ID_BITFIELD GENMASK(23, 16)
#define LPCM_ID_BITFIELD GENMASK(15, 8)
#define REQ_ID_BITFIELD GENMASK(7, 0)
#define PREPARE_OP_ID 0
#define UNPREPARE_OP_ID 1
#define IS_ENABLE_OP_ID 2
#define RECALC_RATE_OP_ID 3
#define ROUND_RATE_OP_ID 4
#define SET_RATE_OP_ID 5
#define NORMAL_GATE 1
#define AUTO_GATE 2
#define CLK_GATE 1
#define AUTO_CLK_GATE 2
#define QCH_MODE 4
#define NO_ERROR 0
#define ERR_LPCM_NOT_SUPPORTED (-24)
#define ERR_LPCM_INVALID_ARGS (-8)
#define ERR_LPCM_TIMED_OUT (-13)
#define ERR_LPCM_UNAVAILABLE (-3)
#define ERR_LPCM_GENERIC (-1)
#define MAX_RATE_NUM 32
#define GOOG_CPM_CLK_AUTOSUSPEND_DELAY_MS 1

#define SIP_SVC_CLK_CONTROL 0x82000702

#define to_goog_cpm_clk(_hw) container_of(_hw, struct goog_cpm_clk, hw)

struct goog_cpm_clk_mbox {
	struct cpm_iface_client *client;
};

struct goog_cpm_clk {
	struct clk_hw hw;
	struct goog_cpm_clk_dev_data *dev_data;
	const char *name;

	unsigned int lpcm_id;
	unsigned int clock_id;
	int clk_gate_type;
	unsigned long rates[MAX_RATE_NUM];
	int nr_rates;
	bool use_smc;

	/* TODO(b/441581865) */
	bool restore_rate_on_aoss_ssr;
	int rate_restored_at;
};

struct goog_cpm_clk_dev_data {
	struct device *dev;
	struct goog_cpm_clk_mbox cpm_mbox;
	int num_pds;
	struct device **pd_devs;

	/* TODO(b/441581865) */
	struct notifier_block aoss_ssr_nb;
	atomic_t aoss_ssr_epoch;
};


static void detach_power_domains(struct goog_cpm_clk_dev_data *dev_data,
				 int end_idx)
{
	int i;

	for (i = end_idx - 1; i >= 0; i--) {
		if (dev_data->pd_devs[i] &&
		    !IS_ERR(dev_data->pd_devs[i]))
			dev_pm_domain_detach(dev_data->pd_devs[i], true);
	}
}

static int power_on_domains(struct goog_cpm_clk_dev_data *dev_data)
{
	struct device *dev = dev_data->dev;
	int i, ret = 0;

	dev_data->num_pds =
		of_count_phandle_with_args(dev->of_node, "power-domains",
					   "#power-domain-cells");
	if (dev_data->num_pds == -EINVAL) {
		dev_data->num_pds = 0;
		return 0;
	}

	if (dev_data->num_pds < 0) {
		dev_err(dev, "failed to read power-domains property\n");
		return -EINVAL;
	}
	dev_data->pd_devs =
		devm_kmalloc_array(dev, dev_data->num_pds,
				   sizeof(*dev_data->pd_devs), GFP_KERNEL);
	if (!dev_data->pd_devs)
		return -ENOMEM;

	for (i = 0; i < dev_data->num_pds; i++) {
		dev_data->pd_devs[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR(dev_data->pd_devs[i])) {
			dev_err(dev,
				"failed to attach power domain at index %d\n",
				i);
			ret = PTR_ERR(dev_data->pd_devs[i]);
			goto clean_up;
		}
		pm_runtime_use_autosuspend(dev_data->pd_devs[i]);
		pm_runtime_set_autosuspend_delay(dev_data->pd_devs[i],
						 GOOG_CPM_CLK_AUTOSUSPEND_DELAY_MS);
		ret = pm_runtime_get_sync(dev_data->pd_devs[i]);
		if (ret < 0) {
			/*
			 * No error returned because it is possible to register clocks
			 * even when power is OFF: CPM FW handles the power error and
			 * returns an initial clock rate of 0.
			 */
			dev_warn(dev,
				 "failed to pm_runtime_get_sync %s, ret = %d",
				 dev_name(dev_data->pd_devs[i]), ret);
		}
	}

	return ret;

clean_up:
	detach_power_domains(dev_data, i);
	return ret;
}

static bool goog_cpm_clk_can_adjust_clk_rate(struct goog_cpm_clk *cpm_clk)
{
	return cpm_clk->nr_rates > 0;
}

static int goog_cpm_clk_init_cpm_mailbox(struct device *dev,
					 struct goog_cpm_clk_mbox *cpm_mbox)
{
	int ret = 0;

	cpm_mbox->client = cpm_iface_request_client(dev, 0, NULL, NULL);
	if (IS_ERR(cpm_mbox->client)) {
		ret = PTR_ERR(cpm_mbox->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request cpm client ret %d\n", ret);
	}
	return ret;
}

static int goog_cpm_clk_mba_resp_hdl(struct goog_cpm_clk *cpm_clk, int result)
{
	struct device *dev = cpm_clk->dev_data->dev;
	const char *name = cpm_clk->name;

	if (result >= 0)
		return result; /* .recalc_rate returns clock's frequency instead of err code */

	switch (result) {
	case ERR_LPCM_NOT_SUPPORTED:
		dev_err(dev, "%s: the received request_id is invalid\n", name);
		return -EPROTO;
	case ERR_LPCM_INVALID_ARGS:
		dev_err(dev, "%s: invalid lpcm_id or clock_id\n", name);
		return -EINVAL;
	case ERR_LPCM_TIMED_OUT:
		dev_err(dev, "%s: operation timed out\n", name);
		return -ETIMEDOUT;
	case ERR_LPCM_GENERIC:
		dev_err(dev, "%s: internal error occurred\n", name);
		return -ESERVERFAULT;
	case ERR_LPCM_UNAVAILABLE:
		dev_err(dev, "%s: CPM unavailable\n", name);
		return -ENODATA;
	default:
		dev_err(dev, "%s: unknown error response from CPM (%d)\n", name, result);
		return -EPROTO;
	}
}

static int goog_cpm_clk_send_mba_mail(struct goog_cpm_clk *cpm_clk, u8 op_id,
				      unsigned long *clk_param)
{
	int ret;
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct device *dev = cpm_clk->dev_data->dev;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MAILBOX_SEND_TIMEOUT_MS;
	if (of_property_present(dev->of_node, "in_emulation"))
		cpm_req.tout_ms *= MAILBOX_TIMEOUT_EMULATION_MULTIPLIER;
	cpm_req.dst_id = LPCM_REMOTE_CHANNEL;

	req_msg.payload[0] = FIELD_PREP(REQ_ID_BITFIELD, LPCM_CMD_SET_LCM_CLK) |
			     FIELD_PREP(LPCM_ID_BITFIELD, cpm_clk->lpcm_id) |
			     FIELD_PREP(CLOCK_ID_BITFIELD, cpm_clk->clock_id) |
			     FIELD_PREP(OP_ID_BITFIELD, op_id);
	if (op_id == SET_RATE_OP_ID || op_id == PREPARE_OP_ID ||
	    op_id == UNPREPARE_OP_ID) {
		if (!clk_param) {
			dev_err(dev,
				"clk_param is NULL for op_id %d\n", op_id);
			return -EINVAL;
		}
		req_msg.payload[1] = *clk_param;
	}
	dev_dbg(dev,
		"lpcm_id %d, clock_id %d: send mba mail: [%d, %d, %d]\n",
		cpm_clk->lpcm_id, cpm_clk->clock_id, req_msg.payload[0],
		req_msg.payload[1], req_msg.payload[2]);
	ret = cpm_send_message(cpm_clk->dev_data->cpm_mbox.client, &cpm_req);
	if (ret < 0) {
		dev_err(dev, "lpcm_id %d, clock_id %d: failed to send request to CPM, ret=%d\n",
			cpm_clk->lpcm_id, cpm_clk->clock_id, ret);
		return ret;
	}

	dev_dbg(dev, "got resp from %u, data %d.\n", cpm_req.dst_id, ret);
	return goog_cpm_clk_mba_resp_hdl(cpm_clk, cpm_req.resp_msg->payload[0]);
}

static int goog_cpm_clk_send_mbox_msg_set_gate(struct goog_cpm_clk *cpm_clk,
					       bool enable)
{
	int op_id = enable ? PREPARE_OP_ID : UNPREPARE_OP_ID;
	unsigned long param = CLK_GATE;

	return goog_cpm_clk_send_mba_mail(cpm_clk, op_id, &param);
}

static int goog_cpm_clk_send_mbox_msg_set_rate(struct goog_cpm_clk *cpm_clk,
					       unsigned long rate)
{
	return goog_cpm_clk_send_mba_mail(cpm_clk, SET_RATE_OP_ID, &rate);
}

static int goog_cpm_clk_send_mbox_msg_recalc_rate(struct goog_cpm_clk *cpm_clk)
{
	return goog_cpm_clk_send_mba_mail(cpm_clk, RECALC_RATE_OP_ID, NULL);
}

static int goog_cpm_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct goog_cpm_clk *cpm_clk = to_goog_cpm_clk(hw);

	dev_dbg(cpm_clk->dev_data->dev, "%s: lpcm_id %d, clock_id %d: set rate %lu\n",
		cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id, rate);
	return goog_cpm_clk_send_mbox_msg_set_rate(cpm_clk, rate);
}

static unsigned long goog_cpm_clk_recacl_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct goog_cpm_clk *cpm_clk = to_goog_cpm_clk(hw);

	dev_dbg(cpm_clk->dev_data->dev, "%s: lpcm_id %d, clock_id %d: get rate\n",
		cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id);
	return goog_cpm_clk_send_mbox_msg_recalc_rate(cpm_clk);
}

static long goog_cpm_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *parent_rate)
{
	struct goog_cpm_clk *cpm_clk = to_goog_cpm_clk(hw);

	dev_dbg(cpm_clk->dev_data->dev, "%s: lpcm_id %d, clock_id %d: round rate\n",
		cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id);
	for (int i = 0; i < cpm_clk->nr_rates; i++) {
		if (cpm_clk->rates[i] >= rate)
			return cpm_clk->rates[i];
	}
	if (cpm_clk->nr_rates > 0)
		return cpm_clk->rates[cpm_clk->nr_rates - 1];
	return -EINVAL;
}

static inline unsigned long lpcm_smc(struct clk_hw *hw, bool enable)
{
	struct arm_smccc_res res;
	struct goog_cpm_clk *cpm_clk = to_goog_cpm_clk(hw);

	arm_smccc_smc(SIP_SVC_CLK_CONTROL, cpm_clk->lpcm_id, cpm_clk->clock_id,
			enable, 0, 0, 0, 0, &res);

	WARN_ON(res.a0);
	if ((unsigned long)res.a1) {
		dev_err(cpm_clk->dev_data->dev,
			"Failed LPCM SMC: %d clk_id: %d, enable: %d res.a1: %lu\n",
			cpm_clk->lpcm_id, cpm_clk->clock_id, enable, res.a1);
		return -EINVAL;
	} else
		return 0;
}

static int goog_cpm_clk_restore_after_aoss_ssr(struct goog_cpm_clk *cpm_clk)
{
	int ret;
	int aoss_ssr_epoch;
	unsigned long rate;

	if (!cpm_clk->restore_rate_on_aoss_ssr)
		return 0;

	aoss_ssr_epoch = atomic_read_acquire(&cpm_clk->dev_data->aoss_ssr_epoch);

	dev_dbg(cpm_clk->dev_data->dev,
		"%s: lpcm_id %d, clock_id %d: rate restored at %d, aoss ssr epoch: %d\n",
		cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id,
		cpm_clk->rate_restored_at, aoss_ssr_epoch);

	if (cpm_clk->rate_restored_at >= aoss_ssr_epoch)
		return 0;

	rate = clk_hw_get_rate(&cpm_clk->hw);
	ret = goog_cpm_clk_send_mbox_msg_set_rate(cpm_clk, rate);
	if (!ret) {
		cpm_clk->rate_restored_at = aoss_ssr_epoch;
		dev_dbg(cpm_clk->dev_data->dev,
			"%s: lpcm_id %d, clock_id %d: rate %lu restored after aoss ssr\n",
			cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id, rate);
	} else {
		dev_err(cpm_clk->dev_data->dev,
			"%s: lpcm_id %d, clock_id %d: failed to restore rate %lu, ret=%d\n",
			cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id, rate, ret);
	}

	return ret;
}

static int goog_cpm_clk_prepare(struct clk_hw *hw)
{
	int ret;
	struct goog_cpm_clk *cpm_clk = to_goog_cpm_clk(hw);

	dev_dbg(cpm_clk->dev_data->dev, "%s: lpcm_id %d, clock_id %d: enable\n",
		cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id);

	if (cpm_clk->use_smc)
		ret = lpcm_smc(hw, true);
	else
		ret = goog_cpm_clk_send_mbox_msg_set_gate(cpm_clk, true);

	if (ret)
		return ret;

	ret = goog_cpm_clk_restore_after_aoss_ssr(cpm_clk);

	return ret;
}

static void goog_cpm_clk_unprepare(struct clk_hw *hw)
{
	struct goog_cpm_clk *cpm_clk = to_goog_cpm_clk(hw);

	dev_dbg(cpm_clk->dev_data->dev, "%s: lpcm_id %d, clock_id %d: disable\n",
		cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id);
	if (cpm_clk->use_smc)
		lpcm_smc(hw, false);
	else
		goog_cpm_clk_send_mbox_msg_set_gate(cpm_clk, false);
}

static int goog_cpm_clk_enable(struct clk_hw *hw)
{
	struct goog_cpm_clk *cpm_clk = to_goog_cpm_clk(hw);

	dev_dbg(cpm_clk->dev_data->dev, "%s: lpcm_id %d, clock_id %d: enable\n",
		cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id);
	if (cpm_clk->use_smc)
		return lpcm_smc(hw, true);

	dev_warn(cpm_clk->dev_data->dev, "smc call not supported for %s\n", cpm_clk->name);
	return -EINVAL;
}

static void goog_cpm_clk_disable(struct clk_hw *hw)
{
	struct goog_cpm_clk *cpm_clk = to_goog_cpm_clk(hw);

	dev_dbg(cpm_clk->dev_data->dev, "%s: lpcm_id %d, clock_id %d: disable\n",
		cpm_clk->name, cpm_clk->lpcm_id, cpm_clk->clock_id);
	if (cpm_clk->use_smc)
		lpcm_smc(hw, false);
	else
		dev_warn(cpm_clk->dev_data->dev, "smc call not supported for %s\n", cpm_clk->name);
}

static const struct clk_ops goog_cpm_mutable_rate_clk_ops = {
	.set_rate = goog_cpm_clk_set_rate,
	.recalc_rate = goog_cpm_clk_recacl_rate,
	.round_rate = goog_cpm_clk_round_rate,
	.prepare = goog_cpm_clk_prepare,
	.unprepare = goog_cpm_clk_unprepare,
};

static const struct clk_ops goog_cpm_gate_clk_ops = {
	.prepare = goog_cpm_clk_prepare,
	.unprepare = goog_cpm_clk_unprepare,
};

static const struct clk_ops goog_cpm_gate_smc_clk_ops = {
	.enable = goog_cpm_clk_enable,
	.disable = goog_cpm_clk_disable,
};

static int goog_cpm_clk_cmp_rate(const void *A, const void *B)
{
	const unsigned long *a = A, *b = B;

	if (*a < *b)
		return -1;
	else if (*a == *b)
		return 0;
	else
		return 1;
}

static int goog_cpm_clk_register_clk(struct device *dev,
				     struct device_node *node,
				     struct goog_cpm_clk_dev_data *dev_data)
{
	struct goog_cpm_clk *cpm_clk;
	struct clk_init_data init = {};
	const char *name = node->name;
	int err;

	cpm_clk = devm_kzalloc(dev, sizeof(*cpm_clk), GFP_KERNEL);
	cpm_clk->name = name;
	if (!cpm_clk)
		return -ENOMEM;
	err = of_property_read_u32(node, "lpcm-id", &cpm_clk->lpcm_id);
	if (err < 0) {
		dev_err(dev, "%s: failed to read lpcm-id\n", name);
		return err;
	}

	err = of_property_read_u32(node, "clock-id", &cpm_clk->clock_id);
	if (err < 0) {
		dev_err(dev, "%s: failed to read clock-id\n", name);
		return err;
	}

	cpm_clk->nr_rates = of_property_count_u64_elems(node, "rates");
	if (cpm_clk->nr_rates < 0)
		cpm_clk->nr_rates = 0;
	if (cpm_clk->nr_rates > MAX_RATE_NUM) {
		dev_err(dev,
			"%s: number of rates exceeded max supported number: %d > %d\n",
			name, cpm_clk->nr_rates, MAX_RATE_NUM);
		return -EINVAL;
	}

	if (cpm_clk->nr_rates > 0) {
		err = of_property_read_u64_array(node, "rates",
						 (u64 *)&cpm_clk->rates,
						 cpm_clk->nr_rates);
		if (err) {
			dev_err(dev, "%s: failed to read rates prop\n", name);
			return err;
		}

		sort(cpm_clk->rates, cpm_clk->nr_rates, sizeof(*cpm_clk->rates),
		     goog_cpm_clk_cmp_rate, NULL);
	}

	cpm_clk->use_smc = of_property_read_bool(node, "use-smc");
	cpm_clk->restore_rate_on_aoss_ssr =
		of_property_read_bool(node, "restore-rate-on-aoss-ssr");

	init.name = node->name;
	if (cpm_clk->restore_rate_on_aoss_ssr)
		dev_dbg(dev, "%s: get rate cache enabled\n", name);
	else
		init.flags = CLK_GET_RATE_NOCACHE;

	if (goog_cpm_clk_can_adjust_clk_rate(cpm_clk))
		init.ops = &goog_cpm_mutable_rate_clk_ops;
	else if (cpm_clk->use_smc)
		init.ops = &goog_cpm_gate_smc_clk_ops;
	else
		init.ops = &goog_cpm_gate_clk_ops;
	cpm_clk->hw.init = &init;
	cpm_clk->dev_data = dev_data;
	err = devm_clk_hw_register(dev, &cpm_clk->hw);
	if (err) {
		dev_err(dev, "%s: failed to register clk hw: %d", name, err);
		return err;
	}

	err = of_clk_add_hw_provider(node, of_clk_hw_simple_get, &cpm_clk->hw);

	if (err)
		dev_err(dev, "%s: failed to expose clk hw: %d", name, err);
	return err;
}

static inline void goog_cpm_clk_mbox_free(struct goog_cpm_clk_mbox *mbox)
{
	cpm_iface_free_client(mbox->client);
}

static void goog_cpm_clk_controller_remove(struct platform_device *pdev)
{
	struct goog_cpm_clk_dev_data *dev_data = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct device_node *child_node, *node = dev->of_node;

	aoss_ssr_remove_notifier(&dev_data->aoss_ssr_nb);

	goog_cpm_clk_mbox_free(&dev_data->cpm_mbox);
	for_each_available_child_of_node(node, child_node)
		of_clk_del_provider(child_node);
	detach_power_domains(dev_data, dev_data->num_pds);
}

int goog_cpm_clk_toggle_auto_gate(struct clk *clk, bool enable)
{
	struct clk_hw *clk_hw = __clk_get_hw(clk);
	struct goog_cpm_clk *goog_cpm_clk = to_goog_cpm_clk(clk_hw);
	int op_id = enable ? PREPARE_OP_ID : UNPREPARE_OP_ID;
	unsigned long param = AUTO_CLK_GATE;

	return goog_cpm_clk_send_mba_mail(goog_cpm_clk, op_id, &param);
}
EXPORT_SYMBOL_GPL(goog_cpm_clk_toggle_auto_gate);

int goog_cpm_clk_toggle_qch_mode(struct clk *clk, bool enable)
{
	struct clk_hw *clk_hw = __clk_get_hw(clk);
	struct goog_cpm_clk *goog_cpm_clk = to_goog_cpm_clk(clk_hw);
	int op_id = enable ? PREPARE_OP_ID : UNPREPARE_OP_ID;
	unsigned long param = QCH_MODE;

	return goog_cpm_clk_send_mba_mail(goog_cpm_clk, op_id, &param);
}
EXPORT_SYMBOL_GPL(goog_cpm_clk_toggle_qch_mode);

static int aoss_ssr_notifier(struct notifier_block *notifier, unsigned long event, void *v)
{
	struct goog_cpm_clk_dev_data *dev_data =
		container_of(notifier, struct goog_cpm_clk_dev_data, aoss_ssr_nb);
	int epoch;

	switch (event) {
	case AOSS_SSR_PG_DOWN:
		dev_dbg(dev_data->dev, "aoss_ssr down, epoch=%d\n",
			 atomic_read(&dev_data->aoss_ssr_epoch));
		break;

	case AOSS_SSR_PG_UP:
		epoch = atomic_inc_return(&dev_data->aoss_ssr_epoch);
		dev_dbg(dev_data->dev, "aoss_ssr up, new epoch=%d\n",
			 epoch);
		break;
	}

	return NOTIFY_OK;
}

static int goog_cpm_clk_controller_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node, *child_node;
	struct goog_cpm_clk_dev_data *dev_data;
	int ret = 0, i;

	dev_data = devm_kzalloc(dev, sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;
	dev_data->dev = dev;
	ret = goog_cpm_clk_init_cpm_mailbox(dev, &dev_data->cpm_mbox);
	if (ret < 0)
		return ret;

	/*
	 * Clock framework calls get_rate to calculate init frequency during clock
	 * registration. Thus, powering on domains before registering clocks to
	 * prevent redundant error logs. Use pm runtime autosuspend to avoid
	 * flipping power domain on and off too frequently.
	 */
	ret = power_on_domains(dev_data);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, dev_data);
	for_each_available_child_of_node(node, child_node) {
		ret = goog_cpm_clk_register_clk(dev, child_node, dev_data);
		if (ret < 0) {
			goog_cpm_clk_controller_remove(pdev);
			return ret;
		}
	}

	for (i = 0; i < dev_data->num_pds; i++) {
		pm_runtime_mark_last_busy(dev_data->pd_devs[i]);
		ret = pm_runtime_put_autosuspend(dev_data->pd_devs[i]);
		if (ret < 0) {
			dev_err(dev, "failed to pm_runtime_put_autosuspend %s, ret = %d",
				dev_name(dev_data->pd_devs[i]), ret);
			return ret;
		}
	}

	dev_data->aoss_ssr_nb.notifier_call = aoss_ssr_notifier;
	aoss_ssr_add_notifier(&dev_data->aoss_ssr_nb);

	return ret;
};

static const struct of_device_id goog_cpm_clk_controller_of_match_table[] = {
	{ .compatible = "google,cpm-clk" },
	{},
};
MODULE_DEVICE_TABLE(of, goog_cpm_clk_controller_of_match_table);

static struct platform_driver goog_cpm_clk_driver = {
	.probe = goog_cpm_clk_controller_probe,
	.remove = goog_cpm_clk_controller_remove,
	.driver = {
		.name = "goog_cpm_clk",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goog_cpm_clk_controller_of_match_table),
	},
};

module_platform_driver(goog_cpm_clk_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google CPM Clock Driver");
MODULE_LICENSE("GPL");
