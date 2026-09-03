/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal GPCA defines.
 *
 * Copyright (C) 2022-2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_INTERNAL_H
#define _GOOGLE_GPCA_INTERNAL_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/semaphore.h>
#include <linux/types.h>

#include "gpca_registers.h"

#define MAX_WRAPPED_KEY_SIZE_BYTES 1312

/* GPCA FIFO types */
enum gpca_cmd_fifo_type {
	GPCA_FIFO_TYPE_CRYPTO = 0,
	GPCA_FIFO_TYPE_KEY,
	GPCA_FIFO_MAX_VALUE
};

/**
 * GPCA key handle state
 *
 * +----------------+  Add Key      +-------------------+  Debug State Change
 * |                +-------------->|                   +-----------------+
 * | UNINITIALIZED  |               |   IN_KEY_TABLE    |                 |
 * |                |<--------------+                   |                 v
 * +----------------+   Clear Key   +------+------------+          +-------------+
 *              ^                          |       ^               |             |
 *              |                          |       |               |   EVICTED   |
 *              |                 Swap Out |       |Swap In        |             |
 *              |                          |       |               +-------------+
 *              |                          v       |                       ^
 *              |                       +-------------+-----+              |
 *              |     Clear Key         |                   |              |
 *              +-----------------------+   SWAPPED_OUT     +--------------+
 *                                      |                   |  Debug State Change
 *                                      +-------------------+
 *
 */
enum gpca_key_state {
	UNINITIALIZED = 0,
	IN_KEY_TABLE,
	SWAPPED_OUT,
	EVICTED,
};

/**
 * GPCA crypto operation handle state
 * .-------------. Set Crypto Params .-----------.
 * |UNINITIALIZED| ----------------> |IN_OP_TABLE|--
 * '-------------' <---------------- '-----------' |
 *        ^          Clear op         ^            |
 *        |                   Swap In |    Swap out|
 *        |                           |            |
 *        |                           |            |
 *        |         Clear op         .---------.   |
 *        -------------------------- |IN_OP_CTX|<---
 *                                   '---------'
 */
enum gpca_crypto_op_state {
	GPCA_OP_UNINITIALIZED = 0,
	GPCA_OP_IN_OP_TABLE,
	GPCA_OP_IN_OP_CTX,
};

/**
 * struct gpca_key_handle - GPCA Key handle.
 * @key_state: Key state i.e. in table or swapped out.
 * @keyslot: Keyslot if the key is IN_KEY_TABLE.
 * @is_public_key: Is the key public key.
 * @is_swappable: Can the key be swapped in/out of the key table.
 * @is_large_key: Is the key length greater than 544b.
 * @wrapping_keyslot: Static Key Wrapping key slot.
 * @wrapped_key_blob: Wrapped key if the key is SWAPPED_OUT.
 * @wrapped_key_blob_size: Wrapped key blob size.
 */
struct gpca_key_handle {
	enum gpca_key_state key_state;
	u8 keyslot;
	bool is_public_key;
	bool is_swappable;
	bool is_large_key;
	u8 wrapping_keyslot;
	u8 wrapped_key_blob[MAX_WRAPPED_KEY_SIZE_BYTES];
	u32 wrapped_key_blob_size;
};

/* GPCA key slot node */
struct gpca_key_slot_node {
	u8 keyslot;
	struct gpca_key_handle *key_handle;
	struct list_head list;
};

/**
 * struct gpca_crypto_op_handle - GPCA Crypto operation handle.
 *
 * op_state: Operation state IN_OP_TABLE or swapped out IN_OP_CTX.
 * opslot: Operation slot number if IN_OP_TABLE.
 * key_handle: Key handle associated with the operation if any.
 * op_ctx_buf: Operation context buffer if IN_OP_CTX.
 */
struct gpca_crypto_op_handle {
	enum gpca_crypto_op_state op_state;
	u8 opslot;
	struct gpca_key_handle *key_handle;
	u8 op_ctx_buf[GPCA_OP_CTX_SIZE_BYTES];
};

/* GPCA operation slot node */
struct gpca_op_slot_node {
	u8 opslot;
	struct gpca_crypto_op_handle *op_handle;
	struct list_head op_slot_list;
};

enum gpca_cmd_state {
	GPCA_CMD_INVALID_STATE,
	GPCA_CMD_PUT_REQ,
	GPCA_CMD_GET_RSP,
};

struct gpca_cmd_ctx {
	const u32 *req_buf;
	u32 req_buf_size_words;
	u32 req_buf_offset;
	u32 *rsp_buf;
	u32 rsp_buf_size_words;
	u32 rsp_buf_offset;
	u32 rsp_buf_size_out;
	struct completion *cpl;
	enum gpca_cmd_state state;
	enum gpca_cmd_fifo_type cmd_type;
	int ret;
};

/**
 * struct gpca_driverdata - GPCA driver data
 * @hw_bug_hmac_digest_size_keys: HMAC keys are digest size if the
 *                                RTL bug b/257538165 exists.
 *                                HMAC keys should ideally be block size.
 */
struct gpca_driverdata {
	bool hw_bug_hmac_digest_size_keys;
};

/**
 * struct gpca_dev - GPCA Device.
 * @dev: Platform bus device.
 * @reg_base: IO remapped Register base.
 * @gpca_regmap: GPCA regmap
 * @gpca_dev_num: GPCA device number.
 * @gpca_class: GPCA device class.
 * @gpca_cdev: GPCA character device.
 * @small_keys: Small key slot configuration.
 * @large_key: Large key slot configuration.
 * @pinned_head: Pinned/Unswappable key slots list head.
 * @swap_head: Swappable key slots list head.
 * @gpca_op_queue_head: GPCA operations queue head.
 * @gpca_op_queue_mutex: Mutex for serialization of updates
 *                       to GPCA operations queue.
 * @gpca_op_wq: GPCA operations workqueue.
 * @gpca_op_process: Work structure for processing operation
 *                   from GPCA operation queue.
 * @op_slots: GPCA operations slots configuration.
 * @gpca_crypto_op_slot_head: Crypto Operation slots list head.
 * @reversed_iv: Initialization vector needs to be reversed before
 *               sending to GPCA.
 * @drv_data: GPCA driver data.
 */
struct gpca_dev {
	struct device *dev;
	void __iomem *reg_base;
	struct regmap *gpca_regmap;
	dev_t gpca_dev_num;
	struct class *gpca_class;
	struct cdev gpca_cdev;
	struct gpca_key_slot_node
		small_keys[DOMAIN_MAX_KEYSLOT - DOMAIN_MIN_KEYSLOT + 1];
	struct gpca_key_slot_node large_key;
	struct list_head pinned_head;
	struct list_head swap_head;
	struct list_head gpca_op_queue_head;
	struct mutex gpca_op_queue_mutex;
	struct workqueue_struct *gpca_op_wq;
	struct work_struct gpca_op_process;
	struct gpca_op_slot_node
		op_slots[DOMAIN_MAX_OPSLOT - DOMAIN_MIN_OPSLOT + 1];
	struct list_head gpca_crypto_op_slot_head;
	bool reversed_iv;
	struct semaphore gpca_busy;
	struct work_struct gpca_cmd_process_req;
	struct work_struct gpca_cmd_process_rsp;
	struct gpca_cmd_ctx cur_gpca_cmd_ctx;
	const struct gpca_driverdata *drv_data;
};

#if IS_ENABLED(CONFIG_GOOGLE_GPCA_KEY_TEST)
struct platform_device *get_gpca_platform_device(void);
#endif

#endif /* _GOOGLE_GPCA_INTERNAL_H */
