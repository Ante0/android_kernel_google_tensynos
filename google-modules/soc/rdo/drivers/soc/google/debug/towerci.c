// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 *
 */

#include <kunit/visibility.h>
#include <linux/arm-smccc.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include "towerci.h"

static const u32 mcn_err_grp_offsets[] = {
	MCN_ERR_GRP1_OFFSET, MCN_ERR_GRP2_OFFSET, MCN_ERR_GRP3_OFFSET
};

enum tower_ci_node_type {
	NODE_TYPE_ASNI = 0x4,
	NODE_TYPE_MCN = 0x63,
};

static const struct reg_desc asni_cfg_regs[] = {
	{"sam_status", 0x10004},
	{"nh_region0_cfg0", 0x10100},
	{"nh_region0_cfg1", 0x10104},
	{"nh_region0_cfg2", 0x10108},
	{"nh_region0_cfg3", 0x1010C},
	{"nh_region1_cfg0", 0x10110},
	{"nh_region1_cfg1", 0x10114},
	{"nh_region1_cfg2", 0x10118},
	{"nh_region1_cfg3", 0x1011C},
	{"nh_region2_cfg0", 0x10120},
	{"nh_region2_cfg1", 0x10124},
	{"nh_region2_cfg2", 0x10128},
	{"nh_region2_cfg3", 0x1012C},
	{"nh_region3_cfg0", 0x10130},
	{"nh_region3_cfg1", 0x10134},
	{"nh_region3_cfg2", 0x10138},
	{"nh_region3_cfg3", 0x1013C},
	{"nh_region4_cfg0", 0x10140},
	{"nh_region4_cfg1", 0x10144},
	{"nh_region4_cfg2", 0x10148},
	{"nh_region4_cfg3", 0x1014C},
	{"nh_region5_cfg0", 0x10150},
	{"nh_region5_cfg1", 0x10154},
	{"nh_region5_cfg2", 0x10158},
	{"nh_region5_cfg3", 0x1015C},
	{"htg_region0_cfg0", 0x10900},
	{"htg_region0_cfg1", 0x10904},
	{"htg_region0_cfg2", 0x10908},
	{"htg_region0_cfg3", 0x1090C},
	{"htg_region1_cfg0", 0x10910},
	{"htg_region1_cfg1", 0x10914},
	{"htg_region1_cfg2", 0x10918},
	{"htg_region1_cfg3", 0x1091C},
	{"htg_region2_cfg0", 0x10920},
	{"htg_region2_cfg1", 0x10924},
	{"htg_region2_cfg2", 0x10928},
	{"htg_region2_cfg3", 0x1092C},
	{"htg_region3_cfg0", 0x10930},
	{"htg_region3_cfg1", 0x10934},
	{"htg_region3_cfg2", 0x10938},
	{"htg_region3_cfg3", 0x1093C},
	{"htg_tgtid_cfg0", 0x10B00},
};

static const struct reg_desc mcn_cfg_regs[] = {
	{"MCN_HN_CTRL_NS", 0x10010},
	{"MCN_HN_CTRL_S", 0x14010},
	{"MCN_MTE_DISABLE", 0xE4020},
};

static const char * const uet_record[] = {
	"Uncorrected, uncontainable",
	"Uncorrected, unrecoverable",
	"Uncorrected, Latent or Restartable",
	"Signaled or Recoverable"
};

static const char * const ierr_record[] = {
	"Reserved",
	"NOC",
	"DSU"
};

static const char * const serr_record[] = {
	"No error",
	"Reserved",
	"Data value from internal non-associative memory",
	"Reserved",
	"Assertion failure",
	"Error detected on internal data path",
	"Data value from internal associative memory",
	"Address/control value from associative (ECC error on cache tag)",
	"Reserved",
	"Reserved",
	"Data value from producer (upstream system write data)",
	"Address/control value from producer",
	"Data value from external/downstream memory",
	"Illegal Address (access to unpopulated memory)",
	"Illegal Access (byte write to word memory)",
	"Illegal State (device not ready)",
	"Internal data register",
	"Internal control register",
	"Error response from external/downstream",
	"External timeout",
	"Internal timeout",
	"Deferred error from downstream cannot be propagated/contained upstream",
	"Deferred error from Requester not supported at Completer",
	"Other internal"
};

static const char * const sys_intf_opcode_record0[] = {
	"ReqLCrdReturn",
	"ReadShared",
	"ReadClean",
	"ReadOnce",
	"ReadNoSnp",
	"PCrdReturn",
	"Reserved",
	"ReadUnique",
	"CleanShared",
	"CleanInvalid",
	"MakeInvalid",
	"CleanUnique",
	"MakeUnique",
	"Evict",
	"Reserved",
	"Reserved",
	"Reserved",
	"ReadNoSnpSep",
	"Reserved",
	"CleanSharedPersistSep",
	"DVMOp",
	"WriteEvictFull",
	"Reserved",
	"WriteCleanFull",
	"WriteUniquePtl",
	"WriteUniqueFull",
	"WriteBackPtl",
	"WriteBackFull",
	"WriteNoSnpPtl",
	"WriteNoSnpFull",
	"Reserved",
	"Reserved",
	"WriteUniqueFullStash",
	"WriteUniquePtlStash",
	"StashOnceShared",
	"StashOnceUnique",
	"ReadOnceCleanInvalid",
	"ReadOnceMakeInvalid",
	"ReadNotSharedDirty",
	"CleanSharedPersist"
};

static const char * const sys_intf_opcode_record1[] = {
	"Reserved",
	"MakeReadUnique",
	"WriteEvictOrEvict",
	"WriteUniqueZero",
	"WriteNoSnpZero",
	"Reserved",
	"Reserved",
	"StashOnceSepShared",
	"StashOnceSepUnique",
	"Reserved",
	"Reserved",
	"Reserved",
	"ReadPreferUnique",
	"CleanInvalidPoPA",
	"WriteNoSnpDef",
	"Reserved",
	"WriteNoSnpFullCleanSh",
	"WriteNoSnpFullCleanInv",
	"WriteNoSnpFullCleanShPerSep",
	"Reserved",
	"WriteUniqueFullCleanSh",
	"Reserved",
	"WriteUniqueFullCleanShPerSep",
	"Reserved",
	"WriteBackFullCleanSh",
	"WriteBackFullCleanInv",
	"WriteBackFullCleanShPerSep",
	"Reserved",
	"WriteCleanFullCleanSh",
	"Reserved",
	"WriteCleanFullCleanShPerSep",
	"Reserved",
	"WriteNoSnpPtlCleanSh",
	"WriteNoSnpPtlCleanInv",
	"WriteNoSnpPtlCleanShPerSep",
	"Reserved",
	"WriteUniquePtlCleanSh",
	"Reserved",
	"WriteUniquePtlCleanShPerSep",
	"Reserved"
};

static u32 towerci_reg_read(bool is_secure, phys_addr_t reg_s,
		 void __iomem *reg_ns, u32 offset)
{
	if (is_secure) {
		struct arm_smccc_res smc_res;

		arm_smccc_smc(SMC_CMD_PRIV_REG, reg_s + offset,
					 PRIV_REG_OPTION_READ,  0, 0, 0, 0, 0, &smc_res);
		return smc_res.a0;
	}

	return __raw_readl(reg_ns + offset);
}

static void towerci_reg_write(bool is_secure, phys_addr_t reg_s,
		 void __iomem *reg_ns, u32 offset, u32 val)
{
	if (is_secure) {
		struct arm_smccc_res res;

		arm_smccc_smc(SMC_CMD_PRIV_REG, reg_s + offset,
					 PRIV_REG_OPTION_WRITE, val, 0, 0, 0, 0, &res);
	} else {
		__raw_writel(val, reg_ns + offset);
		/* Ensure that the write is visible to the device */
		wmb();
	}
}

VISIBLE_IF_KUNIT const char *get_uet_string(u16 uet)
{
	if (uet < ARRAY_SIZE(uet_record))
		return uet_record[uet];
	return "Unknown UET";
}
EXPORT_SYMBOL_IF_KUNIT(get_uet_string);

VISIBLE_IF_KUNIT const char *get_ierr_string(u16 ierr)
{
	if (ierr < ARRAY_SIZE(ierr_record))
		return ierr_record[ierr];
	return "Unknown IERR";
}
EXPORT_SYMBOL_IF_KUNIT(get_ierr_string);

VISIBLE_IF_KUNIT const char *get_serr_string(u16 serr)
{
	if (serr < ARRAY_SIZE(serr_record))
		return serr_record[serr];
	return "Unknown SERR";
}
EXPORT_SYMBOL_IF_KUNIT(get_serr_string);

VISIBLE_IF_KUNIT const char *get_sys_opcode_string(u16 sys_opcode, bool sys_opcode_select)
{
	if (!sys_opcode_select) {
		if (sys_opcode < ARRAY_SIZE(sys_intf_opcode_record0))
			return sys_intf_opcode_record0[sys_opcode];
	} else {
		if (sys_opcode < ARRAY_SIZE(sys_intf_opcode_record1))
			return sys_intf_opcode_record1[sys_opcode];
	}
	return "Unknown SYS_INTF_OPCODE";
}
EXPORT_SYMBOL_IF_KUNIT(get_sys_opcode_string);

static void parse_and_print_error(struct device *dev, u32 err_record_num, u32 err_status,
		 u64 err_addr, u32 err_misc)
{

	bool av, v, ue, er, of, mv, de, pn, ci, ns, si, ai;
	bool sys_opcode_select, rd_wr_ewa, imprecise_err, snoop_ewa;
	u8 ce;
	u16 uet, ierr, serr, sys_opcode;
	u64 paddr;

	v = FIELD_GET(BIT(30), err_status); /* Valid */
	if (!v) {
		dev_info(dev, "Error Status Not Valid");
		return;
	}

	av = FIELD_GET(BIT(31), err_status); /* Address Valid */
	ue = FIELD_GET(BIT(29), err_status); /* Uncorrected Error */
	er = FIELD_GET(BIT(28), err_status); /* Error Reported */
	of = FIELD_GET(BIT(27), err_status); /* Overflow */
	mv = FIELD_GET(BIT(26), err_status); /* Misc Valid */
	de = FIELD_GET(BIT(23), err_status); /* Deferred Error */
	pn = FIELD_GET(BIT(22), err_status); /* Poison */
	ci = FIELD_GET(BIT(19), err_status); /* Critical Error */

	ce = FIELD_GET(GENMASK(25, 24), err_status);
	uet = FIELD_GET(GENMASK(21, 20), err_status);
	ierr = FIELD_GET(GENMASK(15, 8), err_status);
	serr = FIELD_GET(GENMASK(7, 0), err_status);

	dev_info(dev, "err_status: %#x, ue: %u, er: %u, of: %u, ce: %u, de: %u, pn: %u, ci: %u\n",
			err_status, ue, er, of, ce, de, pn, ci);
	dev_info(dev, "uet: %#x %s, ierr: %#x %s, serr: %#x %s\n",
			uet, get_uet_string(uet), ierr,
			get_ierr_string(ierr), serr, get_serr_string(serr));

	if (av) {
		ns = FIELD_GET(BIT_ULL(63), err_addr); /* Address Non-secure */
		si = FIELD_GET(BIT_ULL(62), err_addr); /* Secure Incorrect */
		ai = FIELD_GET(BIT_ULL(61), err_addr); /* Address Incorrect */
		paddr = FIELD_GET(GENMASK_ULL(55, 0), err_addr);

		dev_info(dev, "err_addr: %pap, ns: %u, si: %u, ai: %u\n", &paddr, ns, si, ai);
	}

	if (mv) {
		/* The format of ERR<n>_MISC0 differs for n=0 and n=1 */
		if (err_record_num == 0) {
			sys_opcode = FIELD_GET(GENMASK(16, 11), err_misc);
			sys_opcode_select = FIELD_GET(BIT(17), err_misc);

			dev_info(dev, "err_misc: %#x, sys_opcode_select: %u, sys_opcode: %#x %s\n",
				 err_misc, sys_opcode_select, sys_opcode,
				 get_sys_opcode_string(sys_opcode, sys_opcode_select));
		} else if (err_record_num == 1) {
			rd_wr_ewa = FIELD_GET(BIT(2), err_misc);
			imprecise_err = FIELD_GET(BIT(1), err_misc);
			snoop_ewa = FIELD_GET(BIT(0), err_misc);

			dev_info(dev,
				 "err_misc: %#x, rd_wr_ewa: %u, imprecise_err: %u, snoop_ewa: %u\n",
				 err_misc, rd_wr_ewa, imprecise_err, snoop_ewa);
		}
	}
}

VISIBLE_IF_KUNIT irqreturn_t tower_irq_handler(int irq_num, void *data)
{
	struct towerci_dev *towerci = data;
	struct node_desc *tnode = towerci->tnode;
	struct node_irq_desc *irq = NULL;
	unsigned long err_gsr;
	u32 ras_s_off, ras_s_err_off, err_status, err_misc, addr_lo, addr_hi;
	void *ras_ns_off, *ras_ns_err_off;
	phys_addr_t addr;
	int i;

	for (i = 0; i < tnode->irq_count; i++) {
		if (tnode->irq[i].irq_num == irq_num) {
			irq = &tnode->irq[i];
			break;
		}
	}

	if (!irq) {
		dev_warn(towerci->dev, "Spurious interrupt received for IRQ %d\n", irq_num);
		return IRQ_NONE;
	}

	dev_info(towerci->dev, "Interrupt Secure: %u Error: %u\n", irq->is_secure, irq->is_error);

	ras_s_off = towerci->res->start + MCN_RAS_BLCK_S;
	ras_ns_off = tnode->node_base + MCN_RAS_BLCK_NS;

	err_gsr = towerci->reg_read(irq->is_secure,
			 ras_s_off, ras_ns_off, MCN_ERRGSR_OFFSET);

	for_each_set_bit(i, &err_gsr, MCN_ERR_NUM_GRPS) {
		ras_s_err_off = ras_s_off + mcn_err_grp_offsets[i];
		ras_ns_err_off = ras_ns_off + mcn_err_grp_offsets[i];

		err_status = towerci->reg_read(irq->is_secure,
				 ras_s_err_off, ras_ns_err_off, MCN_ERR_STATUS_OFFSET);

		addr_lo = towerci->reg_read(irq->is_secure,
				 ras_s_err_off, ras_ns_err_off, MCN_ERR_ADDRL_OFFSET);
		addr_hi = towerci->reg_read(irq->is_secure,
				 ras_s_err_off, ras_ns_err_off, MCN_ERR_ADDRH_OFFSET);
		addr = (((u64)addr_hi << 32) | addr_lo);

		/* ERR_MISC register is only defined for error groups 0 and 1. */
		if (i < 2)
			err_misc = towerci->reg_read(irq->is_secure,
					 ras_s_err_off, ras_ns_err_off, MCN_ERR_MISC_OFFSET);
		else
			err_misc = 0;

		parse_and_print_error(towerci->dev, i, err_status, addr, err_misc);

		/* Clear interrupt */
		towerci->reg_write(irq->is_secure,
				 ras_s_err_off, ras_ns_err_off, MCN_ERR_STATUS_OFFSET, err_status);
	}

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_IF_KUNIT(tower_irq_handler);

static int towerci_dt_irq(struct towerci_dev *towerci)
{
	struct device *dev = towerci->dev;
	struct device_node *np = dev->of_node;
	struct node_desc *tnode = towerci->tnode;
	struct node_irq_desc *irq;
	struct property *prop;
	const char *name;
	int prop_idx;
	u32 val;
	unsigned int irq_num;

	if (towerci->dev_type == TOWER_CI_GLOBAL)
		return 0;

	if (towerci->tnode->node_type == NODE_TYPE_ASNI)
		return 0;

	tnode->irq_count = of_property_count_strings(np, "interrupt-names");
	if (tnode->irq_count <= 0) {
		dev_err(dev, "invalid Interrupt-names property count(%d)\n", tnode->irq_count);
		return -EINVAL;
	}

	tnode->irq = devm_kcalloc(dev, tnode->irq_count,
			  sizeof(struct node_irq_desc), GFP_KERNEL);
	if (!tnode->irq)
		return -ENOMEM;

	irq = tnode->irq;

	prop_idx = 0;
	of_property_for_each_string(np, "interrupt-names", prop, name) {
		int err = 0;

		if (!name) {
			dev_err(dev, "No interrupt name\n");
			return -EINVAL;
		}

		if (!of_property_read_u32_index(np, "interrupt-is-secure", prop_idx, &val)) {
			irq[prop_idx].is_secure = !!val;
		} else {
			dev_err(dev, "cannot find is-secure value for irq\n");
			return -EINVAL;
		}

		if (!of_property_read_u32_index(np, "interrupt-is-error", prop_idx, &val)) {
			irq[prop_idx].is_error = !!val;
		} else {
			dev_err(dev, "cannot find is-error value for irq\n");
			return -EINVAL;
		}

		irq_num = platform_get_irq(towerci->pdev, prop_idx);
		err = devm_request_irq(dev, irq_num, tower_irq_handler,
					IRQF_NOBALANCING | IRQF_NO_THREAD, name, towerci);
		if (err) {
			dev_err(dev, "Request irq%u %s failed\n", irq_num, name);
			return err;
		}
		irq[prop_idx].irq_num = irq_num;

		prop_idx++;
	}

	/* Enable all IRQs */
	towerci->reg_write(true, towerci->res->start, tnode->node_base,
				 MCN_RAS_BLCK_S + MCN_ERR_EN_OFFSET, MCN_ERR_EN);
	towerci->reg_write(false, towerci->res->start, tnode->node_base,
				 MCN_RAS_BLCK_NS + MCN_ERR_EN_OFFSET, MCN_ERR_EN);

	return 0;
}

static int dump_regs_show(struct seq_file *s, void *p)
{
	int i;
	u32 val;
	struct towerci_dev *towerci = s->private;
	struct node_desc *tnode = towerci->tnode;

	seq_printf(s, "Node: %s\n", dev_name(towerci->dev));
	seq_puts(s, "Reg                  Address     Value\n");
	seq_puts(s, "------------------------------------------\n");
	for (i = 0; i < tnode->regs_count; i++) {
		val = towerci->reg_read(true, towerci->res->start,
			 tnode->node_base, tnode->regs[i].offset);
		seq_printf(s, "%-20s %#llx %#010x\n", tnode->regs[i].name,
			 towerci->res->start + tnode->regs[i].offset, val);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dump_regs);

static ssize_t mcn_inject_ns_error_write(struct file *file, const char __user *ubuf,
		 size_t count, loff_t *ppos)
{
	struct towerci_dev *towerci = file->private_data;
	struct node_desc *tnode = towerci->tnode;

	dev_info(towerci->dev, "Injecting MCN Non Secure error\n");

	towerci->reg_write(false, towerci->res->start, tnode->node_base,
				 MCN_RAS_BLCK_NS + MCN_ERR_PFGCDN_OFFSET, MCN_ERR_CDN);
	towerci->reg_write(false, towerci->res->start, tnode->node_base,
				 MCN_RAS_BLCK_NS + MCN_ERR_PFGCTL_OFFSET, MCN_ERR_INJ);

	*ppos += count;
	return count;
}

static const struct file_operations mcn_inject_ns_error_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = mcn_inject_ns_error_write,
	.llseek = noop_llseek,
};

static ssize_t mcn_inject_s_error_write(struct file *file, const char __user *ubuf,
		 size_t count, loff_t *ppos)
{
	struct towerci_dev *towerci = file->private_data;
	struct node_desc *tnode = towerci->tnode;

	dev_info(towerci->dev, "Injecting MCN Secure error\n");

	towerci->reg_write(true, towerci->res->start, tnode->node_base,
				 MCN_RAS_BLCK_S + MCN_ERR_PFGCDN_OFFSET, MCN_ERR_CDN);
	towerci->reg_write(true, towerci->res->start, tnode->node_base,
				 MCN_RAS_BLCK_S + MCN_ERR_PFGCTL_OFFSET, MCN_ERR_INJ);

	*ppos += count;
	return count;
}

static const struct file_operations mcn_inject_s_error_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = mcn_inject_s_error_write,
	.llseek = noop_llseek,
};

static int towerci_dt_debugfs(struct towerci_dev *towerci)
{
	struct device *dev = towerci->dev;
	struct device *parent_dev;
	struct towerci_dev *tower_global;

	switch (towerci->dev_type) {
	case TOWER_CI_GLOBAL:
		towerci->debugfs_dir = debugfs_create_dir("tower-ci", NULL);
		if (IS_ERR_OR_NULL(towerci->debugfs_dir)) {
			dev_err(dev, "Failed to create parent debugfs directory\n");
			return -EIO;
		}
		break;

	case TOWER_CI_NODE:
		parent_dev = dev->parent;
		tower_global = dev_get_drvdata(parent_dev);

		if (!tower_global || !tower_global->debugfs_dir) {
			dev_info(dev, "Parent debugfs not found\n");
			return -EPROBE_DEFER;
		}

		towerci->debugfs_dir = debugfs_create_dir(dev_name(dev), tower_global->debugfs_dir);
		if (IS_ERR_OR_NULL(towerci->debugfs_dir)) {
			dev_err(dev, "Failed to create node debugfs directory\n");
			return -EIO;
		}

		debugfs_create_file("dump_regs", 0400, towerci->debugfs_dir,
					towerci, &dump_regs_fops);

		if (towerci->tnode->node_type == NODE_TYPE_MCN) {
			debugfs_create_file("inject_error_secure", 0200, towerci->debugfs_dir,
					 towerci, &mcn_inject_s_error_fops);
			debugfs_create_file("inject_error_non_secure", 0200, towerci->debugfs_dir,
					 towerci, &mcn_inject_ns_error_fops);
		}

		break;
	}

	return 0;
}

static int get_node_info(struct towerci_dev *towerci)
{
	struct device *dev = towerci->dev;
	struct resource *res = towerci->res;
	struct node_desc *tnode;
	u32 val;

	towerci->tnode = devm_kzalloc(dev, sizeof(*towerci->tnode), GFP_KERNEL);
	if (!towerci->tnode)
		return -ENOMEM;

	tnode = towerci->tnode;

	tnode->node_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!tnode->node_base) {
		dev_err(dev, "not enough memory\n");
		return -ENOMEM;
	}

	val = readl(tnode->node_base);

	tnode->node_type = FIELD_GET(GENMASK(15, 0), val);
	tnode->node_id = FIELD_GET(GENMASK(31, 16), val);
	dev_info(dev, "Node Type: %#x, Node ID: %#x\n", tnode->node_type, tnode->node_id);

	if (tnode->node_type == NODE_TYPE_ASNI) {
		tnode->regs_count = ARRAY_SIZE(asni_cfg_regs);
		tnode->regs = asni_cfg_regs;
	} else if (tnode->node_type == NODE_TYPE_MCN) {
		tnode->regs_count = ARRAY_SIZE(mcn_cfg_regs);
		tnode->regs = mcn_cfg_regs;
	}

	return 0;
}

static int towerci_dt_init(struct towerci_dev *towerci)
{
	struct device *dev = towerci->dev;
	int ret = 0;

	towerci->res = platform_get_resource(towerci->pdev, IORESOURCE_MEM, 0);
	if (!towerci->res) {
		dev_err(dev, "Memory resource not found\n");
		return -ENODEV;
	}

	dev_info(dev, "Base: %#llx\n", towerci->res->start);
	if (towerci->dev_type == TOWER_CI_NODE)
		ret = get_node_info(towerci);

	return ret;
}

static const struct of_device_id towerci_dt_match[] = {
	{.compatible = "google,tower-ci-global", .data = (void *)TOWER_CI_GLOBAL, },
	{.compatible = "google,tower-ci-node", .data = (void *)TOWER_CI_NODE, },
	{},
};
MODULE_DEVICE_TABLE(of, towerci_dt_match);

static int towerci_probe(struct platform_device *pdev)
{
	struct towerci_dev *towerci;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	int ret;

	towerci = devm_kzalloc(dev, sizeof(struct towerci_dev), GFP_KERNEL);
	if (!towerci)
		return -ENOMEM;

	towerci->pdev = pdev;
	towerci->dev = &pdev->dev;
	platform_set_drvdata(pdev, towerci);

	match = of_match_device(towerci_dt_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find matching device\n");
		return -EINVAL;
	}

	towerci->dev_type = (enum tower_ci_dev_type)match->data;

	towerci->reg_read = towerci_reg_read;
	towerci->reg_write = towerci_reg_write;

	ret = towerci_dt_init(towerci);
	if (ret)
		return ret;

	ret = towerci_dt_debugfs(towerci);
	if (ret)
		return ret;

	ret = towerci_dt_irq(towerci);
	if (ret)
		return ret;

	if (towerci->dev_type == TOWER_CI_GLOBAL) {
		ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "Failed to populate child devices: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static void towerci_remove(struct platform_device *pdev)
{
	struct towerci_dev *towerci = platform_get_drvdata(pdev);

	if (towerci && towerci->dev_type == TOWER_CI_GLOBAL) {
		debugfs_remove_recursive(towerci->debugfs_dir);
		of_platform_depopulate(&pdev->dev);
	}

	platform_set_drvdata(pdev, NULL);
}

static struct platform_driver towerci_driver = {
	.probe = towerci_probe,
	.remove = towerci_remove,
	.driver = {
		.name = "tower-ci",
		.of_match_table = towerci_dt_match,
	},
};
module_platform_driver(towerci_driver);

MODULE_DESCRIPTION("Tower-CI driver");
MODULE_AUTHOR("Mayank Rungta <mrungta@google.com>");
MODULE_LICENSE("GPL");
