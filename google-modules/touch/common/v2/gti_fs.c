// SPDX-License-Identifier: GPL
/*
 * GTI File System for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "goog_touch_interface.h"
#include "goog_interface_manager.h"
#include "gti_internal.h"
#include "gti_fs.h"
#include "gti_pm.h"
#include "touch_bus_negotiator.h"

/*-----------------------------------------------------------------------------
 * GTI: forward declarations, structures and functions.
 */
static void ical_state_init_cal(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);
static void ical_state_run_cal(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);
static void ical_state_end_cal(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);
static void ical_state_init_reset(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);
static void ical_state_run_reset(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);
static void ical_state_end_reset(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);
static void ical_state_init_test(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);
static void ical_state_run_test(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);
static void ical_state_end_test(struct goog_touch_interface *gti, u32 next_state, u64 elapsed);

static int gti_do_selftest(struct goog_touch_interface *gti)
{
	int ret = 0;

	ret = goog_process_vendor_cmd(gti, GTI_CMD_SELFTEST);
	if (gti->reset_after_selftest && ret != EOPNOTSUPP) {
		gti->cmd.reset_cmd.setting = GTI_RESET_MODE_AUTO;
		goog_process_vendor_cmd(gti, GTI_CMD_RESET);
	}
	return ret;
}

static int gti_precheck_heatmap(struct goog_touch_interface *gti)
{
	int ret = 0;

	/*
	 * Check the PM wakelock state and pm state for bus ownership before
	 * data request.
	 */
	if (!goog_pm_wake_get_locks(gti) || gti->pm.state == GTI_PM_SUSPEND) {
		GOOG_WARN(gti, "N/A during inactive bus!\n");
		ret = -ENODATA;
	}

	return ret;
}

/*-----------------------------------------------------------------------------
 * GTI/proc: structures and functions.
 */
static int goog_proc_dump_show(struct seq_file *m, void *v);
static int goog_proc_ms_base_show(struct seq_file *m, void *v);
static int goog_proc_ms_diff_show(struct seq_file *m, void *v);
static int goog_proc_ms_raw_show(struct seq_file *m, void *v);
static int goog_proc_ss_base_show(struct seq_file *m, void *v);
static int goog_proc_ss_diff_show(struct seq_file *m, void *v);
static int goog_proc_ss_raw_show(struct seq_file *m, void *v);
static char *gti_proc_name[GTI_PROC_NUM] = {
	[GTI_PROC_DUMP] = "dump",	[GTI_PROC_MS_BASE] = "ms_base",
	[GTI_PROC_MS_DIFF] = "ms_diff", [GTI_PROC_MS_RAW] = "ms_raw",
	[GTI_PROC_SS_BASE] = "ss_base", [GTI_PROC_SS_DIFF] = "ss_diff",
	[GTI_PROC_SS_RAW] = "ss_raw",
};
static int (*gti_proc_show[GTI_PROC_NUM])(struct seq_file *, void *) = {
	[GTI_PROC_DUMP] = goog_proc_dump_show,	     [GTI_PROC_MS_BASE] = goog_proc_ms_base_show,
	[GTI_PROC_MS_DIFF] = goog_proc_ms_diff_show, [GTI_PROC_MS_RAW] = goog_proc_ms_raw_show,
	[GTI_PROC_SS_BASE] = goog_proc_ss_base_show, [GTI_PROC_SS_DIFF] = goog_proc_ss_diff_show,
	[GTI_PROC_SS_RAW] = goog_proc_ss_raw_show,
};
DEFINE_PROC_SHOW_ATTRIBUTE(goog_proc_dump);
DEFINE_PROC_SHOW_ATTRIBUTE(goog_proc_ms_base);
DEFINE_PROC_SHOW_ATTRIBUTE(goog_proc_ms_diff);
DEFINE_PROC_SHOW_ATTRIBUTE(goog_proc_ms_raw);
DEFINE_PROC_SHOW_ATTRIBUTE(goog_proc_ss_base);
DEFINE_PROC_SHOW_ATTRIBUTE(goog_proc_ss_diff);
DEFINE_PROC_SHOW_ATTRIBUTE(goog_proc_ss_raw);

static void goog_proc_heatmap_show(struct seq_file *m, void *v)
{
	struct goog_touch_interface *gti = m->private;
	struct gti_sensor_data_cmd *cmd = &gti->cmd.manual_sensor_data_cmd;
	u16 tx = gti->offload.caps.heatmap_width;
	u16 rx = gti->offload.caps.heatmap_height;
	int x, y;

	if (cmd->size == 0 || cmd->buffer == NULL) {
		seq_puts(m, "result: N/A!\n");
		GOOG_LOGW(gti, "result: N/A!\n");
		return;
	}

	switch (cmd->type) {
	case GTI_SENSOR_DATA_TYPE_MS_BASELINE:
	case GTI_SENSOR_DATA_TYPE_MS_DIFF:
	case GTI_SENSOR_DATA_TYPE_MS_RAW:
		if (cmd->size == TOUCH_OFFLOAD_DATA_SIZE_2D(rx, tx)) {
			seq_puts(m, "result:\n");
			if (cmd->is_unsigned) {
				for (y = 0; y < rx; y++) {
					for (x = 0; x < tx; x++) {
						seq_printf(m, "%5u,",
							   ((u16 *)cmd->buffer)[y * tx + x]);
					}
					seq_puts(m, "\n");
				}
			} else {
				for (y = 0; y < rx; y++) {
					for (x = 0; x < tx; x++) {
						seq_printf(m, "%5d,",
							   ((s16 *)cmd->buffer)[y * tx + x]);
					}
					seq_puts(m, "\n");
				}
			}
		} else {
			seq_printf(m, "error: invalid buffer %p or size %d!\n", cmd->buffer,
				   cmd->size);
			GOOG_LOGW(gti, "error: invalid buffer %p or size %d!\n", cmd->buffer,
				  cmd->size);
		}
		break;

	case GTI_SENSOR_DATA_TYPE_SS_BASELINE:
	case GTI_SENSOR_DATA_TYPE_SS_DIFF:
	case GTI_SENSOR_DATA_TYPE_SS_RAW:
		if (cmd->size == TOUCH_OFFLOAD_DATA_SIZE_1D(rx, tx)) {
			seq_puts(m, "result:\n");
			seq_puts(m, "TX:");
			if (cmd->is_unsigned) {
				for (x = 0; x < tx; x++)
					seq_printf(m, "%5u,", ((u16 *)cmd->buffer)[x]);
				seq_puts(m, "\nRX:");
				for (y = 0; y < rx; y++)
					seq_printf(m, "%5u,", ((u16 *)cmd->buffer)[tx + y]);
			} else {
				for (x = 0; x < tx; x++)
					seq_printf(m, "%5d,", ((s16 *)cmd->buffer)[x]);
				seq_puts(m, "\nRX:");
				for (y = 0; y < rx; y++)
					seq_printf(m, "%5d,", ((s16 *)cmd->buffer)[tx + y]);
			}
			seq_puts(m, "\n");
		} else {
			seq_printf(m, "error: invalid buffer %p or size %d!\n", cmd->buffer,
				   cmd->size);
			GOOG_LOGW(gti, "error: invalid buffer %p or size %d!\n", cmd->buffer,
				  cmd->size);
		}
		break;

	default:
		seq_printf(m, "error: invalid type %#x!\n", cmd->type);
		GOOG_LOGE(gti, "error: invalid type %#x!\n", cmd->type);
		break;
	}
}

static int goog_proc_heatmap_process(struct seq_file *m, void *v, enum gti_sensor_data_type type)
{
	struct goog_touch_interface *gti = m->private;
	struct gti_sensor_data_cmd *cmd = &gti->cmd.manual_sensor_data_cmd;
	int ret = 0;

	/* Only run the vendor command once. */
	if (m->size != PAGE_SIZE)
		return ret;

	ret = gti_precheck_heatmap(gti);
	if (ret) {
		seq_puts(m, "N/A!\n");
		goto heatmap_process_err;
	}

	switch (type) {
	case GTI_SENSOR_DATA_TYPE_MS_BASELINE:
	case GTI_SENSOR_DATA_TYPE_MS_DIFF:
	case GTI_SENSOR_DATA_TYPE_MS_RAW:
	case GTI_SENSOR_DATA_TYPE_SS_BASELINE:
	case GTI_SENSOR_DATA_TYPE_SS_DIFF:
	case GTI_SENSOR_DATA_TYPE_SS_RAW:
		cmd->type = type;
		break;

	default:
		seq_printf(m, "error: invalid type %#x!\n", type);
		GOOG_LOGE(gti, "error: invalid type %#x!\n", type);
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto heatmap_process_err;

	cmd->buffer = NULL;
	cmd->size = 0;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_SENSOR_DATA_MANUAL);
	if (ret) {
		seq_printf(m, "error: %d!\n", ret);
		GOOG_LOGE(gti, "error: %d!\n", ret);
	} else {
		GOOG_LOGI(gti, "type %#x.\n", type);
	}

heatmap_process_err:
	if (ret) {
		cmd->buffer = NULL;
		cmd->size = 0;
	}
	return ret;
}

static int goog_proc_dump_show(struct seq_file *m, void *v)
{
	u64 i, hc_cnt, input_cnt, toggle_cnt;
	int ret, slot;
	ktime_t delta_time;
	time64_t time64_utc;
	s32 remainder;
	struct tm utc;
	struct goog_touch_interface *gti = m->private;
	struct gti_debug_healthcheck *hc_history = gti->debug_healthcheck_history;
	struct gti_debug_input *input_history = gti->debug_input_history;
	struct gti_debug_offload_toggle *toggle_history = gti->debug_offload_toggle_history;
	u32 dev_id = gim_vendor_get_interface_dev_id(gti->vendor_dev);

	/*
	 * Force doing kfifo peek to get up-to-date results.
	 * This is useful to do runtime debug before device suspend.
	 */
	hc_cnt = kfifo_out_peek(&gti->debug_fifo_healthcheck, hc_history,
				kfifo_len(&gti->debug_fifo_healthcheck));
	input_cnt = kfifo_out_peek(&gti->debug_fifo_input, input_history,
				   kfifo_len(&gti->debug_fifo_input));
	toggle_cnt = kfifo_out_peek(&gti->debug_fifo_offload_toggle, toggle_history,
				    kfifo_len(&gti->debug_fifo_offload_toggle));

	ret = mutex_lock_interruptible(&gti->input_process_lock);
	if (ret) {
		seq_puts(m, "error: has been interrupted!\n");
		GOOG_LOGW(gti, "error: has been interrupted!\n");
		return ret;
	}

	/* Output to SYSTEM LOG */
	gti_debug_healthcheck_dump(gti);
	gti_debug_input_dump(gti);
	gti_debug_offload_toggle_dump(gti);

	tbn_debug_configs_dump(m, dev_id);

	/* Output to VENDOR LOG */
	seq_puts(m, "\t### Interrupt ###\n");
	seq_printf(m, "%23s %8s %8s %12s\n", "TIME(UTC)", "INT#", "INPUT#", "SLOT-STATE");
	for (i = 0; i < hc_cnt; i++) {
		if (hc_history[i].irq_index == 0)
			continue;

		time64_utc =
			div_s64_rem(ktime_to_ns(hc_history[i].irq_time), NSEC_PER_SEC, &remainder);
		time64_to_tm(time64_utc, 0, &utc);
		seq_printf(m, "%4ld-%02d-%02d %02d:%02d:%02d.%03ld %8llu %8llu %#12lx\n",
			   utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
			   utc.tm_sec, remainder / NSEC_PER_MSEC, hc_history[i].irq_index,
			   hc_history[i].input_index, hc_history[i].slot_bit_active);
	}
	seq_puts(m, "\n");

	seq_puts(m, "\t### Coordinate(s) ###\n");
	seq_printf(m, "%23s %14s %8s %12s %12s %12s %12s\n", "TIME(UTC)", "DURATION(MS)", "SLOT#",
		   "INT#DOWN", "INT#UP", "X-DELTA(PX)", "Y-DELTA(PX)");
	for (i = 0; i < input_cnt; i++) {
		delta_time =
			ktime_sub(input_history[i].released.time, input_history[i].pressed.time);
		if (delta_time <= 0)
			continue;

		time64_utc = div_s64_rem(ktime_to_ns(input_history[i].pressed.time), NSEC_PER_SEC,
					 &remainder);
		time64_to_tm(time64_utc, 0, &utc);
		seq_printf(
			m,
			"%4ld-%02d-%02d %02d:%02d:%02d.%03ld %14lld %8d %12lld %12lld %12d %12d\n",
			utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
			utc.tm_sec, remainder / NSEC_PER_MSEC, ktime_to_ms(delta_time),
			input_history[i].slot, input_history[i].pressed.irq_index,
			input_history[i].released.irq_index,
			input_history[i].released.coord.x - input_history[i].pressed.coord.x,
			input_history[i].released.coord.y - input_history[i].pressed.coord.y);
	}
	seq_puts(m, "\n");

	seq_puts(m, "\t### Unreleased Coordinate(s) ###\n");
	seq_printf(m, "%8s %12s %12s %12s %12s %12s\n", "SLOT#", "X", "Y", "PRESSURE", "MAJOR",
		   "MINOR");
	for_each_set_bit(slot, &gti->slot_bit_active, MAX_SLOTS) {
		seq_printf(m, "%8d %12u %12u %12u %12u %12u\n", slot, gti->offload.coords[slot].x,
			   gti->offload.coords[slot].y, gti->offload.coords[slot].pressure,
			   gti->offload.coords[slot].major, gti->offload.coords[slot].minor);
	}
	seq_puts(m, "\n");

	seq_puts(m, "\t### Offload Toggle ###\n");
	seq_printf(m, "%23s %8s %8s %8s %8s\n", "TIME(UTC)", "RUNNING", "FRAME#", "INPUT#", "INT#");
	for (i = 0; i < toggle_cnt; i++) {
		time64_utc =
			div_s64_rem(ktime_to_ns(toggle_history[i].time), NSEC_PER_SEC, &remainder);
		time64_to_tm(time64_utc, 0, &utc);
		seq_printf(m, "%4ld-%02d-%02d %02d:%02d:%02d.%03ld %8s %8llu %8llu %8llu\n",
			   utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
			   utc.tm_sec, remainder / NSEC_PER_MSEC,
			   (toggle_history[i].running) ? "O" : "X", toggle_history[i].frame_index,
			   toggle_history[i].input_index, toggle_history[i].irq_index);
	}
	seq_puts(m, "\n");

	seq_puts(m, "\t### Time Delta(MS) from MONO to BOOT ###\n");
	seq_printf(m, "%14lld", ktime_ms_delta(ktime_get_boottime(), ktime_get()));
	seq_puts(m, "\n");

	mutex_unlock(&gti->input_process_lock);
	seq_puts(m, "\n\n");

	return ret;
}

static int goog_proc_ms_base_show(struct seq_file *m, void *v)
{
	struct goog_touch_interface *gti = m->private;
	int ret;

	if (!gti->manual_heatmap_from_irq) {
		ret = mutex_lock_interruptible(&gti->input_heatmap_lock);
		if (ret) {
			seq_puts(m, "error: has been interrupted!\n");
			GOOG_LOGW(gti, "error: has been interrupted!\n");
			return ret;
		}
	}

	ret = goog_proc_heatmap_process(m, v, GTI_SENSOR_DATA_TYPE_MS_BASELINE);
	if (!ret)
		goog_proc_heatmap_show(m, v);

	if (!gti->manual_heatmap_from_irq)
		mutex_unlock(&gti->input_heatmap_lock);

	return ret;
}

static int goog_proc_ms_diff_show(struct seq_file *m, void *v)
{
	struct goog_touch_interface *gti = m->private;
	int ret;

	if (!gti->manual_heatmap_from_irq) {
		ret = mutex_lock_interruptible(&gti->input_heatmap_lock);
		if (ret) {
			seq_puts(m, "error: has been interrupted!\n");
			GOOG_LOGW(gti, "error: has been interrupted!\n");
			return ret;
		}
	}

	ret = goog_proc_heatmap_process(m, v, GTI_SENSOR_DATA_TYPE_MS_DIFF);
	if (!ret)
		goog_proc_heatmap_show(m, v);

	if (!gti->manual_heatmap_from_irq)
		mutex_unlock(&gti->input_heatmap_lock);

	return ret;
}

static int goog_proc_ms_raw_show(struct seq_file *m, void *v)
{
	struct goog_touch_interface *gti = m->private;
	int ret;

	if (!gti->manual_heatmap_from_irq) {
		ret = mutex_lock_interruptible(&gti->input_heatmap_lock);
		if (ret) {
			seq_puts(m, "error: has been interrupted!\n");
			GOOG_LOGW(gti, "error: has been interrupted!\n");
			return ret;
		}
	}

	ret = goog_proc_heatmap_process(m, v, GTI_SENSOR_DATA_TYPE_MS_RAW);
	if (!ret)
		goog_proc_heatmap_show(m, v);

	if (!gti->manual_heatmap_from_irq)
		mutex_unlock(&gti->input_heatmap_lock);

	return ret;
}

static int goog_proc_ss_base_show(struct seq_file *m, void *v)
{
	struct goog_touch_interface *gti = m->private;
	int ret;

	if (!gti->manual_heatmap_from_irq) {
		ret = mutex_lock_interruptible(&gti->input_heatmap_lock);
		if (ret) {
			seq_puts(m, "error: has been interrupted!\n");
			GOOG_LOGW(gti, "error: has been interrupted!\n");
			return ret;
		}
	}

	ret = goog_proc_heatmap_process(m, v, GTI_SENSOR_DATA_TYPE_SS_BASELINE);
	if (!ret)
		goog_proc_heatmap_show(m, v);

	if (!gti->manual_heatmap_from_irq)
		mutex_unlock(&gti->input_heatmap_lock);

	return ret;
}

static int goog_proc_ss_diff_show(struct seq_file *m, void *v)
{
	struct goog_touch_interface *gti = m->private;
	int ret;

	if (!gti->manual_heatmap_from_irq) {
		ret = mutex_lock_interruptible(&gti->input_heatmap_lock);
		if (ret) {
			seq_puts(m, "error: has been interrupted!\n");
			GOOG_LOGW(gti, "error: has been interrupted!\n");
			return ret;
		}
	}

	ret = goog_proc_heatmap_process(m, v, GTI_SENSOR_DATA_TYPE_SS_DIFF);
	if (!ret)
		goog_proc_heatmap_show(m, v);

	if (!gti->manual_heatmap_from_irq)
		mutex_unlock(&gti->input_heatmap_lock);

	return ret;
}

static int goog_proc_ss_raw_show(struct seq_file *m, void *v)
{
	struct goog_touch_interface *gti = m->private;
	int ret;

	if (!gti->manual_heatmap_from_irq) {
		ret = mutex_lock_interruptible(&gti->input_heatmap_lock);
		if (ret) {
			seq_puts(m, "error: has been interrupted!\n");
			GOOG_LOGW(gti, "error: has been interrupted!\n");
			return ret;
		}
	}

	ret = goog_proc_heatmap_process(m, v, GTI_SENSOR_DATA_TYPE_SS_RAW);
	if (!ret)
		goog_proc_heatmap_show(m, v);

	if (!gti->manual_heatmap_from_irq)
		mutex_unlock(&gti->input_heatmap_lock);

	return ret;
}

void gti_procfs_init(struct goog_touch_interface *gti)
{
	struct proc_dir_entry *gti_proc_dir_root =
		gim_get_interface_proc_root(GOOG_INTERFACE_TYPE_TOUCH);
	int type;

	if (!gti_proc_dir_root) {
		GOOG_ERR(gti, "No proc root to init!");
		return;
	}

	gti->proc_dir = proc_mkdir_data(dev_name(gti->dev), 0555, gti_proc_dir_root, gti);
	if (!gti->proc_dir) {
		GOOG_ERR(gti, "proc_mkdir_data failed!\n");
		return;
	}

	for (type = GTI_PROC_DUMP; type < GTI_PROC_NUM; type++) {
		char *name = gti_proc_name[type];

		if (gti_proc_show[type])
			gti->proc_show[type] = proc_create_single_data(name, 0555, gti->proc_dir,
								       gti_proc_show[type], gti);
		if (!gti->proc_show[type])
			GOOG_ERR(gti, "proc_create_single_data failed for %s!\n", name);
	}
}

/*-----------------------------------------------------------------------------
 * GTI/sysfs: structures and functions.
 */
static ssize_t config_name_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t force_active_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t force_active_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size);
static ssize_t fw_coord_filter_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fw_coord_filter_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size);
static ssize_t fw_grip_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fw_grip_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size);
static ssize_t fw_name_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fw_palm_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fw_palm_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size);
static ssize_t fw_ver_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fw_water_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t fw_water_store(struct device *dev, struct device_attribute *attr, const char *buf,
			      size_t size);
static ssize_t gesture_config_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t gesture_config_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size);
static ssize_t int2_mode_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t int2_mode_store(struct device *dev, struct device_attribute *attr, const char *buf,
			       size_t size);
static ssize_t int2_status_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t irq_enabled_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t irq_enabled_store(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t size);
static ssize_t mf_mode_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t mf_mode_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size);
static ssize_t offload_enabled_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t offload_enabled_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size);
static ssize_t offload_id_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t panel_id_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t ping_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t reset_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t reset_store(struct device *dev, struct device_attribute *attr, const char *buf,
			   size_t size);
static ssize_t scan_mode_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t scan_mode_store(struct device *dev, struct device_attribute *attr, const char *buf,
			       size_t size);
static ssize_t screen_protector_mode_enabled_store(struct device *dev,
						   struct device_attribute *attr, const char *buf,
						   size_t size);
static ssize_t screen_protector_mode_enabled_show(struct device *dev, struct device_attribute *attr,
						  char *buf);
static ssize_t self_test_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t sensing_enabled_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t sensing_enabled_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size);
static ssize_t test_limits_name_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t timestamp_correction_enabled_show(struct device *dev, struct device_attribute *attr,
						 char *buf);
static ssize_t timestamp_correction_enabled_store(struct device *dev, struct device_attribute *attr,
						  const char *buf, size_t size);
static ssize_t v4l2_enabled_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t v4l2_enabled_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size);
static ssize_t vsync_hsync_frequency_show(struct device *dev, struct device_attribute *attr,
					  char *buf);
static ssize_t interactive_calibrate_show(struct device *dev, struct device_attribute *attr,
					  char *buf);
static ssize_t interactive_calibrate_store(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size);
static ssize_t resample_latency_us_show(struct device *dev, struct device_attribute *attr,
					char *buf);
static ssize_t resample_latency_us_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size);
static ssize_t rr_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t rr_store(struct device *dev, struct device_attribute *attr, const char *buf,
			size_t size);

static DEVICE_ATTR_RO(config_name);
static DEVICE_ATTR_RW(force_active);
static DEVICE_ATTR_RW(fw_coord_filter);
static DEVICE_ATTR_RW(fw_grip);
static DEVICE_ATTR_RO(fw_name);
static DEVICE_ATTR_RW(fw_palm);
static DEVICE_ATTR_RO(fw_ver);
static DEVICE_ATTR_RW(fw_water);
static DEVICE_ATTR_RW(gesture_config);
static DEVICE_ATTR_RW(int2_mode);
static DEVICE_ATTR_RO(int2_status);
static DEVICE_ATTR_RW(irq_enabled);
static DEVICE_ATTR_RW(mf_mode);
static DEVICE_ATTR_RW(offload_enabled);
static DEVICE_ATTR_ADMIN_RO(offload_id);
static DEVICE_ATTR_ADMIN_RO(panel_id);
static DEVICE_ATTR_RO(ping);
static DEVICE_ATTR_RW(reset);
static DEVICE_ATTR_RW(scan_mode);
static DEVICE_ATTR_RW(screen_protector_mode_enabled);
static DEVICE_ATTR_RO(self_test);
static DEVICE_ATTR_RW(sensing_enabled);
static DEVICE_ATTR_RO(test_limits_name);
static DEVICE_ATTR_RW(timestamp_correction_enabled);
static DEVICE_ATTR_RW(v4l2_enabled);
static DEVICE_ATTR_RO(vsync_hsync_frequency);
static DEVICE_ATTR_RW(interactive_calibrate);
static DEVICE_ATTR_RW(resample_latency_us);
static DEVICE_ATTR_RW(rr);

static struct attribute *goog_attributes[] = {
	&dev_attr_config_name.attr,
	&dev_attr_force_active.attr,
	&dev_attr_fw_coord_filter.attr,
	&dev_attr_fw_grip.attr,
	&dev_attr_fw_name.attr,
	&dev_attr_fw_palm.attr,
	&dev_attr_fw_ver.attr,
	&dev_attr_fw_water.attr,
	&dev_attr_gesture_config.attr,
	&dev_attr_int2_mode.attr,
	&dev_attr_int2_status.attr,
	&dev_attr_irq_enabled.attr,
	&dev_attr_mf_mode.attr,
	&dev_attr_offload_enabled.attr,
	&dev_attr_offload_id.attr,
	&dev_attr_panel_id.attr,
	&dev_attr_ping.attr,
	&dev_attr_reset.attr,
	&dev_attr_scan_mode.attr,
	&dev_attr_screen_protector_mode_enabled.attr,
	&dev_attr_self_test.attr,
	&dev_attr_sensing_enabled.attr,
	&dev_attr_test_limits_name.attr,
	&dev_attr_timestamp_correction_enabled.attr,
	&dev_attr_v4l2_enabled.attr,
	&dev_attr_vsync_hsync_frequency.attr,
	&dev_attr_interactive_calibrate.attr,
	&dev_attr_resample_latency_us.attr,
	&dev_attr_rr.attr,
	NULL,
};

const struct attribute_group goog_attr_group = {
	.attrs = goog_attributes,
};

static ssize_t config_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (gti->config_name[0] == '\0') {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %s\n",
				     gti->config_name);
	}
	GOOG_INFO(gti, "%s", buf);

	return buf_idx;
}

static ssize_t force_active_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	ssize_t buf_idx = 0;
	bool locked = false;

	if (gti->ignore_force_active) {
		GOOG_LOGW(gti, "operation not supported!\n");
		return -EOPNOTSUPP;
	}

	locked = goog_pm_wake_check_locked(gti, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	buf_idx +=
		scnprintf(buf, PAGE_SIZE - buf_idx, "result: %s\n", locked ? "locked" : "unlocked");
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t force_active_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	u32 locked = 0;
	int ret = 0;

	if (buf == NULL || size < 0) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	if (kstrtou32(buf, 10, &locked)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	if (locked > 1) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	if (locked) {
		if (gti->ignore_force_active)
			GOOG_LOGW(gti, "operation not supported!\n");
		else
			ret = goog_pm_wake_lock(gti, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE, false);
	} else {
		if (gti->ignore_force_active)
			GOOG_LOGW(gti, "operation not supported!\n");
		else
			ret = goog_pm_wake_unlock(gti, GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE);
	}

	if (ret < 0) {
		GOOG_LOGE(gti, "error: %d!\n", ret);
		return ret;
	}
	return size;
}

static ssize_t fw_coord_filter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_coord_filter_cmd *cmd = &gti->cmd.coord_filter_cmd;

	cmd->setting = GTI_COORD_FILTER_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_COORD_FILTER_ENABLED);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx +=
			scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n", cmd->setting);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t fw_coord_filter_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size)
{
	int ret = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	bool fw_coord_filter;

	if (kstrtobool(buf, &fw_coord_filter)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	gti->cmd.coord_filter_cmd.setting = fw_coord_filter ? GTI_COORD_FILTER_ENABLE :
							      GTI_COORD_FILTER_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_COORD_FILTER_ENABLED);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "fw_coord_filter= %u\n", gti->cmd.coord_filter_cmd.setting);

	return size;
}

static ssize_t fw_grip_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_grip_cmd *cmd = &gti->cmd.grip_cmd;

	cmd->setting = GTI_GRIP_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_GRIP_MODE);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n",
				     cmd->setting | (gti->ignore_grip_update << 1));
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t fw_grip_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size)
{
	int ret = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	int fw_grip_mode = 0;
	bool enabled = false;

	if (kstrtou32(buf, 10, &fw_grip_mode)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	enabled = fw_grip_mode & 0x01;
	gti->ignore_grip_update = (fw_grip_mode >> 1) & 0x01;
	gti->cmd.grip_cmd.setting = enabled ? GTI_GRIP_ENABLE : GTI_GRIP_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_GRIP_MODE);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "fw_grip_mode: %u\n", fw_grip_mode);

	return size;
}

static ssize_t fw_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (gti->fw_name[0] == '\0') {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else {
		buf_idx +=
			scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %s\n", gti->fw_name);
	}
	GOOG_INFO(gti, "%s", buf);

	return buf_idx;
}

static ssize_t fw_palm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_palm_cmd *cmd = &gti->cmd.palm_cmd;

	cmd->setting = GTI_PALM_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_PALM_MODE);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n",
				     cmd->setting | (gti->ignore_palm_update << 1));
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t fw_palm_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size)
{
	int ret = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	int fw_palm_mode;
	bool enabled;

	if (kstrtou32(buf, 10, &fw_palm_mode)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	enabled = fw_palm_mode & 0x01;
	gti->ignore_palm_update = (fw_palm_mode >> 1) & 0x01;
	gti->cmd.palm_cmd.setting = enabled ? GTI_PALM_ENABLE : GTI_PALM_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_PALM_MODE);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "fw_palm_mode= %u\n", fw_palm_mode);

	return size;
}

static ssize_t fw_ver_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	memset(gti->cmd.fw_version_cmd.buffer, 0, sizeof(gti->cmd.fw_version_cmd.buffer));
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_FW_VERSION);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %s\n",
				     gti->cmd.fw_version_cmd.buffer);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t fw_water_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_water_cmd *cmd = &gti->cmd.water_cmd;

	cmd->setting = GTI_WATER_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_WATER_MODE);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n",
				     cmd->setting);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t fw_water_store(struct device *dev, struct device_attribute *attr, const char *buf,
			      size_t size)
{
	int ret = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	bool fw_water_mode = 0;

	if (kstrtobool(buf, &fw_water_mode)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	gti->cmd.water_cmd.setting = fw_water_mode ? GTI_WATER_ENABLE : GTI_WATER_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_WATER_MODE);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "fw_water_mode: %u\n", fw_water_mode);

	return size;
}

static ssize_t gesture_config_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	int i = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	for (i = 0; i < GTI_GESTURE_PARAMS_MAX; i++) {
		buf_idx += sysfs_emit_at(buf, buf_idx, "%s %u\n", gesture_params_list[i],
					 gti->cmd.gesture_config_cmd.params[i]);
	}

	return buf_idx;
}
static ssize_t gesture_config_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	char *p, *temp_buf, *token;
	u16 config = 0;
	int retval = 0;
	int i = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	temp_buf = kstrdup(buf, GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	goog_pm_wake_lock(gti, GTI_PM_WAKELOCK_TYPE_SYSFS, false);

	p = temp_buf;

	token = strsep(&p, " ");

	if (!token || *token == '\0' || !p) {
		retval = -EINVAL;
		goto exit;
	}

	if (kstrtou16(p, 10, &config)) {
		retval = -EINVAL;
		goto exit;
	}

	memset(gti->cmd.gesture_config_cmd.updating_params, 0, GTI_GESTURE_PARAMS_MAX);

	/* Set gesture parameters */
	for (i = 0; i < GTI_GESTURE_PARAMS_MAX; i++) {
		if (!strncmp(token, gesture_params_list[i], strlen(gesture_params_list[i]))) {
			gti->cmd.gesture_config_cmd.params[i] = config;
			gti->cmd.gesture_config_cmd.updating_params[i] = 1;
			tbn_update_gesture_config(i, config,
					gim_vendor_get_interface_dev_id(gti->vendor_dev));
			retval = goog_process_vendor_cmd(gti, GTI_CMD_SET_GESTURE_CONFIG);
			if (retval) {
				GOOG_ERR(gti, "Fail to set param %s, ret = %d!\n",
					 gesture_params_list[i], retval);
				retval = -EBADRQC;
				goto exit;
			}
			retval = size;
		}
	}

	if (retval == 0)
		retval = -EINVAL;

exit:
	goog_pm_wake_unlock(gti, GTI_PM_WAKELOCK_TYPE_SYSFS);
	kfree(temp_buf);
	return retval;
}

static ssize_t int2_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_int2_cmd *cmd = &gti->cmd.int2_cmd;

	cmd->setting = GTI_INT2_MODE_NA;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_INT2_MODE);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx +=
			scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n", cmd->setting);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t int2_mode_store(struct device *dev, struct device_attribute *attr, const char *buf,
			       size_t size)
{
	int ret = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	int mode = 0;

	if (kstrtou32(buf, 10, &mode)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	if (mode != GTI_INT2_MODE_KEEP_LOW && mode != GTI_INT2_MODE_KEEP_HIGH &&
	    mode != GTI_INT2_MODE_AUTO) {
		GOOG_ERR(gti, "Unsupported INT2 type, 0: keep low, 1: keep high, 2: auto");
		return size;
	}

	gti->cmd.int2_cmd.setting = mode;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_INT2_MODE);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "INT2 type: %d", mode);

	return size;
}

static ssize_t int2_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_int2_status_cmd *cmd = &gti->cmd.int2_status_cmd;

	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_INT2_STATUS);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx +=
			scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n", cmd->setting);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t irq_enabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	gti->cmd.irq_cmd.setting = GTI_IRQ_MODE_NA;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_IRQ_MODE);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n",
				     gti->cmd.irq_cmd.setting);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t irq_enabled_store(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t size)
{
	int ret;
	bool enabled;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (kstrtobool(buf, &enabled)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	gti->cmd.irq_cmd.setting = enabled;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_IRQ_MODE);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "irq_enabled= %u\n", gti->cmd.irq_cmd.setting);

	return size;
}

static ssize_t mf_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n", gti->mf_mode);
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t mf_mode_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	enum gti_mf_mode mode;
	int ret = 0;

	if (buf == NULL || size < 0) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	if (kstrtou32(buf, 10, &mode)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	gti->cmd.continuous_report_cmd.setting = (mode == GTI_MF_MODE_UNFILTER) ?
							 GTI_CONTINUOUS_REPORT_ENABLE :
							 GTI_CONTINUOUS_REPORT_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_CONTINUOUS_REPORT);
	if (ret == -EOPNOTSUPP) {
		GOOG_LOGE(gti, "error: not supported!\n");
	} else if (ret) {
		GOOG_LOGE(gti, "error: %d!\n", ret);
	} else {
		gti->mf_mode = mode;
		GOOG_LOGI(gti, "mf_mode= %u\n", gti->mf_mode);
	}

	return size;
}

static ssize_t offload_enabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	buf_idx +=
		scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %d\n", gti->offload_enabled);
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t offload_enabled_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (kstrtobool(buf, &gti->offload_enabled)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
	} else {
		GOOG_LOGI(gti, "offload_enabled= %d\n", gti->offload_enabled);
		/* Force to turn off offload by request. */
		if (!gti->offload_enabled)
			gti_offload_set_running(gti, false);
	}

	return size;
}

static ssize_t offload_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %c%c%c%c\n",
			     gti->offload_id_byte[0], gti->offload_id_byte[1],
			     gti->offload_id_byte[2], gti->offload_id_byte[3]);
	GOOG_INFO(gti, "%s", buf);
	return buf_idx;
}

static ssize_t panel_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (gti->panel_id < 0) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %d\n",
				     gti->panel_id);
	}
	GOOG_INFO(gti, "%s", buf);

	return buf_idx;
}

static ssize_t ping_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	gti->cmd.ping_cmd.setting = GTI_PING_ENABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_PING);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
		gti->cmd.ping_cmd.setting = GTI_PING_NA;
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
		gti->cmd.ping_cmd.setting = GTI_PING_NA;
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: success.\n");
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t reset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (gti->cmd.reset_cmd.setting == GTI_RESET_MODE_NOP ||
	    gti->cmd.reset_cmd.setting == GTI_RESET_MODE_NA) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n",
				     gti->cmd.reset_cmd.setting);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: success.\n");
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t reset_store(struct device *dev, struct device_attribute *attr, const char *buf,
			   size_t size)
{
	int ret;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	enum gti_reset_mode mode = 0;

	if (buf == NULL || size < 0) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	if (kstrtou32(buf, 10, &mode)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	if (mode <= GTI_RESET_MODE_NOP || mode > GTI_RESET_MODE_AUTO) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	gti->cmd.reset_cmd.setting = mode;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_RESET);
	if (ret == -EOPNOTSUPP) {
		GOOG_LOGE(gti, "error: not supported!\n");
		gti->cmd.reset_cmd.setting = GTI_RESET_MODE_NA;
	} else if (ret) {
		GOOG_LOGE(gti, "error: %d!\n", ret);
		gti->cmd.reset_cmd.setting = GTI_RESET_MODE_NA;
	} else {
		GOOG_LOGI(gti, "reset= 0x%x\n", mode);
	}

	return size;
}

static ssize_t scan_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	gti->cmd.scan_cmd.setting = GTI_SCAN_MODE_NA;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_SCAN_MODE);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n",
				     gti->cmd.scan_cmd.setting);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t scan_mode_store(struct device *dev, struct device_attribute *attr, const char *buf,
			       size_t size)
{
	int ret;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	enum gti_scan_mode mode = 0;

	if (buf == NULL || size < 0) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	if (kstrtou32(buf, 10, &mode)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	if (mode < GTI_SCAN_MODE_AUTO || mode > GTI_SCAN_MODE_LP_IDLE) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	gti->cmd.scan_cmd.setting = mode;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_SCAN_MODE);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "scan_mode= %u\n", mode);

	return size;
}

static ssize_t screen_protector_mode_enabled_store(struct device *dev,
						   struct device_attribute *attr, const char *buf,
						   size_t size)
{
	int ret = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_screen_protector_mode_cmd *cmd = &gti->cmd.screen_protector_mode_cmd;
	u32 dev_id = gim_vendor_get_interface_dev_id(gti->vendor_dev);
	bool enabled = false;

	if (kstrtobool(buf, &enabled)) {
		GOOG_LOGE(gti, "invalid input!\n");
		return -EINVAL;
	}

	if (dev_id < 0) {
		GOOG_LOGE(gti, "failed to get dev_id!\n");
		return -EINVAL;
	}

	cmd->setting = enabled ? GTI_SCREEN_PROTECTOR_MODE_ENABLE :
				 GTI_SCREEN_PROTECTOR_MODE_DISABLE;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_SCREEN_PROTECTOR_MODE);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "enabled= %u\n", enabled);
	gti->screen_protector_mode_setting = enabled ? GTI_SCREEN_PROTECTOR_MODE_ENABLE :
						       GTI_SCREEN_PROTECTOR_MODE_DISABLE;
	tbn_update_high_sensitivity_mode(gti->screen_protector_mode_setting, dev_id);
	return size;
}

static ssize_t screen_protector_mode_enabled_show(struct device *dev, struct device_attribute *attr,
						  char *buf)
{
	int ret = 0;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_screen_protector_mode_cmd *cmd = &gti->cmd.screen_protector_mode_cmd;

	cmd->setting = GTI_SCREEN_PROTECTOR_MODE_NA;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_SCREEN_PROTECTOR_MODE);
	if (ret == 0) {
		buf_idx += scnprintf(buf, PAGE_SIZE - buf_idx, "result: %d\n",
				     cmd->setting == GTI_SCREEN_PROTECTOR_MODE_ENABLE);
	} else {
		buf_idx += scnprintf(buf, PAGE_SIZE - buf_idx, "error: %d\n", ret);
	}
	GOOG_LOGI(gti, "%s", buf);
	return buf_idx;
}

static ssize_t self_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	gti->cmd.selftest_cmd.result = GTI_SELFTEST_RESULT_NA;
	gti->cmd.selftest_cmd.is_ical = false;
	memset(gti->cmd.selftest_cmd.buffer, 0, sizeof(gti->cmd.selftest_cmd.buffer));
	ret = gti_do_selftest(gti);
	if (ret == -EOPNOTSUPP) {
		buf_idx += sysfs_emit_at(buf, buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += sysfs_emit_at(buf, buf_idx, "error: %d!\n", ret);
	} else {
		switch (gti->cmd.selftest_cmd.result) {
		case GTI_SELFTEST_RESULT_PASS:
			buf_idx += sysfs_emit_at(buf, buf_idx, "result: PASS\n");
			buf_idx +=
				sysfs_emit_at(buf, buf_idx, "%s\n", gti->cmd.selftest_cmd.buffer);
			break;
		case GTI_SELFTEST_RESULT_FAIL:
			buf_idx += sysfs_emit_at(buf, buf_idx, "result: FAIL\n");
			buf_idx +=
				sysfs_emit_at(buf, buf_idx, "%s\n", gti->cmd.selftest_cmd.buffer);
			break;
		case GTI_SELFTEST_RESULT_SHELL_CMDS_REDIRECT:
			buf_idx += sysfs_emit_at(buf, buf_idx, "redirect: %s\n",
						 gti->cmd.selftest_cmd.buffer);
			break;
		default:
			buf_idx += sysfs_emit_at(buf, buf_idx, "error: N/A!\n");
			break;
		}
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t sensing_enabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	gti->cmd.sensing_cmd.setting = GTI_SENSING_MODE_NA;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_SENSING_MODE);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %u\n",
				     gti->cmd.sensing_cmd.setting);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t sensing_enabled_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size)
{
	int ret;
	bool enabled;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (kstrtobool(buf, &enabled)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	gti->cmd.sensing_cmd.setting = enabled;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_SENSING_MODE);
	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "sensing_enabled= %u\n", gti->cmd.sensing_cmd.setting);

	return size;
}

static ssize_t test_limits_name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (gti->test_limits_name[0] == '\0') {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %s\n",
				     gti->test_limits_name);
	}
	GOOG_INFO(gti, "%s", buf);

	return buf_idx;
}

static ssize_t timestamp_correction_enabled_show(struct device *dev, struct device_attribute *attr,
						 char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %d\n",
			     gti->timestamp_correction_enabled);
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t timestamp_correction_enabled_store(struct device *dev, struct device_attribute *attr,
						  const char *buf, size_t size)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (kstrtobool(buf, &gti->timestamp_correction_enabled))
		GOOG_LOGE(gti, "error: invalid input!\n");
	else
		GOOG_LOGI(gti, "timestamp_correction_enabled= %d\n",
			  gti->timestamp_correction_enabled);

	return size;
}

static ssize_t v4l2_enabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %d\n", gti->v4l2_enabled);
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t v4l2_enabled_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	if (kstrtobool(buf, &gti->v4l2_enabled))
		GOOG_LOGE(gti, "error: invalid input!\n");
	else
		GOOG_LOGI(gti, "v4l2_enabled= %d\n", gti->v4l2_enabled);

	return size;
}

static ssize_t vsync_hsync_frequency_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	int ret = 0;
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_vsync_hsync_frequency_cmd *cmd = &gti->cmd.vsync_hsync_frequency_cmd;

	cmd->vsync_value = 0;
	cmd->hsync_value = 0;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_TOUCH_VSYNC_HSYNC_FREQ);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx,
				     "vsync: %hu Hz, hsync: %hu kHz\n", cmd->vsync_value,
				     cmd->hsync_value);
	}
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

/* -----------------------------------------------------------------------
 * Interactive calibration states
 *
 * State "IDLE"/ 0 - idle, no operation underway => return to this state after
 * an error and after certain timeouts have elapsed
 *
 * State "INIT_X" / X01 (101, 201, ...) - operation is beginning. The client
 * has displayed warnings and will begin transitioning itself to the
 * "screen off" / "do not touch" state
 *
 * State "RUN_X" / X02 (102, 202, ...) - screen is off and nothing is touching
 * the screen. Operation can begin immediately when this state is entered
 *
 * State "END_X" / X03 (103, 203, ...) - the client has waited the designated
 * time and will assume operation is complete, will read the status of this
 * state as the final operation status. Transition back to the IDLE state will
 * occur automatically.
 */
static bool ical_state_idle(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	/* Valid next-states are 'INIT_X', or 'IDLE' */
	if (next_state == ICAL_STATE_IDLE) {
		gti->ical_result = ICAL_RES_SUCCESS;
		gti->ical_func_result = ICAL_RES_SUCCESS;
		/* Do not update the ical timestamp */
		return false;
	} else if ((next_state == ICAL_STATE_INIT_CAL || next_state == ICAL_STATE_INIT_TEST ||
		    next_state == ICAL_STATE_INIT_RESET) &&
		   elapsed > MIN_DELAY_IDLE) {
		gti->ical_state = next_state;
		gti->ical_result = ICAL_RES_SUCCESS;
	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_IDLE, elapsed);
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL;
	}
	return true;
}

static void ical_state_init_cal(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	int pm_ret = 0;
	u32 ret;

	pm_ret = goog_pm_wake_lock(gti, GTI_PM_WAKELOCK_TYPE_SYSFS, false);
	if (pm_ret < 0 && gti->tbn_enabled) {
		GOOG_ERR(gti, "ical - error: invalid touch bus access!\n");
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL_INVALID_BUS_ACCESS;
		return;
	}

	/* only valid next-state is 'RUN_CAL', as long as time elapsed
	 * is within range. When 'RUN_CAL' is received calibration begins.
	 */
	if (next_state == ICAL_STATE_RUN_CAL && elapsed > MIN_DELAY_INIT_CAL &&
	    elapsed < MAX_DELAY_INIT_CAL) {
		/* Begin calibration */

		gti->cmd.calibrate_cmd.result = GTI_CALIBRATE_RESULT_NA;
		memset(gti->cmd.calibrate_cmd.buffer, 0, sizeof(gti->cmd.calibrate_cmd.buffer));
		ret = goog_process_vendor_cmd(gti, GTI_CMD_CALIBRATE);
		if (ret == 0) {
			if (gti->cmd.calibrate_cmd.result == GTI_CALIBRATE_RESULT_DONE) {
				gti->ical_func_result = gti->cmd.calibrate_cmd.result;
				GOOG_INFO(gti, "ical - CALIBRATE_RESULT_DONE - [%s]\n",
					  gti->cmd.calibrate_cmd.buffer);
			} else {
				gti->ical_func_result = ICAL_RES_FAIL;
				GOOG_ERR(gti, "ical - calibrate result other/fail - N/A or [%s]\n",
					 gti->cmd.calibrate_cmd.buffer);
			}

			gti->ical_state = ICAL_STATE_RUN_CAL;
			gti->ical_result = ICAL_RES_SUCCESS;
		} else {
			GOOG_ERR(gti, "ical - GTI_CMD_CALIBRATE fail(%d)\n", ret);
			gti->ical_state = ICAL_STATE_IDLE;
			gti->ical_result = ICAL_RES_FAIL;
		}
	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_INIT_CAL, elapsed,
			MAX_DELAY_INIT_CAL);
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL;
	}

	if (pm_ret == 0)
		goog_pm_wake_unlock_nosync(gti, GTI_PM_WAKELOCK_TYPE_SYSFS);
}

static void ical_state_run_cal(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	/* only valid next-state is 'END_CAL', as long as time elapsed
	 * is within ranged.
	 */
	if (next_state == ICAL_STATE_END_CAL && elapsed > MIN_DELAY_RUN_CAL &&
	    elapsed < MAX_DELAY_RUN_CAL) {
		GOOG_INFO(gti, "ical - Calibration complete after %lluns\n", elapsed);

		gti->ical_state = ICAL_STATE_END_CAL;
		gti->ical_result = ICAL_RES_SUCCESS;

	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_RUN_CAL, elapsed, MAX_DELAY_RUN_CAL);
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL;
	}
}

static void ical_state_end_cal(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	/* Nothing to do but accept a transition back to IDLE.
	 * Necessary because the interface only executes when called
	 */
	if (next_state == ICAL_STATE_IDLE && elapsed > MIN_DELAY_END_CAL &&
	    elapsed < MAX_DELAY_END_CAL) {
		gti->ical_result = ICAL_RES_SUCCESS;
	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_END_CAL, elapsed, MAX_DELAY_END_CAL);
		gti->ical_result = ICAL_RES_FAIL;
	}
	gti->ical_state = ICAL_STATE_IDLE;
}

static void ical_state_init_test(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	int pm_ret = 0;
	u32 ret;

	pm_ret = goog_pm_wake_lock(gti, GTI_PM_WAKELOCK_TYPE_SYSFS, false);
	if (pm_ret < 0 && gti->tbn_enabled) {
		GOOG_ERR(gti, "ical - error: invalid touch bus access!\n");
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL_INVALID_BUS_ACCESS;
		return;
	}

	/* only valid next-state is 'RUN_TEST', as long as time elapsed
	 * is within range. When 'RUN_TEST' is received test begins.
	 */
	if (next_state == ICAL_STATE_RUN_TEST && elapsed > MIN_DELAY_INIT_TEST &&
	    elapsed < MAX_DELAY_INIT_TEST) {
		/* Begin selftest */

		gti->cmd.selftest_cmd.result = GTI_SELFTEST_RESULT_NA;
		gti->cmd.selftest_cmd.is_ical = true;
		memset(gti->cmd.selftest_cmd.buffer, 0, sizeof(gti->cmd.selftest_cmd.buffer));
		ret = gti_do_selftest(gti);
		if (ret == 0) {
			if (gti->cmd.selftest_cmd.result == GTI_SELFTEST_RESULT_DONE) {
				gti->ical_func_result = gti->cmd.selftest_cmd.result;

				GOOG_INFO(gti, "ical - SELFTEST_RESULT_DONE - [%s]\n",
					  gti->cmd.selftest_cmd.buffer);
			} else if (gti->cmd.selftest_cmd.result ==
				   GTI_SELFTEST_RESULT_SHELL_CMDS_REDIRECT) {
				gti->ical_func_result = ICAL_RES_SUCCESS;

				GOOG_ERR(gti, "ical - SELFTEST_RESULT_SHELL_CMDS_REDIRECT - [%s]\n",
					 gti->cmd.selftest_cmd.buffer);
			} else {
				gti->ical_func_result = ICAL_RES_FAIL;

				GOOG_ERR(gti, "ical - selftest result other/fail - N/A or [%s]\n",
					 gti->cmd.selftest_cmd.buffer);
			}

			gti->ical_state = ICAL_STATE_RUN_TEST;
			gti->ical_result = ICAL_RES_SUCCESS;
		} else {
			GOOG_ERR(gti, "ical - GTI_CMD_SELFTEST fail(%d)\n", ret);
			gti->ical_state = ICAL_STATE_IDLE;
			gti->ical_result = ICAL_RES_FAIL;
		}
	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_INIT_TEST, elapsed,
			MAX_DELAY_INIT_TEST);
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL;
	}

	if (pm_ret == 0)
		goog_pm_wake_unlock_nosync(gti, GTI_PM_WAKELOCK_TYPE_SYSFS);
}

static void ical_state_run_test(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	/* only valid next-state is 'END_TEST', as long as time elapsed
	 * is within ranged.
	 */
	if (next_state == ICAL_STATE_END_TEST && elapsed > MIN_DELAY_RUN_TEST &&
	    elapsed < MAX_DELAY_RUN_TEST) {
		/* Check and evaluate self-test here */
		gti->ical_state = ICAL_STATE_END_TEST;
		gti->ical_result = ICAL_RES_SUCCESS;

	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_RUN_TEST, elapsed,
			MAX_DELAY_RUN_TEST);
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL;
	}
}

static void ical_state_end_test(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	/* Nothing to do but accept a transition back to IDLE.
	 * Necessary because the interface only executes when called
	 */
	if (next_state == ICAL_STATE_IDLE && elapsed > MIN_DELAY_END_TEST &&
	    elapsed < MAX_DELAY_END_TEST) {
		gti->ical_result = ICAL_RES_SUCCESS;
	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_END_TEST, elapsed,
			MAX_DELAY_END_TEST);
		gti->ical_result = ICAL_RES_FAIL;
	}
	gti->ical_state = ICAL_STATE_IDLE;
}

static void ical_state_init_reset(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	int pm_ret = 0;
	u32 ret;

	pm_ret = goog_pm_wake_lock(gti, GTI_PM_WAKELOCK_TYPE_SYSFS, false);
	if (pm_ret < 0 && gti->tbn_enabled) {
		GOOG_ERR(gti, "ical - error: invalid touch bus access!\n");
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL_INVALID_BUS_ACCESS;
		return;
	}

	/* only valid next-state is 'RUN_RESET', as long as time elapsed
	 * is within range. When 'RUN_RESET' is received reset begins.
	 */
	if (next_state == ICAL_STATE_RUN_RESET && elapsed > MIN_DELAY_INIT_RESET &&
	    elapsed < MAX_DELAY_INIT_RESET) {
		/* Begin reset */
		gti->cmd.reset_cmd.setting = GTI_RESET_MODE_AUTO;
		ret = goog_process_vendor_cmd(gti, GTI_CMD_RESET);
		if (ret == 0) {
			GOOG_INFO(gti, "ical - RESET_DONE\n");
			gti->ical_state = ICAL_STATE_RUN_RESET;
			gti->ical_func_result = ICAL_RES_SUCCESS;
			gti->ical_result = ICAL_RES_SUCCESS;
		} else {
			GOOG_ERR(gti, "ical - GTI_CMD_RESET fail(%d)\n", ret);
			gti->ical_state = ICAL_STATE_IDLE;
			gti->ical_func_result = ICAL_RES_NA;
			gti->ical_result = ICAL_RES_FAIL;
		}
	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_INIT_RESET, elapsed,
			MAX_DELAY_INIT_RESET);
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL;
	}

	if (pm_ret == 0)
		goog_pm_wake_unlock_nosync(gti, GTI_PM_WAKELOCK_TYPE_SYSFS);
}

static void ical_state_run_reset(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	/* only valid next-state is 'END_RESET', as long as time elapsed
	 * is within ranged.
	 */
	if (next_state == ICAL_STATE_END_RESET && elapsed > MIN_DELAY_RUN_RESET &&
	    elapsed < MAX_DELAY_RUN_RESET) {
		/* Check and evaluate reset here */
		gti->ical_state = ICAL_STATE_END_RESET;
		gti->ical_result = ICAL_RES_SUCCESS;
	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_RUN_RESET, elapsed,
			MAX_DELAY_RUN_RESET);
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_FAIL;
	}
}

static void ical_state_end_reset(struct goog_touch_interface *gti, u32 next_state, u64 elapsed)
{
	/* Nothing to do but accept a transition back to IDLE.
	 * Necessary because the interface only executes when called
	 */
	if (next_state == ICAL_STATE_IDLE && elapsed > MIN_DELAY_END_RESET &&
	    elapsed < MAX_DELAY_END_RESET) {
		gti->ical_result = ICAL_RES_SUCCESS;
	} else {
		GOOG_ERR(
			gti,
			"ical - error: invalid transition or time! %u => %u, min=%lluns, t=%lluns, max=%lluns\n",
			gti->ical_state, next_state, MIN_DELAY_END_RESET, elapsed,
			MAX_DELAY_END_RESET);
		gti->ical_result = ICAL_RES_FAIL;
	}
	gti->ical_state = ICAL_STATE_IDLE;
}

/* Advance the interactive calibration state machine */
static ssize_t interactive_calibrate_store(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	u32 next_state = 0;
	u64 entry_time = ktime_get_ns();
	u64 elapsed = entry_time - gti->ical_timestamp_ns;

	if (kstrtou32(buf, 10, &next_state)) {
		GOOG_ERR(gti, "error: invalid input!\n");
		return size;
	}

	GOOG_INFO(gti, "ical - [%u] start\n", next_state);

	switch (gti->ical_state) {
	case ICAL_STATE_IDLE:
		if (ical_state_idle(gti, next_state, elapsed))
			gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_INIT_CAL:
		ical_state_init_cal(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_RUN_CAL:
		ical_state_run_cal(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_END_CAL:
		ical_state_end_cal(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_INIT_TEST:
		ical_state_init_test(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_RUN_TEST:
		ical_state_run_test(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_END_TEST:
		ical_state_end_test(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_INIT_RESET:
		ical_state_init_reset(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_RUN_RESET:
		ical_state_run_reset(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	case ICAL_STATE_END_RESET:
		ical_state_end_reset(gti, next_state, elapsed);
		gti->ical_timestamp_ns = entry_time;
		break;

	default:
		GOOG_ERR(gti, "ical - unknown/invalid current state = %u, but will go back to 0.\n",
			 gti->ical_state);
		gti->ical_state = ICAL_STATE_IDLE;
		gti->ical_result = ICAL_RES_SUCCESS;
		break;
	}

	return size;
}

/* Show result/status of the calibrate state machine */
static ssize_t interactive_calibrate_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	buf_idx +=
		sysfs_emit_at(buf, buf_idx, "%d - %d\n", gti->ical_result, gti->ical_func_result);

	GOOG_INFO(gti, "ical - [%u](%d, %d) return\n", gti->ical_state, gti->ical_result,
		  gti->ical_func_result);

	return buf_idx;
}

static ssize_t resample_latency_us_store(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	u32 resample_latency_us = 0;

	if (buf == NULL || size < 0) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	if (kstrtou32(buf, 10, &resample_latency_us)) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	if (resample_latency_us > 100 * USEC_PER_MSEC) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return size;
	}

	gti->resample_latency = ns_to_ktime(resample_latency_us * NSEC_PER_USEC);
	GOOG_LOGI(gti, "resample_latency= %llu ns\n", gti->resample_latency);

	return size;
}

static ssize_t resample_latency_us_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	ssize_t buf_idx = 0;
	struct goog_touch_interface *gti = dev_get_drvdata(dev);

	buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %llu\n",
			     gti->resample_latency);
	GOOG_LOGI(gti, "%s", buf);

	return buf_idx;
}

static ssize_t rr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	struct gti_report_rate_cmd *cmd = &gti->cmd.report_rate_cmd;
	ssize_t buf_idx = 0;
	int ret = 0;

	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_REPORT_RATE);
	if (ret == -EOPNOTSUPP) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: not supported!\n");
	} else if (ret) {
		buf_idx += scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "error: %d!\n", ret);
	} else {
		buf_idx +=
			scnprintf(buf + buf_idx, PAGE_SIZE - buf_idx, "result: %d", cmd->setting);
	}

	return buf_idx;
}

static ssize_t rr_store(struct device *dev, struct device_attribute *attr, const char *buf,
			size_t size)
{
	struct goog_touch_interface *gti = dev_get_drvdata(dev);
	u32 value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret) {
		GOOG_LOGE(gti, "error: invalid input!\n");
		return -EINVAL;
	}

	struct gti_report_rate_cmd *cmd = &gti->cmd.report_rate_cmd;

	cmd->setting = value;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_REPORT_RATE);

	if (ret == -EOPNOTSUPP)
		GOOG_LOGE(gti, "error: not supported!\n");
	else if (ret)
		GOOG_LOGE(gti, "error: %d!\n", ret);
	else
		GOOG_LOGI(gti, "report_rate: %u\n", gti->cmd.report_rate_cmd.setting);

	return size;
}

int gti_sysfs_create_vendor_input_link(struct goog_touch_interface *gti)
{
	int ret = 0;

	if (gti && gti->dev && gti->vendor_input_dev) {
		ret = sysfs_create_link(&gti->dev->kobj, &gti->vendor_input_dev->dev.kobj,
					"vendor_input");
		if (ret)
			GOOG_ERR(gti, "sysfs_create_link() failed for vendor_input, ret=%d!\n",
				 ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gti_sysfs_create_vendor_input_link);

int gti_sysfs_create_vendor_link(struct goog_touch_interface *gti)
{
	int ret = 0;

	if (gti && gti->dev && gti->vendor_dev) {
		ret = sysfs_create_link(&gti->dev->kobj, &gti->vendor_dev->kobj, "vendor");
		if (ret)
			GOOG_ERR(gti, "sysfs_create_link() failed for vendor, ret=%d!\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gti_sysfs_create_vendor_link);

int gti_sysfs_init(struct goog_touch_interface *gti)
{
	if (!gti || !gti->dev)
		return -ENODEV;

	return devm_device_add_group(gti->dev, &goog_attr_group);
}
