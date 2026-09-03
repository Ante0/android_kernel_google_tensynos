// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google LLC
 * Author: Ji Soo Shin <jisshin@google.com>
 * Simple module for Pixel pKVM SMC filtering.
 */

#include <asm/kvm_pkvm_module.h>
#include <linux/arm-smccc.h>
#include <linux/limits.h>
#include <nvhe/spinlock.h>

#define SMCCC_DRM_HISTOGRAM_BINS_SEC (0x82002014)

static hyp_spinlock_t hist_prot_pfn_lock;
const struct pkvm_module_ops *pkvm_ops;

static bool handle_histogram_bins_sec(struct user_pt_regs *regs)
{
	int ret;
	u64 new_pfn = regs->regs[2] >> PAGE_SHIFT;
	static u64 hist_prot_pfn = U64_MAX;

	hyp_spin_lock(&hist_prot_pfn_lock);

	if (hist_prot_pfn != U64_MAX) {
		ret = (hist_prot_pfn != new_pfn) ? EPERM : 0;
	} else {
		/* Only needed to mark the page to PKVM_MODULE_OWNED_PAGE */
		ret = pkvm_ops->host_stage2_mod_prot(new_pfn,
			KVM_PGTABLE_PROT_RW, 1, false);
		if (!ret)
			hist_prot_pfn = new_pfn;
	}

	hyp_spin_unlock(&hist_prot_pfn_lock);

	if (ret) {
		regs->regs[0] = SMCCC_RET_NOT_SUPPORTED;
		return true;
	}

	return false;
}

/*
 * return false will allow the SMC to be forwarded.
 */
bool filter_smc(struct user_pt_regs *regs)
{
	/*
	 * Ignore bits that doesn't change the functionality:
	 * Bit[30]: 32/64 bit convention
	 * Bit[16]: SVE hint
	 */
	u64 mask = ~(ARM_SMCCC_1_3_SVE_HINT | BIT(ARM_SMCCC_CALL_CONV_SHIFT));
	u64 smc_id = regs->regs[0] & mask;

	if (smc_id == SMCCC_DRM_HISTOGRAM_BINS_SEC)
		return handle_histogram_bins_sec(regs);

	return false;
}

int pkvm_smc_filter_hyp_init(const struct pkvm_module_ops *ops)
{
	pkvm_ops = ops;
	hyp_spin_lock_init(&hist_prot_pfn_lock);
	return ops->register_host_smc_handler(filter_smc);
}
