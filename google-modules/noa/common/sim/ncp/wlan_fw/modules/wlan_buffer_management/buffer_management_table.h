#ifndef BUFFER_MANAGER_BUFFER_MANAGEMENT_TABLE_H
#define BUFFER_MANAGER_BUFFER_MANAGEMENT_TABLE_H

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "modules/memory_map/memory_map.h"

#define MAX_FREE_BUFFER_POOL_SIZE 4096

typedef struct BufferInfo {
	uint16_t pktid;
	uint8_t ownership;
	uint16_t buffer_size;
	uint64_t buffer_addr_phy;
	uint64_t buffer_addr_cpu;
} BufferInfo;

typedef struct BufferManagementFreeBufferPool {
	uint16_t top;
	uint16_t size;
	spinlock_t pool_lock;
	uint16_t free_buf_idx[MAX_FREE_BUFFER_POOL_SIZE];
} BufferManagementFreeBufferPool;

typedef struct BufferManagementTable {
	uint16_t table_size;
	uint16_t pktid_offset;
	uint16_t entry_num;
	uint8_t init_flag;
	bool wlan_fw_sync_request;
	uint32_t refill_bitmap_size; // in bytes
	uint32_t bmes_size; // in bytes

	struct BufferManagementFreeBufferPool free_buf_pool;

	// The following pointers point to the carved out shared memory
	struct BufferManagementTableInfo *table_info;
	uint32_t *refill_bitmap;
	struct BufferManagementEntry *bmes;
} BufferManagementTable;

/// @brief Initializes a buffer management table.
///
/// This function initializes a buffer management table with the
/// provided buffer management table information. It sets up the
/// table's internal data structures and synchronizes the table
/// with the shared memory.
///
/// @param[in] tbl Pointer to the buffer management table to be
/// initialized.
/// @param[in] tbl_info Pointer to the buffer management table
/// information.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t BmtInit(struct BufferManagementTable *const tbl,
		       struct BufferManagementTableInfo *const tbl_info);

/// @brief Deinitializes a buffer management table.
///
/// @param[in] tbl Pointer to the buffer management table to be
/// deinitialized.
extern void BmtDeinit(struct BufferManagementTable *const tbl);

/// @brief Synchronizes the buffer management table.
///
/// This function synchronizes the buffer management table with
/// the shared memory. It checks for any pending buffers in the shared
/// memory and adds them to the available buffer pool.
///
/// @param[in] tbl Pointer to the Buffer Management Table to be
/// synchronized.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t BmtSync(struct BufferManagementTable *const tbl, uint32_t *sync_buffer_num,
		       bool *more);

/// @brief Gets a batch of free buffers from the buffer management
/// table.
///
/// @param[in] tbl Pointer to the Buffer Management Table.
/// @param[in] num_bufs Number to the requested buffer.
/// @param[out] bufs Pointer to store the buffer information of the
/// free buffers.
///
/// @return The number of the allocated buffers, -ENOMEM if no free
/// buffer is available.
extern int32_t BmtBufferBatchAlloc(struct BufferManagementTable *const tbl, const uint32_t num_bufs,
				   struct BufferInfo *bufs);

/// @brief Gets a free buffer from the buffer management table.
///
/// @param[in] tbl Pointer to the Buffer Management Table.
/// @param[out] buf Pointer to store the buffer information of the
/// free buffer.
///
/// @return 0 on success, -ENOMEM if no free buffer is available.
extern int32_t BmtBufferAlloc(struct BufferManagementTable *const tbl, BufferInfo *buf);

/// @brief Gets the buffer associated with a specific packet ID.
///
/// @param[in] tbl Pointer to the Buffer Management Table.
/// @param[in] pktid The packet ID.
/// @param[out] buf Pointer to store the buffer information
///
/// @return The buffer address if found, 0 otherwise.
extern int32_t BmtGetBufferInfo(struct BufferManagementTable *const tbl, const uint16_t pktid,
				BufferInfo *buf);

/// @brief Reclaim a buffer to the buffer management table.
///
/// @param[in] tbl Pointer to the Buffer Management Table.
/// @param[in] pktid The packet ID of the buffer to be replenished.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t BmtReclaim(struct BufferManagementTable *const tbl, const uint16_t pktid);

/// @brief Sets the refill bitmap for a specific packet ID in the
/// buffer management table.
///
/// @param[in] tbl Pointer to the Buffer Management Table.
/// @param[in] pktid The packet ID of the buffer to be replenished.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t BmtSetRefillBitmap(struct BufferManagementTable *const tbl, const uint16_t pktid);

extern int32_t BmtSetBufferOwnership(struct BufferManagementTable *const tbl, uint16_t pktid,
				     BufferOwnership ownership);

extern bool BmtCheckBufferOwnership(struct BufferManagementTable *const tbl, uint16_t pktid,
				    BufferOwnership ownership);

#endif /* BUFFER_MANAGER_BUFFER_MANAGEMENT_TABLE_H */
