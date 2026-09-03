/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GPCA operations queue management.
 *
 * Copyright (C) 2022-2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_OP_QUEUE_H
#define _GOOGLE_GPCA_OP_QUEUE_H

#include <linux/types.h>

/* GPCA operation type */
enum gpca_op_type {
	GPCA_OP_TYPE_KEY_GEN = 0,
	GPCA_OP_TYPE_KEY_DERIVE,
	GPCA_OP_TYPE_KEY_WRAP,
	GPCA_OP_TYPE_KEY_UNWRAP,
	GPCA_OP_TYPE_KEY_SEND,
	GPCA_OP_TYPE_KEY_CLEAR,
	GPCA_OP_TYPE_GET_SEED,
	GPCA_OP_TYPE_KEY_IMPORT,
	GPCA_OP_TYPE_GET_KP,
	GPCA_OP_TYPE_GET_METADATA,
	GPCA_OP_TYPE_SET_METADATA,
	GPCA_OP_TYPE_GET_PUB_KEY,
	GPCA_OP_TYPE_SET_PUB_KEY,
	GPCA_OP_TYPE_SECURE_KEY_IMPORT,
	GPCA_OP_TYPE_SET_CRYPTO_PARAMS,
	GPCA_OP_TYPE_START_CRYPTO_OP,
	GPCA_OP_TYPE_CLEAR_CRYPTO_OP,
};

struct gpca_dev;

/**
 * struct gpca_op_context - Operation context
 *
 * @gpca_dev: GPCA Device context.
 * @op_type: GPCA operation type from enum gpca_op_type.
 * @op_handler: GPCA operation handler.
 * @cb: Callback upon GPCA operation completion.
 * @cb_ctx: Callback context to be passed back to caller as it is.
 * @op_list: GPCA operation list management struct.
 */
struct gpca_op_context {
	struct gpca_dev *gpca_dev;
	enum gpca_op_type op_type;
	int (*op_handler)(struct gpca_op_context *op_ctx);
	void (*cb)(int ret, void *cb_ctx);
	void *cb_ctx;
	struct list_head op_list;
};

/**
 * gpca_op_queue_init() - Initialize GPCA Operations queue data structures.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_op_queue_init(struct gpca_dev *gpca_dev);

/**
 * gpca_op_queue_deinit() - Destroy GPCA Operations queue data structures.
 *
 * @gpca_dev: Pointer to GPCA device struct
 */
void gpca_op_queue_deinit(struct gpca_dev *gpca_dev);

/**
 * gpca_op_queue_push_back() - Push a GPCA operation to queue.
 *
 * @gpca_dev: Pointer to GPCA device struct
 * @op_ctx: Context of operation being pushed to queue.
 *
 * Operation context must be dynamically allocated by the caller.
 * Operation context(op_ctx) must be freed by the Operation Handler
 * specified in op_ctx.op_handler.
 *
 * Return : 0 on success, negative if unable to queue GPCA operation.
 */
int gpca_op_queue_push_back(struct gpca_dev *gpca_dev,
			    struct gpca_op_context *op_ctx);

#endif /* _GOOGLE_GPCA_OP_QUEUE_H */
