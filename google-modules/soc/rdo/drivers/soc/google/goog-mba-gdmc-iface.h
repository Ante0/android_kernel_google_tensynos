/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Google LLC
 */

#ifndef _GOOG_MBA_GDMC_IFACE_PRIV_H_
#define _GOOG_MBA_GDMC_IFACE_PRIV_H_

#include <linux/types.h>
#include <linux/workqueue.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_gdmc_service_ids.h>

enum goog_mba_gdmc_msg_prio {
	GOOG_MBA_GDMC_NORMAL_PRIO,
	GOOG_MBA_GDMC_CRITICAL_PRIO,
	GOOG_MBA_GDMC_PRIOS_MAX,
};

/*
 * gdmc_aoc_notify - Notification data for aoc reset
 * @aoc_reset_fn:		Client registered callback used to receive the register dump from
 *				GDMC when AoC reset.
 * @work:			struct work_struct with gdmc function to handle aoc reset and
 *				register dump and send to the client if available.
 * @client_prv_data:		Client given data which will be passed to callback.
 */
struct gdmc_aoc_notify {
	struct gdmc_iface *gdmc_iface;
	gdmc_aoc_reset_cb_t aoc_reset_fn;
	struct work_struct work;
	dma_addr_t shared_buf_phys_addr;
	void *shared_buf;
	u32 shared_buf_size;
	void *client_prv_data;
};

/*
 * gdmc_dpa_notify - Notification data for dpa reset
 * @dpa_reset_fn:		Client registered callback used to receive the register dump from
 *				GDMC when DPA reset.
 * @work:			struct work_struct with gdmc function to handle dpa reset and
 *				register dump and send to the client if available.
 * @client_prv_data:		Client given data which will be passed to callback.
 */
struct gdmc_dpa_notify {
	struct gdmc_iface *gdmc_iface;
	gdmc_dpa_reset_cb_t dpa_reset_fn;
	struct work_struct work;
	dma_addr_t shared_buf_phys_addr;
	void *shared_buf;
	u32 shared_buf_size;
	void *client_prv_data;
};

struct goog_mba_gdmc_service_handler {
	gdmc_host_cb_t host_cb;
	void *priv_data;
};

struct gdmc_iface {
	struct goog_mba_aggr_service *normal_service;
	struct goog_mba_aggr_service *crit_service;
	struct goog_mba_gdmc_service_handler
		host_service_hdls[MAX_APC_CRITICAL_GDMC_MAILBOX_SERVICE_NUM];
	struct device *dev;
	struct gdmc_dhub_iface dhub_iface;
};

#endif /* _GOOG_MBA_GDMC_IFACE_PRIV_H_ */
