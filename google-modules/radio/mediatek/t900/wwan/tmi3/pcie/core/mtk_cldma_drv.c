// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2023, MediaTek Inc.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "mtk_cldma_drv.h"
#include "mtk_debug.h"
#include "mtk_debugfs.h"
#include "mtk_dev.h"
#include "mtk_except.h"
#include "mtk_fsm.h"
#include "mtk_pci.h"
#include "mtk_pci_reg.h"
#ifdef CONFIG_UT_PCIE_CLDMA
#include "ut_cldma_drv.h"
#endif

#define TAG			"CLDMA"
#define WAIT_QUEUE_STOP		(70)
#define REG_SIZE_8_BYTE		(8)
#define REG_SIZE_4_BYTE		(4)
#define DEVICE_SIDE_OFFSET	(0x1000)

void mtk_cldma_drv_dump(struct cldma_drv_info *drv_info)
{
	struct cldma_hw_regs *hw_regs;
	struct mtk_md_dev *mdev;
	u32 base;

	mdev = drv_info->mdev;
	base = drv_info->base_addr;
	hw_regs = drv_info->hw_regs;

	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
		"CLDMA%d Left Side\n", drv_info->hw_id);
	MTK_REGS_DUMP(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP, NULL, base, 0x218 + 4);
	MTK_REGS_DUMP(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
		      NULL, base + 0x400, 0x268 + 4);
	MTK_REGS_DUMP(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
		      NULL, base + 0x800, 0x254 + 4);

	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
		"\nCLDMA%d Right Side\n", drv_info->hw_id);
	MTK_REGS_DUMP(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP, NULL, base + 0x1000, 0x218 + 4);
	MTK_REGS_DUMP(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
		      NULL, base + 0x1000 + 0x400, 0x268 + 4);
	MTK_REGS_DUMP(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
		      NULL, base + 0x1000 + 0x800, 0x254 + 4);
}

void mtk_cldma_drv_init(struct cldma_drv_info *drv_info)
{
	struct cldma_hw_regs *hw_regs;
	struct mtk_md_dev *mdev;
	u32 base, val;

	mdev = drv_info->mdev;
	base = drv_info->base_addr;
	hw_regs = drv_info->hw_regs;

	/* set CLDMA to 64 bit mode GPD */
	val = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_ul_cfg);
	val = (val & (~(0x7 << 5))) | ((0x4) << 5);
	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_ul_cfg, val);

	val = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_so_cfg);
	val = (val & (~(0x7 << 10))) | ((0x4) << 10) | (1 << 2);
	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_so_cfg, val);

	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_rx_work_to_reg_mask_set, ALLQ);
	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_ip_busy_to_pcie_mask_set,
			ALLQ << 16);
	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_ip_busy_to_pcie_mask_clr,
			ALLQ << 24);

	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_ip_busy_to_ap_mask_clr,
			ALLQ << 24);

	/* enable interrupt to PCIe */
	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_int_mask, 0);

	/* disable illegal memory check */
	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_ul_dummy_0, 1);
	mtk_pci_write32(mdev, base + hw_regs->reg_cldma_so_dummy_0, 1);
}

void mtk_cldma_setup_start_addr(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir,
				u32 qno, dma_addr_t addr)
{
	struct cldma_hw_regs *hw_regs;
	u32 addr_l, addr_h, base;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;

	if (dir == DIR_TX) {
		addr_l = base + hw_regs->reg_cldma_ul_start_addrl_0 + qno * REG_SIZE_8_BYTE;
		addr_h = base + hw_regs->reg_cldma_ul_start_addrh_0 + qno * REG_SIZE_8_BYTE;
	} else {
		addr_l = base + hw_regs->reg_cldma_so_start_addrl_0 + qno * REG_SIZE_8_BYTE;
		addr_h = base + hw_regs->reg_cldma_so_start_addrh_0 + qno * REG_SIZE_8_BYTE;
	}

	mtk_pci_write32(drv_info->mdev, addr_l, (u32)addr);
	mtk_pci_write32(drv_info->mdev, addr_h, (u32)((u64)addr >> 32));
}

void mtk_cldma_mask_intr(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir,
			 u32 qno, enum mtk_intr_type type)
{
	struct cldma_hw_regs *hw_regs;
	u32 base, addr, val;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_l2timsr0;
	else
		addr = base + hw_regs->reg_cldma_l2rimsr0;

	if (qno == ALLQ)
		val = qno << type;
	else
		val = BIT(qno) << type;

	mtk_pci_write32(drv_info->mdev, addr, val);
}

void mtk_cldma_unmask_intr(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir,
			   u32 qno, enum mtk_intr_type type)
{
	struct cldma_hw_regs *hw_regs;
	u32 base, addr, val;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_l2timcr0;
	else
		addr = base + hw_regs->reg_cldma_l2rimcr0;

	if (qno == ALLQ)
		val = qno << type;
	else
		val = BIT(qno) << type;

	mtk_pci_write32(drv_info->mdev, addr, val);
}

void mtk_cldma_clr_intr_status(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir,
			       u32 qno, enum mtk_intr_type type)
{
	struct cldma_hw_regs *hw_regs;
	struct mtk_md_dev *mdev;
	u32 base, addr, val;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;
	mdev = drv_info->mdev;

	if (type == QUEUE_ERROR) {
		if (dir == DIR_TX) {
			val = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l3tisar0);
			mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l3tisar0, val);
			val = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l3tisar1);
			mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l3tisar1, val);
			val = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l3tisar2);
			mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l3tisar2, val);
		} else {
			val = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l3risar0);
			mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l3risar0, val);
			val = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l3risar1);
			mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l3risar1, val);
		}
	}

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_l2tisar0;
	else
		addr = base + hw_regs->reg_cldma_l2risar0;

	if (qno == ALLQ)
		val = qno << type;
	else
		val = BIT(qno) << type;

	mtk_pci_write32(mdev, addr, val);
	val = mtk_pci_read32(mdev, addr);
}

u32 mtk_cldma_check_intr_status(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir,
				u32 qno, enum mtk_intr_type type)
{
	struct cldma_hw_regs *hw_regs;
	u32 base, addr, val, sta;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_l2tisar0;
	else
		addr = base + hw_regs->reg_cldma_l2risar0;

	val = mtk_pci_read32(drv_info->mdev, addr);
	if (val == LINK_ERROR_VAL)
		sta = val;
	else if (qno == ALLQ)
		sta = (val >> type) & 0xFF;
	else
		sta = (val >> type) & BIT(qno);

	return sta;
}

void mtk_cldma_start_queue(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir, u32 qno)
{
	struct cldma_hw_regs *hw_regs;
	u32 val = BIT(qno);
	u32 base, addr;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_ul_start_cmd;
	else
		addr = base + hw_regs->reg_cldma_so_start_cmd;

	mtk_pci_write32(drv_info->mdev, addr, val);
}

void mtk_cldma_resume_queue(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir, u32 qno)
{
	struct cldma_hw_regs *hw_regs;
	u32 val = BIT(qno);
	u32 base, addr;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_ul_resume_cmd;
	else
		addr = base + hw_regs->reg_cldma_so_resume_cmd;

	mtk_pci_write32(drv_info->mdev, addr, val);
}

u32 mtk_cldma_queue_status(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir, u32 qno)
{
	struct cldma_hw_regs *hw_regs;
	u32 base, addr, val;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_ul_status;
	else
		addr = base + hw_regs->reg_cldma_so_status;

	val = mtk_pci_read32(drv_info->mdev, addr);

	if (qno == ALLQ || val == LINK_ERROR_VAL)
		return val;

	return val & BIT(qno);
}

u32 mtk_cldma_stop_queue(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir, u32 qno)
{
	u32 val = (qno == ALLQ) ? qno : BIT(qno);
	struct cldma_hw_regs *hw_regs;
	u32 base, addr;
	int cnt = 0;
	u32 active;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_ul_stop_cmd;
	else
		addr = base + hw_regs->reg_cldma_so_stop_cmd;

	mtk_pci_write32(drv_info->mdev, addr, val);

	do {
		active = drv_info->drv_ops->cldma_queue_status(drv_info, dir, qno);
		if (active == LINK_ERROR_VAL || !active)
			break;
		usleep_range(WAIT_QUEUE_STOP, 2 * WAIT_QUEUE_STOP);
	} while (++cnt < 10);

	return active;
}

void mtk_cldma_clear_ip_busy(struct cldma_drv_info *drv_info)
{
	mtk_pci_write32(drv_info->mdev, drv_info->base_addr +
			drv_info->hw_regs->reg_cldma_ip_busy, 0x01);
}

void mtk_cldma_get_intr_status(struct cldma_drv_info *drv_info, u32 *tx_sta, u32 *rx_sta)
{
	struct cldma_hw_regs *hw_regs;
	struct mtk_md_dev *mdev;
	u32 tx_mask, rx_mask;
	u32 base;

	mdev = drv_info->mdev;
	base = drv_info->base_addr;
	hw_regs = drv_info->hw_regs;

	*tx_sta = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l2tisar0);
	tx_mask = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l2timr0);
	*rx_sta = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l2risar0);
	rx_mask = mtk_pci_read32(mdev, base + hw_regs->reg_cldma_l2rimr0);

	*tx_sta = (*tx_sta) & (~tx_mask);
	*rx_sta = (*rx_sta) & (~rx_mask);

	if (*tx_sta) {
		/* TX XFER_DONE and QUEUE_ERROR mask */
		mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l2timsr0, *tx_sta);
		/* TX XFER_DONE clear */
		mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l2tisar0,
				(*tx_sta) & (0xFF << QUEUE_XFER_DONE));
	}

	if (*rx_sta) {
		/* RX XFER_DONE and QUEUE_ERROR mask */
		mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l2rimsr0, *rx_sta);
		/* RX XFER_DONE clear */
		mtk_pci_write32(mdev, base + hw_regs->reg_cldma_l2risar0,
				(*rx_sta) & (0xFF << QUEUE_XFER_DONE));
	}
}

u32 mtk_cldma_get_tx_start_addr(struct cldma_drv_info *drv_info, u32 qno)
{
	u32 addr, val;

	addr = drv_info->base_addr + drv_info->hw_regs->reg_cldma_ul_start_addrl_0 +
	       qno * REG_SIZE_8_BYTE;
	val = mtk_pci_read32(drv_info->mdev, addr);

	return val;
}

u64 mtk_cldma_get_curr_addr(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir, u32 qno)
{
	struct cldma_hw_regs *hw_regs;
	u32 curr_addr_h, curr_addr_l;
	struct mtk_md_dev *mdev;
	u32 base, addr;
	u64 curr_addr;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;
	mdev = drv_info->mdev;

	if (dir == DIR_TX) {
		addr = base + hw_regs->reg_cldma_ul_current_addrh_0 + qno * REG_SIZE_8_BYTE;
		curr_addr_h = mtk_pci_read32(mdev, addr);
		addr = base + hw_regs->reg_cldma_ul_current_addrl_0 + qno * REG_SIZE_8_BYTE;
		curr_addr_l = mtk_pci_read32(mdev, addr);
	} else {
		addr = base + hw_regs->reg_cldma_so_current_addrh_0 + qno * REG_SIZE_8_BYTE;
		curr_addr_h = mtk_pci_read32(mdev, addr);
		addr = base + hw_regs->reg_cldma_so_current_addrl_0 + qno * REG_SIZE_8_BYTE;
		curr_addr_l = mtk_pci_read32(mdev, addr);
	}
	curr_addr = ((u64)curr_addr_h << 32) | curr_addr_l;
	if (curr_addr_h == LINK_ERROR_VAL && curr_addr_l == LINK_ERROR_VAL)
		curr_addr = 0;
	return curr_addr;
}

u32 mtk_cldma_get_gpd_cnt(struct cldma_drv_info *drv_info, enum mtk_tx_rx dir, u32 qno)
{
	struct cldma_hw_regs *hw_regs;
	struct mtk_md_dev *mdev;
	u32 base, addr, val;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;
	mdev = drv_info->mdev;

	if (dir == DIR_TX)
		addr = base + hw_regs->reg_cldma_tq1_done_cnt + (qno >> 1) * REG_SIZE_4_BYTE;
	else
		addr = base + hw_regs->reg_cldma_rq1_done_cnt + (qno >> 1) * REG_SIZE_4_BYTE;
	val = mtk_pci_read32(mdev, addr);
	/* Max value is 0xFFFEFFFE */
	if (val == LINK_ERROR_VAL)
		return val;
	else if (qno & BIT(0))
		return val >> 16;
	else
		return val & U16_MAX;
}

u32 mtk_cldma_check_device_rx_status(struct cldma_drv_info *drv_info, u32 qno)
{
	struct cldma_hw_regs *hw_regs;
	struct mtk_md_dev *mdev;
	u32 base, addr, val;

	hw_regs = drv_info->hw_regs;
	base = drv_info->base_addr;
	mdev = drv_info->mdev;

	addr = base + DEVICE_SIDE_OFFSET + hw_regs->reg_cldma_so_status;
	val = mtk_pci_read32(mdev, addr);
	if (qno == ALLQ || val == LINK_ERROR_VAL)
		return val;

	return val & BIT(qno);
}

