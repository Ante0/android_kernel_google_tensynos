/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD (MTK) Driver
 *
 * Copyright 2024 Google LLC.
 */
#ifndef __NCP_MD_H__
#define __NCP_MD_H__

#include "sys_common.h"

#define NOA_FW_RING_SIZE 512
#define NOA_FW_RING_BUDGET 128
#define NOA_REL_BAT_WEIGHT 128

extern struct noa_md_fw *g_md_fw;

int noa_ncp_md_init(void *data);
int noa_ncp_md_exit(void);

#endif /* __NCP_MD_H__ */
