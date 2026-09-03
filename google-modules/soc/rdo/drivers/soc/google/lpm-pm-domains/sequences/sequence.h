/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Google LLC.
 */

#ifndef __POWER_CONTROLLER_LPM_SEQUENCES_SEQUENCE_H__
#define __POWER_CONTROLLER_LPM_SEQUENCES_SEQUENCE_H__

#include <linux/io.h>
#include <linux/pm_domain.h>

#include <dt-bindings/power/genpd_lga.h>
#include <dt-bindings/power/genpd_mbu.h>

#include "../lpm-pm-domains.h"

#define EMULATION_TIMEOUT_MULTIPLIER 100
#define EMULATION_DELAY_MULTIPLIER 10

struct power_ops {
	int (*power_on)(struct power_domain *pd);
	int (*power_off)(struct power_domain *pd);
};

struct sswrp_power_desc {
	const char *const *reg_names;
	u8 region_count;
	struct power_ops *power_ops_table;
	u8 power_ops_table_size;
	const char *const syscon_name;
	u8 syscon_count;
};

struct csr_psm_status {
	union {
		struct {
			u8 curr_state : 4;
			u8 state_valid : 1;
			u8 psm_sts : 2;
			u8 sw_seq_done : 1;
			u8 psm_err : 1;
			u8 seq_err : 1;
			u32 reserved : 22;
		};
		u32 reg_val;
	};
};

static inline bool eq(u32 l, u32 r)
{
	return l == r;
}

static inline bool le(u32 l, u32 r)
{
	return l <= r;
}

static inline bool ge(u32 l, u32 r)
{
	return l >= r;
}

int poll_for(struct power_domain *pd, void __iomem *addr, u32 mask, u32 target,
	     bool (*cmp)(u32, u32));

int poll_for_psm_state_eq(struct power_domain *pd, void __iomem *psm_addr,
			  u8 target_psm_state);

int poll_for_psm_state_le(struct power_domain *pd, void __iomem *psm_addr,
			  u8 target_psm_state);

int poll_for_psm_state_ge(struct power_domain *pd, void __iomem *psm_addr,
			  u8 target_psm_state);

static inline int poll_for_psm_state(struct power_domain *pd,
				     void __iomem *psm_addr,
				     u8 target_psm_state)
{
	return poll_for_psm_state_eq(pd, psm_addr, target_psm_state);
}

int lcm_clk_en(struct power_domain *pd, void __iomem *clk_base, bool en);
int lcm_rst_deassert(struct power_domain *pd, void __iomem *clk_base, bool deassert);

void set_emulation_flag(bool in_emulation);
bool get_emulation_flag(void);

extern const struct sswrp_power_desc mbu_cpuacc_power_desc_table;
extern const struct sswrp_power_desc mbu_dpu_power_desc_table;
extern const struct sswrp_power_desc mbu_g2d_power_desc_table;
extern const struct sswrp_power_desc mbu_hsio_s_power_desc_table;
extern const struct sswrp_power_desc mbu_ispfe_power_desc_table;
extern const struct sswrp_power_desc mbu_pcie_power_desc_table;

#endif /* __POWER_CONTROLLER_LPM_SEQUENCES_SEQUENCE_H__ */
