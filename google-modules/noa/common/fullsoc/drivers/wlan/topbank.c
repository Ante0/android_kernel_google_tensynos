// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM WLAN Top Bank
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file provides functions for managing the WLAN top bank registers.
 * It includes functions for initializing and de-initializing the top bank,
 * as well as functions for reading from and writing to the top bank registers.
 */

#include "topbank.h"

/**
 * lvm_wlan_topbank_init - Initialize the WLAN top bank
 * @data: Pointer to the WLAN data structure
 *
 * This function initializes the WLAN top bank structure and
 * enables the WLAN device and doorbell.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_wlan_topbank_init(struct wlan_data *data)
{
	struct wlan_topbank *topbank;

	if (!data)
		return -EINVAL;

	topbank = kzalloc(sizeof(struct wlan_topbank), GFP_KERNEL);
	if (!topbank)
		return -ENOMEM;

	topbank->regbase_pa = data->param->reg.fake_dev_base;
	topbank->regbase_va = ioremap(topbank->regbase_pa, WLAN_TOP_REG_RANGE);
	if (!topbank->regbase_va)
		return -EINVAL;

	data->topbank = topbank;

	lvm_wlan_topbank_reg_setbits(topbank,
				     WLAN_TOP_REG_DEV_ENABLE,
				     WLAN_TOP_REG_DEV_ENABLE_MASK);
	lvm_wlan_topbank_reg_setbits(topbank,
				     WLAN_TOP_REG_DOORBELL_ENABLE,
				     WLAN_TOP_REG_DOORBELL_ENABLE_MASK);

	return 0;
}

/**
 * lvm_wlan_topbank_deinit - Deinitialize the WLAN top bank
 * @data: Pointer to the WLAN data structure
 *
 * This function deinitializes the WLAN top bank by disabling the WLAN
 * device and doorbell.
 */
void lvm_wlan_topbank_deinit(struct wlan_data *data)
{
	if (!data || !data->topbank)
		return;

	lvm_wlan_topbank_reg_clrbits(data->topbank,
				     WLAN_TOP_REG_DEV_ENABLE,
				     WLAN_TOP_REG_DEV_ENABLE_MASK);
	lvm_wlan_topbank_reg_clrbits(data->topbank,
				     WLAN_TOP_REG_DOORBELL_ENABLE,
				     WLAN_TOP_REG_DOORBELL_ENABLE_MASK);

	iounmap(data->topbank->regbase_va);

	kfree(data->topbank);
}
