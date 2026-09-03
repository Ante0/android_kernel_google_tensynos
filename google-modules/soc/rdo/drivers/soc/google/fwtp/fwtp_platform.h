/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint platform services header.
 */

#ifndef __FWTP_PLATFORM_H
#define __FWTP_PLATFORM_H

#include <linux/dev_printk.h>
#include <linux/slab.h>

/* CPP header guards. */
#define __BEGIN_CDECLS
#define __END_CDECLS

/* stdint.h defs. */
typedef u32 uint32_t;
typedef u64 uint64_t;

/* Map FWTP error codes to Linux error codes. */
#include "fwtp_err.h"
#define kFwtpErrMemAlloc -ENOMEM
#define kFwtpErrBadMsg -EBADMSG
#define kFwtpErrNotFound -ENOENT
#define kFwtpErrTooBig -E2BIG
#define kFwtpOk 0

/**
 * struct fwtp_if_platform - Structure containing platform specific fields for
 *                           an FWTP interface.
 *
 * @dev: Kernel device record.
 */
struct fwtp_if_platform {
	struct device *dev;
};

/**
 * FWTP_MALLOC - Allocates a block of memory for use by FWTP.
 *
 * @size: Size of block to allocate.
 *
 * Return: Pointer to allocated memory block or NULL on failure.
 */
#define FWTP_MALLOC(size) kvmalloc(size, GFP_KERNEL)

/**
 * FWTP_FREE - Frees an allocated block of memory.
 *
 * @ptr: Pointer to memory block to free.
 */
#define FWTP_FREE(ptr) kvfree(ptr)

/**
 * FWTP_IF_LOG_ERR - Logs an error message for an FWTP interface.
 *
 * @fwtp_if: FWTP interface for which to log message.
 * @msg: Message to log.
 * @...: Arguments for message.
 */
#define FWTP_IF_LOG_ERR(fwtp_if, msg, ...) \
	dev_err((fwtp_if)->platform.dev, msg, ##__VA_ARGS__)

/**
 * FWTP_IF_LOG_WARN - Logs a warning message for an FWTP interface.
 *
 * @fwtp_if: FWTP interface for which to log message.
 * @msg: Message to log.
 * @...: Arguments for message.
 */
#define FWTP_IF_LOG_WARN(fwtp_if, msg, ...) \
	dev_warn((fwtp_if)->platform.dev, msg, ##__VA_ARGS__)

#endif /* __FWTP_PLATFORM_H */
