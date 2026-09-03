// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Device Tree Parser
 *
 * Copyright (c) 2018 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-dt: " fmt

#include "lwis_dt.h"

#include <linux/cleanup.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "lwis_bus_manager.h"
#include "lwis_clock.h"
#include "lwis_device_dpm.h"
#include "lwis_gpio.h"
#include "lwis_i2c.h"
#include "lwis_ioreg.h"
#include "lwis_platform.h"
#include "lwis_pwrseq.h"
#include "lwis_regulator.h"

#define IOREG_RANGE_FIELD_SIZE 3
#define OTP_SETTING_FIELD_SIZE 2

#define LWIS_DT_I3C_OTP_SETTINGS "i3c-otp-settings"
#define LWIS_DT_I3C_OTP_SETTLE_TIME "i3c-otp-settle-time"
#define LWIS_DT_I2C_OTP_SETTINGS "i2c-otp-settings"
#define LWIS_DT_I2C_OTP_SETTLE_TIME "i2c-otp-settle-time"

/* Uncomment this to help debug device tree parsing. */
// #define CONFIG_LWIS_DT_DEBUG

/**
 * parse_u32_array() - Parse a device tree property containing a u32 array.
 * @dev_node:  Pointer to the device tree node to read the property from.
 * @prop_name: The name of the device tree property containing the u32 array.
 * @array:     Output parameter. A pointer to a u32*. On successful return
 *             (return value > 0), this will point to a newly allocated array
 *             containing the property values. If @devres is not NULL, this
 *             memory is device-managed; otherwise, the caller is responsible
 *             for freeing this memory using kfree().
 * @devres:    If not NULL, use device-managed memory allocation
 *             (devm_kmalloc_array) associated with this device.
 *             If NULL, use standard kernel memory allocation (kmalloc_array).
 *
 * Reads the specified device tree property (@prop_name) from the given device
 * node (@dev_node). It expects the property to contain an array of 32-bit
 * unsigned integers.
 *
 * If the property exists and contains elements, this function allocates memory
 * (using either kmalloc_array() or devm_kmalloc_array() based on @devres,
 * with GFP_KERNEL) to hold the array values. It then reads the values from
 * the property into the allocated array and updates the @array pointer to
 * point to this new array.
 *
 * Return:
 * * The number of u32 elements read (> 0) on success.
 * * 0 if the property does not exist or is empty (in which case *array is
 *   not modified).
 * * -ENOMEM if memory allocation fails.
 * * Other negative error codes from of_property_read_u32_array() on read
 *   failure.
 */
static int parse_u32_array(struct device_node *dev_node, const char *prop_name, u32 **array,
			   struct device *devres)
{
	int count;
	int ret;

	count = of_property_count_u32_elems(dev_node, prop_name);
	if (count <= 0)
		return 0;

	if (devres)
		*array = devm_kmalloc_array(devres, count, sizeof(u32), GFP_KERNEL);
	else
		*array = kmalloc_array(count, sizeof(u32), GFP_KERNEL);
	if (*array == NULL)
		return -ENOMEM;

	ret = of_property_read_u32_array(dev_node, prop_name, *array, count);
	if (ret) {
		pr_err("Failed to read %s array: %d\n", prop_name, ret);
		if (!devres)
			kfree(*array);
		return ret;
	}

	return count;
}

/**
 * parse_u64_array() - Parse a device tree property containing a u64 array.
 * @dev_node:  Pointer to the device tree node to read the property from.
 * @prop_name: The name of the device tree property containing the u64 array.
 * @array:     Output parameter. A pointer to a u64*. On successful return
 *             (return value > 0), this will point to a newly allocated array
 *             containing the property values. The caller is responsible for
 *             freeing this memory using kfree().
 *
 * Reads the specified device tree property (@prop_name) from the given device
 * node (@dev_node). It expects the property to contain an array of 64-bit
 * unsigned integers.
 *
 * If the property exists and contains elements, this function allocates memory
 * using kmalloc_array() (with GFP_KERNEL) to hold the array values. It then
 * reads the values from the property into the allocated array and updates the
 * @array pointer to point to this new array.
 *
 * Return:
 * * The number of u64 elements read (> 0) on success.
 * * 0 if the property does not exist or is empty (in which case *array is
 *   not modified).
 * * -ENOMEM if memory allocation fails.
 * * Other negative error codes from of_property_read_u64_array() on read
 *   failure.
 */
static int parse_u64_array(struct device_node *dev_node, const char *prop_name, u64 **array)
{
	int count;
	int ret;

	count = of_property_count_u64_elems(dev_node, prop_name);
	if (count <= 0)
		return 0;

	*array = kmalloc_array(count, sizeof(u64), GFP_KERNEL);
	if (*array == NULL)
		return -ENOMEM;

	ret = of_property_read_u64_array(dev_node, prop_name, *array, count);
	if (ret) {
		pr_err("Failed to read %s array: %d\n", prop_name, ret);
		kfree(*array);
		return ret;
	}

	return count;
}

static int parse_irq_gpios(struct lwis_device *lwis_dev)
{
	int count;
	int name_count;
	int event_count;
	int type_count;
	int ret;
	struct device *dev;
	struct device_node *dev_node;
	struct gpio_descs *gpios;
	const char *name;
	char *irq_gpios_names = NULL;
	u64 *irq_gpios_events = NULL;
	u32 *irq_gpios_types = NULL;
	int i;

	/* Initialize the data structure */
	strscpy(lwis_dev->irq_gpios_info.name, "irq", LWIS_MAX_NAME_STRING_LEN);
	lwis_dev->irq_gpios_info.gpios = NULL;
	lwis_dev->irq_gpios_info.irq_list = NULL;
	lwis_dev->irq_gpios_info.is_shared = false;
	lwis_dev->irq_gpios_info.is_pulse = false;

	dev = lwis_dev->k_dev;
	count = gpiod_count(dev, "irq");
	/* No irq GPIO pins found, just return */
	if (count <= 0)
		return 0;

	dev_node = dev->of_node;
	name_count = of_property_count_strings(dev_node, "irq-gpios-names");
	event_count = of_property_count_elems_of_size(dev_node, "irq-gpios-events", sizeof(u64));
	type_count = of_property_count_elems_of_size(dev_node, "irq-gpios-types", sizeof(u32));

	if (count != event_count || count != name_count || count != type_count) {
		pr_err("Count of irq-gpios-* is not match\n");
		return -EINVAL;
	}

	gpios = lwis_gpio_list_get(dev, "irq");
	if (IS_ERR_OR_NULL(gpios)) {
		pr_err("Error parsing irq GPIO list (%ld)\n", PTR_ERR(gpios));
		return PTR_ERR(gpios);
	}
	lwis_dev->irq_gpios_info.gpios = gpios;

	lwis_dev->irq_gpios_info.irq_list = lwis_interrupt_list_alloc(lwis_dev, gpios->ndescs);
	if (IS_ERR_OR_NULL(lwis_dev->irq_gpios_info.irq_list)) {
		ret = -ENOMEM;
		lwis_dev->irq_gpios_info.irq_list = NULL;
		pr_err("Failed to allocate irq list\n");
		goto error_parse_irq_gpios;
	}

	irq_gpios_names = kmalloc(LWIS_MAX_NAME_STRING_LEN * name_count, GFP_KERNEL);
	if (IS_ERR_OR_NULL(irq_gpios_names)) {
		ret = -ENOMEM;
		goto error_parse_irq_gpios;
	}

	for (i = 0; i < name_count; ++i) {
		ret = of_property_read_string_index(dev_node, "irq-gpios-names", i, &name);
		if (ret < 0) {
			pr_err("Error get GPIO irq name list (%d)\n", ret);
			goto error_parse_irq_gpios;
		}
		strscpy(irq_gpios_names + i * LWIS_MAX_NAME_STRING_LEN, name,
			LWIS_MAX_NAME_STRING_LEN);
	}

	/* Parse gpio irq types (handles allocation internally) */
	type_count =
		parse_u32_array(dev_node, "irq-gpios-types", &irq_gpios_types, /*devres=*/NULL);
	if (type_count <= 0) {
		ret = -EINVAL;
		goto error_parse_irq_gpios;
	}

	/* Parse gpio irq events (handles allocation internally) */
	event_count = parse_u64_array(dev_node, "irq-gpios-events", &irq_gpios_events);
	if (event_count <= 0) {
		ret = -EINVAL;
		goto error_parse_irq_gpios;
	}

	for (i = 0; i < event_count; ++i) {
		ret = lwis_interrupt_set_gpios_event_info(lwis_dev->irq_gpios_info.irq_list, i,
							  irq_gpios_events[i]);
		if (ret) {
			pr_err("Error setting event info for gpios interrupt %d %d\n", i, ret);
			goto error_parse_irq_gpios;
		}
	}

	for (i = 0; i < gpios->ndescs; ++i) {
		char *name;
		int irq;

		irq = gpiod_to_irq(gpios->desc[i]);
		if (irq < 0) {
			pr_err("gpio to irq failed (%d)\n", irq);
			lwis_interrupt_list_free(lwis_dev->irq_gpios_info.irq_list);
			return irq;
		}
		name = irq_gpios_names + i * LWIS_MAX_NAME_STRING_LEN;
		lwis_interrupt_get_gpio_irq(lwis_dev->irq_gpios_info.irq_list, i, name, irq,
					    irq_gpios_types[i]);
	}

	kfree(irq_gpios_names);
	kfree(irq_gpios_events);
	kfree(irq_gpios_types);
	return 0;

error_parse_irq_gpios:
	if (lwis_dev->irq_gpios_info.gpios) {
		lwis_gpio_list_put(lwis_dev->irq_gpios_info.gpios, dev);
		lwis_dev->irq_gpios_info.gpios = NULL;
	}
	if (lwis_dev->irq_gpios_info.irq_list) {
		lwis_interrupt_list_free(lwis_dev->irq_gpios_info.irq_list);
		lwis_dev->irq_gpios_info.irq_list = NULL;
	}
	kfree(irq_gpios_names);
	kfree(irq_gpios_events);
	kfree(irq_gpios_types);
	return ret;
}

static int parse_clocks(struct lwis_device *lwis_dev)
{
	int i;
	int ret = 0;
	int count;
	int __maybe_unused bts_count;
	struct device *dev;
	struct device_node *dev_node;
	const char *name;
	u32 rate;
	int clock_family;

	dev = lwis_dev->k_dev;
	dev_node = dev->of_node;

	count = of_property_count_strings(dev_node, "clock-names");

	/* No clocks found, just return */
	if (count <= 0) {
		lwis_dev->clocks = NULL;
		return 0;
	}

	lwis_dev->clocks = lwis_clock_list_alloc(count);
	if (IS_ERR_OR_NULL(lwis_dev->clocks)) {
		pr_err("Cannot allocate clocks list\n");
		ret = PTR_ERR(lwis_dev->clocks);
		lwis_dev->clocks = NULL;
		return ret;
	}

	/* Parse and acquire clock pointers and frequencies, if applicable */
	for (i = 0; i < count; ++i) {
		of_property_read_string_index(dev_node, "clock-names", i, &name);
		/* It is allowed to omit clock rates for some of the clocks */
		ret = of_property_read_u32_index(dev_node, "clock-rates", i, &rate);
		/* Clock may fetched from devfreq as well */
		rate = (ret == 0) ? rate : 0;

		ret = lwis_clock_get(lwis_dev->clocks, name, dev, rate);
		if (ret < 0) {
			pr_err("Cannot find clock: %s\n", name);
			goto error_parse_clk;
		}
	}

	/* It is allowed to omit clock rates for some of the clocks */
	ret = of_property_read_u32(dev_node, "clock-family", &clock_family);
	lwis_dev->clock_family = (ret == 0) ? clock_family : CLOCK_FAMILY_INVALID;

	/* Parse the BTS block names */
	bts_count = of_property_count_strings(dev_node, "bts-block-names");
	if (bts_count > 0) {
		lwis_dev->bts_block_num = bts_count;
		for (i = 0; i < bts_count; ++i) {
			of_property_read_string_index(dev_node, "bts-block-names", i, &name);
			lwis_dev->bts_block_names[i] = (const char *)name;
		}
	} else {
		lwis_dev->bts_block_num = 1;
		lwis_dev->bts_block_names[0] = lwis_dev->name;
	}

	/* Initialize all the BTS indexes */
	for (i = 0; i < MAX_BTS_BLOCK_NUM; ++i)
		lwis_dev->bts_indexes[i] = BTS_UNSUPPORTED;

	if (IS_ENABLED(CONFIG_LWIS_DT_DEBUG)) {
		pr_info("%s: clock family %d", lwis_dev->name, lwis_dev->clock_family);
		lwis_clock_print(lwis_dev->clocks);
	}

	return 0;

error_parse_clk:
	/* Put back the clock instances for the ones that were alloc'ed */
	for (i = 0; i < count; ++i)
		lwis_clock_put_by_idx(lwis_dev->clocks, i, dev);
	lwis_clock_list_free(lwis_dev->clocks);
	lwis_dev->clocks = NULL;
	return ret;
}

static int parse_interrupt_event_info(struct lwis_interrupt_list *list, int index,
				      struct device_node *event_info)
{
	int ret;
	int int_reg_bits_num = 0;
	u32 *int_reg_bits = NULL;
	int irq_events_num;
	u64 *irq_events = NULL;
	int critical_events_num;
	u64 *critical_events = NULL;

	/* Parse irq_reg_bits (handles allocation internally) */
	int_reg_bits_num =
		parse_u32_array(event_info, "int-reg-bits", &int_reg_bits, /*devres=*/NULL);
	if (int_reg_bits_num <= 0) {
		ret = -EINVAL;
		goto event_info_exit;
	}

	/* Parse irq events (handles allocation internally) */
	irq_events_num = parse_u64_array(event_info, "irq-events", &irq_events);
	if (irq_events_num <= 0) {
		ret = -EINVAL;
		goto event_info_exit;
	}

	if (irq_events_num != int_reg_bits_num) {
		pr_err("Mismatch or invalid count for irq-events (%d) vs int-reg-bits (%d) for IRQ %s\n",
		       irq_events_num, int_reg_bits_num, list->irq[index].name);
		ret = -EINVAL;
		goto event_info_exit;
	}

	/* Parse critical events (handles allocation internally) */
	critical_events_num = parse_u64_array(event_info, "critical-irq-events", &critical_events);
	if (critical_events_num < 0) {
		ret = critical_events_num;
		goto event_info_exit;
	}

	ret = lwis_interrupt_set_event_info(list, index, (int64_t *)irq_events, irq_events_num,
					    int_reg_bits, int_reg_bits_num,
					    (int64_t *)critical_events, critical_events_num);
	if (ret)
		pr_err("Error setting event info for IRQ %s (%d)\n", list->irq[index].name, ret);

event_info_exit:
	kfree(critical_events);
	kfree(irq_events);
	kfree(int_reg_bits);
	return ret;
}

static int find_irq_index_by_name(struct lwis_interrupt_list *list, const char *irq_name)
{
	int i;

	for (i = 0; i < list->count; ++i) {
		if (strncmp(irq_name, list->irq[i].name, IRQ_FULL_NAME_LENGTH - 1) == 0)
			return i;
	}
	return -ENOENT;
}

static int parse_interrupt_leaf_nodes(struct lwis_interrupt_list *list, int index,
				      struct device_node *leaf_info)
{
	int irq_leaves_num;
	int int_reg_bits_num;
	u32 *int_reg_bits = NULL;
	struct of_phandle_iterator it = {};
	int i = 0, ret = 0;
	int32_t *current_leaf_indexes = NULL;

	/* Parse irq_reg_bits (handles allocation internally) */
	int_reg_bits_num =
		parse_u32_array(leaf_info, "int-reg-bits", &int_reg_bits, /*devres=*/NULL);
	if (int_reg_bits_num < 0)
		return int_reg_bits_num;
	if (int_reg_bits_num == 0) {
		pr_err("No 'int-reg-bits' property found or it is empty\n");
		return -EINVAL;
	}

	irq_leaves_num = of_property_count_elems_of_size(leaf_info, "irq-leaf-nodes", 4);
	if (irq_leaves_num < 0) {
		pr_err("Error getting irq-leaf-nodes: %d\n", irq_leaves_num);
		ret = irq_leaves_num;
		goto leaf_error;
	}
	if (irq_leaves_num != int_reg_bits_num || irq_leaves_num == 0) {
		pr_err("Mismatch or invalid count for irq-leaf-nodes (%d) vs int-reg-bits (%d)\n",
		       irq_leaves_num, int_reg_bits_num);
		ret = -EINVAL;
		goto leaf_error;
	}

	i = 0;
	of_for_each_phandle(&it, ret, leaf_info, "irq-leaf-nodes", 0, 0) {
		struct device_node *irq_group_node = it.node;
		int leaf_interrupts_count;
		const char *leaf_interrupt_name;
		int j = 0;

		leaf_interrupts_count =
			of_property_count_strings(irq_group_node, "leaf-interrupt-names");
		if (leaf_interrupts_count == -ENODATA) {
			/* Does not have a value means no leaf interrupt is configured for this */
			/* leaf node */
			continue;
		} else if (leaf_interrupts_count < 0) {
			pr_err("Error counting leaf-interrupt-names: %d\n", leaf_interrupts_count);
			ret = leaf_interrupts_count;
			goto leaf_error;
		}

		current_leaf_indexes = kmalloc(sizeof(int32_t) * leaf_interrupts_count, GFP_KERNEL);
		if (IS_ERR_OR_NULL(current_leaf_indexes)) {
			ret = -ENOMEM;
			goto leaf_error;
		}

		for (j = 0; j < leaf_interrupts_count; ++j) {
			ret = of_property_read_string_index(irq_group_node, "leaf-interrupt-names",
							    j, &leaf_interrupt_name);
			if (ret < 0) {
				pr_err("Error reading leaf-interrupt-names index %d: %d\n", j, ret);
				goto leaf_error;
			}
			current_leaf_indexes[j] = find_irq_index_by_name(list, leaf_interrupt_name);
			if (current_leaf_indexes[j] < 0) {
				ret = current_leaf_indexes[j];
				pr_err("Cannot find leaf irq %s\n", leaf_interrupt_name);
				goto leaf_error;
			}
		}

		ret = lwis_interrupt_add_leaf(list, index, int_reg_bits[i], leaf_interrupts_count,
					      current_leaf_indexes);
		if (ret) {
			pr_err("Error setting leaf nodes for interrupt %d %d\n", index, ret);
			goto leaf_error;
		}
		i++;
	}

	kfree(int_reg_bits);
	return 0;
leaf_error:
	of_node_put(it.node);
	lwis_interrupt_free_leaves(&list->irq[index]);
	kfree(current_leaf_indexes);
	kfree(int_reg_bits);
	return ret;
}

static int parse_interrupts(struct lwis_device *lwis_dev)
{
	int i;
	int ret;
	int count, event_infos_count;
	const char *name;
	struct device_node *dev_node;
	struct platform_device *plat_dev;
	struct of_phandle_iterator it = {};

	if (!lwis_dev->plat_dev) {
		dev_info(lwis_dev->dev, "Non-platform device, skip parse interrupts\n");
		return 0;
	}

	plat_dev = lwis_dev->plat_dev;
	dev_node = lwis_dev->k_dev->of_node;

	/* Test device type DEVICE_TYPE_TEST used for test, platform independent. */
	if (lwis_dev->type == DEVICE_TYPE_TEST)
		count = TEST_DEVICE_IRQ_CNT;
	else
		count = platform_irq_count(plat_dev);

	/* No interrupts found, just return */
	if (count <= 0) {
		lwis_dev->irqs = NULL;
		return 0;
	}

	lwis_dev->irqs = lwis_interrupt_list_alloc(lwis_dev, count);
	if (IS_ERR_OR_NULL(lwis_dev->irqs)) {
		if (lwis_dev->type == DEVICE_TYPE_TEST)
			pr_err("Failed to allocate injection\n");
		else
			pr_err("Failed to allocate IRQ list\n");

		ret = PTR_ERR(lwis_dev->irqs);
		lwis_dev->irqs = NULL;
		return ret;
	}

	for (i = 0; i < count; ++i) {
		ret = of_property_read_string_index(dev_node, "interrupt-names", i, &name);
		if (!ret) {
			ret = lwis_interrupt_init(lwis_dev->irqs, i, name);
			if (ret) {
				pr_err("Cannot initialize irq %s\n", name);
				goto error_get_irq;
			}
		} else {
			pr_err("Cannot find irq with index %d in device tree\n", i);
			goto error_get_irq;
		}
	}

	event_infos_count = of_property_count_elems_of_size(dev_node, "interrupt-event-infos", 4);
	if (count != event_infos_count) {
		pr_err("DT numbers of irqs: %d != event infos: %d in DT\n", count,
		       event_infos_count);
		ret = -EINVAL;
		goto error_get_irq;
	}
	/* Get event infos */
	i = 0;
	of_for_each_phandle(&it, ret, dev_node, "interrupt-event-infos", 0, 0) {
		const char *irq_reg_space = NULL, *irq_type_str = NULL;
		bool irq_mask_reg_toggle;
		u64 irq_src_reg;
		u64 irq_reset_reg = 0;
		u64 irq_mask_reg;
		u64 irq_overflow_reg = 0;
		int irq_reg_bid = -1;
		int irq_reg_bid_count;
		/* To match default value of reg-addr/value-bitwidth. */
		u32 irq_reg_bitwidth = 32;
		int32_t irq_type = REGULAR_INTERRUPT;
		int j;
		struct device_node *event_info = it.node;

		ret = of_property_read_string(event_info, "irq-reg-space", &irq_reg_space);
		if (ret) {
			pr_err("Error getting irq-reg-space from dt: %d\n", ret);
			goto error_event_infos;
		}

		irq_reg_bid_count = of_property_count_strings(dev_node, "reg-names");

		if (irq_reg_bid_count <= 0) {
			pr_err("Error getting reg-names from dt: %d\n", irq_reg_bid_count);
			goto error_event_infos;
		}
		for (j = 0; j < irq_reg_bid_count; j++) {
			const char *bid_name;

			ret = of_property_read_string_index(dev_node, "reg-names", j, &bid_name);

			if (ret)
				break;

			if (!strcmp(bid_name, irq_reg_space)) {
				irq_reg_bid = j;
				break;
			}
		}
		if (irq_reg_bid < 0) {
			pr_err("Could not find a reg bid for %s\n", irq_reg_space);
			goto error_event_infos;
		}

		ret = of_property_read_u64(event_info, "irq-src-reg", &irq_src_reg);
		if (ret) {
			pr_err("Error getting irq-src-reg from dt: %d\n", ret);
			goto error_event_infos;
		}

		of_property_read_u64(event_info, "irq-reset-reg", &irq_reset_reg);

		ret = of_property_read_u64(event_info, "irq-mask-reg", &irq_mask_reg);
		if (ret) {
			pr_err("Error getting irq-mask-reg from dt: %d\n", ret);
			goto error_event_infos;
		}

		of_property_read_u64(event_info, "irq-overflow-reg", &irq_overflow_reg);

		irq_mask_reg_toggle = of_property_read_bool(event_info, "irq-mask-reg-toggle");

		of_property_read_u32(event_info, "irq-reg-bitwidth", &irq_reg_bitwidth);

		ret = of_property_read_string(event_info, "irq-type", &irq_type_str);
		if (ret && ret != -EINVAL) {
			pr_err("Error getting irq-type from dt: %d\n", ret);
			goto error_event_infos;
		} else if (ret && ret == -EINVAL) {
			/* The property does not exist, which means regular*/
			irq_type = REGULAR_INTERRUPT;
		} else {
			if (strcmp(irq_type_str, "regular") == 0) {
				irq_type = REGULAR_INTERRUPT;
			} else if (strcmp(irq_type_str, "aggregate") == 0) {
				irq_type = AGGREGATE_INTERRUPT;
			} else if (strcmp(irq_type_str, "leaf") == 0) {
				irq_type = LEAF_INTERRUPT;
			} else if (strcmp(irq_type_str, "injection") == 0) {
				irq_type = FAKEEVENT_INTERRUPT;
			} else {
				pr_err("Invalid irq-type from dt: %s\n", irq_type_str);
				goto error_event_infos;
			}
		}

		lwis_interrupt_set_basic_info(lwis_dev->irqs, i, irq_reg_space, irq_reg_bid,
					      irq_src_reg, irq_reset_reg, irq_mask_reg,
					      irq_overflow_reg, irq_mask_reg_toggle,
					      irq_reg_bitwidth, irq_type);

		/* Register IRQ handler only for aggregate and regular interrupts */
		if (irq_type == AGGREGATE_INTERRUPT || irq_type == REGULAR_INTERRUPT) {
			ret = lwis_interrupt_get(lwis_dev->irqs, i, plat_dev);
			if (ret) {
				pr_err("Cannot set irq %s\n", name);
				goto error_event_infos;
			}
		} else if (irq_type == FAKEEVENT_INTERRUPT) {
			/*
			 * Hardcode the fake injection irq number to
			 * TEST_DEVICE_FAKE_INJECTION_IRQ
			 */
			lwis_dev->irqs->irq[i].irq = TEST_DEVICE_FAKE_INJECTION_IRQ;
		}

		/* Parse event info */
		ret = parse_interrupt_event_info(lwis_dev->irqs, i, event_info);
		if (ret) {
			pr_err("Cannot set event info %s\n", name);
			goto error_event_infos;
		}

		/* Parse leaf nodes if it's an aggregate interrupt */
		if (irq_type == AGGREGATE_INTERRUPT) {
			ret = parse_interrupt_leaf_nodes(lwis_dev->irqs, i, event_info);
			if (ret) {
				pr_err("Error setting leaf nodes for interrupt %d %d\n", i, ret);
				goto error_event_infos;
			}
		}

		i++;
	}

	if (IS_ENABLED(CONFIG_LWIS_DT_DEBUG))
		lwis_interrupt_print(lwis_dev->irqs);

	return 0;
error_event_infos:
	/* TODO(yromanenko): lwis_interrupt_put */
	of_node_put(it.node);
error_get_irq:
	lwis_interrupt_list_free(lwis_dev->irqs);
	lwis_dev->irqs = NULL;
	return ret;
}

static int parse_phys(struct lwis_device *lwis_dev)
{
	struct device *dev;
	struct device_node *dev_node;
	int i;
	int ret;
	int count;
	const char *name;

	dev = lwis_dev->k_dev;
	dev_node = dev->of_node;

	count = of_count_phandle_with_args(dev_node, "phys", "#phy-cells");

	/* No PHY found, just return */
	if (count <= 0) {
		lwis_dev->phys = NULL;
		return 0;
	}

	lwis_dev->phys = lwis_phy_list_alloc(count);
	if (IS_ERR_OR_NULL(lwis_dev->phys)) {
		pr_err("Failed to allocate PHY list\n");
		ret = PTR_ERR(lwis_dev->phys);
		lwis_dev->phys = NULL;
		return ret;
	}

	for (i = 0; i < count; ++i) {
		of_property_read_string_index(dev_node, "phy-names", i, &name);
		ret = lwis_phy_get(lwis_dev->phys, name, dev);
		if (ret < 0) {
			pr_err("Error adding PHY[%d]\n", i);
			goto error_parse_phy;
		}
	}

	if (IS_ENABLED(CONFIG_LWIS_DT_DEBUG))
		lwis_phy_print(lwis_dev->phys);

	return 0;

error_parse_phy:
	for (i = 0; i < count; ++i)
		lwis_phy_put_by_idx(lwis_dev->phys, i, dev);
	lwis_phy_list_free(lwis_dev->phys);
	lwis_dev->phys = NULL;
	return ret;
}

static int parse_bitwidths(struct lwis_device *lwis_dev)
{
	struct device *dev;
	struct device_node *dev_node;
	u32 addr_bitwidth = 32;
	u32 value_bitwidths[2] = { 32, 32 };
	int count;
	int ret = 0;

	dev = lwis_dev->k_dev;
	dev_node = dev->of_node;

	of_property_read_u32(dev_node, "reg-addr-bitwidth", &addr_bitwidth);
	if (IS_ENABLED(CONFIG_LWIS_DT_DEBUG))
		pr_info("Addr bitwidth set to: %d\n", addr_bitwidth);

	count = of_property_count_elems_of_size(dev_node, "reg-value-bitwidth", sizeof(u32));
	if (count == 2) {
		ret = of_property_read_u32_array(dev_node, "reg-value-bitwidth", value_bitwidths,
						 2);
		if (ret)
			return ret;
	} else if (count == 1 || count == -EINVAL) {
		of_property_read_u32(dev_node, "reg-value-bitwidth", &value_bitwidths[0]);
		value_bitwidths[1] = value_bitwidths[0];
	} else {
		dev_err(dev, "Invalid reg-value-bitwidth element count: %d\n", count);
		return count > 0 ? -EINVAL : count;
	}

	if (IS_ENABLED(CONFIG_LWIS_DT_DEBUG))
		pr_info("Value bitwidth set to: read=%d, write=%d\n", value_bitwidths[0],
			value_bitwidths[1]);

	lwis_dev->native_addr_bitwidth = addr_bitwidth;
	lwis_dev->native_read_value_bitwidth = value_bitwidths[0];
	lwis_dev->native_write_value_bitwidth = value_bitwidths[1];

	return 0;
}

static int parse_power_seqs(struct lwis_device *lwis_dev, const char *seq_name,
			    struct lwis_device_power_sequence_list **list,
			    struct device_node *dev_node_seq)
{
	char str_seq_name[LWIS_MAX_NAME_STRING_LEN];
	char str_seq_type[LWIS_MAX_NAME_STRING_LEN];
	char str_seq_delay[LWIS_MAX_NAME_STRING_LEN];
	struct device *dev;
	struct device_node *dev_node;
	int power_seq_count;
	int power_seq_type_count;
	int power_seq_delay_count;
	int i;
	int ret;
	const char *name;
	const char *type;
	int delay_us;

	scnprintf(str_seq_name, LWIS_MAX_NAME_STRING_LEN, "%s-seqs", seq_name);
	scnprintf(str_seq_type, LWIS_MAX_NAME_STRING_LEN, "%s-seq-types", seq_name);
	scnprintf(str_seq_delay, LWIS_MAX_NAME_STRING_LEN, "%s-seq-delays-us", seq_name);

	dev = lwis_dev->k_dev;
	dev_node = dev->of_node;
	*list = NULL;
	if (dev_node_seq)
		dev_node = dev_node_seq;

	power_seq_count = of_property_count_strings(dev_node, str_seq_name);
	power_seq_type_count = of_property_count_strings(dev_node, str_seq_type);
	power_seq_delay_count =
		of_property_count_elems_of_size(dev_node, str_seq_delay, sizeof(u32));

	/* No power-seqs found, just return */
	if (power_seq_count <= 0)
		return 0;

	if (power_seq_count != power_seq_type_count || power_seq_count != power_seq_delay_count) {
		pr_err("Count of power sequence %s is not match\n", str_seq_name);
		return -EINVAL;
	}

	*list = lwis_dev_power_seq_list_alloc(power_seq_count);
	if (IS_ERR_OR_NULL(*list)) {
		pr_err("Failed to allocate power sequence list\n");
		ret = PTR_ERR(*list);
		*list = NULL;
		return ret;
	}

	for (i = 0; i < power_seq_count; ++i) {
		ret = of_property_read_string_index(dev_node, str_seq_name, i, &name);
		if (ret < 0) {
			pr_err("Error adding power sequence[%d]\n", i);
			goto error_parse_power_seqs;
		}
		strscpy((*list)->seq_info[i].name, name, LWIS_MAX_NAME_STRING_LEN);

		ret = of_property_read_string_index(dev_node, str_seq_type, i, &type);
		if (ret < 0) {
			pr_err("Error adding power sequence type[%d]\n", i);
			goto error_parse_power_seqs;
		}
		strscpy((*list)->seq_info[i].type, type, LWIS_MAX_NAME_STRING_LEN);
		if (strcmp(type, "gpio") == 0) {
			ret = lwis_gpios_list_add_info_by_name(dev, &lwis_dev->gpios_list, name);
			if (ret)
				goto error_parse_power_seqs;
		} else if (strcmp(type, "regulator") == 0) {
			ret = lwis_regulator_list_add_info(dev, &lwis_dev->regulator_list, name);
			if (ret)
				goto error_parse_power_seqs;
		} else if (strcmp(type, "regulator_mode") == 0) {
			ret = lwis_regulator_list_add_info_by_set_mode(
				dev, &lwis_dev->regulator_list, name);
			if (ret)
				goto error_parse_power_seqs;
		} else if (strcmp(type, "pwrseq") == 0) {
			ret = lwis_pwrseq_list_add_info(dev, &lwis_dev->pwrseq_list, name);
			if (ret) {
				dev_err_probe(dev, ret, "Error adding power sequence %s\n", name);
				goto error_parse_power_seqs;
			}
		}

		ret = of_property_read_u32_index(dev_node, str_seq_delay, i, &delay_us);
		if (ret < 0) {
			pr_err("Error adding power sequence delay[%d]\n", i);
			goto error_parse_power_seqs;
		}
		(*list)->seq_info[i].delay_us = delay_us;
	}

	if (IS_ENABLED(CONFIG_LWIS_DT_DEBUG))
		lwis_dev_power_seq_list_print(*list);

	return 0;

error_parse_power_seqs:
	lwis_pwrseq_list_free(&lwis_dev->pwrseq_list);
	lwis_regulator_put_all(&lwis_dev->regulator_list);
	lwis_regulator_list_free(&lwis_dev->regulator_list);
	lwis_gpios_list_free(&lwis_dev->gpios_list);
	lwis_dev_power_seq_list_free(*list);
	*list = NULL;
	return ret;
}

static void lwis_of_node_put(void *node)
{
	of_node_put(node);
}

static int parse_unified_power_seqs(struct lwis_device *lwis_dev)
{
	struct device *dev;
	struct device_node *dev_node;
	struct device_node *dev_node_seq __free(device_node) = NULL;
	int count;
	int ret = 0;

	dev = lwis_dev->k_dev;
	dev_node = dev->of_node;

	count = of_property_count_elems_of_size(dev_node, "power-seq", sizeof(u32));

	/* No power-seq found, or entry does not exist, just return */
	if (count <= 0) {
		lwis_dev->power_seq_handler = NULL;
		return 0;
	}

	dev_node_seq = of_parse_phandle(dev_node, "power-seq", 0);
	if (!dev_node_seq) {
		pr_err("Can't get power-seq node\n");
		return -EINVAL;
	}

	ret = parse_power_seqs(lwis_dev, "power-up", &lwis_dev->power_up_sequence, dev_node_seq);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			pr_err("Error parsing power-up-seqs\n");
		return ret;
	}

	ret = parse_power_seqs(lwis_dev, "power-down", &lwis_dev->power_down_sequence,
			       dev_node_seq);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			pr_err("Error parsing power-down-seqs\n");
		return ret;
	}

	lwis_dev->power_seq_handler = no_free_ptr(dev_node_seq);
	return devm_add_action_or_reset(dev, lwis_of_node_put, lwis_dev->power_seq_handler);
}

static int parse_access_mode(struct lwis_device *lwis_dev)
{
	struct device_node *dev_node;

	dev_node = lwis_dev->k_dev->of_node;

	lwis_dev->is_read_only = of_property_read_bool(dev_node, "lwis,read-only");

	return 0;
}

/*
 * parse_i2c_otp_support:
 * This property supports DEVICE_TYPE_I2C for OTP settings in kernel.
 */
static bool parse_i2c_otp_support(struct lwis_i2c_device *i2c_dev)
{
	struct device_node *dev_node;

	dev_node = i2c_dev->base_dev.k_dev->of_node;

	i2c_dev->is_i2c_otp = of_property_read_bool(dev_node, "lwis,i2c-otp");

	return 0;
}

static int parse_i3c_mode(struct lwis_i2c_device *i2c_dev)
{
	struct device_node *dev_node;

	dev_node = i2c_dev->base_dev.k_dev->of_node;

	i2c_dev->i3c_enabled = of_property_read_bool(dev_node, "i3c-enabled");

	return 0;
}

static int parse_power_up_mode(struct lwis_device *lwis_dev)
{
	struct device_node *dev_node;

	dev_node = lwis_dev->k_dev->of_node;

	lwis_dev->power_up_to_suspend = of_property_read_bool(dev_node, "lwis,power-up-to-suspend");

	return 0;
}

static int parse_thread_priority(struct lwis_device *lwis_dev)
{
	struct device_node *dev_node;

	dev_node = lwis_dev->k_dev->of_node;
	lwis_dev->transaction_thread_priority = 0;

	of_property_read_u32(dev_node, "transaction-thread-priority",
			     &lwis_dev->transaction_thread_priority);

	return 0;
}

static int parse_i2c_device_priority(struct lwis_i2c_device *i2c_dev)
{
	struct device_node *dev_node;
	int ret = 0;

	dev_node = i2c_dev->base_dev.k_dev->of_node;
	/* Set i2c device_priority value to default */
	i2c_dev->device_priority = DEVICE_HIGH_PRIORITY;

	ret = of_property_read_u32(dev_node, "i2c-device-priority", &i2c_dev->device_priority);
	/* If no property in device tree, just return to use default */
	if (ret == -EINVAL)
		return 0;

	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "invalid i2c-device-priority value\n");
		return ret;
	}
	if ((i2c_dev->device_priority < DEVICE_HIGH_PRIORITY) ||
	    (i2c_dev->device_priority > DEVICE_LOW_PRIORITY)) {
		dev_err(i2c_dev->base_dev.dev, "invalid i2c-device-priority value %d\n",
			i2c_dev->device_priority);
		return -EINVAL;
	}

	return 0;
}

static int parse_i2c_lock_group_id(struct lwis_i2c_device *i2c_dev)
{
	struct device_node *dev_node;
	int ret;

	dev_node = i2c_dev->base_dev.k_dev->of_node;
	/* Set i2c_lock_group_id value to default */
	i2c_dev->i2c_lock_group_id = MAX_I2C_LOCK_NUM - 1;

	ret = of_property_read_u32(dev_node, "i2c-lock-group-id", &i2c_dev->i2c_lock_group_id);
	/* If no property in device tree, just return to use default */
	if (ret == -EINVAL)
		return 0;

	if (ret) {
		pr_err("i2c-lock-group-id value wrong\n");
		return ret;
	}
	if (i2c_dev->i2c_lock_group_id >= MAX_I2C_LOCK_NUM - 1) {
		pr_err("i2c-lock-group-id need smaller than MAX_I2C_LOCK_NUM - 1\n");
		return -EOVERFLOW;
	}

	return 0;
}

static int parse_otp_setting(struct lwis_i2c_device *i2c_dev, const char *prop_name,
			     struct lwis_otp_config *otp_config)
{
	struct device *dev = i2c_dev->base_dev.k_dev;
	struct device_node *dev_node = dev->of_node;
	int count;

	/* Parse otp setting (handles device managed allocation internally) */
	count = parse_u32_array(dev_node, prop_name, (u32 **)&otp_config->settings, /*devres=*/dev);
	if (count < 0)
		return count;
	if (count == 0) {
		dev_dbg(dev, "%s property not found or empty, skipping.\n", prop_name);
		otp_config->settings = NULL;
		otp_config->setting_count = 0;
		return 0;
	}
	if (count % OTP_SETTING_FIELD_SIZE != 0) {
		dev_err(dev, "Malformed %s property\n", prop_name);
		return -EINVAL;
	}

	otp_config->setting_count = count / OTP_SETTING_FIELD_SIZE;

	return 0;
}

static int parse_otp_settle_time_us(struct lwis_i2c_device *i2c_dev, const char *prop_name,
				    struct lwis_otp_config *otp_config)
{
	struct device_node *dev_node = i2c_dev->base_dev.k_dev->of_node;

	otp_config->settle_time_us = 0;
	of_property_read_u32(dev_node, prop_name, &otp_config->settle_time_us);

	return 0;
}

static int parse_i3c_ibi_config(struct lwis_i2c_device *i2c_dev)
{
	struct device_node *dev_node = i2c_dev->base_dev.k_dev->of_node;
	int ret_len, ret_slots;

	i2c_dev->ibi_config.ibi_max_payload_len = 0;
	/* The default value for the IBI num_slots in the common drivers is the magic number 8 */
	i2c_dev->ibi_config.ibi_num_slots = 8;
	ret_len = of_property_read_u32(dev_node, "ibi-max-payload-len",
				       &i2c_dev->ibi_config.ibi_max_payload_len);
	ret_slots =
		of_property_read_u32(dev_node, "ibi-num-slots", &i2c_dev->ibi_config.ibi_num_slots);

	i2c_dev->ibi_config.ibi_configured_in_dt = ret_len == 0 && ret_slots == 0;

	return 0;
}

static void parse_transaction_process_limit(struct lwis_device *lwis_dev)
{
	struct device_node *dev_node;

	lwis_dev->transaction_process_limit = 0;
	dev_node = lwis_dev->k_dev->of_node;

	of_property_read_u32(dev_node, "transaction-process-limit",
			     &lwis_dev->transaction_process_limit);
}

static void parse_ioreg_device_group(struct lwis_ioreg_device *ioreg_dev)
{
	struct device_node *dev_node;

	dev_node = ioreg_dev->base_dev.k_dev->of_node;
	ioreg_dev->device_group = LWIS_DEFAULT_DEVICE_GROUP;

	of_property_read_u32(dev_node, "device-group", &ioreg_dev->device_group);
}

static void parse_ioreg_device_priority(struct lwis_ioreg_device *ioreg_dev)
{
	struct device_node *dev_node;
	int ret;

	dev_node = ioreg_dev->base_dev.k_dev->of_node;

	ret = of_property_read_u32(dev_node, "ioreg-device-priority", &ioreg_dev->device_priority);

	if (ret) {
		ioreg_dev->device_priority = DEVICE_HIGH_PRIORITY;
		return;
	}

	/* Validate the device priority specified in the device tree */
	if ((ioreg_dev->device_priority < DEVICE_HIGH_PRIORITY) ||
	    (ioreg_dev->device_priority > DEVICE_LOW_PRIORITY)) {
		dev_err(ioreg_dev->base_dev.dev, "Invalid ioreg-device-priority value %d\n",
			ioreg_dev->device_priority);
		ioreg_dev->device_priority = DEVICE_HIGH_PRIORITY;
		return;
	}
}

static int parse_ioreg_valid_range(struct lwis_ioreg_device *ioreg_dev)
{
	struct device *dev = ioreg_dev->base_dev.k_dev;
	struct device_node *dev_node = dev->of_node;
	int count;

	/* Parse reg valid range (handles device managed allocation internally) */
	count = parse_u32_array(dev_node, "reg-valid-ranges",
				(u32 **)&ioreg_dev->reg_valid_range_list.ranges, /*devres=*/dev);
	if (count < 0)
		return count;
	if (count == 0) {
		dev_dbg(dev, "reg-valid-ranges property not found or empty, skipping.\n");
		return 0;
	}
	if (count % IOREG_RANGE_FIELD_SIZE != 0) {
		dev_err(dev, "Malformed reg-valid-ranges property\n");
		return -EINVAL;
	}

	ioreg_dev->reg_valid_range_list.count = count / IOREG_RANGE_FIELD_SIZE;

	if (IS_ENABLED(CONFIG_LWIS_DT_DEBUG))
		lwis_ioreg_device_valid_range_list_print(ioreg_dev);

	return 0;
}

static int parse_ioreg_reg_offsets(struct lwis_ioreg_device *ioreg_dev)
{
	int i;

	for (i = 0; i < ioreg_dev->reg_list.count; ++i) {
		/* Initialize to 0 before setting, since this is an optional value */
		ioreg_dev->reg_list.block[i].offset = 0;
		of_property_read_u64(ioreg_dev->base_dev.k_dev->of_node, "reg-offset",
				     &ioreg_dev->reg_list.block[i].offset);
	}
	return 0;
}

#if IS_ENABLED(CONFIG_GOOGLE_IIS)
static int parse_ioreg_access_controllers(struct lwis_ioreg_device *ioreg_dev)
{
	struct device *k_dev = ioreg_dev->base_dev.k_dev;
	struct device_node *ac_node;
	struct platform_device *ac_pdev;
	struct device_link *link;
	int ac_index = 0;
	int ret = -ENODEV;

	while ((ac_node = of_parse_phandle(k_dev->of_node, "access-controllers", ac_index))) {
		if (!of_device_is_compatible(ac_node, "google,iis")) {
			of_node_put(ac_node);
			ac_index++;
			continue;
		}

		ac_pdev = of_find_device_by_node(ac_node);
		if (!ac_pdev) {
			ret = dev_err_probe(k_dev, -EPROBE_DEFER, "Failed to find IIS device\n");
			goto put_node;
		}

		link = device_link_add(k_dev, &ac_pdev->dev,
				       DL_FLAG_PM_RUNTIME | DL_FLAG_AUTOREMOVE_CONSUMER);
		if (!link) {
			ret = dev_err_probe(k_dev, -EINVAL, "Failed to link to IIS\n");
			goto put_device;
		}
		ret = 0;

put_device:
		put_device(&ac_pdev->dev);
put_node:
		of_node_put(ac_node);
		break;
	}

	return ret;
}
#endif

int lwis_base_parse_dt(struct lwis_device *lwis_dev)
{
	struct device *dev;
	struct device_node *dev_node;
	const char *name_str;
	int ret = 0;

	dev = lwis_dev->k_dev;
	dev_node = dev->of_node;

	if (!dev_node) {
		pr_err("Cannot find device node\n");
		return -ENODEV;
	}

	ret = of_property_read_string(dev_node, "node-name", &name_str);
	if (ret) {
		pr_err("Error parsing node name\n");
		return -EINVAL;
	}
	strscpy(lwis_dev->name, name_str, LWIS_MAX_NAME_STRING_LEN);

	ret = lwis_platform_check_qos_box_probed(dev, name_str);
	if (ret) {
		pr_err("%s device cannot be probed before qos_box device probed\n", name_str);
		return -EINVAL;
	}

	pr_debug("Device tree entry [%s] - begin\n", lwis_dev->name);

	ret = parse_irq_gpios(lwis_dev);
	if (ret) {
		pr_err("Error parsing irq-gpios\n");
		return ret;
	}

	ret = parse_unified_power_seqs(lwis_dev);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			pr_err("Error parse_unified_power_seqs\n");
		return ret;
	}

	if (lwis_dev->power_up_sequence == NULL) {
		ret = parse_power_seqs(lwis_dev, "power-up", &lwis_dev->power_up_sequence, NULL);
		if (ret) {
			if (ret != -EPROBE_DEFER)
				pr_err("Error parsing power-up-seqs\n");
			return ret;
		}
	}

	if (lwis_dev->power_down_sequence == NULL) {
		ret = parse_power_seqs(lwis_dev, "power-down", &lwis_dev->power_down_sequence,
				       NULL);
		if (ret) {
			if (ret != -EPROBE_DEFER)
				pr_err("Error parsing power-down-seqs\n");
			return ret;
		}
	}

	ret = parse_power_seqs(lwis_dev, "suspend", &lwis_dev->suspend_sequence, NULL);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			pr_err("Error parsing suspend-seqs\n");
		return ret;
	}

	ret = parse_power_seqs(lwis_dev, "resume", &lwis_dev->resume_sequence, NULL);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			pr_err("Error parsing resume-seqs\n");
		return ret;
	}

	ret = parse_clocks(lwis_dev);
	if (ret) {
		pr_err("Error parsing clocks\n");
		return ret;
	}

	ret = parse_interrupts(lwis_dev);
	if (ret) {
		pr_err("Error parsing interrupts\n");
		return ret;
	}

	ret = parse_phys(lwis_dev);
	if (ret) {
		pr_err("Error parsing phy's\n");
		return ret;
	}

	parse_access_mode(lwis_dev);
	parse_power_up_mode(lwis_dev);
	parse_thread_priority(lwis_dev);
	ret = parse_bitwidths(lwis_dev);
	if (ret) {
		dev_err(lwis_dev->dev, "Error parsing bitwidths (%d)\n", ret);
		return ret;
	}
	parse_transaction_process_limit(lwis_dev);

	lwis_dev->bts_scenario_name = NULL;
	of_property_read_string(dev_node, "bts-scenario", &lwis_dev->bts_scenario_name);

	lwis_dev->mem_qos_scenario_name = NULL;
	of_property_read_string(dev_node, "mem-qos-scenario", &lwis_dev->mem_qos_scenario_name);

	dev_node->data = lwis_dev;

	pr_debug("Device tree entry [%s] - end\n", lwis_dev->name);

	return ret;
}

int lwis_i2c_device_parse_dt(struct lwis_i2c_device *i2c_dev)
{
	struct device_node *dev_node;
	struct device_node *dev_node_i2c;
	int ret;
	int size;
	int i;

	dev_node = i2c_dev->base_dev.k_dev->of_node;

	size = of_property_count_u32_elems(dev_node, "i2c-bus");

	for (i = 0; i < size; ++i) {
		dev_node_i2c = of_parse_phandle(dev_node, "i2c-bus", i);
		if (!dev_node_i2c) {
			dev_err(i2c_dev->base_dev.dev, "Cannot find i2c/i3c node\n");
			return -ENODEV;
		}
		if (of_device_is_available(dev_node_i2c)) {
			i2c_dev->adapter = of_find_i2c_adapter_by_node(dev_node_i2c);
			break;
		}
	}

	of_node_put(dev_node_i2c);
	if (!i2c_dev->adapter) {
		dev_err(i2c_dev->base_dev.dev, "Cannot find i2c adapter\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(dev_node, "i2c-addr", (u32 *)&i2c_dev->address);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Failed to read i2c-addr\n");
		return ret;
	}

	ret = parse_i2c_lock_group_id(i2c_dev);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Error parsing i2c lock group id\n");
		return ret;
	}

	ret = parse_i2c_device_priority(i2c_dev);
	if (ret)
		return ret;

	parse_i2c_otp_support(i2c_dev);

	if (i2c_dev->is_i2c_otp) {
		ret = parse_otp_setting(i2c_dev, LWIS_DT_I2C_OTP_SETTINGS,
					&i2c_dev->i2c_otp_config);
		if (ret) {
			dev_err(i2c_dev->base_dev.dev, "Error parsing i2c otp settings\n");
			return ret;
		}
		ret = parse_otp_settle_time_us(i2c_dev, LWIS_DT_I2C_OTP_SETTLE_TIME,
					       &i2c_dev->i2c_otp_config);
		if (ret) {
			dev_err(i2c_dev->base_dev.dev, "Error parsing i2c otp settle time\n");
			return ret;
		}
	}

	return 0;
}

static void parse_i3c_match_properties(struct lwis_i2c_device *i2c_dev)
{
	u32 val;

	i2c_dev->match_type = LWIS_I3C_MATCH_NONE;

	if (!of_property_read_u64(i2c_dev->base_dev.k_dev->of_node, "i3c-pid",
				  &i2c_dev->match.pid)) {
		i2c_dev->match_type = LWIS_I3C_MATCH_PID;
	} else if (!of_property_read_u32(i2c_dev->base_dev.k_dev->of_node, "i3c-dcr", &val)) {
		i2c_dev->match_type = LWIS_I3C_MATCH_DCR;
		i2c_dev->match.dcr = val;
	} else if (!of_property_read_u32(i2c_dev->base_dev.k_dev->of_node, "i3c-bcr", &val)) {
		i2c_dev->match_type = LWIS_I3C_MATCH_BCR;
		i2c_dev->match.bcr = val;
	} else {
		dev_info(i2c_dev->base_dev.dev, "No i3c-pid/dcr/bcr definition\n");
	}
}

int lwis_i3c_proxy_device_parse_dt(struct lwis_i2c_device *i2c_dev)
{
	struct device *dev = i2c_dev->base_dev.dev;
	int ret;

	ret = parse_otp_setting(i2c_dev, LWIS_DT_I2C_OTP_SETTINGS, &i2c_dev->i2c_otp_config);
	if (ret) {
		dev_err(dev, "Error parsing i2c otp settings: %d\n", ret);
		return ret;
	}

	ret = parse_otp_settle_time_us(i2c_dev, LWIS_DT_I2C_OTP_SETTLE_TIME,
				       &i2c_dev->i2c_otp_config);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Error parsing i2c otp settle time\n");
		return ret;
	}

	ret = parse_otp_setting(i2c_dev, LWIS_DT_I3C_OTP_SETTINGS, &i2c_dev->i3c_otp_config);
	if (ret) {
		dev_err(dev, "Error parsing i3c otp settings: %d\n", ret);
		return ret;
	}

	ret = parse_otp_settle_time_us(i2c_dev, LWIS_DT_I3C_OTP_SETTLE_TIME,
				       &i2c_dev->i3c_otp_config);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Error parsing i3c otp settle time\n");
		return ret;
	}

	parse_i3c_match_properties(i2c_dev);
	parse_i3c_ibi_config(i2c_dev);
	parse_i3c_mode(i2c_dev);

	return 0;
}

int lwis_spi_device_parse_dt(struct lwis_spi_device *spi_dev)
{
	return 0;
}

int lwis_ioreg_device_parse_dt(struct lwis_ioreg_device *ioreg_dev)
{
	struct device_node *dev_node;
	int i;
	int ret;
	int blocks;
	int reg_tuple_size;
	const char *name;

	dev_node = ioreg_dev->base_dev.k_dev->of_node;
	reg_tuple_size = of_n_addr_cells(dev_node) + of_n_size_cells(dev_node);

	blocks = of_property_count_elems_of_size(dev_node, "reg", reg_tuple_size * sizeof(u32));
	if (blocks <= 0) {
		dev_err(ioreg_dev->base_dev.dev, "No register space found\n");
		return -EINVAL;
	}

	ret = lwis_ioreg_list_alloc(ioreg_dev, blocks);
	if (ret) {
		dev_err(ioreg_dev->base_dev.dev, "Failed to allocate ioreg list\n");
		return ret;
	}

	for (i = 0; i < blocks; ++i) {
		of_property_read_string_index(dev_node, "reg-names", i, &name);
		ret = lwis_ioreg_get(ioreg_dev, i, name);
		if (ret) {
			dev_err(ioreg_dev->base_dev.dev, "Cannot set ioreg info for %s\n", name);
			goto error_ioreg;
		}
	}

	parse_ioreg_device_priority(ioreg_dev);
	parse_ioreg_device_group(ioreg_dev);
	parse_ioreg_valid_range(ioreg_dev);
	parse_ioreg_reg_offsets(ioreg_dev);
#if IS_ENABLED(CONFIG_GOOGLE_IIS)
	ret = parse_ioreg_access_controllers(ioreg_dev);
	if (ret == -ENODEV) {
		if (strstr(dev_name(ioreg_dev->base_dev.dev), "pdma")) {
			dev_err(ioreg_dev->base_dev.dev, "Missing IIS device!\n");
			goto error_ioreg;
		}
	} else if (ret) {
		dev_err_probe(ioreg_dev->base_dev.dev, ret, "Failed to attach iis device\n");
		goto error_ioreg;
	}
#endif

	return 0;

error_ioreg:
	for (i = 0; i < blocks; ++i)
		lwis_ioreg_put_by_idx(ioreg_dev, i);
	lwis_ioreg_list_free(ioreg_dev);
	return ret;
}

int lwis_slc_device_parse_dt(struct lwis_slc_device *slc_dev)
{
	struct lwis_device *lwis_dev = NULL;
	struct device_node *node = NULL;
	int num_pt_id = 0;
	int num_pt_size = 0;
	int i;
	size_t pt_size_kb[MAX_NUM_PT] = {};

	if (!slc_dev) {
		pr_err("SLC device cannot be NULL\n");
		return -ENODEV;
	}
	lwis_dev = &slc_dev->io_dev.base_dev;
	node = lwis_dev->k_dev->of_node;

	num_pt_id = of_property_count_strings(node, "pt_id");
	num_pt_size = of_property_count_u32_elems(node, "pt_size");
	if (num_pt_id != num_pt_size) {
		dev_err(lwis_dev->dev,
			"Mismatch partition names and sizes: %d partition names VS %d partition sizes",
			num_pt_id, num_pt_size);
		return -EINVAL;
	}
	if (num_pt_id > MAX_NUM_PT) {
		dev_err(lwis_dev->dev,
			"The number of partitions in slc device is %d, exceeds the max value %d",
			num_pt_id, MAX_NUM_PT);
		return -EINVAL;
	}

	for (i = 0; i < num_pt_id; i++) {
		/*
		 * Make sure pt_size are in ascending order, since it's required by the
		 * allocation logic.
		 */
		of_property_read_u32_index(node, "pt_size", i, (u32 *)&pt_size_kb[i]);
		if (i > 0 && pt_size_kb[i] < pt_size_kb[i - 1]) {
			dev_err(lwis_dev->dev, "SLC partition sizes are not in ascending order!");
			return -EINVAL;
		}
	}

	slc_dev->num_pt = num_pt_id;
	for (i = 0; i < slc_dev->num_pt; i++) {
		slc_dev->pt[i].id = i;
		slc_dev->pt[i].size_kb = pt_size_kb[i];
		slc_dev->pt[i].fd = -1;
		slc_dev->pt[i].partition_id = lwis_platform_get_default_pt_id();
		slc_dev->pt[i].partition_handle = NULL;
	}

	return 0;
}

int lwis_pwrseq_device_parse_dt(struct lwis_pwrseq_device *pwrseq_dev)
{
	const char *name;
	int ret = 0;
	struct device_node *of_node = pwrseq_dev->base_dev.k_dev->of_node;

	ret = of_property_read_string(of_node, "target-name", &name);
	if (ret < 0) {
		dev_err(pwrseq_dev->base_dev.dev,
			"Error parsing target name from device tree (%d)\n", ret);
		return ret;
	}
	strscpy(pwrseq_dev->target_name, name, LWIS_MAX_NAME_STRING_LEN);

	return 0;
}

int lwis_top_device_parse_dt(struct lwis_top_device *top_dev)
{
	/* To be implemented */
	return 0;
}

int lwis_test_device_parse_dt(struct lwis_test_device *test_dev)
{
	/* To be implemented */
	return 0;
}
