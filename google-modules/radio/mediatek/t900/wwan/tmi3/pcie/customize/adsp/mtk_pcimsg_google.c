// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/jiffies.h>
#include <linux/delay.h>

#include "mtk_debug.h"
#include "mtk_except.h"
#include "mtk_pcimsg.h"

#include "radio-bridge.h"

#include "mtk-pcie-pm-user-internal.h"

#define TAG "mtk_pcimsg_google"
#define PCI_BUSY_WAIT_TIMEOUT_MS 5000

#define LOG_INFO(fmt, ...) \
	pr_info("%s: %s: " pr_fmt(fmt), TAG, __func__, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) \
	pr_err("%s: %s: " pr_fmt(fmt), TAG, __func__, ##__VA_ARGS__)

static struct notifier_block nb;
DECLARE_WAIT_QUEUE_HEAD(__pci_busy_wq);

int mtk_pcimsg_send_msg_to_user(struct mtk_md_dev *mdev, int msg_id)
{
	switch (msg_id) {
	case MTK_PCIMSG_H2C_READY:
		break;
	case MTK_PCIMSG_H2C_EXCEPT:
		LOG_INFO("Modem exception!\n");
		radio_br_modem_exception();
		break;
	default:
		LOG_INFO("Received unsupported msg_id: %d\n", msg_id);
		dump_stack();
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_pcimsg_send_msg_to_user);

int mtk_pcimsg_wait_pci_user_inactive(struct mtk_md_dev *mdev)
{
	long ret;
	long timeout_jiffies = msecs_to_jiffies(PCI_BUSY_WAIT_TIMEOUT_MS);

	ret = wait_event_interruptible_timeout(__pci_busy_wq,
			!mtk_pci_user_pm_any_active(),
			timeout_jiffies);

	if (ret == 0) {
		LOG_ERR("timed out!\n");
		mtk_pci_user_dump_active_users();
		return -ETIMEDOUT;
	}

	if (ret < 0) {
		LOG_ERR("wait interrupted (rc: %ld)!\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_pcimsg_wait_pci_user_inactive);

bool mtk_pcimsg_pci_user_is_busy(struct mtk_md_dev *mdev, bool is_suspend)
{
	return mtk_pci_user_spm_eval_busy_state(is_suspend);
}
EXPORT_SYMBOL_GPL(mtk_pcimsg_pci_user_is_busy);

static int mtk_pcimsg_recv_msg_from_user(struct notifier_block *nb,
		unsigned long event, void *data)
{
	bool *handled = data;
	int ret;

	switch (event) {
	case RADIO_BR_MODEM_AUDIO_PATH_PREPARE:
		ret = mtk_pci_user_rpm_get(MTK_PCI_USER_AUDIO);
		LOG_INFO("start voice call, ret=%d\n", ret);
		*handled = !ret;
		return notifier_from_errno(ret);
	case RADIO_BR_MODEM_AUDIO_PATH_UNPREPARE:
		ret = mtk_pci_user_rpm_put(MTK_PCI_USER_AUDIO);
		LOG_INFO("stop voice call, ret=%d\n", ret);
		*handled = !ret;
		return notifier_from_errno(ret);
	}

	return NOTIFY_DONE;
}

int mtk_pcimsg_messenger_init(struct mtk_md_dev *mdev)
{
	int ret;

	ret = mtk_pci_user_register_busy_wq(&__pci_busy_wq);
	if (ret < 0)
		LOG_ERR("Failed to register busy wait queue, ret = %d!\n", ret);

	nb.notifier_call = mtk_pcimsg_recv_msg_from_user;
	ret = radio_br_notifier_register(&nb);
	if (ret < 0) {
		LOG_ERR("Failed to init notifier!\n");
		return -EAGAIN;
	}

	mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_READY);

	LOG_INFO("PCIMSG init done\n");
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_pcimsg_messenger_init);

int mtk_pcimsg_messenger_exit(struct mtk_md_dev *mdev)
{
	mtk_pci_user_unregister_busy_wq();

	radio_br_notifier_unregister(&nb);
	LOG_INFO("PCIMSG exit done\n");
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_pcimsg_messenger_exit);

MODULE_AUTHOR("Mahesh Kallelil <kallelil@google.com>");
MODULE_DESCRIPTION("Google PCIMSG interface for MTK Modem");
MODULE_LICENSE("GPL v2");
