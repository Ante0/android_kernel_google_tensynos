/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Google LLC
 */
#ifndef _VS_MODULE_PARAMS_H_
#define _VS_MODULE_PARAMS_H_

/**
 * enum coredump_source - enum of possible trigger types for subsystem coredump
 *
 * @SSCD_SRC_MANUAL: Manual coredump trigger (via debugfs) for testing
 * @SSCD_SRC_FRAME_UPDATE_TIMEOUT: Timeout waiting for frame start or frame done signal.
 *				   Only triggers timeouts during normal frame timeline.
 * @SSCD_SRC_DISABLE_TIMEOUT: Triggers on frame done timeouts during pipeline disable.
 * @SSCD_SRC_GRAM_COLLISION: Trigger when GRAM collision is detected by panel.
 * @SSCD_SRC_DSI_ERR: Trigger when fatal error bits flagged in DSI transmission,
 *		      specifically when reported by DSI host.
 * @SSCD_SRC_DDIC_ERR: Trigger when fatal error bits flagged in DSI transmission,
 *		       specifically when reported by panel DDIC.
 * @SSCD_SRC_PMIC_ERR: Trigger when panel PMIC reports hardware faults.
 *
 * User can change which errors lead to coredumps or recovery by way of module parameter.
 */
enum coredump_source {
	SSCD_SRC_MANUAL = 0,
	SSCD_SRC_FRAME_UPDATE_TIMEOUT,
	SSCD_SRC_DISABLE_TIMEOUT,
	SSCD_SRC_GRAM_COLLISION,
	SSCD_SRC_DSI_ERR,
	SSCD_SRC_DDIC_ERR,
	SSCD_SRC_PMIC_ERR,
	/** @SSCD_SRC_MAX: bounding enumerator; add new entries before this */
	SSCD_SRC_MAX,
};

/* Accessors */

/**
 * recovery_source_enabled() - Does module allow given source to trigger recovery
 * @source: error source type
 *
 * Return: true if module param allows source to trigger recovery, else false
 */
bool recovery_source_enabled(enum coredump_source source);

/**
 * coredump_source_enabled() - Does module allow given source to trigger coredump
 * @source: error source type
 *
 * Return: true if module param allows source to trigger coredump, else false
 */
bool coredump_source_enabled(enum coredump_source source);

/**
 * is_crtc_recovery_enabled() - Whether crtc recovery enabled overall
 *
 * Return: true if crtc recovery enabled, false otherwise
 */
bool is_crtc_recovery_enabled(void);

/**
 * is_coredump_enabled() - Whether coredump enabled overall
 *
 * Return: true if coredump enabled, false otherwise
 */
bool is_coredump_enabled(void);

/**
 * is_urgent_enabled() - Whether HW QoS urgent feature enabled
 *
 * Return: true if HW QoS urgent feature enabled, false otherwise
 */
bool is_urgent_enabled(void);

#endif // _VS_MODULE_PARAMS_H_
