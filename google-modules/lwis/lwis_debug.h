/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS Debug Utilities
 *
 * Copyright (c) 2020 Google, LLC
 */

#ifndef LWIS_DEBUG_H_
#define LWIS_DEBUG_H_

#include "lwis_device.h"

extern int lwis_debug_feature_mask;

/*
 * enum lwis_enable_feature_debug
 * This defines the features that we enable for detailed logging and tracing.
 * PERF_TRACING: Enables logs related to LWIS performance metrics.
 */
enum lwis_enable_feature_debug { PERF_TRACING = 0x1 };

/* Macro to enable feature level debugging logs */
#define LWIS_FEATURE_LOG(dev, feature, fmt, ...)                                                   \
	({                                                                                         \
		if (feature & lwis_debug_feature_mask) {                                           \
			dev_info(dev, fmt, ##__VA_ARGS__);                                         \
		}                                                                                  \
	})

/* Functions to print debugging info */
int lwis_debug_print_device_info(struct lwis_device *lwis_dev);
int lwis_debug_print_event_states_info(struct lwis_device *lwis_dev, int lwis_event_dump_cnt);
int lwis_debug_print_transaction_info(struct lwis_device *lwis_dev);
int lwis_debug_print_register_io_history(struct lwis_device *lwis_dev);
int lwis_debug_print_buffer_info(struct lwis_device *lwis_dev);

/*
 * lwis_debug_crash_info_dump:
 * Use the customized function handle to print information from each device registered in LWIS
 * when usersapce crash.
 */
void lwis_debug_crash_info_dump(struct lwis_device *lwis_dev);

/* DebugFS specific functions */
int lwis_device_debugfs_setup(struct lwis_device *lwis_dev, struct dentry *dbg_root);
int lwis_device_debugfs_cleanup(struct lwis_device *lwis_dev);

#endif /* LWIS_DEBUG_H_ */
