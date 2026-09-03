// SPDX-License-Identifier: GPL-2.0 only
/*
 * google_bcl_core.c Google bcl core driver
 *
 * Copyright (c) 2022 Google LLC.
 *
 */
#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/atomic.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/debugfs.h>
#include <misc/gvotable.h>
#include "bcl.h"

#include "core_pmic/core_pmic_defs.h"
#include "ifpmic/ifpmic_defs.h"
#include "ifpmic/max77759/max77759_irq.h"
#include "ifpmic/max77779/max77779_irq.h"
#include "soc/soc_defs.h"
#include "soc/userspace/userspace_bcl_qos.h"

#define CREATE_TRACE_POINTS
#include "bcl_trace.h"

static const struct platform_device_id google_id_table[] = {
	{.name = "google_mitigation",},
	{},
};

static struct power_supply *google_get_power_supply(struct bcl_device *bcl_dev)
{
	static struct power_supply *psy[2];
	static struct power_supply *batt_psy;
	int err = 0;

	batt_psy = NULL;
	err = power_supply_get_by_phandle_array(bcl_dev->device->of_node, "google,power-supply",
						psy, ARRAY_SIZE(psy));
	if (err > 0)
		batt_psy = psy[0];
	return batt_psy;
}

static int battery_supply_callback(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct bcl_device *bcl_dev = container_of(nb, struct bcl_device, psy_nb);
	struct power_supply *psy = data;

	if (IS_ERR_OR_NULL(bcl_dev))
		return NOTIFY_OK;

	if (event != PSY_EVENT_PROP_CHANGED || !psy || !psy->desc)
		return NOTIFY_OK;

	if (psy->desc->type == POWER_SUPPLY_TYPE_WIRELESS) {
		union power_supply_propval wlc_online = {};
		int ret;

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE,
						&wlc_online);
		if (ret == 0 && bcl_dev->toggle_wlc) {
			gvotable_cast_vote(
				bcl_dev->toggle_wlc, "BCL_DEV_VOTER", (void *)0,
				(void *)(long)(wlc_online.intval ?
						       WLC_ENABLED_TX :
						       WLC_DISABLED_TX));
		}
	}

	return NOTIFY_OK;
}

struct bcl_device *google_retrieve_bcl_handle(void)
{
	struct device_node *np __free(device_node);
	struct platform_device *pdev;
	struct bcl_device *bcl_dev;

	np = of_find_node_by_name(NULL, "google-mitigation");
	if (!np)
		np = of_find_node_by_name(NULL, "google,mitigation");

	if (!np || !virt_addr_valid(np) || !of_device_is_available(np))
		return NULL;
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return NULL;
	bcl_dev = platform_get_drvdata(pdev);
	if (IS_ERR_OR_NULL(bcl_dev))
		return NULL;

	return bcl_dev;
}
EXPORT_SYMBOL_GPL(google_retrieve_bcl_handle);

int google_init_tpu_ratio(struct bcl_device *data)
{
	if (!IS_ERR_OR_NULL(data))
		return google_init_ratio(data, TPU);
	return 0;
}
EXPORT_SYMBOL_GPL(google_init_tpu_ratio);

int google_init_gpu_ratio(struct bcl_device *data)
{
	if (!IS_ERR_OR_NULL(data))
		return google_init_ratio(data, GPU);
	return 0;
}
EXPORT_SYMBOL_GPL(google_init_gpu_ratio);

int google_init_aur_ratio(struct bcl_device *data)
{
	if (!IS_ERR_OR_NULL(data))
		return google_init_ratio(data, AUR);
	return 0;
}
EXPORT_SYMBOL_GPL(google_init_aur_ratio);

static int google_set_intf_pmic(struct bcl_device *bcl_dev, struct platform_device *pdev)
{
	bcl_dev->batt_psy = google_get_power_supply(bcl_dev);
	return ifpmic_setup(bcl_dev, pdev);
}

/**
 * @brief Enables IRQs for active BCL zones that have dedicated physical GPIOs.
 * @param [in] bcl_dev Pointer to the BCL device structure.
 * @return None.
 */
static void google_bcl_enable_irq(struct bcl_device *bcl_dev)
{
	int i;
	struct bcl_zone *zone;

	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		zone = bcl_dev->zone[i];

		/* Ensure the zone pointer is valid */
		if (!zone)
			continue;

		/* Skip if the zone is disabled */
		if (zone->disabled)
			continue;

		/* Skip if there is no valid physical pin */
		if (IS_ERR_OR_NULL(zone->bcl_pin))
			continue;

		/* Skip IF PMIC IRQs (shared IRQs) */
		if (is_if_pmic_irq(zone->idx))
			continue;

		enable_irq(zone->bcl_irq);
	}
}

static void google_bcl_init_power_supply(struct bcl_device *bcl_dev)
{
	int ret;

	bcl_dev->batt_psy = google_get_power_supply(bcl_dev);
	bcl_dev->batt_psy_initialized = false;
	bcl_dev->psy_nb.notifier_call = battery_supply_callback;
	ret = power_supply_reg_notifier(&bcl_dev->psy_nb);
	if (ret < 0)
		dev_err(bcl_dev->device, "soc notifier registration error. defer. err:%d\n", ret);
	else
		bcl_dev->batt_psy_initialized = true;
	bcl_dev->main_charger_psy = power_supply_get_by_name("main-charger");
}

static bool google_bcl_disable(struct bcl_device *bcl_dev)
{
	struct device_node *np = bcl_dev->device->of_node;

	if (!np)
		return false;

	return of_property_read_bool(np, "disabled");
}

static bool google_bcl_pm_qos_disable(struct bcl_device *bcl_dev)
{
	struct device_node *np = bcl_dev->device->of_node;

	if (!np)
		return false;

	return of_property_read_bool(np, "pm_qos_disabled");
}

static int google_bcl_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct bcl_device *bcl_dev;

	bcl_dev = devm_kzalloc(&pdev->dev, sizeof(*bcl_dev), GFP_KERNEL);
	if (IS_ERR_OR_NULL(bcl_dev))
		return -ENOMEM;

	mutex_init(&bcl_dev->sysreg_lock);
	bcl_dev->device = &pdev->dev;

	if (google_bcl_disable(bcl_dev)) {
		dev_err(bcl_dev->device, "Google BCL is disabled\n");
		return -EINVAL;
	}

	ret = ifpmic_setup_dev(bcl_dev);
	if (ret == -EPROBE_DEFER) {
		/*
		 * There are so many tangles with dependencies that we'll
		 * choose to be loud if we are deferring. Don't use
		 * dev_err_probe() here since that will blow away the more
		 * detailed probe reason that ifpmic_setup_dev() already set.
		 */
		dev_info(bcl_dev->device, "Google BCL deferring\n");
		return ret;
	} else if (ret) {
		/*
		 * All other errors were already printed, so just bail out of
		 * the driver.
		 */
		goto bcl_soc_probe_exit;
	}
	platform_set_drvdata(pdev, bcl_dev);
	google_bcl_init_power_supply(bcl_dev);

	google_bcl_parse_clk_div_dtree(bcl_dev);
	ret = google_bcl_init_instruction(bcl_dev);
	if (ret < 0)
		goto bcl_soc_probe_exit;

	if (google_bcl_setup_mailbox(bcl_dev) < 0)
		goto bcl_soc_probe_exit;

	core_pmic_parse_dtree(bcl_dev);
	ret = core_pmic_main_setup(bcl_dev, pdev);
	if (ret < 0)
		goto bcl_soc_probe_exit;
	ret = core_pmic_sub_setup(bcl_dev);
	if (ret < 0)
		goto bcl_soc_probe_exit;
	google_bcl_configure_modem(bcl_dev);

	if (google_set_intf_pmic(bcl_dev, pdev) < 0)
		goto bcl_soc_probe_exit;
	/* IRQ setup needs to be done after gpiod pins are acquired (b/408030740) */
	google_bcl_setup_irq_mb(bcl_dev);

	if (userspace_bcl_qos_setup(bcl_dev) < 0)
		goto bcl_soc_probe_exit;

	if (google_bcl_pm_qos_disable(bcl_dev)) {
		dev_err(bcl_dev->device, "PM_QOS disabled\n");
	} else if (google_bcl_parse_qos(bcl_dev) != 0) {
		dev_err(bcl_dev->device, "Cannot parse QOS\n");
		goto bcl_soc_probe_exit;
	}

	if (!google_bcl_pm_qos_disable(bcl_dev) &&
		google_bcl_setup_qos(bcl_dev) != 0) {
		dev_err(bcl_dev->device, "Cannot Initiate QOS\n");
		goto bcl_soc_probe_exit;
	}

	google_init_debugfs(bcl_dev);
	ret = google_bcl_init_data_logging(bcl_dev);
	if (ret < 0)
		goto bcl_soc_probe_exit;
	/* br_stats no need to run without mitigation app */
	bcl_dev->enabled_br_stats = false;
	bcl_dev->triggered_idx = TRIGGERED_SOURCE_MAX;
	ret = ifpmic_init_fs(bcl_dev);
	if (ret < 0)
		goto debug_fs_removal;
	ret = google_bcl_init_notifier(bcl_dev);
	if (ret < 0)
		goto debug_init_fs;
	google_bcl_setup_votable(bcl_dev);
	google_bcl_clk_div(bcl_dev);

	/* Enable available irq after QoS workqueue finish initialization */
	google_bcl_enable_irq(bcl_dev);

	core_pmic_get_cpm_cached_sys_evt(bcl_dev);

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG14) || IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || \
	IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	google_init_ratio(bcl_dev, CPU1);
	google_init_ratio(bcl_dev, CPU2);
#endif

	/* Ensure sw mitigation enabled is correctly set */
	smp_store_release(&bcl_dev->sw_mitigation_enabled, true);

	/* Ensure hw mitigation enabled is correctly set */
	smp_store_release(&bcl_dev->hw_mitigation_enabled, true);

	/* Ensure bcl driver is initialized to avoid receiving external calls */
	smp_store_release(&bcl_dev->initialized, true);

	dev_info(bcl_dev->device, "BCL done\n");

	return 0;

debug_init_fs:
	ifpmic_destroy_fs(bcl_dev);
debug_fs_removal:
	debugfs_remove_recursive(bcl_dev->debug_entry);
bcl_soc_probe_exit:
	google_bcl_remove_thermal(bcl_dev);

	/* Only defer if the error is actually deferrable.
	 * Otherwise, fallback to HW mode to prevent boot loops.
	 */
	if (ret == -EPROBE_DEFER)
		return ret;

	dev_err(bcl_dev->device, "BCL SW disabled.  Revert to HW mitigation\n");
	return 0;
}

static void google_bcl_remove(struct platform_device *pdev)
{
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	ifpmic_destroy_fs(bcl_dev);
	debugfs_remove_recursive(bcl_dev->debug_entry);
	cpu_pm_unregister_notifier(&bcl_dev->cpu_nb);
	google_bcl_remove_thermal(bcl_dev);
}

static void google_bcl_shutdown(struct platform_device *pdev)
{
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (bcl_dev)
		power_supply_unreg_notifier(&bcl_dev->psy_nb);
}

static const struct of_device_id match_table[] = {
	{ .compatible = "google,google-bcl"},
	{},
};

static struct platform_driver google_bcl_driver = {
	.probe  = google_bcl_probe,
	.remove = google_bcl_remove,
	.shutdown = google_bcl_shutdown,
	.id_table = google_id_table,
	.driver = {
		.name           = "google_mitigation",
		.owner          = THIS_MODULE,
		.of_match_table = match_table,
	},
};

module_platform_driver(google_bcl_driver);

MODULE_SOFTDEP("pre: i2c-acpm");
MODULE_DESCRIPTION("Google Battery Current Limiter");
MODULE_AUTHOR("George Lee <geolee@google.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(BCL_VERSION);
