/* SPDX-License-Identifier: GPL-2.0 only */

#include <linux/bits.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/i2c.h>
#include <linux/i3c/master.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>		/* For kmalloc/kzalloc */
#include <linux/io.h>		/* For ioremap */
#include <linux/device.h>	/* Needed for dev_get_drvdata */
#include <linux/types.h>

#include "dw-i3c-xfer-host.h"
#include "dw-i3c-registers.h"

#define CREATE_TRACE_POINTS
#include "dw-i3c-xfer-host-trace.h"

struct dw_i3c_fifo_caps {
	u8 cmdfifodepth;
	u8 datafifodepth;
};
struct dw_i3c_xfer_ap_host {
	struct dw_i3c_xfer_host_ops_t ops;
	struct device *dev;
	void __iomem *regs;
	int irq;
	struct {
		struct list_head list;
		struct dw_i3c_host_xfer *cur;
		spinlock_t lock;
	} xferqueue;
	struct dw_i3c_fifo_caps caps;
};

const struct dw_i3c_xfer_host_ops_t *dw_i3c_xfer_host_get_ops(struct platform_device *pdev)
{
	struct dw_i3c_xfer_ap_host *host = platform_get_drvdata(pdev);

	if (host == NULL) {
		return NULL;
	}

	return &host->ops;
}
EXPORT_SYMBOL_GPL(dw_i3c_xfer_host_get_ops);

void __iomem *dw_i3c_xfer_host_get_regs(struct platform_device *pdev)
{
	struct dw_i3c_xfer_ap_host *host = platform_get_drvdata(pdev);

	if (host == NULL) {
		return NULL;
	}

	return host->regs;
}
EXPORT_SYMBOL_GPL(dw_i3c_xfer_host_get_regs);

static void dump_regs_locked(struct dw_i3c_xfer_ap_host *host)
{

}

static void dw_i3c_xfer_host_wr_tx_fifo(struct dw_i3c_xfer_ap_host *host,
					const u8 *bytes, int nbytes)
{
	writesl(host->regs + RX_TX_DATA_PORT, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, bytes + (nbytes & ~3), nbytes & 3);
		writesl(host->regs + RX_TX_DATA_PORT, &tmp, 1);
	}
}

static void dw_i3c_xfer_host_read_fifo(struct dw_i3c_xfer_ap_host *host,
					int reg, u8 *bytes, int nbytes)
{
	readsl(host->regs + reg, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		readsl(host->regs + reg, &tmp, 1);
		memcpy(bytes + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

static void dw_i3c_xfer_host_rd_rx_fifo(struct dw_i3c_xfer_ap_host *host, u8 *bytes, int nbytes)
{
	return dw_i3c_xfer_host_read_fifo(host, RX_TX_DATA_PORT, bytes, nbytes);
}

static bool dw_i3c_xfer_host_msg_rnw(struct dw_i3c_host_xfer *xfer, int idx)
{
	if (xfer->type == XFER_TYPE_I3C) {
		return xfer->msgs.i3c_msgs[idx].rnw;
	} else if (xfer->type == XFER_TYPE_I2C) {
		return xfer->msgs.i2c_msgs[idx].flags & I2C_M_RD;
	} else if (xfer->type == XFER_TYPE_CCC) {
		return xfer->msgs.ccc_msg->rnw;
	}
	return false;
}

static u16 dw_i3c_xfer_host_get_msg_len(struct dw_i3c_host_xfer *xfer, int idx)
{
	if (xfer->type == XFER_TYPE_I3C) {
		return xfer->msgs.i3c_msgs[idx].len;
	} else if (xfer->type == XFER_TYPE_I2C) {
		return xfer->msgs.i2c_msgs[idx].len;
	} else if (xfer->type == XFER_TYPE_CCC) {
		return xfer->msgs.ccc_msg->dests[0].payload.len;
	}
	return 0;
}

static const u8 *dw_i3c_xfer_host_get_msg_tx_buf(struct dw_i3c_host_xfer *xfer,
						int idx)
{
	if (xfer->type == XFER_TYPE_I3C) {
		return xfer->msgs.i3c_msgs[idx].data.out;
	} else if (xfer->type == XFER_TYPE_I2C) {
		return xfer->msgs.i2c_msgs[idx].buf;
	} else if (xfer->type == XFER_TYPE_CCC) {
		return xfer->msgs.ccc_msg->dests[0].payload.data;
	}
	return NULL;
}

static u8 *dw_i3c_xfer_host_get_msg_rx_buf(struct dw_i3c_host_xfer *xfer, int idx)
{
	if (xfer->type == XFER_TYPE_I3C) {
		return xfer->msgs.i3c_msgs[idx].data.in;
	} else if (xfer->type == XFER_TYPE_I2C) {
		return xfer->msgs.i2c_msgs[idx].buf;
	} else if (xfer->type == XFER_TYPE_CCC) {
		return xfer->msgs.ccc_msg->dests[0].payload.data;
	}
	return NULL;
}

static void dw_i3c_xfer_host_build_i3c_cmd(const struct dw_i3c_host_xfer *xfer,
					struct dw_i3c_cmd *cmd, int idx)
{
	cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(xfer->msgs.i3c_msgs[idx].len) |
		      COMMAND_PORT_TRANSFER_ARG;

	if (xfer->msgs.i3c_msgs[idx].rnw) {
		cmd->cmd_lo = COMMAND_PORT_READ_TRANSFER |
			      COMMAND_PORT_SPEED(xfer->dev_data.read_ds);

	} else {
		cmd->cmd_lo = COMMAND_PORT_SPEED(xfer->dev_data.write_ds);
	}

	cmd->cmd_lo |= COMMAND_PORT_TID(idx) |
		       COMMAND_PORT_DEV_INDEX(xfer->dev_data.index) |
		       COMMAND_PORT_ROC;

	if (idx == (xfer->ncmds - 1))
		cmd->cmd_lo |= COMMAND_PORT_TOC;
}

static void dw_i3c_xfer_host_build_i2c_cmd(const struct dw_i3c_host_xfer *xfer,
					struct dw_i3c_cmd *cmd, int idx)
{
	cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(xfer->msgs.i2c_msgs[idx].len) |
		      COMMAND_PORT_TRANSFER_ARG;

	cmd->cmd_lo = COMMAND_PORT_TID(idx) |
		      COMMAND_PORT_DEV_INDEX(xfer->dev_data.index) |
		      COMMAND_PORT_SPEED(xfer->dev_data.write_ds) |
		      COMMAND_PORT_ROC;

	if (xfer->msgs.i2c_msgs[idx].flags & I2C_M_RD) {
		cmd->cmd_lo |= COMMAND_PORT_READ_TRANSFER;
	}

	if (idx == (xfer->ncmds - 1))
		cmd->cmd_lo |= COMMAND_PORT_TOC;
}

static bool dw_i3c_xfer_finished(struct dw_i3c_host_xfer *xfer)
{
	bool data_finished = false;
	bool queue_finished = false;

	if (!xfer)
		return false;

	data_finished = xfer->res_tx_entries == 0 && xfer->res_rx_entries == 0;
	queue_finished = xfer->cmd_idx >= xfer->ncmds && xfer->resp_idx >= xfer->ncmds;

	if (data_finished && queue_finished)
		xfer->ret = 0;

	return data_finished && queue_finished;
}

static bool dw_i3c_xfer_error(struct dw_i3c_host_xfer *xfer, u32 resp)
{
	switch (RESPONSE_PORT_ERR_STATUS(resp)) {
	case RESPONSE_NO_ERROR:
		break;

	case RESPONSE_ERROR_PARITY:
	case RESPONSE_ERROR_TRANSF_ABORT:
	case RESPONSE_ERROR_CRC:
	case RESPONSE_ERROR_FRAME:
		xfer->ret = -EIO;
		break;

	case RESPONSE_ERROR_OVER_UNDER_FLOW:
		xfer->ret = -ENOSPC;
		break;

	case RESPONSE_ERROR_I2C_W_NACK_ERR:
	case RESPONSE_ERROR_ADDRESS_NACK:
	default:
		xfer->ret = -EINVAL;
		break;
	}

	return xfer->ret != -ETIMEDOUT;
}

static void dw_i3c_xfer_host_irq_thld_locked(struct dw_i3c_xfer_ap_host *host)
{
	struct dw_i3c_host_xfer *xfer = host->xferqueue.cur;
	u32 queue_thld_ctrl, data_thld_ctrl, intr_signal_en;
	unsigned int cmd_len, resp_len;

	/* Since every command takes two locations in the command and response buffers,
	* the FIFO depth has to be divided by two to get the number of commands it can take.
	*/
	cmd_len = umin(host->caps.cmdfifodepth / 2, 2 * (xfer->ncmds - xfer->cmd_idx));
	resp_len = umin(host->caps.cmdfifodepth / 2, xfer->ncmds - xfer->resp_idx);

	intr_signal_en = readl(host->regs + INTR_SIGNAL_EN);
	intr_signal_en |= INTR_XFER_MASK;

	queue_thld_ctrl = readl(host->regs + QUEUE_THLD_CTRL);
	queue_thld_ctrl &= ~QUEUE_THLD_CTRL_RESP_BUF_MASK;
	queue_thld_ctrl &= ~QUEUE_THLD_CTRL_CMD_EMPTY_BUF_MASK;

	if (resp_len)
		queue_thld_ctrl |= QUEUE_THLD_CTRL_RESP_BUF(resp_len);
	else
		intr_signal_en &= ~INTR_RESP_READY_STAT;

	if (cmd_len)
		queue_thld_ctrl |= QUEUE_THLD_CTRL_CMD_EMPTY_BUF(cmd_len);
	else
		intr_signal_en &= ~INTR_CMD_QUEUE_READY_STAT;

	data_thld_ctrl = readl(host->regs + DATA_BUFFER_THLD_CTRL);
	data_thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_TX_BUF_MASK;
	data_thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_RX_BUF_MASK;

	if (xfer->res_tx_entries >= 16)
		data_thld_ctrl |=
			DATA_BUFFER_THLD_CTRL_TX_BUF(INTR_DATA_BUF_THLD_16);
	else if (xfer->res_tx_entries >= 8)
		data_thld_ctrl |=
			DATA_BUFFER_THLD_CTRL_TX_BUF(INTR_DATA_BUF_THLD_8);
	else if (xfer->res_tx_entries >= 4)
		data_thld_ctrl |=
			DATA_BUFFER_THLD_CTRL_TX_BUF(INTR_DATA_BUF_THLD_4);
	else if (xfer->res_tx_entries >= 1)
		data_thld_ctrl |=
			DATA_BUFFER_THLD_CTRL_TX_BUF(INTR_DATA_BUF_THLD_1);
	else
		intr_signal_en &= ~INTR_TX_THLD_STAT;

	if (xfer->res_rx_entries >= 16)
		data_thld_ctrl |=
			DATA_BUFFER_THLD_CTRL_RX_BUF(INTR_DATA_BUF_THLD_16);
	else if (xfer->res_rx_entries >= 8)
		data_thld_ctrl |=
			DATA_BUFFER_THLD_CTRL_RX_BUF(INTR_DATA_BUF_THLD_8);
	else if (xfer->res_rx_entries >= 4)
		data_thld_ctrl |=
			DATA_BUFFER_THLD_CTRL_RX_BUF(INTR_DATA_BUF_THLD_4);
	else if (xfer->res_rx_entries >= 1)
		data_thld_ctrl |=
			DATA_BUFFER_THLD_CTRL_RX_BUF(INTR_DATA_BUF_THLD_1);
	else
		intr_signal_en &= ~INTR_RX_THLD_STAT;

	writel(intr_signal_en, host->regs + INTR_SIGNAL_EN);
	writel(queue_thld_ctrl, host->regs + QUEUE_THLD_CTRL);
	writel(data_thld_ctrl, host->regs + DATA_BUFFER_THLD_CTRL);

	trace_dw_i3c_xfer_thld_update(queue_thld_ctrl, data_thld_ctrl, intr_signal_en);
}

static void dw_i3c_xfer_host_enqueue_xfer(struct dw_i3c_xfer_ap_host *host,
					struct dw_i3c_host_xfer *xfer)
{
	unsigned long flags;

	INIT_LIST_HEAD(&xfer->node);
	xfer->ret = -ETIMEDOUT;
	init_completion(&xfer->comp);
	xfer->cmd_idx = 0;
	xfer->resp_idx = 0;
	xfer->tx_cmd_idx = 0;
	xfer->rx_cmd_idx = 0;
	xfer->tx_buf_cursor = 0;
	xfer->rx_buf_cursor = 0;

	spin_lock_irqsave(&host->xferqueue.lock, flags);
	if (host->xferqueue.cur) {
		list_add_tail(&xfer->node, &host->xferqueue.list);
	} else {
		host->xferqueue.cur = xfer;
		dw_i3c_xfer_host_irq_thld_locked(host);
	}
	spin_unlock_irqrestore(&host->xferqueue.lock, flags);
}

static void dw_i3c_xfer_host_dequeue_xfer_locked(struct dw_i3c_xfer_ap_host *host,
					struct dw_i3c_host_xfer *xfer)
{
	dump_regs_locked(host);

	if (host->xferqueue.cur == xfer) {
		u32 status;

		host->xferqueue.cur = NULL;

		writel(RESET_CTRL_RX_FIFO | RESET_CTRL_TX_FIFO |
		       RESET_CTRL_RESP_QUEUE | RESET_CTRL_CMD_QUEUE,
		       host->regs + RESET_CTRL);

		readl_poll_timeout_atomic(host->regs + RESET_CTRL, status,
					  !status, 10, 1000000);
	} else {
		list_del_init(&xfer->node);
	}
}

static void
dw_i3c_xfer_host_push_cmd_queue_locked(struct dw_i3c_xfer_ap_host *host,
					u32 queue_status_level)
{
	struct dw_i3c_host_xfer *xfer = host->xferqueue.cur;
	unsigned int empty_cmds;
	int i;

	/* Per dw_i3c_cmd will push two commands into the command queue */
	empty_cmds = QUEUE_STATUS_LEVEL_CMD(queue_status_level) / 2;

	for (i = 0; i < empty_cmds && xfer->cmd_idx < xfer->ncmds;
		i++, xfer->cmd_idx++) {
		struct dw_i3c_cmd cmd;

		if (xfer->type == XFER_TYPE_I3C) {
			dw_i3c_xfer_host_build_i3c_cmd(xfer, &cmd, xfer->cmd_idx);
		} else if (xfer->type == XFER_TYPE_I2C) {
			dw_i3c_xfer_host_build_i2c_cmd(xfer, &cmd, xfer->cmd_idx);
		} else if (xfer->type == XFER_TYPE_CCC || xfer->type == XFER_TYPE_DAA) {
			cmd = xfer->ccc_cmd;
		} else {
			dev_err(host->dev, "invalid xfer type.");
			return;
		}

		trace_dw_i3c_xfer_cmd_push(cmd.cmd_hi, cmd.cmd_lo);

		writel(cmd.cmd_hi, host->regs + COMMAND_QUEUE_PORT);
		writel(cmd.cmd_lo, host->regs + COMMAND_QUEUE_PORT);
	}
}

static bool
dw_i3c_xfer_host_pull_resp_queue_locked(struct dw_i3c_xfer_ap_host *host,
					u32 queue_status_level)
{
	struct dw_i3c_host_xfer *xfer = host->xferqueue.cur;
	unsigned int nresps;
	bool is_msg_rnw;
	int i;

	nresps = QUEUE_STATUS_LEVEL_RESP(queue_status_level);

	for (i = 0; i < nresps && xfer->resp_idx < xfer->ncmds; i++, xfer->resp_idx++) {
		int resp_data_len;
		u32 resp = readl(host->regs + RESPONSE_QUEUE_PORT);

		trace_dw_i3c_xfer_resp_pull(resp);

		is_msg_rnw = dw_i3c_xfer_host_msg_rnw(xfer, xfer->resp_idx);
		resp_data_len =
			is_msg_rnw ? dw_i3c_xfer_host_get_msg_len(xfer, xfer->resp_idx) : 0;

		/* ccc resp is carries different information from normal transfers. */
		if (xfer->type == XFER_TYPE_CCC || xfer->type == XFER_TYPE_DAA)
			xfer->ccc_resp = resp;

		if (RESPONSE_PORT_ERR_STATUS(resp)) {
			/* ENTDAA is expected to end with IBA NACK */
			if (xfer->end_with_iba_nack &&
				RESPONSE_PORT_ERR_STATUS(resp) == RESPONSE_ERROR_IBA_NACK)
				continue;
			else {
				dev_err(host->dev,
					"resp err status %#08x, expect nack %d\n",
					resp, xfer->end_with_iba_nack);
				dump_regs_locked(host);
				xfer->ret = -EIO;
				break;
			}
		} else if (RESPONSE_PORT_TID(resp) != COMMON_COMMAND_CODE_TID &&
			RESPONSE_PORT_TID(resp) != COMMAND_PORT_TID_MASK(xfer->resp_idx)) {
			dev_err(host->dev,
				"resp RID %lu does not match the current TID %u\n",
				RESPONSE_PORT_TID(resp), xfer->resp_idx);
			dump_regs_locked(host);
			xfer->ret = -EIO;
			break;
		} else if (xfer->type != XFER_TYPE_CCC && xfer->type != XFER_TYPE_DAA &&
			resp_data_len != RESPONSE_PORT_DATA_LEN(resp)) {
			/* For write transfer, the remaining data length of the transfer if
			 * terminated early.
			 * For Read transfer, this field represents the actual amount of data
			 * received in bytes.
			 * For Address Assignment command, this field represents the remaining
			 * device count.
			 */
			dev_err(host->dev,
				"resp RX LEN %lu does not match the current RX LEN %u\n",
				RESPONSE_PORT_DATA_LEN(resp), resp_data_len);
			dump_regs_locked(host);
			xfer->ret = -EIO;
			break;
		}

		if (dw_i3c_xfer_error(xfer, resp))
			break;
	}
	return xfer->ret != -ETIMEDOUT;
}

static void dw_i3c_xfer_host_rd_rx_data_locked(struct dw_i3c_xfer_ap_host *host,
						u32 buf_status_level)
{
	struct dw_i3c_host_xfer *xfer = host->xferqueue.cur;
	bool is_msg_rnw;
	u16 buf_len, rx_buf_level, rx_len;
	u8 *rx_bytes;

	if (!xfer) {
		dev_err(host->dev,
			"%s: data remain in DATA buffer with no xfer processing\n", __func__);
		dump_regs_locked(host);
		return;
	}

	rx_buf_level = DATA_BUFFER_STATUS_LEVEL_RX(buf_status_level);

	while (rx_buf_level && xfer->rx_cmd_idx < xfer->ncmds) {
		is_msg_rnw = dw_i3c_xfer_host_msg_rnw(xfer, xfer->rx_cmd_idx);

		if (!is_msg_rnw) {
			xfer->rx_cmd_idx++;
			xfer->rx_buf_cursor = 0;
			continue;
		}

		rx_len = dw_i3c_xfer_host_get_msg_len(xfer, xfer->rx_cmd_idx);
		if (rx_len <= xfer->rx_buf_cursor) {
			xfer->rx_cmd_idx++;
			xfer->rx_buf_cursor = 0;
			continue;
		}

		buf_len = min(rx_buf_level * DATA_BUF_ENTRY_SIZE, rx_len - xfer->rx_buf_cursor);
		rx_bytes = dw_i3c_xfer_host_get_msg_rx_buf(xfer, xfer->rx_cmd_idx);
		dw_i3c_xfer_host_rd_rx_fifo(host, rx_bytes + xfer->rx_buf_cursor, buf_len);

		trace_dw_i3c_xfer_rx_data(buf_len, rx_buf_level);

		rx_buf_level -= (buf_len + DATA_BUF_ENTRY_SIZE - 1) / DATA_BUF_ENTRY_SIZE;
		xfer->rx_buf_cursor += buf_len;
		xfer->res_rx_entries -= (buf_len + DATA_BUF_ENTRY_SIZE - 1) / DATA_BUF_ENTRY_SIZE;
	}
}

static void dw_i3c_xfer_host_wr_tx_data_locked(struct dw_i3c_xfer_ap_host *host,
						u32 buf_status_level)
{
	struct dw_i3c_host_xfer *xfer = host->xferqueue.cur;
	bool is_msg_rnw;
	u16 buf_len, tx_empty_buf_level, tx_len;
	const u8 *tx_bytes;

	if (!xfer) {
		dev_err(host->dev,
			"%s: data remain in DATA buffer with no xfer processing\n", __func__);
		dump_regs_locked(host);
		return;
	}

	tx_empty_buf_level = DATA_BUFFER_STATUS_LEVEL_TX(buf_status_level);

	while (tx_empty_buf_level && xfer->tx_cmd_idx < xfer->ncmds) {
		is_msg_rnw = dw_i3c_xfer_host_msg_rnw(xfer, xfer->tx_cmd_idx);

		if (is_msg_rnw) {
			xfer->tx_cmd_idx++;
			xfer->tx_buf_cursor = 0;
			continue;
		}

		tx_len = dw_i3c_xfer_host_get_msg_len(xfer, xfer->tx_cmd_idx);
		if (tx_len <= xfer->tx_buf_cursor) {
			xfer->tx_cmd_idx++;
			xfer->tx_buf_cursor = 0;
			continue;
		}

		buf_len = min(tx_empty_buf_level * DATA_BUF_ENTRY_SIZE,
			      tx_len - xfer->tx_buf_cursor);
		tx_bytes = dw_i3c_xfer_host_get_msg_tx_buf(xfer, xfer->tx_cmd_idx);
		dw_i3c_xfer_host_wr_tx_fifo(host, tx_bytes + xfer->tx_buf_cursor, buf_len);

		trace_dw_i3c_xfer_tx_data(buf_len, tx_empty_buf_level);

		tx_empty_buf_level -= (buf_len + DATA_BUF_ENTRY_SIZE - 1) / DATA_BUF_ENTRY_SIZE;
		xfer->tx_buf_cursor += buf_len;
		xfer->res_tx_entries -= (buf_len + DATA_BUF_ENTRY_SIZE - 1) / DATA_BUF_ENTRY_SIZE;
	}
}

static void dw_i3c_end_xfer_locked(struct dw_i3c_xfer_ap_host *host)
{
	struct dw_i3c_host_xfer *xfer = host->xferqueue.cur;
	u32 intr_signal_en;

	if (!xfer)
		return;

	intr_signal_en = readl(host->regs + INTR_SIGNAL_EN);
	intr_signal_en &= (~INTR_XFER_MASK);
	writel(intr_signal_en, host->regs + INTR_SIGNAL_EN);

	if (xfer->ret < 0) {
		dw_i3c_xfer_host_dequeue_xfer_locked(host, xfer);
		writel(readl(host->regs + DEVICE_CTRL) | DEV_CTRL_RESUME | DEV_CTRL_ENABLE,
			host->regs + DEVICE_CTRL);
		dev_err(host->dev, "xfer failed with ret value: %d", xfer->ret);
	}

	complete(&xfer->comp);

	xfer = list_first_entry_or_null(&host->xferqueue.list,
					struct dw_i3c_host_xfer, node);

	host->xferqueue.cur = xfer;

	if (xfer) {
		list_del_init(&xfer->node);
		dw_i3c_xfer_host_irq_thld_locked(host);
	}
}

static irqreturn_t dw_i3c_xfer_ap_host_irq_handler(int irq, void *dev_id)
{
	struct dw_i3c_xfer_ap_host *host = dev_id;
	u32 intr_status, buf_status_level, queue_status_level;

	intr_status = readl(host->regs + INTR_STATUS);

	trace_dw_i3c_xfer_irq(intr_status);

	if (!(intr_status & readl(host->regs + INTR_STATUS_EN))) {
		writel(INTR_ALL, host->regs + INTR_STATUS);
		return IRQ_NONE;
	}

	spin_lock(&host->xferqueue.lock);

	if (intr_status & INTR_TRANSFER_ERR_STAT)
		writel(INTR_TRANSFER_ERR_STAT, host->regs + INTR_STATUS);

	if (host->xferqueue.cur) {
		buf_status_level = readl(host->regs + DATA_BUFFER_STATUS_LEVEL);
		dw_i3c_xfer_host_wr_tx_data_locked(host, buf_status_level);
		dw_i3c_xfer_host_rd_rx_data_locked(host, buf_status_level);

		queue_status_level = readl(host->regs + QUEUE_STATUS_LEVEL);
		dw_i3c_xfer_host_push_cmd_queue_locked(host, queue_status_level);

		if (dw_i3c_xfer_host_pull_resp_queue_locked(host, queue_status_level))
			dw_i3c_end_xfer_locked(host);
		else if (dw_i3c_xfer_finished(host->xferqueue.cur))
			dw_i3c_end_xfer_locked(host);
		else
			dw_i3c_xfer_host_irq_thld_locked(host);
	}

	spin_unlock(&host->xferqueue.lock);

	return IRQ_HANDLED;
}

static void dw_i3c_xfer_host_set_intr_regs(struct dw_i3c_xfer_ap_host *host)
{
	u32 thld_ctrl;

	thld_ctrl = readl(host->regs + QUEUE_THLD_CTRL);
	thld_ctrl &= ~(QUEUE_THLD_CTRL_RESP_BUF_MASK |
			QUEUE_THLD_CTRL_IBI_STAT_MASK |
			QUEUE_THLD_CTRL_IBI_STAT_MASK);
	thld_ctrl |= QUEUE_THLD_CTRL_IBI_STAT(1) | QUEUE_THLD_CTRL_IBI_DATA(31);
	writel(thld_ctrl, host->regs + QUEUE_THLD_CTRL);

	thld_ctrl = readl(host->regs + DATA_BUFFER_THLD_CTRL);
	thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_RX_BUF_MASK;
	writel(thld_ctrl, host->regs + DATA_BUFFER_THLD_CTRL);

	writel(INTR_ALL, host->regs + INTR_STATUS);
	writel(INTR_ALL, host->regs + INTR_STATUS_EN);
	writel(0, host->regs + INTR_SIGNAL_EN);

	writel(IBI_REQ_REJECT_ALL, host->regs + IBI_SIR_REQ_REJECT);
	writel(IBI_REQ_REJECT_ALL, host->regs + IBI_MR_REQ_REJECT);

	if (readl(host->regs + I3C_VER_ID) == I3C_VER_101) {
		u32 val = readl(host->regs + SCL_EXT_TERMN_LCNT_TIMING);

		val &= ~STOP_HLD_CNT_MASK;
		writel(val, host->regs + SCL_EXT_TERMN_LCNT_TIMING);

		val =  SDA_OD_PP_SWITCH_DLY(1) | SDA_PP_OD_SWITCH_DLY(1) | SDA_TX_HOLD(1);
		writel(val, host->regs + SDA_HOLD_SWITCH_DLY_TIMING);
	}
}


static const char *get_name(struct platform_device *pdev)
{
	return "dw_i3c_xfer_host_ap";
}

static int resume(struct platform_device *pdev)
{
	struct dw_i3c_xfer_ap_host *host = platform_get_drvdata(pdev);

	enable_irq(host->irq);
	dw_i3c_xfer_host_set_intr_regs(host);
	return 0;
}

static int suspend(struct platform_device *pdev)
{
	struct dw_i3c_xfer_ap_host *host = platform_get_drvdata(pdev);

	disable_irq(host->irq);
	return 0;
}

static int execute_xfer(struct dw_i3c_host_xfer *xfer, struct platform_device *pdev)
{
	struct dw_i3c_xfer_ap_host *host = platform_get_drvdata(pdev);

	trace_dw_i3c_xfer_exec_start(xfer->type, xfer->ncmds);

	dw_i3c_xfer_host_enqueue_xfer(host, xfer);
	if (!wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT)) {
		unsigned long flags;

		spin_lock_irqsave(&host->xferqueue.lock, flags);
		dw_i3c_xfer_host_dequeue_xfer_locked(host, xfer);
		spin_unlock_irqrestore(&host->xferqueue.lock, flags);
	}

	trace_dw_i3c_xfer_exec_done(xfer->ret);

	return xfer->ret;
}

static int priv_xfers(struct i3c_dev_desc *dev, struct i3c_priv_xfer *i3c_xfers,
		      int i3c_nxfers, struct platform_device *pdev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct dw_i3c_host_xfer xfer = {};
	int i;

	if (!i3c_nxfers) {
		return 0;
	}

	xfer.dev_data = *data;
	xfer.msgs.i3c_msgs = i3c_xfers;
	xfer.res_rx_entries = 0;
	xfer.res_tx_entries = 0;
	xfer.type = XFER_TYPE_I3C;
	xfer.ncmds = i3c_nxfers;

	for (i = 0; i < i3c_nxfers; i++) {
		if (i3c_xfers[i].rnw) {
			xfer.res_rx_entries +=
				(i3c_xfers[i].len + DATA_BUF_ENTRY_SIZE - 1) / DATA_BUF_ENTRY_SIZE;
		} else {
			xfer.res_tx_entries +=
				(i3c_xfers[i].len + DATA_BUF_ENTRY_SIZE - 1) / DATA_BUF_ENTRY_SIZE;
		}
	}

	return execute_xfer(&xfer, pdev);
}

static int i2c_xfers(struct i2c_dev_desc *dev, const struct i2c_msg *i2c_xfers,
		     int i2c_nxfers, struct platform_device *pdev)
{
	struct dw_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct dw_i3c_host_xfer xfer = {};
	int i;

	if (!i2c_nxfers) {
		return 0;
	}

	xfer.dev_data = *data;
	xfer.msgs.i2c_msgs = i2c_xfers;
	xfer.res_rx_entries = 0;
	xfer.res_tx_entries = 0;
	xfer.type = XFER_TYPE_I2C;
	xfer.ncmds = i2c_nxfers;

	for (i = 0; i < i2c_nxfers; i++) {
		if (i2c_xfers[i].flags & I2C_M_RD) {
			xfer.res_rx_entries +=
				(i2c_xfers[i].len + DATA_BUF_ENTRY_SIZE - 1) / DATA_BUF_ENTRY_SIZE;
		} else {
			xfer.res_tx_entries +=
				(i2c_xfers[i].len + DATA_BUF_ENTRY_SIZE - 1) / DATA_BUF_ENTRY_SIZE;
		}
	}

	return execute_xfer(&xfer, pdev);
}

static const struct dw_i3c_xfer_host_ops_t dw_xfer_host_ops = {
	.get_name = get_name,
	.resume = resume,
	.suspend = suspend,
	.priv_xfers = priv_xfers,
	.i2c_xfers = i2c_xfers,
	.execute_xfer = execute_xfer,
};

static int dw_i3c_xfer_ap_host_probe(struct platform_device *pdev)
{
	struct dw_i3c_xfer_ap_host *host;
	int ret;
	u32 fifo_depth;

	dw_i3c_xfer_host_trace_init(pdev);

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = &pdev->dev;
	INIT_LIST_HEAD(&host->xferqueue.list);
	spin_lock_init(&host->xferqueue.lock);

	host->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->regs))
		return PTR_ERR(host->regs);

	host->irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, host->irq, dw_i3c_xfer_ap_host_irq_handler,
				0, dev_name(&pdev->dev), host);

	disable_irq(host->irq);

	of_property_read_u32(pdev->dev.of_node, "cmdfifodepth", &fifo_depth);
	host->caps.cmdfifodepth = QUEUE_STATUS_LEVEL_CMD(fifo_depth);

	of_property_read_u32(pdev->dev.of_node, "datafifodepth", &fifo_depth);
	host->caps.datafifodepth = DATA_BUFFER_STATUS_LEVEL_TX(fifo_depth);

	host->ops = dw_xfer_host_ops;

	platform_set_drvdata(pdev, host);

	return ret;
}

static void dw_i3c_xfer_ap_host_remove(struct platform_device *pdev)
{

}

static const struct of_device_id dw_i3c_xfer_ap_host_of_match[] = {
	{
		.compatible = "google,dw-i3c-xfer-ap-host",
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_i3c_xfer_ap_host_of_match);

static struct platform_driver dw_i3c_xfer_ap_host = {
	.probe = dw_i3c_xfer_ap_host_probe,
	.remove_new = dw_i3c_xfer_ap_host_remove,
	.driver = {
		.name = "dw-i3c-xfer-ap-host",
		.of_match_table = dw_i3c_xfer_ap_host_of_match,
	},
};

module_platform_driver(dw_i3c_xfer_ap_host);

MODULE_AUTHOR("Bill Chang <billdir@google.com>");
MODULE_AUTHOR("Ivan Zaitsev <zaitsev@google.com>");
MODULE_DESCRIPTION("Google I3C Xfer Host AP DRIVER");
MODULE_LICENSE("GPL");
