#include "ext_svc.h"

#include "sys_if/common.h"
#include "sys_if/types/types.h"

int32_t ExtSvcInit(ExternalServices *const ext_svc, const ExternalServicesInitParams *const params)
{
	memset(ext_svc, 0, sizeof(ExternalServices));

	ext_svc->wlan_rpc_service.send_event_to_apc = params->send_event_to_apc;
	ext_svc->ring_rpc_service.ring_activate = params->activate_wlan_fw_ring;
	ext_svc->ring_rpc_service.ring_input_deactivate = params->deactivate_wlan_fw_input_ring;
	ext_svc->ring_rpc_service.ring_output_deactivate = params->deactivate_wlan_fw_output_ring;
	ext_svc->ring_rpc_service.tx_buffer_pool_activate = params->activate_nep_tx_buffer_pool;
	ext_svc->ring_rpc_service.tx_buffer_pool_deactivate = params->deactivate_nep_tx_buffer_pool;
	ext_svc->net_engine_rpc_service.send_command_to_net_engine =
		params->send_command_to_net_engine;
	ext_svc->noa_system_service.noa_power_vote = params->noa_power_vote;

	return 0;
}

void ExtSvcDeinit(ExternalServices *const ext_svc)
{
	memset(ext_svc, 0, sizeof(ExternalServices));
}
