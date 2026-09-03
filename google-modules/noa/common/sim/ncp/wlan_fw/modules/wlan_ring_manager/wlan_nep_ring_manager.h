#ifndef MODEULES_WLAN_RING_MANAGER_WLAN_NEP_RING_MANAGER_H
#define MODEULES_WLAN_RING_MANAGER_WLAN_NEP_RING_MANAGER_H

#include "wlan_ring_manager.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"

#define NUM_NEP_TX_POST_RING (1U)
#define NUM_NEP_RX_POST_RING (0U)
#define NUM_NEP_TX_CMPL_RING (1U)
#define NUM_NEP_RX_CMPL_RING (1U)

typedef struct WlanNepRingManager {
	WlanRingManager manager;
	WlanRing tx_post_ring_pool[NUM_NEP_TX_POST_RING];
	WlanRing rx_cmpl_ring_pool[NUM_NEP_RX_CMPL_RING];
	WlanRing tx_cmpl_ring_pool[NUM_NEP_TX_CMPL_RING];
} WlanNepRingManager;

static inline WlanNepRingManager *GetWlanNepRingManager(WlanRingManager *const manager)
{
	return container_of(manager, WlanNepRingManager, manager);
}

extern int32_t WlanNepRingManagerInit(WlanRingManager *const manager);
extern void WlanNepRingManagerDeinit(WlanRingManager *const manager);

#endif /* MODEULES_WLAN_RING_MANAGER_WLAN_NEP_RING_MANAGER_H */
