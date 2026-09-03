/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

bool freq_is_equal(u32 a, u32 b);
uint32_t first_bit_set(uint32_t data);
uint32_t get(uint32_t data, uint32_t mask);
uint32_t set(uint32_t data, uint32_t mask, uint32_t value);
uint8_t get8(uint8_t data, uint8_t mask);
uint8_t set8(uint8_t data, uint8_t mask, uint8_t value);
uint16_t get16(uint16_t data, uint16_t mask);
uint16_t set16(uint16_t data, uint16_t mask, uint16_t value);



int bus_write(struct dptx *dptx, u32 idx, u32 offset, u32 data);
int bus_read(struct dptx *dptx, u32 idx, u32 offset);

int ctrl_write(struct dptx *dptx, u32 offset, u32 data);
int ctrl_read(struct dptx *dptx, u32 offset);

int clkmng_write(struct dptx *dptx, u32 offset, u32 data);
int clkmng_read(struct dptx *dptx, u32 offset);

int rstmng_write(struct dptx *dptx, u32 offset, u32 data);

int vg_write(struct dptx *dptx, u32 offset, u32 data);
int vg_read(struct dptx *dptx, u32 offset);

int ag_write(struct dptx *dptx, u32 offset, u32 data);
int ag_read(struct dptx *dptx, u32 offset);
