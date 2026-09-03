// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <kunit/visibility.h>

#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog_mba_nq_xport.h>

#include "google_gdmc_dhub.h"

struct uart_attribute {
	struct device_attribute dev_attr;
	u32 num;
};

struct uart_sysfs_attr_data {
	struct uart_attribute *uart_attrs;
	struct attribute_group *uart_group;
	int num_groups_created;
};

struct gdmc_dhub_driver {
	struct gdmc_dhub gdmc_dhub;
	struct uart_sysfs_attr_data attr_data;
};

static const struct uart_id_name uart_id_names_gen1[] = {
	{ GDMC_MBA_LGA_DHUB_UART_ID_CPM, "cpm" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_APC, "apc" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_AOC, "aoc" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_BMSM, "bmsm" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_GSA, "gsa" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_ISPFE, "ispfe" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_AURDSP, "aurdsp" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_TPU, "tpu" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_MODEM, "modem" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_GSC, "gsc" },
	{ GDMC_MBA_LGA_DHUB_UART_ID_GDMC, "gdmc" },
};

VISIBLE_IF_KUNIT const struct dhub_uart_list dhub_uart_list_gen1 = {
	.id_names_length = ARRAY_SIZE(uart_id_names_gen1),
	.id_names = uart_id_names_gen1,
};
EXPORT_SYMBOL_IF_KUNIT(dhub_uart_list_gen1);

static const struct uart_id_name uart_id_names_gen2[] = {
	{ GDMC_MBA_MBU_DHUB_UART_ID_AOSS_CPM_M55, "aoss_cpm_m55" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_APC, "apc" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_AOSS_A32, "aoss_a32" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_BMSM, "bmsm" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_GSA, "gsa" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_AOSS_CPM_M0P, "aoss_cpm_m0p" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_AURDSP, "aurdsp" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_TPU, "tpu" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_MODEM, "modem" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_GSC, "gsc" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_GDMC, "gdmc" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_AOSS_SENSOR_CORE, "aoss_sensor_core" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_NANO_TPU, "nano_tpu" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_CPUACC_SSU, "cpuacc_ssu" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_HSION_EBU_M0P, "hsion_ebu_m0p" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_PCIE_DPA0, "pcie_dpa0" },
	{ GDMC_MBA_MBU_DHUB_UART_ID_PCIE_DPA1, "pcie_dpa1" },
};

VISIBLE_IF_KUNIT const struct dhub_uart_list dhub_uart_list_gen2 = {
	.id_names_length = ARRAY_SIZE(uart_id_names_gen2),
	.id_names = uart_id_names_gen2,
};
EXPORT_SYMBOL_IF_KUNIT(dhub_uart_list_gen2);

static inline struct uart_attribute *to_uart_attr(struct device_attribute *attr)
{
	return container_of(attr, struct uart_attribute, dev_attr);
}

/*
 * Converts uart name into a uart id
 * Returns -EINVAL if the name is not valid.
 */
static int google_uart_name_to_id(const struct gdmc_dhub *gdmc_dhub, const char *name)
{
	const struct dhub_uart_list *dhub_uart_list = gdmc_dhub->dhub_uart_list;

	if (sysfs_streq(name, "none"))
		return GDMC_MBA_DHUB_UART_MUX_ID_NONE;

	if (sysfs_streq(name, "virt"))
		return GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED;

	for (int i = 0; i < dhub_uart_list->id_names_length; i++) {
		if (sysfs_streq(name, dhub_uart_list->id_names[i].name))
			return dhub_uart_list->id_names[i].id;
	}

	return -EINVAL;
}

/*
 * Converts uart id into a uart name
 * Returns -EINVAL if the id is not valid.
 */
static const char *google_uart_id_to_name(const struct gdmc_dhub *gdmc_dhub, u32 id)
{
	const struct dhub_uart_list *dhub_uart_list = gdmc_dhub->dhub_uart_list;

	if (id == GDMC_MBA_DHUB_UART_MUX_ID_NONE)
		return "none";

	if (id == GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED)
		return "virt";

	for (int i = 0; i < dhub_uart_list->id_names_length; i++) {
		if (id == dhub_uart_list->id_names[i].id)
			return dhub_uart_list->id_names[i].name;
	}

	return ERR_PTR(-EINVAL);
}

static inline u32 uart_num_to_bit(const struct gdmc_dhub *gdmc_dhub, u32 uart_num)
{
	const struct dhub_uart_list *dhub_uart_list = gdmc_dhub->dhub_uart_list;

	for (int i = 0; i < dhub_uart_list->id_names_length; i++) {
		if (uart_num == dhub_uart_list->id_names[i].id)
			return BIT(uart_num);
	}

	return 0;
}

/*
 * Get the UART mux.
 * @gdmc_dhub: The gdmc_dhub device.
 *
 * Returns a pointer to a constant string representing the name of the
 * active UART (e.g., "apc", "gsc") or "none". On failure,
 * returns an ERR_PTR() encoded error code
 */
VISIBLE_IF_KUNIT const char *mux_get(struct gdmc_dhub *gdmc_dhub)
{
	struct gdmc_dhub_iface *dhub_iface = gdmc_dhub->dhub_iface;
	u32 uart_id;
	int ret;

	ret = dhub_iface->dhub_mux_get(dhub_iface, &uart_id);
	if (ret < 0)
		return ERR_PTR(ret);

	return google_uart_id_to_name(gdmc_dhub, uart_id);
}
EXPORT_SYMBOL_IF_KUNIT(mux_get);

/*
 * Set the UART mux.
 * @gdmc_dhub: The gdmc_dhub device.
 * @name: The name of the UART to select, can be a sysfs string ('\0' or '\n' terminated).
 *
 * Returns 0 on success, or a negative error code on failure.
 */
VISIBLE_IF_KUNIT int mux_set(struct gdmc_dhub *gdmc_dhub, const char *name)
{
	struct gdmc_dhub_iface *dhub_iface = gdmc_dhub->dhub_iface;
	int uart_id = google_uart_name_to_id(gdmc_dhub, name);

	if (uart_id < 0)
		return -EINVAL;

	return dhub_iface->dhub_mux_set(dhub_iface, uart_id);
}
EXPORT_SYMBOL_IF_KUNIT(mux_set);

/*
 * Get the baud rate.
 * @gdmc_dhub: The gdmc_dhub device.
 * @uart_num: UART ID.
 * @baudrate: A pointer to a u32 variable where the current baud
 * rate will be stored.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
VISIBLE_IF_KUNIT int baudrate_get(struct gdmc_dhub *gdmc_dhub, u32 uart_num, u32 *baudrate)
{
	struct gdmc_dhub_iface *dhub_iface = gdmc_dhub->dhub_iface;

	return dhub_iface->dhub_baudrate_get(dhub_iface, uart_num, baudrate);
}
EXPORT_SYMBOL_IF_KUNIT(baudrate_get);

/*
 * Set the baud rate.
 * @gdmc_dhub: The gdmc_dhub device.
 * @uart_num: UART ID.
 * @baudrate: The new baud rate to configure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
VISIBLE_IF_KUNIT int baudrate_set(struct gdmc_dhub *gdmc_dhub, u32 uart_num, u32 baudrate)
{
	struct gdmc_dhub_iface *dhub_iface = gdmc_dhub->dhub_iface;

	return dhub_iface->dhub_baudrate_set(dhub_iface, uart_num, baudrate);
}
EXPORT_SYMBOL_IF_KUNIT(baudrate_set);

/*
 * Get the virtual state for a specific UART.
 * @gdmc_dhub: The gdmc_dhub device.
 * @uart_num: UART ID.
 * @enable: A pointer to a bool variable where the state will be
 * stored (true if enabled, false if disabled).
 *
 * Return: 0 on success, or a negative error code on failure.
 */
VISIBLE_IF_KUNIT int virt_en_get(struct gdmc_dhub *gdmc_dhub, u32 uart_num, bool *enable)
{
	struct gdmc_dhub_iface *dhub_iface = gdmc_dhub->dhub_iface;
	u32 mask;
	int ret;

	ret = dhub_iface->dhub_virt_en_get(dhub_iface, &mask);
	if (ret < 0)
		return ret;

	*enable = !!(mask & uart_num_to_bit(gdmc_dhub, uart_num));

	return ret;
}
EXPORT_SYMBOL_IF_KUNIT(virt_en_get);

/*
 * Set the virtual state for a specific UART.
 * @gdmc_dhub: The gdmc_dhub device.
 * @uart_num: UART ID.
 * @enable: The new state to set (true to enable, false to disable).
 *
 * Return: 0 on success, or a negative error code on failure.
 */
VISIBLE_IF_KUNIT int virt_en_set(struct gdmc_dhub *gdmc_dhub, u32 uart_num, bool enable)
{
	struct gdmc_dhub_iface *dhub_iface = gdmc_dhub->dhub_iface;
	int ret;
	u32 mask, bit;

	ret = dhub_iface->dhub_virt_en_get(dhub_iface, &mask);
	if (ret < 0)
		return ret;

	bit = uart_num_to_bit(gdmc_dhub, uart_num);
	if (enable)
		mask |= bit;
	else
		mask &= ~bit;

	/* Enable bit must be clear for valid command */
	mask &= ~GDMC_MBA_DHUB_VIRT_MASK_EN;

	return dhub_iface->dhub_virt_en_set(dhub_iface, mask);
}
EXPORT_SYMBOL_IF_KUNIT(virt_en_set);

static ssize_t baudrate_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct gdmc_dhub *gdmc_dhub = dev_get_drvdata(dev);
	struct uart_attribute *uart_attr = to_uart_attr(attr);
	u32 uart_num = uart_attr->num;
	u32 baudrate;

	int ret = baudrate_get(gdmc_dhub, uart_num, &baudrate);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%u\n", baudrate);
}

static ssize_t baudrate_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t count)
{
	struct gdmc_dhub *gdmc_dhub = dev_get_drvdata(dev);
	struct uart_attribute *uart_attr = to_uart_attr(attr);
	u32 uart_num = uart_attr->num;
	int ret;
	u32 baudrate;

	ret = kstrtou32(buf, 0, &baudrate);
	if (ret != 0)
		return ret;

	ret = baudrate_set(gdmc_dhub, uart_num, baudrate);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t virt_en_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gdmc_dhub *gdmc_dhub = dev_get_drvdata(dev);
	struct uart_attribute *uart_attr = to_uart_attr(attr);
	u32 uart_num = uart_attr->num;
	bool enable;

	int ret = virt_en_get(gdmc_dhub, uart_num, &enable);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%u\n", enable);
}

static ssize_t virt_en_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf,
			     size_t count)
{
	struct gdmc_dhub *gdmc_dhub = dev_get_drvdata(dev);
	struct uart_attribute *uart_attr = to_uart_attr(attr);
	u32 uart_num = uart_attr->num;
	int ret;
	bool enable;

	ret = kstrtobool(buf, &enable);
	if (ret != 0)
		return ret;

	ret = virt_en_set(gdmc_dhub, uart_num, enable);
	if (ret)
		return ret;

	return count;
}

static void cleanup_uart_attributes(struct device *dev)
{
	struct gdmc_dhub *gdmc_dhub = dev_get_drvdata(dev);
	struct gdmc_dhub_driver *gdmc_dhub_driver = container_of(gdmc_dhub,
					 struct gdmc_dhub_driver, gdmc_dhub);
	struct uart_sysfs_attr_data *attr_data = &gdmc_dhub_driver->attr_data;

	// Remove groups in reverse order of creation
	for (int i = attr_data->num_groups_created - 1; i >= 0; i--) {
		// Ensure the group pointer is valid before removing
		if (attr_data->uart_group && attr_data->uart_group[i].name)
			sysfs_remove_group(&dev->kobj, &attr_data->uart_group[i]);
	}
}

static int create_uart_list_attributes(struct device *dev)
{
	int uart_idx, uart_attr_idx, ret;
	struct gdmc_dhub *gdmc_dhub = dev_get_drvdata(dev);
	struct gdmc_dhub_driver *gdmc_dhub_driver = container_of(gdmc_dhub,
					 struct gdmc_dhub_driver, gdmc_dhub);
	const struct dhub_uart_list *dhub_uart_list = gdmc_dhub->dhub_uart_list;
	struct uart_sysfs_attr_data *attr_data = &gdmc_dhub_driver->attr_data;
	const int num_uart = dhub_uart_list->id_names_length;
	const int num_attrs_per_uart = 2; // baudrate, virt_en
	const int num_ptrs_per_group = num_attrs_per_uart + 1; // +1 for NULL

	attr_data->uart_attrs =
		 devm_kcalloc(dev, num_uart * num_attrs_per_uart,
			 sizeof(struct uart_attribute), GFP_KERNEL);
	if (!attr_data->uart_attrs) {
		ret = -ENOMEM;
		goto fail;
	}

	attr_data->uart_group =
		 devm_kcalloc(dev, num_uart, sizeof(struct attribute_group), GFP_KERNEL);
	if (!attr_data->uart_group) {
		ret = -ENOMEM;
		goto fail;
	}

	uart_attr_idx = 0;
	for (uart_idx = 0; uart_idx < num_uart; uart_idx++) {
		// Create attribute group for a uart from the uart list
		struct attribute_group *uart_group = &attr_data->uart_group[uart_idx];
		struct uart_attribute *uart_attr;

		uart_group->attrs = devm_kcalloc(dev, num_ptrs_per_group,
			sizeof(struct attribute *), GFP_KERNEL);
		if (!uart_group->attrs) {
			ret = -ENOMEM;
			goto fail;
		}

		// Create baudrate attribute
		uart_attr = &attr_data->uart_attrs[uart_attr_idx++];
		uart_attr->dev_attr =
			(struct device_attribute)__ATTR_RW(baudrate);
		uart_attr->num = dhub_uart_list->id_names[uart_idx].id;
		sysfs_attr_init(&uart_attr->dev_attr.attr);
		// Store the pointer to baudrate attribute
		uart_group->attrs[0] = &uart_attr->dev_attr.attr;

		// Create virt_en attribute
		uart_attr = &attr_data->uart_attrs[uart_attr_idx++];
		uart_attr->dev_attr =
			(struct device_attribute)__ATTR_RW(virt_en);
		uart_attr->num = dhub_uart_list->id_names[uart_idx].id;
		sysfs_attr_init(&uart_attr->dev_attr.attr);
		// Store the pointer to virt_en attribute
		uart_group->attrs[1] = &uart_attr->dev_attr.attr;

		// Store the uart group attribute name
		uart_group->name = dhub_uart_list->id_names[uart_idx].name;
		ret = sysfs_create_group(&dev->kobj, uart_group);
		if (ret) {
			dev_err(dev, "Failed to create sysfs group '%s': %d\n",
				 uart_group->name, ret);
			// Error occurred, stop creating more groups and go to cleanup
			goto fail;
		}

		attr_data->num_groups_created++;
	}

	return 0;
fail:
	dev_err(dev, "Failed to create all UART sysfs attributes (%d)\n", ret);
	cleanup_uart_attributes(dev);
	return ret;
}

static ssize_t uart_mux_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct gdmc_dhub *gdmc_dhub = dev_get_drvdata(dev);
	const char *name = mux_get(gdmc_dhub);

	if (IS_ERR(name))
		return PTR_ERR(name);

	return sysfs_emit(buf, "%s\n", name);
}

static ssize_t uart_mux_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t count)
{
	struct gdmc_dhub *gdmc_dhub = dev_get_drvdata(dev);
	int ret;

	ret = mux_set(gdmc_dhub, buf);
	if (ret)
		return ret;

	return count;
}

DEVICE_ATTR_RW(uart_mux);
static struct uart_attribute uart_attr_sbu_baudrate = {
	.dev_attr = __ATTR_RW(baudrate),
	.num = GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED,
};

static struct attribute *dhub_attrs[] = {
	&dev_attr_uart_mux.attr,
	&uart_attr_sbu_baudrate.dev_attr.attr,
	NULL,
};

static const struct attribute_group root_attr_group = {
	.attrs = dhub_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&root_attr_group,
	NULL,
};

static const struct of_device_id google_gdmc_dhub_of_match_table[] = {
	{ .compatible = "google,gdmc-dhub-gen1", .data = &dhub_uart_list_gen1},
	{ .compatible = "google,gdmc-dhub-gen2", .data = &dhub_uart_list_gen2},
	{},
};
MODULE_DEVICE_TABLE(of, google_gdmc_dhub_of_match_table);

VISIBLE_IF_KUNIT void gdmc_dhub_init(struct gdmc_dhub *gdmc_dhub,
				 struct gdmc_dhub_iface *dhub_iface,
				 const struct dhub_uart_list *dhub_uart_list)
{
	gdmc_dhub->dhub_uart_list = dhub_uart_list;
	gdmc_dhub->dhub_iface = dhub_iface;
}
EXPORT_SYMBOL_IF_KUNIT(gdmc_dhub_init);

static int google_gdmc_dhub_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gdmc_dhub_driver *gdmc_dhub_driver;
	struct gdmc_dhub *gdmc_dhub;
	struct gdmc_dhub_iface *dhub_iface;
	int ret;

	gdmc_dhub_driver = devm_kzalloc(dev, sizeof(*gdmc_dhub), GFP_KERNEL);
	if (!gdmc_dhub_driver)
		return -ENOMEM;
	platform_set_drvdata(pdev, &gdmc_dhub_driver->gdmc_dhub);

	dhub_iface = dhub_iface_get(dev);
	if (IS_ERR(dhub_iface))
		return PTR_ERR(dhub_iface);

	const struct of_device_id *match = of_match_device(google_gdmc_dhub_of_match_table, dev);

	gdmc_dhub_init(&gdmc_dhub_driver->gdmc_dhub, dhub_iface, match->data);

	ret = create_uart_list_attributes(dev);
	if (ret != 0)
		dhub_iface_put(dhub_iface);

	return ret;
}

static void google_gdmc_dhub_remove(struct platform_device *pdev)
{
	struct gdmc_dhub *gdmc_dhub = platform_get_drvdata(pdev);

	cleanup_uart_attributes(&pdev->dev);
	dhub_iface_put(gdmc_dhub->dhub_iface);
}

static struct platform_driver google_gdmc_dhub_driver = {
	.probe = google_gdmc_dhub_probe,
	.remove = google_gdmc_dhub_remove,
	.driver = {
		.name = "google-gdmc-dhub",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_gdmc_dhub_of_match_table),
		.dev_groups = attr_groups,
	},
};
module_platform_driver(google_gdmc_dhub_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GDMC DHUB");
MODULE_LICENSE("GPL");
