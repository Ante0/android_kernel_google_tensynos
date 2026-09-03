// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */
 #include "ebu_platform.h"
#include <linux/module.h>

#define EBU_CFG 0x8
#define INEP_INDEX_MAP_ENUM_VAL 0xC
#define OUTEP_INDEX_MAP_ENUM_VAL 0x10
#define EBU_TRACE_TIMEOUT 0x14
#define TRACE_CONTROL_WORD_LOW 0x18
#define TRACE_CONTROL_WORD_HIGH 0x1C
#define UD_EBC_SRAM 0x30
#define UD_IEBC_TIMEOUT 0x34

// This isn't a typo. Polarity is swapped in MBU
#define EBU_SUPER_SPEED 1
#define EBU_HIGH_SPEED 0

// HPG mentioned 500 but it is too low based on b/408034721
#define EBU_TRACE_TIMEOUT_VAL 0xfffffffe

static uint32_t trace_control_word_low = 0x7fff7fff;
static uint32_t trace_control_word_high = 0x7fff7fff;

module_param(trace_control_word_low, uint, 0444);
module_param(trace_control_word_high, uint, 0444);

/**
 * union ebu_cfg
 * @high_super_speed: Connected USB controller is running in SS mode.
 * EBU only works with HS and SS
 * @trace_timeout_en: Timeout behaviour. When enabled, the trace_timeout
 * value is meaningful
 * @internal_retry: Whether EBU attempts to retransmit or relies on
 * the USB controller's retransmission logic
 */
union ebu_cfg {
	uint32_t value;
	struct {
		uint32_t high_super_speed : 1;
		uint32_t trace_timeout_en : 1;
		uint32_t internal_retry : 1;
	} __packed;
};

/**
 * union ud_iebc_timeout
 * @val: Timeout value
 * @en: Timeout enable
 */
union ud_iebc_timeout {
	uint32_t value;
	struct {
		uint32_t val : 16;
		uint32_t en : 1;
	} __packed;
};

/**
 * union inep_index_map_enum_val
 * @trace: Trace ep num
 * @ud: UD ep num
 * @hsat: HSAT ep num
 */
union inep_index_map_enum_val {
	uint32_t value;
	struct {
		uint32_t trace : 4;
		uint32_t ud : 4;
		uint32_t hsat : 4;
	} __packed;
};

/**
 * union outep_index_map_enum_val
 * @ud: UD ep num
 * @hsat: HSAT ep num
 */
union outep_index_map_enum_val {
	uint32_t value;
	struct {
		uint32_t ud : 4;
		uint32_t hsat : 4;
	} __packed;
};

int mbu_google_ebu_setup(struct google_ebu *gebu, struct platform_device *pdev)
{
	struct resource *res;

	dev_dbg(gebu->dev, "MBU ebu setup\n");
	gebu->ebu_core_clk = devm_clk_get(gebu->dev, "ebu_core_clk");
	if (IS_ERR(gebu->ebu_core_clk)) {
		dev_err(gebu->dev, "EBU core clk failed %ld\n",
			PTR_ERR(gebu->ebu_core_clk));
		return PTR_ERR(gebu->ebu_core_clk);
	}

	gebu->ebu_core_rst =
		devm_reset_control_get_exclusive(gebu->dev, "ebu_core_rst");
	if (IS_ERR(gebu->ebu_core_rst)) {
		dev_err(gebu->dev, "EBU core rst failed %ld\n",
			PTR_ERR(gebu->ebu_core_rst));
		return PTR_ERR(gebu->ebu_core_rst);
	}

	gebu->csr_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gebu->csr_base)) {
		dev_err(gebu->dev, "Couldn't Remap EBU CSRs\n");
		return PTR_ERR(gebu->csr_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "ebu_trace_iebc_fifo");
	if (!res) {
		dev_err(gebu->dev, "No trace FIFO specified for EBU\n");
		return -ENODEV;
	}
	gebu->trace_fifo_base = (dma_addr_t)res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "ebu_ud_ebc_fifos");
	if (!res) {
		dev_err(gebu->dev, "No FIFO specified for EBU\n");
		return -ENODEV;
	}
	gebu->ud_fifo_base = (dma_addr_t)res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "ebu_secure_csr");
	if (!res) {
		dev_err(gebu->dev, "Failed to get ebu_secure_csr resource\n");
		return -ENODEV;
	}

	gebu->secure_csr_base = devm_ioremap_resource(gebu->dev, res);
	if (IS_ERR(gebu->secure_csr_base)) {
		dev_err(gebu->dev,
			"Failed to ioremap ebu_secure_csr resource\n");
		return PTR_ERR(gebu->secure_csr_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "ebu_m0p_sram");
	if (!res) {
		dev_err(gebu->dev, "Failed to get ebu_m0p_sram resource\n");
		return -ENODEV;
	}

	gebu->m0p_sram_base = devm_ioremap_resource(gebu->dev, res);
	if (IS_ERR(gebu->m0p_sram_base)) {
		dev_err(gebu->dev, "Failed to ioremap ebu_m0p_sram resource\n");
		return PTR_ERR(gebu->m0p_sram_base);
	}

	gebu->ebu_cfg = ((union ebu_cfg){
				 .high_super_speed = EBU_SUPER_SPEED,
				 .trace_timeout_en = 1,
				 .internal_retry = 1,
			 })
				.value;

	return 0;
}

int mbu_reinit_ebu(struct google_ebu *gebu, enum usb_device_speed speed)
{
	union ebu_cfg cfg;
	union inep_index_map_enum_val inep_map;
	union outep_index_map_enum_val outep_map;

	cfg.value = gebu->ebu_cfg;
	switch (speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		cfg.high_super_speed = EBU_SUPER_SPEED;
		break;
	case USB_SPEED_HIGH:
		cfg.high_super_speed = EBU_HIGH_SPEED;
		break;
	default:
		dev_err(gebu->dev, "Unexpected speed in reinit\n");
		return -EINVAL;
	}
	ebu_writel(gebu, EBU_TRACE_TIMEOUT, EBU_TRACE_TIMEOUT_VAL);
	ebu_writel(gebu, EBU_CFG, cfg.value);
	ebu_writel(gebu, TRACE_CONTROL_WORD_LOW, trace_control_word_low);
	ebu_writel(gebu, TRACE_CONTROL_WORD_HIGH, trace_control_word_high);

	union ud_iebc_timeout timeout;

	timeout.value = ebu_readl(gebu, UD_IEBC_TIMEOUT);
	timeout.val = 0xffff;
	timeout.en = 0x1;
	ebu_writel(gebu, UD_IEBC_TIMEOUT, timeout.value);

	// If it is HSAT then UD_OR_HSAT = 0x1. Programming it to 0x0 as
	// HSAT will not be used in linux
	ebu_writel(gebu, UD_EBC_SRAM, 0x0);

	// Assuming this is what the HPG is trying to say
	inep_map.value = ebu_readl(gebu, INEP_INDEX_MAP_ENUM_VAL);
	inep_map.trace = ((gebu->ch_map[1]) >> (4 * EBU_TRACE_CHANNEL)) & 0xf;
	inep_map.ud = ((gebu->ch_map[1]) >> (4 * EBU_UD_CHANNEL)) & 0xf;
	inep_map.hsat = ((gebu->ch_map[1]) >> (4 * EBU_LAST_CHANNEL)) & 0xf;
	ebu_writel(gebu, INEP_INDEX_MAP_ENUM_VAL, inep_map.value);

	outep_map.value = ebu_readl(gebu, OUTEP_INDEX_MAP_ENUM_VAL);
	outep_map.ud = ((gebu->ch_map[0]) >> (4 * EBU_UD_CHANNEL)) & 0xf;
	outep_map.hsat = ((gebu->ch_map[0]) >> (4 * EBU_LAST_CHANNEL)) & 0xf;
	ebu_writel(gebu, OUTEP_INDEX_MAP_ENUM_VAL, outep_map.value);

	return 0;
}
