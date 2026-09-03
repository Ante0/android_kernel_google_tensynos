/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Definitions of constants and structs to communicate with DPA secure App in
 * Trusty. The definitions shall be the same as those in Trusty.
 *
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GOOGLE_DPA_SECURE_PROTOCOL_H
#define _GOOGLE_DPA_SECURE_PROTOCOL_H

#include <linux/build_bug.h>
#include <linux/types.h>

#define GOOGLE_DPA_SECURE_IPC_PORT_NAME "com.android.trusty.pixel.dpa"

/**
 * dpa_secure_ipc_command - command identifiers for DPA service
 * @DPA_SECURE_IPC_IMG_AUTH:
 *     DPA secure app protects the DRAM region of firmware image and performs
 *     the firmware image authentication.
 * @DPA_SECURE_IPC_IMG_UNLOAD:
 *     DPA secure app removes the protection created by DPA_SECURE_IPC_IMG_AUTH
 *     command
 * @DPA_SECURE_IPC_BOOT:
 *     DPA secure app loads firmware ELF segments into DPA SRAM and releases
 *     NCP from the wait state
 */
enum dpa_secure_ipc_command {
	DPA_IMG_AUTH,
	DPA_IMG_UNLOAD,
	DPA_BOOT,
};

/**
 * dpa_secure_ipc_fw_image - The struct for passing fw image to DPA secure app
 * @pa: Physical address of the DRAM where the firmware image is located
 * @size: Size of the image
 */
struct dpa_secure_ipc_fw_image {
	u64 pa;
	u64 size;
};
static_assert(sizeof(struct dpa_secure_ipc_fw_image) == 16,
	      "dpa_secure_ipc_fw_image is not packed.");

/**
 * dpa_secure_ipc_img_auth_req - The struct for requesting image auth
 * @ncp_img: NCP FW image
 * @nep_img: NEP FW image
 */
/* TODO(b/426465018): Remove v1 struct after the migration */
struct dpa_secure_ipc_img_auth_req_v1 {
	struct dpa_secure_ipc_fw_image ncp_img;
	struct dpa_secure_ipc_fw_image nep_img;
};
static_assert(sizeof(struct dpa_secure_ipc_img_auth_req_v1) == 32,
	      "dpa_secure_ipc_img_auth_req_v1 is not packed.");

/**
 * dpa_secure_ipc_img_auth_req_v2 - The struct for requesting image auth
 * @dpa_img: DPA FW image
 */
struct dpa_secure_ipc_img_auth_req_v2 {
	struct dpa_secure_ipc_fw_image dpa_img;
};
static_assert(sizeof(struct dpa_secure_ipc_img_auth_req_v2) == 16,
	      "dpa_secure_ipc_img_auth_req_v2 is not packed.");

/**
 * dpa_secure_ipc_req_base - The base structure of DPA IPC request
 * @version: Version number of the IPC command
 * @command: Command ID (one of dpa_secure_ipc_command)
 */
struct dpa_secure_ipc_req_base {
	u32 version;
	u32 command;
};
static_assert(sizeof(struct dpa_secure_ipc_req_base) == 8,
	      "dpa_secure_ipc_req_base is not packed.");

/**
 * dpa_secure_ipc_req - The struct for sending request to DPA secure app
 * @base: Base struct of DPA IPC request
 * @img_auth_req: Payload for IMG_AUTH command
 */
struct dpa_secure_ipc_req {
	struct dpa_secure_ipc_req_base base;
	union {
		/* TODO(b/426465018): Remove v1 struct after the migration */
		struct dpa_secure_ipc_img_auth_req_v1 img_auth_req;
		struct dpa_secure_ipc_img_auth_req_v2 img_auth_req_v2;
	};
};
static_assert(sizeof(struct dpa_secure_ipc_req) == 40, "dpa_secure_ipc_req is not packed.");

/**
 * dpa_secure_ipc_rsp - The struct of DPA secure IPC response
 * @command: Command ID (one of dpa_secure_ipc_command)
 * @result: Return code of the IPC command
 */
struct dpa_secure_ipc_rsp {
	u32 command;
	s32 result;
};
static_assert(sizeof(struct dpa_secure_ipc_rsp) == 8, "dpa_secure_ipc_rsp is not packed.");

#endif /* _GOOGLE_DPA_SECURE_PROTOCOL_H */
