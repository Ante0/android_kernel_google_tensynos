/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DC_WRITEBACK_H__
#define __VS_DC_WRITEBACK_H__

#include "drm/vs_drm.h"
#include "vs_dc_info.h"
#include "vs_dc_property.h"

extern u8 wb_split_id;

bool vs_dc_register_writeback_states(struct vs_dc_property_state_group *states,
				     const struct vs_wb_info *wb_info);

#endif
