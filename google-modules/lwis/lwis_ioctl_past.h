/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS IOCTL Handler
 *
 * Copyright (c) 2024 Google, LLC
 */

#include "lwis_ioctl.h"

extern struct cmd_transaction_submit_ops transaction_cmd_v6_ops;
extern struct cmd_transaction_submit_ops transaction_cmd_v7_ops;

extern const struct cmd_dpm_qos_update_ops cmd_dpm_qos_v4_ops;
extern const struct cmd_dpm_qos_update_ops cmd_dpm_qos_v3_ops;
