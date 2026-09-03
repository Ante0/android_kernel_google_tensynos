/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM WLAN Device Ring Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_DRIVER_WLAN_RING_H__
#define __LVM_DRIVER_WLAN_RING_H__

#include <core/bitwise.h>
#include "mcu.h"

/**
 * Ring register offset definition
 *
 * WLAN_RING_REG_CTRL	- Ring control register offset
 * WLAN_RING_REG_INFO	- Ring information register offset
 * WLAN_RING_REG_DESCBASE - Ring descriptor base address register offset
 * WLAN_RING_REG_NUM	- Number of descriptors in the ring register offset
 * WLAN_RING_REG_LEN	- Length of each descriptor in the ring register offset
 * WLAN_RING_REG_READ	- Ring read pointer register offset
 * WLAN_RING_REG_WRITE	- Ring write pointer register offset
 */
#define WLAN_RING_REG_CTRL		0x00
#define WLAN_RING_REG_INFO		0x04
#define WLAN_RING_REG_DESCBASE		0x10
#define WLAN_RING_REG_NUM		0x14
#define WLAN_RING_REG_LEN		0x18
#define WLAN_RING_REG_READ		0x20
#define WLAN_RING_REG_WRITE		0x24


/**
 * Ring register mask definition
 *
 * WLAN_RING_REG_CTRL_MASK	- Mask for the ring control register
 * WLAN_RING_REG_INFO_MASK	- Mask for the ring information register
 * WLAN_RING_REG_DESCBASE_MASK	- Mask for the ring descriptor base
 *				  address register
 * WLAN_RING_REG_NUM_MASK	- Mask for the number of descriptors in
 *				  the ring register
 * WLAN_RING_REG_LEN_MASK	- Mask for the length of each descriptor
 *				  in the ring register
 * WLAN_RING_REG_READ_MASK	- Mask for the ring read pointer register
 * WLAN_RING_REG_WRITE_MASK	- Mask for the ring write pointer register
 */
#define WLAN_RING_REG_CTRL_MASK		GENMASK(0,  0)
#define WLAN_RING_REG_INFO_MASK		GENMASK(31, 0)
#define WLAN_RING_REG_DESCBASE_MASK	GENMASK(31, 0)
#define WLAN_RING_REG_NUM_MASK		GENMASK(31, 0)
#define WLAN_RING_REG_LEN_MASK		GENMASK(31, 0)
#define WLAN_RING_REG_READ_MASK		GENMASK(31, 0)
#define WLAN_RING_REG_WRITE_MASK	GENMASK(31, 0)


/**
 * Ring misc definition
 *
 * WLAN_RING_NAME_SIZE - Size of the ring name string
 * WLAN_RING_REG_RANGE - Size of the ring register space
 * WLAN_RING_REG_BASE  - Calculate the ring register base address
 */
#define WLAN_RING_NAME_SIZE		32
#define WLAN_RING_TX_DATA_NUM		(__WLAN_RING_ID_MAX - WLAN_RING_ID_TX_DATA0)
#define WLAN_RING_REG_RANGE		0x100
#define WLAN_RING_REG_BASE(id)		(param->reg.fake_dev_base +		\
					 (param->ring[id].hw_id *		\
					  WLAN_RING_REG_RANGE) +		\
					 ((param->ring[id].dir ==		\
					   WLAN_DIR_TO_DEV) ?			\
					   0x10000 : 0x20000))


struct wlan_data;


/**
 * WLAN device ring IDs
 */
enum {
	WLAN_RING_ID_RX_DATA0,
	WLAN_RING_ID_RX_POST0,
	WLAN_RING_ID_TX_CPL0,
	WLAN_RING_ID_TX_DATA0,
	WLAN_RING_ID_TX_DATA1,

	__WLAN_RING_ID_MAX,
};


/**
 * WLAN device ring types
 */
enum {
	WLAN_RING_TYPE_RX_DATA,
	WLAN_RING_TYPE_RX_POST,
	WLAN_RING_TYPE_TX_CPL,
	WLAN_RING_TYPE_TX_DATA,

	__WLAN_RING_TYPE_MAX,
};


/**
 * WLAN device ring directions
 */
enum {
	WLAN_DIR_TO_DEV,
	WLAN_DIR_FROM_DEV,

	__WLAN_DIR_MAX,
};


/**
 * struct wlan_ring - WLAN device ring structure
 * @ring_id: Ring ID
 * @hw_id: Hardware ID
 * @type: Ring type
 * @dir: Ring direction
 * @read_pos: Current read pointer
 * @write_pos: Current write pointer
 * @name: Ring name
 * @desc_num: Number of descriptors in the ring
 * @desc_len: Length of each descriptor
 * @desc_va: Virtual address of the descriptor base
 * @desc_pa: Physical address of the descriptor base
 * @regbase_va: Virtual address of the ring register base
 * @regbase_pa: Physical address of the ring register base
 */
struct wlan_ring {
	u8				ring_id;
	u8				hw_id;
	u8				type;
	u8				dir;
	u32				read_pos;
	u32				write_pos;
	char				name[WLAN_RING_NAME_SIZE];

	size_t				desc_num;
	size_t				desc_len;

	void				*desc_va;
	dma_addr_t			desc_pa;

	void __iomem			*regbase_va;
	u32				regbase_pa;
};


u32 lvm_wlan_ring_write_cnt_get(struct wlan_ring *ring);
u32 lvm_wlan_ring_read_cnt_get(struct wlan_ring *ring);
int lvm_wlan_ring_init(struct wlan_data *data);
void lvm_wlan_ring_deinit(struct wlan_data *data);

static inline void lvm_wlan_ring_reg_write(struct wlan_ring *ring, u32 reg,
					   u32 val)
{
	writel(val, ring->regbase_va + reg);
}

static inline u32 lvm_wlan_ring_reg_read(struct wlan_ring *ring, u32 reg)
{
	return readl(ring->regbase_va + reg);
}

static inline void lvm_wlan_ring_reg_setbits(struct wlan_ring *ring,
					     u32 reg, u32 mask)
{
	setbits(ring->regbase_va + reg, mask);
}

static inline void lvm_wlan_ring_reg_clrbits(struct wlan_ring *ring,
					     u32 reg, u32 mask)
{
	clrbits(ring->regbase_va + reg, mask);
}

static inline void *lvm_wlan_ring_write_base_get(struct wlan_ring *ring)
{
	return (char *)ring->desc_va + ring->write_pos * ring->desc_len;
}

static inline void *lvm_wlan_ring_read_base_get(struct wlan_ring *ring)
{
	return (char *)ring->desc_va + ring->read_pos * ring->desc_len;
}

static inline void lvm_wlan_ring_write_pos_move(struct wlan_ring *ring)
{
	ring->write_pos++;
	ring->write_pos %= ring->desc_num;
}

static inline void lvm_wlan_ring_read_pos_move(struct wlan_ring *ring)
{
	ring->read_pos++;
	ring->read_pos %= ring->desc_num;
}

#endif  /* __LVM_DRIVER_WLAN_RING_H__ */
