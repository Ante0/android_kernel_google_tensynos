/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __MTK_PCIE_PM_USER__
#define __MTK_PCIE_PM_USER__

#include <linux/types.h>
#include "mtk_dev.h"

/*
 * This enum defines the external users of the WWAN PM voting
 * mechanism. These IDs are used for debugging and tracking.
 */
enum mtk_pci_user_id { MTK_PCI_USER_AUDIO, MTK_PCI_USER_GNSS, MTK_PCI_USER_NOA, MTK_PCI_USER_MAX };

/*
 * mtk_pci_user_rpm_get - Acquire a runtime PM reference for an external user.
 * @user_id: The ID of the user (e.g., MTK_PCI_USER_GNSS).
 *
 * This function increments the WWAN device's RPM usage count, preventing it
 * from entering runtime suspend. This is a synchronous call and will block
 * until the device is resumed.
 *
 * @return 0 on success
 * @return 1 if the device was already in the active state. No resume callback
 * was needed.
 * @return A negative error code on failure.
 */
int mtk_pci_user_rpm_get(enum mtk_pci_user_id user_id);

/*
 * mtk_pci_user_rpm_put - Release a runtime PM reference for an external user.
 * @user_id: The ID of the user (e.g., MTK_PCI_USER_GNSS).
 *
 * This function decrements the WWAN device's RPM usage count, allowing it
 * to enter runtime suspend if it is otherwise idle.
 *
 * @return 0 on success.
 * @return 1 if the usage counter was decremented but is still greater than
 * zero, so the device remains active.
 * @return -EAGAIN if the service is not yet ready and the call should be
 * retried.
 * @return Another negative error code on other failures (e.g., invalid
 * user_id).
 */
int mtk_pci_user_rpm_put(enum mtk_pci_user_id user_id);

// TODO: b/473984596 - Workaround to lock DS during VoLTE/VoNR.
struct mtk_pci_user_pm_ops {
	void (*lock)(struct mtk_md_dev *dev, enum mtk_user_id id);
	void (*unlock)(struct mtk_md_dev *dev, enum mtk_user_id id);
	int (*wait_complete)(struct mtk_md_dev *dev, enum mtk_user_id id);
};

void mtk_pci_user_register_pm_ops(struct mtk_pci_user_pm_ops *ops);

#endif /* __MTK_PCIE_PM_USER__ */
