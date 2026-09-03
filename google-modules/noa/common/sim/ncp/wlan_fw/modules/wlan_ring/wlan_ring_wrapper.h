// NOLINTBEGIN
#ifndef __NOA_WLAN_WLAN_RING_WRAPPER_H__
#define __NOA_WLAN_WLAN_RING_WRAPPER_H__

#include "../../wlan_service_system.h"

#ifndef HOST_SIMULATOR
#include <common/noa_hw_ring.h>
#include "common/ring.h"
#endif /* HOST_SIMULATOR */

struct wlan_input_act {
	uint16_t dst;
	uint16_t head_offset;
	uint16_t data_len;
	uint16_t pktid;
	uint64_t va;
	uint64_t pa;
};

struct wlan_ring_buffer_info {
	size_t size;
	void *desc_buff_vbase;
	dma_addr_t desc_buff_pbase;
};

struct wlan_ring_extension {
	struct wlan_ring_buffer_info buffer_info;
	int available_cnt;
	spinlock_t lock;
};

struct wlan_ring_wrapper {
	union {
#if defined(__cplusplus)
		// FIXME : should be removed after introducing new unified ring lib
		::noa::service::wlan_service::WlanHostSimRing sim_ring;
#endif
#ifndef HOST_SIMULATOR
		struct noa_hw_ring hw_ring;
		struct noa_ring_wrapper noa_ring;
#endif /* HOST_SIMULATOR */
	};
	struct wlan_ring_extension ext;

#if defined(__cplusplus)
	wlan_ring_wrapper()
	{
	}

	~wlan_ring_wrapper()
	{
	}
#endif
};

#endif /* __NOA_WLAN_WLAN_RING_WRAPPER_H__ */
// NOLINTEND
