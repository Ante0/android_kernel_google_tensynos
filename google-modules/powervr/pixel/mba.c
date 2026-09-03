// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022-2023 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/iopoll.h>

#include "sysconfig.h"
#include "mba.h"
#include "glue_mba_host_csr_csr.h"
#include "mba_gpu_client_csr_CLIENT_0_csr.h"

#define MBA_CLIENT_SIZE (0x1000)
#define MBA_MSG_OFFSET(idx)                                                                        \
	(MBA_CLIENT_SIZE * (idx) + MBA_GPU_CLIENT_CSR__CLIENT_0__COMMON_MSG_offset)


void mba_signal(struct pixel_gpu_device *pixel_dev, u32 payload)
{
	u32 __iomem *const itr = (u32 __iomem *)&pixel_dev->mba.glue_csr[INTR_GPU_MBA_ITR_offset];
	/* IRQ is edge triggered, so clear to zero first */
	iowrite32(0, itr);
	iowrite32((1 << payload), itr);
}

int mba_init(struct pixel_gpu_device *pixel_dev)
{
	struct resource *mba_client_res;
	int ret = -EINVAL;

	/* Map glue and client CSRs for KM access */
	pixel_dev->mba.glue_csr = devm_platform_ioremap_resource_byname(
		to_platform_device(pixel_dev->dev), "mba_glue");
	if (IS_ERR(pixel_dev->mba.glue_csr))
		goto exit;

	mba_client_res = platform_get_resource_byname(to_platform_device(pixel_dev->dev),
						      IORESOURCE_MEM, "mba_client");
	pixel_dev->mba.client_csr = devm_ioremap_resource(pixel_dev->dev, mba_client_res);
	if (IS_ERR(pixel_dev->mba.client_csr))
		goto exit;

	pixel_dev->mba.n_clients = resource_size(mba_client_res) / MBA_CLIENT_SIZE;

	ret = 0;
exit:
	return ret;
}

void mba_term(struct pixel_gpu_device *pixel_dev)
{
	(void)pixel_dev;
}
