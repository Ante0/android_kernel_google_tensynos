/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef G2D_PLANE_HW_H_
#define G2D_PLANE_HW_H_

struct sc_hw;
void plane_commit(struct sc_hw *hw, u8 layer_id);

#endif //G2D_PLANE_HW_H_
