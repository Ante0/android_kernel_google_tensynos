// SPDX-License-Identifier: GPL-2.0
/**
 * @copyright Copyright (c) 2023 Samsung Electronics Co., Ltd
 *
 */

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

#include "include/uwb.h"
#include "include/uwb_sysnodes.h"

static ssize_t devid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int is_valid = 0;
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx, uwb_node.uwb_kobj);

	for (int i = 0; i < DEV_ID_LEN; i++) {
		if (uwb_ctx->fw_info.devid[i]) {
			is_valid = 1;
			break;
		}
	}

	if (!is_valid)
		return sysfs_emit(buf, "Cannot retrieve ChipID\n");

	return sysfs_emit(buf, "%*phN\n", DEV_ID_LEN, uwb_ctx->fw_info.devid);
}
static struct kobj_attribute devid_attr = __ATTR(devid, 0444, devid_show, NULL);

static ssize_t fwversion_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx, uwb_node.uwb_kobj);

	if (uwb_ctx && uwb_ctx->fw_info.version[0] != '\0')
		return sysfs_emit(buf, "%s\n", uwb_ctx->fw_info.version);
	else
		return sysfs_emit(buf, "Cannot retrieve FW version\n");
}
static struct kobj_attribute fwversion_attr = __ATTR(fwversion, 0444, fwversion_show, NULL);

static ssize_t num_spi_slow_txs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx, uwb_node.uwb_kobj);
	int tx_count = atomic_read(&uwb_ctx->num_spi_slow_txs);

	if (tx_count < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%d\n", tx_count);
}
static struct kobj_attribute num_spi_slow_txs_attr = __ATTR(num_spi_slow_txs, 0444,
		num_spi_slow_txs_show, NULL);

static ssize_t bl1_retries_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx, uwb_node.uwb_kobj);

	if (uwb_ctx->uwb_fw_ctx.bl1_retries < -1)
		return -EINVAL;

	return sysfs_emit(buf, "%d\n", uwb_ctx->uwb_fw_ctx.bl1_retries);
}
static struct kobj_attribute bl1_retries_attr = __ATTR(bl1_flash_retries, 0444,
		bl1_retries_show, NULL);

static u64 get_current_duration(enum u100_power_state current_state,
		enum u100_power_state target_state, u64 current_time_ms,
		struct u100_power_stats_data *power_stats_data)
{
	return current_state != target_state ? power_stats_data->duration :
		power_stats_data->duration + (current_time_ms - power_stats_data->last_entry);
}

static u64 show_power_stats_count_data(struct u100_ctx *uwb_ctx,
		enum u100_power_state state,
		char *buf)
{
	u64 count;

	if (!uwb_ctx->power_stats) {
		UWB_ERR("power_stats is not initialized.\n");
		return -EINVAL;
	}

	mutex_lock(&uwb_ctx->power_stats->power_stats_mutex);
	count = uwb_ctx->power_stats->power_stats_data[state].count;
	mutex_unlock(&uwb_ctx->power_stats->power_stats_mutex);

	return sysfs_emit(buf, "%llu\n", count);
}

static u64 show_power_stats_duration_data(struct u100_ctx *uwb_ctx,
		enum u100_power_state state,
		char *buf)
{
	u64 duration;
	enum u100_power_state current_state;

	if (!uwb_ctx->power_stats) {
		UWB_ERR("power_stats is not initialized.\n");
		return -EINVAL;
	}

	mutex_lock(&uwb_ctx->power_stats->power_stats_mutex);
	current_state = uwb_ctx->power_stats->current_power_state;
	duration = get_current_duration(current_state, state,
		ktime_to_ms(ktime_get_boottime()),
		&uwb_ctx->power_stats->power_stats_data[state]);
	mutex_unlock(&uwb_ctx->power_stats->power_stats_mutex);

	return sysfs_emit(buf, "%llu\n", duration);
}

static u64 show_power_stats_last_entry_data(struct u100_ctx *uwb_ctx,
		enum u100_power_state state,
		char *buf)
{
	u64 last_entry;

	if (!uwb_ctx->power_stats) {
		UWB_ERR("power_stats is not initialized.\n");
		return -EINVAL;
	}

	mutex_lock(&uwb_ctx->power_stats->power_stats_mutex);
	last_entry = uwb_ctx->power_stats->power_stats_data[state].last_entry;
	mutex_unlock(&uwb_ctx->power_stats->power_stats_mutex);

	return sysfs_emit(buf, "%llu\n", last_entry);
}

static u64 show_power_stats_last_exit_data(struct u100_ctx *uwb_ctx,
		enum u100_power_state state,
		char *buf)
{
	u64 last_exit;

	if (!uwb_ctx->power_stats) {
		UWB_ERR("power_stats is not initialized.\n");
		return -EINVAL;
	}

	mutex_lock(&uwb_ctx->power_stats->power_stats_mutex);
	last_exit = uwb_ctx->power_stats->power_stats_data[state].last_exit;
	mutex_unlock(&uwb_ctx->power_stats->power_stats_mutex);

	return sysfs_emit(buf, "%llu\n", last_exit);
}

static ssize_t idle_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx, uwb_node.power_stats_kobj);

	return show_power_stats_count_data(uwb_ctx, U100_SLEEP, buf);
}

static ssize_t idle_duration_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx,
					uwb_node.power_stats_kobj);

	return show_power_stats_duration_data(uwb_ctx, U100_SLEEP, buf);
}

static ssize_t idle_last_entry_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx,
						uwb_node.power_stats_kobj);

	return show_power_stats_last_entry_data(uwb_ctx, U100_SLEEP, buf);
}

static ssize_t idle_last_exit_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj,
				struct u100_ctx, uwb_node.power_stats_kobj);

	return show_power_stats_last_exit_data(uwb_ctx, U100_SLEEP, buf);
}

static ssize_t active_count_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx,
						uwb_node.power_stats_kobj);

	return show_power_stats_count_data(uwb_ctx, U100_ACTIVE, buf);
}

static ssize_t active_duration_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx,
						uwb_node.power_stats_kobj);

	return show_power_stats_duration_data(uwb_ctx, U100_ACTIVE, buf);
}

static ssize_t active_last_entry_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj, struct u100_ctx,
						uwb_node.power_stats_kobj);

	return show_power_stats_last_entry_data(uwb_ctx, U100_ACTIVE, buf);
}

static ssize_t active_last_exit_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	struct u100_ctx *uwb_ctx = container_of(kobj,
				struct u100_ctx, uwb_node.power_stats_kobj);

	return show_power_stats_last_exit_data(uwb_ctx, U100_ACTIVE, buf);
}

/* Define the sysfs attributes for each value */
static struct kobj_attribute idle_count_attr = __ATTR_RO(idle_count);
static struct kobj_attribute idle_duration_attr = __ATTR_RO(idle_duration);
static struct kobj_attribute idle_last_entry_attr = __ATTR_RO(idle_last_entry);
static struct kobj_attribute idle_last_exit_attr = __ATTR_RO(idle_last_exit);
static struct kobj_attribute active_count_attr = __ATTR_RO(active_count);
static struct kobj_attribute active_duration_attr = __ATTR_RO(active_duration);
static struct kobj_attribute active_last_entry_attr = __ATTR_RO(active_last_entry);
static struct kobj_attribute active_last_exit_attr = __ATTR_RO(active_last_exit);

static struct attribute *power_stats_attrs[] = {
	&idle_count_attr.attr,
	&idle_duration_attr.attr,
	&idle_last_entry_attr.attr,
	&idle_last_exit_attr.attr,
	&active_count_attr.attr,
	&active_duration_attr.attr,
	&active_last_entry_attr.attr,
	&active_last_exit_attr.attr,
	NULL,
};

static const struct attribute_group power_stats_attr_group = {
	.attrs = power_stats_attrs,
};

static const struct kobj_type ktype_uwb = {
	.sysfs_ops = &kobj_sysfs_ops,
};

static int create_sysfs_file(struct kobject *kobj, struct kobj_attribute *attr)
{
	int retval;

	retval = sysfs_create_file(kobj, &attr->attr);
	if (retval)
		UWB_ERR("Failed to create sysfs file %s: %d", attr->attr.name,
									retval);

	return retval;
}

static int create_power_stats_subdir(struct uwb_sysnode *uwb_node)
{
	int retval = kobject_init_and_add(&uwb_node->power_stats_kobj,
			&ktype_uwb, &uwb_node->uwb_kobj, "power_stats");

	if (retval) {
		UWB_ERR("Failed to create power stats directory");
		goto err;
	}

	retval = sysfs_create_group(&uwb_node->power_stats_kobj, &power_stats_attr_group);
	if (retval) {
		UWB_ERR("Create power stats sysnode group failed.\n");
		goto err;
	}

	return 0;

err:
	kobject_put(&uwb_node->power_stats_kobj); //clean up if any creation fails
	return retval;
}

/* Function to remove a sysfs file */
static void remove_sysfs_file(struct kobject *kobj, struct kobj_attribute *attr)
{
	sysfs_remove_file(kobj, &attr->attr);
}

static int create_sysfs_files(struct uwb_sysnode *uwb_node)
{
	int retval;

	retval = create_sysfs_file(&uwb_node->uwb_kobj, &devid_attr);
	if (retval)
		return retval;

	retval = create_sysfs_file(&uwb_node->uwb_kobj, &fwversion_attr);
	if (retval) {
		remove_sysfs_file(&uwb_node->uwb_kobj, &devid_attr);
		return retval;
	}

	retval = create_sysfs_file(&uwb_node->uwb_kobj, &bl1_retries_attr);
	if (retval) {
		remove_sysfs_file(&uwb_node->uwb_kobj, &fwversion_attr);
		remove_sysfs_file(&uwb_node->uwb_kobj, &devid_attr);
		return retval;
	}

	retval = create_sysfs_file(&uwb_node->uwb_kobj, &num_spi_slow_txs_attr);
	if (retval) {
		remove_sysfs_file(&uwb_node->uwb_kobj, &fwversion_attr);
		remove_sysfs_file(&uwb_node->uwb_kobj, &devid_attr);
		remove_sysfs_file(&uwb_node->uwb_kobj, &bl1_retries_attr);
		return retval;
	}

	return retval;
}

/* Function to clean up all sysfs files */
static void cleanup_sysfs_files(struct uwb_sysnode *uwb_node)
{
	remove_sysfs_file(&uwb_node->uwb_kobj, &fwversion_attr);
	remove_sysfs_file(&uwb_node->uwb_kobj, &devid_attr);
	remove_sysfs_file(&uwb_node->uwb_kobj, &bl1_retries_attr);
	remove_sysfs_file(&uwb_node->uwb_kobj, &num_spi_slow_txs_attr);

	sysfs_remove_group(&uwb_node->power_stats_kobj, &power_stats_attr_group);
}

static int create_sysfs_dir(struct uwb_sysnode *uwb_node)
{
	int ret = kobject_init_and_add(&uwb_node->uwb_kobj, &ktype_uwb,
							kernel_kobj, "uwb");

	if (ret) {
		kobject_put(&uwb_node->uwb_kobj);
		UWB_ERR("Failed to create directory");
	}

	return ret;
}

/* Function to remove the sysfs directory */
static void remove_sysfs_dir(struct uwb_sysnode *uwb_node)
{
	kobject_del(&uwb_node->uwb_kobj);
	kobject_put(&uwb_node->uwb_kobj);
	kobject_del(&uwb_node->power_stats_kobj);
	kobject_put(&uwb_node->power_stats_kobj);
}

int uwb_sysfs_init(struct u100_ctx *u100_ctx)
{
	int retval;
	struct uwb_sysnode *uwb_node = &u100_ctx->uwb_node;

	retval = create_sysfs_dir(uwb_node);
	if (retval) {
		UWB_ERR("Failed to create sysfs directory for uwb_node: %d\n", retval);
		return retval;
	}

	retval = create_sysfs_files(uwb_node);
	if (retval) {
		UWB_ERR("Failed to create sysfs files: %d\n", retval);
		remove_sysfs_dir(uwb_node); /* Clean up if sysfs files creation fails */
		return retval;
	}

	retval = create_power_stats_subdir(uwb_node);
	if (retval) {
		remove_sysfs_file(&uwb_node->uwb_kobj, &fwversion_attr);
		remove_sysfs_file(&uwb_node->uwb_kobj, &devid_attr);
		remove_sysfs_file(&uwb_node->uwb_kobj, &bl1_retries_attr);
		remove_sysfs_file(&uwb_node->uwb_kobj, &num_spi_slow_txs_attr);
		remove_sysfs_dir(uwb_node);
		return retval;
	}

	uwb_node->init_kobj_res = !retval;
	UWB_INFO("Sysfs files initialized successfully for uwb_node.\n");

	return 0;
}

void uwb_sysfs_exit(struct u100_ctx *u100_ctx)
{
	struct uwb_sysnode *uwb_node = &u100_ctx->uwb_node;

	if (uwb_node->init_kobj_res) {
		cleanup_sysfs_files(uwb_node);
		remove_sysfs_dir(uwb_node);
		uwb_node->init_kobj_res = false;
	}
}
