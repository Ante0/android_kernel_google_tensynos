#ifndef CORE_WLAN_PM_WLAN_PM_H
#define CORE_WLAN_PM_WLAN_PM_H

#include "ext_svc/ext_svc.h"
#include "sys_if/types/types.h"

/// @brief Definitions of the WLAN bus power state.
typedef enum WlanBusPowerState {
	kWlanBusPowerStatePowerOff,
	kWlanBusPowerStatePoweringOn,
	kWlanBusPowerStatePowerOn,
	kWlanBusPowerStateUnknown,
} WlanBusPowerState;

/// @brief Enumeration of WLAN power-related events.
typedef enum WlanPowerEvent {
	kWlanPowerEventBusOn,
} WlanPowerEvent;

/// @brief Structure defining a WLAN power management client.
typedef struct {
	/// @brief Callback function to be invoked when a power event occurs.
	/// @param event The WLAN power event that occurred.
	/// @param context User-defined data passed to the callback.
	void (*on_power_evt_cb)(WlanPowerEvent event, void *context);
	/// @brief Client-defined data that will be passed as the `context`
	///        argument to the `on_power_evt_cb` callback.
	void *context;
} WlanPmClient;

typedef void (*OnBusPowerOnCallback)(void);
/// @brief Encapsulates the WLAN power management initialization parameters.
typedef struct {
	/// @brief Reference pointer to a wlan_ext_svc instance. The instance provides
	/// the external services needed by wlan.
	ExternalServices *ext_svc;
	/// @brief WLAN power management client configuration. Allows the caller module
	///        to register a callback to receive WLAN power events.
	WlanPmClient client;
} WlanPmInitParams;

typedef struct WlanPmIface {
	/// @brief Gets the current power state of wlan bus.
	WlanBusPowerState (*get_wlan_power_bus_state)(void);
	/// @brief Casts a vote to keep wlan bus be up.
	/// This api powers on the wlan bus, bring the PCIe link state from L2 -> L0,
	/// prevents the noa system from entering system suspend.
	int32_t (*vote_bus_power)(void);
	/// @brief Removes a previous vote to allow bus be power off.
	/// This api clears up the internal structure. Once no one need the wlan bus
	/// power, it powers down the PCIe link state to L2 and allows the noa system
	/// entering system suspend.
	int32_t (*devote_bus_power)(void);
	/// @brief Triggers the bus power-on event.
	void (*notify_bus_on)(void);
} WlanPmIface;

/// @brief Initializes the wlan power management service module.
///
/// @param[in] params Pointer to the WLAN power management initialization parameters.
/// @param[out] pm_iface Returned WLAN power managemet iterface instance.
///
/// @return Error code inidicating the result of initialization.
extern int32_t WlanPmInit(const WlanPmInitParams *params, WlanPmIface *const pm_iface);

/// @brief Deinitializes the WLAN power management service module.
///
/// @param[in] pm_iface Instance of the wlan_pm instance retrieved from previous
/// WlanInit().
extern void WlanPmDeinit(WlanPmIface *const pm_iface);

/// @brief Vote for wlan bus be up.
///
/// @param[in] pm_iface Instance of the wlan_pm instance retrieved from previous
/// WlanInit().
extern int32_t VoteBusPower(WlanPmIface pm_iface);

/// @brief Devote for wlan bus.
///
/// @param[in] pm_iface Instance of the wlan_pm instance retrieved from previous
/// WlanInit().
extern int32_t DevoteBusPower(WlanPmIface pm_iface);

/// @brief Get the current power state of wlan bus.
///
/// @param[in] pm_iface Instance of the wlan_pm instance retrieved from previous
/// WlanInit().
extern WlanBusPowerState GetBusPowerState(WlanPmIface pm_iface);

#endif // CORE_WLAN_PM_WLAN_PM_H
