// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021, Google LLC
 *
 * Pogo management driver
 */

#include <linux/alarmtimer.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/usb/tcpm.h>
#include <misc/gvotable.h>

#include "google_bms.h"
#include "google_psy.h"
#include "tcpci_max77759.h"

#define POGO_TIMEOUT_MS 10000
#define LC_WAKEUP_TIMEOUT_MS 10000
#define POGO_USB_CAPABLE_THRESHOLD_UV 10500000
#define POGO_USB_RETRY_COUNT 10
#define POGO_USB_RETRY_INTEREVAL_MS 50
#define POGO_PSY_DEBOUNCE_MS 50
#define POGO_PSY_NRDY_RETRY_MS 500
#define POGO_ACC_GPIO_DEBOUNCE_MS 20
#define LC_DELAY_CHECK_MS 5000
#define LC_DISABLE_MS 1800000 /* 30 min */
#define LC_ENABLE_MS 300000 /* 5 min */
#define LC_BOOTUP_MS 3000
#define ACC_CHARGING_TIMEOUT_SEC 1800 /* 30 min */

#define KEEP_USB_PATH 2
#define KEEP_HUB_PATH 2
#define DEFAULT_STATE_MACHINE_ENABLE true

#define POGO_VOTER "POGO"
#define SSPHY_RESTART_EL "SSPHY_RESTART"

/*
 * State Description:
 *	INVALID_STATE,
 *  (A)	STANDBY,			// Nothing attached, hub disabled
 *	DOCKING_DEBOUNCED,		// STANDBY -> DOCK_HUB, pogo gpio
 *	STANDBY_ACC_DEBOUNCED,		// STANDBY -> ACC_DIRECT, acc gpio
 *  (B)	DOCK_HUB,			// Dock online, hub enabled
 *  (C)	DOCK_DEVICE_HUB,		// Dock online, usb device online, hub enabled
 *  (H)	DOCK_AUDIO_HUB,			// Dock online, usb audio online, hub enabled
 *  (I)	AUDIO_HUB,			// Usb audio online, hub enabled
 *	AUDIO_HUB_DOCKING_DEBOUNCED,	// AUDIO_HUB -> DOCK_AUDIO_HUB, pogo gpio
 *	AUDIO_HUB_ACC_DEBOUNCED,	// AUDIO_HUB -> ACC_AUDIO_HUB, acc gpio
 *  (D)	DEVICE_HUB,			// Usb device online, hub enabled
 *      DEVICE_HUB_DOCKING_DEBOUNCED,   // DEVICE_HUB -> DOCK_DEVICE_HUB, pogo gpio
 *      DEVICE_HUB_ACC_DEBOUNCED,	// DEVICE_HUB -> ACC_DEVICE_HUB, acc gpio
 *  (E)	DEVICE_DIRECT,			// Usb device online, hub disabled
 *	DEVICE_DOCKING_DEBOUNCED,	// DEVICE_DIRECT -> DOCK_DEVICE_HUB, pogo gpio
 *	DEVICE_DIRECT_ACC_DEBOUNCED,	// DEVICE_DIRECT -> ACC_DEVICE_HUB, acc gpio
 *  (F)	AUDIO_DIRECT,			// Usb audio online, hub disabled
 *	AUDIO_DIRECT_DOCKING_DEBOUNCED,	// AUDIO_DIRECT -> AUDIO_DIRECT_DOCK_OFFLINE, pogo gpio
 *	AUDIO_DIRECT_ACC_DEBOUNCED,	// AUDIO_DIRECT -> ACC_DEVICE_HUB, acc gpio
 *  (G)	AUDIO_DIRECT_DOCK_OFFLINE,	// Usb audio online, dock offline, hub disabled
 *  (J)	HOST_DIRECT,			// Usb host online, hub disabled
 *	HOST_DIRECT_DOCKING_DEBOUNCED,	// HOST_DIRECT -> HOST_DIRECT_DOCK_OFFLINE, pogo gpio
 *  (K)	HOST_DIRECT_DOCK_OFFLINE,	// Usb host online, dock offline, hub disabled
 *      HOST_DIRECT_ACC_DEBOUNCED,	// HOST_DIRECT -> HOST_DIRECT_ACC_OFFLINE, acc gpio
 *  (L)	DOCK_HUB_HOST_OFFLINE,		// Dock online, usb host offline, hub enabled
 *  (M)	ACC_DIRECT,			// Acc online, hub disabled
 *  (N)	ACC_DEVICE_HUB,			// Acc online, usb device online, hub enabled
 *  (O)	ACC_HUB,			// Acc online, hub enabled
 *  (T)	ACC_HUB_HOST_OFFLINE,		// Acc online, hub enabled, usb host offline
 *  (P)	ACC_AUDIO_HUB,			// Acc online, usb audio online, hub enabled
 *  (Q)	LC,				// Pogo Vout off
 *  (U)	LC_DEVICE_DIRECT,		// Pogo Vout off, Usb device online, hub disabled
 *  (V)	LC_AUDIO_DIRECT,		// Pogo Vout off, Usb audio online, hub disabled
 *  (W)	LC_ALL_OFFLINE,			// Pogo Vout off, Usb host offline, hub disabled
 *  (X)	LC_HOST_DIRECT,			// Pogo Vout off, USb host online, hub disabled
 *  (R)	HOST_DIRECT_ACC_OFFLINE,	// Usb host online, acc offline, hub disabled
 *  (S)	ACC_DIRECT_HOST_OFFLINE,	// Acc online, usb host offline
 */

#define FOREACH_STATE(S)			\
	S(INVALID_STATE),			\
	S(STANDBY),				\
	S(DOCKING_DEBOUNCED),			\
	S(STANDBY_ACC_DEBOUNCED),		\
	S(DOCK_HUB),				\
	S(DOCK_DEVICE_HUB),			\
	S(DOCK_AUDIO_HUB),			\
	S(AUDIO_HUB),				\
	S(AUDIO_HUB_DOCKING_DEBOUNCED),		\
	S(AUDIO_HUB_ACC_DEBOUNCED),		\
	S(DEVICE_HUB),				\
	S(DEVICE_HUB_DOCKING_DEBOUNCED),	\
	S(DEVICE_HUB_ACC_DEBOUNCED),		\
	S(DEVICE_DIRECT),			\
	S(DEVICE_DOCKING_DEBOUNCED),		\
	S(DEVICE_DIRECT_ACC_DEBOUNCED),		\
	S(AUDIO_DIRECT),			\
	S(AUDIO_DIRECT_DOCKING_DEBOUNCED),	\
	S(AUDIO_DIRECT_ACC_DEBOUNCED),		\
	S(AUDIO_DIRECT_DOCK_OFFLINE),		\
	S(HOST_DIRECT),				\
	S(HOST_DIRECT_DOCKING_DEBOUNCED),	\
	S(HOST_DIRECT_DOCK_OFFLINE),		\
	S(HOST_DIRECT_ACC_DEBOUNCED),		\
	S(DOCK_HUB_HOST_OFFLINE),		\
	S(ACC_DIRECT),				\
	S(ACC_DEVICE_HUB),			\
	S(ACC_HUB),				\
	S(ACC_HUB_HOST_OFFLINE),		\
	S(ACC_AUDIO_HUB),			\
	S(LC),					\
	S(LC_DEVICE_DIRECT),			\
	S(LC_AUDIO_DIRECT),			\
	S(LC_ALL_OFFLINE),			\
	S(LC_HOST_DIRECT),			\
	S(HOST_DIRECT_ACC_OFFLINE),		\
	S(ACC_DIRECT_HOST_OFFLINE)

#define GENERATE_ENUM(e)	e
#define GENERATE_STRING(s)	#s

enum pogo_state {
	FOREACH_STATE(GENERATE_ENUM)
};

static const char * const pogo_states[] = {
	FOREACH_STATE(GENERATE_STRING)
};

enum pogo_event_type {
	/* Reported when docking status changes */
	EVENT_DOCKING,
	/* Enable USB-C data, when pogo usb data is active */
	EVENT_MOVE_DATA_TO_USB,
	/* Enable pogo data, when pogo is available */
	EVENT_MOVE_DATA_TO_POGO,
	/* Retry reading power supply voltage to detect dock type */
	EVENT_RETRY_READ_VOLTAGE,
	/* Reported when data over USB-C is enabled/disabled */
	EVENT_DATA_ACTIVE_CHANGED,

	/* 5 */
	/* Hub operation; workable only if hub_embedded is true */
	EVENT_ENABLE_HUB,
	EVENT_DISABLE_HUB,
	EVENT_HALL_SENSOR_ACC_DETECTED,
	EVENT_HALL_SENSOR_ACC_MALFUNCTION,
	EVENT_HALL_SENSOR_ACC_UNDOCKED,

	/* 10 */
	EVENT_POGO_ACC_DEBOUNCED,
	EVENT_POGO_ACC_CONNECTED,
	/* Bypass the accessory detection and enable POGO Vout and POGO USB capability */
	/* This event is for debug only and never used in normal operations. */
	EVENT_FORCE_ACC_CONNECT,
	/* Reported when CC orientation has changed */
	EVENT_ORIENTATION_CHANGED,
};

#define EVENT_POGO_IRQ			BIT(0)
#define EVENT_USBC_DATA_CHANGE		BIT(1)
#define EVENT_ENABLE_USB_DATA		BIT(2)
#define EVENT_HES_H1S_CHANGED		BIT(3)
#define EVENT_ACC_GPIO_ACTIVE		BIT(4)
#define EVENT_ACC_CONNECTED		BIT(5)
#define EVENT_AUDIO_DEV_ATTACHED	BIT(6)
#define EVENT_USBC_ORIENTATION		BIT(7)
#define EVENT_LC_STATUS_CHANGED		BIT(8)
#define EVENT_USB_SUSPEND		BIT(9)
#define EVENT_FORCE_POGO		BIT(10)
#define EVENT_LAST_EVENT_TYPE		BIT(63)

enum lc_stages {
	STAGE_UNKNOWN,
	STAGE_WAIT_FOR_SUSPEND,
	STAGE_VOUT_ENABLED,
	STAGE_VOUT_DISABLED,
};

#define bus_suspend(p) ((p)->main_hcd_suspend && (p)->shared_hcd_suspend)

static bool modparam_force_usb;
module_param_named(force_usb, modparam_force_usb, bool, 0644);
MODULE_PARM_DESC(force_usb, "Force enabling usb path over pogo");

/* Overrides device tree config */
static int modparam_pogo_accessory_enable;
module_param_named(pogo_accessory_enable, modparam_pogo_accessory_enable, int, 0644);
MODULE_PARM_DESC(pogo_accessory_enable, "Enabling accessory detection over pogo");

/* Set to 1 (enable) or 2 (disable) to override the default value */
static int modparam_state_machine_enable;
module_param_named(state_machine_enable, modparam_state_machine_enable, int, 0644);
MODULE_PARM_DESC(state_machine_enable, "Enabling pogo state machine transition");

extern void register_bus_suspend_callback(void (*callback)(void *bus_suspend_payload, bool main_hcd,
							   bool suspend),
					  void *data);

struct pogo_event {
	struct kthread_delayed_work work;
	struct pogo_transport *pogo_transport;
	enum pogo_event_type event_type;
};

enum pogo_accessory_detection {
	/* Pogo accessory detection is disabled. */
	DISABLED,
	/*
	 * Pogo accessory detection is only based on HALL output mapped to pogo-acc-hall-capable.
	 * Expected seq:
	 * EVENT_HALL_SENSOR_ACC_DETECTED -> EVENT_HALL_SENSOR_ACC_UNDOCKED
	 */
	HALL_ONLY,
	/*
	 * Pogo accessory detection POR mapped to pogo-acc-capable.
	 * Expected seq:
	 * EVENT_HALL_SENSOR_ACC_DETECTED -> EVENT_POGO_ACC_DEBOUNCED ->
	 * EVENT_POGO_ACC_CONNECTED -> EVENT_HALL_SENSOR_ACC_UNDOCKED
	 */
	ENABLED
};

struct pogo_transport_udev_ids {
	__le16 vendor;
	__le16 product;
};

struct pogo_transport {
	struct device *dev;
	struct max77759_plat *chip;
	struct logbuffer *log;
	int pogo_gpio;
	int pogo_irq;
	int pogo_data_mux_gpio;
	int pogo_hub_sel_gpio;
	int pogo_hub_reset_gpio;
	int pogo_ovp_en_gpio;
	int pogo_acc_gpio;
	int pogo_acc_irq;
	unsigned int pogo_acc_gpio_debounce_ms;
	struct regulator *hub_ldo;
	struct regulator *acc_detect_ldo;
	/* Raw value of the active state. Set to 1 when pogo_ovp_en is ACTIVE_HIGH */
	bool pogo_ovp_en_active_state;
	struct pinctrl *pinctrl;
	struct pinctrl_state *susp_usb_state;
	struct pinctrl_state *susp_pogo_state;
	struct pinctrl_state *hub_state;
	/* When true, Usb data active over pogo pins. */
	bool pogo_usb_active;
	/* When true, Pogo connection is capable of usb transport. */
	bool pogo_usb_capable;
	/* When true, both pogo and usb-c have equal priority. */
	bool equal_priority;
	/* When true, USB data is routed to the hub. */
	bool pogo_hub_active;
	/* When true, the board has a hub embedded in the pogo system. */
	bool hub_embedded;
	/* When true, pogo takes higher priority */
	bool force_pogo;
	/* When true, pogo irq is enabled */
	bool pogo_irq_enabled;
	/* When true, acc irq is enabled */
	bool acc_irq_enabled;
	/* True if acc gpio was active before the acc irq was disabled */
	bool acc_gpio_result_cache;
	/* When true, hall1_s sensor reports attach event */
	bool hall1_s_state;
	/* When true, the path won't switch to pogo if accessory is attached */
	bool mfg_acc_test;
	/* When true, the hub will remain enabled after undocking */
	bool force_hub_enabled;
	/*
	 * When true, skip acc detection and POGO Vout as well as POGO USB will be enabled.
	 * Only applicable for debugfs capable builds.
	 */
	bool mock_hid_connected;
	/* When true, lc is triggered */
	bool lc;
	/* When true, the bus not yet suspended after lc is triggered */
	bool wait_for_suspend;

	struct kthread_worker *wq;
	struct kthread_delayed_work state_machine;
	struct kthread_work event_work;
	enum pogo_state prev_state;
	enum pogo_state state;
	enum pogo_state delayed_state;
	unsigned long delayed_runtime;
	unsigned long delay_ms;
	unsigned long lc_delay_check_ms;
	unsigned long lc_enable_ms;
	unsigned long lc_disable_ms;
	unsigned long lc_bootup_ms;
	u64 acc_charging_timeout_sec;
	u64 acc_charging_full_begin_ns;
	u64 acc_discharging_begin_ns;
	unsigned long event_map;
	bool state_machine_running;
	bool state_machine_enabled;
	spinlock_t pogo_event_lock;

	/* Register the notifier from USB core */
	struct notifier_block udev_nb;
	/* When true, a superspeed (or better) USB device is enumerated */
	bool ss_udev_attached;
	bool main_hcd_suspend;
	bool shared_hcd_suspend;

	/* Register the notifier for reboot events */
	struct notifier_block reboot_nb;
	struct mutex reboot_lock;
	bool rebooting;

	/* To read voltage at the pogo pins */
	struct power_supply *pogo_psy;
	/* To read the status exported from pogo accessory charger */
	struct power_supply *acc_charger_psy;
	char *acc_charger_psy_name;
	enum lc_stages lc_stage;
	/* Retry when voltage is less than POGO_USB_CAPABLE_THRESHOLD_UV */
	unsigned int retry_count;
	/* To signal userspace extcon observer */
	struct extcon_dev *extcon;
	/* When true, disable voltage based detection of pogo partners */
	bool disable_voltage_detection;
	struct gvotable_election *charger_mode_votable;
	struct gvotable_election *ssphy_restart_votable;

	/* Used for cancellable work such as pogo debouncing */
	struct kthread_delayed_work pogo_accessory_debounce_work;

	struct alarm lc_check_alarm;
	struct kthread_work lc_work;

	/* Pogo accessory detection status */
	enum pogo_accessory_detection accessory_detection_enabled;

	/* Orientation of USB-C, 0:TYPEC_POLARITY_CC1 1:TYPEC_POLARITY_CC2 */
	enum typec_cc_polarity polarity;

	/* Cache values from the Type-C driver */
	enum typec_data_role usbc_data_role;
	bool usbc_data_active;
};

static const unsigned int pogo_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_DOCK,
	EXTCON_NONE,
};

/*
 * list of USB VID:PID pair of udevs which are audio docks with pogo interfaces
 * Both VID and PID are required in each entry.
 */
static const struct pogo_transport_udev_ids audio_dock_ids[] = {
	{
		.vendor = cpu_to_le16(0x18d1),
		.product = cpu_to_le16(0x9480),
	},
	{ },
};

/* Return true if @vid and @pid pair is found in @list. Otherwise, return false. */
static bool pogo_transport_match_udev(const struct pogo_transport_udev_ids *list, const u16 vid,
				      const u16 pid)
{
	if (list) {
		while (list->vendor && list->product) {
			if (list->vendor == cpu_to_le16(vid) && list->product == cpu_to_le16(pid))
				return true;
			list++;
		}
	}
	return false;
}

static void pogo_transport_event(struct pogo_transport *pogo_transport,
				 enum pogo_event_type event_type, int delay_ms);

static void update_extcon_dev(struct pogo_transport *pogo_transport, bool docked, bool usb_capable)
{
	int ret;

	/* While docking, Signal EXTCON_USB before signalling EXTCON_DOCK */
	if (docked) {
		ret = extcon_set_state_sync(pogo_transport->extcon, EXTCON_USB, usb_capable);
		if (ret)
			dev_err(pogo_transport->dev, "%s Failed to %s EXTCON_USB\n", __func__,
				usb_capable ? "set" : "clear");
		ret = extcon_set_state_sync(pogo_transport->extcon, EXTCON_DOCK, true);
		if (ret)
			dev_err(pogo_transport->dev, "%s Failed to set EXTCON_DOCK\n", __func__);
		return;
	}

	/* b/241919179: While undocking, Signal EXTCON_DOCK before signalling EXTCON_USB */
	ret = extcon_set_state_sync(pogo_transport->extcon, EXTCON_DOCK, false);
	if (ret)
		dev_err(pogo_transport->dev, "%s Failed to clear EXTCON_DOCK\n", __func__);
	ret = extcon_set_state_sync(pogo_transport->extcon, EXTCON_USB, false);
	if (ret)
		dev_err(pogo_transport->dev, "%s Failed to clear EXTCON_USB\n", __func__);
}

static void ssphy_restart_control(struct pogo_transport *pogo_transport, bool enable)
{
	mutex_lock(&pogo_transport->reboot_lock);
	if (pogo_transport->rebooting) {
		dev_info(pogo_transport->dev, "ignore ssphy_restart during reboot\n");
		mutex_unlock(&pogo_transport->reboot_lock);
		return;
	}

	if (!pogo_transport->ssphy_restart_votable)
		pogo_transport->ssphy_restart_votable =
				gvotable_election_get_handle(SSPHY_RESTART_EL);

	if (!pogo_transport->ssphy_restart_votable) {
		logbuffer_log(pogo_transport->log, "SSPHY_RESTART get failed\n");
		mutex_unlock(&pogo_transport->reboot_lock);
		return;
	}

	logbuffer_log(pogo_transport->log, "ssphy_restart_control %u", enable);
	gvotable_cast_long_vote(pogo_transport->ssphy_restart_votable, POGO_VOTER, enable, enable);
	mutex_unlock(&pogo_transport->reboot_lock);
}

/*
 * Update the polarity to EXTCON_USB_HOST. If @sync is true, use the sync version to set the
 * property.
 */
static void pogo_transport_update_polarity(struct pogo_transport *pogo_transport, int polarity,
					   bool sync)
{
	union extcon_property_value prop = {.intval = polarity};
	struct max77759_plat *chip = pogo_transport->chip;
	int ret;

	if (sync)
		ret = extcon_set_property_sync(chip->extcon, EXTCON_USB_HOST,
					       EXTCON_PROP_USB_TYPEC_POLARITY,
					       prop);
	else
		ret = extcon_set_property(chip->extcon, EXTCON_USB_HOST,
					  EXTCON_PROP_USB_TYPEC_POLARITY,
					  prop);
	logbuffer_log(pogo_transport->log, "%sset polarity to %d sync %u", ret ? "failed to " : "",
		      prop.intval, sync);
}

static void disable_and_bypass_hub(struct pogo_transport *pogo_transport)
{
	int ret;

	if (!pogo_transport->hub_embedded)
		return;

	/* USB_MUX_HUB_SEL set to 0 to bypass the hub */
	gpio_set_value(pogo_transport->pogo_hub_sel_gpio, 0);
	logbuffer_log(pogo_transport->log, "POGO: hub-mux:%d",
		      gpio_get_value(pogo_transport->pogo_hub_sel_gpio));
	pogo_transport->pogo_hub_active = false;

	/*
	 * No further action in the callback of the votable if it is disabled. Disable it here for
	 * the bookkeeping purpose in the dumpstate.
	 */
	ssphy_restart_control(pogo_transport, false);

	if (pogo_transport->hub_ldo && regulator_is_enabled(pogo_transport->hub_ldo) > 0) {
		ret = regulator_disable(pogo_transport->hub_ldo);
		if (ret)
			logbuffer_log(pogo_transport->log, "Failed to disable hub_ldo %d", ret);
	}
}

static void switch_to_usbc_locked(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;
	int ret;

	if (pogo_transport->pogo_usb_active) {
		ret = extcon_set_state_sync(chip->extcon, EXTCON_USB_HOST, 0);
		logbuffer_log(pogo_transport->log, "%s: %s turning off host for Pogo", __func__,
			      ret < 0 ? "Failed" : "Succeeded");
		pogo_transport->pogo_usb_active = false;
	}

	disable_and_bypass_hub(pogo_transport);

	ret = pinctrl_select_state(pogo_transport->pinctrl, pogo_transport->susp_usb_state);
	if (ret)
		dev_err(pogo_transport->dev, "failed to select suspend in usb state ret:%d\n", ret);

	gpio_set_value(pogo_transport->pogo_data_mux_gpio, 0);
	logbuffer_log(pogo_transport->log, "POGO: data-mux:%d",
		      gpio_get_value(pogo_transport->pogo_data_mux_gpio));
	data_alt_path_active(chip, false);

	/*
	 * Calling extcon_set_state_sync to turn off the host resets the orientation of USB-C and
	 * the USB phy was also reset to the default value CC1.
	 * Update the orientation for superspeed phy if USB-C is connected and CC2 is active.
	 */
	if (pogo_transport->polarity == TYPEC_POLARITY_CC2)
		pogo_transport_update_polarity(pogo_transport, TYPEC_POLARITY_CC2, false);

	enable_data_path_locked(chip);
	/* pogo_transport->pogo_usb_active updated. Delaying till usb-c is activated. */
	kobject_uevent(&pogo_transport->dev->kobj, KOBJ_CHANGE);
}

static void switch_to_pogo_locked(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;
	int ret;

	data_alt_path_active(chip, true);
	if (chip->data_active) {
		ret = extcon_set_state_sync(chip->extcon, chip->active_data_role == TYPEC_HOST ?
					    EXTCON_USB_HOST : EXTCON_USB, 0);

		logbuffer_log(pogo_transport->log, "%s turning off %s", ret < 0 ?
			      "Failed" : "Succeeded", chip->active_data_role == TYPEC_HOST ?
			      "Host" : "Device");
		chip->data_active = false;
	}

	disable_and_bypass_hub(pogo_transport);

	ret = pinctrl_select_state(pogo_transport->pinctrl, pogo_transport->susp_pogo_state);
	if (ret)
		dev_err(pogo_transport->dev, "failed to select suspend in pogo state ret:%d\n",
			ret);

	gpio_set_value(pogo_transport->pogo_data_mux_gpio, 1);
	logbuffer_log(pogo_transport->log, "POGO: data-mux:%d",
		      gpio_get_value(pogo_transport->pogo_data_mux_gpio));
	ret = extcon_set_state_sync(chip->extcon, EXTCON_USB_HOST, 1);
	logbuffer_log(pogo_transport->log, "%s: %s turning on host for Pogo", __func__, ret < 0 ?
		      "Failed" : "Succeeded");
	pogo_transport->pogo_usb_active = true;
	/* pogo_transport->pogo_usb_active updated */
	kobject_uevent(&pogo_transport->dev->kobj, KOBJ_CHANGE);
}

static void switch_to_hub_locked(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;
	int ret;

	/*
	 * TODO: set alt_path_active; re-design this function for
	 * 1. usb-c only (hub disabled)
	 * 2. pogo only (hub disabled)
	 * 3. hub enabled for both usb-c host and pogo host
	 */
	data_alt_path_active(chip, true);

	/* if usb-c is active, disable it */
	if (chip->data_active) {
		ret = extcon_set_state_sync(chip->extcon, chip->active_data_role == TYPEC_HOST ?
					    EXTCON_USB_HOST : EXTCON_USB, 0);

		logbuffer_log(pogo_transport->log, "%s turning off %s", ret < 0 ?
			      "Failed" : "Succeeded", chip->active_data_role == TYPEC_HOST ?
			      "Host" : "Device");
		chip->data_active = false;
	}

	/* if pogo-usb is active, disable it */
	if (pogo_transport->pogo_usb_active) {
		ret = extcon_set_state_sync(chip->extcon, EXTCON_USB_HOST, 0);
		logbuffer_log(pogo_transport->log, "%s: %s turning off host for Pogo", __func__,
			      ret < 0 ? "Failed" : "Succeeded");
		/*
		 * Skipping KOBJ_CHANGE here as it's a transient state. Should be changed if the
		 * function logic changes to having branches to exit the function before
		 * pogo_usb_active to true.
		 */
		pogo_transport->pogo_usb_active = false;
	}

	if (pogo_transport->hub_ldo) {
		ret = regulator_enable(pogo_transport->hub_ldo);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to enable hub_ldo %d",
				      __func__, ret);
	}

	ret = pinctrl_select_state(pogo_transport->pinctrl, pogo_transport->hub_state);
	if (ret)
		dev_err(pogo_transport->dev, "failed to select hub state ret:%d\n", ret);

	/* USB_MUX_POGO_SEL set to 0 to direct usb-c to AP or hub */
	gpio_set_value(pogo_transport->pogo_data_mux_gpio, 0);

	/* USB_MUX_HUB_SEL set to 1 to switch the path to hub */
	gpio_set_value(pogo_transport->pogo_hub_sel_gpio, 1);
	logbuffer_log(pogo_transport->log, "POGO: data-mux:%d hub-mux:%d",
		      gpio_get_value(pogo_transport->pogo_data_mux_gpio),
		      gpio_get_value(pogo_transport->pogo_hub_sel_gpio));

	/* wait for the host mode to be turned off completely */
	mdelay(60);

	/*
	 * The polarity was reset to 0 when Host Mode was disabled for USB-C or POGO. If current
	 * polarity is CC2, update it to ssphy before enabling the Host Mode for hub.
	 */
	if (pogo_transport->polarity == TYPEC_POLARITY_CC2)
		pogo_transport_update_polarity(pogo_transport, pogo_transport->polarity, false);

	ret = extcon_set_state_sync(chip->extcon, EXTCON_USB_HOST, 1);
	logbuffer_log(pogo_transport->log, "%s: %s turning on host for hub", __func__, ret < 0 ?
		      "Failed" : "Succeeded");

	pogo_transport->pogo_usb_active = true;
	pogo_transport->pogo_hub_active = true;
	/* pogo_transport->pogo_usb_active updated.*/
	kobject_uevent(&pogo_transport->dev->kobj, KOBJ_CHANGE);
}

static void update_pogo_transport(struct pogo_transport *pogo_transport,
				  enum pogo_event_type event_type)
{
	struct max77759_plat *chip = pogo_transport->chip;
	int ret;
	union power_supply_propval voltage_now = {0};
	bool docked = !gpio_get_value(pogo_transport->pogo_gpio);
	bool acc_detected = gpio_get_value(pogo_transport->pogo_acc_gpio);

	ret = power_supply_get_property(pogo_transport->pogo_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW,
					&voltage_now);
	if (ret) {
		dev_err(pogo_transport->dev, "%s voltage now read err: %d\n", __func__, ret);
		if (ret == -EAGAIN)
			pogo_transport_event(pogo_transport, EVENT_RETRY_READ_VOLTAGE,
					     POGO_PSY_NRDY_RETRY_MS);
		goto free;
	}

	if (event_type == EVENT_DOCKING || event_type == EVENT_RETRY_READ_VOLTAGE) {
		if (docked) {
			if (pogo_transport->disable_voltage_detection ||
			    voltage_now.intval >= POGO_USB_CAPABLE_THRESHOLD_UV) {
				pogo_transport->pogo_usb_capable = true;
				update_extcon_dev(pogo_transport, true, true);
			} else {
				/* retry every 50ms * 10 times */
				if (pogo_transport->retry_count < POGO_USB_RETRY_COUNT) {
					pogo_transport->retry_count++;
					pogo_transport_event(pogo_transport,
							     EVENT_RETRY_READ_VOLTAGE,
							     POGO_USB_RETRY_INTEREVAL_MS);
				} else {
					pogo_transport->pogo_usb_capable = false;
					update_extcon_dev(pogo_transport, true, false);
				}
				goto free;
			}
		} else {
			/* Clear retry count when un-docked */
			pogo_transport->retry_count = 0;
			pogo_transport->pogo_usb_capable = false;
			update_extcon_dev(pogo_transport, false, false);
		}
	}

	mutex_lock(&chip->data_path_lock);

	/* Special case for force_usb: ignore everything */
	if (modparam_force_usb)
		goto exit;

	/*
	 * Special case for force_pogo: switch to pogo if available; switch to usbc when undocking.
	 */
	if (pogo_transport->force_pogo) {
		if (pogo_transport->pogo_usb_capable && !pogo_transport->pogo_usb_active)
			switch_to_pogo_locked(pogo_transport);
		else if (!pogo_transport->pogo_usb_capable && pogo_transport->pogo_usb_active)
			switch_to_usbc_locked(pogo_transport);
		goto exit;
	}

	if (pogo_transport->mock_hid_connected) {
		switch (event_type) {
		case EVENT_ENABLE_HUB:
		case EVENT_DISABLE_HUB:
		case EVENT_FORCE_ACC_CONNECT:
		case EVENT_HALL_SENSOR_ACC_UNDOCKED:
			break;
		default:
			logbuffer_log(pogo_transport->log, "%s: skipping mock_hid_connected set",
				      __func__);
			goto exit;
		}
	}

	switch (event_type) {
	case EVENT_DOCKING:
	case EVENT_RETRY_READ_VOLTAGE:
		if (pogo_transport->pogo_usb_capable && !pogo_transport->pogo_usb_active) {
			/*
			 * Pogo treated with same priority as USB-C, hence skip enabling
			 * pogo usb as USB-C is active.
			 */
			if (chip->data_active && pogo_transport->equal_priority) {
				dev_info(pogo_transport->dev,
					 "usb active, skipping enable pogo usb\n");
				goto exit;
			}
			switch_to_pogo_locked(pogo_transport);
		} else if (!pogo_transport->pogo_usb_capable && pogo_transport->pogo_usb_active) {
			if (pogo_transport->pogo_hub_active && pogo_transport->force_hub_enabled) {
				pogo_transport->pogo_usb_capable = true;
				logbuffer_log(pogo_transport->log, "%s: keep enabling the hub",
					      __func__);
			} else {
				switch_to_usbc_locked(pogo_transport);
			}
		}
		break;
	case EVENT_MOVE_DATA_TO_USB:
		if (pogo_transport->pogo_usb_active)
			switch_to_usbc_locked(pogo_transport);
		break;
	case EVENT_MOVE_DATA_TO_POGO:
		/* Currently this event is bundled to force_pogo. This case is unreachable. */
		break;
	case EVENT_DATA_ACTIVE_CHANGED:
		/* Do nothing if USB-C data becomes active or hub is enabled. */
		if ((chip->data_active && pogo_transport->equal_priority) ||
		    pogo_transport->pogo_hub_active)
			break;

		/* Switch to POGO if POGO path is available. */
		if (pogo_transport->pogo_usb_capable && !pogo_transport->pogo_usb_active)
			switch_to_pogo_locked(pogo_transport);
		break;
	case EVENT_ENABLE_HUB:
		pogo_transport->pogo_usb_capable = true;
		switch_to_hub_locked(pogo_transport);
		break;
	case EVENT_DISABLE_HUB:
		if (pogo_transport->pogo_usb_capable)
			switch_to_pogo_locked(pogo_transport);
		else
			switch_to_usbc_locked(pogo_transport);
		break;
	case EVENT_HALL_SENSOR_ACC_DETECTED:
		/* Disable OVP to prevent the voltage going through POGO_VIN */
		if (pogo_transport->pogo_ovp_en_gpio >= 0)
			gpio_set_value_cansleep(pogo_transport->pogo_ovp_en_gpio,
						!pogo_transport->pogo_ovp_en_active_state);

		if (pogo_transport->acc_detect_ldo &&
		    pogo_transport->accessory_detection_enabled == ENABLED) {
			ret = regulator_enable(pogo_transport->acc_detect_ldo);
			if (ret)
				logbuffer_log(pogo_transport->log, "%s: Failed to enable acc_detect %d",
					      __func__, ret);
		} else if (pogo_transport->accessory_detection_enabled == HALL_ONLY) {
			logbuffer_log(pogo_transport->log,
				      "%s: Skip enabling comparator logic, enable vout", __func__);
			if (pogo_transport->pogo_irq_enabled) {
				disable_irq_nosync(pogo_transport->pogo_irq);
				pogo_transport->pogo_irq_enabled = false;
			}
			ret = gvotable_cast_long_vote(pogo_transport->charger_mode_votable,
						      POGO_VOTER, GBMS_POGO_VOUT, 1);
			if (ret)
				logbuffer_log(pogo_transport->log,
					      "%s: Failed to vote VOUT, ret %d", __func__, ret);
			switch_to_pogo_locked(pogo_transport);
			pogo_transport->pogo_usb_capable = true;
		}
		break;
	case EVENT_HALL_SENSOR_ACC_UNDOCKED:
		pogo_transport->mock_hid_connected = 0;
		ret = gvotable_cast_long_vote(pogo_transport->charger_mode_votable, POGO_VOTER,
					      GBMS_POGO_VOUT, 0);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to unvote VOUT, ret %d",
				      __func__, ret);

		if (pogo_transport->acc_detect_ldo &&
		    regulator_is_enabled(pogo_transport->acc_detect_ldo)) {
			ret = regulator_disable(pogo_transport->acc_detect_ldo);
			if (ret)
				logbuffer_log(pogo_transport->log, "%s: Failed to disable acc_detect %d",
					      __func__, ret);
		}

		if (!pogo_transport->pogo_irq_enabled) {
			enable_irq(pogo_transport->pogo_irq);
			pogo_transport->pogo_irq_enabled = true;
		}

		if (!pogo_transport->acc_irq_enabled) {
			enable_irq(pogo_transport->pogo_acc_irq);
			pogo_transport->acc_irq_enabled = true;
		}

		if (pogo_transport->pogo_hub_active && pogo_transport->force_hub_enabled) {
			logbuffer_log(pogo_transport->log, "%s: keep enabling the hub", __func__);
		} else {
			switch_to_usbc_locked(pogo_transport);
			pogo_transport->pogo_usb_capable = false;
		}
		break;
	case EVENT_POGO_ACC_DEBOUNCED:
		logbuffer_log(pogo_transport->log, "%s: acc detect debounce %s", __func__,
			      acc_detected ? "success, enabling pogo_vout" : "fail");
		/* Do nothing if debounce fails */
		if (!acc_detected)
			break;

		if (pogo_transport->acc_irq_enabled) {
			disable_irq(pogo_transport->pogo_acc_irq);
			pogo_transport->acc_irq_enabled = false;
		}

		ret = gvotable_cast_long_vote(pogo_transport->charger_mode_votable, POGO_VOTER,
					      GBMS_POGO_VOUT, 1);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to vote VOUT, ret %d",
				      __func__, ret);
		break;
	case EVENT_POGO_ACC_CONNECTED:
		/*
		 * Enable pogo only if the acc regulator was enabled. If the regulator has been
		 * disabled, it means EVENT_HALL_SENSOR_ACC_UNDOCKED was triggered before this
		 * event.
		 */
		if (pogo_transport->acc_detect_ldo &&
		    regulator_is_enabled(pogo_transport->acc_detect_ldo)) {
			ret = regulator_disable(pogo_transport->acc_detect_ldo);
			if (ret)
				logbuffer_log(pogo_transport->log, "%s: Failed to disable acc_detect_ldo %d",
					      __func__, ret);
		}
		if (pogo_transport->accessory_detection_enabled) {
			if (!pogo_transport->mfg_acc_test) {
				switch_to_pogo_locked(pogo_transport);
				pogo_transport->pogo_usb_capable = true;
			}
		}
		break;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	case EVENT_FORCE_ACC_CONNECT:
		if (pogo_transport->pogo_irq_enabled) {
			disable_irq(pogo_transport->pogo_irq);
			pogo_transport->pogo_irq_enabled = false;
		}

		if (pogo_transport->acc_irq_enabled) {
			disable_irq(pogo_transport->pogo_acc_irq);
			pogo_transport->acc_irq_enabled = false;
		}

		if (pogo_transport->pogo_ovp_en_gpio >= 0)
			gpio_set_value_cansleep(pogo_transport->pogo_ovp_en_gpio,
						!pogo_transport->pogo_ovp_en_active_state);

		/* Disable, just in case when docked, if acc_detect_ldo was on */
		if (pogo_transport->acc_detect_ldo &&
		    regulator_is_enabled(pogo_transport->acc_detect_ldo)) {
			ret = regulator_disable(pogo_transport->acc_detect_ldo);
			if (ret)
				logbuffer_log(pogo_transport->log,
					      "%s: Failed to disable acc_detect %d", __func__, ret);
		}

		ret = gvotable_cast_long_vote(pogo_transport->charger_mode_votable, POGO_VOTER,
					      GBMS_POGO_VOUT, 1);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to vote VOUT, ret %d",
				      __func__, ret);

		switch_to_pogo_locked(pogo_transport);
		pogo_transport->pogo_usb_capable = true;
		break;
#endif
	case EVENT_ORIENTATION_CHANGED:
		/* Update the orientation and restart the ssphy if hub is enabled */
		if (pogo_transport->pogo_hub_active) {
			pogo_transport_update_polarity(pogo_transport, pogo_transport->polarity,
						       true);
			ssphy_restart_control(pogo_transport, true);
		}
		break;
	default:
		break;
	}

exit:
	mutex_unlock(&chip->data_path_lock);
	kobject_uevent(&pogo_transport->dev->kobj, KOBJ_CHANGE);
free:
	logbuffer_logk(pogo_transport->log, LOGLEVEL_INFO,
		       "ev:%u dock:%u f_u:%u f_p:%u f_h:%u p_u:%u p_act:%u hub:%u d_act:%u mock:%u v:%d",
		       event_type,
		       docked ? 1 : 0,
		       modparam_force_usb ? 1 : 0,
		       pogo_transport->force_pogo ? 1 : 0,
		       pogo_transport->force_hub_enabled ? 1 : 0,
		       pogo_transport->pogo_usb_capable ? 1 : 0,
		       pogo_transport->pogo_usb_active ? 1 : 0,
		       pogo_transport->pogo_hub_active ? 1 : 0,
		       chip->data_active ? 1 : 0,
		       pogo_transport->mock_hid_connected ? 1 : 0,
		       voltage_now.intval);
}

static void process_generic_event(struct kthread_work *work)
{
	struct pogo_event *event =
		container_of(container_of(work, struct kthread_delayed_work, work),
			     struct pogo_event, work);
	struct pogo_transport *pogo_transport = event->pogo_transport;

	update_pogo_transport(pogo_transport, event->event_type);

	devm_kfree(pogo_transport->dev, event);
}

static void process_debounce_event(struct kthread_work *work)
{
	struct pogo_transport *pogo_transport =
		container_of(container_of(work, struct kthread_delayed_work, work),
			     struct pogo_transport, pogo_accessory_debounce_work);

	update_pogo_transport(pogo_transport, EVENT_POGO_ACC_DEBOUNCED);
}

static void pogo_transport_event(struct pogo_transport *pogo_transport,
				 enum pogo_event_type event_type, int delay_ms)
{
	struct pogo_event *evt;

	if (event_type == EVENT_POGO_ACC_DEBOUNCED) {
		kthread_mod_delayed_work(pogo_transport->wq,
					 &pogo_transport->pogo_accessory_debounce_work,
					 msecs_to_jiffies(delay_ms));
		return;
	}

	evt = devm_kzalloc(pogo_transport->dev, sizeof(*evt), GFP_KERNEL);
	if (!evt) {
		logbuffer_log(pogo_transport->log, "POGO: Dropping event");
		return;
	}
	kthread_init_delayed_work(&evt->work, process_generic_event);
	evt->pogo_transport = pogo_transport;
	evt->event_type = event_type;
	kthread_mod_delayed_work(pogo_transport->wq, &evt->work, msecs_to_jiffies(delay_ms));
}

/*-------------------------------------------------------------------------*/
/* State Machine Functions                                                 */
/*-------------------------------------------------------------------------*/

/*
 * State transition
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_set_state(struct pogo_transport *pogo_transport, enum pogo_state state,
				     unsigned int delay_ms)
{
	if (delay_ms) {
		logbuffer_log(pogo_transport->log, "pending state change %s -> %s @ %u ms",
			      pogo_states[pogo_transport->state], pogo_states[state], delay_ms);
		pogo_transport->delayed_state = state;
		kthread_mod_delayed_work(pogo_transport->wq, &pogo_transport->state_machine,
					 msecs_to_jiffies(delay_ms));
		pogo_transport->delayed_runtime = jiffies + msecs_to_jiffies(delay_ms);
		pogo_transport->delay_ms = delay_ms;
	} else {
		logbuffer_logk(pogo_transport->log, LOGLEVEL_INFO, "state change %s -> %s [%s]",
			       pogo_states[pogo_transport->state], pogo_states[state],
			       pogo_transport->lc ? "lc" : "");
		pogo_transport->delayed_state = INVALID_STATE;
		pogo_transport->prev_state = pogo_transport->state;
		pogo_transport->state = state;

		if (!pogo_transport->state_machine_running)
			kthread_mod_delayed_work(pogo_transport->wq, &pogo_transport->state_machine,
						 0);
	}
}

/*
 * Accessory Detection regulator control
 *  - Return -ENXIO if Accessory Detection regulator does not exist
 *  - Return 0 if @enable is the same as the status of the regulator
 *  - Otherwise, return the return value from regulator_enable or regulator_disable
 */
static int pogo_transport_acc_regulator(struct pogo_transport *pogo_transport, bool enable)
{
	int ret;

	if (!pogo_transport->acc_detect_ldo)
		return -ENXIO;

	if (regulator_is_enabled(pogo_transport->acc_detect_ldo) == enable)
		return 0;

	if (enable)
		ret = regulator_enable(pogo_transport->acc_detect_ldo);
	else
		ret = regulator_disable(pogo_transport->acc_detect_ldo);

	return ret;
}

/*
 * Call this function to:
 *  - Disable POGO Vout by voting 0 to charger_mode_votable
 *  - Disable the regulator for Accessory Detection Logic
 *  - Disable Accessory Detection IRQ
 *  - Enable POGO Voltage Detection IRQ
 *
 *  This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_reset_acc_detection(struct pogo_transport *pogo_transport)
{
	int ret;

	ret = gvotable_cast_long_vote(pogo_transport->charger_mode_votable, POGO_VOTER,
				      GBMS_POGO_VOUT, 0);
	if (ret)
		logbuffer_log(pogo_transport->log, "%s: Failed to unvote VOUT, ret %d", __func__,
			      ret);

	ret = pogo_transport_acc_regulator(pogo_transport, false);
	if (ret)
		logbuffer_log(pogo_transport->log, "%s: Failed to disable acc_detect %d", __func__,
			      ret);

	if (pogo_transport->acc_irq_enabled) {
		disable_irq(pogo_transport->pogo_acc_irq);
		pogo_transport->acc_irq_enabled = false;
	}

	if (!pogo_transport->pogo_irq_enabled) {
		enable_irq(pogo_transport->pogo_irq);
		pogo_transport->pogo_irq_enabled = true;
	}
}

/*
 * This function implements the actions unon entering each state.
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_run_state_machine(struct pogo_transport *pogo_transport)
{
	bool acc_detected = gpio_get_value(pogo_transport->pogo_acc_gpio);
	bool docked = !gpio_get_value(pogo_transport->pogo_gpio);
	struct max77759_plat *chip = pogo_transport->chip;
	int ret;

	switch (pogo_transport->state) {
	case STANDBY:
		/* DATA_STATUS_ENABLED */
		break;
	case DOCKING_DEBOUNCED:
		if (docked) {
			switch_to_hub_locked(pogo_transport);
			pogo_transport_set_state(pogo_transport, DOCK_HUB, 0);
		} else {
			pogo_transport_set_state(pogo_transport, STANDBY, 0);
		}
		break;
	case DOCK_HUB:
		/* clear Dock dock detected notification */
		/* Clear accessory detected notification */
		/* DATA_STATUS_DISABLED_DEVICE_DOCK */
		update_extcon_dev(pogo_transport, true, true);
		break;
	case DOCK_AUDIO_HUB:
		update_extcon_dev(pogo_transport, true, true);
		break;
	case DEVICE_DOCKING_DEBOUNCED:
		if (docked) {
			switch_to_hub_locked(pogo_transport);
			/* switch_to_hub_locked cleared data_active, set it here */
			chip->data_active = true;
			pogo_transport_set_state(pogo_transport, DOCK_DEVICE_HUB, 0);
		} else {
			pogo_transport_set_state(pogo_transport, DEVICE_DIRECT, 0);
		}
		break;
	case DOCK_DEVICE_HUB:
		update_extcon_dev(pogo_transport, true, true);
		/* DATA_STATUS_DISABLED_DEVICE_DOCK */
		break;
	case DEVICE_HUB_DOCKING_DEBOUNCED:
		if (docked) {
			pogo_transport_set_state(pogo_transport, DOCK_DEVICE_HUB, 0);
		} else {
			pogo_transport_set_state(pogo_transport, DEVICE_HUB, 0);
		}
		break;
	case AUDIO_DIRECT_DOCKING_DEBOUNCED:
		if (docked) {
			pogo_transport_set_state(pogo_transport, AUDIO_DIRECT_DOCK_OFFLINE, 0);
		} else {
			pogo_transport_set_state(pogo_transport, AUDIO_DIRECT, 0);
		}
		break;
	case AUDIO_DIRECT_DOCK_OFFLINE:
		/* Push dock detected notification */
		update_extcon_dev(pogo_transport, true, true);
		break;
	case AUDIO_HUB_DOCKING_DEBOUNCED:
		if (docked) {
			pogo_transport_set_state(pogo_transport, DOCK_AUDIO_HUB, 0);
		} else {
			pogo_transport_set_state(pogo_transport, AUDIO_HUB, 0);
		}
		break;
	case HOST_DIRECT:
		/* DATA_STATUS_ENABLED */
		/* Clear Pogo accessory Detected */
		/* Clear USB accessory detected notification */
		break;
	case HOST_DIRECT_DOCKING_DEBOUNCED:
		if (docked) {
			if (pogo_transport->force_pogo) {
				switch_to_hub_locked(pogo_transport);
				/* switch_to_hub_locked cleared data_active, set it here */
				chip->data_active = true;
				pogo_transport_set_state(pogo_transport, DOCK_HUB_HOST_OFFLINE, 0);
			} else {
				pogo_transport_set_state(pogo_transport, HOST_DIRECT_DOCK_OFFLINE,
							 0);
			}
		} else {
			pogo_transport_set_state(pogo_transport, HOST_DIRECT, 0);
		}
		break;
	case HOST_DIRECT_DOCK_OFFLINE:
		/* Push Dock dock detected notification */
		update_extcon_dev(pogo_transport, true, true);
		break;
	case DOCK_HUB_HOST_OFFLINE:
		/* Push accessory detected notification */
		update_extcon_dev(pogo_transport, true, true);
		break;
	case STANDBY_ACC_DEBOUNCED:
	case DEVICE_DIRECT_ACC_DEBOUNCED:
	case DEVICE_HUB_ACC_DEBOUNCED:
	case AUDIO_DIRECT_ACC_DEBOUNCED:
	case AUDIO_HUB_ACC_DEBOUNCED:
	case HOST_DIRECT_ACC_DEBOUNCED:
		/* debounce fail; leave the IRQ and regulator enabled and do nothing */
		if (!acc_detected)
			break;

		/*
		 * Disable the IRQ to ignore the noise after POGO Vout is enabled. It will be
		 * re-enabled when HES reports the attach event.
		 */
		if (pogo_transport->acc_irq_enabled) {
			disable_irq(pogo_transport->pogo_acc_irq);
			pogo_transport->acc_irq_enabled = false;
		}

		/* TODO: queue work for gvotable cast vote if it takes too much time */
		ret = gvotable_cast_long_vote(pogo_transport->charger_mode_votable, POGO_VOTER,
					      GBMS_POGO_VOUT, 1);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to vote VOUT, ret %d",
				      __func__, ret);
		break;
	case ACC_DIRECT:
		/* Clear Pogo accessory Detected */
		/* Clear USB accessory detected notification */
		break;
	case ACC_DEVICE_HUB:
		/* DATA_STATUS_DISABLED_DEVICE_DOCK */
		break;
	case HOST_DIRECT_ACC_OFFLINE:
		/* Push Pogo accessory Detected */
		break;
	default:
		break;
	}
}

/* Main loop of the State Machine */
static void pogo_transport_state_machine_work(struct kthread_work *work)
{
	struct pogo_transport *pogo_transport =
			container_of(container_of(work, struct kthread_delayed_work, work),
			     struct pogo_transport, state_machine);
	struct max77759_plat *chip = pogo_transport->chip;
	enum pogo_state prev_state;

	mutex_lock(&chip->data_path_lock);
	pogo_transport->state_machine_running = true;

	if (pogo_transport->delayed_state) {
		logbuffer_logk(pogo_transport->log, LOGLEVEL_INFO,
			       "state change %s -> %s [delayed %ld ms] [%s]",
			       pogo_states[pogo_transport->state],
			       pogo_states[pogo_transport->delayed_state],
			       pogo_transport->delay_ms,
			       pogo_transport->lc ? "lc" : "");
		pogo_transport->prev_state = pogo_transport->state;
		pogo_transport->state = pogo_transport->delayed_state;
		pogo_transport->delayed_state = INVALID_STATE;
	}

	do {
		prev_state = pogo_transport->state;
		pogo_transport_run_state_machine(pogo_transport);
	} while (pogo_transport->state != prev_state && !pogo_transport->delayed_state);

	pogo_transport->state_machine_running = false;
	mutex_unlock(&chip->data_path_lock);
}

/*
 * Called when POGO Voltage Detection IRQ is active
 *  - Triggered from event: EVENT_POGO_IRQ
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_pogo_irq_active(struct pogo_transport *pogo_transport)
{
	switch (pogo_transport->state) {
	case STANDBY:
	case STANDBY_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, DOCKING_DEBOUNCED, POGO_PSY_DEBOUNCE_MS);
		break;
	case DEVICE_HUB:
	case DEVICE_HUB_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, DEVICE_HUB_DOCKING_DEBOUNCED,
					 POGO_PSY_DEBOUNCE_MS);
		break;
	case DEVICE_DIRECT:
	case DEVICE_DIRECT_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, DEVICE_DOCKING_DEBOUNCED,
					 POGO_PSY_DEBOUNCE_MS);
		break;
	case AUDIO_DIRECT:
	case AUDIO_DIRECT_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, AUDIO_DIRECT_DOCKING_DEBOUNCED,
					 POGO_PSY_DEBOUNCE_MS);
		break;
	case AUDIO_HUB:
	case AUDIO_HUB_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, AUDIO_HUB_DOCKING_DEBOUNCED,
					 POGO_PSY_DEBOUNCE_MS);
		break;
	case HOST_DIRECT:
	case HOST_DIRECT_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, HOST_DIRECT_DOCKING_DEBOUNCED,
					 POGO_PSY_DEBOUNCE_MS);
		break;
	default:
		break;
	}
}

/*
 * Called when POGO Voltage Detection IRQ is standby
 *  - Triggered from event: EVENT_POGO_IRQ
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_pogo_irq_standby(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;

	/* Pogo irq in standy implies undocked. Signal userspace before altering data path. */
	update_extcon_dev(pogo_transport, false, false);
	switch (pogo_transport->state) {
	case STANDBY:
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		break;
	case DOCK_HUB:
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		break;
	case DOCK_DEVICE_HUB:
		pogo_transport_set_state(pogo_transport, DEVICE_HUB, 0);
		break;
	case DOCK_AUDIO_HUB:
		pogo_transport_set_state(pogo_transport, AUDIO_HUB, 0);
		break;
	case AUDIO_DIRECT_DOCK_OFFLINE:
		pogo_transport_set_state(pogo_transport, AUDIO_DIRECT, 0);
		break;
	case HOST_DIRECT_DOCK_OFFLINE:
		pogo_transport_set_state(pogo_transport, HOST_DIRECT, 0);
		break;
	case DOCK_HUB_HOST_OFFLINE:
		/* Clear data_active so that Type-C stack is able to enable the USB data later */
		chip->data_active = false;
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, HOST_DIRECT, 0);
		break;
	default:
		break;
	}
}

/*
 * Called when USB-C port enters Host Mode
 *  - Triggered from event: EVENT_USBC_DATA_CHANGE
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_usbc_host_on(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;

	switch (pogo_transport->state) {
	case STANDBY:
		pogo_transport_set_state(pogo_transport, DEVICE_DIRECT, 0);
		break;
	case DOCK_HUB:
		/* Set data_active since USB-C device is attached */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, DOCK_DEVICE_HUB, 0);
		break;
	case ACC_DIRECT:
		switch_to_hub_locked(pogo_transport);
		/* Set data_active since USB-C device is attached */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, ACC_DEVICE_HUB, 0);
		break;
	case ACC_HUB:
		/* Set data_active since USB-C device is attached */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, ACC_DEVICE_HUB, 0);
		break;
	case LC:
		/* Set data_active since USB-C device is attached */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, LC_DEVICE_DIRECT, 0);
		break;
	default:
		break;
	}
}

/*
 * Called when USB-C port leaves Host Mode
 *  - Triggered from event: EVENT_USBC_DATA_CHANGE
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_usbc_host_off(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;
	bool ss_attached = pogo_transport->ss_udev_attached;

	pogo_transport->ss_udev_attached = false;

	switch (pogo_transport->state) {
	case DOCK_DEVICE_HUB:
		/* Clear data_active since USB-C device is detached */
		chip->data_active = false;
		pogo_transport_set_state(pogo_transport, DOCK_HUB, 0);
		break;
	case DEVICE_HUB:
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		break;
	case DEVICE_DIRECT:
	case AUDIO_DIRECT:
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		break;
	case AUDIO_DIRECT_DOCK_OFFLINE:
		switch_to_hub_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, DOCK_HUB, 0);
		break;
	case DOCK_AUDIO_HUB:
		/* Clear data_active since USB-C device is detached */
		chip->data_active = false;
		pogo_transport_set_state(pogo_transport, DOCK_HUB, 0);
		break;
	case AUDIO_HUB:
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		break;
	case ACC_DEVICE_HUB:
	case ACC_AUDIO_HUB:
		/* b/271669059 */
		if (ss_attached) {
			/* USB_MUX_HUB_SEL set to 0 to bypass the hub */
			gpio_set_value(pogo_transport->pogo_hub_sel_gpio, 0);
			logbuffer_log(pogo_transport->log, "POGO: toggling hub-mux, hub-mux:%d",
				      gpio_get_value(pogo_transport->pogo_hub_sel_gpio));
			mdelay(10);
			/* USB_MUX_HUB_SEL set to 1 to switch the path to hub */
			gpio_set_value(pogo_transport->pogo_hub_sel_gpio, 1);
			logbuffer_log(pogo_transport->log, "POGO: hub-mux:%d",
				      gpio_get_value(pogo_transport->pogo_hub_sel_gpio));
		}

		/* Clear data_active since USB-C device is detached */
		chip->data_active = false;
		pogo_transport_set_state(pogo_transport, ACC_HUB, 0);
		break;
	case LC_DEVICE_DIRECT:
	case LC_AUDIO_DIRECT:
		/* Clear data_active since USB-C device is detached */
		chip->data_active = false;
		pogo_transport_set_state(pogo_transport, LC, 0);
		break;
	default:
		break;
	}
}

/*
 * Called when USB-C port enters Device Mode
 *  - Triggered from event: EVENT_USBC_DATA_CHANGE
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_usbc_device_on(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;

	switch (pogo_transport->state) {
	case STANDBY:
		pogo_transport_set_state(pogo_transport, HOST_DIRECT, 0);
		break;
	case DOCK_HUB:
		/*
		 * Set data_active so that once USB-C cable is detached later, Type-C stack is able
		 * to call back for the data changed event
		 */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, DOCK_HUB_HOST_OFFLINE, 0);
		break;
	case ACC_DIRECT:
		/*
		 * Set data_active so that once USB-C cable is detached later, Type-C stack is able
		 * to call back for the data changed event
		 */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, ACC_DIRECT_HOST_OFFLINE, 0);
		break;
	case ACC_HUB:
		/*
		 * Set data_active so that once USB-C cable is detached later, Type-C stack is able
		 * to call back for the data changed event
		 */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, ACC_HUB_HOST_OFFLINE, 0);
		break;
	case LC:
		/*
		 * Set data_active so that once USB-C cable is detached later, Type-C stack is able
		 * to call back for the data changed event
		 */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, LC_HOST_DIRECT, 0);
		break;
	default:
		break;
	}
}

/*
 * Called when USB-C port leaves Device Mode
 *  - Triggered from event: EVENT_USBC_DATA_CHANGE
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_usbc_device_off(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;

	switch (pogo_transport->state) {
	case HOST_DIRECT:
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		break;
	case HOST_DIRECT_DOCK_OFFLINE:
		switch_to_hub_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, DOCK_HUB, 0);
		break;
	case DOCK_HUB_HOST_OFFLINE:
		/*
		 * Clear data_active so that Type-C stack is able to call back for the data changed
		 * event
		 */
		chip->data_active = false;
		pogo_transport_set_state(pogo_transport, DOCK_HUB, 0);
		break;
	case HOST_DIRECT_ACC_OFFLINE:
		switch_to_pogo_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, ACC_DIRECT, 0);
		break;
	case ACC_DIRECT_HOST_OFFLINE:
		/*
		 * Clear data_active so that Type-C stack is able to call back for the data changed
		 * event
		 */
		chip->data_active = false;
		pogo_transport_set_state(pogo_transport, ACC_DIRECT, 0);
		break;
	case ACC_HUB_HOST_OFFLINE:
		/*
		 * Clear data_active so that Type-C stack is able to call back for the data changed
		 * event
		 */
		chip->data_active = false;
		pogo_transport_set_state(pogo_transport, ACC_HUB, 0);
		break;
	case LC_ALL_OFFLINE:
	case LC_HOST_DIRECT:
		/*
		 * Clear data_active so that Type-C stack is able to call back for the data changed
		 * event
		 */
		chip->data_active = false;
		pogo_transport_set_state(pogo_transport, LC, 0);
		break;
	default:
		break;
	}
}

/*
 * Called when device attribute "move_data_to_usb" is written to 1
 *  - Triggered from event: EVENT_ENABLE_USB_DATA
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_enable_usb_data(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;

	switch (pogo_transport->state) {
	case DOCK_HUB_HOST_OFFLINE:
		/*
		 * Clear data_active so that Type-C stack is able to call back for the data changed
		 * event later
		 */
		chip->data_active = false;
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, HOST_DIRECT_DOCK_OFFLINE, 0);
		break;
	case ACC_DIRECT_HOST_OFFLINE:
	case ACC_HUB_HOST_OFFLINE:
		/*
		 * Clear data_active so that Type-C stack is able to call back for the data changed
		 * event later
		 */
		chip->data_active = false;
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, HOST_DIRECT_ACC_OFFLINE, 0);
		break;
	default:
		return;
	}
}

/*
 * Called when device attribute "force_pogo" is written to 1
 *  - Triggered from event: EVENT_FORCE_POGO
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_force_pogo(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;

	switch (pogo_transport->state) {
	case HOST_DIRECT_DOCK_OFFLINE:
		switch_to_hub_locked(pogo_transport);
		/*
		 * Set data_active so that once USB-C cable is detached later, Type-C stack is able
		 * to call back for the data changed event
		 */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, DOCK_HUB_HOST_OFFLINE, 0);
		break;
	default:
		return;
	}
}

/*
 * Call this function to:
 *  - Disable POGO OVP
 *  - Disable Accessory Detection IRQ
 *  - Disable POGO Voltage Detection IRQ
 *  - Enable POGO Vout by voting 1 to charger_mode_votable
 *
 *  This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_skip_acc_detection(struct pogo_transport *pogo_transport)
{
	int ret;

	logbuffer_log(pogo_transport->log, "%s: Skip enabling comparator logic, enable vout",
		      __func__);

	/*
	 * Disable OVP to prevent the voltage going through POGO_VIN. OVP will be re-enabled once
	 * we vote GBMS_POGO_VIN and GBMS gets the votable result.
	 */
	if (pogo_transport->pogo_ovp_en_gpio >= 0)
		gpio_set_value_cansleep(pogo_transport->pogo_ovp_en_gpio,
					!pogo_transport->pogo_ovp_en_active_state);

	if (pogo_transport->acc_irq_enabled) {
		disable_irq(pogo_transport->pogo_acc_irq);
		pogo_transport->acc_irq_enabled = false;
	}

	if (pogo_transport->pogo_irq_enabled) {
		disable_irq(pogo_transport->pogo_irq);
		pogo_transport->pogo_irq_enabled = false;
	}

	ret = gvotable_cast_long_vote(pogo_transport->charger_mode_votable,
				      POGO_VOTER, GBMS_POGO_VOUT, 1);
	if (ret)
		logbuffer_log(pogo_transport->log, "%s: Failed to vote VOUT, ret %d", __func__,
			      ret);
}

/*
 * Called when device attribute "hall1_s" is written to non-zero
 *  - If accessory_detection_enabled == ENABLED, it won't involve the State transition.
 *    Enable the Accessory Detection IRQ and the regulator for the later detection process.
 *  - If accessory_detection_enabled == HALL_ONLY, transition to related ACC states
 *  - Triggered from event: EVENT_HES_H1S_CHANGED
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_hes_acc_detected(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;
	int ret;

	if (pogo_transport->accessory_detection_enabled == ENABLED) {
		switch (pogo_transport->state) {
		case STANDBY:
		case DEVICE_HUB:
		case DEVICE_DIRECT:
		case AUDIO_DIRECT:
		case AUDIO_HUB:
		case HOST_DIRECT:
			/*
			 * Disable OVP to prevent the voltage going through POGO_VIN. OVP will be
			 * re-enabled once we vote GBMS_POGO_VIN and GBMS gets the votable result.
			 */
			if (pogo_transport->pogo_ovp_en_gpio >= 0)
				gpio_set_value_cansleep(pogo_transport->pogo_ovp_en_gpio,
							!pogo_transport->pogo_ovp_en_active_state);

			if (!pogo_transport->acc_irq_enabled) {
				enable_irq(pogo_transport->pogo_acc_irq);
				pogo_transport->acc_irq_enabled = true;
			}

			ret = pogo_transport_acc_regulator(pogo_transport, true);
			if (ret)
				logbuffer_log(pogo_transport->log, "%s: Failed to enable acc_detect %d",
					      __func__, ret);
			break;
		default:
			break;
		}
	} else if (pogo_transport->accessory_detection_enabled == HALL_ONLY) {
		switch (pogo_transport->state) {
		case STANDBY:
			pogo_transport_skip_acc_detection(pogo_transport);
			if (!pogo_transport->mfg_acc_test)
				switch_to_pogo_locked(pogo_transport);
			pogo_transport_set_state(pogo_transport, ACC_DIRECT, 0);
			break;
		case DEVICE_DIRECT:
		case AUDIO_DIRECT:
			pogo_transport_skip_acc_detection(pogo_transport);
			switch_to_hub_locked(pogo_transport);
			/*
			 * switch_to_hub_locked cleared data_active. Since there is still a USB-C
			 * accessory attached, set data_active.
			 */
			chip->data_active = true;
			pogo_transport_set_state(pogo_transport, ACC_DEVICE_HUB, 0);
			break;
		case DEVICE_HUB:
			pogo_transport_skip_acc_detection(pogo_transport);
			pogo_transport_set_state(pogo_transport, ACC_DEVICE_HUB, 0);
			break;
		case AUDIO_HUB:
			pogo_transport_skip_acc_detection(pogo_transport);
			pogo_transport_set_state(pogo_transport, ACC_AUDIO_HUB, 0);
			break;
		case HOST_DIRECT:
			pogo_transport_skip_acc_detection(pogo_transport);
			if (pogo_transport->force_pogo) {
				switch_to_pogo_locked(pogo_transport);
				/*
				 * Set data_active so that once USB-C cable is detached later,
				 * Type-C stack is able to call back for the data changed event
				 */
				chip->data_active = true;
				pogo_transport_set_state(pogo_transport, ACC_DIRECT_HOST_OFFLINE,
							 0);
			} else {
				pogo_transport_set_state(pogo_transport, HOST_DIRECT_ACC_OFFLINE,
							 0);
			}
			break;
		default:
			break;
		}
	}
}

/*
 * Called when device attribute "hall1_s" is written to 0
 * - Triggered from event: EVENT_HES_H1S_CHANGED
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_hes_acc_detached(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;

	switch (pogo_transport->state) {
	case STANDBY_ACC_DEBOUNCED:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		break;
	case DEVICE_DIRECT_ACC_DEBOUNCED:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, DEVICE_DIRECT, 0);
		break;
	case DEVICE_HUB_ACC_DEBOUNCED:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, DEVICE_HUB, 0);
		break;
	case AUDIO_DIRECT_ACC_DEBOUNCED:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, AUDIO_DIRECT, 0);
		break;
	case AUDIO_HUB_ACC_DEBOUNCED:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, AUDIO_HUB, 0);
		break;
	case HOST_DIRECT_ACC_DEBOUNCED:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, HOST_DIRECT, 0);
		break;
	case ACC_DIRECT:
	case ACC_HUB:
		pogo_transport_reset_acc_detection(pogo_transport);
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		break;
	case ACC_DEVICE_HUB:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, DEVICE_HUB, 0);
		break;
	case ACC_AUDIO_HUB:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, AUDIO_HUB, 0);
		break;
	case HOST_DIRECT_ACC_OFFLINE:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, HOST_DIRECT, 0);
		break;
	case ACC_DIRECT_HOST_OFFLINE:
	case ACC_HUB_HOST_OFFLINE:
		pogo_transport_reset_acc_detection(pogo_transport);
		/* Clear data_active so that Type-C stack is able to enable the USB data later */
		chip->data_active = false;
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, HOST_DIRECT, 0);
		break;
	default:
		break;
	}
}

/*
 * Called when Accessory Detection IRQ is active
 *  - Triggered from event: EVENT_ACC_GPIO_ACTIVE
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_acc_debouncing(struct pogo_transport *pogo_transport)
{
	switch (pogo_transport->state) {
	case STANDBY:
	case STANDBY_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, STANDBY_ACC_DEBOUNCED,
					 pogo_transport->pogo_acc_gpio_debounce_ms);
		break;
	case DEVICE_DIRECT:
	case DEVICE_DIRECT_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, DEVICE_DIRECT_ACC_DEBOUNCED,
					 pogo_transport->pogo_acc_gpio_debounce_ms);
		break;
	case DEVICE_HUB:
	case DEVICE_HUB_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, DEVICE_HUB_ACC_DEBOUNCED,
					 pogo_transport->pogo_acc_gpio_debounce_ms);
		break;
	case AUDIO_DIRECT:
	case AUDIO_DIRECT_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, AUDIO_DIRECT_ACC_DEBOUNCED,
					 pogo_transport->pogo_acc_gpio_debounce_ms);
		break;
	case AUDIO_HUB:
	case AUDIO_HUB_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, AUDIO_HUB_ACC_DEBOUNCED,
					 pogo_transport->pogo_acc_gpio_debounce_ms);
		break;
	case HOST_DIRECT:
	case HOST_DIRECT_ACC_DEBOUNCED:
		pogo_transport_set_state(pogo_transport, HOST_DIRECT_ACC_DEBOUNCED,
					 pogo_transport->pogo_acc_gpio_debounce_ms);
		break;
	default:
		break;
	}
}

/*
 * Called when POGO Voltage Detection IRQ is active while Accessory Detection regulator is enabled.
 *  - Triggered from event: EVENT_ACC_CONNECTED
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_acc_connected(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;
	int ret;

	/*
	 * FIXME: is it possible that when acc regulator is enabled and pogo irq become active
	 * because 12V input through pogo pin? e.g. keep magnet closed to the device and then
	 * docking on korlan?
	 */

	switch (pogo_transport->state) {
	case STANDBY_ACC_DEBOUNCED:
		ret = pogo_transport_acc_regulator(pogo_transport, false);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to disable acc_detect %d",
				      __func__, ret);

		if (!pogo_transport->mfg_acc_test)
			switch_to_pogo_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, ACC_DIRECT, 0);
		break;
	case DEVICE_DIRECT_ACC_DEBOUNCED:
	case AUDIO_DIRECT_ACC_DEBOUNCED:
		ret = pogo_transport_acc_regulator(pogo_transport, false);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to disable acc_detect %d",
				      __func__, ret);

		switch_to_hub_locked(pogo_transport);
		/*
		 * switch_to_hub_locked cleared data_active. Since there is still a USB-C accessory
		 * attached, set data_active.
		 */
		chip->data_active = true;
		pogo_transport_set_state(pogo_transport, ACC_DEVICE_HUB, 0);
		break;
	case DEVICE_HUB_ACC_DEBOUNCED:
		ret = pogo_transport_acc_regulator(pogo_transport, false);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to disable acc_detect %d",
				      __func__, ret);

		pogo_transport_set_state(pogo_transport, ACC_DEVICE_HUB, 0);
		break;
	case AUDIO_HUB_ACC_DEBOUNCED:
		ret = pogo_transport_acc_regulator(pogo_transport, false);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to disable acc_detect %d",
				      __func__, ret);

		pogo_transport_set_state(pogo_transport, ACC_AUDIO_HUB, 0);
		break;
	case HOST_DIRECT_ACC_DEBOUNCED:
		ret = pogo_transport_acc_regulator(pogo_transport, false);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to disable acc_detect %d",
				      __func__, ret);

		if (pogo_transport->force_pogo) {
			switch_to_pogo_locked(pogo_transport);
			/*
			 * Set data_active so that once USB-C cable is detached later, Type-C stack
			 * is able to call back for the data changed event
			 */
			chip->data_active = true;
			pogo_transport_set_state(pogo_transport, ACC_DIRECT_HOST_OFFLINE, 0);
		} else {
			pogo_transport_set_state(pogo_transport, HOST_DIRECT_ACC_OFFLINE, 0);
		}
		break;
	default:
		break;
	}
}

/*
 * Called when a USB device with AUDIO Class is enumerated.
 *  - Triggered from event: EVENT_AUDIO_DEV_ATTACHED
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_audio_dev_attached(struct pogo_transport *pogo_transport)
{
	switch (pogo_transport->state) {
	case DOCK_DEVICE_HUB:
		pogo_transport_set_state(pogo_transport, DOCK_AUDIO_HUB, 0);
		break;
	case DEVICE_DIRECT:
		pogo_transport_set_state(pogo_transport, AUDIO_DIRECT, 0);
		break;
	case ACC_DEVICE_HUB:
		pogo_transport_set_state(pogo_transport, ACC_AUDIO_HUB, 0);
		break;
	case LC_DEVICE_DIRECT:
		pogo_transport_set_state(pogo_transport, LC_AUDIO_DIRECT, 0);
		break;
	default:
		break;
	}
}

/*
 * Called when the detected orientation on USB-C port is changed.
 *  - Triggered from event: EVENT_USBC_ORIENTATION
 *
 * This function is guarded by (max77759_plat)->data_path_lock
 */
static void pogo_transport_usbc_orientation_changed(struct pogo_transport *pogo_transport)
{
	/*
	 * TODO: It is possible that USB-C is toggling between CC2 and Open. We may need to wait for
	 * the orientation being settled and then update the ssphy.
	 */
	switch (pogo_transport->state) {
	/* usbc being connected while hub is enabled */
	case DOCK_HUB:
	case ACC_HUB:
	/* usbc being disconnected while hub is enabled */
	case DOCK_DEVICE_HUB:
	case DOCK_AUDIO_HUB:
	case DOCK_HUB_HOST_OFFLINE:
	case ACC_DEVICE_HUB:
	case ACC_AUDIO_HUB:
		pogo_transport_update_polarity(pogo_transport, (int)pogo_transport->polarity, true);
		ssphy_restart_control(pogo_transport, true);
		break;
	default:
		break;
	}
}

static void pogo_transport_lc_clear(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;

	switch (pogo_transport->state) {
	case LC:
		pogo_transport_skip_acc_detection(pogo_transport);
		if (!pogo_transport->mfg_acc_test)
			switch_to_pogo_locked(pogo_transport);
		pogo_transport_set_state(pogo_transport, ACC_DIRECT, 0);
		break;
	case LC_DEVICE_DIRECT:
		pogo_transport_skip_acc_detection(pogo_transport);
		if (!pogo_transport->mfg_acc_test) {
			switch_to_hub_locked(pogo_transport);
			chip->data_active = true;
		}
		pogo_transport_set_state(pogo_transport, ACC_DEVICE_HUB, 0);
		break;
	case LC_AUDIO_DIRECT:
		pogo_transport_skip_acc_detection(pogo_transport);
		if (!pogo_transport->mfg_acc_test) {
			switch_to_hub_locked(pogo_transport);
			chip->data_active = true;
		}
		pogo_transport_set_state(pogo_transport, ACC_AUDIO_HUB, 0);
		break;
	case LC_ALL_OFFLINE:
		pogo_transport_skip_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, ACC_DIRECT_HOST_OFFLINE, 0);
		break;
	case LC_HOST_DIRECT:
		pogo_transport_skip_acc_detection(pogo_transport);
		if (!pogo_transport->mfg_acc_test) {
			switch_to_pogo_locked(pogo_transport);
			chip->data_active = true;
		}
		pogo_transport_set_state(pogo_transport, HOST_DIRECT_ACC_OFFLINE, 0);
		break;
	default:
		break;
	}
}

static void pogo_transport_lc(struct pogo_transport *pogo_transport)
{
	switch (pogo_transport->state) {
	case ACC_DIRECT:
	case ACC_HUB:
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, LC, 0);
		break;
	case ACC_DEVICE_HUB:
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, LC_DEVICE_DIRECT, 0);
		break;
	case ACC_AUDIO_HUB:
		switch_to_usbc_locked(pogo_transport);
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, LC_AUDIO_DIRECT, 0);
		break;
	case ACC_DIRECT_HOST_OFFLINE:
	case ACC_HUB_HOST_OFFLINE:
		switch_to_pogo_locked(pogo_transport);
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, LC_ALL_OFFLINE, 0);
		break;
	case HOST_DIRECT_ACC_OFFLINE:
		pogo_transport_reset_acc_detection(pogo_transport);
		pogo_transport_set_state(pogo_transport, LC_HOST_DIRECT, 0);
		break;
	default:
		break;
	}
}

#define ACC_CHARGER_PSY_RETRY_COUNT 5
#define ACC_CHARGER_PSY_RETRY_TIMEOUT_MS 100
static int pogo_transport_acc_charger_status(struct pogo_transport *pogo_transport,
					     union power_supply_propval *acc_charger_status,
					     union power_supply_propval *acc_charger_capacity)
{
	int ret, count;
	bool retry;

	if (pogo_transport->pogo_acc_gpio <= 0 || !pogo_transport->acc_charger_psy_name)
		return -EINVAL;

	for (count = 0; count < ACC_CHARGER_PSY_RETRY_COUNT; count++) {
		retry = false;

		pogo_transport->acc_charger_psy = power_supply_get_by_name(
				pogo_transport->acc_charger_psy_name);
		if (IS_ERR_OR_NULL(pogo_transport->acc_charger_psy)) {
			logbuffer_logk(pogo_transport->log, LOGLEVEL_ERR,
				       "acc_charger psy delayed get failed");
			return -ENODEV;
		}

		ret = power_supply_get_property(pogo_transport->acc_charger_psy,
						POWER_SUPPLY_PROP_STATUS, acc_charger_status);
		if (ret) {
			logbuffer_log(pogo_transport->log,
				      "Failed to read acc_charger status (%d), count %d", ret,
				      count);
			acc_charger_status->intval = POWER_SUPPLY_STATUS_UNKNOWN;
			retry = true;
		}

		ret = power_supply_get_property(pogo_transport->acc_charger_psy,
						POWER_SUPPLY_PROP_CAPACITY, acc_charger_capacity);
		if (ret) {
			logbuffer_log(pogo_transport->log,
				      "Failed to read acc_charger capacity (%d), count %d", ret,
				      count);
			acc_charger_capacity->intval = 0;
			retry = true;
		}

		if (!retry)
			break;

		mdelay(ACC_CHARGER_PSY_RETRY_TIMEOUT_MS);
	}

	return 0;
}

#define ACC_CHARGER_SOC_FULL 100
#define ACC_CHARGER_NOT_PRESENT 0
static bool lc_acc_charging_ended(struct pogo_transport *pogo_transport)
{
	union power_supply_propval acc_charger_status = {.intval = POWER_SUPPLY_STATUS_UNKNOWN};
	union power_supply_propval acc_charger_capacity = {0};
	u64 now, elapsed_sec;
	bool charging_ended;
	int ret;

	ret = pogo_transport_acc_charger_status(pogo_transport, &acc_charger_status,
						&acc_charger_capacity);
	logbuffer_log(pogo_transport->log, "ret:%d charger_status:%d cap:%d", ret,
		      acc_charger_status.intval, acc_charger_capacity.intval);

	if (ret < 0) {
		/*
		 * It is expected that pogo Vout will be turned off. So it is safe to reset the
		 * begin time here.
		 */
		pogo_transport->acc_charging_full_begin_ns = 0;
		return true;
	}

	if (acc_charger_status.intval == POWER_SUPPLY_STATUS_CHARGING &&
	    acc_charger_capacity.intval == ACC_CHARGER_SOC_FULL) {
		now = ktime_get_boottime_ns();
		/* other status -> "charging + soc full" */
		if (!pogo_transport->acc_charging_full_begin_ns) {
			pogo_transport->acc_charging_full_begin_ns = now;
			return false;
		/* continuous "charging + soc full" */
		} else {
			elapsed_sec = div_u64(now - pogo_transport->acc_charging_full_begin_ns,
					      (u32)NSEC_PER_SEC);
			/* elapsed time >= timeout */
			if (elapsed_sec >= pogo_transport->acc_charging_timeout_sec) {
				/*
				 * It is expected that pogo Vout will be turned off. So it is safe
				 * to reset the begin time here.
				 */
				pogo_transport->acc_charging_full_begin_ns = 0;
				return true;
			} else {
				return false;
			}
		}
	}

	if (acc_charger_status.intval == POWER_SUPPLY_STATUS_DISCHARGING &&
	    acc_charger_capacity.intval == ACC_CHARGER_NOT_PRESENT) {
		now = ktime_get_boottime_ns();
		/* other status -> "discharging + 0" */
		if (!pogo_transport->acc_discharging_begin_ns) {
			pogo_transport->acc_discharging_begin_ns = now;
			return false;
		/* continuous "discharging + 0" */
		} else {
			elapsed_sec = div_u64(now - pogo_transport->acc_discharging_begin_ns,
					      (u32)NSEC_PER_SEC);
			/* elapsed time >= timeout */
			if (elapsed_sec >= pogo_transport->acc_charging_timeout_sec) {
				/*
				 * It is expected that pogo Vout will be turned off. So it is safe
				 * to reset the begin time here.
				 */
				pogo_transport->acc_discharging_begin_ns = 0;
				return true;
			} else {
				return false;
			}
		}
	}


	pogo_transport->acc_discharging_begin_ns = 0;
	pogo_transport->acc_charging_full_begin_ns = 0;

	charging_ended = acc_charger_status.intval == POWER_SUPPLY_STATUS_DISCHARGING &&
			 acc_charger_capacity.intval == ACC_CHARGER_SOC_FULL;
	charging_ended |= acc_charger_status.intval == POWER_SUPPLY_STATUS_FULL;

	return charging_ended;
}

static void pogo_transport_lc_stage_transition(struct pogo_transport *pogo_transport)
{
	struct max77759_plat *chip = pogo_transport->chip;
	bool acc_charging_ended;

	logbuffer_log(pogo_transport->log, "stage:%u lc:%u wait_for_suspend:%u",
		      pogo_transport->lc_stage, pogo_transport->lc,
		      pogo_transport->wait_for_suspend);

	if (!pogo_transport->lc)
		return;

	mutex_lock(&chip->data_path_lock);

	switch (pogo_transport->lc_stage) {
	case STAGE_WAIT_FOR_SUSPEND:
		if (pogo_transport->wait_for_suspend)
			break;

		if (!pogo_transport->acc_charger_psy_name) {
			pogo_transport_lc(pogo_transport);
			pogo_transport->lc_stage = STAGE_VOUT_DISABLED;
			break;
		}

		acc_charging_ended = lc_acc_charging_ended(pogo_transport);
		if (acc_charging_ended) {
			pogo_transport_lc(pogo_transport);
			pogo_transport->lc_stage = STAGE_VOUT_DISABLED;
			alarm_start_relative(&pogo_transport->lc_check_alarm,
					     ms_to_ktime(pogo_transport->lc_disable_ms));
		} else {
			pogo_transport->lc_stage = STAGE_VOUT_ENABLED;
			alarm_start_relative(&pogo_transport->lc_check_alarm,
					     ms_to_ktime(pogo_transport->lc_enable_ms));
		}
		break;
	case STAGE_VOUT_DISABLED:
		pogo_transport_lc_clear(pogo_transport);
		pogo_transport->lc_stage = STAGE_VOUT_ENABLED;
		alarm_start_relative(&pogo_transport->lc_check_alarm,
				     ms_to_ktime(pogo_transport->lc_bootup_ms));
		break;
	case STAGE_VOUT_ENABLED:
		acc_charging_ended = lc_acc_charging_ended(pogo_transport);
		if (acc_charging_ended) {
			pogo_transport_lc(pogo_transport);
			pogo_transport->lc_stage = STAGE_VOUT_DISABLED;
			alarm_start_relative(&pogo_transport->lc_check_alarm,
					     ms_to_ktime(pogo_transport->lc_disable_ms));
		} else {
			alarm_start_relative(&pogo_transport->lc_check_alarm,
					     ms_to_ktime(pogo_transport->lc_enable_ms));
		}
		break;
	default:
		break;
	}

	mutex_unlock(&chip->data_path_lock);
}

static void pogo_transport_event_handler(struct kthread_work *work)
{
	struct pogo_transport *pogo_transport = container_of(work, struct pogo_transport,
							     event_work);
	struct max77759_plat *chip = pogo_transport->chip;
	unsigned long events;

	mutex_lock(&chip->data_path_lock);
	spin_lock(&pogo_transport->pogo_event_lock);
	while (pogo_transport->event_map) {
		events = pogo_transport->event_map;
		pogo_transport->event_map = 0;

		spin_unlock(&pogo_transport->pogo_event_lock);

		if (events & EVENT_POGO_IRQ) {
			int pogo_gpio = gpio_get_value(pogo_transport->pogo_gpio);

			logbuffer_log(pogo_transport->log, "EV:POGO_IRQ %s", pogo_gpio ?
				      "STANDBY" : "ACTIVE");
			if (pogo_gpio)
				pogo_transport_pogo_irq_standby(pogo_transport);
			else
				pogo_transport_pogo_irq_active(pogo_transport);
		}
		if (events & EVENT_USBC_ORIENTATION) {
			logbuffer_log(pogo_transport->log, "EV:ORIENTATION %u",
				      pogo_transport->polarity);
			pogo_transport_usbc_orientation_changed(pogo_transport);
		}
		if (events & EVENT_USBC_DATA_CHANGE) {
			logbuffer_log(pogo_transport->log, "EV:DATA_CHANGE usbc-role %u usbc-active %u",
				      pogo_transport->usbc_data_role,
				      pogo_transport->usbc_data_active);
			if (pogo_transport->usbc_data_role == TYPEC_HOST) {
				if (pogo_transport->usbc_data_active)
					pogo_transport_usbc_host_on(pogo_transport);
				else
					pogo_transport_usbc_host_off(pogo_transport);
			} else {
				if (pogo_transport->usbc_data_active)
					pogo_transport_usbc_device_on(pogo_transport);
				else
					pogo_transport_usbc_device_off(pogo_transport);
			}
		}
		if (events & EVENT_ENABLE_USB_DATA) {
			logbuffer_log(pogo_transport->log, "EV:ENABLE_USB");
			pogo_transport_enable_usb_data(pogo_transport);
		}
		if (events & EVENT_FORCE_POGO) {
			logbuffer_log(pogo_transport->log, "EV:FORCE_POGO");
			pogo_transport_force_pogo(pogo_transport);
		}
		if (events & EVENT_HES_H1S_CHANGED) {
			logbuffer_log(pogo_transport->log, "EV:H1S state %d",
				      pogo_transport->hall1_s_state);
			if (pogo_transport->hall1_s_state)
				pogo_transport_hes_acc_detected(pogo_transport);
			else
				pogo_transport_hes_acc_detached(pogo_transport);
		}
		if (events & EVENT_ACC_GPIO_ACTIVE) {
			logbuffer_log(pogo_transport->log, "EV:ACC_GPIO_ACTIVE, H1S %d",
				      pogo_transport->hall1_s_state);
			/* b/288341638 step to debouncing only if H1S stays active */
			if (pogo_transport->hall1_s_state)
				pogo_transport_acc_debouncing(pogo_transport);
			else
				pogo_transport_hes_acc_detached(pogo_transport);
		}
		if (events & EVENT_ACC_CONNECTED) {
			logbuffer_log(pogo_transport->log, "EV:ACC_CONNECTED");
			pogo_transport_acc_connected(pogo_transport);
		}
		if (events & EVENT_AUDIO_DEV_ATTACHED) {
			logbuffer_log(pogo_transport->log, "EV:AUDIO_ATTACHED");
			pogo_transport_audio_dev_attached(pogo_transport);
		}
		if (events & EVENT_USB_SUSPEND) {
			logbuffer_log(pogo_transport->log, "EV:USB_SUSPEND stage %u",
				      pogo_transport->lc_stage);
			if (pogo_transport->lc &&
			    pogo_transport->lc_stage == STAGE_WAIT_FOR_SUSPEND)
				alarm_start_relative(&pogo_transport->lc_check_alarm, 0);
		}
		if (events & EVENT_LC_STATUS_CHANGED) {
			logbuffer_log(pogo_transport->log, "EV:LC %u", pogo_transport->lc);
			if (pogo_transport->lc) {
				if (bus_suspend(pogo_transport))
					pogo_transport->wait_for_suspend = false;
				pogo_transport->lc_stage = STAGE_WAIT_FOR_SUSPEND;
				alarm_start_relative(&pogo_transport->lc_check_alarm,
						ms_to_ktime(pogo_transport->lc_delay_check_ms));
			} else {
				if (pogo_transport->lc_stage == STAGE_VOUT_DISABLED)
					pogo_transport_lc_clear(pogo_transport);
				pogo_transport->lc_stage = STAGE_UNKNOWN;
				pogo_transport->wait_for_suspend = true;
			}
		}
		spin_lock(&pogo_transport->pogo_event_lock);
	}
	spin_unlock(&pogo_transport->pogo_event_lock);
	mutex_unlock(&chip->data_path_lock);
}

static void pogo_transport_queue_event(struct pogo_transport *pogo_transport, unsigned long event)
{
	unsigned long flags;

	pm_wakeup_event(pogo_transport->dev, POGO_TIMEOUT_MS);
	/*
	 * Print the event number derived from the bit position; e.g. BIT(0) -> 0
	 * Note that ffs() only return the least significant set bit.
	 */
	logbuffer_log(pogo_transport->log, "QUEUE EVENT %d", ffs((int)event) - 1);

	spin_lock_irqsave(&pogo_transport->pogo_event_lock, flags);
	pogo_transport->event_map |= event;
	spin_unlock_irqrestore(&pogo_transport->pogo_event_lock, flags);

	kthread_queue_work(pogo_transport->wq, &pogo_transport->event_work);
}

static void lc_check_alarm_work_item(struct kthread_work *work)
{
	struct pogo_transport *pogo_transport = container_of(work, struct pogo_transport, lc_work);

	pogo_transport_lc_stage_transition(pogo_transport);
}

static enum alarmtimer_restart lc_check_alarm_handler(struct alarm *alarm, ktime_t time)
{
	struct pogo_transport *pogo_transport = container_of(alarm, struct pogo_transport,
							     lc_check_alarm);

	pm_wakeup_event(pogo_transport->dev, LC_WAKEUP_TIMEOUT_MS);
	kthread_queue_work(pogo_transport->wq, &pogo_transport->lc_work);

	return ALARMTIMER_NORESTART;
}

/*-------------------------------------------------------------------------*/
/* Events triggering                                                       */
/*-------------------------------------------------------------------------*/

static irqreturn_t pogo_acc_irq(int irq, void *dev_id)
{
	struct pogo_transport *pogo_transport = dev_id;

	/*
	 * Cache the acc gpio result as it might change after the IRQ is disabled and we need the
	 * latest acc gpio status before the disabling of the IRQ.
	 */
	pogo_transport->acc_gpio_result_cache = gpio_get_value(pogo_transport->pogo_acc_gpio);

	logbuffer_log(pogo_transport->log, "Pogo acc threaded irq running, acc_detect %u",
		      pogo_transport->acc_gpio_result_cache);

	if (pogo_transport->state_machine_enabled) {
		if (pogo_transport->acc_gpio_result_cache)
			pogo_transport_queue_event(pogo_transport, EVENT_ACC_GPIO_ACTIVE);
		return IRQ_HANDLED;
	}

	if (pogo_transport->acc_gpio_result_cache)
		pogo_transport_event(pogo_transport, EVENT_POGO_ACC_DEBOUNCED,
				     pogo_transport->pogo_acc_gpio_debounce_ms);
	else
		kthread_cancel_delayed_work_sync(&pogo_transport->pogo_accessory_debounce_work);

	return IRQ_HANDLED;
}

static irqreturn_t pogo_acc_isr(int irq, void *dev_id)
{
	struct pogo_transport *pogo_transport = dev_id;

	logbuffer_log(pogo_transport->log, "POGO ACC IRQ triggered");
	pm_wakeup_event(pogo_transport->dev, POGO_TIMEOUT_MS);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t pogo_irq(int irq, void *dev_id)
{
	struct pogo_transport *pogo_transport = dev_id;
	int pogo_gpio = gpio_get_value(pogo_transport->pogo_gpio);

	logbuffer_log(pogo_transport->log, "Pogo threaded irq running, pogo_gpio %u", pogo_gpio);

	if (pogo_transport->acc_detect_ldo &&
	    regulator_is_enabled(pogo_transport->acc_detect_ldo) > 0) {
		/*
		 * b/288341638 If the cached acc gpio is not active, it means that the IV detection
		 * has failed when the acc detection regulator is enabled. If the state machine
		 * stays at *_ACC_DEBOUNCED states, i.e., the H1S state is still active, docking
		 * will fail because it "looks like" a normal acc connection. Disable the acc
		 * regulator in this situation and continue to the docking detection procedure.
		 */
		if (!pogo_transport->acc_gpio_result_cache) {
			if (pogo_transport->acc_irq_enabled) {
				disable_irq_nosync(pogo_transport->pogo_acc_irq);
				pogo_transport->acc_irq_enabled = false;
				logbuffer_log(pogo_transport->log, "acc_irq disabled");
			}
			pogo_transport_acc_regulator(pogo_transport, false);
			logbuffer_log(pogo_transport->log,
				      "HES mistriggered, begin docking detection");
			goto dock_detection;
		}

		if (pogo_transport->pogo_irq_enabled) {
			/* disable the irq to prevent the interrupt storm after pogo 5v out */
			disable_irq_nosync(pogo_transport->pogo_irq);
			pogo_transport->pogo_irq_enabled = false;
			if (pogo_transport->state_machine_enabled)
				pogo_transport_queue_event(pogo_transport, EVENT_ACC_CONNECTED);
			else
				pogo_transport_event(pogo_transport, EVENT_POGO_ACC_CONNECTED, 0);
		}
		return IRQ_HANDLED;
	}

dock_detection:
	if (pogo_transport->pogo_ovp_en_gpio >= 0) {
		int ret;

		/*
		 * Vote GBMS_POGO_VIN to notify BMS that there is input voltage on pogo power and
		 * it is over the threshold if pogo_gpio (ACTIVE_LOW) is in active state (0)
		 */
		ret = gvotable_cast_long_vote(pogo_transport->charger_mode_votable, POGO_VOTER,
					      GBMS_POGO_VIN, !pogo_gpio);
		if (ret)
			logbuffer_log(pogo_transport->log, "%s: Failed to vote VIN, ret %d",
				      __func__, ret);
	}

	if (pogo_transport->state_machine_enabled)
		pogo_transport_queue_event(pogo_transport, EVENT_POGO_IRQ);
	else
		pogo_transport_event(pogo_transport, EVENT_DOCKING, !pogo_gpio ?
				     POGO_PSY_DEBOUNCE_MS : 0);
	return IRQ_HANDLED;
}

static irqreturn_t pogo_isr(int irq, void *dev_id)
{
	struct pogo_transport *pogo_transport = dev_id;

	logbuffer_log(pogo_transport->log, "POGO IRQ triggered");
	pm_wakeup_event(pogo_transport->dev, POGO_TIMEOUT_MS);

	return IRQ_WAKE_THREAD;
}

static void data_active_changed(void *data, enum typec_data_role role, bool active)
{
	struct pogo_transport *pogo_transport = data;

	logbuffer_log(pogo_transport->log, "%s: role %u active %d", __func__, role, active);

	pogo_transport->usbc_data_role = role;
	pogo_transport->usbc_data_active = active;

	if (pogo_transport->state_machine_enabled)
		pogo_transport_queue_event(pogo_transport, EVENT_USBC_DATA_CHANGE);
	else
		pogo_transport_event(pogo_transport, EVENT_DATA_ACTIVE_CHANGED, 0);
}

static void orientation_changed(void *data)
{
	struct pogo_transport *pogo_transport = data;
	struct max77759_plat *chip = pogo_transport->chip;

	if (pogo_transport->polarity != chip->polarity) {
		pogo_transport->polarity = chip->polarity;
		if (pogo_transport->state_machine_enabled)
			pogo_transport_queue_event(pogo_transport, EVENT_USBC_ORIENTATION);
		else
			pogo_transport_event(pogo_transport, EVENT_ORIENTATION_CHANGED, 0);
	}
}

static void usb_bus_suspend_resume(void *data, bool main_hcd, bool suspend)
{
	struct pogo_transport *pogo_transport = data;

	if (main_hcd)
		pogo_transport->main_hcd_suspend = suspend;
	else
		pogo_transport->shared_hcd_suspend = suspend;

	/* TODO: mutex lock to protect the read/set of lc and wait_for_suspend */
	if (!pogo_transport->lc)
		return;

	/* lc and still wait for suspend */
	if (bus_suspend(pogo_transport) && pogo_transport->wait_for_suspend) {
		pogo_transport->wait_for_suspend = false;
		pogo_transport_queue_event(pogo_transport, EVENT_USB_SUSPEND);
		logbuffer_log(pogo_transport->log, "first bus suspend after lc");
	}
}

/* Called when a USB hub/device (exclude root hub) is enumerated */
static void pogo_transport_udev_add(struct pogo_transport *pogo_transport, struct usb_device *udev)
{
	struct usb_interface_descriptor *desc;
	struct usb_host_config *config;
	bool audio_dock = false;
	bool audio_dev = false;
	int i;

	/* Don't proceed to the event handling if the udev is an Audio Dock. Skip the check. */
	if (pogo_transport_match_udev(audio_dock_ids, le16_to_cpu(udev->descriptor.idVendor),
				      le16_to_cpu(udev->descriptor.idProduct))) {
		audio_dock = true;
		goto skip_audio_check;
	}

	if (udev->speed >= USB_SPEED_SUPER)
		pogo_transport->ss_udev_attached = true;

	config = udev->config;
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		desc = &config->intf_cache[i]->altsetting->desc;
		if (desc->bInterfaceClass == USB_CLASS_AUDIO) {
			audio_dev = true;
			break;
		}
	}

skip_audio_check:
	logbuffer_log(pogo_transport->log, "udev added %04X:%04X [%s%s%s%s]",
		      le16_to_cpu(udev->descriptor.idVendor),
		      le16_to_cpu(udev->descriptor.idProduct),
		      udev->speed >= USB_SPEED_SUPER ? "Ss" : "",
		      udev->descriptor.bDeviceClass == USB_CLASS_HUB ? "Hu" : "",
		      audio_dock ? "Do" : "",
		      audio_dev ? "Au" : "");

	if (audio_dev && pogo_transport->state_machine_enabled)
		pogo_transport_queue_event(pogo_transport, EVENT_AUDIO_DEV_ATTACHED);
}

/* notifier callback from usb core */
static int pogo_transport_udev_notify(struct notifier_block *nb, unsigned long action, void *dev)
{
	struct pogo_transport *pogo_transport = container_of(nb, struct pogo_transport, udev_nb);
	struct usb_device *udev = dev;

	switch (action) {
	case USB_DEVICE_ADD:
		/* Don't care about the root hubs. */
		if (udev->bus->root_hub == udev)
			break;

		pogo_transport_udev_add(pogo_transport, udev);
		break;
	case USB_DEVICE_REMOVE:
		/* Don't care about the root hubs. */
		if (udev->bus->root_hub == udev)
			break;

		logbuffer_log(pogo_transport->log, "udev removed %04X:%04X",
			      le16_to_cpu(udev->descriptor.idVendor),
			      le16_to_cpu(udev->descriptor.idProduct));
		break;
	}

	return NOTIFY_OK;
}

static int pogo_transport_reboot_notify(struct notifier_block *nb, unsigned long action,
					void *data)
{
	struct pogo_transport *pogo_transport = container_of(nb, struct pogo_transport, reboot_nb);

	mutex_lock(&pogo_transport->reboot_lock);
	pogo_transport->rebooting = true;
	mutex_unlock(&pogo_transport->reboot_lock);

	return NOTIFY_OK;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int mock_hid_connected_set(void *data, u64 val)
{
	struct pogo_transport *pogo_transport = data;

	if (pogo_transport->state_machine_enabled) {
		logbuffer_log(pogo_transport->log, "state machine enabled; ignore mock hid");
		return 0;
	}

	pogo_transport->mock_hid_connected = !!val;

	logbuffer_log(pogo_transport->log, "%s: %u", __func__, pogo_transport->mock_hid_connected);

	if (pogo_transport->mock_hid_connected)
		pogo_transport_event(pogo_transport, EVENT_FORCE_ACC_CONNECT, 0);
	else
		pogo_transport_event(pogo_transport, EVENT_HALL_SENSOR_ACC_UNDOCKED, 0);

	return 0;
}

static int mock_hid_connected_get(void *data, u64 *val)
{
	struct pogo_transport *pogo_transport = data;

	*val = (u64)pogo_transport->mock_hid_connected;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(mock_hid_connected_fops, mock_hid_connected_get, mock_hid_connected_set,
			"%llu\n");

#define POGO_TRANSPORT_DEBUGFS_RW(_name)                                                        \
static int _name##_set(void *data, u64 val)                                                     \
{                                                                                               \
	struct pogo_transport *pogo_transport  = data;                                          \
	pogo_transport->_name = val;                                                          \
	logbuffer_log(pogo_transport->log, "%s: %llu", __func__,                                \
		      (u64)pogo_transport->_name);                                              \
	return 0;                                                                               \
}                                                                                               \
static int _name##_get(void *data, u64 *val)                                                    \
{                                                                                               \
	struct pogo_transport *pogo_transport  = data;                                          \
	*val = (u64)pogo_transport->_name;                                                      \
	return 0;                                                                               \
}                                                                                               \
DEFINE_SIMPLE_ATTRIBUTE(_name##_fops, _name##_get, _name##_set, "%llu\n")
POGO_TRANSPORT_DEBUGFS_RW(lc_delay_check_ms);
POGO_TRANSPORT_DEBUGFS_RW(lc_enable_ms);
POGO_TRANSPORT_DEBUGFS_RW(lc_disable_ms);
POGO_TRANSPORT_DEBUGFS_RW(lc_bootup_ms);
POGO_TRANSPORT_DEBUGFS_RW(acc_charging_timeout_sec);

/*-------------------------------------------------------------------------*/
/* Initialization                                                          */
/*-------------------------------------------------------------------------*/

static void pogo_transport_init_debugfs(struct pogo_transport *pogo_transport)
{
	struct dentry *dentry;

	dentry = debugfs_create_dir("pogo_transport", NULL);

	if (IS_ERR(dentry)) {
		dev_err(pogo_transport->dev, "debugfs dentry failed: %ld", PTR_ERR(dentry));
		return;
	}

	debugfs_create_file("mock_hid_connected", 0644, dentry, pogo_transport,
			    &mock_hid_connected_fops);
	debugfs_create_file("lc_delay_check_ms", 0644, dentry, pogo_transport,
			    &lc_delay_check_ms_fops);
	debugfs_create_file("lc_enable_ms", 0644, dentry, pogo_transport, &lc_enable_ms_fops);
	debugfs_create_file("lc_disable_ms", 0644, dentry, pogo_transport, &lc_disable_ms_fops);
	debugfs_create_file("lc_bootup_ms", 0644, dentry, pogo_transport, &lc_bootup_ms_fops);
	debugfs_create_file("acc_charging_timeout_sec", 0644, dentry, pogo_transport,
			    &acc_charging_timeout_sec_fops);
}
#endif /* IS_ENABLED(CONFIG_DEBUG_FS) */

static int init_regulator(struct pogo_transport *pogo_transport)
{
	if (of_property_read_bool(pogo_transport->dev->of_node, "usb-hub-supply")) {
		pogo_transport->hub_ldo = devm_regulator_get(pogo_transport->dev, "usb-hub");
		if (IS_ERR(pogo_transport->hub_ldo)) {
			dev_err(pogo_transport->dev, "Failed to get usb-hub, ret:%ld\n",
				PTR_ERR(pogo_transport->hub_ldo));
			return PTR_ERR(pogo_transport->hub_ldo);
		}
	}

	if (of_property_read_bool(pogo_transport->dev->of_node, "acc-detect-supply")) {
		pogo_transport->acc_detect_ldo = devm_regulator_get(pogo_transport->dev,
								    "acc-detect");
		if (IS_ERR(pogo_transport->acc_detect_ldo)) {
			dev_err(pogo_transport->dev, "Failed to get acc-detect, ret:%ld\n",
				PTR_ERR(pogo_transport->acc_detect_ldo));
			return PTR_ERR(pogo_transport->acc_detect_ldo);
		}
	}

	return 0;
}

static int init_pogo_irqs(struct pogo_transport *pogo_transport)
{
	int ret;

	/* initialize pogo status irq */
	pogo_transport->pogo_irq = gpio_to_irq(pogo_transport->pogo_gpio);
	if (pogo_transport->pogo_irq <= 0) {
		dev_err(pogo_transport->dev, "Pogo irq not found\n");
		return -ENODEV;
	}

	ret = devm_request_threaded_irq(pogo_transport->dev, pogo_transport->pogo_irq, pogo_isr,
					pogo_irq, (IRQF_SHARED | IRQF_ONESHOT |
						   IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING),
					dev_name(pogo_transport->dev), pogo_transport);
	if (ret < 0) {
		dev_err(pogo_transport->dev, "pogo-transport-status request irq failed ret:%d\n",
			ret);
		return ret;
	}

	pogo_transport->pogo_irq_enabled = true;

	ret = enable_irq_wake(pogo_transport->pogo_irq);
	if (ret) {
		dev_err(pogo_transport->dev, "Enable irq wake failed ret:%d\n", ret);
		goto free_status_irq;
	}

	if (!pogo_transport->pogo_acc_gpio)
		return 0;

	/* initialize pogo accessory irq */
	pogo_transport->pogo_acc_irq = gpio_to_irq(pogo_transport->pogo_acc_gpio);
	if (pogo_transport->pogo_acc_irq <= 0) {
		dev_err(pogo_transport->dev, "Pogo acc irq not found\n");
		ret = -ENODEV;
		goto disable_status_irq_wake;
	}

	ret = devm_request_threaded_irq(pogo_transport->dev, pogo_transport->pogo_acc_irq,
					pogo_acc_isr, pogo_acc_irq,
					(IRQF_SHARED | IRQF_ONESHOT |
					 IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING),
					dev_name(pogo_transport->dev), pogo_transport);
	if (ret < 0) {
		dev_err(pogo_transport->dev, "pogo-acc-detect request irq failed ret:%d\n", ret);
		goto disable_status_irq_wake;
	}

	pogo_transport->acc_irq_enabled = true;

	ret = enable_irq_wake(pogo_transport->pogo_acc_irq);
	if (ret) {
		dev_err(pogo_transport->dev, "Enable acc irq wake failed ret:%d\n", ret);
		goto free_acc_irq;
	}

	return 0;

free_acc_irq:
	devm_free_irq(pogo_transport->dev, pogo_transport->pogo_acc_irq, pogo_transport);
disable_status_irq_wake:
	disable_irq_wake(pogo_transport->pogo_irq);
free_status_irq:
	devm_free_irq(pogo_transport->dev, pogo_transport->pogo_irq, pogo_transport);

	return ret;
}

static int init_acc_gpio(struct pogo_transport *pogo_transport)
{
	int ret;

	pogo_transport->pogo_acc_gpio = of_get_named_gpio(pogo_transport->dev->of_node,
							  "pogo-acc-detect", 0);
	if (pogo_transport->pogo_acc_gpio < 0) {
		dev_err(pogo_transport->dev, "pogo acc detect gpio not found ret:%d\n",
			pogo_transport->pogo_acc_gpio);
		return pogo_transport->pogo_acc_gpio;
	}

	ret = devm_gpio_request(pogo_transport->dev, pogo_transport->pogo_acc_gpio,
				"pogo-acc-detect");
	if (ret) {
		dev_err(pogo_transport->dev, "failed to request pogo-acc-detect gpio, ret:%d\n",
			ret);
		return ret;
	}

	ret = gpio_direction_input(pogo_transport->pogo_acc_gpio);
	if (ret) {
		dev_err(pogo_transport->dev, "failed to set pogo-acc-detect as input, ret:%d\n",
			ret);
		return ret;
	}

	ret = gpio_set_debounce(pogo_transport->pogo_acc_gpio, POGO_ACC_GPIO_DEBOUNCE_MS * 1000);
	if (ret < 0) {
		dev_info(pogo_transport->dev, "failed to set debounce, ret:%d\n", ret);
		pogo_transport->pogo_acc_gpio_debounce_ms = POGO_ACC_GPIO_DEBOUNCE_MS;
	}

	return 0;
}

static int init_hub_gpio(struct pogo_transport *pogo_transport)
{
	pogo_transport->pogo_hub_sel_gpio = of_get_named_gpio(pogo_transport->dev->of_node,
							      "pogo-hub-sel", 0);
	if (pogo_transport->pogo_hub_sel_gpio < 0) {
		dev_err(pogo_transport->dev, "Pogo hub sel gpio not found ret:%d\n",
			pogo_transport->pogo_hub_sel_gpio);
		return pogo_transport->pogo_hub_sel_gpio;
	}

	pogo_transport->pogo_hub_reset_gpio = of_get_named_gpio(pogo_transport->dev->of_node,
								"pogo-hub-reset", 0);
	if (pogo_transport->pogo_hub_reset_gpio < 0) {
		dev_err(pogo_transport->dev, "Pogo hub reset gpio not found ret:%d\n",
			pogo_transport->pogo_hub_reset_gpio);
		return pogo_transport->pogo_hub_reset_gpio;
	}

	pogo_transport->hub_state = pinctrl_lookup_state(pogo_transport->pinctrl, "hub");
	if (IS_ERR(pogo_transport->hub_state)) {
		dev_err(pogo_transport->dev, "failed to find pinctrl hub ret:%ld\n",
			PTR_ERR(pogo_transport->hub_state));
		return PTR_ERR(pogo_transport->hub_state);
	}

	return 0;
}

static int init_pogo_gpio(struct pogo_transport *pogo_transport)
{
	int ret;

	/* initialize pogo status gpio */
	pogo_transport->pogo_gpio = of_get_named_gpio(pogo_transport->dev->of_node,
						      "pogo-transport-status", 0);
	if (pogo_transport->pogo_gpio < 0) {
		dev_err(pogo_transport->dev, "Pogo status gpio not found ret:%d\n",
			pogo_transport->pogo_gpio);
		return pogo_transport->pogo_gpio;
	}

	ret = devm_gpio_request(pogo_transport->dev, pogo_transport->pogo_gpio,
				"pogo-transport-status");
	if (ret) {
		dev_err(pogo_transport->dev,
			"failed to request pogo-transport-status gpio, ret:%d\n",
			ret);
		return ret;
	}

	ret = gpio_direction_input(pogo_transport->pogo_gpio);
	if (ret) {
		dev_err(pogo_transport->dev,
			"failed set pogo-transport-status as input, ret:%d\n",
			ret);
		return ret;
	}

	/* initialize data mux gpio */
	pogo_transport->pogo_data_mux_gpio = of_get_named_gpio(pogo_transport->dev->of_node,
							       "pogo-transport-sel", 0);
	if (pogo_transport->pogo_data_mux_gpio < 0) {
		dev_err(pogo_transport->dev, "Pogo sel gpio not found ret:%d\n",
			pogo_transport->pogo_data_mux_gpio);
		return pogo_transport->pogo_data_mux_gpio;
	}

	ret = devm_gpio_request(pogo_transport->dev, pogo_transport->pogo_data_mux_gpio,
				"pogo-transport-sel");
	if (ret) {
		dev_err(pogo_transport->dev, "failed to request pogo-transport-sel gpio, ret:%d\n",
			ret);
		return ret;
	}

	ret = gpio_direction_output(pogo_transport->pogo_data_mux_gpio, 0);
	if (ret) {
		dev_err(pogo_transport->dev, "failed set pogo-transport-sel as output, ret:%d\n",
			ret);
		return ret;
	}

	/* pinctrl for usb-c path*/
	pogo_transport->pinctrl = devm_pinctrl_get_select(pogo_transport->dev, "suspend-to-usb");
	if (IS_ERR(pogo_transport->pinctrl)) {
		dev_err(pogo_transport->dev, "failed to allocate pinctrl ret:%ld\n",
			PTR_ERR(pogo_transport->pinctrl));
		return PTR_ERR(pogo_transport->pinctrl);
	}

	pogo_transport->susp_usb_state = pinctrl_lookup_state(pogo_transport->pinctrl,
							      "suspend-to-usb");
	if (IS_ERR(pogo_transport->susp_usb_state)) {
		dev_err(pogo_transport->dev, "failed to find pinctrl suspend-to-usb ret:%ld\n",
			PTR_ERR(pogo_transport->susp_usb_state));
		return PTR_ERR(pogo_transport->susp_usb_state);
	}

	/* pinctrl for pogo path */
	pogo_transport->susp_pogo_state = pinctrl_lookup_state(pogo_transport->pinctrl,
							       "suspend-to-pogo");
	if (IS_ERR(pogo_transport->susp_pogo_state)) {
		dev_err(pogo_transport->dev, "failed to find pinctrl suspend-to-pogo ret:%ld\n",
			PTR_ERR(pogo_transport->susp_pogo_state));
		return PTR_ERR(pogo_transport->susp_pogo_state);
	}

	return 0;
}

static int init_pogo_ovp_gpio(struct pogo_transport *pogo_transport)
{
	enum of_gpio_flags flags;
	int ret;

	if (!of_property_read_bool(pogo_transport->dev->of_node, "pogo-ovp-en")) {
		pogo_transport->pogo_ovp_en_gpio = -EINVAL;
		return 0;
	}

	pogo_transport->pogo_ovp_en_gpio = of_get_named_gpio_flags(pogo_transport->dev->of_node,
								   "pogo-ovp-en", 0, &flags);
	if (pogo_transport->pogo_ovp_en_gpio < 0) {
		dev_err(pogo_transport->dev, "Pogo ovp en gpio not found. ret:%d\n",
			pogo_transport->pogo_ovp_en_gpio);
		return pogo_transport->pogo_ovp_en_gpio;
	}

	pogo_transport->pogo_ovp_en_active_state = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;

	ret = devm_gpio_request(pogo_transport->dev, pogo_transport->pogo_ovp_en_gpio,
				"pogo-ovp-en");
	if (ret) {
		dev_err(pogo_transport->dev, "failed to request pogo-ovp-en gpio, ret:%d\n", ret);
		return ret;
	}

	/* Default disable pogo ovp. Set to disable state for pogo_ovp_en */
	ret = gpio_direction_output(pogo_transport->pogo_ovp_en_gpio,
				    !pogo_transport->pogo_ovp_en_active_state);
	if (ret) {
		dev_err(pogo_transport->dev, "failed set pogo-ovp-en as output, ret:%d\n", ret);
		return ret;
	}

	return 0;
}

static int pogo_transport_probe(struct platform_device *pdev)
{
	struct pogo_transport *pogo_transport;
	struct device_node *data_np, *dn;
	struct i2c_client *data_client;
	struct max77759_plat *chip;
	char *pogo_psy_name;
	int ret;

	data_np = of_parse_phandle(pdev->dev.of_node, "data-phandle", 0);
	if (!data_np) {
		dev_err(&pdev->dev, "Failed to find tcpci node\n");
		return -ENODEV;
	}

	data_client = of_find_i2c_device_by_node(data_np);
	if (!data_client) {
		dev_err(&pdev->dev, "Failed to find tcpci client\n");
		ret = -EPROBE_DEFER;
		goto free_np;
	}

	chip = i2c_get_clientdata(data_client);
	if (!chip) {
		dev_err(&pdev->dev, "Failed to find max77759_plat\n");
		ret = -EPROBE_DEFER;
		goto put_client;
	}

	pogo_transport = devm_kzalloc(&pdev->dev, sizeof(*pogo_transport), GFP_KERNEL);
	if (!pogo_transport) {
		ret = -ENOMEM;
		goto put_client;
	}

	pogo_transport->dev = &pdev->dev;
	pogo_transport->chip = chip;

	pogo_transport->log = logbuffer_register("pogo_transport");
	if (IS_ERR_OR_NULL(pogo_transport->log)) {
		dev_err(pogo_transport->dev, "logbuffer get failed\n");
		ret = -EPROBE_DEFER;
		goto put_client;
	}
	platform_set_drvdata(pdev, pogo_transport);

	spin_lock_init(&pogo_transport->pogo_event_lock);
	mutex_init(&pogo_transport->reboot_lock);

	pogo_transport->wq = kthread_create_worker(0, "wq-pogo-transport");
	if (IS_ERR_OR_NULL(pogo_transport->wq)) {
		ret = PTR_ERR(pogo_transport->wq);
		goto unreg_logbuffer;
	}

	kthread_init_delayed_work(&pogo_transport->pogo_accessory_debounce_work,
				  process_debounce_event);
	kthread_init_delayed_work(&pogo_transport->state_machine,
				  pogo_transport_state_machine_work);

	alarm_init(&pogo_transport->lc_check_alarm, ALARM_BOOTTIME, lc_check_alarm_handler);
	kthread_init_work(&pogo_transport->lc_work, lc_check_alarm_work_item);
	kthread_init_work(&pogo_transport->event_work, pogo_transport_event_handler);

	dn = dev_of_node(pogo_transport->dev);
	if (!dn) {
		dev_err(pogo_transport->dev, "of node not found\n");
		ret = -EINVAL;
		goto destroy_worker;
	}

	ret = init_regulator(pogo_transport);
	if (ret)
		goto destroy_worker;

	pogo_psy_name = (char *)of_get_property(dn, "pogo-psy-name", NULL);
	if (!pogo_psy_name) {
		dev_err(pogo_transport->dev, "pogo-psy-name not set\n");
		ret = -EINVAL;
		goto destroy_worker;
	}

	pogo_transport->pogo_psy = power_supply_get_by_name(pogo_psy_name);
	if (IS_ERR_OR_NULL(pogo_transport->pogo_psy)) {
		dev_err(pogo_transport->dev, "pogo psy not up\n");
		ret = -EPROBE_DEFER;
		goto destroy_worker;
	}

	pogo_transport->extcon = devm_extcon_dev_allocate(pogo_transport->dev, pogo_extcon_cable);
	if (IS_ERR(pogo_transport->extcon)) {
		dev_err(pogo_transport->dev, "error allocating extcon: %ld\n",
			PTR_ERR(pogo_transport->extcon));
		ret = PTR_ERR(pogo_transport->extcon);
		goto psy_put;
	}

	ret = devm_extcon_dev_register(pogo_transport->dev, pogo_transport->extcon);
	if (ret < 0) {
		dev_err(chip->dev, "failed to register extcon device:%d\n", ret);
		goto psy_put;
	}

	pogo_transport->charger_mode_votable = gvotable_election_get_handle(GBMS_MODE_VOTABLE);
	if (IS_ERR_OR_NULL(pogo_transport->charger_mode_votable)) {
		dev_err(pogo_transport->dev, "GBMS_MODE_VOTABLE get failed %ld\n",
			PTR_ERR(pogo_transport->charger_mode_votable));
		ret = -EPROBE_DEFER;
		goto psy_put;
	}

	pogo_transport->equal_priority = of_property_read_bool(pogo_transport->dev->of_node,
							       "equal-priority");

	ret = init_pogo_ovp_gpio(pogo_transport);
	if (ret) {
		dev_err(pogo_transport->dev, "init_pogo_ovp_gpio error:%d\n", ret);
		goto psy_put;
	}

	ret = init_pogo_gpio(pogo_transport);
	if (ret) {
		dev_err(pogo_transport->dev, "init_pogo_gpio error:%d\n", ret);
		goto psy_put;
	}

	pogo_transport->hub_embedded = of_property_read_bool(dn, "hub-embedded");
	if (pogo_transport->hub_embedded) {
		ret = init_hub_gpio(pogo_transport);
		if (ret)
			goto psy_put;
	}

	/*
	 * modparam_state_machine_enable
	 * 0 or unset: If property "legacy-event-driven" is found in device tree, disable the state
	 *	       machine. Otherwise, enable/disable the state machine based on
	 *	       DEFAULT_STATE_MACHINE_ENABLE.
	 * 1: Enable the state machine
	 * 2: Disable the state machine
	 */
	if (modparam_state_machine_enable == 1) {
		pogo_transport->state_machine_enabled = true;
	} else if (modparam_state_machine_enable == 2) {
		pogo_transport->state_machine_enabled = false;
	} else {
		if (of_property_read_bool(pogo_transport->dev->of_node, "legacy-event-driven"))
			pogo_transport->state_machine_enabled = false;
		else
			pogo_transport->state_machine_enabled = DEFAULT_STATE_MACHINE_ENABLE;
	}

	if (pogo_transport->state_machine_enabled) {
		pogo_transport_set_state(pogo_transport, STANDBY, 0);
		pogo_transport->wait_for_suspend = true;
		pogo_transport->lc_stage = STAGE_UNKNOWN;
	}

	if (modparam_pogo_accessory_enable) {
		ret = init_acc_gpio(pogo_transport);
		if (ret)
			goto psy_put;
		pogo_transport->accessory_detection_enabled = modparam_pogo_accessory_enable;
	} else if (of_property_read_bool(dn, "pogo-acc-capable") ||
		   of_property_read_bool(dn, "pogo-acc-hall-only")) {
		ret = init_acc_gpio(pogo_transport);
		if (ret)
			goto psy_put;
		if (of_property_read_bool(dn, "pogo-acc-capable"))
			pogo_transport->accessory_detection_enabled = ENABLED;
		else
			pogo_transport->accessory_detection_enabled = HALL_ONLY;
	}

	if (pogo_transport->pogo_acc_gpio > 0) {
		pogo_transport->acc_charger_psy_name =
				(char *)of_get_property(dn, "acc-charger-psy-name", NULL);
		if (!pogo_transport->acc_charger_psy_name)
			dev_info(pogo_transport->dev, "acc-charger-psy-name not set\n");

		pogo_transport->lc_delay_check_ms = LC_DELAY_CHECK_MS;
		pogo_transport->lc_disable_ms = LC_DISABLE_MS;
		pogo_transport->lc_enable_ms = LC_ENABLE_MS;
		pogo_transport->lc_bootup_ms = LC_BOOTUP_MS;
		pogo_transport->acc_charging_timeout_sec = ACC_CHARGING_TIMEOUT_SEC;
		pogo_transport->acc_charging_full_begin_ns = 0;
		pogo_transport->acc_discharging_begin_ns = 0;
	}

	pogo_transport->disable_voltage_detection =
		of_property_read_bool(dn, "disable-voltage-detection");

	ret = init_pogo_irqs(pogo_transport);
	if (ret) {
		dev_err(pogo_transport->dev, "init_pogo_irqs error:%d\n", ret);
		goto psy_put;
	}

#if IS_ENABLED(CONFIG_DEBUG_FS)
	pogo_transport_init_debugfs(pogo_transport);
#endif

	register_data_active_callback(data_active_changed, pogo_transport);
	register_orientation_callback(orientation_changed, pogo_transport);
	register_bus_suspend_callback(usb_bus_suspend_resume, pogo_transport);
	pogo_transport->udev_nb.notifier_call = pogo_transport_udev_notify;
	usb_register_notify(&pogo_transport->udev_nb);
	pogo_transport->reboot_nb.notifier_call = pogo_transport_reboot_notify;
	register_reboot_notifier(&pogo_transport->reboot_nb);
	/* run once in case orientation has changed before registering the callback */
	orientation_changed((void *)pogo_transport);
	dev_info(&pdev->dev, "force usb:%d\n", modparam_force_usb ? 1 : 0);
	dev_info(&pdev->dev, "state machine:%u\n", pogo_transport->state_machine_enabled);
	put_device(&data_client->dev);
	of_node_put(data_np);
	return 0;

psy_put:
	if (pogo_transport->acc_charger_psy)
		power_supply_put(pogo_transport->acc_charger_psy);
	power_supply_put(pogo_transport->pogo_psy);
destroy_worker:
	kthread_destroy_worker(pogo_transport->wq);
unreg_logbuffer:
	logbuffer_unregister(pogo_transport->log);
put_client:
	put_device(&data_client->dev);
free_np:
	of_node_put(data_np);
	return ret;
}

static int pogo_transport_remove(struct platform_device *pdev)
{
	struct pogo_transport *pogo_transport = platform_get_drvdata(pdev);
	struct dentry *dentry;
	int ret;

	unregister_reboot_notifier(&pogo_transport->reboot_nb);
	usb_unregister_notify(&pogo_transport->udev_nb);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	dentry = debugfs_lookup("pogo_transport", NULL);
	if (IS_ERR(dentry)) {
		dev_err(pogo_transport->dev, "%s: Failed to lookup debugfs dir\n", __func__);
	} else {
		debugfs_remove(dentry);
		dput(dentry);
	}
#endif

	if (pogo_transport->hub_ldo && regulator_is_enabled(pogo_transport->hub_ldo) > 0)
		regulator_disable(pogo_transport->hub_ldo);

	ret = pogo_transport_acc_regulator(pogo_transport, false);
	if (ret)
		dev_err(pogo_transport->dev, "%s: Failed to disable acc ldo %d\n", __func__, ret);

	if (pogo_transport->acc_detect_ldo &&
	    regulator_is_enabled(pogo_transport->acc_detect_ldo) > 0)
		regulator_disable(pogo_transport->acc_detect_ldo);

	if (pogo_transport->pogo_acc_irq > 0) {
		disable_irq_wake(pogo_transport->pogo_acc_irq);
		devm_free_irq(pogo_transport->dev, pogo_transport->pogo_acc_irq, pogo_transport);
	}
	disable_irq_wake(pogo_transport->pogo_irq);
	devm_free_irq(pogo_transport->dev, pogo_transport->pogo_irq, pogo_transport);
	if (pogo_transport->acc_charger_psy)
		power_supply_put(pogo_transport->acc_charger_psy);
	power_supply_put(pogo_transport->pogo_psy);
	kthread_destroy_worker(pogo_transport->wq);
	logbuffer_unregister(pogo_transport->log);

	return 0;
}

/*-------------------------------------------------------------------------*/
/* Event triggering part.2                                                 */
/*-------------------------------------------------------------------------*/

#define POGO_TRANSPORT_RO_ATTR(_name)                                                           \
static ssize_t _name##_show(struct device *dev, struct device_attribute *attr, char *buf)       \
{                                                                                               \
	struct pogo_transport *pogo_transport  = dev_get_drvdata(dev);                          \
	return sysfs_emit(buf, "%d\n", pogo_transport->_name);                                  \
}                                                                                               \
static DEVICE_ATTR_RO(_name)
POGO_TRANSPORT_RO_ATTR(equal_priority);
POGO_TRANSPORT_RO_ATTR(pogo_usb_active);

static ssize_t move_data_to_usb_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct pogo_transport *pogo_transport = dev_get_drvdata(dev);
	u8 enable;

	if (kstrtou8(buf, 0, &enable))
		return -EINVAL;

	if (enable != 1)
		return -EINVAL;

	if (pogo_transport->state_machine_enabled)
		pogo_transport_queue_event(pogo_transport, EVENT_ENABLE_USB_DATA);
	else
		pogo_transport_event(pogo_transport, EVENT_MOVE_DATA_TO_USB, 0);

	return size;
}
static DEVICE_ATTR_WO(move_data_to_usb);

static ssize_t force_pogo_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t size)
{
	struct pogo_transport *pogo_transport = dev_get_drvdata(dev);
	bool force_pogo;

	if (kstrtobool(buf, &force_pogo))
		return -EINVAL;

	if (pogo_transport->force_pogo == force_pogo)
		return size;

	pogo_transport->force_pogo = force_pogo;
	if (force_pogo && !pogo_transport->state_machine_enabled)
		pogo_transport_event(pogo_transport, EVENT_MOVE_DATA_TO_POGO, 0);

	if (force_pogo && pogo_transport->state_machine_enabled)
		pogo_transport_queue_event(pogo_transport, EVENT_FORCE_POGO);

	return size;
}

static ssize_t force_pogo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pogo_transport *pogo_transport  = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", pogo_transport->force_pogo);
}
static DEVICE_ATTR_RW(force_pogo);

static ssize_t enable_hub_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t size)
{
	struct pogo_transport *pogo_transport = dev_get_drvdata(dev);
	u8 enable_hub;

	if (pogo_transport->state_machine_enabled) {
		logbuffer_log(pogo_transport->log, "state machine enabled; ignore enable_hub");
		return size;
	}

	if (!pogo_transport->hub_embedded)
		return size;

	if (kstrtou8(buf, 0, &enable_hub))
		return -EINVAL;

	if (pogo_transport->pogo_hub_active == !!enable_hub)
		return size;

	/*
	 * KEEP_HUB_PATH is only for engineering tests where the embedded hub remains enabled after
	 * undocking.
	 */
	if (enable_hub == KEEP_HUB_PATH)
		pogo_transport->force_hub_enabled = true;
	else
		pogo_transport->force_hub_enabled = false;

	dev_info(pogo_transport->dev, "hub %u, force_hub_enabled %u\n", enable_hub,
		 pogo_transport->force_hub_enabled);
	if (enable_hub)
		pogo_transport_event(pogo_transport, EVENT_ENABLE_HUB, 0);
	else
		pogo_transport_event(pogo_transport, EVENT_DISABLE_HUB, 0);

	return size;
}

static ssize_t enable_hub_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pogo_transport *pogo_transport  = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", pogo_transport->pogo_hub_active);
}
static DEVICE_ATTR_RW(enable_hub);

static ssize_t hall1_s_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size)
{
	struct pogo_transport *pogo_transport = dev_get_drvdata(dev);
	u8 enable_acc_detect;

	if (!pogo_transport->acc_detect_ldo)
		return size;

	if (!pogo_transport->accessory_detection_enabled) {
		logbuffer_logk(pogo_transport->log, LOGLEVEL_INFO, "%s:Accessory detection disabled",
			       __func__);
		return size;
	}

	if (kstrtou8(buf, 0, &enable_acc_detect))
		return -EINVAL;

	if (pogo_transport->hall1_s_state == !!enable_acc_detect)
		return size;

	pogo_transport->hall1_s_state = !!enable_acc_detect;

	/*
	 * KEEP_USB_PATH is only for factory tests where the USB connection needs to stay at USB-C
	 * after the accessory is attached.
	 */
	if (enable_acc_detect == KEEP_USB_PATH)
		pogo_transport->mfg_acc_test = true;
	else
		pogo_transport->mfg_acc_test = false;

	logbuffer_log(pogo_transport->log, "H1S: accessory detection %u, mfg %u", enable_acc_detect,
		      pogo_transport->mfg_acc_test);

	if (pogo_transport->state_machine_enabled) {
		pogo_transport_queue_event(pogo_transport, EVENT_HES_H1S_CHANGED);
		return size;
	}

	if (enable_acc_detect)
		pogo_transport_event(pogo_transport, EVENT_HALL_SENSOR_ACC_DETECTED, 0);
	else
		pogo_transport_event(pogo_transport, EVENT_HALL_SENSOR_ACC_UNDOCKED, 0);

	return size;
}
static DEVICE_ATTR_WO(hall1_s);

static ssize_t hall1_n_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size)
{
	struct pogo_transport *pogo_transport = dev_get_drvdata(dev);
	u8 data;

	/* Reserved for HES1 Malfunction detection */

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	logbuffer_log(pogo_transport->log, "H1N: %u", data);
	return size;
}
static DEVICE_ATTR_WO(hall1_n);

static ssize_t hall2_s_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t size)
{
	struct pogo_transport *pogo_transport = dev_get_drvdata(dev);
	u8 data;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	if (pogo_transport->lc == !!data)
		return size;

	pogo_transport->lc = !!data;

	if (!pogo_transport->lc) {
		alarm_cancel(&pogo_transport->lc_check_alarm);
		kthread_cancel_work_sync(&pogo_transport->lc_work);
	}

	logbuffer_log(pogo_transport->log, "H2S: %u", pogo_transport->lc);

	if (pogo_transport->state_machine_enabled)
		pogo_transport_queue_event(pogo_transport, EVENT_LC_STATUS_CHANGED);

	return size;
}
static DEVICE_ATTR_WO(hall2_s);

static ssize_t acc_detect_debounce_ms_store(struct device *dev, struct device_attribute *attr,
					    const char *buf, size_t size)
{
	struct pogo_transport *pogo_transport = dev_get_drvdata(dev);
	unsigned int debounce_ms;
	int ret;

	if (kstrtouint(buf, 0, &debounce_ms))
		return -EINVAL;

	ret = gpio_set_debounce(pogo_transport->pogo_acc_gpio, debounce_ms * 1000);
	if (ret < 0) {
		dev_info(pogo_transport->dev, "failed to set debounce, ret:%d\n", ret);
		pogo_transport->pogo_acc_gpio_debounce_ms = debounce_ms;
	}

	return size;
}

static ssize_t acc_detect_debounce_ms_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct pogo_transport *pogo_transport  = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", pogo_transport->pogo_acc_gpio_debounce_ms);
}
static DEVICE_ATTR_RW(acc_detect_debounce_ms);

static struct attribute *pogo_transport_attrs[] = {
	&dev_attr_move_data_to_usb.attr,
	&dev_attr_equal_priority.attr,
	&dev_attr_pogo_usb_active.attr,
	&dev_attr_force_pogo.attr,
	&dev_attr_enable_hub.attr,
	&dev_attr_hall1_s.attr,
	&dev_attr_hall1_n.attr,
	&dev_attr_hall2_s.attr,
	&dev_attr_acc_detect_debounce_ms.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pogo_transport);

static const struct of_device_id pogo_transport_of_match[] = {
	{.compatible = "pogo-transport"},
	{},
};
MODULE_DEVICE_TABLE(of, pogo_transport_of_match);

static struct platform_driver pogo_transport_driver = {
	.driver = {
		   .name = "pogo-transport",
		   .owner = THIS_MODULE,
		   .of_match_table = pogo_transport_of_match,
		   .dev_groups = pogo_transport_groups,
		   },
	.probe = pogo_transport_probe,
	.remove = pogo_transport_remove,
};

module_platform_driver(pogo_transport_driver);

MODULE_DESCRIPTION("Pogo data management");
MODULE_AUTHOR("Badhri Jagan Sridharan <badhri@google.com>");
MODULE_LICENSE("GPL");
