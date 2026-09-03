// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024 Google LLC */
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <ap-pmic/da9188.h>
#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>
#include <soc/google/goog_cpm_service_ids.h>

/* Defined as da9188_gpio_ids in CPM */
#define DA9188_NUM_PHYSICAL_PINS 19
#define DA9188_NUM_VGPIOS 32

struct da9188_vgpio_ctrl {
	struct device *dev;
	struct gpio_chip vgc;
	struct pmic_mfd_mbox mbox;
	/* DTS configurable parameters. */
	u32 mb_dest_channel;
};

static int gpio_mbox_send_req_helper(struct da9188_vgpio_ctrl *priv, u8 cmd,
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

static int da9188_vgpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct da9188_vgpio_ctrl *priv = gpiochip_get_data(chip);
	u16 pin_num = DA9188_NUM_PHYSICAL_PINS + offset;
	u32 response_data;
	int ret;

	ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_GET_VALUE, pin_num, 0,
					&response_data);
	if (unlikely(ret))
		return ret;

	return response_data;
}

static void da9188_vgpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	struct da9188_vgpio_ctrl *priv = gpiochip_get_data(chip);
	u16 pin_num = DA9188_NUM_PHYSICAL_PINS + offset;
	u32 request_data;
	int ret;

	request_data = value;
	ret = gpio_mbox_send_req_helper(priv, MB_GPIO_CMD_SET_VALUE, pin_num,
					request_data, NULL);
	if (unlikely(ret))
		dev_dbg(priv->dev, "vgpio%d set failed\n", pin_num);
}

static const struct gpio_chip da9188_vgpio_chip = {
	.owner = THIS_MODULE,
	/* Direction is always output can not change. */
	.get = da9188_vgpio_get,
	.set = da9188_vgpio_set,
	.base = -1,
	.ngpio = DA9188_NUM_VGPIOS,
	.can_sleep = true, /* Through Mailbox -> CPM, not direct mem access. */
};

static int da9188_vgpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da9188_vgpio_ctrl *priv;
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

	/* Also register vGPIO config, register callbacks */
	priv->vgc = da9188_vgpio_chip;
	priv->vgc.label = pdev->name;
	priv->vgc.parent = dev;
	priv->vgc.fwnode = of_node_to_fwnode(dev->of_node);

	ret = devm_gpiochip_add_data(dev, &priv->vgc, priv);
	if (ret < 0) {
		dev_err(dev, "Failed to register gpio_chip: %d\n", ret);
		goto free_mbox;
	}

	return 0;

free_mbox:

	da9188_mfd_mbox_release(&priv->mbox);

	return ret;
}

static void da9188_vgpio_remove(struct platform_device *pdev)
{
	struct da9188_vgpio_ctrl *priv = platform_get_drvdata(pdev);

	if (IS_ERR_OR_NULL(priv))
		return;

	da9188_mfd_mbox_release(&priv->mbox);
}

static const struct platform_device_id da9188_vgpio_id[] = {
	{ "da9188-vgpio", 0 },
	{},
};
MODULE_DEVICE_TABLE(platform, da9188_vgpio_id);

static struct platform_driver da9188_vgpio_driver = {
	.driver = {
		   .name = "da9188-vgpio",
		   .owner = THIS_MODULE,
	},
	.probe = da9188_vgpio_probe,
	.remove = da9188_vgpio_remove,
	.id_table = da9188_vgpio_id,
};
module_platform_driver(da9188_vgpio_driver);

MODULE_AUTHOR("Ryan Chu <cychu@google.com>");
MODULE_DESCRIPTION("DA9188 PMIC vGPIO Driver");
MODULE_LICENSE("GPL");
