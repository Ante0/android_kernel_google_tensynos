/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Google LLC
 */

#ifndef _GOOG_MBA_EBU_IFACE_PRIV_H_
#define _GOOG_MBA_EBU_IFACE_PRIV_H_

enum goog_mba_ebu_msg_prio {
	GOOG_MBA_EBU_NORMAL_PRIO,
	GOOG_MBA_EBU_PRIOS_MAX,
};

struct ebu_iface {
	struct goog_mba_aggr_service *normal_service;
	struct device *dev;
};

#endif /* _GOOG_MBA_EBU_IFACE_PRIV_H_ */
