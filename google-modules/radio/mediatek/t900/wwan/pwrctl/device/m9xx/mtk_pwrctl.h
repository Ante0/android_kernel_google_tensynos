// SPDX-License-Identifier: GPL-2.0 only.
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __MTK_PWRCTL_H_
#define __MTK_PWRCTL_H_

/* Power control command magic number */
#define PWRCTL_CMD_MAGIC		'G'
/* Power control command through ioctl of /dev/wwan_pwrctl */
#define PWRCTL_CMD_POWER_ON		_IOW(PWRCTL_CMD_MAGIC, 0, int)
#define PWRCTL_CMD_POWER_OFF		_IOW(PWRCTL_CMD_MAGIC, 1, int)
#define PWRCTL_CMD_WARM_RESET		_IOW(PWRCTL_CMD_MAGIC, 2, int)
#define PWRCTL_CMD_COLD_RESET		_IOW(PWRCTL_CMD_MAGIC, 3, int)
#define PWRCTL_CMD_FORCE_MD_ASSERT	_IOW(PWRCTL_CMD_MAGIC, 5, int)
#define PWRCTL_CMD_AEE_REBOOT		_IOW(PWRCTL_CMD_MAGIC, 6, int)

/* GPIO pin support mapping: REBOOT_INT: bit-1, AEE_REBOOT_H: bit-0 */
#define DEV_PIN_SUPPORT_MAPPING		((1 << 2) | (1 << 1) | (1 << 0))

/* Event ID for power status notification.
 * PWRCTL_EVT_RESET: indicate the devcie is reset.
 * PWRCTL_EVT_PWRON: indicate the device is powered on.
 * PWRCTL_EVT_PWROFF: indicate the devcie is powered off.
 * PWRCTL_EVT_PRERST: indicate the device is waiting host give a reset pulse.
 */
enum pwrctl_evt {
	PWRCTL_EVT_RESET = 0,
	PWRCTL_EVT_PWRON,
	PWRCTL_EVT_PWROFF,
	PWRCTL_EVT_PRERST,
	PWRCTL_EVT_MAXNUM = PWRCTL_EVT_PRERST  // go/NOTYPO
};

enum pwrctl_flags {
	PWRCTL_SKIP_SUSPEND_OFF,
	PWRCTL_PMIC_FAULTB,
	PWRCTL_FLAG_MAXNUM = PWRCTL_PMIC_FAULTB  // go/NOTYPO
};

/* struct pwrctl_gpio - The GPIO description.
 * @pmic_en: The pin connects to PMIC_EN, for power on/off device.
 * @mode_ctrl: The pin connects to PMIC_RST, for warm reset or soft off.
 * @reboot: The pin connects to REBOOT_INT interrupt.
 * @modem_dump: The pin is used to trigger modem dump.
 * @faultb: The pin is used to monitor FAULTB.
 */
struct pwrctl_gpio {
	struct gpio_desc *pmic_en;
	struct gpio_desc *pmic_reset;
	struct gpio_desc *reboot_int_b;
	struct gpio_desc *mddump;
	struct gpio_desc *faultb;
	struct gpio_desc *aee_reboot_h;
};

struct pwrctl_irq {
	int reboot_int_b;
	int faultb;
};

struct pwrctl_dev_mngr {
	struct pwrctl_mdev *mdev;
	struct device *dev;
	struct pwrctl_gpio gpio;
	struct pwrctl_irq irq;
	struct mutex op_lock;
	struct work_struct reboot_int_work;
	struct work_struct faultb_work;
	struct workqueue_struct *reboot_int_wq;
	struct workqueue_struct *faultb_wq;
};

/* Founction for register event notify callback for host PCIe (EP)driver */
int mtk_pwrctl_event_register_callback( void (*cb)(enum pwrctl_evt , void *),
					void *data);
int mtk_pwrctl_event_unregister_callback(void (*cb)(enum pwrctl_evt , void *));

/*
 * Function for setting suspend skip flag, called by host PCIe (EP) driver if
 * can't power off device.
 * @flag: 1: don't power off device when suspend.
 *        0: power off device when suspend.
 */
int mtk_pwrctl_set_suspend_skip(bool flag);

/* Function for warm reset device with sysrst of PMIC_RESET*/
int mtk_pwrctl_fldr(void);

/* Function for code reset device device */
int mtk_pwrctl_pldr(void);

/* Pull PIN to trigger device reboot and aee dump */
int mtk_pwrctl_aee_reboot(int time_ms);

/* Pull PIN to force Modem assert */
int mtk_pwrctl_force_md_assert(void);

/* for debug */
int mtk_pwrctl_remove_dev(void);

#endif //__MTK_PWRCTL_H_
