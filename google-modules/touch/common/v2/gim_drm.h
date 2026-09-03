/* SPDX-License-Identifier: GPL */
/*
 * GIM DRM for Pixel.
 *
 * Copyright 2025 Google LLC.
 */
#ifndef __GIM_DRM_H___
#define __GIM_DRM_H___

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_panel.h>
#include <linux/notifier.h>

#if IS_ENABLED(CONFIG_GS_DRM_PANEL_UNIFIED)
#include <gs_drm/gs_drm_connector.h>
#endif

#include "goog_interface_manager.h"

struct goog_interface_manager;

#ifndef __CODESONAR__
#define ENUM_UL : unsigned long
#define ENUM_U8 : u8
#else
#define ENUM_UL
#define ENUM_U8
#endif

enum aod_mode ENUM_U8 {
	AOD_MODE_DISABLED = 0,
	AOD_MODE_LP,
	AOD_MODE_MP,
};

/*
 * The event ID for the `notifier_block` callback, registered via
 * `gim_register_display_state_notifier()`. This event triggers for each
 * individual DRM bridge change and does not represent the complete state
 * in a dual-panel setup. To get the overall display state, inspect the
 * `state` member of the `goog_display_core` structure.
 */
enum goog_display_event_id ENUM_UL {
	DISPLAY_EVENT_ID_SCREEN_OFF = 0,
	DISPLAY_EVENT_ID_SCREEN_ON = 1,
	DISPLAY_EVENT_ID_AOD_MODE = 2,
	DISPLAY_EVENT_ID_VREFRESH = 3,
};

enum goog_display_state_bit_mask ENUM_UL {
	DISPLAY_STATE_BIT_MASK_PANEL0 = 0,
	DISPLAY_STATE_BIT_MASK_PANEL1 = 4,
	DISPLAY_STATE_BIT_MASK_DUAL_PANEL_FLAG = 28,
	DISPLAY_STATE_BIT_MASK_MAX = BITS_PER_LONG,
};

enum goog_display_state ENUM_UL {
	/* single panel */
	DISPLAY_STATE_SCREEN_OFF = 0x00000000,
	DISPLAY_STATE_SCREEN_ON = 0x00000001,
	/* dual panel */
	DISPLAY_STATE_PANEL1_OFF_PANEL0_OFF = 0x10000000,
	DISPLAY_STATE_PANEL1_OFF_PANEL0_ON = 0x10000001,
	DISPLAY_STATE_PANEL1_ON_PANEL0_OFF = 0x10000010,
	DISPLAY_STATE_PANEL1_ON_PANEL0_ON = 0x10000011,
};

#undef ENUM_UL
#undef ENUM_U8

/*
 * struct goog_display_core - Google Panel Core for Pixel.
 * @connector_notifier_enabled: enable flag for receiving panel notifications.
 * @aod_mode: display is in LP/MP aod mode.
 * @state: enum goog_display_state for this display core.
 * @vrefresh: the display vrefresh rate in Hz.
 * @vendor_dev_node: the vendor device of node.
 * @bridge: struct that used to register panel bridge notification.
 * @connector: struct that used to get panel status.
 * @connector_notifier: notifier from display panel updates.
 * @state_notifier_head: notifier for client to register.
 * @interface: struct goog_interface that bind with panel.
 */
struct goog_display_core {
	bool connector_notifier_enabled;
	enum aod_mode aod_mode;
	const enum goog_display_state *state;
	int vrefresh;
	struct device_node *vendor_dev_node;
	struct drm_bridge *bridge;
	struct drm_connector *connector;
	struct notifier_block connector_notifier;
	struct blocking_notifier_head state_notifier_head;
	struct goog_interface *interface;
};

void gim_register_panel_bridge(struct goog_interface_manager *gim);
void gim_unregister_panel_bridge(struct goog_interface_manager *gim);
int gim_register_display_state_notifier(struct device *vendor_dev, struct notifier_block *nb);
int gim_unregister_display_state_notifier(struct device *vendor_dev, struct notifier_block *nb);

#endif /* __GIM_DRM_H___ */
