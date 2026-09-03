/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2023 Google LLC */

#ifndef _GOOGLE_CLK_CPM_H
#define _GOOGLE_CLK_CPM_H

#include <linux/clk-provider.h>

int goog_cpm_clk_toggle_auto_gate(struct clk *clk, bool enable);
int goog_cpm_clk_toggle_qch_mode(struct clk *clk, bool enable);

#endif //_GOOGLE_CLK_CPM_H
