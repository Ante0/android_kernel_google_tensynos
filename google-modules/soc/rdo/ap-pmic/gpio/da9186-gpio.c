// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2023 Google LLC */
#include <linux/types.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <ap-pmic/da9186.h>

// TODO(b/289399123): Move this to separate header file.
#define MB_PMIC_TARGET_GPIO 3

/* Supported PMIC GPIO control commands. */
enum pmic_mb_gpio_cmds {
	MB_GPIO_CMD_GET_DIRECTION = 1,
	MB_GPIO_CMD_SET_DIRECTION,
	MB_GPIO_CMD_GET_VALUE,
	MB_GPIO_CMD_SET_VALUE,
	MB_GPIO_CMD_GET_OUTPUT_DRIVE,
	MB_GPIO_CMD_SET_OUTPUT_DRIVE,
	MB_GPIO_CMD_GET_MODE,
};

/* GPIO direction constants. */
enum da9186_gpio_direction {
	DA9186_GPIO_DIR_INPUT,
	DA9186_GPIO_DIR_OUTPUT,
	DA9186_GPIO_DIR_MAX,
};

/* GPIO output drive constants. */
enum da9186_gpio_output_drive {
	DA9186_GPIO_OUTPUT_DRIVE_PP,
	DA9186_GPIO_OUTPUT_DRIVE_OD,
	DA9186_GPIO_OUTPUT_DRIVE_MAX,
};

/* Total = DA9186 (8) + DA9187 (11) GPIO pins. */
static const struct pinctrl_pin_desc da9186_pins[] = {
	/* DA9186 */
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	/* DA9187 */
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "gpio16"),
	PINCTRL_PIN(17, "gpio17"),
	PINCTRL_PIN(18, "gpio18"),
};

static const int da9186_num_pins = ARRAY_SIZE(da9186_pins);

struct da9186_gpio_ctrl {
	struct device *dev;
	struct pinctrl_desc pdesc;
	struct pinctrl_dev *pctl;
	struct gpio_chip gc;
	struct pmic_mfd_mbox mbox;
	/* DTS configurable parameters. */
	u32 mb_dest_channel;
};

static int gpio_mbox_send_req_helper(struct da9186_gpio_ctrl *priv, u8 cmd,
				     u16 pin, u32 request_data,
				     u32 *response_data)
{
	struct mailbox_data req_data = { request_data, 0 };
	struct mailbox_data resp_data;
	int ret;

	ret = pmic_mfd_mbox_send_req_blocking_read(priv->dev, &priv->mbox,
						   priv->mb_dest_channel,
						   MB_PMIC_TARGET_GPIO, cmd,
						   pin, req_data, &resp_data);
	if (unlikely(ret))
		return ret;

	/* If NULL means the response is skipped. */
	if (response_data)
		*response_data = resp_data.data[0];

	return 0;
}

static int da9186_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	struct da9186_gpio_ctrl *priv = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 argument = 0;
	u32 response_data;
	int ret;

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		ret = gpio_mbox_send_req_helper(priv,
						MB_GPIO_CMD_GET_OUTPUT_DRIVE,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;
		argument = (response_data == DA9186_GPIO_OUTPUT_DRIVE_OD);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		ret = gpio_mbox_send_req_helper(priv,
						MB_GPIO_CMD_GET_OUTPUT_DRIVE,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;
		argument = (response_data == DA9186_GPIO_OUTPUT_DRIVE_PP);
		break;
	case PIN_CONFIG_OUTPUT:
		ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_DIRECTION,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;
		argument = (response_data == DA9186_GPIO_DIR_OUTPUT);
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, argument);

	return 0;
}

static int da9186_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct da9186_gpio_ctrl *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int i;
	u8 command;
	u32 request_data;
	int ret;

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param =
			pinconf_to_config_param(configs[i]);

		switch (param) {
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			command = MB_GPIO_CMD_SET_OUTPUT_DRIVE;
			request_data = DA9186_GPIO_OUTPUT_DRIVE_OD;
			break;
		case PIN_CONFIG_DRIVE_PUSH_PULL:
			command = MB_GPIO_CMD_SET_OUTPUT_DRIVE;
			request_data = DA9186_GPIO_OUTPUT_DRIVE_PP;
			break;
		case PIN_CONFIG_OUTPUT:
			command = MB_GPIO_CMD_SET_DIRECTION;
			request_data = pinconf_to_config_argument(configs[i]);
			break;
		default:
			return -ENOTSUPP;
		}

		ret = gpio_mbox_send_req_helper(priv, command, pin,
						request_data, NULL);
		if (unlikely(ret))
			return ret;
	}

	return 0;
}

static int da9186_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *da9186_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	return NULL;
}

static int da9186_pinmux_get_funcs_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *da9186_pinmux_get_func_name(struct pinctrl_dev *pctldev,
					       unsigned int selector)
{
	return NULL;
}

static int da9186_pinmux_get_func_groups(struct pinctrl_dev *pctldev,
					 unsigned int selector,
					 const char *const **groups,
					 unsigned int *num_groups)
{
	return -ENOTSUPP;
}

static int da9186_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	return -ENOTSUPP;
}

static int da9186_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct da9186_gpio_ctrl *priv = gpiochip_get_data(chip);
	u32 response_data;
	int ret;

	ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_DIRECTION, offset,
					0, &response_data);
	if (unlikely(ret))
		return ret;

	return (response_data == DA9186_GPIO_DIR_OUTPUT) ?
		       GPIO_LINE_DIRECTION_OUT :
		       GPIO_LINE_DIRECTION_IN;
}

static int da9186_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct da9186_gpio_ctrl *priv = gpiochip_get_data(chip);
	u32 request_data = DA9186_GPIO_DIR_INPUT;

	return gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_SET_DIRECTION,
					 offset, request_data, NULL);
}

static int da9186_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	struct da9186_gpio_ctrl *priv = gpiochip_get_data(chip);
	u32 request_data = DA9186_GPIO_DIR_OUTPUT;

	return gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_SET_DIRECTION,
					 offset, request_data, NULL);
}

static int da9186_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct da9186_gpio_ctrl *priv = gpiochip_get_data(chip);
	u32 response_data;
	int ret;

	ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_VALUE, offset, 0,
					&response_data);
	if (unlikely(ret))
		return ret;

	return response_data;
}

static void da9186_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	struct da9186_gpio_ctrl *priv = gpiochip_get_data(chip);
	u32 request_data = value;
	int ret;

	ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_SET_VALUE, offset,
					request_data, NULL);
	if (unlikely(ret))
		dev_dbg(priv->dev, "gpio%d set failed\n", offset);
}

static const struct pinconf_ops da9186_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = da9186_pinconf_get,
	.pin_config_set = da9186_pinconf_set,
};

/* Not supporting most ops as there is no need for pin group control */
static const struct pinctrl_ops da9186_pinctrl_ops = {
	.get_groups_count = da9186_pinctrl_get_groups_count,
	.get_group_name = da9186_pinctrl_get_group_name,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
};

/* Not supporting most ops as PMIC GPIO pins only have GPIO capability */
static const struct pinmux_ops da9186_pinmux_ops = {
	.get_functions_count = da9186_pinmux_get_funcs_count,
	.get_function_name = da9186_pinmux_get_func_name,
	.get_function_groups = da9186_pinmux_get_func_groups,
	.set_mux = da9186_pinmux_set_mux,
};

static const struct pinctrl_desc da9186_pinctrl_desc = {
	.pins = da9186_pins,
	.npins = ARRAY_SIZE(da9186_pins),
	.pctlops = &da9186_pinctrl_ops,
	.pmxops = &da9186_pinmux_ops,
	.confops = &da9186_pinconf_ops,
	.owner = THIS_MODULE,
};

static const struct gpio_chip da9186_gpio_chip = {
	.owner = THIS_MODULE,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.direction_input = da9186_gpio_direction_input,
	.direction_output = da9186_gpio_direction_output,
	.get_direction = da9186_gpio_get_direction,
	.get = da9186_gpio_get,
	.set = da9186_gpio_set,
	.set_config = gpiochip_generic_config,
	.base = -1,
	.ngpio = da9186_num_pins,
	.can_sleep = true, /* Through Mailbox -> CPM, not direct mem access. */
};

static int da9186_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da9186_gpio_ctrl *priv;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	ret = of_property_read_u32(dev->of_node, "mba-dest-channel",
				   &priv->mb_dest_channel);
	if (ret < 0) {
		dev_err(dev, "Failed to read mba-dest-channel.\n");
		return ret;
	}

	ret = pmic_mfd_mbox_request(dev, &priv->mbox);
	if (ret < 0)
		return ret;

	/* pinctrl config, register callbacks */
	priv->pdesc = da9186_pinctrl_desc;
	priv->pdesc.name = dev_name(dev);

	ret = devm_pinctrl_register_and_init(dev, &priv->pdesc, priv,
					     &priv->pctl);
	if (ret < 0) {
		dev_err(dev, "Failed to register pin_ctrl: %d\n", ret);
		goto free_mbox;
	}

	ret = pinctrl_enable(priv->pctl);
	if (ret < 0) {
		dev_err(dev, "Failed to enable pin_ctrl: %d\n", ret);
		goto free_mbox;
	}

	/* GPIO config, register callbacks */
	priv->gc = da9186_gpio_chip;
	priv->gc.label = pdev->name;
	priv->gc.parent = dev;
	priv->gc.fwnode = of_node_to_fwnode(dev->of_node);

	ret = devm_gpiochip_add_data(dev, &priv->gc, priv);
	if (ret < 0) {
		dev_err(dev, "Failed to register gpio_chip: %d\n", ret);
		goto free_mbox;
	}

	return 0;

free_mbox:
	pmic_mfd_mbox_release(&priv->mbox);

	return ret;
}

static void da9186_gpio_remove(struct platform_device *pdev)
{
	struct da9186_gpio_ctrl *priv = platform_get_drvdata(pdev);

	if (IS_ERR_OR_NULL(priv))
		return;

	pmic_mfd_mbox_release(&priv->mbox);
}

static const struct platform_device_id da9186_gpio_id[] = {
	{ "da9186-gpio", 0 },
	{},
};
MODULE_DEVICE_TABLE(platform, da9186_gpio_id);

static struct platform_driver da9186_gpio_driver = {
	.driver = {
		   .name = "da9186-gpio",
		   .owner = THIS_MODULE,
	},
	.probe = da9186_gpio_probe,
	.remove = da9186_gpio_remove,
	.id_table = da9186_gpio_id,
};
module_platform_driver(da9186_gpio_driver);

MODULE_AUTHOR("Ryan Chu <cychu@google.com>");
MODULE_DESCRIPTION("DA9186 PMIC GPIO Driver");
MODULE_LICENSE("GPL");
