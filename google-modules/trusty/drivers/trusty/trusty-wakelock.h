/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google, Inc.
 */

#ifndef __TRUSTY_WAKELOCK_H__
#define __TRUSTY_WAKELOCK_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define TZ_SUSPEND_SERVICE_PORT "com.android.trusty.suspendservice"

/**
 * SUSPENDSERVICE_ACQUIRE_MAX_CLIENT_COUNT - Maximum number of active suspend
 *                                           blocks allowed per client connection.
 */
#define SUSPENDSERVICE_ACQUIRE_MAX_CLIENT_COUNT 8

/**
 * SUSPENDSERVICE_CMD_RESP - Bit set in suspendservice_cmd to indicate a
 *                           response.
 */
#define SUSPENDSERVICE_CMD_RESP 0x80000000u

/**
 * enum suspendservice_cmd - command identifiers for suspendservice functions
 * @SUSPENDSERVICE_CMD_ACQUIRE_BLOCK: Acquire suspend blocker
 * @SUSPENDSERVICE_CMD_RELEASE_BLOCK: Release suspend blocker
 */
enum suspendservice_cmd {
	SUSPENDSERVICE_CMD_ACQUIRE_BLOCK = 1,
	SUSPENDSERVICE_CMD_RELEASE_BLOCK = 2,
};

/**
 * enum suspendservice_error - error codes for suspendservice
 * @SUSPENDSERVICE_ERR_OK:              Success
 * @SUSPENDSERVICE_ERR_GENERIC:         Generic error
 * @SUSPENDSERVICE_ERR_NOT_HELD:        Suspend block was not previously acquired.
 * @SUSPENDSERVICE_ERR_OVERFLOW:        Too many acquires.
 * @SUSPENDSERVICE_ERR_UNKNOWN_CMD:     Unknown command
 */
enum suspendservice_error {
	SUSPENDSERVICE_ERR_OK = 0,
	SUSPENDSERVICE_ERR_GENERIC = 1,
	SUSPENDSERVICE_ERR_NOT_HELD = 2,
	SUSPENDSERVICE_ERR_OVERFLOW = 3,
	SUSPENDSERVICE_ERR_UNKNOWN_CMD = 4,
};

/**
 * struct suspendservice_req - common request header
 * @cmd:    Command identifier (one of enum suspendservice_cmd)
 */
struct suspendservice_req {
	uint32_t cmd;
};

/**
 * struct suspendservice_rsp - common response header
 * @cmd:    Command identifier
 * @error:  Result code (one of enum suspendservice_error)
 */
struct suspendservice_rsp {
	uint32_t cmd;
	uint32_t error;
};

#endif /* __TRUSTY_WAKELOCK_H__ */
