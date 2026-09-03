/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VS_RECOVERY_H_
#define _VS_RECOVERY_H_

#include <linux/types.h>
#include <linux/workqueue.h>

struct vs_crtc;
struct vs_crtc_state;

/**
 * struct vs_recovery - Information relating to ESD recovery
 * @work: reference to handler for recovery operation
 * @count: how many times recovery has triggered
 * @recovering: whether recovery operation currently active
 * @pending_recovery_srcs: atomic bitmask of pending trigger sources
 */
struct vs_recovery {
	struct work_struct work;
	int count;
	atomic_t recovering;
	atomic_t pending_recovery_srcs;
};

/**
 * vs_recovery_register() - registers recovery handler
 * @vs_crtc: crtc object to attach recovery to
 */
void vs_recovery_register(struct vs_crtc *vs_crtc);

/**
 * vs_crtc_trigger_recovery() - Triggers crtc recovery
 * @vs_crtc: crtc object to trigger recovery for
 * @srcs: bitmap of trigger sources causing this recovery
 */
void vs_crtc_trigger_recovery(struct vs_crtc *vs_crtc, u32 srcs);

/**
 * vs_execute_recovery_or_coredump_if_needed() - Evaluate errors and trigger recovery/coredump
 * @vs_crtc_state: current crtc state
 * @panel_errors: u64 representation of panel error bitmap
 * @dsi_errors: u64 representation of DSI error bitmap
 * @pmic_errors: u64 representation of PMIC error bitmap
 * @srcs: bitmask of all sources triggering the potential coredump/recovery
 */
void vs_execute_recovery_or_coredump_if_needed(struct vs_crtc_state *vs_crtc_state,
					       u64 panel_errors,
					       u64 dsi_errors,
					       u64 pmic_errors,
					       u32 srcs);

#endif // _VS_RECOVERY_H_
