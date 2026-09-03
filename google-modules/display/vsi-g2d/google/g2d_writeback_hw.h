/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef _G2D_WRITEBACK_HW_H_
#define _G2D_WRITEBACK_HW_H_

struct sc_hw;
void wb_hw_commit(struct sc_hw *hw, u8 hw_id, struct drm_rect *dst);

#endif /* _G2D_WRITEBACK_HW_H_ */
