/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#ifndef _GOOGLE_GEM_CTRL_H
#define _GOOGLE_GEM_CTRL_H

enum gem_msg_mode {
	GEM_MODE_USERCTRL = 0,
	GEM_MODE_ONCE = 1,
	GEM_MODE_INTERVAL = 2,
};

enum gem_ctrl_cmd {
	GEMCTRL_CMD_IP_CTRL = 1,
	GEMCTRL_CMD_EVENT_CTRL = 2,
	GEMCTRL_CMD_MODE_CTRL = 3,
	GEMCTRL_CMD_IP_LIST = 7,
	GEMCTRL_CMD_FILTER_CTRL = 12,
	GEMCTRL_CMD_TRACE_CTRL = 13,
};

struct gem_msg_ip_list {
	union {
		struct {
			u16 data_size;
			u32 data_pa_low;
			u32 data_pa_high;
		};
		struct {
			u16 size;
		} resp;
	};
} __packed;

struct gem_msg_ip_ctrl {
	u8 ip;
	u8 on;
} __packed;

#define GEM_EVENT_CFG_ON_OFFSET 0
#define GEM_EVENT_CFG_ON_LENGTH 1
#define GEM_EVENT_CFG_TYPE_OFFSET 1
#define GEM_EVENT_CFG_TYPE_LENGTH 2

enum gem_event_type {
	GEM_EVENT_TYPE_UNKNOWN,
	GEM_EVENT_TYPE_ACCUMULATOR,
	GEM_EVENT_TYPE_HISTORICAL_HIGH,
	GEM_EVENT_TYPE_OUTSTANDING,
};

struct gem_msg_event_ctrl {
	u8 ip;
	u8 counter_id;
	u8 event_cfg;
	u8 event_id;
	u32 buf_pa_low;
	u32 buf_pa_high;
} __packed;

struct gem_msg_mode_ctrl {
	u8 mode;
	u32 period_ms;
} __packed;

#define GEM_FILTER_CFG_CNTR_ID_OFFSET 0
#define GEM_FILTER_CFG_CNTR_ID_LENGTH 7
#define GEM_FILTER_CFG_TYPE_OFFSET 7
#define GEM_FILTER_CFG_TYPE_LENGTH 1

#define GEM_FILTER_TYPE_COUNTER 0
#define GEM_FILTER_TYPE_TRACE 1

#define GEM_FILTER_CMD_OP_RESET    0
#define GEM_FILTER_CMD_OP_ADD_LOW  1
#define GEM_FILTER_CMD_OP_ADD_HIGH 2
#define GEM_FILTER_CMD_OP_DISABLE  3

struct gem_msg_filter_ctrl {
	u8 ip;
	u8 cfg;
	u8 op;
	u8 type;
	u32 value;
	u32 mask;
} __packed;

struct gem_msg_trace_ctrl {
	u8 ip;
	u8 enable;
	u8 id;
	u8 types;
} __packed;

#endif // _GOOGLE_GEM_CTRL_H
