/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 */

int handle_hotplug(struct dptx *dptx);
int handle_hotplug_core(struct dptx *dptx);
int handle_hotunplug(struct dptx *dptx);
int handle_hotunplug_core(struct dptx *dptx, bool hpd_irq);
int handle_sink_request(struct dptx *dptx);
