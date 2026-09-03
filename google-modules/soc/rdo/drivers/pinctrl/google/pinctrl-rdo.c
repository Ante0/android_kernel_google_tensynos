// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Google LLC
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>

#include "common.h"
#define google_pinmux__ -1

GOOGLE_PINS(0);
GOOGLE_PINS(1);
GOOGLE_PINS(2);
GOOGLE_PINS(3);
GOOGLE_PINS(4);
GOOGLE_PINS(5);
GOOGLE_PINS(6);
GOOGLE_PINS(7);
GOOGLE_PINS(8);
GOOGLE_PINS(9);
GOOGLE_PINS(10);
GOOGLE_PINS(11);
GOOGLE_PINS(12);
GOOGLE_PINS(13);
GOOGLE_PINS(14);
GOOGLE_PINS(15);
GOOGLE_PINS(16);
GOOGLE_PINS(17);
GOOGLE_PINS(18);
GOOGLE_PINS(19);
GOOGLE_PINS(20);
GOOGLE_PINS(21);
GOOGLE_PINS(22);
GOOGLE_PINS(23);
GOOGLE_PINS(24);
GOOGLE_PINS(25);
GOOGLE_PINS(26);
GOOGLE_PINS(27);
GOOGLE_PINS(28);
GOOGLE_PINS(29);
GOOGLE_PINS(30);
GOOGLE_PINS(31);
GOOGLE_PINS(32);
GOOGLE_PINS(33);
GOOGLE_PINS(34);
GOOGLE_PINS(35);
GOOGLE_PINS(36);
GOOGLE_PINS(37);
GOOGLE_PINS(38);
GOOGLE_PINS(39);
GOOGLE_PINS(40);
GOOGLE_PINS(41);
GOOGLE_PINS(42);
GOOGLE_PINS(43);
GOOGLE_PINS(44);
GOOGLE_PINS(45);
GOOGLE_PINS(46);
GOOGLE_PINS(47);
GOOGLE_PINS(48);
GOOGLE_PINS(49);
GOOGLE_PINS(50);
GOOGLE_PINS(51);
GOOGLE_PINS(52);
GOOGLE_PINS(53);
GOOGLE_PINS(54);
GOOGLE_PINS(55);
GOOGLE_PINS(56);
GOOGLE_PINS(57);
GOOGLE_PINS(58);
GOOGLE_PINS(59);
GOOGLE_PINS(60);
GOOGLE_PINS(61);
GOOGLE_PINS(62);
GOOGLE_PINS(63);
GOOGLE_PINS(64);
GOOGLE_PINS(65);
GOOGLE_PINS(66);
GOOGLE_PINS(67);
GOOGLE_PINS(68);
GOOGLE_PINS(69);
GOOGLE_PINS(70);
GOOGLE_PINS(71);
GOOGLE_PINS(72);
GOOGLE_PINS(73);
GOOGLE_PINS(74);
GOOGLE_PINS(75);
GOOGLE_PINS(76);
GOOGLE_PINS(77);
GOOGLE_PINS(78);
GOOGLE_PINS(79);
GOOGLE_PINS(80);
GOOGLE_PINS(81);
GOOGLE_PINS(82);
GOOGLE_PINS(83);
GOOGLE_PINS(84);
GOOGLE_PINS(85);
GOOGLE_PINS(86);
GOOGLE_PINS(87);
GOOGLE_PINS(88);
GOOGLE_PINS(89);
GOOGLE_PINS(90);

static const struct pinctrl_pin_desc google_rdo_hsion[] = {
	PINCTRL_PIN_TYPE(0, "XHSION_PCIE0_CLKREQN0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(1, "XHSION_GPIO7", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(2, "XHSION_PCIE0_PERSTN0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(3, "XHSION_GPIO8", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(4, "XHSION_GPIO0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(5, "XHSION_GPIO1", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(6, "XHSION_GPIO2", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(7, "XHSION_GPIO3", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(8, "XHSION_GPIO4", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(9, "XHSION_GPIO5", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(10, "XHSION_GPIO6", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(11, "XHSION_ATB0", PIN_TYPE_RESERVED),
};

enum google_rdo_pinmux_hsion_functions {
	google_pinmux_hsion_gpio,
	google_pinmux_hsion_pcie0,
	google_pinmux_hsion_atb0,
};

FUNCTION_GROUPS(hsion_gpio, "XHSION_PCIE0_CLKREQN0", "XHSION_GPIO7",
		"XHSION_PCIE0_PERSTN0", "XHSION_GPIO8", "XHSION_GPIO0",
		"XHSION_GPIO1", "XHSION_GPIO2", "XHSION_GPIO3", "XHSION_GPIO4",
		"XHSION_GPIO5", "XHSION_GPIO6");
FUNCTION_GROUPS(hsion_pcie0, "XHSION_PCIE0_CLKREQN0", "XHSION_PCIE0_PERSTN0");
FUNCTION_GROUPS(hsion_atb0, "XHSION_ATB0");

static const struct google_pingroup google_rdo_hsion_groups[] = {
	PIN_GROUP(0, "XHSION_PCIE0_CLKREQN0", hsion_gpio, hsion_pcie0, _, _, _,
		  _, _, _, _),
	PIN_GROUP(1, "XHSION_GPIO7", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(2, "XHSION_PCIE0_PERSTN0", hsion_gpio, hsion_pcie0, _, _, _,
		  _, _, _, _),
	PIN_GROUP(3, "XHSION_GPIO8", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(4, "XHSION_GPIO0", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(5, "XHSION_GPIO1", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(6, "XHSION_GPIO2", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(7, "XHSION_GPIO3", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(8, "XHSION_GPIO4", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(9, "XHSION_GPIO5", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(10, "XHSION_GPIO6", hsion_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(11, "XHSION_ATB0", _, hsion_atb0, _, _, _, _, _, _, _),
};

static const struct google_pin_function google_rdo_hsion_functions[] = {
	FUNCTION(hsion_gpio),
	FUNCTION(hsion_pcie0),
	FUNCTION(hsion_atb0),
};

#define MAX_NR_GPIO_HSION 12

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_hsion = {
	.pins = google_rdo_hsion,
	.num_pins = ARRAY_SIZE(google_rdo_hsion),
	.groups = google_rdo_hsion_groups,
	.num_groups = ARRAY_SIZE(google_rdo_hsion_groups),
	.funcs = google_rdo_hsion_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_hsion_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_HSION,
	.label = "hsion"
};

static const struct pinctrl_pin_desc google_rdo_hsios[] = {
	PINCTRL_PIN_TYPE(0, "XHSIOS_CLKBUF_1", PIN_TYPE_RESERVED),
	PINCTRL_PIN_TYPE(1, "XHSIOS_UFS_REFCLK", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(2, "XHSIOS_UFS_RESETB", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(3, "XHSIOS_GPIO7", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(4, "XHSIOS_GPIO8", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(5, "XHSIOS_GPIO0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(6, "XHSIOS_GPIO1", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(7, "XHSIOS_GPIO2", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(8, "XHSIOS_GPIO3", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(9, "XHSIOS_GPIO4", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(10, "XHSIOS_UFS_PWR_EN", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(11, "XHSIOS_ATB1", PIN_TYPE_RESERVED),
	PINCTRL_PIN_TYPE(12, "XHSIOS_PCIE1_CLKREQN0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(13, "XHSIOS_PCIE1_PERSTN0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(14, "XHSIOS_GPIO5", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(15, "XHSIOS_GPIO6", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(16, "XHSIOS_VSYNC4", PIN_DRV_3_BITS_WITH_SLEW),
};

enum google_rdo_pinmux_hsios_functions {
	google_pinmux_hsios_clkbuf,
	google_pinmux_hsios_ufs,
	google_pinmux_hsios_gpio,
	google_pinmux_hsios_sd_data,
	google_pinmux_hsios_sd_cmd,
	google_pinmux_hsios_sd_fbclk,
	google_pinmux_hsios_sd_clk,
	google_pinmux_hsios_atb1,
	google_pinmux_hsios_pcie1,
	google_pinmux_hsios_vsync,
};

FUNCTION_GROUPS(hsios_clkbuf, "XHSIOS_CLKBUF_1");
FUNCTION_GROUPS(hsios_ufs, "XHSIOS_UFS_REFCLK", "XHSIOS_UFS_RESETB",
		"XHSIOS_UFS_PWR_EN");
FUNCTION_GROUPS(hsios_gpio, "XHSIOS_GPIO7", "XHSIOS_GPIO8", "XHSIOS_GPIO0",
		"XHSIOS_GPIO1", "XHSIOS_GPIO2", "XHSIOS_GPIO3", "XHSIOS_GPIO4",
		"XHSIOS_PCIE1_CLKREQN0", "XHSIOS_PCIE1_PERSTN0", "XHSIOS_GPIO5",
		"XHSIOS_GPIO6", "XHSIOS_VSYNC4");
FUNCTION_GROUPS(hsios_sd_data, "XHSIOS_GPIO7", "XHSIOS_GPIO8", "XHSIOS_GPIO1",
		"XHSIOS_GPIO2");
FUNCTION_GROUPS(hsios_sd_cmd, "XHSIOS_GPIO0");
FUNCTION_GROUPS(hsios_sd_fbclk, "XHSIOS_GPIO3");
FUNCTION_GROUPS(hsios_sd_clk, "XHSIOS_GPIO4");
FUNCTION_GROUPS(hsios_atb1, "XHSIOS_ATB1");
FUNCTION_GROUPS(hsios_pcie1, "XHSIOS_PCIE1_CLKREQN0", "XHSIOS_PCIE1_PERSTN0");
FUNCTION_GROUPS(hsios_vsync, "XHSIOS_VSYNC4");

static const struct google_pingroup google_rdo_hsios_groups[] = {
	PIN_GROUP(0, "XHSIOS_CLKBUF_1", _, hsios_clkbuf, _, _, _, _, _, _, _),
	PIN_GROUP(1, "XHSIOS_UFS_REFCLK", _, hsios_ufs, _, _, _, _, _, _, _),
	PIN_GROUP(2, "XHSIOS_UFS_RESETB", hsios_ufs, _, _, _, _, _, _, _, _),
	PIN_GROUP(3, "XHSIOS_GPIO7", hsios_gpio, _, hsios_sd_data, _, _, _, _,
		  _, _),
	PIN_GROUP(4, "XHSIOS_GPIO8", hsios_gpio, _, hsios_sd_data, _, _, _, _,
		  _, _),
	PIN_GROUP(5, "XHSIOS_GPIO0", hsios_gpio, _, hsios_sd_cmd, _, _, _, _, _,
		  _),
	PIN_GROUP(6, "XHSIOS_GPIO1", hsios_gpio, _, hsios_sd_data, _, _, _, _,
		  _, _),
	PIN_GROUP(7, "XHSIOS_GPIO2", hsios_gpio, _, hsios_sd_data, _, _, _, _,
		  _, _),
	PIN_GROUP(8, "XHSIOS_GPIO3", hsios_gpio, _, hsios_sd_fbclk, _, _, _, _,
		  _, _),
	PIN_GROUP(9, "XHSIOS_GPIO4", hsios_gpio, _, hsios_sd_clk, _, _, _, _, _,
		  _),
	PIN_GROUP(10, "XHSIOS_UFS_PWR_EN", hsios_ufs, _, _, _, _, _, _, _, _),
	PIN_GROUP(11, "XHSIOS_ATB1", _, hsios_atb1, _, _, _, _, _, _, _),
	PIN_GROUP(12, "XHSIOS_PCIE1_CLKREQN0", hsios_gpio, hsios_pcie1, _, _, _,
		  _, _, _, _),
	PIN_GROUP(13, "XHSIOS_PCIE1_PERSTN0", hsios_gpio, hsios_pcie1, _, _, _,
		  _, _, _, _),
	PIN_GROUP(14, "XHSIOS_GPIO5", hsios_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(15, "XHSIOS_GPIO6", hsios_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(16, "XHSIOS_VSYNC4", hsios_gpio, hsios_vsync, _, _, _, _, _,
		  _, _),
};

static const struct google_pin_function google_rdo_hsios_functions[] = {
	FUNCTION(hsios_clkbuf), FUNCTION(hsios_ufs),
	FUNCTION(hsios_gpio),	FUNCTION(hsios_sd_data),
	FUNCTION(hsios_sd_cmd), FUNCTION(hsios_sd_fbclk),
	FUNCTION(hsios_sd_clk), FUNCTION(hsios_atb1),
	FUNCTION(hsios_pcie1),	FUNCTION(hsios_vsync),
};

#define MAX_NR_GPIO_HSIOS 17

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_hsios = {
	.pins = google_rdo_hsios,
	.num_pins = ARRAY_SIZE(google_rdo_hsios),
	.groups = google_rdo_hsios_groups,
	.num_groups = ARRAY_SIZE(google_rdo_hsios_groups),
	.funcs = google_rdo_hsios_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_hsios_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_HSIOS,
	.label = "hsios"
};

static const struct pinctrl_pin_desc google_rdo_dpu[] = {
	PINCTRL_PIN_TYPE(0, "XDPU_TE0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(1, "XDPU_TE1", PIN_DRV_4_BITS_NO_SLEW),
};

enum google_rdo_pinmux_dpu_functions {
	google_pinmux_dpu_gpio,
	google_pinmux_dpu_te,
};

FUNCTION_GROUPS(dpu_gpio, "XDPU_TE0", "XDPU_TE1");
FUNCTION_GROUPS(dpu_te, "XDPU_TE0", "XDPU_TE1");

static const struct google_pingroup google_rdo_dpu_groups[] = {
	PIN_GROUP(0, "XDPU_TE0", dpu_gpio, dpu_te, _, _, _, _, _, _, _),
	PIN_GROUP(1, "XDPU_TE1", dpu_gpio, dpu_te, _, _, _, _, _, _, _),
};

static const struct google_pin_function google_rdo_dpu_functions[] = {
	FUNCTION(dpu_gpio),
	FUNCTION(dpu_te),
};

#define MAX_NR_GPIO_DPU 2

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_dpu = {
	.pins = google_rdo_dpu,
	.num_pins = ARRAY_SIZE(google_rdo_dpu),
	.groups = google_rdo_dpu_groups,
	.num_groups = ARRAY_SIZE(google_rdo_dpu_groups),
	.funcs = google_rdo_dpu_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_dpu_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_DPU,
	.label = "dpu"
};

static const struct pinctrl_pin_desc google_rdo_aoc[] = {
	PINCTRL_PIN_TYPE(0, "XAOC_PDM1_MIC_IN", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(1, "XAOC_PDM1_MIC_CLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(2, "XAOC_PDM2_MIC_IN", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(3, "XAOC_PDM2_MIC_CLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(4, "XAOC_PDM3_MIC_IN", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(5, "XAOC_PDM3_MIC_CLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(6, "XAOC_PDM0_MIC_IN", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(7, "XAOC_PDM0_MIC_CLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(8, "XAOC_I2S0_BCLK", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(9, "XAOC_I2S0_WS", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(10, "XAOC_I2S0_SDO", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(11, "XAOC_I2S0_SDI", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(12, "XAOC_GPIO0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(13, "XAOC_GPIO1", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(14, "XAOC_UART0_RXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(15, "XAOC_UART0_TXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(16, "XAOC_UART0_RTSn", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(17, "XAOC_UART0_CTSn", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(18, "XAOC_UART3_RXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(19, "XAOC_UART3_TXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(20, "XAOC_UART3_RTSn", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(21, "XAOC_UART3_CTSn", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(22, "XAOC_GPIO2", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(23, "XAOC_GPIO3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(24, "XAOC_GPIO4", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(25, "XAOC_GPIO5", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(26, "XAOC_SPI0_SCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(27, "XAOC_SPI0_MOSI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(28, "XAOC_SPI0_MISO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(29, "XAOC_SPI0_SSB", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(30, "XAOC_TDM0_BCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(31, "XAOC_TDM0_WS", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(32, "XAOC_TDM0_SDO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(33, "XAOC_TDM0_SDI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(34, "XAOC_OUT_MCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(35, "XAOC_PDM0_FLCKR_IN", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(36, "XAOC_PDM0_FLCKR_CLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(37, "XAOC_I2C2_SCL", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(38, "XAOC_I2C2_SDA", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(39, "XAOC_I2C4_SCL", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(40, "XAOC_I2C4_SDA", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(41, "XAOC_GPIO6", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(42, "XAOC_GPIO7", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(43, "XAOC_SPI1_SCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(44, "XAOC_SPI1_MOSI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(45, "XAOC_SPI1_MISO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(46, "XAOC_SPI1_SSB", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(47, "XAOC_TDM1_BCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(48, "XAOC_TDM1_WS", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(49, "XAOC_TDM1_SDO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(50, "XAOC_TDM1_SDI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(51, "XAOC_SPI4_SCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(52, "XAOC_SPI4_MOSI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(53, "XAOC_SPI4_MISO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(54, "XAOC_SPI4_SSB", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(55, "XAOC_GPIO8", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(56, "XAOC_GPIO9", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(57, "XAOC_GPIO10", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(58, "XAOC_GPIO11", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(59, "XAOC_I2S1_BCLK", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(60, "XAOC_I2S1_WS", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(61, "XAOC_I2S1_SDO", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(62, "XAOC_I2S1_SDI", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(63, "XAOC_I2C0_SCL", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(64, "XAOC_I2C0_SDA", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(65, "XAOC_GPIO12", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(66, "XAOC_GPIO13", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(67, "XAOC_I3C0_SCL", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(68, "XAOC_I3C0_SDA", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(69, "XAOC_I3C1_SCL", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(70, "XAOC_I3C1_SDA", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(71, "XAOC_SPI3_SCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(72, "XAOC_SPI3_MOSI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(73, "XAOC_SPI3_MISO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(74, "XAOC_SPI3_SSB", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(75, "XAOC_SPI2_SCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(76, "XAOC_SPI2_MOSI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(77, "XAOC_SPI2_MISO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(78, "XAOC_SPI2_SSB", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(79, "XAOC_UART1_RXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(80, "XAOC_UART1_TXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(81, "XAOC_UART1_RTSn", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(82, "XAOC_UART1_CTSn", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(83, "XAOC_UART4_RXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(84, "XAOC_UART4_TXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(85, "XAOC_UART4_RTSn", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(86, "XAOC_UART4_CTSn", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(87, "XAOC_SPI5_SCLK", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(88, "XAOC_SPI5_MOSI", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(89, "XAOC_SPI5_MISO", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(90, "XAOC_SPI5_SSB", PIN_DRV_3_BITS_WITH_SLEW),
};

enum google_rdo_pinmux_aoc_functions {
	google_pinmux_aoc_int_gpio,
	google_pinmux_aoc_pdm1,
	google_pinmux_aoc_pdm2,
	google_pinmux_aoc_pdm3,
	google_pinmux_aoc_pdm,
	google_pinmux_aoc_i2s,
	google_pinmux_aoc_cpm_eint,
	google_pinmux_aoc_uart0,
	google_pinmux_aoc_cpm_uart,
	google_pinmux_aoc_uart3,
	google_pinmux_aoc_fiq,
	google_pinmux_aoc_spi0,
	google_pinmux_aoc_cli_int_0,
	google_pinmux_aoc_tdm0,
	google_pinmux_aoc_out_mclk,
	google_pinmux_aoc_pdm0_flckr,
	google_pinmux_aoc_i2c2,
	google_pinmux_aoc_i3c5,
	google_pinmux_aoc_elaoutput,
	google_pinmux_aoc_i2c4,
	google_pinmux_aoc_i3c4,
	google_pinmux_aoc_uart2,
	google_pinmux_aoc_spi1,
	google_pinmux_aoc_cli_int_1,
	google_pinmux_aoc_tdm1,
	google_pinmux_aoc_sdwire0,
	google_pinmux_aoc_sdwire1,
	google_pinmux_aoc_spi4,
	google_pinmux_aoc_i2c0,
	google_pinmux_aoc_i3c0,
	google_pinmux_aoc_i2c1,
	google_pinmux_aoc_i3c1,
	google_pinmux_aoc_i3c3,
	google_pinmux_aoc_spi3,
	google_pinmux_aoc_cli_int_3,
	google_pinmux_aoc_i3c2,
	google_pinmux_aoc_spi2,
	google_pinmux_aoc_cli_int_2,
	google_pinmux_aoc_uart1,
	google_pinmux_aoc_uart4,
	google_pinmux_aoc_uart5,
	google_pinmux_aoc_spi5,
	google_pinmux_aoc_i2c3,
	google_pinmux_aoc_cli_int_4,
};

FUNCTION_GROUPS(aoc_int_gpio, "XAOC_PDM1_MIC_IN", "XAOC_PDM2_MIC_IN",
		"XAOC_PDM3_MIC_IN", "XAOC_PDM0_MIC_IN", "XAOC_I2S0_WS",
		"XAOC_I2S0_SDO", "XAOC_I2S0_SDI", "XAOC_GPIO0", "XAOC_GPIO1",
		"XAOC_UART0_RXD", "XAOC_UART0_TXD", "XAOC_UART0_RTSn",
		"XAOC_UART0_CTSn", "XAOC_UART3_RXD", "XAOC_UART3_TXD",
		"XAOC_UART3_RTSn", "XAOC_UART3_CTSn", "XAOC_GPIO2",
		"XAOC_GPIO3", "XAOC_GPIO4", "XAOC_GPIO5", "XAOC_SPI0_SCLK",
		"XAOC_SPI0_MOSI", "XAOC_SPI0_MISO", "XAOC_SPI0_SSB",
		"XAOC_TDM0_WS", "XAOC_TDM0_SDO", "XAOC_TDM0_SDI",
		"XAOC_PDM0_FLCKR_IN", "XAOC_PDM0_FLCKR_CLK", "XAOC_I2C2_SCL",
		"XAOC_I2C2_SDA", "XAOC_I2C4_SCL", "XAOC_I2C4_SDA", "XAOC_GPIO6",
		"XAOC_GPIO7", "XAOC_SPI1_SCLK", "XAOC_SPI1_MOSI",
		"XAOC_SPI1_MISO", "XAOC_SPI1_SSB", "XAOC_TDM1_BCLK",
		"XAOC_TDM1_WS", "XAOC_TDM1_SDO", "XAOC_TDM1_SDI",
		"XAOC_SPI4_SCLK", "XAOC_SPI4_MOSI", "XAOC_SPI4_MISO",
		"XAOC_SPI4_SSB", "XAOC_GPIO8", "XAOC_GPIO9", "XAOC_GPIO10",
		"XAOC_GPIO11", "XAOC_I2S1_WS", "XAOC_I2S1_SDO", "XAOC_I2S1_SDI",
		"XAOC_I2C0_SCL", "XAOC_I2C0_SDA", "XAOC_GPIO12", "XAOC_GPIO13",
		"XAOC_I3C0_SCL", "XAOC_I3C0_SDA", "XAOC_I3C1_SCL",
		"XAOC_I3C1_SDA", "XAOC_SPI3_SCLK", "XAOC_SPI3_MOSI",
		"XAOC_SPI3_MISO", "XAOC_SPI3_SSB", "XAOC_SPI2_SCLK",
		"XAOC_SPI2_MOSI", "XAOC_SPI2_MISO", "XAOC_SPI2_SSB",
		"XAOC_UART1_RXD", "XAOC_UART1_TXD", "XAOC_UART1_RTSn",
		"XAOC_UART1_CTSn", "XAOC_UART4_RXD", "XAOC_UART4_TXD",
		"XAOC_UART4_RTSn", "XAOC_UART4_CTSn", "XAOC_SPI5_SCLK",
		"XAOC_SPI5_MOSI", "XAOC_SPI5_MISO", "XAOC_SPI5_SSB");
FUNCTION_GROUPS(aoc_pdm1, "XAOC_PDM1_MIC_IN", "XAOC_PDM1_MIC_CLK");
FUNCTION_GROUPS(aoc_pdm2, "XAOC_PDM2_MIC_IN", "XAOC_PDM2_MIC_CLK");
FUNCTION_GROUPS(aoc_pdm3, "XAOC_PDM3_MIC_IN", "XAOC_PDM3_MIC_CLK");
FUNCTION_GROUPS(aoc_pdm, "XAOC_PDM0_MIC_IN", "XAOC_PDM0_MIC_CLK");
FUNCTION_GROUPS(aoc_i2s, "XAOC_I2S0_BCLK", "XAOC_I2S0_WS", "XAOC_I2S0_SDO",
		"XAOC_I2S0_SDI", "XAOC_I2S1_BCLK", "XAOC_I2S1_WS",
		"XAOC_I2S1_SDO", "XAOC_I2S1_SDI");
FUNCTION_GROUPS(aoc_cpm_eint, "XAOC_GPIO0", "XAOC_GPIO1", "XAOC_GPIO2",
		"XAOC_GPIO3", "XAOC_GPIO4", "XAOC_GPIO5", "XAOC_GPIO6",
		"XAOC_GPIO7", "XAOC_GPIO8", "XAOC_GPIO9", "XAOC_GPIO10",
		"XAOC_GPIO11", "XAOC_GPIO12", "XAOC_GPIO13");
FUNCTION_GROUPS(aoc_uart0, "XAOC_UART0_RXD", "XAOC_UART0_TXD",
		"XAOC_UART0_RTSn", "XAOC_UART0_CTSn");
FUNCTION_GROUPS(aoc_cpm_uart, "XAOC_UART0_RXD", "XAOC_UART0_TXD",
		"XAOC_UART0_RTSn", "XAOC_UART0_CTSn");
FUNCTION_GROUPS(aoc_uart3, "XAOC_UART3_RXD", "XAOC_UART3_TXD",
		"XAOC_UART3_RTSn", "XAOC_UART3_CTSn");
FUNCTION_GROUPS(aoc_fiq, "XAOC_GPIO3", "XAOC_GPIO4", "XAOC_GPIO5", "XAOC_GPIO6",
		"XAOC_GPIO7", "XAOC_TDM1_BCLK", "XAOC_TDM1_WS", "XAOC_TDM1_SDO",
		"XAOC_TDM1_SDI", "XAOC_SPI4_MISO", "XAOC_GPIO8", "XAOC_GPIO9",
		"XAOC_GPIO10", "XAOC_GPIO11", "XAOC_GPIO12", "XAOC_GPIO13",
		"XAOC_UART4_RXD", "XAOC_UART4_RTSn");
FUNCTION_GROUPS(aoc_spi0, "XAOC_SPI0_SCLK", "XAOC_SPI0_MOSI", "XAOC_SPI0_MISO",
		"XAOC_SPI0_SSB");
FUNCTION_GROUPS(aoc_cli_int_0, "XAOC_SPI0_SCLK", "XAOC_SPI0_MOSI",
		"XAOC_SPI0_MISO", "XAOC_SPI0_SSB");
FUNCTION_GROUPS(aoc_tdm0, "XAOC_TDM0_BCLK", "XAOC_TDM0_WS", "XAOC_TDM0_SDO",
		"XAOC_TDM0_SDI");
FUNCTION_GROUPS(aoc_out_mclk, "XAOC_OUT_MCLK");
FUNCTION_GROUPS(aoc_pdm0_flckr, "XAOC_PDM0_FLCKR_IN", "XAOC_PDM0_FLCKR_CLK");
FUNCTION_GROUPS(aoc_i2c2, "XAOC_I2C2_SCL", "XAOC_I2C2_SDA");
FUNCTION_GROUPS(aoc_i3c5, "XAOC_I2C2_SCL", "XAOC_I2C2_SDA");
FUNCTION_GROUPS(aoc_elaoutput, "XAOC_I2C2_SCL", "XAOC_I2C2_SDA",
		"XAOC_I3C1_SCL", "XAOC_I3C1_SDA");
FUNCTION_GROUPS(aoc_i2c4, "XAOC_I2C4_SCL", "XAOC_I2C4_SDA");
FUNCTION_GROUPS(aoc_i3c4, "XAOC_I2C4_SCL", "XAOC_I2C4_SDA");
FUNCTION_GROUPS(aoc_uart2, "XAOC_I2C4_SCL", "XAOC_I2C4_SDA", "XAOC_I2C0_SCL",
		"XAOC_I2C0_SDA");
FUNCTION_GROUPS(aoc_spi1, "XAOC_SPI1_SCLK", "XAOC_SPI1_MOSI", "XAOC_SPI1_MISO",
		"XAOC_SPI1_SSB");
FUNCTION_GROUPS(aoc_cli_int_1, "XAOC_SPI1_SCLK", "XAOC_SPI1_MOSI",
		"XAOC_SPI1_MISO", "XAOC_SPI1_SSB");
FUNCTION_GROUPS(aoc_tdm1, "XAOC_TDM1_BCLK", "XAOC_TDM1_WS", "XAOC_TDM1_SDO",
		"XAOC_TDM1_SDI", "XAOC_GPIO8", "XAOC_GPIO9", "XAOC_GPIO10",
		"XAOC_GPIO11");
FUNCTION_GROUPS(aoc_sdwire0, "XAOC_TDM1_BCLK", "XAOC_TDM1_WS", "XAOC_GPIO8",
		"XAOC_GPIO9");
FUNCTION_GROUPS(aoc_sdwire1, "XAOC_TDM1_SDO", "XAOC_TDM1_SDI", "XAOC_GPIO10",
		"XAOC_GPIO11");
FUNCTION_GROUPS(aoc_spi4, "XAOC_SPI4_SCLK", "XAOC_SPI4_MOSI", "XAOC_SPI4_MISO",
		"XAOC_SPI4_SSB");
FUNCTION_GROUPS(aoc_i2c0, "XAOC_I2C0_SCL", "XAOC_I2C0_SDA");
FUNCTION_GROUPS(aoc_i3c0, "XAOC_I3C0_SCL", "XAOC_I3C0_SDA");
FUNCTION_GROUPS(aoc_i2c1, "XAOC_I3C1_SCL", "XAOC_I3C1_SDA");
FUNCTION_GROUPS(aoc_i3c1, "XAOC_I3C1_SCL", "XAOC_I3C1_SDA");
FUNCTION_GROUPS(aoc_i3c3, "XAOC_SPI3_SCLK", "XAOC_SPI3_MOSI");
FUNCTION_GROUPS(aoc_spi3, "XAOC_SPI3_SCLK", "XAOC_SPI3_MOSI", "XAOC_SPI3_MISO",
		"XAOC_SPI3_SSB");
FUNCTION_GROUPS(aoc_cli_int_3, "XAOC_SPI3_SCLK", "XAOC_SPI3_MOSI",
		"XAOC_SPI3_MISO", "XAOC_SPI3_SSB");
FUNCTION_GROUPS(aoc_i3c2, "XAOC_SPI2_SCLK", "XAOC_SPI2_MOSI");
FUNCTION_GROUPS(aoc_spi2, "XAOC_SPI2_SCLK", "XAOC_SPI2_MOSI", "XAOC_SPI2_MISO",
		"XAOC_SPI2_SSB");
FUNCTION_GROUPS(aoc_cli_int_2, "XAOC_SPI2_SCLK", "XAOC_SPI2_MOSI",
		"XAOC_SPI2_MISO", "XAOC_SPI2_SSB");
FUNCTION_GROUPS(aoc_uart1, "XAOC_UART1_RXD", "XAOC_UART1_TXD",
		"XAOC_UART1_RTSn", "XAOC_UART1_CTSn");
FUNCTION_GROUPS(aoc_uart4, "XAOC_UART4_RXD", "XAOC_UART4_TXD",
		"XAOC_UART4_RTSn", "XAOC_UART4_CTSn");
FUNCTION_GROUPS(aoc_uart5, "XAOC_SPI5_SCLK", "XAOC_SPI5_MOSI", "XAOC_SPI5_MISO",
		"XAOC_SPI5_SSB");
FUNCTION_GROUPS(aoc_spi5, "XAOC_SPI5_SCLK", "XAOC_SPI5_MOSI", "XAOC_SPI5_MISO",
		"XAOC_SPI5_SSB");
FUNCTION_GROUPS(aoc_i2c3, "XAOC_SPI5_SCLK", "XAOC_SPI5_MOSI");
FUNCTION_GROUPS(aoc_cli_int_4, "XAOC_SPI5_SCLK", "XAOC_SPI5_MOSI",
		"XAOC_SPI5_MISO", "XAOC_SPI5_SSB");

static const struct google_pingroup google_rdo_aoc_groups[] = {
	PIN_GROUP(0, "XAOC_PDM1_MIC_IN", aoc_int_gpio, aoc_pdm1, _, _, _, _, _,
		  _, _),
	PIN_GROUP(1, "XAOC_PDM1_MIC_CLK", _, aoc_pdm1, _, _, _, _, _, _, _),
	PIN_GROUP(2, "XAOC_PDM2_MIC_IN", aoc_int_gpio, aoc_pdm2, _, _, _, _, _,
		  _, _),
	PIN_GROUP(3, "XAOC_PDM2_MIC_CLK", _, aoc_pdm2, _, _, _, _, _, _, _),
	PIN_GROUP(4, "XAOC_PDM3_MIC_IN", aoc_int_gpio, aoc_pdm3, _, _, _, _, _,
		  _, _),
	PIN_GROUP(5, "XAOC_PDM3_MIC_CLK", _, aoc_pdm3, _, _, _, _, _, _, _),
	PIN_GROUP(6, "XAOC_PDM0_MIC_IN", aoc_int_gpio, aoc_pdm, _, _, _, _, _,
		  _, _),
	PIN_GROUP(7, "XAOC_PDM0_MIC_CLK", _, aoc_pdm, _, _, _, _, _, _, _),
	PIN_GROUP(8, "XAOC_I2S0_BCLK", _, aoc_i2s, _, _, _, _, _, _, _),
	PIN_GROUP(9, "XAOC_I2S0_WS", aoc_int_gpio, aoc_i2s, _, _, _, _, _, _,
		  _),
	PIN_GROUP(10, "XAOC_I2S0_SDO", aoc_int_gpio, aoc_i2s, _, _, _, _, _, _,
		  _),
	PIN_GROUP(11, "XAOC_I2S0_SDI", aoc_int_gpio, aoc_i2s, _, _, _, _, _, _,
		  _),
	PIN_GROUP(12, "XAOC_GPIO0", aoc_int_gpio, _, _, _, _, _, aoc_cpm_eint,
		  _, _),
	PIN_GROUP(13, "XAOC_GPIO1", aoc_int_gpio, _, _, _, _, _, aoc_cpm_eint,
		  _, _),
	PIN_GROUP(14, "XAOC_UART0_RXD", aoc_int_gpio, aoc_uart0, _, _, _, _,
		  aoc_cpm_uart, _, _),
	PIN_GROUP(15, "XAOC_UART0_TXD", aoc_int_gpio, aoc_uart0, _, _, _, _,
		  aoc_cpm_uart, _, _),
	PIN_GROUP(16, "XAOC_UART0_RTSn", aoc_int_gpio, aoc_uart0, _, _, _, _,
		  aoc_cpm_uart, _, _),
	PIN_GROUP(17, "XAOC_UART0_CTSn", aoc_int_gpio, aoc_uart0, _, _, _, _,
		  aoc_cpm_uart, _, _),
	PIN_GROUP(18, "XAOC_UART3_RXD", aoc_int_gpio, aoc_uart3, _, _, _, _, _,
		  _, _),
	PIN_GROUP(19, "XAOC_UART3_TXD", aoc_int_gpio, aoc_uart3, _, _, _, _, _,
		  _, _),
	PIN_GROUP(20, "XAOC_UART3_RTSn", aoc_int_gpio, aoc_uart3, _, _, _, _, _,
		  _, _),
	PIN_GROUP(21, "XAOC_UART3_CTSn", aoc_int_gpio, aoc_uart3, _, _, _, _, _,
		  _, _),
	PIN_GROUP(22, "XAOC_GPIO2", aoc_int_gpio, _, _, _, _, _, aoc_cpm_eint,
		  _, _),
	PIN_GROUP(23, "XAOC_GPIO3", aoc_int_gpio, aoc_fiq, _, _, _, _,
		  aoc_cpm_eint, _, _),
	PIN_GROUP(24, "XAOC_GPIO4", aoc_int_gpio, aoc_fiq, _, _, _, _,
		  aoc_cpm_eint, _, _),
	PIN_GROUP(25, "XAOC_GPIO5", aoc_int_gpio, aoc_fiq, _, _, _, _,
		  aoc_cpm_eint, _, _),
	PIN_GROUP(26, "XAOC_SPI0_SCLK", aoc_int_gpio, _, aoc_spi0, _, _,
		  aoc_cli_int_0, _, _, _),
	PIN_GROUP(27, "XAOC_SPI0_MOSI", aoc_int_gpio, _, aoc_spi0, _, _,
		  aoc_cli_int_0, _, _, _),
	PIN_GROUP(28, "XAOC_SPI0_MISO", aoc_int_gpio, _, aoc_spi0, _, _,
		  aoc_cli_int_0, _, _, _),
	PIN_GROUP(29, "XAOC_SPI0_SSB", aoc_int_gpio, _, aoc_spi0, _, _,
		  aoc_cli_int_0, _, _, _),
	PIN_GROUP(30, "XAOC_TDM0_BCLK", _, aoc_tdm0, _, _, _, _, _, _, _),
	PIN_GROUP(31, "XAOC_TDM0_WS", aoc_int_gpio, aoc_tdm0, _, _, _, _, _, _,
		  _),
	PIN_GROUP(32, "XAOC_TDM0_SDO", aoc_int_gpio, aoc_tdm0, _, _, _, _, _, _,
		  _),
	PIN_GROUP(33, "XAOC_TDM0_SDI", aoc_int_gpio, aoc_tdm0, _, _, _, _, _, _,
		  _),
	PIN_GROUP(34, "XAOC_OUT_MCLK", _, aoc_out_mclk, _, _, _, _, _, _, _),
	PIN_GROUP(35, "XAOC_PDM0_FLCKR_IN", aoc_int_gpio, aoc_pdm0_flckr, _, _,
		  _, _, _, _, _),
	PIN_GROUP(36, "XAOC_PDM0_FLCKR_CLK", aoc_int_gpio, aoc_pdm0_flckr, _, _,
		  _, _, _, _, _),
	PIN_GROUP(37, "XAOC_I2C2_SCL", aoc_int_gpio, aoc_i2c2, aoc_i3c5,
		  aoc_elaoutput, _, _, _, _, _),
	PIN_GROUP(38, "XAOC_I2C2_SDA", aoc_int_gpio, aoc_i2c2, aoc_i3c5,
		  aoc_elaoutput, _, _, _, _, _),
	PIN_GROUP(39, "XAOC_I2C4_SCL", aoc_int_gpio, aoc_i2c4, aoc_i3c4,
		  aoc_uart2, _, _, _, _, _),
	PIN_GROUP(40, "XAOC_I2C4_SDA", aoc_int_gpio, aoc_i2c4, aoc_i3c4,
		  aoc_uart2, _, _, _, _, _),
	PIN_GROUP(41, "XAOC_GPIO6", aoc_int_gpio, aoc_fiq, _, _, _, _,
		  aoc_cpm_eint, _, _),
	PIN_GROUP(42, "XAOC_GPIO7", aoc_int_gpio, aoc_fiq, _, _, _, _,
		  aoc_cpm_eint, _, _),
	PIN_GROUP(43, "XAOC_SPI1_SCLK", aoc_int_gpio, _, aoc_spi1, _, _,
		  aoc_cli_int_1, _, _, _),
	PIN_GROUP(44, "XAOC_SPI1_MOSI", aoc_int_gpio, _, aoc_spi1, _, _,
		  aoc_cli_int_1, _, _, _),
	PIN_GROUP(45, "XAOC_SPI1_MISO", aoc_int_gpio, _, aoc_spi1, _, _,
		  aoc_cli_int_1, _, _, _),
	PIN_GROUP(46, "XAOC_SPI1_SSB", aoc_int_gpio, _, aoc_spi1, _, _,
		  aoc_cli_int_1, _, _, _),
	PIN_GROUP(47, "XAOC_TDM1_BCLK", aoc_int_gpio, aoc_tdm1, aoc_sdwire0,
		  aoc_fiq, _, _, _, _, _),
	PIN_GROUP(48, "XAOC_TDM1_WS", aoc_int_gpio, aoc_tdm1, aoc_sdwire0,
		  aoc_fiq, _, _, _, _, _),
	PIN_GROUP(49, "XAOC_TDM1_SDO", aoc_int_gpio, aoc_tdm1, aoc_sdwire1,
		  aoc_fiq, _, _, _, _, _),
	PIN_GROUP(50, "XAOC_TDM1_SDI", aoc_int_gpio, aoc_tdm1, aoc_sdwire1,
		  aoc_fiq, _, _, _, _, _),
	PIN_GROUP(51, "XAOC_SPI4_SCLK", aoc_int_gpio, _, aoc_spi4, _, _, _, _,
		  _, _),
	PIN_GROUP(52, "XAOC_SPI4_MOSI", aoc_int_gpio, _, aoc_spi4, _, _, _, _,
		  _, _),
	PIN_GROUP(53, "XAOC_SPI4_MISO", aoc_int_gpio, aoc_fiq, aoc_spi4, _, _,
		  _, _, _, _),
	PIN_GROUP(54, "XAOC_SPI4_SSB", aoc_int_gpio, _, aoc_spi4, _, _, _, _, _,
		  _),
	PIN_GROUP(55, "XAOC_GPIO8", aoc_int_gpio, _, aoc_tdm1, aoc_sdwire0,
		  aoc_fiq, _, aoc_cpm_eint, _, _),
	PIN_GROUP(56, "XAOC_GPIO9", aoc_int_gpio, _, aoc_tdm1, aoc_sdwire0,
		  aoc_fiq, _, aoc_cpm_eint, _, _),
	PIN_GROUP(57, "XAOC_GPIO10", aoc_int_gpio, _, aoc_tdm1, aoc_sdwire1,
		  aoc_fiq, _, aoc_cpm_eint, _, _),
	PIN_GROUP(58, "XAOC_GPIO11", aoc_int_gpio, _, aoc_tdm1, aoc_sdwire1,
		  aoc_fiq, _, aoc_cpm_eint, _, _),
	PIN_GROUP(59, "XAOC_I2S1_BCLK", _, aoc_i2s, _, _, _, _, _, _, _),
	PIN_GROUP(60, "XAOC_I2S1_WS", aoc_int_gpio, aoc_i2s, _, _, _, _, _, _,
		  _),
	PIN_GROUP(61, "XAOC_I2S1_SDO", aoc_int_gpio, aoc_i2s, _, _, _, _, _, _,
		  _),
	PIN_GROUP(62, "XAOC_I2S1_SDI", aoc_int_gpio, aoc_i2s, _, _, _, _, _, _,
		  _),
	PIN_GROUP(63, "XAOC_I2C0_SCL", aoc_int_gpio, aoc_i2c0, aoc_uart2, _, _,
		  _, _, _, _),
	PIN_GROUP(64, "XAOC_I2C0_SDA", aoc_int_gpio, aoc_i2c0, aoc_uart2, _, _,
		  _, _, _, _),
	PIN_GROUP(65, "XAOC_GPIO12", aoc_int_gpio, aoc_fiq, _, _, _, _,
		  aoc_cpm_eint, _, _),
	PIN_GROUP(66, "XAOC_GPIO13", aoc_int_gpio, aoc_fiq, _, _, _, _,
		  aoc_cpm_eint, _, _),
	PIN_GROUP(67, "XAOC_I3C0_SCL", aoc_int_gpio, aoc_i3c0, _, _, _, _, _, _,
		  _),
	PIN_GROUP(68, "XAOC_I3C0_SDA", aoc_int_gpio, aoc_i3c0, _, _, _, _, _, _,
		  _),
	PIN_GROUP(69, "XAOC_I3C1_SCL", aoc_int_gpio, aoc_i2c1, aoc_i3c1,
		  aoc_elaoutput, _, _, _, _, _),
	PIN_GROUP(70, "XAOC_I3C1_SDA", aoc_int_gpio, aoc_i2c1, aoc_i3c1,
		  aoc_elaoutput, _, _, _, _, _),
	PIN_GROUP(71, "XAOC_SPI3_SCLK", aoc_int_gpio, aoc_i3c3, aoc_spi3, _, _,
		  aoc_cli_int_3, _, _, _),
	PIN_GROUP(72, "XAOC_SPI3_MOSI", aoc_int_gpio, aoc_i3c3, aoc_spi3, _, _,
		  aoc_cli_int_3, _, _, _),
	PIN_GROUP(73, "XAOC_SPI3_MISO", aoc_int_gpio, _, aoc_spi3, _, _,
		  aoc_cli_int_3, _, _, _),
	PIN_GROUP(74, "XAOC_SPI3_SSB", aoc_int_gpio, _, aoc_spi3, _, _,
		  aoc_cli_int_3, _, _, _),
	PIN_GROUP(75, "XAOC_SPI2_SCLK", aoc_int_gpio, aoc_i3c2, aoc_spi2, _, _,
		  aoc_cli_int_2, _, _, _),
	PIN_GROUP(76, "XAOC_SPI2_MOSI", aoc_int_gpio, aoc_i3c2, aoc_spi2, _, _,
		  aoc_cli_int_2, _, _, _),
	PIN_GROUP(77, "XAOC_SPI2_MISO", aoc_int_gpio, _, aoc_spi2, _, _,
		  aoc_cli_int_2, _, _, _),
	PIN_GROUP(78, "XAOC_SPI2_SSB", aoc_int_gpio, _, aoc_spi2, _, _,
		  aoc_cli_int_2, _, _, _),
	PIN_GROUP(79, "XAOC_UART1_RXD", aoc_int_gpio, aoc_uart1, _, _, _, _, _,
		  _, _),
	PIN_GROUP(80, "XAOC_UART1_TXD", aoc_int_gpio, aoc_uart1, _, _, _, _, _,
		  _, _),
	PIN_GROUP(81, "XAOC_UART1_RTSn", aoc_int_gpio, aoc_uart1, _, _, _, _, _,
		  _, _),
	PIN_GROUP(82, "XAOC_UART1_CTSn", aoc_int_gpio, aoc_uart1, _, _, _, _, _,
		  _, _),
	PIN_GROUP(83, "XAOC_UART4_RXD", aoc_int_gpio, aoc_uart4, aoc_fiq, _, _,
		  _, _, _, _),
	PIN_GROUP(84, "XAOC_UART4_TXD", aoc_int_gpio, aoc_uart4, _, _, _, _, _,
		  _, _),
	PIN_GROUP(85, "XAOC_UART4_RTSn", aoc_int_gpio, aoc_uart4, aoc_fiq, _, _,
		  _, _, _, _),
	PIN_GROUP(86, "XAOC_UART4_CTSn", aoc_int_gpio, aoc_uart4, _, _, _, _, _,
		  _, _),
	PIN_GROUP(87, "XAOC_SPI5_SCLK", aoc_int_gpio, aoc_uart5, aoc_spi5,
		  aoc_i2c3, _, aoc_cli_int_4, _, _, _),
	PIN_GROUP(88, "XAOC_SPI5_MOSI", aoc_int_gpio, aoc_uart5, aoc_spi5,
		  aoc_i2c3, _, aoc_cli_int_4, _, _, _),
	PIN_GROUP(89, "XAOC_SPI5_MISO", aoc_int_gpio, aoc_uart5, aoc_spi5, _, _,
		  aoc_cli_int_4, _, _, _),
	PIN_GROUP(90, "XAOC_SPI5_SSB", aoc_int_gpio, aoc_uart5, aoc_spi5, _, _,
		  aoc_cli_int_4, _, _, _),
};

static const struct google_pin_function google_rdo_aoc_functions[] = {
	FUNCTION(aoc_int_gpio),	 FUNCTION(aoc_pdm1),
	FUNCTION(aoc_pdm2),	 FUNCTION(aoc_pdm3),
	FUNCTION(aoc_pdm),	 FUNCTION(aoc_i2s),
	FUNCTION(aoc_cpm_eint),	 FUNCTION(aoc_uart0),
	FUNCTION(aoc_cpm_uart),	 FUNCTION(aoc_uart3),
	FUNCTION(aoc_fiq),	 FUNCTION(aoc_spi0),
	FUNCTION(aoc_cli_int_0), FUNCTION(aoc_tdm0),
	FUNCTION(aoc_out_mclk),	 FUNCTION(aoc_pdm0_flckr),
	FUNCTION(aoc_i2c2),	 FUNCTION(aoc_i3c5),
	FUNCTION(aoc_elaoutput), FUNCTION(aoc_i2c4),
	FUNCTION(aoc_i3c4),	 FUNCTION(aoc_uart2),
	FUNCTION(aoc_spi1),	 FUNCTION(aoc_cli_int_1),
	FUNCTION(aoc_tdm1),	 FUNCTION(aoc_sdwire0),
	FUNCTION(aoc_sdwire1),	 FUNCTION(aoc_spi4),
	FUNCTION(aoc_i2c0),	 FUNCTION(aoc_i3c0),
	FUNCTION(aoc_i2c1),	 FUNCTION(aoc_i3c1),
	FUNCTION(aoc_i3c3),	 FUNCTION(aoc_spi3),
	FUNCTION(aoc_cli_int_3), FUNCTION(aoc_i3c2),
	FUNCTION(aoc_spi2),	 FUNCTION(aoc_cli_int_2),
	FUNCTION(aoc_uart1),	 FUNCTION(aoc_uart4),
	FUNCTION(aoc_uart5),	 FUNCTION(aoc_spi5),
	FUNCTION(aoc_i2c3),	 FUNCTION(aoc_cli_int_4),
};

#define MAX_NR_GPIO_AOC 91

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_aoc = {
	.pins = google_rdo_aoc,
	.num_pins = ARRAY_SIZE(google_rdo_aoc),
	.groups = google_rdo_aoc_groups,
	.num_groups = ARRAY_SIZE(google_rdo_aoc_groups),
	.funcs = google_rdo_aoc_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_aoc_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_AOC,
	.label = "aoc"
};

static const struct pinctrl_pin_desc google_rdo_gdmc[] = {
	PINCTRL_PIN_TYPE(0, "XGDMC_UART0_RXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(1, "XGDMC_UART0_TXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(2, "XGDMC_IN_DEBUG_1", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(3, "XGDMC_DEBUG_DEFAULT_1", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(4, "XGDMC_UART_MODEM_RXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(5, "XGDMC_UART_MODEM_TXD", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(6, "XGDMC_DBGSEL", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(7, "XGDMC_JTAG_TRST_n", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(8, "XGDMC_JTAG_SRST_n", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(9, "XGDMC_JTAG_TMS", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(10, "XGDMC_JTAG_TCK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(11, "XGDMC_JTAG_TDI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(12, "XGDMC_JTAG_TDO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(13, "XGDMC_SOC_DEBUG_UART_RXD",
			 PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(14, "XGDMC_SOC_DEBUG_UART_TXD",
			 PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(15, "XGDMC_CTI_0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(16, "XGDMC_UART_GSC_RXD", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(17, "XGDMC_UART_GSC_TXD", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(18, "XGDMC_IN_DEBUG_0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(19, "XGDMC_DEBUG_DEFAULT_0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(20, "XGDMC_CTI_1", PIN_DRV_4_BITS_NO_SLEW),
};

enum google_rdo_pinmux_gdmc_functions {
	google_pinmux_gdmc_int,
	google_pinmux_gdmc_uart,
	google_pinmux_gdmc_debug,
	google_pinmux_gdmc_dbgsel,
	google_pinmux_gdmc_jtag,
	google_pinmux_gdmc_cti,
};

FUNCTION_GROUPS(gdmc_int, "XGDMC_UART0_RXD", "XGDMC_UART0_TXD",
		"XGDMC_IN_DEBUG_1", "XGDMC_DEBUG_DEFAULT_1",
		"XGDMC_UART_MODEM_RXD", "XGDMC_UART_MODEM_TXD",
		"XGDMC_SOC_DEBUG_UART_RXD", "XGDMC_SOC_DEBUG_UART_TXD",
		"XGDMC_CTI_0", "XGDMC_UART_GSC_RXD", "XGDMC_UART_GSC_TXD",
		"XGDMC_IN_DEBUG_0", "XGDMC_DEBUG_DEFAULT_0", "XGDMC_CTI_1");
FUNCTION_GROUPS(gdmc_uart, "XGDMC_UART0_RXD", "XGDMC_UART0_TXD",
		"XGDMC_UART_MODEM_RXD", "XGDMC_UART_MODEM_TXD",
		"XGDMC_UART_GSC_RXD", "XGDMC_UART_GSC_TXD");
FUNCTION_GROUPS(gdmc_debug, "XGDMC_IN_DEBUG_1", "XGDMC_DEBUG_DEFAULT_1",
		"XGDMC_SOC_DEBUG_UART_RXD", "XGDMC_SOC_DEBUG_UART_TXD",
		"XGDMC_IN_DEBUG_0", "XGDMC_DEBUG_DEFAULT_0");
FUNCTION_GROUPS(gdmc_dbgsel, "XGDMC_DBGSEL");
FUNCTION_GROUPS(gdmc_jtag, "XGDMC_JTAG_TRST_n", "XGDMC_JTAG_SRST_n",
		"XGDMC_JTAG_TMS", "XGDMC_JTAG_TCK", "XGDMC_JTAG_TDI",
		"XGDMC_JTAG_TDO");
FUNCTION_GROUPS(gdmc_cti, "XGDMC_CTI_0", "XGDMC_CTI_1");

static const struct google_pingroup google_rdo_gdmc_groups[] = {
	PIN_GROUP(0, "XGDMC_UART0_RXD", gdmc_int, gdmc_uart, _, _, _, _, _, _,
		  _),
	PIN_GROUP(1, "XGDMC_UART0_TXD", gdmc_int, gdmc_uart, _, _, _, _, _, _,
		  _),
	PIN_GROUP(2, "XGDMC_IN_DEBUG_1", gdmc_int, gdmc_debug, _, _, _, _, _, _,
		  _),
	PIN_GROUP(3, "XGDMC_DEBUG_DEFAULT_1", gdmc_int, gdmc_debug, _, _, _, _,
		  _, _, _),
	PIN_GROUP(4, "XGDMC_UART_MODEM_RXD", gdmc_int, gdmc_uart, _, _, _, _, _,
		  _, _),
	PIN_GROUP(5, "XGDMC_UART_MODEM_TXD", gdmc_int, gdmc_uart, _, _, _, _, _,
		  _, _),
	PIN_GROUP(6, "XGDMC_DBGSEL", _, gdmc_dbgsel, _, _, _, _, _, _, _),
	PIN_GROUP(7, "XGDMC_JTAG_TRST_n", _, gdmc_jtag, _, _, _, _, _, _, _),
	PIN_GROUP(8, "XGDMC_JTAG_SRST_n", _, gdmc_jtag, _, _, _, _, _, _, _),
	PIN_GROUP(9, "XGDMC_JTAG_TMS", _, gdmc_jtag, _, _, _, _, _, _, _),
	PIN_GROUP(10, "XGDMC_JTAG_TCK", _, gdmc_jtag, _, _, _, _, _, _, _),
	PIN_GROUP(11, "XGDMC_JTAG_TDI", _, gdmc_jtag, _, _, _, _, _, _, _),
	PIN_GROUP(12, "XGDMC_JTAG_TDO", _, gdmc_jtag, _, _, _, _, _, _, _),
	PIN_GROUP(13, "XGDMC_SOC_DEBUG_UART_RXD", gdmc_int, gdmc_debug, _, _, _,
		  _, _, _, _),
	PIN_GROUP(14, "XGDMC_SOC_DEBUG_UART_TXD", gdmc_int, gdmc_debug, _, _, _,
		  _, _, _, _),
	PIN_GROUP(15, "XGDMC_CTI_0", gdmc_int, gdmc_cti, _, _, _, _, _, _, _),
	PIN_GROUP(16, "XGDMC_UART_GSC_RXD", gdmc_int, gdmc_uart, _, _, _, _, _,
		  _, _),
	PIN_GROUP(17, "XGDMC_UART_GSC_TXD", gdmc_int, gdmc_uart, _, _, _, _, _,
		  _, _),
	PIN_GROUP(18, "XGDMC_IN_DEBUG_0", gdmc_int, gdmc_debug, _, _, _, _, _,
		  _, _),
	PIN_GROUP(19, "XGDMC_DEBUG_DEFAULT_0", gdmc_int, gdmc_debug, _, _, _, _,
		  _, _, _),
	PIN_GROUP(20, "XGDMC_CTI_1", gdmc_int, gdmc_cti, _, _, _, _, _, _, _),
};

static const struct google_pin_function google_rdo_gdmc_functions[] = {
	FUNCTION(gdmc_int),    FUNCTION(gdmc_uart), FUNCTION(gdmc_debug),
	FUNCTION(gdmc_dbgsel), FUNCTION(gdmc_jtag), FUNCTION(gdmc_cti),
};

#define MAX_NR_GPIO_GDMC 21

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_gdmc = {
	.pins = google_rdo_gdmc,
	.num_pins = ARRAY_SIZE(google_rdo_gdmc),
	.groups = google_rdo_gdmc_groups,
	.num_groups = ARRAY_SIZE(google_rdo_gdmc_groups),
	.funcs = google_rdo_gdmc_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_gdmc_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_GDMC,
	.label = "gdmc"
};

static const struct pinctrl_pin_desc google_rdo_cpm[] = {
	PINCTRL_PIN_TYPE(0, "XCPM_CLKBUF_0", PIN_TYPE_RESERVED),
	PINCTRL_PIN_TYPE(1, "XCPM_BP_PWROK", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(2, "XCPM_PWR_REQ", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(3, "XCPM_XnRESET", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(4, "XCPM_XnWRESET", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(5, "XCPM_XnRESET_OUT", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(6, "XCPM_IN_STBY_CLK", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(7, "XCPM_SPMI0_SCLK", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(8, "XCPM_SPMI0_SDATA", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(9, "XCPM_PRE_UVLO", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(10, "XCPM_PRE_OCP_TPU", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(11, "XCPM_EINT_0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(12, "XCPM_EINT_1", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(13, "XCPM_SOFT_PRE_OCP_TPU", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(14, "XCPM_PRE_OCP_CPU1", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(15, "XCPM_PRE_OCP_CPU2", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(16, "XCPM_SOFT_PRE_OCP_CPU1",
			 PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(17, "XCPM_SOFT_PRE_OCP_CPU2",
			 PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(18, "XCPM_PRE_OCP_CPU0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(19, "XCPM_SOFT_PRE_OCP_CPU0",
			 PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(20, "XCPM_EINT_2", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(21, "XCPM_EINT_3", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(22, "XCPM_CLKOUT0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(23, "XCPM_CLKOUT0_EN", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(24, "XCPM_CLKOUT1", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(25, "XCPM_CLKOUT1_EN", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(26, "XCPM_EINT_4", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(27, "XCPM_EINT_5", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(28, "XCPM_EINT_6", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(29, "XCPM_M0_BOOT_SEL", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(30, "XCPM_OTP_EMU_MODE", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(31, "XCPM_BOOT_CONFIG_SPARE",
			 PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(32, "XCPM_EINT_7", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(33, "XCPM_EINT_8", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(34, "XCPM_EINT_9", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(35, "XCPM_SPI0_SCLK", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(36, "XCPM_SPI0_MOSI", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(37, "XCPM_SPI0_MISO", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(38, "XCPM_SPI0_SSB", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(39, "XCPM_VDROOP1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(40, "XCPM_VDROOP2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(41, "XCPM_GPI0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(42, "XCPM_EINT_10", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(43, "XCPM_EINT_11", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(44, "XCPM_I2C0_SCL", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(45, "XCPM_I2C0_SDA", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(46, "XCPM_CLKBUF_ON", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(47, "XCPM_XOM_0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(48, "XCPM_GPI1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(49, "XCPM_EINT_12", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(50, "XCPM_EINT_13", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(51, "XCPM_SRC_OPT0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(52, "XCPM_SRC_OPT1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(53, "XCPM_SRC_OPT2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(54, "XCPM_EINT_14", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(55, "XCPM_EINT_15", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(56, "XCPM_EINT_16", PIN_DRV_4_BITS_NO_SLEW),
};

enum google_rdo_pinmux_cpm_functions {
	google_pinmux_cpm_clkbuf,
	google_pinmux_cpm_pwrok,
	google_pinmux_cpm_pwr,
	google_pinmux_cpm_reset,
	google_pinmux_cpm_stby,
	google_pinmux_cpm_int,
	google_pinmux_cpm_spmi,
	google_pinmux_cpm_pre,
	google_pinmux_cpm_irq,
	google_pinmux_cpm_clkout,
	google_pinmux_cpm_xtal,
	google_pinmux_cpm_uart0,
	google_pinmux_cpm_m0_uart,
	google_pinmux_cpm_boot,
	google_pinmux_cpm_otp_emu,
	google_pinmux_cpm_xom,
	google_pinmux_cpm_spi,
	google_pinmux_cpm_vdroop,
	google_pinmux_cpm_i2c,
	google_pinmux_cpm_src,
};

FUNCTION_GROUPS(cpm_clkbuf, "XCPM_CLKBUF_0", "XCPM_CLKBUF_ON");
FUNCTION_GROUPS(cpm_pwrok, "XCPM_BP_PWROK");
FUNCTION_GROUPS(cpm_pwr, "XCPM_PWR_REQ");
FUNCTION_GROUPS(cpm_reset, "XCPM_XnRESET", "XCPM_XnWRESET", "XCPM_XnRESET_OUT");
FUNCTION_GROUPS(cpm_stby, "XCPM_IN_STBY_CLK");
FUNCTION_GROUPS(cpm_int, "XCPM_SPMI0_SCLK", "XCPM_SPMI0_SDATA", "XCPM_PRE_UVLO",
		"XCPM_PRE_OCP_TPU", "XCPM_EINT_0", "XCPM_EINT_1",
		"XCPM_SOFT_PRE_OCP_TPU", "XCPM_PRE_OCP_CPU1",
		"XCPM_PRE_OCP_CPU2", "XCPM_SOFT_PRE_OCP_CPU1",
		"XCPM_SOFT_PRE_OCP_CPU2", "XCPM_PRE_OCP_CPU0",
		"XCPM_SOFT_PRE_OCP_CPU0", "XCPM_EINT_2", "XCPM_EINT_3",
		"XCPM_CLKOUT0", "XCPM_CLKOUT0_EN", "XCPM_CLKOUT1",
		"XCPM_CLKOUT1_EN", "XCPM_EINT_4", "XCPM_EINT_5", "XCPM_EINT_6",
		"XCPM_M0_BOOT_SEL", "XCPM_OTP_EMU_MODE",
		"XCPM_BOOT_CONFIG_SPARE", "XCPM_EINT_7", "XCPM_EINT_8",
		"XCPM_EINT_9", "XCPM_SPI0_SCLK", "XCPM_SPI0_MOSI",
		"XCPM_SPI0_MISO", "XCPM_SPI0_SSB", "XCPM_VDROOP1",
		"XCPM_VDROOP2", "XCPM_GPI0", "XCPM_EINT_10", "XCPM_EINT_11",
		"XCPM_I2C0_SCL", "XCPM_I2C0_SDA", "XCPM_CLKBUF_ON",
		"XCPM_XOM_0", "XCPM_GPI1", "XCPM_EINT_12", "XCPM_EINT_13",
		"XCPM_SRC_OPT0", "XCPM_SRC_OPT1", "XCPM_SRC_OPT2",
		"XCPM_EINT_14", "XCPM_EINT_15", "XCPM_EINT_16");
FUNCTION_GROUPS(cpm_spmi, "XCPM_SPMI0_SCLK", "XCPM_SPMI0_SDATA", "XCPM_EINT_3",
		"XCPM_EINT_4");
FUNCTION_GROUPS(cpm_pre, "XCPM_PRE_UVLO", "XCPM_PRE_OCP_TPU",
		"XCPM_SOFT_PRE_OCP_TPU", "XCPM_PRE_OCP_CPU1",
		"XCPM_PRE_OCP_CPU2", "XCPM_SOFT_PRE_OCP_CPU1",
		"XCPM_SOFT_PRE_OCP_CPU2", "XCPM_PRE_OCP_CPU0",
		"XCPM_SOFT_PRE_OCP_CPU0");
FUNCTION_GROUPS(cpm_irq, "XCPM_EINT_0", "XCPM_EINT_2");
FUNCTION_GROUPS(cpm_clkout, "XCPM_CLKOUT0", "XCPM_CLKOUT0_EN", "XCPM_CLKOUT1",
		"XCPM_CLKOUT1_EN");
FUNCTION_GROUPS(cpm_xtal, "XCPM_CLKOUT1");
FUNCTION_GROUPS(cpm_uart0, "XCPM_EINT_5", "XCPM_EINT_6");
FUNCTION_GROUPS(cpm_m0_uart, "XCPM_EINT_6");
FUNCTION_GROUPS(cpm_boot, "XCPM_M0_BOOT_SEL", "XCPM_BOOT_CONFIG_SPARE");
FUNCTION_GROUPS(cpm_otp_emu, "XCPM_OTP_EMU_MODE");
FUNCTION_GROUPS(cpm_xom, "XCPM_EINT_8", "XCPM_EINT_9", "XCPM_XOM_0",
		"XCPM_EINT_15");
FUNCTION_GROUPS(cpm_spi, "XCPM_SPI0_SCLK", "XCPM_SPI0_MOSI", "XCPM_SPI0_MISO",
		"XCPM_SPI0_SSB");
FUNCTION_GROUPS(cpm_vdroop, "XCPM_VDROOP1", "XCPM_VDROOP2");
FUNCTION_GROUPS(cpm_i2c, "XCPM_I2C0_SCL", "XCPM_I2C0_SDA");
FUNCTION_GROUPS(cpm_src, "XCPM_SRC_OPT0", "XCPM_SRC_OPT1", "XCPM_SRC_OPT2");

static const struct google_pingroup google_rdo_cpm_groups[] = {
	PIN_GROUP(0, "XCPM_CLKBUF_0", _, cpm_clkbuf, _, _, _, _, _, _, _),
	PIN_GROUP(1, "XCPM_BP_PWROK", _, cpm_pwrok, _, _, _, _, _, _, _),
	PIN_GROUP(2, "XCPM_PWR_REQ", _, cpm_pwr, _, _, _, _, _, _, _),
	PIN_GROUP(3, "XCPM_XnRESET", _, cpm_reset, _, _, _, _, _, _, _),
	PIN_GROUP(4, "XCPM_XnWRESET", _, cpm_reset, _, _, _, _, _, _, _),
	PIN_GROUP(5, "XCPM_XnRESET_OUT", _, cpm_reset, _, _, _, _, _, _, _),
	PIN_GROUP(6, "XCPM_IN_STBY_CLK", _, cpm_stby, _, _, _, _, _, _, _),
	PIN_GROUP(7, "XCPM_SPMI0_SCLK", cpm_int, _, cpm_spmi, _, _, _, _, _, _),
	PIN_GROUP(8, "XCPM_SPMI0_SDATA", cpm_int, _, cpm_spmi, _, _, _, _, _,
		  _),
	PIN_GROUP(9, "XCPM_PRE_UVLO", cpm_int, cpm_pre, _, _, _, _, _, _, _),
	PIN_GROUP(10, "XCPM_PRE_OCP_TPU", cpm_int, cpm_pre, _, _, _, _, _, _,
		  _),
	PIN_GROUP(11, "XCPM_EINT_0", cpm_int, cpm_irq, _, _, _, _, _, _, _),
	PIN_GROUP(12, "XCPM_EINT_1", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(13, "XCPM_SOFT_PRE_OCP_TPU", cpm_int, cpm_pre, _, _, _, _, _,
		  _, _),
	PIN_GROUP(14, "XCPM_PRE_OCP_CPU1", cpm_int, cpm_pre, _, _, _, _, _, _,
		  _),
	PIN_GROUP(15, "XCPM_PRE_OCP_CPU2", cpm_int, cpm_pre, _, _, _, _, _, _,
		  _),
	PIN_GROUP(16, "XCPM_SOFT_PRE_OCP_CPU1", cpm_int, cpm_pre, _, _, _, _, _,
		  _, _),
	PIN_GROUP(17, "XCPM_SOFT_PRE_OCP_CPU2", cpm_int, cpm_pre, _, _, _, _, _,
		  _, _),
	PIN_GROUP(18, "XCPM_PRE_OCP_CPU0", cpm_int, cpm_pre, _, _, _, _, _, _,
		  _),
	PIN_GROUP(19, "XCPM_SOFT_PRE_OCP_CPU0", cpm_int, cpm_pre, _, _, _, _, _,
		  _, _),
	PIN_GROUP(20, "XCPM_EINT_2", cpm_int, cpm_irq, _, _, _, _, _, _, _),
	PIN_GROUP(21, "XCPM_EINT_3", cpm_int, cpm_spmi, _, _, _, _, _, _, _),
	PIN_GROUP(22, "XCPM_CLKOUT0", cpm_int, cpm_clkout, _, _, _, _, _, _, _),
	PIN_GROUP(23, "XCPM_CLKOUT0_EN", cpm_int, cpm_clkout, _, _, _, _, _, _,
		  _),
	PIN_GROUP(24, "XCPM_CLKOUT1", cpm_int, cpm_clkout, cpm_xtal, _, _, _, _,
		  _, _),
	PIN_GROUP(25, "XCPM_CLKOUT1_EN", cpm_int, cpm_clkout, _, _, _, _, _, _,
		  _),
	PIN_GROUP(26, "XCPM_EINT_4", cpm_int, cpm_spmi, _, _, _, _, _, _, _),
	PIN_GROUP(27, "XCPM_EINT_5", cpm_int, _, cpm_uart0, _, _, _, _, _, _),
	PIN_GROUP(28, "XCPM_EINT_6", cpm_int, _, cpm_uart0, cpm_m0_uart, _, _,
		  _, _, _),
	PIN_GROUP(29, "XCPM_M0_BOOT_SEL", cpm_int, cpm_boot, _, _, _, _, _, _,
		  _),
	PIN_GROUP(30, "XCPM_OTP_EMU_MODE", cpm_int, cpm_otp_emu, _, _, _, _, _,
		  _, _),
	PIN_GROUP(31, "XCPM_BOOT_CONFIG_SPARE", cpm_int, cpm_boot, _, _, _, _,
		  _, _, _),
	PIN_GROUP(32, "XCPM_EINT_7", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(33, "XCPM_EINT_8", cpm_int, cpm_xom, _, _, _, _, _, _, _),
	PIN_GROUP(34, "XCPM_EINT_9", cpm_int, cpm_xom, _, _, _, _, _, _, _),
	PIN_GROUP(35, "XCPM_SPI0_SCLK", cpm_int, _, cpm_spi, _, _, _, _, _, _),
	PIN_GROUP(36, "XCPM_SPI0_MOSI", cpm_int, _, cpm_spi, _, _, _, _, _, _),
	PIN_GROUP(37, "XCPM_SPI0_MISO", cpm_int, _, cpm_spi, _, _, _, _, _, _),
	PIN_GROUP(38, "XCPM_SPI0_SSB", cpm_int, _, cpm_spi, _, _, _, _, _, _),
	PIN_GROUP(39, "XCPM_VDROOP1", cpm_int, cpm_vdroop, _, _, _, _, _, _, _),
	PIN_GROUP(40, "XCPM_VDROOP2", cpm_int, cpm_vdroop, _, _, _, _, _, _, _),
	PIN_GROUP(41, "XCPM_GPI0", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(42, "XCPM_EINT_10", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(43, "XCPM_EINT_11", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(44, "XCPM_I2C0_SCL", cpm_int, cpm_i2c, _, _, _, _, _, _, _),
	PIN_GROUP(45, "XCPM_I2C0_SDA", cpm_int, cpm_i2c, _, _, _, _, _, _, _),
	PIN_GROUP(46, "XCPM_CLKBUF_ON", cpm_int, cpm_clkbuf, _, _, _, _, _, _,
		  _),
	PIN_GROUP(47, "XCPM_XOM_0", cpm_int, cpm_xom, _, _, _, _, _, _, _),
	PIN_GROUP(48, "XCPM_GPI1", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(49, "XCPM_EINT_12", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(50, "XCPM_EINT_13", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(51, "XCPM_SRC_OPT0", cpm_int, cpm_src, _, _, _, _, _, _, _),
	PIN_GROUP(52, "XCPM_SRC_OPT1", cpm_int, cpm_src, _, _, _, _, _, _, _),
	PIN_GROUP(53, "XCPM_SRC_OPT2", cpm_int, cpm_src, _, _, _, _, _, _, _),
	PIN_GROUP(54, "XCPM_EINT_14", cpm_int, _, _, _, _, _, _, _, _),
	PIN_GROUP(55, "XCPM_EINT_15", cpm_int, cpm_xom, _, _, _, _, _, _, _),
	PIN_GROUP(56, "XCPM_EINT_16", cpm_int, _, _, _, _, _, _, _, _),
};

static const struct google_pin_function google_rdo_cpm_functions[] = {
	FUNCTION(cpm_clkbuf),  FUNCTION(cpm_pwrok), FUNCTION(cpm_pwr),
	FUNCTION(cpm_reset),   FUNCTION(cpm_stby),  FUNCTION(cpm_int),
	FUNCTION(cpm_spmi),    FUNCTION(cpm_pre),   FUNCTION(cpm_irq),
	FUNCTION(cpm_clkout),  FUNCTION(cpm_xtal),  FUNCTION(cpm_uart0),
	FUNCTION(cpm_m0_uart), FUNCTION(cpm_boot),  FUNCTION(cpm_otp_emu),
	FUNCTION(cpm_xom),     FUNCTION(cpm_spi),   FUNCTION(cpm_vdroop),
	FUNCTION(cpm_i2c),     FUNCTION(cpm_src),
};

#define MAX_NR_GPIO_CPM 57

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_cpm = {
	.pins = google_rdo_cpm,
	.num_pins = ARRAY_SIZE(google_rdo_cpm),
	.groups = google_rdo_cpm_groups,
	.num_groups = ARRAY_SIZE(google_rdo_cpm_groups),
	.funcs = google_rdo_cpm_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_cpm_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_CPM,
	.label = "cpm"
};

static const struct pinctrl_pin_desc google_rdo_lsios[] = {
	PINCTRL_PIN_TYPE(0, "XLSIOS_MCLK1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(1, "XLSIOS_MCLK2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(2, "XLSIOS_MCLK3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(3, "XLSIOS_MCLK4", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(4, "XLSIOS_MCLK5", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(5, "XLSIOS_MCLK6", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(6, "XLSIOS_MCLK7", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(7, "XLSIOS_CLI0_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(8, "XLSIOS_CLI0_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(9, "XLSIOS_CLI0_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(10, "XLSIOS_CLI0_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(11, "XLSIOS_CLI1_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(12, "XLSIOS_CLI1_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(13, "XLSIOS_CLI1_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(14, "XLSIOS_CLI1_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(15, "XLSIOS_CLI2_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(16, "XLSIOS_CLI2_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(17, "XLSIOS_CLI2_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(18, "XLSIOS_CLI2_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(19, "XLSIOS_CLI3_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(20, "XLSIOS_CLI3_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(21, "XLSIOS_CLI3_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(22, "XLSIOS_CLI3_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(23, "XLSIOS_PWM_0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(24, "XLSIOS_PWM_1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(25, "XLSIOS_VSYNC1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(26, "XLSIOS_VSYNC2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(27, "XLSIOS_VSYNC3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(28, "XLSIOS_VSYNC6", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(29, "XLSIOS_VSYNC7", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(30, "XLSIOS_PWM_2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(31, "XLSIOS_GPIO0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(32, "XLSIOS_GPIO1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(33, "XLSIOS_GPIO2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(34, "XLSIOS_CLI5_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(35, "XLSIOS_CLI5_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(36, "XLSIOS_CLI5_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(37, "XLSIOS_CLI5_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(38, "XLSIOS_GPIO3", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(39, "XLSIOS_MCLK0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(40, "XLSIOS_CLI4_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(41, "XLSIOS_CLI4_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(42, "XLSIOS_CLI4_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(43, "XLSIOS_CLI4_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(44, "XLSIOS_VSYNC0", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(45, "XLSIOS_GPIO4", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(46, "XLSIOS_GPIO5", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(47, "XLSIOS_PRE_OCP_GPU", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(48, "XLSIOS_SOFT_PRE_OCP_GPU",
			 PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(49, "XLSIOS_VSYNC5", PIN_DRV_3_BITS_WITH_SLEW),
	PINCTRL_PIN_TYPE(50, "XLSIOS_CLI6_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(51, "XLSIOS_CLI6_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(52, "XLSIOS_CLI6_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(53, "XLSIOS_CLI6_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(54, "XLSIOS_CAMERA_MUTE", PIN_DRV_3_BITS_WITH_SLEW),
};

enum google_rdo_pinmux_lsios_functions {
	google_pinmux_lsios_gpio,
	google_pinmux_lsios_mclk,
	google_pinmux_lsios_debug_mux,
	google_pinmux_lsios_cli0,
	google_pinmux_lsios_qspi0,
	google_pinmux_lsios_cli1,
	google_pinmux_lsios_pwm,
	google_pinmux_lsios_cli2,
	google_pinmux_lsios_cli3,
	google_pinmux_lsios_vsync,
	google_pinmux_lsios_cli5,
	google_pinmux_lsios_cli4,
	google_pinmux_lsios_pre_ocp_gpu,
	google_pinmux_lsios_soft_pre_ocp_gpu,
	google_pinmux_lsios_cli6,
	google_pinmux_lsios_camera_mute,
};

FUNCTION_GROUPS(lsios_gpio, "XLSIOS_MCLK1", "XLSIOS_MCLK2", "XLSIOS_MCLK3",
		"XLSIOS_MCLK4", "XLSIOS_MCLK5", "XLSIOS_MCLK6", "XLSIOS_MCLK7",
		"XLSIOS_CLI0_PIN0", "XLSIOS_CLI0_PIN1", "XLSIOS_CLI0_PIN2",
		"XLSIOS_CLI0_PIN3", "XLSIOS_CLI1_PIN0", "XLSIOS_CLI1_PIN1",
		"XLSIOS_CLI1_PIN2", "XLSIOS_CLI1_PIN3", "XLSIOS_CLI2_PIN0",
		"XLSIOS_CLI2_PIN1", "XLSIOS_CLI2_PIN2", "XLSIOS_CLI2_PIN3",
		"XLSIOS_CLI3_PIN0", "XLSIOS_CLI3_PIN1", "XLSIOS_CLI3_PIN2",
		"XLSIOS_CLI3_PIN3", "XLSIOS_PWM_0", "XLSIOS_PWM_1",
		"XLSIOS_VSYNC1", "XLSIOS_VSYNC2", "XLSIOS_VSYNC3",
		"XLSIOS_VSYNC6", "XLSIOS_VSYNC7", "XLSIOS_PWM_2",
		"XLSIOS_GPIO0", "XLSIOS_GPIO1", "XLSIOS_GPIO2",
		"XLSIOS_CLI5_PIN0", "XLSIOS_CLI5_PIN1", "XLSIOS_CLI5_PIN2",
		"XLSIOS_CLI5_PIN3", "XLSIOS_GPIO3", "XLSIOS_MCLK0",
		"XLSIOS_CLI4_PIN0", "XLSIOS_CLI4_PIN1", "XLSIOS_CLI4_PIN2",
		"XLSIOS_CLI4_PIN3", "XLSIOS_VSYNC0", "XLSIOS_GPIO4",
		"XLSIOS_GPIO5", "XLSIOS_PRE_OCP_GPU", "XLSIOS_SOFT_PRE_OCP_GPU",
		"XLSIOS_VSYNC5", "XLSIOS_CLI6_PIN0", "XLSIOS_CLI6_PIN1",
		"XLSIOS_CLI6_PIN2", "XLSIOS_CLI6_PIN3");
FUNCTION_GROUPS(lsios_mclk, "XLSIOS_MCLK1", "XLSIOS_MCLK2", "XLSIOS_MCLK3",
		"XLSIOS_MCLK4", "XLSIOS_MCLK5", "XLSIOS_MCLK6", "XLSIOS_MCLK7",
		"XLSIOS_MCLK0");
FUNCTION_GROUPS(lsios_debug_mux, "XLSIOS_MCLK2", "XLSIOS_MCLK3", "XLSIOS_MCLK4",
		"XLSIOS_MCLK5", "XLSIOS_MCLK6", "XLSIOS_MCLK7",
		"XLSIOS_CLI1_PIN2", "XLSIOS_CLI1_PIN3", "XLSIOS_CLI2_PIN0",
		"XLSIOS_CLI2_PIN1", "XLSIOS_CLI2_PIN2", "XLSIOS_CLI2_PIN3",
		"XLSIOS_CLI3_PIN0", "XLSIOS_CLI3_PIN1", "XLSIOS_CLI3_PIN2",
		"XLSIOS_CLI3_PIN3", "XLSIOS_PWM_1", "XLSIOS_VSYNC1",
		"XLSIOS_VSYNC2", "XLSIOS_VSYNC3", "XLSIOS_VSYNC6",
		"XLSIOS_VSYNC7", "XLSIOS_PWM_2", "XLSIOS_GPIO0", "XLSIOS_GPIO1",
		"XLSIOS_GPIO2", "XLSIOS_CLI4_PIN0", "XLSIOS_CLI4_PIN1",
		"XLSIOS_CLI4_PIN2", "XLSIOS_CLI4_PIN3", "XLSIOS_GPIO4",
		"XLSIOS_GPIO5");
FUNCTION_GROUPS(lsios_cli0, "XLSIOS_CLI0_PIN0", "XLSIOS_CLI0_PIN1",
		"XLSIOS_CLI0_PIN2", "XLSIOS_CLI0_PIN3");
FUNCTION_GROUPS(lsios_qspi0, "XLSIOS_CLI0_PIN0", "XLSIOS_CLI0_PIN1",
		"XLSIOS_CLI0_PIN2", "XLSIOS_CLI0_PIN3", "XLSIOS_CLI1_PIN0",
		"XLSIOS_CLI1_PIN1");
FUNCTION_GROUPS(lsios_cli1, "XLSIOS_CLI1_PIN0", "XLSIOS_CLI1_PIN1",
		"XLSIOS_CLI1_PIN2", "XLSIOS_CLI1_PIN3");
FUNCTION_GROUPS(lsios_pwm, "XLSIOS_CLI1_PIN2", "XLSIOS_PWM_0", "XLSIOS_PWM_1",
		"XLSIOS_PWM_2");
FUNCTION_GROUPS(lsios_cli2, "XLSIOS_CLI2_PIN0", "XLSIOS_CLI2_PIN1",
		"XLSIOS_CLI2_PIN2", "XLSIOS_CLI2_PIN3");
FUNCTION_GROUPS(lsios_cli3, "XLSIOS_CLI3_PIN0", "XLSIOS_CLI3_PIN1",
		"XLSIOS_CLI3_PIN2", "XLSIOS_CLI3_PIN3");
FUNCTION_GROUPS(lsios_vsync, "XLSIOS_VSYNC1", "XLSIOS_VSYNC2", "XLSIOS_VSYNC3",
		"XLSIOS_VSYNC6", "XLSIOS_VSYNC7", "XLSIOS_VSYNC0",
		"XLSIOS_VSYNC5");
FUNCTION_GROUPS(lsios_cli5, "XLSIOS_CLI5_PIN0", "XLSIOS_CLI5_PIN1",
		"XLSIOS_CLI5_PIN2", "XLSIOS_CLI5_PIN3");
FUNCTION_GROUPS(lsios_cli4, "XLSIOS_CLI4_PIN0", "XLSIOS_CLI4_PIN1",
		"XLSIOS_CLI4_PIN2", "XLSIOS_CLI4_PIN3");
FUNCTION_GROUPS(lsios_pre_ocp_gpu, "XLSIOS_PRE_OCP_GPU");
FUNCTION_GROUPS(lsios_soft_pre_ocp_gpu, "XLSIOS_SOFT_PRE_OCP_GPU");
FUNCTION_GROUPS(lsios_cli6, "XLSIOS_CLI6_PIN0", "XLSIOS_CLI6_PIN1",
		"XLSIOS_CLI6_PIN2", "XLSIOS_CLI6_PIN3");
FUNCTION_GROUPS(lsios_camera_mute, "XLSIOS_CAMERA_MUTE");

static const struct google_pingroup google_rdo_lsios_groups[] = {
	PIN_GROUP(0, "XLSIOS_MCLK1", lsios_gpio, lsios_mclk, _, _, _, _, _, _,
		  _),
	PIN_GROUP(1, "XLSIOS_MCLK2", lsios_gpio, lsios_mclk, _, lsios_debug_mux,
		  _, _, _, _, _),
	PIN_GROUP(2, "XLSIOS_MCLK3", lsios_gpio, lsios_mclk, _, lsios_debug_mux,
		  _, _, _, _, _),
	PIN_GROUP(3, "XLSIOS_MCLK4", lsios_gpio, lsios_mclk, _, lsios_debug_mux,
		  _, _, _, _, _),
	PIN_GROUP(4, "XLSIOS_MCLK5", lsios_gpio, lsios_mclk, _, lsios_debug_mux,
		  _, _, _, _, _),
	PIN_GROUP(5, "XLSIOS_MCLK6", lsios_gpio, lsios_mclk, _, lsios_debug_mux,
		  _, _, _, _, _),
	PIN_GROUP(6, "XLSIOS_MCLK7", lsios_gpio, lsios_mclk, _, lsios_debug_mux,
		  _, _, _, _, _),
	PIN_GROUP(7, "XLSIOS_CLI0_PIN0", lsios_gpio, lsios_cli0, lsios_qspi0, _,
		  _, _, _, _, _),
	PIN_GROUP(8, "XLSIOS_CLI0_PIN1", lsios_gpio, lsios_cli0, lsios_qspi0, _,
		  _, _, _, _, _),
	PIN_GROUP(9, "XLSIOS_CLI0_PIN2", lsios_gpio, lsios_cli0, lsios_qspi0, _,
		  _, _, _, _, _),
	PIN_GROUP(10, "XLSIOS_CLI0_PIN3", lsios_gpio, lsios_cli0, lsios_qspi0,
		  _, _, _, _, _, _),
	PIN_GROUP(11, "XLSIOS_CLI1_PIN0", lsios_gpio, lsios_cli1, lsios_qspi0,
		  _, _, _, _, _, _),
	PIN_GROUP(12, "XLSIOS_CLI1_PIN1", lsios_gpio, lsios_cli1, lsios_qspi0,
		  _, _, _, _, _, _),
	PIN_GROUP(13, "XLSIOS_CLI1_PIN2", lsios_gpio, lsios_cli1, lsios_pwm,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(14, "XLSIOS_CLI1_PIN3", lsios_gpio, lsios_cli1, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(15, "XLSIOS_CLI2_PIN0", lsios_gpio, lsios_cli2, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(16, "XLSIOS_CLI2_PIN1", lsios_gpio, lsios_cli2, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(17, "XLSIOS_CLI2_PIN2", lsios_gpio, lsios_cli2, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(18, "XLSIOS_CLI2_PIN3", lsios_gpio, lsios_cli2, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(19, "XLSIOS_CLI3_PIN0", lsios_gpio, lsios_cli3, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(20, "XLSIOS_CLI3_PIN1", lsios_gpio, lsios_cli3, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(21, "XLSIOS_CLI3_PIN2", lsios_gpio, lsios_cli3, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(22, "XLSIOS_CLI3_PIN3", lsios_gpio, lsios_cli3, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(23, "XLSIOS_PWM_0", lsios_gpio, lsios_pwm, _, _, _, _, _, _,
		  _),
	PIN_GROUP(24, "XLSIOS_PWM_1", lsios_gpio, lsios_pwm, _, lsios_debug_mux,
		  _, _, _, _, _),
	PIN_GROUP(25, "XLSIOS_VSYNC1", lsios_gpio, lsios_vsync, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(26, "XLSIOS_VSYNC2", lsios_gpio, lsios_vsync, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(27, "XLSIOS_VSYNC3", lsios_gpio, lsios_vsync, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(28, "XLSIOS_VSYNC6", lsios_gpio, lsios_vsync, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(29, "XLSIOS_VSYNC7", lsios_gpio, lsios_vsync, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(30, "XLSIOS_PWM_2", lsios_gpio, lsios_pwm, _, lsios_debug_mux,
		  _, _, _, _, _),
	PIN_GROUP(31, "XLSIOS_GPIO0", lsios_gpio, _, _, lsios_debug_mux, _, _,
		  _, _, _),
	PIN_GROUP(32, "XLSIOS_GPIO1", lsios_gpio, _, _, lsios_debug_mux, _, _,
		  _, _, _),
	PIN_GROUP(33, "XLSIOS_GPIO2", lsios_gpio, _, _, lsios_debug_mux, _, _,
		  _, _, _),
	PIN_GROUP(34, "XLSIOS_CLI5_PIN0", lsios_gpio, lsios_cli5, _, _, _, _, _,
		  _, _),
	PIN_GROUP(35, "XLSIOS_CLI5_PIN1", lsios_gpio, lsios_cli5, _, _, _, _, _,
		  _, _),
	PIN_GROUP(36, "XLSIOS_CLI5_PIN2", lsios_gpio, lsios_cli5, _, _, _, _, _,
		  _, _),
	PIN_GROUP(37, "XLSIOS_CLI5_PIN3", lsios_gpio, lsios_cli5, _, _, _, _, _,
		  _, _),
	PIN_GROUP(38, "XLSIOS_GPIO3", lsios_gpio, _, _, _, _, _, _, _, _),
	PIN_GROUP(39, "XLSIOS_MCLK0", lsios_gpio, lsios_mclk, _, _, _, _, _, _,
		  _),
	PIN_GROUP(40, "XLSIOS_CLI4_PIN0", lsios_gpio, lsios_cli4, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(41, "XLSIOS_CLI4_PIN1", lsios_gpio, lsios_cli4, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(42, "XLSIOS_CLI4_PIN2", lsios_gpio, lsios_cli4, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(43, "XLSIOS_CLI4_PIN3", lsios_gpio, lsios_cli4, _,
		  lsios_debug_mux, _, _, _, _, _),
	PIN_GROUP(44, "XLSIOS_VSYNC0", lsios_gpio, lsios_vsync, _, _, _, _, _,
		  _, _),
	PIN_GROUP(45, "XLSIOS_GPIO4", lsios_gpio, _, _, lsios_debug_mux, _, _,
		  _, _, _),
	PIN_GROUP(46, "XLSIOS_GPIO5", lsios_gpio, _, _, lsios_debug_mux, _, _,
		  _, _, _),
	PIN_GROUP(47, "XLSIOS_PRE_OCP_GPU", lsios_gpio, lsios_pre_ocp_gpu, _, _,
		  _, _, _, _, _),
	PIN_GROUP(48, "XLSIOS_SOFT_PRE_OCP_GPU", lsios_gpio,
		  lsios_soft_pre_ocp_gpu, _, _, _, _, _, _, _),
	PIN_GROUP(49, "XLSIOS_VSYNC5", lsios_gpio, lsios_vsync, _, _, _, _, _,
		  _, _),
	PIN_GROUP(50, "XLSIOS_CLI6_PIN0", lsios_gpio, lsios_cli6, _, _, _, _, _,
		  _, _),
	PIN_GROUP(51, "XLSIOS_CLI6_PIN1", lsios_gpio, lsios_cli6, _, _, _, _, _,
		  _, _),
	PIN_GROUP(52, "XLSIOS_CLI6_PIN2", lsios_gpio, lsios_cli6, _, _, _, _, _,
		  _, _),
	PIN_GROUP(53, "XLSIOS_CLI6_PIN3", lsios_gpio, lsios_cli6, _, _, _, _, _,
		  _, _),
	PIN_GROUP(54, "XLSIOS_CAMERA_MUTE", _, lsios_camera_mute, _, _, _, _, _,
		  _, _),
};

static const struct google_pin_function google_rdo_lsios_functions[] = {
	FUNCTION(lsios_gpio),	     FUNCTION(lsios_mclk),
	FUNCTION(lsios_debug_mux),   FUNCTION(lsios_cli0),
	FUNCTION(lsios_qspi0),	     FUNCTION(lsios_cli1),
	FUNCTION(lsios_pwm),	     FUNCTION(lsios_cli2),
	FUNCTION(lsios_cli3),	     FUNCTION(lsios_vsync),
	FUNCTION(lsios_cli5),	     FUNCTION(lsios_cli4),
	FUNCTION(lsios_pre_ocp_gpu), FUNCTION(lsios_soft_pre_ocp_gpu),
	FUNCTION(lsios_cli6),	     FUNCTION(lsios_camera_mute),
};

#define MAX_NR_GPIO_LSIOS 55

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_lsios = {
	.pins = google_rdo_lsios,
	.num_pins = ARRAY_SIZE(google_rdo_lsios),
	.groups = google_rdo_lsios_groups,
	.num_groups = ARRAY_SIZE(google_rdo_lsios_groups),
	.funcs = google_rdo_lsios_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_lsios_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_LSIOS,
	.label = "lsios"
};

static const struct pinctrl_pin_desc google_rdo_lsioe[] = {
	PINCTRL_PIN_TYPE(0, "XLSIOE_CLI7_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(1, "XLSIOE_CLI7_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(2, "XLSIOE_CLI7_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(3, "XLSIOE_CLI7_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(4, "XLSIOE_CLI8_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(5, "XLSIOE_CLI8_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(6, "XLSIOE_CLI8_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(7, "XLSIOE_CLI8_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(8, "XLSIOE_CLI13_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(9, "XLSIOE_CLI13_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(10, "XLSIOE_CLI13_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(11, "XLSIOE_CLI13_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(12, "XLSIOE_CLI9_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(13, "XLSIOE_CLI9_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(14, "XLSIOE_CLI9_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(15, "XLSIOE_CLI9_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(16, "XLSIOE_CLI10_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(17, "XLSIOE_CLI10_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(18, "XLSIOE_CLI10_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(19, "XLSIOE_CLI10_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(20, "XLSIOE_CLI11_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(21, "XLSIOE_CLI11_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(22, "XLSIOE_CLI11_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(23, "XLSIOE_CLI11_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(24, "XLSIOE_CLI12_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(25, "XLSIOE_CLI12_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(26, "XLSIOE_CLI12_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(27, "XLSIOE_CLI12_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(28, "XLSIOE_CLI14_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(29, "XLSIOE_CLI14_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(30, "XLSIOE_CLI14_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(31, "XLSIOE_CLI14_PIN3", PIN_DRV_4_BITS_NO_SLEW),
};

enum google_rdo_pinmux_lsioe_functions {
	google_pinmux_lsioe_gpio,
	google_pinmux_lsioe_cli7,
	google_pinmux_lsioe_cli8,
	google_pinmux_lsioe_cli13,
	google_pinmux_lsioe_cli9,
	google_pinmux_lsioe_cli10,
	google_pinmux_lsioe_cli11,
	google_pinmux_lsioe_cli12,
	google_pinmux_lsioe_cli14,
};

FUNCTION_GROUPS(lsioe_gpio, "XLSIOE_CLI7_PIN0", "XLSIOE_CLI7_PIN1",
		"XLSIOE_CLI7_PIN2", "XLSIOE_CLI7_PIN3", "XLSIOE_CLI8_PIN0",
		"XLSIOE_CLI8_PIN1", "XLSIOE_CLI8_PIN2", "XLSIOE_CLI8_PIN3",
		"XLSIOE_CLI13_PIN0", "XLSIOE_CLI13_PIN1", "XLSIOE_CLI13_PIN2",
		"XLSIOE_CLI13_PIN3", "XLSIOE_CLI9_PIN0", "XLSIOE_CLI9_PIN1",
		"XLSIOE_CLI9_PIN2", "XLSIOE_CLI9_PIN3", "XLSIOE_CLI10_PIN0",
		"XLSIOE_CLI10_PIN1", "XLSIOE_CLI10_PIN2", "XLSIOE_CLI10_PIN3",
		"XLSIOE_CLI11_PIN0", "XLSIOE_CLI11_PIN1", "XLSIOE_CLI11_PIN2",
		"XLSIOE_CLI11_PIN3", "XLSIOE_CLI12_PIN0", "XLSIOE_CLI12_PIN1",
		"XLSIOE_CLI12_PIN2", "XLSIOE_CLI12_PIN3", "XLSIOE_CLI14_PIN0",
		"XLSIOE_CLI14_PIN1", "XLSIOE_CLI14_PIN2", "XLSIOE_CLI14_PIN3");
FUNCTION_GROUPS(lsioe_cli7, "XLSIOE_CLI7_PIN0", "XLSIOE_CLI7_PIN1",
		"XLSIOE_CLI7_PIN2", "XLSIOE_CLI7_PIN3");
FUNCTION_GROUPS(lsioe_cli8, "XLSIOE_CLI8_PIN0", "XLSIOE_CLI8_PIN1",
		"XLSIOE_CLI8_PIN2", "XLSIOE_CLI8_PIN3");
FUNCTION_GROUPS(lsioe_cli13, "XLSIOE_CLI13_PIN0", "XLSIOE_CLI13_PIN1",
		"XLSIOE_CLI13_PIN2", "XLSIOE_CLI13_PIN3");
FUNCTION_GROUPS(lsioe_cli9, "XLSIOE_CLI9_PIN0", "XLSIOE_CLI9_PIN1",
		"XLSIOE_CLI9_PIN2", "XLSIOE_CLI9_PIN3");
FUNCTION_GROUPS(lsioe_cli10, "XLSIOE_CLI10_PIN0", "XLSIOE_CLI10_PIN1",
		"XLSIOE_CLI10_PIN2", "XLSIOE_CLI10_PIN3");
FUNCTION_GROUPS(lsioe_cli11, "XLSIOE_CLI11_PIN0", "XLSIOE_CLI11_PIN1",
		"XLSIOE_CLI11_PIN2", "XLSIOE_CLI11_PIN3");
FUNCTION_GROUPS(lsioe_cli12, "XLSIOE_CLI12_PIN0", "XLSIOE_CLI12_PIN1",
		"XLSIOE_CLI12_PIN2", "XLSIOE_CLI12_PIN3");
FUNCTION_GROUPS(lsioe_cli14, "XLSIOE_CLI14_PIN0", "XLSIOE_CLI14_PIN1",
		"XLSIOE_CLI14_PIN2", "XLSIOE_CLI14_PIN3");

static const struct google_pingroup google_rdo_lsioe_groups[] = {
	PIN_GROUP(0, "XLSIOE_CLI7_PIN0", lsioe_gpio, lsioe_cli7, _, _, _, _, _,
		  _, _),
	PIN_GROUP(1, "XLSIOE_CLI7_PIN1", lsioe_gpio, lsioe_cli7, _, _, _, _, _,
		  _, _),
	PIN_GROUP(2, "XLSIOE_CLI7_PIN2", lsioe_gpio, lsioe_cli7, _, _, _, _, _,
		  _, _),
	PIN_GROUP(3, "XLSIOE_CLI7_PIN3", lsioe_gpio, lsioe_cli7, _, _, _, _, _,
		  _, _),
	PIN_GROUP(4, "XLSIOE_CLI8_PIN0", lsioe_gpio, lsioe_cli8, _, _, _, _, _,
		  _, _),
	PIN_GROUP(5, "XLSIOE_CLI8_PIN1", lsioe_gpio, lsioe_cli8, _, _, _, _, _,
		  _, _),
	PIN_GROUP(6, "XLSIOE_CLI8_PIN2", lsioe_gpio, lsioe_cli8, _, _, _, _, _,
		  _, _),
	PIN_GROUP(7, "XLSIOE_CLI8_PIN3", lsioe_gpio, lsioe_cli8, _, _, _, _, _,
		  _, _),
	PIN_GROUP(8, "XLSIOE_CLI13_PIN0", lsioe_gpio, lsioe_cli13, _, _, _, _,
		  _, _, _),
	PIN_GROUP(9, "XLSIOE_CLI13_PIN1", lsioe_gpio, lsioe_cli13, _, _, _, _,
		  _, _, _),
	PIN_GROUP(10, "XLSIOE_CLI13_PIN2", lsioe_gpio, lsioe_cli13, _, _, _, _,
		  _, _, _),
	PIN_GROUP(11, "XLSIOE_CLI13_PIN3", lsioe_gpio, lsioe_cli13, _, _, _, _,
		  _, _, _),
	PIN_GROUP(12, "XLSIOE_CLI9_PIN0", lsioe_gpio, lsioe_cli9, _, _, _, _, _,
		  _, _),
	PIN_GROUP(13, "XLSIOE_CLI9_PIN1", lsioe_gpio, lsioe_cli9, _, _, _, _, _,
		  _, _),
	PIN_GROUP(14, "XLSIOE_CLI9_PIN2", lsioe_gpio, lsioe_cli9, _, _, _, _, _,
		  _, _),
	PIN_GROUP(15, "XLSIOE_CLI9_PIN3", lsioe_gpio, lsioe_cli9, _, _, _, _, _,
		  _, _),
	PIN_GROUP(16, "XLSIOE_CLI10_PIN0", lsioe_gpio, lsioe_cli10, _, _, _, _,
		  _, _, _),
	PIN_GROUP(17, "XLSIOE_CLI10_PIN1", lsioe_gpio, lsioe_cli10, _, _, _, _,
		  _, _, _),
	PIN_GROUP(18, "XLSIOE_CLI10_PIN2", lsioe_gpio, lsioe_cli10, _, _, _, _,
		  _, _, _),
	PIN_GROUP(19, "XLSIOE_CLI10_PIN3", lsioe_gpio, lsioe_cli10, _, _, _, _,
		  _, _, _),
	PIN_GROUP(20, "XLSIOE_CLI11_PIN0", lsioe_gpio, lsioe_cli11, _, _, _, _,
		  _, _, _),
	PIN_GROUP(21, "XLSIOE_CLI11_PIN1", lsioe_gpio, lsioe_cli11, _, _, _, _,
		  _, _, _),
	PIN_GROUP(22, "XLSIOE_CLI11_PIN2", lsioe_gpio, lsioe_cli11, _, _, _, _,
		  _, _, _),
	PIN_GROUP(23, "XLSIOE_CLI11_PIN3", lsioe_gpio, lsioe_cli11, _, _, _, _,
		  _, _, _),
	PIN_GROUP(24, "XLSIOE_CLI12_PIN0", lsioe_gpio, lsioe_cli12, _, _, _, _,
		  _, _, _),
	PIN_GROUP(25, "XLSIOE_CLI12_PIN1", lsioe_gpio, lsioe_cli12, _, _, _, _,
		  _, _, _),
	PIN_GROUP(26, "XLSIOE_CLI12_PIN2", lsioe_gpio, lsioe_cli12, _, _, _, _,
		  _, _, _),
	PIN_GROUP(27, "XLSIOE_CLI12_PIN3", lsioe_gpio, lsioe_cli12, _, _, _, _,
		  _, _, _),
	PIN_GROUP(28, "XLSIOE_CLI14_PIN0", lsioe_gpio, lsioe_cli14, _, _, _, _,
		  _, _, _),
	PIN_GROUP(29, "XLSIOE_CLI14_PIN1", lsioe_gpio, lsioe_cli14, _, _, _, _,
		  _, _, _),
	PIN_GROUP(30, "XLSIOE_CLI14_PIN2", lsioe_gpio, lsioe_cli14, _, _, _, _,
		  _, _, _),
	PIN_GROUP(31, "XLSIOE_CLI14_PIN3", lsioe_gpio, lsioe_cli14, _, _, _, _,
		  _, _, _),
};

static const struct google_pin_function google_rdo_lsioe_functions[] = {
	FUNCTION(lsioe_gpio),  FUNCTION(lsioe_cli7),  FUNCTION(lsioe_cli8),
	FUNCTION(lsioe_cli13), FUNCTION(lsioe_cli9),  FUNCTION(lsioe_cli10),
	FUNCTION(lsioe_cli11), FUNCTION(lsioe_cli12), FUNCTION(lsioe_cli14),
};

#define MAX_NR_GPIO_LSIOE 32

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_lsioe = {
	.pins = google_rdo_lsioe,
	.num_pins = ARRAY_SIZE(google_rdo_lsioe),
	.groups = google_rdo_lsioe_groups,
	.num_groups = ARRAY_SIZE(google_rdo_lsioe_groups),
	.funcs = google_rdo_lsioe_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_lsioe_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_LSIOE,
	.label = "lsioe"
};

static const struct pinctrl_pin_desc google_rdo_lsion[] = {
	PINCTRL_PIN_TYPE(0, "XLSION_CLI15_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(1, "XLSION_CLI15_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(2, "XLSION_CLI15_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(3, "XLSION_CLI15_PIN3", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(4, "XLSION_CLI16_PIN0", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(5, "XLSION_CLI16_PIN1", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(6, "XLSION_CLI16_PIN2", PIN_DRV_4_BITS_NO_SLEW),
	PINCTRL_PIN_TYPE(7, "XLSION_CLI16_PIN3", PIN_DRV_4_BITS_NO_SLEW),
};

enum google_rdo_pinmux_lsion_functions {
	google_pinmux_lsion_gpio,
	google_pinmux_lsion_cli15,
	google_pinmux_lsion_cli16,
};

FUNCTION_GROUPS(lsion_gpio, "XLSION_CLI15_PIN0", "XLSION_CLI15_PIN1",
		"XLSION_CLI15_PIN2", "XLSION_CLI15_PIN3", "XLSION_CLI16_PIN0",
		"XLSION_CLI16_PIN1", "XLSION_CLI16_PIN2", "XLSION_CLI16_PIN3");
FUNCTION_GROUPS(lsion_cli15, "XLSION_CLI15_PIN0", "XLSION_CLI15_PIN1",
		"XLSION_CLI15_PIN2", "XLSION_CLI15_PIN3");
FUNCTION_GROUPS(lsion_cli16, "XLSION_CLI16_PIN0", "XLSION_CLI16_PIN1",
		"XLSION_CLI16_PIN2", "XLSION_CLI16_PIN3");

static const struct google_pingroup google_rdo_lsion_groups[] = {
	PIN_GROUP(0, "XLSION_CLI15_PIN0", lsion_gpio, lsion_cli15, _, _, _, _,
		  _, _, _),
	PIN_GROUP(1, "XLSION_CLI15_PIN1", lsion_gpio, lsion_cli15, _, _, _, _,
		  _, _, _),
	PIN_GROUP(2, "XLSION_CLI15_PIN2", lsion_gpio, lsion_cli15, _, _, _, _,
		  _, _, _),
	PIN_GROUP(3, "XLSION_CLI15_PIN3", lsion_gpio, lsion_cli15, _, _, _, _,
		  _, _, _),
	PIN_GROUP(4, "XLSION_CLI16_PIN0", lsion_gpio, lsion_cli16, _, _, _, _,
		  _, _, _),
	PIN_GROUP(5, "XLSION_CLI16_PIN1", lsion_gpio, lsion_cli16, _, _, _, _,
		  _, _, _),
	PIN_GROUP(6, "XLSION_CLI16_PIN2", lsion_gpio, lsion_cli16, _, _, _, _,
		  _, _, _),
	PIN_GROUP(7, "XLSION_CLI16_PIN3", lsion_gpio, lsion_cli16, _, _, _, _,
		  _, _, _),
};

static const struct google_pin_function google_rdo_lsion_functions[] = {
	FUNCTION(lsion_gpio),
	FUNCTION(lsion_cli15),
	FUNCTION(lsion_cli16),
};

#define MAX_NR_GPIO_LSION 8

static const struct google_pinctrl_soc_sswrp_info google_rdo_pinctrl_lsion = {
	.pins = google_rdo_lsion,
	.num_pins = ARRAY_SIZE(google_rdo_lsion),
	.groups = google_rdo_lsion_groups,
	.num_groups = ARRAY_SIZE(google_rdo_lsion_groups),
	.funcs = google_rdo_lsion_functions,
	.num_funcs = ARRAY_SIZE(google_rdo_lsion_functions),
	.gpio_func = GPIO_FUNC_BIT_POS,
	.num_gpios = MAX_NR_GPIO_LSION,
	.label = "lsion"
};

static const struct of_device_id google_rdo_of_match[] = {
	{ .compatible = "google,rdo-hsion-pinctrl",
	  .data = &google_rdo_pinctrl_hsion },
	{ .compatible = "google,rdo-hsios-pinctrl",
	  .data = &google_rdo_pinctrl_hsios },
	{ .compatible = "google,rdo-dpu-pinctrl",
	  .data = &google_rdo_pinctrl_dpu },
	{ .compatible = "google,rdo-aoc-pinctrl",
	  .data = &google_rdo_pinctrl_aoc },
	{ .compatible = "google,rdo-gdmc-pinctrl",
	  .data = &google_rdo_pinctrl_gdmc },
	{ .compatible = "google,rdo-cpm-pinctrl",
	  .data = &google_rdo_pinctrl_cpm },
	{ .compatible = "google,rdo-lsios-pinctrl",
	  .data = &google_rdo_pinctrl_lsios },
	{ .compatible = "google,rdo-lsioe-pinctrl",
	  .data = &google_rdo_pinctrl_lsioe },
	{ .compatible = "google,rdo-lsion-pinctrl",
	  .data = &google_rdo_pinctrl_lsion },
	{}
};

static int google_rdo_pinctrl_probe(struct platform_device *pdev)
{
	return google_pinctrl_probe(pdev, google_rdo_of_match);
}

static struct platform_driver google_rdo_pinctrl_driver = {
	.driver = {
		.name = "google_rdo_pinctrl",
		.of_match_table = google_rdo_of_match,
#ifdef CONFIG_PM
		.pm = &google_pinctrl_pm_ops,
#endif
	},
	.probe = google_rdo_pinctrl_probe,
	.remove = google_pinctrl_remove,
};

static int __init google_rdo_pinctrl_init(void)
{
	return platform_driver_register(&google_rdo_pinctrl_driver);
}
arch_initcall(google_rdo_pinctrl_init);

static void __exit google_rdo_pinctrl_exit(void)
{
	platform_driver_unregister(&google_rdo_pinctrl_driver);
}
module_exit(google_rdo_pinctrl_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google PINCTRL Driver");
MODULE_LICENSE("GPL");
