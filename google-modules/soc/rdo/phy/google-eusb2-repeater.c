// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google eUSB2 Repeater driver.
 * Copyright (C) 2024 Google LLC.
 */

#include "google-eusb2-repeater.h"

static const struct regmap_range teusb2e11_range[] = {
	regmap_reg_range(TUSB2E11_GPIO0_CONFIG, TUSB2E11_GPIO0_CONFIG),
	regmap_reg_range(TUSB2E11_GPIO1_CONFIG, TUSB2E11_GPIO1_CONFIG),
	regmap_reg_range(TUSB2E11_UART_PORT1, TUSB2E11_UART_PORT1),
	regmap_reg_range(TUSB2E11_CONFIG_PORT1, TUSB2E11_CONFIG_PORT1),
	regmap_reg_range(TUSB2E11_U_TX_ADJUST_PORT1, TUSB2E11_U_DISCONNECT_SQUELCH_PORT1),
	regmap_reg_range(TUSB2E11_E_HS_TX_PRE_EMPHASIS_P1, TUSB2E11_E_RX_ADJUST_PORT1),
	regmap_reg_range(TUSB2E11_INT_STATUS_1, TUSB2E11_INT_STATUS_2),
	regmap_reg_range(TUSB2E11_REV_ID, TUSB2E11_REV_ID),
	regmap_reg_range(TUSB2E11_GLOBAL_CONFIG, TUSB2E11_INT_ENABLE_2),
	regmap_reg_range(TUSB2E11_BC_CONTROL, TUSB2E11_BC_STATUS_1),
};

static const struct regmap_access_table teusb2e11_write_table = {
	.yes_ranges = teusb2e11_range,
	.n_yes_ranges = ARRAY_SIZE(teusb2e11_range),
};

static const struct regmap_config teusb2e11_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.wr_table = &teusb2e11_write_table,
};

static const u8 teusb2e11_regs[TUSB2E11_NUM_REGS] = {
	TUSB2E11_GPIO0_CONFIG,
	TUSB2E11_GPIO1_CONFIG,
	TUSB2E11_UART_PORT1,
	TUSB2E11_CONFIG_PORT1,
	TUSB2E11_U_TX_ADJUST_PORT1,
	TUSB2E11_U_HS_TX_PRE_EMPHASIS_P1,
	TUSB2E11_U_RX_ADJUST_PORT1,
	TUSB2E11_U_DISCONNECT_SQUELCH_PORT1,
	TUSB2E11_E_HS_TX_PRE_EMPHASIS_P1,
	TUSB2E11_E_TX_ADJUST_PORT1,
	TUSB2E11_E_RX_ADJUST_PORT1,
	TUSB2E11_INT_STATUS_1,
	TUSB2E11_INT_STATUS_2,
	TUSB2E11_REV_ID,
	TUSB2E11_GLOBAL_CONFIG,
	TUSB2E11_INT_ENABLE_1,
	TUSB2E11_INT_ENABLE_2,
	TUSB2E11_BC_CONTROL,
	TUSB2E11_BC_STATUS_1
};

static const u8 eusb_rap_regs[EUSB_RAP_NUM_REGS] = {
	TUSB2E11_UART_PORT1,
	TUSB2E11_U_TX_ADJUST_PORT1,
	TUSB2E11_U_HS_TX_PRE_EMPHASIS_P1,
	TUSB2E11_U_RX_ADJUST_PORT1,
	TUSB2E11_U_DISCONNECT_SQUELCH_PORT1,
	TUSB2E11_E_HS_TX_PRE_EMPHASIS_P1,
	TUSB2E11_E_TX_ADJUST_PORT1,
	TUSB2E11_E_RX_ADJUST_PORT1,
};

EUSB_RAP_ATTR(uart_port1, TUSB2E11_UART_PORT1);
EUSB_RAP_ATTR(u_tx_adjust_port1, TUSB2E11_U_TX_ADJUST_PORT1);
EUSB_RAP_ATTR(u_hs_tx_pre_emphasis_p1, TUSB2E11_U_HS_TX_PRE_EMPHASIS_P1);
EUSB_RAP_ATTR(u_rx_adjust_port1, TUSB2E11_U_RX_ADJUST_PORT1);
EUSB_RAP_ATTR(u_disconnect_squelch_port1, TUSB2E11_U_DISCONNECT_SQUELCH_PORT1);
EUSB_RAP_ATTR(e_hs_tx_pre_emphasis_p1, TUSB2E11_E_HS_TX_PRE_EMPHASIS_P1);
EUSB_RAP_ATTR(e_tx_adjust_port1, TUSB2E11_E_TX_ADJUST_PORT1);
EUSB_RAP_ATTR(e_rx_adjust_port1, TUSB2E11_E_RX_ADJUST_PORT1);

static ssize_t all_rap_registers_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_eusb2_repeater_plat *chip = i2c_get_clientdata(client);
	int ret, offset = 0, i;
	unsigned int val;

	for (i = 0; i < EUSB_RAP_NUM_REGS; i++) {
		ret = regmap_read(chip->pdata.regmap, eusb_rap_regs[i], &val);
		if (ret < 0)
			return -EIO;

		offset += sysfs_emit_at(buf, offset, "addr 0x%x: value: 0x%x\n",
					eusb_rap_regs[i], val);
	}

	return offset;
}
static DEVICE_ATTR_RO(all_rap_registers);

/* RAP (register access protocol) registers */
static struct attribute *rap_register_attrs[] = {
	&dev_attr_uart_port1.attr,
	&dev_attr_u_tx_adjust_port1.attr,
	&dev_attr_u_hs_tx_pre_emphasis_p1.attr,
	&dev_attr_u_rx_adjust_port1.attr,
	&dev_attr_u_disconnect_squelch_port1.attr,
	&dev_attr_e_hs_tx_pre_emphasis_p1.attr,
	&dev_attr_e_tx_adjust_port1.attr,
	&dev_attr_e_rx_adjust_port1.attr,
	&dev_attr_all_rap_registers.attr,
	NULL
};

static const struct attribute_group rap_register_group = {
	.name = "rap_register",
	.attrs = rap_register_attrs,
};

static const struct attribute_group *rap_register_groups[] = {
	&rap_register_group,
	NULL,
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
static ssize_t google_eusb2_dump_all_registers(struct file *file, char __user *userbuf,
					       size_t count, loff_t *ppos)
{
	struct google_eusb2_repeater_plat *chip = file->private_data;
	struct regmap *regmap = chip->pdata.regmap;
	int ret, offset = 0, i;
	char *tmp;
	unsigned int val;
	size_t msglen = TUSB2E11_NUM_REGS * 12;

	if (!regmap) {
		dev_err(chip->dev, "Failed to dump due to no regmap\n");
		return -EIO;
	}

	tmp = devm_kzalloc(chip->dev, msglen, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	for (i = 0; i < TUSB2E11_NUM_REGS; i++) {
		ret = regmap_read(regmap, teusb2e11_regs[i], &val);
		if (ret < 0) {
			offset = -EIO;
			break;
		}

		offset += scnprintf(tmp + offset, msglen - offset, "0x%x: 0x%x\n",
				    teusb2e11_regs[i], val);
	}

	if (offset > 0)
		offset = simple_read_from_buffer(userbuf, count, ppos, tmp, strlen(tmp));

	devm_kfree(chip->dev, tmp);

	return offset;
}

static const struct file_operations dump_all_registers_on_fops = {
	.read	= google_eusb2_dump_all_registers,
	.open	= simple_open,
};
#endif

static u8 rap_get_value(struct google_eusb2_repeater_plat *chip, u8 reg)
{
	switch (reg) {
	case TUSB2E11_UART_PORT1:
		return chip->pdata.uart_port1.value;
	case TUSB2E11_U_TX_ADJUST_PORT1:
		return chip->pdata.u_tx_adjust_port1.value;
	case TUSB2E11_U_HS_TX_PRE_EMPHASIS_P1:
		return chip->pdata.u_hs_tx_pre_emphasis_p1.value;
	case TUSB2E11_U_RX_ADJUST_PORT1:
		return chip->pdata.u_rx_adjust_port1.value;
	case TUSB2E11_U_DISCONNECT_SQUELCH_PORT1:
		return chip->pdata.u_disconnect_squelch_port1.value;
	case TUSB2E11_E_HS_TX_PRE_EMPHASIS_P1:
		return chip->pdata.e_hs_tx_pre_emphasis_p1.value;
	case TUSB2E11_E_TX_ADJUST_PORT1:
		return chip->pdata.e_tx_adjust_port1.value;
	case TUSB2E11_E_RX_ADJUST_PORT1:
		return chip->pdata.e_rx_adjust_port1.value;
	default:
		//shouldn't use non-rap regs
		return 0;
	}
}

static bool rap_check_valid(struct google_eusb2_repeater_plat *chip, u8 reg)
{
	switch (reg) {
	case TUSB2E11_UART_PORT1:
		return chip->pdata.uart_port1.valid;
	case TUSB2E11_U_TX_ADJUST_PORT1:
		return chip->pdata.u_tx_adjust_port1.valid;
	case TUSB2E11_U_HS_TX_PRE_EMPHASIS_P1:
		return chip->pdata.u_hs_tx_pre_emphasis_p1.valid;
	case TUSB2E11_U_RX_ADJUST_PORT1:
		return chip->pdata.u_rx_adjust_port1.valid;
	case TUSB2E11_U_DISCONNECT_SQUELCH_PORT1:
		return chip->pdata.u_disconnect_squelch_port1.valid;
	case TUSB2E11_E_HS_TX_PRE_EMPHASIS_P1:
		return chip->pdata.e_hs_tx_pre_emphasis_p1.valid;
	case TUSB2E11_E_TX_ADJUST_PORT1:
		return chip->pdata.e_tx_adjust_port1.valid;
	case TUSB2E11_E_RX_ADJUST_PORT1:
		return chip->pdata.e_rx_adjust_port1.valid;
	default:
		//shouldn't use non-rap regs
		return false;
	}
}

static int google_eusb2_repeater_power_ctrl(struct google_eusb2_repeater_plat *chip, bool on)
{
	int ret;

	if (on) {
		ret = regulator_enable(chip->vdd33);
		if (ret) {
			dev_err(chip->dev, "failed to enable vdd33, err:%d\n", ret);
			return ret;
		}

		ret = regulator_enable(chip->vdd18);
		if (ret) {
			regulator_disable(chip->vdd33);
			dev_err(chip->dev, "failed to enable vdd18, err:%d\n", ret);
			return ret;
		}

		ret = regulator_enable(chip->vdd_reset);
		if (ret) {
			regulator_disable(chip->vdd33);
			regulator_disable(chip->vdd18);
			dev_err(chip->dev, "failed to enable vdd_reset, err:%d\n", ret);
			return ret;
		}
	} else {
		regulator_disable(chip->vdd33);
		regulator_disable(chip->vdd18);
		regulator_disable(chip->vdd_reset);
	}

	dev_info(chip->dev, "%s eUSB regulators\n", on ? "Enabling" : "Disabling");

	ret = on ? regulator_set_voltage(chip->vdd33, VOLTAGE_VDD33_DEFAULT_UV,
					 VOLTAGE_VDD33_DEFAULT_UV) :
		   regulator_set_voltage(chip->vdd33, chip->vdd33_min_uv, chip->vdd33_max_uv);

	if (ret)
		dev_info(chip->dev, "fail to set ldo3v3 voltage, ret = %d\n", ret);

	ret = on ? regulator_set_voltage(chip->vdd18, VOLTAGE_VDD18_DEFAULT_UV,
					 VOLTAGE_VDD18_DEFAULT_UV) :
		   regulator_set_voltage(chip->vdd18, chip->vdd18_min_uv, chip->vdd18_max_uv);
	if (ret)
		dev_info(chip->dev, "fail to set ldo1v8 voltage, ret = %d\n", ret);

	ret = on ? regulator_set_voltage(chip->vdd_reset, VOLTAGE_VDD_RESET_DEFAULT_UV,
					 VOLTAGE_VDD_RESET_DEFAULT_UV) :
		   regulator_set_voltage(chip->vdd_reset, chip->vdd_reset_min_uv,
					 chip->vdd_reset_max_uv);
	if (ret)
		dev_info(chip->dev, "fail to set ldo_reset voltage, ret = %d\n", ret);


	// ramp time for vdd33 and vdd18
	if (on)
		mdelay(2);

	return 0;
}

static int google_eusb2_repeater_mode_ctrl(struct google_eusb2_repeater_plat *chip, bool status)
{
	struct regmap *regmap = chip->pdata.regmap;
	int ret;

	ret = regmap_update_bits(regmap, TUSB2E11_GLOBAL_CONFIG,
				 REG_GLOBAL_CONFIG_DISABLE_P1,
				 status ? 0 : REG_GLOBAL_CONFIG_DISABLE_P1);

	dev_info(chip->dev, "%s Repeater mode %s, ret = %d\n", status ? "Enter" : "Exit",
			!ret ? "success" : "failed", ret);

	return ret;
}

static int google_eusb2_repeater_connect(struct google_eusb2_repeater_plat *chip)
{
	int ret, i;

	ret = google_eusb2_repeater_power_ctrl(chip, true);
	if (ret)
		return ret;

	// time for repeater to be ready to access RAP and i2c requests
	// Since resetb pin was set to input no-pull and power source is
	// always-on, there is no need to control resetb pin.
	mdelay(3);

	if (chip->pdata.disabled_mode) {
		ret = google_eusb2_repeater_mode_ctrl(chip, true);
		if (ret < 0)
			return ret;
	}

	// write RAP registers for phy tune
	for (i = 0; i < EUSB_RAP_NUM_REGS; i++) {
		if (rap_check_valid(chip, eusb_rap_regs[i])) {
			ret = regmap_write(chip->pdata.regmap, eusb_rap_regs[i],
					   rap_get_value(chip, eusb_rap_regs[i]));
			if (ret < 0) {
				dev_err(chip->dev, "failed to adjust eUSB phy, ret=%d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static void google_eusb2_repeater_disconnect(struct google_eusb2_repeater_plat *chip)
{
	if (chip->pdata.disabled_mode)
		google_eusb2_repeater_mode_ctrl(chip, false);

	google_eusb2_repeater_power_ctrl(chip, false);

	return;
}

static int google_eusb2_repeater_role_switch_set(struct usb_role_switch *sw, enum usb_role role)
{
	struct google_eusb2_repeater_plat *chip = usb_role_switch_get_drvdata(sw);
	int ret = 0;
	enum eusb_power_state next_state;

	switch (role) {
	case USB_ROLE_DEVICE:
	case USB_ROLE_HOST:
		next_state = EUSB_POWER_ON;
		break;
	case USB_ROLE_NONE:
	default:
		next_state = EUSB_POWER_OFF;
	}

	if (chip->curr_state == next_state) {
		dev_info(chip->dev, "%s: using the same eusb-power-role: %s\n", __func__,
			 chip->curr_state ? "on" : "off");
		return ret;
	}

	if (next_state == EUSB_POWER_ON)
		ret = google_eusb2_repeater_connect(chip);
	else
		google_eusb2_repeater_disconnect(chip);

	chip->curr_state = next_state;

	return ret;

}

static void google_eusb2_repeater_set_default_role(struct google_eusb2_repeater_plat *chip)
{
	enum usb_role role;
	const char *str;
	int ret;

	ret = device_property_read_string(chip->dev, "role-switch-default-mode", &str);
	if (ret < 0)
		return;

	if (strcmp(str, "host") == 0)
		role = USB_ROLE_HOST;
	else if (strcmp(str, "device") == 0)
		role = USB_ROLE_DEVICE;
	else
		role = USB_ROLE_NONE;

	dev_info(chip->dev, "%s: %s\n", __func__, usb_role_string(role));
	google_eusb2_repeater_role_switch_set(chip->role_sw, role);
}

static int google_eusb2_repeater_setup_role_switch(struct google_eusb2_repeater_plat *chip)
{
	struct usb_role_switch_desc eusb_role_switch = {0};

	eusb_role_switch.fwnode = dev_fwnode(chip->dev);
	eusb_role_switch.set = google_eusb2_repeater_role_switch_set;
	eusb_role_switch.driver_data = chip;
	eusb_role_switch.allow_userspace_control = true;
	chip->role_sw = usb_role_switch_register(chip->dev, &eusb_role_switch);
	if (IS_ERR_OR_NULL(chip->role_sw))
		return PTR_ERR_OR_ZERO(chip->role_sw);

	google_eusb2_repeater_set_default_role(chip);
	return 0;
}

static int google_eusb2_repeater_setup_regulator_volt(struct google_eusb2_repeater_plat *chip,
		const char *id, unsigned int *min_uv, unsigned int *max_uv)
{
	struct device_node *dn, *regulator_dn;
	u32 regulator_handle;
	int ret = -ENOENT;

	dn = dev_of_node(chip->dev);

	if (!of_property_read_u32(dn, id, &regulator_handle)) {
		regulator_dn = of_find_node_by_phandle(regulator_handle);
		if (!IS_ERR_OR_NULL(regulator_dn)) {
			ret = of_property_read_u32(regulator_dn, "regulator-min-microvolt",
						   min_uv);
			if (ret) {
				dev_err(chip->dev, "failed to read %s regulator-min-microvolt\n",
						id);
				return ret;
			}

			ret = of_property_read_u32(regulator_dn, "regulator-max-microvolt",
						   max_uv);
			if (ret) {
				dev_err(chip->dev, "failed to read %s regulator-max-microvolt\n",
						id);
				return ret;
			}
		}
	}

	return ret;
}

static int google_eusb2_repeater_probe(struct i2c_client *client)
{
	int ret;
	struct google_eusb2_repeater_plat *chip;
	struct device_node *dn;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	dev_set_name(&client->dev, "i2c-%s-eusb2-repeater", dev_name(&client->dev));
	chip->pdata.regmap = devm_regmap_init_i2c(client, &teusb2e11_regmap_config);

	if (IS_ERR(chip->pdata.regmap)) {
		dev_err(&client->dev, "Regmap init failed\n");
		return PTR_ERR(chip->pdata.regmap);
	}

	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);

	dn = dev_of_node(&client->dev);
	if (!dn) {
		dev_err(&client->dev, "of node not found\n");
		return -EINVAL;
	}

	chip->vdd33 = devm_regulator_get(&client->dev, "vdd33");
	if (IS_ERR(chip->vdd33)) {
		dev_err(&client->dev, "get vdd33 regulator failed: %ld\n", PTR_ERR(chip->vdd33));
		return PTR_ERR(chip->vdd33);
	}

	ret = google_eusb2_repeater_setup_regulator_volt(chip, "vdd33-supply", &chip->vdd33_min_uv,
			&chip->vdd33_max_uv);
	if (ret)
		goto ldo3v3_put;

	chip->vdd18 = devm_regulator_get(&client->dev, "vdd18");
	if (IS_ERR(chip->vdd18)) {
		dev_err(&client->dev, "get vdd18 regulator failed: %ld\n", PTR_ERR(chip->vdd18));
		ret = PTR_ERR(chip->vdd18);
		goto ldo3v3_put;
	}

	ret = google_eusb2_repeater_setup_regulator_volt(chip, "vdd18-supply", &chip->vdd18_min_uv,
			&chip->vdd18_max_uv);
	if (ret)
		goto ldo1v8_put;


	chip->vdd_reset = devm_regulator_get(&client->dev, "vdd_reset");
	if (IS_ERR(chip->vdd_reset)) {
		dev_err(&client->dev, "get vdd_reset regulator failed: %ld\n",
			PTR_ERR(chip->vdd_reset));
		ret = PTR_ERR(chip->vdd_reset);
		goto ldo1v8_put;
	}

	ret = google_eusb2_repeater_setup_regulator_volt(chip, "vdd_reset-supply",
			&chip->vdd_reset_min_uv, &chip->vdd_reset_max_uv);
	if (ret)
		goto ldo_vdd_reset_put;

	chip->pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(chip->pinctrl)) {
		dev_err(&client->dev, "failed to get pinctrl: %ld\n", PTR_ERR(chip->pinctrl));
		ret = PTR_ERR(chip->pinctrl);
		goto ldo_vdd_reset_put;
	}

	chip->init_state = pinctrl_lookup_state(chip->pinctrl, PINCTRL_STATE_DEFAULT);
	if (IS_ERR(chip->init_state)) {
		dev_err(&client->dev, "default state not defined: %ld\n",
			PTR_ERR(chip->init_state));
		ret = PTR_ERR(chip->init_state);
		goto ldo_vdd_reset_put;
	}

	ret = pinctrl_select_state(chip->pinctrl, chip->init_state);
	if (ret) {
		dev_err(&client->dev, "failed to select pinctrl state, err:%d\n", ret);
		return ret;
	}

	chip->curr_state = EUSB_POWER_OFF;

	ret = google_eusb2_repeater_setup_role_switch(chip);
	if (ret) {
		dev_err(&client->dev, "failed to setup role switch, err:%d\n", ret);
		goto ldo_vdd_reset_put;
	}

	ret = of_property_read_u8(dn, "uart_port1", &chip->pdata.uart_port1.value);
	chip->pdata.uart_port1.valid = !ret;

	ret = of_property_read_u8(dn, "u_tx_adjust_port1", &chip->pdata.u_tx_adjust_port1.value);
	chip->pdata.u_tx_adjust_port1.valid = !ret;

	ret = of_property_read_u8(dn, "u_hs_tx_pre_emphasis_p1",
				  &chip->pdata.u_hs_tx_pre_emphasis_p1.value);
	chip->pdata.u_hs_tx_pre_emphasis_p1.valid = !ret;

	ret = of_property_read_u8(dn, "u_rx_adjust_port1", &chip->pdata.u_rx_adjust_port1.value);
	chip->pdata.u_rx_adjust_port1.valid = !ret;

	ret = of_property_read_u8(dn, "u_disconnect_squelch_port1",
				  &chip->pdata.u_disconnect_squelch_port1.value);
	chip->pdata.u_disconnect_squelch_port1.valid = !ret;

	ret = of_property_read_u8(dn, "e_hs_tx_pre_emphasis_p1",
				  &chip->pdata.e_hs_tx_pre_emphasis_p1.value);
	chip->pdata.e_hs_tx_pre_emphasis_p1.valid = !ret;

	ret = of_property_read_u8(dn, "e_tx_adjust_port1", &chip->pdata.e_tx_adjust_port1.value);
	chip->pdata.e_tx_adjust_port1.valid = !ret;

	ret = of_property_read_u8(dn, "e_rx_adjust_port1", &chip->pdata.e_rx_adjust_port1.value);
	chip->pdata.e_rx_adjust_port1.valid = !ret;

	/*
	 * disabled mode is a lower power mode with LDOs turned on.
	 * In the disabled mode, USB data won't work but i2c access
	 * still works.
	 */
	chip->pdata.disabled_mode = of_property_read_bool(dn, "i2c-disabled-mode");

#if IS_ENABLED(CONFIG_DEBUG_FS)
	chip->root = debugfs_create_dir(dev_name(&client->dev), usb_debug_root);
	if (IS_ERR(chip->root)) {
		dev_err(&client->dev, "debugfs dentry failed: %ld", PTR_ERR(chip->root));
	} else {
		debugfs_create_file("registers", 0444, chip->root, chip,
				    &dump_all_registers_on_fops);
	}
#endif

	dev_info(chip->dev, "%s complete\n", __func__);
	return 0;

ldo_vdd_reset_put:
	devm_regulator_put(chip->vdd_reset);
ldo1v8_put:
	devm_regulator_put(chip->vdd18);
ldo3v3_put:
	devm_regulator_put(chip->vdd33);
	return ret;
}

static void google_eusb2_repeater_remove(struct i2c_client *client)
{
	struct google_eusb2_repeater_plat *chip = i2c_get_clientdata(client);

	usb_role_switch_unregister(chip->role_sw);
#if IS_ENABLED(CONFIG_DEBUG_FS)
	debugfs_lookup_and_remove(dev_name(&client->dev), usb_debug_root);
#endif
}

static const struct i2c_device_id google_eusb2_repeater_id_table[] = {
	{ "goog-eusb2-repeater", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, google_eusb2_repeater_id_table);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id google_eusb2_repeater_of_match[] = {
	{ .compatible = "goog-eusb2-repeater", },
	{},
};
MODULE_DEVICE_TABLE(of, google_eusb2_repeater_of_match);
#else
#define google_eusb2_repeater_of_match NULL
#endif

static struct i2c_driver google_eusb2_repeater_driver = {
	.driver = {
		.name = "goog-eusb2-repeater",
		.of_match_table = google_eusb2_repeater_of_match,
		.dev_groups = rap_register_groups,
	},
	.probe = google_eusb2_repeater_probe,
	.remove = google_eusb2_repeater_remove,
	.id_table = google_eusb2_repeater_id_table,
};
module_i2c_driver(google_eusb2_repeater_driver);

MODULE_AUTHOR("Ray Chi <raychi@google.com>");
MODULE_DESCRIPTION("Google eUSB2 Repeater Driver");
MODULE_LICENSE("GPL");

