/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Google LLC
 */

#ifndef _GOOG_MBA_CTRL_PRIV_H_
#define _GOOG_MBA_CTRL_PRIV_H_

#define MAX_Q_PAYLOAD_WORDS 4
#define MAX_NQ_PAYLOAD_WORDS 4

#define CLIENT_IRQ_TRIG_OFFSET   0x0
#define SET_HOST_IRQ		 0x1

#define CLIENT_IRQ_CONFIG_OFFSET 0x4
#define ENABLE_HOST_AUTO_ACK	 BIT(8)
#define CLIENT_IRQ_MASK_MSG_INT	 BIT(16)
#define CLIENT_IRQ_MASK_ACK_INT	 BIT(24)

#define CLIENT_IRQ_STATUS_OFFSET  0x8
#define CLIENT_IRQ_STATUS_MSG_INT 0x1
#define CLIENT_IRQ_STATUS_ACK_INT 0x100

#define CLIENT_MBA_IP_VER	0x30
#define CLIENT_NUM_MSG_REG	0x34
#define CLIENT_OUTSTANDING_MSG	0x10

#define GLOBAL_NUM_MSG_REG_OFFSET(x) (0x10 + ((x) * 4))
#define GLOBAL_MBA_IP_VER_OFFSET 4

#define CLIENT_CLIENT_DOORBELL_TRIG 0x20
#define CLIENT_DOORBELL_MASK_OFFSET 0x24
#define CLIENT_DOORBELL_STATUS_OFFSET 0x28
#define CLIENT_HOST_DOORBELL_OFFSET 0x38
#define CLIENT_CLIENT_DOORBELL_OFFSET 0x3c

#define MAX_MBA_CHANNELS 1
#define NR_PHANDLE_ARG_COUNT 1

#define CMN_MSG_OFFSET_20 0x20
#define CMN_MSG_OFFSET_100 0x100

#define MBA_IP_MAJOR_VER_1 0x1
#define MBA_IP_MAJOR_VER_3 0x3

#define MBA_IP_DT_PROP_MAX_CNT   2
#define MBA_IP_MAJOR_VER_SHIFT   24

#define MAX_MBOX_CTRL_NAME 64

enum goog_mba_ctrl_mode {
	MBOX_CTRL_LEGACY_MODE,
	MBOX_CTRL_QUEUE_MODE,
	MBOX_CTRL_DOORBELL_MODE,
	MBOX_CTRL_MODE_MAX,
};

struct goog_mba_ctrl_info {
	struct device *dev;
	struct regmap *global_reg;
	struct regmap *client_reg;
	u32 cmn_msg_offset;
	int irq;

	struct mbox_controller *mboxes[MBOX_CTRL_MODE_MAX];

	/* doorbell configuration */
	bool db_irq_delegation;

	/* protect active_channels and shared IRQ configuration. */
	struct mutex channel_lock;
	u32 active_channels; /* Number of active channels */
	u32 client_idx; /* client index/offset used in syscon regmap */
	u32 ip_major_ver; /* major version of the MBA IP */
	u32 msg_buf_size; /* size of the common message registers (in words) */
	u32 payload_size; /* size of the protocol payload (in words) */
	u32 *payload; /* message payload received from remote */

	/*
	 * protect queue operation including tx_q_rd_ptr, tx_q_wr_ptr, tx_q_size and tx_q_capacity.
	 */
	spinlock_t lock;
	u32 tx_q_rd_ptr;
	u32 tx_q_wr_ptr;
	u32 tx_q_size;
	u32 tx_q_capacity;

	u32 rx_q_rd_ptr;
	u32 rx_q_capacity;

	u32 max_client_db_chans;
	u32 max_host_db_chans;
};

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CTRL)
void mbox_stop_channel(struct mbox_chan *chan);
int mbox_start_channel(struct mbox_chan *chan);
#else
void mbox_stop_channel(struct mbox_chan *chan) {}
int mbox_start_channel(struct mbox_chan *chan)
{
	return -ENODEV;
}
#endif /* CONFIG_GOOGLE_MBA_CTRL */

#endif /* _GOOG_MBA_CTRL_PRIV_H_ */
