// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024-2026 Google LLC
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/pci-pwrctrl.h>
#include <linux/pcie_google_if.h>
#include <linux/of_platform.h>
#include <linux/version.h>
#include <trace/hooks/pci.h>

#include "pcie-designware-host-customized.h"
#include "pcie-google.h"
#include "pcie-pci-customized.h"

#define CREATE_TRACE_POINTS
#include "pcie_trace.h"

#define DEFAULT_PERST_DELAY_US		20000
#define LINK_UP_MAX_RETRIES		4800
#define LINK_UP_WAIT_TIME_US		10
#define MAX_L2_COUNT			2000
#define L2_PME_TO_ACK_WAIT_US		10
#define WIDTH_SPEED_CHANGE_WAIT_US	10
#define MAX_TIMEOUT_WIDTH_SPEED_CHANGE	10000 /* Link width/speed change timeout (in 10us units) */
#define LINK_STATE_CHANGE_WAIT_US	10
#define MAX_TIMEOUT_LINK_STATE_CHANGE	100000 /* Link state change timeout (in 10us units) */
#define MAX_RETRAIN_TIME_US		1000000
#define MAX_PDG_TXN_CHECK_TIME_US	12000
#define CTRL_PSM_TIMEOUT_US		1000
#define CTRL_PSM_SLEEP_US		100

/* SII region offsets */
#define PE_GEN_CTRL_3			0x8
#define LTSSM_ENABLE			BIT(0)
#define HOT_RESET			BIT(2)

#define PE_GEN_CTRL_4			0xC
#define LINK_FLUSH_TIME_MASK		GENMASK(15, 11)
#define LINK_FLUSH_TIME_128MS		(0x1f << 11)

#define PE_PM_STS			0x34
#define L1SUB_STATE(x)			(((x) & (GENMASK(7, 5))) >> 5)
#define LINK_IN_L0S			BIT(11)
#define LINK_IN_L1			BIT(12)
#define LINK_IN_L1SUB			BIT(13)
#define RXELECIDLE_DIS			BIT(19)
#define TXCOMMONMODE_DIS		BIT(20)

#define PE_ERR_INT_STS			0x84
#define SEND_CORR_ERR_STS		BIT(25)
#define SEND_NF_ERR_STS			BIT(26)
#define SEND_F_ERR_STS			BIT(27)

#define PE_ERR_INT_CTRL			0x94
#define SEND_CORR_ERR_INT_EN		BIT(25)
#define SEND_NF_ERR_INT_EN		BIT(26)
#define SEND_F_ERR_INT_EN		BIT(27)

#define PE_TX_MSG_REQ			0x1c0
#define PME_TURN_OFF_REQ		BIT(19)

#define PE_LINK_DBG_2			0x304
#define LTSSM_STATE(x)			((x) & (GENMASK(5, 0)))
#define SMLH_LINK_UP			BIT(6)
#define RDLH_LINK_UP			BIT(7)
#define RADM_XFER_PENDING		BIT(20)
#define BRDG_SLV_XFER_PENDING		BIT(23)
#define LINK_UP				(SMLH_LINK_UP | RDLH_LINK_UP)

#define PE_FSM_TRACK_BASE		0x380
#define FSM_MODE_MASK			GENMASK(7, 6)
#define FSM_TRIG			(0x2 << 6)

#define DW_PCIE_LTSSM_CFG_LINKWD_START	0x07
#define DW_PCIE_LTSSM_CFG_LINKWD_ACEPT	0x08
#define DW_PCIE_LTSSM_CFG_LANENUM_WAI	0x09
#define DW_PCIE_LTSSM_CFG_LANENUM_ACEPT	0x0A
#define DW_PCIE_LTSSM_CFG_COMPLETE	0x0B
#define DW_PCIE_LTSSM_CFG_IDLE		0x0C
#define DW_PCIE_LTSSM_RCVRY_LOCK	0x0D
#define DW_PCIE_LTSSM_RCVRY_EQ0		0x20
#define DW_PCIE_LTSSM_RCVRY_EQ3		0x23

/* Top region offsets for PCIEX */
#define PM_CTRL_STATUS			0x38
#define L1_PWR_OFF_EN			BIT(5)

#define PM_UNLOCK_ERR_MSG		0x4c
#define RADM_PM_TO_ACK			BIT(1)

#define PCIEX_PERST_REG			0x60

/* Subsystem general offsets */
#define SS_BD_NUM_CTRL_1		0x40
#define PE0_APP_DEV_NUM			GENMASK(7, 3)
#define PE0_APP_BUS_NUM			GENMASK(15, 8)
#define PE1_APP_DEV_NUM			GENMASK(23, 19)
#define PE1_APP_BUS_NUM			GENMASK(31, 24)

/* DWC Port Logic offsets */
/* bit-fields of PCIE_PORT_MULTI_LANE_CTRL */
#define PORT_TARGET_LINK_WIDTH_MASK	GENMASK(5, 0)
#define PORT_DIRECT_LINK_WIDTH_CHANGE	BIT(6)

#define AMBA_LINK_TIMEOUT_OFF		0x8D4
#define LINK_TIMEOUT_PERIOD		GENMASK(7, 0)

#define L1_SUBSTATES_OFF		0xB44
#define LOW_POWER_CLOCK_SWITCH_MODE	BIT(8)

#define UTILITY_OFF			0xC80

#define S5400_VENDOR_DEVICE_ID		0xA5A5144D
#define S5400_MSI_CAP_OFFSET		0x50

/* From L1 PM Substates Capabiliity */
enum {
	PCI_L1SS_TPOWERON_SCALE_2_US,
	PCI_L1SS_TPOWERON_SCALE_10_US,
	PCI_L1SS_TPOWERON_SCALE_100_US,
	PCI_L1SS_TPOWERON_SCALE_RESERVED
};

/*
 * SoC recommendations:
 * T_Poweron = 100us
 * Common_mode_restore_time = 60us
 */
#define GPCIE_TPOWERON_SCALE	PCI_L1SS_TPOWERON_SCALE_10_US
#define GPCIE_TPOWERON_VALUE	10
#define GPCIE_CM_RESTORE_TIME	60

static LIST_HEAD(gpcie_inst_list);
static DEFINE_SPINLOCK(gpcie_inst_lock);

static inline struct google_pcie *bridge_to_gpcie(struct pci_host_bridge *bridge)
{
	return dev_get_drvdata(bridge->dev.parent);
}

static int pci_pm_runtime_get_sync(struct pci_dev *pdev, void *unused)
{
	pm_runtime_get_sync(&pdev->dev);
	return 0;
}

static int pci_pm_runtime_put(struct pci_dev *pdev, void *unused)
{
	pm_runtime_put(&pdev->dev);
	return 0;
}

static int pci_pm_set_d3cold(struct pci_dev *pdev, void *unused)
{
	pdev->current_state = PCI_D3cold;
	return 0;
}

static int pci_pm_set_d0(struct pci_dev *pdev, void *unused)
{
	pci_set_power_state(pdev, PCI_D0);
	return 0;
}

static void google_pcie_walk_bus(struct google_pcie *gpcie,
				 int (*cb)(struct pci_dev *, void *))
{
	pci_walk_bus(gpcie->pci->pp.bridge->bus, cb, NULL);
}

/**
 * google_pcie_hold_bus_rpm - Prevent runtime and system suspend for the bus
 * @gpcie: PCIe controller state
 * @ws: Wakeup source to prevent system suspend
 * @flag: Boolean flag to track the hold state
 *
 * Take a runtime PM reference for all devices on the bus to prevent them from
 * suspending during sensitive operations (like reset or recovery). Also
 * activates the provided wakeup source to prevent the entire system from
 * entering sleep.
 *
 * Callers MUST guarantee strict macro-level serialization of this function to
 * prevent TOCTOU races with the PM core. Do not call concurrently for the same flag.
 */
static void google_pcie_hold_bus_rpm(struct google_pcie *gpcie, struct wakeup_source *ws,
				     bool *flag)
{
	if (*flag)
		return;

	__pm_stay_awake(ws);
	google_pcie_walk_bus(gpcie, pci_pm_runtime_get_sync);
	google_pcie_walk_bus(gpcie, pci_pm_set_d3cold);
	*flag = true;
}

/**
 * google_pcie_drop_bus_rpm - Release PM and system-sleep holds for the bus
 * @gpcie: PCIe controller state
 * @ws: Wakeup source to release
 * @flag: Boolean flag tracking the hold state
 */
static void google_pcie_drop_bus_rpm(struct google_pcie *gpcie, struct wakeup_source *ws,
				     bool *flag)
{
	if (!*flag)
		return;

	*flag = false;
	google_pcie_walk_bus(gpcie, pci_pm_set_d0);
	google_pcie_walk_bus(gpcie, pci_pm_runtime_put);
	__pm_relax(ws);
}

static void google_pcie_set_lp_clock_switch_mode(struct google_pcie *gpcie)
{
	/*
	 * Set LOW_POWER_CLOCK_SWITCH_MODE so that aux_clk is only switched to
	 * the slow platform clock when the link partner deasserts CLKREQ#
	 */
	u32 reg;

	reg = dw_pcie_readl_dbi(gpcie->pci, L1_SUBSTATES_OFF);
	reg |= LOW_POWER_CLOCK_SWITCH_MODE;
	dw_pcie_writel_dbi(gpcie->pci, L1_SUBSTATES_OFF, reg);
}

static void google_pcie_set_l1_ss_capabilities(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;
	u32 val;

	val = dw_pcie_readl_dbi(pci, gpcie->l1ss + PCI_L1SS_CAP);

	/*
	 * Programming Common_Mode_Restore_Time, T_Power_On_Val_Support &
	 * T_Power_On_Scale_Support Capability
	 */
	val &= ~(PCI_L1SS_CAP_CM_RESTORE_TIME | PCI_L1SS_CAP_P_PWR_ON_VALUE |
		 PCI_L1SS_CAP_P_PWR_ON_SCALE);
	val |= FIELD_PREP(PCI_L1SS_CAP_CM_RESTORE_TIME, GPCIE_CM_RESTORE_TIME);
	val |= FIELD_PREP(PCI_L1SS_CAP_P_PWR_ON_VALUE, GPCIE_TPOWERON_VALUE);
	val |= FIELD_PREP(PCI_L1SS_CAP_P_PWR_ON_SCALE, GPCIE_TPOWERON_SCALE);

	dw_pcie_writel_dbi(pci, gpcie->l1ss + PCI_L1SS_CAP, val);
}

static void google_pcie_disable_cpl_timeout(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;
	u32 exp = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 val;

	val = dw_pcie_readl_dbi(pci, exp + PCI_EXP_DEVCTL2);
	val |= PCI_EXP_DEVCTL2_COMP_TMOUT_DIS;
	dw_pcie_writel_dbi(pci, exp + PCI_EXP_DEVCTL2, val);
}

static void google_pcie_set_bus_dev_nr(struct google_pcie *gpcie)
{
	u32 val;

	/*
	 * dev and bus should be 0, but some controllers come with unexpected
	 * values.
	 */
	regmap_read(gpcie->subsystem_general, SS_BD_NUM_CTRL_1, &val);
	val &= ~(PE0_APP_DEV_NUM | PE0_APP_BUS_NUM | PE1_APP_DEV_NUM | PE1_APP_BUS_NUM);
	regmap_write(gpcie->subsystem_general, SS_BD_NUM_CTRL_1, val);
}

static u8 google_pcie_get_speed_capability(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;
	u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 lnkcap2;

	lnkcap2 = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCAP2);

	return PCIE_LNKCAP2_SLS2SPEED(lnkcap2) - PCIE_SPEED_2_5GT + 1;
}

static u8 google_pcie_get_width_capability(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;
	u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	u32 lnkcap = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCAP);

	if (lnkcap)
		return (u8)FIELD_GET(PCI_EXP_LNKCAP_MLW, lnkcap);

	return (u8)PCIE_LNK_WIDTH_UNKNOWN;
}

static void google_pcie_init_speed_width(struct google_pcie *gpcie)
{
	u8 supported_max_speed = google_pcie_get_speed_capability(gpcie);
	u8 supported_max_width = google_pcie_get_width_capability(gpcie);

	/*
	 * If "max-link-speed" DT property (max_link_speed) is set to some value,
	 * set to minimum of max supported by HW and DT property.
	 * If any invalid value is passed in DT, set it to max HW supported.
	 */
	gpcie->max_link_speed = (gpcie->pci->max_link_speed > 0) ?
				 min((u8)gpcie->pci->max_link_speed, supported_max_speed) :
				 supported_max_speed;

	/* Do the same for max width. DT property for max width is: "num-lanes" */
	gpcie->max_link_width = (gpcie->pci->num_lanes > 0) ?
				 min((u8)gpcie->pci->num_lanes, supported_max_width) :
				 supported_max_width;

	dev_dbg(gpcie->dev, "%s: max link speed:%d, max link width:%d\n",
		__func__, gpcie->max_link_speed, gpcie->max_link_width);

	/* Initialize target_link* as max values */
	gpcie->target_link_speed = gpcie->max_link_speed;
	gpcie->target_link_width = gpcie->max_link_width;
}

static void google_pcie_disable_equalization(struct google_pcie *gpcie)
{
	u32 val;

	/* Bit field name implies GEN3, but applies to GEN3 & GEN4 */
	val = dw_pcie_readl_dbi(gpcie->pci, GEN3_RELATED_OFF);
	val |= GEN3_RELATED_OFF_GEN3_EQ_DISABLE;
	dw_pcie_writel_dbi(gpcie->pci, GEN3_RELATED_OFF, val);
	dev_dbg(gpcie->dev, "Disabled GEN3/GEN4 Link equalization\n");
}

static void google_pcie_disable_l1_pwr_gating(struct google_pcie *gpcie)
{
	u32 val;

	val = readl(gpcie->top_base + PM_CTRL_STATUS);
	val &= ~L1_PWR_OFF_EN;
	writel(val, gpcie->top_base + PM_CTRL_STATUS);
}

void google_pcie_assert_perst_n(struct google_pcie *gpcie, bool assert)
{
	dev_dbg(gpcie->dev, "%s PERST GPIO\n", assert ? "asserting" : "de-asserting");
	gpiod_set_value(gpcie->perstn_gpio, assert);
}

/*
 * force the longest flush time (128ms)
 * The EP driver needs some time and read/write transactions to stop
 * its traffic and we might see a bus hang caused by the read/write
 * during this process. Use the longest timeout value to cover
 * this period. The best case will be covering the whole process
 * but 128ms is the longest flushing time we have now.
 */
static void google_pcie_set_link_flush_time(struct google_pcie *gpcie)
{
	u32 val;

	val = readl(gpcie->sii_base + PE_GEN_CTRL_4);
	val &= ~LINK_FLUSH_TIME_MASK;
	val |= LINK_FLUSH_TIME_128MS;
	writel(val, gpcie->sii_base + PE_GEN_CTRL_4);
}

void google_pcie_hot_reset(struct google_pcie *gpcie)
{
	u32 val;

	val = readl(gpcie->sii_base + PE_GEN_CTRL_3);
	val |= HOT_RESET;
	writel(val, gpcie->sii_base + PE_GEN_CTRL_3);
}

static int google_pcie_add_bus(struct pci_bus *bus)
{
	struct dw_pcie *pci;
	struct google_pcie *gpcie;
	struct dw_pcie_rp *pp;

	if (!pci_is_root_bus(bus))
		return 0;

	pci = to_dw_pcie_from_pp(bus->sysdata);
	gpcie = dev_get_drvdata(pci->dev);

	gpcie->domain = bus->domain_nr;
	pp = &pci->pp;

	/*
	 * Make google_pcie_*() APIs available to clients as soon as (but no
	 * sooner than) the bridge bus is established.
	 */
	scoped_guard(spinlock_irqsave, &gpcie_inst_lock) {
		list_add_tail(&gpcie->node, &gpcie_inst_list);
	}

	return 0;
}

static void google_pcie_remove_bus(struct pci_bus *bus)
{
	struct dw_pcie *pci;
	struct google_pcie *gpcie;
	struct dw_pcie_rp *pp;

	if (!pci_is_root_bus(bus))
		return;

	pci = to_dw_pcie_from_pp(bus->sysdata);
	gpcie = dev_get_drvdata(pci->dev);
	pp = &pci->pp;
	pp->msi_irq_chip->irq_set_affinity = NULL;

	scoped_guard(spinlock_irqsave, &gpcie_inst_lock) {
		list_del(&gpcie->node);
	}
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

/* Dedicated ops for accessing RC's config space */
static struct pci_ops google_pcie_ops = {
	.add_bus = google_pcie_add_bus,
	.remove_bus = google_pcie_remove_bus,
	.read = google_pcie_rd_own_conf,
	.write = google_pcie_wr_own_conf,
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
	offset = PCIE_ATU_UNROLL_BASE(PCIE_ATU_REGION_DIR_IB, atu_index);
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
		dw_pcie_read(pci->atu_base + offset + PCIE_ATU_UNR_REGION_CTRL2, 4, &val);
		if (val & PCIE_ATU_ENABLE) {
			dev_info(pci->dev, "Inbound ATU(%d) programmed for %#llx -> %#llx\n",
				 atu_index, src_addr, dst_addr);
			return 0;
		}

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Inbound iATU is not being enabled\n");
	return -EAGAIN;
}
EXPORT_SYMBOL_GPL(google_pcie_inbound_atu_cfg);

static u32 google_pcie_get_link_state(struct google_pcie *gpcie)
{
	u32 link_dbg;
	u32 pm_state;
	u32 ltssm_state;
	unsigned long flags;

	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	if (!gpcie->powered_on) {
		spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
		return L2;
	}

	pm_state = readl(gpcie->sii_base + PE_PM_STS);
	link_dbg = readl(gpcie->sii_base + PE_LINK_DBG_2);
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

	ltssm_state = LTSSM_STATE(link_dbg);
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

	if ((pm_state & LINK_IN_L1SUB) && (pm_state & RXELECIDLE_DIS)) {
		if (pm_state & TXCOMMONMODE_DIS)
			return L12;

		return L11;
	}

	if (pm_state & LINK_IN_L1)
		return L1;

	if (ltssm_state == DW_PCIE_LTSSM_L2_IDLE)
		return L2;

	/* Treat anomalous LTSSM states between L0 and L1ss as L0 */
	if (ltssm_state > DW_PCIE_LTSSM_L0 && ltssm_state < DW_PCIE_LTSSM_L2_IDLE)
		return L0;

	return UNKNOWN;
}

static void google_pcie_hw_init(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;

	google_pcie_set_l1_ss_capabilities(gpcie);

	if (gpcie->skip_link_eq)
		google_pcie_disable_equalization(gpcie);

	/*
	 * Program the PORT_LOGIC space UTILITY register to prevent the
	 * controller from power gating when there is a pending MSI.
	 */
	dw_pcie_writel_dbi(pci, UTILITY_OFF, 0x2);

	google_pcie_set_lp_clock_switch_mode(gpcie);

	/* Disable Power Gating to the controller parts in L1 */
	if (gpcie->l1_pwrgate_disable)
		google_pcie_disable_l1_pwr_gating(gpcie);

	if (device_property_read_bool(gpcie->dev,
				      "google,disable-completion-timeout"))
		google_pcie_disable_cpl_timeout(gpcie);

	google_pcie_set_link_flush_time(gpcie);
	google_pcie_set_bus_dev_nr(gpcie);

	dw_pcie_config_presets(&gpcie->pci->pp);
}

static int google_pcie_reset_root_port(struct pci_host_bridge *bridge,
					struct pci_dev *pdev);

static int google_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	u32 val;
	int ret;

	pp->bridge->ops = &google_pcie_ops;
	gpcie->reset_root_port = google_pcie_reset_root_port;

	gpcie->aer = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_ERR);
	gpcie->exp = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);

	if (gpcie->link_timeout_ms) {
		val = dw_pcie_readl_dbi(pci, AMBA_LINK_TIMEOUT_OFF);
		val = u32_replace_bits(val, gpcie->link_timeout_ms, LINK_TIMEOUT_PERIOD);
		dw_pcie_writel_dbi(pci, AMBA_LINK_TIMEOUT_OFF, val);
		dev_info(gpcie->dev, "Link request queue flush timeout set to %u ms\n",
			 gpcie->link_timeout_ms);
	}
	gpcie->l1ss = dw_pcie_find_ext_capability(pci, PCI_EXT_CAP_ID_L1SS);

	if (pci->num_lanes < 1)
		pci->num_lanes = dw_pcie_link_get_max_link_width(pci);

	google_pcie_init_speed_width(gpcie);

	ret = of_pci_get_equalization_presets(gpcie->dev, &gpcie->presets, pci->num_lanes);
	if (ret) {
		dev_err(pci->dev, "Failed to parse EQ presets: %d\n", ret);
		return ret;
	}

	google_pcie_hw_init(gpcie);

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

	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	if (!gpcie->powered_on) {
		spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
		return 0;
	}

	ctl_status = readl(gpcie->sii_base + PE_LINK_DBG_2);
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

	return (((ctl_status & LINK_UP) == LINK_UP) &&
		((google_pcie_get_link_state(gpcie)) != L2));
}

/* Wait for a max of 48ms for link up, then timeout */
static int google_pcie_wait_for_link(struct dw_pcie *pci)
{
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	u32 ctl_status, debug_info;
	u32 i;

	for (i = 0; i < LINK_UP_MAX_RETRIES; i++) {
		debug_info = readl(pci->dbi_base + PCIE_PORT_DEBUG1);

		/*
		 * Also check for LINK_IN_TRAINING, to ensure speed negotiation
		 * is complete.
		 */
		if (dw_pcie_link_up(pci) &&
		    !(debug_info & PCIE_PORT_DEBUG1_LINK_IN_TRAINING))
			return 0;

		usleep_range(LINK_UP_WAIT_TIME_US, LINK_UP_WAIT_TIME_US + 2);
	}

	ctl_status = readl(gpcie->sii_base + PE_LINK_DBG_2);
	debug_info = readl(pci->dbi_base + PCIE_PORT_DEBUG1);

	dev_info(pci->dev,
		 "Phy link never came up (ctl_status=%#10x, debug_info=%#010x)\n",
		 ctl_status, debug_info);
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

	dev_dbg(gpcie->dev, "disable LTSSM training\n");
	val = readl(gpcie->sii_base + PE_GEN_CTRL_3);
	val &= ~LTSSM_ENABLE;
	writel(val, gpcie->sii_base + PE_GEN_CTRL_3);
}

static void google_pcie_fetch_speed_width(struct dw_pcie *pci)
{
	u32 val;
	u8 exp_cap_offset;
	struct google_pcie *gpcie;

	gpcie = dev_get_drvdata(pci->dev);
	exp_cap_offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	val = dw_pcie_readw_dbi(pci, exp_cap_offset + PCI_EXP_LNKSTA);
	gpcie->current_link_speed = val & PCI_EXP_LNKSTA_CLS;
	gpcie->current_link_width = (val & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;
}

static int google_pcie_retrain_link(struct dw_pcie *pci)
{
	u32 val;
	u8 exp_cap_offset;
	int retrain_time = 0;

	exp_cap_offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	val = dw_pcie_readw_dbi(pci, exp_cap_offset + PCI_EXP_LNKCTL);
	val |= PCI_EXP_LNKCTL_RL;
	dw_pcie_writew_dbi(pci, exp_cap_offset + PCI_EXP_LNKCTL, val);
	while (retrain_time < MAX_RETRAIN_TIME_US) {
		usleep_range(100, 200);
		retrain_time += 100;
		val = dw_pcie_readw_dbi(pci, exp_cap_offset + PCI_EXP_LNKSTA);
		if (!(val & PCI_EXP_LNKSTA_LT))
			return 0;
	}
	return -ETIMEDOUT;
}

static void google_pcie_init_fsm_tracker(struct google_pcie *gpcie)
{
	/* reset FSM tracker */
	writel(0, gpcie->sii_base + PE_FSM_TRACK_BASE);

	/* start tracking */
	writel(FSM_TRIG, gpcie->sii_base + PE_FSM_TRACK_BASE);
}

static int __google_pcie_start_link(struct dw_pcie *pci)
{
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	u32 val;

	/* reset PHY */
	google_pcie_assert_perst_n(gpcie, false);
	usleep_range(gpcie->perst_delay_us, gpcie->perst_delay_us + 2000);

	google_pcie_init_fsm_tracker(gpcie);

	dev_info(pci->dev, "enable app_ltssm_en for link training\n");
	val = readl(gpcie->sii_base + PE_GEN_CTRL_3);
	val |= LTSSM_ENABLE;
	writel(val, gpcie->sii_base + PE_GEN_CTRL_3);

	return google_pcie_wait_for_link(pci);
}

static void google_pcie_send_pme_turn_off(struct google_pcie *gpcie)
{
	u32 val, count;

	/* Assert SII signal to send PME_Turn_Off  */
	val = readl(gpcie->sii_base + PE_TX_MSG_REQ);
	val |= PME_TURN_OFF_REQ;
	writel(val, gpcie->sii_base + PE_TX_MSG_REQ);
	for (count = 0; count < MAX_L2_COUNT; count++) {
		usleep_range(L2_PME_TO_ACK_WAIT_US, L2_PME_TO_ACK_WAIT_US + 1);
		val = readl(gpcie->top_base + PM_UNLOCK_ERR_MSG);
		if (val & RADM_PM_TO_ACK) {
			dev_dbg(gpcie->dev, "Received PME_TO_ACK\n");
			return;
		}
	}
	dev_err(gpcie->dev, "Timed out waiting for PME_TO_ACK\n");
}

static int __google_pcie_rc_poweron(struct google_pcie *gpcie);
static int __google_pcie_rc_poweroff(struct google_pcie *gpcie);

static int google_pcie_start_link(struct dw_pcie *pci)
{
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);

	/*
	 * We often don't have external power ready yet, so poweron() will fail
	 * to train. Return success so as not to confuse pcie-designware; we'll
	 * try again when power is ready (pci_pwrctrl_device_set_ready()).
	 */
	if (!gpcie->power_ready)
		return 0;

	return __google_pcie_rc_poweron(gpcie);
}

static void google_pcie_stop_link(struct dw_pcie *pci)
{
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);

	if (!gpcie->power_ready)
		return;

	__google_pcie_rc_poweroff(gpcie);
}

static const struct dw_pcie_ops google_dw_pcie_ops = {
	.start_link = google_pcie_start_link,
	.stop_link = google_pcie_stop_link,
	.link_up = google_pcie_link_up,
	.read_dbi = google_pcie_read_dbi,
	.write_dbi = google_pcie_write_dbi,
};

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

static void google_pcie_enable_err_irq(struct google_pcie *gpcie)
{
	u32 val;

	/*
	 * Configure PCIe Subsystem to enable Correctable, Non-Fatal, and Fatal
	 * Interrupts.
	 */
	val = readl(gpcie->sii_base + PE_ERR_INT_CTRL);
	val |= SEND_CORR_ERR_INT_EN;
	val |= SEND_NF_ERR_INT_EN;
	val |= SEND_F_ERR_INT_EN;
	writel(val, gpcie->sii_base + PE_ERR_INT_CTRL);

	/*
	 * Configure PCIe Controller to report Correctable, Non-Fatal, and
	 * Fatal Errors.
	 */
	val = dw_pcie_readw_dbi(gpcie->pci, gpcie->exp + PCI_EXP_DEVCTL);
	val |= PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE | PCI_EXP_DEVCTL_FERE;
	dw_pcie_writew_dbi(gpcie->pci, gpcie->exp + PCI_EXP_DEVCTL, val);
}

static void google_pcie_disable_err_irq(struct google_pcie *gpcie)
{
	u32 val;

	/*
	 * Configure PCIe Controller to not report Correctable, Non-Fatal, and
	 * Fatal Errors.
	 */
	val = dw_pcie_readw_dbi(gpcie->pci, gpcie->exp + PCI_EXP_DEVCTL);
	val &= ~(PCI_EXP_DEVCTL_CERE | PCI_EXP_DEVCTL_NFERE | PCI_EXP_DEVCTL_FERE);
	dw_pcie_writew_dbi(gpcie->pci, gpcie->exp + PCI_EXP_DEVCTL, val);

	/*
	 * Configure PCIe Subsystem to disable Correctable, Non-Fatal, and
	 * Fatal Interrupts.
	 */
	val = readl(gpcie->sii_base + PE_ERR_INT_CTRL);
	val &= ~SEND_CORR_ERR_INT_EN;
	val &= ~SEND_NF_ERR_INT_EN;
	val &= ~SEND_F_ERR_INT_EN;
	writel(val, gpcie->sii_base + PE_ERR_INT_CTRL);
}

static void google_pcie_check_pending_txns(struct google_pcie *gpcie)
{
	u32 val = 0;

	readl_poll_timeout(gpcie->sii_base + PE_LINK_DBG_2, val,
			   !(val & BRDG_SLV_XFER_PENDING) && !(val & RADM_XFER_PENDING),
			   10, MAX_PDG_TXN_CHECK_TIME_US);
}

static int __google_pcie_rc_poweron(struct google_pcie *gpcie)
{
	struct dw_pcie *pci = gpcie->pci;
	int ret;
	int retry = LINK_WAIT_MAX_RETRIES;
	unsigned long flags;
	bool link_up = false;
	u32 reg;

	dev_dbg(gpcie->dev, "running poweron sequence...\n");
	if (gpcie->is_link_up) {
		dev_err(gpcie->dev, "Link is up, quit poweron\n");
		return -EINVAL;
	}

	dev_dbg(gpcie->dev, "de-asserting perst reset\n");
	writel(1, gpcie->top_base + PCIEX_PERST_REG);
	ret = readl_poll_timeout(gpcie->ctrl_psm_state, reg,
				 PSM_READY(reg) && PSM_STATE_EQ(reg, 0),
				 CTRL_PSM_SLEEP_US, CTRL_PSM_TIMEOUT_US);
	if (ret) {
		dev_err(gpcie->dev, "PCIe Controller stuck in PG state (reg=%#010x)\n", reg);
		goto reset;
	}

	trace_pci_reset_complete(gpcie->dev);

	gpcie->powered_on = true;

	ret = phy_init(gpcie->phy);
	if (ret < 0) {
		dev_err(gpcie->dev, "Failed to initialize PHY: %d\n", ret);
		goto reset;
	}
	ret = phy_power_on(gpcie->phy);
	if (ret < 0) {
		dev_err(gpcie->dev, "Failed to power on PHY: %d\n", ret);
		goto phy_exit;
	}

	google_pcie_hw_init(gpcie);

	google_pcie_set_link_width_speed(gpcie);
	/*
	 * Restore IP's DBI registers - reinitialize instead of restoring
	 *
	 * TODO(b/415858284): this programs the Link Capability supported
	 * speeds. If we're not careful, this may conflict with the Link
	 * Control 2 Target Link Speed saved by the port driver.
	 */
	dw_pcie_setup_rc(&pci->pp);

	if (gpcie->aggr_err_irq > 0)
		google_pcie_enable_err_irq(gpcie);

	ret = pinctrl_pm_select_default_state(gpcie->dev);
	if (ret) {
		dev_err(gpcie->dev, "Failed to set CLKREQ func: %d\n", ret);
		goto phy_off;
	}

	while (retry--) {
		if (!link_up)
			ret = __google_pcie_start_link(pci);
		else
			ret = google_pcie_retrain_link(pci);

		if (ret) {
			// Stop and attempt to restart the link
			link_up = false;
			google_pcie_assert_perst_n(gpcie, true);
			google_pcie_disable_ltssm(gpcie);

			spin_lock_irqsave(&gpcie->link_stats_lock, flags);
			gpcie->link_stats.link_up_failure_count++;
			spin_unlock_irqrestore(&gpcie->link_stats_lock, flags);

			usleep_range(gpcie->perst_delay_us, gpcie->perst_delay_us + 2000);
		} else {
			link_up = true;
			google_pcie_fetch_speed_width(pci);

			if (gpcie->current_link_speed == gpcie->target_link_speed)
				break;

			/* Retry to get a higher speed */
			dev_dbg(gpcie->dev,
				"%s: Link speed (GEN-%d) is less than target speed (GEN-%d)",
				__func__, gpcie->current_link_speed, gpcie->target_link_speed);
		}
	}

	if (!link_up) {
		dev_err(gpcie->dev, "Failed to start link, giving up retries\n");
		ret = -EPIPE;

		spin_lock_irqsave(&gpcie->link_stats_lock, flags);
		gpcie->link_stats.link_recovery_failure_count++;
		spin_unlock_irqrestore(&gpcie->link_stats_lock, flags);

		google_pcie_create_devcoredump(gpcie);
		goto clkreq_idle;
	}

	if (gpcie->current_link_speed == gpcie->target_link_speed) {
		dev_info(gpcie->dev, "Link up at expected rate. Link speed = Gen%d, Target = Gen%d\n",
			 gpcie->current_link_speed, gpcie->target_link_speed);
	} else {
		dev_info(gpcie->dev, "Link up at reduced rate. Link speed = Gen%d, Target = Gen%d\n",
			 gpcie->current_link_speed, gpcie->target_link_speed);
	}

	/* Clear any previous PME_TO_ACK after link is up */
	reg = RADM_PM_TO_ACK;
	writel(reg, gpcie->top_base + PM_UNLOCK_ERR_MSG);

	gpcie->is_link_up = true;
	if (gpcie->aggr_err_irq > 0)
		enable_irq(gpcie->aggr_err_irq);
	power_stats_update_up(gpcie);

	dev_dbg(gpcie->dev, "poweron success\n");
	device_wakeup_enable(gpcie->dev);

	return 0;

clkreq_idle:
	pinctrl_pm_select_idle_state(gpcie->dev);
phy_off:
	if (gpcie->aggr_err_irq > 0)
		google_pcie_disable_err_irq(gpcie);
	phy_power_off(gpcie->phy);
phy_exit:
	phy_exit(gpcie->phy);
reset:
	gpcie->current_link_speed = 0;
	gpcie->current_link_width = 0;
	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	gpcie->powered_on = false;
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);
	writel(0, gpcie->top_base + PCIEX_PERST_REG);

	return ret;
}

static int __google_pcie_rc_poweroff(struct google_pcie *gpcie)
{
	unsigned long flags;
	int ret;
	u32 reg;

	dev_dbg(gpcie->dev, "running poweroff sequence...\n");
	if (!gpcie->is_link_up) {
		dev_err(gpcie->dev, "Link is down, quit poweroff\n");
		return -EINVAL;
	}

	if (gpcie->aggr_err_irq > 0)
		disable_irq_nosync(gpcie->aggr_err_irq);

	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	gpcie->is_link_up = false;
	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
	gpcie->current_link_speed = 0;
	gpcie->current_link_width = 0;
	google_pcie_init_fsm_tracker(gpcie);
	if (!google_pcie_in_recovery(gpcie)) {
		google_pcie_send_pme_turn_off(gpcie);
		google_pcie_wait_for_state(gpcie, L2);
		/* Give EP some time to wind down after sending L23READY */
		usleep_range(1000, 1200);
		dev_info(gpcie->dev, "Link down\n");
	} else if (test_and_clear_bit(GPCIE_IN_CPL_TIMEOUT,
				      &gpcie->recovery_flags)) {
		google_pcie_check_pending_txns(gpcie);
	}
	clear_bit(GPCIE_IN_LINK_DOWN, &gpcie->recovery_flags);

	google_pcie_assert_perst_n(gpcie, true);
	/*
	 * Guarantee a 20ms delay between perst assertion and
	 * de-assertion
	 */
	msleep(20);
	ret = pinctrl_pm_select_idle_state(gpcie->dev);
	if (ret)
		dev_err(gpcie->dev, "Failed to set CLKREQ idle: %d\n", ret);

	if (gpcie->aggr_err_irq > 0)
		google_pcie_disable_err_irq(gpcie);

	/*
	 * At this point, the perst reset will be asserted.
	 * Thereby, pcie registers are not accessible.
	 * Therefore, mark pcie power off unconditionally.
	 */
	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	gpcie->powered_on = false;
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

	dev_dbg(gpcie->dev, "asserting perst reset\n");
	writel(0, gpcie->top_base + PCIEX_PERST_REG);

	ret = readl_poll_timeout(gpcie->ctrl_psm_state, reg,
				 PSM_READY(reg) && PSM_STATE_EQ(reg, 1),
				 CTRL_PSM_SLEEP_US, CTRL_PSM_TIMEOUT_US);
	if (ret)
		dev_err(gpcie->dev, "polling ctrl PSM PS1 timeout %d, (reg=%#010x)\n",
			ret, reg);

	google_pcie_disable_ltssm(gpcie);

	power_stats_update_down(gpcie);
	ret = phy_power_off(gpcie->phy);
	if (ret)
		dev_warn(gpcie->dev, "Failed to power off PHY: %d\n", ret);
	ret = phy_exit(gpcie->phy);
	if (ret)
		dev_warn(gpcie->dev, "Failed to exit PHY: %d\n", ret);

	dev_dbg(gpcie->dev, "poweroff success\n");
	device_wakeup_disable(gpcie->dev);

	return 0;
}

static int google_pcie_reset_root_port(struct pci_host_bridge *bridge,
					struct pci_dev *pdev)
{
	struct pci_bus *bus = bridge->bus;
	struct dw_pcie_rp *pp = bus->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct google_pcie *gpcie = dev_get_drvdata(pci->dev);
	int ret;

	ret = pm_runtime_force_suspend(gpcie->dev);
	if (ret) {
		dev_err(gpcie->dev, "Failed to force suspend/reset: %d\n", ret);
		return ret;
	}

	ret = pm_runtime_force_resume(gpcie->dev);
	if (ret) {
		dev_err(gpcie->dev, "Failed to force resume/reset: %d\n", ret);
		return ret;
	}
	return 0;
}

/*
 * pci-pwrctrl controls external power. We need to wait for external power
 * before deasserting endpoint PERST#. Upstream pci-pwrctrl does not yet handle
 * this.
 *
 * We log 'power_ready' state so we can coordinate with other operations.
 */
static int google_pcie_perst_assert(struct google_pcie *gpcie, bool assert)
{
	struct pci_bus *bus = gpcie->pci->pp.bridge->bus;
	struct pci_dev *pci_dev;
	int ret = 0;

	ret = pm_runtime_resume_and_get(gpcie->dev);
	if (ret < 0) {
		/* clear error */
		pm_runtime_set_suspended(gpcie->dev);
		return ret;
	}

	WARN_ON(gpcie->power_ready == !assert);

	if (assert) {
		/*
		 * Prevent runtime-suspend and system-suspend of the bus and its
		 * children during a power-off/power-on sequence.
		 *
		 * Note: This executes safely outside of link_lock because we rely
		 * on the pwrctrl framework to serialize these operations.
		 */
		google_pcie_hold_bus_rpm(gpcie, gpcie->perst_ws, &gpcie->perst_walk_rpm);

		scoped_guard(mutex, &gpcie->link_lock) {
			if (!google_pcie_in_recovery(gpcie)) {
				for_each_pci_bridge(pci_dev, bus)
					pci_save_state(pci_dev);
			}

			ret = __google_pcie_rc_poweroff(gpcie);
			if (ret)
				dev_err(gpcie->dev, "Failed to power off: %d\n", ret);

			/* pwrctrl asserts PERST when power is going away. */
			gpcie->power_ready = false;
		}
	} else {
		scoped_guard(mutex, &gpcie->link_lock) {
			/* pwrctrl deasserts PERST when power is ready. */
			gpcie->power_ready = true;

			ret = __google_pcie_rc_poweron(gpcie);
			if (ret)
				dev_err(gpcie->dev, "Failed to power on: %d\n", ret);
		}

		if (!ret) {
			for_each_pci_bridge(pci_dev, bus) {
				/* restore state saved when powering off */
				pci_restore_state(pci_dev);
				/*
				 * save a good copy of pci state for recovery
				 * The perst path is called when powering on,
				 * we keep the good copy here.
				 */
				pci_save_state(pci_dev);
			}
			scoped_guard(mutex, &gpcie->rpm_walk_lock)
				google_pcie_drop_bus_rpm(gpcie, gpcie->recovery_ws,
							 &gpcie->error_recovery_walk_rpm);
		}

		google_pcie_drop_bus_rpm(gpcie, gpcie->perst_ws, &gpcie->perst_walk_rpm);
	}

	pm_runtime_put(gpcie->dev);

	return ret;
}

static bool google_pcie_is_link_in_l0(struct google_pcie *gpcie)
{
	return google_pcie_get_link_state(gpcie) == L0;
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
	val = dw_pcie_readw_dbi(pci, exp_cap_offset + PCI_EXP_LNKCTL2);
	val &= ~PCI_EXP_LNKCTL2_TLS;
	val |= speed;
	dw_pcie_writew_dbi(pci, exp_cap_offset + PCI_EXP_LNKCTL2, val);

	/* 2. Deassert:  GEN2_CTRL_OFF -> DIRECT_SPEED_CHANGE */
	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	/* 3. Assert:  GEN2_CTRL_OFF -> DIRECT_SPEED_CHANGE */
	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	/* Check the target link speed against current link speed */
	for (i = 0; i < MAX_TIMEOUT_WIDTH_SPEED_CHANGE; i++) {
		linkstat = dw_pcie_readw_dbi(pci, exp_cap_offset + PCI_EXP_LNKSTA);
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
	mutex_unlock(&gpcie->link_lock);
	dev_dbg(gpcie->dev, "%s: link speed changed to %d\n", __func__, speed);

	return 0;
unlock:
	mutex_unlock(&gpcie->link_lock);
	return ret;
}

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
	val = dw_pcie_readl_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL);
	val &= ~PORT_TARGET_LINK_WIDTH_MASK;
	val |= width;
	dw_pcie_writel_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, val);

	/* 2. Deassert the PCIE_PORT_MULTI_LANE_CTRL -> DIRECT_LINK_WIDTH_CHANGE */
	val = dw_pcie_readl_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL);
	val &= ~PORT_DIRECT_LINK_WIDTH_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, val);

	/* 3. Assert the PCIE_PORT_MULTI_LANE_CTRL -> DIRECT_LINK_WIDTH_CHANGE */
	val = dw_pcie_readl_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL);
	val |= PORT_DIRECT_LINK_WIDTH_CHANGE;
	dw_pcie_writel_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, val);

	/* Check the negotiated link width */
	for (i = 0; i < MAX_TIMEOUT_WIDTH_SPEED_CHANGE; i++) {
		linkstat = dw_pcie_readw_dbi(pci, exp_cap_offset + PCI_EXP_LNKSTA);
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

	return 0;
unlock:
	mutex_unlock(&gpcie->link_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(google_pcie_rc_change_link_width);

/*
 * Stub API left for compatibility. Functionality has moved into power
 * management framework, and google_pcie_rc_change_link_speed().
 */
int google_pcie_poweron_withspeed(int num, unsigned int speed)
{
	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_poweron_withspeed);

/*
 * Stub API left for compatibility. Functionality has moved into power
 * management framework.
 */
int google_pcie_rc_poweron(int num)
{
	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_rc_poweron);

/*
 * Stub API left for compatibility. Functionality has moved into power
 * management framework.
 */
int google_pcie_rc_poweroff(int num)
{
	return 0;
}
EXPORT_SYMBOL_GPL(google_pcie_rc_poweroff);

/**
 * google_pcie_register_callback - Register a callback where events will be sent
 *
 * This will register a callback function that will be invoked upon various
 * events like completion timeout, link_down, etc.
 *
 * @num: The domain number representing the PCIe controller in the system
 * @cb_func: Pointer to the callback function which should be registered
 * @priv: Private data that will be passed as-is when the callback function
 *	is invoked
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

	return (google_pcie_get_link_state(gpcie) < L2);
}
EXPORT_SYMBOL_GPL(google_pcie_link_status);

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

static void google_pcie_do_recovery(struct google_pcie *gpcie,
				    enum google_pcie_callback_type cb_type)
{
	struct pci_bus *bus = gpcie->pci->pp.bridge->bus;
	struct pci_dev *pci_dev;
	google_pcie_create_devcoredump(gpcie);

	/*
	 * for completion timeout: issue a hot reset to flush pending
	 * transactions to avoid bus hang.
	 */
	if (cb_type == GPCIE_CB_CPL_TIMEOUT) {
		google_pcie_hot_reset(gpcie);
		dev_info(gpcie->dev, "force hot reset in %s\n", __func__);
	}

	if (gpcie->cb_func) {
		/*
		 * Prevent runtime-suspend and system-suspend of the bus and its
		 * children during recovery.
		 *
		 * NB: this imitates pci_host_handle_link_down() ->
		 * pcie_do_recovery().
		 */
		scoped_guard(mutex, &gpcie->rpm_walk_lock)
			google_pcie_hold_bus_rpm(gpcie, gpcie->recovery_ws,
						 &gpcie->error_recovery_walk_rpm);

		dev_dbg(gpcie->dev, "Invoking registered callback: %d\n",
			cb_type);
		gpcie->cb_func(cb_type, gpcie->cb_priv);

		/*
		 * restore the latest good state for customized cb_func()
		 * Setting state_saved to true is a workaround to re-use the
		 * previous saved one.
		 */
		for_each_pci_bridge(pci_dev, bus) {
			pci_dev->state_saved = true;
			pci_restore_state(pci_dev);
		}
	} else {
		__pm_stay_awake(gpcie->recovery_ws);

		for_each_pci_bridge(pci_dev, bus)
			google_pci_host_handle_link_down(pci_dev);

		__pm_relax(gpcie->recovery_ws);
	}
}

static void google_pcie_cpl_timeout_work_func(struct work_struct *work)
{
	struct google_pcie *gpcie = container_of(work, struct google_pcie, cpl_timeout_work);
	unsigned long flags = 0;

	set_bit(GPCIE_IN_CPL_TIMEOUT, &gpcie->recovery_flags);

	dev_info(gpcie->dev, "Starting cpl_timeout recovery\n");

	spin_lock_irqsave(&gpcie->link_stats_lock, flags);
	gpcie->link_stats.complete_timeout_irq_count++;
	spin_unlock_irqrestore(&gpcie->link_stats_lock, flags);

	google_pcie_do_recovery(gpcie, GPCIE_CB_CPL_TIMEOUT);
	enable_irq(gpcie->cpl_timeout_irq);
}

static irqreturn_t google_pcie_cpl_timeout_handler(int irq, void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;
	dev_dbg(gpcie->dev, "Starting cpl_timeout handler\n");

	disable_irq_nosync(irq);
	queue_work(gpcie->recovery_wq, &gpcie->cpl_timeout_work);
	return IRQ_HANDLED;
}

static int gpcie_log_aer(struct pci_dev *dev, void *unused)
{
	int aer = dev->aer_cap;
	u32 status, mask;
	u16 status16;

	if (!aer)
		return 0;

	pci_read_config_dword(dev, aer + PCI_ERR_COR_STATUS, &status);
	pci_read_config_dword(dev, aer + PCI_ERR_COR_MASK, &mask);

	if (!PCI_POSSIBLE_ERROR(status) && !PCI_POSSIBLE_ERROR(mask) && (status & ~mask)) {
		struct pci_driver *driver;

		dev_info_ratelimited(&dev->dev,
				     "AER correctable error: status=%#010x/mask=%#010x\n",
				     status, mask);

		device_lock(&dev->dev);
		driver = dev->driver;
		if (driver && driver->err_handler && driver->err_handler->cor_error_detected)
			driver->err_handler->cor_error_detected(dev);
		device_unlock(&dev->dev);

		pci_write_config_dword(dev, aer + PCI_ERR_COR_STATUS, status);
	}

	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, &status);
	pci_read_config_dword(dev, aer + PCI_ERR_UNCOR_MASK, &mask);

	if (!PCI_POSSIBLE_ERROR(status) && !PCI_POSSIBLE_ERROR(mask) && (status & ~mask)) {
		dev_info(&dev->dev, "AER uncorrectable error: status=%#010x/mask=%#010x\n",
			 status, mask);
		pci_write_config_dword(dev, aer + PCI_ERR_UNCOR_STATUS, status);
	}

	/* Clear device status */
	pcie_capability_read_word(dev, PCI_EXP_DEVSTA, &status16);
	pcie_capability_write_word(dev, PCI_EXP_DEVSTA, status16);

	return 0;
}

static void google_pcie_aggr_err_work_func(struct work_struct *work)
{
	struct google_pcie *gpcie = container_of(work, struct google_pcie, aggr_err_work);
	struct pci_bus *bus = gpcie->pci->pp.bridge->bus;
	struct pci_dev *pci_dev;
	u32 val;

	dev_dbg(gpcie->dev, "Starting NF/F/CORR recovery\n");

	val = readl(gpcie->sii_base + PE_ERR_INT_STS);
	writel(val, gpcie->sii_base + PE_ERR_INT_STS);

	if (!(val & (SEND_NF_ERR_STS | SEND_F_ERR_STS | SEND_CORR_ERR_STS))) {
		dev_warn(gpcie->dev, "Missing NF/F/CORR status: %#010x\n", val);
		goto out;
	}

	if (val & SEND_NF_ERR_STS) {
		val = dw_pcie_readl_dbi(gpcie->pci, gpcie->aer + PCI_ERR_UNCOR_STATUS);
		if (val & PCI_ERR_UNC_COMP_TIME) {
			/*
			 * Fallback: On some platforms (like Malibu), there is no dedicated
			 * CPL timeout interrupt, and it is routed through the aggregate
			 * error interrupt instead.
			 */
			dev_info(gpcie->dev, "Starting cpl_timeout recovery\n");
			set_bit(GPCIE_IN_CPL_TIMEOUT, &gpcie->recovery_flags);
			google_pcie_do_recovery(gpcie, GPCIE_CB_CPL_TIMEOUT);
		}
	}

	for_each_pci_bridge(pci_dev, bus) {
		gpcie_log_aer(pci_dev, NULL);
		pci_walk_bus(bus, gpcie_log_aer, NULL);
	}
out:
	enable_irq(gpcie->aggr_err_irq);
}

static irqreturn_t google_pcie_aggr_err_int_handler(int irq, void *data)
{
	u32 val;
	struct google_pcie *gpcie = (struct google_pcie *)data;

	val = readl(gpcie->sii_base + PE_ERR_INT_STS);

	if (!(val & (SEND_NF_ERR_STS | SEND_F_ERR_STS | SEND_CORR_ERR_STS)))
		return IRQ_NONE;

	dev_dbg(gpcie->dev, "Starting NF/F/CORR handler\n");

	disable_irq_nosync(irq);
	queue_work(gpcie->recovery_wq, &gpcie->aggr_err_work);
	return IRQ_HANDLED;
}

static void google_pcie_link_down_work_func(struct work_struct *work)
{
	struct google_pcie *gpcie = container_of(work, struct google_pcie, link_down_work);
	unsigned long flags = 0;

	dev_info(gpcie->dev, "Starting link_down recovery\n");

	spin_lock_irqsave(&gpcie->link_up_lock, flags);
	if (!gpcie->is_link_up) {
		dev_warn(gpcie->dev, "Unexpected link down event when link is not up\n");
		spin_unlock_irqrestore(&gpcie->link_up_lock, flags);
		enable_irq(gpcie->link_down_irq);
		return;
	}
	spin_unlock_irqrestore(&gpcie->link_up_lock, flags);

	set_bit(GPCIE_IN_LINK_DOWN, &gpcie->recovery_flags);

	spin_lock_irqsave(&gpcie->link_stats_lock, flags);
	gpcie->link_stats.link_down_irq_count++;
	spin_unlock_irqrestore(&gpcie->link_stats_lock, flags);

	google_pcie_do_recovery(gpcie, GPCIE_CB_LINK_DOWN);
	enable_irq(gpcie->link_down_irq);
}

static irqreturn_t google_pcie_link_down_handler(int irq, void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;
	dev_info(gpcie->dev, "Starting link_down handler\n");

	disable_irq_nosync(irq);
	queue_work(gpcie->recovery_wq, &gpcie->link_down_work);
	return IRQ_HANDLED;
}

static const char * const link_state_names[] = {
	[L0] = "L0",
	[RECOVERY] = "REC",
	[L0S] = "L0s",
	[L1] = "L1.0",
	[L11] = "L1.1",
	[L12] = "L1.2",
	[L2] = "L2",
	[UNKNOWN] = "OTHERS",
};

static ssize_t link_state_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	u32 link_state;
	int ret;

	link_state = google_pcie_get_link_state(gpcie);
	ret = scnprintf(buf, PAGE_SIZE, "%s\n", link_state_names[link_state]);

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

static ssize_t power_stats_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	struct google_pcie_power_stats link_up_copy;
	struct google_pcie_power_stats link_down_copy;
	unsigned long flags;
	u64 current_ts;
	u64 link_up_delta = 0;
	u64 link_down_delta = 0;
	int ret;

	spin_lock_irqsave(&gpcie->power_stats_lock, flags);
	static_assert(sizeof(link_up_copy) == sizeof(gpcie->link_up));
	memcpy(&link_up_copy, &gpcie->link_up, sizeof(gpcie->link_up));
	static_assert(sizeof(link_down_copy) == sizeof(gpcie->link_down));
	memcpy(&link_down_copy, &gpcie->link_down, sizeof(gpcie->link_down));
	spin_unlock_irqrestore(&gpcie->power_stats_lock, flags);

	current_ts = power_stats_get_ts();

	if (gpcie->is_link_up)
		link_up_delta = current_ts - link_up_copy.last_entry_ms;
	else
		link_down_delta = current_ts - link_down_copy.last_entry_ms;

	ret = scnprintf(buf, PAGE_SIZE, "Version: 1\n");

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			 "Link up:\n  Cumulative count: 0x%llx\n"
			 "  Cumulative duration msec: 0x%llx\n"
			 "  Last entry timestamp msec: 0x%llx\n",
			 link_up_copy.count,
			 link_up_copy.duration + link_up_delta,
			 link_up_copy.last_entry_ms);

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			 "Link down:\n  Cumulative count: 0x%llx\n"
			 "  Cumulative duration msec: 0x%llx\n"
			 "  Last entry timestamp msec: 0x%llx\n",
			 link_down_copy.count,
			 link_down_copy.duration + link_down_delta,
			 link_down_copy.last_entry_ms);

	return ret;
}

#define SHOW_FIELD(_name)									\
static ssize_t _name##_show(struct device *dev, struct device_attribute *attr, char *buf)	\
{												\
	struct google_pcie *gpcie = dev_get_drvdata(dev);					\
	int ret;										\
												\
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", gpcie->_name);					\
	return ret;										\
}

#define STORE_FIELD(_name)									\
static ssize_t _name##_store(struct device *dev, struct device_attribute *attr,			\
			       const char *buf, size_t count)					\
{												\
	struct google_pcie *gpcie = dev_get_drvdata(dev);					\
												\
	if (kstrtobool(buf, &gpcie->_name) < 0)							\
		return -EINVAL;									\
												\
	return count;										\
}

static DEVICE_ATTR_RO(link_state);
static DEVICE_ATTR_RO(power_stats);

static struct attribute *gpcie_device_attrs[] = {
	&dev_attr_link_state.attr,
	&dev_attr_power_stats.attr,
	NULL,
};

static const struct attribute_group gpcie_device_group = {
	.attrs = gpcie_device_attrs,
};

/* Helper for showing link stats */
static ssize_t pcie_link_stats_show_helper(struct device *dev,
					  struct device_attribute *attr,
					  char *buf, u64 *stat, u64 *reported)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	u64 delta;

	scoped_guard(spinlock_irqsave, &gpcie->link_stats_lock) {
		delta = *stat - *reported;
	}

	return sysfs_emit(buf, "%llu\n", delta);
}

/* Helper for storing (clearing/reporting) link stats */
static ssize_t pcie_link_stats_store_helper(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count,
					   u64 *stat, u64 *reported)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	u64 value, delta;

	if (kstrtoull(buf, 10, &value) < 0)
		return -EINVAL;

	scoped_guard(spinlock_irqsave, &gpcie->link_stats_lock) {
		delta = *stat - *reported;
		if (value > delta) {
			dev_info(dev, "Value needs to be <= %llu\n", delta);
			return -EINVAL;
		}
		*reported += value;
	}

	return count;
}

#define LINK_STATS_ATTR_RW(_name) \
static ssize_t _name##s_show(struct device *dev, \
			    struct device_attribute *attr, char *buf) \
{ \
	struct google_pcie *gpcie = dev_get_drvdata(dev); \
	return pcie_link_stats_show_helper(dev, attr, buf, \
					   &gpcie->link_stats._name##_count, \
					   &gpcie->link_stats_reported._name##_count); \
} \
static ssize_t _name##s_store(struct device *dev, \
			     struct device_attribute *attr, \
			     const char *buf, size_t count) \
{ \
	struct google_pcie *gpcie = dev_get_drvdata(dev); \
	return pcie_link_stats_store_helper(dev, attr, buf, count, \
					    &gpcie->link_stats._name##_count, \
					    &gpcie->link_stats_reported._name##_count); \
} \
static DEVICE_ATTR_RW(_name##s)

LINK_STATS_ATTR_RW(link_down_irq);
LINK_STATS_ATTR_RW(complete_timeout_irq);
LINK_STATS_ATTR_RW(link_up_failure);
LINK_STATS_ATTR_RW(link_recovery_failure);

static struct attribute *link_stats_attrs[] = {
	&dev_attr_link_down_irqs.attr,
	&dev_attr_complete_timeout_irqs.attr,
	&dev_attr_link_up_failures.attr,
	&dev_attr_link_recovery_failures.attr,
	NULL,
};

static const struct attribute_group link_stats_group = {
	.attrs = link_stats_attrs,
	.name = "link_stats",
};

static const struct attribute_group *gpcie_device_groups[] = {
	&gpcie_device_group,
	&link_stats_group,
	NULL,
};

static int google_pcie_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	u32 reg;
	struct google_pcie *gpcie = container_of(nb, struct google_pcie, top_nb);

	switch (action) {
	case GENPD_NOTIFY_OFF:
		dev_dbg(gpcie->dev, "PCIe controller OFF\n");
		break;
	case GENPD_NOTIFY_ON:
		dev_dbg(gpcie->dev, "PCIe controller ON\n");
		return regmap_read_poll_timeout(gpcie->top_psm_state, 0, reg,
						PSM_READY(reg) && PSM_STATE_LE(reg, 1),
						TOP_PSM_SLEEP_US, TOP_PSM_TIMEOUT_US);
	default:
		break;
	}
	return 0;
}

static void google_pcie_init_genpd(struct google_pcie *gpcie)
{
	struct device *dev = gpcie->dev;

	gpcie->top_nb.notifier_call = google_pcie_notifier;
	dev_pm_genpd_add_notifier(dev, &gpcie->top_nb);
}

static void google_pcie_destroy_wq(void *data)
{
	destroy_workqueue(data);
}

static void google_pcie_override_portdrv(struct google_pcie *gpcie)
{
	struct pci_bus *bus = gpcie->pci->pp.bridge->bus;
	struct pci_dev *pci_dev;

	for_each_pci_bridge(pci_dev, bus) {
		/*
		 * portdrv.c configures NO_DIRECT_COMPLETE and SMART_SUSPEND.
		 * We want to override with SMART_SUSPEND and MAY_SKIP_RESUME.
		 * Warn if the underlying configuration doesn't match what we
		 * expect, in case the kernel did something new we weren't
		 * expecting.
		 */
		WARN_ON_ONCE(!dev_pm_test_driver_flags(&pci_dev->dev, DPM_FLAG_NO_DIRECT_COMPLETE));
		WARN_ON_ONCE(!dev_pm_test_driver_flags(&pci_dev->dev, DPM_FLAG_SMART_SUSPEND));
		WARN_ON_ONCE(dev_pm_test_driver_flags(&pci_dev->dev, DPM_FLAG_MAY_SKIP_RESUME));

		dev_pm_set_driver_flags(&pci_dev->dev, DPM_FLAG_SMART_SUSPEND | DPM_FLAG_MAY_SKIP_RESUME);
	}
}

static int google_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct google_pcie *gpcie;
	struct dw_pcie *pci;
	struct resource *sii_res;
	struct resource *top_res;
	int ret;
	u32 reg;
	char *ws_name;

	gpcie = devm_kzalloc(dev, sizeof(*gpcie), GFP_KERNEL);
	if (!gpcie)
		return -ENOMEM;

	gpcie->phy = devm_phy_get(dev, "phy");
	if (IS_ERR(gpcie->phy))
		return dev_err_probe(dev, PTR_ERR(gpcie->phy), "Failed to get PHY\n");

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

	sii_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sii");
	if (!sii_res)
		return -EINVAL;

	gpcie->sii_base = devm_ioremap_resource(dev, sii_res);
	if (IS_ERR(gpcie->sii_base))
		return PTR_ERR(gpcie->sii_base);

	gpcie->sii_size = resource_size(sii_res);

	gpcie->ctrl_psm_state = devm_platform_ioremap_resource_byname(pdev, "psm_state");
	if (IS_ERR(gpcie->ctrl_psm_state))
		return PTR_ERR(gpcie->ctrl_psm_state);

	gpcie->subsystem_general = syscon_regmap_lookup_by_phandle_args(np,
			"google,pcie-subsystem-general", 0, NULL);
	if (IS_ERR(gpcie->subsystem_general))
		return PTR_ERR(gpcie->subsystem_general);

	gpcie->top_psm_state = syscon_regmap_lookup_by_phandle_args(np, "top-psm-state", 0, NULL);
	if (IS_ERR(gpcie->top_psm_state))
		return PTR_ERR(gpcie->top_psm_state);

	pci->dev = dev;
	pci->ops = &google_dw_pcie_ops;
	pci->pp.ops = &google_pcie_host_ops;
	gpcie->dev = dev;
	gpcie->pci = pci;

	if (of_property_read_u32(np, "google,perst-delay-us", &gpcie->perst_delay_us)) {
		gpcie->perst_delay_us = DEFAULT_PERST_DELAY_US;
		dev_info(dev, "PERST delay is NOT defined...default to %u ms\n",
			 gpcie->perst_delay_us / 1000);
	}

	platform_set_drvdata(pdev, gpcie);

	gpcie->perstn_gpio = devm_gpiod_get(dev, "perstn", GPIOD_OUT_HIGH);
	if (IS_ERR(gpcie->perstn_gpio)) {
		dev_err(dev, "Failed to request perstn GPIO\n");
		return -EINVAL;
	}

	/*
	 * Use a private ordered workqueue to serialize recovery tasks and prevent
	 * AB-BA deadlocks. This path is strictly for error handling and does not
	 * impact normal suspend/resume performance.
	 */
	gpcie->recovery_wq = alloc_ordered_workqueue("gpcie_recovery_%s",
						     WQ_MEM_RECLAIM | WQ_FREEZABLE, dev_name(dev));
	if (!gpcie->recovery_wq)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, google_pcie_destroy_wq, gpcie->recovery_wq);
	if (ret)
		return ret;

	/*
	 * Note: These are dedicated platform interrupts and are
	 * not shared. Masking them via disable_irq_nosync() in the handler is safe.
	 */
	INIT_WORK(&gpcie->cpl_timeout_work, google_pcie_cpl_timeout_work_func);
	INIT_WORK(&gpcie->link_down_work, google_pcie_link_down_work_func);
	INIT_WORK(&gpcie->aggr_err_work, google_pcie_aggr_err_work_func);

	scnprintf(gpcie->cpl_timeout_irqname, sizeof(gpcie->cpl_timeout_irqname),
		  "%s_cpl_timeout", dev_name(dev));
	gpcie->cpl_timeout_irq = platform_get_irq_byname_optional(pdev, "cpl_timeout");
	if (gpcie->cpl_timeout_irq > 0) {
		ret = devm_request_irq(dev, gpcie->cpl_timeout_irq, google_pcie_cpl_timeout_handler,
				       0, gpcie->cpl_timeout_irqname, gpcie);
		if (ret) {
			dev_err(dev, "Failed to request cpl_timeout interrupt %d\n", ret);
			return ret;
		}
	} else {
		scnprintf(gpcie->aggr_err_irqname, sizeof(gpcie->aggr_err_irqname),
			  "%s_aggr_err", dev_name(dev));
		gpcie->aggr_err_irq = platform_get_irq_byname(pdev, "aggr_err");
		if (gpcie->aggr_err_irq < 0)
			return -EINVAL;
		ret = devm_request_irq(dev, gpcie->aggr_err_irq, google_pcie_aggr_err_int_handler,
				       IRQF_NO_AUTOEN, gpcie->aggr_err_irqname, gpcie);
		if (ret) {
			dev_err(dev, "Failed to request aggregated interrupt %d\n", ret);
			return ret;
		}
	}

	scnprintf(gpcie->link_down_irqname, sizeof(gpcie->link_down_irqname),
		  "%s_link_down", dev_name(dev));
	gpcie->link_down_irq = platform_get_irq_byname(pdev, "link_down");
	if (gpcie->link_down_irq < 0)
		return -EINVAL;
	ret = devm_request_irq(dev, gpcie->link_down_irq, google_pcie_link_down_handler, 0,
			       gpcie->link_down_irqname, gpcie);
	if (ret) {
		dev_err(dev, "Failed to request link_down interrupt %d\n", ret);
		return ret;
	}

	gpcie->l1_pwrgate_disable = device_property_read_bool(dev, "google,l1-pwrgate-disable");

	gpcie->skip_link_eq = device_property_read_bool(dev, "google,skip-link-equalization");

	ret = regmap_read_poll_timeout(gpcie->top_psm_state, 0, reg,
				       PSM_READY(reg) && PSM_STATE_LE(reg, 1),
				       TOP_PSM_SLEEP_US, TOP_PSM_TIMEOUT_US);
	if (ret)
		return ret;

	gpcie->allow_suspend_in_linkup =
		device_property_read_bool(dev, "google,allow-suspend-in-linkup");

	if (gpcie->allow_suspend_in_linkup)
		device_set_wakeup_capable(dev, true);

	ret = pinctrl_pm_select_idle_state(dev);
	if (ret)
		dev_err(dev, "Failed to set CLKREQ idle: %d\n", ret);

	of_property_read_u8(np, "google,link-timeout-ms", &gpcie->link_timeout_ms);

	ws_name = devm_kasprintf(dev, GFP_KERNEL, "perst_ws_%s",
			dev_name(dev));
	if (!ws_name) {
		ret = -ENOMEM;
		goto wakeup_disable;
	}
	gpcie->perst_ws = wakeup_source_register(dev, ws_name);
	if (!gpcie->perst_ws) {
		ret = -ENOMEM;
		goto wakeup_disable;
	}

	ws_name = devm_kasprintf(dev, GFP_KERNEL, "recovery_ws_%s",
			dev_name(dev));
	if (!ws_name) {
		ret = -ENOMEM;
		goto unregister_perst_wakeup_source;
	}
	gpcie->recovery_ws = wakeup_source_register(dev, ws_name);
	if (!gpcie->recovery_ws) {
		ret = -ENOMEM;
		goto unregister_perst_wakeup_source;
	}

	spin_lock_init(&gpcie->power_on_lock);
	spin_lock_init(&gpcie->power_stats_lock);
	spin_lock_init(&gpcie->link_up_lock);
	spin_lock_init(&gpcie->link_stats_lock);
	ret = devm_mutex_init(dev, &gpcie->link_lock);
	if (ret)
		goto unregister_recovery_wakeup_source;

	ret = devm_mutex_init(dev, &gpcie->rpm_walk_lock);
	if (ret)
		goto unregister_recovery_wakeup_source;

	power_stats_init(gpcie);

	/* We probe while powered up. */
	gpcie->powered_on = true;

	/* Tell dw_pcie_host_init() to skip waiting for the link to come up. */
	pci->pp.use_linkup_irq = true;

	ret = dw_pcie_host_init(&pci->pp);
	if (ret) {
		dev_err(dev, "Failed to initialize host\n");
		goto unregister_recovery_wakeup_source;
	}

	google_pcie_override_portdrv(gpcie);

	ret = google_pcie_init_debugfs(gpcie);
	if (ret)
		goto host_deinit;

	google_pcie_init_devcoredump(gpcie);

	ret = devm_add_action_or_reset(dev, google_pcie_exit_debugfs, gpcie);
	if (ret)
		goto host_deinit;

	google_pcie_init_genpd(gpcie);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	device_enable_async_suspend(gpcie->dev);

	return 0;

host_deinit:
	dw_pcie_host_deinit(&pci->pp);
unregister_recovery_wakeup_source:
	wakeup_source_unregister(gpcie->recovery_ws);
unregister_perst_wakeup_source:
	wakeup_source_unregister(gpcie->perst_ws);
wakeup_disable:
	device_set_wakeup_capable(dev, false);

	return ret;
}

static void google_pcie_remove(struct platform_device *pdev)
{
	struct google_pcie *gpcie = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	/* 1. Disable IRQs to stop new recovery work from being queued */
	if (gpcie->cpl_timeout_irq > 0)
		disable_irq(gpcie->cpl_timeout_irq);
	if (gpcie->aggr_err_irq > 0)
		disable_irq(gpcie->aggr_err_irq);
	if (gpcie->link_down_irq > 0)
		disable_irq(gpcie->link_down_irq);

	/* 2. Flush the recovery workqueue. */
	if (gpcie->recovery_wq)
		flush_workqueue(gpcie->recovery_wq);

	/* 3. PCIe Hardware and PM de-initialization */
	__pm_stay_awake(gpcie->perst_ws);
	pm_runtime_get_sync(dev);
	dw_pcie_host_deinit(&gpcie->pci->pp);
	dev_pm_genpd_remove_notifier(dev);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);
	__pm_relax(gpcie->perst_ws);
	device_set_wakeup_capable(dev, false);
	wakeup_source_unregister(gpcie->perst_ws);
	wakeup_source_unregister(gpcie->recovery_ws);
}

static const struct of_device_id google_pcie_of_match[] = {
	{
		.compatible = "google,malibu-pcie",
	},
	{},
};
MODULE_DEVICE_TABLE(of, google_pcie_of_match);

static int google_pcie_runtime_suspend(struct device *dev)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	unsigned long flags;
	int ret;

	trace_pci_suspend_start(dev);

	guard(mutex)(&gpcie->link_lock);

	if (gpcie->power_ready) {
		ret = __google_pcie_rc_poweroff(gpcie);
		if (ret) {
			dev_err(dev, "Failed to power off: %d\n", ret);
			goto err;
		}
	}

	/*
	 * After this point, the controller will be powered down.
	 * Thereby, pcie registers are not accessible.
	 * Therefore, mark pcie power off unconditionally.
	 */
	spin_lock_irqsave(&gpcie->power_on_lock, flags);
	gpcie->powered_on = false;
	spin_unlock_irqrestore(&gpcie->power_on_lock, flags);

err:
	trace_pci_suspend_end(dev);

	return 0;
}

static int google_pcie_suspend(struct device *dev)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	struct pci_bus *bus = gpcie->pci->pp.bridge->bus;
	struct pci_dev *pci_dev;
	bool port_suspend = true;
	int ret;

	for_each_pci_bridge(pci_dev, bus) {
		if (pci_dev->current_state < PCI_D3hot)
			port_suspend = false;
		if (pci_dev->skip_bus_pm)
			port_suspend = false;
	}

	if (!port_suspend && gpcie->allow_suspend_in_linkup) {
		dev_dbg(dev, "Keep link on in suspend\n");
		return 0;
	}

	if (WARN(!port_suspend, "%s: tried to suspend with link on\n", dev_name(dev)))
		return -EINVAL;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		return ret;

	/*
	 * We use the controller's "may wakeup" property to control whether
	 * PCIe remains fully powered in system suspend. However, our children
	 * may claim they need to wake up in suspend, and when
	 * pm_suspend_ignore_children() is false, that propagates to the
	 * controller, even though this can be achieved without PCIe power
	 * (e.g., PEWAKE#).
	 *
	 * Force device_wakeup_path() back to false if we don't intend to
	 * retain PCIe power. This is a bit of a hack, because we don't really
	 * differentiate the two types of "may wakeup" use cases.
	 */
	if (!device_may_wakeup(dev))
		dev->power.wakeup_path = false;

	return 0;
}

static int google_pcie_runtime_resume(struct device *dev)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);
	int ret = 0;

	trace_pci_resume_start(dev);

	guard(mutex)(&gpcie->link_lock);

	gpcie->powered_on = true;

	if (gpcie->power_ready) {
		ret = __google_pcie_rc_poweron(gpcie);
		if (ret) {
			dev_err(dev, "Failed to power on: %d\n", ret);
			goto err;
		}
	}

	trace_pci_resume_end(dev);

err:
	return ret;
}

static int google_pcie_resume(struct device *dev)
{
	struct google_pcie *gpcie = dev_get_drvdata(dev);

	if (gpcie->is_link_up)
		return 0;

	return pm_runtime_force_resume(dev);
}

/*
 * Extend pci-pwrctrl, because the upstream framework has not yet integrated
 * PERST and link-training-retry.
 */
#undef pci_pwrctrl_init
#undef pci_pwrctrl_device_set_ready
#undef pci_pwrctrl_device_unset_ready
#undef devm_pci_pwrctrl_device_set_ready
void google_pci_pwrctrl_init(struct pci_pwrctrl *pwrctrl, struct device *dev)
{
	pci_pwrctrl_init(pwrctrl, dev);
}
EXPORT_SYMBOL_GPL(google_pci_pwrctrl_init);

int google_pci_pwrctrl_device_set_ready(struct pci_pwrctrl *pwrctrl)
{
	struct pci_host_bridge *bridge = to_pci_host_bridge(pwrctrl->dev->parent);
	struct google_pcie *gpcie = bridge_to_gpcie(bridge);
	int ret;

	ret = google_pcie_perst_assert(gpcie, false);
	if (ret) {
		dev_err(gpcie->dev, "Failed to deassert PERST: %d\n", ret);
		return ret;
	}

	ret = pci_pwrctrl_device_set_ready(pwrctrl);
	if (ret) {
		google_pcie_perst_assert(gpcie, true);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(google_pci_pwrctrl_device_set_ready);

void google_pci_pwrctrl_device_unset_ready(struct pci_pwrctrl *pwrctrl)
{
	struct pci_host_bridge *bridge = to_pci_host_bridge(pwrctrl->dev->parent);
	struct google_pcie *gpcie = bridge_to_gpcie(bridge);
	int ret;

	ret = google_pcie_perst_assert(gpcie, true);
	if (ret)
		dev_warn(gpcie->dev, "Failed to assert PERST: %d\n", ret);

	pci_pwrctrl_device_unset_ready(pwrctrl);
}
EXPORT_SYMBOL_GPL(google_pci_pwrctrl_device_unset_ready);

static void devm_google_pci_pwrctrl_device_unset_ready(void *data)
{
	struct pci_pwrctrl *pwrctrl = data;

	pci_pwrctrl_device_unset_ready(pwrctrl);
}

int devm_google_pci_pwrctrl_device_set_ready(struct device *dev,
					     struct pci_pwrctrl *pwrctrl)
{
	int ret;

	ret = google_pci_pwrctrl_device_set_ready(pwrctrl);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev,
					devm_google_pci_pwrctrl_device_unset_ready,
					pwrctrl);
}
EXPORT_SYMBOL_GPL(devm_google_pci_pwrctrl_device_set_ready);

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
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(google_pcie_suspend, google_pcie_resume)
	SET_RUNTIME_PM_OPS(google_pcie_runtime_suspend, google_pcie_runtime_resume,
			   NULL)
};

static struct platform_driver google_pcie_driver = {
	.driver = {
		.name	= "google-pcie",
		.of_match_table = google_pcie_of_match,
		.pm = &google_pcie_dev_pm_ops,
		.dev_groups = gpcie_device_groups,
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

struct google_pcie_dev_platdata {
	pci_power_t state;
};

static struct google_pcie_dev_platdata *
google_pcie_dev_get_platdata(struct google_pcie *gpcie, struct pci_dev *pdev)
{
	/*
	 * PCI devices don't get |platform_data| from any other source today.
	 * We borrow this field for our platform PM state for now.
	 */
	struct google_pcie_dev_platdata *pdata = dev_get_platdata(&pdev->dev);

	if (pdata)
		return pdata;

	pdata = devm_kzalloc(gpcie->dev, sizeof(*pdata), GFP_ATOMIC);
	if (!pdata)
		return NULL;

	pdev->dev.platform_data = pdata;
	pdata->state = PCI_D0;

	return pdata;
}

static void google_pcie_power_manageable(void *data, struct pci_dev *pci_dev,
					 bool *manageable)
{
	struct google_pcie *gpcie = pci_to_gpcie(pci_dev);

	if (!gpcie)
		return;

	*manageable = true;
}

static void google_pcie_set_power_state(void *data, struct pci_dev *pci_dev,
					pci_power_t t, int *ret)
{
	struct google_pcie *gpcie = pci_to_gpcie(pci_dev);
	struct google_pcie_dev_platdata *pdata;

	if (!gpcie)
		return;

	pdata = google_pcie_dev_get_platdata(gpcie, pci_dev);
	if (!pdata) {
		*ret = -ENOMEM;
		return;
	}

	/*
	 * We don't actually "set" the state here. We just wait for the bridge
	 * to suspend, so we transition to L2/L3. We only track the state
	 * transitions, so we can feed the state back to the PCI core.
	 */
	pdata->state = t;
	*ret = 0;
}

static void google_pcie_get_power_state(void *data, struct pci_dev *pci_dev,
					pci_power_t *state)
{
	struct google_pcie *gpcie = pci_to_gpcie(pci_dev);
	struct google_pcie_dev_platdata *pdata;

	if (!gpcie)
		return;

	pdata = google_pcie_dev_get_platdata(gpcie, pci_dev);
	if (!pdata)
		return;

	*state = pdata->state;
}

/*
 * For choosing a low-power state to aim for when suspending a device.
 *
 * We're going to put the link in L2. Everyone will be nonresponsive, so D3cold
 * is the best target.
 */
static void google_pcie_choose_state(void *data, struct pci_dev *pci_dev,
				     pci_power_t *state)
{
	struct google_pcie *gpcie = pci_to_gpcie(pci_dev);

	if (!gpcie)
		return;

	/* We account for any L2/D3cold resume delays elsewhere. */
	pci_dev->d3cold_delay = 0;
	*state = PCI_D3cold;
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
	ret = register_trace_android_vh_platform_pci_set_power_state(
			google_pcie_set_power_state, NULL);
	if (ret)
		return ret;
	ret = register_trace_android_vh_platform_pci_get_power_state(
			google_pcie_get_power_state, NULL);
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
	unregister_trace_android_vh_platform_pci_get_power_state(
			google_pcie_get_power_state, NULL);
	unregister_trace_android_vh_platform_pci_set_power_state(
			google_pcie_set_power_state, NULL);
	unregister_trace_android_vh_platform_pci_power_manageable(
			google_pcie_power_manageable, NULL);
}

module_init(google_pcie_init);
module_exit(google_pcie_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google PCIe V2 Driver");
MODULE_LICENSE("GPL");
