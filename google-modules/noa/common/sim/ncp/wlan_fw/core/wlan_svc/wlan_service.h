#ifndef CORE_WLAN_SVC_WLAN_SERVICE_H
#define CORE_WLAN_SVC_WLAN_SERVICE_H

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "modules/wlan_ring_manager/wlan_wdev_ring_manager.h"
#include "modules/wlan_ring_manager/wlan_nep_ring_manager.h"
#include "modules/wlan_ring_manager/wlan_apc_ring_manager.h"
#include "modules/sta_table/sta_table.h"
#include "modules/flow_id_table/flow_id_table.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"
#include "modules/wlan_nep_buffer_pool/wlan_nep_buffer_pool.h"
#include "modules/noa_ring_svc/noa_ring_svc.h"
#include "wdev_if/wdev_if.h"
#include "core/dp/wlan_dp.h"
#include "core/wlan_pm/wlan_pm.h"
#include "ext_svc/ext_svc.h"
#include "core/wlan_shared_mem/memory_map_helper.h"
#include "core/wlan_svc/wlan_service_fsm.h"

/// @brief WLAN service state definitions.
typedef enum WlanServiceState {
	kWlanServiceStateUndefined = 0,
	kWlanServiceStatePlatInit,
	kWlanServiceStateReady,
	kWlanServiceStateStart,
	kWlanServiceStateStop,
	kWlanServiceStateExit,
	kWlanServiceStateEnd,
	kWlanServiceStateNum = kWlanServiceStateEnd,
} WlanServiceState;

/// @brief WLAN service initialization parameters.
typedef struct WlanServicePlatformInitParams {
	/// @brief Function pointer to send events to APC.
	int32_t (*send_event_to_apc)(uint32_t event, void *msg, const uint32_t msg_len);
	/// @brief Function pointer to activate NEP ring.
	int32_t (*nep_ring_activate)(void);
	/// @brief Function pointer to deactivate NEP input ring.
	int32_t (*nep_input_ring_deactivate)(void);
	/// @brief Function pointer to deactivate NEP output ring.
	int32_t (*nep_output_ring_deactivate)(void);
	/// @brief Function pointer to send commands to the network engine.
	int32_t (*send_command_to_net_engine)(int32_t cmd, void *msg, size_t msg_len);
	/// @brief Function pointer to activate NEP ring buffer pool.
	int32_t (*nep_ring_buffer_pool_activate)(void);
	/// @brief Function pointer to deactivate NEP ring buffer pool.
	int32_t (*nep_ring_buffer_pool_deactivate)(void);
	/// @brief Function pointer to cast a vote on whether the NOA system can
	/// enter power-gated mode.
	int32_t (*noa_power_vote)(bool active);
} WlanServicePlatformInitParams;

/// @brief WLAN service data structure.
typedef struct WlanService {
	/// @brief Current state of the WLAN service.
	WlanServiceState state;
	/// @brief Ring manager.
	WlanRingManager ring_manager;
	/// @brief Station table.
	StaTable sta_table;
	/// @brief NEP TX buffer pool.
	WlanNepTxBufferPool nep_tx_buffer_pool;
	/// @brief WLAN data path instance.
	WlanDp dp;
	/// @brief External services.
	ExternalServices ext_svc;
	/// @brief NOA ring service.
	NoaRingSvc ring_svc;
	/// @brief WDEV interface.
	WdevIf wdev_if;
	/// @brief NOA WLAN configuration space
	MemoryMapHelper memory_map_helper;
	/// @brief WLAN power managenebt interface.
	WlanPmIface pm_iface;
	/// @brief NOA WLAN state machine
	WlanServiceFsm fsm;
	/// @brief WLAN Flow id table
	FlowIdTable flow_id_table;
} WlanService;

/// @brief Function pointer type for WLAN command handlers.
///
/// @param[in] svc Pointer to the WLAN service instance.
/// @param[in] msg Pointer to the command message.
/// @param[in] resp_buf_len Length of the response buffer.
/// @param[in] resp_buf Pointer to the response buffer.
///
/// @return  Error code indicating the result of command processing.
typedef int32_t (*WlanCmdHandler)(WlanService *const svc, const void *msg, uint32_t resp_buf_len,
				  void *const resp_buf);

/// @brief Function pointer type for WLAN service shell request handlers.
///
/// This function pointer type defines the signature for functions that handle
/// shell requests for the WLAN service.
///
/// @param[in] svc Pointer to the WLAN service instance.
/// @param[in] argv_len Length of the argument vector.
/// @param[in] argv Argument vector containing the shell command and its
/// arguments.
///
/// @return Returns 0 on success, a negative error code on failure.
typedef int32_t (*WlanServiceShellRequestHandler)(WlanService *const svc, uint32_t argv_len,
						  const char *const argv);

/// @brief Entry in the WLAN service shell request table.
typedef struct WlanServiceShellRequestTableEntry {
	/// @brief The shell command string.
	const char *cmd;
	/// @brief Pointer to the handler function.
	WlanServiceShellRequestHandler handler;
} WlanServiceShellRequestTableEntry;

/// @brief Initializes the WLAN service with platform-specific parameters.
///
/// @param[in] svc Pointer to the WLAN service instance.
/// @param[in] params Pointer to the WLAN service initialization parameters.
///
/// @return  Error code indicating the result of initialization.
extern int32_t WlanServicePlatInit(WlanService *const svc,
				   const WlanServicePlatformInitParams *const params);

/// @brief Processes a command for the WLAN service.
///
/// @param[in] svc Pointer to the WLAN service instance.
/// @param[in] cmd Command ID.
/// @param[in] msg Pointer to the command message.
/// @param[in] resp_buf_len Length of the response buffer.s
/// @param[out] resp_buf Pointer to the response buffer.
///
/// @return  Error code indicating the result of command processing.
extern int32_t WlanServiceCommand(WlanService *const svc, int32_t cmd, const void *msg,
				  uint32_t resp_buf_len, void *const resp_buf);

#endif /* CORE_WLAN_SVC_WLAN_SERVICE_H */
