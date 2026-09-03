// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */
#include "ebu_platform.h"

#define EBU_CFG 0x0
#define EBU_TID_TRACE 0x4
#define EBU_TRACE_TIMEOUT 0x8
#define EBU_IN_EP_MAP_LOW 0x1c
#define EBU_IN_EP_MAP_HIGH 0x20

#define EBU_SUPER_SPEED 0
#define EBU_HIGH_SPEED 1

#define EBU_TRACE_TIMEOUT_VAL 1000

/**
 * union ebu_cfg
 * @speed: Connected USB controller is running in HS mode. EBU only works
 *	with HS and SS
 * @trace_timeout_en: Timeout behaviour. When enabled, the trace_timeout value
 *	is meaningful
 * @lossy_mode: Whether EBU will drop packets that when the FIFO is full
 * @internal_retry: Whether EBU attempts to retransmit or relies on the USB
 *	controller's retransmission logic
 */
union ebu_cfg {
	uint32_t value;
	struct {
		uint32_t speed : 1;
		uint32_t trace_timeout_en : 1;
		uint32_t lossy_mode : 1;
		uint32_t internal_retry : 1;
	} __packed;
};

int lga_google_ebu_setup(struct google_ebu *gebu, struct platform_device *pdev)
{
	struct resource *fifo_res;

	dev_dbg(gebu->dev, "LGA ebu setup\n");

	gebu->csr_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gebu->csr_base)) {
		dev_err(gebu->dev, "Couldn't Remap EBU CSRs\n");
		return PTR_ERR(gebu->csr_base);
	}

	fifo_res =
		platform_get_resource_byname(pdev, IORESOURCE_MEM, "ebu_fifo");
	if (!fifo_res) {
		dev_err(gebu->dev, "No FIFO specified for EBU\n");
		return -ENODEV;
	}
	gebu->trace_fifo_base = (dma_addr_t)fifo_res->start;
	gebu->ud_fifo_base = 0;

	gebu->ebu_cfg = ((union ebu_cfg){
				 .speed = EBU_SUPER_SPEED,
				 .trace_timeout_en = false,
				 .lossy_mode = true,
				 .internal_retry = true,
			 }).value;

	return 0;
}

int lga_reinit_ebu(struct google_ebu *gebu, enum usb_device_speed speed)
{
	union ebu_cfg cfg;

	cfg.value = gebu->ebu_cfg;

	switch (speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		cfg.speed = EBU_SUPER_SPEED;
		break;
	case USB_SPEED_HIGH:
		cfg.speed = EBU_HIGH_SPEED;
		break;
	default:
		dev_err(gebu->dev, "Enexpected speed in reinit\n");
		return -EINVAL;
	}
	ebu_writel(gebu, EBU_TID_TRACE, gebu->trace_tid);
	ebu_writel(gebu, EBU_TRACE_TIMEOUT, EBU_TRACE_TIMEOUT_VAL);
	ebu_writel(gebu, EBU_CFG, cfg.value);
	// LGA only supports IN direction
	ebu_writel(gebu, EBU_IN_EP_MAP_LOW, (uint32_t)gebu->ep_map[1]);
	ebu_writel(gebu, EBU_IN_EP_MAP_HIGH, gebu->ep_map[1] >> 32);
	return 0;
}
