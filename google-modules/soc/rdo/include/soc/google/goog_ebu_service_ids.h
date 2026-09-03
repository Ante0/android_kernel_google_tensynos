/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022-2024, Google LLC
 */

#ifndef __GOOG_EBU_SERVICE_ID_H
#define __GOOG_EBU_SERVICE_ID_H

enum ebu_mba_service_id {
	EBU_MBA_SERVICE_ID_RESERVED = 0,
	EBU_MBA_SERVICE_ID_PING = 1,
	EBU_MBA_SERVICE_ID_USB = 2,
	EBU_NUM_MBA_SERVICES
};

#endif /* __GOOG_EBU_SERVICE_ID_H */
