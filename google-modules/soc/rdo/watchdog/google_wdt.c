// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/watchdog.h>

#include <soc/google/google_timestamp_sync.h>
#include <soc/google/google_wdt.h>

#include "google_wdt_local.h"

static LIST_HEAD(google_wdt_list);

/**
 * struct wdt_pet_callback_info - Watchdog pet timestamp callback information
 * @lock: Spinlock to protect access to the callback function pointer.
 * @callback: Function pointer to the callback to be invoked when the
 *            watchdog is pet. This callback receives the kernel monotonic
 *            raw time and GTC time.
 *
 * This structure manages the registration of a single callback function
 * that is notified with timestamp data upon a watchdog pet event.
 */
struct wdt_pet_callback_info {
	spinlock_t lock;			/* Protects @callback */
	wdt_pet_timestamp_callback_t callback;	/* The registered callback */
};

static struct wdt_pet_callback_info wdt_pet_time_cb = {
	.lock = __SPIN_LOCK_UNLOCKED(wdt_pet_time_cb.lock),
	.callback = NULL,
};

void google_wdt_register_pet_timestamp_callback(wdt_pet_timestamp_callback_t callback)
{
	unsigned long flags;

	if (!callback) {
		pr_err("%s: Cannot register NULL callback\n", __func__);
		return;
	}

	spin_lock_irqsave(&wdt_pet_time_cb.lock, flags);
	if (wdt_pet_time_cb.callback)
		pr_err("%s: Pet timestamp callback already registered\n", __func__);
	else
		wdt_pet_time_cb.callback = callback;
	spin_unlock_irqrestore(&wdt_pet_time_cb.lock, flags);
}
EXPORT_SYMBOL_GPL(google_wdt_register_pet_timestamp_callback);

void google_wdt_unregister_pet_timestamp_callback(void)
{
	unsigned long flags;

	spin_lock_irqsave(&wdt_pet_time_cb.lock, flags);
	wdt_pet_time_cb.callback = NULL;
	spin_unlock_irqrestore(&wdt_pet_time_cb.lock, flags);
}
EXPORT_SYMBOL_GPL(google_wdt_unregister_pet_timestamp_callback);

struct watchdog_device *google_wdt_wdd_get(struct device *dev)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct google_wdt *wdt;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-ENODEV);
	}

	np = of_parse_phandle(dev->of_node, "wdt-handle", 0);
	if (!np) {
		dev_dbg(dev, "failed to parse 'wdt-handle' phandle property\n");
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_dbg(dev, "failed to find pdev by node\n");
		return ERR_PTR(-ENODEV);
	}

	wdt = platform_get_drvdata(pdev);
	if (!wdt) {
		platform_device_put(pdev);
		dev_dbg(dev, "failed to get wdt by pdev\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	return &wdt->wdd;
}
EXPORT_SYMBOL_GPL(google_wdt_wdd_get);

static inline u32 google_wdt_readl(struct google_wdt *wdt, ptrdiff_t offset)
{
	return readl(wdt->base + offset);
}

static inline void google_wdt_writel(struct google_wdt *wdt, u32 val,
				     ptrdiff_t offset)
{
	writel(val, wdt->base + offset);
}

int google_wdt_validate_id_part_num(struct google_wdt *wdt)
{
	if (wdt->read(wdt, GOOGLE_WDT_ID_PART_NUM) !=
	    GOOGLE_WDT_ID_PART_NUM_VALUE)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(google_wdt_validate_id_part_num);

static int google_wdt_validate_key(struct google_wdt *wdt)
{
	u32 key_enabled;

	/*
	 * If the value wrote to the GOOGLE_WDT_WDT_KEY matches key-unlock
	 * The KEY_ENABLE bit would be set
	 */
	wdt->write(wdt, wdt->key_unlock, GOOGLE_WDT_WDT_KEY);
	key_enabled = wdt->read(wdt, GOOGLE_WDT_WDT_CONTROL) &
		      GOOGLE_WDT_WDT_CONTROL_FIELD_KEY_ENABLE;
	if (!key_enabled)
		return -EINVAL;

	/*
	 * If the value wrote to the GOOGLE_WDT_WDT_KEY matches key-lock
	 * The KEY_ENABLE bit would be reset
	 */

	wdt->write(wdt, wdt->key_lock, GOOGLE_WDT_WDT_KEY);
	key_enabled = wdt->read(wdt, GOOGLE_WDT_WDT_CONTROL) &
		      GOOGLE_WDT_WDT_CONTROL_FIELD_KEY_ENABLE;
	if (key_enabled)
		return -EINVAL;

	return 0;
}

irqreturn_t google_wdt_isr(int irq, void *data)
{
	struct google_wdt *wdt = data;
	u32 interrupt;

	spin_lock(&wdt->lock);

	wdt->write(wdt, wdt->key_unlock, GOOGLE_WDT_WDT_KEY);
	interrupt = wdt->read(wdt, GOOGLE_WDT_WDT_CONTROL);
	interrupt |= GOOGLE_WDT_WDT_CONTROL_FIELD_INT_CLEAR;
	wdt->write(wdt, interrupt, GOOGLE_WDT_WDT_CONTROL);
	wdt->write(wdt, wdt->key_lock, GOOGLE_WDT_WDT_KEY);

	/*
	 * Mark the watchdog as stopped to make it restartable from userspace
	 * for easier testing.
	 */
	clear_bit(WDOG_ACTIVE, &wdt->wdd.status);
	// Clear this bit so that kernel stops pinging (if it is managing).
	clear_bit(WDOG_HW_RUNNING, &wdt->wdd.status);

	spin_unlock(&wdt->lock);
	return IRQ_WAKE_THREAD;
}
EXPORT_SYMBOL_GPL(google_wdt_isr);

static irqreturn_t google_wdt_isr_threaded(int irq, void *data)
{
	struct google_wdt *wdt = data;
	struct device *dev = wdt->dev;

	/*
	 * WDT should never barks to AP in healthy system. This is for testing
	 * and diagnosing.
	 */
	dev_warn(dev, "barks.\n");
	return IRQ_HANDLED;
}

static void google_wdt_set_ping_value(struct google_wdt *wdt)
{
	struct device *dev = wdt->dev;
	unsigned long tclk_rate_khz;

	tclk_rate_khz = wdt->tclk_rate_hz / 1000;
	if (wdt->wdd.timeout > wdt->wdd.max_hw_heartbeat_ms / 1000) {
		dev_warn(dev, "timeout exceeds maximum value.\n");
		wdt->ping_value = wdt->wdd.max_hw_heartbeat_ms * tclk_rate_khz;
	} else {
		wdt->ping_value = wdt->wdd.timeout * wdt->tclk_rate_hz;
	}
}

static int google_wdt_watchdog_start(struct google_wdt *wdt)
{
	unsigned long flags;
	u32 control;

	spin_lock_irqsave(&wdt->lock, flags);

	wdt->write(wdt, wdt->key_unlock, GOOGLE_WDT_WDT_KEY);
	wdt->write(wdt, wdt->ping_value, GOOGLE_WDT_WDT_VALUE);
	control = wdt->read(wdt, GOOGLE_WDT_WDT_CONTROL);
	control &= ~GOOGLE_WDT_WDT_CONTROL_FIELD_EXPIRY_ACTION;
	control |= FIELD_PREP(GOOGLE_WDT_WDT_CONTROL_FIELD_EXPIRY_ACTION,
			      wdt->expiry_action);
	control |= GOOGLE_WDT_WDT_CONTROL_FIELD_ENABLE;
	wdt->write(wdt, control, GOOGLE_WDT_WDT_CONTROL);
	wdt->write(wdt, wdt->key_lock, GOOGLE_WDT_WDT_KEY);

	set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);

	spin_unlock_irqrestore(&wdt->lock, flags);
	return 0;
}

int google_wdt_watchdog_op_start(struct watchdog_device *wdd)
{
	return google_wdt_watchdog_start(watchdog_get_drvdata(wdd));
}
EXPORT_SYMBOL_GPL(google_wdt_watchdog_op_start);

int google_wdt_watchdog_op_stop(struct watchdog_device *wdd)
{
	struct google_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned long flags;
	u32 control;

	spin_lock_irqsave(&wdt->lock, flags);

	wdt->write(wdt, wdt->key_unlock, GOOGLE_WDT_WDT_KEY);
	wdt->write(wdt, wdt->ping_value, GOOGLE_WDT_WDT_VALUE);
	control = wdt->read(wdt, GOOGLE_WDT_WDT_CONTROL);
	control &= (~GOOGLE_WDT_WDT_CONTROL_FIELD_ENABLE);
	wdt->write(wdt, control, GOOGLE_WDT_WDT_CONTROL);
	wdt->write(wdt, wdt->key_lock, GOOGLE_WDT_WDT_KEY);

	spin_unlock_irqrestore(&wdt->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(google_wdt_watchdog_op_stop);

static int google_wdt_watchdog_ping(struct google_wdt *wdt)
{
	unsigned long flags;
	struct timestamp_snapshot_data time_data;
	wdt_pet_timestamp_callback_t current_wdt_pet_time_cb;

	spin_lock_irqsave(&wdt->lock, flags);

	wdt->write(wdt, wdt->key_unlock, GOOGLE_WDT_WDT_KEY);
	wdt->write(wdt, wdt->ping_value, GOOGLE_WDT_WDT_VALUE);
	wdt->write(wdt, wdt->key_lock, GOOGLE_WDT_WDT_KEY);

	spin_unlock_irqrestore(&wdt->lock, flags);

	/* Capture timestamp to report to CDD */
	goog_timestamp_sync_snapshot_update();

	spin_lock_irqsave(&wdt_pet_time_cb.lock, flags);
	current_wdt_pet_time_cb = wdt_pet_time_cb.callback;
	spin_unlock_irqrestore(&wdt_pet_time_cb.lock, flags);

	if (current_wdt_pet_time_cb && goog_timestamp_sync_get_nth_snapshot(0, &time_data) == 0) {
		time_data.gtc_nsec += time_data.gtc_sec * NSEC_PER_SEC;
		current_wdt_pet_time_cb(time_data.mono_raw, time_data.gtc_nsec);
	}

	return 0;
}

static int google_wdt_watchdog_op_ping(struct watchdog_device *wdd)
{
	return google_wdt_watchdog_ping(watchdog_get_drvdata(wdd));
}

static int google_wdt_watchdog_op_set_timeout(struct watchdog_device *wdd,
					      unsigned int t)
{
	struct google_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned long flags;

	spin_lock_irqsave(&wdt->lock, flags);

	wdt->write(wdt, wdt->key_unlock, GOOGLE_WDT_WDT_KEY);
	wdt->wdd.timeout = t;
	google_wdt_set_ping_value(wdt);
	wdt->write(wdt, wdt->ping_value, GOOGLE_WDT_WDT_VALUE);
	wdt->write(wdt, wdt->key_lock, GOOGLE_WDT_WDT_KEY);

	spin_unlock_irqrestore(&wdt->lock, flags);

	return 0;
}

static const struct watchdog_info google_wdt_watchdog_info = {
	.identity = "Google Watchdog",
	.options = WDIOF_MAGICCLOSE | // To support "echo V > /dev/watchdog"
		   WDIOF_SETTIMEOUT,
};

static struct watchdog_ops google_wdt_watchdog_ops = {
	.owner = THIS_MODULE,
	.start = google_wdt_watchdog_op_start,
	.stop = google_wdt_watchdog_op_stop,
	.ping = google_wdt_watchdog_op_ping,
	.set_timeout = google_wdt_watchdog_op_set_timeout,
};

static int google_wdt_platform_suspend(struct google_wdt *wdt)
{
	wdt->control = wdt->read(wdt, GOOGLE_WDT_WDT_CONTROL);
	return 0;
}

static int google_wdt_platform_resume(struct google_wdt *wdt)
{
	unsigned long flags;

	spin_lock_irqsave(&wdt->lock, flags);
	if (wdt->control & GOOGLE_WDT_WDT_CONTROL_FIELD_ENABLE) {
		wdt->write(wdt, wdt->key_unlock, GOOGLE_WDT_WDT_KEY);
		wdt->write(wdt, wdt->ping_value, GOOGLE_WDT_WDT_VALUE);
		wdt->write(wdt, wdt->control, GOOGLE_WDT_WDT_CONTROL);
		wdt->write(wdt, wdt->key_lock, GOOGLE_WDT_WDT_KEY);

		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);
	}
	spin_unlock_irqrestore(&wdt->lock, flags);

	return 0;
}

static int google_wdt_platform_syscore_suspend(void)
{
	struct google_wdt *wdt;

	list_for_each_entry_reverse(wdt, &google_wdt_list, node) {
		if (!wdt->use_syscore_pm)
			continue;

		google_wdt_platform_suspend(wdt);
	}

	return 0;
}

static void google_wdt_platform_syscore_resume(void)
{
	struct google_wdt *wdt;

	list_for_each_entry(wdt, &google_wdt_list, node) {
		if (!wdt->use_syscore_pm)
			continue;

		google_wdt_platform_resume(wdt);
	}
}

static void google_wdt_platform_add_list(struct google_wdt *wdt)
{
	list_add_tail(&wdt->node, &google_wdt_list);
}

static struct syscore_ops google_wdt_platform_syscore_ops = {
	.suspend	= google_wdt_platform_syscore_suspend,
	.resume		= google_wdt_platform_syscore_resume,
};

static int google_wdt_pm_notifier(struct notifier_block *notifier,
				  unsigned long pm_event, void *v)
{
	struct google_wdt *wdt;

	list_for_each_entry_reverse(wdt, &google_wdt_list, node)
		switch (pm_event) {
		case PM_SUSPEND_PREPARE:
			google_wdt_watchdog_ping(wdt);
			break;
		}

	return NOTIFY_OK;
}

static struct notifier_block google_wdt_pm_nb = {
	.notifier_call = google_wdt_pm_notifier,
	.priority = 0,
};

static int google_wdt_init(struct google_wdt *wdt)
{
	struct device *dev = wdt->dev;
	unsigned long tclk_rate_khz;
	int err;

	err = google_wdt_validate_id_part_num(wdt);
	if (err < 0) {
		dev_err(dev, "ID part num is not valid.\n");
		return err;
	}

	err = google_wdt_validate_key(wdt);
	if (err < 0) {
		dev_err(dev, "key for lock/unlock is not valid.\n");
		return err;
	}

	if (wdt->irq > 0) {
		err = devm_request_threaded_irq(dev, wdt->irq, google_wdt_isr,
						google_wdt_isr_threaded, 0,
						dev_name(dev), wdt);
		if (err < 0) {
			dev_err(dev, "failed to request irq %d.\n", wdt->irq);
			return err;
		}
	}

	wdt->tclk = devm_clk_get(dev, NULL);
	if (IS_ERR(wdt->tclk))
		return PTR_ERR(wdt->tclk);
	wdt->tclk_rate_hz = clk_get_rate(wdt->tclk);
	tclk_rate_khz = wdt->tclk_rate_hz / 1000;

	spin_lock_init(&wdt->lock);

	wdt->wdd.parent = dev;
	wdt->wdd.info = &google_wdt_watchdog_info;
	wdt->wdd.ops = &google_wdt_watchdog_ops;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_hw_heartbeat_ms = GOOGLE_WDT_VALUE_MAX / tclk_rate_khz;

	// watchdog_init_timeout() checks min_timeout and max_hw_heartbeat_ms.
	err = watchdog_init_timeout(&wdt->wdd, 0, dev);
	if (err < 0)
		return err;

	google_wdt_set_ping_value(wdt);

	/* Start wdt whether it is already enabled in bootloader or not */
	google_wdt_watchdog_start(wdt);

	watchdog_set_drvdata(&wdt->wdd, wdt);

	google_wdt_platform_add_list(wdt);

	/*
	 * Register the watchdog device without setting `nowayout` to make
	 * kernel take over the pinging when we close the watchdog file in
	 * userspace.
	 */
	return devm_watchdog_register_device(dev, &wdt->wdd);
}

static int google_wdt_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_wdt *wdt;
	int err, irq;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;
	platform_set_drvdata(pdev, wdt);

	wdt->dev = dev;

	wdt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	if (!dev->of_node)
		return -ENODEV;

	err = of_property_read_u8(dev->of_node, "expiry-action",
				  &wdt->expiry_action);
	if (err < 0) {
		dev_err(dev, "expiry action value read error: %d.\n", err);
		return err;
	}

	wdt->use_syscore_pm = of_property_read_bool(dev->of_node, "use-syscore-pm");

	err = of_property_read_u32(dev->of_node, "key-unlock",
				   &wdt->key_unlock);
	if (err < 0) {
		dev_err(dev, "key unlock value read error: %d.\n", err);
		return err;
	}
	err = of_property_read_u32(dev->of_node, "key-lock", &wdt->key_lock);
	if (err < 0) {
		dev_err(dev, "key lock value read error: %d.\n", err);
		return err;
	}

	wdt->read = google_wdt_readl;
	wdt->write = google_wdt_writel;
	/*
	 * The driver supports both irq handling and non-irq. If the WDT triggers
	 * an interrupt to the kernel on your platform, then an interrupt should be
	 * specified.
	 */
	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		wdt->irq = irq;
	} else if (irq != -ENXIO) {
		dev_err(dev, "couldn't retrieve IRQ number (%d)\n", irq);
		return irq;
	}

	return google_wdt_init(wdt);
}

static int google_wdt_platform_dpm_suspend(struct device *dev)
{
	struct google_wdt *wdt = dev_get_drvdata(dev);

	if (wdt->use_syscore_pm)
		return 0;

	return google_wdt_platform_suspend(wdt);
}

static int google_wdt_platform_dpm_resume(struct device *dev)
{
	struct google_wdt *wdt = dev_get_drvdata(dev);

	if (wdt->use_syscore_pm)
		return 0;

	return google_wdt_platform_resume(wdt);
}

static DEFINE_SIMPLE_DEV_PM_OPS(google_wdt_platform_pm_ops, google_wdt_platform_dpm_suspend,
				google_wdt_platform_dpm_resume);

static const struct of_device_id google_wdt_of_match_table[] = {
	{ .compatible = "google,wdt" },
	{},
};
MODULE_DEVICE_TABLE(of, google_wdt_of_match_table);

static struct platform_driver google_wdt_driver = {
	.probe = google_wdt_platform_probe,
	.driver = {
		.name = "google-wdt",
		.owner = THIS_MODULE,
		.pm = pm_sleep_ptr(&google_wdt_platform_pm_ops),
		.of_match_table = of_match_ptr(google_wdt_of_match_table),
	},
};

static int google_wdt_platform_init(void)
{
	register_pm_notifier(&google_wdt_pm_nb);

	register_syscore_ops(&google_wdt_platform_syscore_ops);

	return platform_driver_register(&google_wdt_driver);
}

static void google_wdt_platform_exit(void)
{
	unregister_syscore_ops(&google_wdt_platform_syscore_ops);

	unregister_pm_notifier(&google_wdt_pm_nb);

	platform_driver_unregister(&google_wdt_driver);
}

module_init(google_wdt_platform_init);
module_exit(google_wdt_platform_exit);
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google watchdog timer driver");
MODULE_LICENSE("GPL");
