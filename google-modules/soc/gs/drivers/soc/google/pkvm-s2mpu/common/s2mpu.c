// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>
#include "kvm_s2mpu.h"
#include <asm/kvm_host.h>
#include <asm/kvm_asm.h>
#include <soc/google/pkvm-s2mpu.h>

/* Token of S2MPU driver, token is the load address of the module. */
static u64 token;
/* For an nvhe symbol loaded as a module, get the hyp address of it. */
#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x)) *)(pkvm_el2_mod_va(&kvm_nvhe_sym(x), token)))

extern struct kvm_iommu_ops kvm_nvhe_sym(s2mpu_hyp_ops);

extern char __kvm_nvhe___hypmod_text_start[];

extern struct mpt *kvm_nvhe_sym(kvm_hyp_mpt);
#define kvm_hyp_mpt kvm_nvhe_sym(kvm_hyp_mpt)

static int init_s2mpu_driver(u64 tok)
{
	static DEFINE_MUTEX(lock);
	static bool init_done;
	struct mpt *mpt;
	unsigned int gb;
	unsigned long addr;
	u64 pfn;
	int ret = 0;
	const int smpt_order = SMPT_ORDER(MPT_PROT_BITS);
	struct kvm_hyp_memcache atomic_mc = {};

	mutex_lock(&lock);
	if (init_done)
		goto out;

	token = tok;
	/* Allocate a page for driver data. Must fit MPT descriptor. */
	BUILD_BUG_ON(sizeof(*mpt) > PAGE_SIZE);
	addr = __get_free_page(GFP_KERNEL);
	if (!addr) {
		ret = -ENOMEM;
		goto out;
	}

	mpt = (struct mpt *)addr;

	/* Allocate SMPT buffers. */
	for_each_gb(gb) {
		addr = __get_free_pages(GFP_KERNEL, smpt_order);
		if (!addr) {
			ret = -ENOMEM;
			goto out_free;
		}
		mpt->fmpt[gb].smpt = (u32 *)addr;
	}

	/* Share MPT descriptor with hyp. */
	pfn = __pa(mpt) >> PAGE_SHIFT;
	ret = kvm_call_hyp_nvhe(__pkvm_host_share_hyp, pfn);
	if (ret)
		goto out_free;

	kvm_hyp_mpt = mpt;
	ret = kvm_iommu_init_hyp(ksym_ref_addr_nvhe(s2mpu_hyp_ops), &atomic_mc);
	if (ret)
		goto out_unshare;

	init_done = true;

out_unshare:
	WARN_ON(kvm_call_hyp_nvhe(__pkvm_host_unshare_hyp, pfn));
out_free:
	/* TODO - will driver return the memory? */
	if (ret) {
		for_each_gb(gb)
			free_pages((unsigned long)mpt->fmpt[gb].smpt, smpt_order);
		free_page((unsigned long)mpt);
	}
out:
	mutex_unlock(&lock);
	return ret;
}

int pkvm_iommu_s2mpu_init(u64 token)
{
	if (!is_protected_kvm_enabled())
		return -ENODEV;

	return init_s2mpu_driver(token);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_s2mpu_init);
