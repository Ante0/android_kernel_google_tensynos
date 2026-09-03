/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_AUTH_IMAGE_FORMAT_H
#define _GOOGLE_AUTH_IMAGE_FORMAT_H

#include <linux/types.h>

#define GOOGLE_AUTH_IMAGE_FORMAT_HEADER_SIZE 0x1000

struct google_auth_image_header_v2 {
	u32 magic;
	u32 generation;
	u32 rollback_info;
	u32 length;
	u8 flags[16];
	u8 body_hash[64];
	u8 chip_id[32];
	u8 auth_config[256];
	u8 image_config[256];
} __packed;

struct google_auth_image_header {
	u8 signature[512];
	u8 publickey[512];
	struct google_auth_image_header_v2 header_v2;
} __packed;

#endif /* _GOOGLE_AUTH_IMAGE_FORMAT_H */
