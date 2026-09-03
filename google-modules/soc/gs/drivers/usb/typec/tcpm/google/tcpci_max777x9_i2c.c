// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, Google LLC
 *
 * MAX777x9 on I2C bus driver
 */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>

#include "google_tcpci_shim.h"
#include "tcpci_max77759.h"
#include "tcpci_max77759_vendor_reg.h"

#define MAX777x9_BUCK_BOOST_I2C_SID 0x69

static const struct regmap_range max77759_tcpci_range[] = {
	regmap_reg_range(MAX77759_MIN_ADDR, MAX77759_MAX_ADDR)
};

const struct regmap_access_table max77759_tcpci_write_table = {
	.yes_ranges = max77759_tcpci_range,
	.n_yes_ranges = ARRAY_SIZE(max77759_tcpci_range),
};

static const struct regmap_config max77759_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX77759_MAX_ADDR,
	.wr_table = &max77759_tcpci_write_table,
};

static int max777x9_i2c_direct_otg_en(struct max77759_plat *chip, bool en)
{
	struct i2c_client *i2c_client_dev = chip->client;
	u8 buffer[2];

	struct i2c_msg msgs[] = {
		{
			.addr = MAX777x9_BUCK_BOOST_I2C_SID,
			.flags = i2c_client_dev->flags & I2C_M_TEN,
			.len = 2,
			.buf = buffer,
		},
	};

	buffer[0] = (chip->product_id == MAX77779_PRODUCT_ID) ? MAX77779_BUCK_BOOST_OP :
								MAX77759_BUCK_BOOST_OP;
	buffer[1] = en ? MAX777x9_BUCK_BOOST_SOURCE : MAX777x9_BUCK_BOOST_OFF;

	return i2c_transfer(i2c_client_dev->adapter, msgs, 1);
}

static int max777x9_init_alert(struct max77759_plat *chip,
			       struct i2c_client *client)
{
	struct gpio_desc *irq_gpio;

	irq_gpio = devm_gpiod_get_optional(&client->dev, "usbpd,usbpd_int", GPIOD_ASIS);
	client->irq = gpiod_to_irq(irq_gpio);
	if (client->irq < 0)
		return -ENODEV;

	return max77759_register_irq(client->irq, (IRQF_TRIGGER_LOW | IRQF_ONESHOT), chip);
}

static int max777x9_i2c_rx(struct google_shim_tcpci *tcpci, u8 *buf, size_t pd_msg_size)
{
	struct max77759_plat *chip = tdata_to_max77759(tcpci->data);
	int ret;

	ret = regmap_raw_read(tcpci->data->regmap, TCPC_RX_BYTE_CNT, buf,
			      pd_msg_size + TCPC_RECEIVE_BUFFER_METADATA_SIZE);
	if (ret < 0)
		logbuffer_log(chip->log, "Error: TCPC_RX_BYTE_CNT read failed: %d", ret);
	return ret;
}

static int max777x9_i2c_probe(struct i2c_client *client)
{
	int ret;
	struct regmap *regmap;
	struct max777x9_desc desc = {};

	client->dev.init_name = "i2c-max77759tcpc";
	regmap = devm_regmap_init_i2c(client, &max77759_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Regmap init failed");
		return PTR_ERR(regmap);
	}

	desc.dev = &client->dev;
	desc.rmap = regmap;
	desc.client = (void *)client;
	desc.rx = max777x9_i2c_rx;

	ret = max77759_register(&desc, max777x9_i2c_direct_otg_en);
	if (ret)
		return ret;

	if (!desc.plat) {
		dev_err(&client->dev, "Expected non-NULL desc.plat value");
		return -EINVAL;
	}

	ret = max777x9_init_alert(desc.plat, client);
	if (ret < 0) {
		dev_err(&client->dev, "init alert failed");
		max77759_unregister(desc.plat);
		return ret;
	}

	device_init_wakeup(&client->dev, true);

	return ret;
}

static void max777x9_i2c_remove(struct i2c_client *client)
{
	struct max77759_plat *chip = i2c_get_clientdata(client);

	max77759_unregister(chip);
}

static void max777x9_i2c_shutdown(struct i2c_client *client)
{
	struct max77759_plat *chip = i2c_get_clientdata(client);

	max77759_shutdown(chip);
}

static const struct i2c_device_id max777x9_id[] = {
	{ "max77759tcpc", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max777x9_id);

#ifdef CONFIG_OF
static const struct of_device_id max777x9_of_match[] = {
	{ .compatible = "max77759tcpc", },
	{},
};
MODULE_DEVICE_TABLE(of, max777x9_of_match);
#endif

static struct i2c_driver max777x9_i2c_driver = {
	.driver = {
		.name = "max77759tcpc",
		.of_match_table = of_match_ptr(max777x9_of_match),
	},
	.probe = max777x9_i2c_probe,
	.remove = max777x9_i2c_remove,
	.id_table = max777x9_id,
	.shutdown = max777x9_i2c_shutdown,
};

static int __init max777x9_i2c_driver_init(void)
{
	return i2c_add_driver(&max777x9_i2c_driver);
}
module_init(max777x9_i2c_driver_init);

static void __exit max777x9_i2c_driver_exit(void)
{
	i2c_del_driver(&max777x9_i2c_driver);
}
module_exit(max777x9_i2c_driver_exit);

MODULE_DESCRIPTION("MAX777x9 TCPCI I2C Driver");
MODULE_LICENSE("GPL");
