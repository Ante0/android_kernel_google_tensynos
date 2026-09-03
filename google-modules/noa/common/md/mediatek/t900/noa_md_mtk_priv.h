/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA MD MTK Common Private header file
 *
 * This file includes the common private header files
 * for different MediaTek modem platforms.
 *
 * Copyright (c) 2025 Google Inc.
 *
 */
#ifndef __NOA_WWAN_MTK_PRIV_H__
#define __NOA_WWAN_MTK_PRIV_H__

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "noa_md_mtk_priv_fullsoc.h"
#else
#include "mtk_dpmaif_drv.h"
#include "mtk_dpmaif_reg.h"
#include "mtk_pci.h"
#include "mtk_pm.h"
#endif

#include "noa_md_mtk_priv_dpmaif.h"
#include "noa_md_mtk_priv_dpmaif_ring.h"
#include "noa_md_mtk_priv_dpmaif_wwan.h"
#include "noa_md_mtk_priv_cldma_drv.h"
#include "noa_md_mtk_priv_cldma.h"

#endif