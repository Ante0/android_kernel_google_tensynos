/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM WLAN Module Loader Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_DRIVER_WLAN_MODULE_H__
#define __LVM_DRIVER_WLAN_MODULE_H__

int lvm_wlan_module_init(void);
void lvm_wlan_module_exit(void);

#endif  /* __LVM_DRIVER_WLAN_MODULE_H__ */
