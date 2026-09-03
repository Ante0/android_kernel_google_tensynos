// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.
 *
 * Author: Vitor Soares <vitor.soares@synopsys.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <aoss-ssr-notifier/aoss_ssr_notifier.h>

#include "dw-i3c-core.h"

#define CREATE_TRACE_POINTS
#include "dw-i3c-master-trace.h"

static const unsigned int regs2read[] = { DEVICE_CTRL, QUEUE_STATUS_LEVEL, DATA_BUFFER_STATUS_LEVEL,
					  SCL_I3C_OD_TIMING, SCL_I3C_PP_TIMING,
					  DEV_CHAR_TABLE_POINTER, INTR_STATUS, PRESENT_STATE };

static const char * const regs_names[] = { "DEV_CTL", "Q_STAT_LVL", "DAT_BUF_STAT_LVL",
					   "SCL_I3C_OD_TIMING", "SCL_I3C_PP_TIMING", "DCT_PTR",
					   "IR_STAT", "PRES_STAT" };

#define DUMP_REGS_NUM ARRAY_SIZE(regs2read)

static void dump_regs_locked(struct dw_i3c_master *master)
{
	BUILD_BUG_ON(ARRAY_SIZE(regs2read) != ARRAY_SIZE(regs_names));
	static DEFINE_RATELIMIT_STATE(_rs, DEFAULT_RATELIMIT_INTERVAL, DEFAULT_RATELIMIT_BURST);
	u32 dct_addr, dct_size, dct_devs_n, tmp;
	int i;

	if (!__ratelimit(&_rs))
		return;

	tmp = readl(master->regs + regs2read[DCT_PTR_I]);
	dct_addr = tmp & 0xFFF;
	dct_size = (tmp >> 12) & 0x7F;	/* Size in bytes */
	dct_devs_n = dct_size / 16;	/* 4 regs x 4 bytes (32 bits) = 16 bytes per device */

	dev_err(master->dev, "regs:");
	for (i = 0; i < DUMP_REGS_NUM; ++i)
		pr_cont("%s:%#X ", regs_names[i], readl(master->regs + regs2read[i]));
	pr_cont("\n");

	if (dct_devs_n > 0) {
		dev_err(master->dev, "dct-<i>:regs[0-3]");

		for (i = 0; i < dct_devs_n; ++i) {
			dev_err(master->dev, "%d:%#010X %#010X %#010X %#010X", i,
				readl(master->regs + DEV_CHARS_TABLE_LOC(dct_addr, i, 0)),
				readl(master->regs + DEV_CHARS_TABLE_LOC(dct_addr, i, 1)),
				readl(master->regs + DEV_CHARS_TABLE_LOC(dct_addr, i, 2)),
				readl(master->regs + DEV_CHARS_TABLE_LOC(dct_addr, i, 3)));
		}
	}
}

static u8 even_parity(u8 p)
{
	p ^= p >> 4;
	p &= 0xf;

	return (0x9669 >> p) & 1;
}

static bool dev_can_hdr_ddr(struct i3c_dev_desc *dev)
{
	return dev->info.hdr_cap & HDR_CAP_HDR_DDR_FIELD;
}

static void dw_i3c_master_xfer_push_cmd_queue_locked(struct dw_i3c_master *master,
						     u32 queue_status_level);
static void dw_i3c_master_xfer_wr_tx_data_locked(struct dw_i3c_master *master,
						 u32 buf_status_level);
static bool dw_i3c_master_xfer_pull_resp_queue_locked(struct dw_i3c_master *master,
						      u32 queue_status_level);
static void dw_i3c_master_xfer_rd_rx_data_locked(struct dw_i3c_master *master,
						 u32 buf_status_level);
static void dw_i3c_master_end_xfer_locked(struct dw_i3c_master *master);
static void dw_i3c_master_dequeue_xfer_locked(struct dw_i3c_master *master,
					      struct dw_i3c_xfer *xfer);
static void dw_i3c_master_reinit(struct dw_i3c_master *master);

static bool dw_i3c_xfer_finished(struct dw_i3c_xfer *xfer)
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

static bool dw_i3c_xfer_error(struct dw_i3c_xfer *xfer, u32 resp)
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

static int dw_i3c_xfer_suspend_inc(struct dw_i3c_master *master)
{
	unsigned long flags;
	long timeout = msecs_to_jiffies(I3C_SUSPEND_LOCK_TIMEOUT_MS);

	// Use a loop to handle the race condition. Suspended flag can get set by different thread
	// after wait_event_timeout detected that it's false but before we increment the counter.
	spin_lock_irqsave(&master->suspend.lock, flags);
	while (master->suspend.suspended) {
		spin_unlock_irqrestore(&master->suspend.lock, flags);

		timeout = wait_event_timeout(master->suspend.wait_queue,
					    !master->suspend.suspended,
					     timeout);
		if (timeout == 0) {
			dev_warn(master->dev,
				"transaction timed out on wait while I3C master is suspended\n");
			return -ESHUTDOWN;
		}

		spin_lock_irqsave(&master->suspend.lock, flags);
	}

	// Now we are sure the device is not suspended under the lock,
	// so we can safely increment the counter.
	master->suspend.xfers_count++;
	spin_unlock_irqrestore(&master->suspend.lock, flags);

	wake_up_all(&master->suspend.wait_queue);
	return 0;
}

static void dw_i3c_xfer_suspend_dec(struct dw_i3c_master *master)
{
	unsigned long flags;

	spin_lock_irqsave(&master->suspend.lock, flags);
	master->suspend.xfers_count--;
	spin_unlock_irqrestore(&master->suspend.lock, flags);

	wake_up_all(&master->suspend.wait_queue);
}

static void dw_i3c_mark_suspended(struct dw_i3c_master *master)
{
	unsigned long flags;

	spin_lock_irqsave(&master->suspend.lock, flags);
	master->suspend.suspended = true;
	spin_unlock_irqrestore(&master->suspend.lock, flags);

	wake_up_all(&master->suspend.wait_queue);
	wait_event(master->suspend.wait_queue, master->suspend.xfers_count == 0);
}

static void dw_i3c_mark_resumed(struct dw_i3c_master *master)
{
	unsigned long flags;

	spin_lock_irqsave(&master->suspend.lock, flags);
	master->suspend.suspended = false;
	spin_unlock_irqrestore(&master->suspend.lock, flags);

	wake_up_all(&master->suspend.wait_queue);
}

static void i3c_google_start_aoss_ssr(struct dw_i3c_master *master)
{
	dev_info(master->dev, "Starting AOSS SSR\n");

	i2c_mark_adapter_suspended(&master->base.i2c);
	dw_i3c_mark_suspended(master);
	pm_runtime_force_suspend(master->dev);
}

static void i3c_google_finish_aoss_ssr(struct dw_i3c_master *master)
{
	dev_info(master->dev, "Finishing AOSS SSR\n");

	pm_runtime_force_resume(master->dev);
	dw_i3c_mark_resumed(master);
	i2c_mark_adapter_resumed(&master->base.i2c);
}

static int aoss_ssr_read(void *data, u64 *val)
{
	*val = ((struct dw_i3c_master *)data)->aoss_ssr_started;
	return 0;
}

static int aoss_ssr_write(void *data, u64 val)
{
	struct dw_i3c_master *master = data;

	if (val && !master->aoss_ssr_started)
		i3c_google_start_aoss_ssr(master);
	else if (!val && master->aoss_ssr_started)
		i3c_google_finish_aoss_ssr(master);

	master->aoss_ssr_started = val;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(aoss_ssr_fops, aoss_ssr_read, aoss_ssr_write, "%llu\n");

static int clocks_info_show(struct seq_file *s, void *data)
{
	struct dw_i3c_master *master = s->private;
	unsigned long core_rate;
	u32 reg_pp, reg_od, reg_ext, reg_fmp, reg_fm, reg_sda_hold;
	u32 pp_hcnt, pp_lcnt;
	u32 od_hcnt, od_lcnt;
	u32 ext_lcnt_1, ext_lcnt_2, ext_lcnt_3, ext_lcnt_4;
	u32 fmp_hcnt, fmp_lcnt;
	u32 fm_hcnt, fm_lcnt;
	u32 sda_tx_hold, sda_pp_od_switch_dly, sda_od_pp_switch_dly;
	u32 sda_tx_hold_ns, sda_pp_od_switch_dly_ns, sda_od_pp_switch_dly_ns;
	u32 rise_ns = master->i2c_timings.scl_rise_ns;
	u64 freq_pp = 0, freq_od = 0;
	u64 freq_sdr1 = 0, freq_sdr2 = 0, freq_sdr3 = 0, freq_sdr4 = 0;
	u64 freq_i2c_fm = 0, freq_i2c_fmp = 0;
	u64 period_ps;
	int ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "cannot resume i3c bus master, err: %d\n", ret);
		return ret;
	}

	core_rate = clk_get_rate(master->core_clk);
	if (!core_rate) {
		dev_err(master->dev, "failed to get core clock rate\n");
		pm_runtime_mark_last_busy(master->dev);
		pm_runtime_put_autosuspend(master->dev);
		return -EINVAL;
	}

	reg_pp = readl(master->regs + SCL_I3C_PP_TIMING);
	pp_hcnt = SCL_I3C_TIMING_HCNT_VAL(reg_pp);
	pp_lcnt = SCL_I3C_TIMING_LCNT_VAL(reg_pp);

	reg_od = readl(master->regs + SCL_I3C_OD_TIMING);
	od_hcnt = SCL_I3C_TIMING_HCNT_VAL(reg_od);
	od_lcnt = SCL_I3C_TIMING_LCNT_VAL(reg_od);

	reg_ext = readl(master->regs + SCL_EXT_LCNT_TIMING);
	ext_lcnt_1 = reg_ext & 0xff;
	ext_lcnt_2 = (reg_ext >> 8) & 0xff;
	ext_lcnt_3 = (reg_ext >> 16) & 0xff;
	ext_lcnt_4 = (reg_ext >> 24) & 0xff;

	reg_fmp = readl(master->regs + SCL_I2C_FMP_TIMING);
	fmp_hcnt = SCL_I2C_FMP_TIMING_HCNT_VAL(reg_fmp);
	fmp_lcnt = SCL_I2C_FMP_TIMING_LCNT_VAL(reg_fmp);

	reg_fm = readl(master->regs + SCL_I2C_FM_TIMING);
	fm_hcnt = SCL_I2C_FM_TIMING_HCNT_VAL(reg_fm);
	fm_lcnt = SCL_I2C_FM_TIMING_LCNT_VAL(reg_fm);

	reg_sda_hold = readl(master->regs + SDA_HOLD_SWITCH_DLY_TIMING);

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);

	sda_tx_hold = SDA_TX_HOLD_VAL(reg_sda_hold);
	sda_pp_od_switch_dly = SDA_PP_OD_SWITCH_DLY_VAL(reg_sda_hold);
	sda_od_pp_switch_dly = SDA_OD_PP_SWITCH_DLY_VAL(reg_sda_hold);

	sda_tx_hold_ns = DIV_ROUND_CLOSEST_ULL((u64)sda_tx_hold * NSEC_PER_SEC, core_rate);
	sda_pp_od_switch_dly_ns = DIV_ROUND_CLOSEST_ULL((u64)sda_pp_od_switch_dly * NSEC_PER_SEC,
							core_rate);
	sda_od_pp_switch_dly_ns = DIV_ROUND_CLOSEST_ULL((u64)sda_od_pp_switch_dly * NSEC_PER_SEC,
							core_rate);

	if (pp_hcnt + pp_lcnt)
		freq_pp = DIV_ROUND_CLOSEST_ULL((u64)core_rate, pp_hcnt + pp_lcnt);

	if (od_hcnt + od_lcnt)
		freq_od = DIV_ROUND_CLOSEST_ULL((u64)core_rate, od_hcnt + od_lcnt);

	if (pp_hcnt + ext_lcnt_1)
		freq_sdr1 = DIV_ROUND_CLOSEST_ULL((u64)core_rate, pp_hcnt + ext_lcnt_1);

	if (pp_hcnt + ext_lcnt_2)
		freq_sdr2 = DIV_ROUND_CLOSEST_ULL((u64)core_rate, pp_hcnt + ext_lcnt_2);

	if (pp_hcnt + ext_lcnt_3)
		freq_sdr3 = DIV_ROUND_CLOSEST_ULL((u64)core_rate, pp_hcnt + ext_lcnt_3);

	if (pp_hcnt + ext_lcnt_4)
		freq_sdr4 = DIV_ROUND_CLOSEST_ULL((u64)core_rate, pp_hcnt + ext_lcnt_4);

	if (fm_hcnt + fm_lcnt) {
		period_ps = DIV_ROUND_CLOSEST_ULL((u64)(fm_hcnt + fm_lcnt) * 1000000000000ULL,
						  core_rate);
		period_ps += (u64)rise_ns * 1000ULL;
		if (period_ps)
			freq_i2c_fm = DIV64_U64_ROUND_CLOSEST(1000000000000ULL, period_ps);
	}

	if (fmp_hcnt + fmp_lcnt) {
		period_ps = DIV_ROUND_CLOSEST_ULL((u64)(fmp_hcnt + fmp_lcnt) * 1000000000000ULL,
						  core_rate);
		period_ps += (u64)rise_ns * 1000ULL;
		if (period_ps)
			freq_i2c_fmp = DIV64_U64_ROUND_CLOSEST(1000000000000ULL, period_ps);
	}

	seq_printf(s, "core_clk: %lu Hz\n", core_rate);
	seq_printf(s, "apb_clk: %lu Hz\n\n", clk_get_rate(master->apb_clk));

	seq_puts(s, "I3C Push-Pull (PP):\n");
	seq_printf(s, "  Registers: HCNT=%u, LCNT=%u\n", pp_hcnt, pp_lcnt);
	seq_printf(s, "  Calculated Frequency: %llu Hz\n\n", freq_pp);

	seq_puts(s, "I3C Open-Drain (OD):\n");
	seq_printf(s, "  Registers: HCNT=%u, LCNT=%u\n", od_hcnt, od_lcnt);
	seq_printf(s, "  Calculated Frequency: %llu Hz\n\n", freq_od);

	seq_puts(s, "I3C SDR1 (8 MHz Target):\n");
	seq_printf(s, "  Registers: PP_HCNT=%u, SDR1_LCNT=%u\n", pp_hcnt, ext_lcnt_1);
	seq_printf(s, "  Calculated Frequency: %llu Hz\n\n", freq_sdr1);

	seq_puts(s, "I3C SDR2 (6 MHz Target):\n");
	seq_printf(s, "  Registers: PP_HCNT=%u, SDR2_LCNT=%u\n", pp_hcnt, ext_lcnt_2);
	seq_printf(s, "  Calculated Frequency: %llu Hz\n\n", freq_sdr2);

	seq_puts(s, "I3C SDR3 (4 MHz Target):\n");
	seq_printf(s, "  Registers: PP_HCNT=%u, SDR3_LCNT=%u\n", pp_hcnt, ext_lcnt_3);
	seq_printf(s, "  Calculated Frequency: %llu Hz\n\n", freq_sdr3);

	seq_puts(s, "I3C SDR4 (2 MHz Target):\n");
	seq_printf(s, "  Registers: PP_HCNT=%u, SDR4_LCNT=%u\n", pp_hcnt, ext_lcnt_4);
	seq_printf(s, "  Calculated Frequency: %llu Hz\n\n", freq_sdr4);

	seq_puts(s, "Legacy I2C Fast Mode (FM) (400 kHz Target):\n");
	seq_printf(s, "  Registers: HCNT=%u, LCNT=%u\n", fm_hcnt, fm_lcnt);
	seq_printf(s, "  Configured Rise Time: %u ns\n", rise_ns);
	seq_printf(s, "  Calculated Frequency: %llu Hz\n\n", freq_i2c_fm);

	seq_puts(s, "Legacy I2C Fast Mode Plus (FMP) (1 MHz Target):\n");
	seq_printf(s, "  Registers: HCNT=%u, LCNT=%u\n", fmp_hcnt, fmp_lcnt);
	seq_printf(s, "  Configured Rise Time: %u ns\n", rise_ns);
	seq_printf(s, "  Calculated Frequency: %llu Hz\n\n", freq_i2c_fmp);

	seq_puts(s, "SDA Hold and Switch Delay Timing:\n");
	seq_printf(s, "  Registers: TX_HOLD=%u, PP_OD_SWITCH_DLY=%u, OD_PP_SWITCH_DLY=%u\n",
		   sda_tx_hold, sda_pp_od_switch_dly, sda_od_pp_switch_dly);
	seq_printf(s, "  SDA TX Hold: %u ns\n", sda_tx_hold_ns);
	seq_printf(s, "  SDA PP to OD Switch Delay: %u ns\n", sda_pp_od_switch_dly_ns);
	seq_printf(s, "  SDA OD to PP Switch Delay: %u ns\n", sda_od_pp_switch_dly_ns);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(clocks_info);

static void dw_i3c_debugfs_init(struct dw_i3c_master *master)
{
	struct dentry *tmp;

	master->debugfs = debugfs_create_dir(dev_name(master->dev), NULL);
	if (IS_ERR_OR_NULL(master->debugfs)) {
		dev_err(master->dev, "failed to create debugfs directory: %ld\n",
			PTR_ERR(master->debugfs));
		return;
	}

	tmp = debugfs_create_file("trigger-aoss-ssr", 0644, master->debugfs,
				 master, &aoss_ssr_fops);
	if (IS_ERR_OR_NULL(tmp)) {
		dev_err(master->dev, "failed to create debugfs trigger-aoss-ssr file: %ld\n",
			PTR_ERR(tmp));
		debugfs_remove_recursive(master->debugfs);
		master->debugfs = NULL;
		return;
	}

	tmp = debugfs_create_file("clocks-info", 0444, master->debugfs,
				 master, &clocks_info_fops);
	if (IS_ERR_OR_NULL(tmp)) {
		dev_err(master->dev, "failed to create debugfs clocks-info file: %ld\n",
			PTR_ERR(tmp));
		debugfs_remove_recursive(master->debugfs);
		master->debugfs = NULL;
		return;
	}
}

static void aoss_ssr_work_func(struct work_struct *work)
{
	struct dw_i3c_master *master =
		container_of(work, struct dw_i3c_master, aoss_ssr_work.work);

	dev_warn(master->dev, "AOSS_SSR_ONLINE timeout. AOSS SSR %s\n",
		 master->aoss_ssr_started ? "active" : "inactive");
	if (master->aoss_ssr_started) {
		i3c_google_finish_aoss_ssr(master);
		master->aoss_ssr_started = false;
	}
}

static int aoss_ssr_notifier(struct notifier_block *notifier, unsigned long event, void *v)
{
	struct dw_i3c_master *master = container_of(notifier, struct dw_i3c_master, aoss_ssr_nb);

	switch (event) {
	case AOSS_SSR_PG_DOWN:
		dev_info(master->dev, "PG down. AOSS SSR %s\n",
			 master->aoss_ssr_started ? "active" : "inactive");
		if (!master->aoss_ssr_started) {
			i3c_google_start_aoss_ssr(master);
			master->aoss_ssr_started = true;
		}
		schedule_delayed_work(&master->aoss_ssr_work,
				      msecs_to_jiffies(AOSS_SSR_ONLINE_TIMEOUT_MS));
		break;
	case AOSS_SSR_ONLINE:
		dev_info(master->dev, "Online. AOSS SSR %s\n",
			 master->aoss_ssr_started ? "active" : "inactive");
		cancel_delayed_work_sync(&master->aoss_ssr_work);
		if (master->aoss_ssr_started) {
			i3c_google_finish_aoss_ssr(master);
			master->aoss_ssr_started = false;
		}
		break;
	default:
		dev_info(master->dev, "don't care\n");
		break;
	}

	return NOTIFY_OK;
}

/* return true : xfer timeout, false : xfer completed.*/
static bool dw_i3c_wait_for_xfer_completion(struct dw_i3c_xfer *xfer, bool busy_wait_mode)
{
	if (busy_wait_mode) {
		unsigned long start_time = ktime_get_ns();
		/* If polling timeout, fallback to wait_for_completion. */
		while (ktime_get_ns() - start_time < BUSY_WAIT_XFER_TIMEOUT_NS) {
			if (completion_done(&xfer->comp))
				return false;
			udelay(2);
		}
	}

	return wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT) == 0;
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
	case JEDEC_CCC_DEVCTRL:
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

static void dw_i3c_master_wr_tx_fifo(struct dw_i3c_master *master,
				     const u8 *bytes, int nbytes)
{
	writesl(master->regs + RX_TX_DATA_PORT, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, bytes + (nbytes & ~3), nbytes & 3);
		writesl(master->regs + RX_TX_DATA_PORT, &tmp, 1);
	}
}

static void dw_i3c_master_read_fifo(struct dw_i3c_master *master,
				    int reg,  u8 *bytes, int nbytes)
{
	readsl(master->regs + reg, bytes, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		readsl(master->regs + reg, &tmp, 1);
		memcpy(bytes + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

static void dw_i3c_master_read_rx_fifo(struct dw_i3c_master *master,
				       u8 *bytes, int nbytes)
{
	return dw_i3c_master_read_fifo(master, RX_TX_DATA_PORT, bytes, nbytes);
}

static void dw_i3c_master_read_ibi_fifo(struct dw_i3c_master *master,
					u8 *bytes, int nbytes)
{
	return dw_i3c_master_read_fifo(master, IBI_QUEUE_STATUS, bytes, nbytes);
}

static bool dw_i3c_master_msg_rnw(struct dw_i3c_xfer *xfer, int idx)
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

static u16 dw_i3c_master_get_msg_len(struct dw_i3c_xfer *xfer, int idx)
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

static u8 *dw_i3c_master_get_msg_rx_buf(struct dw_i3c_xfer *xfer, int idx)
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

static const u8 *dw_i3c_master_get_msg_tx_buf(struct dw_i3c_xfer *xfer, int idx)
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

static void
dw_i3c_master_program_xfer_irq_thld_locked(struct dw_i3c_master *master)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	u32 queue_thld_ctrl, data_thld_ctrl, intr_signal_en;
	unsigned int cmd_len, resp_len;

	/* Since every command takes two locations in the command and response buffers,
	 * the FIFO depth has to be divided by two to get the number of commands it can take.
	 */
	cmd_len = umin(master->caps.cmdfifodepth / 2, 2 * (xfer->ncmds - xfer->cmd_idx));
	resp_len = umin(master->caps.cmdfifodepth / 2, xfer->ncmds - xfer->resp_idx);

	intr_signal_en = readl(master->regs + INTR_SIGNAL_EN);
	intr_signal_en |= INTR_XFER_MASK;

	queue_thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
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

	data_thld_ctrl = readl(master->regs + DATA_BUFFER_THLD_CTRL);
	data_thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_TX_BUF_MASK;
	data_thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_RX_BUF_MASK;
	data_thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_TX_START_THLD_MASK;
	data_thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_RX_START_THLD_MASK;

	if (!xfer->using_dma) {
		if (xfer->res_tx_entries >= 16)
			data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_TX_BUF(INTR_DATA_BUF_THLD_16);
		else if (xfer->res_tx_entries >= 8)
			data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_TX_BUF(INTR_DATA_BUF_THLD_8);
		else if (xfer->res_tx_entries >= 4)
			data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_TX_BUF(INTR_DATA_BUF_THLD_4);
		else if (xfer->res_tx_entries >= 1)
			data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_TX_BUF(INTR_DATA_BUF_THLD_1);
		else
			intr_signal_en &= ~INTR_TX_THLD_STAT;

		if (xfer->res_rx_entries >= 16)
			data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_RX_BUF(INTR_DATA_BUF_THLD_16);
		else if (xfer->res_rx_entries >= 8)
			data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_RX_BUF(INTR_DATA_BUF_THLD_8);
		else if (xfer->res_rx_entries >= 4)
			data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_RX_BUF(INTR_DATA_BUF_THLD_4);
		else if (xfer->res_rx_entries >= 1)
			data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_RX_BUF(INTR_DATA_BUF_THLD_1);
		else
			intr_signal_en &= ~INTR_RX_THLD_STAT;
	} else {
		intr_signal_en &= ~INTR_TX_THLD_STAT;
		intr_signal_en &= ~INTR_RX_THLD_STAT;
	}

	data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_TX_START_THLD(TX_START_THLD_GOOG_DEFAULT);
	data_thld_ctrl |= DATA_BUFFER_THLD_CTRL_RX_START_THLD(RX_START_THLD_GOOG_DEFAULT);

	writel(intr_signal_en, master->regs + INTR_SIGNAL_EN);
	writel(queue_thld_ctrl, master->regs + QUEUE_THLD_CTRL);
	writel(data_thld_ctrl, master->regs + DATA_BUFFER_THLD_CTRL);
}

static void dw_i3c_master_update_i2c_dct(struct dw_i3c_master *master,
					 struct dw_i3c_xfer *xfer)
{
	if (!master->is_i2c_slot_shared)
		return;

	writel(DEV_ADDR_TABLE_LEGACY_I2C_DEV |
		DEV_ADDR_TABLE_STATIC_ADDR(xfer->dev_data.i2c_addr),
		master->regs +
		DEV_ADDR_TABLE_LOC(master->datstartaddr, master->shared_i2c_pos));
}

static void dw_i3c_master_dma_callback(void *data)
{
	struct dw_i3c_master *master = data;
	struct dw_i3c_xfer *xfer;
	enum dma_status status;
	struct dma_chan *chan;
	unsigned long flags;

	spin_lock_irqsave(&master->xferqueue.lock, flags);

	xfer = master->xferqueue.cur;
	if (!xfer || !xfer->using_dma) {
		dev_err(master->dev, "dma callback on non-dma xfer?");
		goto dma_out;
	}

	chan = (xfer->msgs.i3c_msgs->rnw) ? master->chan_rx : master->chan_tx;

	status = dmaengine_tx_status(chan, chan->cookie, NULL);
	if (status == DMA_COMPLETE) {
		xfer->dma_done = true;
	} else {
		dev_err(master->dev, "dmaengine err %d\n", status);
		xfer->ret = -EIO;
	}

	complete(&xfer->dma_comp);

dma_out:
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void dw_i3c_master_dma_enable(struct dw_i3c_master *master)
{
	writel(readl(master->regs + DEVICE_CTRL) | DEV_CTRL_DMA_ENABLE,
	       master->regs + DEVICE_CTRL);
}

static void dw_i3c_master_dma_disable(struct dw_i3c_master *master)
{
	writel(readl(master->regs + DEVICE_CTRL) & ~DEV_CTRL_DMA_ENABLE,
	       master->regs + DEVICE_CTRL);
}

static void dw_i3c_master_dma_terminate(struct dw_i3c_master *master, struct dw_i3c_xfer *xfer)
{
	bool rnw;
	unsigned long flags;

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	rnw = xfer->msgs.i3c_msgs->rnw;
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);

	if (rnw)
		dmaengine_terminate_sync(master->chan_rx);
	else
		dmaengine_terminate_sync(master->chan_tx);
}

static void dw_i3c_master_dma_cleanup(struct dw_i3c_master *master, struct dw_i3c_xfer *xfer)
{
	struct dma_chan *chan;
	enum dma_data_direction data_dir;
	const struct i3c_priv_xfer *msg;
	size_t len;

	if (!xfer || !xfer->using_dma) {
		dev_err(master->dev, "dma cleanup on non-dma xfer?");
		return;
	}

	msg = xfer->msgs.i3c_msgs;
	len = msg->len;

	if (msg->rnw) {
		chan = master->chan_rx;
		data_dir = DMA_FROM_DEVICE;
	} else {
		chan = master->chan_tx;
		data_dir = DMA_TO_DEVICE;
	}

	dma_unmap_single(chan->device->dev, xfer->dma_addr, len, data_dir);
	dw_i3c_master_dma_disable(master);
}

static int dw_i3c_master_start_dma_locked(struct dw_i3c_master *master, struct dw_i3c_xfer *xfer)
{
	struct scatterlist sg;
	dma_addr_t dma_addr;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *chan;
	enum dma_data_direction data_dir;
	enum dma_transfer_direction xfer_dir;
	void *buf;
	const struct i3c_priv_xfer *msg = xfer->msgs.i3c_msgs;
	u16 len = msg->len;

	if (msg->rnw) {
		/* Read */
		chan = master->chan_rx;
		data_dir = DMA_FROM_DEVICE;
		xfer_dir = DMA_DEV_TO_MEM;
		buf = msg->data.in;
	} else {
		/* Write */
		chan = master->chan_tx;
		data_dir = DMA_TO_DEVICE;
		xfer_dir = DMA_MEM_TO_DEV;
		buf = (void *)msg->data.out;
	}

	dma_addr = dma_map_single(chan->device->dev, buf, len, data_dir);
	if (dma_mapping_error(chan->device->dev, dma_addr)) {
		dev_err(chan->device->dev, "dma map failed\n");
		goto err_out;
	}

	sg_init_table(&sg, 1);
	sg_dma_len(&sg) = len;
	sg_dma_address(&sg) = dma_addr;
	xfer->dma_addr = dma_addr;

	desc = dmaengine_prep_slave_sg(chan, &sg, 1, xfer_dir, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_err(chan->device->dev, "DMA prep_slave_sg failed\n");
		goto err_out;
	}

	desc->callback = dw_i3c_master_dma_callback;
	desc->callback_param = master;

	if (dma_submit_error(dmaengine_submit(desc))) {
		dev_err(chan->device->dev, "DMA submit failed\n");
		goto err_out;
	}

	dw_i3c_master_dma_enable(master);

	spin_unlock_irq(&master->xferqueue.lock);
	dma_async_issue_pending(chan);
	spin_lock_irq(&master->xferqueue.lock);

	return 0;

err_out:
	complete(&xfer->comp);
	complete(&xfer->dma_comp);
	xfer->ret = -ETIMEDOUT;	/* error handling behaviour: start dma fail == xfer timeout */
	return -EIO;
}

static void dw_i3c_master_start_xfer_locked(struct dw_i3c_master *master)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	u32 buf_status_level, queue_status_level;

	if (!xfer)
		return;

	if (master->is_i2c_slot_shared && xfer->type == XFER_TYPE_I2C)
		dw_i3c_master_update_i2c_dct(master, xfer);

	if (!xfer->using_dma) {
		buf_status_level = readl(master->regs + DATA_BUFFER_STATUS_LEVEL);
		dw_i3c_master_xfer_wr_tx_data_locked(master, buf_status_level);
	} else if (dw_i3c_master_start_dma_locked(master, xfer)) {
		return;
	}

	queue_status_level = readl(master->regs + QUEUE_STATUS_LEVEL);
	dw_i3c_master_xfer_push_cmd_queue_locked(master, queue_status_level);

	dw_i3c_master_program_xfer_irq_thld_locked(master);
}

static void dw_i3c_master_enqueue_xfer(struct dw_i3c_master *master,
				       struct dw_i3c_xfer *xfer)
{
	unsigned long flags;

	trace_dw_i3c_xfer_queue(master, xfer);

	INIT_LIST_HEAD(&xfer->node);
	xfer->ret = -ETIMEDOUT;
	xfer->cmd_idx = 0;
	xfer->resp_idx = 0;
	xfer->tx_cmd_idx = 0;
	xfer->rx_cmd_idx = 0;
	xfer->tx_buf_cursor = 0;
	xfer->rx_buf_cursor = 0;
	init_completion(&xfer->comp);

	if (xfer->using_dma)
		init_completion(&xfer->dma_comp);

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	if (master->xferqueue.cur) {
		list_add_tail(&xfer->node, &master->xferqueue.list);
	} else {
		master->xferqueue.cur = xfer;
		dw_i3c_master_start_xfer_locked(master);
	}
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void dw_i3c_master_dequeue_xfer_locked(struct dw_i3c_master *master,
					      struct dw_i3c_xfer *xfer)
{
	trace_dw_i3c_xfer_dequeue(master, xfer);

	dev_err(&master->base.dev, "%s: error detected %d, executing soft reset to recover\n",
		__func__, xfer->ret);
	dump_regs_locked(master);

	if (master->xferqueue.cur == xfer) {
		u32 status;

		master->xferqueue.cur = NULL;

		writel(RESET_CTRL_SOFT, master->regs + RESET_CTRL);
		readl_poll_timeout_atomic(master->regs + RESET_CTRL, status, !status, 10, 1000);
		dw_i3c_master_reinit(master);
	} else {
		list_del_init(&xfer->node);
	}
}

static void dw_i3c_master_dequeue_xfer(struct dw_i3c_master *master,
				       struct dw_i3c_xfer *xfer)
{
	unsigned long flags;

	spin_lock_irqsave(&master->xferqueue.lock, flags);
	dw_i3c_master_dequeue_xfer_locked(master, xfer);
	spin_unlock_irqrestore(&master->xferqueue.lock, flags);
}

static void dw_i3c_master_end_xfer_locked(struct dw_i3c_master *master)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	u32 intr_signal_en;

	trace_dw_i3c_xfer_done(master, xfer);

	if (!xfer)
		return;

	intr_signal_en = readl(master->regs + INTR_SIGNAL_EN);
	intr_signal_en &= (~INTR_XFER_MASK);
	writel(intr_signal_en, master->regs + INTR_SIGNAL_EN);

	if (xfer->ret < 0) {
		dw_i3c_master_dequeue_xfer_locked(master, xfer);
		writel(master->dev_ctrl | DEV_CTRL_RESUME | DEV_CTRL_ENABLE,
		       master->regs + DEVICE_CTRL);
		dev_err(&master->base.dev, "xfer failed with ret value: %d\n", xfer->ret);
	}

	complete(&xfer->comp);

	xfer = list_first_entry_or_null(&master->xferqueue.list,
					struct dw_i3c_xfer,
					node);
	if (xfer)
		list_del_init(&xfer->node);

	master->xferqueue.cur = xfer;
	dw_i3c_master_start_xfer_locked(master);
}

static void dw_i3c_master_set_intr_regs(struct dw_i3c_master *master)
{
	u32 thld_ctrl;

	thld_ctrl = readl(master->regs + QUEUE_THLD_CTRL);
	thld_ctrl &= ~(QUEUE_THLD_CTRL_RESP_BUF_MASK |
		       QUEUE_THLD_CTRL_IBI_STAT_MASK |
		       QUEUE_THLD_CTRL_IBI_STAT_MASK);
	thld_ctrl |= QUEUE_THLD_CTRL_IBI_STAT(1) |
		     QUEUE_THLD_CTRL_IBI_DATA(31);
	writel(thld_ctrl, master->regs + QUEUE_THLD_CTRL);

	thld_ctrl = readl(master->regs + DATA_BUFFER_THLD_CTRL);
	thld_ctrl &= ~DATA_BUFFER_THLD_CTRL_RX_BUF_MASK;
	writel(thld_ctrl, master->regs + DATA_BUFFER_THLD_CTRL);

	writel(INTR_ALL, master->regs + INTR_STATUS);
	writel(INTR_ALL, master->regs + INTR_STATUS_EN);
	writel(0, master->regs + INTR_SIGNAL_EN);

	writel(IBI_REQ_REJECT_ALL, master->regs + IBI_SIR_REQ_REJECT);
	writel(IBI_REQ_REJECT_ALL, master->regs + IBI_MR_REQ_REJECT);

	if (readl(master->regs + I3C_VER_ID) == I3C_VER_101) {
		u32 val = readl(master->regs + SCL_EXT_TERMN_LCNT_TIMING);

		val &= ~STOP_HLD_CNT_MASK;
		writel(val, master->regs + SCL_EXT_TERMN_LCNT_TIMING);

		val =  SDA_OD_PP_SWITCH_DLY(1) | SDA_PP_OD_SWITCH_DLY(1) | SDA_TX_HOLD(1);
		writel(val, master->regs + SDA_HOLD_SWITCH_DLY_TIMING);
	}
}

static int dw_i3c_clk_cfg(struct dw_i3c_master *master)
{
	unsigned long core_rate, core_period;
	u32 scl_timing;
	u8 hcnt, lcnt;

	core_rate = clk_get_rate(master->core_clk);
	if (!core_rate || core_rate > NSEC_PER_SEC)
		return -EINVAL;

	core_period = NSEC_PER_SEC / core_rate;

	hcnt = DIV_ROUND_UP(I3C_BUS_THIGH_MAX_NS, core_period) - 1;
	if (hcnt < SCL_I3C_TIMING_CNT_MIN)
		hcnt = SCL_I3C_TIMING_CNT_MIN;

	lcnt = DIV_ROUND_UP(core_rate, master->base.bus.scl_rate.i3c) - hcnt;
	if (lcnt < SCL_I3C_TIMING_CNT_MIN)
		lcnt = SCL_I3C_TIMING_CNT_MIN;

	scl_timing = SCL_I3C_TIMING_HCNT(hcnt) | SCL_I3C_TIMING_LCNT(lcnt);
	writel(scl_timing, master->regs + SCL_I3C_PP_TIMING);
	master->i3c_pp_timing = scl_timing;

	/*
	 * In pure i3c mode, MST_FREE represents tCAS. In shared mode, this
	 * will be set up by dw_i2c_clk_cfg as tLOW.
	 */
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

	core_rate = clk_get_rate(master->core_clk);
	if (!core_rate || core_rate > NSEC_PER_SEC)
		return -EINVAL;

	core_period = NSEC_PER_SEC / core_rate;

	lcnt = DIV_ROUND_UP(I3C_BUS_I2C_FMP_TLOW_MIN_NS + master->i2c_timings.scl_fall_ns,
			    core_period);
	hcnt = DIV_ROUND_UP(I3C_BUS_I2C_FMP_THIGH_MIN_NS + master->i2c_timings.sda_fall_ns,
			    core_period);
	master->i2c_fmp_timing = SCL_I2C_FMP_TIMING_HCNT(hcnt) | SCL_I2C_FMP_TIMING_LCNT(lcnt);
	master->i3c_od_i2c_timing = SCL_I2C_OD_TIMING_HCNT(hcnt) | SCL_I2C_OD_TIMING_LCNT(lcnt);
	writel(master->i2c_fmp_timing, master->regs + SCL_I2C_FMP_TIMING);

	lcnt = DIV_ROUND_UP(I3C_BUS_I2C_FM_TLOW_MIN_NS, core_period);
	hcnt = DIV_ROUND_UP(core_rate, I3C_BUS_I2C_FM_SCL_RATE) - lcnt;
	master->i2c_fm_timing = SCL_I2C_FM_TIMING_HCNT(hcnt) | SCL_I2C_FM_TIMING_LCNT(lcnt);
	writel(master->i2c_fm_timing, master->regs + SCL_I2C_FM_TIMING);

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
	case I3C_BUS_MODE_MIXED_SLOW:
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

	dw_i3c_master_set_intr_regs(master);
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
	struct dw_i3c_xfer xfer = {};
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
		COMMAND_PORT_TOC | COMMAND_PORT_ROC |
		(master->pec_enable && ccc->id != JEDEC_CCC_DEVCTRL ? COMMAND_PORT_PEC : 0);

	xfer.res_rx_entries = 0;
	xfer.res_tx_entries =
		(ccc->dests[0].payload.len + DATA_BUF_ENTRY_SIZE - 1) /
		DATA_BUF_ENTRY_SIZE;
	xfer.msgs.ccc_msg = ccc;
	xfer.type = XFER_TYPE_CCC;
	xfer.ncmds = 1;

	dw_i3c_master_enqueue_xfer(master, &xfer);
	if (!wait_for_completion_timeout(&(xfer.comp), XFER_TIMEOUT))
		dw_i3c_master_dequeue_xfer(master, &xfer);

	ret = xfer.ret;
	if (RESPONSE_PORT_ERR_STATUS(xfer.ccc_resp) == RESPONSE_ERROR_IBA_NACK)
		ccc->err = I3C_ERROR_M2;

	return ret;
}

static int dw_i3c_ccc_get(struct dw_i3c_master *master, struct i3c_ccc_cmd *ccc)
{
	struct dw_i3c_xfer xfer = {};
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
			      COMMAND_PORT_ROC |
			      (master->pec_enable && ccc->id != JEDEC_CCC_DEVCTRL ?
			       COMMAND_PORT_PEC : 0);

	xfer.res_rx_entries =
		(ccc->dests[0].payload.len + DATA_BUF_ENTRY_SIZE - 1) /
		DATA_BUF_ENTRY_SIZE;
	xfer.res_tx_entries = 0;
	xfer.msgs.ccc_msg = ccc;

	xfer.type = XFER_TYPE_CCC;
	xfer.ncmds = 1;
	dw_i3c_master_enqueue_xfer(master, &xfer);
	if (!wait_for_completion_timeout(&(xfer.comp), XFER_TIMEOUT))
		dw_i3c_master_dequeue_xfer(master, &xfer);

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

	if (ccc->id == I3C_CCC_ENTDAA)
		return -EINVAL;

	ret = dw_i3c_xfer_suspend_inc(master);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev, "%s: cannot resume i3c bus master, err: %d\n", __func__, ret);
		return ret;
	}

	if (ccc->rnw)
		ret = dw_i3c_ccc_get(master, ccc);
	else
		ret = dw_i3c_ccc_set(master, ccc);

	dw_i3c_xfer_suspend_dec(master);
	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
	return ret;
}

static int dw_i3c_master_devctrl(struct i3c_master_controller *m)
{
	u8 payload[] = JEDEC_CCC_DEVCTRL_PAYLOAD;

	/* TODO: Payload is DMA-able */
	struct i3c_ccc_cmd_dest dest = {
		.addr = I3C_BROADCAST_ADDR,
		.payload = {
			.len = sizeof(payload),
			.data = payload
		}
	};

	struct i3c_ccc_cmd ccc = {
		.rnw = false,
		.id = JEDEC_CCC_DEVCTRL,
		.ndests = 1,
		.dests = &dest
	};

	return dw_i3c_master_send_ccc_cmd(m, &ccc);
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

static int dw_i3c_master_daa(struct i3c_master_controller *m)
{
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_xfer xfer = {};
	u32 olddevs, newdevs;
	u8 p, last_addr = 0;
	int ret, pos;

	ret = dw_i3c_xfer_suspend_inc(master);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(master->dev);
	if (ret < 0) {
		dev_err(master->dev,
			"%s: cannot resume i3c bus master, err: %d\n", __func__,
			ret);
		return ret;
	}

	if (master->pec_enable) {
		ret = dw_i3c_master_devctrl(m);
		if (ret)
			dev_err(master->dev, "%s: sending DEVCTRL for PEC fail, err: %d\n", __func__, ret);
	}

	olddevs = ~(master->free_pos);

	/* Prepare DAT before launching DAA. */
	for (pos = 0; pos < master->maxdevs; pos++) {
		if (olddevs & BIT(pos))
			continue;

		ret = i3c_master_get_free_addr(m, last_addr + 1);
		if (ret < 0) {
			ret = -ENOSPC;
			goto out;
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
		goto out;
	}

	if (master->daa_with_i2c_speed)
		writel(master->i3c_od_i2c_timing, master->regs + SCL_I3C_OD_TIMING);

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
	dw_i3c_master_enqueue_xfer(master, &xfer);
	if (!wait_for_completion_timeout(&(xfer.comp), XFER_TIMEOUT))
		dw_i3c_master_dequeue_xfer(master, &xfer);

	/* ENTDAA is expected to end with NACK, resume the controller */
	writel(master->dev_ctrl | DEV_CTRL_RESUME | DEV_CTRL_ENABLE,
	       master->regs + DEVICE_CTRL);

	newdevs = GENMASK(master->maxdevs - RESPONSE_PORT_DATA_LEN(xfer.ccc_resp) - 1, 0);
	newdevs &= ~olddevs;

	for (pos = 0; pos < master->maxdevs; pos++) {
		if (newdevs & BIT(pos))
			i3c_master_add_i3c_dev_locked(m, master->devs[pos].addr);
	}

out:
	if (master->daa_with_i2c_speed)
		dw_i3c_master_restore_timing_regs(master);

	dw_i3c_xfer_suspend_dec(master);
	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
	return ret;
}

static bool dma_valid_buffer(const void *buf, u16 len, u32 threshold, u8 align)
{
	return len > threshold && !is_vmalloc_addr(buf)
		&& IS_ALIGNED(len, align) && IS_ALIGNED((dma_addr_t)buf, align);
}

static bool can_xfer_dma(struct dw_i3c_master *master, struct dw_i3c_xfer *xfer)
{
	const struct i3c_priv_xfer *msg = xfer->msgs.i3c_msgs;

	/* Only private xfers for now! No CCCs or resps */
	if (xfer->ncmds != 1 || !master->can_dma || xfer->type != XFER_TYPE_I3C)
		return false;

	return dma_valid_buffer(msg->rnw ? msg->data.in : msg->data.out, msg->len, master->fifo_sz,
				master->dma_align);
}

/* return true : xfer timeout, false : xfer completed.*/
static bool dw_i3c_wait_for_dma_completion(struct dw_i3c_master *master, struct dw_i3c_xfer *xfer)
{
	unsigned long time_left, flags;

	time_left = wait_for_completion_timeout(&xfer->comp, XFER_TIMEOUT);

	if (!xfer->ret)
		time_left = wait_for_completion_timeout(&xfer->dma_comp, time_left);

	if (!xfer->ret && !xfer->dma_done)
		xfer->ret = -EIO;

	if (xfer->ret && xfer->ret != -ETIMEDOUT)
		dw_i3c_master_dma_terminate(master, xfer);	/* Error but no timeout */

	dw_i3c_master_dma_cleanup(master, xfer);

	if (xfer->ret != -ETIMEDOUT) {
		/* Success or error - but - DIDN'T timeout */
		spin_lock_irqsave(&master->xferqueue.lock, flags);
		dw_i3c_master_end_xfer_locked(master);
		spin_unlock_irqrestore(&master->xferqueue.lock, flags);
	}

	return xfer->ret == -ETIMEDOUT;
}

static int dw_i3c_master_priv_xfers(struct i3c_dev_desc *dev,
				    struct i3c_priv_xfer *i3c_xfers,
				    int i3c_nxfers)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	struct dw_i3c_xfer xfer = {};
	int i, ret = 0, total_bytes = 0;
	bool busy_wait_mode = false, timeout;

	if (!i3c_nxfers)
		return 0;

	ret = dw_i3c_xfer_suspend_inc(master);
	if (ret)
		return ret;

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
	xfer.using_dma = can_xfer_dma(master, &xfer);
	xfer.dma_done = false;

	for (i = 0; i < i3c_nxfers; i++) {
		total_bytes += (i3c_xfers[i].len + ADDRESS_BYTES);

		if (xfer.using_dma)
			continue;

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

	if (total_bytes >= master->i3c_threaded_threshold)
		xfer.in_bottom_half = true;

	trace_dw_i3c_xfers_start(master, i3c_nxfers, total_bytes);

	dw_i3c_master_enqueue_xfer(master, &xfer);

	if (!xfer.using_dma) {
		busy_wait_mode = (master->operation_mode == HYBRID_BUSY_WAIT_MODE) &&
			 (total_bytes < master->i3c_hybrid_threshold);

		timeout = dw_i3c_wait_for_xfer_completion(&xfer, busy_wait_mode);
	} else {
		timeout = dw_i3c_wait_for_dma_completion(master, &xfer);
	}

	ret = xfer.ret;

	if (timeout)
		dw_i3c_master_dequeue_xfer(master, &xfer);

	dw_i3c_xfer_suspend_dec(master);
	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);
	return ret;
}

static void dw_i3c_master_set_speed(struct i3c_dev_desc *dev,
				    struct dw_i3c_i2c_dev_data *data,
				    struct dw_i3c_master *master)
{
	if (master->hdr_ddr_capable && dev_can_hdr_ddr(dev)) {
		data->read_ds = I3C_HDR_DDR_MODE;
		data->write_ds = I3C_HDR_DDR_MODE;
	} else {
		data->read_ds =  dev->info.max_read_ds;
		data->write_ds = dev->info.max_write_ds;
	}
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
		dw_i3c_master_set_speed(dev, data, master);
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
	dw_i3c_master_set_speed(dev, data, master);
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
	struct dw_i3c_xfer xfer = {};
	int i, ret = 0, total_bytes = 0;
	bool busy_wait_mode = false, timeout;

	if (!i2c_nxfers)
		return 0;

	ret = dw_i3c_xfer_suspend_inc(master);
	if (ret)
		return ret;

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

		total_bytes += (i2c_xfers[i].len + ADDRESS_BYTES);
	}

	busy_wait_mode = ((master->operation_mode == HYBRID_BUSY_WAIT_MODE) &&
			  (total_bytes < master->i2c_hybrid_threshold));

	dw_i3c_master_enqueue_xfer(master, &xfer);
	timeout = dw_i3c_wait_for_xfer_completion(&xfer, busy_wait_mode);

	if (timeout)
		dw_i3c_master_dequeue_xfer(master, &xfer);

	ret = xfer.ret;

	dw_i3c_xfer_suspend_dec(master);
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

	if (master->is_i2c_slot_shared)
		pos = master->shared_i2c_pos;
	else
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
	data->i2c_addr = dev->addr;
	i2c_dev_set_master_data(dev, data);

	if (!master->is_i2c_slot_shared) {
		master->free_pos &= ~BIT(pos);
		master->devs[pos].addr = dev->addr;
		master->devs[pos].is_i2c = true;
		writel(DEV_ADDR_TABLE_LEGACY_I2C_DEV |
			DEV_ADDR_TABLE_STATIC_ADDR(dev->addr),
			master->regs +
			DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));
	}

	return 0;
}

static void dw_i3c_master_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i2c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);

	if (!master->is_i2c_slot_shared) {
		writel(0,
			master->regs +
			DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index));
		master->devs[data->index].addr = 0;
		master->free_pos |= BIT(data->index);
	}

	i2c_dev_set_master_data(dev, NULL);
	kfree(data);
}

static int dw_i3c_master_request_ibi(struct i3c_dev_desc *dev,
				     const struct i3c_ibi_setup *req)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	unsigned long flags;

	data->ibi_pool = i3c_generic_ibi_alloc_pool(dev, req);
	if (IS_ERR(data->ibi_pool))
		return PTR_ERR(data->ibi_pool);

	spin_lock_irqsave(&master->devs_lock, flags);
	master->devs[data->index].ibi_dev = dev;
	spin_unlock_irqrestore(&master->devs_lock, flags);

	return 0;
}

static void dw_i3c_master_free_ibi(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	unsigned long flags;

	spin_lock_irqsave(&master->devs_lock, flags);
	master->devs[data->index].ibi_dev = NULL;
	spin_unlock_irqrestore(&master->devs_lock, flags);

	i3c_generic_ibi_free_pool(data->ibi_pool);
	data->ibi_pool = NULL;
}

static void dw_i3c_master_set_sir_enabled(struct dw_i3c_master *master,
					  struct i3c_dev_desc *dev,
					  u8 idx, bool enable)
{
	unsigned long flags;
	u32 dat_entry, reg;
	bool global;

	dat_entry = DEV_ADDR_TABLE_LOC(master->datstartaddr, idx);

	spin_lock_irqsave(&master->devs_lock, flags);
	reg = readl(master->regs + dat_entry);
	if (enable) {
		reg &= ~DEV_ADDR_TABLE_SIR_REJECT;
		if (dev->info.bcr & I3C_BCR_IBI_PAYLOAD)
			reg |= DEV_ADDR_TABLE_IBI_MDB;
	} else {
		reg |= DEV_ADDR_TABLE_SIR_REJECT;
	}
	master->platform_ops->set_dat_ibi(master, dev, enable, &reg);
	writel(reg, master->regs + dat_entry);

	global = true;
	if (master->ibi_sir_req_rej) {
		reg = readl(master->regs + IBI_SIR_REQ_REJECT);
		if (enable) {
			global = reg == IBI_REQ_REJECT_ALL;
			reg &= ~BIT(idx);
		} else {
			global = reg == 0;
			reg |= BIT(idx);
		}
		writel(reg, master->regs + IBI_SIR_REQ_REJECT);
	}

	if (global) {
		spin_lock(&master->xferqueue.lock);
		reg = readl(master->regs + INTR_STATUS_EN);
		reg &= ~INTR_IBI_THLD_STAT;
		if (enable)
			reg |= INTR_IBI_THLD_STAT;
		writel(reg, master->regs + INTR_STATUS_EN);

		reg = readl(master->regs + INTR_SIGNAL_EN);
		reg &= ~INTR_IBI_THLD_STAT;
		if (enable)
			reg |= INTR_IBI_THLD_STAT;
		writel(reg, master->regs + INTR_SIGNAL_EN);
		spin_unlock(&master->xferqueue.lock);
	}

	spin_unlock_irqrestore(&master->devs_lock, flags);
}

static int dw_i3c_master_enable_ibi(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	void __iomem *dev_dat_addr;
	int rc;

	rc = pm_runtime_resume_and_get(master->dev);
	if (rc < 0) {
		dev_err(master->dev, "%s: cannot resume i3c bus master, err: %d\n", __func__, rc);
		return rc;
	}

	dw_i3c_master_set_sir_enabled(master, dev, data->index, true);

	rc = i3c_master_enec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);

	if (rc) {
		dw_i3c_master_set_sir_enabled(master, dev, data->index, false);
		pm_runtime_mark_last_busy(master->dev);
		pm_runtime_put_autosuspend(master->dev);
		return rc;
	}

	if (master->pec_enable) {
		dev_dat_addr = master->regs + DEV_ADDR_TABLE_LOC(master->datstartaddr, data->index);
		writel(readl(dev_dat_addr) | DEV_ADDR_TABLE_IBI_PEC_EN, dev_dat_addr);
	}

	return rc;
}

static int dw_i3c_master_disable_ibi(struct i3c_dev_desc *dev)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct dw_i3c_master *master = to_dw_i3c_master(m);
	int rc;

	rc = i3c_master_disec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
	if (rc)
		return rc;

	dw_i3c_master_set_sir_enabled(master, dev, data->index, false);

	pm_runtime_mark_last_busy(master->dev);
	pm_runtime_put_autosuspend(master->dev);

	return 0;
}

static void dw_i3c_master_recycle_ibi_slot(struct i3c_dev_desc *dev,
					   struct i3c_ibi_slot *slot)
{
	struct dw_i3c_i2c_dev_data *data = i3c_dev_get_master_data(dev);

	i3c_generic_ibi_recycle_slot(data->ibi_pool, slot);
}

static void dw_i3c_master_drain_ibi_queue(struct dw_i3c_master *master,
					  int len)
{
	int i;

	for (i = 0; i < DIV_ROUND_UP(len, 4); i++)
		readl(master->regs + IBI_QUEUE_STATUS);
}

static void dw_i3c_master_handle_ibi_sir(struct dw_i3c_master *master,
					 u32 status)
{
	struct dw_i3c_i2c_dev_data *data;
	struct i3c_ibi_slot *slot;
	struct i3c_dev_desc *dev;
	unsigned long flags;
	u8 addr, len;
	int idx;

	addr = IBI_QUEUE_IBI_ADDR(status);
	len = IBI_QUEUE_STATUS_DATA_LEN(status);

	trace_dw_i3c_ibi(master, status);

	/*
	 * We be tempted to check the error status in bit 30; however, due
	 * to the PEC errata workaround on some platform implementations (see
	 * ast2600_i3c_set_dat_ibi()), those will almost always have a PEC
	 * error on IBI payload data, as well as losing the last byte of
	 * payload.
	 *
	 * If we implement error status checking on that bit, we may need
	 * a new platform op to validate it.
	 */

	spin_lock_irqsave(&master->devs_lock, flags);
	idx = dw_i3c_master_get_addr_pos(master, addr);
	if (idx < 0) {
		dev_err_ratelimited(&master->base.dev,
				    "IBI from unknown addr 0x%x\n", addr);
		goto err_drain;
	}

	dev = master->devs[idx].ibi_dev;
	if (!dev || !dev->ibi) {
		dev_err_ratelimited(&master->base.dev,
				    "IBI from non-requested dev idx %d\n", idx);
		goto err_drain;
	}

	data = i3c_dev_get_master_data(dev);
	if (dev->ibi->max_payload_len < len) {
		dev_err_ratelimited(&master->base.dev,
				    "IBI payload len %d greater than max %d\n",
				    len, dev->ibi->max_payload_len);
		goto err_drain;
	}

	slot = i3c_generic_ibi_get_free_slot(data->ibi_pool);
	if (!slot) {
		dev_err_ratelimited(&master->base.dev,
				    "No IBI slots available\n");
		goto err_drain;
	}

	if (len)
		dw_i3c_master_read_ibi_fifo(master, slot->data, len);

	slot->len = len;

	i3c_master_queue_ibi(dev, slot);

	spin_unlock_irqrestore(&master->devs_lock, flags);

	return;

err_drain:
	spin_lock(&master->xferqueue.lock);
	dump_regs_locked(master);
	spin_unlock(&master->xferqueue.lock);

	dw_i3c_master_drain_ibi_queue(master, len);

	spin_unlock_irqrestore(&master->devs_lock, flags);
}

static void dw_i3c_master_build_i3c_cmd(const struct dw_i3c_xfer *xfer,
					struct dw_i3c_cmd *cmd, int idx,
					struct dw_i3c_master *master)
{
	bool sdr_xfer = true;

	cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(xfer->msgs.i3c_msgs[idx].len) |
		      COMMAND_PORT_TRANSFER_ARG;

	if (xfer->msgs.i3c_msgs[idx].rnw) {
		cmd->cmd_lo = COMMAND_PORT_READ_TRANSFER |
			      COMMAND_PORT_SPEED(xfer->dev_data.read_ds);

		if (xfer->dev_data.read_ds == I3C_HDR_DDR_MODE)
			sdr_xfer = false;
	} else {
		cmd->cmd_lo = COMMAND_PORT_SPEED(xfer->dev_data.write_ds);

		if (xfer->dev_data.write_ds == I3C_HDR_DDR_MODE)
			sdr_xfer = false;
	}

	cmd->cmd_lo |= COMMAND_PORT_TID(idx) |
		       COMMAND_PORT_DEV_INDEX(xfer->dev_data.index) |
		       COMMAND_PORT_ROC |
		       (master->pec_enable && sdr_xfer ? COMMAND_PORT_PEC : 0) |
		       (!sdr_xfer ? COMMAND_PORT_CP : 0);

	if (idx == (xfer->ncmds - 1))
		cmd->cmd_lo |= COMMAND_PORT_TOC;
}

static void dw_i3c_master_build_i2c_cmd(const struct dw_i3c_xfer *xfer,
					struct dw_i3c_cmd *cmd, int idx,
					struct dw_i3c_master *master)
{
	cmd->cmd_hi = COMMAND_PORT_ARG_DATA_LEN(xfer->msgs.i2c_msgs[idx].len) |
		      COMMAND_PORT_TRANSFER_ARG;

	cmd->cmd_lo = COMMAND_PORT_TID(idx) |
		      COMMAND_PORT_DEV_INDEX(xfer->dev_data.index) |
		      COMMAND_PORT_SPEED(xfer->dev_data.write_ds) |
		      COMMAND_PORT_ROC |
		      (master->pec_enable ? COMMAND_PORT_PEC : 0);

	if (xfer->msgs.i2c_msgs[idx].flags & I2C_M_RD) {
		cmd->cmd_lo |= COMMAND_PORT_READ_TRANSFER;
	}

	if (idx == (xfer->ncmds - 1))
		cmd->cmd_lo |= COMMAND_PORT_TOC;
}

static void dw_i3c_master_xfer_push_cmd_queue_locked(struct dw_i3c_master *master,
						     u32 queue_status_level)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	unsigned int empty_cmds;
	int i;

	/* Per dw_i3c_cmd will push two commands into the command queue */
	empty_cmds = QUEUE_STATUS_LEVEL_CMD(queue_status_level) / 2;

	for (i = 0; i < empty_cmds && xfer->cmd_idx < xfer->ncmds;
	     i++, xfer->cmd_idx++) {
		struct dw_i3c_cmd cmd;

		if (xfer->type == XFER_TYPE_I3C) {
			dw_i3c_master_build_i3c_cmd(xfer, &cmd, xfer->cmd_idx, master);
		} else if (xfer->type == XFER_TYPE_I2C) {
			dw_i3c_master_build_i2c_cmd(xfer, &cmd, xfer->cmd_idx, master);
		} else if (xfer->type == XFER_TYPE_DAA || xfer->type == XFER_TYPE_CCC) {
			cmd = xfer->ccc_cmd;
		} else {
			dev_err(&master->base.dev, " invalid xfer type.");
			return;
		}

		trace_dw_i3c_push_cmd(master, &cmd);

		writel(cmd.cmd_hi, master->regs + COMMAND_QUEUE_PORT);
		writel(cmd.cmd_lo, master->regs + COMMAND_QUEUE_PORT);
	}
}

static bool dw_i3c_master_xfer_pull_resp_queue_locked(struct dw_i3c_master *master,
						      u32 queue_status_level)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	unsigned int nresps;
	bool is_msg_rnw;
	int i;

	nresps = QUEUE_STATUS_LEVEL_RESP(queue_status_level);

	for (i = 0; i < nresps && xfer->resp_idx < xfer->ncmds;
	     i++, xfer->resp_idx++) {
		u32 resp = readl(master->regs + RESPONSE_QUEUE_PORT);

		trace_dw_i3c_pull_resp(master, resp);

		is_msg_rnw = dw_i3c_master_msg_rnw(xfer, xfer->resp_idx);
		int resp_data_len =
			is_msg_rnw ? dw_i3c_master_get_msg_len(xfer, xfer->resp_idx) : 0;

		/* ccc resp is carries different information from normal transfers. */
		if (xfer->type == XFER_TYPE_CCC || xfer->type == XFER_TYPE_DAA)
			xfer->ccc_resp = resp;

		if (RESPONSE_PORT_ERR_STATUS(resp)) {
			/* ENTDAA is expected to end with IBA NACK */
			if (xfer->end_with_iba_nack &&
			    RESPONSE_PORT_ERR_STATUS(resp) == RESPONSE_ERROR_IBA_NACK)
				continue;
			else {
				dev_err(&master->base.dev,
					"%s: resp err status: %#08x, expect nack: %d\n",
					__func__, resp,
					xfer->end_with_iba_nack);
				dump_regs_locked(master);
				xfer->ret = -EIO;
				break;
			}
		} else if (RESPONSE_PORT_TID(resp) != COMMON_COMMAND_CODE_TID &&
			   RESPONSE_PORT_TID(resp) != COMMAND_PORT_TID_MASK(xfer->resp_idx)) {
			dev_err(&master->base.dev,
				"%s: resp RID: %lu does not match the current TID: %u\n",
				__func__, RESPONSE_PORT_TID(resp), xfer->resp_idx);
			dump_regs_locked(master);
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
			dev_err(&master->base.dev,
				"%s: resp RX LEN: %lu does not match the current RX LEN: %u\n",
				__func__, RESPONSE_PORT_DATA_LEN(resp), resp_data_len);
			dump_regs_locked(master);
			xfer->ret = -EIO;
			break;
		}

		if (dw_i3c_xfer_error(xfer, resp))
			break;
	}
	return xfer->ret != -ETIMEDOUT;
}

static void dw_i3c_master_xfer_rd_rx_data_locked(struct dw_i3c_master *master,
						 u32 buf_status_level)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	bool is_msg_rnw;
	u16 buf_len, rx_buf_level, rx_len;
	u8 *rx_bytes;

	if (!xfer) {
		dev_err(&master->base.dev,
			"%s: data remain in DATA buffer with no xfer processing\n",
			__func__);
		dump_regs_locked(master);
		return;
	}

	rx_buf_level = DATA_BUFFER_STATUS_LEVEL_RX(buf_status_level);

	while (rx_buf_level && xfer->rx_cmd_idx < xfer->ncmds) {
		is_msg_rnw = dw_i3c_master_msg_rnw(xfer, xfer->rx_cmd_idx);

		if (!is_msg_rnw) {
			xfer->rx_cmd_idx++;
			xfer->rx_buf_cursor = 0;
			continue;
		}

		rx_len = dw_i3c_master_get_msg_len(xfer, xfer->rx_cmd_idx);
		if (rx_len <= xfer->rx_buf_cursor) {
			xfer->rx_cmd_idx++;
			xfer->rx_buf_cursor = 0;
			continue;
		}

		buf_len = min(rx_buf_level * DATA_BUF_ENTRY_SIZE,
			      rx_len - xfer->rx_buf_cursor);
		rx_bytes = dw_i3c_master_get_msg_rx_buf(xfer, xfer->rx_cmd_idx);
		dw_i3c_master_read_rx_fifo(master,
					   rx_bytes + xfer->rx_buf_cursor,
					   buf_len);

		rx_buf_level -= (buf_len + DATA_BUF_ENTRY_SIZE - 1) /
				DATA_BUF_ENTRY_SIZE;
		xfer->rx_buf_cursor += buf_len;
		xfer->res_rx_entries -= (buf_len + DATA_BUF_ENTRY_SIZE - 1) /
					DATA_BUF_ENTRY_SIZE;
	}
}

static void dw_i3c_master_xfer_wr_tx_data_locked(struct dw_i3c_master *master,
						 u32 buf_status_level)
{
	struct dw_i3c_xfer *xfer = master->xferqueue.cur;
	u16 buf_len, tx_empty_buf_level, tx_len;
	bool is_msg_rnw;
	const u8 *tx_bytes;

	if (!xfer) {
		dev_err(&master->base.dev,
			"%s: data remain in DATA buffer with no xfer processing\n",
			__func__);
		dump_regs_locked(master);
		return;
	}

	tx_empty_buf_level = DATA_BUFFER_STATUS_LEVEL_TX(buf_status_level);

	while (tx_empty_buf_level && xfer->tx_cmd_idx < xfer->ncmds) {
		is_msg_rnw = dw_i3c_master_msg_rnw(xfer, xfer->tx_cmd_idx);

		if (is_msg_rnw) {
			xfer->tx_cmd_idx++;
			xfer->tx_buf_cursor = 0;
			continue;
		}

		tx_len = dw_i3c_master_get_msg_len(xfer, xfer->tx_cmd_idx);
		if (tx_len <= xfer->tx_buf_cursor) {
			xfer->tx_cmd_idx++;
			xfer->tx_buf_cursor = 0;
			continue;
		}

		buf_len = min(tx_empty_buf_level * DATA_BUF_ENTRY_SIZE,
			      tx_len - xfer->tx_buf_cursor);
		tx_bytes = dw_i3c_master_get_msg_tx_buf(xfer, xfer->tx_cmd_idx);
		dw_i3c_master_wr_tx_fifo(master, tx_bytes + xfer->tx_buf_cursor,
					 buf_len);

		tx_empty_buf_level -= (buf_len + DATA_BUF_ENTRY_SIZE - 1) /
				      DATA_BUF_ENTRY_SIZE;
		xfer->tx_buf_cursor += buf_len;
		xfer->res_tx_entries -= (buf_len + DATA_BUF_ENTRY_SIZE - 1) /
					DATA_BUF_ENTRY_SIZE;
	}
}

/* "ibis": referring to In-Band Interrupts, and not
 * https://en.wikipedia.org/wiki/Australian_white_ibis. The latter should
 * not be handled.
 */
static void dw_i3c_master_irq_handle_ibis(struct dw_i3c_master *master)
{
	unsigned int i, len, n_ibis;
	u32 reg;

	reg = readl(master->regs + QUEUE_STATUS_LEVEL);
	n_ibis = QUEUE_STATUS_IBI_STATUS_CNT(reg);
	if (!n_ibis)
		return;

	for (i = 0; i < n_ibis; i++) {
		reg = readl(master->regs + IBI_QUEUE_STATUS);

		if (IBI_TYPE_SIRQ(reg)) {
			dw_i3c_master_handle_ibi_sir(master, reg);
		} else {
			len = IBI_QUEUE_STATUS_DATA_LEN(reg);
			dev_info(&master->base.dev,
				 "unsupported IBI type 0x%lx len %d\n",
				 IBI_QUEUE_STATUS_IBI_ID(reg), len);
			dw_i3c_master_drain_ibi_queue(master, len);
		}
	}
}

static irqreturn_t dw_i3c_master_irq_handler(int irq, void *dev_id)
{
	struct dw_i3c_master *master = dev_id;
	struct dw_i3c_xfer *xfer;
	u32 intr_status, buf_status_level, queue_status_level;

	intr_status = readl(master->regs + INTR_STATUS);

	trace_dw_i3c_irq(master, intr_status);

	if (!(intr_status & readl(master->regs + INTR_STATUS_EN))) {
		writel(INTR_ALL, master->regs + INTR_STATUS);
		return IRQ_NONE;
	}

	spin_lock(&master->xferqueue.lock);

	xfer = master->xferqueue.cur;

	if (intr_status & INTR_TRANSFER_ERR_STAT)
		writel(INTR_TRANSFER_ERR_STAT, master->regs + INTR_STATUS);

	if (xfer) {
		if (!xfer->using_dma) {
			buf_status_level =
				readl(master->regs + DATA_BUFFER_STATUS_LEVEL);
			dw_i3c_master_xfer_wr_tx_data_locked(master, buf_status_level);
			dw_i3c_master_xfer_rd_rx_data_locked(master, buf_status_level);
		}

		queue_status_level = readl(master->regs + QUEUE_STATUS_LEVEL);
		dw_i3c_master_xfer_push_cmd_queue_locked(master,
							 queue_status_level);

		if (dw_i3c_master_xfer_pull_resp_queue_locked(master, queue_status_level)) {
			/* Error */
			if (!xfer->using_dma)
				dw_i3c_master_end_xfer_locked(master);
			else
				complete(&xfer->comp);
		} else if (dw_i3c_xfer_finished(xfer)) {
			/* Success */
			if (!xfer->using_dma)
				dw_i3c_master_end_xfer_locked(master);
			else
				complete(&xfer->comp);
		} else {
			dw_i3c_master_program_xfer_irq_thld_locked(master);
		}
	}

	spin_unlock(&master->xferqueue.lock);

	if (intr_status & INTR_IBI_THLD_STAT)
		dw_i3c_master_irq_handle_ibis(master);

	return IRQ_HANDLED;
}

static irqreturn_t dw_i3c_master_irq_top_handler(int irq, void *dev_id)
{
	struct dw_i3c_master *master = dev_id;
	struct dw_i3c_xfer *xfer;
	bool is_xfer_in_bottom_half = false;

	spin_lock(&master->xferqueue.lock);
	xfer = master->xferqueue.cur;

	if (xfer && xfer->in_bottom_half && !xfer->using_dma)
		is_xfer_in_bottom_half = true;

	spin_unlock(&master->xferqueue.lock);

	if (is_xfer_in_bottom_half) {
		return IRQ_WAKE_THREAD;
	} else {
		return dw_i3c_master_irq_handler(irq, master);
	}
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

static const struct i3c_master_controller_ops dw_mipi_i3c_ibi_ops = {
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
	.request_ibi = dw_i3c_master_request_ibi,
	.free_ibi = dw_i3c_master_free_ibi,
	.enable_ibi = dw_i3c_master_enable_ibi,
	.disable_ibi = dw_i3c_master_disable_ibi,
	.recycle_ibi_slot = dw_i3c_master_recycle_ibi_slot,
};

static int dw_i3c_master_enable_clks(struct dw_i3c_master *master)
{
	int ret = 0;

	/* Request the highest available rate for core_clk to
	 * ensure optimal I3C timing calculations.
	 */
	ret = clk_set_rate(master->core_clk, ULONG_MAX);
	if (ret)
		return ret;

	ret = clk_prepare_enable(master->core_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(master->apb_clk);
	if (ret) {
		clk_disable_unprepare(master->core_clk);
		return ret;
	}

	dev_info(master->dev, "core_clk rate: %lu\n",
			clk_get_rate(master->core_clk));

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

static u32 dw_i3c_apply_ixc_threshold_property(struct platform_device *pdev,
					enum dw_i3c_xfer_type xfer_type)
{
	u32 val = 0;

	if (xfer_type == XFER_TYPE_I2C) {
		val = I2C_HYBRID_BUSY_WAIT_BYTES_THLD;
		of_property_read_u32(pdev->dev.of_node, "i2c-hybrid-threshold", &val);
	} else if (xfer_type ==	XFER_TYPE_I3C) {
		val = I3C_HYBRID_BUSY_WAIT_BYTES_THLD;
		of_property_read_u32(pdev->dev.of_node, "i3c-hybrid-threshold", &val);
	}

	return val;
}

static const struct dw_i3c_platform_ops dw_i3c_platform_ops_default = {
	.init = dw_i3c_platform_init_nop,
	.set_dat_ibi = dw_i3c_platform_set_dat_ibi_nop,
};

static int dw_i3c_master_dma_init(struct dw_i3c_master *master)
{
	struct dma_slave_config slave_config;
	int ret = 0;

	master->chan_tx = dma_request_chan(master->dev, "tx");
	if (IS_ERR(master->chan_tx)) {
		ret = PTR_ERR(master->chan_tx);
		master->chan_tx = NULL;
		goto err;
	}

	master->chan_rx = dma_request_chan(master->dev, "rx");
	if (IS_ERR(master->chan_rx)) {
		ret = PTR_ERR(master->chan_rx);
		master->chan_rx = NULL;
		goto err;
	}

	memset(&slave_config, 0, sizeof(slave_config));
	slave_config.src_addr = (dma_addr_t)master->regs_phys + RX_TX_DATA_PORT;
	slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	slave_config.src_maxburst = DMA_MAXBURST;
	slave_config.dst_addr = (dma_addr_t)master->regs_phys + RX_TX_DATA_PORT;
	slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	slave_config.dst_maxburst = DMA_MAXBURST;

	slave_config.direction = DMA_MEM_TO_DEV;
	if (dmaengine_slave_config(master->chan_tx, &slave_config)) {
		dev_err(master->dev, "failed to configure tx channel\n");
		ret = -EINVAL;
		goto err;
	}

	slave_config.direction = DMA_DEV_TO_MEM;
	if (dmaengine_slave_config(master->chan_rx, &slave_config)) {
		dev_err(master->dev, "failed to configure rx channel\n");
		ret = -EINVAL;
		goto err;
	}

	master->can_dma = true;
	dev_dbg(master->dev, "using %s (tx) and %s (rx) for DMA transfers\n",
		dma_chan_name(master->chan_tx), dma_chan_name(master->chan_rx));

	return ret;

err:
	if (ret != -EPROBE_DEFER)
		dev_err(master->dev, "DMA init err %d\n", ret);

	if (master->chan_rx)
		dma_release_channel(master->chan_rx);

	if (master->chan_tx)
		dma_release_channel(master->chan_tx);

	return ret;
}

int dw_i3c_common_probe(struct dw_i3c_master *master,
			struct platform_device *pdev)
{
	const struct i3c_master_controller_ops *ops;
	struct resource *res;
	bool ext_power_control;
	int ret, irq;

	i3c_master_trace_init(pdev);

	if (!master->platform_ops)
		master->platform_ops = &dw_i3c_platform_ops_default;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	master->regs = devm_ioremap_resource(master->dev, res);
	if (IS_ERR(master->regs))
		return PTR_ERR(master->regs);

	master->regs_phys = res->start;

	ret = dw_i3c_master_dma_init(master);
	if (ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;

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

	spin_lock_init(&master->suspend.lock);
	init_waitqueue_head(&master->suspend.wait_queue);

	spin_lock_init(&master->xferqueue.lock);
	INIT_LIST_HEAD(&master->xferqueue.list);

	writel(INTR_ALL, master->regs + INTR_STATUS);
	irq = platform_get_irq(pdev, 0);
	ret = devm_request_threaded_irq(&pdev->dev, irq,
					dw_i3c_master_irq_top_handler,
					dw_i3c_master_irq_handler,
					IRQF_ONESHOT,
					dev_name(&pdev->dev), master);
	if (ret)
		goto err_reg_disable;

	platform_set_drvdata(pdev, master);

	/* Information regarding the FIFOs/QUEUEs depth */
	ret = readl(master->regs + QUEUE_STATUS_LEVEL);
	master->caps.cmdfifodepth = QUEUE_STATUS_LEVEL_CMD(ret);

	ret = readl(master->regs + DATA_BUFFER_STATUS_LEVEL);
	master->caps.datafifodepth = DATA_BUFFER_STATUS_LEVEL_TX(ret);

	ret = readl(master->regs + DEVICE_ADDR_TABLE_POINTER);
	master->datstartaddr = ret;
	master->maxdevs = ret >> 16;
	master->free_pos = GENMASK(master->maxdevs - 1, 0);

	master->ibi_capable = of_property_read_bool(pdev->dev.of_node, "ibi-capable");
	ops = &dw_mipi_i3c_ops;
	if (master->ibi_capable)
		ops = &dw_mipi_i3c_ibi_ops;

	master->is_i2c_slot_shared = of_property_read_bool(pdev->dev.of_node, "i2c-shared-slot");
	if (master->is_i2c_slot_shared) {
		master->shared_i2c_pos = dw_i3c_master_get_free_pos(master);
		master->free_pos &= ~BIT(master->shared_i2c_pos);
		master->devs[master->shared_i2c_pos].is_i2c = true;
	}

	master->daa_with_i2c_speed = of_property_read_bool(pdev->dev.of_node, "i2c-speed-daa");

	/* Mode of operation Polling/Interrupt mode */
	if (of_property_read_bool(pdev->dev.of_node, "hybrid-busy-wait-mode"))
		master->operation_mode = HYBRID_BUSY_WAIT_MODE;
	else
		master->operation_mode = INTERRUPT_MODE;

	master->i2c_hybrid_threshold = dw_i3c_apply_ixc_threshold_property(pdev, XFER_TYPE_I2C);
	master->i3c_hybrid_threshold = dw_i3c_apply_ixc_threshold_property(pdev, XFER_TYPE_I3C);

	master->i3c_threaded_threshold = I3C_THREADED_THRESHOLD_DEFAULT;
	of_property_read_u32(pdev->dev.of_node, "i3c-threaded-threshold",
			     &master->i3c_threaded_threshold);

	i2c_parse_fw_timings(&pdev->dev, &master->i2c_timings, true);

	master->hdr_ddr_capable = of_property_read_bool(pdev->dev.of_node, "hdr-ddr-capable");
	master->pec_enable = of_property_read_bool(pdev->dev.of_node, "pec-enable");

	if (master->can_dma) {
		master->dma_align = DMA_DEFAULT_ALIGN;
		of_property_read_u8_array(pdev->dev.of_node, "dma-align", &master->dma_align, 1);

		master->fifo_sz = master->caps.datafifodepth * 4; /* bytes */
		of_property_read_u32(pdev->dev.of_node, "dma-threshold-size", &master->fifo_sz);
	}

	/* Increment PM usage to avoid possible spurious runtime suspend */
	pm_runtime_get_noresume(&pdev->dev);

	ext_power_control = of_property_read_bool(pdev->dev.of_node, "external-power-control");
	if (!ext_power_control) {
		pm_runtime_set_autosuspend_delay(&pdev->dev, RPM_AUTOSUSPEND_TIMEOUT_MS);
		pm_runtime_use_autosuspend(&pdev->dev);
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = i3c_master_register(&master->base, &pdev->dev, ops, false);
	if (ret)
		goto err_disable_pm;

	dw_i3c_debugfs_init(master);

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

err_reg_disable:
	if (master->io_vreg_dev && regulator_disable(master->io_vreg_dev))
		dev_err(&pdev->dev, "failed to disable camio regulator\n");

err_assert_rst:
	reset_control_assert(master->core_rst);

	dw_i3c_master_disable_clks(master);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_i3c_common_probe);

/* base platform implementation */

static int dw_i3c_probe(struct platform_device *pdev)
{
	struct dw_i3c_master *master;
	int res;
	bool aoc_ssr_capable;

	master = devm_kzalloc(&pdev->dev, sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->dev = &pdev->dev;

	aoc_ssr_capable = of_property_read_bool(pdev->dev.of_node, "aoc-ssr-capable");
	if (aoc_ssr_capable) {
		INIT_DELAYED_WORK(&master->aoss_ssr_work, aoss_ssr_work_func);
		master->aoss_ssr_nb.notifier_call = aoss_ssr_notifier;
		aoss_ssr_add_notifier(&master->aoss_ssr_nb);
	}

	res = dw_i3c_common_probe(master, pdev);
	if (res && aoc_ssr_capable)
		aoss_ssr_remove_notifier(&master->aoss_ssr_nb);

	return res;
}

static void dw_i3c_remove(struct platform_device *pdev)
{
	struct dw_i3c_master *master = platform_get_drvdata(pdev);

	debugfs_remove_recursive(master->debugfs);

	i3c_master_unregister(&master->base);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	if (master->chan_tx)
		dma_release_channel(master->chan_tx);

	if (master->chan_rx)
		dma_release_channel(master->chan_rx);

	reset_control_assert(master->core_rst);

	dw_i3c_master_disable_clks(master);

	aoss_ssr_remove_notifier(&master->aoss_ssr_nb);
	disable_delayed_work_sync(&master->aoss_ssr_work);
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

static int __maybe_unused dw_i3c_master_runtime_suspend(struct device *dev)
{
	struct dw_i3c_master *master = dev_get_drvdata(dev);
	int ret = 0;

	dw_i3c_master_disable(master);

	dw_i3c_master_disable_clks(master);
	reset_control_assert(master->core_rst);
	pinctrl_pm_select_sleep_state(dev);

	if (master->io_vreg_dev) {
		ret = regulator_disable(master->io_vreg_dev);
		if (ret)
			dev_err(dev, "failed to disable camio regulator\n");
	}

	trace_dw_i3c_runtime_suspend(dev, ret);

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
	dw_i3c_master_reinit(master);

	trace_dw_i3c_runtime_resume(dev, ret);

	return ret;
}

static void dw_i3c_master_reinit(struct dw_i3c_master *master)
{
	dw_i3c_master_set_intr_regs(master);
	dw_i3c_master_restore_timing_regs(master);
	dw_i3c_master_restore_addrs(master);
	dw_i3c_master_enable(master);
}

static int dw_i3c_master_suspend(struct device *dev)
{
	struct dw_i3c_master *master = dev_get_drvdata(dev);
	int ret;

	i2c_mark_adapter_suspended(&master->base.i2c);
	dw_i3c_mark_suspended(master);
	ret = pm_runtime_force_suspend(dev);

	trace_dw_i3c_suspend(dev, ret);
	return ret;
}

static int dw_i3c_master_resume(struct device *dev)
{
	struct dw_i3c_master *master = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		goto out;

	dw_i3c_mark_resumed(master);
	i2c_mark_adapter_resumed(&master->base.i2c);

out:
	trace_dw_i3c_resume(dev, ret);
	return ret;
}

static const struct dev_pm_ops dw_i3c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_i3c_master_suspend, dw_i3c_master_resume)
	SET_RUNTIME_PM_OPS(dw_i3c_master_runtime_suspend, dw_i3c_master_runtime_resume, NULL)
};

static const struct of_device_id dw_i3c_master_of_match[] = {
	{ .compatible = "snps,dw-i3c-master-1.00a", },
	{},
};
MODULE_DEVICE_TABLE(of, dw_i3c_master_of_match);

static struct platform_driver dw_i3c_driver = {
	.probe = dw_i3c_probe,
	.remove_new = dw_i3c_remove,
	.driver = {
		.name = "dw-i3c-master",
		.of_match_table = dw_i3c_master_of_match,
		.pm = &dw_i3c_pm_ops,
	},
};
module_platform_driver(dw_i3c_driver);

MODULE_AUTHOR("Vitor Soares <vitor.soares@synopsys.com>");
MODULE_DESCRIPTION("DesignWare MIPI I3C driver");
MODULE_LICENSE("GPL v2");
