/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cpif shim layer for google SOC PCIE
 *
 * Copyright 2023, Google LLC
 *
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/iommu.h>
#include <linux/dma-map-ops.h>
#include "modem_v1.h"
#include "cpif_pcie_shim_google.h"

#define MSI0_MASK_REG 0x0c80082c
#define MSI0_STATUS_REG 0x0c800830
#define MSI1_MASK_REG 0x0c800838
#define MSI1_STATUS_REG 0x0c80083c
#define MSI2_MASK_REG 0x0c800844
#define MSI2_STATUS_REG 0x0c800848
#define MSI3_MASK_REG 0x0c800850
#define MSI3_STATUS_REG 0x0c800854
#define MSI4_MASK_REG 0x0c80085c
#define MSI4_STATUS_REG 0x0c800860

void pcie_unimplemented_return_void(const char *s) {
	mif_debug("%s() has not been implemented\n", s);
}

int pcie_unimplemented_return_int_0(const char *s) {
	mif_debug("%s() has not been implemented\n", s);
	return 0;
}

static void cpif_pcie_callback(enum google_pcie_callback_type type, void *priv)
{
	pcie_register_event_t *reg = priv;
	enum pcie_event cpif_event_type;

	/* map from google_pcie_callback_type to cpif pcie_event type*/
	switch (type) {
	case GPCIE_CB_CPL_TIMEOUT:
		cpif_event_type = PCIE_EVENT_CPL_TIMEOUT;
		break;
	case GPCIE_CB_LINK_DOWN:
		cpif_event_type = PCIE_EVENT_LINKDOWN;
		break;
	default:
		cpif_event_type = PCIE_EVENT_INVALID;
		break;
	}

	if (reg && reg->callback && (reg->mode == PCIE_TRIGGER_CALLBACK) &&
	    (reg->events & cpif_event_type)) {
		struct pci_dev *pci_dev = reg->user;
		pcie_notify_t *notify = &reg->notify;

		notify->event = cpif_event_type;
		notify->user = reg->user;
		dev_info(&pci_dev->dev, "Callback for event %#x\n", notify->event);
		reg->callback(notify);
	}
}

int pcie_register_event(pcie_register_event_t *reg)
{
	if (reg && reg->user && reg->user->bus)
		return google_pcie_register_callback(reg->user->bus->domain_nr,
						     cpif_pcie_callback, reg);

	mif_err("Unable to determine PCIE controller\n");
	return -EINVAL;
}

int pcie_unregister_event(pcie_register_event_t *reg)
{
	if (reg && reg->user && reg->user->bus)
		return google_pcie_unregister_callback(reg->user->bus->domain_nr);

	mif_err("Unable to determine PCIE controller\n");
	return -EINVAL;
}

int pcie_l1ss_ctrl(int aspm_state, int ch_num)
{
	int ret;
	struct pci_bus *pci_bus;
	struct pci_dev *pci_dev;

	pci_bus = pci_find_bus(ch_num, 1);
	if (!pci_bus) {
		mif_err("Child bus-1 for channel %d not found\n", ch_num);
		return -ENODEV;
	}

	pci_dev = pci_get_slot(pci_bus, PCI_DEVFN(0, 0));
	if (!pci_dev) {
		mif_err("Endpoint device for channel %d not found\n", ch_num);
		return -ENODEV;
	}

	ret = pci_enable_link_state(pci_dev, aspm_state);
	pci_dev_put(pci_dev);
	return ret;
}

static void __check_pending_msi(struct modem_ctl *mc, u32 mask_reg, u32 status_reg, int num)
{
	u32 __iomem *reg_ptr;
	u32 mask, status;

	reg_ptr = ioremap(mask_reg, SZ_4);
	if (!reg_ptr) {
		mif_err("ioremap(%#x) failed!\n", mask_reg);
		return;
	}
	mask = ioread32(reg_ptr);
	iounmap(reg_ptr);

	reg_ptr = ioremap(status_reg, SZ_4);
	if (!reg_ptr) {
		mif_err("ioremap(%#x) failed!\n", status_reg);
		return;
	}
	status = ioread32(reg_ptr);
	iounmap(reg_ptr);

	if (status & ~mask) {
		mif_err("MSI%d Pending (STATUS_REG == %#x)!\n", num, status);
		mc->msi_missed[num]++;
	}
}

void pcie_check_pending_msi(struct modem_ctl *mc)
{
	__check_pending_msi(mc, MSI0_MASK_REG, MSI0_STATUS_REG, 0);
	__check_pending_msi(mc, MSI1_MASK_REG, MSI1_STATUS_REG, 1);
	__check_pending_msi(mc, MSI2_MASK_REG, MSI2_STATUS_REG, 2);
	__check_pending_msi(mc, MSI3_MASK_REG, MSI3_STATUS_REG, 3);
	__check_pending_msi(mc, MSI4_MASK_REG, MSI4_STATUS_REG, 4);
}

#if IS_ENABLED(CONFIG_LINK_DEVICE_PCIE_IOMMU)
inline bool pcie_is_sysmmu_enabled(int ch_num)
{
	/*
	 * Lassen controls the sysmmu using a device tree property. But
	 * it is always enabled for LGA.
	 */
	return false;
}

void cpif_iommu_tlb_invalidate_all(struct modem_ctl *mc)
{
	/* This is automatically done for LGA */
}

int cpif_iommu_map(unsigned long iova, phys_addr_t paddr, size_t size,
			  int prot, struct modem_ctl *mc)
{
	if (!mc->s51xx_pdev) {
		mif_info("mc->s51xx_pdev is NULL!\n");
		return 0;
	}

	if (dev_is_dma_coherent(&mc->s51xx_pdev->dev))
		prot = IOMMU_READ | IOMMU_WRITE | IOMMU_CACHE;
	else
		prot = IOMMU_READ | IOMMU_WRITE;

	return iommu_map(iommu_get_domain_for_dev(&mc->s51xx_pdev->dev),
			 iova, paddr, size, prot, GFP_KERNEL);
}
size_t cpif_iommu_unmap(unsigned long iova, size_t size, struct modem_ctl *mc)
{
	if (!mc->s51xx_pdev) {
		mif_info("mc->s51xx_pdev is NULL!\n");
		return 0;
	}

	return iommu_unmap(iommu_get_domain_for_dev(&mc->s51xx_pdev->dev), iova, size);
}
#endif
