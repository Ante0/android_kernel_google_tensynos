// SPDX-License-Identifier: GPL-2.0
/*
 * Secure IOVA Management
 *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 * Author: <hyesoo.yu@samsung.com> for Samsung
 */

#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/module.h>
#include <linux/samsung-secure-iova.h>

#if IS_ENABLED(CONFIG_SOC_GS101) || IS_ENABLED(CONFIG_SOC_GS201) || IS_ENABLED(CONFIG_SOC_ZUMA)
#define SECURE_DMA_BASE	0x40000000

/*
 * MFC have H/W restriction that could only access 0xC000_0000 offset
 * from base, and we are reserving 0xF000_0000 to 0xFFFF_FFFF for
 * other usage.
 */
#define SECURE_DMA_SIZE 0xB0000000
#else

/*
 * The Buenos TPU DIVE core cannot access memory below the 2 GB address
 * range. IOVA allocator should only allocate from 0x80000000 and above.
 * An additional 16MB is reserved for the model binary.
 */
#define SECURE_DMA_BASE	0x81000000

/*
 * DSP reserved 0xF000_0000 to 0xFFFF_FFFF for other usage.
 */
#define SECURE_DMA_SIZE 0x6F000000
#endif

static struct gen_pool *secure_iova_pool;

/*
 * Alignment to a secure address larger than 16MiB is not beneficial because
 * the protection alignment just needs 64KiB by the buffer protection H/W and
 * the largest granule of H/W security firewall (the secure context of SysMMU)
 * is 16MiB.
 */
#define MAX_SECURE_VA_ALIGN	(SZ_16M / PAGE_SIZE)

unsigned long secure_iova_alloc(unsigned long size, unsigned int align)
{
	unsigned long out_addr;
	struct genpool_data_align alignment = {
		.align = max_t(int, PFN_DOWN(align), MAX_SECURE_VA_ALIGN),
	};

	if (WARN_ON_ONCE(!secure_iova_pool))
		return 0;

	out_addr = gen_pool_alloc_algo(secure_iova_pool, size,
				       gen_pool_first_fit_align, &alignment);

	if (out_addr == 0)
		pr_err("failed to allocate secure IOVA address space. %zu/%zu bytes used",
		       gen_pool_avail(secure_iova_pool),
		       gen_pool_size(secure_iova_pool));

	return out_addr;
}
EXPORT_SYMBOL_GPL(secure_iova_alloc);

void secure_iova_free(unsigned long addr, unsigned long size)
{
	gen_pool_free(secure_iova_pool, addr, size);
}
EXPORT_SYMBOL_GPL(secure_iova_free);

static int __init samsung_secure_iova_init(void)
{
	int ret;

	secure_iova_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!secure_iova_pool) {
		pr_err("failed to create Secure IOVA pool\n");
		return -ENOMEM;
	}

	ret = gen_pool_add(secure_iova_pool, SECURE_DMA_BASE, SECURE_DMA_SIZE, -1);
	if (ret) {
		pr_err("failed to set address range of Secure IOVA pool");
		gen_pool_destroy(secure_iova_pool);
		return ret;
	}

	return 0;
}

static void __exit samsung_secure_iova_exit(void)
{
	gen_pool_destroy(secure_iova_pool);
}

module_init(samsung_secure_iova_init);
module_exit(samsung_secure_iova_exit);
MODULE_DESCRIPTION("Samsung Secure IOVA Manager");
MODULE_LICENSE("GPL v2");
