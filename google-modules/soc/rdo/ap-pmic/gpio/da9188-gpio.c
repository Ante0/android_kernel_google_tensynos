// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024 Google LLC */
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <ap-pmic/da9188.h>
#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>
#include <soc/google/goog_cpm_service_ids.h>

/* GPIO direction constants used in MBA protocol. */
enum da9188_gpio_direction {
	DA9188_GPIO_DIR_INPUT,
	DA9188_GPIO_DIR_OUTPUT,
	DA9188_GPIO_DIR_MAX,
};

/* GPIO output drive constants used in MBA protocol. */
enum da9188_gpio_output_drive {
	DA9188_GPIO_OUTPUT_DRIVE_PP,
	DA9188_GPIO_OUTPUT_DRIVE_OD,
	DA9188_GPIO_OUTPUT_DRIVE_MAX,
};

/* GPIO output drive strength constants used in MBA protocol. */
enum da9188_gpio_output_drive_strength {
	DA9188_GPIO_OUTPUT_DRIVE_STR_2MA,
	DA9188_GPIO_OUTPUT_DRIVE_STR_4MA,
	DA9188_GPIO_OUTPUT_DRIVE_STR_6MA,
	DA9188_GPIO_OUTPUT_DRIVE_STR_8MA,
	DA9188_GPIO_OUTPUT_DRIVE_STR_MAX,
};

/* GPIO pull configuration constants used in MBA protocol. */
enum da9188_gpio_pull_config {
	DA9188_GPIO_PULL_OFF,
	DA9188_GPIO_PULL_UP_STRONG,
	DA9188_GPIO_PULL_UP_WEAK,
	DA9188_GPIO_PULL_DOWN_STRONG,
	DA9188_GPIO_PULL_DOWN_WEAK,
	DA9188_GPIO_PULL_MAX,
};

/* GPIO supported output drive strength constants, unit in mA. */
enum da9188_gpio_supported_output_drive_strength {
	DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_2MA = 2,
	DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_4MA = 4,
	DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_6MA = 6,
	DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_8MA = 8,
};

/* GPIO supported pull configuration constants, unit in Ohm. */
enum da9188_gpio_supported_pull_config {
	DA9188_GPIO_SUPPORTED_PULL_20KOHM = 20000,
	DA9188_GPIO_SUPPORTED_PULL_800KOHM = 800000,
};

/* Total = DA9188 (8) + DA9189 (11) GPIO pins. */
static const struct pinctrl_pin_desc da9188_pins[] = {
	/* DA9188 */
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	/* DA9189 */
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

static const int da9188_num_pins = ARRAY_SIZE(da9188_pins);

struct da9188_gpio_ctrl {
	struct device *dev;
	struct pinctrl_desc pdesc;
	struct pinctrl_dev *pctl;
	struct gpio_chip gc;
	struct pmic_mfd_mbox mbox;
	/* DTS configurable parameters. */
	u32 mb_dest_channel;
};

static int gpio_mbox_send_req_helper(struct da9188_gpio_ctrl *priv, u8 cmd,
				     u16 pin, u32 request_data,
				     u32 *response_data)
{
	struct mailbox_data req_data = { request_data, 0 };
	struct mailbox_data resp_data;
	int ret;

	ret = da9188_mfd_mbox_send_req_blocking_read(priv->dev, &priv->mbox,
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

static int da9188_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *config)
{
	struct da9188_gpio_ctrl *priv = pinctrl_dev_get_drvdata(pctldev);
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
		if (response_data == DA9188_GPIO_OUTPUT_DRIVE_OD)
			argument = 1;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		ret = gpio_mbox_send_req_helper(priv,
						MB_GPIO_CMD_GET_OUTPUT_DRIVE,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;
		if (response_data == DA9188_GPIO_OUTPUT_DRIVE_PP)
			argument = 1;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_OUTPUT:
		ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_DIRECTION,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;

		if (response_data == DA9188_GPIO_DIR_INPUT)
			return -EINVAL;
		ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_VALUE,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;
		argument = response_data;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_DIRECTION,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;
		if (response_data == DA9188_GPIO_DIR_OUTPUT)
			argument = 1;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = gpio_mbox_send_req_helper(priv,
						MB_GPIO_CMD_GET_DRIVE_STRENGTH,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;
		// Mapping to mA that the framework expects.
		switch (response_data) {
		case DA9188_GPIO_OUTPUT_DRIVE_STR_2MA:
			argument = DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_2MA;
			break;
		case DA9188_GPIO_OUTPUT_DRIVE_STR_4MA:
			argument = DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_4MA;
			break;
		case DA9188_GPIO_OUTPUT_DRIVE_STR_6MA:
			argument = DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_6MA;
			break;
		case DA9188_GPIO_OUTPUT_DRIVE_STR_8MA:
			argument = DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_8MA;
			break;
		default:
			// Impossible.
			return -EINVAL;
		}
		break;
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = gpio_mbox_send_req_helper(priv,
						MB_GPIO_CMD_GET_PULL_CONFIG,
						pin, 0, &response_data);
		if (unlikely(ret))
			return ret;

		if (param == PIN_CONFIG_BIAS_DISABLE) {
			if (response_data == DA9188_GPIO_PULL_OFF)
				argument = 1;
			else
				return -EINVAL;
		} else if (param == PIN_CONFIG_BIAS_PULL_UP) {
			if (response_data == DA9188_GPIO_PULL_UP_STRONG)
				argument = DA9188_GPIO_SUPPORTED_PULL_20KOHM;
			else if (response_data == DA9188_GPIO_PULL_UP_WEAK)
				argument = DA9188_GPIO_SUPPORTED_PULL_800KOHM;
			else
				return -EINVAL;
		} else if (param == PIN_CONFIG_BIAS_PULL_DOWN) {
			if (response_data == DA9188_GPIO_PULL_DOWN_STRONG)
				argument = DA9188_GPIO_SUPPORTED_PULL_20KOHM;
			else if (response_data == DA9188_GPIO_PULL_DOWN_WEAK)
				argument = DA9188_GPIO_SUPPORTED_PULL_800KOHM;
			else
				return -EINVAL;
		}
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, argument);

	return 0;
}

static int da9188_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			      unsigned long *configs, unsigned int num_configs)
{
	struct da9188_gpio_ctrl *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int i;
	u8 command;
	u32 request_data;
	int ret;

	for (i = 0; i < num_configs; ++i) {
		enum pin_config_param param =
			pinconf_to_config_param(configs[i]);
		u32 argument = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			command = MB_GPIO_CMD_SET_OUTPUT_DRIVE;
			request_data = DA9188_GPIO_OUTPUT_DRIVE_OD;
			break;
		case PIN_CONFIG_DRIVE_PUSH_PULL:
			command = MB_GPIO_CMD_SET_OUTPUT_DRIVE;
			request_data = DA9188_GPIO_OUTPUT_DRIVE_PP;
			break;
		case PIN_CONFIG_OUTPUT:
			/* First set to output. */
			ret = gpio_mbox_send_req_helper(priv,
							MB_GPIO_CMD_SET_DIRECTION,
							pin,
							DA9188_GPIO_DIR_OUTPUT,
							NULL);
			if (unlikely(ret))
				return ret;
			// Issue another command to set the output value.
			command = MB_GPIO_CMD_SET_VALUE;
			request_data = argument ? 1 : 0;
			break;
		case PIN_CONFIG_OUTPUT_ENABLE:
			command = MB_GPIO_CMD_SET_DIRECTION;
			request_data = argument ? DA9188_GPIO_DIR_OUTPUT :
						  DA9188_GPIO_DIR_INPUT;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			command = MB_GPIO_CMD_SET_DRIVE_STRENGTH;
			switch (argument) {
			case DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_2MA:
				request_data = DA9188_GPIO_OUTPUT_DRIVE_STR_2MA;
				break;
			case DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_4MA:
				request_data = DA9188_GPIO_OUTPUT_DRIVE_STR_4MA;
				break;
			case DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_6MA:
				request_data = DA9188_GPIO_OUTPUT_DRIVE_STR_6MA;
				break;
			case DA9188_GPIO_SUPPORTED_OUTPUT_DRIVE_STR_8MA:
				request_data = DA9188_GPIO_OUTPUT_DRIVE_STR_8MA;
				break;
			default:
				dev_err(priv->dev,
					"Drive-strength %umA not supported\n",
					argument);
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			command = MB_GPIO_CMD_SET_PULL_CONFIG;
			request_data = DA9188_GPIO_PULL_OFF;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			command = MB_GPIO_CMD_SET_PULL_CONFIG;
			if (argument == DA9188_GPIO_SUPPORTED_PULL_20KOHM) {
				request_data = DA9188_GPIO_PULL_UP_STRONG;
			} else if (argument ==
				   DA9188_GPIO_SUPPORTED_PULL_800KOHM) {
				request_data = DA9188_GPIO_PULL_UP_WEAK;
			} else {
				dev_err(priv->dev,
					"Pull-up %u ohm not supported\n",
					argument);
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			command = MB_GPIO_CMD_SET_PULL_CONFIG;
			if (argument == DA9188_GPIO_SUPPORTED_PULL_20KOHM) {
				request_data = DA9188_GPIO_PULL_DOWN_STRONG;
			} else if (argument ==
				   DA9188_GPIO_SUPPORTED_PULL_800KOHM) {
				request_data = DA9188_GPIO_PULL_DOWN_WEAK;
			} else {
				dev_err(priv->dev,
					"Pull-down %u ohm not supported\n",
					argument);
				return -ENOTSUPP;
			}
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

static int da9188_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *da9188_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int selector)
{
	return NULL;
}

static int da9188_pinmux_get_funcs_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *da9188_pinmux_get_func_name(struct pinctrl_dev *pctldev,
					       unsigned int selector)
{
	return NULL;
}

static int da9188_pinmux_get_func_groups(struct pinctrl_dev *pctldev,
					 unsigned int selector,
					 const char *const **groups,
					 unsigned int *num_groups)
{
	return -ENOTSUPP;
}

static int da9188_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	return -ENOTSUPP;
}

static int da9188_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
					    struct pinctrl_gpio_range *range,
					    unsigned int offset, bool input)
{
	unsigned long config =
		pinconf_to_config_packed(PIN_CONFIG_OUTPUT_ENABLE, !input);

	return da9188_pinconf_set(pctldev, offset, &config, 1);
}

static int da9188_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct da9188_gpio_ctrl *priv = gpiochip_get_data(chip);
	u32 response_data;
	int ret;

	ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_VALUE, offset, 0,
					&response_data);
	if (unlikely(ret))
		return ret;

	return response_data;
}

static void da9188_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	struct da9188_gpio_ctrl *priv = gpiochip_get_data(chip);
	u32 request_data = value;
	int ret;

	ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_SET_VALUE, offset,
					request_data, NULL);
	if (unlikely(ret))
		dev_dbg(priv->dev, "gpio%d set failed\n", offset);
}

static int da9188_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct da9188_gpio_ctrl *priv = gpiochip_get_data(chip);
	u32 response_data;
	int ret;

	ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_DIRECTION, offset,
					0, &response_data);
	if (unlikely(ret))
		return ret;

	return (response_data == DA9188_GPIO_DIR_OUTPUT) ?
		       GPIO_LINE_DIRECTION_OUT :
		       GPIO_LINE_DIRECTION_IN;
}

static int da9188_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	int ret;

	ret = pinctrl_gpio_direction_output(chip, offset);
	if (unlikely(ret))
		return ret;

	da9188_gpio_set(chip, offset, value);

	return ret;
}

static const struct pinconf_ops da9188_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = da9188_pinconf_get,
	.pin_config_set = da9188_pinconf_set,
};

/* Not supporting most ops as there is no need for pin group control */
static const struct pinctrl_ops da9188_pinctrl_ops = {
	.get_groups_count = da9188_pinctrl_get_groups_count,
	.get_group_name = da9188_pinctrl_get_group_name,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
};

/* Not supporting most ops as PMIC GPIO pins only have GPIO capability */
static const struct pinmux_ops da9188_pinmux_ops = {
	.get_functions_count = da9188_pinmux_get_funcs_count,
	.get_function_name = da9188_pinmux_get_func_name,
	.get_function_groups = da9188_pinmux_get_func_groups,
	.set_mux = da9188_pinmux_set_mux,
	.gpio_set_direction = da9188_pinmux_gpio_set_direction,
};

static const struct pinctrl_desc da9188_pinctrl_desc = {
	.pins = da9188_pins,
	.npins = ARRAY_SIZE(da9188_pins),
	.pctlops = &da9188_pinctrl_ops,
	.pmxops = &da9188_pinmux_ops,
	.confops = &da9188_pinconf_ops,
	.owner = THIS_MODULE,
};

static const struct gpio_chip da9188_gpio_chip = {
	.owner = THIS_MODULE,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.direction_input = pinctrl_gpio_direction_input,
	.direction_output = da9188_gpio_direction_output,
	.get_direction = da9188_gpio_get_direction,
	.get = da9188_gpio_get,
	.set = da9188_gpio_set,
	.set_config = gpiochip_generic_config,
	.base = -1,
	.ngpio = da9188_num_pins,
	.can_sleep = true, /* Through Mailbox -> CPM, not direct mem access. */
};

static int da9188_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da9188_gpio_ctrl *priv;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);
	priv->mb_dest_channel = CPM_COMMON_PMIC_SERVICE;

	ret = da9188_mfd_mbox_request(dev, &priv->mbox);
	if (ret < 0)
		return ret;

	/* pinctrl config, register callbacks */
	priv->pdesc = da9188_pinctrl_desc;
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
	priv->gc = da9188_gpio_chip;
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
	da9188_mfd_mbox_release(&priv->mbox);

	return ret;
}

static void da9188_gpio_remove(struct platform_device *pdev)
{
	struct da9188_gpio_ctrl *priv = platform_get_drvdata(pdev);

	if (IS_ERR_OR_NULL(priv))
		return;

	da9188_mfd_mbox_release(&priv->mbox);
}

static const struct platform_device_id da9188_gpio_id[] = {
	{ "da9188-gpio", 0 },
	{},
};
MODULE_DEVICE_TABLE(platform, da9188_gpio_id);

static struct platform_driver da9188_gpio_driver = {
	.driver = {
		   .name = "da9188-gpio",
		   .owner = THIS_MODULE,
	},
	.probe = da9188_gpio_probe,
	.remove = da9188_gpio_remove,
	.id_table = da9188_gpio_id,
};
module_platform_driver(da9188_gpio_driver);

MODULE_AUTHOR("Ryan Chu <cychu@google.com>");
MODULE_DESCRIPTION("DA9188 PMIC GPIO Driver");
MODULE_LICENSE("GPL");
