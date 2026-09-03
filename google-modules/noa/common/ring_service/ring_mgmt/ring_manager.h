/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP ring management
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifndef __NOA_NEP_RING_MANAGER_H__
#define __NOA_NEP_RING_MANAGER_H__

#ifdef linux
#include "ring_shared_info.h"

#include "port.h"
#include <common/core.h>
#include <common/ring.h>
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
#include <soc/google/google_dpa.h>
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#else /* linux */
#include "ring_mgmt/ring_shared_info.h"

#include "common/core.h"
#include "common/ring.h"
#include "ring_mgmt/port.h"
#endif /* linux */

struct noa_nep_stat {
	// TODO(b/386895928) -To prevent out-of-bounds errors in unit tests
	// that use random values for reason and mode, we've used the maximum
	// size of the statistics.  A better solution is to add mocking for
	// the nep_stat structure in the future.
	unsigned long reason[1U << NOA_DESC_REASON_BIT_FIELD];
	unsigned long src[NOA_PORT_MAX];
	unsigned long dst[NOA_PORT_MAX];
	unsigned long cp[NOA_PORT_MAX];
	unsigned long mode[1U << NOA_DESC_MODE_BIT_FIELD];
};

extern struct noa_ring *get_ring_info(uint8_t port_id, RingType ring_type);

void noa_ring_shared_info_initialization(void);
void rings_initialization(struct device *dev);
int noa_ring_shared_info_register(struct ring_shared_info *info);

struct device *get_ring_device(void);
struct noa_nep_stat *get_nep_stat(void);

int noa_ring_manager_regs_get(struct noa_ring_regs *regs, uint8_t port, RingType type);

int NoaRingSharedInfoRootRegister(const struct NoaRingSharedInfoRoot *root);
struct noa_ring *NoaRingSharedInfoGet(uint8_t interface, uint8_t flow, uint8_t category,
				      uint8_t direction);
#ifdef linux
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
/**
 * @brief Get ring shared register info from AP through kernel driver
 *
 * @return 0 on success
 */
int32_t NoaDpaRingSharedRegsGet(struct google_dpa *dpa, struct noa_ring_regs *regs,
				uint8_t interface, uint8_t flow, uint8_t category,
				uint8_t direction);
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#endif /* linux */
int32_t NoaRingSharedRegsGet(struct noa_ring_regs *regs, uint8_t interface, uint8_t flow,
			     uint8_t category, uint8_t direction);
void NoaRingSharedInfoInit(void);
int32_t NoaRingSharedInfoDumpAll(char *buf, int32_t len);

#endif /* __NOA_NEP_RING_MANAGER_H_ */
