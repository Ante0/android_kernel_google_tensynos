#include "net/vpn_pipeline_service.h"

#include <type_traits>

#include "net/nep.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#ifdef USE_NESTED_RING_SERVICE
#include "vpn/nested_ring_pipeline_service_adaptor.h"
#else /* USE_NESTED_RING_SERVICE */
#include "vpn/pipeline_service_adaptor.h"
#endif /* USE_NESTED_RING_SERVICE */
#include "vpn/vpn_controller.h"

namespace vpn = noa::module::vpn;

namespace
{

template <typename RingServiceAdaptor = vpn::VpnController::RingServiceAdaptor>
vpn::PipelineServiceAdaptor *GetVpnPipelineServiceAdaptor()
{
	noa::apps::nep::netengine::Nep *nep = noa::apps::nep::netengine::Nep::GetService();
	if (nep == nullptr || nep->GetVpnController() == nullptr) {
		return nullptr;
	}

	if constexpr (!std::is_same_v<RingServiceAdaptor, vpn::PipelineServiceAdaptor>) {
		return nullptr;
	} else {
		RingServiceAdaptor *ring_service_adaptor =
			&nep->GetVpnController()->GetRingServiceAdaptor();
		return ring_service_adaptor;
	}
}

} // namespace

#ifdef USE_NESTED_RING_SERVICE
int32_t NestedRingVpnStageSetup(const NestedRingVpnStageSetupParams *params)
{
	using noa::module::vpn::VpnProcessingContext;
	using noa::module::vpn::TrafficType;
	vpn::PipelineServiceAdaptor *pipeline_service_adaptor = GetVpnPipelineServiceAdaptor();
	if (pipeline_service_adaptor == nullptr) {
		return 0;
	}
	VpnProcessingContext *context = nullptr;

	if (params->is_wlan) {
		TrafficType type =
			params->is_tx_path ? TrafficType::kWlanOutbound : TrafficType::kWlanInbound;
		context = pipeline_service_adaptor->GetProcessingContext(type);
	} else {
		TrafficType type = params->is_tx_path ? TrafficType::kModemOutbound :
							TrafficType::kModemInbound;
		context = pipeline_service_adaptor->GetProcessingContext(type);
	}

	NestedRingStage *stage = params->stage;
	NestedRingEngine *engine = pipeline_service_adaptor->GetNestedRingEngine();
	NestedRingStageInit(stage, true, *params->shadow_ring_info, *params->cached_ring_info,
			    context, engine, params->next_stage, params->task, params->name,
			    nullptr);
	return 0;
}

int32_t NestedRingVpnEngineSetup(NepBitmapTaskScheduler *scheduler)
{
	vpn::PipelineServiceAdaptor *pipeline_service_adaptor = GetVpnPipelineServiceAdaptor();
	if (pipeline_service_adaptor == nullptr) {
		return 0;
	}
	return pipeline_service_adaptor->SetupNestedRingEngine(scheduler);
}

#else /* USE_NESTED_RING_SERVICE */
int32_t NepVpnTxPathSetup(NepEngine *sender_engine, NepTaskScheduler *scheduler,
			  NepStage ***vpn_fallback_stage)
{
	vpn::PipelineServiceAdaptor *pipeline_service_adaptor = GetVpnPipelineServiceAdaptor();
	if (pipeline_service_adaptor == nullptr) {
		return 0;
	}
	return pipeline_service_adaptor->SetupTxPath(sender_engine, scheduler, vpn_fallback_stage);
}

NepStage *NepVpnTxStageSingletonGet()
{
	vpn::PipelineServiceAdaptor *pipeline_service_adaptor = GetVpnPipelineServiceAdaptor();
	if (pipeline_service_adaptor == nullptr) {
		return nullptr;
	}
	return &pipeline_service_adaptor->GetVpnTxStage();
}

int32_t NepVpnRxPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			  struct NepStage ***vpn_fallback_stage)
{
	vpn::PipelineServiceAdaptor *pipeline_service_adaptor = GetVpnPipelineServiceAdaptor();
	if (pipeline_service_adaptor == nullptr) {
		return 0;
	}
	return pipeline_service_adaptor->SetupRxPath(sender_engine, scheduler, vpn_fallback_stage);
}

NepStage *NepVpnRxStageSingletonGet()
{
	vpn::PipelineServiceAdaptor *pipeline_service_adaptor = GetVpnPipelineServiceAdaptor();
	if (pipeline_service_adaptor == nullptr) {
		return nullptr;
	}
	return &pipeline_service_adaptor->GetVpnRxStage();
}
#endif /* USE_NESTED_RING_SERVICE */
