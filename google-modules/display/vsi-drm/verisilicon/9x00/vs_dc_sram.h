/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_DPU_SRAM_POOL_H_
#define _VS_DPU_SRAM_POOL_H_

#include "vs_dc_info.h"
#include <asm/delay.h>
/************************************************************************
 * Below are the sram pool interfaces.
 *
 *
 ************************************************************************/

/* Alignment with a power of two value. */
#define vsALIGN(n, align) (((n) + ((align) - 1)) & ~((align) - 1))

/* Alignment with a non-power of two value. */
#define vsALIGN_NP2(n, align) (((n) + (align) - 1) - (((n) + (align) - 1) % (align)))

/*
 * SRAM POOL type
 */
#define SRAM_NODE_NUM 32
#define SRAM_NODE_LIST_NUM 4
#define SRAM_THRESHOLD 64
#define MAX_SRAM_NODE_NUM_BIT_MASK ((0xFFFFFFFFU << (32 - SRAM_NODE_NUM)) >> (32 - SRAM_NODE_NUM))

#define INVALID_OFFSET 0xFFFFFFFFU

#define ALIGN32KB (32 * 1024)
#define ALIGN36KB (36 * 1024)
#define ALIGN64KB (64 * 1024)

enum vs_dpu_sram_pool_type {
	VS_DPU_SPOOL_FE0_DMA = 0,
	VS_DPU_SPOOL_FE0_SCL,
	VS_DPU_SPOOL_FE1_DMA,
	VS_DPU_SPOOL_FE1_SCL,
	VS_DPU_SPOOL_COUNT,
};

struct vs_dpu_sram_node {
	u32 size;
	u32 handle;
};

/* struct _vs_resemem_node_list will store the address of the resemem_node(vs_dpu_resemem_node)
 * as array. Num of the nodes is MEM_NODE_NUM in one _vs_resemem_node_list;
 */
struct vs_dpu_sram_node_list {
	u32 bitmap;
	/*array of sram node*/
	struct vs_dpu_sram_node *sram_node[SRAM_NODE_NUM];
};

struct vs_dpu_sram_pool {
	enum vs_dpu_sram_pool_type type;
	u32 free;
	u32 size;

	u32 list_num;
	struct vs_dpu_sram_node_list *lists;
	struct vs_dpu_sram_node sentinel;

	void *mutex;
};

struct dc_hw;

int32_t _get_dma_sram_size_with_rot(u8 format, u8 tile, u16 width, u32 *ret_size, u16 sp_alignment,
				    bool sp_extra_buffer, u8 sp_unit_size);

int32_t _get_dma_sram_size_without_rot(u8 format, u8 tile, u16 width, u32 *ret_size,
				       u16 sp_alignment, bool sp_extra_buffer, u8 sp_unit_size);

int32_t vs_dpu_sram_pools_init(struct dc_hw *hw);

int32_t vs_dpu_sram_pools_deinit(void);

int32_t vs_dpu_sram_alloc(enum vs_dpu_sram_pool_type type, u32 size, u32 *node_handle,
			  u32 *node_offset, int32_t realloc, u8 plane_id);

int32_t vs_dpu_sram_free(enum vs_dpu_sram_pool_type type, u32 node_handle, u8 plane_id);

int32_t vs_dpu_sram_pools_free_all(void);

int32_t vs_dpu_sram_pool_dump_usage(struct device *dev, enum vs_dpu_sram_pool_type type);

int32_t vs_dpu_query_sram_usage(u32 sram_pool_index, u32 *rest_size, u32 *total_size);

int32_t vs_dpu_get_dma_sram_size(u8 format, u8 tile, u8 rot, u16 width, u32 *ret_size,
				 u16 sp_alignment, bool sp_extra_buffer, u8 sp_unit_size);

int32_t vs_dpu_get_fescl_sram_size(u8 format, u16 width_scaled, u32 *ret_size);
#endif
