// SPDX-License-Identifier: GPL-2.0 only.
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include "mtk_pwrctl_common.h"
#include "mtk_pwrctl_interface.h"
#ifdef CONFIG_WWAN_GPIO_PWRCTL_UT
#include "ut_pwrctl_pcie_fake.h"
#endif
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "config/pcie-config.h"
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#define PWRCTL_PCIE_PORT_NUM		(GOOGLE_PCIE_PORT_NUM)
#else
#define PWRCTL_PCIE_PORT_NUM		(1)
#endif
#define PWRCTL_PCIE_PIN_OFF		(4)

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
static __attribute__ ((weakref("mtk_pcie_probe_port"))) int mtk_pwrctl_pcie_probe_port(struct platform_device *pdev, int port);
#else
static __attribute__ ((weakref("mtk_pcie_probe_port"))) int mtk_pwrctl_pcie_probe_port(int port);
#endif
static __attribute__ ((weakref("mtk_pcie_remove_port"))) int mtk_pwrctl_pcie_remove_port(int port);
static __attribute__ ((weakref("mtk_pcie_soft_on"))) \
		       int mtk_pwrctl_pcie_soft_on(struct pci_bus *bus);
static __attribute__ ((weakref("mtk_pcie_soft_off"))) \
		       int mtk_pwrctl_pcie_soft_off(struct pci_bus *bus);

static __attribute__ ((weakref("mtk_pcie_disable_refclk"))) \
			int mtk_pwrctl_pcie_disable_refclk(int port);
static __attribute__ ((weakref("mtk_pcie_pinmux_select"))) \
			int mtk_pwrctl_pcie_pinmux_select(int port_num, int state);
static __attribute__ ((weakref("mtk_pcie_disable_data_trans"))) \
			int mtk_pwrctl_pcie_disable_data_trans(int port);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#define MTK_PCIE_PROBE_PORT(pdev, port) \
	do { if (mtk_pwrctl_pcie_probe_port) mtk_pwrctl_pcie_probe_port(pdev, port); } while(0)
#else
#define MTK_PCIE_PROBE_PORT(pdev) \
	do { if (mtk_pwrctl_pcie_probe_port) mtk_pwrctl_pcie_probe_port(port); } while(0)
#endif
#define MTK_PCIE_REMOVE_PORT(port) \
	do { if (mtk_pwrctl_pcie_remove_port)  mtk_pwrctl_pcie_remove_port(port); } while(0)
#define MTK_PCIE_SOFT_ON(bus) \
	do { if (mtk_pwrctl_pcie_soft_on) mtk_pwrctl_pcie_soft_on(bus); } while(0)
#define MTK_PCIE_SOFT_OFF(bus) \
	do { if (mtk_pwrctl_pcie_soft_off) mtk_pwrctl_pcie_soft_off(bus); } while(0)

#define MTK_PCIE_DISABLE_REFCLK(port) \
	do { if (mtk_pwrctl_pcie_disable_refclk) mtk_pwrctl_pcie_disable_refclk(port); } while(0)
#define MTK_PCIE_PIN_SELECT(port, state) \
	do { if (mtk_pwrctl_pcie_pinmux_select) mtk_pwrctl_pcie_pinmux_select(port, state); } \
	while(0)
#define MTK_PCIE_DISABLE_DATA_TRANS(port) \
	do { if (mtk_pwrctl_pcie_disable_data_trans) mtk_pwrctl_pcie_disable_data_trans(port);} \
	while(0)

int mtk_pwrctl_request_to_controller(struct pwrctl_mdev *mdev, int type)
{
	struct pci_bus *bus;
	int port_num = PWRCTL_PCIE_PORT_NUM;
	bus = pci_find_bus(port_num, 0);

	switch(type) {
		case PWRCTL_CONTROLLER_REQUEST_SOFT_OFF:
			if (bus)
				MTK_PCIE_SOFT_OFF(bus);
			else
				pr_err("pwrctl: not found pcie bus of port: %d\n", port_num);
			break;
		case PWRCTL_CONTROLLER_REQUEST_SOFT_ON:
			if (bus)
				MTK_PCIE_SOFT_ON(bus);
			else
				pr_err("pwrctl: not found pcie bus of port: %d\n", port_num);
			break;
		case PWRCTL_CONTROLLER_REQUEST_SCAN_PORT:
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
			MTK_PCIE_PROBE_PORT(mdev->pdev, port_num);
#else
			MTK_PCIE_PROBE_PORT(port_num);
#endif
			pr_info("pwrctl: trigger controller probe device done\n");
			break;
		case PWRCTL_CONTROLLER_REQUEST_REMOVE_PORT:
			MTK_PCIE_REMOVE_PORT(port_num);
			pr_info("pwrctl: trigger controller remove device done\n");
			break;
		default:
			break;
	}
	return 0;
}

int mtk_pwrctl_controller_pin_off(struct pwrctl_mdev *mdev)
{
	MTK_PCIE_PIN_SELECT(PWRCTL_PCIE_PORT_NUM, PWRCTL_PCIE_PIN_OFF);
	pr_info("pwrctl: set controller pin off done\n");
	return 0;
}

int mtk_pwrctl_controller_disable_refclk(struct pwrctl_mdev *mdev)
{
	MTK_PCIE_DISABLE_REFCLK(PWRCTL_PCIE_PORT_NUM);
	pr_info("pwrctl: disable controller refclk done\n");
	return 0;
}

int mtk_pwrctl_controller_disable_data_trans(struct pwrctl_mdev *mdev)
{
	MTK_PCIE_DISABLE_DATA_TRANS(PWRCTL_PCIE_PORT_NUM);
	pr_info("pwrctl: disable trans data\n");
	return 0;
}
