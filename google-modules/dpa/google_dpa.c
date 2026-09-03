// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for DPA device.
 *
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/reset.h>
#include <linux/vmalloc.h>

#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/google_dpa.h>
#include <soc/google/google_dpa_doorbell.h>

#include "google_dpa_boot.h"
#include "google_dpa_crash_dump_internal.h"
#include "google_dpa_ctrl_internal.h"
#include "google_dpa_internal.h"
#include "google_dpa_log_proxy.h"
#include "google_dpa_netlink.h"
#include "google_dpa_rpc_internal.h"
#include "google_dpa_secure.h"
#include "google_dpa_sysfs.h"
#include "google_dpa_power_domain.h"

#define GOOGLE_DPA_DEFAULT_NCP_FIRMWARE_NAME "ncp.bin"
#define GOOGLE_DPA_DEFAULT_NEP_FIRMWARE_NAME "nep.bin"
#define GOOGLE_DPA_DEFAULT_DPA_FIRMWARE_NAME "noa.bin"
#define GOOGLE_DPA_EMULATION_TIME_MULTIPLIER 200
#define GOOGLE_DPA_BOOT_COMPLETION_TIMEOUT_US (1000 * 1000) // 1sec

/* TODO: Remove default offsets after all DT are migrated */
#define GOOGLE_DPA_LPM_NCP_M55WAIT_COMPLETED_DEFAULT_OFFSET 0xC008
#define GOOGLE_DPA_LPM_PSM_STATUS_DEFAULT_OFFSET 0x1040

/*
 * TODO(b/379034261): We should obtain this address from the resource table or image config.
 */
#define GOOGLE_DPA_SHARED_INFO_DEFAULT_DEVICE_ADDRESS 0x40100000

/* TODO(b/409201019): Use the secure boot by default */
static bool use_secure_boot;
module_param(use_secure_boot, bool, 0444);
MODULE_PARM_DESC(use_secure_boot, "Use the secure firmware boot");

#ifndef GOOGLE_DPA_BOOT_AT_PROBE
#define GOOGLE_DPA_BOOT_AT_PROBE false
#endif
static bool boot_at_probe = GOOGLE_DPA_BOOT_AT_PROBE;
module_param(boot_at_probe, bool, 0444);
MODULE_PARM_DESC(boot_at_probe, "Enable auto-booting of DPA firmware at probe");

/* TODO(b/426465018): Remove this flag */
static bool use_merged_firmware = true;
module_param(use_merged_firmware, bool, 0444);
MODULE_PARM_DESC(use_merged_firmware, "Use the merged firmware image");

void *google_dpa_da_to_va_internal(struct google_dpa *dpa, struct google_dpa_mcu *mcu, u64 da,
				   size_t len, bool *is_iomem)
{
	struct google_dpa_addr_mapping *mapping;
	ptrdiff_t start, end, offset;

	list_for_each_entry(mapping, &mcu->addr_mappings, node) {
		start = mapping->mcu_view_addr;
		end = start + mapping->len;
		if (start <= da && da + len <= end) {
			*is_iomem = true;
			offset = da - start;
			return mapping->ioremapped_addr + offset;
		}
	}

	return NULL;
}

void *google_dpa_da_to_va(struct google_dpa *dpa, enum google_dpa_mcu_kind mcu_kind, u64 da,
			  size_t len, bool *is_iomem)
{
	struct google_dpa_mcu *mcu;

	switch (mcu_kind) {
	case GOOGLE_DPA_MCU_NCP:
		mcu = &dpa->ncp;
		break;
	case GOOGLE_DPA_MCU_NEP:
		mcu = &dpa->nep;
		break;
	default:
		dev_err(dpa->dev, "Invalid mcu kind %u to transform da to va.\n", mcu_kind);
		return NULL;
	}

	return google_dpa_da_to_va_internal(dpa, mcu, da, len, is_iomem);
}
EXPORT_SYMBOL_GPL(google_dpa_da_to_va);

static int google_dpa_get_fw_img_carveout(struct google_dpa *dpa,
					  struct google_dpa_carveout_region *carveout)
{
	struct device *dev = dpa->dev;
	struct device_node *mem_node;
	struct reserved_mem *reserved_mem;
	void *vaddr;

	carveout->va = NULL;
	carveout->pa = 0;
	carveout->len = 0;

	mem_node = of_parse_phandle(dev->of_node, "fw-image-carveout", 0);
	if (!mem_node) {
		dev_err(dev, "Failed to parse fw-image-carveout property.\n");
		return -ENOENT;
	}

	reserved_mem = of_reserved_mem_lookup(mem_node);
	of_node_put(mem_node);
	if (!reserved_mem) {
		dev_err(dev, "Failed to get a reserved memory from fw-image-carveout property.\n");
		return -EINVAL;
	}

	vaddr = devm_memremap(dev, reserved_mem->base, reserved_mem->size, MEMREMAP_WC);
	if (!vaddr) {
		dev_err(dev, "Failed to map the fw-image-carveout region\n");
		return -EINVAL;
	}

	carveout->va = vaddr;
	carveout->pa = reserved_mem->base;
	carveout->len = reserved_mem->size;

	return 0;
}

static int google_dpa_dev_pm_genpd_add_notifier(struct google_dpa *dpa)
{
	return dev_pm_genpd_add_notifier(dpa->pd_vdev, &dpa->power_domain_notify);
}

static void google_dpa_dev_pm_genpd_remove_notifier(struct google_dpa *dpa)
{
	dev_pm_genpd_remove_notifier(dpa->pd_vdev);
}

static int google_dpa_suspend(struct device *dev)
{
	if (google_dpa_ctrl_get_state() == NOA_STATE_READY)
		google_dpa_ctrl_set_pcie_ownership(NOA_PCIE_OWNERSHIP_DPA);
	return pm_runtime_force_suspend(dev);
}

static int google_dpa_resume(struct device *dev)
{
	if (google_dpa_ctrl_get_state() == NOA_STATE_READY)
		google_dpa_ctrl_set_pcie_ownership(NOA_PCIE_OWNERSHIP_APC);
	return pm_runtime_force_resume(dev);
}

static int google_dpa_get_doorbells(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	struct device_node *np = dev->of_node;
	int num, i;

	num = of_property_count_strings(np, "doorbell_ids");
	if (num < 0) {
		dev_err(dev, "Failed to count \"doorbell_ids\" property.\n");
		return num;
	}

	dpa->num_doorbell = num;
	dpa->doorbells = devm_kcalloc(dev, num, sizeof(dpa->doorbells[0]), GFP_KERNEL);
	if (!dpa->doorbells)
		return -ENOMEM;

	for (i = 0; i < num; ++i) {
		dpa->doorbells[i] = google_dpa_get_doorbell_by_index(dev, i);
		if (IS_ERR(dpa->doorbells[i]))
			return dev_err_probe(dev, PTR_ERR(dpa->doorbells[i]),
					     "Failed to get doorbell at index %d.\n", i);
	}

	return 0;
}

static __maybe_unused irqreturn_t google_dpa_cpm_int_handler(int irq, void *dev)
{
	return IRQ_HANDLED;
}

static int google_dpa_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_dpa *dpa;
	struct gdmc_iface *gdmc_iface;
	struct resource *res;
	int ret;

	gdmc_iface = gdmc_iface_get(dev);
	if (IS_ERR(gdmc_iface))
		return dev_err_probe(dev, PTR_ERR(gdmc_iface), "Failed to get gdmc_iface.\n");

	dpa = devm_kzalloc(dev, sizeof(*dpa), GFP_KERNEL);
	if (!dpa) {
		ret = -ENOMEM;
		goto put_gdmc_iface;
	}

	dpa->use_log_proxy = true;
	dpa->gdmc_iface = gdmc_iface;
	INIT_LIST_HEAD(&dpa->iommu_mappings);
	INIT_LIST_HEAD(&dpa->ncp.addr_mappings);
	INIT_LIST_HEAD(&dpa->nep.addr_mappings);
	dpa->use_secure_boot = use_secure_boot;
	dpa->use_merged_firmware_image = use_merged_firmware;
	mutex_init(&dpa->mutex);
	dpa->fw_boot_completion_timeout_us = GOOGLE_DPA_BOOT_COMPLETION_TIMEOUT_US;
	if (of_property_present(pdev->dev.of_node, "in_emulation"))
		dpa->fw_boot_completion_timeout_us *= GOOGLE_DPA_EMULATION_TIME_MULTIPLIER;
	dpa->fw_state = GOOGLE_DPA_FW_UNLOADED;
	init_completion(&dpa->power_off_completion);
	dpa->dev = dev;
	dpa->power_domain_notify.notifier_call = google_dpa_power_domain_notify_callback;
	platform_set_drvdata(pdev, dpa);

	ret = google_dpa_get_doorbells(dpa);
	if (ret)
		goto put_gdmc_iface;

	ret = google_dpa_get_fw_img_carveout(dpa, &dpa->fw_img_carveout);
	if (ret == -ENOENT && !dpa->use_secure_boot)
		dev_warn(dev,
			 "Falling back to dynamic allocation. This is supported only in NS boot.");
	else if (ret)
		goto put_gdmc_iface;

#ifdef USE_PIXEL_CPM
	int irq =  platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Failed to get irq, err: %d\n", ret);
		ret = irq;
		goto put_gdmc_iface;
	}

	ret = devm_request_irq(dev, irq, google_dpa_cpm_int_handler, 0, dev_name(dev), NULL);
	if (ret < 0) {
		dev_err(dev, "Failed to request irq, err: %d\n", ret);
		goto put_gdmc_iface;
	}

	ret = device_init_wakeup(dev, of_property_read_bool(dev->of_node, "wakeup-source"));
	if (ret < 0) {
		dev_err(dev, "Failed to init wakeup, err: %d\n", ret);
		goto put_gdmc_iface;
	}

	ret = dev_pm_set_wake_irq(dev, irq);
	if (ret < 0) {
		dev_err(dev, "Failed to set wake irq, err: %d\n", ret);
		goto put_gdmc_iface;
	}
#endif // USE_PIXEL_CPM

	// TODO(b/369258778): Remove ioremap and use CPM interface.
	//                    CPM still do not have mailbox services for
	//                    changing the DPA core state and this driver needs
	//                    to updates PCIe LPM directly.
	//                    lpm-pm-domains claims this region and this driver
	//                    cannot use devm_platform_ioremap_resource_byname()
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpm");
	if (!res) {
		dev_err(dev, "Failed to find \"lpm\" reg entry.\n");
		ret = -EINVAL;
		goto put_gdmc_iface;
	}
	dpa->lpm_base = devm_ioremap(dev, res->start, res->end - res->start);
	if (!dpa->lpm_base) {
		dev_err(dev, "Failed to ioremap \"lpm\" region.\n");
		ret = -EINVAL;
		goto put_gdmc_iface;
	}

	ret = of_property_read_u32(dev->of_node, "google,dpa-lpm-ncp-wait-complete-offset",
				   &dpa->lpm_ncp_wait_complete_offset);
	if (ret) {
		dpa->lpm_ncp_wait_complete_offset =
			GOOGLE_DPA_LPM_NCP_M55WAIT_COMPLETED_DEFAULT_OFFSET;
	}
	ret = of_property_read_u32(dev->of_node, "google,dpa-lpm-psm-status-offset",
				   &dpa->lpm_psm_status_offset);
	if (ret) {
		dpa->lpm_psm_status_offset = GOOGLE_DPA_LPM_PSM_STATUS_DEFAULT_OFFSET;
	}

	/*
	 * TODO(b/379034261): We should obtain this address from the resource table or image config
	 */
	ret = of_property_read_u32(dev->of_node, "google,dpa-shared-info-device-address",
				   &dpa->shared_info_device_address);
	if (ret) {
		dpa->shared_info_device_address = GOOGLE_DPA_SHARED_INFO_DEFAULT_DEVICE_ADDRESS;
	}

#ifdef USE_PIXEL_CPM
	dpa->uart_clk = devm_clk_get(dev, "dpa_uart_clk");
	if (IS_ERR(dpa->uart_clk)) {
		dev_err(dev, "Failed to get DPA UART clock.\n");
		ret = PTR_ERR(dpa->uart_clk);
		goto put_gdmc_iface;
	}
	dpa->uart_clk_reset = devm_reset_control_get_exclusive(dev, "dpa_uart_clk");
	if (IS_ERR(dpa->uart_clk_reset)) {
		dev_err(dev, "Failed to get DPA UART clock reset controller.\n");
		ret = PTR_ERR(dpa->uart_clk_reset);
		goto put_gdmc_iface;
	}
#endif // USE_PIXEL_CPM

	dpa->ncp.firmware_name = kmemdup(GOOGLE_DPA_DEFAULT_NCP_FIRMWARE_NAME,
					 sizeof(GOOGLE_DPA_DEFAULT_NCP_FIRMWARE_NAME), GFP_KERNEL);
	if (!dpa->ncp.firmware_name) {
		ret = -ENOMEM;
		goto put_gdmc_iface;
	}
	dpa->nep.firmware_name = kmemdup(GOOGLE_DPA_DEFAULT_NEP_FIRMWARE_NAME,
					 sizeof(GOOGLE_DPA_DEFAULT_NEP_FIRMWARE_NAME), GFP_KERNEL);
	if (!dpa->nep.firmware_name) {
		ret = -ENOMEM;
		goto free_ncp_firmware_name;
	}
	dpa->firmware_name = kmemdup(GOOGLE_DPA_DEFAULT_DPA_FIRMWARE_NAME,
				     sizeof(GOOGLE_DPA_DEFAULT_DPA_FIRMWARE_NAME), GFP_KERNEL);
	if (!dpa->firmware_name) {
		ret = -ENOMEM;
		goto free_nep_firmware_name;
	}

	ret = google_dpa_attach_power_domain(dev, &dpa->pd_vdev, &dpa->pd_link);
	if (ret) {
		dev_err(dev, "Failed ot attach a power domain.\n");
		goto free_firmware_name;
	}

	ret = google_dpa_dev_pm_genpd_add_notifier(dpa);
	if (ret) {
		dev_err(dev, "Failed to register a DPA power-domain notification callback.\n");
		goto detach_power_domain;
	}

	ret = devm_pm_runtime_enable(dev);
	if (ret) {
		dev_err(dev, "Failed to enable runtime PM\n");
		goto remove_genpd_notifier;
	}

	ret = gdmc_register_dpa_reset_notifier(dpa->gdmc_iface, google_dpa_crash_callback, dpa);
	if (ret) {
		dev_err(dev, "Failed to register a DPA crash notification callback.\n");
		goto remove_genpd_notifier;
	}

	ret = google_dpa_init_crashdump(dpa);
	if (ret != 0) {
		dev_err(dev, "Failed to initialize crashdump module.\n");
		goto unregister_dpa_reset_notifier;
	}

	ret = google_dpa_init_sysfs(dpa);
	if (ret) {
		dev_err(dev, "Failed to create sysfs nodes\n");
		goto deinit_crashdump;
	}

	ret = google_dpa_log_proxy_init(dpa);
	if (ret) {
		dev_err(dev, "Failed to create log proxy cdev nodes\n");
		goto deinit_crashdump;
	}

	ret = google_dpa_init_debugfs(dpa);
	if (ret) {
		dev_err(dev, "Failed to create debugfs nodes\n");
		goto deinit_log_proxy;
	}

	ret = google_dpa_ctrl_init(dpa);
	if (ret) {
		dev_err(dev, "Failed to init ctrl.\n");
		goto exit_debugfs;
	}

	ret = google_dpa_netlink_init(dpa);
	if (ret) {
		dev_err(dev, "Failed to initialize netlink.\n");
		goto deinit_dpa_ctrl;
	}

	if (dpa->use_secure_boot) {
		dpa->secure_chan = google_dpa_secure_connect(dev);
		if (IS_ERR(dpa->secure_chan)) {
			ret = PTR_ERR(dpa->secure_chan);
			dpa->secure_chan = NULL;
			dev_err(dev, "Failed to connect to DPA secure app, err:%d.\n", ret);
			goto deinit_netlink;
		}
	}

	if (boot_at_probe) {
		ret = google_dpa_boot(dpa);
		if (ret)
			dev_warn(dev, "Failed to boot DPA: %d by default\n", ret);
	}

	return 0;

deinit_netlink:
	google_dpa_netlink_deinit(dpa);
deinit_dpa_ctrl:
	google_dpa_ctrl_deinit(dpa);
exit_debugfs:
	google_dpa_exit_debugfs(dpa);
deinit_log_proxy:
	google_dpa_log_proxy_deinit(dpa);
deinit_crashdump:
	google_dpa_deinit_crashdump(dpa);
unregister_dpa_reset_notifier:
	gdmc_unregister_dpa_reset_notifier(dpa->gdmc_iface);
remove_genpd_notifier:
	google_dpa_dev_pm_genpd_remove_notifier(dpa);
detach_power_domain:
	google_dpa_detach_power_domain(dpa->pd_vdev, dpa->pd_link);
free_firmware_name:
	kfree_const(dpa->firmware_name);
free_nep_firmware_name:
	kfree_const(dpa->nep.firmware_name);
free_ncp_firmware_name:
	kfree_const(dpa->ncp.firmware_name);
put_gdmc_iface:
	gdmc_iface_put(gdmc_iface);

	return ret;
}

static void google_dpa_driver_remove(struct platform_device *pdev)
{
	struct google_dpa *dpa = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int err;
	int i;

	err = google_dpa_boot_cleanup(dpa);
	if (err)
		dev_err(dev, "Failed to clean up DPA boot resources.\n");

	// No PA indicates dynamic allocation.
	if (!dpa->fw_img_carveout.pa)
		vfree(dpa->fw_img_carveout.va);

	if (dpa->secure_chan)
		google_dpa_secure_shutdown_connection(dpa->secure_chan);
	google_dpa_deinit_crashdump(dpa);
	google_dpa_dev_pm_genpd_remove_notifier(dpa);
	google_dpa_detach_power_domain(dpa->pd_vdev, dpa->pd_link);
	gdmc_unregister_dpa_reset_notifier(dpa->gdmc_iface);
	gdmc_iface_put(dpa->gdmc_iface);
	google_dpa_ctrl_deinit(dpa);
	google_dpa_exit_debugfs(dpa);
	google_dpa_log_proxy_deinit(dpa);
	google_dpa_netlink_deinit(dpa);
	kfree_const(dpa->firmware_name);
	kfree_const(dpa->nep.firmware_name);
	kfree_const(dpa->ncp.firmware_name);

	/*
	 * Deinit doorbell because DPA firmware cannot be booted without DPA
	 * driver and no one can communicate with DPA firmware after DPA driver
	 * is removed.
	 * Calling doorbell_deinit() ensures that DPA power-domain will not be
	 * turned on when doorbell devices are removed.
	 */
	for (i = 0; i < dpa->num_doorbell; ++i)
		google_dpa_doorbell_deinit(dpa->doorbells[i]);
}

static const struct of_device_id google_dpa_of_match_table[] = { { .compatible = "google,dpa" },
								 {} };
MODULE_DEVICE_TABLE(of, google_dpa_of_match_table);

static const struct dev_pm_ops google_dpa_dev_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(google_dpa_suspend, google_dpa_resume)
	RUNTIME_PM_OPS(NULL, NULL, NULL)
};

static struct platform_driver google_dpa_driver = {
	.probe = google_dpa_driver_probe,
	.remove = google_dpa_driver_remove,
	.driver = {
		.name = "google-dpa",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_dpa_of_match_table),
		.pm = &google_dpa_dev_pm_ops,
	},
};

static struct platform_driver *const google_dpa_drivers[] = {
	&google_dpa_doorbell_driver,
	&google_dpa_driver,
};

static int __init google_dpa_init(void)
{
	return platform_register_drivers(google_dpa_drivers, ARRAY_SIZE(google_dpa_drivers));
}
module_init(google_dpa_init);

static void __exit google_dpa_exit(void)
{
	platform_unregister_drivers(google_dpa_drivers, ARRAY_SIZE(google_dpa_drivers));
}
module_exit(google_dpa_exit);

struct google_dpa *google_dpa_get(struct device *dev)
{
	struct device_node *np;
	struct google_dpa *dpa;
	struct device *dpa_dev;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-ENODEV);
	}

	np = of_parse_phandle(dev->of_node, "dpa", 0);
	if (!np) {
		dev_dbg(dev, "failed to parse 'dpa' phandle property\n");
		return ERR_PTR(-ENODEV);
	}

	dpa_dev = driver_find_device_by_of_node(&google_dpa_driver.driver, np);
	of_node_put(np);
	if (!dpa_dev)
		return ERR_PTR(-EPROBE_DEFER);

	dpa = dev_get_drvdata(dpa_dev);
	if (!dpa) {
		put_device(dpa_dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	return dpa;
}
EXPORT_SYMBOL_GPL(google_dpa_get);

void google_dpa_put(struct google_dpa *dpa)
{
	put_device(dpa->dev);
}
EXPORT_SYMBOL_GPL(google_dpa_put);

struct device *google_dpa_get_dpa_dev(struct google_dpa *dpa)
{
	return dpa->dev;
}
EXPORT_SYMBOL_GPL(google_dpa_get_dpa_dev);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google DPA driver");
MODULE_LICENSE("GPL");
