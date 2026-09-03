/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * NOA Modem CLDMA Path Module
 */

#ifndef __NOA_MD_CLDMA_H__
#define __NOA_MD_CLDMA_H__

#include <linux/types.h>
#include "noa_md_dma_mapper.h"

struct noa_dpath_client;

/**
 * struct noa_md_cldma - Manages the CLDMA path for NOA.
 * @dpath_client: Pointer to the data path switching client.
 * @mapper: Dedicated DMA mapper for the CLDMA path.
 */
struct noa_md_cldma {
	struct noa_dpath_client *dpath_client;
	struct noa_md_dma_mapper mapper;
};

/**
 * noa_md_cldma_setup() - Initializes all software resources for the CLDMA path.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_cldma_setup(void);

/**
 * noa_md_cldma_release() - Releases all software resources for the CLDMA path.
 */
void noa_md_cldma_release(void);

#endif /* __NOA_MD_CLDMA_H__ */
