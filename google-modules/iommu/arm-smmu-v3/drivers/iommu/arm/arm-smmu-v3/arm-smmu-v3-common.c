// SPDX-License-Identifier: GPL-2.0
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/string_choices.h>

#include "arm-smmu-v3.h"
/* This header is in ACK under drivers/iommu. */
#include <drivers/iommu/dma-iommu.h>

struct arm_smmu_option_prop {
	u32 opt;
	const char *prop;
};

static struct arm_smmu_option_prop arm_smmu_options[] = {
	{ ARM_SMMU_OPT_SKIP_PREFETCH, "hisilicon,broken-prefetch-cmd" },
	{ ARM_SMMU_OPT_PAGE0_REGS_ONLY, "cavium,cn9900-broken-page1-regspace"},
	{ ARM_SMMU_OPT_OVR_INSTCFG_DATA, "arm,instdata-override"},
	{ ARM_SMMU_OPT_RPM_DISABLE, "disable-runtime-pm"},
	{ ARM_SMMU_OPT_NON_COHERENT_TTW, "non-coherent-ttw"},
	{ ARM_SMMU_OPT_SYNC_FW, "synchronize-fw"},
	{ ARM_SMMU_OPT_NON_COHERENT_S2_TTW, "non-coherent-s2-ttw"},
	{ 0, NULL},
};

static phys_addr_t arm_smmu_msi_cfg[ARM_SMMU_MAX_MSIS][3] = {
	[EVTQ_MSI_INDEX] = {
		ARM_SMMU_EVTQ_IRQ_CFG0,
		ARM_SMMU_EVTQ_IRQ_CFG1,
		ARM_SMMU_EVTQ_IRQ_CFG2,
	},
	[GERROR_MSI_INDEX] = {
		ARM_SMMU_GERROR_IRQ_CFG0,
		ARM_SMMU_GERROR_IRQ_CFG1,
		ARM_SMMU_GERROR_IRQ_CFG2,
	},
	[PRIQ_MSI_INDEX] = {
		ARM_SMMU_PRIQ_IRQ_CFG0,
		ARM_SMMU_PRIQ_IRQ_CFG1,
		ARM_SMMU_PRIQ_IRQ_CFG2,
	},
};

static const char * const event_str[] = {
	[EVT_ID_BAD_STREAMID_CONFIG] = "C_BAD_STREAMID",
	[EVT_ID_STE_FETCH_FAULT] = "F_STE_FETCH",
	[EVT_ID_BAD_STE_CONFIG] = "C_BAD_STE",
	[EVT_ID_STREAM_DISABLED_FAULT] = "F_STREAM_DISABLED",
	[EVT_ID_BAD_SUBSTREAMID_CONFIG] = "C_BAD_SUBSTREAMID",
	[EVT_ID_CD_FETCH_FAULT] = "F_CD_FETCH",
	[EVT_ID_BAD_CD_CONFIG] = "C_BAD_CD",
	[EVT_ID_TRANSLATION_FAULT] = "F_TRANSLATION",
	[EVT_ID_ADDR_SIZE_FAULT] = "F_ADDR_SIZE",
	[EVT_ID_ACCESS_FAULT] = "F_ACCESS",
	[EVT_ID_PERMISSION_FAULT] = "F_PERMISSION",
	[EVT_ID_VMS_FETCH_FAULT] = "F_VMS_FETCH",
};

static const char * const event_class_str[] = {
	[0] = "CD fetch",
	[1] = "Stage 1 translation table fetch",
	[2] = "Input address caused fault",
	[3] = "Reserved",
};

static void parse_driver_options(struct arm_smmu_device *smmu)
{
	int i = 0;

	do {
		if (of_property_read_bool(smmu->dev->of_node,
						arm_smmu_options[i].prop)) {
			smmu->options |= arm_smmu_options[i].opt;
			dev_notice(smmu->dev, "option %s\n",
				arm_smmu_options[i].prop);
		}
	} while (arm_smmu_options[++i].opt);
}

static struct pci_bus *arm_smmu_find_pci_root_bus(struct pci_bus *bus)
{
	while (bus->parent)
		bus = bus->parent;

	return bus;
}

struct device *arm_smmu_pci_get_host_bridge_device(struct pci_dev *dev)
{
	struct pci_bus *root_bus = arm_smmu_find_pci_root_bus(dev->bus);
	struct device *bridge = root_bus->bridge;

	kobject_get(&bridge->kobj);
	return bridge;
}

void arm_smmu_pci_put_host_bridge_device(struct device *dev)
{
	kobject_put(&dev->kobj);
}

static void arm_smmu_dt_adjust_sid_bits(struct arm_smmu_device *smmu)
{
	struct device *dev = smmu->dev;
	u32 max_sid_bits = U32_MAX;

	of_property_read_u32(dev->of_node, "arm,max-sid-bits", &max_sid_bits);

	if (smmu->sid_bits > max_sid_bits) {
		dev_dbg(dev,
			"Reducing width of SID from %u to %u bits as per device tree configuration\n",
			smmu->sid_bits, max_sid_bits);
		smmu->sid_bits = max_sid_bits;
	}
}

static void arm_smmu_dump_raw_event(struct arm_smmu_device *smmu, u64 *raw,
				    struct arm_smmu_event *event)
{
	int i;

	dev_err(smmu->dev, "event 0x%02x received:\n", event->id);

	for (i = 0; i < EVTQ_ENT_DWORDS; ++i)
		dev_err(smmu->dev, "\t0x%016llx\n", raw[i]);
}

#define ARM_SMMU_EVT_KNOWN(e)	((e)->id < ARRAY_SIZE(event_str) && event_str[(e)->id])
#define ARM_SMMU_LOG_EVT_STR(e) ARM_SMMU_EVT_KNOWN(e) ? event_str[(e)->id] : "UNKNOWN"
#define ARM_SMMU_LOG_CLIENT(e)	(e)->dev ? dev_name((e)->dev) : "(unassigned sid)"

void arm_smmu_dump_event(struct arm_smmu_device *smmu, u64 *raw,
			 struct arm_smmu_event *evt,
			 struct ratelimit_state *rs)
{
	if (!__ratelimit(rs))
		return;

	arm_smmu_dump_raw_event(smmu, raw, evt);

	switch (evt->id) {
	case EVT_ID_TRANSLATION_FAULT:
	case EVT_ID_ADDR_SIZE_FAULT:
	case EVT_ID_ACCESS_FAULT:
	case EVT_ID_PERMISSION_FAULT:
		dev_err(smmu->dev, "event: %s client: %s sid: %#x ssid: %#x iova: %#llx ipa: %#llx",
			ARM_SMMU_LOG_EVT_STR(evt), ARM_SMMU_LOG_CLIENT(evt),
			evt->sid, evt->ssid, evt->iova, evt->ipa);

		dev_err(smmu->dev, "%s %s %s %s \"%s\"%s%s stag: %#x",
			evt->privileged ? "priv" : "unpriv",
			evt->instruction ? "inst" : "data",
			str_read_write(evt->read),
			evt->s2 ? "s2" : "s1", event_class_str[evt->class],
			evt->class_tt ? (evt->ttrnw ? " ttd_read" : " ttd_write") : "",
			evt->stall ? " stall" : "", evt->stag);

		break;

	case EVT_ID_STE_FETCH_FAULT:
	case EVT_ID_CD_FETCH_FAULT:
	case EVT_ID_VMS_FETCH_FAULT:
		dev_err(smmu->dev, "event: %s client: %s sid: %#x ssid: %#x fetch_addr: %#llx",
			ARM_SMMU_LOG_EVT_STR(evt), ARM_SMMU_LOG_CLIENT(evt),
			evt->sid, evt->ssid, evt->fetch_addr);

		break;

	default:
		dev_err(smmu->dev, "event: %s client: %s sid: %#x ssid: %#x",
			ARM_SMMU_LOG_EVT_STR(evt), ARM_SMMU_LOG_CLIENT(evt),
			evt->sid, evt->ssid);
	}
}

#ifdef CONFIG_ACPI
#ifdef CONFIG_TEGRA241_CMDQV
static void acpi_smmu_dsdt_probe_tegra241_cmdqv(struct acpi_iort_node *node,
						struct arm_smmu_device *smmu)
{
	const char *uid = kasprintf(GFP_KERNEL, "%u", node->identifier);
	struct acpi_device *adev;

	/* Look for an NVDA200C node whose _UID matches the SMMU node ID */
	adev = acpi_dev_get_first_match_dev("NVDA200C", uid, -1);
	if (adev) {
		/* Tegra241 CMDQV driver is responsible for put_device() */
		smmu->impl_dev = &adev->dev;
		smmu->options |= ARM_SMMU_OPT_TEGRA241_CMDQV;
		dev_info(smmu->dev, "found companion CMDQV device: %s\n",
			 dev_name(smmu->impl_dev));
	}
	kfree(uid);
}
#else
static void acpi_smmu_dsdt_probe_tegra241_cmdqv(struct acpi_iort_node *node,
						struct arm_smmu_device *smmu)
{
}
#endif

static int acpi_smmu_iort_probe_model(struct acpi_iort_node *node,
				      struct arm_smmu_device *smmu)
{
	struct acpi_iort_smmu_v3 *iort_smmu =
		(struct acpi_iort_smmu_v3 *)node->node_data;

	switch (iort_smmu->model) {
	case ACPI_IORT_SMMU_V3_CAVIUM_CN99XX:
		smmu->options |= ARM_SMMU_OPT_PAGE0_REGS_ONLY;
		break;
	case ACPI_IORT_SMMU_V3_HISILICON_HI161X:
		smmu->options |= ARM_SMMU_OPT_SKIP_PREFETCH;
		break;
	case ACPI_IORT_SMMU_V3_GENERIC:
		/*
		 * Tegra241 implementation stores its SMMU options and impl_dev
		 * in DSDT. Thus, go through the ACPI tables unconditionally.
		 */
		acpi_smmu_dsdt_probe_tegra241_cmdqv(node, smmu);
		break;
	}

	dev_notice(smmu->dev, "option mask 0x%x\n", smmu->options);
	return 0;
}

static int arm_smmu_device_acpi_probe(struct platform_device *pdev,
				      struct arm_smmu_device *smmu)
{
	struct acpi_iort_smmu_v3 *iort_smmu;
	struct device *dev = smmu->dev;
	struct acpi_iort_node *node;

	node = *(struct acpi_iort_node **)dev_get_platdata(dev);

	/* Retrieve SMMUv3 specific data */
	iort_smmu = (struct acpi_iort_smmu_v3 *)node->node_data;

	if (iort_smmu->flags & ACPI_IORT_SMMU_V3_COHACC_OVERRIDE)
		smmu->features |= ARM_SMMU_FEAT_COHERENCY;

	switch (FIELD_GET(ACPI_IORT_SMMU_V3_HTTU_OVERRIDE, iort_smmu->flags)) {
	case IDR0_HTTU_ACCESS_DIRTY:
		smmu->features |= ARM_SMMU_FEAT_HD;
		fallthrough;
	case IDR0_HTTU_ACCESS:
		smmu->features |= ARM_SMMU_FEAT_HA;
	}

	return acpi_smmu_iort_probe_model(node, smmu);
}
#else
static inline int arm_smmu_device_acpi_probe(struct platform_device *pdev,
					     struct arm_smmu_device *smmu)
{
	return -ENODEV;
}
#endif

static int arm_smmu_device_dt_probe(struct platform_device *pdev,
				    struct arm_smmu_device *smmu)
{
	struct device *dev = &pdev->dev;
	u32 cells;
	int ret = -EINVAL;

	if (of_property_read_u32(dev->of_node, "#iommu-cells", &cells))
		dev_err(dev, "missing #iommu-cells property\n");
	else if (cells != 1)
		dev_err(dev, "invalid #iommu-cells value (%d)\n", cells);
	else
		ret = 0;

	parse_driver_options(smmu);

	if (of_dma_is_coherent(dev->of_node))
		smmu->features |= ARM_SMMU_FEAT_COHERENCY;

	return ret;
}

static unsigned long arm_smmu_resource_size(struct arm_smmu_device *smmu)
{
	if (smmu->options & ARM_SMMU_OPT_PAGE0_REGS_ONLY)
		return SZ_64K;
	else
		return SZ_128K;
}

static void __iomem *arm_smmu_ioremap(struct device *dev, resource_size_t start,
				      resource_size_t size)
{
	struct resource res = DEFINE_RES_MEM(start, size);

	return devm_ioremap_resource(dev, &res);
}

int arm_smmu_device_ioremap(struct platform_device *pdev,
			    struct arm_smmu_device *smmu,
			    phys_addr_t *ioaddr, size_t *iosize)
{
	struct resource *res;
	struct device *dev = &pdev->dev;

	/* Base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	if (resource_size(res) < arm_smmu_resource_size(smmu)) {
		dev_err(dev, "MMIO region too small (%pr)\n", res);
		return -EINVAL;
	}
	*ioaddr = res->start;
	if (iosize)
		*iosize = resource_size(res);

	/*
	 * Don't map the IMPLEMENTATION DEFINED regions, since they may contain
	 * the PMCG registers which are reserved by the PMU driver.
	 */
	smmu->base = arm_smmu_ioremap(dev, *ioaddr, ARM_SMMU_REG_SZ);
	if (IS_ERR(smmu->base))
		return PTR_ERR(smmu->base);

	if (arm_smmu_resource_size(smmu) > SZ_64K) {
		smmu->page1 = arm_smmu_ioremap(dev, *ioaddr + SZ_64K,
					       ARM_SMMU_REG_SZ);
		if (IS_ERR(smmu->page1))
			return PTR_ERR(smmu->page1);
	} else {
		smmu->page1 = smmu->base;
	}

	return 0;
}

int arm_smmu_fw_probe(struct platform_device *pdev,
		      struct arm_smmu_device *smmu)
{
	if (smmu->dev->of_node)
		return arm_smmu_device_dt_probe(pdev, smmu);
	else
		return arm_smmu_device_acpi_probe(pdev, smmu);
}

#define IIDR_IMPLEMENTER_ARM		0x43b
#define IIDR_PRODUCTID_ARM_MMU_600	0x483
#define IIDR_PRODUCTID_ARM_MMU_700	0x487

static void arm_smmu_device_iidr_probe(struct arm_smmu_device *smmu)
{
	u32 reg;
	unsigned int implementer, productid, variant, revision;

	reg = readl_relaxed(smmu->base + ARM_SMMU_IIDR);
	implementer = FIELD_GET(IIDR_IMPLEMENTER, reg);
	productid = FIELD_GET(IIDR_PRODUCTID, reg);
	variant = FIELD_GET(IIDR_VARIANT, reg);
	revision = FIELD_GET(IIDR_REVISION, reg);

	switch (implementer) {
	case IIDR_IMPLEMENTER_ARM:
		switch (productid) {
		case IIDR_PRODUCTID_ARM_MMU_600:
			/* Arm erratum 1076982 */
			if (variant == 0 && revision <= 2)
				smmu->features &= ~ARM_SMMU_FEAT_SEV;
			/* Arm erratum 1209401 */
			if (variant < 2)
				smmu->features &= ~ARM_SMMU_FEAT_NESTING;
			break;
		case IIDR_PRODUCTID_ARM_MMU_700:
			/* Arm erratum 2812531 */
			smmu->features &= ~ARM_SMMU_FEAT_BTM;
			smmu->options |= ARM_SMMU_OPT_CMDQ_FORCE_SYNC;

			/* Arm errata 2268618, 2812531 */
			if (variant < 1 || (variant == 1 && revision < 1))
				smmu->features &= ~ARM_SMMU_FEAT_NESTING;

			smmu->features |= ARM_SMMU_FEAT_PMCG_PAGE0_2000;
			break;
		}
		break;
	}
}

static void arm_smmu_get_httu(struct arm_smmu_device *smmu, u32 reg)
{
	u32 fw_features = smmu->features & (ARM_SMMU_FEAT_HA | ARM_SMMU_FEAT_HD);
	u32 hw_features = 0;

	switch (FIELD_GET(IDR0_HTTU, reg)) {
	case IDR0_HTTU_ACCESS_DIRTY:
		hw_features |= ARM_SMMU_FEAT_HD;
		fallthrough;
	case IDR0_HTTU_ACCESS:
		hw_features |= ARM_SMMU_FEAT_HA;
	}

	if (smmu->dev->of_node)
		smmu->features |= hw_features;
	else if (hw_features != fw_features)
		/* ACPI IORT sets the HTTU bits */
		dev_warn(smmu->dev,
			 "IDR0.HTTU features(0x%x) overridden by FW configuration (0x%x)\n",
			  hw_features, fw_features);
}

int arm_smmu_device_hw_probe(struct arm_smmu_device *smmu)
{
	u32 reg;
	bool coherent = smmu->features & ARM_SMMU_FEAT_COHERENCY;

	/* IDR0 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR0);

	/* 2-level structures */
	if (FIELD_GET(IDR0_ST_LVL, reg) == IDR0_ST_LVL_2LVL)
		smmu->features |= ARM_SMMU_FEAT_2_LVL_STRTAB;

	if (reg & IDR0_CD2L)
		smmu->features |= ARM_SMMU_FEAT_2_LVL_CDTAB;

	/*
	 * Translation table endianness.
	 * We currently require the same endianness as the CPU, but this
	 * could be changed later by adding a new IO_PGTABLE_QUIRK.
	 */
	switch (FIELD_GET(IDR0_TTENDIAN, reg)) {
	case IDR0_TTENDIAN_MIXED:
		smmu->features |= ARM_SMMU_FEAT_TT_LE | ARM_SMMU_FEAT_TT_BE;
		break;
#ifdef __BIG_ENDIAN
	case IDR0_TTENDIAN_BE:
		smmu->features |= ARM_SMMU_FEAT_TT_BE;
		break;
#else
	case IDR0_TTENDIAN_LE:
		smmu->features |= ARM_SMMU_FEAT_TT_LE;
		break;
#endif
	default:
		dev_err(smmu->dev, "unknown/unsupported TT endianness!\n");
		return -ENXIO;
	}

	/* Boolean feature flags */
	if (IS_ENABLED(CONFIG_PCI_PRI) && reg & IDR0_PRI)
		smmu->features |= ARM_SMMU_FEAT_PRI;

	if (IS_ENABLED(CONFIG_PCI_ATS) && reg & IDR0_ATS)
		smmu->features |= ARM_SMMU_FEAT_ATS;

	if (reg & IDR0_SEV)
		smmu->features |= ARM_SMMU_FEAT_SEV;

	if (reg & IDR0_MSI) {
		smmu->features |= ARM_SMMU_FEAT_MSI;
		if (coherent)
			smmu->options |= ARM_SMMU_OPT_MSIPOLL;
	}

	if (reg & IDR0_HYP) {
		smmu->features |= ARM_SMMU_FEAT_HYP;
		if (cpus_have_cap(ARM64_HAS_VIRT_HOST_EXTN))
			smmu->features |= ARM_SMMU_FEAT_E2H;
	}

	arm_smmu_get_httu(smmu, reg);

	/*
	 * The coherency feature as set by FW is used in preference to the ID
	 * register, but warn on mismatch.
	 */
	if (!!(reg & IDR0_COHACC) != coherent)
		dev_warn(smmu->dev, "IDR0.COHACC overridden by FW configuration (%s)\n",
			 str_true_false(coherent));

	switch (FIELD_GET(IDR0_STALL_MODEL, reg)) {
	case IDR0_STALL_MODEL_FORCE:
		smmu->features |= ARM_SMMU_FEAT_STALL_FORCE;
		fallthrough;
	case IDR0_STALL_MODEL_STALL:
		smmu->features |= ARM_SMMU_FEAT_STALLS;
	}

	if (reg & IDR0_S1P)
		smmu->features |= ARM_SMMU_FEAT_TRANS_S1;

	if (reg & IDR0_S2P)
		smmu->features |= ARM_SMMU_FEAT_TRANS_S2;

	if (!(reg & (IDR0_S1P | IDR0_S2P))) {
		dev_err(smmu->dev, "no translation support!\n");
		return -ENXIO;
	}

	/* We only support the AArch64 table format at present */
	switch (FIELD_GET(IDR0_TTF, reg)) {
	case IDR0_TTF_AARCH32_64:
		smmu->ias = 40;
		fallthrough;
	case IDR0_TTF_AARCH64:
		break;
	default:
		dev_err(smmu->dev, "AArch64 table format not supported!\n");
		return -ENXIO;
	}

	/* ASID/VMID sizes */
	smmu->asid_bits = reg & IDR0_ASID16 ? 16 : 8;
	smmu->vmid_bits = reg & IDR0_VMID16 ? 16 : 8;

	/* IDR1 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR1);
	if (reg & (IDR1_TABLES_PRESET | IDR1_QUEUES_PRESET | IDR1_REL)) {
		dev_err(smmu->dev, "embedded implementation not supported\n");
		return -ENXIO;
	}

	if (reg & IDR1_ATTR_TYPES_OVR)
		smmu->features |= ARM_SMMU_FEAT_ATTR_TYPES_OVR;

	/* Queue sizes, capped to ensure natural alignment */
	smmu->cmdq.q.llq.max_n_shift = min_t(u32, CMDQ_MAX_SZ_SHIFT,
					     FIELD_GET(IDR1_CMDQS, reg));
	if (smmu->cmdq.q.llq.max_n_shift <= ilog2(CMDQ_BATCH_ENTRIES)) {
		/*
		 * We don't support splitting up batches, so one batch of
		 * commands plus an extra sync needs to fit inside the command
		 * queue. There's also no way we can handle the weird alignment
		 * restrictions on the base pointer for a unit-length queue.
		 */
		dev_err(smmu->dev, "command queue size <= %d entries not supported\n",
			CMDQ_BATCH_ENTRIES);
		return -ENXIO;
	}

	smmu->evtq.q.llq.max_n_shift = min_t(u32, EVTQ_MAX_SZ_SHIFT,
					     FIELD_GET(IDR1_EVTQS, reg));
	smmu->priq.q.llq.max_n_shift = min_t(u32, PRIQ_MAX_SZ_SHIFT,
					     FIELD_GET(IDR1_PRIQS, reg));

	/* SID/SSID sizes */
	smmu->ssid_bits = FIELD_GET(IDR1_SSIDSIZE, reg);
	smmu->sid_bits = FIELD_GET(IDR1_SIDSIZE, reg);
	smmu->iommu.max_pasids = 1UL << smmu->ssid_bits;

	/* Adjust SID sizes based on device tree settings */
	arm_smmu_dt_adjust_sid_bits(smmu);

	/*
	 * If the SMMU supports fewer bits than would fill a single L2 stream
	 * table, use a linear table instead.
	 */
	if (smmu->sid_bits <= STRTAB_SPLIT)
		smmu->features &= ~ARM_SMMU_FEAT_2_LVL_STRTAB;

	if (reg & IDR1_ATTR_PERMS_OVR) {
		smmu->features |= ARM_SMMU_FEAT_PERMS_OVR;
	} else if (smmu->options & ARM_SMMU_OPT_OVR_INSTCFG_DATA) {
		dev_err(smmu->dev, "Inst/Data attribute override not supported\n");
		return -ENXIO;
	}

	/* IDR3 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR3);
	if (FIELD_GET(IDR3_RIL, reg))
		smmu->features |= ARM_SMMU_FEAT_RANGE_INV;

	/* IDR5 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR5);

	/* Maximum number of outstanding stalls */
	smmu->evtq.max_stalls = FIELD_GET(IDR5_STALL_MAX, reg);

	/* Page sizes */
	if (reg & IDR5_GRAN64K)
		smmu->pgsize_bitmap |= SZ_64K | SZ_512M;
	if (reg & IDR5_GRAN16K)
		smmu->pgsize_bitmap |= SZ_16K | SZ_32M;
	if (reg & IDR5_GRAN4K)
		smmu->pgsize_bitmap |= SZ_4K | SZ_2M | SZ_1G;

	/* Input address size */
	if (FIELD_GET(IDR5_VAX, reg) == IDR5_VAX_52_BIT)
		smmu->features |= ARM_SMMU_FEAT_VAX;

	/* Output address size */
	switch (FIELD_GET(IDR5_OAS, reg)) {
	case IDR5_OAS_32_BIT:
		smmu->oas = 32;
		break;
	case IDR5_OAS_36_BIT:
		smmu->oas = 36;
		break;
	case IDR5_OAS_40_BIT:
		smmu->oas = 40;
		break;
	case IDR5_OAS_42_BIT:
		smmu->oas = 42;
		break;
	case IDR5_OAS_44_BIT:
		smmu->oas = 44;
		break;
	case IDR5_OAS_52_BIT:
		smmu->oas = 52;
		smmu->pgsize_bitmap |= 1ULL << 42; /* 4TB */
		break;
	default:
		dev_info(smmu->dev,
			"unknown output address size. Truncating to 48-bit\n");
		fallthrough;
	case IDR5_OAS_48_BIT:
		smmu->oas = 48;
	}

	/* Set the DMA mask for our table walker */
	if (dma_set_mask_and_coherent(smmu->dev, DMA_BIT_MASK(smmu->oas)))
		dev_warn(smmu->dev,
			 "failed to set DMA mask for table walker\n");

	smmu->ias = max(smmu->ias, smmu->oas);

	if ((smmu->features & ARM_SMMU_FEAT_TRANS_S1) &&
	    (smmu->features & ARM_SMMU_FEAT_TRANS_S2))
		smmu->features |= ARM_SMMU_FEAT_NESTING;

	arm_smmu_device_iidr_probe(smmu);

	dev_info(smmu->dev, "ias %lu-bit, oas %lu-bit (features 0x%08x)\n",
		 smmu->ias, smmu->oas, smmu->features);
	return 0;
}

int arm_smmu_write_reg_sync(struct arm_smmu_device *smmu, u32 val,
			    unsigned int reg_off, unsigned int ack_off)
{
	u32 reg;

	writel_relaxed(val, smmu->base + reg_off);
	return readl_relaxed_poll_timeout(smmu->base + ack_off, reg, reg == val,
					  1, ARM_SMMU_POLL_TIMEOUT_US);
}

/* GBPA is "special" */
int arm_smmu_update_gbpa(struct arm_smmu_device *smmu, u32 set, u32 clr)
{
	int ret;
	u32 reg, __iomem *gbpa = smmu->base + ARM_SMMU_GBPA;

	ret = readl_relaxed_poll_timeout(gbpa, reg, !(reg & GBPA_UPDATE),
					 1, ARM_SMMU_POLL_TIMEOUT_US);
	if (ret)
		return ret;

	reg &= ~clr;
	reg |= set;
	writel_relaxed(reg | GBPA_UPDATE, gbpa);
	ret = readl_relaxed_poll_timeout(gbpa, reg, !(reg & GBPA_UPDATE),
					 1, ARM_SMMU_POLL_TIMEOUT_US);

	if (ret)
		dev_err(smmu->dev, "GBPA not responding to update\n");
	return ret;
}

int arm_smmu_device_disable(struct arm_smmu_device *smmu)
{
	int ret;

	ret = arm_smmu_write_reg_sync(smmu, 0, ARM_SMMU_CR0, ARM_SMMU_CR0ACK);
	if (ret)
		dev_err(smmu->dev, "failed to clear cr0\n");

	return ret;
}

struct iommu_group *arm_smmu_device_group(struct device *dev)
{
	struct iommu_group *group;

	/*
	 * We don't support devices sharing stream IDs other than PCI RID
	pick f473c7c1b189 fixup drv splt * aliases, since the necessary ID-to-device lookup becomes rather
	 * impractical given a potential sparse 32-bit stream ID space.
	 */
	if (dev_is_pci(dev))
		group = pci_device_group(dev);
	else
		group = generic_device_group(dev);

	return group;
}

int arm_smmu_of_xlate(struct device *dev, const struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 1);
}

void arm_smmu_make_s1_cd_common(struct arm_smmu_cd *target, const struct io_pgtable_cfg *pgtbl_cfg,
				bool stall_enabled, u16 asid)
{
	typeof(&pgtbl_cfg->arm_lpae_s1_cfg.tcr) tcr =
		&pgtbl_cfg->arm_lpae_s1_cfg.tcr;

	memset(target, 0, sizeof(*target));

	target->data[0] = cpu_to_le64(
		FIELD_PREP(CTXDESC_CD_0_TCR_T0SZ, tcr->tsz) |
		FIELD_PREP(CTXDESC_CD_0_TCR_TG0, tcr->tg) |
		FIELD_PREP(CTXDESC_CD_0_TCR_IRGN0, tcr->irgn) |
		FIELD_PREP(CTXDESC_CD_0_TCR_ORGN0, tcr->orgn) |
		FIELD_PREP(CTXDESC_CD_0_TCR_SH0, tcr->sh) |
#ifdef __BIG_ENDIAN
		CTXDESC_CD_0_ENDI |
#endif
		CTXDESC_CD_0_TCR_EPD1 |
		CTXDESC_CD_0_V |
		FIELD_PREP(CTXDESC_CD_0_TCR_IPS, tcr->ips) |
		CTXDESC_CD_0_AA64 |
		(stall_enabled ? CTXDESC_CD_0_S : 0) |
		CTXDESC_CD_0_R |
		CTXDESC_CD_0_A |
		CTXDESC_CD_0_ASET |
		FIELD_PREP(CTXDESC_CD_0_ASID, asid)
		);

	/* To enable dirty flag update, set both Access flag and dirty state update */
	if (pgtbl_cfg->quirks & IO_PGTABLE_QUIRK_ARM_HD)
		target->data[0] |= cpu_to_le64(CTXDESC_CD_0_TCR_HA |
					       CTXDESC_CD_0_TCR_HD);

	target->data[1] = cpu_to_le64(pgtbl_cfg->arm_lpae_s1_cfg.ttbr &
				      CTXDESC_CD_1_TTB0_MASK);
	target->data[3] = cpu_to_le64(pgtbl_cfg->arm_lpae_s1_cfg.mair);
}

void arm_smmu_make_cdtable_ste_common(struct arm_smmu_ste *target, struct arm_smmu_device *smmu,
				      struct arm_smmu_ctx_desc_cfg *cd_table, bool stall_enabled,
				      bool ats_enabled, unsigned int s1dss)
{
	unsigned int s1c, s1sh;

	if (smmu->features & ARM_SMMU_FEAT_COHERENCY) {
		s1c = STRTAB_STE_1_S1C_CACHE_WBRA;
		s1sh = ARM_SMMU_SH_ISH;
	} else {
		s1c = STRTAB_STE_1_S1C_CACHE_NC;
		s1sh = ARM_SMMU_SH_OSH;
	}

	memset(target, 0, sizeof(*target));
	target->data[0] = cpu_to_le64(
		STRTAB_STE_0_V |
		FIELD_PREP(STRTAB_STE_0_CFG, STRTAB_STE_0_CFG_S1_TRANS) |
		FIELD_PREP(STRTAB_STE_0_S1FMT, cd_table->s1fmt) |
		(cd_table->cdtab_dma & STRTAB_STE_0_S1CTXPTR_MASK) |
		FIELD_PREP(STRTAB_STE_0_S1CDMAX, cd_table->s1cdmax));

	target->data[1] = cpu_to_le64(
		FIELD_PREP(STRTAB_STE_1_S1DSS, s1dss) |
		FIELD_PREP(STRTAB_STE_1_S1CIR, s1c) |
		FIELD_PREP(STRTAB_STE_1_S1COR, s1c) |
		FIELD_PREP(STRTAB_STE_1_S1CSH, s1sh) |
		((smmu->features & ARM_SMMU_FEAT_STALLS &&
		  !stall_enabled) ?
			 STRTAB_STE_1_S1STALLD :
			 0) |
		FIELD_PREP(STRTAB_STE_1_EATS,
			   ats_enabled ? STRTAB_STE_1_EATS_TRANS : 0)) |
		FIELD_PREP(STRTAB_STE_1_INSTCFG,
			   smmu->options & ARM_SMMU_OPT_OVR_INSTCFG_DATA ?
				   STRTAB_STE_1_INSTCFG_DATA :
				   STRTAB_STE_1_INSTCFG_INCOMING);

	if ((smmu->features & ARM_SMMU_FEAT_ATTR_TYPES_OVR) &&
	    s1dss == STRTAB_STE_1_S1DSS_BYPASS)
		target->data[1] |= cpu_to_le64(FIELD_PREP(
			STRTAB_STE_1_SHCFG, STRTAB_STE_1_SHCFG_INCOMING));

	if (smmu->features & ARM_SMMU_FEAT_E2H) {
		/*
		 * To support BTM the streamworld needs to match the
		 * configuration of the CPU so that the ASID broadcasts are
		 * properly matched. This means either S/NS-EL2-E2H (hypervisor)
		 * or NS-EL1 (guest). Since an SVA domain can be installed in a
		 * PASID this should always use a BTM compatible configuration
		 * if the HW supports it.
		 */
		target->data[1] |= cpu_to_le64(
			FIELD_PREP(STRTAB_STE_1_STRW, STRTAB_STE_1_STRW_EL2));
	} else {
		target->data[1] |= cpu_to_le64(
			FIELD_PREP(STRTAB_STE_1_STRW, STRTAB_STE_1_STRW_NSEL1));

		/*
		 * VMID 0 is reserved for stage-2 bypass EL1 STEs, see
		 * arm_smmu_domain_alloc_id()
		 */
		target->data[2] =
			cpu_to_le64(FIELD_PREP(STRTAB_STE_2_S2VMID, 0));
	}
}

void arm_smmu_get_resv_regions(struct device *dev,
			       struct list_head *head)
{
	struct iommu_resv_region *region;
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	region = iommu_alloc_resv_region(MSI_IOVA_BASE, MSI_IOVA_LENGTH,
					 prot, IOMMU_RESV_SW_MSI, GFP_KERNEL);
	if (!region)
		return;

	list_add_tail(&region->list, head);

	iommu_dma_get_resv_regions(dev, head);
}

int arm_smmu_init_one_queue(struct arm_smmu_device *smmu,
			    struct arm_smmu_queue *q, void __iomem *page,
			    unsigned long prod_off, unsigned long cons_off,
			    size_t dwords, const char *name)
{
	size_t qsz;

	do {
		qsz = ((1 << q->llq.max_n_shift) * dwords) << 3;
		q->base = dmam_alloc_coherent(smmu->dev, PAGE_ALIGN(qsz), &q->base_dma,
					      GFP_KERNEL);
		if (q->base || qsz < PAGE_SIZE)
			break;

		q->llq.max_n_shift--;
	} while (1);

	if (!q->base) {
		dev_err(smmu->dev,
			"failed to allocate queue (0x%zx bytes) for %s\n",
			qsz, name);
		return -ENOMEM;
	}

	if (!WARN_ON(q->base_dma & (qsz - 1))) {
		dev_info(smmu->dev, "allocated %u entries for %s\n",
			 1 << q->llq.max_n_shift, name);
	}

	q->prod_reg	= page + prod_off;
	q->cons_reg	= page + cons_off;
	q->ent_dwords	= dwords;

	q->q_base  = Q_BASE_RWA;
	q->q_base |= q->base_dma & Q_BASE_ADDR_MASK;
	q->q_base |= FIELD_PREP(Q_BASE_LOG2SIZE, q->llq.max_n_shift);

	q->llq.prod = q->llq.cons = 0;
	return 0;
}

/* Stream table initialization functions */
static int arm_smmu_init_strtab_2lvl(struct arm_smmu_device *smmu)
{
	u32 l1size;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;
	unsigned int last_sid_idx =
		arm_smmu_strtab_l1_idx((1ULL << smmu->sid_bits) - 1);

	/* Calculate the L1 size, capped to the SIDSIZE. */
	cfg->l2.num_l1_ents = min(last_sid_idx + 1, STRTAB_MAX_L1_ENTRIES);
	if (cfg->l2.num_l1_ents <= last_sid_idx)
		dev_warn(smmu->dev,
			 "2-level strtab only covers %u/%u bits of SID\n",
			 ilog2(cfg->l2.num_l1_ents * STRTAB_NUM_L2_STES),
			 smmu->sid_bits);

	l1size = cfg->l2.num_l1_ents * sizeof(struct arm_smmu_strtab_l1);
	cfg->l2.l1tab = dmam_alloc_coherent(smmu->dev, PAGE_ALIGN(l1size), &cfg->l2.l1_dma,
					    GFP_KERNEL);
	if (!cfg->l2.l1tab) {
		dev_err(smmu->dev,
			"failed to allocate l1 stream table (%u bytes)\n",
			l1size);
		return -ENOMEM;
	}

	cfg->l2.l2ptrs = devm_kcalloc(smmu->dev, cfg->l2.num_l1_ents,
				      sizeof(*cfg->l2.l2ptrs), GFP_KERNEL);
	if (!cfg->l2.l2ptrs)
		return -ENOMEM;

	return 0;
}

static int arm_smmu_init_strtab_linear(struct arm_smmu_device *smmu)
{
	u32 size;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;

	size = (1 << smmu->sid_bits) * sizeof(struct arm_smmu_ste);
	cfg->linear.table = dmam_alloc_coherent(smmu->dev, PAGE_ALIGN(size),
						&cfg->linear.ste_dma,
						GFP_KERNEL);
	if (!cfg->linear.table) {
		dev_err(smmu->dev,
			"failed to allocate linear stream table (%u bytes)\n",
			size);
		return -ENOMEM;
	}
	cfg->linear.num_ents = 1 << smmu->sid_bits;

	return 0;
}

int arm_smmu_init_strtab(struct arm_smmu_device *smmu)
{
	int ret;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB)
		ret = arm_smmu_init_strtab_2lvl(smmu);
	else
		ret = arm_smmu_init_strtab_linear(smmu);
	if (ret)
		return ret;

	ida_init(&smmu->vmid_map);

	return 0;
}

void arm_smmu_write_strtab(struct arm_smmu_device *smmu)
{
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;
	dma_addr_t dma;
	u32 reg;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB) {
		reg = FIELD_PREP(STRTAB_BASE_CFG_FMT,
				 STRTAB_BASE_CFG_FMT_2LVL) |
		      FIELD_PREP(STRTAB_BASE_CFG_LOG2SIZE,
				 ilog2(cfg->l2.num_l1_ents) + STRTAB_SPLIT) |
		      FIELD_PREP(STRTAB_BASE_CFG_SPLIT, STRTAB_SPLIT);
		dma = cfg->l2.l1_dma;
	} else {
		reg = FIELD_PREP(STRTAB_BASE_CFG_FMT,
				 STRTAB_BASE_CFG_FMT_LINEAR) |
		      FIELD_PREP(STRTAB_BASE_CFG_LOG2SIZE, smmu->sid_bits);
		dma = cfg->linear.ste_dma;
	}
	writeq_relaxed((dma & STRTAB_BASE_ADDR_MASK) | STRTAB_BASE_RA,
		       smmu->base + ARM_SMMU_STRTAB_BASE);
	writel_relaxed(reg, smmu->base + ARM_SMMU_STRTAB_BASE_CFG);
}

static void arm_smmu_free_msis(void *data)
{
	struct device *dev = data;

	platform_device_msi_free_irqs_all(dev);
}

static void arm_smmu_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	phys_addr_t doorbell;
	struct device *dev = msi_desc_to_dev(desc);
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);
	phys_addr_t *cfg = arm_smmu_msi_cfg[desc->msi_index];

	doorbell = (((u64)msg->address_hi) << 32) | msg->address_lo;
	doorbell &= MSI_CFG0_ADDR_MASK;

	writeq_relaxed(doorbell, smmu->base + cfg[0]);
	writel_relaxed(msg->data, smmu->base + cfg[1]);
	writel_relaxed(ARM_SMMU_MEMATTR_DEVICE_nGnRE, smmu->base + cfg[2]);
}

static void arm_smmu_setup_msis(struct arm_smmu_device *smmu)
{
	int ret, nvec = ARM_SMMU_MAX_MSIS;
	struct device *dev = smmu->dev;

	/* Clear the MSI address regs */
	writeq_relaxed(0, smmu->base + ARM_SMMU_GERROR_IRQ_CFG0);
	writeq_relaxed(0, smmu->base + ARM_SMMU_EVTQ_IRQ_CFG0);

	if (smmu->features & ARM_SMMU_FEAT_PRI)
		writeq_relaxed(0, smmu->base + ARM_SMMU_PRIQ_IRQ_CFG0);
	else
		nvec--;

	if (!(smmu->features & ARM_SMMU_FEAT_MSI))
		return;

	if (!dev->msi.domain) {
		dev_info(smmu->dev, "msi_domain absent - falling back to wired irqs\n");
		return;
	}

	/* Allocate MSIs for evtq, gerror and priq. Ignore cmdq */
	ret = platform_device_msi_init_and_alloc_irqs(dev, nvec, arm_smmu_write_msi_msg);
	if (ret) {
		dev_warn(dev, "failed to allocate MSIs - falling back to wired irqs\n");
		return;
	}

	smmu->evtq.q.irq = msi_get_virq(dev, EVTQ_MSI_INDEX);
	smmu->gerr_irq = msi_get_virq(dev, GERROR_MSI_INDEX);
	smmu->priq.q.irq = msi_get_virq(dev, PRIQ_MSI_INDEX);

	/* Add callback to free MSIs on teardown */
	devm_add_action_or_reset(dev, arm_smmu_free_msis, dev);
}

void arm_smmu_setup_unique_irqs(struct arm_smmu_device *smmu,
				irqreturn_t evtqirq(int irq, void *dev),
				irqreturn_t gerrorirq(int irq, void *dev),
				irqreturn_t priirq(int irq, void *dev))
{
	int irq, ret;

	arm_smmu_setup_msis(smmu);

	/* Request interrupt lines */
	irq = smmu->evtq.q.irq;
	if (irq) {
		ret = devm_request_threaded_irq(smmu->dev, irq, NULL,
						evtqirq,
						IRQF_ONESHOT,
						"arm-smmu-v3-evtq", smmu);
		if (ret < 0)
			dev_warn(smmu->dev, "failed to enable evtq irq\n");
	} else {
		dev_warn(smmu->dev, "no evtq irq - events will not be reported!\n");
	}

	irq = smmu->gerr_irq;
	if (irq) {
		ret = devm_request_irq(smmu->dev, irq, gerrorirq,
				       0, "arm-smmu-v3-gerror", smmu);
		if (ret < 0)
			dev_warn(smmu->dev, "failed to enable gerror irq\n");
	} else {
		dev_warn(smmu->dev, "no gerr irq - errors will not be reported!\n");
	}

	if (smmu->features & ARM_SMMU_FEAT_PRI) {
		irq = smmu->priq.q.irq;
		if (irq) {
			ret = devm_request_threaded_irq(smmu->dev, irq, NULL,
							priirq,
							IRQF_ONESHOT,
							"arm-smmu-v3-priq",
							smmu);
			if (ret < 0)
				dev_warn(smmu->dev,
					 "failed to enable priq irq\n");
		} else {
			dev_warn(smmu->dev, "no priq irq - PRI will be broken\n");
		}
	}
}

void arm_smmu_probe_irq(struct platform_device *pdev,
			struct arm_smmu_device *smmu)
{
	int irq;

	irq = platform_get_irq_byname_optional(pdev, "combined");
	if (irq > 0)
		smmu->combined_irq = irq;
	else {
		irq = platform_get_irq_byname_optional(pdev, "eventq");
		if (irq > 0)
			smmu->evtq.q.irq = irq;

		irq = platform_get_irq_byname_optional(pdev, "priq");
		if (irq > 0)
			smmu->priq.q.irq = irq;

		irq = platform_get_irq_byname_optional(pdev, "gerror");
		if (irq > 0)
			smmu->gerr_irq = irq;
	}
}

int arm_smmu_register_iommu(struct arm_smmu_device *smmu,
			    struct iommu_ops *ops, phys_addr_t ioaddr)
{
	int ret;
	struct device *dev = smmu->dev;

	ret = iommu_device_sysfs_add(&smmu->iommu, dev, NULL,
				     "smmu3.%pa", &ioaddr);
	if (ret)
		return ret;

	ret = iommu_device_register(&smmu->iommu, ops, dev);
	if (ret) {
		dev_err(dev, "Failed to register iommu\n");
		iommu_device_sysfs_remove(&smmu->iommu);
		return ret;
	}

	return 0;
}

void arm_smmu_unregister_iommu(struct arm_smmu_device *smmu)
{
	iommu_device_unregister(&smmu->iommu);
	iommu_device_sysfs_remove(&smmu->iommu);
}
