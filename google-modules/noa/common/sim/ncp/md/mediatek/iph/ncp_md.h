/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD (MTK) Driver
 *
 * Copyright 2024 Google LLC.
 */
#ifndef __NCP_MD_H__
#define __NCP_MD_H__

#include "sys_common.h"

#define NOA_FW_RING_BUDGET 128

extern struct noa_md_fw *g_md_fw;

// Sync from soc/google/google_dpa_ctrl.h
enum dpa_data_path {
	NOA_DATA_PATH_DIRECT,
	NOA_DATA_PATH_OFFLOAD,
	NOA_DATA_PATH_COUNT,
};

int noa_ncp_md_init(void *data);
int noa_ncp_md_exit(void);

#endif /* __NCP_MD_H__ */
