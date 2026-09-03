/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Google Corp.
 *
 * Author:
 *  Howard.Yen <howardyen@google.com>
 */

#ifndef __LINUX_AOC_USB_H
#define __LINUX_AOC_USB_H

#include <linux/notifier.h>
#include <linux/usb/role.h>

#include "usbaudio.h"
#include "xhci.h"

enum aoc_usb_msg {
	GET_TR_DEQUEUE_PTR,
	SETUP_DONE,
	SYNC_CONN_STAT
};

enum aoc_usb_state {
	USB_DISCONNECTED,
	USB_CONNECTED
};

enum aoc_recovery_type {
	DISABLE_OFFLOAD,
	EXIT_HOST_MODE
};


struct aoc_usb_drvdata {
	struct aoc_service_dev *adev;

	struct mutex lock;
	struct wakeup_source *ws;

	struct notifier_block nb;
	struct delayed_work aoc_ready_work;
	struct gvotable_election *usb_data_role_votable;
	enum aoc_usb_state usb_state;
	enum aoc_recovery_type recovery_type;
	long service_timeout;
	int aoc_ready_work_retries;
};

struct conn_stat_args {
	u16 bus_id;
	u16 dev_num;
	u16 slot_id;
	u32 conn_stat;
};

struct get_isoc_tr_info_args {
	u16 ep_id;
	u16 dir;
	u32 type;
	u32 num_segs;
	u32 seg_ptr;
	u32 max_packet;
	u32 deq_ptr;
	u32 enq_ptr;
	u32 cycle_state;
	u32 num_trbs_free;
};

int register_aoc_usb_notifier(struct notifier_block *nb);
int unregister_aoc_usb_notifier(struct notifier_block *nb);

extern bool aoc_alsa_usb_callback_register(void (*callback)(struct usb_device*,
							    struct usb_host_endpoint*));
extern bool aoc_alsa_usb_callback_unregister(void);
extern bool aoc_alsa_usb_conn_callback_register(void (*callback)(struct usb_device *udev,
								 bool conn_state));
extern bool aoc_alsa_usb_conn_callback_unregister(void);

int xhci_setup_done(void);
int xhci_sync_conn_stat(unsigned int bus_id, unsigned int dev_num, unsigned int slot_id,
			       unsigned int conn_stat);
int usb_host_mode_state_notify(enum aoc_usb_state usb_state);
int xhci_get_usb_audio_count(void);
int usb_audio_offload_resume(void);
int usb_audio_offload_pause(void);

#if IS_ENABLED(CONFIG_AOC_USB_AUDIO_OFFLOAD)
int usb_offload_helper_init(void);
void usb_offload_helper_exit(void);
#else /* CONFIG_AOC_USB_AUDIO_OFFLOAD */
static inline int usb_offload_helper_init(void) { return 0; }
static inline void usb_offload_helper_exit(void) {}
#endif /* CONFIG_AOC_USB_AUDIO_OFFLOAD */

extern struct gvotable_election *gvotable_election_get_handle(const char *name);
extern int gvotable_cast_vote(struct gvotable_election *el, const char *reason,
		       void *vote, bool enabled);
extern int gvotable_get_int_vote(struct gvotable_election *el, const char *reason);

#endif /* __LINUX_AOC_USB_H */
