// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2023 Google LLC */
#include <linux/debugfs.h>

#include "google_thermal_cooling.h"
#include "google_thermal_debugfs.h"
#include "thermal/cpm_thermal.h"

DEFINE_THERMAL_PID_PARAM_DEBUGFS_ATTRIBUTE(k_po, THERMAL_PARAM_K_PO);
DEFINE_THERMAL_PID_PARAM_DEBUGFS_ATTRIBUTE(k_pu, THERMAL_PARAM_K_PU);
DEFINE_THERMAL_PID_PARAM_DEBUGFS_ATTRIBUTE(k_i, THERMAL_PARAM_K_I);
DEFINE_THERMAL_PID_PARAM_DEBUGFS_ATTRIBUTE(i_max, THERMAL_PARAM_I_MAX);
DEFINE_THERMAL_GRADUAL_PARAM_DEBUGFS_ATTRIBUTE(irq_gradual_gain, THERMAL_PARAM_IRQ_GAIN);
DEFINE_THERMAL_GRADUAL_PARAM_DEBUGFS_ATTRIBUTE(timer_gradual_gain, THERMAL_PARAM_TIMER_GAIN);

static ssize_t state2power_table_write(struct file *filp,
				       const char __user *buf,
				       size_t count,
				       loff_t *ppos)
{
	struct thermal_data *data = filp->private_data;
	struct thermal_zone_device *tz = data->tz;
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;
	struct thermal_cooling_device *power_actor = tz2poweractor(tz);
	char write_buf[THERMAL_DEBUGFS_BUF_SIZE];
	unsigned long max_state;
	ssize_t written_to_buffer;
	char **argv;
	int argc;
	int ret;

	/* Empty string or just a newline */
	if (count <= 1) {
		dev_err(data->dev, "Empty string was passed.\n");
		ret = -EINVAL;
	}

	if (count > THERMAL_DEBUGFS_BUF_SIZE) {
		dev_err(data->dev, "User's input is too long.\n");
		ret = -ETOOSMALL;
	}

	if (!power_actor)
		return -ENODEV;

	written_to_buffer = simple_write_to_buffer(write_buf,
						   THERMAL_DEBUGFS_BUF_SIZE, ppos,
						   buf,
						   count);
	if (written_to_buffer < 0) {
		dev_err(data->dev, "write to buffer error: %zd\n", written_to_buffer);
		return written_to_buffer;
	}

	dev_dbg(data->dev, "User input: %s, written_to_buffer: %zu\n", write_buf,
		written_to_buffer);

	argv = argv_split(GFP_KERNEL, buf, &argc);
	if (!argv) {
		dev_err(data->dev, "argv_split error: %ld\n", PTR_ERR(argv));
		return -EINVAL;
	}

	power_actor->ops->get_max_state(power_actor, &max_state);
	if (argc != (int)(max_state + 1)) {
		dev_err(data->dev, "invalid state to power table number\n");
		ret = -EINVAL;
		goto free_argv;
	} else {
		int i;

		for (i = 0; i <= (int)max_state; ++i) {
			int val;

			ret = kstrtou32(argv[i], 10, &val);
			if (ret)
				goto free_argv;
			cpm_thermal->ops->set_powertable(cpm_thermal, data->id, i, val);
		}
	}

	ret = count;

free_argv:
	argv_free(argv);
	return ret;
}

static ssize_t state2power_table_read(struct file *filp,
				      char __user *buf,
				      size_t count,
				      loff_t *ppos)
{
	struct thermal_data *data = filp->private_data;
	struct thermal_zone_device *tz = data->tz;
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;
	struct thermal_cooling_device *power_actor = tz2poweractor(tz);
	char read_buf[THERMAL_DEBUGFS_BUF_SIZE];
	unsigned long max_state;
	int len = 0;
	int i;
	ssize_t ret;

	power_actor = tz2poweractor(tz);

	if (!power_actor)
		return -ENODEV;

	power_actor->ops->get_max_state(power_actor, &max_state);

	for (i = 0; i <= max_state; ++i) {
		u32 power;

		cpm_thermal->ops->get_powertable(cpm_thermal, data->id, i, &power);
		len += scnprintf(buf + len, THERMAL_DEBUGFS_BUF_SIZE - len, "%u ", power);
	}
	len += scnprintf(buf + len, THERMAL_DEBUGFS_BUF_SIZE - len, "\n");

	ret = simple_read_from_buffer(buf, count, ppos, read_buf, THERMAL_DEBUGFS_BUF_SIZE);

	return ret;
}

static const struct file_operations state2power_table_fops = {
	.open = simple_open,
	.write = state2power_table_write,
	.read = state2power_table_read,
};

static int polling_delay_set(void *data, u64 val)
{
	struct thermal_data *thermal_data = data;
	struct google_cpm_thermal *cpm_thermal = thermal_data->cpm_thermal;

	if (val > U16_MAX) {
		dev_warn(thermal_data->dev, "cap polling_delay to u16_max\n");
		val = U16_MAX;
	}

	thermal_data->cpm_polling_delay_ms = val;

	return cpm_thermal->ops->set_polling_delay(cpm_thermal, thermal_data->id, val);
}

static int polling_delay_get(void *data, u64 *val)
{
	struct thermal_data *thermal_data = data;

	*val = thermal_data->cpm_polling_delay_ms;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cpm_polling_delay_ms, polling_delay_get, polling_delay_set, "%llu\n");

void thermal_init_debugfs(struct thermal_data *data)
{
	struct device *dev = data->dev;

	data->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	debugfs_create_file("k_po", 0660, data->debugfs_root,
			    data, &k_po);
	debugfs_create_file("k_pu", 0660, data->debugfs_root,
			    data, &k_pu);
	debugfs_create_file("k_i", 0660, data->debugfs_root,
			    data, &k_i);
	debugfs_create_file("i_max", 0660, data->debugfs_root,
			    data, &i_max);
	debugfs_create_file("irq_gradual_gain", 0660, data->debugfs_root,
			    data, &irq_gradual_gain);
	debugfs_create_file("timer_gradual_gain", 0660, data->debugfs_root,
			    data, &timer_gradual_gain);
	debugfs_create_file("state2power_table", 0660, data->debugfs_root,
			    data, &state2power_table_fops);
	debugfs_create_file("cpm_polling_delay_ms", 0660, data->debugfs_root, data,
			    &cpm_polling_delay_ms);
}
