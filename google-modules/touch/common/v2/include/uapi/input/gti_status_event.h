/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_GTI_STATUS_EVENT_H
#define _UAPI_GTI_STATUS_EVENT_H

#include <linux/types.h>

#define GTI_STATUS_EVENT_MAJOR_VERSION 1
#define GTI_STATUS_EVENT_MINOR_VERSION 1

#define GTI_STATUS_EVENT_MAX_PACKET_SIZE 1024

enum gti_status_packet_type {
	GTI_STATUS_PACKET_TYPE_INVALID = 0,
	GTI_STATUS_PACKET_TYPE_EXTEND_TOUCH_STATUS_EVENT = 1,
	GTI_STATUS_PACKET_TYPE_TOUCH_SUEZ = 2,
};

enum gti_status_event_type {
	GTI_STATUS_EVENT_TYPE_INVALID = 0,
	GTI_STATUS_EVENT_TYPE_INVALID_GESTURE = 1,
};

enum gti_status_invalid_gesture_type {
	GTI_STATUS_GESTURE_UNKNOWN = 0x00,

	/* STTW (Single Tap to Wake) */
	GTI_STATUS_INVALID_GESTURE_STTW_OUT_OF_RANGE = 0x01,
	GTI_STATUS_INVALID_GESTURE_STTW_SHIFT = 0x02,
	GTI_STATUS_INVALID_GESTURE_STTW_PALM = 0x03,
	GTI_STATUS_INVALID_GESTURE_STTW_MULTI_TOUCH = 0x04,
	GTI_STATUS_INVALID_GESTURE_STTW_LONG_PRESS = 0x05,
	GTI_STATUS_INVALID_GESTURE_STTW_RAPID_TOUCH = 0x06,

	/* LPTW (Long Press to Wake) */
	GTI_STATUS_INVALID_GESTURE_LPTW_OUT_OF_RANGE = 0x11,
	GTI_STATUS_INVALID_GESTURE_LPTW_SHIFT = 0x12,
	GTI_STATUS_INVALID_GESTURE_LPTW_TOO_SMALL = 0x13,
	GTI_STATUS_INVALID_GESTURE_LPTW_PALM = 0x14,
	GTI_STATUS_INVALID_GESTURE_LPTW_RAPID_TOUCH = 0x15,
};

struct gti_status_packet_header {
	__u32 packet_size;
	__u16 packet_type;
	__u8 reserved[2];
} __attribute__((packed));

struct gti_status_event_header {
	__u32 size;
	__u16 event_type;
	__u8 reserved[2];
} __attribute__((packed));

struct gti_status_invalid_gesture_event {
	__u64 timestamp;
	__u16 x_down;
	__u16 y_down;
	__u16 x_valid;
	__u16 y_valid;
	__u16 x_final;
	__u16 y_final;
	__u16 distance;

	__u8 gesture_type;
	__u8 node_count;
	__u8 finger_count;
	__u8 frame_count;

	__u8 reserved[6];
} __attribute__((packed));

#endif /* _UAPI_GTI_STATUS_EVENT_H */
