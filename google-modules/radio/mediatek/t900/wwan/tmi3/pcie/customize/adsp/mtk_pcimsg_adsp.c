// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */
#if IS_ENABLED(CONFIG_MTK_AUDIODSP_SUPPORT)
#include <adsp_helper.h>
#include <adsp_ipi_queue.h>
#include <audio_ipi_platform_common.h>
#include <pcie-mediatek-gen3.h>
#endif
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "mtk_debug.h"
#include "mtk_dev.h"
#include "mtk_except.h"
#include "mtk_fsm.h"
#include "mtk_pci.h"
#include "mtk_pcimsg.h"

#ifdef CONFIG_TX00_UT_PCIMSG
#include "ut_mtk_pcimsg_adsp.h"
#endif

#define TAG							("IPI")
#define ADSP_IPI_PCIE				(32)
#define MAX_IPI_MSG_PAYLOAD_SIZE	(104)
#define ADSP_MEM_ID					(9)
#define INVALID_BAR_ADDR			(0xFFFFFFFFFFFFFFFF)

struct ipi_pci_msg_t {
	u32 op_id;
	u32 payload_size;
	u8 payload[MAX_IPI_MSG_PAYLOAD_SIZE];
};

struct adsp_ipi_t {
	int ipi_id;
	struct mtk_md_dev *mdev;
};

static struct adsp_ipi_t *adsp_ipi_saved;

static void mtk_pcimsg_recv_msg_from_user(int ipi_id, void *data, unsigned int len)
{
	struct ipi_pci_msg_t *ipi_msg = data;
	struct mtk_md_dev *mdev;
	struct mtk_md_fsm *fsm;

	if (!adsp_ipi_saved)
		return;

	mdev = adsp_ipi_saved->mdev;
	fsm = mdev->fsm;
	MTK_INFO(mdev, "recevice IPI message: 0x%x\n", ipi_msg->op_id);

	switch (ipi_msg->op_id) {
	case MTK_PCIMSG_C2H_REQ_BAR:
		mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_BAR);
		break;
	case MTK_PCIMSG_C2H_EXCEPT:
		mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_EXCEPT_ACK);
		mtk_exception_report_evt(mdev, EXCEPTION_LINK_ERR);
		break;
	case MTK_PCIMSG_C2H_REQ_SMEM:
		mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_SMEM);
		break;
	case MTK_PCIMSG_C2H_REQ_STATE:
		if (fsm->state == FSM_STATE_OFF)
			mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_EXCEPT);
		else
			mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_NORMAL);
		break;
	default:
		break;
	}
}

int mtk_pcimsg_send_msg_to_user(struct mtk_md_dev *mdev, int msg_id)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_md_fsm *fsm = mdev->fsm;
	struct ipi_pci_msg_t ipi_msg;

	switch (msg_id) {
	case MTK_PCIMSG_H2C_READY:
	case MTK_PCIMSG_H2C_EXCEPT:
	case MTK_PCIMSG_H2C_EXCEPT_ACK:
	case MTK_PCIMSG_H2C_NORMAL:
		ipi_msg.op_id = msg_id;
		ipi_msg.payload_size = 0;
		break;
	case MTK_PCIMSG_H2C_BAR:
		ipi_msg.op_id = MTK_PCIMSG_H2C_BAR;
		ipi_msg.payload_size = 16;
		if (fsm->state == FSM_STATE_OFF) {
			*((uint64_t *)ipi_msg.payload) = INVALID_BAR_ADDR;
			*(((uint64_t *)ipi_msg.payload) + 1) = INVALID_BAR_ADDR;
		} else {
			*((uint64_t *)ipi_msg.payload) = pdev->resource[0].start;
			*(((uint64_t *)ipi_msg.payload) + 1) = pdev->resource[2].start;
			*(((uint64_t *)ipi_msg.payload) + 1) -= ATR_PCIE_REG_TRSL_ADDR;
		}
		break;
	case MTK_PCIMSG_H2C_SMEM:
		ipi_msg.op_id = MTK_PCIMSG_H2C_SMEM;
		ipi_msg.payload_size = 16;
		*((uint64_t *)ipi_msg.payload) = (uint64_t)adsp_get_reserve_mem_phys(ADSP_MEM_ID);
		*(((uint64_t *)ipi_msg.payload) + 1) =
			(uint64_t)adsp_get_reserve_mem_size(ADSP_MEM_ID);
		break;
	default:
		break;
	}

	adsp_register_feature(PCIE_FEATURE_ID);
	dsp_send_msg_to_queue(AUDIO_OPENDSP_USE_HIFI3_A, ADSP_IPI_PCIE,
			      &ipi_msg, sizeof(ipi_msg), 10);
	dsp_send_msg_to_queue(AUDIO_OPENDSP_USE_HIFI3_B, ADSP_IPI_PCIE,
			      &ipi_msg, sizeof(ipi_msg), 10);
	adsp_deregister_feature(PCIE_FEATURE_ID);

	MTK_INFO(mdev, "send IPI message: 0x%x\n", msg_id);

	return 0;
}

int mtk_pcimsg_messenger_init(struct mtk_md_dev *mdev)
{
	int err = 0;
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct pci_dev *parent = pdev->bus->self;
	struct irq_data *cldma2_irq_data;
	struct mtk_pci_priv *pci_priv;
	struct irq_data *ado_irq_data;
	struct adsp_ipi_t *adsp_ipi;
	uint32_t config = 0;
	int irqn;
	int i;

	pci_priv = mdev->hw_priv;
	adsp_ipi = devm_kzalloc(mdev->dev, sizeof(*adsp_ipi), GFP_KERNEL);
	if (!adsp_ipi) {
		MTK_ERR(mdev, "Failed to allocate memory for adsp ipi\n");
		return -ENOMEM;
	}

	adsp_ipi->ipi_id = ADSP_IPI_PCIE;
	adsp_ipi->mdev = mdev;

	err = adsp_ipi_registration(ADSP_IPI_PCIE,
				    mtk_pcimsg_recv_msg_from_user, "PCIe");
	if (err) {
		MTK_ERR(mdev, "Failed to register ipi message, err:%d\n", err);
		goto free_mem;
	}

	adsp_ipi_saved = adsp_ipi;
	pci_priv->messenger = adsp_ipi;

	irqn = pci_irq_vector(pdev, mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_CLDMA2));
	if (irqn < 0) {
		MTK_ERR(mdev, "Failed to get CLDMA2 IRQ number,irqn:%d\n", irqn);
		goto free_mem;
	}

	cldma2_irq_data = irq_get_irq_data(irqn);
	if (!cldma2_irq_data)
	{
		MTK_ERR(mdev, "CLDMA2 IRQ data is NULL\n");
		goto free_mem;
	}

	irqn = pci_irq_vector(to_pci_dev(mdev->dev), mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_ADO));
	if (irqn < 0) {
		MTK_ERR(mdev, "Failed to get AUDIO IRQ number,irqn:%d\n", irqn);
		goto free_mem;
	}
	ado_irq_data = irq_get_irq_data(irqn);
	if (!ado_irq_data)
	{
		MTK_ERR(mdev, "AUDIO IRQ data is NULL\n");
		goto free_mem;
	}

	irq_chip_mask_parent(cldma2_irq_data);
	irq_chip_mask_parent(ado_irq_data);

	pci_read_config_dword(parent, 0x490, &config);
	config &= GENMASK(3, 2);

	if (!config)
		config = BIT(1);

	for (i = 1; i < 4; i++) {
		if (config & BIT(i)) {
			mtk_msi_unmask_to_other_mcu(cldma2_irq_data, i);
			mtk_msi_unmask_to_other_mcu(ado_irq_data, i);
		}
	}

	mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_READY);

	MTK_INFO(mdev, "PCIe messenger init done, config=0x%x\n", config);

	return 0;

free_mem:
	adsp_ipi_saved = NULL;
	pci_priv->messenger = NULL;
	devm_kfree(mdev->dev, adsp_ipi);

	return err;
}

int mtk_pcimsg_messenger_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *pci_priv;
	struct adsp_ipi_t *adsp_ipi;

	pci_priv = mdev->hw_priv;
	adsp_ipi = pci_priv->messenger;

	if (!adsp_ipi)
		return 0;
	adsp_ipi_unregistration(ADSP_IPI_PCIE);
	devm_kfree(mdev->dev, adsp_ipi);
	pci_priv->messenger = NULL;
	adsp_ipi_saved = NULL;

	return 0;
}

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
bool mtk_pcimsg_pci_user_is_busy(struct mtk_md_dev *mdev, __maybe_unused bool is_suspend)
#else
bool mtk_pcimsg_pci_user_is_busy(struct mtk_md_dev *mdev)
#endif
{
	bool is_busy = false;
	is_busy = mtk_pcie_in_use(MTK_PCIE_PORT_NUM);
	return is_busy;
}

int mtk_pcimsg_wait_pci_user_inactive(struct mtk_md_dev *mdev)
{
	int count = 0;

	do {
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		if (likely(!mtk_pcimsg_pci_user_is_busy(mdev, true)))
#else
		if (likely(!mtk_pcimsg_pci_user_is_busy(mdev)))
#endif
			break;
		msleep(1);
		count++;
	} while (count < 2);

	if (count == 2)
		MTK_WARN(mdev, "External PCI user is busy!\n");

	return 0;
}
