/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 VeriSilicon Holdings Co., Ltd.
 */
#ifndef __VS_DC_DSC_H__
#define __VS_DC_DSC_H__

#include "vs_dc_hw.h"

int dc_hw_config_dsc(struct dc_hw *hw, u8 hw_id, const struct dc_hw_dsc_usage *sw_cfg,
		     const struct drm_dsc_config *dsc_cfg);
int dc_hw_read_dsc_status(struct dc_hw *hw, u8 hw_id);
#endif
