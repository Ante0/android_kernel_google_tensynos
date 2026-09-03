#ifdef linux
#include "network_pipeline_service/option.h"
#else /* linux */
#include "network_pipeline_service_private/option.h"
#endif /* linux */

#include "common/core.h"
#include "common/modem_ring_id.h"
#include "common/wlan_ring_id.h"

struct DataPathOptions GetDataPathOptions(void)
{
	struct DataPathOptions options = {
		.is_ci_testing = false,

		// Modem D2H
		.modem_d2h_isr_port = NOA_PORT_MODEM_FW,
		.modem_d2h_isr_ring_id_offset = 0,
		.modem_d2h_isr_buffer_pool_ring_id = kNoaModemNepBufferPool,
		.modem_d2h_feedthrough_notifier_port = NOA_PORT_MODEM_SW,
		.modem_d2h_feedback_notifier_port = NOA_PORT_MODEM_FW,
		.modem_d2h_forward_wlan_notifier_port = NOA_PORT_WLAN_FW,

		// Modem H2D
		.modem_h2d_isr_port = NOA_PORT_MODEM_SW,
		.modem_h2d_isr_ring_id = kNoaModemRingTxData,
		.modem_h2d_feedthrough_notifier_port = NOA_PORT_MODEM_FW,
		.modem_h2d_feedback_notifier_port = NOA_PORT_MODEM_SW,

		// WLAN D2H
		.wlan_d2h_isr_port = NOA_PORT_WLAN_FW,
		.wlan_d2h_isr_ring_id = kNoaWlanRingRxData,
		.wlan_d2h_isr_buffer_pool_ring_id = kNoaWlanNepBufferPool,
		.wlan_d2h_feedthrough_notifier_port = NOA_PORT_WLAN_SW,
		.wlan_d2h_feedback_notifier_port = NOA_PORT_WLAN_FW,
		.wlan_d2h_forward_wlan_notifier_port = NOA_PORT_WLAN_FW,
		.wlan_d2h_forward_modem_notifier_port = NOA_PORT_MODEM_FW,

		// WLAN H2D
		.wlan_h2d_isr_port = NOA_PORT_WLAN_SW,
		.wlan_h2d_isr_ring_id = kNoaWlanRingTxData,
		.wlan_h2d_feedthrough_notifier_port = NOA_PORT_WLAN_FW,
		.wlan_h2d_feedback_notifier_port = NOA_PORT_WLAN_SW,
	};

	return options;
}
