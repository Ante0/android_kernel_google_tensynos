/// @brief This file defines the interface for external services.
#ifndef EXT_SVC_EXT_SVC_H
#define EXT_SVC_EXT_SVC_H

#include "sys_if/common.h"
#include "sys_if/types/types.h"

/// @brief Function pointer type for sending an event to the APC.
///
/// @param[in] event The event ID.
/// @param[in] msg A pointer to the message data.
/// @param[in] msg_len The length of the message data.
///
/// @return 0 on success, a negative error code otherwise.
typedef int32_t (*SendEventToApc)(uint32_t event, void *msg, uint32_t msg_len);

/// @brief Function pointer type for activating the WLAN FW ring.
///
/// @return 0 on success, a negative error code otherwise.
typedef int32_t (*ActivateWlanFwRing)(void);

/// @brief Function pointer type for deactivating the WLAN FW input ring.
///
/// @return 0 on success, a negative error code otherwise.
typedef int32_t (*DeactivateWlanFwInputRing)(void);

/// @brief Function pointer type for deactivating the WLAN FW output ring.
///
/// @return 0 on success, a negative error code otherwise.
typedef int32_t (*DeactivateWlanFwOutputRing)(void);

/// @brief Function pointer type for activating the NEP TX buffer pool.
///
/// @return 0 on success, a negative error code otherwise.
typedef int32_t (*ActivateNepTxBufferPool)(void);

/// @brief Function pointer type for deactivating the NEP TX buffer
/// pool.
///
/// @return 0 on success, a negative error code otherwise.
typedef int32_t (*DeactivateNepTxBufferPool)(void);

/// @brief Function pointer type for sending a command to the Net
/// Engine.
///
/// @param[in] cmd The command ID.
/// @param[in] msg A pointer to the message data.
/// @param[in] msg_len The length of the message data.
typedef int32_t (*SendCommandToNetEngine)(int32_t cmd, void *msg, size_t msg_len);

/// @brief Function pointer to cast a vote on whether the NOA system can
/// enter power-gated mode.
///
/// @param[in] active True to vote for allowing power-gating, false to vote
/// against.
typedef int32_t (*NoaPowerVote)(bool active);

/// @brief Structure grouping the WLAN RPC services.
typedef struct WlanRpcService {
	SendEventToApc send_event_to_apc;
} WlanRpcService;

/// @brief Structure grouping the ring RPC services.
typedef struct RingRpcService {
	ActivateWlanFwRing ring_activate;
	DeactivateWlanFwInputRing ring_input_deactivate;
	DeactivateWlanFwOutputRing ring_output_deactivate;
	ActivateNepTxBufferPool tx_buffer_pool_activate;
	DeactivateNepTxBufferPool tx_buffer_pool_deactivate;
} RingRpcService;

/// @brief Structure grouping the Net Engine RPC services.
typedef struct NetEngineRpcService {
	SendCommandToNetEngine send_command_to_net_engine;
} NetEngineRpcService;

/// @brief Structure grouping the Net Engine RPC services.
typedef struct NoaSystemService {
	NoaPowerVote noa_power_vote;
} NoaSystemService;

/// @brief Structure holding all external service functions.
typedef struct ExternalServices {
	WlanRpcService wlan_rpc_service;
	RingRpcService ring_rpc_service;
	NetEngineRpcService net_engine_rpc_service;
	NoaSystemService noa_system_service;
} ExternalServices;

/// @brief Structure holding initialization parameters for external
/// services.
typedef struct ExternalServicesInitParams {
	SendEventToApc send_event_to_apc;
	ActivateWlanFwRing activate_wlan_fw_ring;
	DeactivateWlanFwInputRing deactivate_wlan_fw_input_ring;
	DeactivateWlanFwOutputRing deactivate_wlan_fw_output_ring;
	ActivateNepTxBufferPool activate_nep_tx_buffer_pool;
	DeactivateNepTxBufferPool deactivate_nep_tx_buffer_pool;
	SendCommandToNetEngine send_command_to_net_engine;
	NoaPowerVote noa_power_vote;
} ExternalServicesInitParams;

/// @brief Sends an event to the APC.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure.
/// @param[in] event The event ID.
/// @param[in] msg A pointer to the message data.
/// @param[in] msg_len The length of the message data.
///
/// @return 0 on success, a negative error code otherwise.
static inline int32_t ExtSvcSendEventToApc(ExternalServices *const ext_svc, uint32_t event,
					   void *msg, uint32_t msg_len)
{
	if (ext_svc->wlan_rpc_service.send_event_to_apc) {
		return ext_svc->wlan_rpc_service.send_event_to_apc(event, msg, msg_len);
	}

	return -ENODEV;
}

/// @brief Activates the WLAN FW ring.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure.
///
/// @return 0 on success, a negative error code otherwise.
static inline int32_t ExtSvcActivateWlanFwRing(ExternalServices *const ext_svc)
{
	if (ext_svc->ring_rpc_service.ring_activate) {
		return ext_svc->ring_rpc_service.ring_activate();
	}

	return -ENODEV;
}

/// @brief Deactivates the WLAN FW input ring.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure.
///
/// @return 0 on success, a negative error code otherwise.
static inline int32_t ExtSvcDeactivateWlanFwInputRing(ExternalServices *const ext_svc)
{
	if (ext_svc->ring_rpc_service.ring_input_deactivate) {
		return ext_svc->ring_rpc_service.ring_input_deactivate();
	}

	return -ENODEV;
}

/// @brief Deactivates the WLAN FW output ring.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure.
///
/// @return 0 on success, a negative error code otherwise.
static inline int32_t ExtSvcDeactivateWlanFwOutputRing(ExternalServices *const ext_svc)
{
	if (ext_svc->ring_rpc_service.ring_output_deactivate) {
		return ext_svc->ring_rpc_service.ring_output_deactivate();
	}

	return -ENODEV;
}

/// @brief Activates the NEP TX buffer pool.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure.
///
/// @return 0 on success, a negative error code otherwise.
static inline int32_t ExtSvcActivateNepTxBufferPool(ExternalServices *const ext_svc)
{
	if (ext_svc->ring_rpc_service.tx_buffer_pool_activate) {
		return ext_svc->ring_rpc_service.tx_buffer_pool_activate();
	}

	return -ENODEV;
}

/// @brief Deactivates the NEP TX buffer pool.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure.
///
/// @return 0 on success, a negative error code otherwise.
static inline int32_t ExtSvcDeactivateNepTxBufferPool(ExternalServices *const ext_svc)
{
	if (ext_svc->ring_rpc_service.tx_buffer_pool_deactivate) {
		return ext_svc->ring_rpc_service.tx_buffer_pool_deactivate();
	}

	return -ENODEV;
}

/// @brief Sends a command to the Net Engine.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure.
/// @param[in] cmd The command ID.
/// @param[in] msg A pointer to the message data.
/// @param[in] msg_len The length of the message data.
///
/// @return 0 on success, a negative error code otherwise.
static inline int32_t ExtSvcSendCommandToNetEngine(ExternalServices *const ext_svc, int32_t cmd,
						   void *msg, size_t msg_len)
{
	if (ext_svc->net_engine_rpc_service.send_command_to_net_engine) {
		return ext_svc->net_engine_rpc_service.send_command_to_net_engine(cmd, msg,
										  msg_len);
	}

	return -ENODEV;
}

/// @brief Cast a vote on whether the NOA system can enter power-gated mode.
///
/// @param[in] ext_svc Pointer to the ExternalServices structure.
/// @param[in] active True to vote for allowing power-gating, false to vote
/// against.
///
/// @return 0 on success, -ENODEV if no NOA power vote function is registered.
static inline int32_t ExtSvcNoaPowerVote(ExternalServices *const ext_svc, bool active)
{
	if (ext_svc->noa_system_service.noa_power_vote) {
		return ext_svc->noa_system_service.noa_power_vote(active);
	}

	return -ENODEV;
}

/// @brief Initializes the external services.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure to
/// initialize.
/// @param[in] params A pointer to the ExternalServicesInitParams
/// structure containing the initialization parameters.
///
/// @return 0 on success, a negative error code otherwise.
extern int32_t ExtSvcInit(ExternalServices *const ext_svc,
			  const ExternalServicesInitParams *const params);

/// @brief Deinitializes the external services.
///
/// @param[in] ext_svc A pointer to the ExternalServices structure to
/// deinitialize.
extern void ExtSvcDeinit(ExternalServices *const ext_svc);

#endif /* EXT_SVC_EXT_SVC_H */
