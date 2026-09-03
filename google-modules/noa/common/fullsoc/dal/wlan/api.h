/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM Device Abstraction Layer Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_DAL_WLAN_API_H__
#define __LVM_DAL_WLAN_API_H__

#include <wlan/google_plat.h>
#include <buffer/manager.h>

typedef int (*lvm_dal_rxbm_sync_t)(void *priv, u32 count,
				   struct lvm_buffer **rxbm);

extern lvm_dal_rxbm_sync_t rxbm_sync_cb;

static inline void lvm_dal_rxbm_sync_cb_register(lvm_dal_rxbm_sync_t cb)
{
	rxbm_sync_cb = cb;
}

static inline int
lvm_dal_rxbm_sync_cb_invoke(void *priv, u32 count, struct lvm_buffer **rxbm)
{
	if (rxbm_sync_cb)
		return rxbm_sync_cb(priv, count, rxbm);

	return -EINVAL;
}

#endif  /* __LVM_DAL_WLAN_API_H__ */
