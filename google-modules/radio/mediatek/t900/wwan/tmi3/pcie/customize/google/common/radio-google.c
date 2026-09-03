// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/pci.h>

#include "ap-awake.h"
#include "feature-control.h"
#include "fsm-listener.h"
#include "link-exception.h"
#include "md2ap-wakemon.h"
#include "modem_metrics.h"
#include "mtk-pcie-pm-user-internal.h"
#include "mtk-pcie.h"
#include "mtk_dev.h"
#include "radio-google.h"
#include "radio-utils.h"
#include "remote-wakeup.h"
#include "soc-qos-ext.h"
#include "dpmaif-google.h"

int radio_google_early_init(void)
{
	int ret;

	google_feature_control_init();

	ret = dpmaif_google_affinity_init();
	if (ret) {
		LOG_ERR("dpmaif_google_affinity_init() failed, ret=%d\n", ret);
		goto free_google_feature_control;
	}

	return 0;

free_google_feature_control:
	google_feature_control_exit();

	return ret;
}
EXPORT_SYMBOL_GPL(radio_google_early_init);

void radio_google_late_exit(void)
{
	dpmaif_google_affinity_exit();
	google_feature_control_exit();
}
EXPORT_SYMBOL_GPL(radio_google_late_exit);

int radio_google_init(struct mtk_md_dev *mdev, struct tmi_ops *ops)
{
	struct radio_google *radio_google;
	int ret;

	radio_google = devm_kzalloc(mdev->dev, sizeof(*radio_google), GFP_KERNEL);
	if (!radio_google) {
		ret = -ENOMEM;
		goto out;
	}

	radio_google->mdev = mdev;
	radio_google->tmi_ops = ops;
	mdev->google = radio_google;

	/* Mandatory features */

	ret = mtk_google_pcie_init(radio_google);
	if (ret) {
		LOG_ERR("mtk_google_pcie_init() failed, ret=%d\n", ret);
		goto out;
	}

	ret = remote_wakeup_init(radio_google);
	if (ret) {
		LOG_ERR("remote_wakeup_init() failed, ret=%d\n", ret);
		goto free_mtk_google_pcie;
	}

	ret = mtk_pci_user_init(radio_google);
	if (ret) {
		LOG_ERR("mtk_pci_user_init() failed, ret=%d\n", ret);
		goto free_remote_wakeup;
	}

	ret = fsm_listener_init(radio_google);
	if (ret) {
		LOG_ERR("fsm_listener_init() failed, ret=%d\n", ret);
		goto free_mtk_pci_user;
	}

	ret = dpmaif_google_lro_size_limit_init();
	if (ret) {
		LOG_ERR("dpmaif_google_lro_size_limit_init() failed, ret=%d\n", ret);
		goto free_fsm_listener;
	}

	/* Optional features */

	ret = link_exception_init(radio_google);
	if (ret)
		LOG_WARN("link_exception_init() failed, ret=%d\n", ret);

	ret = soc_qos_init(radio_google);
	if (ret)
		LOG_WARN("soc_qos_init() failed, ret=%d\n", ret);

	ret = modem_metrics_init(radio_google);
	if (ret)
		LOG_WARN("modem_metrics_init() failed, ret=%d\n", ret);

	ret = md2ap_wakemon_init(radio_google);
	if (ret)
		LOG_WARN("md2ap_wakemon_init() failed, ret=%d\n", ret);

	ret = ap_awake_gpio_init(radio_google);
	if (ret)
		LOG_WARN("ap_awake_gpio_init() failed, ret=%d\n", ret);

	return 0;

free_fsm_listener:
	fsm_listener_exit(radio_google);
free_mtk_pci_user:
	mtk_pci_user_exit(radio_google);
free_remote_wakeup:
	remote_wakeup_exit(radio_google);
free_mtk_google_pcie:
	mtk_google_pcie_exit(radio_google);
	devm_kfree(mdev->dev, radio_google);
out:
	LOG_ERR("Failed to init radio_google, ret=%d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(radio_google_init);

void radio_google_exit(struct mtk_md_dev *mdev)
{
	struct radio_google *radio_google = mdev->google;

	md2ap_wakemon_exit(radio_google);
	modem_metrics_exit(radio_google);
	soc_qos_exit(radio_google);
	dpmaif_google_lro_size_limit_exit();
	fsm_listener_exit(radio_google);
	mtk_pci_user_exit(radio_google);
	remote_wakeup_exit(radio_google);
	mtk_google_pcie_exit(radio_google);

	devm_kfree(mdev->dev, radio_google);
}
EXPORT_SYMBOL_GPL(radio_google_exit);

MODULE_AUTHOR("Mahesh Kallelil <kallelil@google.com>");
MODULE_DESCRIPTION("Module for Google radio features");
MODULE_LICENSE("GPL");
