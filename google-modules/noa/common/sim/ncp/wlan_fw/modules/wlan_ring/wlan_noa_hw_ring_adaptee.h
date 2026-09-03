// NOLINTBEGIN
#ifndef __NOA_WLAN_WLAN_NOA_HW_RING_ADAPTEE_H__
#define __NOA_WLAN_WLAN_NOA_HW_RING_ADAPTEE_H__

#include "wlan_ring_wrapper.h"

struct wlan_noa_hw_ring_adaptee_plat_params {
	void *(*malloc)(size_t size, dma_addr_t *const dma_addr);
	void (*free)(size_t size, void *const cpu_addr, dma_addr_t *const dma_addr);
};

extern int wlan_noa_hw_ring_adaptee_plat_init(struct wlan_noa_hw_ring_adaptee_plat_params *params);
extern const struct wlan_ring_adaptee_ops *wlan_noa_hw_ring_adaptee_get_tx_ops(void);
extern const struct wlan_ring_adaptee_ops *wlan_noa_hw_ring_adaptee_get_rx_ops(void);

#endif /* __NOA_WLAN_WLAN_NOA_HW_RING_ADAPTEE_H__ */
// NOLINTEND
