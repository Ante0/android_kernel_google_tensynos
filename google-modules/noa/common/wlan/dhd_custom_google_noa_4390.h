/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux cfg80211 driver - Dongle Host Driver (DHD) related
 *
 * Copyright (C) 2022, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef __DHD_CUSTOM_GOOGLE_NOA_H__
#define __DHD_CUSTOM_GOOGLE_NOA_H__

#include <dhd_msgbuf.h>

#define noa_wlan_err(fmt, ...) \
do {                            \
	pr_err("%s:" fmt, "noa_wlan", ##__VA_ARGS__);    \
} while (0)

#define INVALID_TXQ_ID 0xFFFFU
#define FLOWID_TO_RINGID(flowid) (flowid - 2)

enum dhd_mapper_pool_id {
	DHD_MAPPER_POOL_APC_RX,
	DHD_MAPPER_POOL_APC_TX,

	__DHD_MAPPER_POOL_MAX,
};

static void *
BCMFASTPATH(dhd_pktid_map_get)(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, uint32 nkey)
{
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;
	void *pkt = NULL;
	unsigned long flags;

	if (!handle || !dhd) {
		return NULL;
	}
	map = (dhd_pktid_map_t *)handle;

	DHD_PKTID_LOCK(map->pktid_lock, flags);

	/* XXX PLEASE DO NOT remove this ASSERT, fix the bug in caller. */
	if ((nkey == 0) || (nkey > map->items) ||
			(dhd->dhd_induce_error == DHD_INDUCE_PKTID_INVALID_FREE)) {
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
		return NULL;
	}

	locker = &map->lockers[nkey];
	pkt = locker->pkt;
	DHD_PKTID_UNLOCK(map->pktid_lock, flags);
	return pkt;
}

static int
BCMFASTPATH(dhd_pktid_map_get_avail)(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle)
{
	dhd_pktid_map_t *map;

	if (!handle || !dhd) {
		return 0;
	}
	map = (dhd_pktid_map_t *)handle;
	return map->avail;
}

#define DHD_RING_MEM_MEMBER_ADDR(bus, ringid, member) \
	((bus)->ring_sh[ringid].ring_mem_addr + OFFSETOF(ring_mem_t, member))

#define DHD_RING_INFO_MEMBER_ADDR(bus, member) \
	((bus)->pcie_sh->rings_info_ptr + OFFSETOF(ring_info_t, member))

#endif