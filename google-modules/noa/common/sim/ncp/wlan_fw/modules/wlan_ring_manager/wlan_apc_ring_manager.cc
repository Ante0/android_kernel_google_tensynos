#include "wlan_apc_ring_manager.h"

int32_t WlanApcRingManagerInit(WlanRingManager *const manager)
{
	WlanApcRingManager *apc_ring_manager = GetWlanApcRingManager(manager);

	manager->ring_groups[kTxPostRingGroup].num_ring = NUM_APC_TX_POST_RING;
	manager->ring_groups[kTxPostRingGroup].ring_pool = apc_ring_manager->tx_post_ring_pool;

	manager->ring_groups[kRxPostRingGroup].num_ring = NUM_APC_RX_POST_RING;
	manager->ring_groups[kRxPostRingGroup].ring_pool = apc_ring_manager->rx_post_ring_pool;

	manager->ring_groups[kTxCmplRingGroup].num_ring = NUM_APC_TX_CMPL_RING;
	manager->ring_groups[kTxCmplRingGroup].ring_pool = apc_ring_manager->tx_cmpl_ring_pool;

	manager->ring_groups[kRxCmplRingGroup].num_ring = NUM_APC_RX_CMPL_RING;
	manager->ring_groups[kRxCmplRingGroup].ring_pool = apc_ring_manager->rx_cmpl_ring_pool;

	return 0;
}

void WlanApcRingManagerDeinit(WlanRingManager *const manager)
{
	WlanApcRingManager *apc_ring_manager = GetWlanApcRingManager(manager);
	memset(apc_ring_manager, 0, sizeof(WlanApcRingManager));
}
