// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google MailBox Array (MBA) Driver
 *
 * Copyright (c) 2023 Google LLC
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/spinlock.h>

#include "goog-mba-ctrl.h"

#define CREATE_TRACE_POINTS
#include "goog-mba-ctrl-trace.h"

static inline u32 mba_readl(struct goog_mba_ctrl_info *mba_info, u32 offset)
{
	u32 payload;
	int ret;

	ret = regmap_read(mba_info->client_reg, offset, &payload);
	if (WARN_ON(ret)) {
		dev_err(mba_info->dev, "Failed to read offset %x (ret: %d)\n", offset, ret);
		return 0;
	}

	return payload;
}

static inline void mba_writel(struct goog_mba_ctrl_info *mba_info, u32 val, u32 offset)
{
	int ret;

	ret = regmap_write(mba_info->client_reg, offset, val);
	if (WARN_ON(ret))
		dev_err(mba_info->dev, "Failed to write offset %x (ret: %d)\n", offset, ret);
}

static inline int goog_mba_ctrl_get_msg_reg_off(struct goog_mba_ctrl_info *mbox_info,
						u32 wd_idx)
{
	return mbox_info->cmn_msg_offset + (wd_idx * sizeof(u32));
}

static inline struct mbox_controller *
goog_mba_ctrl_get_q_mode_mbox(struct goog_mba_ctrl_info *mbox_info)
{
	return mbox_info->mboxes[MBOX_CTRL_QUEUE_MODE];
}

static inline struct mbox_controller *
goog_mba_ctrl_get_db_mode_mbox(struct goog_mba_ctrl_info *mbox_info)
{
	return mbox_info->mboxes[MBOX_CTRL_DOORBELL_MODE];
}

static int goog_mba_ctrl_set_ip_major_version(struct goog_mba_ctrl_info *mbox_info)
{
	const char *mbox_prop_name = "mba-ip-version";
	int ret = 0, cnt;
	struct device *dev = mbox_info->dev;
	u32 val;

	cnt = of_property_count_elems_of_size(dev->of_node, mbox_prop_name, sizeof(u32));
	if (cnt > MBA_IP_DT_PROP_MAX_CNT) {
		dev_err(dev, "%s: DT property %s element count exceeded(%d) ret: %d",
			__func__, mbox_prop_name, cnt, -EOVERFLOW);
		return -EOVERFLOW;
	} else if (cnt >= 0) {
		ret = of_property_read_u32_index(dev->of_node, mbox_prop_name, 0,
						 &mbox_info->ip_major_ver);
		switch (ret) {
		case 0:
		case -EINVAL:
			break;
		case -EOVERFLOW:
		case -ENODATA:
		default:
			dev_err(dev, "%s: DT property %s ret: %d",
				__func__, mbox_prop_name, ret);
			return ret;
		}
	}

	if (!mbox_info->ip_major_ver) {
		/*
		 * DT property is not available, read the MBA IP version from register.
		 * Older MBA IP reads from global register via the syscon regmap, while
		 * newer MBA IP reads from client instance register via ioremap.
		 */
		if (mbox_info->global_reg) {
			ret = regmap_read(mbox_info->global_reg, GLOBAL_MBA_IP_VER_OFFSET, &val);
			if (ret)
				return ret;
		} else {
			val = mba_readl(mbox_info, CLIENT_MBA_IP_VER);
		}
		mbox_info->ip_major_ver = (val >> MBA_IP_MAJOR_VER_SHIFT);
	}

	return 0;
}

static int goog_mba_ctrl_set_cmn_msg_offset(struct goog_mba_ctrl_info *mbox_info)
{
	if (mbox_info->ip_major_ver == MBA_IP_MAJOR_VER_1)
		mbox_info->cmn_msg_offset = CMN_MSG_OFFSET_20;
	else
		mbox_info->cmn_msg_offset = CMN_MSG_OFFSET_100;

	return 0;
};

/* clear all the pending interrupts */
static void goog_mba_ctrl_clr_intrs(struct goog_mba_ctrl_info *mbox_info)
{
	u32 irq_status;

	irq_status = mba_readl(mbox_info, CLIENT_IRQ_STATUS_OFFSET);
	irq_status &= CLIENT_IRQ_STATUS_MSG_INT | CLIENT_IRQ_STATUS_ACK_INT;

	mba_writel(mbox_info, irq_status, CLIENT_IRQ_STATUS_OFFSET);
}

/*
 * Reads payload data from the mailbox controller into the designated payload slot.
 *
 * Returns the offset of the payload within the payload buffer.
 */
static u32 goog_mba_ctrl_rd_payload(struct goog_mba_ctrl_info *mbox_info)
{
	u32 wd_idx;
	u32 payload_off = 0;

	if (goog_mba_ctrl_get_q_mode_mbox(mbox_info))
		payload_off = mbox_info->rx_q_rd_ptr * mbox_info->payload_size;

	for (wd_idx = 0; wd_idx < mbox_info->payload_size; wd_idx++)
		mbox_info->payload[payload_off + wd_idx] = mba_readl(mbox_info,
			goog_mba_ctrl_get_msg_reg_off(mbox_info, payload_off + wd_idx));

	return payload_off;
}

static void goog_mba_ctrl_wr_payload(struct goog_mba_ctrl_info *mbox_info,
				     u32 *data)
{
	u32 wd_idx;
	u32 payload_off = 0;

	if (goog_mba_ctrl_get_q_mode_mbox(mbox_info))
		payload_off = mbox_info->tx_q_wr_ptr * mbox_info->payload_size;

	for (wd_idx = 0; wd_idx < mbox_info->payload_size; wd_idx++)
		mba_writel(mbox_info, data[wd_idx],
			goog_mba_ctrl_get_msg_reg_off(mbox_info, payload_off + wd_idx));
}

static void goog_mba_ctrl_init_q_state(struct goog_mba_ctrl_info *info, int num_chans)
{
	info->tx_q_rd_ptr = 0;
	info->tx_q_wr_ptr = 0;
	info->tx_q_size = 0;
	info->tx_q_capacity = num_chans;

	info->rx_q_rd_ptr = 0;
	info->rx_q_capacity = num_chans;
}

#define GOOG_MBA_Q_XPORT_INIT_PROTO_VAL 0x100
static bool goog_mba_ctrl_check_q_reset(struct goog_mba_ctrl_info *mbox_info)
{
	if (mbox_info->tx_q_rd_ptr == 0) {
		u32 val = mba_readl(mbox_info, goog_mba_ctrl_get_msg_reg_off(mbox_info, 0));

		if (val == GOOG_MBA_Q_XPORT_INIT_PROTO_VAL)
			return true;
	}

	return false;
}

static int goog_mba_ctrl_get_chan_idx(struct mbox_controller *mbox,
				      struct mbox_chan *chan)
{
	int chan_idx;

	for (chan_idx = 0; chan_idx < mbox->num_chans; chan_idx++) {
		if (&mbox->chans[chan_idx] == chan)
			return chan_idx;
	}

	return -EINVAL;
}

static void goog_mba_ctrl_process_nq_txdone(struct goog_mba_ctrl_info *mbox_info)
{
	struct mbox_controller *mbox = mbox_info->mboxes[MBOX_CTRL_LEGACY_MODE];

	trace_goog_mba_ctrl_process_nq_txdone(mbox_info);
	mbox_chan_txdone(&mbox->chans[0], 0);
}

static void goog_mba_ctrl_process_q_txdone(struct goog_mba_ctrl_info *mbox_info)
{
	struct mbox_chan *chan;
	struct mbox_controller *mbox = goog_mba_ctrl_get_q_mode_mbox(mbox_info);
	u32 outstanding_msgs;
	u32 reqs_completed;
	u32 chan_idx;

	spin_lock(&mbox_info->lock);
	outstanding_msgs = mba_readl(mbox_info, CLIENT_OUTSTANDING_MSG);
	reqs_completed = mbox_info->tx_q_size - outstanding_msgs;
	spin_unlock(&mbox_info->lock);

	while (reqs_completed) {
		bool q_reset;

		spin_lock(&mbox_info->lock);
		chan_idx = mbox_info->tx_q_rd_ptr;
		chan = &mbox->chans[chan_idx];

		trace_goog_mba_ctrl_process_q_txdone(mbox_info, reqs_completed, outstanding_msgs);
		q_reset = goog_mba_ctrl_check_q_reset(mbox_info);
		if (!q_reset) {
			mbox_info->tx_q_rd_ptr = (mbox_info->tx_q_rd_ptr + 1) %
						  mbox_info->tx_q_capacity;
			mbox_info->tx_q_size--;
			reqs_completed--;
		} else {
			goog_mba_ctrl_init_q_state(mbox_info, mbox->num_chans);
		}
		spin_unlock(&mbox_info->lock);

		mbox_chan_txdone(chan, 0);

		if (q_reset)
			break;
	}
}

static void goog_mba_ctrl_process_nq_rx(struct goog_mba_ctrl_info *mbox_info, void *msg)
{
	struct mbox_controller *mbox = mbox_info->mboxes[MBOX_CTRL_LEGACY_MODE];

	trace_goog_mba_ctrl_process_nq_rx(mbox_info, msg);
	mbox_chan_received_data(&mbox->chans[0], msg);
}

static void goog_mba_ctrl_process_q_rx(struct goog_mba_ctrl_info *mbox_info)
{
	struct mbox_chan *chan;
	struct mbox_controller *mbox = goog_mba_ctrl_get_q_mode_mbox(mbox_info);
	u32 chan_idx;
	u32 payload_off;

	chan_idx = mbox_info->rx_q_rd_ptr;
	chan = &mbox->chans[chan_idx];

	payload_off = goog_mba_ctrl_rd_payload(mbox_info);
	trace_goog_mba_ctrl_process_q_rx(mbox_info, &mbox_info->payload[payload_off]);

	mbox_info->rx_q_rd_ptr = (mbox_info->rx_q_rd_ptr + 1) % mbox_info->rx_q_capacity;
	mbox_chan_received_data(chan, &mbox_info->payload[payload_off]);
}

static irqreturn_t goog_mba_ctrl_handle_doorbell_isr(struct goog_mba_ctrl_info *mbox_info)
{
	struct device *dev = mbox_info->dev;
	struct mbox_controller *mbox = goog_mba_ctrl_get_db_mode_mbox(mbox_info);
	u32 irq_status = mba_readl(mbox_info, CLIENT_DOORBELL_STATUS_OFFSET);
	int chan_idx;

	trace_goog_mba_ctrl_handle_doorbell_isr(mbox_info, irq_status);

	for (chan_idx = 0; chan_idx < mbox_info->max_client_db_chans && irq_status; chan_idx++) {
		if (!(irq_status & (1 << chan_idx)))
			continue;
		if (!mbox->chans[chan_idx].cl) {
			dev_warn(dev, "Trigger to free doorbell mbox(mbox: %s, bit#%d)\n",
				 dev_name(dev), chan_idx);
			continue;
		}

		/*
		 * doorbell IRQ mask delegation:
		 *   Mailbox controller of goog-mba-ctrl forward shadow doorbell IRQ status to
		 *   mailbox clients. Mailbox clients are allowed to clear "more than 1 activated
		 *   bits". So, this interrupt routine will skip those bits who has been clear by
		 *   mailbox clients.
		 *
		 *   Note: clients can only clear activated bits given by shadow IRQ register.
		 */
		if (mbox_info->db_irq_delegation) {
			u32 bits_to_clear, local_irq_status = irq_status;

			mbox_chan_received_data(&mbox->chans[chan_idx], &local_irq_status);
			bits_to_clear = ~local_irq_status & irq_status;
			bits_to_clear |= 1 << chan_idx;

			mba_writel(mbox_info, bits_to_clear, CLIENT_DOORBELL_STATUS_OFFSET);
			irq_status &= ~bits_to_clear;
		} else {
			u32 doorbell_bit_mask = 1 << chan_idx;

			mba_writel(mbox_info, doorbell_bit_mask, CLIENT_DOORBELL_STATUS_OFFSET);
			irq_status &= ~doorbell_bit_mask;
			mbox_chan_received_data(&mbox->chans[chan_idx], &doorbell_bit_mask);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t goog_mba_ctrl_isr(int irq, void *dev)
{
	struct goog_mba_ctrl_info *mbox_info = dev_get_drvdata(dev);
	u32 irq_status;

	if (goog_mba_ctrl_get_db_mode_mbox(mbox_info))
		return goog_mba_ctrl_handle_doorbell_isr(mbox_info);

	irq_status = mba_readl(mbox_info, CLIENT_IRQ_STATUS_OFFSET);

	if (!irq_status)
		return IRQ_NONE;

	if (irq_status & CLIENT_IRQ_STATUS_ACK_INT) {
		/*
		 * ACK interrupt needs to be cleared before handling tx_done()
		 * so that the next ack is not missed while the current ACK is getting processed.
		 */
		mba_writel(mbox_info, CLIENT_IRQ_STATUS_ACK_INT, CLIENT_IRQ_STATUS_OFFSET);

		if (goog_mba_ctrl_get_q_mode_mbox(mbox_info))
			goog_mba_ctrl_process_q_txdone(mbox_info);
		else
			goog_mba_ctrl_process_nq_txdone(mbox_info);
	}

	if (irq_status & CLIENT_IRQ_STATUS_MSG_INT) {
		if (goog_mba_ctrl_get_q_mode_mbox(mbox_info)) {
			/*
			 * TODO: move time to clear MSG interrupt earlier before process client's
			 * callback after data copy out.
			 */
			goog_mba_ctrl_process_q_rx(mbox_info);
			mba_writel(mbox_info, CLIENT_IRQ_STATUS_MSG_INT, CLIENT_IRQ_STATUS_OFFSET);
		} else {
			u32 payload_off = goog_mba_ctrl_rd_payload(mbox_info);

			/*
			 * For use case of GDMC, we need to make sure we clear irq register after
			 * data copy to protect data integraty.
			 * For AoC use case, MSG interrupt is used to send notification
			 * regardless client site state.
			 * b388309333: report a latency limitation that we should release irq line
			 * ASAP otherwise next coming MSG interrupt will be lost.
			 */
			mba_writel(mbox_info, CLIENT_IRQ_STATUS_MSG_INT, CLIENT_IRQ_STATUS_OFFSET);
			goog_mba_ctrl_process_nq_rx(mbox_info, &mbox_info->payload[payload_off]);
		}
	}

	return IRQ_HANDLED;
}

static int goog_mba_ctrl_trigger_doorbell(struct goog_mba_ctrl_info *mbox_info,
					  struct mbox_chan *chan)
{
	struct mbox_controller *mbox = goog_mba_ctrl_get_db_mode_mbox(mbox_info);
	struct device *dev = mbox_info->dev;
	int idx;

	idx = goog_mba_ctrl_get_chan_idx(mbox, chan);
	if (idx >= mbox_info->max_client_db_chans) {
		dev_warn(dev, "trigger invalid doorbell bit(chan: %d, max_client_db_chans: %u)\n",
			 idx, mbox_info->max_client_db_chans);
		return 0;
	}

	mba_writel(mbox_info, (1U << idx), CLIENT_CLIENT_DOORBELL_TRIG);

	return 0;
}

static void goog_mba_ctrl_trigger_host_irq(struct goog_mba_ctrl_info *mbox_info)
{
	mba_writel(mbox_info, SET_HOST_IRQ, CLIENT_IRQ_TRIG_OFFSET);
}

static inline void goog_mba_ctrl_disable_client_db_irq_locked(struct goog_mba_ctrl_info *mbox_info,
							      u32 bits_to_disable)
{
	u32 val, db_pending_irq_mask;

	/*
	 * MBA doorbell feature has high resolution doorbell slot in doorbell status register.
	 * The way to tear down single slot of doorbell mailbox would be slightly different to
	 * legacy mailbox which has only single slot.
	 *
	 * To safely disable doorbell interrupt have few steps:
	 *  1. Disable doorbell mask
	 */
	val = mba_readl(mbox_info, CLIENT_DOORBELL_MASK_OFFSET);
	val &= ~bits_to_disable;
	mba_writel(mbox_info, val, CLIENT_DOORBELL_MASK_OFFSET);

	/*
	 *  2. Synchronized irq line to ensure no in-flight ISR for potentail race condition
	 *  3. Clear interrupt register if this doorbell mailbox has pending irq bit
	 */
	synchronize_irq(mbox_info->irq);
	db_pending_irq_mask = mba_readl(mbox_info, CLIENT_DOORBELL_STATUS_OFFSET) & bits_to_disable;
	if (db_pending_irq_mask)
		mba_writel(mbox_info, db_pending_irq_mask, CLIENT_DOORBELL_STATUS_OFFSET);
}

static inline void goog_mba_ctrl_enable_client_db_irq_locked(struct goog_mba_ctrl_info *mbox_info,
							     u32 bits_to_enable)
{
	u32 val;

	val = mba_readl(mbox_info, CLIENT_DOORBELL_MASK_OFFSET);
	val |= bits_to_enable;
	mba_writel(mbox_info, val, CLIENT_DOORBELL_MASK_OFFSET);
}

static inline void
goog_mba_ctrl_disable_client_all_db_irq_locked(struct goog_mba_ctrl_info *mbox_info)
{
	mba_writel(mbox_info, 0, CLIENT_DOORBELL_MASK_OFFSET);
}

static inline void
goog_mba_ctrl_disable_client_mbox_irq_locked(struct goog_mba_ctrl_info *mbox_info)
{
	u32 val;
	val = mba_readl(mbox_info, CLIENT_IRQ_CONFIG_OFFSET);
	val &= ~(CLIENT_IRQ_MASK_MSG_INT | CLIENT_IRQ_MASK_ACK_INT | ENABLE_HOST_AUTO_ACK);
	mba_writel(mbox_info, val, CLIENT_IRQ_CONFIG_OFFSET);
}

static inline void goog_mba_ctrl_enable_client_mbox_irq_locked(struct goog_mba_ctrl_info *mbox_info)
{
	u32 val;

	val = mba_readl(mbox_info, CLIENT_IRQ_CONFIG_OFFSET);
	val |= (CLIENT_IRQ_MASK_MSG_INT | CLIENT_IRQ_MASK_ACK_INT | ENABLE_HOST_AUTO_ACK);
	mba_writel(mbox_info, val, CLIENT_IRQ_CONFIG_OFFSET);
}

void mbox_stop_channel(struct mbox_chan *chan)
{
	struct goog_mba_ctrl_info *mbox_info;
	int channel_idx;

	if (!chan || !chan->cl)
		return;

	mbox_info = dev_get_drvdata(chan->mbox->dev);

	mutex_lock(&mbox_info->channel_lock);

	WARN_ON(mbox_info->active_channels == 0);
	mbox_info->active_channels--;

	if (goog_mba_ctrl_get_db_mode_mbox(mbox_info)) {
		channel_idx = chan - chan->mbox->chans;

		goog_mba_ctrl_disable_client_db_irq_locked(mbox_info, 1UL << channel_idx);
	} else if (!mbox_info->active_channels) {
		goog_mba_ctrl_disable_client_mbox_irq_locked(mbox_info);
	}

	if (!mbox_info->active_channels)
		disable_irq(mbox_info->irq);

	mutex_unlock(&mbox_info->channel_lock);
}
EXPORT_SYMBOL_GPL(mbox_stop_channel);

int mbox_start_channel(struct mbox_chan *chan)
{
	struct goog_mba_ctrl_info *mbox_info;

	if (!chan || !chan->cl)
		return -EINVAL;

	mbox_info = dev_get_drvdata(chan->mbox->dev);

	mutex_lock(&mbox_info->channel_lock);
	if (!mbox_info->active_channels)
		enable_irq(mbox_info->irq);

	if (goog_mba_ctrl_get_db_mode_mbox(mbox_info)) {
		int channel_idx = chan - chan->mbox->chans;

		goog_mba_ctrl_enable_client_db_irq_locked(mbox_info, 1UL << channel_idx);
	} else if (!mbox_info->active_channels) {
		goog_mba_ctrl_enable_client_mbox_irq_locked(mbox_info);
	}

	mbox_info->active_channels++;
	mutex_unlock(&mbox_info->channel_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mbox_start_channel);

static int goog_mba_ctrl_send_data_nq(struct goog_mba_ctrl_info *mbox_info, void *data)
{
	u32 data_size;

	data_size = data ? mbox_info->payload_size : 0;
	trace_goog_mba_ctrl_send_data_nq(mbox_info, data, data_size);
	if (data)
		goog_mba_ctrl_wr_payload(mbox_info, data);

	goog_mba_ctrl_trigger_host_irq(mbox_info);

	return 0;
}

static int goog_mba_ctrl_send_data_q(struct goog_mba_ctrl_info *mbox_info,
				     struct mbox_chan *chan, void *data)
{
	u32 data_size;
	int chan_idx;
	struct mbox_controller *mbox = goog_mba_ctrl_get_q_mode_mbox(mbox_info);
	unsigned long flags;
	int ret = 0;

	chan_idx = goog_mba_ctrl_get_chan_idx(mbox, chan);
	if (chan_idx < 0) {
		dev_err(mbox_info->dev, "chan invalid, ret %d", chan_idx);
		return chan_idx;
	}

	spin_lock_irqsave(&mbox_info->lock, flags);
	if (mbox_info->tx_q_size == mbox_info->tx_q_capacity) {
		dev_err(mbox_info->dev, "mailbox queue is full");
		ret = -EBUSY;
		goto exit;
	}

	if (mbox_info->tx_q_wr_ptr != chan_idx) {
		dev_err(mbox_info->dev, "chan_idx (%d) != tx_q_wr_ptr (%d)",
			chan_idx, mbox_info->tx_q_wr_ptr);
		ret = -EPROTO;
		goto exit;
	}

	data_size = data ? mbox_info->payload_size : 0;
	trace_goog_mba_ctrl_send_data_q(mbox_info, data, data_size);
	if (data) {
		goog_mba_ctrl_wr_payload(mbox_info, data);

		mbox_info->tx_q_wr_ptr = (mbox_info->tx_q_wr_ptr + 1) % mbox_info->tx_q_capacity;
		mbox_info->tx_q_size++;
	}

	goog_mba_ctrl_trigger_host_irq(mbox_info);
exit:
	spin_unlock_irqrestore(&mbox_info->lock, flags);

	return ret;
}

static int goog_mba_ctrl_send_data(struct mbox_chan *chan, void *data)
{
	struct goog_mba_ctrl_info *mbox_info = dev_get_drvdata(chan->mbox->dev);
	int ret;

	if (goog_mba_ctrl_get_q_mode_mbox(mbox_info))
		ret = goog_mba_ctrl_send_data_q(mbox_info, chan, data);
	else if (goog_mba_ctrl_get_db_mode_mbox(mbox_info))
		ret = goog_mba_ctrl_trigger_doorbell(mbox_info, chan);
	else
		ret = goog_mba_ctrl_send_data_nq(mbox_info, data);

	return ret;
}

static int goog_mba_ctrl_startup(struct mbox_chan *chan)
{
	return mbox_start_channel(chan);
}

static void goog_mba_ctrl_shutdown(struct mbox_chan *chan)
{
	mbox_stop_channel(chan);
}

static struct mbox_chan_ops goog_mba_ctrl_chan_ops = {
	.send_data = goog_mba_ctrl_send_data,
	.startup = goog_mba_ctrl_startup,
	.shutdown = goog_mba_ctrl_shutdown,
};

static const struct regmap_config goog_mba_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static struct regmap *goog_mba_ctrl_init_regmap(struct goog_mba_ctrl_info *mbox_info,
						struct platform_device *pdev)
{
	struct device *dev = mbox_info->dev;
	void __iomem *base;
	struct regmap *client_reg;

	/*
	 * Mailbox controller can share MBA client region with other controller. Use syscon
	 * framework and get shared structure of regmap for operation by lookup phandle with name of
	 * "google,syscon-client-phandle".
	 *
	 * One use case is that two mailbox controllers for both one of legacy mode and doorbell
	 * mode. Their control registers are shared in the same region. So the regmap should be
	 * shared and get by syscon.
	 */
	client_reg = syscon_regmap_lookup_by_phandle(dev->of_node, "google,syscon-client-phandle");
	if (!IS_ERR(client_reg))
		return client_reg;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(dev, "Failed to get and ioremap region (ret: %ld)\n", PTR_ERR(base));
		return base;
	}

	client_reg = devm_regmap_init_mmio(dev, base, &goog_mba_regmap_config);
	if (IS_ERR(client_reg))
		dev_err(dev, "Failed to init regmap (ret: %ld)\n", PTR_ERR(client_reg));

	return client_reg;
}

static void goog_mba_ctrl_syscon_regmap(struct goog_mba_ctrl_info *mbox_info)
{
	struct device *dev = mbox_info->dev;
	unsigned int out_args[NR_PHANDLE_ARG_COUNT];
	struct regmap *syscon;

	syscon = syscon_regmap_lookup_by_phandle_args(dev->of_node, "google,syscon-phandle",
						      NR_PHANDLE_ARG_COUNT, out_args);

	if (!IS_ERR(syscon)) {
		mbox_info->global_reg = syscon;
		mbox_info->client_idx = out_args[0];
	}
}

static int goog_mba_ctrl_syscon_regmap_read(struct goog_mba_ctrl_info *mbox_info,
					    unsigned int reg_offset, unsigned int client_idx,
					    u32 *read_value)
{
	int ret;
	struct device *dev = mbox_info->dev;

	if (reg_offset >= PAGE_SIZE) {
		dev_err(dev, "Offset size bigger than one page. (offset: 0x%x)\n", reg_offset);
		return -EINVAL;
	}

	if (!mbox_info->global_reg) {
		dev_err(dev, "syscon regmap is not available\n");
		return -EINVAL;
	}

	ret = regmap_read(mbox_info->global_reg, reg_offset, read_value);
	if (ret) {
		dev_err(dev, "Failed to read from regmap for client index#%d. (ret: %d)",
			client_idx, ret);
		return ret;
	}

	return 0;
}

static int goog_mba_ctrl_set_msg_buf_size(struct goog_mba_ctrl_info *mbox_info)
{
	unsigned int msg_reg_offset;

	/* After MBA_IP_MAJOR_VER_3, client instance register supports NUM_MSG_REG */
	if (mbox_info->ip_major_ver >= MBA_IP_MAJOR_VER_3) {
		mbox_info->msg_buf_size = mba_readl(mbox_info, CLIENT_NUM_MSG_REG);
		return 0;
	}

	/*
	 * If client instance register doesn't support NUM_MSG_REG, then read it from
	 * global register.
	 */
	msg_reg_offset = GLOBAL_NUM_MSG_REG_OFFSET(mbox_info->client_idx);

	return goog_mba_ctrl_syscon_regmap_read(mbox_info, msg_reg_offset, mbox_info->client_idx,
						&mbox_info->msg_buf_size);
}

static int goog_mba_ctrl_alloc_payload_buf(struct goog_mba_ctrl_info *mbox_info)
{
	int ret;
	struct device *dev = mbox_info->dev;
	u32 payload_size;

	ret = of_property_read_u32(dev->of_node, "payload-size", &payload_size);
	if (!ret)
		mbox_info->payload_size = min(mbox_info->msg_buf_size, payload_size);
	else
		mbox_info->payload_size = mbox_info->msg_buf_size;

	/* allocate the payload buffer as large as msg_buf_size to cover the queue mode */
	mbox_info->payload = devm_kcalloc(dev, mbox_info->msg_buf_size,
					  sizeof(*mbox_info->payload), GFP_KERNEL);
	if (!mbox_info->payload)
		return -ENOMEM;

	return 0;
}

static int goog_mba_ctrl_request_irq(struct goog_mba_ctrl_info *mbox_info,
				     struct platform_device *pdev)
{
	struct device *dev = mbox_info->dev;
	int ret;

	mbox_info->irq = platform_get_irq(pdev, 0);
	if (mbox_info->irq < 0) {
		dev_err(dev, "No interrupt for device\n");
		return -EINVAL;
	}

	/*
	 * Don't enable interrupt by default for two reasons:
	 *  1. The mailbox controller does not need to handle interrupts when no clients
	 *     are active, as the interrupt source is also disabled.
	 *  2. This prevents client callbacks from being invoked before a channel has
	 *     been formally requested.
	 *
	 * Interrupts will be enabled by the .startup callback when a client requests
	 * a mailbox channel.
	 */
	ret = devm_request_irq(dev, mbox_info->irq, goog_mba_ctrl_isr,
			       IRQF_TRIGGER_HIGH | IRQF_NO_SUSPEND | IRQF_NO_AUTOEN,
			       dev_name(dev), dev);
	if (ret != 0) {
		dev_err(dev, "failed to register interrupt handler: %d\n", ret);
		return -ENXIO;
	}

	return 0;
}

static struct mbox_controller *goog_mba_ctrl_init_mbox(struct goog_mba_ctrl_info *mbox_info,
						       enum goog_mba_ctrl_mode mbox_ctrler_type)
{
	struct device *dev = mbox_info->dev;
	struct mbox_controller *mbox;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return ERR_PTR(-ENOMEM);

	mbox->txdone_irq = true;
	mbox->dev = dev;
	mbox->ops = &goog_mba_ctrl_chan_ops;

	if (mbox_ctrler_type == MBOX_CTRL_QUEUE_MODE) {
		mbox->num_chans = mbox_info->msg_buf_size / mbox_info->payload_size;
		goog_mba_ctrl_init_q_state(mbox_info, mbox->num_chans);
	} else if (mbox_ctrler_type == MBOX_CTRL_DOORBELL_MODE) {
		mbox_info->max_client_db_chans = mba_readl(mbox_info,
							   CLIENT_CLIENT_DOORBELL_OFFSET);
		mbox_info->max_host_db_chans = mba_readl(mbox_info, CLIENT_HOST_DOORBELL_OFFSET);

		mbox->num_chans = max(mbox_info->max_client_db_chans, mbox_info->max_host_db_chans);
		if (mbox->num_chans == 0)
			return ERR_PTR(-EINVAL);
	} else {
		mbox->num_chans = 1;
	}

	mbox->chans = devm_kcalloc(dev, mbox->num_chans, sizeof(*mbox->chans), GFP_KERNEL);
	if (!mbox->chans)
		return ERR_PTR(-ENOMEM);

	return mbox;
}

static int goog_mba_ctrl_probe(struct platform_device *pdev)
{
	struct goog_mba_ctrl_info *mbox_info;
	int ret;
	struct device *dev = &pdev->dev;
	enum goog_mba_ctrl_mode mbox_type;

	mbox_info = devm_kzalloc(dev, sizeof(*mbox_info), GFP_KERNEL);
	if (!mbox_info)
		return -ENOMEM;

	mbox_info->dev = dev;
	spin_lock_init(&mbox_info->lock);
	mutex_init(&mbox_info->channel_lock);

	mbox_info->client_reg = goog_mba_ctrl_init_regmap(mbox_info, pdev);
	if (IS_ERR(mbox_info->client_reg))
		return PTR_ERR(mbox_info->client_reg);

	goog_mba_ctrl_syscon_regmap(mbox_info);

	ret = goog_mba_ctrl_set_ip_major_version(mbox_info);
	if (ret)
		return ret;

	ret = goog_mba_ctrl_set_msg_buf_size(mbox_info);
	if (ret)
		return ret;

	ret = goog_mba_ctrl_alloc_payload_buf(mbox_info);
	if (ret)
		return ret;

	if (of_property_read_bool(dev->of_node, "queue-mode")) {
		if (mbox_info->payload_size == 0) {
			dev_err(dev, "queue mode doesn't support zero payload_size\n");
			return -EINVAL;
		}
		if ((mbox_info->msg_buf_size % mbox_info->payload_size) != 0) {
			dev_err(mbox_info->dev,
				"msg_buf_size (%u) is not multiple of payload_size (%u)\n",
				mbox_info->msg_buf_size, mbox_info->payload_size);
			return -EINVAL;
		}

		mbox_type = MBOX_CTRL_QUEUE_MODE;
	} else if (of_property_read_bool(dev->of_node, "doorbell-mode")) {
		mbox_type = MBOX_CTRL_DOORBELL_MODE;
		mbox_info->db_irq_delegation = of_property_read_bool(dev->of_node,
								     "doorbell-irq-delegation");
	} else {
		mbox_type = MBOX_CTRL_LEGACY_MODE;
	}

	mbox_info->mboxes[mbox_type] = goog_mba_ctrl_init_mbox(mbox_info, mbox_type);
	if (IS_ERR(mbox_info->mboxes[mbox_type])) {
		ret = PTR_ERR(mbox_info->mboxes[mbox_type]);
		dev_err(dev, "fail to init mbox (mode: %d, ret: %d)\n", mbox_type, ret);
		return ret;
	}

	ret = goog_mba_ctrl_set_cmn_msg_offset(mbox_info);
	if (ret != 0) {
		dev_err(dev, "%s: error setting the cmn_msg_offset: ret: %d", __func__, ret);
		return ret;
	}

	if (goog_mba_ctrl_get_db_mode_mbox(mbox_info))
		goog_mba_ctrl_disable_client_all_db_irq_locked(mbox_info);
	else
		goog_mba_ctrl_disable_client_mbox_irq_locked(mbox_info);

	goog_mba_ctrl_clr_intrs(mbox_info);

	ret = goog_mba_ctrl_request_irq(mbox_info, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mbox_info);

	ret = devm_mbox_controller_register(dev, mbox_info->mboxes[mbox_type]);
	if (ret != 0) {
		dev_err(dev, "failed to register mailbox controller: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id goog_mba_ctrl_match[] = {
	{ .compatible = "google,mba-ctrl" },
	{ /* Sentinel */ }
};

static struct platform_driver goog_mba_ctrl = {
	.probe = goog_mba_ctrl_probe,
	.driver = {
		.name = "goog-mba-ctrl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goog_mba_ctrl_match),
	},
};

module_platform_driver(goog_mba_ctrl);

MODULE_DESCRIPTION("Google MailBox Array (MBA) Driver");
MODULE_AUTHOR("Lucas Wei <lucaswei@google.com>");
MODULE_LICENSE("GPL");
