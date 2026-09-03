// NOLINTBEGIN
#include "wlan_ring_adapter.h"
#include "wlan_ring_wrapper.h"
#include "wlan_nep_ring_adaptee.h"
#include "wlan_noa_hw_ring_adaptee.h"

int wlan_ring_adapter_initialize(struct wlan_ring_adapter *const self, uint32_t ring_type)
{
	memset((void *)self, 0, sizeof(struct wlan_ring_adapter));

	if (ring_type >= ADAPTEE_TYPE_MAX || ring_type == ADAPTEE_TYPE_UNDEFINED) {
		return -EINVAL;
	}

	switch (ring_type) {
	case ADAPTEE_TYPE_NEP_TX_RING:
		self->adaptee_ops = wlan_nep_ring_adaptee_get_tx_ops();
		break;
	case ADAPTEE_TYPE_NEP_RX_RING:
		self->adaptee_ops = wlan_nep_ring_adaptee_get_rx_ops();
		break;
	case ADAPTEE_TYPE_NOA_HW_TX_RING:
		self->adaptee_ops = wlan_noa_hw_ring_adaptee_get_tx_ops();
		break;
	case ADAPTEE_TYPE_NOA_HW_RX_RING:
		self->adaptee_ops = wlan_noa_hw_ring_adaptee_get_rx_ops();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

// NOLINTEND
