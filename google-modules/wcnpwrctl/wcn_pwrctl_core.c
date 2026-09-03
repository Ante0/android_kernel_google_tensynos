/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WCN Subsystem Power Control Driver - Core Framework
 *
 * This file implements the core logic for the WCN power manager. It handles
 * platform driver registration, device probing, and system-level reboot
 * notifications to ensure safe power-down sequences. It also provides
 * the abstraction layer for PMIC mailbox communication.
 */

#include <wcn_pwrctl/wcn_pwrctl.h>
#include "wcn_pwrctl_priv.h"

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mutex.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/component.h>

/* Same strut mailbox_data is defined in both da9188.h */
#if IS_ENABLED(CONFIG_SOC_MBU)
#include <ap-pmic/da9188.h>
#endif

#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>
#include <soc/google/goog_cpm_service_ids.h>

static DEFINE_MUTEX(wcn_power_mutex);

/* Singleton pointer for external APIs that lack device context */
static struct wcn_pwrctl_data *g_wcn_data = NULL;

enum CPM_IRQ_TYPE {
	CORE_MAIN_PMIC = 15,
	CORE_SUB_PMIC = 14,
	IF_PMIC = 2,
};

/* --- Platform Helpers --- */

int _wcn_cpm_write_reg(struct wcn_pwrctl_data *data, u16 reg, const char *reg_name, u8 val)
{
	struct mailbox_data req_data = {0};
	int ret;

	if (!data || !data->cpm_initialized || !data->ops->mbox_send_req)
		return -ENODEV;

	req_data.data[0] = CORE_MAIN_PMIC;
	req_data.data[1] = val;

	ret = data->ops->mbox_send_req(data->dev, data->cpm_mbox,
					CPM_COMMON_PMIC_SERVICE,
					MB_PMIC_TARGET_REGISTER,
					MB_REG_CMD_SET_PMIC_REG_SINGLE,
					reg,
					req_data);
	if (ret)
		pr_err("PMIC: Failed to write 0x%02x to %s (0x%04x) - err: %d\n",
				val, reg_name, reg, ret);
	else
		pr_info("PMIC: Successfully to write 0x%02x to %s (0x%04x)\n",
				val, reg_name, reg);

	return ret;
}

/* --- Core Driver Logic --- */

static int wcn_pm_reboot_notify(struct notifier_block *nb, unsigned long action, void *v)
{
	struct wcn_pwrctl_data *data = container_of(nb, struct wcn_pwrctl_data, reboot_nb);

	switch (action) {
		case SYS_POWER_OFF:
		case SYS_DOWN:
		case SYS_HALT:
			pr_info("System going shutdown/reboot, triggering WCN power off\n");
			/*
			 * Cancel any pending or running power-on work to prevent it from
			 * racing with the system shutdown/reboot sequence and inadvertently
			 * turning the device back on.
			 */
			cancel_work_sync(&data->power_on_work);
			mutex_lock(&wcn_power_mutex);

			if (data->ops && data->ops->power_off)
				data->ops->power_off(data);

			mutex_unlock(&wcn_power_mutex);
			break;
		default:
			break;
	}
	return NOTIFY_DONE;
}

/* --- Exported APIs --- */

void wcn_set_fmd_state(bool enable)
{
	mutex_lock(&wcn_power_mutex);
	if (g_wcn_data) {
		g_wcn_data->fmd_enabled = enable;
		pr_info("WCN Power Manager: FMD state set to %s\n", enable ? "True" : "False");
	} else {
		pr_err("WCN Power Manager: Cannot set FMD state, driver not probed\n");
	}
	mutex_unlock(&wcn_power_mutex);
}
EXPORT_SYMBOL(wcn_set_fmd_state);

/* --- Probe and Init --- */

static int wcn_pm_component_bind(struct device *dev, struct device *master, void *data)
{
	pr_info("WCN Power Manager component bound to %s\n", dev_name(master));
	return 0;
}

static void wcn_pm_component_unbind(struct device *dev, struct device *master, void *data)
{
	pr_info("WCN Power Manager component unbound from %s\n", dev_name(master));
}

static const struct component_ops wcn_pm_component_ops = {
	.bind   = wcn_pm_component_bind,
	.unbind = wcn_pm_component_unbind,
};

static void wcn_pm_power_on_worker(struct work_struct *work)
{
	struct wcn_pwrctl_data *data = container_of(work, struct wcn_pwrctl_data, power_on_work);
	int ret = 0;

	/* Execute hardware power-on sequence */
	mutex_lock(&wcn_power_mutex);
	if (data->ops->power_on)
		ret = data->ops->power_on(data);
	mutex_unlock(&wcn_power_mutex);

	if (ret) {
		dev_err(data->dev, "Power on sequence failed: %d\n", ret);
		return;
	}

	ret = component_add(data->dev, &wcn_pm_component_ops);
	if (ret)
		dev_err(data->dev, "Failed to add component: %d\n", ret);
}


static int wcn_pm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wcn_pwrctl_data *data;
	int ret = 0;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) return -ENOMEM;
	data->dev = dev;

	/* Retrieve platform-specific operations from Device Tree Match Table */
	data->ops = of_device_get_match_data(dev);
	if (!data->ops || !data->ops->init ||
			!data->ops->mbox_request || !data->ops->mbox_release) {
		pr_err("No platform operations matching found in DT\n");
		return -ENODEV;
	}

	/* Execute platform-specific initialization (e.g., GPIO requests) */
	ret = data->ops->init(data);
	if (ret || !data->cpm_mbox) {
		pr_err("init failed (%d)\n", ret);
		return -ENODEV;
	}

	/* Request CPM Mailbox */
	ret = data->ops->mbox_request(dev, data->cpm_mbox);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			pr_err("Request PMIC mbox client failed (%d)\n", ret);
		return ret;
	}
	data->cpm_initialized = true;

	/* Register reboot notifier for shutdown sequence */
	data->reboot_nb.notifier_call = wcn_pm_reboot_notify;
	ret = register_reboot_notifier(&data->reboot_nb);
	if (ret) {
		pr_err("Failed to register reboot notifier: %d\n", ret);
		goto err_mbox;
	}

	/* Initialize singleton pointer */
	platform_set_drvdata(pdev, data);

	mutex_lock(&wcn_power_mutex);
	g_wcn_data = data;
	mutex_unlock(&wcn_power_mutex);

	INIT_WORK(&data->power_on_work, wcn_pm_power_on_worker);
	schedule_work(&data->power_on_work);

	return 0;
err_mbox:
	data->ops->mbox_release(data->cpm_mbox);
	return ret;
}

static void wcn_pm_remove(struct platform_device *pdev)
{
	struct wcn_pwrctl_data *data = platform_get_drvdata(pdev);

	if (data) {
		/*
		 * Ensure any asynchronous power-on sequence completes or is canceled
		 * before removing the component and releasing devm_* resources.
		 */
		cancel_work_sync(&data->power_on_work);
	}

	component_del(&pdev->dev, &wcn_pm_component_ops);

	/* Clear singleton pointer to prevent wild pointer access */
	mutex_lock(&wcn_power_mutex);
	g_wcn_data = NULL;
	mutex_unlock(&wcn_power_mutex);

	if (data) {
		unregister_reboot_notifier(&data->reboot_nb);

		/* Cleanup pinctrl handles if they weren't managed by devm */
		if (data->pinctrl_host_wifi && !IS_ERR(data->pinctrl_host_wifi))
			pinctrl_put(data->pinctrl_host_wifi);

		if (data->pinctrl_host_pcie && !IS_ERR(data->pinctrl_host_pcie))
			pinctrl_put(data->pinctrl_host_pcie);

		if (data->cpm_initialized && data->ops->mbox_release) {
			data->ops->mbox_release(data->cpm_mbox);
			data->cpm_initialized = false;
		}
	}
}

static const struct of_device_id wcn_pm_of_match[] = {
	{
		.compatible = "vendor,mbu-wcn7760",
		.data = &mbu_wcn7760_pwrctl_ops
	},
	{ }
};
MODULE_DEVICE_TABLE(of, wcn_pm_of_match);

static struct platform_driver wcn_pm_driver = {
	.probe = wcn_pm_probe,
	.remove = wcn_pm_remove,
	.driver = {
		.name = "wcn-power-manager",
		.of_match_table = wcn_pm_of_match,
	},
};

static int __init wcn_pm_init(void)
{
	return platform_driver_register(&wcn_pm_driver);
}
module_init(wcn_pm_init);

MODULE_LICENSE("GPL");
