// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic page table allocator for IOMMUs.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/bug.h>
#include <arm-smmu-v3/io-pgtable.h>
#include <linux/kernel.h>
#include <linux/types.h>

static const struct io_pgtable_init_fns *
io_pgtable_init_table[IO_PGTABLE_NUM_FMTS] = {
	[ARM_64_LPAE_S1] = &io_pgtable_arm_64_lpae_s1_init_fns_pixel,
	[ARM_64_LPAE_S2] = &io_pgtable_arm_64_lpae_s2_init_fns_pixel,
};

static int check_custom_allocator(enum io_pgtable_fmt fmt,
				  struct io_pgtable_cfg *cfg)
{
	/* No custom allocator, no need to check the format. */
	if (!cfg->alloc && !cfg->free)
		return 0;

	/* When passing a custom allocator, both the alloc and free
	 * functions should be provided.
	 */
	if (!cfg->alloc || !cfg->free)
		return -EINVAL;

	/* Make sure the format supports custom allocators. */
	if (io_pgtable_init_table[fmt]->caps & IO_PGTABLE_CAP_CUSTOM_ALLOCATOR)
		return 0;

	return -EINVAL;
}

struct io_pgtable_ops *alloc_io_pgtable_ops_pixel(enum io_pgtable_fmt fmt,
						  struct io_pgtable_cfg *cfg,
						  void *cookie)
{
	struct io_pgtable *iop;
	const struct io_pgtable_init_fns *fns;

	if (fmt >= IO_PGTABLE_NUM_FMTS)
		return NULL;

	if (check_custom_allocator(fmt, cfg))
		return NULL;

	fns = io_pgtable_init_table[fmt];
	if (!fns)
		return NULL;

	iop = fns->alloc(cfg, cookie);
	if (!iop)
		return NULL;

	iop->fmt	= fmt;
	iop->cookie	= cookie;
	iop->cfg	= *cfg;

	return &iop->ops;
}
EXPORT_SYMBOL_GPL(alloc_io_pgtable_ops_pixel);

/*
 * It is the IOMMU driver's responsibility to ensure that the page table
 * is no longer accessible to the walker by this point.
 */
void free_io_pgtable_ops_pixel(struct io_pgtable_ops *ops)
{
	struct io_pgtable *iop;

	if (!ops)
		return;

	iop = io_pgtable_ops_to_pgtable(ops);
	io_pgtable_tlb_flush_all(iop);
	io_pgtable_init_table[iop->fmt]->free(iop);
}
EXPORT_SYMBOL_GPL(free_io_pgtable_ops_pixel);

int io_pgtable_configure_pixel(struct io_pgtable_cfg *cfg)
{
	const struct io_pgtable_init_fns *fns;

	if (cfg->fmt >= IO_PGTABLE_NUM_FMTS)
		return -EINVAL;

	fns = io_pgtable_init_table[cfg->fmt];
	if (!fns || !fns->configure)
		return -EOPNOTSUPP;

	return fns->configure(cfg);
}
EXPORT_SYMBOL_GPL(io_pgtable_configure_pixel);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Pixel's custom ARM page table allocator");
