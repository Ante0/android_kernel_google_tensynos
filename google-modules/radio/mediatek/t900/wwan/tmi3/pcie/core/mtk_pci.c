// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/acpi.h>
#include <linux/aer.h>
#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>

#include "mtk_build_info.h"
#include "mtk_debug.h"
#include "mtk_dev.h"
#include "mtk_devlink.h"
#include "mtk_dpmaif.h"
#include "mtk_except.h"
#include "mtk_frc.h"
#include "mtk_memlog.h"
#include "mtk_pci.h"
#include "mtk_pci_dev_cfg.h"
#include "mtk_pci_reg.h"
#include "mtk_pcimsg.h"
#include "mtk_pm.h"
#include "mtk_port.h"
#include "mtk_port_io.h"
#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
#include "mtk_pwrctl.h"
#endif
#include "mtk_statistics.h"
#include "mtk_trans_ctrl.h"
#include "mtk_utility_cfg.h"
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
#include "pcie-mediatek-gen3.h"
#endif
#ifdef CONFIG_UT_PCIE_PCIE
#include "ut_pci_fake.h"
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "common/radio-google.h"
#include "config/aspm-config.h"
#include "pcie/link-exception.h"
#include "pcie/mtk-pcie.h"
#include "mtk_google.h"
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
#include "pcie/md2ap-wakemon.h"
#endif

#define TAG "PCI"
#define BAR_NUM		6
#define MTK_PCI_DUMP_TO_LOG_ONCE_PERIOD		5

#define PORT1 1
#define MTK_PCI_TRANSPARENT_ATR_SIZE  (0x3F)
#define MTK_PCI_MINIMUM_ATR_SIZE	(0x1000)
#define SNOOP_LATENCY  0x1003
#define LE32_TO_U32(x) ((__force u32)(__le32)(x))
#define SET_HW_BITS(dest, chs, mhccif, dev)		\
	({						\
		if ((chs) & (dev))					\
			(dest) |= FIELD_PREP(mhccif, 1);		\
	})

#define MTK_PCI_REG_DUMP(mdev, flag, dbg_mask, rg_id, fmt, args...) \
	mtk_pci_reg_dump(mdev, flag, dbg_mask, rg_id, "[%s][%s][%d]:" fmt, \
		TAG, __func__, __LINE__, ##args)
#define mdev_get_pm(mdev) (((struct mtk_pci_priv *)((mdev)->hw_priv))->pm)

struct mtk_mhccif_cb {
	struct list_head entry;
	int (*evt_cb)(u32 status, void *data);
	void *data;
	u32 chs;
};

static inline void mtk_pci_aspm_ctrl(struct mtk_md_dev *mdev);

struct sock *mtk_netlink_sock;

static unsigned long last_log_time;
static int mtk_pci_irq_cnt_max = MTK_IRQ_CNT_MAX;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
static int mtk_pci_irq_type = PCI_IRQ_MSIX | PCI_IRQ_LEGACY;
#else
static int mtk_pci_irq_type = PCI_IRQ_MSIX | PCI_IRQ_INTX;
#endif
static u32 mtk_pci_stress_test_loop = 1;
static u32 mtk_pci_config_atr_test_win = 1;
static u32 mtk_pci_config_atr_test_table = 6;
static u32 mtk_pci_stress_test_delay_us;
static bool mtk_pci_stress_test_enable;
static bool mtk_pci_stress_test_log;
static u32 mtk_pci_read_test_res;

#ifdef CONFIG_PCIEASPM
#if IS_ENABLED(CONFIG_GOOGLE_ASPM_CONTROL)
static unsigned short mtk_pci_link_state_init = GOOGLE_PCIE_ASPM_LINK_STATE;
#else
/* BIT(0): Enable ASPM L0s in PCI_EXP_LINKCTL
 * BIT(1): Enable ASPM L1 in PCI_EXP_LINKCTL
 * BIT(2): Enable PCI PM L1.2 in PCI_L1SS_CTL1
 * BIT(3): Enable PCI PM L1.1 in PCI_L1SS_CTL1
 * BIT(4): Enable ASPM L1.2 in PCI_L1SS_CTL1
 * BIT(5): Enable ASPM L1.1 in PCI_L1SS_CTL1
 */
static char mtk_pci_link_state_init = 0xFF;
#endif /* CONFIG_GOOGLE_ASPM_CONTROL */
#endif

/* This table records which bits of the interrupt status register each interrupt corresponds to
 * when there are different numbers of msix interrupts.
 */
static const u32 mtk_msix_bits_map[MTK_IRQ_CNT_MAX / 2][5] = {
	{0xFFFFFFFF, 0x55555555, 0x11111111, 0x01010101, 0x00010001},
	{0x00000000, 0xAAAAAAAA, 0x22222222, 0x02020202, 0x00020002},
	{0x00000000, 0x00000000, 0x44444444, 0x04040404, 0x00040004},
	{0x00000000, 0x00000000, 0x88888888, 0x08080808, 0x00080008},
	{0x00000000, 0x00000000, 0x00000000, 0x10101010, 0x00100010},
	{0x00000000, 0x00000000, 0x00000000, 0x20202020, 0x00200020},
	{0x00000000, 0x00000000, 0x00000000, 0x40404040, 0x00400040},
	{0x00000000, 0x00000000, 0x00000000, 0x80808080, 0x00800080},
	{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x01000100},
	{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x02000200},
	{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x04000400},
	{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x08000800},
	{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10001000},
	{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x20002000},
	{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x40004000},
	{0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x80008000},
};

static void mtk_pci_reg_dump(struct mtk_md_dev *mdev, bool flag,
			     enum mtk_debug_mask dbg_mask, enum mtk_memlog_region_id rg_id,
			     const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	if (flag)
		MTK_INFO(mdev, "%pV", &vaf);
	else
		MTK_DBG(mdev, dbg_mask, rg_id, "%pV", &vaf);

	va_end(args);
}

u32 mtk_pci_mac_read32(struct mtk_pci_priv *priv, u64 addr)
{
	return ioread32(priv->mac_reg_base + addr);
}

void mtk_pci_mac_write32(struct mtk_pci_priv *priv, u64 addr, u32 val)
{
	iowrite32(val, priv->mac_reg_base + addr);
}

#if IS_ENABLED(CONFIG_GOOGLE_ATR_WRITE_RETRY)
void mtk_pci_mac_write32_with_retry(struct mtk_pci_priv *priv, u64 addr, u32 val, u32 retry_count)
{
	u32 read_val;
	u32 i = 0;

	do {
		iowrite32(val, priv->mac_reg_base + addr);
		read_val = ioread32(priv->mac_reg_base + addr);
		if (read_val == val)
			return;
		MTK_WARN(priv->mdev, "Failed to write MAC reg 0x%llx: val 0x%x, read 0x%x\n",
			 addr, val, read_val);
	} while (i++ < retry_count);
}
#endif

u32 mtk_pci_read32(struct mtk_md_dev *mdev, u64 addr)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return ioread32(priv->ext_reg_base + addr);
}

void mtk_pci_write32(struct mtk_md_dev *mdev, u64 addr, u32 val)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	iowrite32(val, priv->ext_reg_base + addr);
}

static u32 mtk_pci_read32_bar45(struct mtk_md_dev *mdev, u64 addr)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return ioread32(priv->bar45_addr + addr);
}

static void mtk_pci_write32_bar45(struct mtk_md_dev *mdev, u64 addr, u32 val)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	iowrite32(val, priv->bar45_addr + addr);
}

int mtk_pci_setup_atr(struct mtk_md_dev *mdev, struct mtk_atr_cfg *cfg)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 addr, val, size_h, size_l;
	int atr_size, pos, offset;

	if (cfg->transparent) {
		atr_size = MTK_PCI_TRANSPARENT_ATR_SIZE; /* No address conversion is performed */
	} else {
		if (cfg->size < MTK_PCI_MINIMUM_ATR_SIZE) {
			MTK_WARN(mdev, "Invalid atr size %llx, config ATR with minimum size 0x%x\n",
				 cfg->size, MTK_PCI_MINIMUM_ATR_SIZE);
			cfg->size = MTK_PCI_MINIMUM_ATR_SIZE;
		}

		if (cfg->src_addr & (cfg->size - 1)) {
			MTK_ERR(mdev, "Invalid atr src addr is not aligned to size\n");
			return -EFAULT;
		}
		if (cfg->trsl_addr & (cfg->size - 1)) {
			MTK_ERR(mdev, "Invalid atr trsl addr is not aligned to size, %llx, %llx\n",
				cfg->trsl_addr, cfg->size - 1);
			return -EFAULT;
		}
		size_l = FIELD_GET(GENMASK_ULL(31, 0), cfg->size);
		size_h = FIELD_GET(GENMASK_ULL(63, 32), cfg->size);
		pos = ffs(size_l);
		if (pos) {
			/* Address Translate Space Size is equal to 2^(atr_size+1).
			 * "-2" means "-1-1", the first "-1" is because of the atr_size register,
			 * the second is because of the ffs() will increase by one.
			 */
			atr_size = pos - 2;
		} else {
			pos = ffs(size_h);
			/* "+30" means "+32-1-1", the meaning of "-1-1" is same as above,
			 * "+32" is because atr_size is large, exceeding 32-bits.
			 */
			atr_size = pos + 30;
		}
	}

	/* Calculate table offset */
	offset = ATR_PORT_OFFSET * cfg->port + ATR_TABLE_OFFSET * cfg->table;
	/* SRC_ADDR_H */
	addr = REG_ATR_PCIE_WIN0_T0_SRC_ADDR_MSB + offset;
	val = (u32)(cfg->src_addr >> 32);
#if IS_ENABLED(CONFIG_GOOGLE_ATR_WRITE_RETRY)
	mtk_pci_mac_write32_with_retry(priv, addr, val, 3);
#else
	mtk_pci_mac_write32(priv, addr, val);
#endif
	/* SRC_ADDR_L */
	addr = REG_ATR_PCIE_WIN0_T0_SRC_ADDR_LSB + offset;
	val = (u32)(cfg->src_addr & 0xFFFFF000) | (atr_size << 1) | 0x1;
#if IS_ENABLED(CONFIG_GOOGLE_ATR_WRITE_RETRY)
	mtk_pci_mac_write32_with_retry(priv, addr, val, 3);
#else
	mtk_pci_mac_write32(priv, addr, val);
#endif

	/* TRSL_ADDR_H */
	addr = REG_ATR_PCIE_WIN0_T0_TRSL_ADDR_MSB + offset;
	val = (u32)(cfg->trsl_addr >> 32);
#if IS_ENABLED(CONFIG_GOOGLE_ATR_WRITE_RETRY)
	mtk_pci_mac_write32_with_retry(priv, addr, val, 3);
#else
	mtk_pci_mac_write32(priv, addr, val);
#endif
	/* TRSL_ADDR_L */
	addr = REG_ATR_PCIE_WIN0_T0_TRSL_ADDR_LSB + offset;
	val = (u32)(cfg->trsl_addr & 0xFFFFF000);
#if IS_ENABLED(CONFIG_GOOGLE_ATR_WRITE_RETRY)
	mtk_pci_mac_write32_with_retry(priv, addr, val, 3);
#else
	mtk_pci_mac_write32(priv, addr, val);
#endif

	/* TRSL_PARAM */
	addr = REG_ATR_PCIE_WIN0_T0_TRSL_PARAM + offset;
	val = (cfg->trsl_param << 16) | cfg->trsl_id;
#if IS_ENABLED(CONFIG_GOOGLE_ATR_WRITE_RETRY)
	mtk_pci_mac_write32_with_retry(priv, addr, val, 3);
#else
	mtk_pci_mac_write32(priv, addr, val);
#endif

	return 0;
}

#if IS_ENABLED(CONFIG_GOOGLE_B495424578_DEBUG)
void mtk_pci_dump_atr_bar23(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	MTK_INFO(mdev, "ATR table dump for BAR23\n");
	MTK_INFO(mdev, "REG_ATR_PCIE_WIN0_T1_SRC_ADDR_LSB: 0x%x\n",
		 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_SRC_ADDR_LSB));
	MTK_INFO(mdev, "REG_ATR_PCIE_WIN0_T1_SRC_ADDR_MSB: 0x%x\n",
		 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_SRC_ADDR_MSB));
	MTK_INFO(mdev, "REG_ATR_PCIE_WIN0_T1_TRSL_ADDR_LSB: 0x%x\n",
		 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_TRSL_ADDR_LSB));
	MTK_INFO(mdev, "REG_ATR_PCIE_WIN0_T1_TRSL_ADDR_MSB: 0x%x\n",
		 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_TRSL_ADDR_MSB));
	MTK_INFO(mdev, "REG_ATR_PCIE_WIN0_T1_TRSL_PARAM: 0x%x\n",
		 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_TRSL_PARAM));
}

void mtk_pci_dump_atr(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int reg;

	MTK_INFO(mdev, "---Dump ATR---\n");
	for (reg = 0x600; reg < 0x700; reg += 0x20)
		MTK_INFO(mdev, "0x%x: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", reg,
			 mtk_pci_mac_read32(priv, reg), mtk_pci_mac_read32(priv, reg + 0x4),
			 mtk_pci_mac_read32(priv, reg + 0x8), mtk_pci_mac_read32(priv, reg + 0xC),
			 mtk_pci_mac_read32(priv, reg + 0x10), mtk_pci_mac_read32(priv, reg + 0x14),
			 mtk_pci_mac_read32(priv, reg + 0x18),
			 mtk_pci_mac_read32(priv, reg + 0x1C));
}
#endif

void mtk_pci_atr_disable(struct mtk_pci_priv *priv)
{
	int port, tbl, offset;
	u32 val;

	/* Disable all ATR table for all ports */
	for (port = ATR_SRC_PCI_WIN0; port <= ATR_SRC_AXIS_3; port++)
		for (tbl = 0; tbl < ATR_TABLE_NUM_PER_ATR; tbl++) {
			/* Calculate table offset */
			offset = ATR_PORT_OFFSET * port + ATR_TABLE_OFFSET * tbl;
			val = mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T0_SRC_ADDR_LSB + offset);
			val = val & (~BIT(0));
			/* Disable table by SRC_ADDR_L */
#if IS_ENABLED(CONFIG_GOOGLE_ATR_WRITE_RETRY)
			mtk_pci_mac_write32_with_retry(
				priv, REG_ATR_PCIE_WIN0_T0_SRC_ADDR_LSB + offset, val, 3);
#else
			mtk_pci_mac_write32(priv, REG_ATR_PCIE_WIN0_T0_SRC_ADDR_LSB + offset, val);
#endif
		}
}

void mtk_pci_dump_atr_doorbell(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	MTK_INFO(mdev, "ATR doorbell: 0x%x, 0x%x, 0x%x, 0x%x\n",
		 mtk_pci_mac_read32(priv, REG_ISTATUS_A_ADT_SLV0),
		 mtk_pci_mac_read32(priv, REG_ISTATUS_A_ADT_SLV1),
		 mtk_pci_mac_read32(priv, REG_ISTATUS_A_ADT_SLV2),
		 mtk_pci_mac_read32(priv, REG_ISTATUS_A_ADT_SLV3));
}

void mtk_pci_clear_atr_doorbell(struct mtk_md_dev *mdev)
{
	mtk_pci_dump_atr_doorbell(mdev);
	mtk_pci_mac_write32(mdev->hw_priv, REG_ISTATUS_HOST, A_ATR_EVT_DOORBELL);
	mtk_pci_dump_atr_doorbell(mdev);
}

static void mtk_pci_set_msi_merged(struct mtk_pci_priv *priv, int irq_cnt)
{
	u32 reg;

	reg = mtk_pci_mac_read32(priv, REG_PCIE_CFG_MSI_MERGED);
	reg &= (~MTK_PCI_MSI_MESSAGE_CONTROL);
	reg |= ((ffs(irq_cnt) - 1) << 4);
	mtk_pci_mac_write32(priv, REG_PCIE_CFG_MSI_MERGED, reg);
}

static void mtk_pci_set_msix_merged(struct mtk_pci_priv *priv, int irq_cnt)
{
	mtk_pci_mac_write32(priv, REG_PCIE_CFG_MSIX, ffs(irq_cnt) * 2 - 1);
}

u32 mtk_pci_get_dev_state(struct mtk_md_dev *mdev)
{
	return mtk_pci_mac_read32(mdev->hw_priv, REG_PCIE_DEBUG_DUMMY_7);
}

void mtk_pci_ack_dev_state(struct mtk_md_dev *mdev, u32 state)
{
	mtk_pci_mac_write32(mdev->hw_priv, REG_PCIE_DEBUG_DUMMY_7, state);
}

u32 mtk_pci_get_ds_status(struct mtk_md_dev *mdev)
{
	return mtk_pci_mac_read32(mdev->hw_priv, REG_PCIE_RESOURCE_STATUS);
}

static void mtk_pci_enable_intr(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 reg;

	reg = mtk_pci_mac_read32(priv, priv->cfg->istatus_host_ctrl_addr);
	reg &= ~MTK_ENABLE_INTR_BIT;
	mtk_pci_mac_write32(priv, priv->cfg->istatus_host_ctrl_addr, reg);
}

static void mtk_pci_force_mac_active(struct mtk_md_dev *mdev, bool enable,
				     enum mtk_mac_active_user user)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->force_mac_active)
		priv->cfg->force_mac_active(mdev, enable, user);
}

void mtk_pci_ds_lock(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->force_mac_sleep)
		priv->cfg->force_mac_sleep(mdev, true);

	mtk_pci_force_mac_active(mdev, true, MAC_ACTIVE_DS_LOCK);
}

void mtk_pci_ds_unlock(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->force_mac_sleep)
		priv->cfg->force_mac_sleep(mdev, false);

	mtk_pci_force_mac_active(mdev, false, MAC_ACTIVE_DS_LOCK);
}

void mtk_pci_enable_l1ss_ds(struct mtk_md_dev *mdev, u32 type, enum mtk_mac_active_user user)
{
	mtk_pci_force_mac_active(mdev, false, user);
	mtk_pci_mac_write32(mdev->hw_priv, REG_DIS_ASPM_LOWPWR_CLR_0, type);
}

void mtk_pci_disable_l1ss_ds(struct mtk_md_dev *mdev, u32 type, enum mtk_mac_active_user user)
{
	mtk_pci_force_mac_active(mdev, true, user);
	mtk_pci_mac_write32(mdev->hw_priv, REG_DIS_ASPM_LOWPWR_SET_0, type);
}

int mtk_pci_get_irq_id(struct mtk_md_dev *mdev, enum mtk_irq_src irq_src)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	const int *irq_tbl = priv->cfg->irq_tbl;
	int irq_id = -EINVAL;

	if (irq_src > MTK_IRQ_SRC_MIN && irq_src < MTK_IRQ_SRC_MAX) {
		irq_id = irq_tbl[irq_src];
		if (unlikely(irq_id < 0 || irq_id >= MTK_IRQ_CNT_MAX))
			irq_id = -EINVAL;
	}

	return irq_id;
}

int mtk_pci_get_virq_id(struct mtk_md_dev *mdev, int irq_id)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int nr = 0;

	nr = irq_id % priv->irq_cnt;

	return pci_irq_vector(pdev, nr);
}

int mtk_pci_register_irq(struct mtk_md_dev *mdev, int irq_id,
			 int (*irq_cb)(int irq_id, void *data), void *data)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (unlikely((irq_id < 0 || irq_id >= MTK_IRQ_CNT_MAX) || !irq_cb))
		return -EINVAL;

	if (priv->irq_cb_list[irq_id]) {
		MTK_ERR(mdev,
			"Unable to register irq, irq_id=%d, it's already been register by %ps.\n",
			irq_id, priv->irq_cb_list[irq_id]);
		return -EFAULT;
	}
	priv->irq_cb_list[irq_id] = irq_cb;
	priv->irq_cb_data[irq_id] = data;
	MTK_INFO(mdev, "Register irq: irq_id=%d irq_cb=%ps data=%p\n",
		 irq_id, irq_cb, data);

	return 0;
}

int mtk_pci_unregister_irq(struct mtk_md_dev *mdev, int irq_id)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (unlikely(irq_id < 0 || irq_id >= MTK_IRQ_CNT_MAX))
		return -EINVAL;

	if (!priv->irq_cb_list[irq_id]) {
		MTK_ERR(mdev, "irq_id=%d has not been registered\n", irq_id);
		return -EFAULT;
	}
	priv->irq_cb_list[irq_id] = NULL;
	priv->irq_cb_data[irq_id] = NULL;
	MTK_INFO(mdev, "Unregister irq: irq_id=%d\n", irq_id);

	return 0;
}

int mtk_pci_mask_irq(struct mtk_md_dev *mdev, int irq_id)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (unlikely((irq_id < 0 || irq_id >= MTK_IRQ_CNT_MAX) || priv->irq_type == 0)) {
		MTK_ERR(mdev, "Failed to mask irq: input irq_id=%d\n", irq_id);
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	if (!mtk_pci_link_check_silent(mdev))
		return 0;
#endif

	if (likely(priv->irq_type == PCI_IRQ_MSIX))
		mtk_pci_mac_write32(priv, REG_IMASK_HOST_MSIX_CLR_GRP0_0, BIT(irq_id));
	else
		mtk_pci_mac_write32(priv, REG_INT_ENABLE_HOST_CLR, BIT(irq_id));

	return 0;
}

int mtk_pci_unmask_irq(struct mtk_md_dev *mdev, int irq_id)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (unlikely((irq_id < 0 || irq_id >= MTK_IRQ_CNT_MAX) || priv->irq_type == 0)) {
		MTK_ERR(mdev, "Failed to unmask irq: input irq_id=%d\n", irq_id);
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	if (!mtk_pci_link_check_silent(mdev))
		return 0;
#endif

	if (likely(priv->irq_type == PCI_IRQ_MSIX))
		mtk_pci_mac_write32(priv, REG_IMASK_HOST_MSIX_SET_GRP0_0, BIT(irq_id));
	else
		mtk_pci_mac_write32(priv, REG_INT_ENABLE_HOST_SET, BIT(irq_id));

	return 0;
}

void mtk_pci_irq_suspend_action(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int mhccif_virq_id;

	mhccif_virq_id = mtk_pci_get_virq_id(mdev, priv->mhccif_irq_id);
	mtk_pci_mask_irq(mdev, priv->mhccif_irq_id);
	synchronize_irq(mhccif_virq_id);
	flush_work(&priv->mhccif_work);
}

void mtk_pci_irq_resume_action(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	mtk_pci_unmask_irq(mdev, priv->mhccif_irq_id);
}

int mtk_pci_clear_irq(struct mtk_md_dev *mdev, int irq_id)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (unlikely((irq_id < 0 || irq_id >= MTK_IRQ_CNT_MAX) || priv->irq_type == 0)) {
		MTK_ERR(mdev, "Failed to clear irq: input irq_id=%d\n", irq_id);
		return -EINVAL;
	}

	if (likely(priv->irq_type == PCI_IRQ_MSIX))
		mtk_pci_mac_write32(priv, REG_MSIX_ISTATUS_HOST_GRP0_0, BIT(irq_id));
	else
		mtk_pci_mac_write32(priv, REG_ISTATUS_HOST, BIT(irq_id));

	return 0;
}

int mtk_pci_reset_sys_irq(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int rgu_virq_id, mhccif_virq_id;

	rgu_virq_id = mtk_pci_get_virq_id(mdev, priv->rgu_irq_id);
	mhccif_virq_id = mtk_pci_get_virq_id(mdev, priv->mhccif_irq_id);

	mtk_pci_mask_irq(mdev, priv->rgu_irq_id);
	synchronize_irq(rgu_virq_id);
	cancel_delayed_work_sync(&priv->rgu_work);

	mtk_pci_mask_irq(mdev, priv->mhccif_irq_id);
	synchronize_irq(mhccif_virq_id);
	cancel_work_sync(&priv->mhccif_work);

	if (priv->mac_active_flag)
		MTK_WARN(mdev, "PCIe mac active ctrl=0x%x\n", priv->mac_active_flag);

	return 0;
}

void mtk_pci_send_sw_evt(struct mtk_md_dev *mdev, enum mtk_pci_h2d_sw_evt evt)
{
	mtk_pci_mac_write32(mdev->hw_priv, REG_RC2EP_SW_TRIG_LOCAL_INTR_SET, BIT(evt));
	mtk_pci_mac_read32(mdev->hw_priv, REG_RC2EP_SW_TRIG_LOCAL_INTR_STAT);
}

void mtk_pci_clear_sw_evt(struct mtk_md_dev *mdev, enum mtk_pci_d2h_sw_evt evt)
{
	mtk_pci_mac_write32(mdev->hw_priv, REG_SW_TRIG_INTR_CLR, BIT(evt));
}

void mtk_pci_trigger_mdee(struct mtk_md_dev *mdev, u32 val)
{
	mtk_pci_mac_write32(mdev->hw_priv, REG_PCIE_DEBUG_DUMMY_34DC, val);
	mtk_pci_send_sw_evt(mdev, H2D_SW_EVT_TRIGGER_MDEE);
}

static u32 mtk_pci_ext_d2h_evt_hw_bits(u32 chs)
{
	u32 hw_bits = 0;

	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_D2H_EXCEPT_INIT,
		    DEV_EVT_D2H_EXCEPT_INIT);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_EXCEPT_INIT_DONE,
		    DEV_EVT_D2H_EXCEPT_INIT_DONE);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_EXCEPT_CLEARQ_DONE,
		    DEV_EVT_D2H_EXCEPT_CLEARQ_DONE);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_EXCEPT_ALLQ_RESET,
		    DEV_EVT_D2H_EXCEPT_ALLQ_RESET);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_BOOT_FLOW_SYNC,
		    DEV_EVT_D2H_BOOT_FLOW_SYNC);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_ASYNC_HS_NOTIFY_SAP,
		    DEV_EVT_D2H_ASYNC_HS_NOTIFY_SAP);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_ASYNC_HS_NOTIFY_MD,
		    DEV_EVT_D2H_ASYNC_HS_NOTIFY_MD);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_MD_REBOOT,
		    DEV_EVT_D2H_MD_REBOOT);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_MD_POWEROFF,
		    DEV_EVT_D2H_MD_POWER_OFF);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_GNSS_ENABLE,
		    DEV_EVT_D2H_GNSS_ENABLE);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_GNSS_DISABLE,
		    DEV_EVT_D2H_GNSS_DISABLE);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_PCIE_DS_LOCK_ACK,
		    EXT_EVT_D2H_PCIE_DS_LOCK_ACK);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_RESERVED_FOR_CLDMA0,
		    EXT_EVT_D2H_RESERVED_FOR_CLDMA0);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_RESERVED_FOR_CLDMA1,
		    EXT_EVT_D2H_RESERVED_FOR_CLDMA1);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_RESERVED_FOR_CLDMA3,
		    EXT_EVT_D2H_RESERVED_FOR_CLDMA3);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_RESERVED_FOR_CLDMA2,
		    EXT_EVT_D2H_RESERVED_FOR_CLDMA2);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_RESERVED_FOR_DPMAIF,
		    EXT_EVT_D2H_RESERVED_FOR_DPMAIF);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_PCIE_PM_SUSPEND_ACK,
		    EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_PCIE_PM_RESUME_ACK,
		    EXT_EVT_D2H_PCIE_PM_RESUME_ACK);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_PCIE_PM_SUSPEND_ACK_AP,
		    EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK_AP);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_PCIE_PM_RESUME_ACK_AP,
		    EXT_EVT_D2H_PCIE_PM_RESUME_ACK_AP);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_SOFT_OFF_NOTIFY,
		    EXT_EVT_D2H_SOFT_OFF_NOTIFY);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_FRC_DONE_NOTIFY,
		    EXT_EVT_D2H_FRC_DONE_NOTIFY);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_RESERVED_FOR_TEST1,
		    EXT_EVT_D2H_RESERVED_FOR_TEST1);
	SET_HW_BITS(hw_bits, chs, MHCCIF_EP2RC_EVT_RESERVED_FOR_TEST2,
		    EXT_EVT_D2H_RESERVED_FOR_TEST2);

	return LE32_TO_U32(cpu_to_le32(hw_bits));
}

static u32 mtk_pci_ext_d2h_evt_chs(u32 hw_bits)
{
	u32 chs = 0;

	if (!hw_bits)
		return chs;

	chs = FIELD_PREP(DEV_EVT_D2H_EXCEPT_INIT,
			 FIELD_GET(MHCCIF_EP2RC_EVT_D2H_EXCEPT_INIT, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_EXCEPT_INIT_DONE,
			 FIELD_GET(MHCCIF_EP2RC_EVT_EXCEPT_INIT_DONE, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_EXCEPT_CLEARQ_DONE,
			 FIELD_GET(MHCCIF_EP2RC_EVT_EXCEPT_CLEARQ_DONE, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_EXCEPT_ALLQ_RESET,
			 FIELD_GET(MHCCIF_EP2RC_EVT_EXCEPT_ALLQ_RESET, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_BOOT_FLOW_SYNC,
			 FIELD_GET(MHCCIF_EP2RC_EVT_BOOT_FLOW_SYNC, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_ASYNC_HS_NOTIFY_SAP,
			 FIELD_GET(MHCCIF_EP2RC_EVT_ASYNC_HS_NOTIFY_SAP, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_ASYNC_HS_NOTIFY_MD,
			 FIELD_GET(MHCCIF_EP2RC_EVT_ASYNC_HS_NOTIFY_MD, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_MD_REBOOT,
			 FIELD_GET(MHCCIF_EP2RC_EVT_MD_REBOOT, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_MD_POWER_OFF,
			 FIELD_GET(MHCCIF_EP2RC_EVT_MD_POWEROFF, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_GNSS_ENABLE,
			 FIELD_GET(MHCCIF_EP2RC_EVT_GNSS_ENABLE, hw_bits)) |
	      FIELD_PREP(DEV_EVT_D2H_GNSS_DISABLE,
			 FIELD_GET(MHCCIF_EP2RC_EVT_GNSS_DISABLE, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_PCIE_DS_LOCK_ACK,
			 FIELD_GET(MHCCIF_EP2RC_EVT_PCIE_DS_LOCK_ACK, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_RESERVED_FOR_CLDMA0,
			 FIELD_GET(MHCCIF_EP2RC_EVT_RESERVED_FOR_CLDMA0, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_RESERVED_FOR_CLDMA1,
			 FIELD_GET(MHCCIF_EP2RC_EVT_RESERVED_FOR_CLDMA1, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_RESERVED_FOR_CLDMA3,
			 FIELD_GET(MHCCIF_EP2RC_EVT_RESERVED_FOR_CLDMA3, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_RESERVED_FOR_CLDMA2,
			 FIELD_GET(MHCCIF_EP2RC_EVT_RESERVED_FOR_CLDMA2, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_RESERVED_FOR_DPMAIF,
			 FIELD_GET(MHCCIF_EP2RC_EVT_RESERVED_FOR_DPMAIF, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK,
			 FIELD_GET(MHCCIF_EP2RC_EVT_PCIE_PM_SUSPEND_ACK, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_PCIE_PM_RESUME_ACK,
			 FIELD_GET(MHCCIF_EP2RC_EVT_PCIE_PM_RESUME_ACK, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK_AP,
			 FIELD_GET(MHCCIF_EP2RC_EVT_PCIE_PM_SUSPEND_ACK_AP, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_PCIE_PM_RESUME_ACK_AP,
			 FIELD_GET(MHCCIF_EP2RC_EVT_PCIE_PM_RESUME_ACK_AP, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_SOFT_OFF_NOTIFY,
			 FIELD_GET(MHCCIF_EP2RC_EVT_SOFT_OFF_NOTIFY, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_FRC_DONE_NOTIFY,
			 FIELD_GET(MHCCIF_EP2RC_EVT_FRC_DONE_NOTIFY, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_RESERVED_FOR_TEST1,
			 FIELD_GET(MHCCIF_EP2RC_EVT_RESERVED_FOR_TEST1, hw_bits)) |
	      FIELD_PREP(EXT_EVT_D2H_RESERVED_FOR_TEST2,
			 FIELD_GET(MHCCIF_EP2RC_EVT_RESERVED_FOR_TEST2, hw_bits));

	return chs;
}

int mtk_pci_register_ext_evt(struct mtk_md_dev *mdev, u32 chs,
			     int (*evt_cb)(u32 status, void *data), void *data)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_mhccif_cb *cb;
	int ret = 0;

	if (!chs || !evt_cb)
		return -EINVAL;

	spin_lock_bh(&priv->mhccif_lock);
	list_for_each_entry(cb, &priv->mhccif_cb_list, entry) {
		if (cb->chs & chs) {
			ret = -EFAULT;
			MTK_ERR(mdev,
				"Unable to register evt, intersection: chs=0x%08x&0x%08x registered_cb=%ps\n",
				chs, cb->chs, cb->evt_cb);
			goto err_spin_unlock;
		}
	}
	cb = devm_kzalloc(mdev->dev, sizeof(*cb), GFP_ATOMIC);
	if (!cb) {
		ret = -ENOMEM;
		goto err_spin_unlock;
	}
	cb->evt_cb = evt_cb;
	cb->data = data;
	cb->chs = chs;
	list_add_tail(&cb->entry, &priv->mhccif_cb_list);
	MTK_INFO(mdev, "Register mhccif: chs=0x%08x evt_cb=%ps data=%p\n",
		 chs, evt_cb, data);
err_spin_unlock:
	spin_unlock_bh(&priv->mhccif_lock);

	return ret;
}

void mtk_pci_unregister_ext_evt(struct mtk_md_dev *mdev, u32 chs)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_mhccif_cb *cb, *next;

	if (!chs)
		return;

	spin_lock_bh(&priv->mhccif_lock);
	list_for_each_entry_safe(cb, next, &priv->mhccif_cb_list, entry) {
		if (cb->chs == chs) {
			list_del(&cb->entry);
			devm_kfree(mdev->dev, cb);
			MTK_INFO(mdev, "Unregister mhccif: chs=0x%08x\n", chs);
			goto out;
		}
	}
	MTK_WARN(mdev, "Unable to unregister evt, no chs=0x%08x has been registered.\n", chs);
out:
	spin_unlock_bh(&priv->mhccif_lock);
}

void mtk_pci_mask_ext_evt(struct mtk_md_dev *mdev, u32 chs)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 hw_bits;

#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	if (!mtk_pci_link_check_silent(mdev))
		return;
#endif

	hw_bits = mtk_pci_ext_d2h_evt_hw_bits(chs);

	mtk_pci_write32(mdev, priv->cfg->mhccif_rc_base_addr +
			MHCCIF_EP2RC_SW_INT_EAP_MASK_SET, hw_bits);
}

void mtk_pci_unmask_ext_evt(struct mtk_md_dev *mdev, u32 chs)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 hw_bits;

#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	if (!mtk_pci_link_check_silent(mdev))
		return;
#endif

	hw_bits = mtk_pci_ext_d2h_evt_hw_bits(chs);

	mtk_pci_write32(mdev, priv->cfg->mhccif_rc_base_addr +
			MHCCIF_EP2RC_SW_INT_EAP_MASK_CLR, hw_bits);
}

void mtk_pci_clear_ext_evt(struct mtk_md_dev *mdev, u32 chs)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 hw_bits;

	hw_bits = mtk_pci_ext_d2h_evt_hw_bits(chs);

	mtk_pci_write32(mdev, priv->cfg->mhccif_rc_base_addr +
			MHCCIF_EP2RC_SW_INT_ACK, hw_bits);
}

static u32 mtk_pci_ext_h2d_evt_hw_bits(u32 chs)
{
	u32 hw_bits = 0;

	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_EXCEPT_ACK,
		    DEV_EVT_H2D_EXCEPT_ACK);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_EXCEPT_CLEARQ_ACK,
		    DEV_EVT_H2D_EXCEPT_CLEARQ_ACK);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_DEVICE_RESET,
		    DEV_EVT_H2D_DEVICE_RESET);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_MD_REBOOT_ACK,
		    DEV_EVT_H2D_MD_REBOOT_ACK);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_TRM_NOTIFY,
		    DEV_EVT_H2D_TRM_NOTIFY);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_PCIE_DS_LOCK,
		    EXT_EVT_H2D_PCIE_DS_LOCK);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_RESERVED_FOR_CLDMA0,
		    EXT_EVT_H2D_RESERVED_FOR_CLDMA0);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_RESERVED_FOR_CLDMA1,
		    EXT_EVT_H2D_RESERVED_FOR_CLDMA1);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_RESERVED_FOR_CLDMA3,
		    EXT_EVT_H2D_RESERVED_FOR_CLDMA3);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_RESERVED_FOR_CLDMA2,
		    EXT_EVT_H2D_RESERVED_FOR_CLDMA2);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_RESERVED_FOR_DPMAIF,
		    EXT_EVT_H2D_RESERVED_FOR_DPMAIF);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_PCIE_PM_SUSPEND_REQ,
		    EXT_EVT_H2D_PCIE_PM_SUSPEND_REQ);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_PCIE_PM_RESUME_REQ,
		    EXT_EVT_H2D_PCIE_PM_RESUME_REQ);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_PCIE_PM_SUSPEND_REQ_AP,
		    EXT_EVT_H2D_PCIE_PM_SUSPEND_REQ_AP);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_PCIE_PM_RESUME_REQ_AP,
		    EXT_EVT_H2D_PCIE_PM_RESUME_REQ_AP);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_DRM_DISABLE_AP,
		    EXT_EVT_H2D_DRM_DISABLE_AP);
	SET_HW_BITS(hw_bits, chs, MHCCIF_RC2EP_EVT_RESERVED_FOR_TEST,
		    EXT_EVT_H2D_RESERVED_FOR_TEST);

	return LE32_TO_U32(cpu_to_le32(hw_bits));
}

int mtk_pci_send_ext_evt(struct mtk_md_dev *mdev, u32 ch)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 rc_base;
	u32 hw_bits;

	rc_base = priv->cfg->mhccif_rc_base_addr;

	/* Only allow one ch to be triggered at a time */
	if (!is_power_of_2(ch)) {
		MTK_ERR(mdev, "Unsupported ext evt ch=0x%08x\n", ch);
		return -EINVAL;
	}

	hw_bits = mtk_pci_ext_h2d_evt_hw_bits(ch);
	mtk_pci_write32(mdev, rc_base + MHCCIF_RC2EP_SW_BSY, hw_bits);
	mtk_pci_write32(mdev, rc_base + MHCCIF_RC2EP_SW_TCHNUM, ffs(hw_bits) - 1);
	return 0;
}

static u32 mtk_pci_get_ext_evt_hw_status(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr + MHCCIF_EP2RC_SW_INT_STS);
}

#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
int mtk_pci_fldr(struct mtk_md_dev *mdev)
{
	return mtk_pwrctl_fldr();
}

int mtk_pci_pldr(struct mtk_md_dev *mdev)
{
	return mtk_pwrctl_pldr();
}
#else
int mtk_pci_fldr(struct mtk_md_dev *mdev)
{
#ifdef CONFIG_ACPI
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status acpi_ret;
	acpi_handle handle;

	if (acpi_disabled) {
		MTK_ERR(mdev, "Unsupported, acpi function isn't enable\n");
		return -ENODEV;
	}
	handle = ACPI_HANDLE(mdev->dev);
	if (!handle) {
		MTK_ERR(mdev, "Unsupported, acpi handle isn't found\n");
		return -ENODEV;
	}
	if (!acpi_has_method(handle, "_RST")) {
		MTK_ERR(mdev, "Unsupported, _RST method isn't found\n");
		return -ENODEV;
	}
	acpi_ret = acpi_evaluate_object(handle, "_RST", NULL, &buffer);
	if (ACPI_FAILURE(acpi_ret)) {
		MTK_ERR(mdev, "Failed to execute _RST method: %s\n",
			acpi_format_exception(acpi_ret));
		return -EFAULT;
	}
	MTK_INFO(mdev, "FLDR DONE\n");
	acpi_os_free(buffer.pointer);

	return 0;
#else
	MTK_ERR(mdev, "Unsupported, CONFIG ACPI hasn't been set to 'y'\n");

	return -ENODEV;
#endif
}

int mtk_pci_pldr(struct mtk_md_dev *mdev)
{
#ifdef CONFIG_ACPI
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct pci_dev *bridge;
	acpi_status acpi_ret;
	acpi_handle handle;

	if (acpi_disabled) {
		MTK_ERR(mdev, "Unsupported, acpi function isn't enable\n");
		return -ENODEV;
	}

	bridge = pci_upstream_bridge(to_pci_dev(mdev->dev));
	if (!bridge) {
		MTK_ERR(mdev, "Unable to find bridge\n");
		return -ENODEV;
	}

	handle = ACPI_HANDLE(&bridge->dev);
	if (!handle) {
		MTK_ERR(mdev, "Unsupported, acpi handle isn't found\n");
		return -ENODEV;
	}
	if (!acpi_has_method(handle, "PXP._OFF") ||
	    !acpi_has_method(handle, "PXP._ON")) {
		MTK_ERR(mdev, "Unsupported, pldr method isn't supported\n");
		return -ENODEV;
	}
	acpi_ret = acpi_evaluate_object(handle, "PXP._OFF", NULL, &buffer);
	if (ACPI_FAILURE(acpi_ret)) {
		MTK_ERR(mdev, "Failed to execute _OFF method: %s\n",
			acpi_format_exception(acpi_ret));
		return -EFAULT;
	}
	msleep(500);
	acpi_ret = acpi_evaluate_object(handle, "PXP._ON", NULL, &buffer);
	if (ACPI_FAILURE(acpi_ret)) {
		MTK_ERR(mdev, "Failed to execute _ON method: %s\n",
			acpi_format_exception(acpi_ret));
		return -EFAULT;
	}
	MTK_INFO(mdev, "PLDR DONE\n");
	acpi_os_free(buffer.pointer);

	return 0;
#else
	MTK_ERR(mdev, "Unsupported, CONFIG ACPI hasn't been set to 'y'\n");

	return -ENODEV;
#endif
}
#endif /* CONFIG_MTK_WWAN_PWRCTL_SUPPORT */

u32 mtk_pci_get_dev_cfg(struct mtk_md_dev *mdev)
{
	u32 val;

	val = mtk_pci_mac_read32(mdev->hw_priv, REG_PCIE_DEBUG_DUMMY_4);
	return (val >> MTK_CFG_INFO_BIT_SHIFT);
}

static u32 mtk_pci_get_dev_info(struct mtk_md_dev *mdev)
{
	return mtk_pci_mac_read32(mdev->hw_priv, REG_PCIE_DEBUG_DUMMY_2);
}

static int mtk_pci_dev_reset(struct mtk_md_dev *mdev, enum mtk_reset_type type)
{
	switch (type) {
	case RESET_MHCCIF:
		return mtk_pci_send_ext_evt(mdev, DEV_EVT_H2D_DEVICE_RESET);
	case RESET_FLDR:
		return mtk_pci_fldr(mdev);
	case RESET_PLDR:
		return mtk_pci_pldr(mdev);
	default:
		break;
	}

	return -EINVAL;
}

int mtk_pci_reset(struct mtk_md_dev *mdev, enum mtk_reset_type type)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->dev_reset)
		return priv->cfg->dev_reset(mdev, type);
	else
		return mtk_pci_dev_reset(mdev, type);
}

int mtk_pci_get_dev_log(struct mtk_md_dev *mdev,
			void *buf, size_t count, enum mtk_dev_log_type type)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int err, ret;

	if (!priv->cfg->get_dev_log)
		return -ENODEV;

	mtk_pm_ds_lock(mdev, MTK_USER_HW);
	err = mtk_pm_ds_wait_complete(mdev, MTK_USER_HW);
	if (unlikely(err)) {
		MTK_ERR(mdev, "Failed to lock ds:%d\n", err);
		ret = -EINVAL;
		goto out;
	}
	ret = priv->cfg->get_dev_log(mdev, buf, count, type);

out:
	mtk_pm_ds_unlock(mdev, MTK_USER_HW);
	return ret;
}

int mtk_pci_get_log_region_size(struct mtk_md_dev *mdev, enum mtk_dev_log_type type)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->get_log_region_size)
		return priv->cfg->get_log_region_size(mdev, type);

	return -ENODEV;
}

static int mtk_pci_dev_log_buff_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->dev_log_buff_init)
		return priv->cfg->dev_log_buff_init(mdev);

	return 0;
}

static int mtk_pci_dev_log_buff_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->dev_log_buff_exit)
		return priv->cfg->dev_log_buff_exit(mdev);

	return 0;
}

static int mtk_pci_dsd_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->dsd_init)
		return priv->cfg->dsd_init(mdev);

	return 0;
}

static int mtk_pci_dsd_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	if (priv->cfg->dsd_exit)
		return priv->cfg->dsd_exit(mdev);

	return 0;
}

static void mtk_pci_bridge_configure(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev, *bridge;
	struct mtk_pci_priv *priv;
	u16 cap, ctl;
	int dpc_cap;

	priv = mdev->hw_priv;
	pdev = to_pci_dev(mdev->dev);

	/* check hotplug capability */
	bridge = pci_upstream_bridge(pdev);
	if (!bridge) {
		MTK_ERR(mdev, "Unable to find bridge\n");
		return;
	}

	priv->rc_hp_on = bridge->is_hotplug_bridge;

	/* Disable DPC Capability */
	dpc_cap = pci_find_ext_capability(bridge, PCI_EXT_CAP_ID_DPC);
	if (dpc_cap) {
		pci_read_config_word(bridge, dpc_cap + PCI_EXP_DPC_CAP, &cap);
		pci_read_config_word(bridge, dpc_cap + PCI_EXP_DPC_CTL, &ctl);
		if (ctl & (PCI_EXP_DPC_CTL_INT_EN | 0x3)) {
			ctl &= ~(PCI_EXP_DPC_CTL_INT_EN | 0x3);
			pci_write_config_word(bridge, dpc_cap + PCI_EXP_DPC_CTL, ctl);
		}
	}

#ifdef CONFIG_PCIEASPM
	/* Re-configure ltr to satisfy remove-rescan case */
	if (bridge->ltr_path) {
		pcie_capability_read_word(bridge, PCI_EXP_DEVCTL2, &ctl);
		if (!(ctl & PCI_EXP_DEVCTL2_LTR_EN))
			pcie_capability_set_word(bridge, PCI_EXP_DEVCTL2, PCI_EXP_DEVCTL2_LTR_EN);
	}
	pcie_capability_read_word(pdev, PCI_EXP_DEVCTL2, &ctl);
	MTK_INFO(mdev, "Called by %ps:Hotplug [%s], LTR_Path[%x/%x], Device CTL2[0x%x] LTR[0x%x]\n",
		 __builtin_return_address(0), priv->rc_hp_on ? "on" : "off", bridge->ltr_path,
		 pdev->ltr_path, ctl, ctl & PCI_EXP_DEVCTL2_LTR_EN);
#endif
}

#if defined(CONFIG_PCIEASPM) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0))
void mtk_pci_restore_aspm_l1ss_state(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 pl_ctl1, pl_ctl2, pl_l1_2_enable;
	u32 cl_ctl1, cl_ctl2, cl_l1_2_enable;
	struct pci_dev *parent;
	u16 clnkctl, plnkctl;
	u32 reg_value;

	if (!pdev || !pdev->bus)
		return;

	parent = pdev->bus->self;

	if (!parent || !parent->l1ss || !pdev->l1ss)
		return;

	cl_ctl1 = priv->l1ss_ctl1;
	cl_ctl2 = priv->l1ss_ctl2;
	pl_ctl1 = priv->parent_l1ss_ctl1;
	pl_ctl2 = priv->parent_l1ss_ctl2;
	if (!(cl_ctl1 || cl_ctl2 || pl_ctl1 || pl_ctl2)) {
		MTK_WARN(mdev, "The L1ss value is not saved!\n");
		pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1, &pl_ctl1);
		pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CTL2, &pl_ctl2);
		cl_ctl1 = pl_ctl1 & (PCI_L1SS_CTL1_L1SS_MASK | PCI_L1SS_CTL1_LTR_L12_TH_VALUE |
			  PCI_L1SS_CTL1_LTR_L12_TH_SCALE);
		cl_ctl2 = pl_ctl2;
		if (!cl_ctl1 || !cl_ctl2) {
			MTK_ERR(mdev, "ctl1:0x%x or ctl2:0x%x is zero.\n", cl_ctl1, cl_ctl2);
			return;
		}
	}

	/* Make sure L0s/L1 are disabled before updating L1SS config */
	pci_read_config_word(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCTL, &clnkctl);
	pci_read_config_word(parent, pci_pcie_cap(parent) + PCI_EXP_LNKCTL, &plnkctl);
	if (FIELD_GET(PCI_EXP_LNKCTL_ASPMC, clnkctl) ||
	    FIELD_GET(PCI_EXP_LNKCTL_ASPMC, plnkctl)) {
		pci_write_config_word(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCTL,
				      clnkctl & ~PCI_EXP_LNKCTL_ASPMC);
		pci_write_config_word(parent, pci_pcie_cap(parent) + PCI_EXP_LNKCTL,
				      plnkctl & ~PCI_EXP_LNKCTL_ASPMC);
	}

	/* Disable L1.2 on this downstream endpoint device first, followed
	 * by the upstream
	 */
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &reg_value);
	reg_value &= ~PCI_L1SS_CTL1_L1_2_MASK;
	pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, reg_value);

	pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1, &reg_value);
	reg_value &= ~PCI_L1SS_CTL1_L1_2_MASK;
	pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1, reg_value);

	/* In addition, Common_Mode_Restore_Time and LTR_L1.2_THRESHOLD
	 * in PCI_L1SS_CTL1 must be programmed *before* setting the L1.2
	 * enable bits, even though they're all in PCI_L1SS_CTL1.
	 */
	pl_l1_2_enable = pl_ctl1 & PCI_L1SS_CTL1_L1_2_MASK;
	pl_ctl1 &= ~PCI_L1SS_CTL1_L1_2_MASK;
	cl_l1_2_enable = cl_ctl1 & PCI_L1SS_CTL1_L1_2_MASK;
	cl_ctl1 &= ~PCI_L1SS_CTL1_L1_2_MASK;

	/* Write back l1ss_ctl without L1.2 enables first */
	pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL2, pl_ctl2);
	pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL2, cl_ctl2);
	pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1, pl_ctl1);
	pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, cl_ctl1);

	/* Then write back the L1.2 enables */
	if (pl_l1_2_enable || cl_l1_2_enable) {
		pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				       pl_ctl1 | pl_l1_2_enable);
		pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1,
				       cl_ctl1 | cl_l1_2_enable);
	}

	/* Restore L0s/L1 if they were enabled */
	if (FIELD_GET(PCI_EXP_LNKCTL_ASPMC, clnkctl) ||
	    FIELD_GET(PCI_EXP_LNKCTL_ASPMC, plnkctl)) {
		pci_write_config_word(parent, pci_pcie_cap(parent) + PCI_EXP_LNKCTL, plnkctl);
		pci_write_config_word(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCTL, clnkctl);
	}
}

static void mtk_pci_save_aspm_l1ss_state(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct pci_dev *parent = pdev->bus->self;
	int parent_l1ss, child_l1ss;

	parent_l1ss = pci_find_ext_capability(parent, PCI_EXT_CAP_ID_L1SS);
	child_l1ss = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);
	if (parent_l1ss && child_l1ss) {
		pci_read_config_dword(parent, parent_l1ss + PCI_L1SS_CTL1, &priv->parent_l1ss_ctl1);
		pci_read_config_dword(parent, parent_l1ss + PCI_L1SS_CTL2, &priv->parent_l1ss_ctl2);
		pci_read_config_dword(pdev, child_l1ss + PCI_L1SS_CTL1, &priv->l1ss_ctl1);
		pci_read_config_dword(pdev, child_l1ss + PCI_L1SS_CTL2, &priv->l1ss_ctl2);
	} else {
		MTK_ERR(mdev, "No L1SS capability found!\n");
	}
}
#endif

static void mtk_pci_update_current_state(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	pci_power_t pre_state;
	u16 pmcsr;

	pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &pmcsr);
	pmcsr &= PCI_PM_CTRL_STATE_MASK;
	if (pdev->current_state == PCI_D0 && pmcsr == 0)
		return;

	pre_state = pdev->current_state;
	pdev->current_state = PCI_D0;
	MTK_WARN(mdev, "Update device state, pre_state = 0x%x, current_state=0x%x, pmcsr=0x%x\n",
		 pre_state, pdev->current_state, pmcsr);
}

int mtk_pci_reinit(struct mtk_md_dev *mdev, enum mtk_reinit_type type)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 bar[BAR_NUM];
	int ret, val, i;

	mtk_pci_bridge_configure(mdev);

	for (i = 0; i < BAR_NUM; i++)
		pci_read_config_dword(to_pci_dev(mdev->dev),
				      PCI_BASE_ADDRESS_0 + (i << 2), bar + i);
	MTK_INFO(mdev, "BAR0/1/2/3/4/5: 0x%08x/0x%08x/0x%08x/0x%08x/0x%08x/0x%08x",
		 bar[0], bar[1], bar[2], bar[3], bar[4], bar[5]);

	if (type == REINIT_TYPE_EXP) {
		/* Update current_state will fail when device power off scenario,
		 * so we need to update it here to ensure restore msix successfully.
		 */
		mtk_pci_update_current_state(mdev);
		/* We have saved it in probe() */
		pci_load_saved_state(pdev, priv->saved_state);
		pci_restore_state(pdev);
		val = mtk_pci_get_dev_cfg(mdev);
		MTK_INFO(mdev, "0x%x reinit reboot reason is 0x%x, devdbg infor is 0x%x\n",
			 val, (val & 0x1F), mtk_pci_get_dev_info(mdev));

		mtk_pci_clear_irq(mdev, priv->rgu_irq_id);
	}

	ret = mtk_pci_reinit_mac(mdev, false);
	if (ret) {
		MTK_ERR(mdev, "Failed to init mac reg, ret=%d\n", ret);
		return ret;
	}

	mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_READY);
	ret = mtk_pci_dev_log_buff_init(mdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to init dev log buff, ret=%d\n", ret);
		return ret;
	}
	mtk_pci_dsd_init(mdev);
	mtk_pcie_slt_reinit(mdev);

	MTK_INFO(mdev, "PCIe reinit type=%d, invoked by %ps\n", type, __builtin_return_address(0));
	return 0;
}

int mtk_pci_reinit_mac(struct mtk_md_dev *mdev, bool resuem_from_L2)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int ret;

#if defined(CONFIG_PCIEASPM) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0))
	mtk_pci_restore_aspm_l1ss_state(mdev);
#endif

	ret = priv->cfg->atr_init(mdev);
	if (ret)
		return ret;

	if (priv->irq_type == PCI_IRQ_MSIX) {
		if (priv->irq_cnt != MTK_IRQ_CNT_MAX)
			mtk_pci_set_msix_merged(priv, priv->irq_cnt);
		if (priv->cfg->flag & MTK_CFG_IRQ_DFLT_MASK) /* mask all L1 level interrupts */
			mtk_pci_mac_write32(priv, REG_IMASK_HOST_MSIX_CLR_GRP0_0, U32_MAX);
	} else if (priv->irq_type == PCI_IRQ_MSI) {
		mtk_pci_set_msi_merged(priv, priv->irq_cnt);
	}

	mtk_pci_unmask_irq(mdev, priv->rgu_irq_id);
	mtk_pci_unmask_irq(mdev, priv->mhccif_irq_id);

	/* In L2 resume, device would disable PCIe interrupt,
	 * and this step would re-enable PCIe interrupt.
	 */
	if (resuem_from_L2)
		mtk_pci_enable_intr(mdev);

	return 0;
}

bool mtk_pci_link_check(struct mtk_md_dev *mdev)
{
	u32 vendor_id;
	bool present;

	pci_read_config_dword(to_pci_dev(mdev->dev), PCI_VENDOR_ID, &vendor_id);
	present = pci_device_is_present(to_pci_dev(mdev->dev));
	MTK_INFO(mdev, "vendor id[%x], device present[%d]\n", vendor_id, present);

	return present;
}

#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
bool mtk_pci_link_check_silent(struct mtk_md_dev *mdev)
{
	u32 vendor_id;
	bool present;

	pci_read_config_dword(to_pci_dev(mdev->dev), PCI_VENDOR_ID, &vendor_id);
	present = pci_device_is_present(to_pci_dev(mdev->dev));

	return present;
}
#endif

#if IS_ENABLED(CONFIG_GOOGLE_B528903481_DEBUG)
void mtk_pci_mmio_hw_check(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	MTK_INFO(mdev, "MHCCIF_EP2RC_SW_INT_EAP_MASK: 0x%x\n",
		 mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr
		+ MHCCIF_EP2RC_SW_INT_EAP_MASK));
	MTK_INFO(mdev, "CLDMA version register: 0x%x\n", mtk_pci_read32(mdev, 0x1021E000));
	MTK_INFO(mdev, "CLDMA 0x1021E014 register: 0x%x\n", mtk_pci_read32(mdev, 0x1021E014));
	MTK_INFO(mdev, "DPMAIF version register: 0x%x\n", mtk_pci_read32(mdev, 0x1022D46C));
}
#endif

bool mtk_pci_mmio_check(struct mtk_md_dev *mdev)
{
	struct pci_dev *child_pdev = to_pci_dev(mdev->dev);
	struct pci_dev *parent_pdev;
	u32 val, stat, busno;
	u16 cmd;

	parent_pdev = pci_upstream_bridge(child_pdev);
	if (!parent_pdev)
		return false;

	pci_read_config_word(child_pdev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_MASTER) || cmd == 0xffff) {
		pci_read_config_dword(parent_pdev, PCI_COMMAND, &stat);
		pci_read_config_dword(parent_pdev, PCI_PRIMARY_BUS, &busno);
		MTK_INFO(mdev, "%ps mmio check link down, cmd = 0x%x\n",
			 __builtin_return_address(0), cmd);
		MTK_INFO(mdev, "RC state = 0x%x, RC bus numbers = 0x%x\n", stat, busno);
		return false;
	}

	val = mtk_pci_mac_read32(mdev->hw_priv, REG_ATR_PCIE_WIN0_T0_SRC_ADDR_LSB);
	if (val == 0xffffffff || val == 0x00000000) {
		pci_read_config_dword(parent_pdev, PCI_COMMAND, &stat);
		pci_read_config_dword(parent_pdev, PCI_PRIMARY_BUS, &busno);
		MTK_INFO(mdev, "%ps mmio check link down, cmd = 0x%x, val = 0x%x\n",
			 __builtin_return_address(0), cmd, val);
		MTK_INFO(mdev, "RC state = 0x%x, RC bus numbers = 0x%x\n", stat, busno);
		return false;
	}
	return true;
}

int mtk_pci_get_hp_status(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return priv->rc_hp_on;
}

int mtk_pci_dump(struct mtk_md_dev *mdev)
{
	if (mtk_pci_mmio_check(mdev)) {
		mtk_pci_info_dump(mdev);
		return 0;
	}
	mtk_exception_report_evt(mdev, EXCEPTION_LINK_ERR);

	return -EFAULT;
}

static void mtk_pci_dump_msix_tbl(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct pci_dev *pdev;
	u32 offset;
	u8 bar_idx;
	int i;

	pdev = to_pci_dev(mdev->dev);
	pci_read_config_dword(pdev, pdev->msix_cap + PCI_MSIX_TABLE, &offset);

	if (PCI_POSSIBLE_ERROR(offset)) {
		MTK_WARN(mdev, "link down occurred fail to get MSIX table\n");
		return;
	}

	offset &= PCI_MSIX_TABLE_OFFSET;
	bar_idx = (u8)(offset & PCI_MSIX_TABLE_BIR);
	if (bar_idx != 0) {
		MTK_ERR(mdev, "Fail to get MSIX table\n");
		return;
	}

	MTK_INFO(mdev, "MSIX tbl Dump\n");
	for (i = 0; i < 32; i++) {
		MTK_INFO(mdev, "0x%04x: %08x %08x %08x %08x\n", offset,
			 mtk_pci_mac_read32(priv, offset + 0),
			 mtk_pci_mac_read32(priv, offset + 4),
			 mtk_pci_mac_read32(priv, offset + 8),
			 mtk_pci_mac_read32(priv, offset + 12));
		offset += 16;
	}
}

void mtk_pci_info_dump(struct mtk_md_dev *mdev)
{
	unsigned long current_time = jiffies / HZ;
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct pci_dev *pdev;
	u32 bar[BAR_NUM];
	bool flag = true;
	u16 lnkctl;
	u32 i, reg;

	pdev = to_pci_dev(mdev->dev);

	for (i = 0; i < BAR_NUM; i++) {
		reg = PCI_BASE_ADDRESS_0 + (i << 2);
		pci_read_config_dword(pdev, reg, bar + i);
	}

	pci_read_config_word(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCTL, &lnkctl);

	if ((current_time - last_log_time) >= MTK_PCI_DUMP_TO_LOG_ONCE_PERIOD) {
		last_log_time = current_time;
		flag = true;
	} else {
		flag = false;
	}

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "Start to dump HW infomartion: Dump triggered by: %ps\n",
			 __builtin_return_address(0));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "BAR0/1/2/3/4/5: 0x%08x/0x%08x/0x%08x/0x%08x/0x%08x/0x%08x\n",
			 bar[0], bar[1], bar[2], bar[3], bar[4], bar[5]);

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_PCIE_LTSSM_STATUS: 0x%x PCI_EXP_LNKCTL: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_PCIE_LTSSM_STATUS), lnkctl);

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_DIS_ASPM_LOWPWR_STS_0: 0x%x, REG_DIS_ASPM_LOWPWR_STS_1: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_DIS_ASPM_LOWPWR_STS_0),
			 mtk_pci_mac_read32(priv, REG_DIS_ASPM_LOWPWR_STS_1));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_PCIE_LOW_POWER_CTRL: 0x%x, REG_PCIE_PEXTP_MAC_SLEEP_CTRL: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_PCIE_LOW_POWER_CTRL),
			 mtk_pci_mac_read32(priv, REG_PCIE_PEXTP_MAC_SLEEP_CTRL));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_INT_ENABLE_HOST: 0x%x, REG_ISTATUS_HOST: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_INT_ENABLE_HOST),
			 mtk_pci_mac_read32(priv, REG_ISTATUS_HOST));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_ISTATUS_LOCAL: 0x%x, REG_IMASK_LOCAL: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_ISTATUS_LOCAL),
			 mtk_pci_mac_read32(priv, REG_IMASK_LOCAL));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_ISTATUS_HOST_CTRL: 0x%x, REG_ISTATUS_PENDING_ADT: 0x%x\n",
			 mtk_pci_mac_read32(priv, priv->cfg->istatus_host_ctrl_addr),
			 mtk_pci_mac_read32(priv, REG_ISTATUS_PENDING_ADT));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_MSIX_ISTATUS_HOST_GRP0_0: 0x%x, REG_IMASK_HOST_MSIX_GRP0_0: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_MSIX_ISTATUS_HOST_GRP0_0),
			 mtk_pci_mac_read32(priv, REG_IMASK_HOST_MSIX_GRP0_0));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_IMASK_HOST_MSIX_CLR_GRP0_0: 0x%x, REG_IMASK_HOST_MSIX_SET_GRP0_0: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_IMASK_HOST_MSIX_CLR_GRP0_0),
			 mtk_pci_mac_read32(priv, REG_IMASK_HOST_MSIX_SET_GRP0_0));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_MSIX_TABLE_GRP0_ENT_0_0: 0x%x, REG_PCIE_MISC_CTRL: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_MSIX_TABLE_GRP0_ENT_0_0),
			 mtk_pci_mac_read32(priv, REG_PCIE_MISC_CTRL));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_PCIE_AER_UNC_STATUS: 0x%x, REG_PCIE_AER_CO_STATUS: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_PCIE_AER_UNC_STATUS),
			 mtk_pci_mac_read32(priv, REG_PCIE_AER_CO_STATUS));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_PCIE_ERR_ADDR_L: 0x%x, REG_PCIE_ERR_ADDR_H: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_PCIE_ERR_ADDR_L),
			 mtk_pci_mac_read32(priv, REG_PCIE_ERR_ADDR_H));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_PCIE_ERR_INFO: 0x%x, REG_PCIE_AER_CAPCTL: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_PCIE_ERR_INFO),
			 mtk_pci_mac_read32(priv, REG_PCIE_AER_CAPCTL));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_RC2EP_SW_TRIG_LOCAL_INTR_MASK: 0x%x, MAC_ACTIVE_FLAG: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_RC2EP_SW_TRIG_LOCAL_INTR_MASK),
			 priv->mac_active_flag);

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "MHCCIF_EP2RC_SW_INT_STS: 0x%x, MHCCIF_EP2RC_SW_INT_EAP_MASK: 0x%x\n",
			 mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr +
					 MHCCIF_EP2RC_SW_INT_STS),
			 mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr +
					 MHCCIF_EP2RC_SW_INT_EAP_MASK));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "MHCCIF_EP2RC_SPARE_REG_1: 0x%x, MHCCIF_EP2RC_SPARE_REG_5: 0x%x\n",
			 mtk_pci_get_md_ack_user(mdev), mtk_pci_get_resume_user(mdev));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_MSIX_PBA_GRP0_0: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_MSIX_PBA_GRP0_0));

	mtk_pci_mac_write32(priv, REG_PCIE_DEBUG_SEL_1, 0x99990100);
	mtk_pci_mac_write32(priv, REG_PCIE_DEBUG_SEL_0, 0xCCCDCECF);

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_PCIE_DEBUG_MONITOR: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_PCIE_DEBUG_MONITOR));

	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_PCIE_MSIX_CAP_CTRL: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_PCIE_MSIX_CAP_CTRL));

#if IS_ENABLED(CONFIG_GOOGLE_B495424578_DEBUG)
	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_ATR_PCIE_WIN0_T1_SRC_ADDR_LSB: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_SRC_ADDR_LSB));
	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_ATR_PCIE_WIN0_T1_SRC_ADDR_MSB: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_SRC_ADDR_MSB));
	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_ATR_PCIE_WIN0_T1_TRSL_ADDR_LSB: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_TRSL_ADDR_LSB));
	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_ATR_PCIE_WIN0_T1_TRSL_ADDR_MSB: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_TRSL_ADDR_MSB));
	MTK_PCI_REG_DUMP(mdev, flag, MTK_DBG_PCIE, MTK_MEMLOG_RG_HIF_DUMP,
			 "REG_ATR_PCIE_WIN0_T1_TRSL_PARAM: 0x%x\n",
			 mtk_pci_mac_read32(priv, REG_ATR_PCIE_WIN0_T1_TRSL_PARAM));

	mtk_pci_dump_atr(mdev);
#endif

	if (pdev->msix_enabled) {
		mtk_pci_dump_msix_tbl(mdev);
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
		MTK_INFO(mdev, "mtk_pcie_dump_link_info\n");
		mtk_pcie_dump_link_info(1);
#endif
	}
}

void mtk_pci_write_pm_cnt(struct mtk_md_dev *mdev, u32 val)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	mtk_pci_write32(mdev, priv->cfg->mhccif_rc_base_addr
			+ priv->cfg->mhccif_rc2ep_pcie_pm_counter, val);
}

u32 mtk_pci_get_resume_state(struct mtk_md_dev *mdev)
{
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	if (mtk_pci_link_check_silent(mdev))
		return mtk_pci_mac_read32(mdev->hw_priv, REG_PCIE_DEBUG_DUMMY_3);
	else
		return U32_MAX;
#else
	return mtk_pci_mac_read32(mdev->hw_priv, REG_PCIE_DEBUG_DUMMY_3);
#endif
}

u32 mtk_pci_get_md_ack_user(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr
		+ MHCCIF_EP2RC_SPARE_REG_1);
}

u32 mtk_pci_get_resume_user(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr
			      + MHCCIF_EP2RC_SPARE_REG_5);
}

u32 mtk_pci_get_tras_cfg(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr + MHCCIF_EP2RC_SPARE_REG_13);
}

u32 mtk_pci_get_tras_frc(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr + MHCCIF_EP2RC_SPARE_REG_14);
}

static void mtk_mhccif_isr_work(struct work_struct *work)
{
	struct mtk_pci_priv *priv = container_of(work, struct mtk_pci_priv, mhccif_work);
	struct mtk_md_dev *mdev = priv->irq_desc->mdev;
	struct mtk_mhccif_cb *cb;
	u32 stat, mask, chs;

	if (priv->cfg->flag & MTK_CFG_DFLT_DISABLE_L1SS)
		mtk_pci_disable_l1ss_ds(mdev,
					L1SS_BIT_L1_1(L1SS_EXT_EVT) | L1SS_BIT_L1_2(L1SS_EXT_EVT),
					MAC_ACTIVE_EXT_EVT_L1SS);

	stat = mtk_pci_get_ext_evt_hw_status(mdev);
	mask = mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr
		+ MHCCIF_EP2RC_SW_INT_EAP_MASK);
	MTK_INFO(mdev, "External events: mhccif_stat=0x%08x mask=0x%08x\n", stat, mask);

	if (unlikely(stat == U32_MAX && !(mtk_pci_link_check(mdev)))) {
		/* When link failed, we don't need to unmask/clear. */
		MTK_ERR(mdev, "Failed to check link in MHCCIF handler.\n");
		mtk_exception_report_evt(mdev, EXCEPTION_LINK_ERR);
		return;
	}

#if IS_ENABLED(CONFIG_GOOGLE_B507778536_DEBUG)
	if (unlikely(stat == U32_MAX && mask == U32_MAX)) {
		mtk_exception_report_evt(mdev, EXCEPTION_LINK_ERR);
		return;
	}
#endif

	stat &= ~mask;
	chs = mtk_pci_ext_d2h_evt_chs(stat);
	spin_lock_bh(&priv->mhccif_lock);
	list_for_each_entry(cb, &priv->mhccif_cb_list, entry) {
		if (cb->chs & chs)
			cb->evt_cb(cb->chs & chs, cb->data);
	}
	spin_unlock_bh(&priv->mhccif_lock);

	if (priv->cfg->flag & MTK_CFG_DFLT_DISABLE_L1SS) {
		/* We must use the 1 bit to not conflict with low power */
		mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L1(L1SS_EXT_EVT), MAC_ACTIVE_EXT_EVT_L1SS);
		stat = mtk_pci_get_ext_evt_hw_status(mdev);
		/* At this point, we read MHCCIF not for handling the channels.
		 * So not checking link status couldn't cause critical issue in some corner case.
		 */
		mask = mtk_pci_read32(mdev, priv->cfg->mhccif_rc_base_addr
			+ MHCCIF_EP2RC_SW_INT_EAP_MASK);
		stat &= ~mask;
		if (!stat)
			mtk_pci_enable_l1ss_ds(mdev, L1SS_BIT_L1_1(L1SS_EXT_EVT) |
					       L1SS_BIT_L1_2(L1SS_EXT_EVT),
					       MAC_ACTIVE_EXT_EVT_L1SS);
	}

	mtk_pci_clear_irq(mdev, priv->mhccif_irq_id);
	mtk_pci_unmask_irq(mdev, priv->mhccif_irq_id);
}

MODULE_DEVICE_TABLE(pci, mtk_pci_ids);

static int mtk_pci_bar_init(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 bar[BAR_NUM];
	int i, ret;

	for (i = 0; i < BAR_NUM; i++)
		pci_read_config_dword(to_pci_dev(mdev->dev),
				      PCI_BASE_ADDRESS_0 + (i << 2), bar + i);

	ret = pcim_iomap_regions(pdev, MTK_REQUESTED_BARS, mdev->dev_str);
	if (ret) {
		MTK_ERR(mdev, "Failed to init MMIO. ret=%d\n", ret);
		return ret;
	}

	/* get ioremapped memory */
	priv->mac_reg_base = pcim_iomap_table(pdev)[MTK_BAR_0_1_IDX];
	priv->bar23_addr = pcim_iomap_table(pdev)[MTK_BAR_2_3_IDX];
	priv->bar45_addr = pcim_iomap_table(pdev)[MTK_BAR_4_5_IDX];
	if (!priv->mac_reg_base || !priv->bar23_addr || !priv->bar45_addr) {
		MTK_ERR(mdev, "Failed to init BAR.\n");
		return -EINVAL;
	}
	MTK_INFO(mdev, "BAR Addr 0/1:0x%llx, BAR2/3 Addr=0x%llx, BAR4/5 Addr=0x%llx\n",
		 priv->mac_reg_base, priv->bar23_addr, priv->bar45_addr);
	MTK_INFO(mdev, "BAR0~5: 0x%08x/0x%08x/0x%08x/0x%08x/0x%08x/0x%08x\n",
		 bar[0], bar[1], bar[2], bar[3], bar[4], bar[5]);

	/* We use MD view base address "0" to observe registers */
	priv->ext_reg_base = priv->bar23_addr - ATR_PCIE_REG_TRSL_ADDR;

	return 0;
}

static void mtk_pci_bar_exit(struct mtk_md_dev *mdev)
{
	pcim_iounmap_regions(to_pci_dev(mdev->dev), MTK_REQUESTED_BARS);
}

static int mtk_mhccif_irq_cb(int irq_id, void *data)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_priv *priv;

	priv = mdev->hw_priv;
	queue_work(priv->mhccif_wq, &priv->mhccif_work);

	return 0;
}

static int mtk_mhccif_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int ret;

	INIT_LIST_HEAD(&priv->mhccif_cb_list);
	spin_lock_init(&priv->mhccif_lock);
	INIT_WORK(&priv->mhccif_work, mtk_mhccif_isr_work);

	priv->mhccif_wq = alloc_workqueue("mhccif_wq", WQ_HIGHPRI | WQ_UNBOUND, 0);
	if (!priv->mhccif_wq) {
		MTK_ERR(mdev, "Failed to allocate mhccif workqueue\n");
		ret = -ENOMEM;
		goto out;
	}

	ret = mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_MHCCIF);
	if (ret < 0) {
		MTK_ERR(mdev, "Failed to get mhccif_irq_id. ret=%d\n", ret);
		goto free_mhccif_wq;
	}
	priv->mhccif_irq_id = ret;

	ret = mtk_pci_register_irq(mdev, priv->mhccif_irq_id, mtk_mhccif_irq_cb, mdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to register mhccif_irq callback\n");
		goto free_mhccif_wq;
	}

	return 0;

free_mhccif_wq:
	destroy_workqueue(priv->mhccif_wq);
out:
	return ret;
}

static void mtk_mhccif_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	mtk_pci_unregister_irq(mdev, priv->mhccif_irq_id);
	cancel_work_sync(&priv->mhccif_work);
	destroy_workqueue(priv->mhccif_wq);
}

static void mtk_rgu_work(struct work_struct *work)
{
	struct mtk_pci_priv *priv;
	struct mtk_md_dev *mdev;
	struct pci_dev *pdev;
	int ret;

	priv = container_of(to_delayed_work(work), struct mtk_pci_priv, rgu_work);
	mdev = priv->mdev;
	pdev = to_pci_dev(mdev->dev);

	MTK_INFO(mdev, "RGU work\n");

	mtk_pci_mask_irq(mdev, priv->rgu_irq_id);
	mtk_pci_clear_irq(mdev, priv->rgu_irq_id);

	ret = mtk_exception_report_evt(mdev, EXCEPTION_RGU);
	if (ret)
		MTK_ERR(mdev, "Failed to report exception with EXCEPTION_RGU\n");

	if (priv->cfg->flag & MTK_CFG_RGU_L2_AUTO_ACK && pdev->msix_enabled)
		mtk_pci_unmask_irq(mdev, priv->rgu_irq_id);
}

static int mtk_rgu_irq_cb(int irq_id, void *data)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_priv *priv;
	struct pci_dev *pdev;

	priv = mdev->hw_priv;
	pdev = to_pci_dev(mdev->dev);

	if (delayed_work_pending(&priv->rgu_work))
		goto exit;
	schedule_delayed_work(&priv->rgu_work, msecs_to_jiffies(1));

exit:
	return 0;
}

static int mtk_rgu_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int ret;

	ret = mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_SAP_RGU);
	if (ret < 0) {
		MTK_ERR(mdev, "Failed to get rgu_irq_id. ret=%d\n", ret);
		goto err;
	}
	priv->rgu_irq_id = ret;

	INIT_DELAYED_WORK(&priv->rgu_work, mtk_rgu_work);

	mtk_pci_mask_irq(mdev, priv->rgu_irq_id);
	mtk_pci_clear_irq(mdev, priv->rgu_irq_id);

	ret = mtk_pci_register_irq(mdev, priv->rgu_irq_id, mtk_rgu_irq_cb, mdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to register rgu_irq callback\n");
		goto err;
	}

	mtk_pci_unmask_irq(mdev, priv->rgu_irq_id);

err:
	return ret;
}

static void mtk_rgu_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	mtk_pci_unregister_irq(mdev, priv->rgu_irq_id);
	cancel_delayed_work_sync(&priv->rgu_work);
}

static irqreturn_t mtk_pci_irq_handler(struct mtk_md_dev *mdev, u32 irq_state)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int irq_id;

	/* Check whether each set bit has a callback, if has, call it */
	do {
		irq_id = fls(irq_state) - 1;
		irq_state &= ~BIT(irq_id);
#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
		md2ap_wakemon_pci_irq(mdev->google, irq_id);
#endif
		if (likely(priv->irq_cb_list[irq_id]))
			priv->irq_cb_list[irq_id](irq_id, priv->irq_cb_data[irq_id]);
		else
			MTK_ERR(mdev, "Unhandled irq_id=%d, no callback for it.\n", irq_id);
	} while (irq_state);

	return IRQ_HANDLED;
}

static irqreturn_t mtk_pci_irq_legacy(int irq, void *data)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_priv *priv;
	u32 irq_state, irq_enable;

	priv = mdev->hw_priv;
	irq_state = mtk_pci_mac_read32(priv, REG_ISTATUS_HOST);
	irq_enable = mtk_pci_mac_read32(priv, REG_INT_ENABLE_HOST);
	irq_state &= irq_enable;

	if (unlikely(!irq_state))
		return IRQ_NONE;

	/* Mask the bit and user needs to unmask by itself */
	mtk_pci_mac_write32(priv, REG_INT_ENABLE_HOST_CLR, irq_state);

	return mtk_pci_irq_handler(mdev, irq_state);
}

static irqreturn_t mtk_pci_irq_msi(int irq, void *data)
{
	struct mtk_pci_irq_desc *irq_desc = data;
	struct mtk_md_dev *mdev = irq_desc->mdev;
	struct mtk_pci_priv *priv;
	u32 irq_state, irq_enable;

	priv = mdev->hw_priv;
	irq_state = mtk_pci_mac_read32(priv, REG_ISTATUS_HOST);
	irq_enable = mtk_pci_mac_read32(priv, REG_INT_ENABLE_HOST);

	irq_state &= irq_enable & irq_desc->msix_bits;

	if (unlikely(!irq_state))
		return IRQ_NONE;

	/* Mask the bit and user needs to unmask by itself */
	mtk_pci_mac_write32(priv, REG_INT_ENABLE_HOST_CLR, irq_state);

	return mtk_pci_irq_handler(mdev, irq_state);
}

static irqreturn_t mtk_pci_irq_msix(int irq, void *data)
{
	struct mtk_pci_irq_desc *irq_desc = data;
	struct mtk_md_dev *mdev = irq_desc->mdev;
	struct mtk_pci_priv *priv;
	u32 irq_state, irq_enable;

	priv = mdev->hw_priv;
	irq_state = mtk_pci_mac_read32(priv, REG_MSIX_ISTATUS_HOST_GRP0_0);
	irq_enable = mtk_pci_mac_read32(priv, REG_IMASK_HOST_MSIX_GRP0_0);
	irq_state &= irq_enable & irq_desc->msix_bits;

	if (unlikely(!irq_state)) {
		MTK_INFO(mdev, "IRQ none, irq_desc->msix_bits = %x\n", irq_desc->msix_bits);
		return IRQ_NONE;
	}

	/* Mask the bit and user needs to unmask by itself */
	mtk_pci_mac_write32(priv, REG_IMASK_HOST_MSIX_CLR_GRP0_0, irq_state & ~BIT(30));

	return mtk_pci_irq_handler(mdev, irq_state);
}

static int mtk_pci_request_irq_legacy(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_pci_irq_desc *irq_desc;
	int ret;

	irq_desc = priv->irq_desc;
	snprintf(irq_desc->name, MTK_IRQ_NAME_LEN, "legacy-%s", mdev->dev_str);
	ret = pci_request_irq(pdev, 0, mtk_pci_irq_legacy, NULL, mdev, irq_desc->name);
	if (ret) {
		MTK_ERR(mdev, "Failed to request legacy irq: ret=%d\n", ret);
		return ret;
	}
	irq_desc->mdev = mdev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
	priv->irq_type = PCI_IRQ_LEGACY;
#else
	priv->irq_type = PCI_IRQ_INTX;
#endif
	priv->irq_cnt = MTK_IRQ_CNT_MIN;

	return 0;
}

static int mtk_pci_request_irq_msi(struct mtk_md_dev *mdev, int irq_cnt_allocated)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_pci_irq_desc *irq_desc;
	struct pci_dev *pdev;
	int irq_cnt;
	int ret, i;

	/* calculate the nearest 2's power number */
	irq_cnt = BIT(fls(irq_cnt_allocated) - 1);
	pdev = to_pci_dev(mdev->dev);
	irq_desc = priv->irq_desc;
	if (irq_cnt != irq_cnt_allocated) {
		MTK_INFO(mdev, "%d irqs have been allocated, but only %d irqs are used\n",
			 irq_cnt_allocated, irq_cnt);
	}

	for (i = 0; i < irq_cnt; i++) {
		irq_desc[i].mdev = mdev;
		if (irq_cnt == MTK_IRQ_CNT_MAX)
			irq_desc[i].msix_bits = BIT(i);
		else
			irq_desc[i].msix_bits = mtk_msix_bits_map[i][ffs(irq_cnt) - 1];
		snprintf(irq_desc[i].name, MTK_IRQ_NAME_LEN, "msi%d-%s", i, mdev->dev_str);
		ret = pci_request_irq(pdev, i, mtk_pci_irq_msi, NULL,
				      &irq_desc[i], irq_desc[i].name);
		if (ret) {
			MTK_ERR(mdev, "Failed to request %s: ret=%d\n", irq_desc[i].name, ret);
			for (i--; i >= 0; i--)
				pci_free_irq(pdev, i, &irq_desc[i]);
			return ret;
		}
	}

	priv->irq_cnt = irq_cnt;
	priv->irq_type = PCI_IRQ_MSI;

	if (irq_cnt != MTK_IRQ_CNT_MAX)
		mtk_pci_set_msi_merged(priv, irq_cnt);

	MTK_INFO(mdev, "%d msi irqs have been allocated, irqs merged cnt is %d\n",
		 irq_cnt_allocated, irq_cnt);

	return 0;
}

static int mtk_pci_request_irq_msix(struct mtk_md_dev *mdev, int irq_cnt_allocated)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_pci_irq_desc *irq_desc;
	struct pci_dev *pdev;
	int irq_cnt;
	int ret, i;

	/* calculate the nearest 2's power number */
	irq_cnt = BIT(fls(irq_cnt_allocated) - 1);
	pdev = to_pci_dev(mdev->dev);
	irq_desc = priv->irq_desc;
	if (irq_cnt != irq_cnt_allocated) {
		MTK_INFO(mdev, "%d irqs have been allocated, but only %d irqs are used\n",
			 irq_cnt_allocated, irq_cnt);
	}

	for (i = 0; i < irq_cnt; i++) {
		irq_desc[i].mdev = mdev;
		if (irq_cnt == MTK_IRQ_CNT_MAX)
			irq_desc[i].msix_bits = BIT(i);
		else
			irq_desc[i].msix_bits = mtk_msix_bits_map[i][ffs(irq_cnt) - 1];

		snprintf(irq_desc[i].name, MTK_IRQ_NAME_LEN, "msix%d-%s", i, mdev->dev_str);
		ret = pci_request_irq(pdev, i, mtk_pci_irq_msix, NULL,
				      &irq_desc[i], irq_desc[i].name);
		if (ret) {
			MTK_ERR(mdev, "Failed to request %s: ret=%d\n", irq_desc[i].name, ret);
			for (i--; i >= 0; i--)
				pci_free_irq(pdev, i, &irq_desc[i]);
			return ret;
		}
	}
	priv->irq_cnt = irq_cnt;
	priv->irq_type = PCI_IRQ_MSIX;

	if (irq_cnt != MTK_IRQ_CNT_MAX)
		mtk_pci_set_msix_merged(priv, irq_cnt);

	MTK_INFO(mdev, "%d msix irqs have been allocated, irqs merged cnt is %d\n",
		 irq_cnt_allocated, irq_cnt);

	return 0;
}

static int mtk_pci_request_irq(struct mtk_md_dev *mdev, int max_irq_cnt, int irq_type)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	int irq_cnt;
	int ret;

	if (max_irq_cnt < MTK_IRQ_CNT_MIN || max_irq_cnt > MTK_IRQ_CNT_MAX)
		return -EINVAL;

	irq_cnt = pci_alloc_irq_vectors(pdev, MTK_IRQ_CNT_MIN, max_irq_cnt, irq_type);

	if (irq_cnt < MTK_IRQ_CNT_MIN) {
		MTK_ERR(mdev,
			"Unable to alloc pci irq vectors. ret=%d maxirqcnt=%d irqtype=0x%x",
			irq_cnt, max_irq_cnt, irq_type);
		return -EINVAL;
	}

	if (pdev->msix_enabled)
		ret = mtk_pci_request_irq_msix(mdev, irq_cnt);
	else if (pdev->msi_enabled)
		ret = mtk_pci_request_irq_msi(mdev, irq_cnt);
	else
		ret = mtk_pci_request_irq_legacy(mdev);

	return ret;
}

static void mtk_pci_free_irq(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	int i;

	if (priv->irq_type == PCI_IRQ_MSIX || priv->irq_type == PCI_IRQ_MSI)
		for (i = 0; i < priv->irq_cnt; i++)
			pci_free_irq(pdev, i, &priv->irq_desc[i]);
	else
		pci_free_irq(pdev, 0, mdev);

	pci_free_irq_vectors(pdev);
}

static int __mtk_str_begin_with(const char *str, const char *begin)
{
	return !strncmp(str, begin, strlen(begin));
}

static ssize_t mtk_pci_dbg_write_reset(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;
	enum mtk_reset_type type;
	int ret;

	if (__mtk_str_begin_with(buf, "rgu"))
		type = RESET_MHCCIF;
	else if (__mtk_str_begin_with(buf, "fldr"))
		type = RESET_FLDR;
	else if (__mtk_str_begin_with(buf, "pldr"))
		type = RESET_PLDR;
	else
		return -EINVAL;

	ret = mtk_pci_reset(mdev, type);
	MTK_INFO(mdev, "Reset done, buf=%s, ret=%d\n", buf, ret);
	if (ret)
		return ret;

	return cnt;
}

MTK_DBGFS(reset, NULL, mtk_pci_dbg_write_reset);

static ssize_t mtk_pci_dbg_write_reinit(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;
	enum mtk_reinit_type type;
	int ret;

	if (__mtk_str_begin_with(buf, "exp"))
		type = REINIT_TYPE_EXP;
	else if (__mtk_str_begin_with(buf, "resume"))
		type = REINIT_TYPE_RESUME;
	else
		return -EINVAL;

	ret = mtk_pci_reinit(mdev, type);
	MTK_INFO(mdev, "Reinit done, buf=%s, ret = %d\n", buf, ret);
	if (ret)
		return ret;

	return cnt;
}

MTK_DBGFS(reinit, NULL, mtk_pci_dbg_write_reinit);

static ssize_t mtk_pci_dbg_read_link(void *data, char *buf, ssize_t max_cnt)
{
	struct mtk_md_dev *mdev = data;
	struct pci_dev *pdev;
	bool ret, tmp;
	u16 vend;

	pdev = to_pci_dev(mdev->dev);
	pci_read_config_word(pdev, PCI_VENDOR_ID, &vend);

	ret = mtk_pci_link_check(mdev);
	tmp = vend != pdev->vendor;

	MTK_INFO(mdev, "ret=%d tmp=%d vend=0x%04X mdev=%p\n", ret, tmp, vend, mdev);

	return snprintf(buf, max_cnt, "link %s\n", ret ? "ok" : "down");
}

MTK_DBGFS(link, mtk_pci_dbg_read_link, NULL);

static ssize_t mtk_pci_dbg_read_hp(void *data, char *buf, ssize_t max_cnt)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_priv *priv;

	priv = mdev->hw_priv;
	return snprintf(buf, max_cnt, "%s\n", priv->rc_hp_on ? "on" : "off");
}

MTK_DBGFS(hp, mtk_pci_dbg_read_hp, NULL); /* note that */

static ssize_t mtk_pci_dbg_write_dump(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;

	if (__mtk_str_begin_with(buf, "dump")) {
		mtk_pci_info_dump(mdev);
	} else if (__mtk_str_begin_with(buf, "rw_test_enable")) {
		MTK_INFO(mdev, "Enable mmio test interface\n");
		mtk_pci_stress_test_enable = 1;
	} else if (__mtk_str_begin_with(buf, "rw_test_disable")) {
		MTK_INFO(mdev, "Disable mmio test interface\n");
		mtk_pci_stress_test_enable = 0;
	} else {
		return -EINVAL;
	}

	return cnt;
}

MTK_DBGFS(pci_dbg, NULL, mtk_pci_dbg_write_dump);

static ssize_t mtk_pci_dbg_read_test_res(void *data, char *buf, ssize_t max_cnt)
{
	return snprintf(buf, max_cnt, "0x%x\n", mtk_pci_read_test_res);
}

static ssize_t mtk_pci_dbg_read_test(void *data, const char *buf, ssize_t cnt)
{
	u32 check, mask = 0xFFFFFFFF, loop;
	int ret = -EFAULT, args_num = 0;
	struct mtk_md_dev *mdev = data;
	char *kbuf, *kstr, *args[3];
	struct mtk_pci_priv *priv;
	u64 addr;

	priv = mdev->hw_priv;

	if (!mtk_pci_stress_test_enable) {
		MTK_ERR(mdev, "Unsupported read test operation\n");
		return -EOPNOTSUPP;
	}

	kbuf = kstrdup(buf, GFP_KERNEL);
	if (!kbuf) {
		MTK_ERR(mdev, "Failed to duplicate user buffer: %s\n", buf);
		return -ENOMEM;
	}

	kstr = kbuf;
	memset(args, 0, sizeof(args));
	while (kstr && args_num < 3)
		args[args_num++] = strsep(&kstr, " ");

	if (kstr) {
		MTK_ERR(mdev, "Too many arguments in: %s\n", buf);
		goto read_end;
	}

	ret = kstrtou64(args[0], 16, &addr);
	if (ret) {
		MTK_ERR(mdev, "Failed to convert address to u64: %s, ret=%d\n", args[0], ret);
		goto read_end;
	}

	if (args[1]) {
		ret = kstrtou32(args[1], 16, &check);
		if (ret) {
			MTK_ERR(mdev, "Failed to convert data to u32: %s, ret=%d\n", args[1], ret);
			goto read_end;
		}

		if (args[2]) {
			ret = kstrtou32(args[2], 16, &mask);
			if (ret) {
				MTK_ERR(mdev, "Failed to convert mask to u32: %s, ret=%d\n",
					args[2], ret);
				goto read_end;
			}
		}
	}

	for (loop = 0; loop < mtk_pci_stress_test_loop; loop++) {
		if (addr & BIT(31))
			mtk_pci_read_test_res = mtk_pci_read32_bar45(mdev, (addr & ~BIT(31)));
		else if (addr & BIT(28))
			mtk_pci_read_test_res = mtk_pci_read32(mdev, addr);
		else if (addr & BIT(24))
			pci_read_config_dword(to_pci_dev(mdev->dev), (addr & ~BIT(24)),
					      &mtk_pci_read_test_res);
		else
			mtk_pci_read_test_res = mtk_pci_mac_read32(priv, addr);

		if (args[1] && check != (mtk_pci_read_test_res & mask))
			MTK_ERR(mdev, "Read value=0x%x is not equal to 0x%x\n",
				mtk_pci_read_test_res & mask, check);

		if (mtk_pci_stress_test_delay_us)
			udelay(mtk_pci_stress_test_delay_us);

		if (mtk_pci_stress_test_log)
			MTK_INFO(mdev, "Read addr=0x%x, val=0x%x\n", addr, mtk_pci_read_test_res);
	}

	ret = cnt;

read_end:
	kfree(kbuf);
	return ret;
}

MTK_DBGFS(read_test, mtk_pci_dbg_read_test_res, mtk_pci_dbg_read_test); /* note that */

static ssize_t mtk_pci_dbg_write_test(void *data, const char *buf, ssize_t cnt)
{
	int ret = -EFAULT, args_num = 0;
	struct mtk_md_dev *mdev = data;
	u32 val, loop, mask = 0, rval;
	char *kbuf, *kstr, *args[3];
	struct mtk_pci_priv *priv;
	u64 addr;

	priv = mdev->hw_priv;

	if (!mtk_pci_stress_test_enable) {
		MTK_ERR(mdev, "Unsupported write test operation\n");
		return -EOPNOTSUPP;
	}

	kbuf = kstrdup(buf, GFP_KERNEL);
	if (!kbuf) {
		MTK_ERR(mdev, "Failed to duplicate user buffer: %s\n", buf);
		return -ENOMEM;
	}

	kstr = kbuf;
	memset(args, 0, sizeof(args));
	while (kstr && args_num < 3)
		args[args_num++] = strsep(&kstr, " ");

	if (kstr) {
		MTK_ERR(mdev, "Too many arguments in: %s\n", buf);
		goto write_end;
	}

	if (args_num < 2) {
		MTK_ERR(mdev, "Too less arguments in: %s\n", buf);
		goto write_end;
	}

	ret = kstrtou64(args[0], 16, &addr);
	if (ret) {
		MTK_ERR(mdev, "Failed to convert address to u64: %s, ret=%d\n", args[0], ret);
		goto write_end;
	}

	ret = kstrtou32(args[1], 16, &val);
	if (ret) {
		MTK_ERR(mdev, "Failed to convert data to u32: %s, ret=%d\n", args[1], ret);
		goto write_end;
	}

	if (args[2]) {
		ret = kstrtou32(args[2], 16, &mask);
		if (ret) {
			MTK_ERR(mdev, "Failed to convert mask to u32: %s, ret=%d\n", args[2], ret);
			goto write_end;
		}
	}

	for (loop = 0; loop < mtk_pci_stress_test_loop; loop++) {
		if (mask) {
			val &= mask;
			if (addr & BIT(31)) {
				val |= (mtk_pci_read32_bar45(mdev, (addr & ~BIT(31))) & ~mask);
			} else if (addr & BIT(28)) {
				val |= (mtk_pci_read32(mdev, addr) & ~mask);
			} else if (addr & BIT(24)) {
				pci_read_config_dword(to_pci_dev(mdev->dev), (addr & ~BIT(24)),
						      &rval);
				val |= (rval & ~mask);
			} else {
				val |= (mtk_pci_mac_read32(priv, addr) & ~mask);
			}
		}

		if (addr & BIT(31))
			mtk_pci_write32_bar45(mdev, (addr & ~BIT(31)), val);
		else if (addr & BIT(28))
			mtk_pci_write32(mdev, addr, val);
		else if (addr & BIT(24))
			pci_write_config_dword(to_pci_dev(mdev->dev), (addr & ~BIT(24)), val);
		else
			mtk_pci_mac_write32(priv, addr, val);

		if (mtk_pci_stress_test_delay_us)
			udelay(mtk_pci_stress_test_delay_us);

		if (mtk_pci_stress_test_log)
			MTK_INFO(mdev, "Write addr=0x%x, val=0x%x, mask=0x%x\n", addr, val, mask);
	}

	ret = cnt;

write_end:
	kfree(kbuf);
	return ret;
}

MTK_DBGFS(write_test, NULL, mtk_pci_dbg_write_test); /* note that */

#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
static ssize_t mtk_pci_dbg_set_rpm_link_state(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;
	struct handshake_info hs_info;
	struct mtk_pci_pm *pm;

	pm = mdev_get_pm(mdev);

	if (mtk_pm_smart_suspend_enabled()) {
		MTK_WARN(mdev, "smart suspend is enabled, can not switch rpm mode!");
		return cnt;
	}

	if (__mtk_str_begin_with(buf, "L1")) {
		hs_info.feature_id = PCIE_RPM_CTRL;
		hs_info.data[0] = RPM_LINK_STATE_L12;
		pm->rpm_link_state = RPM_LINK_STATE_L12;
		MTK_INFO(mdev, "Set rpm link state to L1\n");
	} else if (__mtk_str_begin_with(buf, "L2")) {
		hs_info.feature_id = PCIE_RPM_CTRL;
		hs_info.data[0] = RPM_LINK_STATE_L2;
		pm->rpm_link_state = RPM_LINK_STATE_L2;
		MTK_INFO(mdev, "Set rpm link state to L2\n");
	} else {
		MTK_INFO(mdev, "Invalid param!--->%s\n", buf);
		return cnt;
	}

	mtk_pm_runtime_get(mdev, MTK_USER_HW, true);
	if (mtk_pcie_ep_set_info(PORT1, &hs_info))
		MTK_INFO(mdev, "Set rpm link state fail!\n");
	mtk_pm_runtime_put(mdev, MTK_USER_HW, false);

	return cnt;
}

MTK_DBGFS(pci_rpm_state, NULL, mtk_pci_dbg_set_rpm_link_state); /* note that */
#endif

static ssize_t mtk_pci_dbg_link_ctrl(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_pci_priv *priv;

	/* trigger EP link failure */
	if (__mtk_str_begin_with(buf, "linkdown")) {
		priv = mdev->hw_priv;
		iowrite32(0x2, priv->mac_reg_base + (u64)0x148);
		MTK_INFO(mdev, "trigger link down done\n");
	}
	if (__mtk_str_begin_with(buf, "aspm")) {
		mtk_pci_aspm_ctrl(mdev);
		MTK_INFO(mdev, "trigger ASPM control\n");
	}

	return cnt;
}

MTK_DBGFS(link_ctrl, NULL, mtk_pci_dbg_link_ctrl);

static ssize_t mtk_pci_dbg_get_dev_log(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_md_dev *mdev = data;

	int device_log_type;
	int buffer_size = 0;
	char *log_buffer;
	int loop;

	if (__mtk_str_begin_with(buf, "get_BROM_SRAM_logN")) {
		device_log_type = MTK_DEV_LOG_BROM_SRAM;
		buffer_size = mtk_dev_get_log_region_size(mdev, device_log_type);
		log_buffer = vzalloc(buffer_size);
		MTK_INFO(mdev, "start to get BROM SRAM log with buffer_size:%d\n", buffer_size);
		for (loop = 0; loop < mtk_pci_stress_test_loop; loop++) {
			mtk_dev_get_dev_log(mdev, log_buffer, buffer_size, device_log_type);
			if (((loop + 1) % 10000) == 0)
				MTK_INFO(mdev, "Get BROM SRAM log count: %u done\n", loop);
		}
		vfree(log_buffer);
	} else if (__mtk_str_begin_with(buf, "get_PL_SRAM_logN")) {
		device_log_type = MTK_DEV_LOG_PL_SRAM;
		buffer_size = mtk_dev_get_log_region_size(mdev, device_log_type);
		log_buffer = vzalloc(buffer_size);
		MTK_INFO(mdev, "start to get PL SRAM log with buffer_size:%d\n", buffer_size);
		for (loop = 0; loop < mtk_pci_stress_test_loop; loop++) {
			mtk_dev_get_dev_log(mdev, log_buffer, buffer_size, device_log_type);
			if (((loop + 1) % 10000) == 0)
				MTK_INFO(mdev, "Get PL SRAM log count: %u done\n", loop);
		}
		vfree(log_buffer);
	} else if (__mtk_str_begin_with(buf, "get_PL_DRAM_logN")) {
		device_log_type = MTK_DEV_LOG_PL_DRAM;
		buffer_size = mtk_dev_get_log_region_size(mdev, device_log_type);
		log_buffer = vzalloc(buffer_size);
		MTK_INFO(mdev, "start to get PL DRAM log with buffer_size:%d\n", buffer_size);
		for (loop = 0; loop < mtk_pci_stress_test_loop; loop++) {
			mtk_dev_get_dev_log(mdev, log_buffer, buffer_size, device_log_type);
			if (((loop + 1) % 10000) == 0)
				MTK_INFO(mdev, "Get PL DRAM log count: %u done\n", loop);
		}
		vfree(log_buffer);
	} else if (__mtk_str_begin_with(buf, "get_ATF_DRAM_logN")) {
		device_log_type = MTK_DEV_LOG_ATF_DRAM;
		buffer_size = mtk_dev_get_log_region_size(mdev, device_log_type);
		log_buffer = vzalloc(buffer_size);
		MTK_INFO(mdev, "start to get ATF DRAM log with buffer_size:%d\n", buffer_size);
		for (loop = 0; loop < mtk_pci_stress_test_loop; loop++) {
			mtk_dev_get_dev_log(mdev, log_buffer, buffer_size, device_log_type);
			if (((loop + 1) % 10000) == 0)
				MTK_INFO(mdev, "Get ATF DRAM log count: %u done\n", loop);
		}
		vfree(log_buffer);
	} else {
		return -EINVAL;
	}

	return cnt;
}

MTK_DBGFS(get_dev_log, NULL, mtk_pci_dbg_get_dev_log); /* note that */

static ssize_t mtk_pci_dbg_config_atr_test(void *data, const char *buf, ssize_t cnt)
{
	u32 src_addr_offset, trsl_addr, atr_size;
	int ret = -EFAULT, args_num = 0;
	struct mtk_md_dev *mdev = data;
	char *kbuf, *kstr, *args[3];
	struct mtk_atr_cfg cfg;
	struct pci_dev *pdev;

	pdev = to_pci_dev(mdev->dev);

	if (!mtk_pci_stress_test_enable) {
		MTK_ERR(mdev, "Unsupported config atr test operation\n");
		return -EOPNOTSUPP;
	}

	kbuf = kstrdup(buf, GFP_KERNEL);
	if (!kbuf) {
		MTK_ERR(mdev, "Failed to duplicate user buffer: %s\n", buf);
		return -ENOMEM;
	}

	kstr = kbuf;
	memset(args, 0, sizeof(args));
	while (kstr && args_num < 3)
		args[args_num++] = strsep(&kstr, " ");

	if (kstr) {
		MTK_ERR(mdev, "Too many arguments in: %s\n", buf);
		goto config_atr_end;
	}

	if (args_num < 3) {
		MTK_ERR(mdev, "Too less arguments in: %s\n", buf);
		goto config_atr_end;
	}

	ret = kstrtou32(args[0], 16, &src_addr_offset);
	if (ret) {
		MTK_ERR(mdev, "Failed to convert src address offset to u32: %s, ret=%d\n", args[0],
			ret);
		goto config_atr_end;
	}

	ret = kstrtou32(args[1], 16, &atr_size);
	if (ret) {
		MTK_ERR(mdev, "Failed to convert size to u32: %s, ret=%d\n", args[1], atr_size);
		goto config_atr_end;
	}

	if (src_addr_offset + atr_size > ATR_PCIE_REG_SIZE) {
		MTK_ERR(mdev, "ATR size exceeds maximum value: 0x%x + 0x%x\n", src_addr_offset,
			atr_size);
		goto config_atr_end;
	}

	ret = kstrtou32(args[2], 16, &trsl_addr);
	if (ret) {
		MTK_ERR(mdev, "Failed to convert trsl address to u32: %s, ret=%d\n", args[2], ret);
		goto config_atr_end;
	}

	if (mtk_pci_config_atr_test_win > 1 || mtk_pci_config_atr_test_table > 7) {
		MTK_ERR(mdev, "Invalid ATR config window: %d, table: %d\n",
			mtk_pci_config_atr_test_win, mtk_pci_config_atr_test_table);
		goto config_atr_end;
	}

	MTK_INFO(mdev, "Config ATR win%d tbl%d: src addr offset=0x%x, trsl addr=0x%x, size=0x%x\n",
		 mtk_pci_config_atr_test_win, mtk_pci_config_atr_test_table,
		 src_addr_offset, trsl_addr, atr_size);

	if (mtk_pci_config_atr_test_win == 0) {
		cfg.src_addr = pci_resource_start(pdev, MTK_BAR_2_3_IDX) + src_addr_offset;
		cfg.port = ATR_SRC_PCI_WIN0;
	} else {
		cfg.src_addr = pci_resource_start(pdev, MTK_BAR_4_5_IDX) + src_addr_offset;
		cfg.port = ATR_SRC_PCI_WIN1;
	}

	cfg.size = atr_size;
	cfg.trsl_addr = trsl_addr;
	cfg.type = ATR_PCI2AXI;
	cfg.table = mtk_pci_config_atr_test_table;
	cfg.trsl_id = ATR_PCIE_REG_TRSL_PORT;
	cfg.trsl_param = 0x0;
	cfg.transparent = 0x0;
	ret = mtk_pci_setup_atr(mdev, &cfg);
	if (ret)
		goto config_atr_end;

	ret = cnt;

config_atr_end:
	kfree(kbuf);
	return ret;
}

MTK_DBGFS(config_atr_test, NULL, mtk_pci_dbg_config_atr_test); /* note that */

static void mtk_pci_dbgfs_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct dentry *dentry;

	dentry = mtk_dbgfs_create_dir(mtk_get_dev_dentry(mdev), "pci");
	if (IS_ERR_OR_NULL(dentry))
		return;
	priv->dentry = dentry;

	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_reset, mdev);
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_reinit, mdev);
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_link, mdev);
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_hp, mdev);
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_pci_dbg, mdev);
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_pci_rpm_state, mdev);
#endif
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_link_ctrl, mdev);
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_get_dev_log, mdev);
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_read_test, mdev);
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_write_test, mdev);
	mtk_dbgfs_create_file(dentry, &mtk_dbgfs_config_atr_test, mdev);
}

static void mtk_pci_dbgfs_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	mtk_dbgfs_remove(priv->dentry);
}

static void mtk_pci_save_state(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);

	pci_save_state(pdev);
#if defined(CONFIG_PCIEASPM) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0))
	mtk_pci_save_aspm_l1ss_state(mdev);
#endif
}

#if IS_ENABLED(CONFIG_GOOGLE_ASPM_CONTROL)

static inline void mtk_pci_enable_aspm_l1ss(struct mtk_md_dev *mdev)
{
#ifdef CONFIG_PCIEASPM
	struct pci_dev *pdev = to_pci_dev(mdev->dev);

	MTK_INFO(mdev, "Link state setting: 0x%x\n", PCIE_LINK_STATE_ALL);
	pci_enable_link_state(pdev, PCIE_LINK_STATE_ALL);
#endif
}

static inline void mtk_pci_aspm_ctrl(struct mtk_md_dev *mdev)
{
#ifdef CONFIG_PCIEASPM
	if (mtk_pci_link_state_init != 0xFFFF) {
		struct pci_dev *pdev = to_pci_dev(mdev->dev);

		MTK_INFO(mdev, "Link state setting: 0x%x\n", mtk_pci_link_state_init);
		pci_enable_link_state(pdev, mtk_pci_link_state_init);
	}
#endif
}

#else

static inline bool mtk_pci_aspm_is_enable(struct mtk_md_dev *mdev, bool is_l1)
{
#ifdef CONFIG_PCIEASPM
	u32 parent_l1ss_ctl1, child_l1ss_ctl1;
	struct pci_dev *pcidev, *parent;
	u16 p_lnkctl, c_lnkctl;

	pcidev = to_pci_dev(mdev->dev);
	parent = pcidev->bus->self;
	if (!parent || !parent->l1ss || !pcidev->l1ss)
		return false;

	pci_read_config_word(parent, pci_pcie_cap(parent) + PCI_EXP_LNKCTL, &p_lnkctl);
	pci_read_config_word(pcidev, pci_pcie_cap(pcidev) + PCI_EXP_LNKCTL, &c_lnkctl);
	if (is_l1 && (p_lnkctl & c_lnkctl & PCI_EXP_LNKCTL_ASPM_L1)) {
		MTK_INFO(mdev, "ASPM L1 already enabled p:0x%08x, c:0x%08x\n", p_lnkctl, c_lnkctl);
		pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1, &parent_l1ss_ctl1);
		pci_read_config_dword(pcidev, pcidev->l1ss + PCI_L1SS_CTL1, &child_l1ss_ctl1);
		if ((parent_l1ss_ctl1 & child_l1ss_ctl1 & PCI_L1SS_CTL1_L1SS_MASK) == 0xf) {
			MTK_INFO(mdev, "ASPM L1SS already enabled p:0x%08x, c:0x%08x\n",
				 parent_l1ss_ctl1, child_l1ss_ctl1);
			return true;
		}
	} else if (!is_l1 && (c_lnkctl & PCI_EXP_LNKCTL_ASPM_L0S)) {
		MTK_INFO(mdev, "ASPM L0s already enabled 0x%08x\n", c_lnkctl);
		return true;
	}

	return false;
#else
	MTK_WARN(mdev, "CONFIG_PCIEASPM not defined!\n");
	return false;
#endif
}

#ifdef CONFIG_PCIEASPM
#ifndef PCI_EXP_LNKCAP_ASPM_L1
#define PCI_EXP_LNKCAP_ASPM_L1    (0x00000800) /* ASPM L1 support */
#endif
#ifndef PCI_EXP_LNKCAP_ASPM_L0S
#define PCI_EXP_LNKCAP_ASPM_L0S    (0x00000400) /* ASPM L0s support */
#endif

static void mtk_pci_set_aspm_l0s(struct pci_dev *dev, int config)
{
	u16 lnkctl;

	pci_read_config_word(dev, pci_pcie_cap(dev) + PCI_EXP_LNKCTL, &lnkctl);
	lnkctl &= ~PCI_EXP_LNKCTL_ASPM_L0S;
	lnkctl |= config;
	pci_write_config_word(dev, pci_pcie_cap(dev) + PCI_EXP_LNKCTL, lnkctl);
}

static void mtk_pci_config_aspm_l0s(struct mtk_md_dev *mdev, int config)
{
	struct pci_dev *child, *parent;
	u32 lnkcap;

	child = to_pci_dev(mdev->dev);
	parent = pci_upstream_bridge(child);
	if (!parent)
		return;

	pci_read_config_dword(parent, pci_pcie_cap(parent) + PCI_EXP_LNKCAP, &lnkcap);
	if (lnkcap & PCI_EXP_LNKCAP_ASPM_L0S) {
		pci_read_config_dword(child, pci_pcie_cap(child) + PCI_EXP_LNKCAP, &lnkcap);
		if (lnkcap & PCI_EXP_LNKCAP_ASPM_L0S) {
			if (config) {
				mtk_pci_set_aspm_l0s(parent, config);
				mtk_pci_set_aspm_l0s(child, config);
			} else {
				mtk_pci_set_aspm_l0s(child, config);
				mtk_pci_set_aspm_l0s(parent, config);
			}
			return;
		}
	}

	MTK_INFO(mdev, "Config ASPM L0s fail\n");
}

static void mtk_pci_set_aspm_l1(struct pci_dev *dev, int config)
{
	u16 lnkctl;

	pci_read_config_word(dev, pci_pcie_cap(dev) + PCI_EXP_LNKCTL, &lnkctl);
	lnkctl &= ~PCI_EXP_LNKCTL_ASPM_L1;
	lnkctl |= config;
	pci_write_config_word(dev, pci_pcie_cap(dev) + PCI_EXP_LNKCTL, lnkctl);
}

static void mtk_pci_config_aspm_l1(struct mtk_md_dev *mdev, int config)
{
	struct pci_dev *child, *parent;
	u32 lnkcap;

	child = to_pci_dev(mdev->dev);
	parent = pci_upstream_bridge(child);
	if (!parent)
		return;

	pci_read_config_dword(parent, pci_pcie_cap(parent) + PCI_EXP_LNKCAP, &lnkcap);
	if (lnkcap & PCI_EXP_LNKCAP_ASPM_L1) {
		pci_read_config_dword(child, pci_pcie_cap(child) + PCI_EXP_LNKCAP, &lnkcap);
		if (lnkcap & PCI_EXP_LNKCAP_ASPM_L1) {
			if (config) {
				mtk_pci_set_aspm_l1(parent, config);
				mtk_pci_set_aspm_l1(child, config);
			} else {
				mtk_pci_set_aspm_l1(child, config);
				mtk_pci_set_aspm_l1(parent, config);
			}
			return;
		}
	}

	MTK_INFO(mdev, "Config ASPM L1 fail\n");
}

static bool mtk_pci_set_aspm_l1ss(struct pci_dev *dev, int config)
{
	u16 l1ss_cap_ptr;
	u32 l1ss_ctl1;

	l1ss_cap_ptr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss_cap_ptr)
		return false;

	pci_read_config_dword(dev, l1ss_cap_ptr + PCI_L1SS_CTL1, &l1ss_ctl1);
	l1ss_ctl1 &= ~PCI_L1SS_CTL1_L1SS_MASK;
	l1ss_ctl1 |= config;
	pci_write_config_dword(dev, l1ss_cap_ptr + PCI_L1SS_CTL1, l1ss_ctl1);

	return true;
}

static int mtk_pci_check_aspm_l1ss(struct pci_dev *dev, int config)
{
	int config_value = 0;
	u16 l1ss_cap_ptr;
	u32 l1ss_cap;

	l1ss_cap_ptr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss_cap_ptr)
		return 0;

	pci_read_config_dword(dev, l1ss_cap_ptr + PCI_L1SS_CAP, &l1ss_cap);

	if (l1ss_cap & PCI_L1SS_CAP_ASPM_L1_1)
		config_value |= (PCI_L1SS_CTL1_ASPM_L1_1 & config);

	if (l1ss_cap & PCI_L1SS_CAP_ASPM_L1_2)
		config_value |= (PCI_L1SS_CTL1_ASPM_L1_2 & config);

	if (l1ss_cap & PCI_L1SS_CAP_PCIPM_L1_1)
		config_value |= (PCI_L1SS_CTL1_PCIPM_L1_1 & config);

	if (l1ss_cap & PCI_L1SS_CAP_PCIPM_L1_2)
		config_value |= (PCI_L1SS_CTL1_PCIPM_L1_2 & config);

	return config_value;
}

static void mtk_pci_config_aspm_l1ss(struct mtk_md_dev *mdev, int config)
{
	struct pci_dev *child = to_pci_dev(mdev->dev);
	struct pci_dev *parent;
	int config_value;

	parent = pci_upstream_bridge(child);
	if (!parent)
		return;

	config_value = mtk_pci_check_aspm_l1ss(parent, config);
	config_value = mtk_pci_check_aspm_l1ss(child, config_value);
	if (config_value) {
		mtk_pci_set_aspm_l1ss(parent, config_value);
		mtk_pci_set_aspm_l1ss(child, config_value);
	} else {
		mtk_pci_set_aspm_l1ss(child, config_value);
		mtk_pci_set_aspm_l1ss(parent, config_value);
	}
}
#endif

static inline void mtk_pci_enable_aspm_l1ss(struct mtk_md_dev *mdev)
{
#ifdef CONFIG_PCIEASPM
	mtk_pci_config_aspm_l1(mdev, 0);
	mtk_pci_config_aspm_l1ss(mdev, PCI_L1SS_CTL1_ASPM_L1_1 | PCI_L1SS_CTL1_ASPM_L1_2 |
				 PCI_L1SS_CTL1_PCIPM_L1_1 | PCI_L1SS_CTL1_PCIPM_L1_2);
	mtk_pci_config_aspm_l1(mdev, PCI_EXP_LNKCTL_ASPM_L1);
#endif
}

static inline void mtk_pci_aspm_ctrl(struct mtk_md_dev *mdev)
{
#ifdef CONFIG_PCIEASPM
	if (mtk_pci_link_state_init != 0xFF) {
		mtk_pci_config_aspm_l0s(mdev, 0);
		mtk_pci_config_aspm_l1(mdev, 0);
		mtk_pci_config_aspm_l1ss(mdev, 0);

		MTK_INFO(mdev, "Link state setting: 0x%x\n", mtk_pci_link_state_init);
		if ((mtk_pci_link_state_init >> 2) & PCI_L1SS_CTL1_L1SS_MASK)
			mtk_pci_config_aspm_l1ss(mdev, (mtk_pci_link_state_init >> 2) &
						 PCI_L1SS_CTL1_L1SS_MASK);
		if (mtk_pci_link_state_init & PCI_EXP_LNKCTL_ASPM_L0S)
			mtk_pci_config_aspm_l0s(mdev, PCI_EXP_LNKCTL_ASPM_L0S);
		if (mtk_pci_link_state_init & PCI_EXP_LNKCTL_ASPM_L1)
			mtk_pci_config_aspm_l1(mdev, PCI_EXP_LNKCTL_ASPM_L1);
	}
#endif
}

static void mtk_pci_enable_clkpm(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev;
	u32 link_cap;
	u16 link_ctl;

	pdev = to_pci_dev(mdev->dev);
	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &link_ctl);
	if (link_ctl & PCI_EXP_LNKCTL_CLKREQ_EN)
		return;

	pcie_capability_read_dword(pdev, PCI_EXP_LNKCAP, &link_cap);
	if (link_cap & PCI_EXP_LNKCAP_CLKPM) {
		pcie_capability_set_word(pdev, PCI_EXP_LNKCTL, PCI_EXP_LNKCTL_CLKREQ_EN);
		MTK_INFO(mdev, "Set CLKPM\n");
	} else {
		MTK_ERR(mdev, "Not Support CLKPM!\n");
	}
}

#endif /* CONFIG_GOOGLE_ASPM_CONTROL */

static void mtk_pci_set_snoop_latency(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev;
	u16 nosnoop_val;
	u16 snoop_val;
	int ltr;

	pdev = to_pci_dev(mdev->dev);
	ltr = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_LTR);
	if (!ltr)
		return;

	pci_read_config_word(pdev, ltr + PCI_LTR_MAX_SNOOP_LAT, &snoop_val);
	pci_read_config_word(pdev, ltr + PCI_LTR_MAX_NOSNOOP_LAT, &nosnoop_val);
	if ((snoop_val & PCI_LTR_VALUE_MASK) || (nosnoop_val & PCI_LTR_VALUE_MASK))
		return;

	snoop_val &= ~(PCI_LTR_VALUE_MASK | PCI_LTR_SCALE_MASK);
	snoop_val |= SNOOP_LATENCY;
	nosnoop_val &= ~(PCI_LTR_VALUE_MASK | PCI_LTR_SCALE_MASK);
	nosnoop_val |= SNOOP_LATENCY;
	pci_write_config_word(pdev, ltr + PCI_LTR_MAX_SNOOP_LAT, snoop_val);
	pci_write_config_word(pdev, ltr + PCI_LTR_MAX_NOSNOOP_LAT, nosnoop_val);
	MTK_INFO(mdev, "Set snoop value\n");
}

static void mtk_pci_dev_configure(struct mtk_md_dev *mdev)
{
#if !IS_ENABLED(CONFIG_GOOGLE_ASPM_CONTROL)
	if (!mtk_pci_aspm_is_enable(mdev, false))
		mtk_pci_config_aspm_l0s(mdev, PCI_EXP_LNKCTL_ASPM_L0S);

	if (!mtk_pci_aspm_is_enable(mdev, true))
		mtk_pci_enable_aspm_l1ss(mdev);

	mtk_pci_enable_clkpm(mdev);
#endif
	mtk_pci_set_snoop_latency(mdev);
}

static int mtk_pci_dev_init(struct mtk_md_dev *mdev)
{
	int ret;

	/* do not add error handling here, dbgfs may not support in system */
	mdev->dev_dentry = mtk_dbgfs_create_dir(mtk_get_drv_dentry(), mdev->dev_str);
	MTK_INFO(mdev, "%s\n", BUILD_INFO_STR);
	ret = mtk_memlog_init(mdev, BUILD_INFO_STR);
	if (ret)
		goto out;

	ret = mtk_stats_init(mdev);
	if (ret)
		goto free_dbgfs_and_memlog;

	ret = mtk_fsm_init(mdev);
	if (ret)
		goto free_stats;

	ret = mtk_pm_init(mdev);
	if (ret)
		goto free_fsm;

	ret = mtk_stats_init_late(mdev);
	if (ret)
		goto free_pm;

	ret = mtk_bm_init(mdev);
	if (ret)
		goto free_stats_early;

	ret = mtk_trans_ctrl_init(mdev);
	if (ret)
		goto free_bm;

	ret = mtk_pcie_data_init(mdev);
	if (ret)
		goto free_ctrl_plane;

	ret = mtk_devlink_init(mdev);
	if (ret)
		goto free_data_plane;

	ret = mtk_exception_init(mdev);
	if (ret)
		goto free_devlink;

	ret = mtk_frc_sync_init(mdev);
	if (ret)
		goto free_exception;

	return 0;

free_exception:
	mtk_exception_exit(mdev);
free_devlink:
	mtk_devlink_exit(mdev);
free_data_plane:
	mtk_pcie_data_exit(mdev);
free_ctrl_plane:
	mtk_trans_ctrl_exit(mdev);
free_bm:
	mtk_bm_exit(mdev);
free_stats_early:
	mtk_stats_exit_early(mdev);
free_pm:
	mtk_pm_exit(mdev);
free_fsm:
	mtk_fsm_exit(mdev);
free_stats:
	mtk_stats_exit(mdev);
free_dbgfs_and_memlog:
	mtk_memlog_exit(mdev);
out:
	mtk_dbgfs_remove(mdev->dev_dentry);
	return ret;
}

static void mtk_pci_dev_exit(struct mtk_md_dev *mdev)
{
	mtk_fsm_evt_submit(mdev, FSM_EVT_DEV_RM, 0, NULL, 0,
			   EVT_MODE_BLOCKING | EVT_MODE_TOHEAD);
	mtk_devlink_exit(mdev);
	mtk_pm_exit_early(mdev);
	mtk_pcie_data_exit(mdev);
	mtk_trans_ctrl_exit(mdev);
	mtk_bm_exit(mdev);
	mtk_frc_sync_exit(mdev);
	mtk_stats_exit_early(mdev);
	mtk_pm_exit(mdev);
	mtk_exception_exit(mdev);
	mtk_fsm_exit(mdev);
	mtk_stats_exit(mdev);
	mtk_memlog_exit(mdev);
	mtk_dbgfs_remove(mdev->dev_dentry);
}

static int mtk_pci_dev_start(struct mtk_md_dev *mdev)
{
	mtk_fsm_evt_submit(mdev, FSM_EVT_DEV_ADD, 0, NULL, 0, 0);
	mtk_fsm_start(mdev);
	return 0;
}

static const struct mtk_dev_ops pci_hw_ops = {
	.get_dev_state = mtk_pci_get_dev_state,
	.ack_dev_state = mtk_pci_ack_dev_state,
	.get_dev_cfg = mtk_pci_get_dev_cfg,
	.register_dev_evt = mtk_pci_register_ext_evt,
	.unregister_dev_evt = mtk_pci_unregister_ext_evt,
	.mask_dev_evt = mtk_pci_mask_ext_evt,
	.unmask_dev_evt = mtk_pci_unmask_ext_evt,
	.clear_dev_evt = mtk_pci_clear_ext_evt,
	.send_dev_evt = mtk_pci_send_ext_evt,
	.reinit = mtk_pci_reinit,
	.get_dev_log = mtk_pci_get_dev_log,
	.get_log_region_size = mtk_pci_get_log_region_size,
	.dbg_dump = mtk_pci_dump,
};

static void mtk_pci_get_utility_cfg(struct mtk_md_dev *mdev, u32 hw_ver)
{
	struct mtk_utility_cfg_desc *p_utility_cfg;
	u8 i;

	for (i = 0; (p_utility_cfg = &mtk_utility_cfg_tbl[i]) && p_utility_cfg &&
	     p_utility_cfg->utility_cfg; i++) {
		if (p_utility_cfg->hw_ver == hw_ver) {
			mdev->utility_cfg = p_utility_cfg->utility_cfg;
			break;
		}
	}
}

static int mtk_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct mtk_pci_priv *priv;
	struct mtk_md_dev *mdev;
	int ret;

	mdev = mtk_dev_alloc(dev, &pci_hw_ops);
	if (!mdev) {
		ret = -ENOMEM;
		goto out;
	}

	mtk_pci_get_utility_cfg(mdev, pdev->device);
	if (!mdev->utility_cfg) {
		ret = -ENOMEM;
		goto free_cntx_data;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto free_cntx_data;
	}

	pci_set_drvdata(pdev, mdev);
	priv->cfg = (void *)id->driver_data;
	priv->mdev = mdev;
	mdev->hw_ver  = pdev->device;
	mdev->hw_priv = priv;
	mdev->dev     = dev;
	snprintf(mdev->dev_str, MTK_DEV_STR_LEN, "%02x%02x%d",
		 pdev->bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
	spin_lock_init(&priv->mac_active_lock);

	mtk_pci_bridge_configure(mdev);

	MTK_INFO(mdev, "Start probe 0x%x, state_saved[%d]\n",
		 mdev->hw_ver, pdev->state_saved);

	if (pdev->state_saved) {
		MTK_INFO(mdev, "Restoring configuration space\n");
#if defined(CONFIG_PCIEASPM) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0))
		mtk_pci_restore_aspm_l1ss_state(mdev);
#endif
		pci_restore_state(pdev);
	}

	mtk_pci_dev_configure(mdev);

	ret = pcim_enable_device(pdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to enable pci device.\n");
		goto free_priv_data;
	}

#if IS_ENABLED(CONFIG_GOOGLE_ASPM_CONTROL)
	/* Enable PCI PM substates when device in D0 state */
	mtk_pci_enable_aspm_l1ss(mdev);
#endif

	mtk_pci_aspm_ctrl(mdev);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		MTK_ERR(mdev, "Failed to set DMA Mask and Coherent. (ret=%d)\n", ret);
		goto free_priv_data;
	}

	ret = mtk_pci_bar_init(mdev);
	if (ret)
		goto free_priv_data;

	ret = priv->cfg->atr_init(mdev);
	if (ret)
		goto free_bar;

	ret = mtk_mhccif_init(mdev);
	if (ret)
		goto free_bar;

	/* mask all irqs */
	if (priv->cfg->flag & MTK_CFG_IRQ_DFLT_MASK)
		mtk_pci_mac_write32(priv, REG_IMASK_HOST_MSIX_CLR_GRP0_0, U32_MAX);
	MTK_INFO(mdev, "REG_IMASK_HOST_MSIX_CLR_GRP0_0: 0x%x\n",
		 mtk_pci_mac_read32(priv, REG_IMASK_HOST_MSIX_CLR_GRP0_0));

	ret = mtk_pci_request_irq(mdev, mtk_pci_irq_cnt_max, mtk_pci_irq_type);
	if (ret)
		goto free_mhccif;

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	ret = radio_google_early_init();
	if (ret) {
		MTK_ERR(mdev, "Failed to early init radio_google.\n");
		goto free_irq;
	}
#endif

	ret = mtk_pci_dev_init(mdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to init dev.\n");
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		goto free_google_early;
#else
		goto free_irq;
#endif
	}

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	ret = radio_google_init(mdev, mtk_google_get_tmi_ops());
	if (ret) {
		MTK_ERR(mdev, "Failed to init radio_google.\n");
		goto free_device;
	}
#endif

	ret = mtk_rgu_init(mdev);
	if (ret)
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		goto free_google;
#else
		goto free_device;
#endif

	pci_set_master(pdev);
	mtk_pci_unmask_irq(mdev, priv->mhccif_irq_id);
	mtk_pci_dbgfs_init(mdev);

	if (mtk_pci_link_check(mdev)) {
		mtk_pci_save_state(mdev);
	} else {
		ret = -EFAULT;
		goto clear_master_and_rgu;
	}

	priv->saved_state = pci_store_saved_state(pdev);
	if (!priv->saved_state) {
		ret = -EFAULT;
		goto clear_master_and_rgu;
	}

	ret = mtk_pci_dev_start(mdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to start dev.\n");
		goto free_saved_state;
	}

	ret = mtk_pcimsg_messenger_init(mdev);
	if (ret)
		MTK_ERR(mdev, "Failed to init messenger\n");

	ret = mtk_pci_dev_log_buff_init(mdev);
	if (ret) {
		MTK_ERR(mdev, "Failed to init dev log buff, ret=%d\n");
		goto free_saved_state;
	}

	ret = mtk_pci_dsd_init(mdev);
	if (ret)
		MTK_ERR(mdev, "dsd not support\n");

	mtk_pcie_slt_init(mdev);

	MTK_INFO(mdev, "Probe done hw_ver=0x%x with irq type %d\n", mdev->hw_ver, mtk_pci_irq_type);

	return 0;

free_saved_state:
	pci_load_and_free_saved_state(pdev, &priv->saved_state);
clear_master_and_rgu:
	mtk_pci_dbgfs_exit(mdev);
	pci_clear_master(pdev);
	mtk_rgu_exit(mdev);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
free_google:
	radio_google_exit(mdev);
#endif
free_device:
	mtk_pci_dev_exit(mdev);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
free_google_early:
	radio_google_late_exit();
#endif
free_irq:
	mtk_pci_free_irq(mdev);
free_mhccif:
	mtk_mhccif_exit(mdev);
free_bar:
	mtk_pci_bar_exit(mdev);
free_priv_data:
	devm_kfree(dev, priv);
free_cntx_data:
	mtk_dev_free(mdev);
out:
	dev_err(dev, "Failed to probe device, ret=%d\n", ret);

	return ret;
}

static void mtk_pci_remove(struct pci_dev *pdev)
{
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct device *dev = &pdev->dev;

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	radio_google_exit(mdev);
	radio_google_late_exit();
#endif

	mtk_pci_mask_irq(mdev, priv->rgu_irq_id);
	mtk_pci_mask_irq(mdev, priv->mhccif_irq_id);
	mtk_pci_dbgfs_exit(mdev);
	mtk_pci_dev_log_buff_exit(mdev);
	mtk_pcimsg_messenger_exit(mdev);
	mtk_pci_dsd_exit(mdev);
	mtk_pci_dev_exit(mdev);

#if !defined(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
	if (priv->cfg->flag & MTK_CFG_DISABLE_AP_DRM) {
		mtk_pci_send_ext_evt(mdev, EXT_EVT_H2D_DRM_DISABLE_AP);
		/* Sleep to wait the DRM disable takes effect. */
		msleep(200);
	}

	if (mtk_pci_pldr(mdev)) {
		dev_info(dev, "Failed to execute PLDR , try external event\n");
		mtk_pci_reset(mdev, RESET_MHCCIF);
	}
#endif

	pci_clear_master(pdev);
	mtk_rgu_exit(mdev);
	mtk_mhccif_exit(mdev);
	mtk_pci_free_irq(mdev);
	mtk_pci_bar_exit(mdev);
	pci_load_and_free_saved_state(pdev, &priv->saved_state);
	mtk_pcie_slt_exit(mdev);
	devm_kfree(dev, priv);
	devm_kfree(dev, mdev);
	dev_info(dev, "Remove done, state_saved[%d]\n", pdev->state_saved);
}

#define MTK_REGSCNT_PER_LINE 4
#define MTK_REG_OFFSET_STR_LEN 7
#define MTK_REG_VAL_STR_LEN 9
void mtk_pci_regs_dump(struct mtk_md_dev *mdev, enum mtk_debug_mask mask,
		       enum mtk_memlog_region_id region_id, const char *msg,
		       unsigned long long addr, size_t len)
{
	/* Up multiples of MTK_REGSCNT_PER_LINE */
	int round_up_len = round_up(len, MTK_REGSCNT_PER_LINE);
	int b4_fix_num = round_up_len / MTK_REGSCNT_PER_LINE;
	int b16_tail_num = b4_fix_num % MTK_REGSCNT_PER_LINE;
	int b16_fix_num = b4_fix_num / MTK_REGSCNT_PER_LINE;
	unsigned char buf[MTK_LOG_BUFF_SIZE] = {0};
	unsigned long long base_addr = 0;
	int i, cnt;

	if (len <= 0 || !mdev) {
		pr_err("Invalid parameters!\n");
		return;
	}

	if (msg)
		__mtk_dbg(mdev, mask, region_id, "%s", msg);

	__mtk_dbg(mdev, mask, region_id,
		  "===start address:0x%lx, len(32bit):0x%x===\n", addr, len);
	for (i = 0; i < b16_fix_num; i++) {
		base_addr = addr + i * MTK_BYTES_PER_LINE;
		__mtk_dbg(mdev, mask, region_id,
			  "0x%04X: %08X %08X %08X %08X\n", i * MTK_BYTES_PER_LINE,
			  mtk_pci_read32(mdev, base_addr + 0),
			  mtk_pci_read32(mdev, base_addr + 4),
			  mtk_pci_read32(mdev, base_addr + 8),
			  mtk_pci_read32(mdev, base_addr + 12));
	}

	if (b16_tail_num) {
		base_addr = addr + i * MTK_BYTES_PER_LINE;
		snprintf(buf, (MTK_LOG_BUFF_SIZE - 1), "0x%04X:", i * MTK_BYTES_PER_LINE);
		for (i = 0; i < b16_tail_num; i++) {
			cnt = MTK_REG_OFFSET_STR_LEN + (i * MTK_REG_VAL_STR_LEN);
			snprintf((buf + cnt), (MTK_LOG_BUFF_SIZE - cnt),
				 " %08X", mtk_pci_read32(mdev, base_addr + i * 4));
		}
		__mtk_dbg(mdev, mask, region_id, "%s", buf);
	}
}

static void mtk_pci_ltr_dbg(struct pci_dev *pdev)
{
#ifdef CONFIG_PCIEASPM
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	struct pci_dev *bridge;
	u16 ctl;

	bridge = pci_upstream_bridge(pdev);
	if (!bridge) {
		MTK_ERR(mdev, "Unable to find bridge\n");
		return;
	}
	if (bridge->ltr_path) {
		pcie_capability_read_word(bridge, PCI_EXP_DEVCTL2, &ctl);
		if (!(ctl & PCI_EXP_DEVCTL2_LTR_EN))
			pcie_capability_set_word(bridge, PCI_EXP_DEVCTL2, PCI_EXP_DEVCTL2_LTR_EN);
		MTK_DBG(mdev, MTK_DBG_PCIE, MTK_MEMLOG_RG_COMMON, "DEVCTL2=0x%x, LTR_En=0x%x\n",
			ctl, ctl & PCI_EXP_DEVCTL2_LTR_EN);
	}
	pcie_capability_read_word(pdev, PCI_EXP_DEVCTL2, &ctl);
	MTK_INFO(mdev, "Bridge LTR_Path %x/%x, Device DEVCTL2=0x%x, LTR_En=0x%x\n",
		 bridge->ltr_path, pdev->ltr_path, ctl, ctl & PCI_EXP_DEVCTL2_LTR_EN);
#endif
}

static pci_ers_result_t mtk_pci_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	int ret;

	ret = mtk_exception_report_evt(mdev, EXCEPTION_AER_DETECTED);
	if (ret)
		MTK_ERR(mdev, "Failed to call exception report API with EXCEPTION_AER_DETECTED!\n");
	MTK_INFO(mdev, "AER detected: pci_channel_state_t=%d\n", state);

	mtk_pci_ltr_dbg(pdev);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	radio_google_error_detected_post(pdev, state);
#endif

	/* Slot reset not required. */
	return PCI_ERS_RESULT_CAN_RECOVER;
}

static const struct pci_error_handlers mtk_pci_err_handler = {
	.error_detected = mtk_pci_error_detected,
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	.cor_error_detected = radio_google_cor_error_detected,
#endif
};

static bool mtk_pci_check_atr_init(struct mtk_md_dev *mdev)
{
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
	if (!mtk_pci_link_check_silent(mdev))
		return FALSE;
#endif
	if (mtk_pci_mac_read32(mdev->hw_priv, REG_ATR_PCIE_WIN0_T0_SRC_ADDR_LSB) ==
		ATR_WIN0_SRC_ADDR_LSB_DEFT)
		/* Device reboots and isn't configured ATR, so it is default value. */
		return TRUE;
	return FALSE;
}

static int __maybe_unused mtk_pci_pm_suspend(struct device *dev)
{
	return mtk_pm_suspend(dev);
}

static int __maybe_unused mtk_pci_pm_prepare(struct device *dev)
{
	return mtk_pm_prepare(dev);
}

static void __maybe_unused mtk_pci_pm_complete(struct device *dev)
{
	mtk_pm_complete(dev);
}

static int __maybe_unused mtk_pci_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mtk_md_dev *mdev;
	bool atr_init;

	mdev = pci_get_drvdata(pdev);
	atr_init = mtk_pci_check_atr_init(mdev);

	return mtk_pm_resume(dev, atr_init);
}

static int __maybe_unused mtk_pci_pm_freeze(struct device *dev)
{
	return mtk_pm_freeze(dev);
}

static int __maybe_unused mtk_pci_pm_restore(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mtk_md_dev *mdev;
	bool atr_init;

	mdev = pci_get_drvdata(pdev);
	atr_init = mtk_pci_check_atr_init(mdev);

	return mtk_pm_restore(dev, atr_init);
}

static int __maybe_unused mtk_pci_pm_runtime_suspend(struct device *dev)
{
	return mtk_pm_runtime_suspend(dev);
}

static int __maybe_unused mtk_pci_pm_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mtk_md_dev *mdev;
	bool atr_init;

	mdev = pci_get_drvdata(pdev);
	atr_init = mtk_pci_check_atr_init(mdev);

	return mtk_pm_runtime_resume(dev, atr_init);
}

static int __maybe_unused mtk_pci_pm_runtime_idle(struct device *dev)
{
	return mtk_pm_runtime_idle(dev);
}

static void mtk_pci_pm_shutdown(struct pci_dev *pdev)
{
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_EXCEPT);
#endif
	return mtk_pm_shutdown(mdev);
}

static const struct dev_pm_ops mtk_pci_pm_ops = {
	.prepare = mtk_pci_pm_prepare,
	.complete = mtk_pci_pm_complete,
	.suspend = mtk_pci_pm_suspend,
	.resume = mtk_pci_pm_resume,
	.freeze = mtk_pci_pm_freeze,
	.restore = mtk_pci_pm_restore,

	SET_RUNTIME_PM_OPS(mtk_pci_pm_runtime_suspend, mtk_pci_pm_runtime_resume,
			   mtk_pci_pm_runtime_idle)
};

static struct pci_driver mtk_pci_drv = {
	.name = "mtk_pci_drv",
	.id_table = mtk_pci_ids,
	.probe = mtk_pci_probe,
	.remove = mtk_pci_remove,
	.driver.pm = &mtk_pci_pm_ops,
	.shutdown = mtk_pci_pm_shutdown,
	.err_handler = &mtk_pci_err_handler
};

module_pci_driver(mtk_pci_drv);
module_param(mtk_pci_irq_cnt_max, int, 0644);
MODULE_PARM_DESC(mtk_pci_irq_cnt_max, "The maximum number of irqs requested");
module_param(mtk_pci_irq_type, int, 0644);
MODULE_PARM_DESC(mtk_pci_irq_type, "The type of irqs requested");
module_param(mtk_pci_stress_test_loop, uint, 0644);
MODULE_PARM_DESC(mtk_pci_stress_test_loop, "PCI MMIO and Cfg R/W stress test loop");
module_param(mtk_pci_stress_test_delay_us, uint, 0644);
MODULE_PARM_DESC(mtk_pci_stress_test_delay_us, "Show pci MMIO and Cfg R/W stress test delay time");
module_param(mtk_pci_stress_test_log, bool, 0644);
MODULE_PARM_DESC(mtk_pci_stress_test_log, "Show pci MMIO and Cfg R/W stress test log");
module_param(mtk_pci_config_atr_test_win, uint, 0644);
MODULE_PARM_DESC(mtk_pci_config_atr_test_win, "ATR config test window index");
module_param(mtk_pci_config_atr_test_table, uint, 0644);
MODULE_PARM_DESC(mtk_pci_config_atr_test_table, "ATR config test table index");

#ifdef CONFIG_PCIEASPM
#if IS_ENABLED(CONFIG_GOOGLE_ASPM_CONTROL)
module_param(mtk_pci_link_state_init, ushort, 0644);
#else
module_param(mtk_pci_link_state_init, byte, 0644);
#endif
MODULE_PARM_DESC(mtk_pci_link_state_init, "Link state after probe");
#endif

MODULE_LICENSE("Dual BSD/GPL");
