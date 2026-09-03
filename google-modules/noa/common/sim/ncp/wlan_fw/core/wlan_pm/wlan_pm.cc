#include "wlan_pm.h"

#include "common/compiler.h"
#include "ext_svc/ext_svc.h"
#include "sys_if/types/types.h"
#include "wlan_cast.h"
#include "wlan_log/wlan_log.h"
#include "wlan_service_rpc_protocol.h"

#define LOCK(mutex)
#define UNLOCK(mutext)

#define WLAN_PM_LOG_WARN(fmt, ...) WLAN_LOG_WARN(Pm, fmt, ##__VA_ARGS__)
#define WLAN_PM_LOG_ERROR(fmt, ...) WLAN_LOG_ERROR(Pm, fmt, ##__VA_ARGS__)

struct WlanPm {
	bool inited;
	WlanBusPowerState bus_state;
	int32_t bus_active_count;
	WlanPmClient client;
	const ExternalServices *ext_svc;
};

enum {
	kWlanPmPowerOnBus = 1,
	kWlanPmPowerOffBus = 0,
};

SEC_FAST_DATA struct WlanPm wlan_pm = {};
SEC_FAST_DATA bool pm_mutex = false;

static WlanBusPowerState WlanPmGetBusPowerState(void)
{
	return wlan_pm.bus_state;
}

static int32_t WlanPmVoteBusPower(void)
{
	int32_t err = 0;
	bool idle_to_active = false;
	uint32_t power_on_bus = kWlanPmPowerOnBus;

	if (!wlan_pm.inited) {
		WLAN_PM_LOG_ERROR("Failed to vote device power. PM has not yet inited.");
		return -1;
	}

	LOCK(pm_mutex);
	if (wlan_pm.bus_active_count < INT_MAX) {
		wlan_pm.bus_active_count += 1;
	}
	idle_to_active = wlan_pm.bus_active_count == 1;
	UNLOCK(pm_mutex);

	if (idle_to_active) {
		err = wlan_pm.ext_svc->noa_system_service.noa_power_vote(true);
		if (err < 0) {
			WLAN_PM_LOG_ERROR("Failed to cast a vote to noa, err:%d", err);
			return err;
		}

		err = wlan_pm.ext_svc->wlan_rpc_service.send_event_to_apc(
			kWlanEventTypePmBusPower, WLAN_STATIC_CAST(void *, &power_on_bus),
			sizeof(power_on_bus));
		if (err < 0) {
			WLAN_PM_LOG_ERROR("Failed to send the bus power-on request to ap, err:%d",
					  err);
			return err;
		}

		// TODO: b/397775374 - Integrate with NOA PCIe driver to vote/devote
		// PCIe power.

		wlan_pm.bus_state = kWlanBusPowerStatePoweringOn;
	} else {
		wlan_pm.client.on_power_evt_cb(kWlanPowerEventBusOn, wlan_pm.client.context);
	}
	return 0;
}

static int32_t WlanPmDevoteBusPower(void)
{
	int32_t err = 0;
	bool active_to_idle = false;
	uint32_t power_off_bus = kWlanPmPowerOffBus;

	if (!wlan_pm.inited) {
		WLAN_PM_LOG_ERROR("Failed to devote bus power. PM has not yet inited.");
		return -1;
	}

	LOCK(pm_mutex);
	if (wlan_pm.bus_active_count > 0) {
		wlan_pm.bus_active_count -= 1;
	}
	active_to_idle = wlan_pm.bus_active_count == 0;
	UNLOCK(pm_mutex);

	if (active_to_idle) {
		err = wlan_pm.ext_svc->noa_system_service.noa_power_vote(false);
		if (err < 0) {
			WLAN_PM_LOG_ERROR("Failed to remove a vote from noa, err:%d", err);
			return err;
		}

		err = wlan_pm.ext_svc->wlan_rpc_service.send_event_to_apc(
			kWlanEventTypePmBusPower, WLAN_STATIC_CAST(void *, &power_off_bus),
			sizeof(power_off_bus));
		if (err < 0) {
			WLAN_PM_LOG_ERROR("Failed to send the bus power-off request to ap, err:%d",
					  err);
			return err;
		}

		// TODO: b/397775374 - Integrate with NOA PCIe driver to vote/devote
		// PCIe power.

		wlan_pm.bus_state = kWlanBusPowerStatePowerOff;
	}
	return 0;
}

static void WlanPmNotifyBusOn(void)
{
	LOCK(pm_mutex);
	wlan_pm.bus_state = kWlanBusPowerStatePowerOn;
	UNLOCK(pm_mutex);
	wlan_pm.client.on_power_evt_cb(kWlanPowerEventBusOn, wlan_pm.client.context);
}

static bool ValidateParams(const WlanPmInitParams *params)
{
	WlanRpcService *rpc_svc = &params->ext_svc->wlan_rpc_service;
	NoaSystemService *sys_svc = &params->ext_svc->noa_system_service;
	if (!rpc_svc || !rpc_svc->send_event_to_apc) {
		return false;
	}

	if (!sys_svc || !sys_svc->noa_power_vote) {
		return false;
	}

	return params->client.on_power_evt_cb;
}

int32_t WlanPmInit(const WlanPmInitParams *params, WlanPmIface *const pm_iface)
{
	if (!params || !pm_iface) {
		WLAN_PM_LOG_ERROR("Failed to init with NULL params and pm_iface.");
		return -EINVAL;
	}

	if (wlan_pm.inited) {
		WLAN_PM_LOG_WARN("Failed to re-init wlan power.");
		return -1;
	}

	if (ValidateParams(params) == false) {
		WLAN_PM_LOG_WARN("Failed to init with NULL field params.");
		return -EINVAL;
	}

	pm_iface->get_wlan_power_bus_state = WlanPmGetBusPowerState;
	pm_iface->vote_bus_power = WlanPmVoteBusPower;
	pm_iface->devote_bus_power = WlanPmDevoteBusPower;
	pm_iface->notify_bus_on = WlanPmNotifyBusOn;

	memset(&wlan_pm, 0, sizeof(struct WlanPm));
	wlan_pm.inited = true;
	wlan_pm.bus_state = kWlanBusPowerStatePowerOff;
	wlan_pm.client.on_power_evt_cb = params->client.on_power_evt_cb;
	wlan_pm.client.context = params->client.context;
	wlan_pm.ext_svc = params->ext_svc;

	return 0;
}

void WlanPmDeinit(WlanPmIface *const UNUSED(pm_iface))
{
	if (!wlan_pm.inited) {
		return;
	}

	while (wlan_pm.bus_state != kWlanBusPowerStatePowerOff) {
		if (WlanPmDevoteBusPower() < 0) {
			WLAN_PM_LOG_WARN("Failed to release bus power.");
			break;
		}
	}

	LOCK(pm_mutex);
	wlan_pm.inited = false;
	UNLOCK(pm_mutex);
}

int32_t VoteBusPower(WlanPmIface pm_iface)
{
	if (!pm_iface.vote_bus_power) {
		WLAN_PM_LOG_ERROR("vote_bus_power is not set.");
		return -EINVAL;
	}

	return pm_iface.vote_bus_power();
}

int32_t DevoteBusPower(WlanPmIface pm_iface)
{
	if (!pm_iface.devote_bus_power) {
		WLAN_PM_LOG_ERROR("devote_bus_power is not set.");
		return -EINVAL;
	}

	return pm_iface.devote_bus_power();
}

WlanBusPowerState GetBusPowerState(WlanPmIface pm_iface)
{
	if (!pm_iface.get_wlan_power_bus_state) {
		WLAN_PM_LOG_ERROR("%s(): get_wlan_power_bus_state is not set.");
		return kWlanBusPowerStateUnknown;
	}

	return pm_iface.get_wlan_power_bus_state();
}
