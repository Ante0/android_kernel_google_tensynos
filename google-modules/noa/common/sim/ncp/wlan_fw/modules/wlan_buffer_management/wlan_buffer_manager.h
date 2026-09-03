#ifndef BUFFER_MANAGER_WLAN_BUFFER_MANAGER_H
#define BUFFER_MANAGER_WLAN_BUFFER_MANAGER_H

#include "sys_if/types/types.h"
#include "sys_if/common.h"

typedef enum BufferManagerType {
	kBufferManagerTypeStart = 0,
	kVendorRxBufferManager = kBufferManagerTypeStart,
	kNoaTxBufferManager,
	kBufferManagerTypeEnd,
	kBufferManagerTypeNum = kBufferManagerTypeEnd,
} BufferManagerType;

/// @brief Defines the lifecycle states of a buffer locker.
typedef enum LockerState {
	/// @brief The locker is free and can be acquired by a new buffer.
	/// This is the default state for an unused or released locker.
	kLockerStateAvailable = 0,
	/// @brief The locker is currently assigned to a buffer and is in active use.
	/// An occupied locker holds valid buffer metadata.
	kLockerStateOccupied,
	/// @brief The locker is assigned but is temporarily paused or held.
	/// An on-hold locker cannot be released until it is moved back to the
	/// 'occupied' state.
	kLockerStateOnHold,
} LockerState;

typedef struct BmTkidItem {
	uint8_t state;
	uint8_t rsv[3];
	uint16_t tkid;
	uint16_t size;
	uint64_t pa;
	uint64_t va;
} BmTkidItem;

/// @brief Initializes a specific buffer manager instance.
///
/// @param[in] bm_type The type of buffer manager to initialize
/// @return 0 on success, or a negative error code on failure.
extern int32_t BmInit(BufferManagerType bm_type);

/// @brief Deinitializes a buffer manager, freeing all associated resources.
///
/// @param[in] bm_type The type of buffer manager to deinitialize.
extern void BmDeinit(BufferManagerType bm_type);

/// @brief Finds a registered buffer's information
///
/// @param[in] bm_type The buffer manager instance to search within.
/// @param[in] tkid The tkid of the buffer to find.
/// @param[out] buf_info A pointer to a pointer that will be updated to point
/// to the found BmTkidItem structure.
/// @return 0 if the buffer is found, or a negative error code if not found.
extern int32_t BmFind(BufferManagerType bm_type, uint16_t tkid, const BmTkidItem **buf_info);

/// @brief Registers a new buffer with a specific buffer manager.
///
/// @param[in] bm_type The buffer manager instance to register the buffer with.
/// @param[in] tkid The unique tkid for the new buffer.
/// @param[in] buf_size The size of the buffer in bytes.
/// @param[in] pa The physical address of the buffer.
/// @param[in] va The virtual address of the buffer.
/// @return 0 on success, or a negative error code if registration fails.
extern int32_t BmAcquire(BufferManagerType bm_type, uint16_t tkid, uint16_t buf_size, uint64_t pa,
			 uint64_t va);

/// @brief Removes a buffer's registration from a buffer manager using its TKID.
/// @param[in] bm_type The buffer manager instance to remove the buffer from.
/// @param[in] tkid The tkid of the buffer to remove.
extern void BmRelease(BufferManagerType bm_type, uint16_t tkid);

/// @brief  Moves an occupied buffer locker to an on-hold state.
///
/// @param[in] bm_type The buffer manager pool to operate on.
/// @param[in] tkid The Tracker ID of the buffer to place on hold.
/// @return 0 on success, or a negative error code if registration fails.
extern int32_t BmPlaceOnHold(BufferManagerType bm_type, uint16_t tkid);

/// @brief Reactivates a buffer, returning it from an 'on-hold' to 'occupied' state.
///
/// @param[in] tkid The Tracker ID of the buffer to reactivate.
/// @return 0 on success, or a negative error code if registration fails.
extern int32_t BmReactivate(BufferManagerType bm_type, uint16_t tkid);

#endif /* BUFFER_MANAGER_WLAN_BUFFER_MANAGER_H */
