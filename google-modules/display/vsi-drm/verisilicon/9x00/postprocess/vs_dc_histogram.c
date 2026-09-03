/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */
#include "vs_dc_histogram.h"

#include "vs_dc_property.h"
#include "vs_dc_reg_be.h"

#include "vs_dc.h"
#include "vs_crtc.h"

#include <trace/dpu_trace.h>

/* histogram operation mode
 * hardcoded for now.
 */
#define DC_HW_HISTOGRAM_READCONFIRM BIT(0) /* double buffered */
#define DC_HW_HISTOGRAM_WDMA BIT(1) /* WDMA */

/* histogram channel configuration flags. possible values are provided above */
static const u32 hist_chan_flags = DC_HW_HISTOGRAM_WDMA;
static const u32 hist_rgb_flags = DC_HW_HISTOGRAM_WDMA;

bool vs_dc_hist_chan_is_wdma(void)
{
	return !!(hist_chan_flags & DC_HW_HISTOGRAM_WDMA);
}

bool vs_dc_hist_rgb_is_wdma(void)
{
	return !!(hist_rgb_flags & DC_HW_HISTOGRAM_WDMA);
}

/* offset between histogram idx channels */
#define VS_HIST_IDX_OFFSET (DCREG_PANEL0_HIST1_CONTROL_Address - DCREG_PANEL0_HIST0_CONTROL_Address)

/*
 * @brief Validate histogram channel configuration data
 *
 * Performs simple histogram channel configuration checks. The configuration data is passed from
 * userspace via DRM property blob.
 */
static bool vs_dc_hist_chan_check(const struct dc_hw *hw, const u8 display_id,
				  const enum drm_vs_hist_idx idx,
				  const struct drm_vs_hist_chan *config)
{
	const struct dc_hw_display *display = &hw->display[display_id];

	/* check if histogram is supported */
	if (!display->info || !display->info->histogram) {
		dev_err(hw->dev, "%s:crtc[%u]: unsupported histogram\n", __func__, display_id);
		return false;
	}

	if (idx >= VS_HIST_CHAN_IDX_COUNT) {
		dev_err(hw->dev, "%s:crtc[%u]: invalid histogram channel %d\n", __func__,
			display_id, idx);
		return false;
	}

	if (config->pos >= VS_HIST_POS_COUNT) {
		dev_err(hw->dev, "%s:crtc[%u]: invalid histogram position\n", __func__, display_id);
		return false;
	}

	if (config->bin_mode >= VS_HIST_BIN_MODE_COUNT) {
		dev_err(hw->dev, "%s:crtc[%u]: invalid histogram bin_mode\n", __func__, display_id);
		return false;
	}

	if (config->bin_mode == VS_HIST_BIN_MODE_WEIGHTS) {
		u16 sum = config->weights[0] + config->weights[1] + config->weights[2];

		if (sum != VS_HIST_BIN_MODE_WEIGHTS_SUM) {
			dev_err(hw->dev, "%s:crtc[%u]: invalid histogram weights\n",
				__func__, display_id);
			return false;
		}
	}

	return true;
}

/*
 * @brief Validate histogram channels configuration data
 */
bool vs_dc_hist_chans_check(const struct dc_hw *hw, u8 display_id,
			    const struct vs_crtc_state *crtc_state)
{
	/* iterate all supported channels */
	for (int i = VS_HIST_CHAN_IDX_0; i < VS_HIST_CHAN_IDX_COUNT; i++) {
		const struct vs_drm_blob_state *hist_chan_state = &crtc_state->hist_chan[i];

		/* unconfigured or unchanged: skip */
		if (!hist_chan_state->changed || !hist_chan_state->blob)
			continue;

		if (!vs_dc_hist_chan_check(hw, display_id, i, hist_chan_state->blob->data))
			return false;
	}

	return true;
}

/*
 * @brief calculate base register offset for regular histogram channels.
 * Note, hist_idx validation must be outside of the function.
 */
static u32 hist_get_channel_offset(u8 hw_id, const enum drm_vs_hist_idx idx)
{
	u32 offset = 0;

	/* take into account display offset */
	if (hw_id == HW_DISPLAY_1)
		offset = DCREG_PANEL1_HIST0_CONTROL_Address - DCREG_PANEL0_HIST0_CONTROL_Address;

	/* take into account histogram channel offset */
	offset += VS_HIST_IDX_OFFSET * idx;

	return offset;
}

/*
 * @brief Update histogram channel configuration.
 * @config: NULL - disable channel, else enable
 */
static void vs_dc_hist_chan_update(struct dc_hw *hw, struct dc_hw_display *display,
				   const enum drm_vs_hist_idx idx,
				   const struct drm_vs_hist_chan *config)
{
	u8 hw_id = display->info->id;
	struct dc_hw_hist_chan *hw_hist_chan;
	struct drm_vs_hist_chan *hw_hist_chan_config;
	unsigned long flags;

	if (idx >= VS_HIST_CHAN_IDX_COUNT) {
		dev_err(hw->dev, "%s: crtc[%u]: invalid histogram channel %d\n", __func__,
			hw_id, idx);
		return;
	}

	hw_hist_chan = &display->hw_hist_chan[idx];
	hw_hist_chan_config = &hw_hist_chan->drm_config;

	spin_lock_irqsave(&hw->histogram_slock, flags);

	if (config) {
		/* preserve new config into config_stage */
		memcpy(hw_hist_chan_config, config, sizeof(struct drm_vs_hist_chan));

		/*
		 * workaround for the h/w b/362424832: max weight value cannot be
		 * VS_HIST_BIN_MODE_WEIGHTS_SUM
		 */
		if (hw->info->cid == VS_CHIP_CID_316 || hw->info->cid == VS_CHIP_CID_32A ||
		    hw->info->cid == VS_CHIP_CID_20000005) {
			if (config->weights[1] == 0 && config->weights[2] == 0)
				hw_hist_chan_config->weights[0] = VS_HIST_BIN_MODE_WEIGHTS_SUM - 1;
			else if (config->weights[0] == 0 && config->weights[2] == 0)
				hw_hist_chan_config->weights[1] = VS_HIST_BIN_MODE_WEIGHTS_SUM - 1;
			else if (config->weights[0] == 0 && config->weights[1] == 0)
				hw_hist_chan_config->weights[2] = VS_HIST_BIN_MODE_WEIGHTS_SUM - 1;
		}
	}

	/* update trackers */
	hw_hist_chan->enable = !!(config);
	hw_hist_chan->dirty = true;
	hw_hist_chan->changed = true;

	spin_unlock_irqrestore(&hw->histogram_slock, flags);

}

/*
 * @brief Update histogram channels configuration
 *
 * Function updates histogram channel configuration.
 * The configuration data is passed from userspace via DRM property blob.
 */
bool vs_dc_hist_chans_update(struct dc_hw *hw, u8 display_id,
			     const struct vs_crtc_state *crtc_state)
{
	struct dc_hw_display *display = &hw->display[display_id];

	/* don't process if unsupported */
	if (!display->info || !display->info->histogram)
		return false;

	/* handle all regular channels */
	for (int i = VS_HIST_CHAN_IDX_0; i < VS_HIST_CHAN_IDX_COUNT; i++) {
		const struct vs_drm_blob_state *hist_chan_state = &crtc_state->hist_chan[i];

		/* update only on changed blob state */
		if (hist_chan_state->changed) {
			struct drm_vs_hist_chan *config = NULL;

			if (hist_chan_state->blob)
				config = hist_chan_state->blob->data;

			/* update histogram configuration */
			vs_dc_hist_chan_update(hw, display, i, config);
		}
	}

	return true;
}

/*
 * @brief Update histogram rgb configuration
 */
bool vs_dc_hist_rgb_update(struct dc_hw *hw, u8 display_id, const struct vs_crtc_state *crtc_state)
{
	struct dc_hw_display *display = &hw->display[display_id];
	struct dc_hw_hist_rgb *hw_hist_rgb = &display->hw_hist_rgb;

	if (!display->info || !display->info->rgb_hist)
		return false;

	hw_hist_rgb->dirty = crtc_state->hist_rgb_enable != hw_hist_rgb->enable;
	hw_hist_rgb->enable = crtc_state->hist_rgb_enable;

	return true;
}

/*
 * @brief Free gem pool node, set stage_gem_node to gem_node
 */
static void stage_gem_node_reset(struct vs_gem_pool *gem_pool,
				 struct vs_gem_node **stage_gem_node,
				 struct vs_gem_node *gem_node)
{
	vs_gem_pool_node_release(gem_pool, *stage_gem_node);
	*stage_gem_node = gem_node;
}

/*
 * @brief Free gem pool node, reset stage_gem_node
 */
static void stage_gem_node_release(struct vs_gem_pool *gem_pool,
				   struct vs_gem_node **stage_gem_node)
{
	stage_gem_node_reset(gem_pool, stage_gem_node, NULL);
}

/*
 * @brief Release range of stage gem_nodes
 */
static void stage_gem_node_release_upto(struct vs_gem_pool *gem_pool,
					struct vs_gem_node *gem_node[VS_HIST_STAGE_COUNT],
					enum dc_hw_hist_stage stage
					)
{
	if (stage > VS_HIST_STAGE_COUNT)
		return;

	for (int i = 0; i < stage; i++)
		if (gem_node[i])
			stage_gem_node_release(gem_pool, &gem_node[i]);
}

/*
 * @brief Configure histogram channel
 *
 * Function configures histogram channel. The configuration data is passed from
 * userspace via DRM property blob.
 * Note, executed only on change (dirty state)
 */
static void vs_dc_hist_chan_commit(struct dc_hw *hw, u8 display_id, struct dc_hw_display *display,
				   const enum drm_vs_hist_idx idx)
{
	struct dc_hw_hist_chan *hw_hist_chan;
	const struct drm_vs_hist_chan *hist_config;
	u8 hw_id;
	u32 offset;
	u32 hist_control;
	bool enable;
	struct vs_dc *dc = dev_get_drvdata(hw->dev);
	struct vs_crtc *vs_crtc = dc->crtc[display_id];
	struct vs_gem_pool *gem_pool;
	struct vs_gem_node *gem_node;
	unsigned long flags;

	if (idx >= VS_HIST_CHAN_IDX_COUNT)
		return;

	hw_id = display->info->id;
	hw_hist_chan = &display->hw_hist_chan[idx];
	hist_config = &hw_hist_chan->drm_config;
	gem_pool = &vs_crtc->hist_chan_gem_pool[idx];

	spin_lock_irqsave(&hw->histogram_slock, flags);

	enable = hw_hist_chan->enable;

	/* query node */
	if (enable) {
		/* release CONFIG and RUNNING nodes if used */
		stage_gem_node_release_upto(gem_pool, hw_hist_chan->gem_node, VS_HIST_STAGE_DONE);

		/* get unused gem_node */
		gem_node = vs_gem_pool_node_get(gem_pool);
		hw_hist_chan->gem_node[VS_HIST_STAGE_CONFIG] = gem_node;
		if (!gem_node) {
			spin_unlock_irqrestore(&hw->histogram_slock, flags);
			dev_err(hw->dev, "%s: unable to get histogram gem_node for chan %d\n",
				__func__, idx);
			return;
		}
	} else {
		stage_gem_node_release_upto(gem_pool, hw_hist_chan->gem_node, VS_HIST_STAGE_USER);
	}
	spin_unlock_irqrestore(&hw->histogram_slock, flags);

	/* calculate channel offset */
	offset = hist_get_channel_offset(hw_id, idx);

	/* read current histogram control mode */
	hist_control = dc_read(hw, DCREG_PANEL0_HIST0_CONTROL_Address + offset);

	/* allow option to only enable|disable histogram without overwriting configuration bits */
	if (enable) {
		const struct drm_vs_rect *roi = &hist_config->roi;
		const struct drm_vs_rect *block = &hist_config->blocked_roi;
		u32 rd_confirm = 0; /* read confirm register hist_config */

		/* position */
		dc_write_relaxed(hw, DCREG_PANEL0_HIST0_INPUT_SELECT_Address + offset,
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_INPUT_SELECT, VALUE, hist_config->pos));

		/* bin mode */
		dc_write_relaxed(hw, DCREG_PANEL0_HIST0_BIN_MODE_Address + offset,
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_BIN_MODE, VALUE,
				      hist_config->bin_mode));

		/* weight coefficients */
		if (hist_config->bin_mode == VS_HIST_BIN_MODE_WEIGHTS) {
			dc_write_relaxed(hw, DCREG_PANEL0_HIST0_BIN_COEF0_Address + offset,
				 hist_config->weights[0]);
			dc_write_relaxed(hw, DCREG_PANEL0_HIST0_BIN_COEF1_Address + offset,
				 hist_config->weights[1]);
			dc_write_relaxed(hw, DCREG_PANEL0_HIST0_BIN_COEF2_Address + offset,
				 hist_config->weights[2]);
		}

		/* roi */
		dc_write_relaxed(hw, DCREG_PANEL0_HIST0_ROI_ORIGIN_Address + offset,
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_ROI_ORIGIN, X, roi->x) |
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_ROI_ORIGIN, Y, roi->y));
		dc_write_relaxed(hw, DCREG_PANEL0_HIST0_ROI_SIZE_Address + offset,
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_ROI_SIZE, WIDTH, roi->w) |
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_ROI_SIZE, HEIGHT, roi->h));

		/* blocked roi */
		dc_write_relaxed(hw, DCREG_PANEL0_HIST0_BLOCK_ROI_ORIGIN_Address + offset,
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_BLOCK_ROI_ORIGIN, X, block->x) |
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_BLOCK_ROI_ORIGIN, Y, block->y));
		dc_write_relaxed(hw, DCREG_PANEL0_HIST0_BLOCK_ROI_SIZE_Address + offset,
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_BLOCK_ROI_SIZE, WIDTH, block->w) |
			 VS_SET_FIELD(0, DCREG_PANEL0_HIST0_BLOCK_ROI_SIZE, HEIGHT, block->h));

		/* read-confirm control */
		if (hist_chan_flags & DC_HW_HISTOGRAM_READCONFIRM) {
			rd_confirm = VS_SET_FIELD(0, DCREG_PANEL0_HIST0_READ_CONFIRM,
						  ENABLE, 1);
			rd_confirm = VS_SET_FIELD(rd_confirm, DCREG_PANEL0_HIST0_READ_CONFIRM,
						  UPDATE, 1);
		}
		dc_write_relaxed(hw, DCREG_PANEL0_HIST0_READ_CONFIRM_Address + offset, rd_confirm);

		/* WDMA configuration */
		if (vs_dc_hist_chan_is_wdma()) {
			dc_write_relaxed(hw, DCREG_PANEL0_HIST0_WB_ADDRESS_Address + offset,
				 lower_32_bits(gem_node->paddr));
			dc_write_relaxed(hw, DCREG_PANEL0_HIST0_WB_HIGH_ADDRESS_Address + offset,
				 upper_32_bits(gem_node->paddr));

			/* enable wdma. just update variable here: it gets updated later */
			hist_control = VS_SET_FIELD(hist_control, DCREG_PANEL0_HIST0_CONTROL,
						    WB_ENABLE, 1);
		} else {
			/* disable wdma. just update variable here: it gets updated later */
			hist_control = VS_SET_FIELD(hist_control, DCREG_PANEL0_HIST0_CONTROL,
						    WB_ENABLE, 0);
		}
	}

	/* enable | disable histogram control flags (including WDMA if set above) */
	hist_control = VS_SET_FIELD(hist_control, DCREG_PANEL0_HIST0_CONTROL, ENABLE, enable);
	dc_write(hw, DCREG_PANEL0_HIST0_CONTROL_Address + offset, hist_control);
}

/*
 * @brief Configure histogram channels
 */
bool vs_dc_hist_chans_commit(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_display *display = &hw->display[display_id];

	/* don't process if unsupported */
	if (!display->info || !display->info->histogram)
		return false;

	for (int i = VS_HIST_CHAN_IDX_0; i < VS_HIST_CHAN_IDX_COUNT; i++) {
		struct dc_hw_hist_chan *hw_hist_chan = &display->hw_hist_chan[i];

		/* no changes ? */
		if (!hw_hist_chan->dirty)
			continue;

		// TODO(b/458277944): More fine-grained DPU secure control.
		mutex_lock(&hw->secure_lock);
		if (!bitmap_empty(hw->secured_layers_mask, HW_PLANE_NUM)) {
			dev_err_ratelimited(
				hw->dev,
				"Active secure layers. Skipping histogram commit on idx: %d", i);
			mutex_unlock(&hw->secure_lock);
			return false;
		}

		vs_dc_hist_chan_commit(hw, display_id, display, i);
		mutex_unlock(&hw->secure_lock);
	}

	return true;
}

/*
 * @brief Reset histogram channels by resetting stage gem node bindings
 */
void vs_dc_hist_chans_reset(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_display *display = &hw->display[display_id];
	struct vs_dc *dc = dev_get_drvdata(hw->dev);
	struct vs_crtc *vs_crtc = dc->crtc[display_id];
	struct dc_hw_hist_chan *hw_hist_chan;
	struct vs_gem_pool *gem_pool;
	unsigned long flags;
	int i;

	/* don't process if unsupported */
	if (!display->info || !display->info->histogram)
		return;

	spin_lock_irqsave(&hw->histogram_slock, flags);
	for (i = VS_HIST_CHAN_IDX_0; i < VS_HIST_CHAN_IDX_COUNT; i++) {
		hw_hist_chan = &display->hw_hist_chan[i];
		gem_pool = &vs_crtc->hist_chan_gem_pool[i];

		stage_gem_node_release_upto(gem_pool, hw_hist_chan->gem_node, VS_HIST_STAGE_COUNT);

		/* Reset bookkeeping flags */
		hw_hist_chan->enable = false;
		hw_hist_chan->dirty = false;
		hw_hist_chan->changed = false;
	}
	spin_unlock_irqrestore(&hw->histogram_slock, flags);
}

/*
 * @brief Reset RGB histogram by resetting stage gem node bindings
 */
void vs_dc_hist_rgb_reset(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_display *display = &hw->display[display_id];
	struct vs_dc *dc = dev_get_drvdata(hw->dev);
	struct vs_crtc *vs_crtc = dc->crtc[display_id];
	struct dc_hw_hist_rgb *hw_hist_rgb = &display->hw_hist_rgb;
	struct vs_gem_pool *gem_pool = &vs_crtc->hist_rgb_gem_pool;
	unsigned long flags;

	/* don't process if unsupported */
	if (!display->info || !display->info->rgb_hist)
		return;

	spin_lock_irqsave(&hw->histogram_slock, flags);

	stage_gem_node_release_upto(gem_pool, hw_hist_rgb->gem_node, VS_HIST_STAGE_COUNT);

	/* Reset bookkeeping flags */
	hw_hist_rgb->enable = false;
	hw_hist_rgb->dirty = false;

	spin_unlock_irqrestore(&hw->histogram_slock, flags);
}

/*
 * @brief Update histogram channel on flip_done
 */
static void vs_dc_hist_chan_flip_done(struct dc_hw *hw, u8 display_id,
				      const enum drm_vs_hist_idx idx)
{
	struct dc_hw_display *display = &hw->display[display_id];
	struct dc_hw_hist_chan *hw_hist_chan = &display->hw_hist_chan[idx];

	/* clear changes flag */
	if (hw_hist_chan->dirty)
		hw_hist_chan->dirty = false;
	if (hw_hist_chan->changed)
		hw_hist_chan->changed = false;
}

/*
 * @brief Update histogram channels on flip_done
 */
static bool vs_dc_hist_chans_flip_done(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_display *display = &hw->display[display_id];

	/* don't process if unsupported */
	if (!display->info || !display->info->histogram)
		return false;

	/* handle histogram channels */
	for (int i = VS_HIST_CHAN_IDX_0; i < VS_HIST_CHAN_IDX_COUNT; i++)
		vs_dc_hist_chan_flip_done(hw, display_id, i);

	return true;
}

static bool vs_dc_hist_rgb_flip_done(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_display *display = &hw->display[display_id];
	struct dc_hw_hist_rgb *hw_hist_rgb = &display->hw_hist_rgb;

	if (!display->info || !display->info->rgb_hist)
		return false;

	/* clear changes flag */
	if (hw_hist_rgb->dirty)
		hw_hist_rgb->dirty = false;

	return true;
}

/*
 * @brief Capture histogram channel data
 *
 * Function captures histogram channel data.
 * We shall handle 2 cases
 *    1. WDMA: only histogram configuration should be preserved
 */
static void vs_dc_hist_chan_collect(struct dc_hw *hw, u8 display_id,
				    struct dc_hw_display *display,
				    const enum drm_vs_hist_idx idx)
{
	struct dc_hw_hist_chan *hw_hist_chan;
	u8 hw_id;
	u32 offset;
	struct vs_dc *dc = dev_get_drvdata(hw->dev);
	struct vs_crtc *vs_crtc = dc->crtc[display_id];
	struct vs_gem_pool *gem_pool;
	struct vs_gem_node *gem_node;
	unsigned long flags;

	if (idx >= VS_HIST_CHAN_IDX_COUNT)
		return;

	hw_id = display->info->id;
	hw_hist_chan = &display->hw_hist_chan[idx];
	gem_pool = &vs_crtc->hist_chan_gem_pool[idx];
	offset = hist_get_channel_offset(hw_id, idx);

	// TODO(b/458277944): More fine-grained DPU secure control.
	mutex_lock(&hw->secure_lock);
	spin_lock_irqsave(&hw->histogram_slock, flags);

	/* check if enabled */
	if (!hw_hist_chan->enable) {
		spin_unlock_irqrestore(&hw->histogram_slock, flags);
		mutex_unlock(&hw->secure_lock);
		return;
	}

	if (!bitmap_empty(hw->secured_layers_mask, HW_PLANE_NUM)) {
		dev_err_ratelimited(
			hw->dev, "Active secure layers. Skipping histogram collection on idx: %d",
			idx);
		spin_unlock_irqrestore(&hw->histogram_slock, flags);
		mutex_unlock(&hw->secure_lock);
		return;
	}

	DPU_ATRACE_BEGIN(__func__);

	/* move gem_node out of RUNNING stage */
	gem_node = hw_hist_chan->gem_node[VS_HIST_STAGE_RUNNING];
	if (gem_node)
		hw_hist_chan->gem_node[VS_HIST_STAGE_RUNNING] = NULL;

	/*
	 * MEMIO: read data
	 */
	if (!vs_dc_hist_chan_is_wdma()) {
		if (gem_node) {
			spin_unlock_irqrestore(&hw->histogram_slock, flags);
			/* read all bins */
			for (int i = 0; i < VS_HIST_RESULT_BIN_CNT; i++) {
				struct drm_vs_hist_chan_bins *bins = gem_node->vaddr;

				bins->result[i] = dc_read_relaxed(hw,
					DCREG_PANEL0_HIST0_BIN_RESULT_Address + offset + 4 * i);
			}
			/* force reads */
			rmb();
			spin_lock_irqsave(&hw->histogram_slock, flags);
		}
	}

	/* keep old data until there is new data captured or config has changed */
	if (gem_node || hw_hist_chan->changed)
		stage_gem_node_reset(gem_pool, &hw_hist_chan->gem_node[VS_HIST_STAGE_DONE],
				     gem_node);

	/* move gem_node from CONFIG to RUNNING stage */
	gem_node = hw_hist_chan->gem_node[VS_HIST_STAGE_CONFIG];
	stage_gem_node_reset(gem_pool, &hw_hist_chan->gem_node[VS_HIST_STAGE_RUNNING], gem_node);

	/* prepare node for next frame */
	gem_node = vs_gem_pool_node_get(gem_pool);
	hw_hist_chan->gem_node[VS_HIST_STAGE_CONFIG] = gem_node;
	vs_gem_pool_node_acquire(gem_pool, gem_node);

	spin_unlock_irqrestore(&hw->histogram_slock, flags);

	if (!gem_node) {
		dev_err(hw->dev, "%s: unable to get histogram gem_node for chan %d\n", __func__,
			idx);
	} else {
		/* WDMA: configure next frame */
		if (vs_dc_hist_chan_is_wdma()) {
			dc_write(hw, DCREG_PANEL0_HIST0_WB_ADDRESS_Address + offset,
				 lower_32_bits(gem_node->paddr));
			dc_write(hw, DCREG_PANEL0_HIST0_WB_HIGH_ADDRESS_Address + offset,
				 upper_32_bits(gem_node->paddr));
		}

		spin_lock_irqsave(&hw->histogram_slock, flags);

		vs_gem_pool_node_release(gem_pool, gem_node);

		spin_unlock_irqrestore(&hw->histogram_slock, flags);
	}

	/*
	 * read_confirm: we don't need to read register back
	 * instead, just write expected configuration
	 */
	if (hist_chan_flags & DC_HW_HISTOGRAM_READCONFIRM) {
		u64 read_confirm_addr;
		u32 config;

		read_confirm_addr = DCREG_PANEL0_HIST0_READ_CONFIRM_Address + offset;
		config = VS_SET_FIELD(0, DCREG_PANEL0_HIST0_READ_CONFIRM, UPDATE, 1);
		config = VS_SET_FIELD(config, DCREG_PANEL0_HIST0_READ_CONFIRM, ENABLE, 1);

		dc_write(hw, read_confirm_addr, config);
	}

	mutex_unlock(&hw->secure_lock);
	DPU_ATRACE_END(__func__);
}

/*
 * @brief Capture histogram channels data (if required)
 */
static bool vs_dc_hist_chans_collect(struct dc_hw *hw, u8 display_id,
				     const struct dc_hw_interrupt_status *irq_status)
{
	struct dc_hw_display *display = &hw->display[display_id];

	/* don't process if unsupported */
	if (!display->info || !display->info->histogram)
		return false;

	/* handle histogram channels */
	for (int i = VS_HIST_CHAN_IDX_0; i < VS_HIST_CHAN_IDX_COUNT; i++)
		vs_dc_hist_chan_collect(hw, display_id, display, i);

	return true;
}

/*
 * @brief Configure histogram rgb
 *
 * Function configures histogram rgb.
 */
static bool hist_rgb_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	int display_id = vs_dc_hw_get_display_id(hw, hw_id);
	struct vs_dc *dc = dev_get_drvdata(hw->dev);
	struct vs_crtc *vs_crtc;
	struct dc_hw_display *display;
	struct dc_hw_hist_rgb *hw_hist_rgb;
	struct vs_gem_pool *gem_pool;
	struct vs_gem_node *gem_node;
	unsigned long flags;
	u32 hist_control = 0;

	if (display_id == -1) {
		dev_err(hw->dev, "%s: invalid hw_id(%d)\n", __func__, hw_id);
		return false;
	}

	vs_crtc = dc->crtc[display_id];
	gem_pool = &vs_crtc->hist_rgb_gem_pool;
	display = &hw->display[display_id];
	hw_hist_rgb = &display->hw_hist_rgb;

	/* query node */
	spin_lock_irqsave(&hw->histogram_slock, flags);

	if (enable) {
		/* release CONFIG and RUNNING nodes if used */
		stage_gem_node_release_upto(gem_pool, hw_hist_rgb->gem_node, VS_HIST_STAGE_DONE);

		/* get unused gem_node */
		gem_node = vs_gem_pool_node_get(gem_pool);
		hw_hist_rgb->gem_node[VS_HIST_STAGE_CONFIG] = gem_node;
		if (!gem_node) {
			spin_unlock_irqrestore(&hw->histogram_slock, flags);
			dev_err(hw->dev, "%s: unable to get histogram gem_node for RGB\n",
				__func__);
			return false;
		}
	} else {
		stage_gem_node_release_upto(gem_pool, hw_hist_rgb->gem_node, VS_HIST_STAGE_USER);
	}
	spin_unlock_irqrestore(&hw->histogram_slock, flags);

	if (enable) {
		u32 rd_confirm = 0; /* read confirm register hist_config */

		/* read-confirm control */
		if (hist_rgb_flags & DC_HW_HISTOGRAM_READCONFIRM) {
			rd_confirm = VS_SET_FIELD(0, DCREG_PANEL0_HIST_RED_READ_CONFIRM,
						  ENABLE, 1);
			rd_confirm = VS_SET_FIELD(rd_confirm,
						  DCREG_PANEL0_HIST_RED_READ_CONFIRM,
						  UPDATE, 1);
		}
		dc_write(hw, DCREG_PANEL0_HIST_RED_READ_CONFIRM_Address, rd_confirm);


		/* WDMA configuration */
		if (vs_dc_hist_rgb_is_wdma()) {
			dc_write(hw, DCREG_PANEL0_HIST_RGB_WB_ADDRESS_Address,
				 lower_32_bits(gem_node->paddr));
			dc_write(hw, DCREG_PANEL0_HIST_RGB_WB_HIGH_ADDRESS_Address,
				 upper_32_bits(gem_node->paddr));

			/* enable wdma. just update variable here: it gets updated later */
			hist_control = VS_SET_FIELD(hist_control, DCREG_PANEL0_HIST_RGB_CONTROL,
						    WB_ENABLE, 1);
		}
	}

	/* enable | disable histogram control flags (including WDMA if set above) */
	hist_control = VS_SET_FIELD(hist_control, DCREG_PANEL0_HIST_RGB_CONTROL, ENABLE, enable);
	dc_write(hw, DCREG_PANEL0_HIST_RGB_CONTROL_Address, hist_control);

	return true;
}

/*
 * @brief Configure histogram rgb
 *
 * Function configures hardcoded histogram rgb.
 * Note, executed only on change (dirty state)
 */
bool vs_dc_hist_rgb_commit(struct dc_hw *hw, u8 display_id)
{
	struct dc_hw_display *display = &hw->display[display_id];
	struct dc_hw_hist_rgb *hw_hist_rgb = &display->hw_hist_rgb;

	/* don't process if unsupported */
	if (!display->info || !display->info->histogram)
		return false;

	if (hw_hist_rgb->dirty) {
		// TODO(b/458277944): More fine-grained DPU secure control.
		mutex_lock(&hw->secure_lock);
		if (!bitmap_empty(hw->secured_layers_mask, HW_PLANE_NUM)) {
			dev_err_ratelimited(hw->dev,
					    "Active secure layers. Skipping histogram rgb commit");
			mutex_unlock(&hw->secure_lock);
			return false;
		}
		hist_rgb_config_hw(hw, display->info->id, hw_hist_rgb->enable, NULL);
		mutex_unlock(&hw->secure_lock);
	}

	return true;
}
/*
 * @brief Capture histogram rgb data (if required)
 */
static void vs_dc_hist_rgb_collect(struct dc_hw *hw, u8 display_id,
				   const struct dc_hw_interrupt_status *irq_status)
{
	struct dc_hw_display *display = &hw->display[display_id];
	struct vs_dc *dc = dev_get_drvdata(hw->dev);
	struct vs_crtc *vs_crtc = dc->crtc[display_id];
	struct vs_gem_pool *gem_pool = &vs_crtc->hist_rgb_gem_pool;
	struct dc_hw_hist_rgb *hw_hist_rgb = &display->hw_hist_rgb;
	struct vs_gem_node *gem_node;
	unsigned long flags;

	/* don't process if unsupported */
	if (!display->info || !display->info->rgb_hist)
		return;

	// TODO(b/458277944): More fine-grained DPU secure control.
	mutex_lock(&hw->secure_lock);
	spin_lock_irqsave(&hw->histogram_slock, flags);

	/* check if enabled */
	if (!hw_hist_rgb->enable) {
		spin_unlock_irqrestore(&hw->histogram_slock, flags);
		mutex_unlock(&hw->secure_lock);
		return;
	}

	if (!bitmap_empty(hw->secured_layers_mask, HW_PLANE_NUM)) {
		dev_err_ratelimited(hw->dev,
				    "Active secure layers. Skipping histogram rgb collection");
		spin_unlock_irqrestore(&hw->histogram_slock, flags);
		mutex_unlock(&hw->secure_lock);
		return;
	}

	DPU_ATRACE_BEGIN(__func__);

	/* move gem_node out of RUNNING stage */
	gem_node = hw_hist_rgb->gem_node[VS_HIST_STAGE_RUNNING];
	if (gem_node)
		hw_hist_rgb->gem_node[VS_HIST_STAGE_RUNNING] = NULL;

	/*
	 * MEMIO: read data
	 */
	if (!vs_dc_hist_rgb_is_wdma()) {
		if (gem_node) {
			spin_unlock_irqrestore(&hw->histogram_slock, flags);
			u32 offset = DCREG_PANEL0_HIST_RED_BIN_RESULT_Address;

			/* read rgb bins */
			for (int color = 0; color < 3; color++) {
				struct drm_vs_hist_chan_bins *bins = gem_node->vaddr +
					color * sizeof(struct drm_vs_hist_chan_bins);

				/* read single color space histogram bins */
				for (int i = 0; i < VS_HIST_RESULT_BIN_CNT; i++) {
					bins->result[i] = dc_read(hw, offset);
					offset += 4;
				}
			}
			spin_lock_irqsave(&hw->histogram_slock, flags);
		}
	}

	/* keep old data until there is new data captured */
	if (gem_node)
		stage_gem_node_reset(gem_pool, &hw_hist_rgb->gem_node[VS_HIST_STAGE_DONE],
				     gem_node);

	/* move gem_node from CONFIG to RUNNING stage */
	gem_node = hw_hist_rgb->gem_node[VS_HIST_STAGE_CONFIG];
	stage_gem_node_reset(gem_pool, &hw_hist_rgb->gem_node[VS_HIST_STAGE_RUNNING], gem_node);

	/* prepare node for next frame */
	gem_node = vs_gem_pool_node_get(gem_pool);
	hw_hist_rgb->gem_node[VS_HIST_STAGE_CONFIG] = gem_node;
	vs_gem_pool_node_acquire(gem_pool, gem_node);

	spin_unlock_irqrestore(&hw->histogram_slock, flags);

	/* WDMA: configure next frame */
	if (!gem_node) {
		dev_err(hw->dev, "%s: unable to get histogram gem_node for RGB\n", __func__);
	} else {
		if (vs_dc_hist_rgb_is_wdma()) {
			dc_write(hw, DCREG_PANEL0_HIST_RGB_WB_ADDRESS_Address,
				 lower_32_bits(gem_node->paddr));
			dc_write(hw, DCREG_PANEL0_HIST_RGB_WB_HIGH_ADDRESS_Address,
				 upper_32_bits(gem_node->paddr));
		}

		spin_lock_irqsave(&hw->histogram_slock, flags);

		vs_gem_pool_node_release(gem_pool, gem_node);

		spin_unlock_irqrestore(&hw->histogram_slock, flags);
	}

	/*
	 * read_confirm: write expected configuration
	 */
	if (hist_rgb_flags & DC_HW_HISTOGRAM_READCONFIRM) {
		u32 confirm;

		confirm = VS_SET_FIELD(0, DCREG_PANEL0_HIST_RED_READ_CONFIRM, UPDATE, 1);
		confirm = VS_SET_FIELD(confirm, DCREG_PANEL0_HIST_RED_READ_CONFIRM, ENABLE, 1);

		dc_write(hw, DCREG_PANEL0_HIST_RED_READ_CONFIRM_Address, confirm);
	}

	mutex_unlock(&hw->secure_lock);
	DPU_ATRACE_END(__func__);
}

/*
 * @brief Update histogram channels + rgb on flip_done
 */
bool vs_dc_hist_flip_done(struct dc_hw *hw, u8 display_id)
{
	/*
	 * Workaround (cid == 0x316 || cid == 0x32a) where histogram
	 * data needs to be read on SOF, not EOF.
	 * Note, order is important.
	 *  1. histogram collection
	 *  2. dirty flag reset
	 * context:
	 *  1. dirty channel flag is always true on first flip_done after reconfigure
	 *  2. dirty channel flag is reset inside vs_dc_hist_chans_flip_done.
	 */
	vs_dc_hist_chans_collect(hw, display_id, NULL);
	vs_dc_hist_rgb_collect(hw, display_id, NULL);

	vs_dc_hist_chans_flip_done(hw, display_id);
	vs_dc_hist_rgb_flip_done(hw, display_id);

	return true;
}
