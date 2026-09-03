// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 * Google's USB data role switch driver
 */

#ifndef GOOGLE_ROLE_SW_H_
#define GOOGLE_ROLE_SW_H_

#include <linux/extcon-provider.h>
#include <linux/usb/role.h>

#define AOC_VOTER "AOC"
#define OFFLOAD_VOTER "OFFLOAD"
#define TCPCI_COMB_VOTER "TCPCI_COMB"
#define DISABLE_USB_DATA_VOTER "DISABLE_USB_DATA"
#define VOTABLE_USB_DATA_ROLE "USB_DR_EL"

static const unsigned int data_role_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

enum downstream_sw {
	NONE = 0,
	USB_ROLE_SWITCH,
	EXTCON_DEV,
};

enum disable_usb_data {
	USB_DATA_ENABLED = 0,
	USB_DATA_DISABLED,
};

static const char * const downstream_sws[] = {
	[NONE]			= "none",
	[USB_ROLE_SWITCH]	= "role-sw-dev",
	[EXTCON_DEV]		= "extcon",
};

struct google_role_sw {
	struct gvotable_election *usb_data_role_votable;
	struct usb_role_switch *eusb_role_sw;
	struct usb_role_switch *role_sw;
	struct delayed_work init_role_sw_work;
	struct extcon_dev *extcon;
	struct device *dev;
	struct mutex update_role_lock;
	enum downstream_sw downstream;
	int curr_role;
};

#if IS_ENABLED(CONFIG_KUNIT)
	int update_data_role(struct gvotable_election *el, const char *reason, void *value);
#endif

#endif  // GOOGLE_ROLE_SW_H_
