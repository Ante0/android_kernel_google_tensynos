// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_tmu_debugfs_helper.c Helper for TMU debugfs functionality.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <kunit/visibility.h>
#include "thermal_tmu_debugfs_helper.h"
#include "thermal_tmu_mock.h"

#define TMU_DEBUGFS_ROOT "tmu"
#define TMU_DEBUGFS_BUF_SIZE 128

static struct dentry *tmu_debugfs_root;

/*
 * DEFINE_TMU_GOV_PARAM_DEBUGFS_ATTRIBUTE
 *
 * This macro create debugfs attribute for a tmu gov parameter
 *
 * @__gov: name of the governor
 * @__param: name of the parameter
 * @__type: parameter type from enum tmu_gov_param_type
 */
#define DEFINE_TMU_GOV_PARAM_DEBUGFS_ATTRIBUTE(__gov, __param, __type)			\
int thermal_tmu_debugfs_##__gov##_##__param##_set(void *data, u64 val)			\
{											\
	struct google_sensor_data *sensor_data = data;					\
	struct tmu_sensor_data *tmu_data = sensor_data->sens_priv_data;			\
	u8 tz_id;									\
	int ret;									\
	if (!tmu_data)									\
		return -EINVAL;								\
	tz_id = (u8)tmu_data->hw_tzid;							\
	ret = tmu_set_gov_param(tz_id, __type, val);					\
	if (!ret)									\
		tmu_data->__gov##_param.__param = val;					\
	return ret;									\
}											\
int thermal_tmu_debugfs_##__gov##_##__param##_get(void *data, u64 *val)			\
{											\
	struct google_sensor_data *sensor_data = data;					\
	struct tmu_sensor_data *tmu_data = sensor_data->sens_priv_data;			\
	int val_s32;									\
	if (!tmu_data)									\
		return -EINVAL;								\
	val_s32 = tmu_data->__gov##_param.__param;					\
	*val = val_s32;									\
	return 0;									\
}											\
DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(__gov##_##__param,					\
				thermal_tmu_debugfs_##__gov##_##__param##_get,		\
				thermal_tmu_debugfs_##__gov##_##__param##_set, "%lld\n")

DEFINE_TMU_GOV_PARAM_DEBUGFS_ATTRIBUTE(gradual, irq_gain, TMU_GOV_PARAM_IRQ_GAIN);
DEFINE_TMU_GOV_PARAM_DEBUGFS_ATTRIBUTE(gradual, timer_gain, TMU_GOV_PARAM_TIMER_GAIN);
DEFINE_TMU_GOV_PARAM_DEBUGFS_ATTRIBUTE(pi, k_po, TMU_GOV_PARAM_K_PO);
DEFINE_TMU_GOV_PARAM_DEBUGFS_ATTRIBUTE(pi, k_pu, TMU_GOV_PARAM_K_PU);
DEFINE_TMU_GOV_PARAM_DEBUGFS_ATTRIBUTE(pi, k_i, TMU_GOV_PARAM_K_I);
DEFINE_TMU_GOV_PARAM_DEBUGFS_ATTRIBUTE(early_throttle, k_p, TMU_GOV_PARAM_EARLY_THROTTLE_K_P);

/*
 * DEFINE_TMU_GOV_SELECT_DEBUGFS_ATTRIBUTE
 *
 * This macro create debugfs attribute for a tmu gov selector
 *
 * @__gov: name of the governor
 * @__bitmask: selector bitmask of the governor, member of enum tmu_gov_select_bit_offset
 */
#define DEFINE_TMU_GOV_SELECT_DEBUGFS_ATTRIBUTE(__gov, __bitmask)			\
int thermal_tmu_debugfs_##__gov##_select_set(void *data, u64 val)			\
{											\
	struct google_sensor_data *sensor_data = data;					\
	struct tmu_sensor_data *tmu_data = sensor_data->sens_priv_data;			\
	u8 tz_id;									\
	unsigned long gov_select;							\
	int ret;									\
	if (!tmu_data)									\
		return -EINVAL;								\
	tz_id = (u8)tmu_data->hw_tzid;							\
	gov_select = tmu_data->gov_select;						\
	if (val == 0)									\
		clear_bit(__bitmask, &gov_select);					\
	else										\
		set_bit(__bitmask, &gov_select);					\
	ret = tmu_set_gov_select(tz_id, (u8)gov_select);				\
	if (!ret)									\
		tmu_data->gov_select = gov_select;					\
	return ret;									\
}											\
int thermal_tmu_debugfs_##__gov##_select_get(void *data, u64 *val)			\
{											\
	struct google_sensor_data *sensor_data = data;					\
	struct tmu_sensor_data *tmu_data = sensor_data->sens_priv_data;			\
	if (!tmu_data)									\
		return -EINVAL;								\
	*val = test_bit(__bitmask, &tmu_data->gov_select) ? 1 : 0;			\
	return 0;									\
}											\
DEFINE_DEBUGFS_ATTRIBUTE(gov_##__gov##_select,						\
			thermal_tmu_debugfs_##__gov##_select_get,			\
			thermal_tmu_debugfs_##__gov##_select_set, "%lld\n")

DEFINE_TMU_GOV_SELECT_DEBUGFS_ATTRIBUTE(gradual, TMU_GOV_GRADUAL_SELECT);
DEFINE_TMU_GOV_SELECT_DEBUGFS_ATTRIBUTE(pi, TMU_GOV_PI_SELECT);
DEFINE_TMU_GOV_SELECT_DEBUGFS_ATTRIBUTE(temp_lut, TMU_GOV_TEMP_LUT_SELECT);
DEFINE_TMU_GOV_SELECT_DEBUGFS_ATTRIBUTE(hardlimit_via_pid, TMU_GOV_HARDLIMIT_VIA_PID_SELECT);
DEFINE_TMU_GOV_SELECT_DEBUGFS_ATTRIBUTE(early_throttle, TMU_GOV_EARLY_THROTTLE_SELECT);

/*
 * thermal_tmu_debugfs_polling_delay_ms_set: setter for polling_delay debugfs node
 *
 * @data: pointer to debugfs node data
 * @val: polling_delay_ms to set
 *
 * Return: return code of thermal_tmu_set_polling_delay_ms
 */
int thermal_tmu_debugfs_polling_delay_ms_set(void *data, u64 val)
{
	struct google_sensor_data *sensor = data;

	return thermal_tmu_set_polling_delay_ms(sensor, (u32)val);
}

/*
 * thermal_tmu_debugfs_polling_delay_ms_get: getter for polling_delay debugfs node
 *
 * @data: pointer to debugfs node data
 * @val: pointer to polling_delay_ms to get value
 *
 * Return: 0 on success
 *	error code from thermal_tmu_set_polling_delay_ms
 */
int thermal_tmu_debugfs_polling_delay_ms_get(void *data, u64 *val)
{
	struct google_sensor_data *sensor = data;
	int ret;
	u32 val_u32;

	ret = thermal_tmu_get_polling_delay_ms(sensor, &val_u32);
	if (ret)
		return ret;

	*val = val_u32;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(polling_delay_ms, thermal_tmu_debugfs_polling_delay_ms_get,
			thermal_tmu_debugfs_polling_delay_ms_set, "%lld\n");

/*
 * thermal_tmu_debugfs_init: This function initialize the root directory of TMU debugfs nodes.
 *
 * @dev: point to device
 *
 * Return: 0 on success
 *	-EINVAL: invalid device pointer
 *	-EOPNOTSUPP: debugfs not supported
 */
int thermal_tmu_debugfs_init(struct device *dev)
{
	if (!dev)
		return -EINVAL;

	tmu_debugfs_root = debugfs_lookup(TMU_DEBUGFS_ROOT, NULL);

	if (IS_ERR(tmu_debugfs_root)) {
		dev_warn(dev, "Debugfs not supported");
		return 0;
	}

	if (!tmu_debugfs_root) {
		tmu_debugfs_root = debugfs_create_dir(TMU_DEBUGFS_ROOT, NULL);
		if (IS_ERR(tmu_debugfs_root)) {
			/* fail to crate dedugfs root doesn't fail device probe */
			dev_warn(dev, "Fail to create TMU debugfs root directory: %s: ret=%ld",
				 TMU_DEBUGFS_ROOT, PTR_ERR(tmu_debugfs_root));
			tmu_debugfs_root = NULL;
		}
	}
	return 0;
}


/*
 * thermal_tmu_debugfs_junction_offset_set - setter for trip offsets
 */
static ssize_t thermal_tmu_debugfs_junction_offset_set(struct file *filp, const char __user *buf,
						       size_t count, loff_t *ppos)
{
	struct google_sensor_data *sensor = filp->private_data;
	char *write_buf, **argv;
	int argc, ret, new_offsets[THERMAL_TMU_NR_TRIPS] = {0};

	write_buf = kzalloc(count, GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	ret = simple_write_to_buffer(write_buf, count, ppos, buf, count);
	if (ret <= 0) {
		dev_err(sensor->dev, "TZ %s: Failed to write debugfs offset data: ret=%d",
			thermal_zone_device_type(sensor->tzd), ret);
		goto exit_free_buf;
	}

	argv = argv_split(GFP_KERNEL, write_buf, &argc);
	if (!argv) {
		dev_err(sensor->dev, "TZ %s: Failed to read input buffer",
			thermal_zone_device_type(sensor->tzd));
		ret = -ENODEV;
		goto exit_free_buf;
	}

	if (argc != THERMAL_TMU_NR_TRIPS) {
		dev_err(sensor->dev, "TZ %s: Incorrect number of offsets: %d",
			thermal_zone_device_type(sensor->tzd), argc);
		ret = -EINVAL;
		goto exit_free_argv;
	}

	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i) {
		int val;

		ret = kstrtos32(argv[i], 10, &val);
		if (ret) {
			dev_err(sensor->dev, "TZ %s: Failed to parse junction_offset: %s",
				thermal_zone_device_type(sensor->tzd), argv[i]);
			goto exit_free_argv;
		}
		new_offsets[i] = val;
	}

	ret = thermal_tmu_set_junction_offset(sensor, new_offsets, THERMAL_TMU_NR_TRIPS);
	if (ret) {
		dev_err(sensor->dev, "TZ %s: Failed to set junction_offset: ret=%d",
			thermal_zone_device_type(sensor->tzd), ret);
		goto exit_free_argv;
	}

	ret = count;
exit_free_argv:
	argv_free(argv);
exit_free_buf:
	kfree(write_buf);
	return ret;
}

/*
 * thermal_tmu_debugfs_junction_offset_get - getter for trip offsets
 */
static ssize_t thermal_tmu_debugfs_junction_offset_get(struct file *filp, char __user *buf,
						       size_t count, loff_t *ppos)
{
	struct google_sensor_data *sensor = filp->private_data;
	char read_buf[TMU_DEBUGFS_BUF_SIZE];
	int len = 0, ret, offsets[THERMAL_TMU_NR_TRIPS];

	ret = thermal_tmu_get_junction_offset(sensor, offsets);
	if (ret) {
		dev_err(sensor->dev, "TZ %s: Failed to get offset from TMU: ret=%d",
			thermal_zone_device_type(sensor->tzd), ret);
		return ret;
	}

	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		len += scnprintf(read_buf + len, TMU_DEBUGFS_BUF_SIZE - len, "%d ", offsets[i]);
	len += scnprintf(read_buf + len, TMU_DEBUGFS_BUF_SIZE - len, "\n");

	return simple_read_from_buffer(buf, count, ppos, read_buf, len);
}

static const struct file_operations junction_offset_fops = {
	.open = simple_open,
	.write = thermal_tmu_debugfs_junction_offset_set,
	.read = thermal_tmu_debugfs_junction_offset_get,
};

/*
 * __thermal_tmu_debugfs_temp_state_table_set - internal setter for temp_state_table
 *
 * @sensor pointer to the target google TMU sensor
 * @write_buf kernel space write buffer
 * @count the length of write_buf in byte
 */
VISIBLE_IF_KUNIT ssize_t __thermal_tmu_debugfs_temp_state_table_set(
		struct google_sensor_data *sensor, char *write_buf, size_t count)
{
	if (!sensor || !write_buf)
		return -EINVAL;

	char **argv;
	int argc, ret;
	int table_len = 0;
	struct tmu_temp_state_st *table = NULL;

	argv = argv_split(GFP_KERNEL, write_buf, &argc);
	if (!argv) {
		dev_err(sensor->dev, "TZ %s: Failed to read input buffer",
			thermal_zone_device_type(sensor->tzd));
		ret = -ENODEV;
		goto exit;
	}

	if (argc % 2) {
		dev_err(sensor->dev, "TZ %s: odd number of elements: %d",
			thermal_zone_device_type(sensor->tzd), argc);
		ret = -EINVAL;
		goto exit_free_argv;
	}

	table_len = argc / 2;
	table = kcalloc(table_len, sizeof(*table), GFP_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto exit_free_argv;
	}
	for (int i = 0; i < table_len; ++i) {
		u8 temp, state;

		ret = kstrtou8(argv[2 * i], 10, &temp);
		if (ret) {
			dev_err(sensor->dev, "TZ %s: Failed to parse temp_state_table: %s",
				thermal_zone_device_type(sensor->tzd), argv[2 * i]);
			goto exit_free_table;
		}
		ret = kstrtou8(argv[2 * i + 1], 10, &state);
		if (ret) {
			dev_err(sensor->dev, "TZ %s: Failed to parse temp_state_table: %s",
				thermal_zone_device_type(sensor->tzd), argv[2 * i + 1]);
			goto exit_free_table;
		}
		table[i].temp = temp;
		table[i].state = state;
	}

	ret = thermal_tmu_set_temp_state_table(sensor, table, table_len);
	if (ret) {
		dev_err(sensor->dev, "TZ %s: Failed to set temp_state_table: ret=%d",
			thermal_zone_device_type(sensor->tzd), ret);
		goto exit_free_table;
	}

	ret = count;
exit_free_table:
	kfree(table);
exit_free_argv:
	argv_free(argv);
exit:
	return ret;
}

/*
 * thermal_tmu_debugfs_temp_state_table_set - setter for temp_state_table
 *	copy from user and use __thermal_tmu_debugfs_temp_state_table_set
 */
static ssize_t thermal_tmu_debugfs_temp_state_table_set(struct file *filp,
							const char __user *buf,
							size_t count, loff_t *ppos)
{
	struct google_sensor_data *sensor = filp->private_data;
	char *write_buf;
	int ret;

	write_buf = kzalloc(count, GFP_KERNEL);
	if (!write_buf)
		return -ENOMEM;

	/* copy from user */
	ret = simple_write_to_buffer(write_buf, count, ppos, buf, count);
	if (ret <= 0) {
		dev_err(sensor->dev, "TZ %s: Failed to write debugfs offset data: ret=%d",
			thermal_zone_device_type(sensor->tzd), ret);
		goto exit_free_buf;
	}

	ret = __thermal_tmu_debugfs_temp_state_table_set(sensor, write_buf, count);
exit_free_buf:
	kfree(write_buf);
	return ret;
}

/*
 * __thermal_tmu_debugfs_temp_state_table_get - internal getter for temp_state_table
 *
 * @sensor pointer to the target google TMU sensor
 * @read_buf kernel space read buffer
 * @read_buf_size the length of read_buf in byte
 */
VISIBLE_IF_KUNIT int __thermal_tmu_debugfs_temp_state_table_get(
		struct google_sensor_data *sensor, char *read_buf, size_t read_buf_size)
{
	if (!sensor || !read_buf || !read_buf_size)
		return -EINVAL;

	int table_len = 0, ret, len = 0;
	struct tmu_temp_state_st table[THERMAL_TEMP_LUT_MAX_ENTRIES] = {0};

	ret = thermal_tmu_get_temp_state_table(sensor, &table[0], &table_len);
	if (ret || table_len == 0 || table_len > THERMAL_TEMP_LUT_MAX_ENTRIES) {
		dev_warn(sensor->dev, "TZ %s: Failed to get temp_state_table from TMU: ret=%d",
			thermal_zone_device_type(sensor->tzd), ret);
		len = scnprintf(read_buf,
			read_buf_size, "n/a\n");
	} else {
		for (int i = 0; i < table_len; ++i)
			len += scnprintf(read_buf + len, read_buf_size - len, "%d %d\n",
					table[i].temp, table[i].state);
	}

	return len;
}

/*
 * thermal_tmu_debugfs_temp_state_table_get - getter for temp_state_table
 *	use __thermal_tmu_debugfs_temp_state_table_get then copy to user
 */
static ssize_t thermal_tmu_debugfs_temp_state_table_get(struct file *filp,
							char __user *buf,
							size_t count, loff_t *ppos)
{
	struct google_sensor_data *sensor = filp->private_data;
	char read_buf[TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE];
	int len = __thermal_tmu_debugfs_temp_state_table_get(sensor, read_buf,
					TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE);

	return simple_read_from_buffer(buf, count, ppos, read_buf, len);
}

static const struct file_operations temp_state_table_fops = {
	.open = simple_open,
	.write = thermal_tmu_debugfs_temp_state_table_set,
	.read = thermal_tmu_debugfs_temp_state_table_get,
};

/*
 * thermal_tmu_debugfs_sensor_init
 *
 * This function adds debugfs nodes to debug_root/tmu directory.
 * New debugfs nodes are added under debug_root/tmu/<tzd->type>
 *
 * @sensor: pointer to tmu sensor data
 *
 * Return: 0 on success
 *	-EINVAL: invalidate arguments or NULL tmu_sensor_data
 *	error code from debugfs_create_dir for debug_root/tmu and <tzd->type>
 */
int thermal_tmu_debugfs_sensor_init(struct google_sensor_data *sensor)
{
	struct tmu_sensor_data *tmu_data = NULL;
	struct dentry *sens_root;

	if (!sensor)
		return -EINVAL;

	tmu_data = sensor->sens_priv_data;
	if (!tmu_data)
		return -EINVAL;

	if (!tmu_debugfs_root) {
		dev_warn(sensor->dev, "TMU debugfs root directory not found");
		/* fail to find dedugfs root doesn't fail device probe */
		return 0;
	}

	sens_root = debugfs_create_dir(sensor->tzd->type, tmu_debugfs_root);
	if (IS_ERR(sens_root)) {
		dev_warn(sensor->dev, "Fail to create TMU debugfs sensor directory: %s: ret=%ld",
			 thermal_zone_device_type(sensor->tzd), PTR_ERR(sens_root));
		return 0;
	}

	debugfs_create_file("pi_k_po", 0660, sens_root, sensor, &pi_k_po);
	debugfs_create_file("pi_k_pu", 0660, sens_root, sensor, &pi_k_pu);
	debugfs_create_file("pi_k_i", 0660, sens_root, sensor, &pi_k_i);
	debugfs_create_file("gradual_irq_gain", 0660, sens_root, sensor, &gradual_irq_gain);
	debugfs_create_file("gradual_timer_gain", 0660, sens_root, sensor, &gradual_timer_gain);
	debugfs_create_file("early_throttle_k_p", 0660, sens_root, sensor, &early_throttle_k_p);
	debugfs_create_file("gov_gradual_select", 0660, sens_root, sensor, &gov_gradual_select);
	debugfs_create_file("gov_pi_select", 0660, sens_root, sensor, &gov_pi_select);
	debugfs_create_file("gov_temp_lut_select", 0660, sens_root, sensor, &gov_temp_lut_select);
	debugfs_create_file("gov_hardlimit_via_pid_select", 0660, sens_root, sensor,
			    &gov_hardlimit_via_pid_select);
	debugfs_create_file("gov_early_throttle_select", 0660, sens_root, sensor,
			    &gov_early_throttle_select);
	debugfs_create_file("polling_delay_ms", 0660, sens_root, sensor, &polling_delay_ms);
	debugfs_create_file("junction_offset", 0660, sens_root, sensor, &junction_offset_fops);
	debugfs_create_file("temp_state_table", 0660, sens_root, sensor, &temp_state_table_fops);

	return 0;
}

/*
 * thermal_tmu_debugfs_cleanup: This function removes tmu debugfs root directory recursively
 */
void thermal_tmu_debugfs_cleanup(void)
{
	if (!tmu_debugfs_root)
		return;

	debugfs_remove_recursive(tmu_debugfs_root);
	tmu_debugfs_root = NULL;
}
