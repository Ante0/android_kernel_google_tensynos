/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2024 Google LLC.
 *
 * Authors: Henry Daitx <daitx@google.com>, Jack Diver <diverj@google.com>
 *
 * `struct pixel_fw_info` describes the format of the footer we insert into
 * the rgxfw binary. The footer data is authored as part of the firmware build,
 * and consumed at runtime by the GPU kernel module, or TEE gpu_secure app.
 * This header file is shared between all three modules (FW, KM, TEE), to ensure
 * a consistent view of the footer layout. Care should be taken to ensure that
 * the header is kept in sync across all three modules.
 */
#pragma once

#if !defined(__KERNEL__)
#include <stdint.h>
#endif

/* The footer occupies a 2K region at the end of the firmware binary */
#define PIXEL_FOOTER_AREA_OFFSET_FROM_END 0x800

/* Arbitrary magic */
#define PIXEL_FOOTER_MAGIC 0x1BADD00D

/* This version must be bumped if a breaking change to the footer is made */
#define PIXEL_FOOTER_VERSION 0

/* Max number of physical address ranges the footer could contain */
#define PIXEL_MAX_PA_RANGE_COUNT 64

/* Max length for UINT64_T is 20, keeping it aligned to 8 bytes.*/
#define MAX_LEN_BUILD_ID 24

/* The page size of the mapping for pixel physical address ranges. */
#define PIXEL_PA_RANGE_PAGE_SIZE (1UL << 14)

/**
 * struct pixel_fw_pa_range - Physical address range
 */
struct __attribute__((packed)) pixel_fw_pa_range {
	/** @base_pa: Start address for the range */
	uint64_t base_pa;

	/** @extent: Size of the range */
	uint64_t extent;

	/** @flags: Reserved for future use */
	uint32_t flags;
};

/**
 * struct pixel_fw_info - Footer layout
 */
struct __attribute__((packed)) pixel_fw_info {
	/** @fwabi_version: Version of the FW/KM interface */
	uint32_t fwabi_version;

	/** @magic: Magic number to detect footer presence, should not change */
	uint32_t magic;

	/** @footer_version: Version of the footer layout */
	uint32_t footer_version;

	/** @pa_range_count: Number of physical ranges stored in the footer */
	uint32_t pa_range_count;

	/** @fw_build_id: Build ID of the firmware. */
	const char fw_build_id[MAX_LEN_BUILD_ID];

	/** @_reserved: Reserved for future use */
	uint8_t _reserved[104];

	/** @pa_ranges: Physical address ranges to be mapped for FW access */
	struct pixel_fw_pa_range pa_ranges[PIXEL_MAX_PA_RANGE_COUNT];
};

static_assert(sizeof(struct pixel_fw_info) <= PIXEL_FOOTER_AREA_OFFSET_FROM_END,
		"Footer area ran out of space");
