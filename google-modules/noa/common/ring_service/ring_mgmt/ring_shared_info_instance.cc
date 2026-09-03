// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_shared_info_instance.h"

#include <common/compiler.h>
#include <common/ring_id.h>
#include <common/noa_ring_id.h>
#else /* linux */
#include "ring_mgmt/ring_shared_info_instance.h"

#include "common/compiler.h"
#include "common/ring_id.h"
#include "common/noa_ring_id.h"
#endif /* linux */

#define MAX_RING_NUM (16U)

/* Returning the DPA SRAM physical address of g_root_base */
struct NoaRingSharedInfoRootBase *NoaRingSharedInfoRootBaseInstance(void)
{
	/* Initialize ring shared info array in SRAM shared region */
	SEC_PUBLIC static struct NoaRingSharedInfoRootBase g_root_base = {
		.num = NOA_NEP_RING_MAX,
		.entries = { { 0, 0, 0, 0, 0, 0 } },
	};
	return &g_root_base;
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingSharedInfoRootBaseInstance);
#endif /* linux */

struct ring_shared_info *noa_ring_service_shared_info_instance(void)
{
	SEC_PUBLIC static struct noa_ring info[MAX_RING_NUM] = { { 0, 0, 0, 0, 0, 0 } };
	SEC_PUBLIC static struct ring_shared_info shared_info = {
		.ring_infos = &info[0],
		.size = MAX_RING_NUM,
	};
	return &shared_info;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ring_service_shared_info_instance);
#endif /* linux */


