// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based ea8182-f10 AMOLED LCD panel driver.
 *
 * Copyright (c) 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_vblank.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "panel/panel-samsung-drv.h"

#define EA8182_F10_WRCTRLD_DIMMING_BIT    0x08
#define EA8182_F10_WRCTRLD_BCTRL_BIT      0x20
#define EA8182_F10_WRCTRLD_HBM_BIT        0xE0

#define VLIN1_CMD_SIZE  2
#define VGH_CMD_SIZE    4
#define VREG_CMD_SIZE  11
/**
 * struct ea8182_f10_panel - panel specific runtime info
 *
 * This struct maintains ea8182_f10 panel specific runtime info, any fixed details about panel
 * should most likely go into struct exynos_panel_desc
 */
struct ea8182_f10_panel {
	/** @base: base panel struct */
	struct exynos_panel base;

	/** @panel_voltage: panel default voltage  */
	struct panel_voltage {
		u8 vlin1_default[VLIN1_CMD_SIZE];
		u8 vgh_default[VGH_CMD_SIZE];
		u8 vreg_default[VREG_CMD_SIZE];
		u8 vreg_offset[VREG_CMD_SIZE];
	} panel_voltage;
	/**
	 * @is_pixel_off: pixel-off command is sent to panel. Only sending normal-on or resetting
	 *			panel can recover to normal mode after entering pixel-off state.
	 */
	bool is_pixel_off;
};

#define to_spanel(ctx) container_of(ctx, struct ea8182_f10_panel, base)

static const u8 pps_setting[] = {
	0x9E, 0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x08, 0x2C,
	0x04, 0x38, 0x02, 0x0B, 0x02, 0x1C, 0x02, 0x1C,
	0x02, 0x00, 0x02, 0x0E, 0x00, 0x20, 0x32, 0x90,
	0x00, 0x07, 0x00, 0x0C, 0x00, 0x30, 0x00, 0x32,
	0x18, 0x00, 0x10, 0xF0, 0x03, 0x0C, 0x20, 0x00,
	0x06, 0x0B, 0x0B, 0x33, 0x0E, 0x1C, 0x2A, 0x38,
	0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7B,
	0x7D, 0x7E, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40,
	0x09, 0xBE, 0x19, 0xFC, 0x19, 0xFA, 0x19, 0xF8,
	0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xF6,
	0x2B, 0x34, 0x2B, 0x74, 0x3B, 0x74, 0x6B, 0xF4,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const u8 display_off[] = { MIPI_DCS_SET_DISPLAY_OFF };
static const u8 display_on[] = { MIPI_DCS_SET_DISPLAY_ON };
static const u8 sleep_in[] = { MIPI_DCS_ENTER_SLEEP_MODE };
static const u8 sleep_out[] = { MIPI_DCS_EXIT_SLEEP_MODE };
static const u8 unlock_cmd_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 lock_cmd_f0[] = { 0xF0, 0xA5, 0xA5 };
static const u8 vlin1_7v9[] = { 0xE7, 0x01 };
static const u8 vgh_7v4[] = { 0xE3, 0x12, 0x12, 0x12 };
static const u8 pixel_off[] = { 0x22 };
static const u8 normal_on[] = { 0x13 };

static const struct exynos_dsi_cmd ea8182_f10_sleep_in_cmds[] = {
	EXYNOS_DSI_CMD(sleep_in, 130),
};
static DEFINE_EXYNOS_CMD_SET(ea8182_f10_sleep_in);

static const struct exynos_dsi_cmd ea8182_f10_lp_cmds[] = {
	EXYNOS_DSI_CMD(display_off, 0),

};
static DEFINE_EXYNOS_CMD_SET(ea8182_f10_lp);

static const struct exynos_dsi_cmd ea8182_f10_lp_off_cmds[] = {
	EXYNOS_DSI_CMD(display_off, 0),
};


static const struct exynos_dsi_cmd ea8182_f10_lp_low_cmds[] = {
	EXYNOS_DSI_CMD0_REV(unlock_cmd_f0, PANEL_REV_LT(PANEL_REV_DVT1)),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_EVT1), 0xC3, 0x01),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_EVT1|PANEL_REV_EVT1_1, 0xB0, 0xBC),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_EVT1|PANEL_REV_EVT1_1, 0xB7, 0x12, 0x06, 0xBC, 0x01, 0x00),
	EXYNOS_DSI_CMD0_REV(lock_cmd_f0, PANEL_REV_LT(PANEL_REV_DVT1)),
	EXYNOS_DSI_CMD_SEQ_DELAY_REV(PANEL_REV_LT(PANEL_REV_EVT1), 34, 0x53, 0x25),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_EVT1), 0x53, 0x24),
	EXYNOS_DSI_CMD_SEQ_DELAY_REV(PANEL_REV_EVT1|PANEL_REV_EVT1_1, 34, 0x51, 0x07, 0xFF),
	EXYNOS_DSI_CMD_SEQ_DELAY_REV(PANEL_REV_GE(PANEL_REV_DVT1), 34, 0x51, 0x00, 0x1A),

	EXYNOS_DSI_CMD(display_on, 0),
};

static const struct exynos_dsi_cmd ea8182_f10_lp_high_cmds[] = {
	EXYNOS_DSI_CMD0_REV(unlock_cmd_f0, PANEL_REV_LT(PANEL_REV_DVT1)),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_LT(PANEL_REV_EVT1), 0xC3, 0x01),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_EVT1|PANEL_REV_EVT1_1, 0xB0, 0xBC),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_EVT1|PANEL_REV_EVT1_1, 0xB7, 0x02, 0x06, 0xBC, 0x01, 0x00),
	EXYNOS_DSI_CMD0_REV(lock_cmd_f0, PANEL_REV_LT(PANEL_REV_DVT1)),
	EXYNOS_DSI_CMD_SEQ_DELAY_REV(PANEL_REV_LT(PANEL_REV_EVT1), 34, 0x53, 0x24),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_EVT1), 0x53, 0x24),
	EXYNOS_DSI_CMD_SEQ_DELAY_REV(PANEL_REV_GE(PANEL_REV_EVT1), 34, 0x51, 0x07, 0xFF),

	EXYNOS_DSI_CMD(display_on, 0),
};

static const struct exynos_binned_lp ea8182_f10_binned_lp[] = {
	BINNED_LP_MODE("off", 0, ea8182_f10_lp_off_cmds),
	/* rising time = delay = 0, falling time = delay + width = 0 + 16 */
	BINNED_LP_MODE_TIMING("low", 80, ea8182_f10_lp_low_cmds, 0, 0 + 16),
	BINNED_LP_MODE_TIMING("high", 2047, ea8182_f10_lp_high_cmds, 0, 0 + 16)
};


static const struct exynos_dsi_cmd ea8182_f10_init_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(0x35), /* TE on */

	/* TE2 Setting */
	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x15),
	EXYNOS_DSI_CMD_SEQ(0xE2, 0x03),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x1A),
	EXYNOS_DSI_CMD_SEQ(0xE2, 0x00, 0x0B, 0x01, 0x0A),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1, 0xB0, 0x70),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_PROTO1, 0xB7, 0x17, 0x77), /* WRDISBV TH setting */
	EXYNOS_DSI_CMD0(lock_cmd_f0),

	EXYNOS_DSI_CMD_SEQ(0x2A, 0x00, 0x00, 0x04, 0x37), /* CASET */
	EXYNOS_DSI_CMD_SEQ(0x2B, 0x00, 0x00, 0x08, 0x2B), /* PASET */
};
static DEFINE_EXYNOS_CMD_SET(ea8182_f10_init);

static void ea8182_f10_change_frequency(struct exynos_panel *ctx,
				     unsigned int vrefresh)
{
	if (!ctx || (vrefresh != 30 && vrefresh != 60 && vrefresh != 120))
		return;

	/* We need to set the default 60Hz setting before going into AoD */
	if (vrefresh == 30) {
		if (ctx->panel_rev >= PANEL_REV_DVT1) {
			vrefresh = 60;
			dev_dbg(ctx->dev, "%s: set to default refresh rate\n", __func__);
		} else {
			return;
		}
	}

	EXYNOS_DCS_WRITE_SEQ(ctx, 0x60, (vrefresh == 120) ? 0x08 : 0x00, 0x00);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xEB, 0x14, 0x00);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xF7, 0x07);

	if (ctx->panel_rev >= PANEL_REV_DVT1) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x16);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xE2, 0x08, 0x48, 0x00,
					(vrefresh == 120) ? 0x30 : 0x1C);
	}

	dev_dbg(ctx->dev, "%s: change to %uhz\n", __func__, vrefresh);
}

static void ea8182_f10_update_wrctrld(struct exynos_panel *ctx)
{
	u8 val = EA8182_F10_WRCTRLD_BCTRL_BIT;

	if (IS_HBM_ON(ctx->hbm_mode))
		val |= EA8182_F10_WRCTRLD_HBM_BIT;

	if (ctx->dimming_on)
		val |= EA8182_F10_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev, "%s(wrctrld:0x%x, hbm: %s, dimming: %s)\n",
		__func__, val, IS_HBM_ON(ctx->hbm_mode) ? "on" : "off",
		ctx->dimming_on ? "on" : "off");

	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);

	/* TODO: need to perform gamma updates */
}

static void ea8182_f10_set_nolp_mode(struct exynos_panel *ctx,
				  const struct exynos_panel_mode *pmode)
{
	unsigned int vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 delay_us = mult_frac(1000, 1020, vrefresh);

	if (!ctx->enabled)
		return;

	EXYNOS_DCS_WRITE_TABLE(ctx, display_off);
	EXYNOS_DCS_WRITE_TABLE(ctx, unlock_cmd_f0);

	if ((ctx->panel_rev >= PANEL_REV_DVT1) && vrefresh == 60) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x04);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xEE, 0x83);
	}

	ea8182_f10_change_frequency(ctx, vrefresh);

	if (ctx->panel_rev <= PANEL_REV_PROTO1_1)
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xC3, 0x02);

	if ((ctx->panel_rev >= PANEL_REV_EVT1) && (ctx->panel_rev < PANEL_REV_DVT1)) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0xBC);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB7, 0x02);
	}

	EXYNOS_DCS_WRITE_TABLE(ctx, lock_cmd_f0);
	ea8182_f10_update_wrctrld(ctx);
	usleep_range(delay_us, delay_us + 10);
	EXYNOS_DCS_WRITE_TABLE(ctx, display_on);

	dev_info(ctx->dev, "exit LP mode\n");
}

static void ea8182_f10_set_default_voltage(struct exynos_panel *ctx, bool enable)
{
	struct ea8182_f10_panel *spanel = to_spanel(ctx);
	const u8 *vlin1 = spanel->panel_voltage.vlin1_default;
	const u8 *vgh = spanel->panel_voltage.vgh_default;
	const u8 *vreg = spanel->panel_voltage.vreg_default;
	const u8 *vreg_offset = spanel->panel_voltage.vreg_offset;

	if (ctx->panel_rev < PANEL_REV_DVT1)
		return;

	dev_dbg(ctx->dev, "%s enable = %d\n", __func__, enable);
	EXYNOS_DCS_WRITE_TABLE(ctx, unlock_cmd_f0);

	if (enable) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x09);
		exynos_dcs_write(ctx, vlin1, VLIN1_CMD_SIZE);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x14);
		exynos_dcs_write(ctx, vgh, VGH_CMD_SIZE);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x1D);
		exynos_dcs_write(ctx, vreg, VREG_CMD_SIZE);
	} else {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x09);
		EXYNOS_DCS_WRITE_TABLE(ctx, vlin1_7v9);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x14);
		EXYNOS_DCS_WRITE_TABLE(ctx, vgh_7v4);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x1D);
		exynos_dcs_write(ctx, vreg_offset, VREG_CMD_SIZE);
	}

	EXYNOS_DCS_WRITE_TABLE(ctx, lock_cmd_f0);
}

static void ea8182_f10_panel_reset(struct exynos_panel *ctx)
{
	dev_dbg(ctx->dev, "%s +\n", __func__);

	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10100, 10110);

	dev_dbg(ctx->dev, "%s -\n", __func__);

	exynos_panel_init(ctx);
}

static int ea8182_f10_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_dbg(ctx->dev, "%s\n", __func__);

	ea8182_f10_panel_reset(ctx);

	EXYNOS_DCS_WRITE_SEQ(ctx, 0x9D, 0x01);  /* Compression Enable */
	EXYNOS_DCS_WRITE_TABLE(ctx, pps_setting);
	EXYNOS_DCS_WRITE_TABLE(ctx, sleep_out);
	usleep_range(10000, 10010);

	ea8182_f10_set_default_voltage(ctx, false);
	usleep_range(20000, 20010);

	exynos_panel_send_cmd_set(ctx, &ea8182_f10_init_cmd_set);
	usleep_range(90000, 90010);
	EXYNOS_DCS_WRITE_TABLE(ctx, unlock_cmd_f0);
	ea8182_f10_change_frequency(ctx, drm_mode_vrefresh(&pmode->mode));
	EXYNOS_DCS_WRITE_TABLE(ctx, lock_cmd_f0);
	ea8182_f10_update_wrctrld(ctx);             /* dimming and HBM */

	ctx->enabled = true;

	if (pmode->exynos_mode.is_lp_mode) {
		exynos_panel_set_lp_mode(ctx, pmode);
	} else {
		EXYNOS_DCS_WRITE_TABLE(ctx, display_on);
	}
	ea8182_f10_set_default_voltage(ctx, true);

	return 0;
}

static int ea8182_f10_disable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	dev_dbg(ctx->dev, "%s\n", __func__);

	/* exit lp mode via 0x53 cmd to avoid green flicker when adjusting voltage  */
	if (pmode && pmode->exynos_mode.is_lp_mode)
		EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				EA8182_F10_WRCTRLD_BCTRL_BIT);

	EXYNOS_DCS_WRITE_TABLE_DELAY(ctx, 20, display_off);
	ea8182_f10_set_default_voltage(ctx, false);
	exynos_panel_disable(panel);

	return 0;
}

static void ea8182_f10_set_hbm_mode(struct exynos_panel *exynos_panel,
				 enum exynos_hbm_mode mode)
{
	const bool hbm_update =
		(IS_HBM_ON(exynos_panel->hbm_mode) != IS_HBM_ON(mode));
	const bool irc_update =
		(IS_HBM_ON_IRC_OFF(exynos_panel->hbm_mode) != IS_HBM_ON_IRC_OFF(mode));

	exynos_panel->hbm_mode = mode;

	if (hbm_update)
		ea8182_f10_update_wrctrld(exynos_panel);

	if (irc_update) {
		EXYNOS_DCS_WRITE_SEQ(exynos_panel, 0xF0, 0x5A, 0x5A);
		EXYNOS_DCS_WRITE_SEQ(exynos_panel, 0xB0, 0x01);
		EXYNOS_DCS_WRITE_SEQ(exynos_panel, 0xC6, IS_HBM_ON_IRC_OFF(mode) ? 0x05 : 0x25);
		EXYNOS_DCS_WRITE_SEQ(exynos_panel, 0xF0, 0xA5, 0xA5);
	}
	dev_info(exynos_panel->dev, "hbm_on=%d hbm_ircoff=%d\n", IS_HBM_ON(exynos_panel->hbm_mode),
		IS_HBM_ON_IRC_OFF(exynos_panel->hbm_mode));
}

static void ea8182_f10_set_dimming_on(struct exynos_panel *exynos_panel,
				 bool dimming_on)
{
	const struct exynos_panel_mode *pmode = exynos_panel->current_mode;

	exynos_panel->dimming_on = dimming_on;
	if (pmode->exynos_mode.is_lp_mode) {
		dev_info(exynos_panel->dev,"in lp mode, skip to update");
		return;
	}

	ea8182_f10_update_wrctrld(exynos_panel);
}

static void ea8182_f10_mode_set(struct exynos_panel *ctx,
			     const struct exynos_panel_mode *pmode)
{
	if (!ctx->enabled)
		return;

	EXYNOS_DCS_WRITE_TABLE(ctx, unlock_cmd_f0);
	ea8182_f10_change_frequency(ctx, drm_mode_vrefresh(&pmode->mode));
	EXYNOS_DCS_WRITE_TABLE(ctx, lock_cmd_f0);
}

static bool ea8182_f10_is_mode_seamless(const struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	/* seamless mode switch is possible if only changing refresh rate */
	return drm_mode_equal_no_clocks(&ctx->current_mode->mode, &pmode->mode);
}

static void ea8182_f10_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 rev = (((build_code & 0xE0) >> 3) | (build_code & 0x0C) >> 2);

	switch (rev) {
	case 1:
		ctx->panel_rev = PANEL_REV_PROTO1;
		break;
	case 2:
		ctx->panel_rev = PANEL_REV_PROTO1_1;
		break;
	case 4:
		ctx->panel_rev = PANEL_REV_EVT1;
		break;
	case 6:
		ctx->panel_rev = PANEL_REV_EVT1_1;
		break;
	case 7:
		ctx->panel_rev = PANEL_REV_EVT1_2;
		break;
	case 9:
		ctx->panel_rev = PANEL_REV_DVT1;
		break;
	case 0xA:
		ctx->panel_rev = PANEL_REV_DVT1_1;
		break;
	case 0x10:
		ctx->panel_rev = PANEL_REV_PVT;
		break;
	default:
		dev_warn(ctx->dev,
			 "unknown rev from panel (0x%x), default to latest\n",
			 rev);
		ctx->panel_rev = PANEL_REV_LATEST;
		return;
	}

	dev_info(ctx->dev, "panel_rev: 0x%x\n", ctx->panel_rev);
}

static int ea8182_f10_set_power(struct exynos_panel *ctx, bool enable)
{
	int ret;

	if (enable) {
		if (ctx->vddi) {
			ret = regulator_enable(ctx->vddi);
			if (ret) {
				dev_err(ctx->dev, "vddi enable failed\n");
				return ret;
			}
		}

		if (ctx->vddd) {
			ret = regulator_enable(ctx->vddd);
			if (ret) {
				dev_err(ctx->dev, "vddd enable failed\n");
				return ret;
			}
		}

		if (ctx->vci) {
			ret = regulator_enable(ctx->vci);
			if (ret) {
				dev_err(ctx->dev, "vci enable failed\n");
				return ret;
			}
			usleep_range(11000, 11010);
		}
	} else {
		gpiod_set_value(ctx->reset_gpio, 0);
		usleep_range(10000, 10010);

		if (ctx->vci) {
			ret = regulator_disable(ctx->vci);
			if (ret) {
				dev_err(ctx->dev, "vci disable failed\n");
				return ret;
			}
		}

		if (ctx->vddd) {
			ret = regulator_disable(ctx->vddd);
			if (ret) {
				dev_err(ctx->dev, "vddd disable failed\n");
				return ret;
			}
		}

		if (ctx->vddi) {
			ret = regulator_disable(ctx->vddi);
			if (ret) {
				dev_err(ctx->dev, "vddi disable failed\n");
				return ret;
			}
		}
	}

	return 0;
}

static int ea8182_f10_set_brightness(struct exynos_panel *ctx, u16 br)
{
	u16 brightness;
	struct ea8182_f10_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		const struct exynos_panel_funcs *funcs;

		/* don't stay at pixel-off state in AOD, or black screen is possibly seen */
		if (spanel->is_pixel_off) {
			EXYNOS_DCS_WRITE_TABLE(ctx, normal_on);
			spanel->is_pixel_off = false;
		}
		funcs = ctx->desc->exynos_panel_func;
		if (funcs && funcs->set_binned_lp)
			funcs->set_binned_lp(ctx, br);
		return 0;
	}

	/* Use pixel off command instead of setting DBV 0 */
	if (!br) {
		if (!spanel->is_pixel_off) {
			EXYNOS_DCS_WRITE_TABLE(ctx, pixel_off);
			spanel->is_pixel_off = true;
			dev_dbg(ctx->dev, "%s: pixel off instead of dbv 0\n", __func__);
		}
		return 0;
	} else if (br && spanel->is_pixel_off) {
		EXYNOS_DCS_WRITE_TABLE(ctx, normal_on);
		spanel->is_pixel_off = false;
	}

	brightness = (br & 0xff) << 8 | br >> 8;

	return exynos_dcs_set_brightness(ctx, brightness);
}


static void ea8182_f10_get_vreg_offset_voltage(struct exynos_panel *ctx)
{
	struct ea8182_f10_panel *spanel = to_spanel(ctx);
	u8 *vreg = spanel->panel_voltage.vreg_default;
	u8 *vreg_offset = spanel->panel_voltage.vreg_offset;
	int i;

	vreg_offset[0] = vreg[0];
	dev_info(ctx->dev, "%s: vreg_offset: (0x%2x)\n", __func__, vreg_offset[0]);
	for (i = 1; i < VREG_CMD_SIZE; i++) {
		/* VREG offset -0.3V */
		vreg_offset[i] = vreg[i] - 3;
		dev_info(ctx->dev, "%s: vreg_offset: (0x%2x)\n", __func__, vreg_offset[i]);
	}
}

static int ea8182_f10_read_default_voltage(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct ea8182_f10_panel *spanel = to_spanel(ctx);
	int ret;
	u8 *vlin1 = spanel->panel_voltage.vlin1_default;
	u8 *vgh = spanel->panel_voltage.vgh_default;
	u8 *vreg = spanel->panel_voltage.vreg_default;
	u8 buf[VLIN1_CMD_SIZE * 2] = {0};
	u8 buf1[VGH_CMD_SIZE * 2] = {0};
	u8 buf2[VREG_CMD_SIZE * 2] = {0};

	EXYNOS_DCS_WRITE_SEQ(ctx, 0xF0, 0x5A, 0x5A);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x09);

	ret = mipi_dsi_dcs_read(dsi, 0xE7, vlin1 + 1, VLIN1_CMD_SIZE - 1);
	if (ret == (VLIN1_CMD_SIZE - 1)) {
		vlin1[0] = 0xE7;
		exynos_bin2hex(vlin1 + 1, VLIN1_CMD_SIZE - 1, buf, sizeof(buf));

		dev_info(ctx->dev, "%s: vlin1: %s\n", __func__, buf);
	} else {
		dev_err(ctx->dev, "unable to raed vlin1\n");
	}

	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x14);

	ret = mipi_dsi_dcs_read(dsi, 0xE3, vgh + 1, VGH_CMD_SIZE - 1);
	if (ret == (VGH_CMD_SIZE - 1)) {
		vgh[0] = 0xE3;
		exynos_bin2hex(vgh + 1, VGH_CMD_SIZE - 1, buf1, sizeof(buf1));

		dev_info(ctx->dev, "%s: vgh: %s\n", __func__, buf1);
	} else {
		dev_err(ctx->dev, "unable to raed vgh\n");
	}

	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x1D);

	ret = mipi_dsi_dcs_read(dsi, 0xE3, vreg + 1, VREG_CMD_SIZE - 1);
	if (ret == (VREG_CMD_SIZE - 1)) {
		vreg[0] = 0xE3;
		exynos_bin2hex(vreg + 1, VREG_CMD_SIZE - 1, buf2, sizeof(buf2));

		dev_info(ctx->dev, "%s: vreg: %s\n", __func__, buf2);
	} else {
		dev_err(ctx->dev, "unable to raed vreg\n");
	}

	EXYNOS_DCS_WRITE_SEQ(ctx, 0xF0, 0xA5, 0xA5);

	return 0;
}
static int ea8182_f10_read_id(struct exynos_panel *ctx)
{
	int ret;

	ret = exynos_panel_read_id(ctx);
	if (ret)
		return ret;

	if (ctx->panel_rev < PANEL_REV_DVT1)
		return 0;

	ea8182_f10_read_default_voltage(ctx);
	ea8182_f10_get_vreg_offset_voltage(ctx);

	return 0;
}

static int ea8182_f10_panel_probe(struct mipi_dsi_device *dsi)
{
	struct ea8182_f10_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->is_pixel_off = false;

	return exynos_panel_common_init(dsi, &spanel->base);
}


static void ea8182_f10_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	struct dentry *csroot = debugfs_lookup("cmdsets", root);

	if (!ctx || !csroot)
		return;

	exynos_panel_debugfs_create_cmdset(ctx, csroot, &ea8182_f10_init_cmd_set, "init");
}

static void ea8182_f10_panel_init(struct exynos_panel *ctx)
{
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 350,
	.te_var = 1,
};

static const struct exynos_panel_mode ea8182_f10_modes[] = {
	{
		/* 1080x2092 @ 60Hz */
		.mode = {
			.clock = 144480,
			.hdisplay = 1080,
			.hsync_start = 1080 + 21, // add hfp
			.hsync_end = 1080 + 21 + 1, // add hsa
			.htotal = 1080 + 21 + 1 + 18, // add hbp
			.vdisplay = 2092,
			.vsync_start = 2092 + 24, // add vfp
			.vsync_end = 2092 + 24 + 6, // add vsa
			.vtotal = 2092 + 24 + 6 + 28, // add vbp
			.flags = 0,
			.width_mm = 67,
			.height_mm = 130,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 8530,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 1,
				.slice_count = 2,
				.slice_height = 523,
			},
			.underrun_param = &underrun_param,
		},
	},
	{
		/* 1080x2092 @ 120Hz */
		.mode = {
			.clock = 288960,
			.hdisplay = 1080,
			.hsync_start = 1080 + 21, // add hfp
			.hsync_end = 1080 + 21 + 1, // add hsa
			.htotal = 1080 + 21 + 1 + 18, // add hbp
			.vdisplay = 2092,
			.vsync_start = 2092 + 24, // add vfp
			.vsync_end = 2092 + 24 + 6, // add vsa
			.vtotal = 2092 + 24 + 6 + 28, // add vbp
			.flags = 0,
			.width_mm = 67,
			.height_mm = 130,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 217,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 1,
				.slice_count = 2,
				.slice_height = 523,
			},
			.underrun_param = &underrun_param,
		},
	},
};

static const struct exynos_panel_mode ea8182_f10_lp_mode = {
	.mode = {
		/* 1080x2092 @ 30Hz */
		.name = "1080x2092x30",
		.clock = 72240,
		.hdisplay = 1080,
		.hsync_start = 1080 + 21, // add hfp
		.hsync_end = 1080 + 21 + 1, // add hsa
		.htotal = 1080 + 21 + 1 + 18, // add hbp
		.vdisplay = 2092,
		.vsync_start = 2092 + 24, // add vfp
		.vsync_end = 2092 + 24 + 6, // add vsa
		.vtotal = 2092 + 24 + 6 + 28, // add vbp
		.flags = 0,
		.width_mm = 67,
		.height_mm = 130,
	},
	.exynos_mode = {
		.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
		.vblank_usec = 120,
		.bpc = 8,
		.dsc = {
			.enabled = true,
			.dsc_count = 1,
			.slice_count = 2,
			.slice_height = 523,
		},
		.underrun_param = &underrun_param,
		.is_lp_mode = true,
	}
};

static const struct drm_panel_funcs ea8182_f10_drm_funcs = {
	.disable = ea8182_f10_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = ea8182_f10_enable,
	.get_modes = exynos_panel_get_modes,
	.debugfs_init = ea8182_f10_debugfs_init,
};

static const struct exynos_panel_funcs ea8182_f10_exynos_funcs = {
	.set_brightness = ea8182_f10_set_brightness,
	.set_lp_mode = exynos_panel_set_lp_mode,
	.set_nolp_mode = ea8182_f10_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = ea8182_f10_set_hbm_mode,
	.set_dimming_on = ea8182_f10_set_dimming_on,
	.is_mode_seamless = ea8182_f10_is_mode_seamless,
	.mode_set = ea8182_f10_mode_set,
	.panel_init = ea8182_f10_panel_init,
	.get_panel_rev = ea8182_f10_get_panel_rev,
	.set_power = ea8182_f10_set_power,
	.read_id = ea8182_f10_read_id,
};

const struct brightness_capability ea8182_f10_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 600,
		},
		.level = {
			.min = 4,
			.max = 1536,
		},
		.percentage = {
			.min = 0,
			.max = 50,
		},
	},
	.hbm = {
		.nits = {
			.min = 600,
			.max = 1200,
		},
		.level = {
			.min = 2048,
			.max = 3584,
		},
		.percentage = {
			.min = 50,
			.max = 100,
		},
	},
};

const struct exynos_panel_desc samsung_ea8182_f10 = {
	.data_lane_cnt = 4,
	.max_brightness = 3584,
	.min_brightness = 4,
	.dft_brightness = 1023,
	.brt_capability = &ea8182_f10_brightness_capability,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.modes = ea8182_f10_modes,
	.num_modes = ARRAY_SIZE(ea8182_f10_modes),
	.off_cmd_set = &ea8182_f10_sleep_in_cmd_set,
	.lp_mode = &ea8182_f10_lp_mode,
	.lp_cmd_set = &ea8182_f10_lp_cmd_set,
	.binned_lp = ea8182_f10_binned_lp,
	.num_binned_lp = ARRAY_SIZE(ea8182_f10_binned_lp),
	.panel_func = &ea8182_f10_drm_funcs,
	.exynos_panel_func = &ea8182_f10_exynos_funcs,
};

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "samsung,ea8182-f10", .data = &samsung_ea8182_f10 },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = ea8182_f10_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-samsung-ea8182-f10",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_DESCRIPTION("MIPI-DSI based Samsung ea8182-f10 panel driver");
MODULE_LICENSE("GPL");
