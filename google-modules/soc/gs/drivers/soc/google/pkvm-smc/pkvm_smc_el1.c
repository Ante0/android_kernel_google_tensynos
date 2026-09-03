// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google LLC
 * Author: Ji Soo Shin <jisshin@google.com>
 * Pixel adaptation for pKVM SMC filtering module.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>

int kvm_nvhe_sym(pkvm_smc_filter_hyp_init)(const struct pkvm_module_ops *ops);

static int __init smc_filter_init(void)
{
	return pkvm_load_el2_module(kvm_nvhe_sym(pkvm_smc_filter_hyp_init),
				   NULL);
}

module_init(smc_filter_init);

MODULE_AUTHOR("Ji Soo Shin <jisshin@google.com>");
MODULE_DESCRIPTION("Pixel pKVM SMC filter");
MODULE_LICENSE("GPL");
