/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef MTK_GOOGLE_H
#define MTK_GOOGLE_H

#include <linux/ktime.h>
#include <linux/rtc.h>
#include <linux/time.h>

#include "mtk_debug.h"
#include "mtk_dev.h"
#include "mtk_fsm.h"
#include "mtk_port.h"

#define MTK_GOOGLE_INFO_RTC(mdev, fmt, args...)                                                  \
	do {                                                                                     \
		struct timespec64 _ts;                                                           \
		struct rtc_time _tm;                                                             \
		ktime_get_real_ts64(&_ts);                                                       \
		rtc_time64_to_tm(_ts.tv_sec - (sys_tz.tz_minuteswest * 60), &_tm);               \
		MTK_INFO(mdev, "[%ptRt.%06lu]:" fmt, &_tm, _ts.tv_nsec / NSEC_PER_USEC, ##args); \
	} while (0)

struct tmi_fsm_ops {
	int (*notifier_register)(struct mtk_md_dev *mdev, enum mtk_user_id id,
				 void (*cb)(struct mtk_fsm_param *, void *data), void *data,
				 enum mtk_fsm_prio prio, bool is_pre);
	int (*notifier_unregister)(struct mtk_md_dev *mdev, enum mtk_user_id id);
};

struct tmi_pwrctl_ops {
	int (*force_md_assert)(void);
};

struct tmi_ops {
	struct tmi_fsm_ops fsm;
	struct tmi_pwrctl_ops pwrctl;
};

struct tmi_ops *mtk_google_get_tmi_ops(void);

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
u32 mtk_google_get_active_irqs(struct mtk_md_dev *mdev);
struct mtk_port *mtk_google_resolve_skb_port(struct sk_buff *skb, void *priv);
#endif

#endif /* MTK_GOOGLE_H */
