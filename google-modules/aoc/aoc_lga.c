// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google Whitechapel AoC LGA library
 *
 * Copyright (c) 2019-2023 Google LLC
 */

#include "aoc.h"
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>

#include <soc/google/goog-mba-gdmc-iface.h>
#include <aoss-ssr-notifier/aoss_ssr_notifier.h>

#define SSWRP_AOC_POWER_OFF_TIMEOUT 5000
#define AOC_CORE_POWER_OFF_TIMEOUT 1000
#define LCPM_A32_POWER_OFFSET 0x400
#define LCPM_SSWRP_POWER_OFFSET 0x480

#define WAKE_REASON_BUF_SIZE 64

/* Make sure this is always greater than the value in AOC FW */
#define AP_UNLOCK_MSG_ID_TOT 25

DEFINE_MUTEX(aoc_core_pd_cnt_lock);

const char * const control_channels[] = {
	"control",
};
const int control_channels_size = ARRAY_SIZE(control_channels);

/* Copied from GDMC FW */
struct gdmc_mba_aarch32_register_dump {
	uint32_t flags;
	uint32_t gprs[13];
	uint32_t sp;
	uint32_t lr;
	uint32_t pc;
	uint32_t cpsr;
	uint32_t spsr;
	uint32_t dfsr;
	uint32_t dfar;
	uint32_t ifsr;
	uint32_t ifar;
};

extern void google_pinctrl_aoc_ssr_request(bool start);

struct aoc_lga_prvdata {
	struct platform_device *aoc_pdev;
	struct device *aoc_dev;
	struct device *sswrp_aoc_pd;
	struct device *aoc_core_pd;
	struct notifier_block sswrp_aoc_nb;
	struct completion sswrp_aoc_pd_power_off;
	struct notifier_block aoc_core_nb;
	struct completion aoc_core_pd_power_off;
	struct gdmc_iface *gdmc_iface;
	struct aoc_prvdata *aoc_prvdata;
	int aoc_core_pd_cnt;
	int irqs[AP_UNLOCK_MSG_ID_TOT];
	char **wake_reasons;
	int wake_reasons_size;
	char wake_reasons_ap_unlocked[AP_UNLOCK_MSG_ID_TOT][WAKE_REASON_BUF_SIZE];
	int wake_reasons_ap_unlocked_size;
};

struct aoc_lga_prvdata *lga_prvdata;

struct resource *aoc_lcpm_resource;

static inline void *aoc_lcpm_translate(struct aoc_prvdata *p, u32 offset)
{
	if (offset >resource_size(aoc_lcpm_resource))
		return NULL;

	return p->lcpm_virt + offset;
}

bool aoc_release_from_reset(struct aoc_prvdata *prvdata)
{
	u32 a32_power_value = 0x0;
	void __iomem *a32_power = aoc_lcpm_translate(prvdata, LCPM_A32_POWER_OFFSET);
	unsigned long timeout;

	if (!a32_power)
		return false;

	a32_power_value = 0x41;
	iowrite32(a32_power_value, a32_power);

	timeout = jiffies + (2 * HZ);
	while (time_before(jiffies, timeout)) {
		a32_power_value = ioread32(a32_power);
		if (a32_power_value == 0x55)
			break;
		msleep(100);
	}

	return a32_power_value == 0x55;
}

static int sswrp_aoc_pd_power_off(void)
{
	int ret;
	struct device *dev = lga_prvdata->aoc_dev;

	reinit_completion(&lga_prvdata->sswrp_aoc_pd_power_off);

	ret = pm_runtime_put_sync(lga_prvdata->sswrp_aoc_pd);
	if (ret < 0) {
		dev_err(dev, "failed to call pm_runtime_put_sync on %s, ret = %d\n",
			dev_name(lga_prvdata->sswrp_aoc_pd), ret);
		return ret;
	}

	dev_dbg(dev, "waiting for sswrp_aoc_pd to power off...\n");
	ret = wait_for_completion_timeout(&lga_prvdata->sswrp_aoc_pd_power_off,
					msecs_to_jiffies(SSWRP_AOC_POWER_OFF_TIMEOUT));
	if (ret == 0) {
		dev_err(dev, "timed out waiting for sswrp_aoc_pd to power off, panic!\n");
		ret = pm_runtime_get_sync(lga_prvdata->sswrp_aoc_pd);
		if (ret < 0) {
			dev_err(dev, "failed to call pm_runtime_get_sync on %s, ret = %d\n",
				dev_name(lga_prvdata->sswrp_aoc_pd), ret);
		}
		return -ETIMEDOUT;
	}

	dev_dbg(dev, "sswrp_aoc_pd powered off!\n");

	return 0;
}

static int sswrp_aoc_pd_notifier(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	switch (action) {
	case GENPD_NOTIFY_OFF:
		complete(&lga_prvdata->sswrp_aoc_pd_power_off);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int aoc_core_pd_notifier(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	switch (action) {
	case GENPD_NOTIFY_OFF:
		complete(&lga_prvdata->aoc_core_pd_power_off);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static void gdmc_callback(void *reg_dump, unsigned int reg_dump_len, void *priv_data)
{
	struct aoc_prvdata *prvdata = priv_data;
	struct device *dev = prvdata->dev;

	dev_notice(dev, "AoC received interrupt from GDMC\n");
	if (reg_dump) {
		int i;
		struct gdmc_mba_aarch32_register_dump *gdmc_dump;
		gdmc_dump = (struct gdmc_mba_aarch32_register_dump *) reg_dump;
		dev_info(dev, "sp:   %#x lr:   %#x pc:   %#x\n",
			gdmc_dump->sp, gdmc_dump->lr, gdmc_dump->pc);
		dev_info(dev, "cpsr: %#x spsr: %#x dfsr: %#x\n",
			gdmc_dump->cpsr, gdmc_dump->spsr, gdmc_dump->dfsr);
		dev_info(dev, "dfar: %#x ifsr: %#x ifar: %#x\n",
			gdmc_dump->dfar, gdmc_dump->ifsr, gdmc_dump->ifar);
		for (i = 0; i < 12; i++)
			dev_info(dev, "r%d:   %#x\n", i, gdmc_dump->gprs[i]);
	}

	trigger_aoc_ssr(false, "AOC watchdog interrupt from GDMC");
}

static irqreturn_t aoc_cpm_int_handler(int irq, void *dev)
{
	pm_wakeup_ws_event(lga_prvdata->aoc_prvdata->wakelock, 100, true);
	return IRQ_HANDLED;
}

int platform_specific_probe(struct platform_device *pdev, struct aoc_prvdata *prvdata)
{
	int ret, rc = 0;
	struct device *dev = &pdev->dev;
	void __iomem *sswrp_power;

	aoc_lcpm_resource =
		platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcpm");

	if (!aoc_lcpm_resource) {
		dev_err(dev,
			"failed to get memory resources for lcpm\n");
		return -ENOMEM;
	}

	prvdata->lcpm_virt = devm_ioremap_resource(dev, aoc_lcpm_resource);
	if (IS_ERR(prvdata->lcpm_virt))
		return -ENOMEM;

	lga_prvdata = devm_kzalloc(dev, sizeof(*lga_prvdata), GFP_KERNEL);
	if (!lga_prvdata)
		return -ENOMEM;

	lga_prvdata->aoc_pdev = pdev;
	lga_prvdata->aoc_prvdata = prvdata;
	lga_prvdata->aoc_core_pd_cnt = 0;

	lga_prvdata->gdmc_iface = gdmc_iface_get(dev);
	if (IS_ERR(lga_prvdata->gdmc_iface)) {
		dev_err(dev, "failed to get gdmc interface %ld\n",
			PTR_ERR(lga_prvdata->gdmc_iface));
	}

	lga_prvdata->sswrp_aoc_pd = dev_pm_domain_attach_by_name(dev, "sswrp_aoc_pd");
	if (!lga_prvdata->sswrp_aoc_pd) {
		dev_err(dev, "Couldn't attach power domain sswrp_aoc_pd\n");
		return -EINVAL;
	}

	lga_prvdata->aoc_core_pd = dev_pm_domain_attach_by_name(dev, "aoc_core_pd");
	if (!lga_prvdata->aoc_core_pd) {
		dev_err(dev, "Couldn't attach power domain aoc_core_pd\n");
		rc = -EINVAL;
		goto err_attach_aoc_core;
	}

	ret = pm_runtime_get_sync(lga_prvdata->sswrp_aoc_pd);
	if (ret < 0) {
		dev_err(dev, "failed to call pm_runtime_get_sync on %s, ret = %d\n",
			dev_name(lga_prvdata->sswrp_aoc_pd), ret);
		rc = -EINVAL;
		goto err_pm_get;
	}

	lga_prvdata->aoc_dev = dev;
	lga_prvdata->sswrp_aoc_nb.notifier_call = sswrp_aoc_pd_notifier;
	lga_prvdata->aoc_core_nb.notifier_call = aoc_core_pd_notifier;

	init_completion(&lga_prvdata->sswrp_aoc_pd_power_off);

	ret = dev_pm_genpd_add_notifier(lga_prvdata->sswrp_aoc_pd, &lga_prvdata->sswrp_aoc_nb);
	if (ret) {
		dev_err(dev, "failed to add genpd notifier on sswrp_aoc_pd, ret = %d\n", ret);
		rc = -EINVAL;
		goto err_add_genpd_notifier;
	}

	init_completion(&lga_prvdata->aoc_core_pd_power_off);

	ret = dev_pm_genpd_add_notifier(lga_prvdata->aoc_core_pd, &lga_prvdata->aoc_core_nb);
	if (ret) {
		dev_err(dev, "failed to add genpd notifier on aoc_core_pd, ret = %d\n", ret);
		rc = -EINVAL;
		goto err_add_genpd_notifier;
	}

	if (!IS_ERR(lga_prvdata->gdmc_iface))
		gdmc_register_aoc_reset_notifier(lga_prvdata->gdmc_iface, gdmc_callback, prvdata);

	/* Write 0x0 to sswrp_power to deassert fabstby_aoc */
	sswrp_power = aoc_lcpm_translate(prvdata, LCPM_SSWRP_POWER_OFFSET);
	if (sswrp_power)
		iowrite32(0x0, sswrp_power);

	lga_prvdata->wake_reasons = NULL;

	/* Properly initialize count for genpd */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

err_add_genpd_notifier:
	pm_runtime_put_sync(lga_prvdata->sswrp_aoc_pd);
err_pm_get:
	dev_pm_domain_detach(lga_prvdata->aoc_core_pd, false);
err_attach_aoc_core:
	dev_pm_domain_detach(lga_prvdata->sswrp_aoc_pd, false);

	return rc;
}

void request_aoc_on(struct aoc_prvdata *p, bool status)
{
	int ret;
	struct device *dev = lga_prvdata->aoc_dev;

	if (status) {
		ret = pm_runtime_get_sync(lga_prvdata->aoc_core_pd);
		if (ret < 0) {
			dev_err(dev, "failed to call pm_runtime_get_sync on %s, ret = %d\n",
				dev_name(lga_prvdata->aoc_core_pd), ret);
			return;
		}
		mutex_lock(&aoc_core_pd_cnt_lock);
		lga_prvdata->aoc_core_pd_cnt++;
		dev_dbg(dev, "new aoc_core_pd_cnt value: %d\n", lga_prvdata->aoc_core_pd_cnt);
		mutex_unlock(&aoc_core_pd_cnt_lock);
	} else {
		reinit_completion(&lga_prvdata->aoc_core_pd_power_off);

		mutex_lock(&aoc_core_pd_cnt_lock);
		while (lga_prvdata->aoc_core_pd_cnt > 0) {
			ret = pm_runtime_put_sync(lga_prvdata->aoc_core_pd);
			if (ret < 0) {
				dev_err(dev, "failed to call pm_runtime_put_sync on %s, ret = %d\n",
					dev_name(lga_prvdata->aoc_core_pd), ret);
				return;
			}

			lga_prvdata->aoc_core_pd_cnt--;
			dev_dbg(dev, "new aoc_core_pd_cnt value: %d\n",
				lga_prvdata->aoc_core_pd_cnt);
		}
		mutex_unlock(&aoc_core_pd_cnt_lock);

		ret = wait_for_completion_timeout(&lga_prvdata->aoc_core_pd_power_off,
				msecs_to_jiffies(AOC_CORE_POWER_OFF_TIMEOUT));
		if (ret == 0)
			dev_err(dev, "timed out waiting for aoc_core_pd to power off\n");
	}
}

int wait_for_aoc_status(struct aoc_prvdata *p, bool status)
{
	return 0;
}

static void notify_aoc_ssr(bool start)
{
	google_pinctrl_aoc_ssr_request(start);
}

int aoc_watchdog_restart(struct aoc_prvdata *prvdata,
		struct aoc_module_parameters *aoc_module_params)
{
	int ret;
	struct device *dev = prvdata->dev;

	request_aoc_on(prvdata, false);

	dev_dbg(dev, "restarting sswrp_aoc...\n");
	notify_aoc_ssr(true);
	aoss_ssr_notify(AOSS_SSR_AMBSS_DOWN);
	aoss_ssr_notify(AOSS_SSR_PG_DOWN);
	ret = sswrp_aoc_pd_power_off();
	if (ret) {
		notify_aoc_ssr(false);
		dev_err(dev, "failed to restart sswrp_aoc, ret = %d\n", ret);
		return ret;
	}

	ret = pm_runtime_get_sync(lga_prvdata->sswrp_aoc_pd);
	if (ret < 0) {
		dev_err(dev, "failed to call pm_runtime_get_sync on %s, ret = %d\n",
			dev_name(lga_prvdata->sswrp_aoc_pd), ret);
		return ret;
	}

	notify_aoc_ssr(false);
	aoss_ssr_notify(AOSS_SSR_PG_UP);
	aoss_ssr_notify(AOSS_SSR_AMBSS_UP);

	/* Set device to RPM active */
	pm_runtime_get(dev);

	dev_dbg(dev, "restarting sswrp_aoc completed\n");
	return ret;
}

void trigger_aoc_ramdump(struct aoc_prvdata *prvdata)
{
	struct mbox_chan *channel = prvdata->mbox_channels[15].channel;
	static const uint32_t command = 0x0deada0c;

	dev_notice(prvdata->dev, "Attempting to force AoC coredump\n");

	mbox_send_message(channel, (void *)&command);
}
EXPORT_SYMBOL_GPL(trigger_aoc_ramdump);

void aoc_configure_ssmt(struct platform_device *pdev) {}

int configure_iommu_interrupts(struct device *dev, struct device_node *iommu_node,
		struct aoc_prvdata *prvdata)
{
	return 0;
}

int configure_watchdog_interrupt(struct platform_device *pdev, struct aoc_prvdata *prvdata)
{
	return 0;
}

void platform_specific_remove(struct platform_device *pdev, struct aoc_prvdata *prvdata)
{
	gdmc_register_aoc_reset_notifier(lga_prvdata->gdmc_iface, NULL, NULL);
	gdmc_iface_put(lga_prvdata->gdmc_iface);
}

void configure_crash_interrupts(struct aoc_prvdata *prvdata, bool enable)
{
}

u32 aoc_chip_get_revision(void)
{
	u32 product_id;
	u32 major;
	u32 minor;

	if (aoc_read_soc_compatible(lga_prvdata->aoc_dev, &product_id, &major, &minor))
		return 0;

	return ((major & 0xF) << 4) | (minor & 0xF);
}

u32 aoc_chip_get_type(void)
{
	return 0;
}

size_t platform_specific_get_dvfs_freq(char *buff)
{
	return -ENODATA;
}
EXPORT_SYMBOL_GPL(platform_specific_get_dvfs_freq);

u32 aoc_chip_get_product_id(void)
{
	u32 product_id;
	u32 major;
	u32 minor;

	if (aoc_read_soc_compatible(lga_prvdata->aoc_dev, &product_id, &major, &minor))
		return 0;

	return product_id;
}

static int aoc_register_device_interrupt(struct platform_device *pdev,
	struct aoc_prvdata *prvdata, int index)
{
	const char *name;
	aoc_service *s;
	int irq, ret;
	struct device *dev = prvdata->dev;
	struct aoc_service_dev *service_dev;

	s = service_at_index(prvdata, index);
	if (!s)
		return -EIO;

	name = aoc_service_name(s);
	if (!name)
		return -EIO;

	lga_prvdata->wake_reasons[index] = kasprintf(GFP_KERNEL, "aoc_%s", name);
	irq = platform_get_irq(pdev, index);
	if (irq < 0)
		return irq;

	service_dev = service_dev_at_index(prvdata, index);
	service_dev->irq = irq;

	ret = devm_request_irq(&service_dev->dev, irq, aoc_cpm_int_handler,
		0, lga_prvdata->wake_reasons[index], prvdata);
	if (ret < 0) {
		dev_err(dev, "Failed to request irq %d, err: %d\n", irq, ret);
		return ret;
	}

	device_init_wakeup(&service_dev->dev, true);

	return 0;
}

int platform_specific_aoc_online(void)
{
	int i, ret, s, irq;

	s = aoc_num_services();
	lga_prvdata->wake_reasons_size = 0;
	lga_prvdata->wake_reasons = kcalloc(s, sizeof(char *), GFP_KERNEL);
	for (i = 0; i < s; i++) {
		ret = aoc_register_device_interrupt(lga_prvdata->aoc_pdev,
			lga_prvdata->aoc_prvdata, i);
		if (ret < 0) {
			dev_err(lga_prvdata->aoc_dev,
				"failed to register device interrupt at index %d: %d\n", i, ret);
			return ret;
		}
		lga_prvdata->wake_reasons_size++;
	}

	lga_prvdata->wake_reasons_ap_unlocked_size = 0;
	for (i = 0; i < AP_UNLOCK_MSG_ID_TOT; i++) {
		scnprintf(lga_prvdata->wake_reasons_ap_unlocked[i],
			WAKE_REASON_BUF_SIZE, "aoc_ap_unlock_%d", i);
		irq = platform_get_irq(lga_prvdata->aoc_pdev, s + i);
		if (irq < 0)
			return irq;

		lga_prvdata->irqs[i] = irq;
		ret = devm_request_irq(lga_prvdata->aoc_dev, irq, aoc_cpm_int_handler,
			0, lga_prvdata->wake_reasons_ap_unlocked[i], lga_prvdata->aoc_prvdata);
		if (ret < 0) {
			dev_err(lga_prvdata->aoc_dev, "Failed to request irq %d, err: %d\n",
				irq, ret);
			return ret;
		}

		lga_prvdata->wake_reasons_ap_unlocked_size++;
	}

	device_init_wakeup(lga_prvdata->aoc_dev, true);

	aoss_ssr_notify(AOSS_SSR_ONLINE);

	return 0;
}
EXPORT_SYMBOL_GPL(platform_specific_aoc_online);

int platform_specific_aoc_offline(void)
{
	int i;

	for (i = 0; i < lga_prvdata->wake_reasons_ap_unlocked_size; i++)
		devm_free_irq(lga_prvdata->aoc_dev, lga_prvdata->irqs[i], lga_prvdata->aoc_prvdata);
	lga_prvdata->wake_reasons_ap_unlocked_size = 0;

	for (i = 0; i < lga_prvdata->wake_reasons_size; i++) {
		kfree(lga_prvdata->wake_reasons[i]);
		lga_prvdata->wake_reasons[i] = NULL;
	}
	lga_prvdata->wake_reasons_size = 0;
	kfree(lga_prvdata->wake_reasons);
	lga_prvdata->wake_reasons = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(platform_specific_aoc_offline);

void platform_specific_aoc_core_suspend(void)
{
	if (device_may_wakeup(lga_prvdata->aoc_dev)) {
		int i;

		for (i = 0; i < lga_prvdata->wake_reasons_ap_unlocked_size; i++)
			enable_irq_wake(lga_prvdata->irqs[i]);
	}
}
EXPORT_SYMBOL_GPL(platform_specific_aoc_core_suspend);

void platform_specific_aoc_core_resume(void)
{
	if (device_may_wakeup(lga_prvdata->aoc_dev)) {
		int i;

		for (i = 0; i < lga_prvdata->wake_reasons_ap_unlocked_size; i++)
			disable_irq_wake(lga_prvdata->irqs[i]);
	}
}
EXPORT_SYMBOL_GPL(platform_specific_aoc_core_resume);

u64 aoc_get_timer_ticks(void)
{
	return arch_timer_read_counter();
}

void aoc_print_core_boot_breadcrumbs(struct aoc_prvdata *prvdata)
{
}

void aoc_init_core_boot_breadcrumbs(struct aoc_prvdata *prvdata)
{
}
