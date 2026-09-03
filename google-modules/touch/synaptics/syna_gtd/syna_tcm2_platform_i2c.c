// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics TouchCom touchscreen driver
 *
 * Copyright (C) 2017-2024 Synaptics Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

/**
 * @file syna_tcm2_platform_i2c.c
 *
 * This file is the reference code of platform I2C bus module being used to
 * communicate with Synaptics TouchCom device over I2C.
 */

#include <linux/i2c.h>

#include "syna_tcm2.h"
#include "syna_tcm2_platform.h"

#define I2C_MODULE_NAME "synaptics_tcm_i2c"

#define XFER_ATTEMPTS 5

static struct platform_device *p_device;


/**
 * @brief  Request and return the device pointer for managed
 *
 * @param
 *     void.
 *
 * @return
 *     a device pointer allocated previously
 */
#if defined(DEV_MANAGED_API)
struct device *syna_request_managed_device(void)
{
	if (!p_device)
		return NULL;

	return p_device->dev.parent;
}
#endif


/**
 * @brief  Release the GPIO.
 *
 * @param
 *     [ in] gpio:   the target gpio
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_put_gpio(int gpio)
{
	/* release gpios */
	if (gpio <= 0) {
		LOGE("Invalid gpio pin\n");
		return -EINVAL;
	}

	gpio_free(gpio);
	LOGD("GPIO-%d released\n", gpio);

	return 0;
}
/**
 * @brief  Request a gpio and perform the requested setup
 *
 * @param
 *    [ in] gpio:   the target gpio
 *    [ in] dir:    default direction of gpio
 *    [ in] state:  default state of gpio
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_get_gpio(int gpio, int dir, int state, char *label)
{
	int retval;

	if (gpio < 0) {
		LOGE("Invalid gpio pin\n");
		return -EINVAL;
	}

	retval = scnprintf(label, 16, "tcm_gpio_%d\n", gpio);
	if (retval < 0) {
		LOGE("Fail to set GPIO label\n");
		return retval;
	}

	retval = gpio_request(gpio, label);
	if (retval < 0) {
		LOGE("Fail to request GPIO %d\n", gpio);
		return retval;
	}

	if (dir == 0)
		retval = gpio_direction_input(gpio);
	else
		retval = gpio_direction_output(gpio, state);

	if (retval < 0) {
		LOGE("Fail to set GPIO %d direction\n", gpio);
		return retval;
	}

	LOGD("GPIO-%d requested\n", gpio);

	return 0;
}
/**
 * @brief  Release the regulator.
 *
 * @param
 *    [ in] reg_dev: regulator to release
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_put_regulator(struct regulator *reg_dev)
{
	if (!reg_dev) {
		LOGE("Invalid regulator device\n");
		return -EINVAL;
	}
#ifdef DEV_MANAGED_API
	devm_regulator_put(reg_dev);
#else /* Legacy API */
	regulator_put(reg_dev);
#endif

	return 0;
}
/**
 * @brief  Requested a regulator according to the name.
 *
 * @param
 *    [ in] name: name of requested regulator
 *
 * @return
 *    on success, return the pointer to the requested regulator; otherwise, on error.
 */
static struct regulator *syna_i2c_get_regulator(const char *name)
{
	struct regulator *reg_dev = NULL;
	struct device *dev = p_device->dev.parent;

	if (name != NULL && *name != 0) {
#ifdef DEV_MANAGED_API
		reg_dev = devm_regulator_get(dev, name);
#else /* Legacy API */
		reg_dev = regulator_get(dev, name);
#endif
		if (IS_ERR(reg_dev)) {
			LOGW("Regulator is not ready\n");
			return (struct regulator *)PTR_ERR(reg_dev);
		}
	}

	return reg_dev;
}
/**
 * @brief  Parse the touch test limit property name and limit array from the device tree
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *    [ in] dev: device node
 *    [ in] index: panel index
 *
 * @return
 *    void.
 */
static void syna_parse_test_limit_name(struct syna_hw_interface *hw_if,
	struct device_node *np, int index)
{
	int retval;
	const char *name;

	/* pt05 */
	retval = of_property_read_string_index(np, "synaptics,pt05_high_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt05_high_limit_name, name, sizeof(hw_if->pt05_high_limit_name));
	} else {
		strncpy(hw_if->pt05_high_limit_name, "synaptics,pt05_high",
				sizeof(hw_if->pt05_high_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt05_low_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt05_low_limit_name, name, sizeof(hw_if->pt05_low_limit_name));
	} else {
		strncpy(hw_if->pt05_low_limit_name, "synaptics,pt05_low",
				sizeof(hw_if->pt05_low_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt05_gap_x_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt05_gap_x_limit_name, name, sizeof(hw_if->pt05_gap_x_limit_name));
	} else {
		strncpy(hw_if->pt05_gap_x_limit_name, "synaptics,pt05_gap_x",
				sizeof(hw_if->pt05_gap_x_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt05_gap_y_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt05_gap_y_limit_name, name, sizeof(hw_if->pt05_gap_y_limit_name));
	} else {
		strncpy(hw_if->pt05_gap_y_limit_name, "synaptics,pt05_gap_y",
				sizeof(hw_if->pt05_gap_y_limit_name));
	}

	/* pt0a */
	retval = of_property_read_string_index(np, "synaptics,pt0a_high_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt0a_high_limit_name, name, sizeof(hw_if->pt0a_high_limit_name));
	} else {
		strncpy(hw_if->pt0a_high_limit_name, "synaptics,pt0a_high",
				sizeof(hw_if->pt0a_high_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt0a_low_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt0a_low_limit_name, name, sizeof(hw_if->pt0a_low_limit_name));
	} else {
		strncpy(hw_if->pt0a_low_limit_name, "synaptics,pt0a_low",
				sizeof(hw_if->pt0a_low_limit_name));
	}

	/* pt10 */
	retval = of_property_read_string_index(np, "synaptics,pt10_high_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt10_high_limit_name, name, sizeof(hw_if->pt10_high_limit_name));
	} else {
		strncpy(hw_if->pt10_high_limit_name, "synaptics,pt10_high",
				sizeof(hw_if->pt10_high_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt10_low_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt10_low_limit_name, name, sizeof(hw_if->pt10_low_limit_name));
	} else {
		strncpy(hw_if->pt10_low_limit_name, "synaptics,pt10_low",
				sizeof(hw_if->pt10_low_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt10_gap_x_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt10_gap_x_limit_name, name, sizeof(hw_if->pt10_gap_x_limit_name));
	} else {
		strncpy(hw_if->pt10_gap_x_limit_name, "synaptics,pt10_gap_x",
				sizeof(hw_if->pt10_gap_x_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt10_gap_y_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt10_gap_y_limit_name, name, sizeof(hw_if->pt10_gap_y_limit_name));
	} else {
		strncpy(hw_if->pt10_gap_y_limit_name, "synaptics,pt10_gap_y",
				sizeof(hw_if->pt10_gap_y_limit_name));
	}

	/* pt11 */
	retval = of_property_read_string_index(np, "synaptics,pt11_high_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt11_high_limit_name, name, sizeof(hw_if->pt11_high_limit_name));
	} else {
		strncpy(hw_if->pt11_high_limit_name, "synaptics,pt11_high",
				sizeof(hw_if->pt11_high_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt11_low_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt11_low_limit_name, name, sizeof(hw_if->pt11_low_limit_name));
	} else {
		strncpy(hw_if->pt11_low_limit_name, "synaptics,pt11_low",
				sizeof(hw_if->pt11_low_limit_name));
	}

	/* pt12 */
	retval = of_property_read_string_index(np, "synaptics,pt12_high_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt12_high_limit_name, name, sizeof(hw_if->pt12_high_limit_name));
	} else {
		strncpy(hw_if->pt12_high_limit_name, "synaptics,pt12_high",
				sizeof(hw_if->pt12_high_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt12_low_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt12_low_limit_name, name, sizeof(hw_if->pt12_low_limit_name));
	} else {
		strncpy(hw_if->pt12_low_limit_name, "synaptics,pt12_low",
				sizeof(hw_if->pt12_low_limit_name));
	}

	/* pt16 */
	retval = of_property_read_string_index(np, "synaptics,pt16_high_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt16_high_limit_name, name, sizeof(hw_if->pt16_high_limit_name));
	} else {
		strncpy(hw_if->pt16_high_limit_name, "synaptics,pt16_high",
				sizeof(hw_if->pt16_high_limit_name));
	}

	retval = of_property_read_string_index(np, "synaptics,pt16_low_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt16_low_limit_name, name, sizeof(hw_if->pt16_low_limit_name));
	} else {
		strncpy(hw_if->pt16_low_limit_name, "synaptics,pt16_low",
				sizeof(hw_if->pt16_low_limit_name));
	}

	/*pt tag moisture*/
	retval = of_property_read_string_index(np, "synaptics,pt_tag_moisture_name", index, &name);
	if (retval == 0) {
		strncpy(hw_if->pt_tag_moisture_limit_name, name,
				sizeof(hw_if->pt_tag_moisture_limit_name));
	} else {
		strncpy(hw_if->pt_tag_moisture_limit_name, "synaptics,pt_tag_moisture",
				sizeof(hw_if->pt_tag_moisture_limit_name));
	}
}

static inline void google_parse_panel_setting(struct syna_hw_interface *hw_if,
	struct device_node *np, int setting_id)
{
	const char *name;
	u32 value;
	int retval = 0;

	if (hw_if == NULL || np == NULL)
		return;

	retval = of_property_read_string_index(np,
			"synaptics,firmware_names",
			setting_id, &name);
	if (retval < 0) {
		strncpy(hw_if->fw_name, FW_IMAGE_NAME,
				sizeof(hw_if->fw_name));
	} else {
		strncpy(hw_if->fw_name, name, sizeof(hw_if->fw_name));
	}
	LOGD("Firmware name %s from device tree", hw_if->fw_name);

	retval = of_property_read_u32_index(np, "synaptics,test_algo",
			setting_id, &value);
	if (retval < 0)
		hw_if->test_algo = 0;
	else
		hw_if->test_algo = value;
}

#if defined(ENABLE_DISP_NOTIFIER) && defined(CONFIG_DRM_BRIDGE)
static inline int google_parse_panel_setting_id(struct syna_hw_interface *hw_if, struct device_node *np)
{
	int index = 0;
	int retval = 0;
	struct of_phandle_args panelmap;
	struct drm_panel *panel = NULL;

	if (hw_if == NULL || np == NULL)
		return -EINVAL;

	if (!of_property_read_bool(np, "synaptics,panel_map")) {
		strncpy(hw_if->fw_name, FW_IMAGE_NAME, sizeof(hw_if->fw_name));
		return 0;
	};

	for (index = 0 ;; index++) {
		retval = of_parse_phandle_with_fixed_args(np,
				"synaptics,panel_map",
				1,
				index,
				&panelmap);
		if (retval)
			return retval;

		panel = of_drm_find_panel(panelmap.np);
		of_node_put(panelmap.np);
		if (IS_ERR_OR_NULL(panel)) {
			continue;
		}
		return panelmap.args[0];
	}
}
#endif
/**
 * @brief  Parse and obtain board specific data from the device tree source file.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *    [ in] dev: device model
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
#ifdef CONFIG_OF
static int syna_i2c_parse_dt(struct syna_hw_interface *hw_if,
		struct device *dev)
{
	int retval;
	int setting_id = 0;
	u32 value;
	struct property *prop;
	struct device_node *np = dev->of_node;
	struct syna_hw_attn_data *attn = &hw_if->bdata_attn;
	struct syna_hw_pwr_data *pwr = &hw_if->bdata_pwr;
	struct syna_hw_rst_data *rst = &hw_if->bdata_rst;
	struct syna_hw_bus_data *bus = &hw_if->bdata_io;
	struct product_specific *product = &hw_if->product;
	int temp_value[5] = { 0 };

#if defined(ENABLE_DISP_NOTIFIER) && defined(CONFIG_DRM_BRIDGE)
	setting_id = google_parse_panel_setting_id(hw_if, np);
	if (setting_id < 0)
		return -EPROBE_DEFER;

	google_parse_panel_setting(hw_if, np, setting_id);
#endif
	syna_parse_test_limit_name(hw_if, np, setting_id);

	attn->irq_gpio = -1;
	prop = of_find_property(np, "synaptics,irq-gpio", NULL);
	if (prop && prop->length)
		attn->irq_gpio = of_get_named_gpio_flags(np, "synaptics,irq-gpio", 0,
				(enum of_gpio_flags *)&attn->irq_flags);

	attn->irq_on_state = 0;
	prop = of_find_property(np, "synaptics,irq-on-state", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,irq-on-state", &attn->irq_on_state);


	pwr->power_on_state = 1;
	prop = of_find_property(np, "synaptics,power-on-state", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,power-on-state", &pwr->power_on_state);

	pwr->power_delay_ms = 0;
	prop = of_find_property(np, "synaptics,power-delay-ms", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,power-delay-ms", &pwr->power_delay_ms);

	pwr->vdd.control = 0;
	prop = of_find_property(np, "synaptics,vdd-control", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,vdd-control", &pwr->vdd.control);

	pwr->vdd.regulator_name = NULL;
	prop = of_find_property(np, "synaptics,vdd-name", NULL);
	if (prop && prop->length)
		of_property_read_string(np, "synaptics,vdd-name", &pwr->vdd.regulator_name);

	pwr->vdd.gpio = -1;
	prop = of_find_property(np, "synaptics,vdd-gpio", NULL);
	if (prop && prop->length)
		pwr->vdd.gpio = of_get_named_gpio_flags(np, "synaptics,vdd-gpio", 0, NULL);

	pwr->vdd.power_on_delay_ms = 0;
	prop = of_find_property(np, "synaptics,vdd-power-on-delay-ms", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,vdd-power-on-delay-ms", &pwr->vdd.power_on_delay_ms);

	pwr->vdd.power_off_delay_ms = 0;
	prop = of_find_property(np, "synaptics,vdd-power-off-delay-ms", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,vdd-power-off-delay-ms", &pwr->vdd.power_off_delay_ms);

	pwr->vio.control = 0;
	prop = of_find_property(np, "synaptics,vio-control", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,vio-control", &pwr->vio.control);

	pwr->vio.regulator_name = NULL;
	prop = of_find_property(np, "synaptics,vio-name", NULL);
	if (prop && prop->length)
		of_property_read_string(np, "synaptics,vio-name", &pwr->vio.regulator_name);

	pwr->vio.gpio = -1;
	prop = of_find_property(np, "synaptics,vio-gpio", NULL);
	if (prop && prop->length)
		pwr->vio.gpio = of_get_named_gpio_flags(np, "synaptics,vio-gpio", 0, NULL);

	pwr->vio.power_on_delay_ms = 0;
	prop = of_find_property(np, "synaptics,vio-power-on-delay-ms", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,vio-power-on-delay-ms", &pwr->vio.power_on_delay_ms);

	pwr->vio.power_off_delay_ms = 0;
	prop = of_find_property(np, "synaptics,vio-power-off-delay-ms", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,vio-power-off-delay-ms", &pwr->vio.power_off_delay_ms);


	rst->reset_on_state = 0;
	prop = of_find_property(np, "synaptics,reset-on-state", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,reset-on-state", &rst->reset_on_state);

	rst->reset_gpio = -1;
	prop = of_find_property(np, "synaptics,reset-gpio", NULL);
	if (prop && prop->length)
		rst->reset_gpio = of_get_named_gpio_flags(np, "synaptics,reset-gpio", 0, NULL);

	prop = of_find_property(np, "synaptics,reset-active-ms", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,reset-active-ms", &rst->reset_active_ms);

	prop = of_find_property(np, "synaptics,reset-delay-ms", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,reset-delay-ms", &rst->reset_delay_ms);


	bus->switch_gpio = -1;
	prop = of_find_property(np, "synaptics,io-switch-gpio", NULL);
	if (prop && prop->length)
		bus->switch_gpio = of_get_named_gpio_flags(np, "synaptics,io-switch-gpio", 0, NULL);

	prop = of_find_property(np, "synaptics,io-switch-state", NULL);
	if (prop && prop->length)
		of_property_read_u32(np, "synaptics,io-switch-state", &bus->switch_state);


	prop = of_find_property(np, "synaptics,chunks", NULL);
	if (prop && prop->length) {
		retval = of_property_read_u32_array(np, "synaptics,chunks", temp_value, 2);
		if (retval >= 0) {
			hw_if->hw_platform.rd_chunk_size = temp_value[0];
			hw_if->hw_platform.wr_chunk_size = temp_value[1];
		}
	}

	prop = of_find_property(np, "synaptics,flash-access-delay-us", NULL);
	if (prop && prop->length) {
		retval = of_property_read_u32_array(np, "synaptics,flash-access-delay-usy",
				temp_value, 3);
		if (retval >= 0) {
			product->default_flash_delay_us[0] = temp_value[0];
			product->default_flash_delay_us[1] = temp_value[1];
			product->default_flash_delay_us[2] = temp_value[2];
		}
	}

	prop = of_find_property(np, "synaptics,command-timeout-ms", NULL);
	if (prop && prop->length)
		retval = of_property_read_u32(np, "synaptics,command-timeout-ms",
				&product->default_cmd_timeout_ms);

	prop = of_find_property(np, "synaptics,command-polling-ms", NULL);
	if (prop && prop->length)
		retval = of_property_read_u32(np, "synaptics,command-polling-ms",
				&product->default_cmd_polling_ms);

	prop = of_find_property(np, "synaptics,command-turnaround-us", NULL);
	if (prop && prop->length) {
		retval = of_property_read_u32_array(np, "synaptics,command-turnaround-us",
				temp_value, 2);
		if (retval >= 0) {
			product->default_cmd_turnaround_us[0] = temp_value[0];
			product->default_cmd_turnaround_us[1] = temp_value[1];
		}
	}

	prop = of_find_property(np, "synaptics,command-retry-us", NULL);
	if (prop && prop->length) {
		retval = of_property_read_u32_array(np, "synaptics,command-retry-us",
				temp_value, 2);
		if (retval >= 0) {
			product->default_cmd_retry_us[0] = temp_value[0];
			product->default_cmd_retry_us[1] = temp_value[1];
		}
	}

	prop = of_find_property(np, "synaptics,fw-switch-delay-ms", NULL);
	if (prop && prop->length)
		retval = of_property_read_u32(np, "synaptics,fw-switch-delay-ms",
				&product->default_fw_switch_delay_ms);

	/*
	 * Set default as 1 to let the driver report the value from the
	 * touch IC if pixels_per_mm is not set.
	 */
	hw_if->pixels_per_mm = 1;
	prop = of_find_property(np, "synaptics,pixels-per-mm", NULL);
	if (prop && prop->length)
		retval = of_property_read_u32(np, "synaptics,pixels-per-mm",
				&hw_if->pixels_per_mm);

	/*
	 * Set default as 15.
	 */
	hw_if->compression_threshold = 15;
	prop = of_find_property(np, "synaptics,compression-threshold", NULL);
	if (prop && prop->length) {
		retval = of_property_read_u32(np, "synaptics,compression-threshold", &value);
		hw_if->compression_threshold = value;
	}
	/*
	 * Set default as 50.
	 */
	hw_if->grip_delta_threshold = 50;
	prop = of_find_property(np, "synaptics,grip-delta-threshold", NULL);
	if (prop && prop->length) {
		retval = of_property_read_u32(np, "synaptics,grip-delta-threshold", &value);
		hw_if->grip_delta_threshold = value;
	}
	/*
	 * Set default as 50.
	 */
	hw_if->grip_border_threshold = 50;
	prop = of_find_property(np, "synaptics,grip-border-threshold", NULL);
	if (prop && prop->length) {
		retval = of_property_read_u32(np, "synaptics,grip-border-threshold", &value);
		hw_if->grip_border_threshold = value;
	}
	LOGI("Load from dt: chunk size(%d %d) reset (%d %d) vdd delay(%d %d) vio delay(%d %d)\n",
		hw_if->hw_platform.rd_chunk_size, hw_if->hw_platform.wr_chunk_size,
		rst->reset_active_ms, rst->reset_delay_ms, pwr->vdd.power_on_delay_ms,
		pwr->vdd.power_off_delay_ms, pwr->vio.power_on_delay_ms, pwr->vio.power_off_delay_ms);
	LOGI("Load from dt: command timeout(%d) turnaround time(%d %d) retry time(%d %d)\n",
		product->default_cmd_timeout_ms, product->default_cmd_turnaround_us[0],
		product->default_cmd_turnaround_us[1], product->default_cmd_retry_us[0],
		product->default_cmd_retry_us[1]);
	LOGI("Load from dt: flash erase(%d) flash write(%d) flash read(%d) fw switch(%d)\n",
		product->default_flash_delay_us[0], product->default_flash_delay_us[1],
		product->default_flash_delay_us[2], product->default_fw_switch_delay_ms);

	return 0;
}
#endif

/**
 * @brief  Release the resources for the use of ATTN.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_release_attn_resources(struct syna_hw_interface *hw_if)
{
	struct syna_hw_attn_data *attn;

	if (!hw_if)
		return -EINVAL;

	attn = &hw_if->bdata_attn;
	if (!attn)
		return -EINVAL;

	syna_pal_mutex_free(&attn->irq_en_mutex);

	if (attn->irq_gpio > 0)
		syna_i2c_put_gpio(attn->irq_gpio);

	return 0;
}
/**
 * @brief  Initialize the resources for the use of ATTN.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_request_attn_resources(struct syna_hw_interface *hw_if)
{
	int retval;
	static char str_attn_gpio[32] = {0};
	struct syna_hw_attn_data *attn;

	if (!hw_if)
		return -EINVAL;

	attn = &hw_if->bdata_attn;
	if (!attn)
		return -EINVAL;

	syna_pal_mutex_alloc(&attn->irq_en_mutex);

	if (attn->irq_gpio > 0) {
		retval = syna_i2c_get_gpio(attn->irq_gpio, 0, 0, str_attn_gpio);
		if (retval < 0) {
			LOGE("Fail to request GPIO %d for attention\n",
				attn->irq_gpio);
			return retval;
		}
	}

	return 0;
}
/**
 * @brief  Release the resources for the use of hardware reset.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_release_reset_resources(struct syna_hw_interface *hw_if)
{
	struct syna_hw_rst_data *rst;

	if (!hw_if)
		return -EINVAL;

	rst = &hw_if->bdata_rst;
	if (!rst)
		return -EINVAL;

	if (rst->reset_gpio > 0)
		syna_i2c_put_gpio(rst->reset_gpio);

	return 0;
}
/**
 * @brief  Initialize the resources for the use of hardware reset.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_request_reset_resources(struct syna_hw_interface *hw_if)
{
	int retval;
	static char str_rst_gpio[32] = {0};
	struct syna_hw_rst_data *rst;

	if (!hw_if)
		return -EINVAL;

	rst = &hw_if->bdata_rst;
	if (!rst)
		return -EINVAL;

	if (rst->reset_gpio > 0) {
		retval = syna_i2c_get_gpio(rst->reset_gpio, 1,
				rst->reset_on_state, str_rst_gpio);
		if (retval < 0) {
			LOGE("Fail to request GPIO %d for reset\n",
				rst->reset_gpio);
			return retval;
		}
	}

	return 0;
}
/**
 * @brief  Release the resources for the use of bus transferring.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_release_bus_resources(struct syna_hw_interface *hw_if)
{
	struct syna_hw_bus_data *bus;

	if (!hw_if)
		return -EINVAL;

	bus = &hw_if->bdata_io;
	if (!bus)
		return -EINVAL;

	syna_pal_mutex_free(&bus->io_mutex);

	if (bus->switch_gpio > 0)
		syna_i2c_put_gpio(bus->switch_gpio);

	return 0;
}
/**
 * @brief  Initialize the resources for the use of bus transferring.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_request_bus_resources(struct syna_hw_interface *hw_if)
{
	int retval;
	static char str_switch_gpio[32] = {0};
	struct syna_hw_bus_data *bus;

	if (!hw_if)
		return -EINVAL;

	bus = &hw_if->bdata_io;
	if (!bus)
		return -EINVAL;

	syna_pal_mutex_alloc(&bus->io_mutex);

	if (bus->switch_gpio > 0) {
		retval = syna_i2c_get_gpio(bus->switch_gpio, 1,
				bus->switch_state, str_switch_gpio);
		if (retval < 0) {
			LOGE("Fail to request GPIO %d for io switch\n", bus->switch_gpio);
			return retval;
		}
	}

	return 0;
}
/**
 * @brief  Release the resources for the use of power control.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_release_power_resources(struct syna_hw_interface *hw_if)
{
	struct syna_hw_pwr_data *pwr;

	if (!hw_if)
		return -EINVAL;

	pwr = &hw_if->bdata_pwr;
	if (!pwr)
		return -EINVAL;

	/* release power resource for vio */
	if (pwr->vio.control == PSU_REGULATOR) {
		if (pwr->vio.regulator_dev)
			syna_i2c_put_regulator(pwr->vio.regulator_dev);
	} else if (pwr->vio.control > 0) {
		if (pwr->vio.gpio > 0)
			syna_i2c_put_gpio(pwr->vio.gpio);
	}
	/* release power resource for VDD */
	if (pwr->vdd.control == PSU_REGULATOR) {
		if (pwr->vdd.regulator_dev)
			syna_i2c_put_regulator(pwr->vdd.regulator_dev);
	} else if (pwr->vdd.control > 0) {
		if (pwr->vdd.gpio > 0)
			syna_i2c_put_gpio(pwr->vdd.gpio);
	}

	return 0;
}
/**
 * @brief  Initialize the resources for the use of power control.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_i2c_request_power_resources(struct syna_hw_interface *hw_if)
{
	int retval;
	static char str_vdd_gpio[32] = {0};
	static char str_avdd_gpio[32] = {0};
	struct syna_hw_pwr_data *pwr;

	if (!hw_if)
		return -EINVAL;

	pwr = &hw_if->bdata_pwr;
	if (!pwr)
		return -EINVAL;

	/* request power resource for  VDD */
	if (pwr->vdd.control == PSU_REGULATOR) {
		if (!pwr->vdd.regulator_name || (strlen(pwr->vdd.regulator_name) <= 0)) {
			LOGE("Fail to get regulator for vdd, no given name of vdd\n");
			return -ENXIO;
		}
		pwr->vdd.regulator_dev = syna_i2c_get_regulator(pwr->vdd.regulator_name);
		if (IS_ERR((struct regulator *)pwr->vdd.regulator_dev)) {
			LOGE("Fail to request regulator for vdd\n");
			return -ENXIO;
		}
	} else if (pwr->vdd.control > 0) {
		if (pwr->vdd.gpio > 0) {
			retval = syna_i2c_get_gpio(pwr->vdd.gpio, 1, !pwr->power_on_state,
					str_avdd_gpio);
			if (retval < 0) {
				LOGE("Fail to request GPIO %d for vdd\n", pwr->vdd.gpio);
				return retval;
			}
		}
	}
	/* request power resource for VIO */
	if (pwr->vio.control == PSU_REGULATOR) {
		if (!pwr->vio.regulator_name || (strlen(pwr->vdd.regulator_name) <= 0)) {
			LOGE("Fail to get regulator for vio, no given name of vio\n");
			return -ENXIO;
		}
		pwr->vio.regulator_dev = syna_i2c_get_regulator(pwr->vio.regulator_name);
		if (IS_ERR((struct regulator *)pwr->vio.regulator_dev)) {
			LOGE("Fail to configure regulator for vio\n");
			return -ENXIO;
		}
	} else if (pwr->vio.control > 0)  {
		if (pwr->vio.gpio > 0) {
			retval = syna_i2c_get_gpio(pwr->vio.gpio, 1, !pwr->power_on_state,
					str_vdd_gpio);
			if (retval < 0) {
				LOGE("Fail to request GPIO %d for vio\n", pwr->vio.gpio);
				return retval;
			}
		}
	}

	return 0;
}


/**
 * @brief  Enable or disable the kernel irq.
 *
 * @param
 *    [ in] hw:    the handle of abstracted hardware interface
 *    [ in] en:    '1' for enabling, and '0' for disabling
 *
 * @return
 *   0 in case of nothing changed, positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_enable_irq(struct tcm_hw_platform *hw, bool en)
{
	struct syna_hw_interface *hw_if = (struct syna_hw_interface *)hw->device;
	struct syna_hw_attn_data *attn;

	if (!hw_if) {
		LOGE("Invalid handle of hw_if\n");
		return -ENXIO;
	}

	attn = &hw_if->bdata_attn;
	if (!attn || (attn->irq_id == 0))
		return -ENXIO;

	syna_pal_mutex_lock(&attn->irq_en_mutex);

	/* enable the handling of interrupt */
	if (en) {
		if (attn->irq_enabled) {
			LOGD("Interrupt already enabled\n");
			goto exit;
		}

		enable_irq(attn->irq_id);
		attn->irq_enabled = true;

		LOGD("Interrupt enabled\n");
	}
	/* disable the handling of interrupt */
	else {
		if (!attn->irq_enabled) {
			LOGD("Interrupt already disabled\n");
			goto exit;
		}

		disable_irq_nosync(attn->irq_id);
		attn->irq_enabled = false;

		LOGD("Interrupt disabled\n");
	}

exit:
	syna_pal_mutex_unlock(&attn->irq_en_mutex);

	return 0;
}
/**
 * @brief  Toggle the hardware gpio pin to perform the chip reset.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *
 * @return
 *     void.
 */
static void syna_i2c_hw_reset(struct syna_hw_interface *hw_if)
{
	struct syna_hw_rst_data *rst = &hw_if->bdata_rst;

	if (rst->reset_gpio == 0)
		return;

	LOGD("Prepare to toggle reset, hold:%d delay:%d\n",
		rst->reset_active_ms, rst->reset_delay_ms);

	gpio_set_value(rst->reset_gpio, (rst->reset_on_state & 0x01));
	syna_pal_sleep_ms(rst->reset_active_ms);
	gpio_set_value(rst->reset_gpio, ((!rst->reset_on_state) & 0x01));
	syna_pal_sleep_ms(rst->reset_delay_ms);

	LOGD("Reset done\n");

}
/**
 * @brief  Power on touch controller through regulators or gpios for PWM.
 *
 * @param
 *    [ in] hw_if: the handle of hw interface
 *    [ in] on:    '1' for powering on, and '0' for powering off
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_power_on(struct syna_hw_interface *hw_if, bool on)
{
	int retval = 0;
	struct syna_hw_pwr_data *pwr;

	pwr = &hw_if->bdata_pwr;
	if (!pwr)
		return -EINVAL;

	LOGD("Prepare to %s power ...\n", (on) ? "enable" : "disable");

	if (on) {
		if (pwr->vdd.control > 0) {
			/* power on VDD */
			if (pwr->vdd.control == PSU_REGULATOR) {
				if (IS_ERR((struct regulator *)pwr->vdd.regulator_dev)) {
					LOGE("Invalid regulator for vdd\n");
					goto exit;
				}
				retval = regulator_enable((struct regulator *)pwr->vdd.regulator_dev);
				if (retval < 0) {
					LOGE("Fail to enable vdd regulator\n");
					goto exit;
				}
			} else {
				if (pwr->vdd.gpio > 0)
					gpio_set_value(pwr->vdd.gpio, pwr->power_on_state);
			}

			if (pwr->vdd.power_on_delay_ms > 0)
				syna_pal_sleep_ms(pwr->vdd.power_on_delay_ms);
		}

		if (pwr->vio.control > 0) {
			/* power on VIO */
			if (pwr->vio.control == PSU_REGULATOR) {
				if (IS_ERR((struct regulator *)pwr->vio.regulator_dev)) {
					LOGE("Invalid regulator for vio\n");
					goto exit;
				}
				retval = regulator_enable((struct regulator *)pwr->vio.regulator_dev);
				if (retval < 0) {
					LOGE("Fail to enable vio regulator\n");
					goto exit;
				}
			} else {
				if (pwr->vio.gpio > 0)
					gpio_set_value(pwr->vio.gpio, pwr->power_on_state);
			}

			if (pwr->vio.power_on_delay_ms > 0)
				syna_pal_sleep_ms(pwr->vio.power_on_delay_ms);
		}
	} else {
		if (pwr->vio.control > 0) {
			/* power off VIO */
			if (pwr->vio.control == PSU_REGULATOR) {
				regulator_disable((struct regulator *)pwr->vio.regulator_dev);
			} else {
				if (pwr->vio.gpio > 0)
					gpio_set_value(pwr->vio.gpio, !pwr->power_on_state);
			}

			if (pwr->vio.power_off_delay_ms > 0)
				syna_pal_sleep_ms(pwr->vio.power_off_delay_ms);
		}
		if (pwr->vdd.control > 0) {
			/* power off vdd */
			if (pwr->vdd.control == PSU_REGULATOR) {
				regulator_disable((struct regulator *)pwr->vdd.regulator_dev);
			} else {
				if (pwr->vdd.gpio > 0)
					gpio_set_value(pwr->vdd.gpio, !pwr->power_on_state);
			}

			if (pwr->vdd.power_off_delay_ms > 0)
				syna_pal_sleep_ms(pwr->vdd.power_off_delay_ms);
		}
	}

	LOGI("Device power %s\n", (on) ? "On" : "Off");

exit:
	return retval;
}
/**
 * @brief  Implement the I2C transaction to read out data over I2C bus.
 *
 * @param
 *    [ in] hw:      the handle of abstracted hardware interface
 *    [out] rd_data: buffer for storing data retrieved from device
 *    [ in] rd_len:  number of bytes retrieved from device
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_read(struct tcm_hw_platform *hw, unsigned char *rd_data,
	unsigned int rd_len)
{
	int retval;
	unsigned int attempt;
	struct syna_hw_interface *hw_if = (struct syna_hw_interface *)hw->device;
	struct i2c_msg msg;
	struct i2c_client *i2c;
	struct syna_hw_bus_data *bus;

	if (!hw_if) {
		LOGE("Invalid handle of hw_if\n");
		return -ENXIO;
	}

	i2c = hw_if->pdev;
	bus = &hw_if->bdata_io;
	if (!i2c || !bus) {
		LOGE("Invalid bus io device\n");
		return -ENXIO;
	}

	syna_pal_mutex_lock(&bus->io_mutex);

	if ((rd_len & 0xffff) == 0xffff) {
		LOGE("Invalid read length 0x%X\n", (rd_len & 0xffff));
		retval = -EINVAL;
		goto exit;
	}

	msg.addr = i2c->addr;
	msg.flags = I2C_M_RD;
	msg.len = rd_len;
	msg.buf = rd_data;

	for (attempt = 0; attempt < XFER_ATTEMPTS; attempt++) {
		retval = i2c_transfer(i2c->adapter, &msg, 1);
		if (retval == 1) {
			retval = rd_len;
			goto exit;
		}
		LOGE("Transfer attempt %d failed at addr 0x%02x\n",
			attempt + 1, i2c->addr);

		if (attempt + 1 == XFER_ATTEMPTS) {
			retval = -EIO;
			goto exit;
		}

		syna_pal_sleep_ms(20);
	}

exit:
	syna_pal_mutex_unlock(&bus->io_mutex);

	return retval;
}

/**
 * @brief  Implement the I2C transaction to write data over I2C bus.
 *
 * @param
 *    [ in] hw:      the handle of abstracted hardware interface
 *    [ in] wr_data: written data
 *    [ in] wr_len:  length of written data in bytes
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_write(struct tcm_hw_platform *hw, unsigned char *wr_data,
	unsigned int wr_len)
{
	int retval;
	unsigned int attempt;
	struct syna_hw_interface *hw_if = (struct syna_hw_interface *)hw->device;
	struct i2c_msg msg;
	struct i2c_client *i2c;
	struct syna_hw_bus_data *bus;

	if (!hw_if) {
		LOGE("Invalid handle of hw_if\n");
		return -ENXIO;
	}

	i2c = hw_if->pdev;
	bus = &hw_if->bdata_io;
	if (!i2c || !bus) {
		LOGE("Invalid bus io device\n");
		return -ENXIO;
	}

	syna_pal_mutex_lock(&bus->io_mutex);

	if ((wr_len & 0xffff) == 0xffff) {
		LOGE("Invalid write length 0x%X\n", (wr_len & 0xffff));
		retval = -EINVAL;
		goto exit;
	}

	msg.addr = i2c->addr;
	msg.flags = 0;
	msg.len = wr_len;
	msg.buf = wr_data;

	for (attempt = 0; attempt < XFER_ATTEMPTS; attempt++) {
		retval = i2c_transfer(i2c->adapter, &msg, 1);
		if (retval == 1) {
			retval = wr_len;
			goto exit;
		}
		LOGE("Transfer attempt %d failed at addr 0x%02x\n",
			attempt + 1, i2c->addr);

		if (attempt + 1 == XFER_ATTEMPTS) {
			retval = -EIO;
			goto exit;
		}

		syna_pal_sleep_ms(20);
	}

exit:
	syna_pal_mutex_unlock(&bus->io_mutex);

	return retval;
}

/* An example of hardware settings for an I2C device */
static struct syna_hw_interface syna_i2c_hw_if = {
	.hw_platform = {
		.type = BUS_TYPE_I2C,
		.rd_chunk_size = RD_CHUNK_SIZE,
		.wr_chunk_size = WR_CHUNK_SIZE,
		.ops_read_data = syna_i2c_read,
		.ops_write_data = syna_i2c_write,
		.ops_enable_attn = syna_i2c_enable_irq,
		.support_attn = true,
#ifdef DATA_ALIGNMENT
		.alignment_base = ALIGNMENT_BASE,
		.alignment_boundary = ALIGNMENT_SIZE_BOUNDARY,
#endif
	},
	.ops_power_on = syna_i2c_power_on,
	.ops_hw_reset = syna_i2c_hw_reset,
};

/**
 * @brief  Probe and register the platform i2c device.
 *
 * @param
 *    [ in] i2c:    i2c client device
 *    [ in] dev_id: i2c device id
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *dev_id)
{
	int retval;

#ifdef CONFIG_OF
	syna_i2c_parse_dt(&syna_i2c_hw_if, &i2c->dev);
#endif

	/* keep the i/o device */
	syna_i2c_hw_if.pdev = i2c;

	p_device->dev.parent = &i2c->dev;
	p_device->dev.platform_data = &syna_i2c_hw_if;

	syna_i2c_hw_if.hw_platform.device = &syna_i2c_hw_if;

	/* initialize resources for the use of power */
	retval = syna_i2c_request_power_resources(&syna_i2c_hw_if);
	if (retval < 0) {
		LOGE("Fail to request resources for power\n");
		return retval;
	}

	/* initialize resources for the use of bus transferring */
	retval = syna_i2c_request_bus_resources(&syna_i2c_hw_if);
	if (retval < 0) {
		LOGE("Fail to request resources for bus\n");
		syna_i2c_release_power_resources(&syna_i2c_hw_if);
		return retval;
	}

	/* initialize resources for the use of reset */
	retval = syna_i2c_request_reset_resources(&syna_i2c_hw_if);
	if (retval < 0) {
		LOGE("Fail to request resources for reset\n");
		syna_i2c_release_bus_resources(&syna_i2c_hw_if);
		syna_i2c_release_power_resources(&syna_i2c_hw_if);
		return retval;
	}

	/* initialize resources for the use of attn */
	retval = syna_i2c_request_attn_resources(&syna_i2c_hw_if);
	if (retval < 0) {
		LOGE("Fail to request resources for attn\n");
		syna_i2c_release_reset_resources(&syna_i2c_hw_if);
		syna_i2c_release_bus_resources(&syna_i2c_hw_if);
		syna_i2c_release_power_resources(&syna_i2c_hw_if);
		return retval;
	}

	return 0;
}

/**
 * @brief  Unregister the platform i2c device.
 *
 * @param
 *    [ in] i2c: i2c client device
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
static void syna_i2c_remove(struct i2c_client *i2c)
#else
static int syna_i2c_remove(struct i2c_client *i2c)
#endif
{
	/* release resources */
	syna_i2c_release_attn_resources(&syna_i2c_hw_if);
	syna_i2c_release_reset_resources(&syna_i2c_hw_if);
	syna_i2c_release_bus_resources(&syna_i2c_hw_if);
	syna_i2c_release_power_resources(&syna_i2c_hw_if);

#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
	return;
#else
	return 0;
#endif
}
/**
 * @brief  Release the platform I2C device.
 *
 * @param
 *    [ in] dev: pointer to device
 *
 * @return
 *    none
 */
static void syna_i2c_release(struct device *dev)
{
	LOGI("I2C device removed\n");
}

/* Example of an i2c device driver */
static const struct i2c_device_id syna_i2c_id_table[] = {
	{I2C_MODULE_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, syna_i2c_id_table);

#ifdef CONFIG_OF
static const struct of_device_id syna_i2c_of_match_table[] = {
	{
		.compatible = "synaptics,tcm-i2c",
	},
	{},
};
MODULE_DEVICE_TABLE(of, syna_i2c_of_match_table);
#else
#define syna_i2c_of_match_table NULL
#endif

static struct i2c_driver syna_i2c_driver = {
	.driver = {
		.name = I2C_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = syna_i2c_of_match_table,
	},
	.probe = syna_i2c_probe,
	.remove = syna_i2c_remove,
	.id_table = syna_i2c_id_table,
};

/* Example of a platform device */
static struct platform_device syna_i2c_device = {
	.name = PLATFORM_DRIVER_NAME,
	.dev = {
		.release = syna_i2c_release,
	}
};


/**
 * @brief  Initialize the hardware module.
 *
 * @param
 *    void
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_hw_interface_init(void)
{
	int retval;

	/* register the platform device */
	retval = platform_device_register(&syna_i2c_device);
	if (retval < 0) {
		LOGE("Fail to register platform device\n");
		return retval;
	}

	p_device = &syna_i2c_device;

	/* add an i2c driver */
	retval = i2c_add_driver(&syna_i2c_driver);
	if (retval < 0) {
		LOGE("Fail to add i2c driver\n");
		return retval;
	}

	return retval;
}

/**
 * @brief  Delete the hardware module.
 *
 * @param
 *    void
 *
 * @return
 *    void.
 */
void syna_hw_interface_exit(void)
{
	/* delete the i2c driver */
	i2c_del_driver(&syna_i2c_driver);

	/* unregister the platform device */
	platform_device_unregister(&syna_i2c_device);
}

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics TouchCom I2C Bus Module");
MODULE_LICENSE("GPL v2");

