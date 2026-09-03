#ifndef WLAN_SERVICE_FSM_H
#define WLAN_SERVICE_FSM_H

#include "sys_if/types/types.h"
#include "sys_if/common.h"

typedef enum WlanServiceFsmState {
	kWlanServiceFsmStateStart = 0,
	kWlanServiceFsmStateExit = kWlanServiceFsmStateStart,
	kWlanServiceFsmStateStop,
	kWlanServiceFsmStatePassive,
	kWlanServiceFsmStateActive,
	kWlanServiceFsmStateError,
	kWlanServiceFsmStateEnd,
	kWlanServiceFsmStateNum = kWlanServiceFsmStateEnd,
} WlanServiceFsmState;

typedef enum WlanServiceFsmEvent {
	kWlanServiceFsmEventInit = 0,
	kWlanServiceFsmEventStart,
	kWlanServiceFsmEventActivate,
	kWlanServiceFsmEventDeactivate,
	kWlanServiceFsmEventStop,
	kWlanServiceFsmEventExit,
	kWlanServiceFsmEventError,
	kWlanServiceFsmEventNum,
} WlanServiceFsmEvent;

typedef int32_t (*FsmCallbackFn)(void *);

typedef struct WlanServiceFsmCallbackContext {
	void *context;
	FsmCallbackFn callback;
} WlanServiceFsmCallbackContext;

typedef struct WlanServiceFsm {
	WlanServiceFsmCallbackContext callback_contexts[kWlanServiceFsmStateNum]
						       [kWlanServiceFsmEventNum];
	WlanServiceFsmState state;
	spinlock_t lock;
	uintptr_t lock_flags;
} WlanServiceFsm;

/// @brief Handles events for the WLAN Service FSM.
///
/// This function processes events and triggers state transitions for
/// the WLAN Service FSM.
///
/// @param[in] fsm Pointer to the WlanServiceFsm structure.
/// @param[in] event The event to be handled.
/// @return 0 on success, or an error code on failure.
extern int32_t WlanServiceFsmEventHandler(WlanServiceFsm *fsm, WlanServiceFsmEvent event);

/// @brief Sets a callback function for a specific state and event.
///
/// @param[in] fsm Pointer to the WLAN service FSM structure.
/// @param[in] state The state for which the callback is registered.
/// @param[in] event The event for which the callback is registered.
/// @param[in] callback The callback function to be executed.
/// @param[in] context A context pointer that will be passed to the
/// callback.
/// @return 0 on success, or a negative error code on failure.
extern int32_t WlanServiceFsmSetEventCallback(WlanServiceFsm *fsm, WlanServiceFsmState state,
					      WlanServiceFsmEvent event, FsmCallbackFn callback,
					      void *context);

/// @brief Initializes the WLAN service FSM.
///
/// @param[in] fsm Pointer to the WLAN service FSM structure.
/// @return 0 on success, or a negative error code on failure.
extern int32_t WlanServiceFsmInit(WlanServiceFsm *fsm);

/// @brief Deinitializes the WLAN service FSM.
///
/// @param[in] fsm Pointer to the WLAN service FSM structure.
extern void WlanServiceFsmDeinit(WlanServiceFsm *fsm);

#endif // WLAN_SERVICE_FSM_H
