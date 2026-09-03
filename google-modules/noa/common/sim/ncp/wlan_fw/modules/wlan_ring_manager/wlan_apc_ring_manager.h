#ifndef MODEULES_WLAN_RING_MANAGER_WLAN_APC_RING_MANAGER_H
#define MODEULES_WLAN_RING_MANAGER_WLAN_APC_RING_MANAGER_H

#include "wlan_ring_manager.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"

#define NUM_APC_TX_POST_RING (1U)
#define NUM_APC_RX_POST_RING (1U)
#define NUM_APC_TX_CMPL_RING (1U)
#define NUM_APC_RX_CMPL_RING (1U)

typedef struct WlanApcRingManager {
	WlanRingManager manager;
	WlanRing tx_post_ring_pool[NUM_APC_TX_POST_RING];
	WlanRing tx_cmpl_ring_pool[NUM_APC_TX_CMPL_RING];
	WlanRing rx_post_ring_pool[NUM_APC_RX_POST_RING];
	WlanRing rx_cmpl_ring_pool[NUM_APC_RX_CMPL_RING];
} WlanApcRingManager;

static inline WlanApcRingManager *GetWlanApcRingManager(WlanRingManager *const manager)
{
	return container_of(manager, WlanApcRingManager, manager);
}

extern int32_t WlanApcRingManagerInit(WlanRingManager *const manager);
extern void WlanApcRingManagerDeinit(WlanRingManager *const manager);

#endif /* MODEULES_WLAN_RING_MANAGER_WLAN_APC_RING_MANAGER_H */
