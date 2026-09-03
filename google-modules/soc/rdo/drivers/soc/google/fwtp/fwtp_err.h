/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint error services header.
 *
 * This header is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT licenses.
 */

#ifndef __FWTP_ERR_H
#define __FWTP_ERR_H

/**
 * enum fwtp_error_code - Set of FWTP error codes.
 *
 * @kFwtpOk: No error.
 * @kFwtpErrBadLen: Bad length requested error.
 * @kFwtpErrNotFound: Requested item not found error.
 * @kFwtpErrUnsupported: Request unsupported error.
 * @kFwtpErrMemAlloc: Memory allocationt error.
 * @kFwtpErrBadMsg: Malformed message error.
 * @kFwtpErrTooBig: Value is too big error.
 * @kFwtpErrIo: I/O error.
 */
typedef enum fwtp_error_code {
	kFwtpOk = 0,
	kFwtpErrBadLen = 1,
	kFwtpErrNotFound = 2,
	kFwtpErrUnsupported = 3,
	kFwtpErrMemAlloc = 4,
	kFwtpErrBadMsg = 5,
	kFwtpErrTooBig = 6,
	kFwtpErrIo = 7,
} fwtp_error_code_t;

#endif /* __FWTP_ERR_H */
