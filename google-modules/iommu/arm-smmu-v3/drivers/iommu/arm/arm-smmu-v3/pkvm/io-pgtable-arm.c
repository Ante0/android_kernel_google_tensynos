// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Arm Ltd.
 */
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <linux/types.h>
#include <linux/gfp_types.h>
#include <arm-smmu-v3/io-pgtable-arm.h>

#include <nvhe/alloc.h>
#include <nvhe/iommu.h>
#include <nvhe/mem_protect.h>

#include "arm_smmu_v3.h"
#include "arm-smmu-v3-module.h"

#define io_pgtable_cfg_to_pgtable(x) container_of((x), struct io_pgtable, cfg)

#define io_pgtable_cfg_to_data(x)					\
	io_pgtable_to_data(io_pgtable_cfg_to_pgtable(x))

int arm_lpae_map_exists(void)
{
	return -EEXIST;
}

void arm_lpae_unmap_empty(void)
{
}

void *__arm_lpae_alloc_pages(size_t size, gfp_t gfp,
			     struct io_pgtable_cfg *cfg, void *cookie)
{
	void *addr;
	struct arm_lpae_io_pgtable *data = io_pgtable_cfg_to_data(cfg);

	if (!PAGE_ALIGNED(size))
		return NULL;

	if (data->idmapped) {
		addr = kvm_iommu_donate_pages_atomic(get_order(size));
		WARN_ON(!addr);
		cfg->telemetry_cb->atomic_pages_tel(size >> PAGE_SHIFT);
	} else {
		addr = kvm_iommu_donate_pages(get_order(size), 0);
		if (addr)
			cfg->telemetry_cb->s1_pages_tel(size >> PAGE_SHIFT);
	}

	if (addr && !cfg->coherent_walk)
		kvm_flush_dcache_to_poc(addr, size);

	return addr;
}

void __arm_lpae_free_pages(void *addr, size_t size, struct io_pgtable_cfg *cfg,
			   void *cookie)
{
	u8 order;
	struct arm_lpae_io_pgtable *data = io_pgtable_cfg_to_data(cfg);

	/*
	 * It's guaranteed all allocations are aligned, but core code
	 * might free PGD with it's actual size.
	 */
	order = get_order(PAGE_ALIGN(size));

	if (!cfg->coherent_walk)
		kvm_flush_dcache_to_poc(addr, size);

	if (data->idmapped) {
		kvm_iommu_reclaim_pages_atomic(addr, order);
		cfg->telemetry_cb->atomic_pages_tel(-(size >> PAGE_SHIFT));
	} else {
		kvm_iommu_reclaim_pages(addr, order);
		cfg->telemetry_cb->s1_pages_tel(-(size >> PAGE_SHIFT));
	}
}

void __arm_lpae_sync_pte(arm_lpae_iopte *ptep, int num_entries,
			 struct io_pgtable_cfg *cfg)
{
	if (!cfg->coherent_walk)
		kvm_flush_dcache_to_poc(ptep, sizeof(*ptep) * num_entries);
}

static int kvm_arm_io_pgtable_init(struct io_pgtable_cfg *cfg,
				   struct arm_lpae_io_pgtable *data)
{
	int ret = -EINVAL;

	if (cfg->fmt == ARM_64_LPAE_S2)
		ret = arm_lpae_init_pgtable_s2(cfg, data);
	else if (cfg->fmt == ARM_64_LPAE_S1)
		ret = arm_lpae_init_pgtable_s1(cfg, data);

	if (ret)
		return ret;

	data->iop.cfg = *cfg;
	data->iop.fmt	= cfg->fmt;

	return 0;
}

struct io_pgtable *kvm_arm_io_pgtable_alloc_pixel(struct io_pgtable_cfg *cfg,
						  void *cookie,
						  int *out_ret)
{
	size_t pgd_size, alignment;
	struct arm_lpae_io_pgtable *data;
	int ret;

	data = hyp_alloc(sizeof(*data));
	if (!data) {
		kvm_iommu_request_hyp_alloc();
		*out_ret = hyp_alloc_errno();
		return NULL;
	}

	ret = kvm_arm_io_pgtable_init(cfg, data);
	if (ret)
		goto out_free;

	pgd_size = PAGE_ALIGN(ARM_LPAE_PGD_SIZE(data));
	data->pgd = __arm_lpae_alloc_pages(pgd_size, 0, &data->iop.cfg, cookie);
	if (!data->pgd) {
		ret = -ENOMEM;
		goto out_free;
	}
	/*
	 * If it has eight or more entries, the table must be aligned on
	 * its size. Otherwise 64 bytes.
	 */
	alignment = max(pgd_size, 8 * sizeof(arm_lpae_iopte));
	if (!IS_ALIGNED(hyp_virt_to_phys(data->pgd), alignment)) {
		__arm_lpae_free_pages(data->pgd, pgd_size,
				      &data->iop.cfg, cookie);
		ret = -EINVAL;
		goto out_free;
	}

	data->iop.cookie = cookie;
	if (cfg->fmt == ARM_64_LPAE_S2)
		data->iop.cfg.arm_lpae_s2_cfg.vttbr = __arm_lpae_virt_to_phys(data->pgd);
	else if (cfg->fmt == ARM_64_LPAE_S1)
		data->iop.cfg.arm_lpae_s1_cfg.ttbr = __arm_lpae_virt_to_phys(data->pgd);

	if (!data->iop.cfg.coherent_walk)
		kvm_flush_dcache_to_poc(data->pgd, pgd_size);

	/* Ensure the empty pgd is visible before any actual TTBR write */
	wmb();

	*out_ret = 0;
	return &data->iop;
out_free:
	hyp_free(data);
	*out_ret = ret;
	return NULL;
}

int kvm_arm_io_pgtable_free_pixel(struct io_pgtable *iopt)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_to_data(iopt);
	size_t pgd_size = ARM_LPAE_PGD_SIZE(data);

	if (!data->iop.cfg.coherent_walk)
		kvm_flush_dcache_to_poc(data->pgd, pgd_size);

	io_pgtable_tlb_flush_all(iopt);
	__arm_lpae_free_pgtable(data, data->start_level, data->pgd);
	hyp_free(data);
	return 0;
}

static bool arm_lpae_iopte_is_mmio(struct arm_lpae_io_pgtable *data,
				   arm_lpae_iopte pte)
{
	if (data->iop.fmt == ARM_64_LPAE_S1)
		return ((pte >> ARM_LPAE_PTE_ATTRINDX_SHIFT) & 0x7) == ARM_LPAE_MAIR_ATTR_IDX_DEV;

	return (pte & (0xf << 2)) == ARM_LPAE_PTE_MEMATTR_DEV;
}

#define ARM_LPAE_TABLE_LAST_IDX	GENMASK(7, 2)
static u32 arm_lpae_table_get_last_idx(struct arm_lpae_io_pgtable *data,
				       arm_lpae_iopte table)
{
	u16 val = FIELD_GET(ARM_LPAE_TABLE_LAST_IDX, table);

	return val << (data->bits_per_level - 6);
}

static void arm_lpae_table_set_last_idx(struct arm_lpae_io_pgtable *data,
					arm_lpae_iopte *tablep, u32 idx)
{
	u16 val = idx >> (data->bits_per_level - 6);

	u64p_replace_bits(tablep, val, ARM_LPAE_TABLE_LAST_IDX);
}

static bool arm_lpae_scan_next_level(struct arm_lpae_io_pgtable *data,
				     arm_lpae_iopte *tablep, u32 level)
{
	u32 n, idx, start, nentries;
	arm_lpae_iopte table = *tablep, *cptep = iopte_deref(table, data);

	nentries = ARM_LPAE_PTES_PER_TABLE(data);
	start = arm_lpae_table_get_last_idx(data, table);
	idx = start;

	for (n = 0; n < nentries; ++n) {
		arm_lpae_iopte pte = cptep[idx];

		if (!iopte_leaf(pte, level, data->iop.fmt) || arm_lpae_iopte_is_mmio(data, pte))
			break;

		idx = (idx + 1) % nentries;
	}

	if (n != nentries && idx != start)
		arm_lpae_table_set_last_idx(data, tablep, idx);

	return n == nentries;
}

void arm_lpae_post_table_walk(struct arm_lpae_io_pgtable *data,
			      unsigned long iova, size_t pgsize,
			      size_t pgcount, arm_lpae_iopte prot,
			      int level, arm_lpae_iopte *ptep)
{
	size_t block_size = ARM_LPAE_BLOCK_SIZE(level, data);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;

	if (!data->idmapped)
		return;

	/* Last level always leafs. */
	if (level >= ARM_LPAE_MAX_LEVELS - 1)
		return;

	/* Can't install a block here. */
	if (block_size != (block_size & cfg->pgsize_bitmap))
		return;

	if (!arm_lpae_scan_next_level(data, ptep, level + 1))
		return;

	iova &= ~(block_size - 1);
	WARN_ON(arm_lpae_init_pte(data, iova, iova, prot, level, 1, ptep));
}

static inline arm_lpae_iopte *contpte_align_down(struct arm_lpae_io_pgtable *data,
						 int lvl, arm_lpae_iopte *ptep)
{
	int adj_contpte = arm_lpae_adjacent_contptes(lvl, data);

	return PTR_ALIGN_DOWN(ptep, sizeof(*ptep) * adj_contpte);
}

static void __arm_lpae_coalesce_contptes(struct arm_lpae_io_pgtable *data,
					 int lvl, arm_lpae_iopte *ptep)
{
	size_t sz = ARM_LPAE_BLOCK_SIZE(lvl, data);
	int adj_contpte = arm_lpae_adjacent_contptes(lvl, data);
	arm_lpae_iopte expected_pte;
	arm_lpae_iopte *start_ptep, *end_ptep;
	phys_addr_t paddr;
	int i;

	start_ptep = contpte_align_down(data, lvl, ptep);
	end_ptep = start_ptep + adj_contpte;

	expected_pte = *ptep & ~ARM_LPAE_PTE_ADDR_MASK;
	paddr = iopte_to_paddr(*ptep, data);

	ptep++;
	paddr += sz;

	for (i = 1; i < adj_contpte; i++) {
		if (ptep == end_ptep) {
			ptep = start_ptep;
			paddr -= sz * adj_contpte;
		}
		if (*ptep != (expected_pte | paddr_to_iopte(paddr, data)))
			return;
		ptep++;
		paddr += sz;
	}

	for (i = 0; i < adj_contpte; i++)
		start_ptep[i] |= ARM_LPAE_PTE_CONT;
}

void arm_lpae_coalesce_contptes(struct arm_lpae_io_pgtable *data,
				int lvl, int num_entries,
				arm_lpae_iopte *ptep)
{
	arm_lpae_iopte *start_group, *end_group, *last_ptep, *next_ptep, *next_group;

	if (!data->idmapped)
		return;

	if (!num_entries)
		return;

	start_group = contpte_align_down(data, lvl, ptep);
	next_ptep = ptep + num_entries;
	last_ptep = next_ptep - 1;
	end_group = contpte_align_down(data, lvl, next_ptep - 1);
	next_group = contpte_align_down(data, lvl, next_ptep);
	/*
	 * Unless the array of newly initialized ptes ends on a group boundary,
	 * we want to look at the existing ptes immediately following the newly
	 * initialized ptes.
	 */
	if (next_ptep != next_group) {
		__arm_lpae_coalesce_contptes(data, lvl, last_ptep);
		if (start_group == end_group)
			return;
	}

	if (start_group != ptep && iopte_leaf(*start_group, lvl, data->iop.fmt))
		__arm_lpae_coalesce_contptes(data, lvl, start_group);
}
