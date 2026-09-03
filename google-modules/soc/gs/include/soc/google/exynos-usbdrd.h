/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2025 Google LLC
 */

#ifndef __EXYNOS_USBDRD_H
#define __EXYNOS_USBDRD_H

struct phy;
struct device;

extern void __iomem *phycon_base_addr;

int exynos_usbdrd_phy_tune(struct phy *phy, int phy_state);
void exynos_usbdrd_usbdp_tca_set(struct phy *phy, int mux, int low_power_en);
int exynos_usbdrd_set_s2mpu_pm_ops(int (*cb)(struct device *dev, bool on));
int exynos_usbdrd_s2mpu_manual_control(bool on);
int exynos_usbdrd_phy_vendor_set(struct phy *phy, int is_enable,
				 int is_cancel);
int exynos_usbdrd_pipe3_enable(struct phy *phy);
int exynos_usbdrd_pipe3_disable(struct phy *phy);

#endif /* __EXYNOS_USBDRD_H */
