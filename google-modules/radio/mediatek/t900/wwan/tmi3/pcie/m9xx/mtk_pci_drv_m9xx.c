// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */
#include <asm/barrier.h>
#include <asm/cacheflush.h>
#include <linux/minmax.h>
#include <linux/of_reserved_mem.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include "mtk_debug.h"
#include "mtk_except.h"
#include "mtk_fsm.h"
#include "mtk_pci.h"
#include "mtk_pci_drv_m9xx.h"
#include "mtk_pci_reg.h"
#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
#include "mtk_pwrctl.h"
#endif

#ifdef CONFIG_UT_PCIE_PCIE_M9XX
#include "ut_pci_m9xx_fake.h"
#endif

#define TAG "PCI"

#define MTK_AEE_ON_DELAY_TIME	1300
#define MTK_AEE_OFF_DELAY_TIME	300
#define MTK_DEV_LOG_ONE_TIME_READ_SIZE	0x8

static int dev_type;

static struct dsdmem_region {
	void *dsdmem_addr_virt;
	phys_addr_t dsdmem_addr_phy;
	u64 size;
} dsdmem_region_info;

struct dsdmem_cfg {
	u64 md_addr_phy;
	u64 md_size;
};

static struct proc_dir_entry *mtk_dsdmem_proc;

static int mtk_dsd_handler(struct mtk_md_dev *mdev, void *data)
{
	struct dsdmem_cfg cfg;

	if (dsdmem_region_info.dsdmem_addr_phy && data) {
		cfg.md_addr_phy = dsdmem_region_info.dsdmem_addr_phy;
		cfg.md_size = dsdmem_region_info.size;
		memcpy(data, &cfg, sizeof(cfg));
		MTK_INFO(mdev, "Send dsdmem to MD: addr=%llx, size=%llx\n",
			 cfg.md_addr_phy, cfg.md_size);
	} else {
		MTK_WARN(mdev, "dsdmem or data addr NULL!");
		return -EFAULT;
	}

	return sizeof(cfg);
}

static ssize_t mtk_dsdmem_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	unsigned int pos, len, available;
	unsigned int *ptr;

	ptr = file->private_data;
	pos = *ptr;
	available = dsdmem_region_info.size - pos;
	len = min_t(unsigned int, size, available);
	if (len == 0)
		return 0;

	if (copy_to_user(buf, dsdmem_region_info.dsdmem_addr_virt + pos, len))
		return -EFAULT;

	*ptr = pos + len;

	return len;
}

static int mtk_dsdmem_open(struct inode *inode, struct file *file)
{
	void *end_addr = dsdmem_region_info.dsdmem_addr_virt + dsdmem_region_info.size;
	void *current_addr;
	unsigned int *ptr;
	struct page *page;

	ptr = kzalloc(sizeof(unsigned int), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	file->private_data = ptr;

	for (current_addr = dsdmem_region_info.dsdmem_addr_virt; current_addr < end_addr;
	     current_addr += PAGE_SIZE) {
		page = virt_to_page(current_addr);
		flush_dcache_page(page);
	}

	return 0;
}

static int mtk_dsdmem_close(struct inode *inode, struct file *file)
{
	unsigned int *ptr = file->private_data;

	kfree(ptr);

	return 0;
}

static const struct proc_ops mtk_dsdmem_ops = {
	.proc_read = mtk_dsdmem_read,
	.proc_open = mtk_dsdmem_open,
	.proc_release = mtk_dsdmem_close,
};

static int mtk_pci_dsd_mem_get(struct mtk_md_dev *mdev)
{
	struct reserved_mem *dsdmem;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "mediatek,md_mddebug_emi");
	if (!node) {
		MTK_WARN(mdev, "dsdmem node not found!\n");
		return -EINVAL;
	}

	dsdmem = of_reserved_mem_lookup(node);
	if (!dsdmem) {
		MTK_WARN(mdev, "dsdmem addr get fail!\n");
		return -EINVAL;
	}

	dsdmem_region_info.dsdmem_addr_phy = dsdmem->base;
	dsdmem_region_info.size = dsdmem->size;

	return 0;
}

static int mtk_pci_init_dsd(struct mtk_md_dev *mdev)
{
	if (mtk_pci_dsd_mem_get(mdev))
		return -EINVAL;

	mtk_fsm_supported_rtft_register(SUPPORT_RTFT_ID_MD_DSD, mtk_dsd_handler);

	if (mtk_dsdmem_proc) {
		MTK_INFO(mdev, "proc exist\n");
		return 0;
	}

	dsdmem_region_info.dsdmem_addr_virt = phys_to_virt(dsdmem_region_info.dsdmem_addr_phy);
	MTK_INFO(mdev, "map dsdmem phy->vir: 0x%llx->0x%llx, size = 0x%llx\n",
		 dsdmem_region_info.dsdmem_addr_phy, dsdmem_region_info.dsdmem_addr_virt,
		 dsdmem_region_info.size);

	mtk_dsdmem_proc = proc_create("ccci_sib", 0444, NULL, &mtk_dsdmem_ops);
	if (!mtk_dsdmem_proc) {
		MTK_WARN(mdev, "fail to create dsdmem proc entry\n");
		return -EINVAL;
	}

	MTK_INFO(mdev, "dsdmem init success\n");

	return 0;
}

static int mtk_pci_exit_dsd(struct mtk_md_dev *mdev)
{
	if (mtk_dsdmem_proc) {
		proc_remove(mtk_dsdmem_proc);
		MTK_INFO(mdev, "dsdmem exit success\n");
		return 0;
	}

	MTK_WARN(mdev, "dsdmem not exist!\n");

	return -EINVAL;
}

#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
static int mtk_pci_aee_reboot_m9xx(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_except *except;
	int aee_type, ret;

	except = priv->except;

	aee_type = (except->config_info >> MTK_EXCEPTION_CONFIG_OFFSET) & MTK_EXCEPTION_CONFIG_INFO;

	if (aee_type == MTK_AEE_TYPE_AEE_OFF)
		ret = mtk_pwrctl_aee_reboot(MTK_AEE_OFF_DELAY_TIME);
	else
		ret = mtk_pwrctl_aee_reboot(MTK_AEE_ON_DELAY_TIME);
	return ret;
}
#endif

static int mtk_pci_dev_reset_m9xx(struct mtk_md_dev *mdev, enum mtk_reset_type type)
{
	switch (type) {
	case RESET_MHCCIF:
		return mtk_pci_send_ext_evt(mdev, DEV_EVT_H2D_DEVICE_RESET);
	case RESET_FLDR:
		return mtk_pci_fldr(mdev);
	case RESET_PLDR:
		return mtk_pci_pldr(mdev);
#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
	case RESET_AEE_REBOOT:
		return mtk_pci_aee_reboot_m9xx(mdev);
#endif
	default:
		break;
	}

	return -EINVAL;
}

static void mtk_pci_get_device_type(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 reg_val;

	reg_val = mtk_pci_mac_read32(priv, REG_PCIE_DEBUG_DUMMY_7);
	dev_type = (reg_val & FSM_F_DL_PL) == FSM_F_DL_PL ? 1 : 0;
}

static int mtk_pci_atr_init_m9xx(struct mtk_md_dev *mdev)
{
	struct pci_dev *pdev = to_pci_dev(mdev->dev);
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_atr_cfg cfg;
	int port, ret;

#if IS_ENABLED(CONFIG_GOOGLE_B495424578_DEBUG)
	if (!mtk_pci_link_check(mdev)) {
		MTK_INFO(mdev, "Linkdown occurs when config ATR\n");
		return -EFAULT;
	}
#endif

	mtk_pci_atr_disable(priv);

	mtk_pci_get_device_type(mdev);

	/* Config ATR for RC to access device's register */
	cfg.src_addr = pci_resource_start(pdev, MTK_BAR_2_3_IDX);
	cfg.size = ATR_PCIE_REG_SIZE;
	cfg.trsl_addr = ATR_PCIE_REG_TRSL_ADDR;
	cfg.type = ATR_PCI2AXI;
	cfg.port = ATR_PCIE_REG_PORT;
	cfg.table = ATR_PCIE_REG_TABLE_NUM;
	cfg.trsl_id = ATR_PCIE_REG_TRSL_PORT;
	cfg.trsl_param = 0x0;
	cfg.transparent = 0x0;
	ret = mtk_pci_setup_atr(mdev, &cfg);
#if IS_ENABLED(CONFIG_GOOGLE_B495424578_DEBUG)
	mtk_pci_dump_atr_bar23(mdev);
#endif
	if (ret)
		return ret;

	/* Config ATR for EP to access RC's memory */
	for (port = ATR_SRC_AXIS_0; port <= ATR_SRC_AXIS_3; port++) {
		cfg.src_addr = ATR_PCIE_DEV_DMA_SRC_ADDR;
		cfg.size = ATR_PCIE_DEV_DMA_SIZE;
		cfg.trsl_addr = ATR_PCIE_DEV_DMA_TRSL_ADDR;
		cfg.type = ATR_AXI2PCI;
		cfg.port = port;
		cfg.table = ATR_PCIE_DEV_DMA_TABLE_NUM;
		cfg.trsl_id = ATR_DST_PCI_TRX;
		cfg.trsl_param = 0x0;
		/* Enable transparent translation */
		cfg.transparent = ATR_PCIE_DEV_DMA_TRANSPARENT;
		ret = mtk_pci_setup_atr(mdev, &cfg);
		if (ret)
			return ret;
	}

	if (priv->cfg->flag & MTK_CFG_HOST_GET_DEV_LOG) {
		/* Config ATR for BROM SRAM log */
		cfg.src_addr = pci_resource_start(pdev, MTK_BAR_4_5_IDX);
		cfg.src_addr += ATR_PCIE_BROM_SRAM_OFFSET;
		cfg.size = ATR_PCIE_BROM_SRAM_SIZE;
		cfg.trsl_addr = ATR_PCIE_BROM_SRAM_TRASL_ADDR;
		cfg.type = ATR_PCI2AXI;
		cfg.port = ATR_SRC_PCI_WIN1;
		cfg.table = ATR_PCIE_BROM_SRAM_TABLE_NUM;
		cfg.trsl_id = ATR_PCIE_REG_TRSL_PORT;
		cfg.trsl_param = 0x0;
		cfg.transparent = 0x0;
		ret = mtk_pci_setup_atr(mdev, &cfg);
		if (ret)
			return ret;

		/* Config ATR for PL SRAM log */
		cfg.src_addr = pci_resource_start(pdev, MTK_BAR_4_5_IDX);
		cfg.src_addr += ATR_PCIE_PL_SRAM_OFFSET;
		cfg.size = ATR_PCIE_PL_SRAM_SIZE;
		cfg.trsl_addr = ATR_PCIE_PL_SRAM_TRASL_ADDR;
		cfg.type = ATR_PCI2AXI;
		cfg.port = ATR_SRC_PCI_WIN1;
		cfg.table = ATR_PCIE_PL_SRAM_TABLE_NUM;
		cfg.trsl_id = ATR_PCIE_REG_TRSL_PORT;
		cfg.trsl_param = 0x0;
		cfg.transparent = 0x0;
		ret = mtk_pci_setup_atr(mdev, &cfg);
		if (ret)
			return ret;

		/* Config ATR for PL DRAM log */
		cfg.src_addr = pci_resource_start(pdev, MTK_BAR_4_5_IDX);
		cfg.src_addr += ATR_PCIE_PL_DRAM_OFFSET;
		cfg.size = ATR_PCIE_PL_DRAM_SIZE;
		cfg.trsl_addr = ATR_PCIE_PL_DRAM_TRASL_ADDR;
		cfg.type = ATR_PCI2AXI;
		cfg.port = ATR_SRC_PCI_WIN1;
		cfg.table = ATR_PCIE_PL_DRAM_TABLE_NUM;
		cfg.trsl_id = ATR_PCIE_REG_TRSL_PORT;
		cfg.trsl_param = 0x0;
		cfg.transparent = 0x0;
		ret = mtk_pci_setup_atr(mdev, &cfg);
		if (ret)
			return ret;

		/* Config ATR for ATF DRAM log */
		cfg.src_addr = pci_resource_start(pdev, MTK_BAR_4_5_IDX);
		cfg.src_addr += ATR_PCIE_ATF_DRAM_OFFSET;
		cfg.size = ATR_PCIE_ATF_DRAM_SIZE;
		if (dev_type)
			cfg.trsl_addr = ATR_PCIE_ATF_DRAM_TRASL_ADDR;
		else
			cfg.trsl_addr = ATR_PCIE_ATF_DRAM_TRASL_ADDR_FB;
		cfg.type = ATR_PCI2AXI;
		cfg.port = ATR_SRC_PCI_WIN1;
		cfg.table = ATR_PCIE_ATF_DRAM_TABLE_NUM;
		cfg.trsl_id = ATR_PCIE_REG_TRSL_PORT;
		cfg.trsl_param = 0x0;
		cfg.transparent = 0x0;
		ret = mtk_pci_setup_atr(mdev, &cfg);
		if (ret)
			return ret;

		MTK_INFO(mdev, "Config ATR for BROM SRAM, PL SRAM, PL DRAM and ATF DRAM done\n");
	}

#if IS_ENABLED(CONFIG_GOOGLE_B495424578_DEBUG)
	mtk_pci_dump_atr(mdev);
#endif

	return 0;
}

static int mtk_pci_get_dev_log_m9xx(struct mtk_md_dev *mdev,
				    void *buf, size_t count, enum mtk_dev_log_type type)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	const void __iomem *io_addr = NULL;

	int read_len = 0, offset = 0, copy_size, len;

	MTK_INFO(mdev, "bar45_addr=0x%llx, brom_s:0x%x/0x%x, pl_s:0x%x/0x%x\n",
		 priv->bar45_addr,
		 ATR_PCIE_BROM_SRAM_OFFSET, PCIE_BROM_SRAM_READ_OFFSET,
		 ATR_PCIE_PL_SRAM_OFFSET, PCIE_PL_SRAM_READ_OFFSET);

	MTK_INFO(mdev, "pl_d:0x%x/0x%x, atf_d:0x%x/0x%x\n", ATR_PCIE_PL_DRAM_OFFSET,
		 PCIE_PL_DRAM_READ_OFFSET, ATR_PCIE_ATF_DRAM_OFFSET,
		 dev_type == 1 ? PCIE_ATF_DRAM_READ_OFFSET : PCIE_ATF_DRAM_READ_OFFSET_FB);

	switch (type) {
	case MTK_DEV_LOG_BROM_SRAM:
		io_addr = priv->bar45_addr + ATR_PCIE_BROM_SRAM_OFFSET +
			PCIE_BROM_SRAM_READ_OFFSET;
		read_len = PCIE_BROM_SRAM_LOG_SIZE;
		break;
	case MTK_DEV_LOG_PL_SRAM:
		io_addr = priv->bar45_addr + ATR_PCIE_PL_SRAM_OFFSET + PCIE_PL_SRAM_READ_OFFSET;
		read_len = PCIE_PL_SRAM_LOG_SIZE;
		break;
	case MTK_DEV_LOG_PL_DRAM:
		io_addr = priv->bar45_addr + ATR_PCIE_PL_DRAM_OFFSET + PCIE_PL_DRAM_READ_OFFSET;
		if (dev_type)
			read_len = PCIE_PL_DRAM_LOG_SIZE;
		else
			read_len = PCIE_PL_DRAM_LOG_SIZE_FB;
		break;
	case MTK_DEV_LOG_ATF_DRAM:
		if (dev_type) {
			io_addr = priv->bar45_addr + ATR_PCIE_ATF_DRAM_OFFSET +
				PCIE_ATF_DRAM_READ_OFFSET;
			read_len = PCIE_ATF_DRAM_LOG_SIZE;
		} else {
			io_addr = priv->bar45_addr + ATR_PCIE_ATF_DRAM_OFFSET +
				PCIE_ATF_DRAM_READ_OFFSET_FB;
			read_len = PCIE_ATF_DRAM_LOG_SIZE_FB;
		}
		break;
	default:
		read_len = -EINVAL;
		MTK_ERR(mdev, "Invalid parameter, count=%d, type=%d\n", count, type);
		goto out;
	}

	if (!buf || count != read_len) {
		read_len = -EINVAL;
		MTK_ERR(mdev, "Invalid parameter, count=%d, type=%d\n", count, type);
		goto out;
	}
	MTK_INFO(mdev, "Get device log, type=%d, io_addr=0x%llx, len=0x%x\n",
		 type, io_addr, read_len);

	len = read_len;
	while (len > 0) {
		copy_size = min(len, MTK_DEV_LOG_ONE_TIME_READ_SIZE);
		memcpy_fromio(buf + offset, io_addr + offset, copy_size);
		offset += copy_size;
		len -= copy_size;
	}

out:
	return read_len;
}

static int mtk_pci_get_log_region_size_m9xx(struct mtk_md_dev *mdev, enum mtk_dev_log_type type)
{
	int size;

	switch (type) {
	case MTK_DEV_LOG_BROM_SRAM:
		size = PCIE_BROM_SRAM_LOG_SIZE;
		break;
	case MTK_DEV_LOG_PL_SRAM:
		size = PCIE_PL_SRAM_LOG_SIZE;
		break;
	case MTK_DEV_LOG_PL_DRAM:
		if (dev_type)
			size = PCIE_PL_DRAM_LOG_SIZE;
		else
			size = PCIE_PL_DRAM_LOG_SIZE_FB;
		break;
	case MTK_DEV_LOG_ATF_DRAM:
		if (dev_type)
			size = PCIE_ATF_DRAM_LOG_SIZE;
		else
			size = PCIE_ATF_DRAM_LOG_SIZE_FB;
		break;
	default:
		MTK_ERR(mdev, "Invalid parameter, type=%d\n", type);
		return -EINVAL;
	}

	return size;
}

static void mtk_pci_force_mac_active_m9xx(struct mtk_md_dev *mdev, bool enable,
					  enum mtk_mac_active_user user)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	u32 reg;

	if (user == MAC_ACTIVE_DS_LOCK) {
		reg = enable ? MTK_DISABLE_DS_BIT(0) : MTK_ENABLE_DS_BIT(0);
		mtk_pci_mac_write32(priv, REG_PCIE_PEXTP_MAC_ACTIVE_CTRL, reg);
		reg = mtk_pci_mac_read32(priv, REG_PCIE_PEXTP_MAC_ACTIVE_CTRL);
	} else if (user == MAC_ACTIVE_MD_FRC) {
		reg = enable ? MTK_DISABLE_DS_BIT(3) : MTK_ENABLE_DS_BIT(3);
		mtk_pci_mac_write32(priv, REG_PCIE_PEXTP_MAC_ACTIVE_CTRL, reg);
	} else {
		reg = mtk_pci_mac_read32(priv, REG_PCIE_MISC_CTRL);
		if (enable)
			reg |= MTK_FORCE_MAC_ACTIVE_BIT;
		else
			reg &= ~MTK_FORCE_MAC_ACTIVE_BIT;
		mtk_pci_mac_write32(priv, REG_PCIE_MISC_CTRL, reg);
	}
}

static void mtk_pci_force_mac_sleep_m9xx(struct mtk_md_dev *mdev, bool enable)
{
	u32 reg = enable ? MTK_DISABLE_DS_BIT(0) : MTK_ENABLE_DS_BIT(0);

	mtk_pci_mac_write32(mdev->hw_priv, REG_PCIE_PEXTP_MAC_SLEEP_CTRL, reg);
}

const struct mtk_pci_dev_cfg mtk_dev_cfg_0900 = {
	.flag = MTK_CFG_RGU_L2_AUTO_ACK | MTK_CFG_HOST_GET_DEV_LOG |
		MTK_CFG_PM_SW_IRQ | MTK_CFG_FRC_SYNC,
	.mhccif_rc_base_addr = 0x1000A000,
	.mhccif_rc2ep_pcie_pm_counter = 0x14c,
	.istatus_host_ctrl_addr = REG_ISTATUS_HOST_CTRL_NEW,
	.ds_lock_check_bitmask = 0x7E,
	.ds_lock_check_val = 0x7E,
	.irq_tbl = {
		[MTK_IRQ_SRC_ADO]   = 1,
		[MTK_IRQ_SRC_DPMAIF]  = 24,
		[MTK_IRQ_SRC_CLDMA0]  = 27,
		[MTK_IRQ_SRC_CLDMA1]  = 26,
		[MTK_IRQ_SRC_CLDMA2]  = 25,
		[MTK_IRQ_SRC_MHCCIF]  = 28,
		[MTK_IRQ_SRC_DPMAIF2] = 29,
		[MTK_IRQ_SRC_SAP_RGU] = 30,
		[MTK_IRQ_SRC_CLDMA3]  = 31,
		[MTK_IRQ_SRC_PM_LOCK] = 0,
		[MTK_IRQ_SRC_DPMAIF3] = 7,
		[MTK_IRQ_SRC_CLDMA4]  = 5,
		[MTK_IRQ_SRC_DPMAIF6]  = 10,
		[MTK_IRQ_SRC_TRAS_SYNC] = 9,
	},
	.atr_init = mtk_pci_atr_init_m9xx,
	.dev_reset = mtk_pci_dev_reset_m9xx,
	.get_dev_log = mtk_pci_get_dev_log_m9xx,
	.get_log_region_size = mtk_pci_get_log_region_size_m9xx,
	.dsd_init = mtk_pci_init_dsd,
	.dsd_exit = mtk_pci_exit_dsd,
	.force_mac_active = mtk_pci_force_mac_active_m9xx,
	.force_mac_sleep = mtk_pci_force_mac_sleep_m9xx,
};
