/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 */

#ifndef _TOWERCI_H
#define _TOWERCI_H

#include <linux/platform_device.h>
#include <linux/seq_file.h>

/* For accessing privileged registers */
#define SMC_CMD_PRIV_REG	(0x82000504)
#define PRIV_REG_OPTION_READ	0
#define PRIV_REG_OPTION_WRITE	1

#define MCN_RAS_BLCK_NS 0x60000
#define MCN_RAS_BLCK_S 0x64000

#define MCN_ERR_EN_OFFSET 0x8
#define MCN_ERR_EN 0xD

#define MCN_ERR_NUM_GRPS 0x3
#define MCN_ERR_GRP1_OFFSET 0x10
#define MCN_ERR_GRP2_OFFSET 0x50
#define MCN_ERR_GRP3_OFFSET 0x90

#define MCN_ERR_STATUS_OFFSET 0x0
#define MCN_ERR_ADDRL_OFFSET 0x8
#define MCN_ERR_ADDRH_OFFSET 0xC
#define MCN_ERR_MISC_OFFSET 0x10

#define MCN_ERR_PFGCDN_OFFSET 0x810
#define MCN_ERR_PFGCTL_OFFSET 0x808

#define MCN_ERR_CDN 0x1
#define MCN_ERR_INJ 0x80000004

#define MCN_ERRGSR_OFFSET 0xE00

enum tower_ci_dev_type {
	TOWER_CI_GLOBAL,
	TOWER_CI_NODE,
};

struct reg_desc {
	const char * const name;
	int offset;
};

struct node_irq_desc {
	unsigned int irq_num;
	bool is_secure;
	bool is_error;
};

struct node_desc {
	void __iomem *node_base;
	u16 node_type;
	u16 node_id;
	u32 regs_count;
	const struct reg_desc *regs;
	u32 irq_count;
	struct node_irq_desc *irq;
};

struct towerci_dev {
	struct device *dev;
	struct platform_device *pdev;
	struct resource *res;
	enum tower_ci_dev_type dev_type;
	struct node_desc *tnode;
	struct dentry *debugfs_dir;

	u32 (*reg_read)(bool is_secure, phys_addr_t reg_s, void __iomem *reg_ns, u32 offset);
	void (*reg_write)(bool is_secure, phys_addr_t reg_s, void __iomem *reg_ns, u32 offset, u32 val);
};

#if IS_ENABLED(CONFIG_KUNIT)
irqreturn_t tower_irq_handler(int irq_num, void *data);
const char *get_uet_string(u16 uet);
const char *get_ierr_string(u16 ierr);
const char *get_serr_string(u16 serr);
const char *get_sys_opcode_string(u16 sys_opcode, bool sys_opcode_select);

#endif /* IS_ENABLED(CONFIG_KUNIT) */

#endif /* _TOWERCI_H */
