/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM WLAN Interface Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_DRIVER_WLAN_INTERFACE_H__
#define __LVM_DRIVER_WLAN_INTERFACE_H__

#include <linux/interrupt.h>
#include "format.h"
#include "mcu.h"

struct wlan_data;

int lvm_wlan_txd_prepare(struct wlan_data *data, void *_buf,
			 u32 ifidx, struct host_txbuf_post *txd);
void lvm_wlan_txcpl_process(struct wlan_data *data, void *msg);
void lvm_wlan_rxcpl_process(struct wlan_data *data, void *msg);
void lvm_wlan_rxpost_process(struct wlan_data *data);
int lvm_wlan_rxpost_buf_alloc(struct wlan_data *data, u32 count);

#endif  /* __LVM_DRIVER_WLAN_INTERFACE_H__ */
