#ifndef MODULE_WLAN_NEP_TX_BUFFER_POOL_H
#define MODULE_WLAN_NEP_TX_BUFFER_POOL_H

#include "wlan_nep_buffer_pool.h"

/// @brief Initializes the WLAN NEP TX buffer pool.
///
/// @param[in] buffer_pool The TX buffer pool to initialize.
/// @return 0 on success, negative error code on failure.
extern int32_t WlanNepTxBufferPoolInit(struct WlanNepBufferPool *const buffer_pool);

/// @brief De-initializes the WLAN NEP TX buffer pool.
///
/// @param[in] buffer_pool The TX buffer pool to initialize.
extern void WlanNepTxBufferPoolDeinit(struct WlanNepBufferPool *const buffer_pool);

/// @brief Updates the WLAN NEP TX buffer pool table.
///
/// @param[in] buffer_pool The TX buffer pool to initialize.
/// @param[in] pktid The packet ID.
/// @param[in] phy_addr The physical address.
/// @param[in] cpu_addr The CPU address.
/// @return 0 on success, negative error code on failure.
extern int32_t WlanNepTxBufferPoolUpdateTable(struct WlanNepBufferPool *const buffer_pool,
					      const uint16_t pktid, const uint64_t phy_addr,
					      const uintptr_t cpu_addr);

#endif /* MODULE_WLAN_NEP_TX_BUFFER_POOL_H */
