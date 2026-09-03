/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC
 */
#ifndef _EBU_GOOGLE_H
#define _EBU_GOOGLE_H

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/phy/phy.h>
#include <linux/usb.h>
#include <ebu/ebu.h>

#define MAX_CHANNELS 16

/** enum ebu_state
 * @EBU_STOPPED: Inactive
 * @EBU_RUNNING: Clocks running, reset deasserted
 */
enum ebu_state {
	EBU_STOPPED = 0,
	EBU_RUNNING
};

struct goog_ebu_clk_mbox {
	struct device *dev;
};

struct ebu_google_driverdata;

/**
 * struct google_ebu - Representation of a google EBU
 * @ebu_state: Whether any channel is being routed to an active EP via the EBU
 * @ebu: Generic EBU object exposed to consumers (callbacks and context)
 * @dev: Device
 * @csr_base: Base address for configuration registers
 * @ebu_clk: Must be enabled before config changes and data transmission
 * @ebu_rst: Must be released before config changes and data transmission
 * @trace_fifo_base: Physical address where USB controller can access the head of the
 *	trace fifo
 * @ud_fifo_base: Physical address where USB controller can access the head of the
 *	UART + Debug fifo
 * @trace_tid: Source from which this EBU accepts data
 * @ebu_cfg: Global configuration for this EBU. Cache settings here when the
 *	EBU isn't enabled
 * @ch_use: Bitmap indicating for each channel whether it has been assigned
 * @ch_map: Map of channels to endpoints
 * @ep_map: Map of endpoints to channels. ch_map and ep_map are complementary
 * @lock: Serialize access
 */
struct google_ebu {
	enum ebu_state state;
	struct ebu_controller ebu;
	const struct ebu_google_driverdata *drv_data;
	struct device *dev;
	void __iomem *csr_base;
	void __iomem *secure_csr_base;
	void __iomem *m0p_sram_base;
	// To control phy init and exit from this driver for ambient debug
	struct phy *u2_phy;
	struct phy *u3_phy;
	bool amb_debug_enabled;
	bool ebu_fw_loaded;
	struct clk *ebu_clk;
	struct clk *ebu_core_clk;
	struct reset_control *ebu_rst;
	struct reset_control *ebu_core_rst;
	struct ebu_iface *mba_client;
	dma_addr_t trace_fifo_base;
	dma_addr_t ud_fifo_base;
	u32 trace_tid;
	u32 ebu_cfg;
	u16 ch_use[2];
	u64 ch_map[2];
	u64 ep_map[2];
	struct mutex lock;
};

/**
 * struct ebu_google_driverdata - platform specific driver data
 * @ebu_setup: Platform specific setup
 * @reinit_ebu: Platform specific reinit
 */
struct ebu_google_driverdata {
	int (*ebu_setup)(struct google_ebu *gebu, struct platform_device *pdev);
	int (*reinit_ebu)(struct google_ebu *gebu, enum usb_device_speed speed);
};

uint32_t ebu_readl(struct google_ebu *gebu, uint32_t offset);
void ebu_writel(struct google_ebu *gebu, uint32_t offset, uint32_t value);

#endif /* _EBU_GOOGLE_H */
