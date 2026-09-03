#ifndef NOA_NETWORK_PIPELINE_SERVICE_OPTION_H_
#define NOA_NETWORK_PIPELINE_SERVICE_OPTION_H_
#ifdef linux
#include "linux/types.h"
#else /* linux */
#include <cstdint>
#endif /* linux */

// A structure to hold all build-time configurable options for the data path.
struct DataPathOptions {
	// If true, cache stages use DRAM rings and the singleton cache engine,
	// which is suitable for performance measurement infrastructure.
	bool is_ci_testing;

	// Modem Device-to-Host Path
	uint8_t modem_d2h_isr_port;
	uint8_t modem_d2h_isr_ring_id_offset;
	uint8_t modem_d2h_isr_buffer_pool_ring_id;
	uint8_t modem_d2h_feedthrough_notifier_port;
	uint8_t modem_d2h_feedback_notifier_port;
	uint8_t modem_d2h_forward_wlan_notifier_port;

	// Modem Host-to-Device Path
	uint8_t modem_h2d_isr_port;
	uint8_t modem_h2d_isr_ring_id;
	uint8_t modem_h2d_feedthrough_notifier_port;
	uint8_t modem_h2d_feedback_notifier_port;

	// WLAN Device-to-Host Path
	uint8_t wlan_d2h_isr_port;
	uint8_t wlan_d2h_isr_ring_id;
	uint8_t wlan_d2h_isr_buffer_pool_ring_id;
	uint8_t wlan_d2h_feedthrough_notifier_port;
	uint8_t wlan_d2h_feedback_notifier_port;
	uint8_t wlan_d2h_forward_wlan_notifier_port;
	uint8_t wlan_d2h_forward_modem_notifier_port;

	// WLAN Host-to-Device Path
	uint8_t wlan_h2d_isr_port;
	uint8_t wlan_h2d_isr_ring_id;
	uint8_t wlan_h2d_feedthrough_notifier_port;
	uint8_t wlan_h2d_feedback_notifier_port;
};

// Retrieves the data path options based on the build configuration.
struct DataPathOptions GetDataPathOptions(void);

#endif // NOA_NETWORK_PIPELINE_SERVICE_OPTION_H_
