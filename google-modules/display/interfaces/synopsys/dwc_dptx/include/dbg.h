/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#ifndef __DPTX_DBG_H__
#define __DPTX_DBG_H__

/*#define DPTX_DEBUG_REG*/
#define DPTX_DEBUG_AUX
#define DPTX_DEBUG_BRIDGE
#define DPTX_DEBUG_LINK
#define DPTX_DEBUG_IRQ
/*#define DPTX_DEBUG_DPCD_CMDS*/

#define dptx_dbg(_dp, _fmt...) dev_dbg((_dp)->dev, _fmt)
#define dptx_info(_dp, _fmt...) dev_info((_dp)->dev, _fmt)
#define dptx_warn(_dp, _fmt...) dev_warn((_dp)->dev, _fmt)
#define dptx_err(_dp, _fmt...) dev_err((_dp)->dev, _fmt)

#ifdef DPTX_DEBUG_AUX
#define dptx_dbg_aux(_dp, _fmt...) \
	do { if ((_dp)->aux_debug_en) dev_info((_dp)->dev, _fmt); } while (0)
#else
#define dptx_dbg_aux(_dp, _fmt...) dev_dbg((_dp)->dev, _fmt)
#endif

#ifdef DPTX_DEBUG_BRIDGE
#define dptx_dbg_bridge(_dp, _fmt...) dev_info((_dp)->dev, _fmt)
#else
#define dptx_dbg_bridge(_dp, _fmt...) dev_dbg((_dp)->dev, _fmt)
#endif

#ifdef DPTX_DEBUG_LINK
#define dptx_dbg_link(_dp, _fmt...) dev_info((_dp)->dev, _fmt)
#else
#define dptx_dbg_link(_dp, _fmt...) dev_dbg((_dp)->dev, _fmt)
#endif

#ifdef DPTX_DEBUG_IRQ
#define dptx_dbg_irq(_dp, _fmt...) dev_dbg((_dp)->dev, _fmt)
#else
#define dptx_dbg_irq(_dp, _fmt...)
#endif

#endif
