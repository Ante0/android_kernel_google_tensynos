// SPDX-License-Identifier: GPL
/*
 * GIM DRM for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/version.h>
#include "gim_drm.h"

#undef pr_fmt
#define pr_fmt(fmt) "gti: drm: " fmt
#undef dev_fmt
#define dev_fmt(fmt) "gti: " fmt

static enum goog_display_state goog_display_state;
static struct goog_display_core display_cores[MAX_INTERFACE_DEVICES];

static void goog_notify_display_state(struct goog_display_core *display_core, bool screen_on)
{
	int i;
	struct goog_interface *interface = display_core->interface;
	unsigned long event_id = screen_on ? DISPLAY_EVENT_ID_SCREEN_ON :
					     DISPLAY_EVENT_ID_SCREEN_OFF;
	long state_mask;

	if (!display_core || !interface) {
		pr_warn("invalid display_core or interface\n");
		return;
	}

	/* Update global goog_display_state for all clients */
	if (interface->dev_id)
		state_mask = DISPLAY_STATE_BIT_MASK_PANEL1;
	else
		state_mask = DISPLAY_STATE_BIT_MASK_PANEL0;

	if (screen_on)
		set_bit(state_mask, &goog_display_state);
	else
		clear_bit(state_mask, &goog_display_state);

	/*
	 * Notify client(s) under one of the following conditions:
	 * 1. The client is associated with the `drm_bridge` for the current display notification.
	 * 2. The client has no `drm_bridge` association.
	 */
	for (i = 0; i < ARRAY_SIZE(display_cores); i++) {
		if (display_cores[i].bridge) {
			if (display_cores[i].bridge != display_core->bridge)
				continue;
			if (!display_cores[i].state_notifier_head.head) {
				pr_info("%s %s before state notifier registered\n", interface->name,
					(screen_on) ? "screen-on" : "screen-off");
				continue;
			}
		} else {
			if (!display_cores[i].state_notifier_head.head)
				continue;
		}
		blocking_notifier_call_chain(&display_cores[i].state_notifier_head, event_id,
					     &display_cores[i]);
	}
}

static struct drm_connector *get_bridge_connector(struct drm_bridge *bridge)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(bridge->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->encoder == bridge->encoder)
			break;
	}
	drm_connector_list_iter_end(&conn_iter);
	return connector;
}

static int connector_notifier_call(struct notifier_block *nb, unsigned long id, void *data)
{
	struct goog_display_core *display_core = (struct goog_display_core *)container_of(
		nb, struct goog_display_core, connector_notifier);
	struct drm_bridge *bridge = display_core->bridge;

	if (!display_core->connector || !display_core->connector->state)
		display_core->connector = get_bridge_connector(bridge);

	if (!display_core->connector)
		return 0;

#if IS_ENABLED(CONFIG_GS_DRM_PANEL_UNIFIED)
	if (is_gs_drm_connector(display_core->connector) && (id == GS_PANEL_NOTIFIER_SET_OP_HZ))
		pr_debug("NOTIFIER_SET_OP_HZ: %d\n", *(unsigned int *)data);
#endif

	return 0;
}

static int panel_bridge_attach(struct drm_bridge *bridge, enum drm_bridge_attach_flags flags)
{
	struct goog_display_core *display_core = bridge->driver_private;

	if (display_core->connector_notifier_enabled) {
		if (!display_core->connector || !display_core->connector->state)
			display_core->connector = get_bridge_connector(bridge);

		if (!display_core->connector) {
			pr_warn("can't get panel connector to register notification!\n");
			return 0;
		}

		display_core->connector_notifier.notifier_call = connector_notifier_call;
#if IS_ENABLED(CONFIG_GS_DRM_PANEL_UNIFIED)
		if (is_gs_drm_connector(display_core->connector))
			gs_connector_register_op_hz_notifier(display_core->connector,
							     &display_core->connector_notifier);
#endif
	}

	return 0;
}

static void panel_bridge_detach(struct drm_bridge *bridge)
{
	struct goog_display_core *display_core = bridge->driver_private;

	if (display_core->connector_notifier_enabled) {
		if (!display_core->connector || !display_core->connector->state)
			display_core->connector = get_bridge_connector(bridge);

		if (!display_core->connector)
			return;

#if IS_ENABLED(CONFIG_GS_DRM_PANEL_UNIFIED)
		if (is_gs_drm_connector(display_core->connector))
			gs_connector_unregister_op_hz_notifier(display_core->connector,
							       &display_core->connector_notifier);
#endif
	}
}

static void panel_bridge_atomic_enable_internal(struct drm_bridge *bridge)
{
	struct goog_display_core *display_core = bridge->driver_private;

	if (!display_core->interface)
		return;
	if (display_core->aod_mode != AOD_MODE_DISABLED) {
		pr_debug("%s skip screen-on because of aod_mode = %d!\n",
			 display_core->interface->name, display_core->aod_mode);
		return;
	}

	goog_notify_display_state(display_core, true);
}

static void panel_bridge_atomic_disable(struct drm_bridge *bridge,
					__always_unused struct drm_bridge_state *old_state)
{
	struct goog_display_core *display_core = bridge->driver_private;

	if (!display_core->interface)
		return;
	if (bridge->encoder && bridge->encoder->crtc) {
		const struct drm_crtc_state *crtc_state = bridge->encoder->crtc->state;

		if (drm_atomic_crtc_effectively_active(crtc_state))
			return;
	}

	goog_notify_display_state(display_core, false);
}

static bool panel_bridge_is_lp_mode(struct drm_connector *connector)
{
	if (connector && connector->state) {
#if IS_ENABLED(CONFIG_GS_DRM_PANEL_UNIFIED)
		if (is_gs_drm_connector(connector)) {
			struct gs_drm_connector_state *s = to_gs_connector_state(connector->state);

			return s->gs_mode.is_lp_mode;
		}
#endif
	}
	return false;
}

static bool panel_bridge_is_mp_mode(struct drm_connector_state *conn_state)
{
#if IS_ENABLED(CONFIG_GS_DRM_PANEL_UNIFIED)
	if (conn_state) {
		struct gs_drm_connector_state *s = to_gs_connector_state(conn_state);

		return s->panel_power_state == GS_PANEL_POWER_STATE_MP;
	}
#endif
	return false;
}

static bool panel_bridge_has_mp_mode_update(struct drm_connector_state *conn_state)
{
#if IS_ENABLED(CONFIG_GS_DRM_PANEL_UNIFIED)
	if (conn_state) {
		struct gs_drm_connector_state *s = to_gs_connector_state(conn_state);

		return s->pending_update_flags & GS_FLAG_POWER_STATE_UPDATE;
	}
#endif
	return false;
}

static void panel_bridge_mode_set(struct drm_bridge *bridge, const struct drm_display_mode *mode,
				  const struct drm_display_mode *adjusted_mode)
{
	bool panel_is_lp_mode;
	struct goog_display_core *display_core = bridge->driver_private;

	if (!display_core->interface)
		return;

	if (!display_core->connector || !display_core->connector->state)
		display_core->connector = get_bridge_connector(bridge);

	panel_is_lp_mode = panel_bridge_is_lp_mode(display_core->connector);
	if ((display_core->aod_mode == AOD_MODE_LP) != panel_is_lp_mode) {
		pr_info("%s %s AOD_MODE_LP from aod_mode 0x%x.\n", display_core->interface->name,
			panel_is_lp_mode ? "enter" : "exit", display_core->aod_mode);

		display_core->aod_mode = panel_is_lp_mode ? AOD_MODE_LP : AOD_MODE_DISABLED;

		if (panel_is_lp_mode)
			goog_notify_display_state(display_core, false);
		else
			goog_notify_display_state(display_core, true);
	}

	if (mode) {
		int vrefresh = drm_mode_vrefresh(mode);

		if (display_core->vrefresh != vrefresh) {
			pr_debug("%s vrefresh(Hz) changed to %d from %d.\n",
				 display_core->interface->name, vrefresh, display_core->vrefresh);
			display_core->vrefresh = vrefresh;
		}
	}
}

static void panel_bridge_atomic_enable(struct drm_bridge *bridge,
				       __always_unused struct drm_bridge_state *old_state)
{
	struct goog_display_core *display_core = bridge->driver_private;
	struct drm_connector *connector;
	struct drm_crtc_state *crtc_state;

	if (!IS_ENABLED(CONFIG_GOOGLE_DRM_BRIDGE_MODE_SET)) {
		if (!display_core->connector || !display_core->connector->state)
			display_core->connector = get_bridge_connector(bridge);

		connector = display_core->connector;
		if (connector && connector->state && connector->state->crtc) {
			crtc_state = connector->state->crtc->state;
			panel_bridge_mode_set(bridge, &crtc_state->mode,
					      &crtc_state->adjusted_mode);
		}
	}

	panel_bridge_atomic_enable_internal(bridge);
}

static int panel_bridge_atomic_check(struct drm_bridge *bridge,
				     struct drm_bridge_state *bridge_state,
				     struct drm_crtc_state *new_crtc_state,
				     struct drm_connector_state *conn_state)
{
	bool panel_is_mp_mode;
	struct goog_display_core *display_core = bridge->driver_private;

	if (!display_core->interface)
		return 0;

	if (panel_bridge_has_mp_mode_update(conn_state)) {
		panel_is_mp_mode = panel_bridge_is_mp_mode(conn_state);
		if ((display_core->aod_mode == AOD_MODE_MP) != panel_is_mp_mode) {
			pr_info("%s %s AOD_MODE_MP from aod_mode 0x%x.\n",
				display_core->interface->name, panel_is_mp_mode ? "enter" : "exit",
				display_core->aod_mode);

			display_core->aod_mode = panel_is_mp_mode ? AOD_MODE_MP : AOD_MODE_DISABLED;

			if (panel_is_mp_mode)
				goog_notify_display_state(display_core, false);
			else
				goog_notify_display_state(display_core, true);
		}
	}

	return 0;
}

static const struct drm_bridge_funcs panel_bridge_funcs = {
	.attach = panel_bridge_attach,
	.detach = panel_bridge_detach,
	.atomic_enable = panel_bridge_atomic_enable,
	.atomic_disable = panel_bridge_atomic_disable,
#if IS_ENABLED(CONFIG_GOOGLE_DRM_BRIDGE_MODE_SET)
	.mode_set = panel_bridge_mode_set,
#endif
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_check = panel_bridge_atomic_check,
};

void gim_register_panel_bridge(struct goog_interface_manager *gim)
{
	u8 i;
	struct device_node *dn;
	struct drm_bridge *bridge;

	if (!gim)
		return;
	for (i = 0; i < ARRAY_SIZE(gim->interfaces) && i < ARRAY_SIZE(display_cores); i++) {
		if (!gim->interfaces[i].context || !gim->interfaces[i].vendor_dev_node)
			continue;

		display_cores[i].interface = &gim->interfaces[i];
		display_cores[i].state = &goog_display_state;
		display_cores[i].vendor_dev_node = gim->interfaces[i].vendor_dev_node;
		BLOCKING_INIT_NOTIFIER_HEAD(&display_cores[i].state_notifier_head);
		if (gim->interfaces[i].dev_id == 1)
			set_bit(DISPLAY_STATE_BIT_MASK_DUAL_PANEL_FLAG, &goog_display_state);
		/*
		 * Only support primary and secondary panel to associate with struct
		 * drm_bridge, but still needs to configure the display state mask above
		 * for client(s) with no struct drm_bridge association for notifier.
		 */
		if (gim->interfaces[i].dev_id >= 2 ||
		    gim->interfaces[i].type != GOOG_INTERFACE_TYPE_TOUCH) {
			continue;
		}
		dn = gim->interfaces[i].vendor_dev_node;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0) //refer to pa/3446481
		bridge = __devm_drm_bridge_alloc(gim->dev, sizeof(struct drm_bridge), 0,
						 &panel_bridge_funcs);
#else
		bridge = devm_kzalloc(gim->dev, sizeof(struct drm_bridge), GFP_KERNEL);
		if (bridge)
			bridge->funcs = &panel_bridge_funcs;
#endif
		if (!bridge)
			continue;
		bridge->driver_private = &display_cores[i];
#ifdef CONFIG_OF
		bridge->of_node = dn;
#endif
		display_cores[i].bridge = bridge;
		display_cores[i].connector_notifier_enabled =
			of_property_read_bool(dn, "goog,panel-notifier-enabled");
		drm_bridge_add(bridge);
		pr_debug("%s for %s", __func__, display_cores[i].interface->name);
	}
}
EXPORT_SYMBOL_GPL(gim_register_panel_bridge);

void gim_unregister_panel_bridge(struct goog_interface_manager *gim)
{
	u8 i;
	struct drm_bridge *bridge;
	struct drm_bridge *node;

	for (i = 0; i < ARRAY_SIZE(display_cores); i++) {
		if (!display_cores[i].bridge || !display_cores[i].interface)
			continue;

		bridge = display_cores[i].bridge;
		drm_bridge_remove(bridge);
		pr_info("%s for %s", __func__, display_cores[i].interface->name);
		if (!bridge->dev) /* not attached */
			continue;

		drm_modeset_lock(&bridge->dev->mode_config.connection_mutex, NULL);
		list_for_each_entry(node, &bridge->encoder->bridge_chain, chain_node) {
			if (node == bridge) {
				if (bridge->funcs->detach)
					bridge->funcs->detach(bridge);
				list_del(&bridge->chain_node);
				break;
			}
		}
		drm_modeset_unlock(&bridge->dev->mode_config.connection_mutex);
		bridge->dev = NULL;
	}
}
EXPORT_SYMBOL_GPL(gim_unregister_panel_bridge);

int gim_register_display_state_notifier(struct device *vendor_dev, struct notifier_block *nb)
{
	int i;

	if (!vendor_dev || !nb)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(display_cores); i++) {
		if (display_cores[i].interface && display_cores[i].interface->vendor_dev_node &&
		    device_match_of_node(vendor_dev, display_cores[i].interface->vendor_dev_node)) {
			pr_info("register display state notifier for %s\n",
				display_cores[i].interface->name);
			return blocking_notifier_chain_register(
				&display_cores[i].state_notifier_head, nb);
		}
	}

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(gim_register_display_state_notifier);

int gim_unregister_display_state_notifier(struct device *vendor_dev, struct notifier_block *nb)
{
	int i;

	if (!vendor_dev || !nb)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(display_cores); i++) {
		if (display_cores[i].interface && display_cores[i].interface->vendor_dev_node &&
		    device_match_of_node(vendor_dev, display_cores[i].interface->vendor_dev_node)) {
			return blocking_notifier_chain_unregister(
				&display_cores[i].state_notifier_head, nb);
		}
	}

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(gim_unregister_display_state_notifier);
