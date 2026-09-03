/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"

/*
 *       ALPM - Advanced Link Power Management
 */

#define RECEIVER_ALPM_CAPABILITIES 0x0002E
#define RECEIVER_ALPM_CONFIGURATIONS 0x00116
#define FW_SLEEP 1
#define FW_STANDBY 0

int alpm_is_available(struct dptx *dptx);
int alpm_get_status(struct dptx *dptx);
int alpm_set_status(struct dptx *dptx, int value);
int alpm_get_state(struct dptx *dptx);
int alpm_set_state(struct dptx *dptx, int state);
int fill_as_sdp_header(struct dptx *dptx, struct adaptive_sync_sdp_data *sdp);
int fill_as_sdp(struct dptx *dptx, struct adaptive_sync_sdp_data *sdp, u8 adaptive_sync_mode);
