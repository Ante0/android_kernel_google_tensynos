// SPDX-License-Identifier: GPL-2.0
/*
 * TEE driver for goodix fingerprint sensor
 * Copyright (C) 2016 Goodix
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/refcount.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/cpufreq.h>
#include "gf_spi.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif


#define VER_MAJOR 1
#define VER_MINOR 2
#define PATCH_LEVEL 13

#define IRQ_WAKELOCK_HOLD_TIME_MS 2000 /* in ms */
#define SPI_WAKELOCK_TIME_MS 5000 /* ms*/

#define GF_SPIDEV_NAME "goodix,fingerprint"
/* device name after register in character */
#define GF_DEV_NAME "goodix_fp"
#define GF_INPUT_NAME "goodix_fingerprint"

#define CHRD_DRIVER_NAME "goodix_fp_spi"
#define CLASS_NAME "goodix_fp"

#define N_SPI_MINORS 32 /* ... up to 256 */
static int SPIDEV_MAJOR;

static DECLARE_BITMAP(minors, N_SPI_MINORS);
static LIST_HEAD(device_list);
static DEFINE_MUTEX(gf_spi_lock);
static struct wakeup_source *fp_wakeup_source;
static struct gf_dev gf;

static struct gf_key_map maps[] = {
	{ EV_KEY, GF_KEY_INPUT_HOME },
	{ EV_KEY, GF_KEY_INPUT_MENU },
	{ EV_KEY, GF_KEY_INPUT_BACK },
	{ EV_KEY, GF_KEY_INPUT_POWER },
#if defined(SUPPORT_NAV_EVENT)
	{ EV_KEY, GF_NAV_INPUT_UP },
	{ EV_KEY, GF_NAV_INPUT_DOWN },
	{ EV_KEY, GF_NAV_INPUT_RIGHT },
	{ EV_KEY, GF_NAV_INPUT_LEFT },
	{ EV_KEY, GF_KEY_INPUT_CAMERA },
	{ EV_KEY, GF_NAV_INPUT_CLICK },
	{ EV_KEY, GF_NAV_INPUT_DOUBLE_CLICK },
	{ EV_KEY, GF_NAV_INPUT_LONG_PRESS },
	{ EV_KEY, GF_NAV_INPUT_HEAVY },
#endif
};

/**
 * timed_wakelock_acquire() - Acquire a wakelock and set a timeout.
 * @tw:   Pointer to the timed_wakelock struct.
 * @msec: The timeout for the wakelock in milliseconds.
 *
 * This function acquires the wakelock if it's not already active, increments
 * a reference counter, and sets (or resets) a timer to automatically release
 * the wakelock after the specified timeout. This prevents the wakelock from
 * being held indefinitely.
 */
static void timed_wakelock_acquire(struct timed_wakelock *tw,
	unsigned int msec)
{
	unsigned long flags;

	spin_lock_irqsave(&tw->lock, flags);

	if (!refcount_inc_not_zero(&tw->active_count)) {
		/* Refcount was 0, acquire wakelock */
		refcount_set(&tw->active_count, 1);
		__pm_stay_awake(tw->ws);
	}
	/* Set/reset the timer to the desired expiration. */
	mod_timer(&tw->timer, jiffies + msecs_to_jiffies(msec));
	spin_unlock_irqrestore(&tw->lock, flags);
}
/**
 * timed_wakelock_release() - Release a wakelock.
 * @tw:            Pointer to the timed_wakelock struct.
 * @force_release: If true, the wakelock is released immediately, ignoring the
 *                 reference count.
 *
 * This function decrements the wakelock's reference count. The wakelock is
 * only fully released (and the timeout timer cancelled) if the count reaches
 * zero or if a forced release is requested.
 */
static void timed_wakelock_release(struct timed_wakelock *tw, bool force_release)
{
	unsigned long flags;

	spin_lock_irqsave(&tw->lock, flags);
	if (refcount_read(&tw->active_count) == 0) {
		/* Wakelock already released, do nothing. */
	} else if (refcount_dec_and_test(&tw->active_count) || force_release) {
		del_timer(&tw->timer);
		__pm_relax(tw->ws);
		refcount_set(&tw->active_count, 0);
	}
	spin_unlock_irqrestore(&tw->lock, flags);
}
/**
 * timed_wakelock_timeout() - Timer callback to release a wakelock.
 * @t: Pointer to the timer_list struct.
 *
 * This function is the callback executed when the timer expires. It forces
 * the release of the associated wakelock to ensure it is not held indefinitely.
 */
static void timed_wakelock_timeout(struct timer_list *t)
{
	struct timed_wakelock *tw = from_timer(tw, t, timer);

	pr_debug("%s timer expired\n", tw->ws->name);
	timed_wakelock_release(tw, true);
}
/**
 * init_timed_wakelock() - Initialize a timed wakelock.
 * @tw:   Pointer to the timed_wakelock struct to be initialized.
 * @name: The name for the wakeup source.
 *
 * This function initializes the components of a timed_wakelock struct,
 * including registering a new wakeup source, setting up the timer, and
 * initializing the spinlock and reference counter.
 */
static void init_timed_wakelock(struct timed_wakelock *tw, const char *name)
{
	tw->ws = wakeup_source_register(NULL, name);
	timer_setup(&tw->timer, timed_wakelock_timeout, 0);
	refcount_set(&tw->active_count, 0);
	spin_lock_init(&tw->lock);
}
/**
 * cleanup_timed_wakelock() - Clean up a timed wakelock.
 * @tw: Pointer to the timed_wakelock struct to be cleaned up.
 *
 * This function releases the resources associated with a timed wakelock. It
 * ensures the timer is deleted and the wakeup source is unregistered.
 */
static void cleanup_timed_wakelock(struct timed_wakelock *tw)
{
	if (!tw->ws)
		return;
	del_timer_sync(&tw->timer);
	wakeup_source_unregister(tw->ws);
	tw->ws = NULL;
}

static void gf_enable_irq(struct gf_dev *gf_dev)
{
	if (gf_dev->irq_enabled) {
		pr_warn("IRQ has been enabled.\n");
		return;
	}

	enable_irq(gf_dev->irq);
	if (device_may_wakeup(&gf_dev->spi->dev))
		enable_irq_wake(gf_dev->irq);
	gf_dev->irq_enabled = 1;
}

static void gf_disable_irq(struct gf_dev *gf_dev)
{
	if (!gf_dev->irq_enabled) {
		pr_warn("IRQ has been disabled.\n");
		return;
	}

	if (device_may_wakeup(&gf_dev->spi->dev))
		disable_irq_wake(gf_dev->irq);
	disable_irq(gf_dev->irq);
	gf_dev->irq_enabled = 0;
}

#ifdef AP_CONTROL_CLK
static long spi_clk_max_rate(struct clk *clk, unsigned long rate)
{
	long lowest_available, nearest_low, step_size, cur;
	long step_direction = -1;
	long guess = rate;
	int max_steps = 10;

	cur = clk_round_rate(clk, rate);
	if (cur == rate)
		return rate;

	/* if we got here then: cur > rate */
	lowest_available = clk_round_rate(clk, 0);
	if (lowest_available > rate)
		return -EINVAL;

	step_size = (rate - lowest_available) >> 1;
	nearest_low = lowest_available;

	while (max_steps-- && step_size) {
		guess += step_size * step_direction;
		cur = clk_round_rate(clk, guess);

		if ((cur < rate) && (cur > nearest_low))
			nearest_low = cur;
		/*
		 * if we stepped too far, then start stepping in the other
		 * direction with half the step size
		 */
		if (((cur > rate) && (step_direction > 0)) ||
				((cur < rate) && (step_direction < 0))) {
			step_direction = -step_direction;
			step_size >>= 1;
		}
	}
	return nearest_low;
}

static void spi_clock_set(struct gf_dev *gf_dev, int speed)
{
	long rate;
	int rc;

	rate = spi_clk_max_rate(gf_dev->core_clk, speed);
	if (rate < 0) {
		pr_info("%s: no match found for requested clock frequency:%d",
			__func__, speed);
		return;
	}

	rc = clk_set_rate(gf_dev->core_clk, rate);
}

static int gfspi_ioctl_clk_init(struct gf_dev *data)
{
	pr_debug("%s: enter\n", __func__);

	data->clk_enabled = 0;
	data->core_clk = clk_get(&data->spi->dev, "core_clk");
	if (IS_ERR_OR_NULL(data->core_clk)) {
		pr_err("%s: fail to get core_clk\n", __func__);
		return -EPERM;
	}
	data->iface_clk = clk_get(&data->spi->dev, "iface_clk");
	if (IS_ERR_OR_NULL(data->iface_clk)) {
		pr_err("%s: fail to get iface_clk\n", __func__);
		clk_put(data->core_clk);
		data->core_clk = NULL;
		return -ENOENT;
	}
	return 0;
}

static int gfspi_ioctl_clk_enable(struct gf_dev *data)
{
	int err;

	pr_debug("%s: enter\n", __func__);

	if (data->clk_enabled)
		return 0;

	err = clk_prepare_enable(data->core_clk);
	if (err) {
		pr_err("%s: fail to enable core_clk\n", __func__);
		return -EPERM;
	}

	err = clk_prepare_enable(data->iface_clk);
	if (err) {
		pr_err("%s: fail to enable iface_clk\n", __func__);
		clk_disable_unprepare(data->core_clk);
		return -ENOENT;
	}

	data->clk_enabled = 1;

	return 0;
}

static int gfspi_ioctl_clk_disable(struct gf_dev *data)
{
	pr_debug("%s: enter\n", __func__);

	if (!data->clk_enabled)
		return 0;

	clk_disable_unprepare(data->core_clk);
	clk_disable_unprepare(data->iface_clk);
	data->clk_enabled = 0;

	return 0;
}

static int gfspi_ioctl_clk_uninit(struct gf_dev *data)
{
	pr_debug("%s: enter\n", __func__);

	if (data->clk_enabled)
		gfspi_ioctl_clk_disable(data);

	if (!IS_ERR_OR_NULL(data->core_clk)) {
		clk_put(data->core_clk);
		data->core_clk = NULL;
	}

	if (!IS_ERR_OR_NULL(data->iface_clk)) {
		clk_put(data->iface_clk);
		data->iface_clk = NULL;
	}

	return 0;
}
#endif

static void nav_event_input(struct gf_dev *gf_dev, gf_nav_event_t nav_event)
{
	uint32_t nav_input = 0;

	switch (nav_event) {
	case GF_NAV_FINGER_DOWN:
		pr_debug("%s nav finger down\n", __func__);
		break;

	case GF_NAV_FINGER_UP:
		pr_debug("%s nav finger up\n", __func__);
		break;

	case GF_NAV_DOWN:
		nav_input = GF_NAV_INPUT_DOWN;
		pr_debug("%s nav down\n", __func__);
		break;

	case GF_NAV_UP:
		nav_input = GF_NAV_INPUT_UP;
		pr_debug("%s nav up\n", __func__);
		break;

	case GF_NAV_LEFT:
		nav_input = GF_NAV_INPUT_LEFT;
		pr_debug("%s nav left\n", __func__);
		break;

	case GF_NAV_RIGHT:
		nav_input = GF_NAV_INPUT_RIGHT;
		pr_debug("%s nav right\n", __func__);
		break;

	case GF_NAV_CLICK:
		nav_input = GF_NAV_INPUT_CLICK;
		pr_debug("%s nav click\n", __func__);
		break;

	case GF_NAV_HEAVY:
		nav_input = GF_NAV_INPUT_HEAVY;
		pr_debug("%s nav heavy\n", __func__);
		break;

	case GF_NAV_LONG_PRESS:
		nav_input = GF_NAV_INPUT_LONG_PRESS;
		pr_debug("%s nav long press\n", __func__);
		break;

	case GF_NAV_DOUBLE_CLICK:
		nav_input = GF_NAV_INPUT_DOUBLE_CLICK;
		pr_debug("%s nav double click\n", __func__);
		break;

	default:
		pr_warn("%s unknown nav event: %d\n", __func__, nav_event);
		break;
	}

	if ((nav_event != GF_NAV_FINGER_DOWN) &&
			(nav_event != GF_NAV_FINGER_UP)) {
		input_report_key(gf_dev->input, nav_input, 1);
		input_sync(gf_dev->input);
		input_report_key(gf_dev->input, nav_input, 0);
		input_sync(gf_dev->input);
	}
}

static irqreturn_t gf_irq(int irq, void *handle)
{
#if defined(GF_NETLINK_ENABLE)
	char msg = GF_NET_EVENT_IRQ;

	__pm_wakeup_event(fp_wakeup_source, IRQ_WAKELOCK_HOLD_TIME_MS);
	sendnlmsg(&msg);
#elif defined(GF_FASYNC)
	struct gf_dev *gf_dev = &gf;

	if (gf_dev->async)
		kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
#endif

	return IRQ_HANDLED;
}

static int irq_setup(struct gf_dev *gf_dev)
{
	int status;

	gf_dev->irq = gf_irq_num(gf_dev);
	status = request_threaded_irq(gf_dev->irq, NULL, gf_irq,
							IRQF_TRIGGER_RISING | IRQF_ONESHOT, "gf",
							gf_dev);

	if (status) {
		pr_err("failed to request IRQ:%d\n", gf_dev->irq);
		return status;
	}
	enable_irq_wake(gf_dev->irq);
	gf_dev->irq_enabled = 1;

	return status;
}

static void irq_cleanup(struct gf_dev *gf_dev)
{
	gf_disable_irq(gf_dev);
	free_irq(gf_dev->irq, gf_dev);
}

static void gf_kernel_key_input(struct gf_dev *gf_dev, struct gf_key *gf_key)
{
	uint32_t key_input = 0;

	if (gf_key->key == GF_KEY_HOME) {
		key_input = GF_KEY_INPUT_HOME;
	} else if (gf_key->key == GF_KEY_POWER) {
		key_input = GF_KEY_INPUT_POWER;
	} else if (gf_key->key == GF_KEY_CAMERA) {
		key_input = GF_KEY_INPUT_CAMERA;
	} else {
		/* add special key define */
		key_input = gf_key->key;
	}
	pr_info("%s: received key event[%d], key=%d, value=%d\n", __func__,
		key_input, gf_key->key, gf_key->value);

	if ((GF_KEY_POWER == gf_key->key || GF_KEY_CAMERA == gf_key->key) &&
			(gf_key->value == 1)) {
		input_report_key(gf_dev->input, key_input, 1);
		input_sync(gf_dev->input);
		input_report_key(gf_dev->input, key_input, 0);
		input_sync(gf_dev->input);
	}
}

static long gf_ioctl_handler(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gf_dev *gf_dev = &gf;
	struct gf_key gf_key;
#if defined(SUPPORT_NAV_EVENT)
	gf_nav_event_t nav_event = GF_NAV_NONE;
#endif
	int retval = 0;
	int rpm_res;
	u8 netlink_route = NETLINK_TEST;
	struct gf_ioc_chip_info info;

	if (_IOC_TYPE(cmd) != GF_IOC_MAGIC)
		return -ENODEV;

	if (_IOC_DIR(cmd) & _IOC_READ)
		retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		retval = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (retval)
		return -EFAULT;

	if (gf_dev->device_available == 0) {
		if ((cmd == GF_IOC_ENABLE_POWER) || (cmd == GF_IOC_DISABLE_POWER)) {
			pr_info("power cmd\n");
		} else {
			pr_info("Sensor is power off currently.\n");
			return -ENODEV;
		}
	}

	switch (cmd) {
		case GF_IOC_INIT:
			pr_debug("%s GF_IOC_INIT\n", __func__);
			if (copy_to_user((void __user *)arg, (void *)&netlink_route,
							 sizeof(u8))) {
				pr_err("GF_IOC_INIT failed\n");
				retval = -EFAULT;
				break;
			}
			break;

		case GF_IOC_EXIT:
			pr_debug("%s GF_IOC_EXIT\n", __func__);
			break;

		case GF_IOC_DISABLE_IRQ:
			pr_debug("%s GF_IOC_DISABLE_IRQ\n", __func__);
			gf_disable_irq(gf_dev);
			break;

		case GF_IOC_ENABLE_IRQ:
			pr_debug("%s GF_IOC_ENABLE_IRQ\n", __func__);
			gf_enable_irq(gf_dev);
			break;

		case GF_IOC_RESET:
			pr_debug("%s GF_IOC_RESET\n", __func__);
			gf_hw_reset(gf_dev, 10);
			break;

		case GF_IOC_INPUT_KEY_EVENT:
			if (copy_from_user(&gf_key, (void __user *)arg,
								 sizeof(struct gf_key))) {
				pr_err("failed to copy input key event from user to kernel\n");
				retval = -EFAULT;
				break;
			}

			gf_kernel_key_input(gf_dev, &gf_key);
			break;

#if defined(SUPPORT_NAV_EVENT)
		case GF_IOC_NAV_EVENT:
			pr_debug("%s GF_IOC_NAV_EVENT\n", __func__);
			if (copy_from_user(&nav_event, (void __user *)arg,
								 sizeof(gf_nav_event_t))) {
				pr_err("failed to copy nav event from user to kernel\n");
				retval = -EFAULT;
				break;
			}

			nav_event_input(gf_dev, nav_event);
			break;
#endif

		case GF_IOC_ENABLE_SPI_CLK:
			pr_debug("%s GF_IOC_ENABLE_SPI_CLK\n", __func__);
#ifdef AP_CONTROL_CLK
			gfspi_ioctl_clk_enable(gf_dev);
#else
			pr_debug("doesn't support control clock!\n");
#endif
			timed_wakelock_acquire(&gf_dev->ioctl_wakelock, SPI_WAKELOCK_TIME_MS);

			if (IS_ENABLED(USE_PLATFORM_BUS)) {
				rpm_res = pm_runtime_resume_and_get(&gf_dev->spi_pinctrl_pdev->dev);
				if (rpm_res < 0)
					pr_info("pm_runtime_resume_and_get failed: %d\n", rpm_res);
			}

			break;
		case GF_IOC_DISABLE_SPI_CLK:
			pr_debug("%s GF_IOC_DISABLE_SPI_CLK\n", __func__);
#ifdef AP_CONTROL_CLK
			gfspi_ioctl_clk_disable(gf_dev);
#else
			pr_debug("doesn't support control clock!\n");
#endif
			if (IS_ENABLED(USE_PLATFORM_BUS))
				pm_runtime_put(&gf_dev->spi_pinctrl_pdev->dev);

			timed_wakelock_release(&gf_dev->ioctl_wakelock, false);
			break;

		case GF_IOC_ENABLE_POWER:
			pr_debug("%s GF_IOC_ENABLE_POWER\n", __func__);
			if (gf_dev->device_available == 1)
				pr_info("Sensor has already powered-on.\n");
			else
				gf_power_on(gf_dev);
			gf_dev->device_available = 1;
			break;

		case GF_IOC_DISABLE_POWER:
			pr_debug("%s GF_IOC_DISABLE_POWER\n", __func__);
			if (gf_dev->device_available == 0)
				pr_info("Sensor has already powered-off.\n");
			else
				gf_power_off(gf_dev);
			gf_dev->device_available = 0;
			break;

		case GF_IOC_ENTER_SLEEP_MODE:
			pr_debug("%s GF_IOC_ENTER_SLEEP_MODE\n", __func__);
			break;

		case GF_IOC_GET_FW_INFO:
			pr_debug("%s GF_IOC_GET_FW_INFO\n", __func__);
			break;

		case GF_IOC_REMOVE:
			pr_debug("%s GF_IOC_REMOVE\n", __func__);
			break;

		case GF_IOC_CHIP_INFO:
			pr_debug("%s GF_IOC_CHIP_INFO\n", __func__);
			if (copy_from_user(&info, (void __user *)arg,
								 sizeof(struct gf_ioc_chip_info))) {
				retval = -EFAULT;
				break;
			}
			pr_info("vendor_id : 0x%x\n", info.vendor_id);
			pr_info("mode : 0x%x\n", info.mode);
			pr_info("operation: 0x%x\n", info.operation);
			break;

		default:
			pr_warn("unsupport cmd:0x%x\n", cmd);
			break;
	}

	return retval;
}

static long gf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	mutex_lock(&gf_spi_lock);
	retval = gf_ioctl_handler(filp, cmd, arg);
	mutex_unlock(&gf_spi_lock);

	return retval;
}

#ifdef CONFIG_COMPAT
static long gf_compat_ioctl(struct file *filp, unsigned int cmd,
					unsigned long arg)
{
	return gf_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif /*CONFIG_COMPAT*/

static int gf_open(struct inode *inode, struct file *filp)
{
	struct gf_dev *gf_dev = NULL;
	struct list_head *pos;
	int status = -ENXIO;

	mutex_lock(&gf_spi_lock);

	list_for_each(pos, &device_list) {
		struct gf_dev *iterator = list_entry(pos, struct gf_dev, device_entry);

		if (iterator->devt == inode->i_rdev) {
			gf_dev = iterator;
			status = 0;
			break;
		}
	}

	if (status) {
		pr_info("No device for minor %d\n", iminor(inode));
		goto err_setup;
	}

	if (!gf_dev->users) {
		gf_power_on(gf_dev);
		status = irq_setup(gf_dev);
		if (status)
			goto err_setup;
	}
	gf_dev->users++;

	filp->private_data = gf_dev;
	nonseekable_open(inode, filp);
	pr_info("Succeed to open device. irq = %d\n",
		gf_dev->irq);
	//gf_hw_reset(gf_dev, 10);
	gf_dev->device_available = 1;

err_setup:
	mutex_unlock(&gf_spi_lock);
	return status;
}

#ifdef GF_FASYNC
static int gf_fasync(int fd, struct file *filp, int mode)
{
	struct gf_dev *gf_dev = filp->private_data;
	int ret;

	ret = fasync_helper(fd, filp, mode, &gf_dev->async);
	return ret;
}
#endif

static int gf_release(struct inode *inode, struct file *filp)
{
	struct gf_dev *gf_dev = &gf;
	int status = 0;

	mutex_lock(&gf_spi_lock);
	gf_dev = filp->private_data;
	filp->private_data = NULL;

	/* last close?? */
	gf_dev->users--;
	if (!gf_dev->users) {
		irq_cleanup(gf_dev);
	}

	if (gf_dev->device_available) {
		gf_power_off(gf_dev);
		gf_dev->device_available = 0;
	}

	mutex_unlock(&gf_spi_lock);
	return status;
}

static const struct file_operations gf_fops = {
	.owner = THIS_MODULE,
	/* REVISIT switch to aio primitives, so that userspace
	 * gets more complete API coverage.  It'll simplify things
	 * too, except for the locking.
	 */
	.unlocked_ioctl = gf_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gf_compat_ioctl,
#endif /*CONFIG_COMPAT*/
	.open = gf_open,
	.release = gf_release,
#ifdef GF_FASYNC
	.fasync = gf_fasync,
#endif
};

static int goodix_fb_state_chg_callback(struct notifier_block *nb,
										unsigned long val, void *data) {
	struct gf_dev *gf_dev;
	struct fb_event *evdata = data;
	unsigned int blank;
	char msg = 0;

	pr_info("[info] %s go to the goodix_fb_state_chg_callback value = %d\n",
			__func__, (int)val);
	gf_dev = container_of(nb, struct gf_dev, notifier);
	if (evdata && evdata->data && val == FB_EVENT_BLANK && gf_dev) {
		blank = *(int *)(evdata->data);
		switch (blank) {
			case FB_BLANK_POWERDOWN:
				if (gf_dev->device_available == 1) {
					gf_dev->fb_black = 1;
#if defined(GF_NETLINK_ENABLE)
					msg = GF_NET_EVENT_FB_BLACK;
					sendnlmsg(&msg);
#elif defined(GF_FASYNC)
					if (gf_dev->async)
						kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
#endif
				}
				break;
			case FB_BLANK_UNBLANK:
				if (gf_dev->device_available == 1) {
					gf_dev->fb_black = 0;
#if defined(GF_NETLINK_ENABLE)
					msg = GF_NET_EVENT_FB_UNBLACK;
					sendnlmsg(&msg);
#elif defined(GF_FASYNC)
					if (gf_dev->async)
						kill_fasync(&gf_dev->async, SIGIO, POLL_IN);
#endif
				}
				break;
			default:
				pr_info("%s default\n", __func__);
				break;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block goodix_noti_block = {
	.notifier_call = goodix_fb_state_chg_callback,
};

static struct class *gf_class;
#if defined(USE_SPI_BUS)
static int gf_probe(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static int gf_probe(struct platform_device *pdev)
#endif
{
	struct gf_dev *gf_dev = &gf;
	int status = -EINVAL;
	unsigned long minor;
	int i;
	struct device *dev;

	/* Initialize the driver data */
	INIT_LIST_HEAD(&gf_dev->device_entry);
#if defined(USE_SPI_BUS)
	gf_dev->spi = spi;
#elif defined(USE_PLATFORM_BUS)
	gf_dev->spi = pdev;
#endif
	gf_dev->irq_gpio = -EINVAL;
	gf_dev->reset_gpio = -EINVAL;
	gf_dev->pwr_gpio = -EINVAL;
	gf_dev->device_available = 0;
	gf_dev->fb_black = 0;

	status = gf_parse_dts(gf_dev);
	if (status)
		return status;

	gf_dev->input = input_allocate_device();
	if (gf_dev->input == NULL) {
		dev_err(&gf_dev->spi->dev, "Failed to allocate input device\n");
		status = -ENOMEM;
		goto error_dev;
	}
	for (i = 0; i < ARRAY_SIZE(maps); i++)
		input_set_capability(gf_dev->input, maps[i].type, maps[i].code);

	gf_dev->input->name = GF_INPUT_NAME;
	status = input_register_device(gf_dev->input);
	if (status) {
		dev_err(&gf_dev->spi->dev, "Failed to register input device\n");
		goto error_input;
	}
	init_timed_wakelock(&gf_dev->ioctl_wakelock, "gf_spi_wakelock");

#ifdef AP_CONTROL_CLK
	pr_debug("Get the clk resource.\n");
	/* Enable spi clock */
	if (gfspi_ioctl_clk_init(gf_dev))
		goto gfspi_probe_clk_init_failed;

	if (gfspi_ioctl_clk_enable(gf_dev))
		goto gfspi_probe_clk_enable_failed;

	spi_clock_set(gf_dev, 1000000);
#endif

	fp_wakeup_source = wakeup_source_register(NULL, "fp_wakeup");
	if (!fp_wakeup_source) {
		status = -EINVAL;
		dev_err(&gf_dev->spi->dev, "Failed to register wakeup source!\n");
		goto error_fp_ws;
	}

	status = device_init_wakeup(&gf_dev->spi->dev, true);
	if (status) {
		dev_err(&gf_dev->spi->dev, "Failed to init wakeup source!\n");
		goto error_ws;
	}

	gf_dev->notifier = goodix_noti_block;
	status = fb_register_client(&gf_dev->notifier);
	if (status) {
		dev_err(&gf_dev->spi->dev, "Failed to register fb client!\n");
		goto error_fb;
	}

	/*
	 * If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&gf_spi_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor >= N_SPI_MINORS) {
		dev_err(&gf_dev->spi->dev, "no minor number available!\n");
		status = -ENODEV;
		mutex_unlock(&gf_spi_lock);
		goto error_hw;
	}

	gf_dev->devt = MKDEV(SPIDEV_MAJOR, minor);
	dev = device_create(gf_class, &gf_dev->spi->dev, gf_dev->devt,
					gf_dev, GF_DEV_NAME);
	status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	if (status) {
		dev_err(&gf_dev->spi->dev, "Failed to create device!\n");
		gf_dev->devt = 0;
		mutex_unlock(&gf_spi_lock);
		goto error_hw;
	}

	set_bit(minor, minors);
	list_add(&gf_dev->device_entry, &device_list);
	mutex_unlock(&gf_spi_lock);

	dev_info(&gf_dev->spi->dev, "version V%d.%d.%02d\n", VER_MAJOR, VER_MINOR,
			 PATCH_LEVEL);

	return status;

	/*
	 * Going to an error label below undoes every
	 * initialization done before the failure.
	 */
error_hw:
	fb_unregister_client(&gf_dev->notifier);
error_fb:
	device_init_wakeup(&gf_dev->spi->dev, false);
error_ws:
	wakeup_source_unregister(fp_wakeup_source);
error_fp_ws:
	cleanup_timed_wakelock(&gf_dev->ioctl_wakelock);
#ifdef AP_CONTROL_CLK
	gfspi_ioctl_clk_disable(gf_dev);
gfspi_probe_clk_enable_failed:
	gfspi_ioctl_clk_uninit(gf_dev);
gfspi_probe_clk_init_failed:
#endif
	input_unregister_device(gf_dev->input);
error_input:
	if (gf_dev->input != NULL)
		input_free_device(gf_dev->input);
error_dev:
	gf_dev->device_available = 0;

	return status;
}

#if defined(USE_SPI_BUS)
static int gf_remove(struct spi_device *spi)
#elif defined(USE_PLATFORM_BUS)
static void gf_remove(struct platform_device *pdev)
#endif
{
	struct gf_dev *gf_dev = &gf;

	fb_unregister_client(&gf_dev->notifier);
	wakeup_source_unregister(fp_wakeup_source);
	device_init_wakeup(&gf_dev->spi->dev, false);
	cleanup_timed_wakelock(&gf_dev->ioctl_wakelock);
	if (gf_dev->input)
		input_unregister_device(gf_dev->input);
	input_free_device(gf_dev->input);

	/* prevent new opens */
	mutex_lock(&gf_spi_lock);
	list_del(&gf_dev->device_entry);
	device_destroy(gf_class, gf_dev->devt);
	clear_bit(MINOR(gf_dev->devt), minors);
	mutex_unlock(&gf_spi_lock);

	if (IS_ENABLED(USE_PLATFORM_BUS))
		put_device(&gf_dev->spi_pinctrl_pdev->dev);

#if defined(USE_SPI_BUS)
	return 0;
#endif
}

static const struct of_device_id gx_match_table[] = {
	{ .compatible = GF_SPIDEV_NAME },
	{},
};

#if defined(USE_SPI_BUS)
static struct spi_driver gf_driver = {
#elif defined(USE_PLATFORM_BUS)
static struct platform_driver gf_driver = {
#endif
	.driver = {
		.name = GF_DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = gx_match_table,
	},
	.probe = gf_probe,
	.remove = gf_remove,
};

static int __init gf_init(void)
{
	int status;

	/*
	 * Claim our N_SPI_MINORS reserved device numbers. Then register a class
	 * that will key udev/mdev to add/remove /dev nodes. Last, register
	 * the driver which manages those device numbers.
	 */

	BUILD_BUG_ON(N_SPI_MINORS > 256);
	status = register_chrdev(SPIDEV_MAJOR, CHRD_DRIVER_NAME, &gf_fops);
	if (status < 0) {
		pr_warn("Failed to register char device!\n");
		return status;
	}
	SPIDEV_MAJOR = status;
	gf_class = class_create(CLASS_NAME);
	if (IS_ERR(gf_class)) {
		unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
		pr_warn("Failed to create class.\n");
		return PTR_ERR(gf_class);
	}
#if defined(USE_PLATFORM_BUS)
	status = platform_driver_register(&gf_driver);
#elif defined(USE_SPI_BUS)
	status = spi_register_driver(&gf_driver);
#endif
	if (status < 0) {
		class_destroy(gf_class);
		unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
		pr_warn("Failed to register SPI driver.\n");
	}

#ifdef GF_NETLINK_ENABLE
	netlink_init();
#endif
	pr_info("status = 0x%x\n", status);
	return status;
}
module_init(gf_init);

static void __exit gf_exit(void)
{
#ifdef GF_NETLINK_ENABLE
	netlink_exit();
#endif
#if defined(USE_PLATFORM_BUS)
	platform_driver_unregister(&gf_driver);
#elif defined(USE_SPI_BUS)
	spi_unregister_driver(&gf_driver);
#endif
	class_destroy(gf_class);
	unregister_chrdev(SPIDEV_MAJOR, gf_driver.driver.name);
}
module_exit(gf_exit);

MODULE_AUTHOR("Jiangtao Yi, <yijiangtao@goodix.com>");
MODULE_AUTHOR("Jandy Gou, <gouqingsong@goodix.com>");
MODULE_DESCRIPTION("goodix fingerprint sensor device driver");
MODULE_LICENSE("GPL");
