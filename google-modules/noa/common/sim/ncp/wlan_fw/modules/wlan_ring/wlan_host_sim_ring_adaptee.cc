#include "wlan_host_sim_ring_adaptee.h"
#include "wlan_ring_adapter.h"
#include "wlan_ring_wrapper.h"

const struct wlan_ring_adaptee_ops *wlan_host_sim_ring_adaptee_get_tx_ops()
{
	static struct wlan_ring_adaptee_ops wlan_host_sim_ring_adaptee_tx_ops = {
		.begin_processing = [](struct wlan_ring_wrapper *const) -> int { return 0; },
		.complete_processing = [](struct wlan_ring_wrapper *const) -> int { return 0; },
		.read = nullptr,
		.write = [](struct wlan_ring_wrapper *const adaptee, void *const data,
			    size_t len) -> int {
			::noa::service::wlan_service::WlanHostSimRing *sim_ring =
				&adaptee->sim_ring;
			return sim_ring->Write(data, len);
		},
		.tail_inc = nullptr,
		.tail_rollback = nullptr,
		.is_empty = [](struct wlan_ring_wrapper *const adaptee) -> bool {
			::noa::service::wlan_service::WlanHostSimRing *sim_ring =
				&adaptee->sim_ring;
			return sim_ring->GetWriteCount() == 0;
		},
		.init = [](struct wlan_ring_wrapper *const adaptee, const void *const) -> int {
			::noa::service::wlan_service::WlanHostSimRing *sim_ring =
				&adaptee->sim_ring;
			return sim_ring->Init();
		},
		.exit = nullptr,
		.activate = nullptr,
		.get_name = [](struct wlan_ring_wrapper *const) -> const char * {
			return "HOST_SIM_TX_RING";
		},
		.is_active = [](struct wlan_ring_wrapper *const) -> bool { return true; },
		.get_hw_idx = [](struct wlan_ring_wrapper *const) -> uint32_t { return 0; },
		.get_write_idx = [](struct wlan_ring_wrapper *const adaptee) -> uint32_t {
			::noa::service::wlan_service::WlanHostSimRing *sim_ring =
				&adaptee->sim_ring;
			return sim_ring->GetWriteIdx();
		},
		.get_sn = nullptr,
		.sn_inc = nullptr,
	};

	return &wlan_host_sim_ring_adaptee_tx_ops;
}

const struct wlan_ring_adaptee_ops *wlan_host_sim_ring_adaptee_get_rx_ops()
{
	static struct wlan_ring_adaptee_ops wlan_host_sim_ring_adaptee_rx_ops = {
		.begin_processing = [](struct wlan_ring_wrapper *const) -> int { return 0; },
		.complete_processing = [](struct wlan_ring_wrapper *const) -> int { return 0; },
		.read = [](struct wlan_ring_wrapper *const adaptee, void *const data,
			   size_t len) -> int {
			::noa::service::wlan_service::WlanHostSimRing *sim_ring =
				&adaptee->sim_ring;
			return sim_ring->Read(data, len);
		},
		.write = nullptr,
		.tail_inc = nullptr,
		.tail_rollback = nullptr,
		.is_empty = [](struct wlan_ring_wrapper *const adaptee) -> bool {
			::noa::service::wlan_service::WlanHostSimRing *sim_ring =
				&adaptee->sim_ring;
			return sim_ring->GetReadCount() == 0;
		},
		.init = [](struct wlan_ring_wrapper *const adaptee, const void *const) -> int {
			::noa::service::wlan_service::WlanHostSimRing *sim_ring =
				&adaptee->sim_ring;
			return sim_ring->Init();
		},
		.exit = nullptr,
		.activate = nullptr,
		.get_name = [](struct wlan_ring_wrapper *const) -> const char * {
			return "HOST_SIM_RX_RING";
		},
		.is_active = [](struct wlan_ring_wrapper *const) -> bool { return true; },
		.get_hw_idx = [](struct wlan_ring_wrapper *const) -> uint32_t { return 0; },
		.get_write_idx = nullptr,
		.get_sn = nullptr,
		.sn_inc = nullptr,
	};

	return &wlan_host_sim_ring_adaptee_rx_ops;
}
