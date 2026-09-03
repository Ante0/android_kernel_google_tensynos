/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __FEATURE_CONTROL_H__
#define __FEATURE_CONTROL_H__

#include <linux/kernel.h>

void google_feature_control_init(void);

void google_feature_control_exit(void);

struct dentry *get_radio_debugfs_root(void);

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_CDD)
bool get_cdd_enable_status(void);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
bool get_wakemon_enable_status(void);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
bool get_soc_qos_enable_status(void);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
bool get_lro_size_limit_enable_status(void);
#endif

#endif /* __FEATURE_CONTROL_H__ */
