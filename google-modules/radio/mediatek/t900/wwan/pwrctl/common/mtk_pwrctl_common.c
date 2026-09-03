// SPDX-License-Identifier: GPL-2.0 only.
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/syscore_ops.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include "mtk_pwrctl_common.h"

#ifdef CONFIG_WWAN_GPIO_PWRCTL_UT
#include "ut_pwrctl_common_fake.h"
#endif

#define PWRCTL_DRIVER_NAME		"wwan_gpio_pwrctl"
#define PWRCTL_DEV_NODE_NAME		"wwan_pwrctl"
#define PWRCTL_UEVENT_BUF_MAXLEN	(128)

struct pwrctl_mdev *mdev = NULL;
/* For cover L2/L3 test */
static bool pwrctl_l3_support = 0;
/* For manufactory test */
static __attribute__ ((weakref("mtk_pwrctl_slt_gpio_init"))) \
int mtk_pwrctl_slt_init(struct pwrctl_mdev *mdev);
static __attribute__ ((weakref("mtk_pwrctl_slt_gpio_uninit"))) \
void mtk_pwrctl_slt_uninit(struct pwrctl_mdev *mdev);

void mtk_pwrctl_set_power_state(struct pwrctl_mdev *mdev,enum pwrctl_state to_state)
{
	mdev->pm.state = to_state;
	dev_info(&mdev->pdev->dev, "The power state switch to %d\n", to_state);
}
int mtk_pwrctl_get_power_state(struct pwrctl_mdev *mdev)
{
	return mdev->pm.state;
}
int mtk_pwrctl_uevent_notify_user(const char * euvent_info)
{
	struct device *dev = &mdev->pdev->dev;
	char buf[PWRCTL_UEVENT_BUF_MAXLEN];
	char *envp_ext[2];

	memset(buf, 0, sizeof(buf));
	snprintf(buf, PWRCTL_UEVENT_BUF_MAXLEN, "info=%s", euvent_info);
	envp_ext[0] = buf;
	envp_ext[1] = NULL;
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp_ext);
	dev_info(dev, "Uevent notify: %s\n", envp_ext[0]);
	return 0;
}

int mtk_pwrctl_cb_notify_user(enum pwrctl_evt evt)
{
	struct pwrctl_evt_cb *evt_cb;

	mutex_lock(&mdev->notifier.mlock);
	list_for_each_entry(evt_cb, &mdev->notifier.cb_list, entry) {
		if (evt_cb->cb)
			evt_cb->cb(evt,evt_cb->priv);
	}

	mutex_unlock(&mdev->notifier.mlock);

	return 0;
}

static int mtk_pwrctl_fops_open(struct inode *inode, struct file *filep)
{
	if (atomic_read(&mdev->port.usage_cnt) > 0) {
		dev_err(&mdev->pdev->dev, "Port [%s] is busy\n", PWRCTL_DEV_NODE_NAME);
		return -EBUSY;
	}

	atomic_inc(&mdev->port.usage_cnt);
	dev_info(&mdev->pdev->dev, "Open port [%s] success\n", PWRCTL_DEV_NODE_NAME);
	return 0;
}

static int mtk_pwrctl_fops_close(struct inode *inode, struct file *filep)
{
	atomic_set(&mdev->port.usage_cnt, 0);
	dev_info(&mdev->pdev->dev, "Close port [%s]\n", PWRCTL_DEV_NODE_NAME);
	return 0;
}

static long mtk_pwrctl_fops_ioctl(struct file *filep, unsigned int ioctl_cmd, unsigned long arg)
{
	return mtk_pwrctl_cmd_process(mdev, ioctl_cmd);
}

static const struct file_operations mtk_pwrctl_fops = {
	.owner = THIS_MODULE,
	.open = mtk_pwrctl_fops_open,
	.release = mtk_pwrctl_fops_close,
	.unlocked_ioctl = mtk_pwrctl_fops_ioctl,
};

static struct miscdevice mtk_pwrctl_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = PWRCTL_DEV_NODE_NAME,
	.fops = &mtk_pwrctl_fops,
};

static int mtk_pwrctl_create_node(void)
{
	int ret;
	atomic_set(&mdev->port.usage_cnt, 0);
	ret = misc_register(&mtk_pwrctl_miscdev);
	if ( ret < 0)
	{
		dev_err(&mdev->pdev->dev, "misc register fail: %d\n",ret);
		return ret;
	}
	mdev->port.dev = mtk_pwrctl_miscdev.this_device;
	dev_info(&mdev->pdev->dev, "Create misc device node[%s] done\n",
		 mtk_pwrctl_miscdev.name);
	return 0;
}
static void mtk_pwrctl_delete_node(void)
{
	if(!mdev->port.dev)
		return;
	misc_deregister(&mtk_pwrctl_miscdev);
	mdev->port.dev = NULL;
	pr_info("[%s][%d]: Delete device node[%s] done\n",
		PWRCTL_DEV_NODE_NAME, __LINE__, mtk_pwrctl_miscdev.name);
}
int mtk_pwrctl_event_register_callback( void (*cb)(enum pwrctl_evt , void *),
					void *data)
{
	struct pwrctl_notify *notifier;
	struct pwrctl_evt_cb *evt_cb;

	if (!mdev) {
		pr_err("[%s][%d]: the pwrctl driver is not ready\n",
		       PWRCTL_DEV_NODE_NAME, __LINE__);
		return -EPERM;
	}

	notifier = &mdev->notifier;
	mutex_lock(&notifier->mlock);
	list_for_each_entry(evt_cb, &notifier->cb_list, entry) {
		if(evt_cb->cb == cb) {
			pr_info("[%s][%d]: this callback was registered\n",
			       PWRCTL_DEV_NODE_NAME, __LINE__);
			mutex_unlock(&notifier->mlock);
			return 0;
		}
	}

	evt_cb = kzalloc(sizeof(*evt_cb), GFP_KERNEL);
	if (!evt_cb) {
		pr_err("[%s][%d]: Allocate event callback memory failed for %ps\n",
		       PWRCTL_DEV_NODE_NAME, __LINE__, __builtin_return_address(0));
		mutex_unlock(&notifier->mlock);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&evt_cb->entry);
	evt_cb->cb = cb;
	evt_cb->priv = data;

	list_add_tail(&evt_cb->entry, &notifier->cb_list);
	mutex_unlock(&notifier->mlock);

	pr_info("[%s][%d]: Register event callback for %ps\n",
		PWRCTL_DEV_NODE_NAME, __LINE__, __builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(mtk_pwrctl_event_register_callback);

int mtk_pwrctl_event_unregister_callback(void (*cb)(enum pwrctl_evt , void *))
{
	struct pwrctl_notify *notifier = &mdev->notifier;
	struct pwrctl_evt_cb *evt_cb, *evt_cb_back;
	if (!mdev) {
		pr_err("[%s][%d]: unregister_callback: the gpio driver is not ready\n",
		       PWRCTL_DEV_NODE_NAME, __LINE__);
		return -EPERM;
	}

	mutex_lock(&notifier->mlock);
	list_for_each_entry_safe(evt_cb, evt_cb_back, &notifier->cb_list, entry) {
		if(evt_cb->cb == cb){
			pr_info("[%s][%d]: Delete event callback for %ps\n",
				PWRCTL_DEV_NODE_NAME, __LINE__, __builtin_return_address(0));
			list_del(&evt_cb->entry);
			kfree(evt_cb);
			break;
		}
	}

	mutex_unlock(&notifier->mlock);
	pr_info("[%s][%d]: Unregister event callback for %ps\n",
		PWRCTL_DEV_NODE_NAME, __LINE__, __builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(mtk_pwrctl_event_unregister_callback);

int mtk_pwrctl_set_suspend_skip(bool flag)
{
	if (!mdev) {
		pr_err("[%s][%d]: unregister_callback: the gpio driver is not ready\n",
		       PWRCTL_DEV_NODE_NAME, __LINE__);
		return -EPERM;
	}

	if(flag)
		set_bit(PWRCTL_SKIP_SUSPEND_OFF, &mdev->pm.flags);
	else
		clear_bit(PWRCTL_SKIP_SUSPEND_OFF, &mdev->pm.flags);

	pr_info("Set suspend skip flag = %d by %ps\n", flag, __builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(mtk_pwrctl_set_suspend_skip);

int mtk_pwrctl_gpio_request(struct device *dev, const char *label)
{
	int pin;
	int ret;

	pin = of_get_named_gpio(dev->of_node, label, 0);
	if (pin < 0) {
		dev_err(dev, "The GPIO <%s> is probed fail, %d\n", label, pin);
		return -1;
	}

	ret = devm_gpio_request(dev, pin, label);
	if (ret) {
		dev_err(dev, "Request GPIO <%s> fail, %d\n", label, ret);
		return -1;
	}

	dev_info(dev, "Request GPIO <%s:%d> success\n", label, pin);
	return pin;

}

int mtk_pwrctl_irq_request(struct device *dev, int pin, irq_handler_t handler,
				  unsigned long irqflags, bool irq_wake_flag)
{
	int irq;
	int ret;

	if (pin < 0) {
		dev_err(dev,"Invalid GPIO<%d>\n", pin);
		return IRQ_NOTCONNECTED;;
	}

	irq = gpio_to_irq(pin);
	if (irq < 0) {
		dev_err(dev,"Failed to get irq of GPIO<%d>\n", pin);
		return IRQ_NOTCONNECTED;
	}

	ret = devm_request_irq(dev,irq, handler,irqflags, "pwrctl", NULL);
	if (ret < 0) {
		dev_err(dev,"Register handler of GPIO<%d> fail, %d\n", pin, ret);
		return IRQ_NOTCONNECTED;
	}

	dev_info(dev,"Request irq<%d> and register handler of GPIO<%d> success\n", irq, pin);
	if (irq_wake_flag) {
		ret = enable_irq_wake(irq);
		dev_info(&mdev->pdev->dev, "Enable IRQ wake of GPIO<%d> %s\n", pin, ret? "fail" : "success");
	}

	return irq;
}

int mtk_pwrctl_irq_free(struct device *dev, int irq)
{
	if (irq == IRQ_NOTCONNECTED)
		return -EINVAL;

	disable_irq(irq);
	devm_free_irq(dev, irq, NULL);
	dev_info(dev, "free irq %d done\n", irq);
	return 0;
}

static int mtk_pwrctl_syspm_suspend(void)
{
	if(!test_bit(PWRCTL_SKIP_SUSPEND_OFF, &mdev->pm.flags) && pwrctl_l3_support) {
		mtk_pwrctl_disable_irqs();
		mtk_pwrctl_power_off(mdev, false);
	}

	pr_info("pwrctl: Syscore suspend success!\n");
	return 0;
}

static void mtk_pwrctl_syspm_resume(void)
{
	if(!test_bit(PWRCTL_SKIP_SUSPEND_OFF, &mdev->pm.flags) && pwrctl_l3_support) {
		mtk_pwrctl_power_on(mdev, false);
		mtk_pwrctl_enable_irqs();
	}

	pr_info("pwrctl: Syscore resume success!\n");
}
static struct syscore_ops mtk_pwrctl_pm_sysops = {
	.suspend = mtk_pwrctl_syspm_suspend,
	.resume = mtk_pwrctl_syspm_resume,
};
static int mtk_pwrctl_probe(struct platform_device *pdev)
{
	if (!mdev){
		dev_err(&pdev->dev, "The PWRCTL driver is not initialized\n");
		return -EPERM;
	}

	mdev->pdev = pdev;
	dev_info(&mdev->pdev->dev, "probe in\n");
	mutex_init(&mdev->notifier.mlock);

	mtk_pwrctl_dev_init(mdev);
	if (mtk_pwrctl_slt_init)
		mtk_pwrctl_slt_init(mdev);

	register_syscore_ops(&mtk_pwrctl_pm_sysops);
	INIT_LIST_HEAD(&mdev->notifier.cb_list);
#if !IS_ENABLED(CONFIG_ARCH_GOOGLE)
	mtk_pwrctl_power_on(mdev, true);
#endif
	mtk_pwrctl_create_node();
	dev_info(&pdev->dev, "probe done\n");
	return 0;
}

static void mtk_pwrctl_remove(struct platform_device *pdev)
{
	dev_info(&mdev->pdev->dev, "pwrctl removing\n");

	mtk_pwrctl_delete_node();
	unregister_syscore_ops(&mtk_pwrctl_pm_sysops);
	mtk_pwrctl_dev_uninit(mdev);
	if (mtk_pwrctl_slt_uninit)
		mtk_pwrctl_slt_uninit(mdev);

	dev_info(&pdev->dev, "mtk_pwrctl_remove done\n");
}

#ifdef CONFIG_OF
static const struct of_device_id mtk_pwrctl_of_ids[] = {
	{ .compatible = "mediatek,wwan_pwrctl" },
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	{ .compatible = "pci14c3,0900" },
#endif
	{},
};
MODULE_DEVICE_TABLE(of, mtk_pwrctl_of_ids);
#endif

static struct platform_driver wwan_gpio_pwrctl = {
	.driver = {
		.name = "wwan_pwrctl",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(mtk_pwrctl_of_ids),
	},
	.probe = mtk_pwrctl_probe,
	.remove = mtk_pwrctl_remove,
};

static int __init mtk_pwrctl_init(void)
{
	int ret;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		pr_err("[%s][%d]: Alloc mdev memory fail", PWRCTL_DEV_NODE_NAME, __LINE__);
		return -ENOMEM;
	}
	ret = platform_driver_register(&wwan_gpio_pwrctl);
	if (ret < 0) {
		pr_err("[%s][%d]: Register platform driver fail", PWRCTL_DEV_NODE_NAME, __LINE__);
		kfree(mdev);
		mdev = NULL;
		return -1;
	}
	pr_info("[%s][%d]: Register platform driver done\n", PWRCTL_DEV_NODE_NAME, __LINE__);
	return 0;
}

static void __exit mtk_pwrctl_exit(void)
{
	pr_info("[%s][%d]: exit platform driver\n", PWRCTL_DEV_NODE_NAME, __LINE__);
	platform_driver_unregister(&wwan_gpio_pwrctl);
	kfree(mdev);
	mdev = NULL;
}

module_init(mtk_pwrctl_init);
module_exit(mtk_pwrctl_exit);

module_param(pwrctl_l3_support, bool, 0644);
MODULE_PARM_DESC(pwrctl_l3_support, "This parameter is used to cover L3 power state\n");

MODULE_LICENSE("GPL");
