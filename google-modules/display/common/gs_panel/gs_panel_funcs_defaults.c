// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */
#include <drm/drm_modes.h>

#include <linux/delay.h>

#include <linux/delay.h>

#include "gs_panel/gs_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"
#include "gs_panel_internal.h"
#include "trace/panel_trace.h"

#define PANEL_ID_REG_DEFAULT 0xA1
#define PANEL_ID_LEN 7
#define PANEL_ID_OFFSET 6
#define PANEL_ID_READ_SIZE (PANEL_ID_LEN + PANEL_ID_OFFSET)
#define PANEL_SLSI_DDIC_ID_REG 0xD6
#define PANEL_SLSI_DDIC_ID_LEN 5
#define PROJECT_CODE_MAX 5

/* The threshold of tracing panel settings if unchanged. */
#define PANEL_SETTINGS_TRACE_TH_MS 2000

#define panel_rev_return_case(REV_NAME) \
	{                               \
	case PANEL_REVID_##REV_NAME:    \
		return #REV_NAME;       \
	}

static const char *get_panel_rev_name(u32 panel_rev)
{
	switch (panel_rev) {
		panel_rev_return_case(PROTO1);
		panel_rev_return_case(PROTO1_0_1);
		panel_rev_return_case(PROTO1_1);
		panel_rev_return_case(PROTO1_2);
		panel_rev_return_case(PROTO2);
		panel_rev_return_case(EVT1);
		panel_rev_return_case(EVT1_0_2);
		panel_rev_return_case(EVT1_1);
		panel_rev_return_case(EVT1_1_1);
		panel_rev_return_case(EVT1_2);
		panel_rev_return_case(EVT2);
		panel_rev_return_case(DVT1);
		panel_rev_return_case(DVT1_1);
		panel_rev_return_case(PVT);
		panel_rev_return_case(MP);
		panel_rev_return_case(LATEST);
	default:
		return "Unknown";
	}
}

void gs_panel_get_panel_rev(struct gs_panel *ctx, u8 rev)
{
	switch (rev) {
	case 0:
		ctx->panel_rev_id.id = PANEL_REVID_PROTO1;
		break;
	case 1:
		ctx->panel_rev_id.id = PANEL_REVID_PROTO1_1;
		break;
	case 2:
		ctx->panel_rev_id.id = PANEL_REVID_PROTO1_2;
		break;
	case 8:
		ctx->panel_rev_id.id = PANEL_REVID_EVT1;
		break;
	case 9:
		ctx->panel_rev_id.id = PANEL_REVID_EVT1_1;
		break;
	case 0xA:
		ctx->panel_rev_id.id = PANEL_REVID_EVT1_2;
		break;
	case 0xC:
		ctx->panel_rev_id.id = PANEL_REVID_DVT1;
		break;
	case 0xD:
		ctx->panel_rev_id.id = PANEL_REVID_DVT1_1;
		break;
	case 0x10:
		ctx->panel_rev_id.id = PANEL_REVID_PVT;
		break;
	case 0x14:
		ctx->panel_rev_id.id = PANEL_REVID_MP;
		break;
	default:
		dev_warn(ctx->dev, "unknown rev from panel (0x%x), default to latest\n", rev);
		ctx->panel_rev_id.id = PANEL_REVID_LATEST;
		return;
	}

	dev_info(ctx->dev, "panel_rev: %s\n", get_panel_rev_name(ctx->panel_rev_id.id));
}
EXPORT_SYMBOL_GPL(gs_panel_get_panel_rev);

void gs_panel_get_panel_rev_full(struct gs_panel *ctx, u32 id)
{
	panel_rev_id_t rev_id = { .id = 0x0 };
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 main = (build_code & 0xF0) >> 4;
	u8 sub = (build_code & 0x0C) >> 2;
	u8 var = build_code & 0x03;

	/* stage */
	switch (main) {
	case 0x0:
		rev_id.s.stage = STAGE_PROTO;
		break;
	case 0x4:
		rev_id.s.stage = STAGE_EVT;
		break;
	case 0x6:
		rev_id.s.stage = STAGE_DVT;
		break;
	case 0x8:
		rev_id.s.stage = STAGE_PVT;
		break;
	case 0xA:
		rev_id.s.stage = STAGE_MP;
		break;
	default:
		rev_id.id = PANEL_REVID_LATEST;
		break;
	}

	/* major/minor (if stage correct) */
	if (rev_id.id != PANEL_REVID_LATEST) {
		switch (sub) {
		case 0x0:
			rev_id.s.major = 1;
			break;
		case 0x1:
			rev_id.s.major = 1;
			rev_id.s.minor = 1;
			break;
		case 0x2:
			rev_id.s.major = 1;
			rev_id.s.minor = 2;
			break;
		case 0x3:
			rev_id.s.major = 2;
			rev_id.s.minor = 0;
			break;
		default:
			dev_warn(ctx->dev, "Unknown sub-build %#04x, defaulting to 2.0\n", sub);
			rev_id.s.major = 2;
			rev_id.s.minor = 0;
			break;
		}
		rev_id.s.variant = var;
	}

	ctx->panel_rev_id = rev_id;

	if (ctx->panel_rev_id.id == PANEL_REVID_LATEST)
		dev_warn(ctx->dev, "Unknown revision from panel (%#010x), default to latest\n", id);
	else
		dev_info(ctx->dev, "panel_rev: %s\n", get_panel_rev_name(ctx->panel_rev_id.id));
}
EXPORT_SYMBOL_GPL(gs_panel_get_panel_rev_full);

void gs_panel_get_panel_rev_no_variant(struct gs_panel *ctx, u32 id)
{
	static const u32 variant_mask = ~0x00000300;

	return gs_panel_get_panel_rev_full(ctx, id & variant_mask);
}
EXPORT_SYMBOL_GPL(gs_panel_get_panel_rev_no_variant);

int gs_panel_read_slsi_ddic_id(struct gs_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = ctx->dev;
	char buf[PANEL_SLSI_DDIC_ID_LEN] = { 0 };
	int ret;

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xF0, 0x5A, 0x5A);
	ret = mipi_dsi_dcs_read(dsi, PANEL_SLSI_DDIC_ID_REG, buf, PANEL_SLSI_DDIC_ID_LEN);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xF0, 0xA5, 0xA5);
	if (ret != PANEL_SLSI_DDIC_ID_LEN) {
		dev_warn(dev, "Unable to read DDIC id (%d)\n", ret);
		return ret;
	}

	bin2hex(ctx->panel_serial_number, buf, PANEL_SLSI_DDIC_ID_LEN);
	return 0;
}
EXPORT_SYMBOL_GPL(gs_panel_read_slsi_ddic_id);

int gs_panel_read_serial(struct gs_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	char buf[PANEL_ID_READ_SIZE];
	int ret;

	ret = mipi_dsi_dcs_read(dsi, ctx->desc->panel_id_reg ?: PANEL_ID_REG_DEFAULT, buf,
				PANEL_ID_READ_SIZE);
	if (ret != PANEL_ID_READ_SIZE) {
		dev_warn(ctx->dev, "Unable to read panel id (%d)\n", ret);
		return ret;
	}

	bin2hex(ctx->panel_serial_number, buf + PANEL_ID_OFFSET, PANEL_ID_LEN);

	return 0;
}
EXPORT_SYMBOL_GPL(gs_panel_read_serial);

void gs_panel_model_init(struct gs_panel *ctx, const char *project, u8 extra_info)
{
	u8 vendor_info;
	u8 panel_rev;

	if (ctx->panel_id == PANEL_ID_INVALID_VALUE || ctx->panel_rev_id.id == 0 || !project)
		return;

	if (strlen(project) > PROJECT_CODE_MAX) {
		dev_err(ctx->dev, "Project Code '%s' is longer than maximum %d characters\n",
			project, PROJECT_CODE_MAX);
		return;
	}

	vendor_info = (ctx->panel_id >> 8) & 0x0f;
	panel_rev = __builtin_ctz(ctx->panel_rev_bitmask);

	/*
	 * Panel Model Format:
	 * [Project Code]-[Vendor Info][Panel Revision]-[Extra Info]
	 */
	scnprintf(ctx->panel_model, PANEL_MODEL_MAX, "%s-%01X%02X-%02X", project, vendor_info,
		  panel_rev, extra_info);
}
EXPORT_SYMBOL_GPL(gs_panel_model_init);

bool gs_panel_is_mode_seamless_atomic_helper(const struct gs_panel *ctx,
					     const struct gs_panel_mode *old_pmode,
					     const struct gs_panel_mode *new_pmode)
{
	const struct drm_display_mode *old_mode = &old_pmode->mode;
	const struct drm_display_mode *new_mode = &new_pmode->mode;

	/* seamless mode switch is possible if the resolution is the same */
	return (old_mode->vdisplay == new_mode->vdisplay) &&
	       (old_mode->hdisplay == new_mode->hdisplay);
}
EXPORT_SYMBOL_GPL(gs_panel_is_mode_seamless_atomic_helper);

bool gs_panel_is_mode_seamless_helper(const struct gs_panel *ctx,
				      const struct gs_panel_mode *new_pmode)
{
	return gs_panel_is_mode_seamless_atomic_helper(ctx, ctx->current_mode, new_pmode);
}
EXPORT_SYMBOL_GPL(gs_panel_is_mode_seamless_helper);

ssize_t gs_panel_get_te2_edges_helper(struct gs_panel *ctx, char *buf, bool lp_mode)
{
	struct gs_te2_mode_data *data;
	size_t len = 0;
	int i;

	if (!ctx)
		return -EINVAL;

	for_each_te2_timing(ctx, lp_mode, data, i) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%dx%d@%d", data->mode->hdisplay,
				 data->mode->vdisplay, drm_mode_vrefresh(data->mode));

		if (data->binned_lp)
			len += scnprintf(buf + len, PAGE_SIZE - len, "-lp_%s",
					 data->binned_lp->name);

		len += scnprintf(buf + len, PAGE_SIZE - len, " rising %u falling %u\n",
				 data->timing.rising_edge, data->timing.falling_edge);
	}

	return len;
}
EXPORT_SYMBOL_GPL(gs_panel_get_te2_edges_helper);

int gs_panel_set_te2_edges_helper(struct gs_panel *ctx, u32 *timings, bool lp_mode)
{
	struct gs_te2_mode_data *data;
	const u32 *t;
	int i;

	if (!ctx || !timings)
		return -EINVAL;

	t = timings;

	for_each_te2_timing(ctx, lp_mode, data, i) {
		data->timing.rising_edge = t[0];
		data->timing.falling_edge = t[1];
		t += 2;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gs_panel_set_te2_edges_helper);

static int gs_panel_read_errfg(struct gs_panel *ctx,
			       const struct gs_panel_detect_fault_desc *desc,
			       bool *read_dsi_err, bool *read_failure)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 buf[2] = { 0 };
	u32 status = 0;
	int ret = 0;

	if (unlikely(desc->errfg_len != 1 && desc->errfg_len != 2)) {
		dev_err(ctx->dev, "The length of ERR_FG is expected to be 1 or 2.\n");
		return -EINVAL;
	}

	ret = mipi_dsi_dcs_read(dsi, desc->errfg_reg, buf, desc->errfg_len);
	if (ret != desc->errfg_len) {
		*read_failure = true;
		return -EIO;
	}

	dev_dbg(ctx->dev, "ERR_FG: %02x %02x\n", buf[0], buf[1]);

	status = (desc->errfg_len == 1) ? buf[0] : ((buf[0] << 8) | buf[1]);

	if (status & desc->vgh_mask)
		set_bit(GS_PANEL_ERR_VGH, ctx->panel_errors);
	if (status & desc->vlin1_mask)
		set_bit(GS_PANEL_ERR_VLIN1, ctx->panel_errors);
	if (status & desc->csum_mask)
		set_bit(GS_PANEL_ERR_CHECKSUM, ctx->panel_errors);
	if (status & desc->te_mask)
		set_bit(GS_PANEL_ERR_TE, ctx->panel_errors);

	*read_dsi_err = !desc->dsi_err_mask || (status & desc->dsi_err_mask);

	return 0;
}

static int gs_panel_read_dsi_err(struct gs_panel *ctx,
				 const struct gs_panel_detect_fault_desc *desc)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 buf[2] = { 0 };
	int ret = 0;

	ret = mipi_dsi_dcs_read(dsi, desc->dsi_err_reg, buf, 2);
	if (ret != 2) {
		dev_err(ctx->dev, "Error reading DSI error register (%pe)\n", ERR_PTR(ret));
		return -EIO;
	}

	if (buf[0] || buf[1]) {
		bitmap_set_value8(ctx->panel_errors, buf[0], 8);
		bitmap_set_value8(ctx->panel_errors, buf[1], 0);
	} else {
		dev_dbg(ctx->dev, "DSI error flag set, but no specific error");
	}

	return 0;
}

int gs_panel_detect_fault_helper(struct gs_panel *ctx,
				 const struct gs_panel_detect_fault_desc *desc, bool irq_triggered)
{
	bool read_dsi_err = true;
	bool read_failure = false;
	int ret = 0;

	if (unlikely(!ctx || !desc))
		return -EINVAL;

	PANEL_ATRACE_BEGIN("gs_panel_detect_fault_helper");

	if (desc->errfg_reg) {
		ret = gs_panel_read_errfg(ctx, desc, &read_dsi_err, &read_failure);
		if (ret)
			goto end;
	}

	if (read_dsi_err && desc->dsi_err_reg) {
		ret = gs_panel_read_dsi_err(ctx, desc);
		if (ret) {
			read_failure = true;
			goto end;
		}
	}

	ctx->ddic_read_fail_cnt = 0;
	if (!bitmap_empty(ctx->panel_errors, GS_PANEL_ERR_MAX))
		dev_err(ctx->dev, "DDIC error: %*pbl\n", GS_PANEL_ERR_MAX, ctx->panel_errors);

end:
	if (read_failure) {
		ctx->ddic_read_fail_cnt++;
		dev_warn(ctx->dev, "Error reading DDIC register (%d), cnt=%d\n", ret,
			ctx->ddic_read_fail_cnt);
		if (ctx->ddic_read_fail_cnt == MAX_DDIC_READ_FAIL_CNT)
			set_bit(GS_PANEL_ERR_DSI_READ_FAILURE, ctx->panel_errors);
	}
	PANEL_ATRACE_END("gs_panel_detect_fault_helper");
	return ret;
}
EXPORT_SYMBOL_GPL(gs_panel_detect_fault_helper);

static inline bool is_backlight_lp_state(const struct backlight_device *bl)
{
	return (bl->props.state & BL_STATE_LP) != 0;
}

void gs_panel_set_binned_lp_helper(struct gs_panel *ctx, const u16 brightness)
{
	int i;
	const struct gs_binned_lp *binned_lp;
	struct backlight_device *bl = ctx->bl;
	bool is_lp_state;
	enum gs_panel_state panel_state;

	for (i = 0; i < ctx->desc->num_binned_lp; i++) {
		binned_lp = &ctx->desc->binned_lp[i];
		if (brightness <= binned_lp->bl_threshold)
			break;
	}
	if (i == ctx->desc->num_binned_lp)
		return;

	mutex_lock(&ctx->bl_state_lock); /*TODO(b/267170999): BL*/
	is_lp_state = is_backlight_lp_state(bl);
	mutex_unlock(&ctx->bl_state_lock); /*TODO(b/267170999): BL*/

	if (is_lp_state && ctx->current_binned_lp &&
	    binned_lp->bl_threshold == ctx->current_binned_lp->bl_threshold) {
		return;
	}

	PANEL_ATRACE_BEGIN(__func__);
	gs_panel_send_cmdset(ctx, &binned_lp->cmdset);

	ctx->current_binned_lp = binned_lp;
	dev_info(ctx->dev, "enter lp_%s\n", ctx->current_binned_lp->name);

	panel_state = !binned_lp->bl_threshold ? GPANEL_STATE_BLANK : GPANEL_STATE_LP;
	gs_panel_set_backlight_state(ctx, panel_state);

	if (bl)
		sysfs_notify(&bl->dev.kobj, NULL, "lp_state");

	if (panel_state == GPANEL_STATE_LP)
		gs_panel_update_te2(ctx);
	PANEL_ATRACE_END(__func__);
}
EXPORT_SYMBOL_GPL(gs_panel_set_binned_lp_helper);

void gs_panel_set_lp_mode_helper(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const u16 brightness = gs_panel_get_brightness(ctx);

	if (ctx->desc->lp_cmdset) {
		gs_panel_send_cmdset(ctx, ctx->desc->lp_cmdset);
		gs_panel_set_binned_lp_helper(ctx, brightness);
		dev_info(ctx->dev, "enter %dhz LP mode\n", drm_mode_vrefresh(&pmode->mode));
	} else {
		dev_err(ctx->dev, "No LP cmdset in panel description\n");
	}
}
EXPORT_SYMBOL_GPL(gs_panel_set_lp_mode_helper);

int gs_panel_set_vddd_regulator_helper(struct gs_panel *ctx, bool is_lp)
{
	int ret;
	u32 uv = is_lp ? ctx->regulator.vddd_lp_uV : ctx->regulator.vddd_normal_uV;

	if (!ctx->regulator.vddd)
		return -EINVAL;

	if (!uv)
		return 0;

	ret = regulator_set_voltage(ctx->regulator.vddd, uv, uv);
	if (ret)
		dev_err(ctx->dev, "failed to set vddd at %u uV (%d)\n", uv, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(gs_panel_set_vddd_regulator_helper);

int gs_panel_set_vddd_optional_gpio_helper(struct gs_panel *ctx, bool is_lp)
{
	if (IS_ERR_OR_NULL(ctx->gpio.gpiod[DISP_VDDD_GPIO]))
		return gs_panel_set_vddd_regulator_helper(ctx, is_lp);
	return gs_panel_gpio_set(ctx, DISP_VDDD_GPIO, !is_lp);
}
EXPORT_SYMBOL_GPL(gs_panel_set_vddd_optional_gpio_helper);

bool gs_panel_refresh_ctrl_full_helper(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const u32 ctrl = ctx->refresh_ctrl;
	unsigned long *feat = ctx->sw_status.feat;
	u32 min_vrefresh = ctx->sw_status.idle_vrefresh;
	u32 vrefresh;
	bool lp_mode;
	bool idle_vrefresh_changed = false;
	bool prev_feat_frame_auto_enabled = test_bit(FEAT_FRAME_AUTO, feat);
	bool prev_feat_early_exit_enabled = test_bit(FEAT_EARLY_EXIT, feat);

	if (!pmode)
		return false;

	dev_dbg(ctx->dev, "refresh_ctrl=0x%X\n", ctrl);

	vrefresh = drm_mode_vrefresh(&pmode->mode);
	lp_mode = pmode->gs_mode.is_lp_mode;

	if (ctrl & GS_PANEL_REFRESH_CTRL_MIN_REFRESH_RATE_MASK) {
		min_vrefresh = GS_PANEL_REFRESH_CTRL_MIN_REFRESH_RATE(ctrl);

		if (min_vrefresh > vrefresh) {
			dev_warn(ctx->dev, "%s: min RR %uHz requested, but valid range is 1-%uHz\n",
				 __func__, min_vrefresh, vrefresh);
			min_vrefresh = vrefresh;
		}
		ctx->sw_status.idle_vrefresh = min_vrefresh;
		idle_vrefresh_changed = true;
	}

	if (ctrl & GS_PANEL_REFRESH_CTRL_FI_AUTO) {
		if (min_vrefresh == vrefresh)
			clear_bit(FEAT_FRAME_AUTO, feat);
		else
			set_bit(FEAT_FRAME_AUTO, feat);
	} else
		clear_bit(FEAT_FRAME_AUTO, feat);

	if (ctrl & GS_PANEL_REFRESH_CTRL_EARLY_EXIT)
		set_bit(FEAT_EARLY_EXIT, feat);
	else {
		clear_bit(FEAT_EARLY_EXIT, feat);
		clear_bit(FEAT_FRAME_AUTO, feat);
	}

	/* check and guard refresh ctrl settings */
	if (test_bit(FEAT_FRAME_AUTO, feat) && min_vrefresh == 0) {
		dev_warn(ctx->dev,
			 "refresh_ctrl: suspicious refresh_ctrl(%#x):fi=%u, min_rr=%u\n",
			 ctrl, test_bit(FEAT_FRAME_AUTO, feat), min_vrefresh);
		clear_bit(FEAT_FRAME_AUTO, feat);
	}
	if (!test_bit(FEAT_EARLY_EXIT, feat) && min_vrefresh <= 1) {
		dev_warn(ctx->dev,
			 "refresh_ctrl: suspicious refresh_ctrl(%#x):ee=%u, min_rr=%u\n",
			 ctrl, test_bit(FEAT_EARLY_EXIT, feat), min_vrefresh);
		set_bit(FEAT_EARLY_EXIT, feat);
	}
	if (!test_bit(FEAT_EARLY_EXIT, feat) && GS_PANEL_REFRESH_CTRL_FI_FRAME_COUNT(ctrl) > 0) {
		dev_warn(ctx->dev,
			 "refresh_ctrl: suspicious refresh_ctrl(%#x): ee=%u, insert frames=%lu\n",
			 ctrl, test_bit(FEAT_EARLY_EXIT, feat),
			 GS_PANEL_REFRESH_CTRL_FI_FRAME_COUNT(ctrl));
		set_bit(FEAT_EARLY_EXIT, feat);
	}

	PANEL_ATRACE_INT_PID_FMT(ctx->sw_status.idle_vrefresh, ctx->trace_pid,
				 "idle_vrefresh[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(test_bit(FEAT_FRAME_AUTO, feat), ctx->trace_pid,
				 "FEAT_FRAME_AUTO[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(test_bit(FEAT_EARLY_EXIT, feat), ctx->trace_pid,
				 "FEAT_EARLY_EXIT[%s]", ctx->panel_model);

	return idle_vrefresh_changed ||
	       (prev_feat_frame_auto_enabled != test_bit(FEAT_FRAME_AUTO, feat)) ||
	       (prev_feat_early_exit_enabled != test_bit(FEAT_EARLY_EXIT, feat));
}
EXPORT_SYMBOL_GPL(gs_panel_refresh_ctrl_full_helper);

bool gs_panel_refresh_ctrl_lite_helper(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				       u8 min_rr_supported)
{
	struct device *dev = ctx->dev;
	const u32 ctrl = ctx->refresh_ctrl;
	bool changes_detected = false;

	if (!pmode)
		return false;

	dev_dbg(ctx->dev, "refresh_ctrl=%#X\n", ctrl);

	if (!gs_is_vrr_mode(pmode)) {
		dev_warn(dev, "refresh_ctrl: mode control not supported for %s\n",
			       pmode->mode.name);
		return false;
	}

	if (ctrl & GS_PANEL_REFRESH_CTRL_FI_FRAME_COUNT_MASK ||
	    ctrl & GS_PANEL_REFRESH_CTRL_FI_AUTO)
		dev_warn(dev, "refresh_ctrl: FI functionality not supported\n");

	if (ctrl & GS_PANEL_REFRESH_CTRL_MIN_REFRESH_RATE_MASK) {
		u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
		u32 min_vrefresh = GS_PANEL_REFRESH_CTRL_MIN_REFRESH_RATE(ctrl);

		if (min_vrefresh == vrefresh || min_vrefresh == min_rr_supported) {
			ctx->sw_status.idle_vrefresh = min_vrefresh;
			PANEL_ATRACE_INT_PID_FMT(ctx->sw_status.idle_vrefresh, ctx->trace_pid,
						 "idle_vrefresh[%s]", ctx->panel_model);
			changes_detected = true;
		} else {
			dev_warn(ctx->dev,
				 "refresh_ctrl: %uHz min RR requested, only %u/%u Hz supported\n",
				 min_vrefresh, min_rr_supported, vrefresh);
		}
	}

	return changes_detected;
}
EXPORT_SYMBOL_GPL(gs_panel_refresh_ctrl_lite_helper);

static bool _gs_panel_should_trace_settings(struct gs_panel *ctx, ktime_t now)
{
	return (ctx->panel_settings_changed ||
	        ktime_ms_delta(now, ctx->timestamps.last_panel_settings_trace_ts)
			>= PANEL_SETTINGS_TRACE_TH_MS);
}

void gs_panel_trace_settings_full_helper(struct gs_panel *ctx)
{
	ktime_t now = ktime_get();

	if (_gs_panel_should_trace_settings(ctx, now)) {
		const struct gs_panel_mode *pmode = ctx->current_mode;
		const struct gs_panel_status *hw_status = &ctx->hw_status;
		const unsigned long *feat = hw_status->feat;
		const u32 idle_vrefresh = hw_status->idle_vrefresh;

		trace_panel_settings_full(
			ctx->gs_connector->panel_index, test_bit(FEAT_HBM, feat),
			hw_status->irc_mode, test_bit(FEAT_PWM_HIGH, feat),
			test_bit(FEAT_FRAME_AUTO, feat), test_bit(FEAT_FRAME_MANUAL_FI, feat),
			test_bit(FEAT_EARLY_EXIT, feat),
			idle_vrefresh ? idle_vrefresh : hw_status->vrefresh,
			drm_mode_vrefresh(&pmode->mode), gs_drm_mode_te_freq(&pmode->mode));

		ctx->timestamps.last_panel_settings_trace_ts = now;
		ctx->panel_settings_changed = false;
	}
}
EXPORT_SYMBOL_GPL(gs_panel_trace_settings_full_helper);

void gs_panel_trace_settings_lite_helper(struct gs_panel *ctx)
{
	ktime_t now = ktime_get();

	if (_gs_panel_should_trace_settings(ctx, now)) {
		const struct gs_panel_mode *pmode = ctx->current_mode;
		const bool is_vrr_mode = gs_is_vrr_mode(pmode);
		const u32 idle_vrefresh = ctx->sw_status.idle_vrefresh;
		const u32 vrefresh = is_vrr_mode ? idle_vrefresh : drm_mode_vrefresh(&pmode->mode);

		trace_panel_settings_lite(
			ctx->gs_connector->panel_index,
			is_vrr_mode, idle_vrefresh ? idle_vrefresh : vrefresh,
			drm_mode_vrefresh(&pmode->mode), gs_drm_mode_te_freq(&pmode->mode));

		ctx->timestamps.last_panel_settings_trace_ts = now;
		ctx->panel_settings_changed = false;
	}
}
EXPORT_SYMBOL_GPL(gs_panel_trace_settings_lite_helper);
