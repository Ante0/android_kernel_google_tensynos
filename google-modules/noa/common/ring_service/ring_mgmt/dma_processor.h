/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP simualtor
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifndef __NOA_NEP_DMA_PROCESSOR_H__
#define __NOA_NEP_DMA_PROCESSOR_H__

#ifdef linux
#include <common/core.h>
#include <linux/interrupt.h>
#include "ring_manager_instance.h"
#else /* linux */
#include "common/core.h"
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */

void configure_dma(int channel, void *driver);
int perform_dma_copy(unsigned long dst_addr, unsigned long src_addr, uint32_t size,
		     struct dma_processor *dma_data);
#endif /* __NOA_NEP_DMA_PROCESSOR_H__ */
