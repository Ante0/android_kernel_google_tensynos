/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Configuration file for NOA driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NOA_CONFIG_H_
#define __NOA_CONFIG_H_

/* NEP simulator configuration */
#define NOA_SIM_DIRECT_TX 1
#define NOA_FW_DIRECT_TX 0
#define NOA_SIM_DIRECT_ISR 0
#define NOA_SIM_DIRECT_DB 0

/* NCP WLAN FW configuration */
#define NOA_WLAN_FORWARD_NET 1

#endif  /* _NOA_CONFIG_H_ */
