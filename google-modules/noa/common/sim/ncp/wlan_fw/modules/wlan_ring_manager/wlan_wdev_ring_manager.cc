#include "wlan_wdev_ring_manager.h"

int32_t WlanWdevRingManagerInit(WlanRingManager *const manager)
{
	WlanWdevRingManager *wdev_ring_manager = GetWlanWdevRingManager(manager);

	manager->ring_groups[kTxPostRingGroup].num_ring = NUM_WDEV_TX_POST_RING;
	manager->ring_groups[kTxPostRingGroup].ring_pool = wdev_ring_manager->tx_post_ring_pool;

	manager->ring_groups[kRxPostRingGroup].num_ring = NUM_WDEV_RX_POST_RING;
	manager->ring_groups[kRxPostRingGroup].ring_pool = wdev_ring_manager->rx_post_ring_pool;

	manager->ring_groups[kTxCmplRingGroup].num_ring = NUM_WDEV_TX_CMPL_RING;
	manager->ring_groups[kTxCmplRingGroup].ring_pool = wdev_ring_manager->tx_cmpl_ring_pool;

	manager->ring_groups[kRxCmplRingGroup].num_ring = NUM_WDEV_RX_CMPL_RING;
	manager->ring_groups[kRxCmplRingGroup].ring_pool = wdev_ring_manager->rx_cmpl_ring_pool;

	return 0;
}

void WlanWdevRingManagerDeinit(WlanRingManager *const manager)
{
	WlanWdevRingManager *wdev_ring_manager = GetWlanWdevRingManager(manager);
	memset(wdev_ring_manager, 0, sizeof(WlanWdevRingManager));
}
