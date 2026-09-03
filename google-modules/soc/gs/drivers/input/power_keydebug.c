// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 *
 * powerkey debug driver.
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched/debug.h>
#include <linux/sched.h>
#include <linux/of.h>
#include <linux/time.h>
#include <linux/sysrq.h>
#include <linux/reboot.h>
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include <soc/google/google-cdd.h>
#else
#include <soc/google/debug-snapshot.h>
#endif
#include <linux/kernel-top.h>

#define POWER_KEYDEBUG_NAME "power-keydebug"
#define FORCE_POWER_DELAY 18000 /* millisecond */

struct long_press_work {
	struct delayed_work dwork;
	bool is_pressed;
	spinlock_t lock;
	struct kernel_top_context *ktop;
	struct device *dev;
};

static void long_press_work_func(struct work_struct *work)
{
	struct long_press_work *button_work = container_of(work, struct long_press_work,
							   dwork.work);

	pr_debug("Power key %s pressed=%d\n", __func__, button_work->is_pressed);
	if (button_work->is_pressed) {
		pr_info("Power key worker trigger\n");

		if (button_work->ktop) {
			kernel_top_print(button_work->ktop);
			kernel_top_destroy(button_work->ktop);
		}

		pr_info("=======     Show D state tasks++   =======\n");
		handle_sysrq('w');
		pr_info("=======     Show D state tasks--   =======\n");
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		google_cdd_set_powerkey_status(true);
#else
		dbg_snapshot_set_powerkey_status(true);
#endif
		emergency_restart();
	}
}

static struct long_press_work power_button_work = {
	.dwork = __DELAYED_WORK_INITIALIZER(power_button_work.dwork, long_press_work_func, 0),
	.is_pressed = false,
	.lock = __SPIN_LOCK_UNLOCKED(power_button_work.lock),
};

// Called for each event from a connected input_dev
static void key_power_handler_event(struct input_handle *handle, unsigned int type,
				    unsigned int code, int value)
{
	if (type == EV_KEY && code == KEY_POWER) {
		unsigned long flags;

		pr_devel("Power key event via input_handler: type=%u, code=%u, value=%d from %s\n",
			type, code, value, handle->dev->name);
		spin_lock_irqsave(&power_button_work.lock, flags);
		if (value && !power_button_work.is_pressed) {
			power_button_work.is_pressed = true;
			schedule_delayed_work(&power_button_work.dwork,
						msecs_to_jiffies(FORCE_POWER_DELAY));
		} else if (!value && power_button_work.is_pressed) {
			power_button_work.is_pressed = false;
			cancel_delayed_work(&power_button_work.dwork);
		}
		spin_unlock_irqrestore(&power_button_work.lock, flags);
	}
}

// Called when an input_dev matches our ID table
static int key_power_handler_connect(struct input_handler *handler, struct input_dev *dev,
				     const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = input_get_device(dev); // Increment dev refcount
	handle->handler = handler;
	handle->name = "key_pwr_hdlr";

	error = input_register_handle(handle);
	if (error) {
		pr_err("Failed to register input handle: %d\n", error);
		goto err_input_register_handle;
	}

	error = input_open_device(handle);
	if (error) {
		pr_err("Failed to open input handle: %d\n", error);
		goto err_input_open_device;
	}

	return 0;

err_input_open_device:
	input_unregister_handle(handle);
err_input_register_handle:
	input_put_device(dev); // Decrement dev refcount
	kfree(handle);
	return error;
}

// Called when the input_dev is unregistered
static void key_power_handler_disconnect(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	input_close_device(handle);
	input_unregister_handle(handle);
	input_put_device(dev); // Decrement dev refcount
	kfree(handle);
}

// This ID table attches the handler only to devices that report EV_KEY and specifically KEY_POWER
static const struct input_device_id key_power_handler_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER) },
	},
	{} /* Terminating empty entry */
};
MODULE_DEVICE_TABLE(input, key_power_handler_ids);

// The input_handler structure
static struct input_handler key_power_handler = {
	.event = key_power_handler_event,
	.connect = key_power_handler_connect,
	.disconnect = key_power_handler_disconnect,
	.name = "key_power_handler",
	.id_table = key_power_handler_ids,
};

static int power_keydebug_probe(struct platform_device *pdev)
{
	int ret;

	pr_info("Power key debug probe\n");
	power_button_work.dev = &pdev->dev;
	kernel_top_init(power_button_work.dev, &power_button_work.ktop);
	ret = input_register_handler(&key_power_handler);
	if (ret) {
		pr_err("Failed to register input handler: %d\n", ret);
		return ret;
	}

	return ret;
}

static void power_keydebug_remove(struct platform_device *pdev)
{
	input_unregister_handler(&key_power_handler);
	cancel_delayed_work_sync(&power_button_work.dwork);
	if (power_button_work.ktop) {
		kernel_top_destroy(power_button_work.ktop);
		power_button_work.ktop = NULL;
	}
}

#ifdef CONFIG_OF
static const struct of_device_id power_keydebug_match_table[] = {
	{ .compatible = POWER_KEYDEBUG_NAME},
	{},
};
MODULE_DEVICE_TABLE(of, power_keydebug_match_table);
#else
#define power_keydebug_match_table NULL
#endif

struct platform_driver power_keydebug_driver = {
	.probe = power_keydebug_probe,
	.remove = power_keydebug_remove,
	.driver = {
		.name = POWER_KEYDEBUG_NAME,
		.owner = THIS_MODULE,
		.of_match_table = power_keydebug_match_table,
	},
};

static int __init power_keydebug_init(void)
{
	return platform_driver_register(&power_keydebug_driver);
}

static void __exit power_keydebug_exit(void)
{
	return platform_driver_unregister(&power_keydebug_driver);
}

module_init(power_keydebug_init);
module_exit(power_keydebug_exit);
MODULE_DESCRIPTION("Power keydebug Driver");
MODULE_LICENSE("GPL");
