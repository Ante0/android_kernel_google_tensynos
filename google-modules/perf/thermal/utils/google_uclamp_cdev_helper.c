// SPDX-License-Identifier: GPL-2.0
/*
 * google_uclamp_cdev_helper.c Helper for uclamp cooling device.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/static_stub.h>
#include <kunit/visibility.h>
#include <linux/of.h>

#include "cdev_helper.h"
#include "cdev_cpufreq_helper.h"
#include "google_uclamp_cdev_helper.h"
#include "sched.h"
#include "thermal_core.h"

VISIBLE_IF_KUNIT
int guc_of_property_count_u32_elems(const struct device_node *np, const char *propname)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_of_property_count_u32_elems, np, propname);
	return of_property_count_u32_elems(np, propname);
}

VISIBLE_IF_KUNIT
int guc_of_property_read_u32_index(const struct device_node *np, const char *propname, u32 index,
				   u32 *out_value)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_of_property_read_u32_index, np, propname, index, out_value);
	return of_property_read_u32_index(np, propname, index, out_value);
}

VISIBLE_IF_KUNIT
struct device_node *guc_of_find_node_by_phandle(u32 phandle)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_of_find_node_by_phandle, phandle);
	return of_find_node_by_phandle(phandle);
}

VISIBLE_IF_KUNIT
int guc_of_cpu_node_to_id(struct device_node *node)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_of_cpu_node_to_id, node);
	return of_cpu_node_to_id(node);
}

VISIBLE_IF_KUNIT
void guc_of_node_put(struct device_node *node)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_of_node_put, node);
	of_node_put(node);
}

VISIBLE_IF_KUNIT
int guc_cdev_cpufreq_get_opp_count(unsigned int cpu)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_cdev_cpufreq_get_opp_count, cpu);
	return cdev_cpufreq_get_opp_count(cpu);
}

VISIBLE_IF_KUNIT
int guc_cdev_cpufreq_update_opp_table(unsigned int cpu, enum hw_dev_type cdev_id,
				      struct cdev_opp_table *cdev_table, unsigned int num_opp)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_cdev_cpufreq_update_opp_table, cpu, cdev_id, cdev_table,
				   num_opp);
	return cdev_cpufreq_update_opp_table(cpu, cdev_id, cdev_table, num_opp);
}

VISIBLE_IF_KUNIT
struct thermal_cooling_device *
guc_thermal_cooling_device_register(const char *type, void *devdata,
				    const struct thermal_cooling_device_ops *ops)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_thermal_cooling_device_register, type, devdata, ops);
	return thermal_cooling_device_register(type, devdata, ops);
}

VISIBLE_IF_KUNIT
void guc_thermal_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_thermal_cooling_device_unregister, cdev);
	thermal_cooling_device_unregister(cdev);
}

VISIBLE_IF_KUNIT
void guc_sched_thermal_freq_cap(unsigned int cpu, unsigned long freq)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_sched_thermal_freq_cap, cpu, freq);
	sched_thermal_freq_cap(cpu, freq);
}

VISIBLE_IF_KUNIT
int guc_device_create_file(struct device *device, const struct device_attribute *entry)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_device_create_file, device, entry);
	return device_create_file(device, entry);
}

VISIBLE_IF_KUNIT
void guc_device_remove_file(struct device *dev, const struct device_attribute *attr)
{
	KUNIT_STATIC_STUB_REDIRECT(guc_device_remove_file, dev, attr);
	device_remove_file(dev, attr);
}

static LIST_HEAD(therm_uclamp_cdev_list);

static int __thermal_uclamp_cpu_opp_table_setup(struct device *dev,
						struct thermal_uclamp_cdev *uclamp_cdev,
						enum hw_dev_type cdev_id)
{
	int num_opps, ret;

	num_opps = guc_cdev_cpufreq_get_opp_count(uclamp_cdev->cpu);
	if (num_opps <= 0) {
		dev_err(dev, "uclamp No OPP values available for cpu:%d, err: %d\n",
			uclamp_cdev->cpu, num_opps);
		return -EPROBE_DEFER;
	}

	uclamp_cdev->max_state = num_opps - 1;
	uclamp_cdev->opp_table =
		devm_kcalloc(dev, num_opps, sizeof(*uclamp_cdev->opp_table), GFP_KERNEL);
	if (!uclamp_cdev->opp_table)
		return -ENOMEM;

	ret = guc_cdev_cpufreq_update_opp_table(uclamp_cdev->cpu, cdev_id, uclamp_cdev->opp_table,
						num_opps);
	if (ret) {
		dev_err(dev, "uclamp Error updating OPP table for cpu:%d, ret:%d\n",
			uclamp_cdev->cpu, ret);
		return ret;
	}

	return 0;
}

static int thermal_uclamp_get_max_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct thermal_uclamp_cdev *uclamp_cdev = cdev->devdata;

	*state = uclamp_cdev->max_state;

	return 0;
}

static int thermal_uclamp_get_cur_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct thermal_uclamp_cdev *uclamp_cdev = cdev->devdata;

	*state = uclamp_cdev->cur_state;

	return 0;
}

static int thermal_uclamp_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct thermal_uclamp_cdev *uclamp_cdev = cdev->devdata;
	int idx = 0;

	if (state != uclamp_cdev->cur_state) {
		idx = uclamp_cdev->max_state - state;
		pr_debug("cdev:[%s] new state request:[%lu] frequeny:[%u]\n", cdev->type, state,
			 uclamp_cdev->opp_table[idx].freq);
		uclamp_cdev->cur_state = state;
		guc_sched_thermal_freq_cap(uclamp_cdev->cpu, uclamp_cdev->opp_table[idx].freq);
	}

	return 0;
}

static struct thermal_cooling_device_ops thermal_uclamp_cdev_ops = {
	.get_max_state = thermal_uclamp_get_max_state,
	.get_cur_state = thermal_uclamp_get_cur_state,
	.set_cur_state = thermal_uclamp_set_cur_state,
};

VISIBLE_IF_KUNIT
ssize_t state2power_table_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	struct thermal_uclamp_cdev *uclamp_cdev = cdev->devdata;
	int i, count = 0, idx;
	u32 power;

	if (!uclamp_cdev)
		return -ENODEV;

	for (i = 0; i <= uclamp_cdev->max_state; i++) {
		idx = uclamp_cdev->max_state - i;
		power = uclamp_cdev->opp_table[idx].power;
		count += sysfs_emit_at(buf, count, "%u ", DIV_ROUND_UP(power, 1000));
	}
	count += sysfs_emit_at(buf, count, "\n");

	return count;
}

static DEVICE_ATTR_RO(state2power_table);

static void thermal_uclamp_cleanup(void)
{
	struct thermal_uclamp_cdev *uclamp_cdev = NULL, *n;

	list_for_each_entry_safe(uclamp_cdev, n, &therm_uclamp_cdev_list, cdev_list) {
		list_del(&uclamp_cdev->cdev_list);
		if (uclamp_cdev->cdev) {
			guc_device_remove_file(&uclamp_cdev->cdev->device,
					       &dev_attr_state2power_table);
			guc_thermal_cooling_device_unregister(uclamp_cdev->cdev);
		}

		if (uclamp_cdev->opp_table)
			guc_sched_thermal_freq_cap(
				uclamp_cdev->cpu,
				uclamp_cdev->opp_table[uclamp_cdev->max_state].freq);
	}
}

int thermal_uclamp_probe_helper(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret = 0, i, num_clusters;

	if (!list_empty(&therm_uclamp_cdev_list)) {
		dev_err(dev, "Thermal uclamp cdev already initialized\n");
		return -EALREADY;
	}

	num_clusters = guc_of_property_count_u32_elems(np, "google,cluster-cdev-map");
	if (num_clusters <= 0 || num_clusters % 2) {
		dev_err(dev, "Invalid 'google,cluster-cdev-map' in DT\n");
		return num_clusters < 0 ? num_clusters : -EINVAL;
	}
	num_clusters /= 2;

	for (i = 0; i < num_clusters; i++) {
		struct thermal_uclamp_cdev *uclamp_cdev = NULL;
		char *name = NULL;
		enum hw_dev_type cdev_id;
		u32 phandle, cdev_id_val;
		struct device_node *cpu_node;
		unsigned int cpu;
		int found_cpu;

		ret = guc_of_property_read_u32_index(np, "google,cluster-cdev-map", i * 2,
						     &phandle);
		if (ret) {
			dev_err(dev, "failed to read phandle for cluster %d\n", i);
			goto cleanup_cdev;
		}

		ret = guc_of_property_read_u32_index(np, "google,cluster-cdev-map", i * 2 + 1,
						     &cdev_id_val);
		if (ret) {
			dev_err(dev, "failed to read cdev_id for cluster %d\n", i);
			goto cleanup_cdev;
		}

		if (cdev_id_val >= HW_CDEV_MAX) {
			dev_err(dev, "Invalid cdev_id %u for cluster %d\n", cdev_id_val, i);
			ret = -EINVAL;
			goto cleanup_cdev;
		}

		cpu_node = guc_of_find_node_by_phandle(phandle);
		if (!cpu_node) {
			dev_err(dev, "failed to find cpu node for phandle %u\n", phandle);
			ret = -EINVAL;
			goto cleanup_cdev;
		}

		found_cpu = guc_of_cpu_node_to_id(cpu_node);
		guc_of_node_put(cpu_node);

		if (found_cpu < 0) {
			dev_err(dev, "failed to find logical cpu for phandle %u, err: %d\n",
				phandle, found_cpu);
			ret = found_cpu;
			goto cleanup_cdev;
		}

		cpu = found_cpu;
		cdev_id = (enum hw_dev_type)cdev_id_val;

		uclamp_cdev = devm_kzalloc(dev, sizeof(*uclamp_cdev), GFP_KERNEL);
		if (!uclamp_cdev)
			return -ENOMEM;

		uclamp_cdev->cpu = cpu;

		ret = __thermal_uclamp_cpu_opp_table_setup(dev, uclamp_cdev, cdev_id);
		if (ret)
			goto cleanup_cdev;

		list_add_tail(&uclamp_cdev->cdev_list, &therm_uclamp_cdev_list);
		guc_sched_thermal_freq_cap(uclamp_cdev->cpu,
					   uclamp_cdev->opp_table[uclamp_cdev->max_state].freq);

		name = devm_kasprintf(dev, GFP_KERNEL, "thermal-uclamp-%d", uclamp_cdev->cpu);
		if (!name) {
			ret = -ENOMEM;
			goto cleanup_cdev;
		}

		uclamp_cdev->cdev = guc_thermal_cooling_device_register(name, uclamp_cdev,
									&thermal_uclamp_cdev_ops);
		if (IS_ERR(uclamp_cdev->cdev)) {
			ret = PTR_ERR(uclamp_cdev->cdev);
			dev_err(dev, "uclamp cdev:[%s] register error. err:%d\n", name, ret);
			uclamp_cdev->cdev = NULL;
			goto cleanup_cdev;
		}

		ret = guc_device_create_file(&uclamp_cdev->cdev->device,
					     &dev_attr_state2power_table);
		if (ret) {
			dev_err(dev, "cdev:[%s] state2power attr failed. err:%d\n", name, ret);
			goto cleanup_cdev;
		}

		dev_info(dev, "Registered cdev:%s\n", name);
	}

	platform_set_drvdata(pdev, &therm_uclamp_cdev_list);

	return 0;

cleanup_cdev:
	thermal_uclamp_cleanup();

	return ret;
}

void thermal_uclamp_remove_helper(struct platform_device *pdev)
{
	thermal_uclamp_cleanup();
}
