#ifndef MODULE_WLAN_NEP_BUFFER_POOL_H
#define MODULE_WLAN_NEP_BUFFER_POOL_H

#include "common/ring.h"
#include "noa_desc.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"

/// forward declaration
struct WlanNepBufferPool;

/// @brief Size of the WLAN NEP TX buffer pool.
#define WLAN_NEP_TX_BUFFER_POOL_SIZE 4096

/// @brief Mask for TX Packet IDs used in NOA.
#define WLAN_NEP_PKTID_MASK 0x9001

/// @brief Enumeration of NEP buffer pool types.
enum NepBufferPoolType {
	kNepBufferPoolTypeStart = 0,
	kNepTxBufferPool = kNepBufferPoolTypeStart,
	kNepBufferPoolTypeEnd,
	kNepBufferPoolTypeNum = kNepBufferPoolTypeEnd,
};

/// @brief Structure defining the operations for a WLAN NEP buffer
/// pool.
typedef struct WlanNepBufferPoolOps {
	/// @brief Initialization function.
	int32_t (*init)(struct WlanNepBufferPool *const buffer_pool);
	/// @brief Deinitialization function.
	void (*deinit)(struct WlanNepBufferPool *const buffer_pool);
} WlanNepBufferPoolOps;

/// @brief Structure representing a generic WLAN NEP buffer pool.
typedef struct WlanNepBufferPool {
	/// @brief Type of the buffer pool.
	enum NepBufferPoolType buffer_pool_type;
	/// @brief Buffer pool operations.
	struct WlanNepBufferPoolOps ops;
	/// @brief Size of the buffer pool.
	uint32_t buffer_pool_size;
	/// @brief Offset for packet IDs.
	uint16_t pktid_offset;
	/// @brief Ring buffer for refill requests.
	struct noa_ring_wrapper refill_ring;
	/// @brief Registers for the ring buffer.
	struct noa_ring_regs ring_regs;
} WlanNepBufferPool;

/// @brief Structure representing a WLAN NEP TX buffer pool.
typedef struct WlanNepTxBufferPool {
	/// @brief Base buffer pool structure.
	struct WlanNepBufferPool base;
	/// @brief Buffer for the refill ring.
	char *refill_ring_buffer;
} WlanNepTxBufferPool;

/// @brief Initializes the WLAN NEP buffer pool.
///
/// @param[in] buffer_pool The buffer pool to initialize.
/// @param[in] type The type of the buffer pool.
/// @return 0 on success, negative error code on failure.
extern int32_t WlanNepBufferPoolInit(struct WlanNepBufferPool *const buffer_pool,
				     const enum NepBufferPoolType type);

/// @brief De-initializes the WLAN NEP buffer pool.
///
/// @param[in] buffer_pool The buffer pool to de-initialize.
extern void WlanNepBufferPoolDeinit(struct WlanNepBufferPool *const buffer_pool);

/// @brief Replenishes the WLAN NEP buffer pool.
///
/// @param[in] buffer_pool The buffer pool to replenish.
/// @param[in] noa_buffer_desc The buffer descriptor.
/// @return 0 on success, negative error code on failure.
extern int32_t WlanNepBufferPoolReplenish(struct WlanNepBufferPool *const buffer_pool,
					  const NoaBufferPoolDesc *noa_buffer_desc);

/// @brief Replenishes the buffer pool with a batch of packet IDs.
///
/// @param[in] buffer_pool A pointer to the WlanNepBufferPool instance to replenish.
/// @param[in] bm_buffer_num The number of buffer descriptor provided in bm_pktid_buf.
/// @param[in] noa_buffer_desc_array A pointer to an array of buffer descriptor to add to the pool.
///
/// @return 0 on success.
/// @return -EAGAIN on failure, indicating that the ring operation could not be
/// completed and may need to be retried.
extern int32_t WlanNepBufferPoolBatchReplenish(struct WlanNepBufferPool *const buffer_pool,
					       uint32_t bm_buffer_num,
					       const NoaBufferPoolDesc *noa_buffer_desc_array);

/// @brief Gets the size of the WLAN NEP buffer pool.
///
/// @param[in] buffer_pool The buffer pool.
/// @return The size of the buffer pool.
extern uint32_t WlanNepBufferPoolGetPoolSize(const struct WlanNepBufferPool *const buffer_pool);

/// @brief Sets the packet ID offset for the WLAN NEP buffer pool.
///
/// @param[in] buffer_pool The buffer pool.
/// @param[in] pktid_offset The packet ID offset.
extern void WlanNepBufferPoolSetPktidOffset(struct WlanNepBufferPool *const buffer_pool,
					    const uint16_t pktid_offset);

/// @brief Retrieves the bm_pktid associated with a given nep_pktid.
///
/// @param[in] buffer_pool Pointer to the NEP buffer pool structure.
/// @param[in] nep_pktid The NEP packet ID to translate.
/// @param[in] bm_pktid Pointer to store the retrieved buffer manager packet
/// ID.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t WlanNepBufferPoolRetrieveBmPktid(struct WlanNepBufferPool *const buffer_pool,
						uint16_t nep_pktid, uint16_t *bm_pktid);

#endif /* MODULE_WLAN_NEP_BUFFER_POOL_H */
