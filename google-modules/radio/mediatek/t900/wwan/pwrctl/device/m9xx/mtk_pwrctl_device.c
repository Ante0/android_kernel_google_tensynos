// SPDX-License-Identifier: GPL-2.0 only.
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/sched.h>
#include <linux/syscore_ops.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include "mtk_pwrctl.h"
#include "mtk_pwrctl_common.h"
#include "mtk_pwrctl_interface.h"
#ifdef CONFIG_WWAN_GPIO_PWRCTL_UT
#include "ut_pwrctl_m9xx_fake.h"
#endif
#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
#include "metrics_collection.h"
#endif /* CONFIG_METRICS_COLLECTION_FRAMEWORK */
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "common/pwrctrl_google.h"
#endif


/* Uevent to user */
#define PWRCTL_UEVENT_POWER_ON		"power_on"
#define PWRCTL_UEVENT_POWER_OFF		"power_off"
#define PWRCTL_UEVENT_RESET		"reset"
/* GPIO pin name */
#define PWRCTL_GPIO_PMIC_EN		"pmic-en"
#define PWRCTL_GPIO_RESET		"pmic-reset"
#define PWRCTL_GPIO_REBOOT_INT		"reboot-int-b"
#define PWRCTL_GPIO_MODOME_DUMP		"mddump"
#define PWRCTL_GPIO_PMIC_FAULTB		"pmic-faultb"
#define PWRCTL_GPIO_AEE_REBOOT_H	"aee-reboot-h"

#define PWRCTL_WAIT_AEE_DUMP_TIME	(1300)

static __attribute__ ((weakref("mtk_gpio_sap_ctrl_set_off"))) \
		       int mtk_gpio_set_mode_off(void);
static __attribute__ ((weakref("mtk_gpio_sap_ctrl_set_working"))) \
		       int mtk_gpio_set_mode_working(void);

#define GPIO_SET_MODE_OFF() do { if (mtk_gpio_set_mode_off) mtk_gpio_set_mode_off(); } while(0)
#define GPIO_SET_MODE_WORKING() \
	do { if (mtk_gpio_set_mode_working)  mtk_gpio_set_mode_working(); } while(0)

#define PWRCTL_GPIOD_GET(dev, gpio_con, flags) ({ \
	struct gpio_desc *_gpio; \
	_gpio = devm_gpiod_get(dev, gpio_con, flags);\
	if (IS_ERR(_gpio)) { \
		dev_err(dev, "Failed to get GPIOD <%s>\n", gpio_con); \
		ret = PTR_ERR(_gpio); \
		goto gpio_init_err; \
	} else { \
		dev_info(dev, "Get GPIOD <%s:%d> successfully\n", gpio_con, desc_to_gpio(_gpio)); \
	} \
	_gpio; \
})

static struct pwrctl_dev_mngr * dev_mngr = NULL;

static void mtk_pwrctl_clear_irq(int irq)
{
	struct irq_desc *desc;
	struct irq_chip *chip;

	desc = irq_to_desc(irq);
	if (!desc)
		return;

	chip = irq_desc_get_chip(desc);
	if (!chip)
		return;

	if (chip->irq_ack) {
		chip->irq_ack(&desc->irq_data);
		pr_info("pwrctl: Clear irq<%d> done\n", irq);
	}
}

void mtk_pwrctl_enable_irqs(void)
{
	mtk_pwrctl_clear_irq(dev_mngr->irq.reboot_int_b);
	enable_irq_wake(dev_mngr->irq.reboot_int_b);
	enable_irq(dev_mngr->irq.reboot_int_b);
}

void mtk_pwrctl_disable_irqs(void)
{
	disable_irq(dev_mngr->irq.reboot_int_b);
	disable_irq_wake(dev_mngr->irq.reboot_int_b);
}

static void mtk_pwrctl_event_notify(enum pwrctl_evt evt, const char *uevent)
{
	mtk_pwrctl_uevent_notify_user(uevent);

	mtk_pwrctl_cb_notify_user(evt);
}

int mtk_pwrctl_power_on(struct pwrctl_mdev *mdev, int nt_rc)
{
	if (!dev_mngr) {
		dev_err(&mdev->pdev->dev, "[Power on] GPIO is not initialized\n");
		return -EINVAL;
	}

	if (!mutex_trylock(&dev_mngr->op_lock)) {
		dev_info(&mdev->pdev->dev, "lock failed, ignor the power on operation\n");
		return -EBUSY;
	}

	dev_info(&mdev->pdev->dev, "Powere on device\n");
	gpiod_set_value(dev_mngr->gpio.pmic_reset, 1);
	wmb();
	gpiod_set_value(dev_mngr->gpio.pmic_en, 1);
#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	mcf_notify_modem_boot_start(MODEM_BOOT_TYPE_MASK_NORMAL,
				    MODEM_BOOT_TYPE_NORMAL);
#endif /* CONFIG_METRICS_COLLECTION_FRAMEWORK */
	msleep(50);
	GPIO_SET_MODE_WORKING();

	if (nt_rc)
		mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SCAN_PORT);

	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWRON);
	mutex_unlock(&dev_mngr->op_lock);
	dev_info(&mdev->pdev->dev, "power on device done\n");
	return 0;
}

int mtk_pwrctl_power_off(struct pwrctl_mdev *mdev, int nt_rc)
{
	if (!dev_mngr) {
		dev_err(&mdev->pdev->dev, "[Power off] GPIO is not initialized\n");
		return -EINVAL;
	}

	if (mtk_pwrctl_get_power_state(mdev) == PWRCTL_STATE_PWROFF){
		dev_info(&mdev->pdev->dev, "Ignore the repeated power off\n");
		return -EINVAL;
	}

	if (!mutex_trylock(&dev_mngr->op_lock)) {
		dev_info(&mdev->pdev->dev, "lock failed, ignor the power off operation\n");
		return -EBUSY;
	}

	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWROFF);
	if (nt_rc)
		mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_REMOVE_PORT);

	mtk_pwrctl_controller_pin_off(mdev);
	GPIO_SET_MODE_OFF();
	gpiod_set_value(dev_mngr->gpio.pmic_en, 0);
	usleep_range(1000, 1100);
	gpiod_set_value(dev_mngr->gpio.pmic_reset, 0);

	mutex_unlock(&dev_mngr->op_lock);
	msleep(100);
	dev_info(&mdev->pdev->dev, "power off device done\n");
	return 0;
}

int mtk_pwrctl_fldr(void)
{
	struct pwrctl_mdev *mdev;
	int ret;
	int state;

	if (!dev_mngr){
		pr_err("pwrctl: No FLDR method!\n");
		return -EPERM;
	}

	mdev = dev_mngr->mdev;
	dev_info(&mdev->pdev->dev, "FLDR in by %ps\n", __builtin_return_address(0));

	state = mtk_pwrctl_get_power_state(mdev);
	if (state == PWRCTL_STATE_PWROFF) {
		dev_info(&mdev->pdev->dev, "Device is powered/soft off, ignore the warm reset\n");
		return 0;
	}

	if (!mutex_trylock(&dev_mngr->op_lock)) {
		dev_info(&mdev->pdev->dev, "lock failed, ignor the FLDR operation\n");
		return -EBUSY;
	}

	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWROFF);
	mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SOFT_OFF);

	gpiod_set_value(dev_mngr->gpio.pmic_reset, 0);
	udelay(2000);
	gpiod_set_value(dev_mngr->gpio.pmic_reset, 1);

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	mcf_notify_modem_boot_start(
		MODEM_BOOT_TYPE_MASK_WARM_RESET | MODEM_BOOT_TYPE_MASK_DUMP,
		MODEM_BOOT_TYPE_WARM_RESET);
#endif /* CONFIG_METRICS_COLLECTION_FRAMEWORK */

	msleep(300);
	ret = mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SOFT_ON);
	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWRON);
	mutex_unlock(&dev_mngr->op_lock);

	dev_info(&mdev->pdev->dev, "FLDR done\n");

	return ret;
}
EXPORT_SYMBOL(mtk_pwrctl_fldr);

int mtk_pwrctl_pldr(void)
{
	struct pwrctl_mdev *mdev;
	int ret;

	if (!dev_mngr){
		pr_err("pwrctl: No PLDR method!\n");
		return -EPERM;
	}

	mdev = dev_mngr->mdev;
	dev_info(&mdev->pdev->dev, "PLDR in by %ps\n", __builtin_return_address(0));
	if (mtk_pwrctl_get_power_state(mdev) == PWRCTL_STATE_PWROFF){
		dev_info(&mdev->pdev->dev, "Device is powered off, ignore the coldreset\n");
		return -EPERM;
	}

	if (!mutex_trylock(&dev_mngr->op_lock)) {
		dev_info(&mdev->pdev->dev, "lock failed, ignor the PLDR operation\n");
		return -EBUSY;
	}

	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWROFF);
	mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SOFT_OFF);

	mtk_pwrctl_controller_pin_off(mdev);
	GPIO_SET_MODE_OFF();
	gpiod_set_value(dev_mngr->gpio.pmic_en, 0);
	usleep_range(1000, 1100);
	gpiod_set_value(dev_mngr->gpio.pmic_reset, 0);
	msleep(220);
	gpiod_set_value(dev_mngr->gpio.pmic_reset, 1);
	wmb();
	gpiod_set_value(dev_mngr->gpio.pmic_en, 1);

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	mcf_notify_modem_boot_start(
		MODEM_BOOT_TYPE_MASK_NORMAL | MODEM_BOOT_TYPE_MASK_DUMP,
		MODEM_BOOT_TYPE_NORMAL);
#endif /* CONFIG_METRICS_COLLECTION_FRAMEWORK */

	msleep(50);
	GPIO_SET_MODE_WORKING();

	ret = mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SOFT_ON);
	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWRON);

	mutex_unlock(&dev_mngr->op_lock);
	dev_info(&mdev->pdev->dev, "PLDR done\n");
	return ret;
}
EXPORT_SYMBOL(mtk_pwrctl_pldr);

int mtk_pwrctl_remove_dev(void)
{
	/* for debug */
	return 0;
}
EXPORT_SYMBOL(mtk_pwrctl_remove_dev);

int mtk_pwrctl_aee_reboot(int time_ms)
{
	struct pwrctl_mdev *mdev;
	int ret;

	if (!dev_mngr){
		pr_err("pwrctl: No warm reboot method!\n");
		return -EPERM;
	}

	mdev = dev_mngr->mdev;
	dev_info(&mdev->pdev->dev, "AEE reboot in\n");
	if (mtk_pwrctl_get_power_state(mdev) == PWRCTL_STATE_PWROFF){
		dev_info(&mdev->pdev->dev, "Device is powered off, ignore aee reboot\n");
		return -EPERM;
	}

	if (!mutex_trylock(&dev_mngr->op_lock)) {
		dev_info(&mdev->pdev->dev, "lock fail, ignor the aee reboot\n");
		return -EPERM;
	}

	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWROFF);
	mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SOFT_OFF);
	gpiod_set_value(dev_mngr->gpio.aee_reboot_h, 1);
	udelay(1000);
	gpiod_set_value(dev_mngr->gpio.aee_reboot_h, 0);

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	mcf_notify_modem_boot_start(
		MODEM_BOOT_TYPE_MASK_WARM_RESET | MODEM_BOOT_TYPE_MASK_DUMP,
		MODEM_BOOT_TYPE_DUMP);
#endif /* CONFIG_METRICS_COLLECTION_FRAMEWORK */

	msleep(time_ms);
	ret = mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SOFT_ON);
	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWRON);
	mutex_unlock(&dev_mngr->op_lock);
	dev_info(&mdev->pdev->dev, "AEE reboot done\n");
	return ret;
}
EXPORT_SYMBOL(mtk_pwrctl_aee_reboot);

int mtk_pwrctl_force_md_assert(void)
{
	struct pwrctl_mdev *mdev;

	if (!dev_mngr){
		pr_err("pwrctl: GPIO is not initialized\n");
		return -EINVAL;
	}

	mdev = dev_mngr->mdev;
	dev_info(&mdev->pdev->dev, "Force modem assert in\n");
	if (mtk_pwrctl_get_power_state(mdev) == PWRCTL_STATE_PWROFF){
		dev_info(&mdev->pdev->dev, "Device is powered off, ignore force MD assert\n");
		return -EPERM;
	}

	if (!mutex_trylock(&dev_mngr->op_lock)) {
		dev_info(&mdev->pdev->dev, "lock fail, ignore force MD assert\n");
		return -EPERM;
	}

	gpiod_set_value(dev_mngr->gpio.mddump, 1);
	mdelay(15);
	gpiod_set_value(dev_mngr->gpio.mddump, 0);
	mutex_unlock(&dev_mngr->op_lock);
	dev_info(&mdev->pdev->dev, "Force modem assert done\n");

	return 0;
}
EXPORT_SYMBOL(mtk_pwrctl_force_md_assert);

static bool mtk_pwrctl_gpio_debounce(struct gpio_desc *pin, bool level, int dt_us)
{
	int db_cnt = (dt_us < 100) ? 1 : (dt_us / 100);

	do {
		if (level != gpiod_get_value(pin))
			return false;
		udelay(100);
	} while( db_cnt-- );

	return true;
}

static irqreturn_t mtk_pwrctl_reboot_int_handler(int irq, void *arg)
{
	struct pwrctl_mdev *mdev = dev_mngr->mdev;

	if (irq != dev_mngr->irq.reboot_int_b) {
		dev_warn(&mdev->pdev->dev, "The received IRQ<%d> is not expected IRQ<%d>\n",
			 irq, dev_mngr->irq.reboot_int_b);
		goto end;
	}

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	dev_info(&mdev->pdev->dev, "Reboot interrupt detected; scheduling debounce work\n");
#endif

	queue_work(dev_mngr->reboot_int_wq, &dev_mngr->reboot_int_work);

end:
	return IRQ_HANDLED;
}

static irqreturn_t mtk_pwrctl_faultb_handler(int irq, void *arg)
{
	struct pwrctl_mdev *mdev = dev_mngr->mdev;

	if (irq != dev_mngr->irq.faultb) {
		dev_warn(&mdev->pdev->dev, "The received IRQ<%d> is not expected IRQ<%d>\n",
			 irq, dev_mngr->irq.faultb);
		return IRQ_HANDLED;
	}

	queue_work(dev_mngr->faultb_wq, &dev_mngr->faultb_work);

	return IRQ_HANDLED;
}

static void mtk_pwrctl_reboot_int_work(struct work_struct * work)
{
	struct pwrctl_mdev *mdev = dev_mngr->mdev;

	if (!mtk_pwrctl_gpio_debounce(dev_mngr->gpio.reboot_int_b, 0, 60000)) {
		dev_info(&dev_mngr->mdev->pdev->dev,
			 "Ignore the glitch of reboot interrupt\n");
		return;
	}

	if (mtk_pwrctl_get_power_state(mdev) == PWRCTL_STATE_PWROFF ||
		test_bit(PWRCTL_PMIC_FAULTB, &mdev->pm.flags)) {
		dev_info(&mdev->pdev->dev,
			 "Device is powered off, ignore the reboot interrupt\n");
		return;
	}

	dev_info(&dev_mngr->mdev->pdev->dev, "Device is rebooting\n");
	mtk_pwrctl_event_notify(PWRCTL_EVT_PRERST, PWRCTL_UEVENT_RESET);
}

static void mtk_pwrctl_faultb_int_work(struct work_struct * work)
{
	struct pwrctl_mdev *mdev = dev_mngr->mdev;

	if (!mtk_pwrctl_gpio_debounce(dev_mngr->gpio.faultb, 0, 2000))
		return;

	if (test_bit(PWRCTL_PMIC_FAULTB, &mdev->pm.flags) ||
	    mtk_pwrctl_get_power_state(mdev) == PWRCTL_STATE_PWROFF) {
		dev_info(&mdev->pdev->dev, "Repeated FAULTB interrupt, ignore\n");
		return;
	}

	dev_info(&mdev->pdev->dev, "Device is powered off with PMIC FAULTB\n");
	mutex_lock(&dev_mngr->op_lock);
	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWROFF);
	mtk_pwrctl_controller_disable_data_trans(mdev);
	mtk_pwrctl_controller_disable_refclk(mdev);
	mtk_pwrctl_controller_pin_off(mdev);
	GPIO_SET_MODE_OFF();
	mtk_pwrctl_event_notify(PWRCTL_EVT_PWROFF, PWRCTL_UEVENT_POWER_OFF);
	mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SOFT_OFF);

	gpiod_set_value(dev_mngr->gpio.pmic_en, 0);
	gpiod_set_value(dev_mngr->gpio.pmic_reset, 0);
	set_bit(PWRCTL_PMIC_FAULTB, &mdev->pm.flags);

	msleep(220);

	gpiod_set_value(dev_mngr->gpio.pmic_reset, 1);
	gpiod_set_value(dev_mngr->gpio.pmic_en, 1);
	msleep(50);
	GPIO_SET_MODE_WORKING();
	mtk_pwrctl_request_to_controller(mdev, PWRCTL_CONTROLLER_REQUEST_SOFT_ON);
	mtk_pwrctl_event_notify(PWRCTL_EVT_PWRON, PWRCTL_UEVENT_POWER_ON);
	clear_bit(PWRCTL_PMIC_FAULTB, &mdev->pm.flags);
	mtk_pwrctl_set_power_state(mdev, PWRCTL_STATE_PWRON);
	mutex_unlock(&dev_mngr->op_lock);
	dev_info(&mdev->pdev->dev, "FAULTB int work done\n");
}

long mtk_pwrctl_cmd_process(struct pwrctl_mdev *mdev, int cmd)
{
	enum pwrctl_state state = mtk_pwrctl_get_power_state(mdev);

	dev_info(&mdev->pdev->dev, "IOCTL CMD: %d by user[%d:%s]\n", _IOC_NR(cmd),
		 current->pid, current->comm);

	switch(cmd) {
	case PWRCTL_CMD_POWER_ON:
		mtk_pwrctl_power_on(mdev, true);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		mtk_pwrctl_event_notify(PWRCTL_EVT_PWRON, PWRCTL_UEVENT_POWER_ON);
#else
		mtk_pwrctl_uevent_notify_user(PWRCTL_UEVENT_POWER_ON);
#endif
		break;
	case PWRCTL_CMD_POWER_OFF:
		if (state == PWRCTL_STATE_PWROFF){
			dev_info(&mdev->pdev->dev,
				 "Device is powered off, ignore the repeated operation\n");
			return -EINVAL;
		}

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		mtk_pwrctl_event_notify(PWRCTL_EVT_PWROFF, PWRCTL_UEVENT_POWER_OFF);
#else
		mtk_pwrctl_uevent_notify_user(PWRCTL_UEVENT_POWER_OFF);
#endif
		mtk_pwrctl_power_off(mdev, true);
		break;
	case PWRCTL_CMD_COLD_RESET:
		if (state == PWRCTL_STATE_PWROFF){
			dev_info(&mdev->pdev->dev,
				 "Device is powered off, please execute power on command first\n");
			return -EINVAL;
		}

		mtk_pwrctl_event_notify(PWRCTL_EVT_PWROFF, PWRCTL_UEVENT_POWER_OFF);
		if (mtk_pwrctl_pldr())
			break;

		mtk_pwrctl_event_notify(PWRCTL_EVT_PWRON, PWRCTL_UEVENT_POWER_ON);
		break;
	case PWRCTL_CMD_WARM_RESET:
		if(state == PWRCTL_STATE_PWROFF) {
			dev_info(&mdev->pdev->dev,
				"The device is powered off, ignore warm reset operation\n");
			return -EINVAL;
		}

		mtk_pwrctl_event_notify(PWRCTL_EVT_PWROFF, PWRCTL_UEVENT_POWER_OFF);
		if (mtk_pwrctl_fldr())
			break;

		mtk_pwrctl_event_notify(PWRCTL_EVT_PWRON, PWRCTL_UEVENT_POWER_ON);
		break;
	case PWRCTL_CMD_FORCE_MD_ASSERT:
		if(state == PWRCTL_STATE_PWROFF) {
			dev_info(&mdev->pdev->dev,
				"The device is powered off, ignore dump operation\n");
			return -EINVAL;
		}

		mtk_pwrctl_force_md_assert();
		break;
	case PWRCTL_CMD_AEE_REBOOT:
		if(state == PWRCTL_STATE_PWROFF) {
			dev_info(&mdev->pdev->dev,
				"The device is powered off, ignore aee reboot\n");
			return -EINVAL;
		}

		mtk_pwrctl_event_notify(PWRCTL_EVT_PWROFF, PWRCTL_UEVENT_POWER_OFF);
		if (mtk_pwrctl_aee_reboot(PWRCTL_WAIT_AEE_DUMP_TIME))
			break;

		mtk_pwrctl_event_notify(PWRCTL_EVT_PWRON, PWRCTL_UEVENT_POWER_ON);
		break;
	default:
		dev_err(&mdev->pdev->dev, "Invalid command\n");
		return -EINVAL;
	break;
        }
	return 0;
}

static int mtk_pwrctl_dev_irq_req(struct device *dev, struct gpio_desc *gpio_desc, irq_handler_t handler,
				  unsigned long irqflags, bool irq_wake_flag)
{
	int gpio_num;
	int irq;
	int ret;

	if (IS_ERR(gpio_desc))
		return IRQ_NOTCONNECTED;

	gpio_num = desc_to_gpio(gpio_desc);
	irq = gpiod_to_irq(gpio_desc);
	if (irq < 0) {
		dev_err(dev,"Failed to get irq of GPIO<%d>\n", gpio_num);
		return IRQ_NOTCONNECTED;
	}

	ret = devm_request_irq(dev,irq, handler,irqflags, "pwrctl", NULL);
	if (ret < 0) {
		dev_err(dev,"Register handler of GPIO<%d> fail, %d\n", gpio_num, ret);
		return IRQ_NOTCONNECTED;
	}

	dev_info(dev,"Request irq<%d> and register handler of GPIO<%d> success\n", irq, gpio_num);
	if (irq_wake_flag) {
		ret = enable_irq_wake(irq);
		dev_info(dev, "Enable IRQ wake of GPIO<%d> %s\n", gpio_num, ret? "fail" : "success");
	}

	return irq;
}

static int mtk_pwrctl_dev_gpio_init(struct pwrctl_dev_mngr *dev_mngr)
{
	struct pwrctl_gpio *gpio;
	struct pwrctl_irq *irq;
	int ret;

	gpio = &dev_mngr->gpio;
	irq = &dev_mngr->irq;

	dev_mngr->reboot_int_wq = alloc_workqueue("reboot_int", WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!dev_mngr->reboot_int_wq) {
		dev_info(dev_mngr->dev, "Failed to alloc reboot_int_wq\n");
		ret = -ENOMEM;
		goto reboot_int_wq_err;
	}

	dev_mngr->faultb_wq = alloc_workqueue("faultb_wq", WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!dev_mngr->faultb_wq) {
		dev_info(dev_mngr->dev, "Failed to alloc faultb_wq\n");
		ret = -ENOMEM;
		goto faultb_wq_err;
	}

	INIT_WORK(&dev_mngr->reboot_int_work, mtk_pwrctl_reboot_int_work);
	INIT_WORK(&dev_mngr->faultb_work, mtk_pwrctl_faultb_int_work);

	gpio->pmic_reset = PWRCTL_GPIOD_GET(dev_mngr->dev, PWRCTL_GPIO_RESET, GPIOD_OUT_HIGH);
	gpio->pmic_en = PWRCTL_GPIOD_GET(dev_mngr->dev, PWRCTL_GPIO_PMIC_EN, GPIOD_OUT_LOW);
	gpio->aee_reboot_h = PWRCTL_GPIOD_GET(dev_mngr->dev, PWRCTL_GPIO_AEE_REBOOT_H,
					      GPIOD_OUT_LOW);
	gpio->mddump = PWRCTL_GPIOD_GET(dev_mngr->dev, PWRCTL_GPIO_MODOME_DUMP, GPIOD_OUT_LOW);
	gpio->reboot_int_b = PWRCTL_GPIOD_GET(dev_mngr->dev, PWRCTL_GPIO_REBOOT_INT, GPIOD_IN);
	gpio->faultb = PWRCTL_GPIOD_GET(dev_mngr->dev, PWRCTL_GPIO_PMIC_FAULTB, GPIOD_IN);

	irq->reboot_int_b = mtk_pwrctl_dev_irq_req(dev_mngr->dev, gpio->reboot_int_b,
						mtk_pwrctl_reboot_int_handler,
						IRQF_TRIGGER_FALLING,
						true);
	irq->faultb = mtk_pwrctl_dev_irq_req(dev_mngr->dev, gpio->faultb,
						mtk_pwrctl_faultb_handler,
						IRQF_TRIGGER_FALLING,
						true);

	dev_info(dev_mngr->dev, "gpio init done\n");
	return 0;

gpio_init_err:
	destroy_workqueue(dev_mngr->reboot_int_wq);
	destroy_workqueue(dev_mngr->faultb_wq);
faultb_wq_err:
	destroy_workqueue(dev_mngr->reboot_int_wq);
reboot_int_wq_err:
	return ret;
}
static void mtk_pwrctl_dev_gpio_uninit(struct pwrctl_dev_mngr *dev_mngr)
{
	struct pwrctl_irq *irq = &dev_mngr->irq;

	flush_work(&dev_mngr->reboot_int_work);
	flush_work(&dev_mngr->faultb_work);
	mtk_pwrctl_irq_free(dev_mngr->dev, irq->reboot_int_b);
	mtk_pwrctl_irq_free(dev_mngr->dev, irq->faultb);
	destroy_workqueue(dev_mngr->reboot_int_wq);
	destroy_workqueue(dev_mngr->faultb_wq);

	dev_info(dev_mngr->dev, "gpio free done\n");
}

static void mtk_pwrctl_dev_param_init(struct pwrctl_mdev *mdev)
{
	dev_mngr = &mdev->dev_mngr;
	dev_mngr->dev = &mdev->pdev->dev;
	dev_mngr->mdev = mdev;

	mutex_init(&dev_mngr->op_lock);

	clear_bit(PWRCTL_PMIC_FAULTB, &mdev->pm.flags);
	clear_bit(PWRCTL_SKIP_SUSPEND_OFF, &mdev->pm.flags);
}

static void mtk_pwrctl_dev_param_uninit(struct pwrctl_mdev *mdev)
{
	dev_mngr->dev = NULL;
	dev_mngr->mdev = NULL;
	dev_mngr = NULL;
}

int mtk_pwrctl_dev_init(struct pwrctl_mdev *mdev)
{
	mtk_pwrctl_dev_param_init(mdev);
	mtk_pwrctl_dev_gpio_init(&mdev->dev_mngr);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	pwrctrl_google_init(mdev->pdev);
#endif

	return 0;
}

int mtk_pwrctl_dev_uninit(struct pwrctl_mdev *mdev)
{
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	pwrctrl_google_exit(mdev->pdev);
#endif
	mtk_pwrctl_dev_gpio_uninit(&mdev->dev_mngr);
	mtk_pwrctl_dev_param_uninit(mdev);
	return 0;
}
