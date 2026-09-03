/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_sram.h"
#include "vs_dc_hw.h"
#include "vs_trace.h"

/*SRAM Pool features*/

struct vs_dpu_sram_pool *sram_pools[VS_DPU_SPOOL_COUNT];

static int _sram_create_mutex(void **mutex)
{
	struct mutex *mutex_ptr;

	if (mutex == NULL)
		return -EINVAL;

	mutex_ptr = kzalloc(sizeof(*mutex_ptr), GFP_KERNEL);
	if (!mutex_ptr)
		return -ENOMEM;

	mutex_init(mutex_ptr);

	*mutex = mutex_ptr;
	return 0;
}
#define SRAM_DC_INFINITE ((u32)(~0U))

static int _sram_acquire_mutex(void *mutex, u32 timeout)
{
	if (mutex == NULL) {
		pr_err("%s has invalid argument.\n", __func__);
		return -EINVAL;
	}
	if (timeout == SRAM_DC_INFINITE) {
		mutex_lock(mutex);
		return 0;
	}

	for (;;) {
		/* Try to acquire the mutex. */
		if (mutex_trylock(mutex)) {
			/* Success. */
			return 0;
		}
		if (timeout-- == 0)
			break;
		/* Wait for 1 millisecond. */
		udelay(1000);
	}

	return -ETIMEDOUT;
}

static int _sram_release_mutex(void *mutex)
{
	if (mutex == NULL) {
		pr_err("%s has invalid argument.\n", __func__);
		return -EINVAL;
	}

	mutex_unlock(mutex);
	return 0;
}

int32_t vs_dpu_sram_pools_init(struct dc_hw *hw)
{
	u32 i = 0;
	int32_t status = 0;
	struct vs_dpu_sram_pool *sram_pool = NULL;
	struct vs_dpu_sram_node_list *node_list = NULL;
	u32 sram_size[VS_DPU_SPOOL_COUNT] = {
		hw->info->fe0_dma_sram_size * 1024, /*VS_DPU_SPOOL_FE0_DMA SRAM POOL SIZE (bytes) */
		hw->info->fe0_scl_sram_size * 1024, /*VS_DPU_SPOOL_FE0_SCL SRAM POOL SIZE (bytes) */
		hw->info->fe1_dma_sram_size * 1024, /*VS_DPU_SPOOL_FE1_DMA SRAM POOL SIZE (bytes) */
		hw->info->fe1_scl_sram_size * 1024 /*VS_DPU_SPOOL_FE1_SCL SRAM POOL SIZE (bytes) */
	};

	for (i = 0; i < VS_DPU_SPOOL_COUNT; i++) {
		sram_pool = kzalloc(sizeof(*sram_pool), GFP_KERNEL);
		if (!sram_pool) {
			status = -ENOMEM;
			goto OnError;
		}
		sram_pools[i] = sram_pool;
		sram_pools[i]->type = (enum vs_dpu_sram_pool_type)i;
		sram_pools[i]->free = sram_pools[i]->size = sram_size[i];
		sram_pools[i]->list_num = SRAM_NODE_LIST_NUM;
		node_list = kzalloc(sizeof(*node_list) * SRAM_NODE_LIST_NUM, GFP_KERNEL);
		if (!node_list) {
			status = -ENOMEM;
			goto OnError;
		}
		sram_pools[i]->lists = node_list;

		status = _sram_create_mutex(&(sram_pools[i]->mutex));
		if (status)
			goto OnError;
	}

OnError:
	return status;
}
int32_t vs_dpu_sram_pools_deinit(void)
{
	int32_t status = 0;
	u32 i = 0;

	for (i = 0; i < VS_DPU_SPOOL_COUNT; i++)
		;

	return status;
}

static int32_t _add_node_in_list(struct vs_dpu_sram_pool *pool, struct vs_dpu_sram_node *curr,
				 u32 *node_handle)
{
	int32_t status = 0;
	u32 j;
	void *pointer;
	struct vs_dpu_sram_node_list *lists = pool->lists;
	u32 list_num = pool->list_num;

	for (j = 0; j < list_num; j++) {
		if (lists[j].bitmap < MAX_SRAM_NODE_NUM_BIT_MASK) {
			u32 index = 1;

			for (; index < SRAM_NODE_NUM; index++) {
				if (!(lists[j].bitmap & (1u << index)))
					break;
			}

			if (lists[j].sram_node[index])
				return -EINVAL;

			lists[j].sram_node[index] = curr;
			lists[j].bitmap |= (1u << index);
			*node_handle = j * SRAM_NODE_NUM + index;
			return 0;
		}
	}
	pointer = kzalloc(sizeof(*lists) * 2 * list_num, GFP_KERNEL);
	if (!pointer) {
		status = -ENOMEM;
		goto OnError;
	}
	memcpy(pointer, lists, sizeof(struct vs_dpu_sram_node_list) * list_num);

	kfree(lists);

	lists = pool->lists = (struct vs_dpu_sram_node_list *)pointer;
	pool->list_num = 2 * list_num;

	j++;
	lists[j].sram_node[0] = curr;
	lists[j].bitmap |= (1u << 0);
	*node_handle = j * SRAM_NODE_NUM;

OnError:
	return status;
}

static int32_t get_sram_node_in_list(struct vs_dpu_sram_pool *pool, u32 node_handle,
				     struct vs_dpu_sram_node **curr, int32_t remove_node)
{
	u32 list_num = node_handle >> 5;
	u32 index = node_handle % SRAM_NODE_NUM;
	struct vs_dpu_sram_node_list *lists = pool->lists;

	if (list_num > pool->list_num)
		return -EINVAL;
	if ((lists[list_num].bitmap & (1u << index)) == 0)
		return -EINVAL;

	*curr = lists[list_num].sram_node[index];

	if (!(*curr))
		return -EINVAL;

	if (remove_node) {
		lists[list_num].sram_node[index] = NULL;
		lists[list_num].bitmap &= ~(1u << index);
	}

	return 0;
}

int32_t vs_dpu_sram_alloc(enum vs_dpu_sram_pool_type type, u32 size, u32 *node_handle,
			  u32 *node_offset, int32_t realloc, u8 plane_id)
{
	int32_t status = 0;
	struct vs_dpu_sram_node *curr = NULL;
	u32 acquired = 0;
	u32 handle = 0;
	u32 old_size = 0;

	if (size == 0) {
		status = -EINVAL;
		goto OnError;
	}

	if (!node_handle) {
		status = -EINVAL;
		goto OnError;
	}

	status = _sram_acquire_mutex(sram_pools[type]->mutex, SRAM_DC_INFINITE);
	if (status)
		goto OnError;
	acquired = 1;

	if (realloc) {
		status = get_sram_node_in_list(sram_pools[type], *node_handle, &curr, 0);
		if (status)
			goto OnError;

		old_size = curr->size;

		if (sram_pools[type]->free + curr->size < size) {
			status = -ENOMEM;
			goto OnError;
		}

		/*
		 * Shrinking involves returning unused SRAM to the pool. However, the layer transfer
		 * might still be in progress. We should revisit the release logic here.
		 * TODO(b/407894810): Release SRAM to pool a the appropriate time
		 */
		sram_pools[type]->free -= (size - curr->size);
		curr->size = size;
	} else {
		if (sram_pools[type]->free < size) {
			status = -ENOMEM;
			goto OnError;
		}

		curr = kzalloc(sizeof(*curr), GFP_KERNEL);
		if (!curr) {
			status = -EINVAL;
			goto OnError;
		}

		curr->size = size;
		sram_pools[type]->free -= curr->size;

		status = _add_node_in_list(sram_pools[type], curr, &handle);
		if (status)
			goto OnError;

		curr->handle = handle;
		*node_handle = handle;
	}
OnError:
	trace_sram_allocation(type, plane_id, realloc, old_size, size, sram_pools[type]->free);
	trace_sram_alloc_failure(type, status, sram_pools[type]->free, plane_id);
	if (acquired)
		_sram_release_mutex(sram_pools[type]->mutex);

	return status;
}

int32_t vs_dpu_sram_free(enum vs_dpu_sram_pool_type type, u32 node_handle, u8 plane_id)
{
	int32_t status = 0;
	struct vs_dpu_sram_node *curr = NULL;
	int32_t remove_node = 1;
	u32 acquired = 0;

	status = _sram_acquire_mutex(sram_pools[type]->mutex, SRAM_DC_INFINITE);
	if (status)
		goto OnError;
	acquired = 1;

	status = get_sram_node_in_list(sram_pools[type], node_handle, &curr, remove_node);
	if (status)
		goto OnError;

	sram_pools[type]->free += curr->size;

	trace_sram_free(type, plane_id, curr->size, sram_pools[type]->free);

	kfree(curr);

OnError:
	trace_sram_free_failure(type, status, sram_pools[type]->free, plane_id);
	if (acquired)
		_sram_release_mutex(sram_pools[type]->mutex);
	return status;
}

int32_t vs_dpu_sram_pools_free_all(void)
{
	int32_t status = 0;
	u32 i = 0;
	u32 max_node_in_pool = 0;
	u32 node_index = 0;

	for (i = 0; i < VS_DPU_SPOOL_COUNT; i++) {
		max_node_in_pool = sram_pools[i]->list_num * SRAM_NODE_NUM;
		for (node_index = 0; node_index < max_node_in_pool; node_index++)
			vs_dpu_sram_free(sram_pools[i]->type, node_index,
					 255 /* No plane ID since we're doing a bulk free */);
	}
	return status;
}

int32_t vs_dpu_query_sram_usage(u32 sram_pool_index, u32 *rest_size, u32 *total_size)
{
	u32 acquired = 0;
	int32_t status = 0;

	status = _sram_acquire_mutex(sram_pools[sram_pool_index]->mutex, SRAM_DC_INFINITE);
	if (status)
		goto OnError;
	acquired = 1;

	*rest_size = sram_pools[sram_pool_index]->free;
	*total_size = sram_pools[sram_pool_index]->size;

OnError:
	if (acquired)
		_sram_release_mutex(sram_pools[sram_pool_index]->mutex);
	return status;
}

int32_t vs_dpu_sram_pool_dump_usage(struct device *dev, enum vs_dpu_sram_pool_type type)
{
	int32_t status = 0;
	u32 rest_size = 0;
	u32 total_size = 0;
	u32 list_num = 0;
	u32 j = 0;
	struct vs_dpu_sram_node_list *lists;

	vs_dpu_query_sram_usage(type, &rest_size, &total_size);
	dev_info(dev, "sram pool index: %d, rest size:%uK, total size:%uK\n", type, rest_size >> 10,
		  total_size >> 10);

	status = _sram_acquire_mutex(sram_pools[type]->mutex, SRAM_DC_INFINITE);
	if (status)
		goto OnError;

	list_num = sram_pools[type]->list_num;
	lists = sram_pools[type]->lists;

	for (j = 0; j < list_num; j++) {
		u32 index;

		for (index = 0; index < SRAM_NODE_NUM; index++) {
			struct vs_dpu_sram_node *sram_node = lists[j].sram_node[index];

			if (sram_node) {
				dev_info(dev, "sram_node->size:%uK, sram_node->handle:%u\n",
					 sram_node->size >> 10, sram_node->handle);
			} else if (lists[j].bitmap & (1u << index)) {
				dev_warn(dev, "list:%u index:%u sram_node is NULL\n", j, index);
			}
		}
	}
	_sram_release_mutex(sram_pools[type]->mutex);
OnError:
	return status;
}

int32_t _get_dma_sram_size_with_rot(u8 format, u8 tile, u16 width, u32 *ret_size, u16 sp_alignment,
				    bool sp_extra_buffer, u8 sp_unit_size)
{
	int ret = 0;
	u32 size = 0;
	u32 extra_size0 = 0;
	u32 extra_size1 = 0;

	if (!ret_size)
		return -EINVAL;

	if (tile == TILE_MODE_LINEAR) {
		switch (format) {
		case FORMAT_YV12:
			size = vsALIGN(width * 1, sp_alignment) * 32 * 2;
			extra_size0 = sp_alignment * 32;
			extra_size1 = sp_alignment * 32;
			break;
		case FORMAT_NV12:
			size = vsALIGN(width * 1, sp_alignment) * 32 +
			       vsALIGN(width / 2 * 2, sp_alignment) * 16;
			extra_size0 = sp_alignment * 32;
			extra_size1 = sp_alignment * 16;
			break;
		case FORMAT_NV16:
			size = vsALIGN(width * 1, sp_alignment) * 32 +
			       vsALIGN(width * 2, sp_alignment) * 16;
			extra_size0 = sp_alignment * 32;
			extra_size1 = sp_alignment * 16;
			break;
		case FORMAT_P010:
			size = vsALIGN(width * 2, sp_alignment) * 16 +
				   vsALIGN(width / 2 * 4, sp_alignment) * 8;
			extra_size0 = sp_alignment * 16;
			extra_size1 = sp_alignment * 8;
			break;
		case FORMAT_P210:
			size = vsALIGN(width * 2, sp_alignment) * 16 +
				   vsALIGN(width * 4, sp_alignment) * 8;
			extra_size0 = sp_alignment * 16;
			extra_size1 = sp_alignment * 8;
			break;
		case FORMAT_YUV420_PACKED_10BIT:
			size = vsALIGN(width * 2, sp_alignment) * 24 +
				   vsALIGN(width / 2 * 4, sp_alignment) * 12;
			extra_size0 = sp_alignment * 24;
			extra_size1 = sp_alignment * 12;
			break;
		default:
			pr_err("%s : format %d with tile %d not support Rotation.\n", __func__,
			       format, tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_32X8) {
		switch (format) {
		case FORMAT_NV12:
			size = vsALIGN(width * 1, sp_alignment) * 32 +
			       vsALIGN(width / 2 * 2, sp_alignment) * 16;
			extra_size0 = sp_alignment * 32;
			extra_size1 = sp_alignment * 16;
			break;
		default:
			pr_err("%s : format %d with tile %d not support Rotation.\n", __func__,
			       format, tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_16X8) {
		switch (format) {
		case FORMAT_P010:
			size = vsALIGN(width * 2, sp_alignment) * 16 +
			       vsALIGN(width / 2 * 4, sp_alignment) * 8;
			extra_size0 = sp_alignment * 16;
			extra_size1 = sp_alignment * 8;
			break;
		default:
			pr_err("%s : format %d with tile %d not support Rotation.\n", __func__,
			       format, tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_8X8_UNIT2X2) {
		switch (format) {
		case FORMAT_A8R8G8B8:
		case FORMAT_A2R10G10B10:
			size = vsALIGN(width * 4, sp_alignment) * 8;
			break;
		default:
			pr_err("%s : format %d with tile %d not support Rotation.\n", __func__,
			       format, tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_8X4_UNIT2X2) {
		switch (format) {
		case FORMAT_A16R16G16B16:
			size = vsALIGN(width * 8, sp_alignment) * 4;
			break;
		default:
			pr_err("%s : format %d with tile %d not support Rotation.\n", __func__,
			       format, tile);
			ret = -EINVAL;
			break;
		}
	} else {
		pr_err("%s : format %d with tile %d not support Rotation.\n", __func__, format,
		       tile);
		ret = -EINVAL;
	}

	if (sp_extra_buffer)
		*ret_size = size + extra_size0 + extra_size1;
	else
		*ret_size = size;

	if (sp_unit_size == SRAM_UNIT_SIZE_32KB)
		*ret_size = vsALIGN(*ret_size, ALIGN32KB);
	else
		*ret_size = vsALIGN(*ret_size, ALIGN64KB);

	return ret;
}

int32_t _get_dma_sram_size_without_rot(u8 format, u8 tile, u16 width, u32 *ret_size,
				       u16 sp_alignment, bool sp_extra_buffer, u8 sp_unit_size)
{
	int ret = 0;
	u32 size = 0;
	u32 extra_size0 = 0;
	u32 extra_size1 = 0;

	if (!ret_size)
		return -EINVAL;

	if (tile == TILE_MODE_LINEAR) {
		switch (format) {
		case FORMAT_A8R8G8B8:
		case FORMAT_X8R8G8B8:
		case FORMAT_A2R10G10B10:
		case FORMAT_X2R10G10B10:
			size = vsALIGN((width * 4), sp_alignment);
			break;
		case FORMAT_A4R4G4B4:
		case FORMAT_X4R4G4B4:
		case FORMAT_A1R5G5B5:
		case FORMAT_X1R5G5B5:
		case FORMAT_R5G6B5:
			size = vsALIGN((width * 2), sp_alignment);
			break;
		case FORMAT_A16R16G16B16:
			size = vsALIGN((width * 8), sp_alignment);
			break;
		case FORMAT_YV12:
		case FORMAT_NV12:
		case FORMAT_NV16:
			size = vsALIGN((width * 1), sp_alignment) * 3 * 2;
			break;
		case FORMAT_P010:
		case FORMAT_P210:
		case FORMAT_YUV420_PACKED_10BIT:
			size = vsALIGN((width * 2), sp_alignment) * 3 * 2;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_16X4) {
		switch (format) {
		case FORMAT_A8R8G8B8:
		case FORMAT_X8R8G8B8:
		case FORMAT_A2R10G10B10:
		case FORMAT_X2R10G10B10:
			size = vsALIGN((width * 4), sp_alignment) * 4;
			extra_size0 = sp_alignment * 4;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_32X4) {
		switch (format) {
		case FORMAT_R5G6B5:
			size = vsALIGN((width * 2), sp_alignment) * 4;
			extra_size0 = sp_alignment * 4;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_32X2) {
		switch (format) {
		case FORMAT_A16R16G16B16:
			size = vsALIGN((width * 8), sp_alignment) * 2;
			extra_size0 = sp_alignment * 2;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_16X16) {
		switch (format) {
		case FORMAT_NV12:
			size = vsALIGN((width * 1), sp_alignment) * 16 * 2;
			extra_size0 = sp_alignment * 16;
			extra_size1 = sp_alignment * 16;
			break;
		case FORMAT_P010:
			size = vsALIGN((width * 2), sp_alignment) * 16 * 2;
			extra_size0 = sp_alignment * 16;
			extra_size1 = sp_alignment * 16;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_32X8) {
		switch (format) {
		case FORMAT_A8R8G8B8:
		case FORMAT_A2R10G10B10:
			size = vsALIGN((width * 4), sp_alignment) * 8;
			extra_size0 = sp_alignment * 8;
			break;
		case FORMAT_NV12:
			size = vsALIGN((width * 1), sp_alignment) * 8 * 2;
			extra_size0 = sp_alignment * 8;
			extra_size1 = sp_alignment * 8;
			break;
		case FORMAT_P010:
			size = vsALIGN((width * 2), sp_alignment) * 8 * 2;
			extra_size0 = sp_alignment * 8;
			extra_size1 = sp_alignment * 8;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_16X8) {
		switch (format) {
		case FORMAT_P010:
			size = vsALIGN((width * 2), sp_alignment) * 8 * 2;
			extra_size0 = sp_alignment * 8;
			extra_size1 = sp_alignment * 8;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_8X8_UNIT2X2) {
		switch (format) {
		case FORMAT_A8R8G8B8:
		case FORMAT_A2R10G10B10:
			size = vsALIGN((width * 4), sp_alignment) * 8;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else if (tile == TILE_MODE_8X4_UNIT2X2) {
		switch (format) {
		case FORMAT_A16R16G16B16:
			size = vsALIGN((width * 8), sp_alignment) * 4;
			break;
		default:
			pr_err("%s : format %d with tile %d not support.\n", __func__, format,
			       tile);
			ret = -EINVAL;
			break;
		}
	} else {
		pr_err("%s : format %d with tile %d not support.\n", __func__, format, tile);
		ret = -EINVAL;
	}

	if (sp_extra_buffer)
		*ret_size = size + extra_size0 + extra_size1;
	else
		*ret_size = size;

	if (sp_unit_size == SRAM_UNIT_SIZE_32KB)
		*ret_size = vsALIGN(*ret_size, ALIGN32KB);
	else
		*ret_size = vsALIGN(*ret_size, ALIGN64KB);

	return ret;
}

int32_t vs_dpu_get_dma_sram_size(u8 format, u8 tile, u8 rot, u16 width, u32 *ret_size,
				 u16 sp_manage_unit, bool sp_extra_buffer, u8 sp_unit_size)
{
	if (rot == ROT_90 || rot == ROT_270)
		return _get_dma_sram_size_with_rot(format, tile, width, ret_size, sp_manage_unit,
						   sp_extra_buffer, sp_unit_size);
	else
		return _get_dma_sram_size_without_rot(format, tile, width, ret_size, sp_manage_unit,
						      sp_extra_buffer, sp_unit_size);
}

int32_t vs_dpu_get_fescl_sram_size(u8 format, u16 width_scaled, u32 *ret_size)
{
	u32 align_w;

	/*
	 * 1. Each pixel is 6 Bytes
	 * 2. Each address manage 96 pixels, each address 576 Bytes
	 * 3. Need 5 line buffer since Vertical filter is 5Tap
	 * 4. total size should be alinged to 36KBytes
	 */
	if (!ret_size)
		return -EINVAL;
	align_w = vsALIGN_NP2(width_scaled, 96);
	*ret_size = (align_w / 96) * 576 * 5;
	*ret_size = vsALIGN_NP2(*ret_size, ALIGN36KB);

	return 0;
}
