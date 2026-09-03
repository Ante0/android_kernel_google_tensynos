/* SPDX-License-Identifier: GPL-2.0  WITH Linux-syscall-note */

#ifndef _UAPI_T900_GPS_DUMP_H_
#define _UAPI_T900_GPS_DUMP_H_

#include <linux/types.h>

#define GPS_SSRDUMP_HEADER_MAGIC_NUMBER 0x54375727

struct gps_ssrdump_info {
	__u32 magic;
	__u32 crashinfo_size;
	__u32 coredump_size;
	char crashinfo[512];
} __packed;

#endif
