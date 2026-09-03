/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Ernie Hsu <erniehsu@google.com>
 */

#ifndef _VPU_SECURE_H_
#define _VPU_SECURE_H_

#include <linux/completion.h>
struct tipc_chan;
struct vpu_dmabuf_info;

struct vpu_secure {
	bool connected;
	/* A completion when connection completes or a response is received */
	struct completion done;
	/* A connected TIPC channel */
	struct tipc_chan *chan;
	/* The result indicated in the current response */
	int rsp_result;
};

int vpu_secure_init(struct vpu_secure *secure);
void vpu_secure_deinit(struct vpu_secure *secure);
int vpu_secure_fw_prot(struct vpu_secure *secure,
		       const struct vpu_dmabuf_info *fw_buf,
		       uint32_t blob_size);
int vpu_secure_fw_unprot(struct vpu_secure *secure);

#endif // _VPU_OF_H_
