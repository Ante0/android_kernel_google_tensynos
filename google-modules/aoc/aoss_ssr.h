/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Google LLC */

#ifndef AOSS_SSR_H
#define AOSS_SSR_H

#include <linux/device.h>
#include <mailbox/protocols/mba/cpm/common/ssr/aoss_ssr_service.h>

struct aoc_prvdata;

struct ssr_data_s {
	struct device *dev;
	struct cpm_iface_client *mb_client;
	u32 mbx_send_timeout_ms;
	u32 mbx_receive_timeout_ms;
	struct completion ssr_resp_done;
	enum ssr_service_result result;
};

/*
 * AOSS SSR init function to be called at probe time
 */
struct ssr_data_s *aoss_ssr_init(struct aoc_prvdata *prvdata);

/*
 * Sends command to CPM to perform a specific operation for AOSS SSR.
 *
 * cmd: AOSS SSR command
 * payload: argument for the AOSS SSR command
 */
int aoss_ssr_send_command(struct ssr_data_s *ssr_data_p, enum ssr_service_cmd cmd, int payload);

/*
 * AOSS SSR clenunp function to be called at device removal time
 */
void aoss_ssr_cleanup(struct ssr_data_s *ssr_data_p);

#endif /* AOSS_SSR_H */
