/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA MD MTK T900 Private header file for DPMAIF
 *
 * This file synchronizes with MTK T900 mtk_dpmaif_ring.c for
 * the definition and structures.
 *
 * Copyright (c) 2025 Google Inc.
 *
 */
#ifndef __NOA_WWAN_MTK_PRIV_DPMAIF_RING_H__
#define __NOA_WWAN_MTK_PRIV_DPMAIF_RING_H__

#define PIT_PD_DATA_LEN		GENMASK(31, 16) /* Indicates the data length of current packet. */
#define PIT_PD_BUF_ID		GENMASK(15, 3) /* The low order of buffer index */
#define PIT_PD_BUF_TYPE		BIT(2) /* 0b: normal BAT entry; 1b: fragment BAT entry */
#define PIT_PD_CONT		BIT(1) /* 0b: last entry; 1b: more entry */
#define PIT_PD_PKT_TYPE		BIT(0) /* 0b: normal PIT entry; 1b: message PIT entry */

#define PIT_PD_HD_OFFSET	GENMASK(23, 19)
#define PIT_PD_IG		BIT(16)
#define PIT_PD_H_BID		GENMASK(10, 8) /* The high order of buffer index */
#define PIT_PD_SEQ		GENMASK(7, 0) /* PIT sequence */

#define PIT_MSG_DP		BIT(31) /* Indicates software to drop this packet if set. */
#define PIT_MSG_CHNL_ID		GENMASK(23, 16) /* channel index */
#define PIT_MSG_ERR		BIT(4)
#define PIT_MSG_CHECKSUM	GENMASK(3, 2)

#define PIT_MSG_HASH		GENMASK(31, 24) /* Hash value calculated by Hardware using packet */
#define PIT_MSG_PRO		GENMASK(17, 16)

#define PIT_MSG_IP		BIT(23)

#define DPMAIF_POLL_STEP 20
#define DPMAIF_POLL_PIT_CNT_MAX 100

#endif // __NOA_WWAN_MTK_PRIV_DPMAIF_RING_H__
