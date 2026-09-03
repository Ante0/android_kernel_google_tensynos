// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */
#include <linux/io.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "google_powerdashboard_helper.h"
#include "soc/google/google_gtc.h"

uint64_t get_ms_from_ticks(uint64_t ticks)
{
	return ticks / powerdashboard_constants.gtc_ticks_per_ms;
}

int google_read_section(struct google_powerdashboard *pd,
			enum pd_section section)
{
	void *data_read_first, *data_read_second;
	int ret = 0, i;

	if (!powerdashboard_iface.sections.section_ptrs[section])
		return -EFAULT;

	data_read_first =
		kzalloc(powerdashboard_iface.sections.section_sizes[section],
			GFP_KERNEL);
	if (!data_read_first) {
		ret = -ENOMEM;
		goto out;
	}
	data_read_second =
		kzalloc(powerdashboard_iface.sections.section_sizes[section],
			GFP_KERNEL);
	if (!data_read_second) {
		ret = -ENOMEM;
		goto out_free_first;
	}
	for (i = 0; i < powerdashboard_constants.read_time; ++i) {
		memcpy_fromio(data_read_first,
			      powerdashboard_iface.sections
				      .section_bases[section],
			      powerdashboard_iface.sections
				      .section_sizes[section]);
		memcpy_fromio(data_read_second,
			      powerdashboard_iface.sections
				      .section_bases[section],
			      powerdashboard_iface.sections
				      .section_sizes[section]);
		if (!memcmp(data_read_first, data_read_second,
			    powerdashboard_iface.sections
				    .section_sizes[section]))
			break;
	}
	if (i == powerdashboard_constants.read_time) {
		dev_warn(pd->dev, "Read/Write Collisions\n");
		ret = -EAGAIN;
		goto out_free_both;
	}
	powerdashboard_iface.sections
		.section_copy_funcs[section](data_read_second);

out_free_both:
	kfree(data_read_second);
out_free_first:
	kfree(data_read_first);
out:
	return ret;
}

ssize_t google_pwrblk_show(char *buf, struct google_powerdashboard *pd,
			   int id, int psm_num)
{
	struct pd_pwrblk_power_state *pb_data =
		powerdashboard_iface.sections.pwrblk_section->pwrblk_power_states[id];
	const struct pd_attr_str *power_state_names = powerdashboard_iface.attrs
						      .pwrblk_power_state_names;
	ssize_t len = 0;
	int i, j;
	u8 curr_power_state;

	for (i = 0; i < psm_num; ++i) {
		if (i == 0)
			len += sysfs_emit_at(buf, len, "\nSSWRP lpb\n");
		else
			len += sysfs_emit_at(buf, len, "psm[%d]\n", i - 1);

		curr_power_state = pb_data[i].curr_power_state;

		if (curr_power_state < power_state_names->count)
			len += sysfs_emit_at(
				buf, len, "current_power_state: %s\n\n",
				power_state_names->values[curr_power_state]);
		else
			len += sysfs_emit_at(buf, len, "current_power_state: %d\n\n",
					    (u32)curr_power_state);

		for (j = 0;
		     j <
		     powerdashboard_iface.attrs.pwrblk_power_state_names->count;
		     ++j) {
			len += sysfs_emit_at(buf, len, "power_state: %s\n",
					     powerdashboard_iface.attrs
						     .pwrblk_power_state_names
						     ->values[j]);
			len += sysfs_emit_at(buf, len, "entry_count: %llu\n",
					     (u64)pb_data[i]
						     .power_state_res[j]
						     .entry_count);
			len += sysfs_emit_at(buf, len,
					     "time_in_state: %llu (us)\n",
					     pb_data[i]
						     .power_state_res[j]
						     .time_in_state);
			len += sysfs_emit_at(buf, len,
					     "last_entry_timestamp: %llu\n",
					     pb_data[i]
						     .power_state_res[j]
						     .last_entry_ts);
			len += sysfs_emit_at(buf, len,
					     "last_exit_timestamp: %llu\n",
					     pb_data[i]
						     .power_state_res[j]
						     .last_exit_ts);
		}

		len += sysfs_emit_at(buf, len, "\n");
	}

	return len;
}
