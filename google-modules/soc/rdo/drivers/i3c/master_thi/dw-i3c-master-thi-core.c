// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 *
 * Author: Vitor Soares <vitor.soares@synopsys.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "dw-i3c-master-thi-core.h"
#include "dw-i3c-registers.h"

#define CREATE_TRACE_POINTS
#include "dw-i3c-master-thi-trace.h"

static u8 even_parity(u8 p)
{
	p ^= p >> 4;
	p &= 0xf;

	return (0x9669 >> p) & 1;
}

static bool dw_i3c_master_supports_ccc_cmd(struct i3c_master_controller *m,
					const struct i3c_ccc_cmd *cmd)
{
	if (cmd->ndests > 1)
		return false;

	switch (cmd->id) {
	case I3C_CCC_ENEC(true):
	case I3C_CCC_ENEC(false):
	case I3C_CCC_DISEC(true):
	case I3C_CCC_DISEC(false):
	case I3C_CCC_ENTAS(0, true):
	case I3C_CCC_ENTAS(0, false):
	case I3C_CCC_RSTDAA(true):
	case I3C_CCC_RSTDAA(false):
	case I3C_CCC_ENTDAA:
	case I3C_CCC_SETMWL(true):
	case I3C_CCC_SETMWL(false):
	case I3C_CCC_SETMRL(true):
	case I3C_CCC_SETMRL(false):
	case I3C_CCC_ENTHDR(0):
	case I3C_CCC_SETDASA:
	case I3C_CCC_SETNEWDA:
	case I3C_CCC_GETMWL:
	case I3C_CCC_GETMRL:
	case I3C_CCC_GETPID:
	case I3C_CCC_GETBCR:
	case I3C_CCC_GETDCR:
	case I3C_CCC_GETSTATUS:
	case I3C_CCC_GETMXDS:
	case I3C_CCC_GETHDRCAP:
		return true;
	default:
		return false;
	}
}

static inline struct dw_i3c_master *
to_dw_i3c_master(struct i3c_master_controller *master)
{
	return container_of(master, struct dw_i3c_master, base);
}

static void dw_i3c_master_disable(struct dw_i3c_master *master)
{
	writel(master->dev_ctrl & ~DEV_CTRL_ENABLE, master->regs + DEVICE_CTRL);
}

static void dw_i3c_master_enable(struct dw_i3c_master *master)
{
	writel(master->dev_ctrl | DEV_CTRL_ENABLE, master->regs + DEVICE_CTRL);
}

static int dw_i3c_master_get_addr_pos(struct dw_i3c_master *master, u8 addr)
{
	int pos;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (addr == master->devs[pos].addr)
			return pos;
	}

	return -EINVAL;
}

static int dw_i3c_master_get_free_pos(struct dw_i3c_master *master)
{
	if (!(master->free_pos & GENMASK(master->maxdevs - 1, 0)))
		return -ENOSPC;

	return ffs(master->free_pos) - 1;
}

static int dw_i3c_clk_cfg(struct dw_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u32 scl_timing;
	u8 hcnt, lcnt;

	core_rate = clk_get_rate(master->core_clk);
	if (!core_rate)
		return -EINVAL;

	core_period = DIV_ROUND_UP(1000000000, core_rate);

	hcnt = DIV_ROUND_UP(I3C_BUS_THIGH_MAX_NS, core_period) - 1;
	if (hcnt < SCL_I3C_TIMING_CNT_MIN)
		hcnt = SCL_I3C_TIMING_CNT_MIN;

	lcnt = DIV_ROUND_UP(core_rate, master->base.bus.scl_rate.i3c) - hcnt;
	if (lcnt < SCL_I3C_TIMING_CNT_MIN)
		lcnt = SCL_I3C_TIMING_CNT_MIN;

	scl_timing = SCL_I3C_TIMING_HCNT(hcnt) | SCL_I3C_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I3C_PP_TIMING);
	master->i3c_pp_timing = scl_timing;

	if (master->base.bus.mode == I3C_BUS_MODE_PURE) {
		writel(BUS_I3C_MST_FREE(lcnt), master->regs + BUS_FREE_TIMING);
		master->bus_free_timing = BUS_I3C_MST_FREE(lcnt);
	}

	lcnt = max_t(u8,
		DIV_ROUND_UP(I3C_BUS_TLOW_OD_MIN_NS + TLOW_OD_MARGIN_NS, core_period), lcnt);
	scl_timing = SCL_I3C_TIMING_HCNT(hcnt) | SCL_I3C_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I3C_OD_TIMING);
	master->i3c_od_timing = scl_timing;

	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR1_SCL_RATE) - hcnt;
	scl_timing = SCL_EXT_LCNT_1(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR2_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_2(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR3_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_3(lcnt);
	lcnt = DIV_ROUND_UP(core_rate, I3C_BUS_SDR4_SCL_RATE) - hcnt;
	scl_timing |= SCL_EXT_LCNT_4(lcnt);
	writel(scl_timing, master->regs + SCL_EXT_LCNT_TIMING);
	master->ext_lcnt_timing = scl_timing;

	return 0;
}

static int dw_i2c_clk_cfg(struct dw_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u16 hcnt, lcnt;
	u32 scl_timing;

	core_rate = clk_get_rate(master->core_clk);
	if (!core_rate)
		return -EINVAL;

	core_period = DIV_ROUND_UP(1000000000, core_rate);

	lcnt = DIV_ROUND_UP(I3C_BUS_I2C_FMP_TLOW_MIN_NS, core_period);
	hcnt = DIV_ROUND_UP(core_rate, I3C_BUS_I2C_FM_PLUS_SCL_RATE) - lcnt;
	scl_timing = SCL_I2C_FMP_TIMING_HCNT(hcnt) |
		     SCL_I2C_FMP_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FMP_TIMING);
	master->i2c_fmp_timing = scl_timing;

	lcnt = DIV_ROUND_UP(I3C_BUS_I2C_FM_TLOW_MIN_NS, core_period);
	hcnt = DIV_ROUND_UP(core_rate, I3C_BUS_I2C_FM_SCL_RATE) - lcnt;
	scl_timing = SCL_I2C_FM_TIMING_HCNT(hcnt) |
		     SCL_I2C_FM_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I2C_FM_TIMING);
	master->i2c_fm_timing = scl_timing;

	writel(BUS_I3C_MST_FREE(lcnt), master->regs + BUS_FREE_TIMING);
	master->bus_free_timing = BUS_I3C_MST_FREE(lcnt);
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_I2C_SLAVE_PRESENT,
	       master->regs + DEVICE_CTRL);

	return 0;
}

static int dw_i3c_master_bus_init(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct i3c_bus *bus = i3c_master_get_bus(m);
	struct i3c_device_info info = { };
	int ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "%s: cannot resume i3c bus master, err: %d\n", __func__, ret);
		return ret;
	}

	ret = master->platform_ops->init(master);
	if (ret)
		goto rpm_out;

	switch (bus->mode) {
	case I3C_BUS_MODE_MIXED_FAST:
	case I3C_BUS_MODE_MIXED_LIMITED:
		ret = dw_i2c_clk_cfg(master);
		if (ret)
			goto rpm_out;
		fallthrough;
	case I3C_BUS_MODE_PURE:
		ret = dw_i3c_clk_cfg(master);
		if (ret)
			goto rpm_out;
		break;
	default:
		ret = -EINVAL;
		goto rpm_out;
	}

	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		goto rpm_out;

	writel(DEV_ADDR_DYNAMIC_ADDR_VALID | DEV_ADDR_DYNAMIC(ret),
	       master->regs + DEVICE_ADDR);
	master->dev_addr = ret;

	memset(&info, 0, sizeof(info));
	info.dyn_addr = ret;

	ret = i3c_master_set_info(&master->base, &info);
	if (ret)
		goto rpm_out;

	master->ibi_sir_req_rej = false;
	if (readl(master->regs + IBI_SIR_REQ_REJECT) == IBI_REQ_REJECT_ALL)
		master->ibi_sir_req_rej = true;

	/* For now don't support Hot-Join */
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_HOT_JOIN_NACK,
	       master->regs + DEVICE_CTRL);

	master->dev_ctrl = readl(master->regs + DEVICE_CTRL);

	dw_i3c_master_enable(master);

rpm_out:
	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
	return ret;
}

static void dw_i3c_master_bus_cleanup(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	dw_i3c_master_disable(master);
}

static int dw_i3c_ccc_set(struct dw_i3c_master *master,
			struct i3c_ccc_cmd *ccc)
{
	struct dw_i3c_host_xfer xfer = {};
	int ret, pos = 0;

	if (ccc->id & I3C_CCC_DIRECT) {
		pos = dw_i3c_master_get_addr_pos(master, ccc->dests[0].addr);
		if (pos < 0)
			return pos;
	}

	xfer.ccc_cmd.cmd_hi =
		COMMAND_PORT_ARG_DATA_LEN(ccc->dests[0].payload.len) |
		COMMAND_PORT_TRANSFER_ARG;

	xfer.ccc_cmd.cmd_lo =
		COMMAND_PORT_CP | COMMAND_PORT_TID(COMMON_COMMAND_CODE_TID) |
		COMMAND_PORT_DEV_INDEX(pos) | COMMAND_PORT_CMD(ccc->id) |
		COMMAND_PORT_TOC | COMMAND_PORT_ROC;

	xfer.res_rx_entries = 0;
	xfer.res_tx_entries =
		(ccc->dests[0].payload.len + DATA_BUF_ENTRY_SIZE - 1) /
		DATA_BUF_ENTRY_SIZE;
	xfer.msgs.ccc_msg = ccc;
	xfer.type = XFER_TYPE_CCC;
	xfer.ncmds = 1;

	master->xfer_host_ops->execute_xfer(&xfer, master->xfer_host_pdev);

	ret = xfer.ret;
	if (RESPONSE_PORT_ERR_STATUS(xfer.ccc_resp) == RESPONSE_ERROR_IBA_NACK)
		ccc->err = I3C_ERROR_M2;

	return ret;
}

static int dw_i3c_ccc_get(struct dw_i3c_master *master, struct i3c_ccc_cmd *ccc)
{
	struct dw_i3c_host_xfer xfer = {};
	int ret, pos;

	pos = dw_i3c_master_get_addr_pos(master, ccc->dests[0].addr);
	if (pos < 0)
		return pos;

	xfer.ccc_cmd.cmd_hi =
		COMMAND_PORT_ARG_DATA_LEN(ccc->dests[0].payload.len) |
		COMMAND_PORT_TRANSFER_ARG;

	xfer.ccc_cmd.cmd_lo = COMMAND_PORT_READ_TRANSFER |
			      COMMAND_PORT_TID(COMMON_COMMAND_CODE_TID) |
			      COMMAND_PORT_CP | COMMAND_PORT_DEV_INDEX(pos) |
			      COMMAND_PORT_CMD(ccc->id) | COMMAND_PORT_TOC |
			      COMMAND_PORT_ROC;

	xfer.res_rx_entries =
		(ccc->dests[0].payload.len + DATA_BUF_ENTRY_SIZE - 1) /
		DATA_BUF_ENTRY_SIZE;
	xfer.res_tx_entries = 0;
	xfer.msgs.ccc_msg = ccc;

	xfer.type = XFER_TYPE_CCC;
	xfer.ncmds = 1;

	master->xfer_host_ops->execute_xfer(&xfer, master->xfer_host_pdev);

	ret = xfer.ret;
	if (RESPONSE_PORT_ERR_STATUS(xfer.ccc_resp) == RESPONSE_ERROR_IBA_NACK)
		ccc->err = I3C_ERROR_M2;

	return ret;
}

static int dw_i3c_master_send_ccc_cmd(struct i3c_master_controller *m,
				      struct i3c_ccc_cmd *ccc)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	int ret = 0;

	trace_dw_i3c_thi_ccc(ccc->id, ccc->rnw, ccc->dests[0].payload.len);

	if (ccc->id == I3C_CCC_ENTDAA)
		return -EINVAL;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "%s: cannot resume i3c bus master, err: %d\n", __func__, ret);
		return ret;
	}

	if (ccc->rnw)
		ret = dw_i3c_ccc_get(master, ccc);
	else
		ret = dw_i3c_ccc_set(master, ccc);

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
	return ret;
}

static int dw_i3c_master_daa(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_host_xfer xfer = {};
	u32 olddevs, newdevs;
	u8 p, last_addr = 0;
	int ret, pos;

	trace_dw_i3c_thi_daa("start", 0);

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev,
			"%s: cannot resume i3c bus master, err: %d\n", __func__,
			ret);
		return ret;
	}

	olddevs = ~(master->free_pos);

	/* Prepare DAT before launching DAA. */
	for (pos = 0; pos < master->maxdevs; pos++) {
		if (olddevs & BIT(pos))
			continue;

		ret = i3c_master_get_free_addr(m, last_addr + 1);
		if (ret < 0) {
			ret = -ENOSPC;
			goto rpm_out;
		}

		master->devs[pos].addr = ret;
		p = even_parity(ret);
		last_addr = ret;
		ret |= (p << 7);

		writel(DEV_ADDR_TABLE_DYNAMIC_ADDR(ret),
		       master->regs + DEV_ADDR_TABLE_LOC(master->datstartaddr, pos));
	}

	/* Clear return value. The loop above will leave the last address there when finished. */
	ret = 0;

	/* DAA will iterate until NACK to make sure all i3c slaves have address assigned. */
	xfer.end_with_iba_nack = 1;

	pos = dw_i3c_master_get_free_pos(master);
	if (pos < 0) {
		ret = pos;
		goto rpm_out;
	}
	xfer.ccc_cmd.cmd_hi = COMMAND_PORT_TRANSFER_ARG;
	xfer.ccc_cmd.cmd_lo = COMMAND_PORT_DEV_COUNT(master->maxdevs - pos) |
			      COMMAND_PORT_TID(COMMON_COMMAND_CODE_TID) |
			      COMMAND_PORT_DEV_INDEX(pos) |
			      COMMAND_PORT_CMD(I3C_CCC_ENTDAA) |
			      COMMAND_PORT_ADDR_ASSGN_CMD | COMMAND_PORT_TOC |
			      COMMAND_PORT_ROC;

	xfer.res_rx_entries = 0;
	xfer.res_tx_entries = 0;

	xfer.type = XFER_TYPE_DAA;
	xfer.ncmds = 1;

	master->xfer_host_ops->execute_xfer(&xfer, master->xfer_host_pdev);

	trace_dw_i3c_thi_daa("end", xfer.ret);

	/* ENTDAA is expected to end with NACK, resume the controller */
	writel(master->dev_ctrl | DEV_CTRL_RESUME | DEV_CTRL_ENABLE,
	       master->regs + DEVICE_CTRL);

	newdevs = GENMASK(master->maxdevs - RESPONSE_PORT_DATA_LEN(xfer.ccc_resp) - 1, 0);
	newdevs &= ~olddevs;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (newdevs & BIT(pos))
			i3c_master_add_i3c_dev_locked(m, master->devs[pos].addr);
	}

rpm_out:
	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
	return ret;
}

static int dw_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				    struct i3c_priv_xfer *i3c_xfers,
				    int i3c_nxfers)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_host_xfer xfer = {};
	int i, ret = 0;

	if (!i3c_nxfers)
		return 0;

	trace_dw_i3c_thi_xfer(dev->info.dyn_addr, true, i3c_nxfers);

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "%s: cannot resume i3c bus master, err: %d\n", __func__, ret);
		return ret;
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
				(i3c_xfers[i].len + DATA_BUF_ENTRY_SIZE - 1) /
				DATA_BUF_ENTRY_SIZE;
		} else {
			xfer.res_tx_entries +=
				(i3c_xfers[i].len + DATA_BUF_ENTRY_SIZE - 1) /
				DATA_BUF_ENTRY_SIZE;
		}
	}

	master->xfer_host_ops->execute_xfer(&xfer, master->xfer_host_pdev);

	ret = xfer.ret;

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
	return ret;
}

static int dw_i3c_master_reattach_i3c_dev(struct i3c_dev_desc *dev,
					  u8 old_dyn_addr)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	int pos;

	pos = dw_i3c_master_get_free_pos(master);

	if (data->index > pos && pos > 0) {
		writel(0,
		       master->regs +
		       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

		master->devs[data->index].addr = 0;
		master->free_pos |= BIT(data->index);

		data->index = pos;
		data->read_ds =  dev->info.max_read_ds;
		data->write_ds = dev->info.max_write_ds;
		master->devs[pos].addr = dev->info.dyn_addr;
		master->free_pos &= ~BIT(pos);
	}

	writel(DEV_ADDR_TABLE_DYNAMIC_ADDR(dev->info.dyn_addr),
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	master->devs[data->index].addr = dev->info.dyn_addr;

	return 0;
}

static int dw_i3c_master_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_i2c_dev_data *data;
	int pos;

	pos = dw_i3c_master_get_free_pos(master);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	data->read_ds =  dev->info.max_read_ds;
	data->write_ds = dev->info.max_write_ds;
	master->devs[pos].addr = dev->info.dyn_addr ? : dev->info.static_addr;
	master->free_pos &= ~BIT(pos);
	i3c_dev_set_master_data(dev, data);

	writel(DEV_ADDR_TABLE_DYNAMIC_ADDR(master->devs[pos].addr),
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	return 0;
}

static void dw_i3c_master_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	writel(0,
	       master->regs +
	       DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	i3c_dev_set_master_data(dev, NULL);
	master->devs[data->index].addr = 0;
	master->free_pos |= BIT(data->index);
	kfree(data);
}

static int dw_i3c_master_i2c_xfers(struct i2c_dev_desc *dev,
				   const struct i2c_msg *i2c_xfers,
				   int i2c_nxfers)
{
	struct dw_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_host_xfer xfer = {};
	int i, ret = 0;

	if (!i2c_nxfers)
		return 0;

	trace_dw_i3c_thi_xfer(dev->addr, false, i2c_nxfers);

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev,
			"%s: cannot resume i3c bus master, err: %d\n", __func__,
			ret);
		return ret;
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
				(i2c_xfers[i].len + DATA_BUF_ENTRY_SIZE - 1) /
				DATA_BUF_ENTRY_SIZE;
		} else {
			xfer.res_tx_entries +=
				(i2c_xfers[i].len + DATA_BUF_ENTRY_SIZE - 1) /
				DATA_BUF_ENTRY_SIZE;
		}
	}

	master->xfer_host_ops->execute_xfer(&xfer, master->xfer_host_pdev);

	ret = xfer.ret;

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
	return ret;
}

static int dw_i3c_master_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_i2c_dev_data *data;
	int pos, i2c_speed_mode = I2C_FAST_MODE_PLUS;

	pos = dw_i3c_master_get_free_pos(master);
	if (pos < 0)
		return pos;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->index = pos;
	/* I3C LVR bit[4]: tell whether the device operates in FM (Fast Mode) or FM+ mode */
	if (dev->lvr & I2C_LVR_MODE)
		i2c_speed_mode = I2C_FAST_MODE;

	data->write_ds = i2c_speed_mode;
	data->read_ds = i2c_speed_mode;
	master->devs[pos].addr = dev->addr;
	master->devs[pos].is_i2c = true;
	master->free_pos &= ~BIT(pos);
	i2c_dev_set_master_data(dev, data);

	writel(DEV_ADDR_TABLE_LEGACY_I2C_DEV |
		DEV_ADDR_TABLE_STATIC_ADDR(dev->addr),
		master->regs +
		DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	return 0;
}

static void dw_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	writel(0,
		master->regs +
		DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));

	i2c_dev_set_master_data(dev, NULL);
	master->devs[data->index].addr = 0;
	master->free_pos |= BIT(data->index);
	kfree(data);
}

static const struct i3c_master_controller_ops dw_mipi_i3c_ops = {
	.bus_init = dw_i3c_master_bus_init,
	.bus_cleanup = dw_i3c_master_bus_cleanup,
	.attach_i3c_dev = dw_i3c_master_attach_i3c_dev,
	.reattach_i3c_dev = dw_i3c_master_reattach_i3c_dev,
	.detach_i3c_dev = dw_i3c_master_detach_i3c_dev,
	.do_daa = dw_i3c_master_daa,
	.supports_ccc_cmd = dw_i3c_master_supports_ccc_cmd,
	.send_ccc_cmd = dw_i3c_master_send_ccc_cmd,
	.priv_xfers = dw_i3c_master_priv_xfers,
	.attach_i2c_dev = dw_i3c_master_attach_i2c_dev,
	.detach_i2c_dev = dw_i3c_master_detach_i2c_dev,
	.i2c_xfers = dw_i3c_master_i2c_xfers,
};

static int dw_i3c_master_enable_clks(struct dw_i3c_master *master)
{
	int ret = 0;

	ret = clk_prepare_enable(master->core_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(master->apb_clk);
	if (ret) {
		clk_disable_unprepare(master->core_clk);
		return ret;
	}

	return 0;
}

static inline void dw_i3c_master_disable_clks(struct dw_i3c_master *master)
{
	clk_disable_unprepare(master->apb_clk);
	clk_disable_unprepare(master->core_clk);
}

/* default platform ops implementations */
static int dw_i3c_platform_init_nop(struct dw_i3c_master *i3c)
{
	return 0;
}

static void dw_i3c_platform_set_dat_ibi_nop(struct dw_i3c_master *i3c,
					struct i3c_dev_desc *dev,
					bool enable, u32 *dat)
{
}

static const struct dw_i3c_platform_ops dw_i3c_platform_ops_default = {
	.init = dw_i3c_platform_init_nop,
	.set_dat_ibi = dw_i3c_platform_set_dat_ibi_nop,
};

static int dw_i3c_master_thi_probe(struct dw_i3c_master *master,
				   struct platform_device *pdev)
{
	const struct i3c_master_controller_ops *ops;
	bool ext_power_control;
	int ret;
	struct device_node *xfer_host_dev_node;

	dw_i3c_thi_trace_init(pdev);

	if (!master->platform_ops)
		master->platform_ops = &dw_i3c_platform_ops_default;

	xfer_host_dev_node = of_parse_phandle(pdev->dev.of_node, "xfer-host-pdev", 0);

	if (!xfer_host_dev_node) {
		dev_err(&pdev->dev, "Fail to find xfer_host_dev_node.\n");
		return -ENODEV;
	}

	master->xfer_host_pdev = of_find_device_by_node(xfer_host_dev_node);

	if (!master->xfer_host_pdev) {
		dev_err(&pdev->dev, "Fail to find xfer_host_pdev.\n");
		return -ENODEV;
	}

	master->xfer_host_ops = dw_i3c_xfer_host_get_ops(master->xfer_host_pdev);

	if (!master->xfer_host_ops) {
		dev_err(&pdev->dev, "Fail to get xfer_host_ops.\n");
		return -EPROBE_DEFER;
	}

	master->regs = dw_i3c_xfer_host_get_regs(master->xfer_host_pdev);
	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	master->core_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(master->core_clk))
		return PTR_ERR(master->core_clk);

	master->apb_clk = devm_clk_get_optional(&pdev->dev, "apb_clk");
	if (IS_ERR(master->apb_clk))
		return PTR_ERR(master->apb_clk);

	master->core_rst = devm_reset_control_get_optional_exclusive(&pdev->dev,
								    "core_rst");
	if (IS_ERR(master->core_rst))
		return PTR_ERR(master->core_rst);

	ret = dw_i3c_master_enable_clks(master);
	if (ret)
		return ret;

	master->io_vreg_dev = devm_regulator_get_optional(&pdev->dev, "io-vreg");
	if (IS_ERR(master->io_vreg_dev)) {
		if (PTR_ERR(master->io_vreg_dev) == -ENODEV) {
			master->io_vreg_dev = NULL;
		} else {
			dev_err(&pdev->dev, "camio regulator is not ready\n");
			return PTR_ERR(master->io_vreg_dev);
		}
	}

	if (master->io_vreg_dev) {
		ret = regulator_enable(master->io_vreg_dev);
		if (ret) {
			dev_err(&pdev->dev, "Fail to enable camio regulator\n");
			goto err_assert_rst;
		}
	}

	reset_control_deassert(master->core_rst);

	platform_set_drvdata(pdev, master);

	ret = readl(master->regs + DEVICE_ADDR_TABLE_POINTER);
	master->datstartaddr = ret;
	master->maxdevs = ret >> 16;
	master->free_pos = GENMASK(master->maxdevs - 1, 0);

	ops = &dw_mipi_i3c_ops;

	/* Increment PM usage to avoid possible spurious runtime suspend */
	pm_runtime_get_noresume(&pdev->dev);

	ext_power_control = of_property_read_bool(pdev->dev.of_node, "external-power-control");

	ret = master->xfer_host_ops->resume(master->xfer_host_pdev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = i3c_master_register(&master->base, &pdev->dev, ops, false);
	if (ret)
		goto err_disable_pm;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	if (ext_power_control)
		pm_runtime_suspend(&pdev->dev);

	return 0;

err_disable_pm:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

err_assert_rst:
	reset_control_assert(master->core_rst);

	dw_i3c_master_disable_clks(master);

	return ret;
}

/* base platform implementation */

static int dw_i3c_probe(struct platform_device *pdev)
{
	struct dw_i3c_master *master;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->dev = &pdev->dev;

	return dw_i3c_master_thi_probe(master, pdev);
}

static void dw_i3c_remove(struct platform_device *pdev)
{
	struct dw_i3c_master *master = platform_get_drvdata(pdev);

	i3c_master_unregister(&master->base);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	reset_control_assert(master->core_rst);

	dw_i3c_master_disable_clks(master);
}

static void dw_i3c_master_restore_addrs(struct dw_i3c_master *master)
{
	u32 pos, reg_val;

	writel(DEV_ADDR_DYNAMIC_ADDR_VALID | DEV_ADDR_DYNAMIC(master->dev_addr),
	       master->regs + DEVICE_ADDR);

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (master->free_pos & BIT(pos))
			continue;

		if (master->devs[pos].is_i2c)
			reg_val = DEV_ADDR_TABLE_LEGACY_I2C_DEV |
			       DEV_ADDR_TABLE_STATIC_ADDR(master->devs[pos].addr);
		else
			reg_val = DEV_ADDR_TABLE_DYNAMIC_ADDR(master->devs[pos].addr);

		writel(reg_val, master->regs + DEV_ADDR_TABLE_LOC(master->datstartaddr, pos));
	}
}

static void dw_i3c_master_restore_timing_regs(struct dw_i3c_master *master)
{
	writel(master->i3c_pp_timing, master->regs + SCL_I3C_PP_TIMING);
	writel(master->bus_free_timing, master->regs + BUS_FREE_TIMING);
	writel(master->i3c_od_timing, master->regs + SCL_I3C_OD_TIMING);
	writel(master->ext_lcnt_timing, master->regs + SCL_EXT_LCNT_TIMING);

	if (master->base.bus.mode != I3C_BUS_MODE_PURE) {
		writel(master->i2c_fmp_timing, master->regs + SCL_I2C_FMP_TIMING);
		writel(master->i2c_fm_timing, master->regs + SCL_I2C_FM_TIMING);
	}
}

static int __maybe_unused dw_i3c_master_runtime_suspend(struct device *dev)
{
	struct dw_i3c_master *master = dev_get_drvdata(dev);
	int ret = 0;

	ret = master->xfer_host_ops->suspend(master->xfer_host_pdev);
	if (ret)
		dev_err(dev, "failed to suspend xfer host.\n");

	dw_i3c_master_disable(master);

	reset_control_assert(master->core_rst);
	dw_i3c_master_disable_clks(master);
	pinctrl_pm_select_sleep_state(dev);

	if (master->io_vreg_dev) {
		ret = regulator_disable(master->io_vreg_dev);
		if (ret)
			dev_err(dev, "failed to disable camio regulator\n");
	}

	return ret;
}

static int __maybe_unused dw_i3c_master_runtime_resume(struct device *dev)
{
	struct dw_i3c_master *master = dev_get_drvdata(dev);
	int ret = 0;

	if (master->io_vreg_dev) {
		ret = regulator_enable(master->io_vreg_dev);
		if (ret)
			dev_err(dev, "Fail to enable camio regulator\n");
	}

	pinctrl_pm_select_default_state(dev);
	dw_i3c_master_enable_clks(master);
	reset_control_deassert(master->core_rst);

	dw_i3c_master_restore_timing_regs(master);
	dw_i3c_master_restore_addrs(master);
	dw_i3c_master_enable(master);

	ret = master->xfer_host_ops->resume(master->xfer_host_pdev);

	return ret;
}

static const struct dev_pm_ops dw_i3c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dw_i3c_master_runtime_suspend, dw_i3c_master_runtime_resume, NULL)
};

static const struct of_device_id dw_i3c_master_of_match[] = {
	{ .compatible = "snps,dw-i3c-master-thi", },
	{},
};
MODULE_DEVICE_TABLE(of, dw_i3c_master_of_match);

static struct platform_driver dw_i3c_driver = {
	.probe = dw_i3c_probe,
	.remove_new = dw_i3c_remove,
	.driver = {
		.name = "dw-i3c-master-thi",
		.of_match_table = dw_i3c_master_of_match,
		.pm = &dw_i3c_pm_ops,
	},
};
module_platform_driver(dw_i3c_driver);

MODULE_AUTHOR("Vitor Soares <vitor.soares@synopsys.com>");
MODULE_DESCRIPTION("DesignWare MIPI I3C driver");
MODULE_LICENSE("GPL v2");
