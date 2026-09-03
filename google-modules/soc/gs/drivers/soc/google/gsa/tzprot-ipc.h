/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2024 Google LLC
 */

/* This header is internal only.
 *
 * Public APIs are in //private/google-modules/soc/gs/include/linux/gsa/
 *
 * Include via //private/google-modules/soc/gs:gs_soc_headers
 */

#ifndef __LINUX_TZPROT_IPC_H
#define __LINUX_TZPROT_IPC_H

#include <linux/types.h>

#define TZPROT_PORT "com.android.trusty.media_prot"
#define MAX_HIST_CHANNELS 4U
#define MIN_MEDIA_PROT_RSP 8U

enum media_prot_cmd {
	MEDIA_PROT_CMD_RESP = BIT(31),
	MEDIA_PROT_CMD_SET_IP_PROT = 0,
	MEDIA_PROT_CMD_GET_HISTOGRAM = 1,
};

enum device_hist_id {
	PANEL_0_BE_HIST_8K_ID,
	PANEL_1_BE_HIST_8K_ID,
	PANEL_0_BE_RGB_HIST_4K_ID,
};

enum histogram_channel_mask {
	HIST_CHANNEL_0 = BIT(0),
	HIST_CHANNEL_1 = BIT(1),
	HIST_CHANNEL_2 = BIT(2),
	HIST_CHANNEL_3 = BIT(3),
	HIST_CHANNEL_RGB = BIT(0) | BIT(1) | BIT(2),
};

struct media_prot_hist_luma_req {
	uint8_t hist_id;
	uint8_t channel_mask;
};

struct media_prot_set_ip_prot_req {
	u32 dev_enable_mask;
	u32 dev_disable_mask;
};

struct media_prot_req {
	u32 cmd;
	union {
		struct media_prot_set_ip_prot_req set_ip_prot_req;
		struct media_prot_hist_luma_req get_hist_luma_req;
	};
};

struct media_prot_luma_rsp {
	u16 chan_luma[MAX_HIST_CHANNELS];
};

struct media_prot_rsp {
	uint32_t cmd;
	int32_t err;
	struct media_prot_luma_rsp get_hist_luma_rsp;
};

#endif
