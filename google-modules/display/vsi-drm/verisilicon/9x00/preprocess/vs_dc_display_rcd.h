/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DC_DISPLAY_RCD_H__
#define __VS_DC_DISPLAY_RCD_H__

#include "drm/vs_drm.h"
#include "vs_dc_info.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"

void rcd_fb_config_hw(struct dc_hw *hw, struct dc_hw_rcd_mask *rcd_mask);
void rcd_enable(struct dc_hw *hw, struct dc_hw_rcd_mask *rcd_mask);
void rcd_roi_config_hw(struct dc_hw *hw, struct dc_hw_rcd_mask *rcd_mask);

#endif
