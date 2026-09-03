// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 * API common between SPMI & I2C MAX77759 Type-C Drivers.
 */

#ifndef __TCPCI_MAX77759_H
#define __TCPCI_MAX77759_H

#include <linux/alarmtimer.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/usb/tcpm.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/role.h>
#include <linux/usb/typec_mux.h>
#include <misc/gvotable.h>

#include "google_tcpci_shim.h"
#include "usb_psy.h"

#define TCPC_RECEIVE_BUFFER_METADATA_SIZE               2
#define TCPC_RECEIVE_BUFFER_COUNT_OFFSET                0
#define TCPC_RECEIVE_BUFFER_FRAME_TYPE_OFFSET           1
#define TCPC_RECEIVE_BUFFER_RX_BYTE_BUF_OFFSET          2

#define COMBINED_ROLE_VOTABLE				"COMB_ROLE_VOTABLE"
#define TCPCI_VOTER					"TCPCI_VOTER"
#define POGO_VOTER					"POGO_VOTER"

struct gvotable_election;
struct logbuffer;
struct max777x9_contaminant;
struct max77759_compliance_warnings;
struct google_shim_tcpci_data;
struct max77759_io_error;

typedef int (*enable_otg_direct)(struct max77759_plat *chip, bool en);

struct max77759_plat {
	struct google_shim_tcpci_data data;
	struct google_shim_tcpci *tcpci;
	struct device *dev;
	struct bc12_status *bc12;
	void *client;
	/* Client callback to enable OTG */
	enable_otg_direct client_otg_cb;
	struct power_supply *usb_psy;
	struct power_supply *tcpm_psy;
	struct max777x9_contaminant *contaminant;
	struct gvotable_election *usb_icl_proto_el;
	struct gvotable_election *usb_icl_el;
	struct gvotable_election *charger_mode_votable;
	struct gvotable_election *bcl_usb_votable;
	struct gvotable_election *usb_data_role_votable;
	struct gvotable_election *comb_role_votable;
	bool vbus_enabled;
	/* Data role notified to the data stack */
	enum typec_data_role active_data_role;
	/* Data role from the TCPM stack */
	enum typec_data_role data_role;
	/* CC polarity from the TCPM stack */
	enum typec_cc_polarity polarity;
	/* protects tcpc_enable_data_path */
	struct mutex data_path_lock;
	/* Vote for data from BC1.2 */
	bool bc12_data_capable;
	/* Infered from pd caps */
	bool pd_data_capable;
	/* Vote from TCPC for attached */
	bool attached;
	/* Reflects the signal sent out to the data stack */
	bool data_active;
	/* Alernate data path */
	bool alt_path_active;
	/* Reflects whether the current partner can do PD */
	bool pd_capable;
	void *usb_psy_data;
	struct mutex icl_proto_el_lock;

	/* Indicated whether vbus alarms have been enabled  */
	bool alarm_enabled;
	/* If vbus alarm is enabled, whether its programmed for high or low */
	bool alarm_high;
	/*
	 * Protects vbus voltage alarm flags i.e. alarm_enabled, alarm_high and
	 * ALERT_MASK.VBUS Voltage Alarm Lo/Hi.
	 */
	struct mutex vbus_alarm_lock;

	unsigned int vbus_mv;
	/* USB Data notification */
	struct extcon_dev *extcon;
	struct typec_switch *orientation_sw;
	bool no_bc_12;
	/* Platform does not support external boost */
	bool no_external_boost;
	struct tcpm_port *port;
	struct usb_psy_ops psy_ops;
	/* toggle in_switch to kick debug accessory statemachine when already connected */
	struct gpio_desc *in_switch_gpio;
	struct gpio_desc *sbu_mux_en_gpio;
	struct gpio_desc *sbu_mux_sel_gpio;
	struct gpio_desc *vbus_dis_gpio;
	/* 0:active_low 1:active_high */
	bool in_switch_gpio_active_high;
	bool first_toggle;
	/* Set true to vote "limit_sink_current" on USB ICL */
	bool limit_sink_enable;
	/* uA */
	unsigned int limit_sink_current;
	/* Indicate that the Vbus OVP is restricted to quick ramp-up time for incoming voltage. */
	bool quick_ramp_vbus_ovp;
	int reset_ovp_retry;
	struct mutex ovp_lock;
	/* Set true to vote "limit_accessory_current" on USB ICL */
	bool limit_accessory_enable;
	/* uA */
	unsigned int limit_accessory_current;
	bool usb_throttled;
	struct gvotable_election *usb_throttle_votable;

	/* True when TCPC is in SINK DEBUG ACCESSORY CONNECTED state */
	u8 debug_acc_connected:1;
	/* Cache status when sourcing vbus. Used to modify vbus_present status */
	u8 sourcing_vbus:1;
	u8 sourcing_vbus_high:1;
	/* Cache vbus_present as MAX77759 reports vbus present = 0 when vbus < 4V */
	u8 vbus_present:1;
	u8 cc1;
	u8 cc2;

	/* Runtime flags */
	int frs;
	bool in_frs;
	bool vsafe0v;

	/*
	 * Guards vsafe0v so that consistent values are viewed across different
	 * execution context.
	 */
	struct mutex vsafe0v_lock;

	/*
	 * Current status of contaminant detection.
	 * 0 - Disabled
	 * 1 - AP
	 * 2 - MAXQ
	 */
	int contaminant_detection;
	/* Userspace status */
	bool contaminant_detection_userspace;
	/* Consecutive floating cable instances */
	unsigned int floating_cable_or_sink_detected;
	/* Timer to re-enable auto ultra lower mode for contaminant detection */
	struct alarm reenable_auto_ultra_low_power_mode_alarm;
	/* Bottom half for alarm */
	struct kthread_work reenable_auto_ultra_low_power_mode_work;
	/*
	 * Set in debounce path from TCPM when contaminant is detected.
	 * Cleared after exiting dry detection. Needed to move TCPM back into TOGGLING state.
	 */
	bool check_contaminant;

	/* Protects contaminant_detection variable and role_control */
	struct mutex rc_lock;

	/* Accumulate votes to disable toggling as needed */
	struct gvotable_election *toggle_disable_votable;
	/* Current status of toggle */
	bool toggle_disable_status;
	/* Cached role ctrl setting */
	u8 role_ctrl_cache;

	struct notifier_block psy_notifier;
	int online;
	int usb_type;
	int typec_current_max;
	struct kthread_worker *wq;
	struct kthread_worker *dp_notification_wq;
	struct kthread_worker *bcl_usb_wq;
	struct kthread_delayed_work icl_work;
	struct kthread_delayed_work enable_vbus_work;
	struct kthread_delayed_work vsafe0v_work;
	struct kthread_delayed_work reset_ovp_work;
	struct kthread_delayed_work check_missing_rp_work;
	struct kthread_delayed_work bcl_usb_votable_work;
	struct kthread_delayed_work vbus_dis_work;
	u8 bcl_usb_vote;

	/* Notifier for data role */
	struct usb_role_switch *usb_sw;
	/* Notifier for orientation */
	struct typec_switch_dev *typec_sw;
	/* mode mux */
	struct typec_mux_dev *mode_mux;
	unsigned long mode_mux_value;
	/* mode mux for combo phy driver */
	struct typec_mux *phy_mux;
	/* Cache orientation for dp */
	enum typec_orientation orientation;
	/* Cache the number of lanes */
	int lanes;
	/* DisplayPort Regulator */
	struct regulator *dp_regulator;
	bool dp_regulator_enabled;
	unsigned int dp_regulator_min_uv;
	unsigned int dp_regulator_max_uv;

	/* Reflects whether BC1.2 is still running */
	bool bc12_running;

	/* To handle io error - Last cached IRQ status*/
	u16 irq_status;
	struct kthread_delayed_work max77759_io_error_work;
	/* Hold before calling _max77759_irq */
	struct mutex irq_status_lock;

	/* non compliant reasons */
	struct max77759_compliance_warnings *compliance_warnings;

	/* GPIO state for SBU pin pull up/down */
	int current_sbu_state;
	/* IRQ_HPD event count */
	u32 irq_hpd_count;

	/* Signal from charger when AICL is active. */
	struct gvotable_election *aicl_active_el;

	/* Timer to check for AICL status */
	struct alarm aicl_check_alarm;
	/* Bottom half for alarm */
	struct kthread_work aicl_check_alarm_work;
	/* AICL status from hardware */
	bool aicl_active;

	/* When true debounce disconnects to prevent user notifications during brief disconnects */
	bool debounce_adapter_disconnect;

	/* Read from device tree and denotes the vbus voltage threshold for ovp during source */
	u32 ext_bst_ovp_clear_mv;
	/* Used to check voltage again after a second to see if it meets the ovp threshold */
	struct kthread_delayed_work ext_bst_ovp_clear_work;
	/* protects checking and setting sourcing_vbus_high in check_and_clear_ext_bst */
	struct mutex ext_bst_ovp_clear_lock;

	/* Set to true if EXT_BST_EN is not used */
	bool manual_disable_vbus;

	int device_id;
	int product_id;

	/* Timestamp to record first_toggle */
	ktime_t first_toggle_time_since_boot;

	/* EXT_BST_EN exposed as GPIO */
	struct gpio_chip gpio;

	struct logbuffer *log;

	u8 force_device_mode_on:1;

	/*
	 * Directly vote for vbus out when GBMS_MODE_VOTABLE is not found.
	 * Meant to be used during bring-up.
	 * Add enable-otg-direct dts property.
	 */
	bool enable_otg_direct;

#ifdef CONFIG_DEBUG_FS
	struct dentry *dentry;
#endif

	struct wakeup_source *cc_toggle_ws;
};

/**
 * struct max777x9_desc: Description of a MAX777x9 chip required for populating driver data.
 * @dev: Required; pointer to struct device.
 * @rmap: Required; pointer to regmap handler.
 * @client: Required; pointer to client device.
 * @plat: Optional; pointer to max77759 driver structure. This field is
 *	  populated by the callee.
 * @rx: required; RX handler for receiving PD message.
 * @tx: optional; TX handler for sending PD message.
 */
struct max777x9_desc {
	struct device *dev;
	struct regmap *rmap;
	void *client;
	struct max77759_plat *plat;
	int (*rx)(struct google_shim_tcpci *tcpci, u8 *buf, size_t size);
	int (*tx)(struct google_shim_tcpci *tcpci, enum tcpm_transmit_type type,
		  const struct pd_message *msg, unsigned int negotiated_rev);
};

/*
 * bc1.2 registration
 */

struct max77759_usb;

void register_tcpc(struct max77759_usb *usb, struct max77759_plat *chip);

#define VBUS_VOLTAGE_MASK		0x3ff
#define VBUS_VOLTAGE_LSB_MV		25
#define VBUS_HI_HEADROOM_MV		500
#define VBUS_LO_MV			4500

#define MAX77759_DEVICE_ID_A1 0x2
#define MAX77759_PRODUCT_ID 0x59
#define MAX77779_PRODUCT_ID 0x79

enum tcpm_psy_online_states {
	TCPM_PSY_OFFLINE = 0,
	TCPM_PSY_FIXED_ONLINE,
	TCPM_PSY_PROG_ONLINE,
};

int max77759_register(struct max777x9_desc *desc, enable_otg_direct client_otg_cb);
void max77759_unregister(struct max77759_plat *plat);
int max77759_register_irq(int irq, u32 irqflags, struct max77759_plat *chip);
void max77759_shutdown(struct max77759_plat *plat);

void enable_data_path_locked(struct max77759_plat *chip);
void data_alt_path_active(struct max77759_plat *chip, bool active);
void register_data_active_callback(void (*callback)(void *data_active_payload,
						    enum typec_data_role role, bool active),
				   void *data);
void register_orientation_callback(void (*callback)(void *orientation_payload), void *data);

#define COMPLIANCE_WARNING_OTHER 0
#define COMPLIANCE_WARNING_DEBUG_ACCESSORY 1
#define COMPLIANCE_WARNING_BC12 2
#define COMPLIANCE_WARNING_MISSING_RP 3
#define COMPLIANCE_WARNING_INPUT_POWER_LIMITED 4

struct max77759_compliance_warnings {
	struct max77759_plat *chip;
	bool other;
	bool debug_accessory;
	bool bc12;
	bool missing_rp;
	bool input_power_limited;
};

ssize_t compliance_warnings_to_buffer(struct max77759_compliance_warnings *compliance_warnings,
				      char *buf);
void update_compliance_warnings(struct max77759_plat *chip, int warning, bool value);

static inline struct max77759_plat *tdata_to_max77759(struct google_shim_tcpci_data *tdata)
{
	return container_of(tdata, struct max77759_plat, data);
}

#endif /* __TCPCI_MAX77759_H */
