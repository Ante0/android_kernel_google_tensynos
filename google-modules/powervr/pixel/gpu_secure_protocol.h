/* SPDX-License-Identifier: GPL-2.0 */

/**
 * IPC protocol for the gpu_secure Trusty app.
 *
 *  Requests are a gpu_secure_req_base_t, or a struct that extends it.
 * Responses are a gpu_secure_rsp_base_t, or a struct that extends it.
 *
 * Every request generates one response, with the same command type.
 */

#pragma once

#if !defined(__KERNEL__)
 #include <stddef.h>
#endif

#ifdef __KERNEL__
#define PACKED_ALIGNED(a) __packed __aligned(a)
#else
#define PACKED_ALIGNED(a) __attribute__((packed, aligned(a)))
#endif

/**
 * IPC port details.
 */
#define GPU_SECURE_PORT_NAME        "com.android.trusty.pixel_gpu"
#define GPU_SECURE_MAX_MSG_QUEUE    1
#define GPU_SECURE_MAX_MSG_SIZE     1024
#define GPU_SECURE_MAX_IMAGE_SIZE   (8 * 1024 * 1024)

/**
 * IPC request commands.
 */
#define GPU_SECURE_CMDS {                                    \
	GPU_SECURE_CONV(GPU_SECURE_REQ_PING),                \
	GPU_SECURE_CONV(GPU_SECURE_REQ_SEND_FIRMWARE_IMAGE), \
	GPU_SECURE_CONV(GPU_SECURE_REQ_START),               \
	GPU_SECURE_CONV(GPU_SECURE_REQ_STOP),                \
	GPU_SECURE_CONV(GPU_SECURE_FAULT)                    \
}

#define GPU_SECURE_CONV(x) x
typedef enum GPU_SECURE_CMDS gpu_secure_req_command_t;
#undef GPU_SECURE_CONV

// versions for command GPU_SECURE_REQ_SEND_FIRMWARE_IMAGE
typedef enum gpu_secure_req_firmware_version_t {
	GPU_SECURE_REQ_FIRMWARE_VERSION_PRE_25_2,
	GPU_SECURE_REQ_FIRMWARE_VERSION_25_2,
	GPU_SECURE_REQ_FIRMWARE_VERSION_COUNT,
} gpu_secure_req_firmware_version_t;

/**
 * IPC request base -- the common denominator of all requests.
 */
typedef struct gpu_secure_req_base_t {
	uint32_t command;  /* gpu_secure_req_command_t */
	/* Add versioning per command */
	uint32_t version;
} gpu_secure_req_base_t;

/**
 * IPC response base -- the common denominator of all responses.
 */
typedef struct gpu_secure_rsp_base_t {
	uint32_t command;  /* gpu_secure_req_command_t */
	uint32_t result;   /* PVRSRV_ERROR, where 0 indicates success */
} gpu_secure_rsp_base_t;

/**
 * The four sections required by the GPU firmware.
 */
typedef enum {
	GPU_SECURE_SECTION_CODE,
	GPU_SECURE_SECTION_DATA,
	GPU_SECURE_SECTION_RODATA,
	GPU_SECURE_SECTION_COREMEM_CODE,
	GPU_SECURE_SECTION_COREMEM_DATA,
	GPU_SECURE_SECTION_COUNT
} gpu_secure_section_t;

/**
 * A block of GPU-mapped secure memory.
 */
typedef struct gpu_secure_memory_t {
	uint64_t pa;       /* Physical address (must be inside carveout) */
	uint32_t fw_va;    /* GPU firmware MCU virtual address (32-bit VA) */
	uint32_t size;     /* Size in bytes */
} gpu_secure_memory_t;

/**
 * IPC request -- GPU_SECURE_REQ_SEND_FIRMWARE_IMAGE.
 */
typedef struct gpu_secure_req_firmware_t {
	/** All requests must begin with this. */
	gpu_secure_req_base_t base;

	/**
	 * The firmware image body metadata inside the carveout.
	 */
	gpu_secure_memory_t image_body;
	/**
	 * The firmware image header metadata inside the carveout.
	 */
	gpu_secure_memory_t image_header;
	/**
	 * The four firmware sections, inside the carveout.
	 */
	gpu_secure_memory_t section[GPU_SECURE_SECTION_COUNT];

	/**
	 * Whether START and STOP should be handled by TF-A instead of gpu_secure.
	 */
	uint32_t use_tf_a;

	/**
	 * The page size shift that the host uses, this is used to communicate in
	 * case the host uses 16k page size.
	 */
	uint32_t host_page_shift;

	/*
	 * Any new additional fields must be added here.
	 */
	uint8_t reserved1[384];
} PACKED_ALIGNED(8)
gpu_secure_req_firmware_t;

_Static_assert(sizeof(gpu_secure_req_firmware_t) == GPU_SECURE_MAX_MSG_SIZE/2,
		"gpu_secure: gpu_secure_req_firmware_t is larger than MAX_MSG_SIZE/2");

/**
 * IPC request -- GPU_SECURE_FAULT.
 */
typedef struct gpu_secure_req_fault_t {
	/** All requests must begin with this. */
	gpu_secure_req_base_t base;

	/** Debug session counter to trace the exact REE --> TEE call */
	uint32_t session_counter;

	/*
	 * Any new additional fields must be added here.
	 */
	uint8_t reserved[116];
} PACKED_ALIGNED(8)
gpu_secure_req_fault_t;

_Static_assert(sizeof(gpu_secure_req_fault_t) == 128,
		"gpu_secure: gpu_secure_req_fault_t is not 128 bytes");

/* TF-A interface */

#define SMC_OEM_GPU_CONTROL 0xC300000C
// x1 = OEM_GPU_CONTROL_subfunc

#define OEM_GPU_CONTROL_SETUP 0
// x2 = GPU FW IPA
#define OEM_GPU_CONTROL_START 1
#define OEM_GPU_CONTROL_STOP  2
