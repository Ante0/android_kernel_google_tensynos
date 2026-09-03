// NOLINTBEGIN
#ifndef __NOA_WLAN_WLAN_NEP_RING_ADAPTEE_H__
#define __NOA_WLAN_WLAN_NEP_RING_ADAPTEE_H__

#include "wlan_ring_wrapper.h"

struct wlan_nep_ring_adaptee_plat_params {
	void *(*malloc)(size_t size, dma_addr_t *const dma_addr);
	void (*free)(size_t size, void *const cpu_addr, dma_addr_t *const dma_addr);
	// nep_ring_activate/deactivate will have different definitions depending
	// on the system. In driver mode, noa_ring_activae and noa_ring_deactivate
	// will be used, but in NOAH the nep ring service RPC will be used.
	int (*nep_ring_activate)(struct noa_ring_wrapper *ring);
	int (*nep_ring_deactivate)(struct noa_ring_wrapper *ring);
};

extern int wlan_nep_ring_adaptee_plat_init(struct wlan_nep_ring_adaptee_plat_params *params);

extern const struct wlan_ring_adaptee_ops *wlan_nep_ring_adaptee_get_tx_ops(void);
extern const struct wlan_ring_adaptee_ops *wlan_nep_ring_adaptee_get_rx_ops(void);

#endif /* __NOA_WLAN_WLAN_NEP_RING_ADAPTEE_H__ */
// NOLINTEND
