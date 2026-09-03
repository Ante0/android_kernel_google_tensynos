/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */
#ifndef __VS_DC_DISPLAY_3D_LUT_H__
#define __VS_DC_DISPLAY_3D_LUT_H__

#include "vs_dc_info.h"
#include "vs_dc_property.h"

bool vs_dc_register_display_3d_lut_states(struct vs_dc_property_state_group *states,
					  const struct vs_display_info *display_info);

#endif
