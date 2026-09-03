// SPDX-License-Identifier: GPL-2.0 only.
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __MTK_PWRCTL_COMMON_H_
#define __MTK_PWRCTL_COMMON_H_

#include <linux/interrupt.h>
#include "mtk_pwrctl.h"

enum pwrctl_state {
	PWRCTL_STATE_PWROFF = 0,
	PWRCTL_STATE_SOFTOFF,
	PWRCTL_STATE_PWRON,
	PWRCTL_STATE_WORKING,
	PWRCTL_STATE_MAXNUM = PWRCTL_STATE_WORKING  // go/NOTYPO
};

/* Callback for notify power status */
typedef void (*pwrctl_event_callback)(enum pwrctl_evt evt, void *arg);

struct pwrctl_pm {
	enum pwrctl_state state;
	unsigned long flags;
};
struct pwrctl_port {
	atomic_t usage_cnt;
	struct device *dev;
};

struct pwrctl_evt_cb {
	struct list_head entry;
	pwrctl_event_callback cb;
	void *priv;
};

struct pwrctl_notify {
	struct list_head cb_list;
	enum pwrctl_evt evt;
	struct mutex mlock;
};

struct pwrctl_mdev {
	char *name;
	struct platform_device *pdev;
	struct pwrctl_pm pm;
	struct pwrctl_port port;
	struct pwrctl_notify notifier;
	struct pwrctl_dev_mngr dev_mngr;
	void *priv_data;
};

int mtk_pwrctl_get_power_state(struct pwrctl_mdev *mdev);
void mtk_pwrctl_set_power_state(struct pwrctl_mdev *mdev,enum pwrctl_state to_state);
int mtk_pwrctl_gpio_request(struct device *dev, const char *label);
int mtk_pwrctl_irq_request(struct device *dev, int pin, irq_handler_t handler,
				  unsigned long irqflags, bool irq_wake_flag);
int mtk_pwrctl_irq_free(struct device *dev, int irq);
int mtk_pwrctl_uevent_notify_user(const char * euvent_info);
int mtk_pwrctl_cb_notify_user(enum pwrctl_evt evt);
int mtk_pwrctl_dev_init(struct pwrctl_mdev *mdev);
int mtk_pwrctl_dev_uninit(struct pwrctl_mdev *mdev);
int mtk_pwrctl_power_on(struct pwrctl_mdev *mdev, int nt_rc);
int mtk_pwrctl_power_off(struct pwrctl_mdev *mdev, int nt_rc);
long mtk_pwrctl_cmd_process(struct pwrctl_mdev *mdev, int cmd);
void mtk_pwrctl_enable_irqs(void);
void mtk_pwrctl_disable_irqs(void);
#endif //__MTK_PWRCTL_COMMON_H_
