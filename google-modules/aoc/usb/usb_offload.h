/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Google Corp.
 *
 * Author:
 *  Howard.Yen <howardyen@google.com>
 *  Puma.Hsu   <pumahsu@google.com>
 */

#ifndef __USB_OFFLOAD_H
#define __USB_OFFLOAD_H

#include <linux/list.h>

enum memory_swap_state {
	STATE_MAPPED_SRAM,
	STATE_SWAP_REQUESTING,
	STATE_SWAP_REQUESTED,
	STATE_MAPPED_DRAM
};

enum parsed_usb_device {
	DEVICE_UNKNOWN,
	DEVICE_GENERAL,
	DEVICE_HUB,
	DEVICE_ROOT_HUB,
	DEVICE_STORAGE,
	DEVICE_STORAGE_STREAMING,
	DEVICE_AUDIO,
	DEVICE_AUDIO_ISOC

};

/* Shall match enum parsed_usb_device */
static const char * const parsed_usb_devices[] = {
	[DEVICE_UNKNOWN]                = "unknown",
	[DEVICE_GENERAL]                = "general",
	[DEVICE_HUB]                    = "hub",
	[DEVICE_ROOT_HUB]		= "root hub",
	[DEVICE_STORAGE]                = "storage",
	[DEVICE_STORAGE_STREAMING]      = "streaming storage",
	[DEVICE_AUDIO]			= "audio",
	[DEVICE_AUDIO_ISOC]		= "audio with isoc"
};

enum usb_offload_status {
	DISABLED = 0,        /* Nothing has configured for audio offload. */
	RMEM_CONFIGURED,     /* Only reserved memory is configured (AoC disabled). */
	OFFLOAD_CONFIGURED,  /* Offload function has configured and ready for audio events. */
};

struct usb_offload_data {
	struct usb_bus *ubus;

	struct device *aoc_core_pd;
	struct notifier_block aoc_core_nb;
	struct completion aoc_core_pd_power_on;
	struct completion aoc_core_pd_power_off;

	bool setup_notified;
	enum usb_offload_status offload_status;
	bool aoc_ready;

	struct list_head offload_dev_list; /* List of compatible USB audio devices */
	struct mutex offload_dev_lock; /* Lock to protect offload_dev_list */

	struct work_struct offload_connect_ws;
	struct wakeup_source *wakelock;

	struct gvotable_election *usb_data_role_votable;
	bool memory_swap_enabled;
	enum memory_swap_state mem_swap_stat;
	int wakeup_dev_count; /* Number of devices that enable remote wakeup */
	int total_dev_count; /* Total number of connected devices */
};

int usb_offload_helper_init(void);
void usb_offload_helper_exit(void);

bool usb_offload_get_aoc_ready(void);
int usb_offload_set_aoc_ready(bool is_ready);

#endif /* __USB_OFFLOAD_H */
