/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef _GS_FAULT_EVENT_H_
#define _GS_FAULT_EVENT_H_

#include <linux/device.h>

/**
 * enum gs_fault_event_type - the type of fault event
 * @GS_FAULT_EVENT_TYPE_PANEL_ERROR: A panel error
 * @GS_FAULT_EVENT_TYPE_DSI_ERROR: A DSI error
 * @GS_FAULT_EVENT_TYPE_FRAME_START_TIMEOUT: A frame start timeout
 * @GS_FAULT_EVENT_TYPE_FRAME_DONE_TIMEOUT: A frame done timeout
 * @GS_FAULT_EVENT_TYPE_DPU_UNDERRUN: A DPU underrun
 * @GS_FAULT_EVENT_TYPE_DDIC_UNDERRUN: A DDIC underrun
 */
enum gs_fault_event_type {
	GS_FAULT_EVENT_TYPE_PANEL_ERROR = 0,
	GS_FAULT_EVENT_TYPE_DSI_ERROR,
	GS_FAULT_EVENT_TYPE_FRAME_START_TIMEOUT,
	GS_FAULT_EVENT_TYPE_FRAME_DONE_TIMEOUT,
	GS_FAULT_EVENT_TYPE_DPU_UNDERRUN,
	GS_FAULT_EVENT_TYPE_DDIC_UNDERRUN,
	GS_FAULT_EVENT_TYPE_PMIC_ERROR,
	GS_FAULT_EVENT_TYPE_MAX,
};

/**
 * struct gs_fault_work_data - Data for a fault event work item
 * @work: The work_struct that gets queued.
 * @dev: The device associated with the fault.
 * @type: The type of fault event.
 * @value: A value associated with the fault.
 *
 * This struct is used to hold the necessary information for a fault event
 * that will be emitted as a uevent from a workqueue.
 */
struct gs_fault_work_data {
	struct work_struct work;
	struct device *dev;
	enum gs_fault_event_type type;
	u64 value;
};

void gs_fault_event_emit(struct device *dev, enum gs_fault_event_type type, u64 value);
void non_blocking_gs_fault_event_emit(struct device *dev, enum gs_fault_event_type type, u64 value);
#endif /* _GS_FAULT_EVENT_H_ */
