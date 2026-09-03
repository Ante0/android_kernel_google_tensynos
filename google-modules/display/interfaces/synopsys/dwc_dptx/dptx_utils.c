// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "dptx_utils.h"

 /* Find first (least significant) bit set
  * @param[in] data word to search
  * @return bit position or 32 if none is set
  */
uint32_t first_bit_set(uint32_t data)
{
	uint32_t n = 0;

	if (data != 0) {
		for (n = 0; (data & 1) == 0; n++)
			data >>= 1;
	}
	return n;
}

/*
 * Get bit field
 * @param[in] data raw data
 * @param[in] mask bit field mask
 * @return bit field value
 */
uint32_t get(uint32_t data, uint32_t mask)
{
	return ((data & mask) >> first_bit_set(mask));
}

/*
 * Set bit field
 * @param[in] data raw data
 * @param[in] mask bit field mask
 * @param[in] value new value
 * @return new raw data
 */
uint32_t set(uint32_t data, uint32_t mask, uint32_t value)
{
	return (((value << first_bit_set(mask)) & mask) | (data & ~mask));
}

/*
 * Get bit field
 * @param[in] data raw data
 * @param[in] mask bit field mask
 * @return bit field value
 */
uint8_t get8(uint8_t data, uint8_t mask)
{
	return ((data & mask) >> first_bit_set(mask));
}

/*
 * Set bit field
 * @param[in] data raw data
 * @param[in] mask bit field mask
 * @param[in] value new value
 * @return new raw data
 */
uint8_t set8(uint8_t data, uint8_t mask, uint8_t value)
{
	return (((value << first_bit_set(mask)) & mask) | (data & ~mask));
}

/*
 * Get bit field
 * @param[in] data raw data
 * @param[in] mask bit field mask
 * @return bit field value
 */
uint16_t get16(uint16_t data, uint16_t mask)
{
	return ((data & mask) >> first_bit_set(mask));
}

/*
 * Set bit field
 * @param[in] data raw data
 * @param[in] mask bit field mask
 * @param[in] value new value
 * @return new raw data
 */
uint16_t set16(uint16_t data, uint16_t mask, uint16_t value)
{
	return (((value << first_bit_set(mask)) & mask) | (data & ~mask));
}

int bus_write(struct dptx *dptx, u32 idx, u32 offset, u32 data)
{
	if (offset & 0x3)
		return -EIO;

	writel(data, (void *)(dptx->base[idx] + offset));

	return 0;
}

int bus_read(struct dptx *dptx, u32 idx, u32 offset)
{
	return readl((void *)(dptx->base[idx] + offset));
}

int ctrl_write(struct dptx *dptx, u32 offset, u32 data)
{
	return bus_write(dptx, DPTX, offset, data);
}

int ctrl_read(struct dptx *dptx, u32 offset)
{
	return bus_read(dptx, DPTX, offset);
}

int clkmng_write(struct dptx *dptx, u32 offset, u32 data)
{
	return bus_write(dptx, CLKMGR, offset, data);
}

int clkmng_read(struct dptx *dptx, u32 offset)
{
	return bus_read(dptx, CLKMGR, offset);
}

int rstmng_write(struct dptx *dptx, u32 offset, u32 data)
{
	return bus_write(dptx, RSTMGR, offset, data);
}

int vg_write(struct dptx *dptx, u32 offset, u32 data)
{
	return bus_write(dptx, VG, offset, data);
}

int vg_read(struct dptx *dptx, u32 offset)
{
	return bus_read(dptx, VG, offset);
}

int ag_write(struct dptx *dptx, u32 offset, u32 data)
{
	return bus_write(dptx, AG, offset, data);
}

int ag_read(struct dptx *dptx, u32 offset)
{
	return bus_read(dptx, AG, offset);
}

bool freq_is_equal(u32 a, u32 b)
{
	return (a == b);
}
