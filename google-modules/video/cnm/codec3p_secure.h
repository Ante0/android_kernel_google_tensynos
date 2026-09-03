/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#if defined(__KERNEL__)
 #include <linux/types.h>
 #define __PACKED __packed
#else
 #include <stdint.h>
#endif

#define CODEC3P_SECURE_PORT_NAME        "com.android.trusty.pixel.codec3p"
#define CODEC3P_SECURE_MAX_MSG_QUEUE    1
#define CODEC3P_SECURE_MAX_MSG_SIZE     64

struct codec3p_firmware_memory {
	uint64_t pa;       /* Physical address */
	uint32_t size;     /* Size in bytes */
	uint32_t blob_size;
} __PACKED;

/**
 * enum codec_prot_command - command identifiers for codec protection functions
 * @CODEC3P_SECURE_REQ_PROT_FW: protect it, verify FW, and configure SEC SMMU
 * @CODEC3P_SECURE_REQ_UNPROT_FW: clear SEC SMMU configuration, and un-protect FW
 *
 */
enum codec3p_secure_command {
	CODEC3P_SECURE_REQ_PROT_FW = 1,
	CODEC3P_SECURE_REQ_UNPROT_FW = 2,
};

/**
 * IPC request base of all requests.
 */
struct codec3p_secure_req_base {
	uint32_t command;  /* codec3p_secure_command */
	uint32_t version;
} __PACKED;

struct codec3p_secure_prot_fw_req {
	struct codec3p_secure_req_base base;
	struct codec3p_firmware_memory firmware;
} __PACKED;

/**
 * union of all reqs, used to know the max size of all reqs
 */
struct codec3p_secure_req {
	union {
		struct codec3p_secure_req_base base;
		struct codec3p_secure_prot_fw_req fw_req;
	};
} __PACKED;

/**
 * IPC response base of all responses.
 */
struct codec3p_secure_rsp {
	uint32_t command;  /* codec3p_secure_command */
	uint32_t result;   /* 0 indicates success */
} __PACKED;