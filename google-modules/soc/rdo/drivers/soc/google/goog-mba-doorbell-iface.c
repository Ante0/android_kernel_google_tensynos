// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google MailBox Array (MBA) Doorbell Interface
 *
 * Copyright (c) 2025 Google LLC
 */
#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>

#include <soc/google/goog-mba-doorbell-iface.h>

struct mbox_chan_info {
	struct mbox_client client;
	struct mbox_chan *chan;
	int doorbell_bit_offset;
	struct mbox_doorbell_grp *db_grp;
};

struct mbox_doorbell_grp {
	int nr_doorbell;
	u32 mask;
	struct mbox_chan_info *mbox_chan_infos;
	void (*client_rx_callback)(struct mbox_client *cl, void *mssg);
};

/*
 * mbox_ring_doorbell - ring the doorbell by given channel structure
 * @chan:		mbox_chan structure for ringing host's doorbell
 *
 * Return: 0 or greater values for success, otherwise negative values.
 */
int mbox_ring_doorbell(struct mbox_chan *chan)
{
	return mbox_send_message(chan, NULL);
}
EXPORT_SYMBOL_GPL(mbox_ring_doorbell);

int mbox_ring_doorbell_grp(struct mbox_doorbell_grp *db_grp, int mboxes_idx)
{
	struct mbox_chan_info *info = &db_grp->mbox_chan_infos[mboxes_idx];

	return mbox_send_message(info->chan, NULL);
}
EXPORT_SYMBOL_GPL(mbox_ring_doorbell_grp);

static void mbox_rx_callback(struct mbox_client *cl, void *mssg)
{
	struct mbox_chan_info *info = container_of(cl, struct mbox_chan_info, client);
	struct mbox_doorbell_grp *db_grp = info->db_grp;
	u32 *irq_status = (u32 *)mssg;
	u32 activated_db_bits = (*irq_status) & db_grp->mask;

	if (db_grp->client_rx_callback)
		db_grp->client_rx_callback(cl, &activated_db_bits);

	*irq_status &= ~db_grp->mask;
}

static int mbox_get_first_argv(struct device *dev, int mboxes_idx)
{
	int ret;
	int channel_idx;
	struct of_phandle_args spec;

	ret = of_parse_phandle_with_args(dev->of_node, "mboxes", "#mbox-cells", mboxes_idx, &spec);
	if (ret < 0)
		return ret;

	channel_idx = spec.args[0];
	of_node_put(spec.np);

	return channel_idx;
}

struct mbox_doorbell_grp *mbox_request_doorbell_grp(struct device *dev, struct mbox_client *client)
{
	struct mbox_chan_info *mbox_infos;
	struct mbox_chan_info *info;
	struct mbox_doorbell_grp *db_grp;
	struct mbox_controller *mbox_ctrl = NULL;
	int nr_mboxes;
	int idx;
	int doorbell_bit_offset;
	int ret;

	if (!dev || !dev->of_node) {
		dev_err(dev, "No owner device node\n");
		return ERR_PTR(-ENODEV);
	}

	nr_mboxes = of_count_phandle_with_args(dev->of_node, "mboxes", "#mbox-cells");
	if (nr_mboxes <= 0) {
		dev_err(dev, "can't get phandle number of \"mboxes\" property\n");
		return ERR_PTR(-ENODEV);
	}

	db_grp = devm_kzalloc(dev, sizeof(*db_grp), GFP_KERNEL);
	mbox_infos = devm_kcalloc(dev, nr_mboxes, sizeof(*mbox_infos), GFP_KERNEL);
	if (!mbox_infos || !db_grp)
		return ERR_PTR(-ENOMEM);

	/* replace client's rx_callback by mbox instrument callback */
	db_grp->client_rx_callback = client->rx_callback;

	for (idx = 0; idx < nr_mboxes; idx++) {
		struct mbox_chan *chan;

		info = &mbox_infos[idx];
		info->client = *client;
		info->client.rx_callback = mbox_rx_callback;

		chan = mbox_request_channel(&info->client, idx);
		if (IS_ERR(chan)) {
			dev_err(dev, "fail to request channel (idx:%d, err: %lu)\n", idx,
				PTR_ERR(chan));
			ret = PTR_ERR(chan);
			goto release_mem;
		}
		info->chan = chan;

		if (!mbox_ctrl) {
			mbox_ctrl = info->chan->mbox;
		} else if (mbox_ctrl != info->chan->mbox) {
			dev_err(dev, "mbox of chan#%d mis-match to other channels\n", idx);
			ret = -EINVAL;
			goto free_chans;
		}

		doorbell_bit_offset = mbox_get_first_argv(dev, idx);
		if (doorbell_bit_offset < 0) {
			dev_err(dev, "Failed to get argv[0] of chan#%d (ret:%d)\n", idx,
				doorbell_bit_offset);
			ret = doorbell_bit_offset;
			goto free_chans;
		}

		info->doorbell_bit_offset = doorbell_bit_offset;
		db_grp->mask |= 1 << doorbell_bit_offset;
		info->db_grp = db_grp;
	}
	db_grp->mbox_chan_infos = mbox_infos;
	db_grp->nr_doorbell = nr_mboxes;

	return db_grp;

free_chans:
	for (idx = 0; idx < nr_mboxes; idx++) {
		info = &mbox_infos[idx];
		if (info->chan)
			mbox_free_channel(info->chan);
	}

release_mem:
	devm_kfree(dev, db_grp);
	devm_kfree(dev, mbox_infos);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(mbox_request_doorbell_grp);

MODULE_DESCRIPTION("Google MBA Doorbell Interface");
MODULE_AUTHOR("Lucas Wei <lucaswei@google.com>");
MODULE_LICENSE("GPL");
