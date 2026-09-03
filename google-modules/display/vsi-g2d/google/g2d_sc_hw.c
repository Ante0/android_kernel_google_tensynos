// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <linux/io.h>
#include <linux/types.h>

#include <gs_drm/gs_reg_dump.h>

#include "g2d_registers.h"
#include "g2d_plane_hw.h"
#include "g2d_sc_hw.h"
#include "g2d_trace.h"
#include "g2d_writeback_hw.h"

#define G2D_IP_OFFSET 0x150000

inline u32 sc_read(struct sc_hw *hw, u32 reg)
{
	u32 value = readl(hw->reg_base + reg - G2D_IP_OFFSET);

	dev_dbg(hw->dev, "%s: 0x%08x = 0x%08x\n", __func__, reg, value);
	return value;
}

inline void sc_write(struct sc_hw *hw, u32 reg, u32 value)
{
	dev_dbg(hw->dev, "%s: 0x%08x = 0x%08x\n", __func__, reg, value);
	writel(value, hw->reg_base + reg - G2D_IP_OFFSET);
}

void sc_hw_commit(struct sc_hw *hw, u8 display_id)
{
	G2D_ATRACE_BEGIN(__func__);
	plane_commit(hw, display_id);
	G2D_ATRACE_END(__func__);
}

void sc_hw_wb_commit(struct sc_hw *hw, u8 display_id, struct drm_rect *dst)
{
	G2D_ATRACE_BEGIN(__func__);
	wb_hw_commit(hw, display_id, dst);
	G2D_ATRACE_END(__func__);
}

void sc_hw_start_trigger(struct sc_hw *hw, u8 display_id)
{
	struct sc_hw_wb *wb;
	u32 wb_id = 0;

	wb = &hw->wb[display_id];
	if (display_id >= NUM_PIPELINES) {
		dev_err(hw->dev, "%s: invalid display_id: %d! ", __func__, display_id);
		return;
	}

	if (!wb) {
		dev_err(hw->dev, "%s: sc_hw_wb ptr is invalid!", __func__);
		return;
	}

	if (wb && (display_id < NUM_PIPELINES)) {
		G2D_ATRACE_INSTANT(__func__);
		sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, wb_id, START_Address), !!wb->fb.enable);
		dev_dbg(hw->dev, "%s: wrote SCREG_LAYER%d_START to %d", __func__, wb_id,
			!!wb->fb.enable);
	}
}

void sc_hw_update_wb_fb(struct sc_hw *hw, u8 id, struct sc_hw_fb *fb)
{
	struct sc_hw_wb *wb = &hw->wb[id];

	if (wb && fb) {
		if (fb->enable == false)
			wb->fb.enable = false;
		else
			memcpy(&wb->fb, fb, sizeof(*fb));

		dev_dbg(hw->dev, "%s: plane fb enable: %d, writeback fb enable: %d, id %d",
			__func__, fb->enable, wb->fb.enable, id);
	}
}

void sc_hw_update_plane(struct sc_hw *hw, u8 id, struct sc_hw_fb *fb)
{
	struct sc_hw_plane *plane = &hw->plane[id];

	if (plane && fb) {
		if (fb->enable == false)
			plane->fb.enable = false;
		else
			memcpy(&plane->fb, fb, sizeof(*fb));
	}
}

void sc_hw_update_plane_roi(struct sc_hw *hw, u8 id, struct sc_hw_roi *roi)
{
	struct sc_hw_plane *plane = &hw->plane[id];

	if (plane && roi) {
		memcpy(&plane->roi, roi, sizeof(struct sc_hw_roi));
		plane->roi.enable = true;
	}
}

void sc_hw_update_plane_scale(struct sc_hw *hw, u8 id, struct sc_hw_scale *scale)
{
	struct sc_hw_plane *plane = &hw->plane[id];

	memcpy(&plane->scale, scale, offsetof(struct sc_hw_scale, coefficients_dirty));
	plane->scale.coefficients_dirty |= scale->coefficients_dirty;
}

void sc_hw_update_plane_y2r(struct sc_hw *hw, u8 id, struct sc_hw_y2r *y2r_conf)
{
	struct sc_hw_plane *plane = &hw->plane[id];

	if (plane && y2r_conf)
		memcpy(&plane->y2r, y2r_conf, sizeof(*y2r_conf));
}

void sc_hw_restore_state(struct sc_hw *hw)
{
	int i;
	/* Load default scaling coefficients on init */
	for (i = 0; i < NUM_PIPELINES; i++)
		hw->plane[i].scale.coefficients_dirty = true;
}

void sc_hw_init(struct sc_hw *hw, struct device *dev)
{
	hw->dev = dev;

	sc_hw_restore_state(hw);
}

int sc_hw_get_interrupt(struct sc_hw *hw, struct sc_hw_interrupt_status *status)
{
	u32 intr_status = sc_read(hw, SCREG_G2D_INTR_STATUS_Address);

	if (!intr_status)
		return 0;

	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, SW_RST_DONE))
		status->reset_status = 1;

	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, PIPE0_FRAME_START))
		status->pipe_frame_start |= BIT(HW_PIPE_0);
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, PIPE1_FRAME_START))
		status->pipe_frame_start |= BIT(HW_PIPE_1);
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, PIPE0_FRAME_DONE))
		status->pipe_frame_done |= BIT(HW_PIPE_0);
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, PIPE1_FRAME_DONE))
		status->pipe_frame_done |= BIT(HW_PIPE_1);

	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, APB_HANG))
		status->apb_hang = 1;
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, AXI_RD_BUS_HANG))
		status->axi_rd_bus_hang = 1;
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, AXI_WR_BUS_HANG))
		status->axi_wr_bus_hang = 1;
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, AXI_BUS_ERROR))
		status->axi_bus_err = 1;

	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, PIPE0_PVRIC_DECODE_ERROR))
		status->pvric_decode_err |= BIT(HW_PIPE_0);
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, PIPE1_PVRIC_DECODE_ERROR))
		status->pvric_decode_err |= BIT(HW_PIPE_1);
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, PIPE0_PVRIC_ENCODE_ERROR))
		status->pvric_encode_err |= BIT(HW_PIPE_0);
	if (VS_GET_FIELD(intr_status, SCREG_G2D_INTR_STATUS, PIPE1_PVRIC_ENCODE_ERROR))
		status->pvric_encode_err |= BIT(HW_PIPE_1);

	sc_write(hw, SCREG_G2D_INTR_STATUS_Address, intr_status);

	return 0;
}

int sc_hw_enable_interrupts(struct sc_hw *hw)
{
	/* Todo(b/355089225) support enable/disable of Pipe0 and Pipe1 separately */
	sc_write(hw, SCREG_G2D_INTR_ENABLE_Address, SCREG_G2D_INTR_ENABLE_WriteMask);
	dev_dbg(hw->dev, "Interrupts enabled!");

	return 0;
}

int sc_hw_disable_interrupts(struct sc_hw *hw)
{
	/* Todo(b/355089225) support enable/disable of Pipe0 and Pipe1 separately */
	sc_write(hw, SCREG_G2D_INTR_ENABLE_Address, SCREG_G2D_INTR_ENABLE_ResetValue);
	dev_dbg(hw->dev, "Interrupts disabled!");

	return 0;
}

void sc_hw_print_id_regs(struct sc_hw *hw, struct device *dev)
{
	uint32_t chip_id, chip_rev, chip_date, chip_time, product_id;

	chip_id = sc_read(hw, SCREG_CHIP_ID_Address);
	chip_rev = sc_read(hw, SCREG_CHIP_REV_Address);
	chip_date = sc_read(hw, SCREG_CHIP_DATE_Address);
	chip_time = sc_read(hw, SCREG_CHIP_TIME_Address);
	product_id = sc_read(hw, SCREG_PRODUCT_ID_Address);

	dev_info(dev,
		 "Reading ID registers in Probe:\n"
		 "chip id:             0x%08x\n"
		 "chip revision:       0x%08x\n"
		 "release date:        0x%08x\n"
		 "release time:        0x%08x\n"
		 "product id:          0x%08x\n",
		 chip_id, chip_rev, chip_date, chip_time, product_id);
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static inline void reg_dump_block(char *block_name, void *reg_base, u32 start_reg, u32 end_reg,
				  struct drm_printer *p)
{
	u32 offset = start_reg - SCREG_HI_RESERVED0_Address;
	u32 size = end_reg - start_reg + 4;

	gs_reg_dump(block_name, reg_base, offset, size, p);
}

int sc_hw_reg_dump(struct seq_file *s, struct sc_hw *hw)
{
	struct drm_printer p = drm_seq_file_printer(s);

	reg_dump_block("ID", hw->reg_base, SCREG_CHIP_ID_Address, SCREG_CHIP_CUSTOMER_Address, &p);
	reg_dump_block("Debug", hw->reg_base, SCREG_DEBUG_TOP_AR_REQ_NUM_Address,
		       SCREG_DEBUG_LAYER1_WB_WDMA_BRESP_CNT_Address, &p);
	reg_dump_block("Layer0", hw->reg_base, SCREG_LAYER0_CONFIG_Address,
		       SCREG_LAYER0_ENC_PVRIC_COUNTER_SLAVE_R_Address, &p);
#ifndef CONFIG_VERISILICON_ONE_LAYER_G2D
	reg_dump_block("Layer1", hw->reg_base, SCREG_LAYER1_CONFIG_Address,
		       SCREG_LAYER1_ENC_PVRIC_COUNTER_SLAVE_R_Address, &p);
#endif
	return 0;
}
#endif /* CONFIG_DEBUG_FS */
