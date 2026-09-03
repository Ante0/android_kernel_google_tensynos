/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020-2021 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare MIPI DSI 2 host - Core
 *
 * Author: Marcelo Borges <marcelob@synopsys.com>
 * Author: Pedro Correia <correia@synopsys.com>
 * Author: Nuno Cardoso <cardoso@synopsys.com>
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#ifndef __DW_MIPI_DSI2H__
#define __DW_MIPI_DSI2H__

#include <linux/types.h>

#include <drm/drm_modes.h>

struct drm_display_mode;
struct drm_encoder;
struct dw_mipi_dsi2h;
struct mipi_dsi_device;
struct platform_device;
struct mipi_dsi_host;
struct mipi_dsi_msg;
struct drm_bridge;
struct sscd_funcs;

#define MIPI_DSI_SCRAMBLING_MODE_COMMAND		0x27
#define MIPI_DSI_SERVICE_EXTENSIONS_PACKET		0x25
#define MIPI_DSI_PHYSICAL_EVENT_NOTIFICATION_PACKET	0x26

struct dw_mipi_dsi2h_dphy_timing {
	u16 data_hs2lp;
	u16 data_lp2hs;
	u16 clk_hs2lp;
	u16 clk_lp2hs;
};

struct dw_mipi_dsi2h_phy_ops {
	int (*init)(void *priv_data);
	void (*power_on)(void *priv_data);
	void (*power_off)(void *priv_data);
	int (*get_lane_mbps)(void *priv_data,
			     const struct drm_display_mode *mode,
			     unsigned long mode_flags, u32 lanes, u32 format,
			     unsigned int *lane_mbps);
	int (*get_timing)(void *priv_data, unsigned int lane_mbps,
			  struct dw_mipi_dsi2h_dphy_timing *timing);
};

struct dw_mipi_dsi2h_host_ops {
	int (*attach)(void *priv_data,
		      struct mipi_dsi_device *dsi);
	int (*detach)(void *priv_data,
		      struct mipi_dsi_device *dsi);
};

struct dw_mipi_dsi2h_plat_data {
	bool in_emulation;
	unsigned int mux_id;
	unsigned int max_data_lanes;
	unsigned int clk_type;
	bool auto_calc_off;
	bool hs_link_enable;
	unsigned int ppi_width;
	unsigned int high_speed;
	unsigned int bta;
	unsigned int eotp;
	unsigned int tear_effect;
	unsigned int scrambling;
	unsigned int hib_type;
	unsigned int vid_mode_type;
	unsigned int vfp_hs_en;
	unsigned int vbp_hs_en;
	unsigned int vsa_hs_en;
	unsigned int hfp_hs_en;
	unsigned int hbp_hs_en;
	unsigned int hsa_hs_en;
	unsigned int datarate;
	unsigned int lp2hs_time;
	unsigned int hs2lp_time;
	bool dynamic_hs_clk_en;

	unsigned int is_cphy;
	char *phy_name;

	unsigned int ulps_wakeup_time;
	unsigned int index;
	bool encoder_initialized;

	u32 sys_clk; /* APB clk in KHz */
	u32 ipi_clk; /* IPI clk in KHz */
	u32 phy_clk;
	u32 lptx_clk;

	int irq;

	enum drm_mode_status (*mode_valid)(void *priv_data,
					   const struct drm_display_mode *mode);

	const struct dw_mipi_dsi2h_phy_ops *phy_ops;
	const struct dw_mipi_dsi2h_host_ops *host_ops;

	int (*get_resources)(void *priv_data, struct drm_bridge *bridge,
			     struct mipi_dsi_host *dsi_host);
	void *priv_data;
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
struct dw_mipi_dsi2h_debug_ops {
	u32 (*ctrl_read_dbg)(struct dw_mipi_dsi2h *dsi2h, int offset);
	struct drm_display_mode *(*display_export)(struct dw_mipi_dsi2h *dsi2h);
	struct mipi_dsi_device *(*device_export)(struct dw_mipi_dsi2h *dsi2h);
	union phy_configure_opts *(*phy_opts_export)(struct dw_mipi_dsi2h *dsi2h);
	void (*device_import)(struct dw_mipi_dsi2h *dsi2h,
			      struct mipi_dsi_device *dsi_device);
	int (*dsi_reconfigure)(struct dw_mipi_dsi2h *dsi2h);
	struct mipi_dsi_host * (*dsi_return_host)(struct dw_mipi_dsi2h *dsi2h);
	ssize_t (*dsi_write_command)(struct mipi_dsi_host *host,
				     const struct mipi_dsi_msg *msg);
	int (*dsi_pri_request)(struct dw_mipi_dsi2h *dsi2h, u32 type,
			       u64 val1, u64 val2);
	int (*dsi_set_prop)(struct dw_mipi_dsi2h *dsi2h, u32 prop, u64 val);
	int (*dsi2_start_tear_effect)(struct dw_mipi_dsi2h *dsi2h, u32 sw_type,
								   u32 set_tear_on, u32 arg);
	int (*dsi2_finish_tear_effect)(struct dw_mipi_dsi2h *dsi2h);
};

struct dw_mipi_dsi2h_debug_ops *dw_mipi_dsi2h_get_debug_ops(void);
#endif /* DEBUG_FS */

void *dw_mipi_dsi2h_probe(struct platform_device *pdev,
			  const struct dw_mipi_dsi2h_plat_data *plat_data);
int dw_mipi_dsi2h_remove(void *dsi2h);
int dw_mipi_dsi2h_bind(void *dsi2h, struct drm_encoder *encoder, struct sscd_funcs *coredump_funcs);
void dw_mipi_dsi2h_unbind(void *dsi2h);
/**
 * is_dw_mipi_dsi2h_bridge - Check if the bridge is a dw_mipi_dsi2h bridge
 * @bridge: Pointer to the drm_bridge structure
 *
 * Return: true if the bridge is a dw_mipi_dsi2h bridge, false otherwise.
 */
bool is_dw_mipi_dsi2h_bridge(struct drm_bridge *bridge);
/**
 * dw_mipi_dsi2h_get_device - Get the device associated with the bridge
 * @bridge: Pointer to the drm_bridge structure
 *
 * Return: A pointer to the device structure associated with the dsi2h bridge
 */
struct device *dw_mipi_dsi2h_get_device(struct drm_bridge *bridge);
int dw_mipi_dsi2h_suspend(void *dsi2h);
int dw_mipi_dsi2h_resume(void *dsi2h);

#endif /* __DW_MIPI_DSI2H__ */
