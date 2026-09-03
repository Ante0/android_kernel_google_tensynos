#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <common/core.h>
#include <common/wlan/noa_wlan.h>
#include "wlan_debug_controller_client.h"
#include "wlan/noa_wlan_cfg_space.h"
#include <wlan/wlan_rpc_service/noa_wlan_cmd_dispatch.h>
#include <common/wlan_ring_id.h>
#include "ring_service/ring_mgmt/ring_manager.h"
#include "wlan_debug_trace.h"

static inline void
wlan_dbg_shared_info_fetcher_intr_info(struct noa_wlan_client *client)
{
	noa_wlan_cfg_update_noa_interrupt_state_info(client);
}

static inline ssize_t
wlan_dbg_shared_info_fetcher_nep_ring(struct noa_wlan_client *client)
{
	struct noa_wlan_ring_info ring_info;
	struct noa_ring_wrapper *tx_ring_wrapper = &client->tx_data_ring.ring;
	struct noa_ring_wrapper *rx_ring_wrapper = &client->rx_data_ring.ring;

	/* TX Ring */
	memset(&ring_info, 0, sizeof(ring_info));
	ring_info.regs = tx_ring_wrapper->regs;
	ring_info.ndesc = tx_ring_wrapper->basic.size;
	ring_info.desc_sz = tx_ring_wrapper->basic.item_len;
	ring_info.dma_va = (u64)tx_ring_wrapper->basic.base;
	ring_info.dma_pa = (u64)tx_ring_wrapper->basic.dpa_base;
	strncpy(ring_info.name, tx_ring_wrapper->name, sizeof(ring_info.name) - 1);

	noa_wlan_cfg_update_nep_ring_info(client, 1, RING_TYPE_TX_DATA, &ring_info);

	/* RX Ring */
	memset(&ring_info, 0, sizeof(ring_info));
	ring_info.regs = rx_ring_wrapper->regs;
	ring_info.ndesc = rx_ring_wrapper->basic.size;
	ring_info.desc_sz = rx_ring_wrapper->basic.item_len;
	ring_info.dma_va = (u64)rx_ring_wrapper->basic.base;
	ring_info.dma_pa = (u64)rx_ring_wrapper->basic.dpa_base;
	strncpy(ring_info.name, rx_ring_wrapper->name, sizeof(ring_info.name) - 1);

	noa_wlan_cfg_update_nep_ring_info(client, 1, RING_TYPE_RX_DATA, &ring_info);
	return 0;
}

static inline void wlan_dbg_shared_info_fetcher_mib(struct noa_wlan_client *client
						     )
{
	noa_wlan_cfg_update_noa_wlan_mib_info(client);
}

void wlan_debug_sync_local_copy_to_shared_mem(struct noa_wlan_client *client)
{
	wlan_dbg_shared_info_fetcher_mib(client);
	wlan_dbg_shared_info_fetcher_nep_ring(client);
	wlan_dbg_shared_info_fetcher_intr_info(client);
}
