// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC.
 */

#include "sequence.h"

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>

static bool emulation_flag;

static_assert(sizeof(struct csr_psm_status) == sizeof(u32));

#define CLK_GATE_CTRL_MODE_OFFSET(CLK_BASE)	((CLK_BASE) + 0x0)
#define CLK_GATE_CLK_EN_OFFSET(CLK_BASE)	((CLK_BASE) + 0x4)
#define CLK_GATE_RST_VAL_OFFSET(CLK_BASE)	((CLK_BASE) + 0xC)
#define CLK_GATE_STATUS_OFFSET(CLK_BASE)	((CLK_BASE) + 0x1C)

struct cg_status_csr {
	union {
		struct {
			u32 qstop : 4;
			u32 qrun : 4;
			u32 clk_status : 4;
			u32 rst_status : 4;
			u32 ret_rst_status : 4;
			u32 reserved : 12;
		};
		u32 reg_val;
	};
} __packed;

struct rst_val {
	union {
		struct {
			u32 rst_val_n : 1;
			u32 __reserved0 : 7;
			u32 ret_rst_val_n : 1;
			u32 __reserved1 : 23;
		};
		u32 reg_val;
	};
} __packed;

enum ctrl_mode {
	CTRL_MODE_SW,
	CTRL_MODE_HW,
};

enum clk_en {
	CLK_EN_DISABLE,
	CLK_EN_ENABLE,
};

int lcm_clk_en(struct power_domain *pd, void __iomem *clk_base, bool en)
{
	struct cg_status_csr target_mask = {.clk_status = 1}, target = {0};
	void __iomem *addr;

	addr = CLK_GATE_CTRL_MODE_OFFSET(clk_base);
	writel(CTRL_MODE_SW, addr);
	addr = CLK_GATE_CLK_EN_OFFSET(clk_base);
	writel(en ? CLK_EN_ENABLE : CLK_EN_DISABLE, addr);
	addr = CLK_GATE_CTRL_MODE_OFFSET(clk_base);
	writel(CTRL_MODE_HW, addr);

	addr = CLK_GATE_STATUS_OFFSET(clk_base);
	target.clk_status = en ? 1 : 0;

	return poll_for(pd, addr, target_mask.reg_val, target.reg_val, eq);
}

int lcm_rst_deassert(struct power_domain *pd, void __iomem *clk_base, bool deassert)
{
	struct cg_status_csr target_mask = {.rst_status = 1}, target = {0};
	struct rst_val rst_val = {0};
	void __iomem *addr;

	addr = CLK_GATE_CTRL_MODE_OFFSET(clk_base);
	writel(CTRL_MODE_SW, addr);
	rst_val.rst_val_n = deassert ? 1 : 0;
	addr = CLK_GATE_RST_VAL_OFFSET(clk_base);
	writel(rst_val.reg_val, addr);
	addr = CLK_GATE_CTRL_MODE_OFFSET(clk_base);
	writel(CTRL_MODE_HW, addr);

	addr = CLK_GATE_STATUS_OFFSET(clk_base);
	target.rst_status = deassert ? 1 : 0;

	return poll_for(pd, addr, target_mask.reg_val, target.reg_val, eq);
}

void set_emulation_flag(bool in_emulation)
{
	emulation_flag = in_emulation;
}

bool get_emulation_flag(void)
{
	return emulation_flag;
}

int poll_for(struct power_domain *pd, void __iomem *addr, u32 mask, u32 target,
	     bool (*cmp)(u32, u32))
{
	u32 val;
	u32 delay_us = 1000;
	u32 timeout_us = 100000;
	int ret;

	if (emulation_flag) {
		delay_us *= EMULATION_DELAY_MULTIPLIER;
		timeout_us *= EMULATION_TIMEOUT_MULTIPLIER;
	}

	ret = readl_poll_timeout(addr, val, cmp((val & mask), target), delay_us,
				 timeout_us);
	if (ret != 0) {
		panic("%s: Poll timeout (val: %#x, mask: %#x, target: %#x)\n",
			pd->genpd.name, val, mask, target);
	}

	return ret;
}

static int poll_for_psm_state_common(struct power_domain *pd,
				     void __iomem *psm_addr,
				     u8 target_psm_state, bool (*cmp)(u32, u32))
{
	struct csr_psm_status status_mask = { .curr_state = GENMASK(3, 0),
					      .state_valid = 1 };
	struct csr_psm_status target = { .curr_state = target_psm_state,
					 .state_valid = 1 };

	return poll_for(pd, psm_addr, status_mask.reg_val, target.reg_val, cmp);
}

int poll_for_psm_state_eq(struct power_domain *pd, void __iomem *psm_addr,
			  u8 target_psm_state)
{
	return poll_for_psm_state_common(pd, psm_addr, target_psm_state, eq);
}

int poll_for_psm_state_le(struct power_domain *pd, void __iomem *psm_addr,
			  u8 target_psm_state)
{
	return poll_for_psm_state_common(pd, psm_addr, target_psm_state, le);
}

int poll_for_psm_state_ge(struct power_domain *pd, void __iomem *psm_addr,
			  u8 target_psm_state)
{
	return poll_for_psm_state_common(pd, psm_addr, target_psm_state, ge);
}
