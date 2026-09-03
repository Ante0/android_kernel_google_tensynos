// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021-2024 Google LLC
 */

#include <linux/bitops.h>
#include <linux/firmware.h>
#include <linux/log2.h>
#include <linux/types.h>
#include <linux/version.h>

#include <linux/pcie_google_if.h>
#include <trace/hooks/pci.h>
#include "pcie-google.h"
#include "pcie-designware-host-customized.h"

#define CREATE_TRACE_POINTS
#include "pcie_trace.h"

#define DEFAULT_PERST_DELAY_US		20000
#define LINK_UP_MAX_RETRIES		4800
#define LINK_UP_WAIT_TIME_US		10
#define MAX_L2_COUNT			2000
#define L2_PME_TO_ACK_WAIT_US		10
#define SRAM_INIT_DELAY_US		100
#define SRAM_INIT_TIMEOUT_US		1000
#define WIDTH_SPEED_CHANGE_WAIT_US	10
#define MAX_TIMEOUT_WIDTH_SPEED_CHANGE	10000 /* Link width/speed change timeout (in 10us units) */
#define LINK_STATE_CHANGE_WAIT_US	10
#define MAX_TIMEOUT_LINK_STATE_CHANGE	100000 /* Link state change timeout (in 10us units) */
#define MAX_RETRAIN_TIME_US		1000000
#define MAX_PDG_TXN_CHECK_TIME_US	12000

/* SII region offsets */
#define PCIE_SII_BUS_DBG		0x0
#define BRDG_SLV_XFER_PENDING		BIT(1)
#define RADM_XFER_PENDING		BIT(4)

#define LINK_RST_STATUS			0x1c
#define SMLH_REQ_RST_NOT		BIT(1)

#define PM_UNLOCK_ERR_MSG		0x24
#define APPS_PM_XMT_TURNOFF		BIT(0)
#define RADM_PM_TO_ACK			BIT(7)

#define PM_STATE			0x28
#define CURRENT_STATE(x)		((x) & (GENMASK(2, 0)))
#define LTSSM_STATE(x)			(((x) & (GENMASK(8, 3))) >> 3)
#define LINK_IN_L0S			BIT(12)
#define LINK_IN_L1			BIT(13)
#define LINK_IN_L2			BIT(14)
#define MASTER_STATE(x)			(((x) & (GENMASK(20, 16))) >> 16)
#define SLAVE_STATE(x)			(((x) & (GENMASK(25, 21))) >> 21)
#define L1SUB_STATE(x)			(((x) & (GENMASK(28, 26))) >> 26)
#define LINK_IN_L1SUB			BIT(30)
#define DW_PCIE_LTSSM_CFG_LINKWD_START	0x07
#define DW_PCIE_LTSSM_CFG_LINKWD_ACEPT	0x08
#define DW_PCIE_LTSSM_CFG_LANENUM_WAI	0x09
#define DW_PCIE_LTSSM_CFG_LANENUM_ACEPT	0x0A
#define DW_PCIE_LTSSM_CFG_COMPLETE	0x0B
#define DW_PCIE_LTSSM_CFG_IDLE		0x0C
#define DW_PCIE_LTSSM_RCVRY_LOCK	0x0D
#define DW_PCIE_LTSSM_RCVRY_EQ0		0x20
#define DW_PCIE_LTSSM_RCVRY_EQ3		0x23
#define S_L1_IDLE                       0x14

#define PM_CTRL_STATUS			0x2C
#define L1_PWR_OFF_EN			BIT(11)

/* TOP region offsets */
#define PCIE_PHY_CFG1			0x0
#define PG_MODE_EN			BIT(0)
#define SRAM_EXT_LD_DONE		BIT(23)

#define PCIE_PHY_STATUS1		0x10
#define SRAM_INIT_DONE			BIT(2)

#define PCIE_CTL_CFG1			0x14
#define LTSSM_ENABLE			BIT(0)

#define PCIE_CTL_STATUS1		0x18
#define SMLH_LTSSM_STATE		GENMASK(5, 0)
#define PORT_LOGIC_LTSSM_STATE_L1_IDLE	0x14
#define SMLH_LINKUP			BIT(6)
#define RDLH_LINKUP			BIT(7)
#define LINKUP				(SMLH_LINKUP | RDLH_LINKUP)

#define PCIE_AXI_QOS			0x24
#define PCIE_AW_USER_VC			GENMASK(2, 0)
#define PCIE_AR_USER_VC			GENMASK(5, 3)

#define PCIE_RESET_STATUS		0x38
#define APP_INIT_RST			BIT(27)

#define PIPE_COMMON_STATUS		0x70
#define RXELECIDLE_DISABLE		BIT(29)
#define TXCOMMONMODE_DISABLE		BIT(30)

/* DWC Port Logic offsets */
/* bit-fields of PCIE_PORT_MULTI_LANE_CTRL */
#define PORT_TARGET_LINK_WIDTH_MASK		GENMASK(5, 0)
#define PORT_DIRECT_LINK_WIDTH_CHANGE		BIT(6)

#define VC0_P_RX_Q_CTRL_OFF		0x748
#define VC0_P_DATA_CREDIT		GENMASK(11, 0)

#define AMBA_LINK_TIMEOUT_OFF		0x8D4
#define LINK_TIMEOUT_PERIOD		GENMASK(7, 0)

#define S5400_VENDOR_DEVICE_ID		0xA5A5144D
#define S5400_MSI_CAP_OFFSET		0x50

#define PCI_ICDS_REG			0x3c009554
#define PCI_ICD_OUTSTANDING		BIT(9)

/* From L1 PM Substates Capabiliity */
enum {
	PCI_L1SS_TPOWERON_SCALE_2_US,
	PCI_L1SS_TPOWERON_SCALE_10_US,
	PCI_L1SS_TPOWERON_SCALE_100_US,
	PCI_L1SS_TPOWERON_SCALE_RESERVED
};

/* For LGA
 * T_Poweron = 100us
 * Common_mode_restore_time = 60us
 */
#define LGA_TPOWERON_SCALE	PCI_L1SS_TPOWERON_SCALE_10_US
#define LGA_TPOWERON_VALUE	10
#define LGA_CM_RESTORE_TIME	60

static LIST_HEAD(gpcie_inst_list);
static DEFINE_SPINLOCK(gpcie_inst_lock);

#define PCIE_GEN_4_GOOGLE_ICC_BW_MB 1800
#define PCIE_GEN_3_GOOGLE_ICC_BW_MB 900
#define PCIE_GEN_2_GOOGLE_ICC_BW_MB 450
#define PCIE_GEN_1_GOOGLE_ICC_BW_MB 225

struct pci_pwrctrl;

void google_pci_pwrctrl_init(struct pci_pwrctrl *pwrctrl, struct device *dev)
{
}

int google_pci_pwrctrl_device_set_ready(struct pci_pwrctrl *pwrctrl)
{
	return 0;
}
void google_pci_pwrctrl_device_unset_ready(struct pci_pwrctrl *pwrctrl) {}

int devm_google_pci_pwrctrl_device_set_ready(struct device *dev,
					     struct pci_pwrctrl *pwrctrl)
{
	return 0;
}

static void google_pcie_dump_icd(struct google_pcie *gpcie, char *where)
{
	u32 __iomem  *reg_ptr;
	u32 val;


	if (!where)
		return;

	/* Only print this info for PCIe1 */

	if (gpcie->domain != 1)
		return;

	reg_ptr = ioremap(PCI_ICDS_REG, SZ_4);
	if (!reg_ptr) {
		dev_err(gpcie->dev, "reg_ptr is NULL");
		return;
	}
	val = ioread32(reg_ptr);

	if (val & PCI_ICD_OUTSTANDING)
		dev_info(gpcie->dev, "ICD Pend Status=%#08x %s\n", val, where);

	iounmap(reg_ptr);
}

static int google_pcie_set_icc_bw(struct google_pcie *gpcie, u32 avg_bw,
				  u32 peak_bw)
{
	int ret;

	if (!gpcie->icc_path)
		return -EINVAL;

	google_icc_set_read_bw_gmc(gpcie->icc_path, avg_bw, peak_bw, 0, 1);
	google_icc_set_write_bw_gmc(gpcie->icc_path, avg_bw, peak_bw, 0, 1);
	ret = google_icc_update_constraint_async(gpcie->icc_path);
	if (ret)
		dev_err(gpcie->dev, "failed to update constraints: (%d)\n",
			ret);

	return ret;
}

static void google_pcie_config_bw(struct google_pcie *gpcie)
{
	gpcie->avg_bw = PCIE_GEN_4_GOOGLE_ICC_BW_MB * 2;
	gpcie->peak_bw = PCIE_GEN_4_GOOGLE_ICC_BW_MB * 2;
}

static void google_pcie_set_l1ss_capabilities(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;
	u32 l1ss;
	u32 val;

	l1ss = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_L1SS);
	val = dw_pcie_readl_dbi(pci, l1ss + PCI_L1SS_CAP);

	val &= ~(PCI_L1SS_CAP_CM_RESTORE_TIME | PCI_L1SS_CAP_P_PWR_ON_VALUE |
		 PCI_L1SS_CAP_P_PWR_ON_SCALE);
	val |= FIELD_PREP(PCI_L1SS_CAP_CM_RESTORE_TIME, LGA_CM_RESTORE_TIME);
	val |= FIELD_PREP(PCI_L1SS_CAP_P_PWR_ON_VALUE, LGA_TPOWERON_VALUE);
	val |= FIELD_PREP(PCI_L1SS_CAP_P_PWR_ON_SCALE, LGA_TPOWERON_SCALE);

	dw_pcie_writel_dbi(pci, l1ss + PCI_L1SS_CAP, val);
}

static u32 google_pcie_read_dbi(struct dw_pcie *pci, void __iomem *base,
				 u32 reg, size_t size)
{
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	if (!gpcie->powered_on) {
		spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
		dev_err_ratelimited(gpcie->dev,
			"Preventing invalid attempt to read DBI while powered down");
		return U32_MAX;
	}
	dw_pcie_read(base + reg, size, &val);
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

	return val;
}

static int google_pcie_rd_own_conf(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*val = google_pcie_read_dbi(pci, pci->dbi_base, where, size);

	return PCIBIOS_SUCCESSFUL;
}

static void google_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base,
				  u32 reg, size_t size, u32 val)
{
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	unsigned long flags;

	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	if (!gpcie->powered_on) {
		spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
		dev_err_ratelimited(gpcie->dev,
			"Preventing invalid attempt to write DBI while powered down");
		return;
	}
	dw_pcie_write(base + reg, size, val);
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
}

static int google_pcie_wr_own_conf(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);

	if (PCI_SLOT(devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	google_pcie_write_dbi(pci, pci->dbi_base, where, size, val);

	return PCIBIOS_SUCCESSFUL;
}

static void google_pcie_set_l1_entrance_latency(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;
	u32 val;

	if (gpcie->l1_entrance_latency < U32_MAX) {
		val = google_pcie_read_dbi(pci, pci->dbi_base, PCIE_PORT_AFR, 4);
		val = u32_replace_bits(val, gpcie->l1_entrance_latency,
					PORT_AFR_L1_ENTRANCE_LAT_MASK);
		google_pcie_write_dbi(pci, pci->dbi_base, PCIE_PORT_AFR, 4, val);
	}
}

/* Dedicated ops for accessing RC's config space */
static struct pci_ops google_pcie_ops = {
	.read = google_pcie_rd_own_conf,
	.write = google_pcie_wr_own_conf,
};

static void __iomem *google_pcie_other_conf_map_bus(struct pci_bus *bus,
						    unsigned int devfn,
						    int where)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	struct dw_pcie_rp *pp = bus->sysdata;
#else
	struct pcie_port *pp = bus->sysdata;
#endif
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	u64 limit_addr;
	u32 retries;
	u32 busdev;
	u32 offset;
	u32 val;
	int type;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (gpcie->curr_mapped_busdev == busdev)
		goto atu_enabled;

	if (pci_is_root_bus(bus->parent))
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;

	/* Setup 0th outbound ATU region for config transactions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	offset = PCIE_ATU_UNROLL_BASE(PCIE_ATU_REGION_DIR_OB, 0);
#else
	offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(0);
#endif
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_LOWER_BASE, 4,
		      lower_32_bits(pp->cfg0_base));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_UPPER_BASE, 4,
		      upper_32_bits(pp->cfg0_base));
	limit_addr = pp->cfg0_base + pp->cfg0_size - 1;
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_LOWER_LIMIT, 4,
		      lower_32_bits(limit_addr));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_UPPER_LIMIT, 4,
		      upper_32_bits(limit_addr));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_LOWER_TARGET, 4,
		      lower_32_bits(busdev));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_UPPER_TARGET, 4,
		      upper_32_bits(busdev));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_REGION_CTRL1, 4,
		      type);
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_REGION_CTRL2, 4,
		      PCIE_ATU_ENABLE);

	/* Make sure ATU enable takes effect before any subsequent config and I/O accesses */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		dw_pcie_read(pci->atu_base + offset + PCIE_ATU_UNR_REGION_CTRL2,
			     4, &val);
		if (val & PCIE_ATU_ENABLE) {
			gpcie->curr_mapped_busdev = busdev;
			goto atu_enabled;
		}

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Outbound iATU is not being enabled\n");

	return NULL;

atu_enabled:
	return pp->va_cfg0_base + where;
}

static int google_pcie_rd_other_conf(struct pci_bus *bus, unsigned int devfn,
				     int where, int size, u32 *val)
{
	int ret;
	u32 vendor_device;
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	unsigned long flags;

	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	if (!gpcie->is_link_up) {
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	ret = pci_generic_config_read(bus, devfn, where, size, val);

	if (where == PCI_CLASS_REVISION && (*val >> 8) == 0x0) {

		/* S5400 workaround: Fake class code as network device */
		if ((pci_generic_config_read(bus, devfn, PCI_VENDOR_ID, 4,
					     &vendor_device) == 0) &&
		    (vendor_device == S5400_VENDOR_DEVICE_ID)) {
			*val |= (PCI_CLASS_NETWORK_OTHER << 16);
			dev_dbg(pci->dev,
				 "Fake PCI class as a Network device (0x%x)\n",
				 *val);
		}
	}

	if ((where == S5400_MSI_CAP_OFFSET + PCI_MSI_FLAGS) &&
	    (*val & PCI_MSI_FLAGS_QMASK) == 0x0) {
		/* S5400 workaround: Fake MSI multiple message as capable
		*                   of 16 vectors (0x4) */
		if ((pci_generic_config_read(bus, devfn, PCI_VENDOR_ID, 4,
					     &vendor_device) == 0) &&
		    (vendor_device == S5400_VENDOR_DEVICE_ID)) {
			*val |= (0x4 << 1);
			dev_dbg(pci->dev,
				 "Fake MSI capability as: 0x%x\n", *val);
		}
	}
	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);

	return ret;
}

static int google_pcie_wr_other_conf(struct pci_bus *bus, unsigned int devfn,
				     int where, int size, u32 val)
{
	int ret;
	struct dw_pcie *pci = to_dw_pcie_from_pp(bus->sysdata);
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	unsigned long flags;

	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	if (!gpcie->is_link_up) {
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	ret = pci_generic_config_write(bus, devfn, where, size, val);
	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);

	return ret;
}

static u32 google_pcie_get_link_state(struct google_pcie *gpcie)
{
	u32 pm_state;
	u32 pipe_status;
	u32 ltssm_state;
	unsigned long flags;

	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	if (!gpcie->powered_on) {
		spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
		return L2;
	}

	pm_state = readl(gpcie->sii_base + PM_STATE);
	pipe_status = readl(gpcie->top_base + PIPE_COMMON_STATUS);

	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

	dev_dbg(gpcie->dev, "pm_state: 0x%x, pipe_status: 0x%x\n",
		pm_state, pipe_status);

	ltssm_state = LTSSM_STATE(pm_state);
	if (ltssm_state == DW_PCIE_LTSSM_L0)
		return L0;

	if (ltssm_state >= DW_PCIE_LTSSM_CFG_LINKWD_START &&
	    ltssm_state <= DW_PCIE_LTSSM_CFG_IDLE)
		return RECOVERY;

	if (ltssm_state >= DW_PCIE_LTSSM_RCVRY_LOCK &&
	    ltssm_state < DW_PCIE_LTSSM_L0)
		return RECOVERY;

	if (ltssm_state >= DW_PCIE_LTSSM_RCVRY_EQ0 &&
	    ltssm_state <= DW_PCIE_LTSSM_RCVRY_EQ3)
		return RECOVERY;

	if (pm_state & LINK_IN_L0S)
		return L0S;

	if ((pm_state & LINK_IN_L1SUB) && (pipe_status & RXELECIDLE_DISABLE)) {
		if (pipe_status & TXCOMMONMODE_DISABLE)
			return L12;

		return L11;
	}

	/*
	 * In case link is l1sub, L1SUB_STATE(pm_state) value indicates:
	 * 0x3 - indicates waiting for CLKREQ for L1.2 entry
	 * 0x5 - indicates l1.2 entry complete
	 */
	if (pm_state & LINK_IN_L1SUB)
		dev_dbg(gpcie->dev, "%s: l1sub_state: 0x%lx\n", __func__, L1SUB_STATE(pm_state));

	if (pm_state & LINK_IN_L1)
		return L1;

	if (ltssm_state == DW_PCIE_LTSSM_L2_IDLE)
		return L2;

	/* Treat anomalous LTSSM states between L0 and L1ss as L0 */
	if (ltssm_state > DW_PCIE_LTSSM_L0 && ltssm_state < DW_PCIE_LTSSM_L2_IDLE)
		return L0;

	return UNKNOWN;
}

/* Dedicated ops for accessing EP's config space */
static struct pci_ops google_child_pcie_ops = {
	.map_bus = google_pcie_other_conf_map_bus,
	.read = google_pcie_rd_other_conf,
	.write = google_pcie_wr_other_conf,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int google_pcie_host_init(struct dw_pcie_rp *pp)
#else
static int google_pcie_host_init(struct pcie_port *pp)
#endif
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	u32 val;

	pp->bridge->ops = &google_pcie_ops;
	pp->bridge->child_ops = &google_child_pcie_ops;

	if (gpcie->link_timeout_ms) {
		val = dw_pcie_read_dbi(pci, AMBA_LINK_TIMEOUT_OFF, 4);
		val &= ~LINK_TIMEOUT_PERIOD;
		val |= gpcie->link_timeout_ms;
		dw_pcie_write_dbi(pci, AMBA_LINK_TIMEOUT_OFF, 4, val);
		dev_info(gpcie->dev, "Link request queue flush timeout set to %u ms\n",
			 gpcie->link_timeout_ms);
	}

	google_pcie_set_l1ss_capabilities(gpcie);
	google_pcie_set_l1_entrance_latency(gpcie);

	return 0;
}

static const struct dw_pcie_host_ops google_pcie_host_ops = {
	.init = google_pcie_host_init,
	.msi_init = goog_pcie_msi_host_init,
};

static int google_pcie_link_up(struct dw_pcie *pci)
{
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	unsigned long flags;
	u32 ctl_status;
	u32 debug_info;

	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	if (!gpcie->powered_on) {
		spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
		return 0;
	}

	ctl_status = readl(gpcie->top_base + PCIE_CTL_STATUS1);
	debug_info = readl(pci->dbi_base + PCIE_PORT_DEBUG1);
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

	return (((ctl_status & LINKUP) == LINKUP) &&
		(!(debug_info & PCIE_PORT_DEBUG1_LINK_IN_TRAINING)) &&
		((google_pcie_get_link_state(gpcie)) < L2));
}

void google_pcie_assert_perst_n(struct google_pcie *gpcie, bool val)
{
	dev_dbg(gpcie->dev, "%s PERST GPIO\n", val ? "asserting" : "de-asserting");
	gpiod_set_value(gpcie->perstn_gpio, val);
}

/* Percentage (0-100) to weight new data points in moving average */
#define NEW_DATA_AVERAGE_WEIGHT  10
static void link_stats_init(struct google_pcie *gpcie)
{
	memset(&gpcie->link_stats, 0, sizeof(gpcie->link_stats));
}

static void link_stats_log_link_up(struct google_pcie *gpcie, u64 link_up_time)
{
	gpcie->link_stats.link_up_time_avg =
	    DIV_ROUND_CLOSEST(gpcie->link_stats.link_up_time_avg *
			      (100 - NEW_DATA_AVERAGE_WEIGHT) +
			      link_up_time * NEW_DATA_AVERAGE_WEIGHT, 100);
}

/* Wait for a max of 48ms for link up, then timeout */
static int google_pcie_wait_for_link(struct dw_pcie *pci)
{
	u32 i;
	struct google_pcie *gpcie;

	gpcie = dev_get_drvdata(pci->dev);

	google_pcie_dump_icd(gpcie, "wait for link");

	for (i = 0; i < LINK_UP_MAX_RETRIES; i++) {
		if (dw_pcie_link_up(pci)) {
			link_stats_log_link_up(gpcie, i);
			dev_info(pci->dev, "Link up\n");
			google_pcie_dump_icd(gpcie, "Link up");
			return 0;
		}
		usleep_range(LINK_UP_WAIT_TIME_US, LINK_UP_WAIT_TIME_US + 2);
	}

	dev_info(pci->dev, "Phy link never came up\n");
	google_pcie_dump_icd(gpcie, "Phy link never came up");
	return -ETIMEDOUT;
}

static void google_pcie_set_link_width_speed(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;

	pci->max_link_speed = gpcie->target_link_speed;
	pci->num_lanes = gpcie->target_link_width;

	dev_dbg(gpcie->dev, "Setting the link speed: %d width: %d",
		gpcie->target_link_speed, gpcie->target_link_width);
}

static void google_pcie_disable_equalization(struct google_pcie *gpcie)
{
	u32 val;

	/* Bit field name implies GEN3, but applies to GEN3 & GEN4 */
	val = dw_pcie_read_dbi(gpcie->pci, GEN3_RELATED_OFF, 4);
	val |= GEN3_RELATED_OFF_GEN3_EQ_DISABLE;
	dw_pcie_write_dbi(gpcie->pci, GEN3_RELATED_OFF, 4, val);
	dev_dbg(gpcie->dev, "Disabled GEN3/GEN4 Link equalization\n");
}

static void google_pcie_set_posted_rx_q_credits(struct google_pcie *gpcie,
						u32 cred)
{
	u32 val;

	val = dw_pcie_read_dbi(gpcie->pci, VC0_P_RX_Q_CTRL_OFF, 4);
	val = val & ~VC0_P_DATA_CREDIT;
	val = val | cred;
	dw_pcie_write_dbi(gpcie->pci, VC0_P_RX_Q_CTRL_OFF, 4, val);

	dev_dbg(gpcie->dev, "Set posted RX Queue data credits to %d\n", cred);
}

static void google_pcie_wait_for_state(struct google_pcie *gpcie, u32 state)
{
	int i = 0;

	for (i = 0; i < MAX_TIMEOUT_LINK_STATE_CHANGE; i++) {
		if (google_pcie_get_link_state(gpcie) == state)
			return;

		udelay(LINK_STATE_CHANGE_WAIT_US);
	}

	dev_info(gpcie->dev, "%s: wait for link state %d timedout\n", __func__, state);
}

static void google_pcie_disable_ltssm(struct google_pcie *gpcie)
{
	u32 val;

	if (!gpcie->is_link_up) {
		dev_dbg(gpcie->dev, "disabling LTSSM state\n");
		google_pcie_dump_icd(gpcie, "disable ltssm");
		val = readl(gpcie->top_base + PCIE_CTL_CFG1);
		val &= ~LTSSM_ENABLE;
		writel(val, gpcie->top_base + PCIE_CTL_CFG1);
		google_pcie_dump_icd(gpcie, "disable ltssm done");
	}
}

static void google_pcie_fetch_speed_width(struct dw_pcie *pci)
{
	u32 val;
	u8 exp_cap_offset;
	struct google_pcie *gpcie;

	gpcie = dev_get_drvdata(pci->dev);
	exp_cap_offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	val = dw_pcie_read_dbi(pci, exp_cap_offset + PCI_EXP_LNKSTA, 2);
	gpcie->current_link_speed = val & PCI_EXP_LNKSTA_CLS;
	gpcie->current_link_width = (val & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;
}

static int google_pcie_retrain_link(struct dw_pcie *pci)
{
	u32 val;
	u8 exp_cap_offset;
	int retrain_time = 0;

	exp_cap_offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	val = dw_pcie_read_dbi(pci, exp_cap_offset + PCI_EXP_LNKCTL, 2);
	val |= PCI_EXP_LNKCTL_RL;
	dw_pcie_write_dbi(pci, exp_cap_offset + PCI_EXP_LNKCTL, 2, val);
	while (retrain_time < MAX_RETRAIN_TIME_US) {
		usleep_range(100, 200);
		retrain_time += 100;
		val = dw_pcie_read_dbi(pci, exp_cap_offset + PCI_EXP_LNKSTA, 2);
		if (!(val & PCI_EXP_LNKSTA_LT))
			return 0;
	}
	return -ETIMEDOUT;
}

static int google_pcie_start_link(struct dw_pcie *pci)
{
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	u32 val;

	google_pcie_dump_icd(gpcie, "deassert PERST");

	/* reset PHY */
	google_pcie_assert_perst_n(gpcie, false);
	usleep_range(gpcie->perst_delay_us, gpcie->perst_delay_us + 2000);

	google_pcie_dump_icd(gpcie, "enable LTSSM");

	dev_dbg(pci->dev, "enable app_ltssm_en for link training\n");
	val = readl(gpcie->top_base + PCIE_CTL_CFG1);
	val |= LTSSM_ENABLE;
	writel(val, gpcie->top_base + PCIE_CTL_CFG1);

	google_pcie_dump_icd(gpcie, "enable LTSSM done");

	return google_pcie_wait_for_link(pci);
}

static void google_pcie_send_pme_turn_off(struct google_pcie *gpcie)
{
	u32 val, count;

	google_pcie_dump_icd(gpcie, "send_pme_turn_off");

	/* Toggle SII signal to send PME_Turn_Off  */
	val = readl(gpcie->sii_base + PM_UNLOCK_ERR_MSG);
	val |= APPS_PM_XMT_TURNOFF;
	writel(val, gpcie->sii_base + PM_UNLOCK_ERR_MSG);
	val &= ~APPS_PM_XMT_TURNOFF;
	writel(val, gpcie->sii_base + PM_UNLOCK_ERR_MSG);

	for (count = 0; count < MAX_L2_COUNT; count++) {
		usleep_range(L2_PME_TO_ACK_WAIT_US, L2_PME_TO_ACK_WAIT_US + 1);
		val = readl(gpcie->sii_base + PM_UNLOCK_ERR_MSG);

		if (val & RADM_PM_TO_ACK) {
			val = RADM_PM_TO_ACK;
			writel(val, gpcie->sii_base + PM_UNLOCK_ERR_MSG);
			dev_dbg(gpcie->dev, "Received PME_TO_ACK\n");
			google_pcie_dump_icd(gpcie, "Received PME_TO_ACK");
			return;
		}
	}

	dev_err(gpcie->dev, "Timed out waiting for PME_TO_ACK\n");
	google_pcie_dump_icd(gpcie, "Timed out waiting for PME_TO_ACK");

	return;
}

static const struct dw_pcie_ops google_dw_pcie_ops = {
	.link_up = google_pcie_link_up,
	.read_dbi = google_pcie_read_dbi,
	.write_dbi = google_pcie_write_dbi,
};

static struct google_pcie *google_pcie_get_handle(int num)
{
	struct google_pcie *gpcie = NULL;

	guard(spinlock_irqsave)(&gpcie_inst_lock);

	list_for_each_entry(gpcie, &gpcie_inst_list, node) {
		if (gpcie->domain == num)
			return gpcie;
	}

	pr_err("%s: Cannot find PCIe controller for %d", __func__, num);
	return NULL;
}

static u64 power_stats_get_ts(void)
{
	return ktime_to_ms(ktime_get_boottime());
}

static void power_stats_update_up(struct google_pcie *gpcie)
{
	u64 current_ts;
	unsigned long flags;

	spin_lock_irqsave(&gpcie->power_stats_lock, flags);

	current_ts = power_stats_get_ts();

	gpcie->link_up.count++;
	gpcie->link_up.last_entry_ms = current_ts;
	gpcie->link_down.duration += current_ts - gpcie->link_down.last_entry_ms;

	spin_unlock_irqrestore(&gpcie->power_stats_lock, flags);
}

static void power_stats_update_down(struct google_pcie *gpcie)
{
	u64 current_ts;
	unsigned long flags;

	spin_lock_irqsave(&gpcie->power_stats_lock, flags);

	current_ts = power_stats_get_ts();

	gpcie->link_down.count++;
	gpcie->link_down.last_entry_ms = current_ts;
	gpcie->link_up.duration += current_ts - gpcie->link_up.last_entry_ms;

	spin_unlock_irqrestore(&gpcie->power_stats_lock, flags);
}


static void link_duration_update(struct google_pcie *pcie, u32 opcode)
{
	int i;
	int link_speed;
	int last_speed;
	u64 current_ts;
	u64 link_duration_delta;
	unsigned long flags;

	if (opcode >= LINK_DURATION_OPCODE_MAX)
		return;

	spin_lock_irqsave(&pcie->link_duration_lock, flags);

	switch (opcode) {
	case LINK_DURATION_INIT:
		pcie->link_duration_stats.last_link_speed = 0;
		for (i = 0; i < GPCIE_NUM_LINK_SPEEDS; i++) {
			pcie->link_duration_stats.speed[i].count = 0;
			pcie->link_duration_stats.speed[i].duration = 0;
			pcie->link_duration_stats.speed[i].last_entry_ts = 0;
		}
		break;

	case LINK_DURATION_UP:
		link_speed = pcie->current_link_speed;
		pcie->link_duration_stats.last_link_speed = link_speed;

		if (link_speed > GPCIE_NUM_LINK_SPEEDS || link_speed <= 0)
			break;

		current_ts = power_stats_get_ts();

		pcie->link_duration_stats.speed[link_speed - 1].count++;
		pcie->link_duration_stats.speed[link_speed - 1].last_entry_ts =
			current_ts;
		break;

	case LINK_DURATION_DOWN:
		link_speed = pcie->link_duration_stats.last_link_speed;
		pcie->link_duration_stats.last_link_speed = pcie->current_link_speed;

		if (link_speed > GPCIE_NUM_LINK_SPEEDS || link_speed <= 0)
			break;

		current_ts = power_stats_get_ts();
		link_duration_delta = current_ts -
			pcie->link_duration_stats.speed[link_speed - 1].last_entry_ts;

		pcie->link_duration_stats.speed[link_speed - 1].duration +=
			link_duration_delta;
		pcie->link_duration_stats.speed[link_speed - 1].last_entry_ts =
			current_ts;
		break;

	case LINK_DURATION_SPD_CHG:
		link_speed = pcie->current_link_speed;
		last_speed = pcie->link_duration_stats.last_link_speed;

		if (link_speed == last_speed)
			break;

		current_ts = power_stats_get_ts();

		if (last_speed > 0 && last_speed <= GPCIE_NUM_LINK_SPEEDS) {
			link_duration_delta = current_ts -
				pcie->link_duration_stats.speed[last_speed - 1].last_entry_ts;

			pcie->link_duration_stats.speed[last_speed - 1].duration +=
				link_duration_delta;
			pcie->link_duration_stats.speed[last_speed - 1].last_entry_ts =
				current_ts;
		}

		pcie->link_duration_stats.last_link_speed = link_speed;

		if (link_speed > 0 && link_speed <= GPCIE_NUM_LINK_SPEEDS) {
			pcie->link_duration_stats.speed[link_speed - 1].count++;
			pcie->link_duration_stats.speed[link_speed - 1].last_entry_ts =
				current_ts;
		}

		break;
	}

	spin_unlock_irqrestore(&pcie->link_duration_lock, flags);
}

static void link_duration_get(struct google_pcie *pcie,
		struct google_pcie_link_duration_stats *link_duration)
{
	struct google_pcie_link_duration_stats link_duration_copy;
	unsigned long flags;
	int last_speed;
	u64 current_ts;
	u64 link_duration_delta;

	spin_lock_irqsave(&pcie->link_duration_lock, flags);
	link_duration_copy = pcie->link_duration_stats;
	current_ts = power_stats_get_ts();
	spin_unlock_irqrestore(&pcie->link_duration_lock, flags);

	last_speed = link_duration_copy.last_link_speed;
	if (last_speed > 0 && last_speed <= GPCIE_NUM_LINK_SPEEDS) {
		link_duration_delta = current_ts -
			link_duration_copy.speed[last_speed - 1].last_entry_ts;

		link_duration_copy.speed[last_speed - 1].duration +=
			link_duration_delta;
	}

	if (link_duration)
		*link_duration = link_duration_copy;
}

/**
 * google_pcie_get_link_duration - Query link duration by PCIe controller number
 *
 * Query link duration for the given PCI controller
 *
 * @num: The domain number representing the PCIe controller in the system
 * @link_duration: Buffer used to store link duration stats, could be NULL
 * returns 0 on success or negative for error
 */
int google_pcie_get_link_duration(int num,
				struct google_pcie_link_duration_stats *link_duration)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	link_duration_get(gpcie, link_duration);

	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_get_link_duration);

/**
 * google_pcie_get_link_stats - Query link statistic by PCIe controller number
 *
 * Query link statistic for the given PCI controller
 *
 * @num: The domain number representing the PCIe controller in the system
 * @link_stats: Buffer used to store link statistic, could be NULL
 * returns 0 on success or negative for error
 */
int google_pcie_get_link_stats(int num,
				struct google_pcie_link_stats *link_stats)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	if (link_stats)
		*link_stats = gpcie->link_stats;

	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_get_link_stats);

/**
 * google_pcie_dump_debug - Dump debug registers
 *
 * Endpoints can use this when they encounter error conditions that
 * require a dump of PCIe debug registers.
 *
 * @num: The domain number representing the PCIe controller in the system
 */
void google_pcie_dump_debug(int num)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie) {
		pr_err("Invalid PCI handle chan %d\n", num);
		return;
	}

	google_pcie_create_devcoredump(gpcie);
}
EXPORT_SYMBOL_GPL(google_pcie_dump_debug);

static void google_pcie_check_pending_txns(struct google_pcie *gpcie)
{
	int check_time = 0;
	u32 val = 0;

	while (check_time < MAX_PDG_TXN_CHECK_TIME_US) {
		usleep_range(10, 12);
		check_time += 10;
		val = readl(gpcie->sii_base + PCIE_SII_BUS_DBG);
		if (!(val & BRDG_SLV_XFER_PENDING) && !(val & RADM_XFER_PENDING))
			break;
	}
}

/**
 * google_pcie_rc_poweron - Power on the RC and bringup the link with EP
 *
 * This will setup the RC, run the link_up sequence with a few retries and
 * wait for link to be established. When the link is established successfully
 * first time, it also triggers the bus scanning for the bus starting with
 * the RC controller.
 *
 * @num: The domain number representing the PCIe controller in the system
 * returns 0 on success or negative error codes
 */
int google_pcie_rc_poweron(int num)
{
	struct google_pcie *gpcie = NULL;
	struct dw_pcie *pci = NULL;
	struct pci_bus *bus = NULL;
	struct pci_dev *pci_dev = NULL;
	int ret;
	int retry = LINK_WAIT_MAX_RETRIES;
	unsigned long flags;
	bool link_up = false;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	google_pcie_dump_icd(gpcie, "poweron start");

	dev_dbg(gpcie->dev, "running poweron sequence...\n");
	mutex_lock(&gpcie->link_lock);
	if (gpcie->is_link_up) {
		dev_err(gpcie->dev, "Link is up, quit poweron\n");
		mutex_unlock(&gpcie->link_lock);
		return -EINVAL;
	}

	pci = gpcie->pci;
	bus = pci->pp.bridge->bus;
	pci_dev = pci_get_slot(bus, 0);
	if (!pci_dev) {
		dev_err(gpcie->dev, "Could not find the pci_dev");
		mutex_unlock(&gpcie->link_lock);
		return -ENODEV;
	}

	if (gpcie->aux_clk) {
		ret = clk_prepare_enable(gpcie->aux_clk);
		if (ret < 0) {
			dev_err(gpcie->dev, "Failed aux_clk enable: %d\n", ret);
			pci_dev_put(pci_dev);
			mutex_unlock(&gpcie->link_lock);
			return ret;
		}
	}

	ret = pm_runtime_get_sync(gpcie->dev);
	if (ret < 0) {
		dev_err(gpcie->dev, "Failed to power on PCIe\n");
		goto pm_put;
	} else if (ret > 0)
		dev_warn(gpcie->dev, "Runtime status not suspended ret %d\n", ret);

	ret = reset_control_deassert(gpcie->perst_rst);
	if (ret) {
		dev_err(gpcie->dev, "Failed to de-assert PERST ret %d\n", ret);
		goto pm_put;
	}

	gpcie->powered_on = true;

	dev_dbg(gpcie->dev, "Restoring PCIe root port state");
	ret = pci_load_saved_state(pci_dev, gpcie->saved_state);
	if (ret) {
		dev_err(gpcie->dev, "Could not restore PCIe state\n");
		goto pm_put;
	}

	google_pcie_set_l1ss_capabilities(gpcie);
	google_pcie_set_l1_entrance_latency(gpcie);

	pci_restore_state(pci_dev);

	google_pcie_set_link_width_speed(gpcie);
	/* Restore IP's DBI registers - reinitialize instead of restoring */
	dw_pcie_setup_rc(&pci->pp);

	if (gpcie->skip_link_eq)
		google_pcie_disable_equalization(gpcie);

	/* Set posted RX queue credits */
	if (gpcie->posted_rx_q_credits)
		google_pcie_set_posted_rx_q_credits(gpcie,
						    gpcie->posted_rx_q_credits);

	/*
	 * Program the ATU on poweron - either by map_bus on first poweron(),
	 * or manually at the end on subsequent poweron().
	 */
	gpcie->curr_mapped_busdev = 0;

	ret = pinctrl_pm_select_default_state(gpcie->dev);
	if (ret) {
		dev_err(gpcie->dev, "Failed to set CLKREQ func: %d\n", ret);
		goto pm_put;
	}

	while (retry--) {
		if (!link_up)
			ret = google_pcie_start_link(pci);
		else
			ret = google_pcie_retrain_link(pci);

		if (ret) {
			// Stop and attempt to restart the link
			link_up = false;
			gpcie->link_stats.link_up_failure_count++;
			google_pcie_assert_perst_n(gpcie, true);
			google_pcie_disable_ltssm(gpcie);
			usleep_range(gpcie->perst_delay_us, gpcie->perst_delay_us + 2000);
		} else {
			link_up = true;
			google_pcie_fetch_speed_width(pci);

			if (gpcie->current_link_speed == gpcie->target_link_speed)
				break;

			// Retry to get a higher speed
			dev_dbg(gpcie->dev,
				"%s: Link speed (GEN-%d) is less than target speed (GEN-%d)",
				__func__, gpcie->current_link_speed, gpcie->target_link_speed);
		}
	}

	if (!link_up) {
		dev_err(gpcie->dev, "Failed to start link, giving up retries\n");
		gpcie->link_stats.link_recovery_failure_count++;
		ret = -EPIPE;
		google_pcie_create_devcoredump(gpcie);
		goto clkreq_idle;
	}

	if (gpcie->current_link_speed == gpcie->target_link_speed) {
		dev_dbg(gpcie->dev, "Link up at expected rate. Link speed = Gen%d, Target = Gen%d\n",
			gpcie->current_link_speed, gpcie->target_link_speed);
	} else {
		dev_info(gpcie->dev, "Link up at reduced rate. Link speed = Gen%d, Target = Gen%d\n",
			gpcie->current_link_speed, gpcie->target_link_speed);
	}

	/*
	 * We may see spurious completion timeouts while the link was off.
	 * Ensure we've flushed them before we re-set |is_link_up = true|.
	 */
	synchronize_irq(gpcie->cpl_timeout_irq);

	gpcie->is_link_up = true;

	enable_irq(gpcie->link_down_irq);

	google_pcie_config_bw(gpcie);
	ret = google_pcie_set_icc_bw(gpcie, gpcie->avg_bw, gpcie->peak_bw);
	if (ret) {
		dev_err(gpcie->dev, "failed to set bandwidth: (%d)\n", ret);
		goto link_down;
	}

	ret = goog_cpm_clk_toggle_auto_gate(gpcie->phy_fw_clk, true);
	if (ret) {
		dev_err(gpcie->dev,
			"Failed to enable ACG for phy_fw clk\n");
		goto reset_icc_bw;
	}

	power_stats_update_up(gpcie);
	link_duration_update(gpcie, LINK_DURATION_UP);

	if (!gpcie->enumeration_done) {
		dev_dbg(gpcie->dev, "Rescanning bus for devices\n");
		pci_rescan_bus(bus);
		gpcie->enumeration_done = true;
	}

	if (!gpcie->curr_mapped_busdev)
		google_pcie_other_conf_map_bus(pci_find_bus(num, 1), PCI_DEVFN(0, 0), 0);

	dev_dbg(gpcie->dev, "poweron success\n");
	pci_dev_put(pci_dev);
	google_pcie_dump_icd(gpcie, "poweron end");
	device_wakeup_enable(gpcie->dev);
	mutex_unlock(&gpcie->link_lock);

	return 0;

reset_icc_bw:
	google_pcie_set_icc_bw(gpcie, 0, 0);
link_down:
	gpcie->avg_bw = 0;
	gpcie->peak_bw = 0;
	disable_irq(gpcie->link_down_irq);
	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	gpcie->is_link_up = false;
	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
	google_pcie_send_pme_turn_off(gpcie);
	google_pcie_wait_for_state(gpcie, L2);
clkreq_idle:
	pinctrl_pm_select_idle_state(gpcie->dev);
pm_put:
	if (gpcie->aux_clk)
		clk_disable_unprepare(gpcie->aux_clk);
	gpcie->current_link_speed = 0;
	gpcie->current_link_width = 0;
	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	gpcie->powered_on = false;
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
	reset_control_assert(gpcie->perst_rst);
	pm_runtime_put_sync(gpcie->dev);
	pm_runtime_set_suspended(gpcie->dev);
	pci_dev_put(pci_dev);
	mutex_unlock(&gpcie->link_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(google_pcie_rc_poweron);

void google_pcie_hot_reset(struct google_pcie *gpcie)
{
	u32 val;

	val = readl(gpcie->top_base + PCIE_RESET_STATUS);
	val |= APP_INIT_RST;
	writel(val, gpcie->top_base + PCIE_RESET_STATUS);
}

/**
 * google_pcie_rc_poweroff - Disable link between EP and power OFF the RC
 *
 * This initiates the PME handshake to transition the PCIe link between EP
 * to power saving state L2 and turns off the PCIe RC controller.
 *
 * @num: The domain number representing the PCIe controller in the system
 * returns 0 on success or negative error codes
 */
int google_pcie_rc_poweroff(int num)
{
	struct google_pcie *gpcie = NULL;
	struct dw_pcie *pci = NULL;
	struct pci_bus *bus = NULL;
	struct pci_dev *pci_dev = NULL;
	unsigned long flags;
	bool disable_ltssm = false;
	int ret;
	int need_enable_link_down = 0;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	google_pcie_dump_icd(gpcie, "poweroff start");
	dev_dbg(gpcie->dev, "running poweroff sequence...\n");
	mutex_lock(&gpcie->link_lock);

	/* There are 2 types of poweroff() calls from the client:
	 * - Regular poweroff
	 * - Recovery poweroff from cpl_timeout/link_down case
	 * When the recovery poweroff is called, the is_link_up has already been
	 * set to false from the cpl_timeout/link_down IRQ handler. The poweroff
	 * is still required to assert the PERST GPIO and power down PCIe.
	 */
	if (!gpcie->is_link_up && !google_pcie_in_recovery(gpcie)) {
		dev_err(gpcie->dev, "Link is down, quit poweroff\n");
		mutex_unlock(&gpcie->link_lock);
		return -EINVAL;
	}

	pci = gpcie->pci;
	bus = pci->pp.bridge->bus;
	pci_dev = pci_get_slot(bus, 0);
	if (!pci_dev) {
		dev_err(gpcie->dev, "Could not find the pci_dev");
		mutex_unlock(&gpcie->link_lock);
		return -ENODEV;
	}

	dev_dbg(gpcie->dev, "Saving PCIe root port state");
	pci_save_state(pci_dev);
	kfree(gpcie->saved_state);
	gpcie->saved_state = pci_store_saved_state(pci_dev);

	ret = goog_cpm_clk_toggle_auto_gate(gpcie->phy_fw_clk, false);
	if (ret) {
		dev_err(gpcie->dev, "Failed to auto gate phy_fw clk, ret %d\n", ret);
		mutex_unlock(&gpcie->link_lock);
		return ret;
	}

	gpcie->avg_bw = 0;
	gpcie->peak_bw = 0;
	google_pcie_set_icc_bw(gpcie, gpcie->avg_bw, gpcie->peak_bw);

	/*
	 * Disable link_down IRQ to avoid a spurious IRQ when the link will
	 * enter L2.
	 */
	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	gpcie->is_link_up = false;
	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
	disable_irq(gpcie->link_down_irq);

	gpcie->current_link_speed = 0;
	gpcie->current_link_width = 0;
	if (!google_pcie_in_recovery(gpcie)) {
		google_pcie_send_pme_turn_off(gpcie);
		google_pcie_wait_for_state(gpcie, L2);
		/* Give EP some time to wind down after sending L23READY */
		usleep_range(1000, 1200);
		dev_info(gpcie->dev, "Link down\n");
		google_pcie_dump_icd(gpcie, "Link down");
		disable_ltssm = true;
	} else if (test_bit(GPCIE_IN_CPL_TIMEOUT, &gpcie->recovery_flags)) {
		google_pcie_hot_reset(gpcie);
		google_pcie_check_pending_txns(gpcie);
		dev_info(gpcie->dev, "pcie recovery: force hot reset\n");
		google_pcie_dump_icd(gpcie, "force hot reset");
	}

	google_pcie_assert_perst_n(gpcie, true);
	ret = pinctrl_pm_select_idle_state(gpcie->dev);
	if (ret)
		dev_err(gpcie->dev, "Failed to set CLKREQ idle: %d\n", ret);

	google_pcie_check_pending_txns(gpcie);
	google_pcie_dump_icd(gpcie, "perst asserted");

	/*
	 * At this point, the perst reset will be asserted.
	 * Thereby, pcie registers are not accessible.
	 * Therefore, mark pcie power off unconditionally.
	 */
	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	gpcie->powered_on = false;
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

	/*
	 * The link has been brought down, so we are not able to
	 * bring things back if assert fail, so logging the ret
	 * value first.
	 */
	ret = reset_control_assert(gpcie->perst_rst);
	if (ret)
		dev_err(gpcie->dev, "Failed to Assert PERST, ret %d\n", ret);

	if (disable_ltssm)
		google_pcie_disable_ltssm(gpcie);

	power_stats_update_down(gpcie);
	link_duration_update(gpcie, LINK_DURATION_DOWN);
	ret = pm_runtime_put_sync(gpcie->dev);
	/*
	 * We might not be able to bring the link up again here,
	 * So just print the error log to know what went wrong.
	 */
	if (ret < 0)
		dev_err(gpcie->dev, "Failed to poweroff(put) ret %d\n", ret);

	if (gpcie->aux_clk)
		clk_disable_unprepare(gpcie->aux_clk);

	dev_dbg(gpcie->dev, "poweroff success\n");
	google_pcie_dump_icd(gpcie, "poweroff end");
	pci_dev_put(pci_dev);
	device_wakeup_disable(gpcie->dev);

	/*
	 * It is possible that the error event IRQ gets fired when the regular
	 * poweroff is in progress. Since the regular poweroff will take care
	 * of the PERST assertion and PCIe power-down, mark these flags to false
	 * so that the subsequent recovery poweroff bails out.
	 *
	 * When clearing these flags, balance out any IRQ-disable references we
	 * made.
	 */
	/*
	 * link_up_lock must be held to serialize against the top-half IRQ
	 * handlers. This ensures that the handler's disable_irq_nosync()
	 * call completes before we evaluate the flags and subsequently
	 * call enable_irq(), preventing unbalanced IRQ depth warnings.
	 */
	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	if (test_and_clear_bit(GPCIE_IN_LINK_DOWN, &gpcie->recovery_flags))
		need_enable_link_down++;
	if (test_and_clear_bit(GPCIE_IN_CPL_TIMEOUT, &gpcie->recovery_flags))
		need_enable_link_down++;
	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
	while (need_enable_link_down > 0) {
		enable_irq(gpcie->link_down_irq);
		need_enable_link_down--;
	}

	mutex_unlock(&gpcie->link_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_rc_poweroff);

/**
 * google_pcie_rc_prepare_for_forced_poweroff - Prepares for a forced power-off on link failure.
 *
 * Called by an endpoint driver when the link has failed and a graceful shutdown is not
 * possible. It sets is_link_up to false, which guards the endpoint's configuration
 * space from further access. It also ensures that the next call to google_pcie_rc_poweroff()
 * bypasses the standard teardown, proceeding directly to power down the controller.
 *
 * @num: The domain number representing the PCIe controller in the system.
 */
void google_pcie_rc_prepare_for_forced_poweroff(int num)
{
	struct google_pcie *gpcie;
	unsigned long flags;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie) {
		pr_err("Invalid PCI handle chan %d\n", num);
		return;
	}

	spin_lock_irqsave(&gpcie->link_up_lock, flags);

	if (!gpcie->is_link_up) {
		dev_info(gpcie->dev, "is_link_up is already false\n");
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return;
	}

	if (google_pcie_in_recovery(gpcie)) {
		dev_info(gpcie->dev, "already in recovery\n");
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return;
	}

	dev_info(gpcie->dev, "Preparing for forced power-off due to reported link failure\n");

	gpcie->is_link_up = false;

	set_bit(GPCIE_IN_LINK_DOWN, &gpcie->recovery_flags);
	/*
	 * Avoid spurious link-down IRQs during error handling. We undo this
	 * step when exiting the recovery state.
	 */
	disable_irq_nosync(gpcie->link_down_irq);

	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
}
EXPORT_SYMBOL_GPL(google_pcie_rc_prepare_for_forced_poweroff);

/**
 * google_pcie_link_state - Query link state by PCIe controller number
 *
 * Query link state for the given PCI controller
 *
 * @num: The domain number representing the PCIe controller in the system
 * returns value of enum link_states on success or negative for error
 */
int google_pcie_link_state(int num)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	return google_pcie_get_link_state(gpcie);
}
EXPORT_SYMBOL_GPL(google_pcie_link_state);

static bool google_pcie_is_link_in_l0(struct google_pcie *gpcie)
{
	u32 link_state = UNKNOWN;

	link_state = google_pcie_get_link_state(gpcie);
	return (link_state == L0);
}

/**
 * google_pcie_get_max_link_speed - Get max link speed
 *
 * This will get the max speed that link is capable of for the specified PCIe
 * controller.  The max speed is the lower of the RC max speed capability and
 * the max-speed configured in the device tree.
 *
 * @num: The domain number representing the PCIe controller in the system.
 * returns maximum link speed on success or negative error codes.
 */
int google_pcie_get_max_link_speed(int num)
{
	struct google_pcie *gpcie = google_pcie_get_handle(num);

	if (!gpcie)
		return -EINVAL;

	return gpcie->max_link_speed;
}
EXPORT_SYMBOL_GPL(google_pcie_get_max_link_speed);

/**
 * google_pcie_get_max_link_width - Get max link width
 *
 * This will get the max width that link is capable of for the specified PCIe
 * controller.  The max width is the lower of the RC max width capability and
 * the max-width configured in the device tree.
 *
 * @num: The domain number representing the PCIe controller in the system.
 * returns maximum link width on success or negative error codes.
 */
int google_pcie_get_max_link_width(int num)
{
	struct google_pcie *gpcie = google_pcie_get_handle(num);

	if (!gpcie)
		return -EINVAL;

	return gpcie->max_link_width;
}
EXPORT_SYMBOL_GPL(google_pcie_get_max_link_width);

/**
 * google_pcie_rc_change_link_speed - Change pcie link speed.
 *
 * This will set the link speed to desired values when link is up in L0.
 * If link is not up, the desired link speed change will be in effect from next link up.
 * This function must not be called when Link is up but not in L0 state.
 *
 * This function will fail if requested speed is more than the speed at link up.
 * For ex. if link up was done in Gen3x1, then speed change to Gen4 is prevented
 * by hardware and failure is returned. In this case, gen3->gen2->gen1->gen3 etc.
 * transitions by this function call will be successful.
 *
 * @num: The domain number representing the PCIe controller in the system.
 * @speed: The desired speed to be set for PCIe link.
 * returns 0 on success or negative error codes.
 */
int google_pcie_rc_change_link_speed(int num, unsigned int speed)
{
	struct google_pcie *gpcie = NULL;
	struct dw_pcie *pci = NULL;
	int i = 0;
	u32 val = 0;
	u16 linkstat = 0;
	u32 old_speed = 0;
	u32 new_speed = 0;
	u8 exp_cap_offset = 0;
	int ret = 0;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	if (speed < 1 || speed > gpcie->max_link_speed) {
		dev_err(gpcie->dev, "Invalid speed %d. Valid range: [1-%d].\n",
			speed, gpcie->max_link_speed);
		return -EINVAL;
	}

	if (speed == gpcie->current_link_speed)
		return 0;

	mutex_lock(&gpcie->link_lock);
	old_speed = gpcie->target_link_speed;
	gpcie->target_link_speed = speed;
	if (!gpcie->is_link_up) {
		dev_info(gpcie->dev, "%s: Link speed changed to %d from next link up\n",
			 __func__, speed);
		ret = 0;
		goto unlock;
	}

	if (!google_pcie_is_link_in_l0(gpcie)) {
		dev_err(gpcie->dev, "%s: Link is not in L0.\n", __func__);
		gpcie->target_link_speed = old_speed;
		ret = -EINVAL;
		goto unlock;
	}

	pci = gpcie->pci;

	/* 1. modify link speed: LINK_CONTROL2_LINK_STATUS2_REG -> PCIE_CAP_TARGET_LINK_SPEED */
	exp_cap_offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	val = dw_pcie_read_dbi(pci, exp_cap_offset + PCI_EXP_LNKCTL2, 2);
	val &= ~PCI_EXP_LNKCTL2_TLS;
	val |= speed;
	dw_pcie_write_dbi(pci, exp_cap_offset + PCI_EXP_LNKCTL2, 2, val);

	/* 2. Deassert:  GEN2_CTRL_OFF -> DIRECT_SPEED_CHANGE */
	val = dw_pcie_read_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, 4);
	val &= ~PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_write_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, val);

	/* 3. Assert:  GEN2_CTRL_OFF -> DIRECT_SPEED_CHANGE */
	val = dw_pcie_read_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, 4);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_write_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, val);

	/* Check the target link speed against current link speed */
	for (i = 0; i < MAX_TIMEOUT_WIDTH_SPEED_CHANGE; i++) {
		linkstat = dw_pcie_read_dbi(pci, exp_cap_offset + PCI_EXP_LNKSTA, 2);
		new_speed = linkstat & PCI_EXP_LNKSTA_CLS;

		if (new_speed == speed)
			break;

		udelay(WIDTH_SPEED_CHANGE_WAIT_US);
	}

	if (new_speed != speed) {
		/*
		 * Change didn't happen. Most probable reason of failure:
		 * requested value is more than the value at link up
		 */
		dev_err(gpcie->dev, "%s: Fail: target speed: %d current speed: %d\n",
			__func__, speed, gpcie->current_link_speed);
		gpcie->target_link_speed = old_speed;
		ret = -EINVAL;
		goto unlock;
	}

	google_pcie_wait_for_state(gpcie, L0);

	gpcie->current_link_speed = speed;
	link_duration_update(gpcie, LINK_DURATION_SPD_CHG);
	mutex_unlock(&gpcie->link_lock);
	dev_dbg(gpcie->dev, "%s: link speed changed to %d\n", __func__, speed);

	google_pcie_config_bw(gpcie);
	google_pcie_set_icc_bw(gpcie, gpcie->avg_bw, gpcie->peak_bw);

	return 0;
unlock:
	mutex_unlock(&gpcie->link_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(google_pcie_rc_change_link_speed);

/**
 * google_pcie_rc_change_link_width - Change pcie link width.
 *
 * This will set the link width to desired values when link is up in L0.
 * If link is not up, the desired link width change will be in effect from next link up.
 * This function must not be called when Link is up but not in L0 state.
 *
 * This function will fail if requested width is more than the width at link up.
 * For ex. if link up was done in Gen4x1, then width change to Gen4x2 is prevented
 * by hardware and failure is returned.
 *
 * @num: The domain number representing the PCIe controller in the system
 * @width: The desired width to be set for PCIe link
 * returns 0 on success or negative error codes
 */
int google_pcie_rc_change_link_width(int num, unsigned int width)
{
	struct google_pcie *gpcie = NULL;
	struct dw_pcie *pci = NULL;
	int i = 0;
	u32 val = 0;
	int ret = 0;
	u16 linkstat = 0;
	u32 old_width = 0;
	u32 new_width = 0;
	u8 exp_cap_offset = 0;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	if (width < 1 || width > gpcie->max_link_width) {
		dev_err(gpcie->dev, "Invalid width %d. Valid range: [1-%d].\n",
			width, gpcie->max_link_width);
		return -EINVAL;
	}

	if (width == gpcie->current_link_width)
		return 0;

	mutex_lock(&gpcie->link_lock);
	old_width = gpcie->target_link_width;
	gpcie->target_link_width = width;
	if (!gpcie->is_link_up) {
		dev_info(gpcie->dev, "%s: Link width changed to %d from next link up\n",
			 __func__, width);
		ret = 0;
		goto unlock;
	}

	if (!google_pcie_is_link_in_l0(gpcie)) {
		dev_err(gpcie->dev, "%s: Link is not in L0.\n", __func__);
		ret = -EINVAL;
		gpcie->target_link_width = old_width;
		goto unlock;
	}

	pci = gpcie->pci;

	exp_cap_offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);

	/* 1. Program the PCIE_PORT_MULTI_LANE_CTRL -> TARGET_LINK_WIDTH */
	val = dw_pcie_read_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, 4);
	val &= ~PORT_TARGET_LINK_WIDTH_MASK;
	val |= width;
	dw_pcie_write_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, 4, val);

	/* 2. Deassert the PCIE_PORT_MULTI_LANE_CTRL -> DIRECT_LINK_WIDTH_CHANGE */
	val = dw_pcie_read_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, 4);
	val &= ~PORT_DIRECT_LINK_WIDTH_CHANGE;
	dw_pcie_write_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, 4, val);

	/* 3. Assert the PCIE_PORT_MULTI_LANE_CTRL -> DIRECT_LINK_WIDTH_CHANGE */
	val = dw_pcie_read_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, 4);
	val |= PORT_DIRECT_LINK_WIDTH_CHANGE;
	dw_pcie_write_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, 4, val);

	/* Check the negotiated link width */
	for (i = 0; i < MAX_TIMEOUT_WIDTH_SPEED_CHANGE; i++) {
		linkstat = dw_pcie_read_dbi(pci, exp_cap_offset + PCI_EXP_LNKSTA, 2);
		new_width = (linkstat & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;

		if (new_width == width)
			break;

		udelay(WIDTH_SPEED_CHANGE_WAIT_US);
	}

	if (new_width != width) {
		/*
		 * Change didn't happen. Most probable reason of failure:
		 * requested value is more than the value at link up
		 */
		dev_err(gpcie->dev, "%s: Fail: target width: %d current width: %d\n",
			__func__, width, gpcie->current_link_width);
		gpcie->target_link_width = old_width;
		ret = -EINVAL;
		goto unlock;
	}

	google_pcie_wait_for_state(gpcie, L0);

	gpcie->current_link_width = width;
	mutex_unlock(&gpcie->link_lock);
	dev_dbg(gpcie->dev, "%s: link width changed to %d\n", __func__, width);

	google_pcie_config_bw(gpcie);
	google_pcie_set_icc_bw(gpcie, gpcie->avg_bw, gpcie->peak_bw);

	return 0;
unlock:
	mutex_unlock(&gpcie->link_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(google_pcie_rc_change_link_width);

/**
 * google_pcie_poweron_withspeed - Power on RC with certain speed
 *
 * This will set the target link speed as specified in @speed argument,
 * and then tries to bringup PCIe link with that speed.
 *
 * @num: The domain number representing the PCIe controller in the system
 * @speed: The link speed to be used for link-up
 * returns 0 on success or negative error codes
 */
int google_pcie_poweron_withspeed(int num, unsigned int speed)
{
	struct google_pcie *gpcie = NULL;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	if (speed < 1 || speed > gpcie->max_link_speed) {
		dev_err(gpcie->dev, "Invalid max_link_speed %d. Should be in range [1-%d]\n",
			speed, gpcie->max_link_speed);
		return -EINVAL;
	}

	gpcie->target_link_speed = speed;
	return google_pcie_rc_poweron(num);
}
EXPORT_SYMBOL_GPL(google_pcie_poweron_withspeed);

/**
 * google_pcie_register_callback - Register a callback where events will be sent
 *
 * This will register a callback function that will be invoked upon various
 * events like completion timeout, link_down, etc.
 *
 * @num: The domain number representing the PCIe controller in the system
 * @cb_func: Pointer to the callback function which should be registered
 * @priv: Private data that will be passed as-is when the callback function
 *    is invoked
 * returns 0 on success or negative error codes
 */
int google_pcie_register_callback(int num, google_pcie_callback_func cb_func,
				  void *priv)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	gpcie->cb_func = cb_func;
	gpcie->cb_priv = priv;
	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_register_callback);

/**
 * google_pcie_unregister_callback - Disable callback registration
 *
 * This will unregister the callback function and stop invoking any previously
 * registered callback functions.
 *
 * @num: The domain number representing the PCIe controller in the system
 * returns 0 on success or negative error codes
 */
int google_pcie_unregister_callback(int num)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	gpcie->cb_func =  NULL;
	gpcie->cb_priv = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_unregister_callback);

/**
 * google_pcie_link_status - Query link status by PCIe controller number
 *
 * Query link status for the given PCI controller
 *
 * @num: The domain number representing the PCIe controller in the system
 * returns 1 for link up; 0 for link down or negative for error
 */
int google_pcie_link_status(int num)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie) {
		pr_err("Invalid PCI handle for link status on chan %d\n", num);
		return -EINVAL;
	}
	return ((google_pcie_get_link_state(gpcie)) < L2);
}
EXPORT_SYMBOL_GPL(google_pcie_link_status);


static irqreturn_t google_pcie_cpl_timeout_thread(int irq, void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	dev_info(gpcie->dev, "Starting cpl_timeout work\n");
	google_pcie_dump_icd(gpcie, "cpl_timeout work");

	google_pcie_create_devcoredump(gpcie);

	if (gpcie->cb_func)
		gpcie->cb_func(GPCIE_CB_CPL_TIMEOUT, gpcie->cb_priv);

	return IRQ_HANDLED;
}

static irqreturn_t google_pcie_cpl_timeout_handler(int irq, void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;
	struct dw_pcie *pci = gpcie->pci;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	if (!gpcie->is_link_up) {
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return IRQ_NONE;
	}

	val = dw_pcie_readl_dbi(pci, gpcie->aer + PCI_ERR_UNCOR_STATUS);
	if (!(val & PCI_ERR_UNC_COMP_TIME)) {
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return IRQ_NONE;
	}

	gpcie->link_stats.complete_timeout_irq_count++;

	/* Avoid concurrent error handling */
	if (google_pcie_in_recovery(gpcie)) {
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return IRQ_HANDLED;
	}

	/*
	 * Set is_link_up to false, while the link is in an errored state.
	 * it will be set to true when the EP's recovery handler calls poweron()
	 */
	gpcie->is_link_up = false;
	set_bit(GPCIE_IN_CPL_TIMEOUT, &gpcie->recovery_flags);
	/*
	 * Avoid spurious link-down IRQs during error handling. We undo this
	 * step when exiting the recovery state.
	 */
	disable_irq_nosync(gpcie->link_down_irq);

	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t google_pcie_link_down_thread(int irq, void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	dev_info(gpcie->dev, "Starting link_down thread\n");
	google_pcie_dump_icd(gpcie, "link_down_thread");

	google_pcie_create_devcoredump(gpcie);

	if (gpcie->cb_func)
		gpcie->cb_func(GPCIE_CB_LINK_DOWN, gpcie->cb_priv);

	return IRQ_HANDLED;
}

static irqreturn_t google_pcie_link_down_handler(int irq, void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;
	u32 val = 0;
	unsigned long flags;

	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	/* If the link isn't up, this is spurious. */
	if (!gpcie->is_link_up) {
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return IRQ_NONE;
	}

	/* If it's residual interrupt, and link is up, return */
	val = readl(gpcie->sii_base + LINK_RST_STATUS);
	if (val & SMLH_REQ_RST_NOT) {
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return IRQ_NONE;
	}

	dev_info(gpcie->dev, "Starting link_down handler\n");

	gpcie->link_stats.link_down_irq_count++;

	/* Avoid concurrent error handling */
	if (google_pcie_in_recovery(gpcie)) {
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		return IRQ_HANDLED;
	}

	/*
	 * Set is_link_up to false, while the link is in an errored state.
	 * it will be set to true when the EP's recovery handler calls poweron()
	 */
	gpcie->is_link_up = false;
	set_bit(GPCIE_IN_LINK_DOWN, &gpcie->recovery_flags);
	/*
	 * Avoid spurious link-down IRQs during error handling. We undo this
	 * step when exiting the recovery state.
	 */
	disable_irq_nosync(gpcie->link_down_irq);

	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);

	return IRQ_WAKE_THREAD;
}

/**
 * google_pcie_is_link_down - Check for link down error
 *
 * Endpoints can use this to check if pcie is in a link down state.
 *
 * @num: The domain number representing the PCIe controller in the system
 * returns true if pcie is in link down state.
 */
bool google_pcie_is_link_down(int num)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie) {
		pr_err("Invalid PCI handle chan %d\n", num);
		return false;
	}
	return test_bit(GPCIE_IN_LINK_DOWN, &gpcie->recovery_flags);
}
EXPORT_SYMBOL_GPL(google_pcie_is_link_down);

/**
 * google_pcie_is_cpl_timeout - Check for cpl timeouterror
 *
 * Endpoints can use this to check if pcie is in a cpl timeout state.
 *
 * @num: The domain number representing the PCIe controller in the system
 * returns true if pcie is in cpl timeout state.
 */
bool google_pcie_is_cpl_timeout(int num)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie) {
		pr_err("Invalid PCI handle chan %d\n", num);
		return false;
	}
	return test_bit(GPCIE_IN_CPL_TIMEOUT, &gpcie->recovery_flags);
}
EXPORT_SYMBOL_GPL(google_pcie_is_cpl_timeout);

static ssize_t link_state_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	u32 link_state = UNKNOWN;
	int ret;

	link_state = google_pcie_get_link_state(gpcie);
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", link_state);

	return ret;
}

static void power_stats_init(struct google_pcie *gpcie)
{
	gpcie->link_up.count = 0;
	gpcie->link_up.duration = 0;
	gpcie->link_up.last_entry_ms = 0;
	gpcie->link_down.count = 1;  // since system starts with link_down
	gpcie->link_down.duration = 0;
	gpcie->link_down.last_entry_ms = 0;
}

static void power_stats_get(struct google_pcie *gpcie,
				struct google_pcie_power_stats *link_up,
				struct google_pcie_power_stats *link_down)
{
	struct google_pcie_power_stats link_up_copy;
	struct google_pcie_power_stats link_down_copy;
	unsigned long flags;
	u64 current_ts;

	spin_lock_irqsave(&gpcie->power_stats_lock, flags);
	link_up_copy = gpcie->link_up;
	link_down_copy = gpcie->link_down;
	spin_unlock_irqrestore(&gpcie->power_stats_lock, flags);

	current_ts = power_stats_get_ts();

	if (gpcie->is_link_up)
		link_up_copy.duration += current_ts - link_up_copy.last_entry_ms;
	else
		link_down_copy.duration += current_ts - link_down_copy.last_entry_ms;

	if (link_up)
		*link_up = link_up_copy;

	if (link_down)
		*link_down = link_down_copy;
}

/**
 * google_pcie_get_power_stats - Query power stats by PCIe controller number
 *
 * Query power stats for the given PCI controller
 *
 * @num: The domain number representing the PCIe controller in the system
 * @link_up: Buffer used to store link up stats, could be NULL
 * @link_down: Buffer used to store link down stats, could be NULL
 * returns 0 on success or negative for error
 */
int google_pcie_get_power_stats(int num,
				struct google_pcie_power_stats *link_up,
				struct google_pcie_power_stats *link_down)
{
	struct google_pcie *gpcie;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	power_stats_get(gpcie, link_up, link_down);

	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_get_power_stats);

static ssize_t power_stats_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	struct google_pcie_power_stats link_up_copy;
	struct google_pcie_power_stats link_down_copy;
	int ret;

	power_stats_get(gpcie, &link_up_copy, &link_down_copy);

	ret = scnprintf(buf, PAGE_SIZE, "Version: 1\n");

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			 "Link up:\n  Cumulative count: 0x%llx\n"
			 "  Cumulative duration msec: 0x%llx\n"
			 "  Last entry timestamp msec: 0x%llx\n",
			 link_up_copy.count,
			 link_up_copy.duration,
			 link_up_copy.last_entry_ms);

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			 "Link down:\n  Cumulative count: 0x%llx\n"
			 "  Cumulative duration msec: 0x%llx\n"
			 "  Last entry timestamp msec: 0x%llx\n",
			 link_down_copy.count,
			 link_down_copy.duration,
			 link_down_copy.last_entry_ms);

	return ret;
}

static ssize_t link_duration_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int i;
	int ret = 0;
	struct google_pcie_link_duration_stats link_duration_copy;
	struct google_pcie *pcie = dev_get_drvdata(dev);

	link_duration_get(pcie, &link_duration_copy);

	ret += sysfs_emit(buf, "link_speed:\n  GEN%d\n",
		link_duration_copy.last_link_speed);

	for (i = 0; i < GPCIE_NUM_LINK_SPEEDS; i++) {
		if (link_duration_copy.speed[i].count) {
			ret += sysfs_emit_at(buf, ret,
				"Gen%d:\n    count: %#llx\n    duration msec: %#llx\n",
				i + 1,
				link_duration_copy.speed[i].count,
				link_duration_copy.speed[i].duration);
		}
	}

	return ret;
}

DEVICE_ATTR_RO(link_state);
DEVICE_ATTR_RO(power_stats);
DEVICE_ATTR_RO(link_duration);

static void gpcie_reset_acg(void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	goog_cpm_clk_toggle_auto_gate(gpcie->phy_fw_clk, false);
}

static void gpcie_reset_control_assert(void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	reset_control_assert(gpcie->init_rst);
}

static void gpcie_mutex_destroy(void *data)
{
	mutex_destroy(data);
}

static void gpcie_list_del(void *data)
{
	list_del(data);
}

static void google_pcie_init_speed_width(struct google_pcie *gpcie, struct pci_dev *pci_dev)
{
	enum pci_bus_speed max_speed = PCI_SPEED_UNKNOWN;
	u8 supported_max_speed = 0;
	u8 supported_max_width = 0;

	/* populate max speed and width from min of capabilities and dt */
	max_speed = pcie_get_speed_cap(pci_dev);

	/* convert from pci_bus_speed enum to gen#  */
	supported_max_speed = (u8) (max_speed - PCIE_SPEED_2_5GT + 1);
	supported_max_width = (u8) pcie_get_width_cap(pci_dev);

	/*
	 * If "max-link-speed" DT property (max_link_speed) is set to some value,
	 * set to minimum of max supported by HW and DT property.
	 * If any invalid value is passed in DT, set it to max HW supported.
	 */
	gpcie->max_link_speed = (gpcie->pci->max_link_speed > 0) ?
				 min((u8) gpcie->pci->max_link_speed, supported_max_speed) :
				 supported_max_speed;

	/* Do the same for max width. DT property for max width is: "num-lanes" */
	gpcie->max_link_width = (gpcie->pci->num_lanes > 0) ?
				 min((u8) gpcie->pci->num_lanes, supported_max_width) :
				 supported_max_width;

	dev_dbg(gpcie->dev, "%s: max link speed:%d, max link width:%d\n",
		__func__, gpcie->max_link_speed, gpcie->max_link_width);

	/* Initialize target_link* as max values */
	gpcie->target_link_speed = gpcie->max_link_speed;
	gpcie->target_link_width = gpcie->max_link_width;
}

static void google_pcie_assert_resets(struct google_pcie *gpcie)
{
	int ret;

	dev_dbg(gpcie->dev, "asserting other resets\n");
	google_pcie_dump_icd(gpcie, "assert_resets enter");
	ret = reset_control_assert(gpcie->pwr_up_rst);
	if (ret)
		dev_err(gpcie->dev, "Failed to assert pwr_up_rst, ret %d\n", ret);
	ret = reset_control_assert(gpcie->init_rst);
	if (ret)
		dev_err(gpcie->dev, "Failed to assert init_rst, ret %d\n", ret);
	ret = reset_control_assert(gpcie->app_hold_phy_rst);
	if (ret)
		dev_err(gpcie->dev, "Failed to assert app_hold_phy_rst, ret %d\n", ret);
	google_pcie_dump_icd(gpcie, "assert_resets end");
}

static int google_pcie_top_notifier(struct notifier_block *nb, unsigned long action, void *dat)
{
	struct google_pcie *gpcie = container_of(nb, struct google_pcie, top_nb);

	switch (action) {
	case GENPD_NOTIFY_OFF:
		dev_dbg(gpcie->dev, "pcie_top off\n");
		google_pcie_assert_resets(gpcie);
		google_pcie_dump_icd(gpcie, "pcie_top off");
		break;
	case GENPD_NOTIFY_ON:
		dev_dbg(gpcie->dev, "pcie_top on\n");
		google_pcie_dump_icd(gpcie, "pcie_top on");
		break;
	default:
		break;
	}

	return 0;
}

static void google_pcie_init_genpd(struct google_pcie *gpcie)
{
	struct device *dev = gpcie->dev;
	struct generic_pm_domain *genpd = pd_to_genpd(dev->pm_domain);

	if (genpd->flags & GENPD_FLAG_RPM_ALWAYS_ON)
		return;

	gpcie->top_nb.notifier_call = google_pcie_top_notifier;
	dev_pm_genpd_add_notifier(dev, &gpcie->top_nb);
}

static int google_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct google_pcie *gpcie;
	struct dw_pcie *pci;
	struct pci_bus *bus = NULL;
	struct pci_dev *pci_dev = NULL;
	struct resource *phy_sram_res;
	struct resource *sii_res;
	struct resource *top_res;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	struct dw_pcie_rp *pp;
#else
	struct pcie_port *pp;
#endif
	int ret;
	u32 l1_entrance_latency_us;
	struct of_phandle_args pd_args, additional_parent_pd_args;

	gpcie = devm_kzalloc(dev, sizeof(*gpcie), GFP_KERNEL);
	if (!gpcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	top_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "top");
	if (!top_res)
		return -EINVAL;

	gpcie->top_base = devm_ioremap_resource(dev, top_res);
	if (IS_ERR(gpcie->top_base))
		return PTR_ERR(gpcie->top_base);

	gpcie->top_size = resource_size(top_res);

	phy_sram_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_sram");
	if (phy_sram_res) {
		gpcie->phy_sram_base = devm_ioremap_resource(dev, phy_sram_res);
		if (IS_ERR(gpcie->phy_sram_base))
			return PTR_ERR(gpcie->phy_sram_base);

		gpcie->phy_sram_size = resource_size(phy_sram_res);
	}

	sii_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sii");
	if (!sii_res)
		return -EINVAL;

	gpcie->sii_base = devm_ioremap_resource(dev, sii_res);
	if (IS_ERR(gpcie->sii_base))
		return PTR_ERR(gpcie->sii_base);

	gpcie->sii_size = resource_size(sii_res);

	pci->dev = dev;
	pci->ops = &google_dw_pcie_ops;
	pci->pp.ops = &google_pcie_host_ops;
	gpcie->dev = dev;
	gpcie->pci = pci;
	gpcie->enumeration_done = false;

	if (of_property_read_u32(np, "google,perst-delay-us", &gpcie->perst_delay_us)) {
		gpcie->perst_delay_us = DEFAULT_PERST_DELAY_US;
		dev_info(dev, "PERST delay is NOT defined...default to %u ms\n",
			 gpcie->perst_delay_us / 1000);
	}

	if (of_property_read_u32(np, "aspm-l1-entry-delay-ns", &gpcie->l1_entrance_latency)) {
		gpcie->l1_entrance_latency = U32_MAX;
		dev_dbg(dev, "l1 entrance latency is NOT defined...default to MAX\n");
	} else {
		l1_entrance_latency_us = (gpcie->l1_entrance_latency / NSEC_PER_USEC);
		gpcie->l1_entrance_latency = order_base_2(l1_entrance_latency_us);
	}

	platform_set_drvdata(pdev, gpcie);

	gpcie->perstn_gpio = devm_gpiod_get(dev, "perstn", GPIOD_OUT_HIGH);
	if (IS_ERR(gpcie->perstn_gpio)) {
		dev_err(dev, "Failed to request perstn GPIO\n");
		return -EINVAL;
	}

	scnprintf(gpcie->cpl_timeout_irqname, sizeof(gpcie->cpl_timeout_irqname),
		  "%s_cpl_timeout", dev_name(dev));
	gpcie->cpl_timeout_irq = platform_get_irq_byname(pdev, "cpl_timeout");
	ret = devm_request_threaded_irq(dev, gpcie->cpl_timeout_irq,
					google_pcie_cpl_timeout_handler,
					google_pcie_cpl_timeout_thread,
					IRQF_ONESHOT,
					gpcie->cpl_timeout_irqname, gpcie);
	if (ret) {
		dev_err(dev, "Failed to request cpl_timeout interrupt: %d\n", ret);
		return ret;
	}

	scnprintf(gpcie->link_down_irqname, sizeof(gpcie->link_down_irqname),
		  "%s_link_down", dev_name(dev));
	gpcie->link_down_irq = platform_get_irq_byname(pdev, "link_down");
	irq_set_status_flags(gpcie->link_down_irq, IRQ_DISABLE_UNLAZY);
	ret = devm_request_threaded_irq(dev, gpcie->link_down_irq,
					google_pcie_link_down_handler,
					google_pcie_link_down_thread,
					IRQF_TRIGGER_RISING | IRQF_NO_AUTOEN | IRQF_ONESHOT,
					gpcie->link_down_irqname, gpcie);
	if (ret) {
		dev_err(dev, "Failed to request link_down interrupt: %d\n", ret);
		return ret;
	}

	gpcie->phy_fw_clk = devm_clk_get(dev, "phy_fw");
	if (IS_ERR(gpcie->phy_fw_clk)) {
		dev_err(dev, "Failed to get phy_fw clk\n");
		return PTR_ERR(gpcie->phy_fw_clk);
	}

	gpcie->aux_clk = devm_clk_get(dev, "aux_clk");
	if (IS_ERR(gpcie->aux_clk)) {
		dev_warn(dev, "failed to acquire hsios_aux_clk (%ld)\n",
			 PTR_ERR(gpcie->aux_clk));
		gpcie->aux_clk = NULL;
	}

	ret = devm_add_action_or_reset(dev, gpcie_reset_acg, gpcie);
	if (ret)
		return ret;

	gpcie->init_rst = devm_reset_control_get(dev, "init");
	if (IS_ERR(gpcie->init_rst))
		return PTR_ERR(gpcie->init_rst);

	gpcie->pwr_up_rst = devm_reset_control_get(dev, "power_up");
	if (IS_ERR(gpcie->pwr_up_rst))
		return PTR_ERR(gpcie->pwr_up_rst);

	gpcie->perst_rst = devm_reset_control_get(dev, "perst");
	if (IS_ERR(gpcie->perst_rst))
		return PTR_ERR(gpcie->perst_rst);

	gpcie->app_hold_phy_rst = devm_reset_control_get(dev, "app_hold_phy");
	if (IS_ERR(gpcie->app_hold_phy_rst))
		return PTR_ERR(gpcie->app_hold_phy_rst);

	ret = devm_add_action_or_reset(dev, gpcie_reset_control_assert, gpcie);
	if (ret)
		return ret;

	gpcie->l1_pwrgate_disable = device_property_read_bool(dev, "google,l1-pwrgate-disable");

	gpcie->skip_link_eq = device_property_read_bool(dev, "google,skip-link-equalization");

	gpcie->icc_path = google_devm_of_icc_get(dev, "sswrp-pcie");
	if (IS_ERR(gpcie->icc_path)) {
		dev_err(dev, "devm_of_icc_get(sswrp-pcie) failed\n");
		return PTR_ERR(gpcie->icc_path);
	}

	google_pcie_init_genpd(gpcie);

	additional_parent_pd_args.args_count = 0;
	additional_parent_pd_args.np = of_parse_phandle(np, "additional-parent-domain", 0);
	if (additional_parent_pd_args.np) {
		ret = of_parse_phandle_with_args(np, "power-domains", "#power-domain-cells", 0,
				&pd_args);
		if (ret) {
			dev_err(dev, "Unable to parse the power domain\n");
			of_node_put(additional_parent_pd_args.np);
			goto remove_genpd;
		}

		ret = of_genpd_add_subdomain(&additional_parent_pd_args, &pd_args);
		if (ret) {
			dev_err(dev, "Failed to add subdomain to additional parent %s\n",
					additional_parent_pd_args.np->name);
			of_node_put(additional_parent_pd_args.np);
			of_node_put(pd_args.np);
			goto remove_genpd;
		}
		of_node_put(additional_parent_pd_args.np);
		of_node_put(pd_args.np);
	}

	gpcie->allow_suspend_in_linkup =
		device_property_read_bool(dev, "google,allow-suspend-in-linkup");

	if (gpcie->allow_suspend_in_linkup)
		device_set_wakeup_capable(dev, true);

	if (device_property_read_u32(dev, "google,posted-rx-q-data-credits",
				     &gpcie->posted_rx_q_credits))
		gpcie->posted_rx_q_credits = 0;

	if (gpcie->phy_sram_base) {
		ret = request_firmware(&gpcie->phy_fw, "laguna_pcie_phy_fw.bin", gpcie->dev);
		if (ret < 0) {
			dev_err(gpcie->dev, "Failed to get PHY firmware: %d\n", ret);
			goto wakeup_disable;
		}
	}

	/*
	 * TODO: we currently rely on the fact that the controller device can
	 * runtime suspend even while its children aren't suspended. Remove
	 * this if/when we're ready to respect our descendants' runtime status.
	 */
	pm_suspend_ignore_children(dev, true);

	ret = pinctrl_pm_select_idle_state(dev);
	if (ret)
		dev_err(gpcie->dev, "Failed to set CLKREQ idle: %d\n", ret);

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to power on PCIe\n");
		goto pm_disable;
	}

	of_property_read_u8(np, "google,link-timeout-ms", &gpcie->link_timeout_ms);

	spin_lock_init(&gpcie->power_on_lock);
	spin_lock_init(&gpcie->power_stats_lock);
	spin_lock_init(&gpcie->link_duration_lock);
	spin_lock_init(&gpcie->link_up_lock);
	mutex_init(&gpcie->link_lock);
	ret = devm_add_action_or_reset(dev, gpcie_mutex_destroy, &gpcie->link_lock);
	if (ret)
		goto pm_disable;
	/*
	 * we would like to skip the dw_pcie_wait_for_link() in the dw_pcie_host_init()
	 * so we setup this bit to avoid waiting for the link up during the enumeration.
	 */
#ifdef DW_PCIE_CAP_USE_LINKUP_IRQ
	dw_pcie_cap_set(pci, USE_LINKUP_IRQ);
#else
	pci->pp.use_linkup_irq = true;
#endif
	ret = dw_pcie_host_init(&pci->pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		goto pm_disable;
	}

	pp = &gpcie->pci->pp;
	gpcie->domain = pp->bridge->bus->domain_nr;

	power_stats_init(gpcie);
	link_duration_update(gpcie, LINK_DURATION_INIT);
	link_stats_init(gpcie);

	ret = device_create_file(dev, &dev_attr_power_stats);
	if (ret) {
		dev_err(dev, "Failed to create power_stats sysfs file\n");
		goto host_deinit;
	}

	ret = device_create_file(dev, &dev_attr_link_state);
	if (ret) {
		dev_err(dev, "Failed to create link_state sysfs file\n");
		goto power_stats_remove;
	}

	ret = device_create_file(dev, &dev_attr_link_duration);
	if (ret) {
		dev_err(dev, "Failed to create device link_duration(%d) sysfs file\n", ret);
		goto link_state_remove;
	}

	scoped_guard(spinlock_irqsave, &gpcie_inst_lock) {
		list_add_tail(&gpcie->node, &gpcie_inst_list);
	}

	ret = devm_add_action_or_reset(dev, gpcie_list_del, &gpcie->node);
	if (ret)
		goto link_duration_remove;

	bus = pci->pp.bridge->bus;
	pci_dev = pci_get_slot(bus, 0);
	if (!pci_dev) {
		dev_err(gpcie->dev, "Could not find the pci_dev");
		ret = -ENODEV;
		goto link_duration_remove;
	}

	google_pcie_init_speed_width(gpcie, pci_dev);
	ret = google_pcie_init_debugfs(gpcie);
	if (ret)
		goto put_pci_dev;

	google_pcie_init_devcoredump(gpcie);

	ret = devm_add_action_or_reset(dev, google_pcie_exit_debugfs, gpcie);
	if (ret)
		goto put_pci_dev;

	gpcie->saved_state = pci_store_saved_state(pci_dev);
	gpcie->aer = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_ERR);

	pm_runtime_put(dev);
	pci_dev_put(pci_dev);

	return 0;

put_pci_dev:
	pci_dev_put(pci_dev);
link_duration_remove:
	device_remove_file(dev, &dev_attr_link_duration);
link_state_remove:
	device_remove_file(dev, &dev_attr_link_state);
power_stats_remove:
	device_remove_file(dev, &dev_attr_power_stats);
host_deinit:
	dw_pcie_host_deinit(&pci->pp);
pm_disable:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

	if (gpcie->phy_fw)
		release_firmware(gpcie->phy_fw);

wakeup_disable:
	device_set_wakeup_capable(dev, false);
remove_genpd:
	dev_pm_genpd_remove_notifier(dev);

	return ret;
}

static void google_pcie_remove(struct platform_device *pdev)
{
	struct google_pcie *gpcie = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct pci_dev *pci_dev = NULL;

	if (gpcie->is_link_up) {
		/*
		 * Currently, max one EP can be conncted to RC, which can connect
		 * on bus 1, devfn 0. remove it if it is enumerated
		 */
		pci_dev = pci_get_domain_bus_and_slot(gpcie->domain, 1, 0);
		if (pci_dev)
			pci_stop_and_remove_bus_device_locked(pci_dev);
		google_pcie_rc_poweroff(gpcie->domain);
	}
	device_remove_file(dev, &dev_attr_link_duration);
	device_remove_file(dev, &dev_attr_link_state);
	device_remove_file(dev, &dev_attr_power_stats);
	pm_runtime_get_sync(dev);
	dw_pcie_host_deinit(&gpcie->pci->pp);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

	if (gpcie->phy_fw)
		release_firmware(gpcie->phy_fw);

	device_set_wakeup_capable(dev, false);
	dev_pm_genpd_remove_notifier(dev);
}

static const struct of_device_id google_pcie_of_match[] = {
	{
		.compatible = "google,lga-pcie",
	},
	{},
};

static int google_pcie_runtime_suspend(struct device *dev)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	struct generic_pm_domain *genpd = pd_to_genpd(dev->pm_domain);
	unsigned long flags;
	int ret;

	trace_pci_suspend_start(dev);

	google_pcie_dump_icd(gpcie, "runtime_suspend start");

	/*
	 * At this point, the perst reset will be asserted.
	 * Thereby, pcie registers are not accessible.
	 * Therefore, mark pcie power off unconditionally.
	 */
	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	gpcie->powered_on = false;
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

	dev_dbg(gpcie->dev, "asserting perst reset\n");
	ret = reset_control_assert(gpcie->perst_rst);
	if (ret)
		dev_err(dev, "Failed to assert PERST ret %d\n", ret);

	/*
	 * When pcie top is marked as rpm always on,
	 * the power domain notifier callback will not be called.
	 * So, assert the remaining resets here.
	 */
	if (genpd->flags & GENPD_FLAG_RPM_ALWAYS_ON)
		google_pcie_assert_resets(gpcie);

	trace_pci_suspend_end(dev);

	google_pcie_dump_icd(gpcie, "runtime_suspend end");

	return 0;
}

static int google_pcie_prepare_suspend(struct device *dev)
{
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_sync(dev);
		pm_runtime_set_suspended(dev);
		return ret;
	}

	return 0;
}

static int google_pcie_suspend(struct device *dev)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);

	/*
	 * system suspend should not be called when link is up
	 * unless explicitly specified in DT.
	 * Otherwise it can lead to invalid fabric access.
	 * Therefore, error out and prevent system suspend.
	 */
	if (gpcie->is_link_up) {
		if (!gpcie->allow_suspend_in_linkup)
			return -EINVAL;
		else
			return 0;
	}

	return pm_runtime_force_suspend(dev);
}

static void google_pcie_patch_phy_fw(struct google_pcie *gpcie)
{
	int i, ret;
	u32 val;
	u32 size;
	const struct fw_patch_entry *phy_fw_patch;
	size_t phy_fw_patch_size;

	if (!gpcie->phy_sram_base)
		return;

	ret = readl_poll_timeout(gpcie->top_base + PCIE_PHY_STATUS1,
				 val, (val & SRAM_INIT_DONE),
				 SRAM_INIT_DELAY_US, SRAM_INIT_TIMEOUT_US);
	if (ret)
		return;


	phy_fw_patch = (const struct fw_patch_entry *)gpcie->phy_fw->data;
	phy_fw_patch_size = gpcie->phy_fw->size / sizeof(struct fw_patch_entry);

	size = (phy_fw_patch_size < (gpcie->phy_sram_size / 2)) ?
		phy_fw_patch_size : (gpcie->phy_sram_size / 2);

	if (!size)
		return;

	for (i = 0; i < size; i++) {
		u16 val = le16_to_cpu(phy_fw_patch[i].val);
		u32 addr = le32_to_cpu(phy_fw_patch[i].addr);

		if (addr + sizeof(val) > gpcie->phy_sram_size) {
			dev_err(gpcie->dev, "PHY FW address out of range: %#x/%#zx\n",
				addr, gpcie->phy_sram_size);
			return;
		}

		writew_relaxed(val, gpcie->phy_sram_base + addr);
	}

	/*
	 * Ensure all previous relaxed writes to PHY SRAM are completed and visible
	 * to the hardware before proceeding.
	 */
	wmb();

	val = readl(gpcie->top_base + PCIE_PHY_CFG1);
	val |= SRAM_EXT_LD_DONE;
	writel(val, gpcie->top_base + PCIE_PHY_CFG1);
}

static int google_pcie_runtime_resume(struct device *dev)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	int ret;
	u32 val;
	u32 vc_settings = FIELD_PREP(PCIE_AW_USER_VC, 1) |
				FIELD_PREP(PCIE_AR_USER_VC, 1);

	trace_pci_resume_start(dev);

	trace_pci_reg_enable(dev);

	google_pcie_dump_icd(gpcie, "runtime_resume start");

	dev_dbg(gpcie->dev, "de-asserting resets\n");

	ret = reset_control_deassert(gpcie->init_rst);
	if (ret)
		return ret;

	ret = reset_control_deassert(gpcie->pwr_up_rst);
	if (ret)
		goto assert_init_rst;

	ret = reset_control_deassert(gpcie->perst_rst);
	if (ret)
		goto assert_pwr_up_rst;

	trace_pci_reset_complete(dev);

	google_pcie_patch_phy_fw(gpcie);

	trace_pci_phy_fw_write(dev);

	val = readl(gpcie->top_base + PCIE_PHY_CFG1);
	val |= PG_MODE_EN;
	writel(val, gpcie->top_base + PCIE_PHY_CFG1);

	ret = reset_control_deassert(gpcie->app_hold_phy_rst);
	if (ret)
		goto assert_perst_rst;

	/* Configure VC to 1 for read and write every time after wake */
	val = readl(gpcie->top_base + PCIE_AXI_QOS);
	val &= ~(PCIE_AW_USER_VC | PCIE_AR_USER_VC);
	val |= vc_settings;
	writel(val, gpcie->top_base + PCIE_AXI_QOS);

	/* Disable Power Gating to the controller parts in L1 */
	if (gpcie->l1_pwrgate_disable) {
		val = readl(gpcie->sii_base + PM_CTRL_STATUS);
		val &= ~L1_PWR_OFF_EN;
		writel(val, gpcie->sii_base + PM_CTRL_STATUS);
	}

	trace_pci_resume_end(dev);

	gpcie->powered_on = true;

	google_pcie_dump_icd(gpcie, "runtime_resume end");

	return 0;

assert_perst_rst:
	reset_control_assert(gpcie->perst_rst);
assert_pwr_up_rst:
	reset_control_assert(gpcie->pwr_up_rst);
assert_init_rst:
	reset_control_assert(gpcie->init_rst);
	return ret;
}

static int google_pcie_resume(struct device *dev)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);

	if (gpcie->is_link_up)
		return 0;

	return pm_runtime_force_resume(dev);
}

static void google_pcie_resume_complete(struct device *dev)
{
	pm_runtime_put(dev);
}

int google_pcie_inbound_atu_cfg(int ch_num,
				 unsigned long long src_addr,
				 unsigned long long dst_addr,
				 unsigned long size,
				 int atu_index)
{
	u64 limit_addr;
	u32 retries;
	u32 val;
	u32 offset;
	struct google_pcie *gpcie;
	struct dw_pcie *pci;

	gpcie = google_pcie_get_handle(ch_num);
	if (!gpcie) {
		pr_err("Failed to program inbound ATU, invalid PCIe handle!\n");
		return -EINVAL;
	}
	pci = gpcie->pci;

	/* Setup inbound ATU region for mem transactions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	offset = PCIE_ATU_UNROLL_BASE(PCIE_ATU_REGION_DIR_IB, atu_index);
#else
	offset = PCIE_GET_ATU_INB_UNR_REG_OFFSET(atu_index);
#endif
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_LOWER_BASE, 4,
		      lower_32_bits(src_addr));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_UPPER_BASE, 4,
		      upper_32_bits(src_addr));
	limit_addr = src_addr + size - 1;
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_LOWER_LIMIT, 4,
		      lower_32_bits(limit_addr));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_LOWER_TARGET, 4,
		      lower_32_bits(dst_addr));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_UPPER_TARGET, 4,
		      upper_32_bits(dst_addr));
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_REGION_CTRL1, 4,
		      PCIE_ATU_TYPE_MEM);
	dw_pcie_write(pci->atu_base + offset + PCIE_ATU_UNR_REGION_CTRL2, 4,
		      PCIE_ATU_ENABLE);

	/* Make sure ATU enable takes effect before any subsequent config and I/O accesses */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		dw_pcie_read(pci->atu_base + offset + PCIE_ATU_UNR_REGION_CTRL2,
			     4, &val);
		if (val & PCIE_ATU_ENABLE) {
			dev_dbg(pci->dev, "Inbound ATU(%d) programmed for %#llx -> %#llx\n",
				atu_index, src_addr, dst_addr);
			return 0;
		}

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Inbound iATU is not being enabled\n");
	return -EAGAIN;
}
EXPORT_SYMBOL_GPL(google_pcie_inbound_atu_cfg);

int google_pcie_set_msi_ctrl_addr(int num, u64 msi_ctrl_addr)
{
	struct google_pcie *gpcie = NULL;

	gpcie = google_pcie_get_handle(num);
	if (!gpcie)
		return -EINVAL;

	gpcie->pci->pp.msi_data = msi_ctrl_addr;
	dev_dbg(gpcie->dev, "Updated MSI Control Addr: %pad\n",
		&gpcie->pci->pp.msi_data);

	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_set_msi_ctrl_addr);


static const struct dev_pm_ops google_pcie_dev_pm_ops = {
	.prepare = google_pcie_prepare_suspend,
	.complete = google_pcie_resume_complete,
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(google_pcie_suspend, google_pcie_resume)
	SET_RUNTIME_PM_OPS(google_pcie_runtime_suspend, google_pcie_runtime_resume,
			   NULL)
};

static struct platform_driver google_pcie_driver = {
	.driver = {
		.name	= "google-pcie",
		.of_match_table = google_pcie_of_match,
		.pm = &google_pcie_dev_pm_ops,
	},
	.probe = google_pcie_probe,
	.remove = google_pcie_remove,
};

static struct google_pcie *pci_to_gpcie(struct pci_dev *pci_dev)
{
	struct pci_host_bridge *host_bridge = pci_find_host_bridge(pci_dev->bus);
	struct google_pcie *gpcie;

	guard(spinlock_irqsave)(&gpcie_inst_lock);

	list_for_each_entry(gpcie, &gpcie_inst_list, node) {
		struct pci_host_bridge *bridge = gpcie->pci->pp.bridge;

		if (host_bridge == bridge)
			return gpcie;
	}

	return NULL;
}

static void google_pcie_power_manageable(void *data, struct pci_dev *pci_dev,
					 bool *manageable)
{
	struct google_pcie *gpcie = pci_to_gpcie(pci_dev);

	if (!gpcie)
		return;

	if (!pci_is_root_bus(pci_dev->bus))
		return;

	*manageable = true;
}

/*
 * Keep the target state as D0 for root ports during system suspend
 *
 * For Google PCIe root ports, the power control isn't managed by
 * pci_pm_* ops, but rather by the platform driver itself to ensure
 * proper power sequencing, so we just keep the target state as D0
 * during system suspend to avoid pci_pm_* interfering with our power
 * management.
 */
static void google_pcie_choose_state(void *data, struct pci_dev *pci_dev,
				     pci_power_t *state)
{
	struct google_pcie *gpcie = pci_to_gpcie(pci_dev);

	if (!gpcie)
		return;

	if (!pci_is_root_bus(pci_dev->bus))
		return;

	*state = PCI_D0;
}

static void google_pcie_pm_verify_state(void *data, int *state_ret, pci_power_t *state)
{
	if (state_ret && state)
		*state_ret = (*state == PCI_D3cold);
}

static int __init google_pcie_init(void)
{
	int ret;

	ret = register_trace_android_vh_platform_pci_power_manageable(
			google_pcie_power_manageable, NULL);
	if (ret)
		return ret;
	ret = register_trace_android_vh_platform_pci_choose_state(
			google_pcie_choose_state, NULL);
	if (ret)
		return ret;
	ret = register_trace_android_vh_pci_pm_verify_state(
			google_pcie_pm_verify_state, NULL);
	if (ret)
		return ret;

	return platform_driver_register(&google_pcie_driver);
}

static void __exit google_pcie_exit(void)
{
	platform_driver_unregister(&google_pcie_driver);

	unregister_trace_android_vh_pci_pm_verify_state(
			google_pcie_pm_verify_state, NULL);
	unregister_trace_android_vh_platform_pci_choose_state(
			google_pcie_choose_state, NULL);
	unregister_trace_android_vh_platform_pci_power_manageable(
			google_pcie_power_manageable, NULL);
}

module_init(google_pcie_init);
module_exit(google_pcie_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google PCIe Driver");
MODULE_LICENSE("GPL");
