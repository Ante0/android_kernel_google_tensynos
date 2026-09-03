#include "wlan_service_fsm.h"

#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "wlan_log/wlan_log.h"
#include "sys_if/mailbox/sys_if_mailbox.h"

typedef WlanServiceFsmState (*WlanServiceFsmStateHandler)(WlanServiceFsm *, WlanServiceFsmEvent);

static const char *WlanServiceFsmGetStateName(WlanServiceFsmState state)
{
#define WLAN_FSM_CASE_STATE_NAME(state)                                                            \
	case kWlanServiceFsmState##state:                                                          \
		return #state

	switch (state) {
		WLAN_FSM_CASE_STATE_NAME(Exit);
		WLAN_FSM_CASE_STATE_NAME(Stop);
		WLAN_FSM_CASE_STATE_NAME(Passive);
		WLAN_FSM_CASE_STATE_NAME(Active);
		WLAN_FSM_CASE_STATE_NAME(Error);
	default:
		WLAN_LOG_ERROR(Fsm, "%s(): Unknown state: %" PRIu32, __func__, state);
		break;
	}

	return "Unknown";
}

static const char *WlanServiceFsmGetEventName(WlanServiceFsmEvent event)
{
#define WLAN_FSM_CASE_EVENT_NAME(event)                                                            \
	case kWlanServiceFsmEvent##event:                                                          \
		return #event

	switch (event) {
		WLAN_FSM_CASE_EVENT_NAME(Init);
		WLAN_FSM_CASE_EVENT_NAME(Start);
		WLAN_FSM_CASE_EVENT_NAME(Activate);
		WLAN_FSM_CASE_EVENT_NAME(Deactivate);
		WLAN_FSM_CASE_EVENT_NAME(Stop);
		WLAN_FSM_CASE_EVENT_NAME(Exit);
		WLAN_FSM_CASE_EVENT_NAME(Error);
	default:
		WLAN_LOG_ERROR(Fsm, "%s(): Unknown event: %" PRIu32, __func__, event);
		break;
	}

	return "Unknown";
}

static WlanServiceFsmState WlanServiceFsmStateExitTransit(WlanServiceFsm *UNUSED(fsm),
							  WlanServiceFsmEvent event)
{
	switch (event) {
	case kWlanServiceFsmEventError:
		return kWlanServiceFsmStateError;
	case kWlanServiceFsmEventInit:
		return kWlanServiceFsmStateStop;
	default:
		break;
	}
	return kWlanServiceFsmStateExit;
}

static WlanServiceFsmState WlanServiceFsmStateStopTransit(WlanServiceFsm *UNUSED(fsm),
							  WlanServiceFsmEvent event)
{
	switch (event) {
	case kWlanServiceFsmEventError:
		return kWlanServiceFsmStateError;
	case kWlanServiceFsmEventExit:
		return kWlanServiceFsmStateExit;
	case kWlanServiceFsmEventStart:
		return kWlanServiceFsmStatePassive;
	default:
		break;
	}
	return kWlanServiceFsmStateStop;
}

static WlanServiceFsmState WlanServiceFsmStatePassiveTransit(WlanServiceFsm *UNUSED(fsm),
							     WlanServiceFsmEvent event)
{
	switch (event) {
	case kWlanServiceFsmEventError:
		return kWlanServiceFsmStateError;
	case kWlanServiceFsmEventStop:
		// Fallthrough
	case kWlanServiceFsmEventExit:
		return kWlanServiceFsmStateStop;
	case kWlanServiceFsmEventDeactivate:
		return kWlanServiceFsmStatePassive;
	case kWlanServiceFsmEventActivate:
		return kWlanServiceFsmStateActive;
	default:
		break;
	}
	return kWlanServiceFsmStatePassive;
}

static WlanServiceFsmState WlanServiceFsmStateActiveTransit(WlanServiceFsm *UNUSED(fsm),
							    WlanServiceFsmEvent event)
{
	switch (event) {
	case kWlanServiceFsmEventError:
		return kWlanServiceFsmStateError;
	case kWlanServiceFsmEventStop:
		// Fallthrough
	case kWlanServiceFsmEventExit:
		// Fallthrough
	case kWlanServiceFsmEventDeactivate:
		return kWlanServiceFsmStatePassive;
	case kWlanServiceFsmEventActivate:
		return kWlanServiceFsmStateActive;
	default:
		break;
	}
	return kWlanServiceFsmStateActive;
}

static WlanServiceFsmState WlanServiceFsmStateErrorTransit(WlanServiceFsm *UNUSED(fsm),
							   WlanServiceFsmEvent UNUSED(event))
{
	SysIfNotifyMailbox(kNcpWifiMailboxTypeAp, kDoorbellFwTrapEvent);
	return kWlanServiceFsmStateExit;
}

static void WlanServiceFsmLock(WlanServiceFsm *fsm)
{
	spin_lock_irqsave(&fsm->lock, fsm->lock_flags);
}

static void WlanServiceFsmUnlock(WlanServiceFsm *fsm)
{
	spin_unlock_irqrestore(&fsm->lock, fsm->lock_flags);
}

static void WlanServiceFsmSetStateLocked(WlanServiceFsm *fsm, WlanServiceFsmState state)
{
	fsm->state = state;
}

static WlanServiceFsmState WlanServiceFsmGetState(const WlanServiceFsm *fsm)
{
	return fsm->state;
}

static void WlanServiceFsmInitiateCallback(WlanServiceFsm *fsm, WlanServiceFsmState state,
					   WlanServiceFsmEvent event)
{
	FsmCallbackFn callback;
	void *context;

	WlanServiceFsmLock(fsm);
	callback = fsm->callback_contexts[state][event].callback;
	context = fsm->callback_contexts[state][event].context;
	WlanServiceFsmUnlock(fsm);

	if (callback) {
		callback(context);
	}
}

int32_t WlanServiceFsmEventHandler(WlanServiceFsm *fsm, WlanServiceFsmEvent event)
{
	static const WlanServiceFsmStateHandler
		kWlanServiceFsmStateTransitionTable[kWlanServiceFsmStateNum] = {
			[kWlanServiceFsmStateExit] = WlanServiceFsmStateExitTransit,
			[kWlanServiceFsmStateStop] = WlanServiceFsmStateStopTransit,
			[kWlanServiceFsmStatePassive] = WlanServiceFsmStatePassiveTransit,
			[kWlanServiceFsmStateActive] = WlanServiceFsmStateActiveTransit,
			[kWlanServiceFsmStateError] = WlanServiceFsmStateErrorTransit,
		};
	WlanServiceFsmState current_state = WlanServiceFsmGetState(fsm);
	WlanServiceFsmState next_state;

	// WLAN_LOG_DEBUG(Fsm, "Processing event %s(%u) in state %s(%u)",
	// 	       WlanServiceFsmGetEventName(event), event,
	// 	       WlanServiceFsmGetStateName(current_state), current_state);

	if (current_state < kWlanServiceFsmStateStart || current_state >= kWlanServiceFsmStateEnd) {
		WLAN_LOG_ERROR(
			Fsm,
			"Event %s received in an INVALID state %s(%u)! Forcing to Error state.",
			WlanServiceFsmGetEventName(event),
			WlanServiceFsmGetStateName(current_state), current_state);
		return WlanServiceFsmEventHandler(fsm, kWlanServiceFsmEventError);
	}

	if (kWlanServiceFsmStateTransitionTable[current_state]) {
		next_state = kWlanServiceFsmStateTransitionTable[current_state](fsm, event);
	} else {
		WLAN_LOG_ERROR(
			Fsm,
			"No transition handler defined for state %s(%u)! Forcing to Error state.",
			WlanServiceFsmGetStateName(current_state), current_state);
		next_state = kWlanServiceFsmStateError;
	}

	if (current_state == next_state) {
		// WLAN_LOG_DEBUG(Fsm, "Event %s was handled in state %s with no state change.",
		// 	       WlanServiceFsmGetEventName(event),
		// 	       WlanServiceFsmGetStateName(current_state));
		return 0;
	}

	WLAN_LOG_INFO(Fsm, "[STATE_TRANSITION] Event %s(%u) caused %s(%u) -> %s(%u)",
		      WlanServiceFsmGetEventName(event), event,
		      WlanServiceFsmGetStateName(current_state), current_state,
		      WlanServiceFsmGetStateName(next_state), next_state);

	WlanServiceFsmInitiateCallback(fsm, current_state, event);
	WlanServiceFsmLock(fsm);
	WlanServiceFsmSetStateLocked(fsm, next_state);
	WlanServiceFsmUnlock(fsm);

	return WlanServiceFsmEventHandler(fsm, event);
}

int32_t WlanServiceFsmSetEventCallback(WlanServiceFsm *fsm, WlanServiceFsmState state,
				       WlanServiceFsmEvent event, FsmCallbackFn callback,
				       void *context)
{
	WlanServiceFsmLock(fsm);
	fsm->callback_contexts[state][event].callback = callback;
	fsm->callback_contexts[state][event].context = context;
	WlanServiceFsmUnlock(fsm);

	return 0;
}

int32_t WlanServiceFsmInit(WlanServiceFsm *fsm)
{
	memset(fsm, 0, sizeof(WlanServiceFsm));
	spin_lock_init(&fsm->lock);
	WlanServiceFsmLock(fsm);
	WlanServiceFsmSetStateLocked(fsm, kWlanServiceFsmStateExit);
	WlanServiceFsmUnlock(fsm);
	return 0;
}

void WlanServiceFsmDeinit(WlanServiceFsm *fsm)
{
	memset(fsm, 0, sizeof(WlanServiceFsm));
}
