/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google eUSB2 Repeater driver.
 * Copyright (C) 2024 Google LLC.
 */

#ifndef __GOOGLE_EUSB2_REPEATER_H
#define __GOOGLE_EUSB2_REPEATER_H

#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb.h>
#include <linux/usb/role.h>

/* TUSB2E11 Registers Map. */
#define TUSB2E11_GPIO0_CONFIG			0x0
#define TUSB2E11_GPIO1_CONFIG			0x40
#define TUSB2E11_UART_PORT1			0x50
#define TUSB2E11_CONFIG_PORT1			0x60
#define TUSB2E11_U_TX_ADJUST_PORT1		0x70
#define TUSB2E11_U_HS_TX_PRE_EMPHASIS_P1	0x71
#define TUSB2E11_U_RX_ADJUST_PORT1		0x72
#define TUSB2E11_U_DISCONNECT_SQUELCH_PORT1	0x73
#define TUSB2E11_E_HS_TX_PRE_EMPHASIS_P1	0x77
#define TUSB2E11_E_TX_ADJUST_PORT1		0x78
#define TUSB2E11_E_RX_ADJUST_PORT1		0x79
#define TUSB2E11_INT_STATUS_1			0xa3
#define TUSB2E11_INT_STATUS_2			0xa4
#define TUSB2E11_REV_ID				0xb0
#define TUSB2E11_GLOBAL_CONFIG			0xb2
#define TUSB2E11_INT_ENABLE_1			0xb3
#define TUSB2E11_INT_ENABLE_2			0xb4
#define TUSB2E11_BC_CONTROL			0xb6
#define TUSB2E11_BC_STATUS_1			0xb7

#define TUSB2E11_NUM_REGS			19
#define EUSB_RAP_NUM_REGS			8

/* TUSB2E11_GLOBAL_CONFIG */
#define REG_GLOBAL_CONFIG_DISABLE_P1		BIT(6)

#define EUSB_RAP_ATTR(_name, _reg)								\
static ssize_t _name##_show(struct device *dev, struct device_attribute *attr, char *buf)	\
{												\
	struct i2c_client *client = to_i2c_client(dev);						\
	struct google_eusb2_repeater_plat *chip = i2c_get_clientdata(client);			\
	int ret;										\
	unsigned int val;									\
												\
	ret = regmap_read(chip->pdata.regmap, _reg, &val);					\
	if (ret < 0)										\
		return -EIO;									\
												\
	return sysfs_emit(buf, "addr 0x%x: value: 0x%x\n", _reg, val);				\
}												\
												\
static ssize_t _name##_store(struct device *dev, struct device_attribute *attr,			\
			     const char *buf, size_t n)						\
{												\
	struct i2c_client *client = to_i2c_client(dev);						\
	struct google_eusb2_repeater_plat *chip = i2c_get_clientdata(client);			\
	u8 val;											\
												\
	if (kstrtou8(buf, 0, &val))								\
		return -EINVAL;									\
												\
	chip->pdata._name.value = val;							\
	chip->pdata._name.valid = true;							\
												\
	return n;										\
}												\
static DEVICE_ATTR_RW(_name)

#define VOLTAGE_VDD33_DEFAULT_UV	3300000
#define VOLTAGE_VDD18_DEFAULT_UV	1850000
#define VOLTAGE_VDD_RESET_DEFAULT_UV	1200000

struct google_eusb2_rap_regs {
	u8 value;
	bool valid;
};

enum eusb_power_state {
	EUSB_POWER_OFF,
	EUSB_POWER_ON,
};

struct google_eusb2_repeater_platdata {
	struct regmap *regmap;

	/* rap registers */
	struct google_eusb2_rap_regs uart_port1;
	struct google_eusb2_rap_regs u_tx_adjust_port1;
	struct google_eusb2_rap_regs u_hs_tx_pre_emphasis_p1;
	struct google_eusb2_rap_regs u_rx_adjust_port1;
	struct google_eusb2_rap_regs u_disconnect_squelch_port1;
	struct google_eusb2_rap_regs e_hs_tx_pre_emphasis_p1;
	struct google_eusb2_rap_regs e_tx_adjust_port1;
	struct google_eusb2_rap_regs e_rx_adjust_port1;

	/* 1: mode control by i2c, 0: power control by reset pin */
	bool disabled_mode;
};

struct google_eusb2_repeater_plat {
	struct device *dev;
	struct google_eusb2_repeater_platdata pdata;
	struct i2c_client *client;

	struct pinctrl *pinctrl;
	struct pinctrl_state *init_state;

	struct regulator *vdd33;
	struct regulator *vdd18;
	struct regulator *vdd_reset;

	struct usb_role_switch *role_sw;
	enum eusb_power_state curr_state;

	unsigned int vdd33_min_uv;
	unsigned int vdd33_max_uv;
	unsigned int vdd18_min_uv;
	unsigned int vdd18_max_uv;
	unsigned int vdd_reset_min_uv;
	unsigned int vdd_reset_max_uv;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *root;
#endif
};

#endif /* __GOOGLE_EUSB2_REPEATER_H */
