/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM Network Sysfs Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_NET_SYSFS_H__
#define __LVM_NET_SYSFS_H__

#include <net/platform.h>

int lvm_net_sysfs_init(struct lvm_net *net);
void lvm_net_sysfs_deinit(struct lvm_net *net);

#endif  /* __LVM_NET_SYSFS_H__ */
