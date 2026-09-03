// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google Chip Info Driver
 *
 * Copyright (c) 2024-2025 Google LLC
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/sys_soc.h>

#include <soc/google/goog-chip-info.h>

#include "goog-chip-info.h"
#include "goog-chip-info-translator.h"

static struct goog_chip_info_data *chip_info;

static int __init goog_chip_info_fetch_val(struct goog_chip_info_feature *feature,
					   struct goog_chip_info_descriptor *desc,
					   const struct goog_chip_info_config *config)
{
	struct device *dev = feature->dev;
	u32 val[MAX_REG_INFO];
	const struct reg_info *reg;
	int nr_fields;
	int idx;

	for (nr_fields = 0; nr_fields < MAX_REG_INFO; nr_fields++) {
		reg = &config->reg_infos[nr_fields];

		if (reg->last_bit == reg->first_bit)
			break;

		val[nr_fields] = readl(feature->base + reg->offset);

		val[nr_fields] = (val[nr_fields] & ((1UL << reg->last_bit) - 1)) >> reg->first_bit;
		if (config->is_big_endian) {
			/*
			 * Big-endian fields that are not 32-bit aligned and require shifting are
			 * not supported. The hardware design does not have this case.
			 */
			WARN(reg->shift % 32,
			     "%s: big-endian fields with bit-shifts are not supported\n", __func__);
			val[nr_fields] = cpu_to_be32(val[nr_fields]);
		}
	}

	if (config->is_big_endian) {
		switch (nr_fields) {
		case 1: /* 32-bit field */
			desc->value = val[0];
			break;
		case MAX_REG_INFO: /* 64-bit field */
			desc->value = (u64)val[0] << 32 | val[1];
			break;
		default:
			dev_warn(dev, "Invalid number of fields for big-endian value: %d\n",
				 nr_fields);
		}
	} else {
		for (idx = 0; idx < nr_fields; idx++)
			desc->value |= (u64)val[idx] << config->reg_infos[idx].shift;
	}

	return 0;
}

static int __init goog_chip_info_read_entries(struct goog_chip_info_feature *feature,
					      const struct goog_chip_info_config *configs)
{
	struct device *dev = feature->dev;
	struct goog_chip_info_descriptor *descriptor;
	int idx, ret;

	for (idx = 0; configs[idx].name; idx++) {
		descriptor = &feature->descriptor[idx];

		ret = goog_chip_info_fetch_val(feature, descriptor, &configs[idx]);
		if (ret)
			return ret;
		dev_dbg(dev, "%s: Read descriptor[%d] = %#llx\n", __func__, idx, descriptor->value);
	}

	return idx;
}

static const struct of_device_id goog_chip_info_of_match_table[] = {
	{
		.compatible = "google,chip-info",
	},
	{},
};

static struct bus_type chip_info_subsys = {
	.name = "goog-chip-info",
	.dev_name = "goog-chip-info",
};

static goog_chip_info_translator_cb_t __init
goog_chip_info_match_translator_dt(struct device *dev, struct device_node *node)
{
	const struct translator *translator = NULL;
	const char *prop_name;
	int prop_name_size;
	int ret;

	prop_name = of_get_property(node, "translator", &prop_name_size);
	if (!prop_name)
		return NULL;

	/*
	 * This value is from device tree result and it will take empty string "" as length of 1.
	 * To have further comparison, we use strnlen to count actual string length.
	 */
	prop_name_size = strnlen(prop_name, prop_name_size);

	for (translator = translator_list; translator->callback; translator++) {
		if (strlen(translator->name) != prop_name_size)
			continue;

		ret = strncmp(prop_name, translator->name, prop_name_size);
		if (ret == 0)
			break;
	}

	if (!translator->callback)
		dev_warn(dev, "%s: No translator name: %s\n", dev_name(dev), prop_name);

	return translator->callback;
}

#define TRANS_BUF_SIZE 256
static ssize_t goog_chip_info_show_serial_codes(struct device *dev, char *buf, int pos)
{
	pos += scnprintf(buf + pos, PAGE_SIZE - pos, "[OTP BMSM Information]\n");
	pos += scnprintf(buf + pos, PAGE_SIZE - pos, "serial_num: 0x%08x%08x%08x%08x\n",
			 chip_info->serial_codes[0],
			 chip_info->serial_codes[1],
			 chip_info->serial_codes[2],
			 chip_info->serial_codes[3]);

	return pos;
}

static ssize_t goog_chip_info_sysfs_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct goog_chip_info_feature *feature;
	struct goog_chip_info_descriptor *descriptor;
	char translator_buf[TRANS_BUF_SIZE];
	int idx;
	int pos = 0;

	feature = container_of(attr, struct goog_chip_info_feature, dev_attr);
	descriptor = feature->descriptor;

	if (!feature->is_visible_in_prod &&
	    (chip_info->lcs_state < 0 || chip_info->lcs_state == LCS_MAJOR_PROD))
		return scnprintf(buf, PAGE_SIZE, "sysfs isn't visible to this device stage\n");

	pos += scnprintf(buf, PAGE_SIZE - pos, "[%s]\n", feature->name);

	for (idx = 0; idx < feature->nr_descriptor_entries; idx++) {
		int nr_wroted = 0;

		if (feature->translator_cb) {
			int left_buf_size = min(TRANS_BUF_SIZE, PAGE_SIZE - pos);

			nr_wroted = feature->translator_cb(chip_info, feature,
							   descriptor[idx].value, idx,
							   translator_buf, left_buf_size);
			translator_buf[TRANS_BUF_SIZE - 1] = '\0';
		}
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				     "%s: %llu", descriptor[idx].name, descriptor[idx].value);
		if (nr_wroted)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos,
					     " (%s)", translator_buf);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "\n");
	}

	if (feature->is_device_table)
		pos = goog_chip_info_show_serial_codes(dev, buf, pos);

	return pos;
}

static int __init goog_chip_info_init_feature(struct device *dev,
					      struct goog_chip_info_data *chip_info,
					      struct device_node *node,
					      struct goog_chip_info_descriptor *descriptors,
					      const struct goog_chip_info_config *configs,
					      enum goog_chip_info_feature_type type)
{
	struct goog_chip_info_feature *feature;
	struct device_attribute *dev_attr;
	struct attribute *attr;
	u32 reg_index;
	int ret;
	int *nr_features;

	if (of_property_read_bool(node, "private")) {
		feature = &chip_info->priv_features[chip_info->nr_priv_features];
		nr_features = &chip_info->nr_priv_features;

	} else {
		feature = &chip_info->features[chip_info->nr_features];
		nr_features = &chip_info->nr_features;
	}

	feature->dev = dev;
	feature->type = type;
	feature->descriptor = descriptors;

	ret = of_property_read_string(node, "feature-name", &feature->name);
	if (ret < 0) {
		dev_err(dev, "feature-name is not present\n");
		return ret;
	}

	feature->is_device_table = of_property_read_bool(node, "device-table");
	feature->is_serial_codes = of_property_read_bool(node, "serial-codes");
	feature->is_lcs_state = of_property_read_bool(node, "lcs-state");
	feature->is_rto = of_property_read_bool(node, "rto");
	feature->is_visible_in_prod = of_property_read_bool(node, "visible-in-prod");

	ret = of_property_read_u32(node, "google,reg-index", &reg_index);
	if (ret) {
		dev_err(dev, "google,reg-index is missing in node %s\n", node->full_name);
		return ret;
	}

	if (reg_index >= chip_info->nr_bases) {
		dev_err(dev, "Invalid reg-index %u in node %s\n", reg_index, node->full_name);
		return -ENXIO;
	}
	feature->base = chip_info->bases[reg_index];

	feature->nr_descriptor_entries = goog_chip_info_read_entries(feature, configs);
	if (feature->nr_descriptor_entries <= 0)
		return feature->nr_descriptor_entries;

	feature->translator_cb = goog_chip_info_match_translator_dt(dev, node);

	dev_attr = &feature->dev_attr;
	dev_attr->show = goog_chip_info_sysfs_show;
	attr = &dev_attr->attr;
	attr->name = node->name;
	attr->mode = 0400;

	(*nr_features)++;
	if (*nr_features == MAX_CHIP_INFO_FEATURES) {
		dev_err(dev, "# of features up to limit (max: %d)\n", MAX_CHIP_INFO_FEATURES);
		return -ENOSPC;
	}

	return 0;
}

static void __init goog_chip_info_get_system_val(struct goog_chip_info_data *chip_info)
{
	struct goog_chip_info_feature *feature;
	int idx, i;

	for (idx = 0; idx < chip_info->nr_priv_features; idx++) {
		feature = &chip_info->priv_features[idx];

		if (feature->is_lcs_state)
			chip_info->lcs_state = feature->descriptor[0].value;
		if (feature->is_rto)
			chip_info->rto = feature->descriptor[0].value;
		if (feature->is_serial_codes) {
			for (i = 0; i < MAX_SERIAL_CODE_WORDS; i++)
				chip_info->serial_codes[i] = feature->descriptor[i].value;
		}
	}
}

static int __init goog_chip_info_create_sysfs(struct device *dev,
					      struct goog_chip_info_data *chip_info)
{
	struct attribute **sysfs_attrs;
	struct attribute_group *sysfs_group;
	const struct attribute_group **sysfs_groups;
	int idx;

	sysfs_attrs = devm_kcalloc(dev, chip_info->nr_features + 1, sizeof(*sysfs_attrs),
				   GFP_KERNEL);
	sysfs_group = devm_kzalloc(dev, sizeof(*sysfs_group), GFP_KERNEL);
	sysfs_groups = devm_kcalloc(dev, 2, sizeof(*sysfs_groups), GFP_KERNEL);
	if (!sysfs_attrs || !sysfs_group || !sysfs_groups)
		return -ENOMEM;

	sysfs_groups[0] = sysfs_group;
	sysfs_group->attrs = sysfs_attrs;

	for (idx = 0; idx < chip_info->nr_features; idx++) {
		struct attribute *attr = &chip_info->features[idx].dev_attr.attr;

		sysfs_attrs[idx] = attr;
		sysfs_attr_init(attr);
	}

	subsys_system_register(&chip_info_subsys, sysfs_groups);

	return 0;
}

static int __init goog_chip_info_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct device_node *child;
	const struct goog_chip_info_config *configs;
	struct goog_chip_info_descriptor *descriptors;
	void __iomem *base;
	int nr_bases;
	int i, ret, nr_configs;

	chip_info = devm_kzalloc(dev, sizeof(*chip_info), GFP_KERNEL);
	if (!chip_info)
		return -ENOMEM;

	nr_bases = of_address_count(dev->of_node);
	if (nr_bases > MAX_CHIP_INFO_BASES) {
		dev_err(dev, "Number of bases exceeds limit: %d > %d\n",
			nr_bases, MAX_CHIP_INFO_BASES);
		return -EOVERFLOW;
	}

	if (!nr_bases) {
		dev_err(dev, "No available resource found\n");
		return -ENODEV;
	}

	for (i = 0; i < nr_bases; i++) {
		base = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(base))
			return PTR_ERR(base);

		chip_info->bases[i] = base;
	}
	chip_info->nr_bases = i;

	for_each_child_of_node(dev->of_node, child) {
		match = of_match_node(goog_chip_info_feature_map, child);
		if (!match)
			continue;

		configs = (const struct goog_chip_info_config *)match->data;
		if (!configs) {
			dev_warn(dev, "No match rule for %s\n", child->name);
			continue;
		}

		nr_configs = 0;
		while (configs[nr_configs].name)
			nr_configs++;

		descriptors = devm_kcalloc(dev, nr_configs, sizeof(*descriptors), GFP_KERNEL);
		if (!descriptors) {
			of_node_put(child);
			return -ENOMEM;
		}

		/*
		 * Since name pointer points to string in .rodata.str, keep it is safe to use.
		 */
		for (i = 0; i < nr_configs; i++)
			descriptors[i].name = configs[i].name;

		ret = goog_chip_info_init_feature(dev, chip_info, child, descriptors, configs,
						  match - goog_chip_info_feature_map);
		if (ret < 0) {
			dev_err(dev, "fail to init feature: %s\n", child->name);
			of_node_put(child);
			return ret;
		}
	}

	goog_chip_info_get_system_val(chip_info);
	goog_chip_info_create_sysfs(dev, chip_info);

	platform_set_drvdata(pdev, chip_info);

	return 0;
}

static struct platform_driver goog_chip_info_driver = {
	.driver = {
		.name = "goog-chip-info",
		.of_match_table = of_match_ptr(goog_chip_info_of_match_table),
		.suppress_bind_attrs = true,
	},
};
module_platform_driver_probe(goog_chip_info_driver, goog_chip_info_probe);

MODULE_DESCRIPTION("Google Chip Info driver");
MODULE_AUTHOR("Lucas Wei <lucaswei@google.com>");
MODULE_LICENSE("GPL");
