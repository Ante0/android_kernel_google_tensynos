/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM WLAN Driver Core Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_DRIVER_WLAN_MCU_H__
#define __LVM_DRIVER_WLAN_MCU_H__

#include <linux/device.h>
#include <buffer/manager.h>
#include <net/netdev.h>
#include <net/platform.h>
#include "irq.h"
#include "ring.h"

/**
 * WLAN RXBM and TXBM parameters
 *
 * WLAN_RX_BUF_NUM	- Number of receive buffers
 * WLAN_RX_BUF_LEN	- Length of each receive buffer
 * WLAN_RX_HEAD_LEN	- Length of the receive buffer header
 * WLAN_TX_BUF_NUM	- Number of transmit buffers
 * WLAN_TX_BUF_LEN	- Length of each transmit buffer
 * WLAN_TX_HEAD_LEN	- Length of the transmit buffer header
 * WLAN_TXBM_BUF_NUM	- Number of transmit buffers for Tethering usage
 */
#define WLAN_RX_BUF_NUM			4
#define WLAN_RX_BUF_LEN			2048
#define WLAN_RX_HEAD_LEN		128
#define WLAN_TX_BUF_NUM			2050
#define WLAN_TX_BUF_LEN			2048
#define WLAN_TX_HEAD_LEN		0
#define WLAN_TXBM_BUF_NUM		4096


/**
 * WLAN ring information
 *
 * WLAN_RING_NUM	- Number of WLAN device rings
 * WLAN_RING_NAME_MAX	- Maximum length of a WLAN device ring name
 */
#define WLAN_RING_NUM			5
#define WLAN_RING_NAME_MAX		32


/**
 * WLAN chipset types
 *
 * WLAN_TYPE_BCM4389	- Broadcom BCM4389 WLAN chip
 * WLAN_TYPE_BCM4390	- Broadcom BCM4390 WLAN chip
 * WLAN_TYPE_FAKE_BCM4389 - FAKE Broadcom BCM4389
 * WLAN_TYPE_FAKE_BCM4390 - FAKE Braodcom BCM4390
 * WLAN_TYPE_QCA	- Qualcomm WLAN chip
 */
enum {
	WLAN_TYPE_BCM4389,
	WLAN_TYPE_BCM4390,
	WLAN_TYPE_FAKE_BCM4389,
	WLAN_TYPE_FAKE_BCM4390,
	WLAN_TYPE_QCA,

	__WLAN_TYPE_MAX,
};

/**
 * enum drv_state - LVM driver state machine
 * @DRV_STATE_BYPASS_MODE: LVM driver NOA feature is disabled (Bypass Mode)
 * @DRV_STATE_NOA_MODE: LVM driver NOA feature is enabled (NOA Mode)
 * @__DRV_STATE_MAX: Number of LVM driver states
 */
enum drv_state {
	DRV_STATE_BYPASS_MODE,
	DRV_STATE_NOA_MODE,

	__DRV_STATE_MAX,
};

/**
 * struct wlan_param - WLAN driver parameters
 * @type: WLAN chip type
 * @irq: WLAN interrupt number
 * @ifidx: WLAN interface index
 * @reg: WLAN register map
 * @rxbm: WLAN RX buffer management parameters
 * @txbm: WLAN TX buffer management parameters
 * @ring: WLAN ring parameters
 */
struct wlan_param {
	u32				type;
	u32				ifidx;

	/**
	 * struct - WLAN interrupt configuration
	 * @id: The interrupt ID assigned to the WLAN device
	 * @poll_interval: The interval (in milliseconds) for polling the
	 *      IRQ status in polling mode. This is used as a fallback
	 *      mechanism if interrupt-driven mode is not available or fails
	 */
	struct {
		u32			id;
		u32			poll_interval;
	} irq;

	/**
	 * struct - WLAN register map
	 * @fake_dev_base: Base address for WLAN fake device
	 * @ncp_dev_base: Base address for NCP device
	 * @noa_dram_base: Base address for NOA DRAM
	 * @ncp_dram_base: Base address for NCP DRAM
	 * @fd_dram_base: Base address for fake device DRAM
	 */
	struct {
		u32			fake_dev_base;
		u32			ncp_dev_base;
		u32			noa_dram_base;
		u32			ncp_dram_base;
		u32			fd_dram_base;
	} reg;

	/**
	 * struct - WLAN RX buffer management parameters
	 * @buf_num: Number of RX buffers
	 * @buf_len: Length of each RX buffer
	 * @head_len: Length of RX buffer header
	 */
	struct {
		size_t			buf_num;
		size_t			buf_len;
		size_t			head_len;
	} rxbm;

	/**
	 * struct - WLAN TX buffer management parameters
	 * @buf_num: Number of TX buffers
	 * @buf_len: Length of each TX buffer
	 * @head_len: Length of TX buffer header
	 * @txbm_buf_num: Number of TX buffers for Tethering usage
	 */
	struct {
		size_t			buf_num;
		size_t			buf_len;
		size_t			head_len;
		size_t			txbm_buf_num;
	} txbm;

	/**
	 * struct - WLAN device ring parameters
	 * @hw_id: Hardware ring ID
	 * @type: Ring type
	 * @dir: Ring direction
	 * @desc_num: Number of descriptors in the ring
	 * @desc_len: Size of each descriptor
	 * @name: Ring name
	 */
	struct {
		u8			hw_id;
		u8			type;
		u8			dir;
		size_t			desc_num;
		size_t			desc_len;
		char			name[WLAN_RING_NAME_MAX];
	} ring[WLAN_RING_NUM];
};

/**
 * struct wlan_data - WLAN driver main data structure
 * @dev: Pointer to the device structure
 * @driver: Pointer to the platform driver structure
 * @netdev: Pointer to the network device structure
 * @param: Pointer to the WLAN parameters structure
 * @ring: Array of pointers to WLAN device ring structures
 * @topbank: Pointer to the WLAN topbank structure
 * @irq: Structure containing information related to interrupts
 * @wq: Pointer to the workqueue structure
 * @base: Base address of the WLAN device
 * @state: WLAN driver state machine
 */
struct wlan_data {
	struct device			*dev;
	struct lvm_platform_driver	*driver;
	struct lvm_netdev		*netdev;
	const struct wlan_param		*param;
	struct wlan_ring		*ring[WLAN_RING_NUM];
	struct wlan_topbank		*topbank;
	struct wlan_irq			irq;
	struct workqueue_struct		*wq;
	void __iomem			*base;
	unsigned long			state;
};

int lvm_wlan_dal_init(struct wlan_data *data);
void lvm_wlan_dal_deinit(struct wlan_data *data);
int lvm_wlan_buffer_init(struct lvm_platform_device *pdev);
void lvm_wlan_buffer_deinit(struct lvm_platform_device *pdev);
int lvm_wlan_mcu_init(struct lvm_platform_device *pdev);
void lvm_wlan_mcu_deinit(struct lvm_platform_device *pdev);

static inline bool is_bypass_mode(struct wlan_data *data)
{
	return data->driver->state == DRV_STATE_BYPASS_MODE;
}

static inline bool is_noa_mode(struct wlan_data *data)
{
	return data->driver->state == DRV_STATE_NOA_MODE;
}

#endif  /* __LVM_DRIVER_WLAN_MCU_H__ */
