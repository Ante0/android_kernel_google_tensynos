/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM WLAN Top Bank Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_DRIVER_WLAN_TOPBANK_H__
#define __LVM_DRIVER_WLAN_TOPBANK_H__

#include <core/bitwise.h>
#include "mcu.h"

/**
 * Top register offset definition
 *
 * WLAN_TOP_REG_DEV_ENABLE	- Device enable register offset
 * WLAN_TOP_REG_DOORBELL_ENABLE	- Doorbell enable register offset
 * WLAN_TOP_REG_DEV_ID		- Device ID register offset
 * WLAN_TOP_REG_IN_RING_NUM	- Number of input rings register offset
 * WLAN_TOP_REG_OUT_RING_NUM	- Number of output rings register offset
 * WLAN_TOP_REG_OUT_RING_ISR	- Output ring interrupt status register offset
 * WLAN_TOP_REG_OUT_RING_IMR	- Output ring interrupt mask register offset
 * WLAN_TOP_REG_IN_RING_DOORBELL- Input ring doorbell register offset
 * WLAN_TOP_REG_MIB_TX_PKT	- MIB TX packet counter register offset
 * WLAN_TOP_REG_MIB_TX_BYTE	- MIB TX byte counter register offset
 * WLAN_TOP_REG_MIB_RX_PKT	- MIB RX packet counter register offset
 * WLAN_TOP_REG_MIB_RX_BYTE	- MIB RX byte counter register offset
 * WLAN_TOP_REG_MIB_TX_CPL	- MIB TX completion counter register offset
 * WLAN_TOP_REG_RANGE		- Total size of the WLAN top register range
 */
#define WLAN_TOP_REG_DEV_ENABLE			0x00
#define WLAN_TOP_REG_DOORBELL_ENABLE		0x00
#define WLAN_TOP_REG_DEV_ID			0x18
#define WLAN_TOP_REG_IN_RING_NUM		0x20
#define WLAN_TOP_REG_OUT_RING_NUM		0x24
#define WLAN_TOP_REG_OUT_RING_ISR		0x30
#define WLAN_TOP_REG_OUT_RING_IMR		0x38
#define WLAN_TOP_REG_IN_RING_DOORBELL		0x40
#define WLAN_TOP_REG_MIB_TX_PKT			0x50
#define WLAN_TOP_REG_MIB_TX_BYTE		0x54
#define WLAN_TOP_REG_MIB_RX_PKT			0x60
#define WLAN_TOP_REG_MIB_RX_BYTE		0x64
#define WLAN_TOP_REG_MIB_TX_CPL			0x68
#define WLAN_TOP_REG_RANGE			0x100

/**
 * Top register bit mask definition
 *
 * WLAN_TOP_REG_DEV_ENABLE_MASK		- Device enable bit mask
 * WLAN_TOP_REG_DOORBELL_ENABLE_MASK	- Doorbell enable bit mask
 * WLAN_TOP_REG_DEV_ID_MASK		- Device ID bit mask
 * WLAN_TOP_REG_IN_RING_NUM_MASK	- Number of input rings bit mask
 * WLAN_TOP_REG_OUT_RING_NUM_MASK	- Number of output rings bit mask
 * WLAN_TOP_REG_OUT_RING_ISR_MASK	- Output ring interrupt status bit mask
 * WLAN_TOP_REG_OUT_RING_IMR_MASK	- Output ring interrupt mask bit mask
 * WLAN_TOP_REG_IN_RING_DOORBELL_MASK	- Input ring doorbell bit mask
 * WLAN_TOP_REG_MIB_TX_PKT_MASK		- MIB TX packet counter bit mask
 * WLAN_TOP_REG_MIB_TX_BYTE_MASK	- MIB TX byte counter bit mask
 * WLAN_TOP_REG_MIB_RX_PKT_MASK		- MIB RX packet counter bit mask
 * WLAN_TOP_REG_MIB_RX_BYTE_MASK	- MIB RX byte counter bit mask
 * WLAN_TOP_REG_MIB_TX_CPL_MASK		- MIB TX completion counter bit mask
 */
#define WLAN_TOP_REG_DEV_ENABLE_MASK		GENMASK(0, 0)
#define WLAN_TOP_REG_DOORBELL_ENABLE_MASK	GENMASK(1, 1)
#define WLAN_TOP_REG_DEV_ID_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_IN_RING_NUM_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_OUT_RING_NUM_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_OUT_RING_ISR_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_OUT_RING_IMR_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_IN_RING_DOORBELL_MASK	GENMASK(31, 0)
#define WLAN_TOP_REG_MIB_TX_PKT_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_MIB_TX_BYTE_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_MIB_RX_PKT_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_MIB_RX_BYTE_MASK		GENMASK(31, 0)
#define WLAN_TOP_REG_MIB_TX_CPL_MASK		GENMASK(31, 0)

/**
 * struct wlan_topbank - WLAN top bank structure
 * @regbase_va: Virtual address of the WLAN top bank registers base
 * @regbase_pa: Physical address of the WLAN top bank registers base
 */
struct wlan_topbank {
	void __iomem				*regbase_va;
	u32					regbase_pa;
};

int lvm_wlan_topbank_init(struct wlan_data *data);
void lvm_wlan_topbank_deinit(struct wlan_data *data);

static inline void lvm_wlan_topbank_reg_write(struct wlan_topbank *topbank,
					      u32 reg, u32 val)
{
	writel(val, topbank->regbase_va + reg);
}

static inline u32 lvm_wlan_topbank_reg_read(struct wlan_topbank *topbank,
					    u32 reg)
{
	return readl(topbank->regbase_va + reg);
}

static inline void lvm_wlan_topbank_reg_setbits(struct wlan_topbank *topbank,
						u32 reg, u32 mask)
{
	setbits(topbank->regbase_va + reg, mask);
}

static inline void lvm_wlan_topbank_reg_clrbits(struct wlan_topbank *topbank,
						u32 reg, u32 mask)
{
	clrbits(topbank->regbase_va + reg, mask);
}

#endif  /* __LVM_DRIVER_WLAN_TOPBANK_H__ */
