// SPDX-License-Identifier: GPL-2.0-only

#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "google_icc.h"
#include "google_icc_provider.h"
#include "google_icc_vote.h"
#include "google_irm.h"

#define to_google_provider(_provider) \
	container_of(_provider, struct google_icc_provider, provider)

struct google_icc_provider {
	struct icc_provider provider;
	struct device *dev;
	struct irm_dev *irm_dev;
};

#define NUM_VC   5

static u32 num_vc = NUM_VC;

u32 google_icc_get_num_vc(void)
{
	return num_vc;
}

static int google_icc_get_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	*avg = 0;
	*peak = 0;

	return 0;
}

/* cleans up stale values from prior icc_set */
static void google_icc_pre_aggregate(struct icc_node *node)
{
	struct google_icc_node *gn;
	struct icc_vote_block *block;
	size_t type, rw, i;

	gn = node->data;

	gn->vote.vote_sync_type = 0;

	for (type = ICC_TYPE_GMC; type < NUM_ICC_TYPE; type++) {
		for (rw = ICC_READ; rw < NUM_ICC_RW; rw++) {
			block = &gn->vote.block[type][rw];

			for (i = 0; i < num_vc; i++) {
				block->avg_bw_vc[i] = 0;
				block->peak_bw_vc[i] = 0;
				block->rt_bw_vc[i] = 0;
				block->latency_vc[i] = U32_MAX;
				block->ltv_vc[i] = U32_MAX;
			}
		}
	}
}

static void total_bw_latency_calc(struct icc_vote *vote)
{
	struct icc_vote_block *block;
	size_t type, rw, i;

	for (type = ICC_TYPE_GMC; type < NUM_ICC_TYPE; type++) {
		for (rw = ICC_READ; rw < NUM_ICC_RW; rw++) {
			block = &vote->block[type][rw];

			block->avg_bw = 0;
			block->peak_bw = 0;
			block->rt_bw = 0;
			block->latency = U32_MAX;
			block->ltv = U32_MAX;

			for (i = 0; i < num_vc; i++) {
				block->avg_bw += block->avg_bw_vc[i];
				block->peak_bw += block->peak_bw_vc[i];
				block->rt_bw += block->rt_bw_vc[i];
				block->latency = min(block->latency, block->latency_vc[i]);
				block->ltv = min(block->ltv, block->ltv_vc[i]);
			}
		}
	}
}

static int google_icc_set(struct icc_node *src, struct icc_node *dst)
{
	struct google_icc_node *gn_src, *gn_dst;
	struct google_icc_provider *gp_src;
	int ret = 0;

	if (src == dst)
		return 0;

	gn_src = src->data;
	gn_dst = dst->data;

	gp_src = to_google_provider(src->provider);

	switch (gn_dst->type) {
	case TYPE_GSLC:
	case TYPE_GMC:
		total_bw_latency_calc(&gn_src->vote);

		ret = gp_src->irm_dev->ops->irm_vote(gp_src->irm_dev,
						     gn_src->attr,
						     &gn_src->vote);
		break;
	case TYPE_QBOX:
		/* TODO(b/247494866): Qos Box runtime update */
		break;
	default:
		break;
	}

	return ret;
}

static inline u64 addr_decode(u32 hi, u32 lo)
{
	u64 addr;

	addr = hi;
	addr <<= 32;
	addr += lo;

	return addr;
}

static int google_icc_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
				u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	struct google_icc_node *gn;
	struct icc_vote *path_vote, *vote;
	struct icc_vote_block *path_block, *block;
	size_t type, rw, i;
	u64 addr = addr_decode(avg_bw, peak_bw);

	if (addr == 0)
		return 0;

	gn = node->data;
	vote = &gn->vote;
	path_vote = (struct icc_vote *)addr;

	vote->vote_sync_type |= path_vote->vote_sync_type;

	for (type = ICC_TYPE_GMC; type < NUM_ICC_TYPE; type++) {
		for (rw = ICC_READ; rw < NUM_ICC_RW; rw++) {
			block = &vote->block[type][rw];
			path_block = &path_vote->block[type][rw];

			for (i = 0; i < num_vc; i++) {
				block->avg_bw_vc[i] += path_block->avg_bw_vc[i];
				block->peak_bw_vc[i] += path_block->peak_bw_vc[i];
				block->rt_bw_vc[i] += path_block->rt_bw_vc[i];
				block->latency_vc[i] = min(block->latency_vc[i],
							   path_block->latency_vc[i]);
				block->ltv_vc[i] = min(block->ltv_vc[i], path_block->ltv_vc[i]);
			}
		}
	}

	return 0;
}

static struct irm_dev *of_irm_dev_get(struct device *dev)
{
	struct platform_device *supplier_pdev;
	struct device *supplier_dev;
	struct irm_dev *irm_dev;
	struct device_node *np, *node;
	struct device_link *link;
	int idx = 0;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);

	np = dev->of_node;

	node = of_parse_phandle(np, "google,irm", idx);
	if (!node) {
		dev_warn(dev, "google,irm not found\n");
		return ERR_PTR(-EINVAL);
	}

	supplier_pdev = of_find_device_by_node(node);
	of_node_put(node);
	if (!supplier_pdev) {
		dev_warn(dev, "missing device for %pOF\n", node);
		return ERR_PTR(-EINVAL);
	}

	supplier_dev = &supplier_pdev->dev;

	link = device_link_add(dev, supplier_dev, DL_FLAG_AUTOREMOVE_CONSUMER);

	if (!link) {
		dev_err(dev, "add IRM device_link fail\n");
		return ERR_PTR(-EINVAL);
	}

	/* supplier is not probed */
	if (supplier_dev->links.status != DL_DEV_DRIVER_BOUND) {
		dev_dbg(dev, "supplier: %s is not probed\n", dev_name(supplier_dev));
		return ERR_PTR(-EPROBE_DEFER);
	}

	irm_dev = platform_get_drvdata(supplier_pdev);

	if (!irm_dev) {
		dev_err(dev, "Can't get irm_dev from supplier: %s\n", dev_name(supplier_dev));
		return ERR_PTR(-EINVAL);
	}

	return irm_dev;
}

static int is_valid_irm_dev_ops(struct device *dev, struct irm_dev_ops *ops)
{
	if (!ops) {
		dev_err(dev, "Invalid irm_dev_ops\n");
		return 0;
	}

	if (!ops->irm_vote) {
		dev_err(dev, "irm_dev_ops->irm_vote == NULL\n");
		return -EINVAL;
	}

	return 0;
}

int google_icc_platform_probe(struct platform_device *pdev)
{
	const struct google_icc_desc *desc;
	struct device *dev = &pdev->dev;
	struct icc_provider *provider;
	struct icc_onecell_data *data;
	struct icc_node *node;
	struct google_icc_provider *gp;
	struct google_icc_node *const *gnodes, *gn;
	size_t num_nodes, i, j;
	struct irm_dev *irm_dev;
	int ret;

	irm_dev = of_irm_dev_get(dev);
	if (IS_ERR(irm_dev))
		return PTR_ERR(irm_dev);

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	gnodes = desc->nodes;
	num_nodes = desc->num_nodes;

	gp = devm_kzalloc(dev, sizeof(*gp), GFP_KERNEL);
	if (!gp)
		return -ENOMEM;

	data = devm_kzalloc(dev, struct_size(data, nodes, num_nodes),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	provider = &gp->provider;
	provider->dev = dev;
	provider->inter_set = true;
	provider->set = google_icc_set;
	provider->get_bw = google_icc_get_bw;
	provider->pre_aggregate = google_icc_pre_aggregate;
	provider->aggregate = google_icc_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = data;

	gp->dev = dev;
	gp->irm_dev = irm_dev;

	if (is_valid_irm_dev_ops(dev, gp->irm_dev->ops))
		return -EINVAL;

	ret = icc_provider_register(provider);
	if (ret)
		return ret;

	data->num_nodes = num_nodes;

	for (i = 0; i < num_nodes; i++) {
		gn = gnodes[i];
		if (!gn)
			continue;

		node = icc_node_create(gn->id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = gn->name;
		node->data = gn;
		icc_node_add(node, provider);

		for (j = 0; j < gn->num_links; j++)
			icc_link_create(node, gn->links[j]);

		data->nodes[i] = node;
	}

	platform_set_drvdata(pdev, gp);

	return 0;
err:
	icc_nodes_remove(provider);
	icc_provider_deregister(provider);
	return ret;
}

void google_icc_platform_remove(struct platform_device *pdev)
{
	struct google_icc_provider *gp = platform_get_drvdata(pdev);

	icc_nodes_remove(&gp->provider);
	icc_provider_deregister(&gp->provider);
}

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google interconnect driver");
MODULE_LICENSE("GPL");
