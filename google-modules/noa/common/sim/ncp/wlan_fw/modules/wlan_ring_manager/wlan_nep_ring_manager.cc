#include "wlan_nep_ring_manager.h"

int32_t WlanNepRingManagerInit(WlanRingManager *const manager)
{
	WlanNepRingManager *nep_ring_manager = GetWlanNepRingManager(manager);

	manager->ring_groups[kTxPostRingGroup].num_ring = NUM_NEP_TX_POST_RING;
	manager->ring_groups[kTxPostRingGroup].ring_pool = nep_ring_manager->tx_post_ring_pool;

	manager->ring_groups[kRxPostRingGroup].num_ring = NUM_NEP_RX_POST_RING;
	manager->ring_groups[kRxPostRingGroup].ring_pool = NULL;

	manager->ring_groups[kTxCmplRingGroup].num_ring = NUM_NEP_TX_CMPL_RING;
	manager->ring_groups[kTxCmplRingGroup].ring_pool = nep_ring_manager->tx_cmpl_ring_pool;

	manager->ring_groups[kRxCmplRingGroup].num_ring = NUM_NEP_RX_CMPL_RING;
	manager->ring_groups[kRxCmplRingGroup].ring_pool = nep_ring_manager->rx_cmpl_ring_pool;

	return 0;
}

void WlanNepRingManagerDeinit(WlanRingManager *const manager)
{
	WlanNepRingManager *nep_ring_manager = GetWlanNepRingManager(manager);
	memset(nep_ring_manager, 0, sizeof(WlanNepRingManager));
}
