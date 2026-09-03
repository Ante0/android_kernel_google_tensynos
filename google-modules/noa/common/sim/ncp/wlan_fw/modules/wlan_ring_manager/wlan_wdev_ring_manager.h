#ifndef MODULE_WLAN_RING_MANAGER_WLAN_WDEV_RING_MANAGER_H
#define MODULE_WLAN_RING_MANAGER_WLAN_WDEV_RING_MANAGER_H

#include "wlan_ring_manager.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "modules/wlan_ring/wlan_ring.h"

#if IS_ENABLED(CONFIG_NOA_WLAN_BRCM_SUPPORT)
#define NUM_WDEV_TX_POST_RING (80U)
#define NUM_WDEV_RX_POST_RING (1U)
#define NUM_WDEV_TX_CMPL_RING (1U)
#define NUM_WDEV_RX_CMPL_RING (1U)
#else
#define NUM_WDEV_TX_POST_RING (1U)
#define NUM_WDEV_RX_POST_RING (1U)
#define NUM_WDEV_TX_CMPL_RING (1U)
#define NUM_WDEV_RX_CMPL_RING (1U)
#endif

typedef struct WlanWdevRingManager {
	WlanRingManager manager;
	WlanRing tx_post_ring_pool[NUM_WDEV_TX_POST_RING];
	WlanRing rx_post_ring_pool[NUM_WDEV_RX_POST_RING];
	WlanRing tx_cmpl_ring_pool[NUM_WDEV_TX_CMPL_RING];
	WlanRing rx_cmpl_ring_pool[NUM_WDEV_RX_CMPL_RING];
} WlanWdevRingManager;

static inline WlanWdevRingManager *GetWlanWdevRingManager(WlanRingManager *const manager)
{
	return container_of(manager, WlanWdevRingManager, manager);
}

extern int32_t WlanWdevRingManagerInit(WlanRingManager *const manager);
extern void WlanWdevRingManagerDeinit(WlanRingManager *const manager);

#endif /* MODULE_WLAN_RING_MANAGER_WLAN_WDEV_RING_MANAGER_H */
