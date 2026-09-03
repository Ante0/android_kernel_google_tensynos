// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 *
 */

#include <kunit/static_stub.h>
#include <kunit/visibility.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <bcl.h>
#include <max77759_regs.h>
#include <max777x9_bcl.h>
#include "max77759_bcl_irq.h"

#define OILO_STEP 200
#define OILO_LVL_OFFSET 1

#define OILO_LOWER_LIMIT 4000
#define OILO_UPPER_LIMIT 6800

VISIBLE_IF_KUNIT int
max77759_external_reg_read_helper(struct device *dev, uint8_t reg, uint8_t *val)
{
	KUNIT_STATIC_STUB_REDIRECT(max77759_external_reg_read_helper, dev, reg,
				   val);
	return max77759_external_reg_read(dev, reg, val);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_external_reg_read_helper);

VISIBLE_IF_KUNIT int
max77759_external_reg_write_helper(struct device *dev, uint8_t reg, uint8_t val)
{
	KUNIT_STATIC_STUB_REDIRECT(max77759_external_reg_write_helper, dev, reg,
				   val);
	return max77759_external_reg_write(dev, reg, val);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_external_reg_write_helper);

VISIBLE_IF_KUNIT int max77759_get_irq(struct device *ifpmic_irq_dev, int *idx)
{
	u8 chg_int;
	u8 ret;
	const u8 clr_bcl_irq_mask =
		(MAX77759_CHG_INT2_BAT_OILO_I | MAX77759_CHG_INT2_SYS_UVLO1_I |
		 MAX77759_CHG_INT2_SYS_UVLO2_I);
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	ret = max77759_external_reg_read_helper(data->pmic_dev,
						MAX77759_CHG_INT2, &chg_int);
	if (ret < 0)
		return IRQ_NONE;
	if (!(chg_int & clr_bcl_irq_mask))
		return IRQ_NONE;

	/* UVLO2 has the highest priority and then BATOILO, then UVLO1 */
	if (chg_int & MAX77759_CHG_INT2_SYS_UVLO2_I)
		*idx = UVLO2;
	else if (chg_int & MAX77759_CHG_INT2_BAT_OILO_I)
		*idx = BATOILO;
	else if (chg_int & MAX77759_CHG_INT2_SYS_UVLO1_I)
		*idx = UVLO1;

	return ret;
}
EXPORT_SYMBOL_IF_KUNIT(max77759_get_irq);

VISIBLE_IF_KUNIT int max77759_clr_irq(struct device *ifpmic_irq_dev, int idx)
{
	u8 chg_int = 0;
	int ret;
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	if (idx == UVLO2 || idx == BATOILO)
		chg_int = MAX77759_CHG_INT2_SYS_UVLO2_I |
			  MAX77759_CHG_INT2_BAT_OILO_I;
	else if (idx == UVLO1)
		chg_int = MAX77759_CHG_INT2_SYS_UVLO1_I;

	ret = max77759_external_reg_write_helper(data->pmic_dev,
						 MAX77759_CHG_INT2, chg_int);
	if (ret < 0)
		return IRQ_NONE;
	return ret;
}
EXPORT_SYMBOL_IF_KUNIT(max77759_clr_irq);

VISIBLE_IF_KUNIT int max77759_vimon_read(struct device *ifpmic_irq_dev)
{
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(max77759_vimon_read);

static int max77759_get_raw_sts(struct device *ifpmic_irq_dev, int idx)
{
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (idx) {
	case UVLO1:
		if (!data->vd1_gpio)
			return -ENODEV;
		return gpiod_get_raw_value(data->vd1_gpio);
	case UVLO2:
	case BATOILO:
		if (!data->vd2_gpio)
			return -ENODEV;
		return gpiod_get_raw_value(data->vd2_gpio);
	default:
		return -EINVAL;
	}
}

static irqreturn_t max77759_bcl_vdroop_handler(int irq, void *ptr)
{
	int ret;
	struct max77759_irq_context *irq_data = ptr;
	struct max77759_bcl_irq_data *data = irq_data->parent;

	ret = google_bcl_mitigation_trigger(irq_data->idx, data->bcl_dev);
	if (ret < 0)
		dev_err(data->dev, "Mitigation driver err (%d)", ret);

	/* IRQ clearing handled by bcl_core */

	return IRQ_HANDLED;
}

static irqreturn_t max77759_bcl_irqb_handler(int irq, void *ptr)
{
	int ret;
	struct max77759_irq_context *irq_data = ptr;
	struct max77759_bcl_irq_data *data = irq_data->parent;

	max77759_clr_irq(data->dev, BATOILO);

	ret = google_bcl_mitigation_trigger(irq_data->idx, data->bcl_dev);
	if (ret < 0)
		dev_err(data->dev, "Mitigation driver err (%d)", ret);

	return IRQ_HANDLED;
}

VISIBLE_IF_KUNIT int max77759_set_oilo(struct device *ifpmic_irq_dev, int val)
{
	u8 oilo_threshold, reg;
	int ret;
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	ret = max77759_external_reg_read_helper(data->pmic_dev,
						MAX77759_CHG_CNFG_14, &reg);
	if (ret < 0)
		return ret;

	if (val == 0)
		oilo_threshold = 0;
	else if (val >= OILO_LOWER_LIMIT && val <= OILO_UPPER_LIMIT)
		oilo_threshold =
			OILO_LVL_OFFSET + (val - OILO_LOWER_LIMIT) / OILO_STEP;
	else
		return -EINVAL;

	reg = _chg_cnfg_14_bat_oilo_set(reg, oilo_threshold);
	return max77759_external_reg_write_helper(data->pmic_dev,
						  MAX77759_CHG_CNFG_14, reg);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_set_oilo);

VISIBLE_IF_KUNIT int max77759_get_oilo(struct device *ifpmic_irq_dev, int *val)
{
	u8 reg, oilo_threshold;
	int ret;
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	ret = max77759_external_reg_read_helper(data->pmic_dev,
						MAX77759_CHG_CNFG_14, &reg);
	if (ret < 0)
		return ret;

	oilo_threshold = _chg_cnfg_14_bat_oilo_get(reg);
	if (oilo_threshold == 0)
		*val = 0;
	else
		*val = OILO_STEP * (oilo_threshold - OILO_LVL_OFFSET) +
		       OILO_LOWER_LIMIT;
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(max77759_get_oilo);

static int unimplemented_set_handler(struct device *ifpmic_irq_dev, int val)
{
	dev_err(ifpmic_irq_dev, "%s called with param: %d", __func__, val);
	return -EINVAL;
}

static int unimplemented_get_handler(struct device *ifpmic_irq_dev, int *val)
{
	dev_err(ifpmic_irq_dev, "%s called with ptr param: %p\n", __func__,
		(void *)val);
	return -EINVAL;
}

enum UVLO_TYPE {
	UVLO1_INTERNAL,
	UVLO2_INTERNAL,
};

static int set_uvlo_helper(struct device *ifpmic_irq_dev, int val,
			   enum UVLO_TYPE type)
{
	int addr, ret;
	uint8_t new_reg, read_reg;
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (type) {
	case UVLO1_INTERNAL:
		addr = MAX77759_CHG_CNFG_15;
		break;
	case UVLO2_INTERNAL:
		addr = MAX77759_CHG_CNFG_16;
		break;
	default:
		return -EINVAL;
	}

	if (val < VD_LOWER_LIMIT || val > VD_UPPER_LIMIT)
		return -EINVAL;

	new_reg = (val - VD_LOWER_LIMIT) / VD_STEP;

	ret = max77759_external_reg_read_helper(data->pmic_dev, addr,
						&read_reg);
	if (ret < 0)
		return -EINVAL;

	if (type == UVLO1_INTERNAL)
		read_reg = _chg_cnfg_15_sys_uvlo1_set(read_reg, new_reg);
	else
		read_reg = _chg_cnfg_16_sys_uvlo2_set(read_reg, new_reg);

	ret = max77759_external_reg_write_helper(data->pmic_dev, addr,
						 read_reg);
	if (ret < 0)
		return -EINVAL;

	return ret;
}

static int get_uvlo_helper(struct device *ifpmic_irq_dev, int *val,
			   enum UVLO_TYPE type)
{
	u8 reg;
	int ret, addr;
	uint8_t read_reg;
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (type) {
	case UVLO1_INTERNAL:
		addr = MAX77759_CHG_CNFG_15;
		break;
	case UVLO2_INTERNAL:
		addr = MAX77759_CHG_CNFG_16;
		break;
	default:
		return -EINVAL;
	}

	ret = max77759_external_reg_read_helper(data->pmic_dev, addr, &reg);
	if (ret < 0)
		return -EINVAL;

	if (type == UVLO1_INTERNAL)
		read_reg = _chg_cnfg_15_sys_uvlo1_get(reg);
	else
		read_reg = _chg_cnfg_16_sys_uvlo2_get(reg);

	*val = VD_STEP * read_reg + VD_LOWER_LIMIT;
	return 0;
}

static int set_uvlo_hyst_helper(struct device *ifpmic_irq_dev, int val,
				int uvlo_type)
{
	int ret;
	u8 regval;
	int addr;
	uint8_t (*max77759_sys_uvlo_hyst_set)(uint8_t regval, uint8_t val);
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	if (val < HYST_LOWER_LIMIT || val > HYST_UPPER_LIMIT)
		return -EINVAL;

	switch (uvlo_type) {
	case UVLO1:
		addr = MAX77759_CHG_CNFG_15;
		max77759_sys_uvlo_hyst_set = _chg_cnfg_15_sys_uvlo1_hyst_set;
		break;
	case UVLO2:
		addr = MAX77759_CHG_CNFG_16;
		max77759_sys_uvlo_hyst_set = _chg_cnfg_16_sys_uvlo2_hyst_set;
		break;
	default:
		return -EINVAL;
	}

	ret = max77759_external_reg_read_helper(data->pmic_dev, addr, &regval);

	if (ret < 0)
		return ret;

	val = (val - HYST_LOWER_LIMIT) / HYST_STEP;
	regval = max77759_sys_uvlo_hyst_set(regval, val);
	return max77759_external_reg_write_helper(data->pmic_dev, addr,
						      regval);
}

static int get_uvlo_hyst_helper(struct device *ifpmic_irq_dev, int *val,
				int uvlo_type)
{
	u8 reg;
	int ret, addr;
	uint8_t (*max77759_sys_uvlo_hyst_get)(uint8_t regval);
	struct max77759_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (uvlo_type) {
	case UVLO1:
		addr = MAX77759_CHG_CNFG_15;
		max77759_sys_uvlo_hyst_get = _chg_cnfg_15_sys_uvlo1_hyst_get;
		break;
	case UVLO2:
		addr = MAX77759_CHG_CNFG_16;
		max77759_sys_uvlo_hyst_get = _chg_cnfg_16_sys_uvlo2_hyst_get;
		break;
	default:
		return -EINVAL;
	}

	ret = max77759_external_reg_read_helper(data->pmic_dev, addr, &reg);

	if (ret < 0)
		return ret;

	*val = (HYST_STEP * max77759_sys_uvlo_hyst_get(reg)) + HYST_LOWER_LIMIT;
	return 0;
}

VISIBLE_IF_KUNIT int max77759_set_uvlo1(struct device *ifpmic_irq_dev, int val)
{
	return set_uvlo_helper(ifpmic_irq_dev, val, UVLO1_INTERNAL);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_set_uvlo1);

VISIBLE_IF_KUNIT int max77759_get_uvlo1(struct device *ifpmic_irq_dev, int *val)
{
	return get_uvlo_helper(ifpmic_irq_dev, val, UVLO1_INTERNAL);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_get_uvlo1);

VISIBLE_IF_KUNIT int max77759_set_uvlo2(struct device *ifpmic_irq_dev, int val)
{
	return set_uvlo_helper(ifpmic_irq_dev, val, UVLO2_INTERNAL);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_set_uvlo2);

VISIBLE_IF_KUNIT int max77759_get_uvlo2(struct device *ifpmic_irq_dev, int *val)
{
	return get_uvlo_helper(ifpmic_irq_dev, val, UVLO2_INTERNAL);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_get_uvlo2);

VISIBLE_IF_KUNIT int max77759_set_uvlo1_hyst(struct device *ifpmic_irq_dev, int val)
{
	return set_uvlo_hyst_helper(ifpmic_irq_dev, val, UVLO1);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_set_uvlo1_hyst);

VISIBLE_IF_KUNIT int max77759_get_uvlo1_hyst(struct device *ifpmic_irq_dev, int *val)
{
	return get_uvlo_hyst_helper(ifpmic_irq_dev, val, UVLO1);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_get_uvlo1_hyst);

VISIBLE_IF_KUNIT int max77759_set_uvlo2_hyst(struct device *ifpmic_irq_dev, int val)
{
	return set_uvlo_hyst_helper(ifpmic_irq_dev, val, UVLO2);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_set_uvlo2_hyst);

VISIBLE_IF_KUNIT int max77759_get_uvlo2_hyst(struct device *ifpmic_irq_dev, int *val)
{
	return get_uvlo_hyst_helper(ifpmic_irq_dev, val, UVLO2);
}
EXPORT_SYMBOL_IF_KUNIT(max77759_get_uvlo2_hyst);

static struct bcl_ifpmic_ops max77759_bcl_ops = {
	.get_raw_sts = max77759_get_raw_sts,
	.get_irq = max77759_get_irq,
	.clr_irq = max77759_clr_irq,
	.vimon_read = max77759_vimon_read,
	.get_oilo1 = max77759_get_oilo,
	.set_oilo1 = max77759_set_oilo,
	.get_oilo2 = unimplemented_get_handler,
	.set_oilo2 = unimplemented_set_handler,
	.get_uvlo1 = max77759_get_uvlo1,
	.set_uvlo1 = max77759_set_uvlo1,
	.get_uvlo2 = max77759_get_uvlo2,
	.set_uvlo2 = max77759_set_uvlo2,
	.set_uvlo_vdroop = NULL, /* no vdroop config support on max77759 */
	.set_oilo_vdroop = NULL, /* no vdroop config support on max77759 */
	.get_uvlo1_hyst = max77759_get_uvlo1_hyst,
	.set_uvlo1_hyst = max77759_set_uvlo1_hyst,
	.get_uvlo2_hyst = max77759_get_uvlo2_hyst,
	.set_uvlo2_hyst = max77759_set_uvlo2_hyst,
};

static void max77759_bcl_irq_remove(struct platform_device *pdev)
{
	struct max77759_bcl_irq_data *data = platform_get_drvdata(pdev);

	max77759_clr_irq(data->dev, UVLO1);
	max77759_clr_irq(data->dev, UVLO2);
	max77759_clr_irq(data->dev, BATOILO);

	google_bcl_unregister_ifpmic();
}

static int max77759_bcl_irq_probe(struct platform_device *pdev)
{
	struct max77759_bcl_irq_data *data;
	struct device *google_bcl_dev;
	struct device_node *np;
	struct device_node *ifpmic_np;
	struct device_node *child;
	struct device_node *google_bcl_np;
	struct device_node *irq_list_node;
	struct i2c_client *ifpmic_i2c_client;
	struct platform_device *google_bcl_device;
	struct gpio_desc *gpio_desc;
	const char *label;
	const char *interrupt_name;
	const char *irq_handler_str;
	irq_handler_t handler_to_arm;
	unsigned int child_reg_index;
	int ret = 0;
	int ind = 0;
	int irq;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;
	platform_set_drvdata(pdev, data);

	google_bcl_np =
		of_find_compatible_node(NULL, NULL, "google,google-bcl");
	if (!google_bcl_np)
		return -ENODEV;

	google_bcl_device = of_find_device_by_node(google_bcl_np);
	if (!google_bcl_device) {
		of_node_put(google_bcl_np);
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "Couldn't find google-bcl device\n");
	}

	of_node_put(google_bcl_np);

	google_bcl_dev = &google_bcl_device->dev;

	data->bcl_dev = dev_get_drvdata(google_bcl_dev);
	if (!data->bcl_dev)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "Couldn't get google-bcl drvdata\n");

	ret = google_bcl_register_ifpmic(&max77759_bcl_ops, data->dev);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "Failed to register ifpmic ops\n");

	ifpmic_np = of_parse_phandle(data->dev->of_node, "ifpmic", 0);
	if (!ifpmic_np)
		return -EINVAL;

	ifpmic_i2c_client = of_find_i2c_device_by_node(ifpmic_np);
	if (!ifpmic_i2c_client)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "Couldn't find ifpmic\n");

	data->pmic_dev = &ifpmic_i2c_client->dev;

	of_node_put(ifpmic_np);

	np = data->dev->of_node;

	irq_list_node = of_get_child_by_name(np, "max77759-bcl-interrupts");
	if (!irq_list_node)
		return -EINVAL;

	for_each_available_child_of_node(irq_list_node, child) {
		ret = of_property_read_string(child, "label", &label);
		if (ret < 0)
			goto clean_up_node;

		ret = of_property_read_u32(child, "reg", &child_reg_index);
		if (ret < 0)
			goto clean_up_node;

		gpio_desc =
			devm_gpiod_get_index(data->dev, NULL, ind, GPIOD_ASIS);
		if (IS_ERR(gpio_desc)) {
			ret = PTR_ERR(gpio_desc);
			goto clean_up_node;
		}

		irq = gpiod_to_irq(gpio_desc);
		if (irq < 0) {
			ret = irq;
			goto clean_up_node;
		}

		ret = of_property_read_string(child, "irq-handler",
					      &irq_handler_str);
		if (ret < 0)
			goto clean_up_node;

		ret = of_property_read_string(child, "interrupt-name",
					      &interrupt_name);
		if (ret < 0)
			goto clean_up_node;

		if (!data->irq_ctx[ind]) {
			data->irq_ctx[ind] = devm_kzalloc(
				data->dev, sizeof(struct max77759_irq_context),
				GFP_KERNEL);
			if (!data->irq_ctx[ind])
				return -ENOMEM;
		}

		if (strcmp(label, "uvlo1") == 0)
			data->irq_ctx[ind]->idx = UVLO1;
		else if (strcmp(label, "uvlo2") == 0)
			data->irq_ctx[ind]->idx = UVLO2;
		else if (strcmp(label, "oilo1") == 0)
			data->irq_ctx[ind]->idx = BATOILO1;

		data->irq_ctx[ind]->parent = data;

		if (strcmp(irq_handler_str, "vdroop") == 0) {
			handler_to_arm = max77759_bcl_vdroop_handler;
		} else if (strcmp(irq_handler_str, "irqb") == 0) {
			handler_to_arm = max77759_bcl_irqb_handler;
		} else {
			ret = -EINVAL;
			goto clean_up_node;
		}

		if (strcmp(interrupt_name, "vd1") == 0) {
			data->vd1_gpio = gpio_desc;
		} else if (strcmp(interrupt_name, "vd2") == 0) {
			data->vd2_gpio = gpio_desc;
		}

		ret = devm_request_threaded_irq(
			data->dev, irq, NULL, handler_to_arm,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_SHARED,
			interrupt_name, data->irq_ctx[ind]);
		if (ret < 0)
			goto clean_up_node;

		ind++;
	}

	max77759_clr_irq(data->dev, UVLO1);
	max77759_clr_irq(data->dev, UVLO2);
	max77759_clr_irq(data->dev, BATOILO);
clean_up_node:
	of_node_put(irq_list_node);

	if (ret < 0)
		dev_err_probe(data->dev, ret, "Configuration invalid\n");

	return ret;
}

static const struct platform_device_id max77779_bcl_irq_id[] = {
	{ "max77759-bcl-irq", 0 },
	{},
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id max77779_bcl_irq_match_table[] = {
	{
		.compatible = "max77759-bcl-irq",
	},
	{},
};
#endif

static struct platform_driver max77759_bcl_irq_driver = {
	.driver = {
		.name = "max77759-bcl-irq",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = max77779_bcl_irq_match_table,
#endif
	},
	.probe = max77759_bcl_irq_probe,
	.remove = max77759_bcl_irq_remove,
	/* .id_table = max77759_bcl_irq_id, */
};

module_platform_driver(max77759_bcl_irq_driver);

MODULE_DESCRIPTION("Maxim 77759 BCL IRQ driver");
MODULE_AUTHOR("Allen Jiang <alljiang@google.com>");
MODULE_AUTHOR("Sam Ou <samou@google.com>");
MODULE_AUTHOR("Jasmine Cha <chajasmine@google.com>");
MODULE_AUTHOR("Maggie Cheng <maggiecheng@google.com>");
MODULE_AUTHOR("Hiroshi Akiyama <hiroshiakiyama@google.com>");
MODULE_LICENSE("GPL");
