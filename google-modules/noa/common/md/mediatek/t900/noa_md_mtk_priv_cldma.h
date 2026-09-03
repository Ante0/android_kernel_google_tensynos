/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA MD MTK T900 Private header file for CLDMA
 *
 * This file synchronizes with MTK T900 mtk_cldma.h for
 * the definition and structures.
 *
 * Copyright (c) 2025 Google Inc.
 *
 */
#ifndef __NOA_WWAN_MTK_PRIV_CLDMA_H__
#define __NOA_WWAN_MTK_PRIV_CLDMA_H__

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "noa_md_mtk_priv_cldma_drv.h"
#else
#ifdef drv_ops_name
#undef drv_ops_name
#endif
#include "mtk_cldma.h"
#include "mtk_trans_ctrl.h"
#endif

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
struct mtk_ctrl_trans;

struct cldma_dev {
	struct cldma_drv_info *cldma_drv_info[NR_CLDMA];
	struct mtk_ctrl_trans *trans;
	unsigned long err_event;
	struct dentry *dentry;
};
#endif

#endif // __NOA_WWAN_MTK_PRIV_CLDMA_H__
