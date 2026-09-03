// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA Core
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_data/sscoredump.h>
#include <common/core.h>
#include "noa.h"
#include "pcie_service/noa_pcie_rpc.h"
#ifdef CONFIG_NOA_MD_MEDIATEK_SUPPORT
#include "md/mediatek/noa_md.h"
#endif

static struct sscd_platform_data wlan_sscd_pdata;
static void noa_wlan_sscd_release(struct device *dev)
{
}
static struct platform_device wlan_sscd_pdev = {
	.name            = "noa_wlan",
	.driver_override = SSCD_NAME,
	.id              = -1,
	.dev             = {
		.platform_data = &wlan_sscd_pdata,
		.release = &noa_wlan_sscd_release,
		},
};

static struct noa_core core;

void *noa_entry_register(u32 prv_sz, const char *name, struct kobj_type *ktype)
{
	u32 sz = sizeof(struct noa_core_entry) + ALIGN(prv_sz, 4);
	struct noa_core_entry *entry = kzalloc(sz, GFP_KERNEL);

	if (!entry)
		return NULL;

	entry->core = &core;
	list_add_tail(&entry->list, &core.entries);
	noa_subsysfs_init(entry->priv, name, ktype);
	return entry->priv;
}

void noa_entry_unregister(void *priv)
{
	struct noa_core_entry *entry = container_of(priv, struct noa_core_entry, priv);

	noa_subsysfs_exit(priv);
	list_del(&entry->list);
	kfree(entry);
}

bool noa_wlan_enable_get(void)
{
	return core.noa_wlan_enable;
}

void noa_wlan_enable_set(bool enable){
	core.noa_wlan_enable = enable;
}

void noa_wlan_get_sscd_src(void **pdata, void **pdev)
{
	if (pdata)
		*pdata = (void *)&wlan_sscd_pdata;
	if (pdev)
		*pdev = (void *)&wlan_sscd_pdev;
}

static int __init noa_init(void)
{
	pr_debug("module init: %s\n", __func__);
	INIT_LIST_HEAD(&core.entries);
	if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
		core.noa_wlan_enable = false;
	} else {
		core.noa_wlan_enable = true;
	}
	noa_sysfs_init(&core);
#ifdef CONFIG_NOA_MD_MEDIATEK_SUPPORT
	noa_md_init();
#endif
	platform_device_register(&wlan_sscd_pdev);
	noa_pcie_rpc_init();
	return 0;
}

static void __exit noa_exit(void)
{
	noa_pcie_rpc_exit();
#ifdef CONFIG_NOA_MD_MEDIATEK_SUPPORT
	noa_md_exit();
#endif
	noa_sysfs_exit(&core);
	platform_device_unregister(&wlan_sscd_pdev);
	pr_debug("module exit: %s\n", __func__);
}

module_init(noa_init);
module_exit(noa_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Star Chang <starchang@google.com>");
MODULE_DESCRIPTION("NOA Core Driver");
