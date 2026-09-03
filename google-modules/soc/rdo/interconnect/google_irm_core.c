// SPDX-License-Identifier: GPL-2.0-only

#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#include <dt-bindings/interconnect/google,icc.h>
#include <interconnect/google_irm_api.h>
#include <interconnect/interconnect-google.h>
#include <mailbox/protocols/mba/apc/common/service_ids.h>

#include "google_icc_vote.h"
#include "google_irm.h"
#include "google_irm_debugfs.h"
#include "google_irm_idx_internal.h"
#include "google_irm_of.h"
#include "google_irm_reg_internal.h"

#include "soc/google/google_gtc.h"

#define CREATE_TRACE_POINTS
#include "google_irm_trace.h"

#define CPM_TASK_TIMEOUT_MS		1000

#define POLL_SLEEP_TIME_IN_US		(5)
#define POLL_TIMEOUT_TIME_IN_US		(10000)

static const struct regmap_config irm_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.cache_type = REGCACHE_NONE,
};

static u32 num_vc;
static struct irm_dev *irm_device;

static void irm_mbox_rx_process(struct irm_dev *irm_dev, u32 payload[])
{
	struct irm_client *client;
	u32 idx, client_id;

	dev_dbg(irm_dev->dev, "irm mbox rx callback msg %08x %08x %08x\n",
		payload[0], payload[1], payload[2]);

	trace_google_irm_event(TPS("irm_mbox_rx_callback"), goog_gtc_get_counter());

	for (idx = 0; idx < 32; idx++) {
		if (payload[1] & BIT(idx)) {
			client_id = irm_dev->client_map[idx];
			client = &irm_dev->client[client_id];
			if (client->sync_type & GOOGLE_ICC_UPDATE_SYNC)
				complete(&client->cpm_done);
		}
	}
}

/*
 * This callback is triggered when kernel receives mailbox messages from CPM MIPM.
 */
static void irm_mbox_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct irm_dev *irm_dev = priv_data;
	struct cpm_iface_payload *cpm_msg = msg;

	irm_mbox_rx_process(irm_dev, cpm_msg->payload);
}

static int irm_initialize_mailbox(struct device *dev, struct irm_mbox *mbox)
{
	int ret;
	struct irm_dev *irm_dev = container_of(mbox, struct irm_dev, mbox);

	mbox->client = cpm_iface_request_client(dev, APC_COMMON_SERVICE_ID_MIPM,
						irm_mbox_rx_callback, irm_dev);
	if (IS_ERR(mbox->client)) {
		ret = PTR_ERR(mbox->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(dev, "failed to request cpm mailbox client err %d\n", ret);
		return ret;
	}

	return 0;
}

static inline int poll_until_zero(struct irm_dev *irm_dev, u32 addr)
{
	u32 val = 0;
	int ret = 0;

	ret = regmap_read_poll_timeout(irm_dev->regmap, addr, val, val == 0,
				       POLL_SLEEP_TIME_IN_US, POLL_TIMEOUT_TIME_IN_US);

	return ret;
}

static inline int irm_dvfs_req_trig(struct irm_dev *irm_dev, struct irm_client *client)
{
	u32 addr;
	int ret = 0;

	/* write DVFS_TRIG_EN = 1 */
	addr = client->base + DVFS_REQ_TRIG;
	ret = regmap_write(irm_dev->regmap, addr, DVFS_TRIG_EN);
	if (ret) {
		dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
		return ret;
	}

	trace_google_irm_event(TPS("irm vote write DVFS_TRIG_EN = 1"), goog_gtc_get_counter());

	return 0;
}

static u32 __clamp_value(u64 input, u32 max_val)
{
	u32 result = 0;

	result = (input <= max_val) ? (u32)input : max_val;

	return result;
}

static int irm_vote_reg_write(struct irm_dev *irm_dev, u32 id,
			      enum icc_type_id icc_type,
			      const struct icc_vote *vote)
{
	const struct icc_vote_block *block_r = NULL, *block_w = NULL;
	struct irm_client *client = &irm_dev->client[id];
	u32 base, offset, addr, write_val;
	int ret;

	/* TODO: b/403584451 - delete this when things are settled */
#if !IS_ENABLED(CONFIG_SOC_LGA)
	if (WARN_ON_ONCE(icc_type == ICC_TYPE_GSLC)) {
		dev_err(irm_dev->dev,
			"No GSLC irm registers on this platform!");
		return -EINVAL;
	}
#endif

	base = client->base;
	offset = (icc_type == ICC_TYPE_GSLC) ? GSLC_OFFSET : GMC_OFFSET;

	addr = base + DVFS_REQ_TRIG;
	ret = poll_until_zero(irm_dev, addr);
	if (ret) {
		dev_err(irm_dev->dev,
			"Trig uncleared, skipping IRM write for client %d, icc_type %s\n", id,
			(icc_type == ICC_TYPE_GSLC) ? "GSLC" : "GMC");
		return ret;
	}

	/* write IRM settings */
	block_r = &vote->block[icc_type][ICC_READ];
	block_w = &vote->block[icc_type][ICC_WRITE];

	/* read avg_bw */
	addr = base + offset + DVFS_REQ_RD_BW_AVG_GMC;
	write_val = __clamp_value(block_r->avg_bw, BW_MASK);
	ret = regmap_write(irm_dev->regmap, addr, write_val);
	if (ret) {
		dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
		return ret;
	}

	/* write avg_bw */
	addr = base + offset + DVFS_REQ_WR_BW_AVG_GMC;
	write_val = __clamp_value(block_w->avg_bw, BW_MASK);
	ret = regmap_write(irm_dev->regmap, addr, write_val);
	if (ret) {
		dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
		return ret;
	}

	/* read rt_bw */
	addr = base + offset + DVFS_REQ_RD_BW_RT_GMC;
	write_val = __clamp_value(block_r->rt_bw, BW_MASK);
	ret = regmap_write(irm_dev->regmap, addr, write_val);
	if (ret) {
		dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
		return ret;
	}

	/* write rt_bw */
	addr = base + offset + DVFS_REQ_WR_BW_RT_GMC;
	write_val = __clamp_value(block_w->rt_bw, BW_MASK);
	ret = regmap_write(irm_dev->regmap, addr, write_val);
	if (ret) {
		dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
		return ret;
	}

	/* read peak_bw */
	addr = base + offset + DVFS_REQ_RD_BW_PEAK_GMC;
	write_val = __clamp_value(block_r->peak_bw, BW_MASK);
	ret = regmap_write(irm_dev->regmap, addr, write_val);
	if (ret) {
		dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
		return ret;
	}

	/* write peak_bw */
	addr = base + offset + DVFS_REQ_WR_BW_PEAK_GMC;
	write_val = __clamp_value(block_w->peak_bw, BW_MASK);
	ret = regmap_write(irm_dev->regmap, addr, write_val);
	if (ret) {
		dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
		return ret;
	}

	/* read latency */
	if (block_r->latency != U32_MAX) {
		addr = base + offset + DVFS_REQ_LATENCY_GMC;
		write_val = __clamp_value(block_r->latency, LATENCY_LTV_MAX_VAL);
		ret = regmap_write(irm_dev->regmap, addr, write_val);
		if (ret) {
			dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
			return ret;
		}
	}

	/* read LTV */
	if (icc_type == ICC_TYPE_GSLC) {
		if (block_r->ltv != U32_MAX) {
			addr = base + offset + DVFS_REQ_MIN_CLAMP_GMC;
			write_val = __clamp_value(block_r->ltv, LATENCY_LTV_MAX_VAL);
			ret = regmap_write(irm_dev->regmap, addr, write_val);
			if (ret) {
				dev_err(irm_dev->dev, "write [%u] failed, ret = %d\n", addr, ret);
				return ret;
			}
		}
	}

	return 0;
}

int irm_vote_commit(struct irm_dev *irm_dev, u8 id)
{
	int ret;
	struct irm_client *client = &irm_dev->client[id];

	if (client->irm_type & IRM_TYPE_GMC) {
		ret = irm_vote_reg_write(irm_dev, id, ICC_TYPE_GMC,
					 &client->vote_backup);
		if (ret)
			return ret;
	}
#if IS_ENABLED(CONFIG_SOC_LGA)
	if (client->irm_type & IRM_TYPE_GSLC) {
		ret = irm_vote_reg_write(irm_dev, id, ICC_TYPE_GSLC,
					 &client->vote_backup);
		if (ret)
			return ret;
	}
#endif

	return irm_dvfs_req_trig(irm_dev, client);
}

static void irm_vote_save_block(struct icc_vote_block *backup_block,
				const struct icc_vote_block *block)
{
	int i;

	backup_block->avg_bw = block->avg_bw;
	backup_block->peak_bw = block->peak_bw;
	backup_block->rt_bw = block->rt_bw;
	backup_block->latency = block->latency;
	backup_block->ltv = block->ltv;

	for (i = 0; i < num_vc; i++) {
		backup_block->avg_bw_vc[i] = block->avg_bw_vc[i];
		backup_block->peak_bw_vc[i] = block->peak_bw_vc[i];
	}
}

static void irm_vote_save_type(struct irm_client *client,
			       enum icc_type_id icc_type,
			       const struct icc_vote *vote)
{
	const struct icc_vote_block *block;
	struct icc_vote_block *backup_block;
	size_t rw;

	for (rw = ICC_READ; rw < NUM_ICC_RW; rw++) {
		backup_block = &client->vote_backup.block[icc_type][rw];
		block = &vote->block[icc_type][rw];
		irm_vote_save_block(backup_block, block);
	}
}

static void irm_vote_save(struct irm_dev *irm_dev, u32 id, u32 _irm_type,
			  const struct icc_vote *vote)
{
	struct irm_client *client = &irm_dev->client[id];

	/*
	 * Ideally, for consistency, irm_type should be configured in the Device Tree, mirroring
	 * the configuration of sync_type. Currently, irm_type is configured by parameter attr in
	 * _irm_vote(), which is staticly determined by DEFINE_GNODE().
	 */
	client->irm_type = _irm_type;

	if (_irm_type & IRM_TYPE_GMC)
		irm_vote_save_type(client, ICC_TYPE_GMC, vote);
	if (_irm_type & IRM_TYPE_GSLC)
		irm_vote_save_type(client, ICC_TYPE_GSLC, vote);
}

static int irm_vote_wait_for_cpm_ack(struct device *dev, struct irm_client *client, u32 id)
{
	unsigned long ret_wait;
	int ret = 0;

	ret_wait = wait_for_completion_timeout(&client->cpm_done,
					       msecs_to_jiffies(CPM_TASK_TIMEOUT_MS));

	if (ret_wait == 0) {
		dev_err(dev, "IRM id %u: wait CPM ACK timeout.\n", id);
		ret = -ETIMEDOUT;
	}

	return ret;
}

static int _irm_vote(struct irm_dev *irm_dev, u32 attr,
		     const struct icc_vote *vote)
{
	u32 id, _irm_type;
	struct irm_dbg_client *dbg_client;
	struct irm_client *client;
	int ret = 0;

	trace_google_irm_event(TPS("irm vote begin"), goog_gtc_get_counter());

	id = attr & IRM_IDX_MASK;
	_irm_type = attr & IRM_GMC_GSLC_MASK;

	if (id >= IRM_IDX_NUM) {
		dev_err(irm_dev->dev, "invalid IRM id %u\n", id);
		ret = -EINVAL;
		goto out;
	}

	if (!irm_dev || !vote) {
		ret = -EINVAL;
		goto out;
	}

	if (vote->vote_sync_type == 0)
		goto out;

	dbg_client = &irm_dev->dbg->client[id];
	client = &irm_dev->client[id];

	if (!(vote->vote_sync_type & client->sync_type)) {
		ret = -GOOGLE_ICC_ERR_INVALID_UPDATE_TYPE;
		goto out;
	}

	mutex_lock(&client->mutex);

	irm_vote_save(irm_dev, id, _irm_type, vote);

	if (!dbg_client->ctl) {
		ret = irm_vote_commit(irm_dev, id);
		if (ret) {
			dev_err(irm_dev->dev, "irm_vote_commit() failed: %d\n",
				ret);
			goto out_unlock;
		}

		if (vote->vote_sync_type & GOOGLE_ICC_UPDATE_SYNC)
			ret = irm_vote_wait_for_cpm_ack(irm_dev->dev, client, id);
	}

out_unlock:
	mutex_unlock(&client->mutex);

out:
	trace_google_irm_event_with_ret(TPS("irm vote end"), ret, goog_gtc_get_counter());
	return ret;
}

static struct irm_dev_ops __irm_dev_ops = {
	.irm_vote = _irm_vote,
};

u32 irm_register_read(struct device *dev, u32 client_idx, u32 reg_offset)
{
	struct irm_client *client;
	u32 addr = 0;
	u32 val = 0;
	int ret = 0;

	if (client_idx >= IRM_IDX_NUM) {
		dev_err(dev, "%s: invalid client_idx: %u\n", __func__, client_idx);
		return 0;
	}

	if (reg_offset >= SIZE_DVFS_REQ_REG || reg_offset % sizeof(u32)) {
		dev_err(dev, "%s: invalid reg_offset: %x\n", __func__, reg_offset);
		return 0;
	}

	client = &irm_device->client[client_idx];

	addr = client->base + reg_offset;

	mutex_lock(&client->mutex);

	ret = regmap_read(irm_device->regmap, addr, &val);
	if (ret) {
		dev_err(dev, "%s: read client %u, offset %x failed: %d\n", __func__,
			client_idx, reg_offset, ret);
		return 0;
	}

	mutex_unlock(&client->mutex);

	return val;
}
EXPORT_SYMBOL_GPL(irm_register_read);

int irm_register_write(struct device *dev, u32 client_idx, u32 reg_offset, u32 val)
{
	struct irm_client *client;
	u32 addr = 0;
	int ret = 0;

	if (client_idx >= IRM_IDX_NUM) {
		dev_err(dev, "%s: invalid client_idx: %u\n", __func__, client_idx);
		return -EINVAL;
	}

	if (reg_offset >= SIZE_DVFS_REQ_REG || reg_offset % sizeof(u32)) {
		dev_err(dev, "%s: invalid reg_offset: %x\n", __func__, reg_offset);
		return -EINVAL;
	}

	if (WARN_ON_ONCE(!irm_device))
		return -EINVAL;

	client = &irm_device->client[client_idx];

	addr = client->base + reg_offset;

	mutex_lock(&client->mutex);

	ret = regmap_write(irm_device->regmap, addr, val);
	if (ret)
		dev_err(dev, "%s: write client %u, offset %x failed: %d\n", __func__,
			client_idx, reg_offset, ret);

	mutex_unlock(&client->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(irm_register_write);


static int google_irm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct device_node *np_irm_common_prop;
	struct irm_dev *irm_dev;
	struct irm_client *client;
	int size;
	const char **client_name;
	int ret;
	int idx, index;
	u32 val;

	irm_dev = devm_kzalloc(dev, sizeof(*irm_dev), GFP_KERNEL);
	if (!irm_dev)
		return -ENOMEM;
	irm_dev->dev = dev;

	platform_set_drvdata(pdev, irm_dev);

	np = dev->of_node;

	np_irm_common_prop = of_parse_phandle(np, "google,irm-commom-prop", 0);
	if (!np_irm_common_prop) {
		dev_err(dev, "Read google,irm-commom-prop failed.\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np_irm_common_prop, "num_vc", &num_vc) < 0) {
		dev_err(dev, "Read num_vc failed.\n");
		return -EINVAL;
	}

	irm_dev->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(irm_dev->base_addr))
		return PTR_ERR(irm_dev->base_addr);

	irm_dev->regmap = devm_regmap_init_mmio(dev, irm_dev->base_addr,
						&irm_regmap_config);
	if (IS_ERR(irm_dev->regmap))
		return PTR_ERR(irm_dev->regmap);

	irm_dev->dbg = devm_kzalloc(dev, sizeof(*irm_dev->dbg), GFP_KERNEL);
	if (!irm_dev->dbg)
		return -ENOMEM;

	irm_dev->client_map = devm_kzalloc(dev, sizeof(*irm_dev->client_map) * 32, GFP_KERNEL);
	if (!irm_dev->client_map)
		return -ENOMEM;

	for (idx = 0; idx < 32; idx++)
		irm_dev->client_map[idx] = -1;

	/* client-prop: <offset, cpm_irm_client_id, type> */
	size = of_property_count_u32_elems(np, "client-prop");
	if (size < 0) {
		dev_err(dev, "Get property 'client-prop' failed, ret %d.\n", size);
		return -EINVAL;
	}

	if (size != (NF_IRM_CLIENT_PROP * IRM_IDX_NUM)) {
		dev_err(dev, "Size of client-prop (%d) != %d * %d\n",
			size, NF_IRM_CLIENT_PROP, IRM_IDX_NUM);
		return -EINVAL;
	}

	irm_dev->client = devm_kzalloc(dev,
				       sizeof(*irm_dev->client) * IRM_IDX_NUM,
				       GFP_KERNEL);
	if (!irm_dev->client)
		return -ENOMEM;

	for (idx = 0; idx < IRM_IDX_NUM; idx++) {
		client = &irm_dev->client[idx];

		index = NF_IRM_CLIENT_PROP * idx + IRM_CLIENT_PROP_REG_OFFSET;
		ret = of_property_read_u32_index(np, "client-prop",
						 index, &client->base);
		if (ret) {
			dev_err(dev, "Read %s property index %u failed, ret = %d\n",
				"client-prop", index, ret);
			return -EINVAL;
		}

		index = NF_IRM_CLIENT_PROP * idx + IRM_CLIENT_PROP_CPM_IRM_CLIENT_ID;
		ret = of_property_read_u32_index(np, "client-prop",
						 index, &val);
		if (ret) {
			dev_err(dev, "Read %s property index %u failed, ret = %d\n",
				"client-prop", index, ret);
			return -EINVAL;
		}

		irm_dev->client_map[val] = idx;

		index = NF_IRM_CLIENT_PROP * idx + IRM_CLIENT_PROP_TYPE;
		ret = of_property_read_u32_index(np, "client-prop",
						 index, &client->sync_type);
		if (ret) {
			dev_err(dev, "Read %s property index %u failed, ret = %d\n",
				"client-prop", index, ret);
			return -EINVAL;
		}

		init_completion(&client->cpm_done);

		mutex_init(&client->mutex);
	}

	ret = irm_initialize_mailbox(dev, &irm_dev->mbox);
	if (ret)
		return ret;

	size = of_property_count_strings(np, "client-name");
	if (size != IRM_IDX_NUM) {
		dev_err(dev, "Size of client-name (%d) != %d", size, IRM_IDX_NUM);
		return -EINVAL;
	}

	client_name = devm_kzalloc(dev,
				   sizeof(*client_name) * IRM_IDX_NUM,
				   GFP_KERNEL);

	ret = of_property_read_string_array(np, "client-name", client_name, IRM_IDX_NUM);
	if (ret < 0) {
		dev_err(dev, "Failed to read 'client-name' data, ret (%d).\n",
			ret);
		return -EINVAL;
	}

	ret = of_google_irm_init(irm_dev);
	if (ret < 0)
		return -EINVAL;

	irm_dev->ops = &__irm_dev_ops;

	irm_device = irm_dev;

	ret = irm_create_debugfs(irm_dev, IRM_IDX_NUM, client_name);

	return ret;
}

bool irm_probing_completed(void)
{
	return irm_device != NULL;
}
EXPORT_SYMBOL(irm_probing_completed);

static inline void irm_mbox_free(struct irm_mbox *mbox)
{
	cpm_iface_free_client(mbox->client);
}

static void google_irm_platform_remove(struct platform_device *pdev)
{
	struct irm_dev *irm_dev = platform_get_drvdata(pdev);

	irm_device = NULL;
	irm_remove_debugfs(irm_dev);
	irm_mbox_free(&irm_dev->mbox);
}

static const struct of_device_id google_irm_of_match_table[] = {
	{ .compatible = "google,irm" },
	{}
};
MODULE_DEVICE_TABLE(of, google_irm_of_match_table);

static struct platform_driver google_irm_platform_driver = {
	.probe = google_irm_platform_probe,
	.remove = google_irm_platform_remove,
	.driver = {
		.name = "google-irm",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_irm_of_match_table),
	},
};
module_platform_driver(google_irm_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google IRM driver");
MODULE_LICENSE("GPL");
