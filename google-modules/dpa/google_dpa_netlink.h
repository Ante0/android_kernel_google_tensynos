/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GOOGLE_DPA_NETLINK_H
#define _GOOGLE_DPA_NETLINK_H

#include "google_dpa_internal.h"

int google_dpa_netlink_init(struct google_dpa *dpa);

void google_dpa_netlink_deinit(struct google_dpa *dpa);

int google_dpa_netlink_send_data(struct google_dpa *dpa, void *data, size_t size);

#endif /* _GOOGLE_DPA_NETLINK_H */
