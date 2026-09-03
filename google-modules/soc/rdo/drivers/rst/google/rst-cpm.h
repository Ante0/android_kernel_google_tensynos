/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Google rst-cpm driver header file.
 *
 * Copyright (C) 2023 Google LLC.
 */
#ifndef _GOOGLE_RST_CPM_H
#define _GOOGLE_RST_CPM_H

#include <linux/mailbox_client.h>
#include <linux/reset-controller.h>

#include <soc/google/goog_mba_cpm_iface.h>

/* TODO(b/280700962): determine the right timeout value */
#define MAILBOX_SEND_TIMEOUT_MS 3000
#define MAILBOX_RECEIVE_TIMEOUT_MS 3000
#define LPCM_REMOTE_CHANNEL 0x8
#define LPCM_CMD_SET_RST 0x4
#define ASSERT_OP_ID 0
#define DEASSERT_OP_ID 1
#define NO_ERROR 0
#define ERR_LPCM_NOT_SUPPORTED (-24)
#define ERR_LPCM_INVALID_ARGS (-8)
#define ERR_LPCM_TIMED_OUT (-13)
#define ERR_LPCM_GENERIC (-1)

#define to_goog_cpm_rst(rsd) container_of(rsd, struct goog_cpm_rst, rcdev);

struct cpm_msg {
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
};

struct goog_cpm_rst_mbox {
	struct cpm_iface_client *client;
	int (*send_msg_and_block)(struct cpm_iface_client *client, struct cpm_iface_req *req);
};

struct goog_cpm_rst {
	u32 lpcm_id;
	struct reset_controller_dev rcdev;
	struct goog_cpm_rst_mbox *cpm_mbox;
};

struct goog_cpm_rst_controller {
	struct device *dev;
	struct goog_cpm_rst *resets;
	struct goog_cpm_rst_mbox cpm_mbox;
	struct dentry *debugfs_root;
};

struct goog_cpm_lpcm_service_req {
	uint8_t req_id;
	uint8_t lpcm_id;
	uint8_t rst_id;
	uint8_t op_id;
} __packed __aligned(4);

int goog_cpm_rst_send_mba_mail(struct goog_cpm_rst *cpm_rst, unsigned long rst_id, int op_id);

void goog_cpm_init_payload(struct goog_cpm_rst *cpm_rst, struct cpm_msg *msg,
			   unsigned long rst_id, int op_id);

#endif //_GOOGLE_RST_CPM_H
