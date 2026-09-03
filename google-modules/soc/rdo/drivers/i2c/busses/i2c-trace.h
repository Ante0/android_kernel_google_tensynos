/* SPDX-License-Identifier: GPL-2.0-only */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM i2c_dw

#if !defined(_TRACE_I2C_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_I2C_H

#include <linux/trace.h>
#include <linux/tracepoint.h>

#define MAX_LABEL_SIZE 32

#ifdef I2C_MASTER_TRACES

TRACE_EVENT(i2c_dw_set_timings_master_cnts,
	TP_PROTO(const struct dw_i2c_dev *dev, u16 ss_hcnt, u16 ss_lcnt, u32 sda_falling_time,
		 u32 scl_falling_time),
	TP_ARGS(dev, ss_hcnt, ss_lcnt, sda_falling_time, scl_falling_time),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u16, ss_hcnt)
		__field(u16, ss_lcnt)
		__field(u32, sda_falling_time)
		__field(u32, scl_falling_time)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->ss_hcnt = ss_hcnt;
		__entry->ss_lcnt = ss_lcnt;
		__entry->sda_falling_time = sda_falling_time;
		__entry->scl_falling_time = scl_falling_time;
	),
	TP_printk("%s: Standard Mode HCNT:LCNT=%d:%d sda_fall_t=%uns scl_fall_t=%uns",
		__entry->label,
		__entry->ss_hcnt, __entry->ss_lcnt,
		__entry->sda_falling_time, __entry->scl_falling_time)
);

TRACE_EVENT(i2c_dw_set_timings_master_fast_mod_cnts,
	TP_PROTO(const struct dw_i2c_dev *dev, u16 fs_hcnt, u16 fs_lcnt, const char *fp_str),
	TP_ARGS(dev, fs_hcnt, fs_lcnt, fp_str),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u16, fs_hcnt)
		__field(u16, fs_lcnt)
		__field(const char *, fp_str)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->fs_hcnt = fs_hcnt;
		__entry->fs_lcnt = fs_lcnt;
		__entry->fp_str = fp_str;
	),
	TP_printk("%s: Fast Mode%s HCNT:LCNT=%d:%d",
		__entry->label,
		__entry->fp_str, __entry->fs_hcnt, __entry->fs_lcnt)
);

TRACE_EVENT(i2c_dw_set_timings_master_hs_mod_cnts,
	TP_PROTO(const struct dw_i2c_dev *dev, u16 hs_hcnt, u16 hs_lcnt),
	TP_ARGS(dev, hs_hcnt, hs_lcnt),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u16, hs_hcnt)
		__field(u16, hs_lcnt)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->hs_hcnt = hs_hcnt;
		__entry->hs_lcnt = hs_lcnt;
	),
	TP_printk("%s: High Speed Mode HCNT:LCNT=%d:%d",
		__entry->label,
		__entry->hs_hcnt, __entry->hs_lcnt)
);

TRACE_EVENT(i2c_dw_set_timings_master_bus_speed,
	TP_PROTO(const struct dw_i2c_dev *dev, const char *i2c_freq_mod_str),
	TP_ARGS(dev, i2c_freq_mod_str),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(const char *, i2c_freq_mod_str)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->i2c_freq_mod_str = i2c_freq_mod_str;
	),
	TP_printk("%s: Bus speed: %s",
		__entry->label,
		__entry->i2c_freq_mod_str)
);

TRACE_EVENT(i2c_dw_xfer,
	TP_PROTO(const struct dw_i2c_dev *dev, const char *func, const int num),
	TP_ARGS(dev, func, num),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(const char *, func)
		__field(int, num)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->func = func;
		__entry->num = num;
	),
	TP_printk("%s: %s: msgs: %d",
		__entry->label,
		__entry->func, __entry->num)
);

TRACE_EVENT(i2c_dw_isr,
	TP_PROTO(const struct dw_i2c_dev *dev, unsigned int stat, unsigned int enabled,
		 unsigned int dev_stat, int xfer_stat),
	TP_ARGS(dev, stat, enabled, dev_stat, xfer_stat),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(unsigned int, stat)
		__field(unsigned int, enabled)
		__field(unsigned int, dev_stat)
		__field(int, xfer_stat)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->stat = stat;
		__entry->enabled = enabled;
		__entry->dev_stat = dev_stat;
		__entry->xfer_stat = xfer_stat;
	),
	TP_printk("%s: stat=%#x enabled=%#x dev_stat=%#x xfer_stat=%#x",
		__entry->label,
		__entry->stat, __entry->enabled, __entry->dev_stat, __entry->xfer_stat)
);

TRACE_EVENT(i2c_dw_xfer_init,
	TP_PROTO(struct dw_i2c_dev *dev),
	TP_ARGS(dev),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
	),
	TP_printk("%s: entry",
		__entry->label)
);

TRACE_EVENT(i2c_dw_xfer_msg,
	TP_PROTO(struct dw_i2c_dev *dev, __u16 xfer_bytes),
	TP_ARGS(dev, xfer_bytes),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(__u16, xfer_bytes)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->xfer_bytes = xfer_bytes;
	),
	TP_printk("%s: msg len: %hu B",
		__entry->label,
		__entry->xfer_bytes)
);

#endif /* I2C_MASTER_TRACES */

#ifdef I2C_COMMON_TRACES

TRACE_EVENT(i2c_dw_set_sda_hold,
	TP_PROTO(struct dw_i2c_dev *dev, u32 sda_hold_tx, u32 sda_hold_rx),
	TP_ARGS(dev, sda_hold_tx, sda_hold_rx),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, sda_hold_tx)
		__field(u32, sda_hold_rx)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->sda_hold_tx = sda_hold_tx;
		__entry->sda_hold_rx = sda_hold_rx;
	),
	TP_printk("%s: SDA Hold Time TX:RX=%d:%d",
		__entry->label,
		__entry->sda_hold_tx, __entry->sda_hold_rx)
);

#endif /* I2C_COMMON_TRACES */

#ifdef I2C_PLAT_TRACES

DECLARE_EVENT_CLASS(i2c_power_group_event,
	TP_PROTO(struct dw_i2c_dev *dev, int ret),
	TP_ARGS(dev, ret),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(int, ret)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", dev_name(dev->dev));
		__entry->ret = ret;
	),
	TP_printk("%s ret=%d", __entry->label, ret)
);

DEFINE_EVENT(i2c_power_group_event, i2c_dw_suspend,
	TP_PROTO(struct dw_i2c_dev *dev, int ret),
	TP_ARGS(dev, ret)
);

DEFINE_EVENT(i2c_power_group_event, i2c_dw_resume,
	TP_PROTO(struct dw_i2c_dev *dev, int ret),
	TP_ARGS(dev, ret)
);

DEFINE_EVENT(i2c_power_group_event, i2c_dw_runtime_suspend,
	TP_PROTO(struct dw_i2c_dev *dev, int ret),
	TP_ARGS(dev, ret)
);

DEFINE_EVENT(i2c_power_group_event, i2c_dw_runtime_resume,
	TP_PROTO(struct dw_i2c_dev *dev, int ret),
	TP_ARGS(dev, ret)
);

#endif /* I2C_PLAT_TRACES */

void google_i2c_trace_init(struct platform_device *pdev);

#endif /* _TRACE_I2C_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/i2c/busses

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE i2c-trace

#include <trace/define_trace.h>
