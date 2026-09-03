#pragma once
#include <cstdint>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "network_pipeline_framework/nested_ring_stage.h"

#ifdef USE_NESTED_RING_SERVICE
typedef struct {
	const char *name;
	bool is_wlan;
	bool is_tx_path;
	NestedRingTask *task;
	NestedRingStage *stage;
	NestedShadowRing *shadow_ring_info;
	NestedCachedRing *cached_ring_info;
	NestedRingEngine *engine;
	NestedRingStage *next_stage;
} NestedRingVpnStageSetupParams;

/**
 *
 * @brief Sets up a nested ring stage for VPN traffic.
 *
 * This function configures a specific stage within the nested ring pipeline
 * designed to handle VPN traffic.
 *
 * @param[in] params A pointer to the NestedRingVpnStageSetupParams
 * structure containing all necessary configuration details.
 *
 * @return 0 on successful setup.
 *         Negative error code otherwise.
 */
int32_t NestedRingVpnStageSetup(const NestedRingVpnStageSetupParams *params);

/**
 * @brief Sets up the nested ring engine for VPN processing.
 *
 * This function initializes and configures the nested ring engine, a core
 * component for managing and processing VPN traffic within the nested ring
 * pipeline architecture.
 *
 * @param[in] scheduler A pointer to the NepBitmapTaskScheduler that the nested
 *  ring engine will use for task scheduling.
 * @return 0 on successful setup, or a negative error code if the setup fails.
 */
int32_t NestedRingVpnEngineSetup(NepBitmapTaskScheduler *scheduler);

#else /* USE_NESTED_RING_SERVICE */
/// @brief Set up the VPN Tx path
///
/// This function is called by the pipeline service framework to set up
/// the stages required for processing Tx VPN packets.
///
/// @param[in] sender_engine The `NepEngine` instance used to forward packets to rings.
///
/// The NepEngine instance used to forward packets to rings.
/// @param[in] scheduler The task scheduler of the pipeline service.
/// @param[out] vpn_fallback_stage A pointer to a pointer to the next stage
/// pointer for VPN fallback. This allows subsequent data path connections
/// after the fallback.
///
/// @return 0 on success, negative value for error code.
int32_t NepVpnTxPathSetup(NepEngine *sender_engine, NepTaskScheduler *scheduler,
			  NepStage ***vpn_fallback_stage);

/// @brief Get the VPN Tx stage singleton object.
///
/// @return A valid NepStage pointer representing the VPN RX stage singleton.
/// Returns `nullptr` if VPN functionality or the pipeline service integration
/// is disabled.
NepStage *NepVpnTxStageSingletonGet();

/// @brief Set up the VPN Rx path
///
/// This function is called by the pipeline service framework to set up
/// the stages required for processing Tx VPN packets.
///
/// @param[in] sender_engine The `NepEngine` instance used to forward packets to rings.
///
/// The NepEngine instance used to forward packets to rings.
/// @param[in] scheduler The task scheduler of the pipeline service.
/// @param[out] vpn_fallback_stage A pointer to a pointer to the next stage
/// pointer for VPN fallback. This allows subsequent data path connections
/// after the fallback.
///
/// @return 0 on success, negative value for error code.
int32_t NepVpnRxPathSetup(NepEngine *sender_engine, NepTaskScheduler *scheduler,
			  NepStage ***vpn_fallback_stage);

/// @brief Get the VPN Rx stage singleton object.
///
/// @return A valid NepStage pointer representing the VPN RX stage singleton.
/// Returns `nullptr` if VPN functionality or the pipeline service integration
/// is disabled.
NepStage *NepVpnRxStageSingletonGet();
#endif /* USE_NESTED_RING_SERVICE */
