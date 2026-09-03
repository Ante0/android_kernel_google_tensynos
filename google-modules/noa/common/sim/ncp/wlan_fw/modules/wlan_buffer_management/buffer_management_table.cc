#include "buffer_management_table.h"

#include "wlan_log/wlan_log.h"
#include "sys_if/memory/sys_if_memory.h"
#include "wlan_cast.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"

static uint16_t BmtPktidToEntryIdx(struct BufferManagementTable *const tbl, const uint16_t pktid)
{
	return pktid - tbl->pktid_offset;
}

static void BmFbpInit(struct BufferManagementFreeBufferPool *const pool, const uint16_t size)
{
	memset(pool, 0, sizeof(struct BufferManagementFreeBufferPool));
	pool->size = size;
	spin_lock_init(&pool->pool_lock);
}

static int32_t BmFbpAdd(struct BufferManagementFreeBufferPool *const pool, const uint16_t idx)
{
	uintptr_t lock_flags = 0;

	spin_lock_irqsave(&pool->pool_lock, lock_flags);
	if (pool->top < pool->size) {
		pool->free_buf_idx[pool->top] = idx;
		pool->top++;
		spin_unlock_irqrestore(&pool->pool_lock, lock_flags);
		return 0;
	}
	spin_unlock_irqrestore(&pool->pool_lock, lock_flags);
	return -ENOMEM;
}

static int32_t BmFbpAlloc(struct BufferManagementFreeBufferPool *const pool, uint16_t *idx)
{
	uintptr_t lock_flags = 0;

	spin_lock_irqsave(&pool->pool_lock, lock_flags);
	if (pool->top == 0) {
		spin_unlock_irqrestore(&pool->pool_lock, lock_flags);
		return -ENOMEM;
	}

	*idx = pool->free_buf_idx[pool->top - 1];
	pool->top--;
	spin_unlock_irqrestore(&pool->pool_lock, lock_flags);
	return 0;
}

static void BmtAttachTable(struct BufferManagementTable *const tbl,
			   struct BufferManagementTableInfo *const table_info)
{
	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, table_info),
			   sizeof(struct BufferManagementTableInfo));

	tbl->table_info = table_info;
	tbl->entry_num = table_info->entry_num;
	tbl->pktid_offset = table_info->pktid_offset;
	tbl->refill_bitmap = WLAN_REINTERPRET_CAST(uint32_t *, table_info->refill_bitmap_addr);
	tbl->bmes = WLAN_REINTERPRET_CAST(struct BufferManagementEntry *, table_info->bmes_addr);
	tbl->refill_bitmap_size = table_info->refill_bitmap_size;
	tbl->bmes_size = table_info->bmes_size;
}

static bool BmtSanityCheck(struct BufferManagementTable *const tbl)
{
	// Verify that the provided addresses are not null pointers.
	if (!tbl->refill_bitmap || !tbl->bmes) {
		return false;
	}

	// Verify that the provided sizees are appropriate.
	if (tbl->refill_bitmap_size < BITS_TO_UINT32S(tbl->entry_num) * sizeof(uint32_t)) {
		return false;
	}

	if (tbl->bmes_size < tbl->entry_num * sizeof(struct BufferManagementEntry)) {
		return false;
	}

	return true;
}

int32_t BmtInit(struct BufferManagementTable *const tbl,
		struct BufferManagementTableInfo *const table_info)
{
	memset(tbl, 0, sizeof(struct BufferManagementTable));

	BmFbpInit(&tbl->free_buf_pool, MAX_FREE_BUFFER_POOL_SIZE);
	BmtAttachTable(tbl, table_info);

	if (!BmtSanityCheck(tbl)) {
		// TODO(b/370880556): Report the reason for initialization
		// failure to the WLAN debug framework when available.
		WLAN_LOG_ERROR(Bm, "%s(): buffertable validity check failed.", __func__);
		return -EINVAL;
	} else {
		tbl->init_flag = 1;
	}

	return 0;
}

void BmtDeinit(struct BufferManagementTable *const tbl)
{
	uint32_t i;

	if (!tbl->init_flag) {
		return;
	}

	// Return all buffer back to APC if buffer ownership is with NCP
	if (tbl->bmes) {
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, tbl->bmes), tbl->bmes_size);
		for (i = 0; i < tbl->entry_num; i++) {
			if (tbl->bmes[i].ownership == kBufferOwnershipWlanFw) {
				tbl->bmes[i].ownership = kBufferOwnershipWlanSw;
			}
		}
		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, tbl->bmes), tbl->bmes_size);
	}

	memset(tbl, 0, sizeof(struct BufferManagementTable));
}

int32_t BmtSync(struct BufferManagementTable *const tbl, uint32_t *sync_buffer_num, bool *more)
{
	struct BufferManagementEntry *entry;
	uint16_t idx, group_idx;
	uint16_t group_num = BITS_TO_UINT32S(tbl->entry_num);
	uint32_t refill_bitmap;
	uint16_t entry_idx;
	uint32_t sync_num = 0;

	*more = false;

	if (!tbl->init_flag) {
		return -ENODEV;
	}

	SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, &tbl->table_info->wlan_sw_sync_request),
			   sizeof(uint8_t));
	if (tbl->table_info->wlan_sw_sync_request || tbl->wlan_fw_sync_request) {
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, tbl->refill_bitmap),
				   tbl->refill_bitmap_size);
		for (group_idx = 0; group_idx < group_num; group_idx++) {
			refill_bitmap = tbl->refill_bitmap[group_idx];
			if (refill_bitmap) {
				// Check refill bitmap
				for (idx = 0; refill_bitmap && idx < UINT32_WIDTH; idx++) {
					entry_idx = group_idx * UINT32_WIDTH + idx;

					if (TEST_BIT(tbl->refill_bitmap, entry_idx)) {
						if (BmFbpAdd(&tbl->free_buf_pool, entry_idx)) {
							*more = true;
							break;
						} else {
							entry = &tbl->bmes[entry_idx];
							SysIfInvalidDCache(
								WLAN_REINTERPRET_CAST(PhyAddr,
										      entry),
								sizeof(struct BufferManagementEntry));
							entry->ownership = kBufferOwnershipWlanFw;
							SysIfFlushDCache(
								WLAN_REINTERPRET_CAST(PhyAddr,
										      entry),
								sizeof(struct BufferManagementEntry));
							sync_num++;
							CLEAR_BIT(tbl->refill_bitmap, entry_idx);
							// WLAN_LOG_DEBUG(
							// 	Bm,
							// 	"%s(): sync entry_idx: %" PRIu16,
							// 	__func__, entry_idx);
						}
					}
				}
			}
		}
		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, tbl->refill_bitmap),
				 tbl->refill_bitmap_size);
		tbl->wlan_fw_sync_request = 0;
		tbl->table_info->wlan_sw_sync_request = 0;
		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr,
						       &tbl->table_info->wlan_sw_sync_request),
				 sizeof(uint8_t));
	}

	*sync_buffer_num = sync_num;

	return 0;
}

int32_t BmtBufferBatchAlloc(struct BufferManagementTable *const tbl, const uint32_t num_bufs,
			    BufferInfo *bufs)
{
	struct BufferManagementEntry *entry;
	uint32_t bufs_idx;
	uint16_t entry_idx;

	if (!tbl->init_flag) {
		return -ENODEV;
	}

	if (!num_bufs) {
		return -EINVAL;
	}

	for (bufs_idx = 0; bufs_idx < num_bufs; bufs_idx++) {
		if (BmFbpAlloc(&tbl->free_buf_pool, &entry_idx)) {
			break;
		}

		entry = &tbl->bmes[entry_idx];
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, entry),
				   sizeof(struct BufferManagementEntry));
		bufs[bufs_idx].pktid = entry->pktid;
		bufs[bufs_idx].buffer_addr_cpu = entry->buffer_addr_cpu;
		bufs[bufs_idx].buffer_addr_phy = entry->buffer_addr_phy;
		bufs[bufs_idx].buffer_size = entry->buffer_size;

		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, entry),
				 sizeof(struct BufferManagementEntry));
	}

	if (bufs_idx == 0) {
		return -ENOMEM;
	}

	return bufs_idx;
}

int32_t BmtBufferAlloc(struct BufferManagementTable *const tbl, struct BufferInfo *buf)
{
	if (!tbl->init_flag) {
		return -ENODEV;
	}

	return BmtBufferBatchAlloc(tbl, 1, buf) == 1 ? 0 : -ENOMEM;
}

int32_t BmtGetBufferInfo(struct BufferManagementTable *const tbl, const uint16_t pktid,
			 struct BufferInfo *buf)
{
	uint16_t entry_idx = BmtPktidToEntryIdx(tbl, pktid);

	if (!tbl->init_flag) {
		return -ENODEV;
	}

	if (entry_idx < tbl->entry_num) {
		struct BufferManagementEntry *entry = &tbl->bmes[entry_idx];
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, entry),
				   sizeof(struct BufferManagementEntry));
		buf->pktid = entry->pktid;
		buf->ownership = entry->ownership;
		buf->buffer_addr_cpu = entry->buffer_addr_cpu;
		buf->buffer_addr_phy = entry->buffer_addr_phy;
		buf->buffer_size = entry->buffer_size;
	} else {
		WLAN_LOG_WARN(Bm, "%s(): unknown pktid: %" PRIu16, __func__, pktid);
		return -EINVAL;
	}

	return 0;
}

int32_t BmtReclaim(struct BufferManagementTable *const tbl, const uint16_t pktid)
{
	uint16_t entry_idx;

	if (!tbl->init_flag) {
		return -ENODEV;
	}

	entry_idx = BmtPktidToEntryIdx(tbl, pktid);
	if (entry_idx < tbl->entry_num) {
		struct BufferManagementEntry *entry = &tbl->bmes[entry_idx];
		WLAN_LOG_DEBUG(Bm, "%s(): reclaim pktid: %" PRIu32, __func__, pktid);
		entry->ownership = kBufferOwnershipWlanFw;
		BmFbpAdd(&tbl->free_buf_pool, entry_idx);
		return 0;
	} else {
		WLAN_LOG_WARN(Bm, "%s(): unknown pktid: %" PRIu32, __func__, pktid);
	}

	return -EINVAL;
}

int32_t BmtSetRefillBitmap(struct BufferManagementTable *const tbl, const uint16_t pktid)
{
	uint16_t entry_idx;

	if (!tbl->init_flag) {
		return -ENODEV;
	}

	entry_idx = BmtPktidToEntryIdx(tbl, pktid);
	if (entry_idx < tbl->entry_num) {
		SET_BIT(tbl->refill_bitmap, entry_idx);
		return 0;
	} else {
		WLAN_LOG_WARN(Bm, "%s(): unknown pktid: %" PRIu32, __func__, pktid);
	}

	return -EINVAL;
}

int32_t BmtSetBufferOwnership(struct BufferManagementTable *const tbl, uint16_t pktid,
			      BufferOwnership ownership)
{
	uint16_t entry_idx;

	if (!tbl->init_flag) {
		return -ENODEV;
	}

	entry_idx = BmtPktidToEntryIdx(tbl, pktid);
	if (entry_idx < tbl->entry_num) {
		struct BufferManagementEntry *entry = &tbl->bmes[entry_idx];
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, entry),
				   sizeof(struct BufferManagementEntry));
		entry->ownership = ownership;
		SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, entry),
				 sizeof(struct BufferManagementEntry));
		return 0;
	} else {
		WLAN_LOG_WARN(Bm, "%s(): unknown pktid: %" PRIu32, __func__, pktid);
	}

	return -EINVAL;
}

bool BmtCheckBufferOwnership(struct BufferManagementTable *const tbl, uint16_t pktid,
			     BufferOwnership ownership)
{
	uint16_t entry_idx;

	if (!tbl->init_flag) {
		return -ENODEV;
	}

	entry_idx = BmtPktidToEntryIdx(tbl, pktid);
	if (entry_idx < tbl->entry_num) {
		struct BufferManagementEntry *entry = &tbl->bmes[entry_idx];
		SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, entry),
				   sizeof(struct BufferManagementEntry));
		if (entry->ownership == ownership) {
			return true;
		} else {
			WLAN_LOG_WARN(Bm,
				      "%s(): expected ownership: %" PRIu32
				      " != actual ownership: %" PRIu32,
				      __func__, ownership, entry->ownership);
		}
	} else {
		WLAN_LOG_WARN(Bm, "%s(): unknown pktid: %" PRIu32, __func__, pktid);
	}

	return false;
}
