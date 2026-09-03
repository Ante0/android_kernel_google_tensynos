// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019, Google LLC
 *
 * USB input current management.
 * API common between SPMI & I2C MAX77759 Type-C drivers.
 *
 */
#ifndef __USB_PSY__
#define __USB_PSY__
#include <misc/logbuffer.h>
#include <linux/device.h>
#include <linux/power_supply.h>

struct max77759_plat;

struct usb_psy_ops {
	int (*tcpc_get_vbus_voltage_max_mv)(struct device *dev);
	int (*tcpc_set_vbus_voltage_max_mv)(struct device *dev, unsigned int mv);
	int (*tcpc_get_vbus_voltage_mv)(struct device *dev);
	void (*tcpc_set_port_data_capable)(struct device *dev, enum power_supply_usb_type usb_type);
};

typedef void (*non_compliant_bc12_callback) (void *chip, bool status);

void usb_psy_set_sink_state(void *usb_psy, bool enabled);
void usb_psy_set_attached_state(void *usb_psy, bool attached);
void *usb_psy_setup(struct device *dev, struct logbuffer *log, struct usb_psy_ops *ops,
		    void *chip, non_compliant_bc12_callback callback);
void usb_psy_teardown(void *usb_data);
void usb_psy_start_sdp_timeout(void *usb_psy);
void usb_psy_stop_sdp_timeout(void *usb_psy);
void apply_sdp_enum_failure_wa(void *usb_psy);
#endif
