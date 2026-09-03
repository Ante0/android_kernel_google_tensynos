/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_PCI_H__
#define __MTK_PCI_H__

#include <linux/pci.h>
#include "mtk_debug.h"
#include "mtk_debugfs.h"
#include "mtk_dev.h"

enum mtk_irq_src {
	MTK_IRQ_SRC_INVALID = -1,
	MTK_IRQ_SRC_MIN,
	MTK_IRQ_SRC_MHCCIF,
	MTK_IRQ_SRC_SAP_RGU,
	MTK_IRQ_SRC_DPMAIF,
	MTK_IRQ_SRC_DPMAIF2,
	MTK_IRQ_SRC_CLDMA0,
	MTK_IRQ_SRC_CLDMA1,
	MTK_IRQ_SRC_CLDMA2,
	MTK_IRQ_SRC_CLDMA3,
	MTK_IRQ_SRC_PM_LOCK,
	MTK_IRQ_SRC_DPMAIF3,
	MTK_IRQ_SRC_ADO,
	MTK_IRQ_SRC_CLDMA4,
	MTK_IRQ_SRC_DPMAIF6,
	MTK_IRQ_SRC_TRAS_SYNC,
	MTK_IRQ_SRC_MAX
};

/* Software event to device */
enum mtk_pci_h2d_sw_evt {
	H2D_SW_EVT_PM_LOCK = 0,
	H2D_SW_EVT_FRC_SYNC_START = 2,
	H2D_SW_EVT_FRC_SYNC_END = 3,
	H2D_SW_EVT_TRIGGER_MDEE = 5,
};

enum mtk_pci_d2h_sw_evt {
	D2H_SW_EVT_PM_LOCK_ACK = 0,
};

enum mtk_reset_type {
	RESET_FLDR,
	RESET_PLDR,
	RESET_MHCCIF,
	RESET_AEE_REBOOT,
	RESET_NONE,
};

enum mtk_atr_type {
	ATR_PCI2AXI = 0,
	ATR_AXI2PCI,
};

enum mtk_atr_src_port {
	ATR_SRC_PCI_WIN0 = 0,
	ATR_SRC_PCI_WIN1,
	ATR_SRC_AXIS_0,
	ATR_SRC_AXIS_1,
	ATR_SRC_AXIS_2,
	ATR_SRC_AXIS_3,
};

enum mtk_atr_dst_port {
	ATR_DST_PCI_TRX = 0,
	ATR_DST_AXIM_0 = 4,
	ATR_DST_AXIM_1,
	ATR_DST_AXIM_2,
	ATR_DST_AXIM_3,
};

enum mtk_l1ss_grp {
	L1SS_PM = 0,
	L1SS_EXT_EVT = 1,
	L1SS_DATA = 2,
	L1SS_MD_FRC = 4,
};

enum mtk_mac_active_user {
	MAC_ACTIVE_PM_L1SS	= BIT(0),
	MAC_ACTIVE_EXT_EVT_L1SS	= BIT(1),
	MAC_ACTIVE_DATA_L1SS	= BIT(2),
	MAC_ACTIVE_DS_LOCK	= BIT(3),
	MAC_ACTIVE_MD_FRC	= BIT(4),
};

enum mtk_pci_evt_h2d {
	DEV_EVT_H2D_EXTEND_BASE            = DEV_EVT_H2D_MAX,
	EXT_EVT_H2D_PCIE_DS_LOCK           = DEV_EVT_H2D_EXTEND_BASE,
	EXT_EVT_H2D_RESERVED_FOR_CLDMA0    = DEV_EVT_H2D_EXTEND_BASE << 1,
	EXT_EVT_H2D_RESERVED_FOR_CLDMA1    = DEV_EVT_H2D_EXTEND_BASE << 2,
	EXT_EVT_H2D_RESERVED_FOR_CLDMA3    = DEV_EVT_H2D_EXTEND_BASE << 3,
	EXT_EVT_H2D_RESERVED_FOR_CLDMA2    = DEV_EVT_H2D_EXTEND_BASE << 4,
	EXT_EVT_H2D_RESERVED_FOR_DPMAIF    = DEV_EVT_H2D_EXTEND_BASE << 5,
	EXT_EVT_H2D_PCIE_PM_SUSPEND_REQ    = DEV_EVT_H2D_EXTEND_BASE << 6,
	EXT_EVT_H2D_PCIE_PM_RESUME_REQ     = DEV_EVT_H2D_EXTEND_BASE << 7,
	EXT_EVT_H2D_PCIE_PM_SUSPEND_REQ_AP = DEV_EVT_H2D_EXTEND_BASE << 8,
	EXT_EVT_H2D_PCIE_PM_RESUME_REQ_AP  = DEV_EVT_H2D_EXTEND_BASE << 9,
	EXT_EVT_H2D_DRM_DISABLE_AP         = DEV_EVT_H2D_EXTEND_BASE << 10,
	EXT_EVT_H2D_RESERVED_FOR_TEST      = DEV_EVT_H2D_EXTEND_BASE << 11,
};

enum mtk_pci_evt_d2h {
	DEV_EVT_D2H_EXTEND_BASE            = DEV_EVT_D2H_MAX,
	EXT_EVT_D2H_PCIE_DS_LOCK_ACK       = DEV_EVT_D2H_EXTEND_BASE,
	EXT_EVT_D2H_RESERVED_FOR_CLDMA0    = DEV_EVT_D2H_EXTEND_BASE << 1,
	EXT_EVT_D2H_RESERVED_FOR_CLDMA1    = DEV_EVT_D2H_EXTEND_BASE << 2,
	EXT_EVT_D2H_RESERVED_FOR_CLDMA3    = DEV_EVT_D2H_EXTEND_BASE << 3,
	EXT_EVT_D2H_RESERVED_FOR_CLDMA2    = DEV_EVT_D2H_EXTEND_BASE << 4,
	EXT_EVT_D2H_RESERVED_FOR_DPMAIF    = DEV_EVT_D2H_EXTEND_BASE << 5,
	EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK    = DEV_EVT_D2H_EXTEND_BASE << 6,
	EXT_EVT_D2H_PCIE_PM_RESUME_ACK     = DEV_EVT_D2H_EXTEND_BASE << 7,
	EXT_EVT_D2H_PCIE_PM_SUSPEND_ACK_AP = DEV_EVT_D2H_EXTEND_BASE << 8,
	EXT_EVT_D2H_PCIE_PM_RESUME_ACK_AP  = DEV_EVT_D2H_EXTEND_BASE << 9,
	EXT_EVT_D2H_SOFT_OFF_NOTIFY        = DEV_EVT_D2H_EXTEND_BASE << 10,
	EXT_EVT_D2H_FRC_DONE_NOTIFY        = DEV_EVT_D2H_EXTEND_BASE << 11,
	EXT_EVT_D2H_RESERVED_FOR_TEST1	   = DEV_EVT_D2H_EXTEND_BASE << 12,
	EXT_EVT_D2H_RESERVED_FOR_TEST2	   = DEV_EVT_D2H_EXTEND_BASE << 13,
};

#define L1SS_BIT_L1(grp)        BIT(((grp) << 2) + 1)
#define L1SS_BIT_L1_1(grp)      BIT(((grp) << 2) + 2)
#define L1SS_BIT_L1_2(grp)      BIT(((grp) << 2) + 3)
#define L1SS_BIT_L0S_L1(grp)    (0xF << ((grp) << 2))

#define MTK_IRQSRC_EXCEPT_ATR_MASK    0xFF77FFFF

#define MTK_ENABLE_INTR_BIT           BIT(0)
#define MTK_FORCE_MAC_ACTIVE_BIT      BIT(6)
#define MTK_DS_LOCK_REG_BIT           BIT(7)

#define MTK_PCI_MSI_MESSAGE_CONTROL  GENMASK(6, 4)

#define MTK_PCI_CLASS                 0x0D4000
#define MTK_PCI_VENDOR_ID             0x14C3

#define MTK_DISABLE_DS_BIT(grp)       BIT(grp)
#define MTK_ENABLE_DS_BIT(grp)        (BIT(grp) << 8)

#define MTK_CFG_INFO_BIT_SHIFT        4

#define MTK_PCI_DEV_CFG(id, cfg) \
{ \
	PCI_DEVICE(MTK_PCI_VENDOR_ID, id), \
	MTK_PCI_CLASS, PCI_ANY_ID, \
	.driver_data = (kernel_ulong_t)&(cfg), \
}

#define MTK_CFG_IRQ_DFLT_MASK		BIT(0)
#define MTK_CFG_DFLT_DISABLE_L1SS	BIT(1)
#define MTK_CFG_DISABLE_AP_DRM		BIT(2)
#define MTK_CFG_MHCCIF_TRSL		BIT(3)
#define MTK_CFG_RGU_L2_AUTO_ACK		BIT(4)
#define MTK_CFG_HOST_GET_DEV_LOG	BIT(5)
#define MTK_CFG_PM_SW_IRQ		BIT(6)
#define MTK_CFG_FRC_SYNC		BIT(7)

#define MTK_BAR_0_1_IDX                 0
#define MTK_BAR_2_3_IDX                 2
#define MTK_BAR_4_5_IDX                 4

/* Only use BAR0/1 and 2/3, so we should input 0b0101 for the two bar,
 * Input 0xf would cause error.
 */
#define MTK_REQUESTED_BARS \
	((1 << MTK_BAR_0_1_IDX) | \
	 (1 << MTK_BAR_2_3_IDX) | \
	 (1 << MTK_BAR_4_5_IDX))

#define MTK_IRQ_CNT_MIN				1
#define MTK_IRQ_CNT_MAX				32
#define MTK_IRQ_NAME_LEN			20

#define ATR_PORT_OFFSET				0x100
#define ATR_TABLE_OFFSET			0x20
#define ATR_TABLE_NUM_PER_ATR			8
#define ATR_WIN0_SRC_ADDR_LSB_DEFT		0x0000007f
#define ATR_PCIE_REG_TRSL_ADDR			0x10000000
#define ATR_PCIE_REG_SIZE			0x00400000
#define ATR_PCIE_REG_PORT			ATR_SRC_PCI_WIN0
#define ATR_PCIE_REG_TABLE_NUM			1
#define ART_PCIE_REG_MHCCIF_TABLE_NUM		0
#define ATR_PCIE_REG_TRSL_PORT			ATR_DST_AXIM_0
#define ATR_PCIE_DEV_DMA_PORT_START		ATR_SRC_AXIS_0
#define ATR_PCIE_DEV_DMA_PORT_END		ATR_SRC_AXIS_2
#define ATR_PCIE_DEV_DMA_SRC_ADDR		0x00000000
#define ATR_PCIE_DEV_DMA_TRANSPARENT		1
#define ATR_PCIE_DEV_DMA_SIZE			0
#define ATR_PCIE_DEV_DMA_TABLE_NUM		0
#define ATR_PCIE_DEV_DMA_TRSL_ADDR		0x00000000

#define MTK_PCIE_PORT_NUM			(1)

struct mtk_pci_irq_desc {
	struct mtk_md_dev *mdev;
	u32 msix_bits;
	char name[MTK_IRQ_NAME_LEN];
};

struct mtk_pci_dev_cfg {
	u32 flag;
	u32 mhccif_rc_base_addr;
	u32 mhccif_rc_reg_trsl_addr;
	u32 mhccif_trsl_size;
	u32 mhccif_rc2ep_pcie_pm_counter;
	u32 istatus_host_ctrl_addr;
	u32 ds_lock_check_bitmask;
	u32 ds_lock_check_val;
	int irq_tbl[MTK_IRQ_SRC_MAX];
	int (*atr_init)(struct mtk_md_dev *mdev);
	int (*dev_reset)(struct mtk_md_dev *mdev, enum mtk_reset_type type);
	int (*get_dev_log)(struct mtk_md_dev *mdev,
			   void *buf, size_t count, enum mtk_dev_log_type type);
	int (*get_log_region_size)(struct mtk_md_dev *mdev, enum mtk_dev_log_type type);
	int (*dev_log_buff_init)(struct mtk_md_dev *mdev);
	int (*dev_log_buff_exit)(struct mtk_md_dev *mdev);
	int (*dsd_init)(struct mtk_md_dev *mdev);
	int (*dsd_exit)(struct mtk_md_dev *mdev);
	void (*force_mac_active)(struct mtk_md_dev *mdev, bool enable,
				 enum mtk_mac_active_user user);
	void (*force_mac_sleep)(struct mtk_md_dev *mdev, bool enable);
};

struct mtk_pci_priv {
	struct mtk_md_dev *mdev;
	const struct mtk_pci_dev_cfg *cfg;
	void __iomem *bar23_addr;
	void __iomem *bar45_addr;
	void __iomem *mac_reg_base;
	void __iomem *ext_reg_base;
	int rc_hp_on; /* Bridge hotplug status */
	int rgu_irq_id;
	int irq_cnt;
	int irq_type;
	void *irq_cb_data[MTK_IRQ_CNT_MAX];

	int (*irq_cb_list[MTK_IRQ_CNT_MAX])(int irq_id, void *data);
	struct mtk_pci_irq_desc irq_desc[MTK_IRQ_CNT_MAX];
	struct list_head mhccif_cb_list;
	/* mhccif_lock: lock to protect mhccif_cb_list */
	spinlock_t mhccif_lock;
	struct work_struct mhccif_work;
	struct workqueue_struct *mhccif_wq;
	/* mac_active_lock: lock to protect mac active */
	spinlock_t mac_active_lock;
	u32 mac_active_flag;
	int mhccif_irq_id;
	struct delayed_work rgu_work;
	struct pci_saved_state *saved_state;
	u16 ltr_max_snoop_lat;
	u16 ltr_max_nosnoop_lat;
	u32 l1ss_ctl1;
	u32 l1ss_ctl2;
	u32 parent_l1ss_ctl1;
	u32 parent_l1ss_ctl2;
	void *dev_log_buff;
	void *except;
	void *frc;
	void *pm;
	void *messenger;
	void *log_region_cfg;

	struct dentry *dentry;
};

struct mtk_atr_cfg {
	u64 src_addr;
	u64 trsl_addr;
	u64 size;
	u32 type;      /* Port type */
	u32 port;      /* Port number */
	u32 table;     /* Table number (8 tables for each port) */
	u32 trsl_id;
	u32 trsl_param;
	u32 transparent;
};

/* Read value from MD. For PCIe, it's BAR 0/1 MMIO read */
u32 mtk_pci_mac_read32(struct mtk_pci_priv *priv, u64 addr);
/* Write value to MD. For PCIe, it's BAR 0/1 MMIO write */
void mtk_pci_mac_write32(struct mtk_pci_priv *priv, u64 addr, u32 val);
#if IS_ENABLED(CONFIG_GOOGLE_ATR_WRITE_RETRY)
void mtk_pci_mac_write32_with_retry(struct mtk_pci_priv *priv, u64 addr, u32 val, u32 retry_count);
#endif
/* Read value from MD. For PCIe, it's BAR 2/3 MMIO read */
u32 mtk_pci_read32(struct mtk_md_dev *mdev, u64 addr);
/* Write value to MD. For PCIe, it's BAR 2/3 MMIO write */
void mtk_pci_write32(struct mtk_md_dev *mdev, u64 addr, u32 val);
/* Device operations */
u32 mtk_pci_get_dev_state(struct mtk_md_dev *mdev);
void mtk_pci_ack_dev_state(struct mtk_md_dev *mdev, u32 state);
u32 mtk_pci_get_ds_status(struct mtk_md_dev *mdev);
void mtk_pci_ds_lock(struct mtk_md_dev *mdev);
void mtk_pci_ds_unlock(struct mtk_md_dev *mdev);
void mtk_pci_enable_l1ss_ds(struct mtk_md_dev *mdev, u32 type, enum mtk_mac_active_user user);
void mtk_pci_disable_l1ss_ds(struct mtk_md_dev *mdev, u32 type, enum mtk_mac_active_user user);
u32 mtk_pci_get_resume_state(struct mtk_md_dev *mdev);
u32 mtk_pci_get_dev_cfg(struct mtk_md_dev *mdev);
/* IRQ Related operations */
int mtk_pci_get_irq_id(struct mtk_md_dev *mdev, enum mtk_irq_src irq_src);
int mtk_pci_get_virq_id(struct mtk_md_dev *mdev, int irq_id);
int mtk_pci_register_irq(struct mtk_md_dev *mdev, int irq_id,
			 int (*irq_cb)(int irq_id, void *data), void *data);
int mtk_pci_unregister_irq(struct mtk_md_dev *mdev, int irq_id);
int mtk_pci_mask_irq(struct mtk_md_dev *mdev, int irq_id);
int mtk_pci_unmask_irq(struct mtk_md_dev *mdev, int irq_id);
void mtk_pci_irq_suspend_action(struct mtk_md_dev *mdev);
void mtk_pci_irq_resume_action(struct mtk_md_dev *mdev);
int mtk_pci_clear_irq(struct mtk_md_dev *mdev, int irq_id);
int mtk_pci_reset_sys_irq(struct mtk_md_dev *mdev);
void mtk_pci_send_sw_evt(struct mtk_md_dev *mdev, enum mtk_pci_h2d_sw_evt evt);
void mtk_pci_clear_sw_evt(struct mtk_md_dev *mdev, enum mtk_pci_d2h_sw_evt evt);
void mtk_pci_trigger_mdee(struct mtk_md_dev *mdev, u32 val);
/* External event related */
int mtk_pci_register_ext_evt(struct mtk_md_dev *mdev, u32 chs,
			     int (*evt_cb)(u32 status, void *data), void *data);
void mtk_pci_unregister_ext_evt(struct mtk_md_dev *mdev, u32 chs);
void mtk_pci_mask_ext_evt(struct mtk_md_dev *mdev, u32 chs);
void mtk_pci_unmask_ext_evt(struct mtk_md_dev *mdev, u32 chs);
void mtk_pci_clear_ext_evt(struct mtk_md_dev *mdev, u32 chs);
int mtk_pci_send_ext_evt(struct mtk_md_dev *mdev, u32 ch);
int mtk_pci_fldr(struct mtk_md_dev *mdev);
int mtk_pci_pldr(struct mtk_md_dev *mdev);
int mtk_pci_reset(struct mtk_md_dev *mdev, enum mtk_reset_type type);
int mtk_pci_reinit(struct mtk_md_dev *mdev, enum mtk_reinit_type type);
int mtk_pci_reinit_mac(struct mtk_md_dev *mdev, bool resuem_from_L2);
bool mtk_pci_link_check(struct mtk_md_dev *mdev);
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
bool mtk_pci_link_check_silent(struct mtk_md_dev *mdev);
#endif
bool mtk_pci_mmio_check(struct mtk_md_dev *mdev);
int mtk_pci_get_hp_status(struct mtk_md_dev *mdev);
int mtk_pci_dump(struct mtk_md_dev *mdev);
void mtk_pci_info_dump(struct mtk_md_dev *mdev);
void mtk_pci_write_pm_cnt(struct mtk_md_dev *mdev, u32 val);
u32 mtk_pci_get_md_ack_user(struct mtk_md_dev *mdev);
u32 mtk_pci_get_resume_user(struct mtk_md_dev *mdev);
int mtk_pci_get_dev_log(struct mtk_md_dev *mdev,
			void *buf, size_t count, enum mtk_dev_log_type type);
int mtk_pci_get_log_region_size(struct mtk_md_dev *mdev, enum mtk_dev_log_type type);
u32 mtk_pci_get_tras_cfg(struct mtk_md_dev *mdev);
u32 mtk_pci_get_tras_frc(struct mtk_md_dev *mdev);
int mtk_pci_setup_atr(struct mtk_md_dev *mdev, struct mtk_atr_cfg *cfg);
#if IS_ENABLED(CONFIG_GOOGLE_B495424578_DEBUG)
void mtk_pci_dump_atr_bar23(struct mtk_md_dev *mdev);
void mtk_pci_dump_atr(struct mtk_md_dev *mdev);
#endif
#if IS_ENABLED(CONFIG_GOOGLE_B528903481_DEBUG)
void mtk_pci_mmio_hw_check(struct mtk_md_dev *mdev);
#endif
void mtk_pci_atr_disable(struct mtk_pci_priv *priv);
void mtk_pci_dump_atr_doorbell(struct mtk_md_dev *mdev);
void mtk_pci_clear_atr_doorbell(struct mtk_md_dev *mdev);
void mtk_pci_regs_dump(struct mtk_md_dev *mdev, enum mtk_debug_mask mask,
		       enum mtk_memlog_region_id region_id,
		       const char *msg, unsigned long long addr, size_t len);
void mtk_pci_restore_aspm_l1ss_state(struct mtk_md_dev *mdev);
#define MTK_REGS_DUMP(mdev, mask, region_id, msg, addr, len) \
	mtk_pci_regs_dump(mdev, mask, region_id, msg, addr, len)

#if IS_ENABLED(CONFIG_MTK_PCIESLT_SUPPORT)
void mtk_pcie_slt_init(struct mtk_md_dev *mdev);
void mtk_pcie_slt_reinit(struct mtk_md_dev *mdev);
void mtk_pcie_slt_exit(struct mtk_md_dev *mdev);
#else
static inline void mtk_pcie_slt_init(struct mtk_md_dev *mdev) {}
static inline void mtk_pcie_slt_reinit(struct mtk_md_dev *mdev) {}
static inline void mtk_pcie_slt_exit(struct mtk_md_dev *mdev) {}
#endif /* CONFIG_MTK_PCIESLT_SUPPORT */
#endif /* __MTK_PCI_H__ */
