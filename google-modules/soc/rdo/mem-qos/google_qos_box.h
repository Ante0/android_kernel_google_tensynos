/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_QOS_BOX_H
#define _GOOGLE_QOS_BOX_H

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <dt-bindings/mem-qos/google,mem-qos.h>

#include "google_qos_box_debugfs.h"
#include "google_qos_box_reg.h"

enum qos_box_version {
	QOS_BOX_VERSION_RDO = 0,
	QOS_BOX_VERSION_LGA,
	QOS_BOX_VERSION_NUM,
};

struct qos_box_policy {
	union {
		struct {
			union {
				struct {
					u32 arqos_ovrd_en: 1;
					u32 reserved_0: 3;
					u32 arqos_ovrd_val: 2;
					u32 reserved_1: 2;
					u32 awqos_ovrd_en: 1;
					u32 reserved_2: 3;
					u32 awqos_ovrd_val: 2;
					u32 reserved_3: 18;
				};
				u32 val;
			} qos_ovrd_cfg;
			union {
				struct {
					u32 arqos_latmod_en: 1;
					u32 reserved_0: 3;
					u32 arqos_lat_step_th: 12;
					u32 awqos_latmod_en: 1;
					u32 reserved_1: 3;
					u32 awqos_lat_step_th: 12;
				};
				u32 val;
			} qos_latmod_cfg;
			union {
				struct {
					u32 arqos_bwmod_en: 1;
					u32 reserved_0: 3;
					u32 arqos_bw_step_th: 12;
					u32 awqos_bwmod_en: 1;
					u32 reserved_1: 3;
					u32 awqos_bw_step_th: 12;
				};
				u32 val;
			} qos_bwmod_cfg;
			union {
				struct {
					u32 arqos_urgovrd_en: 1;
					u32 reserved_0: 3;
					u32 awqos_urgovrd_en: 1;
					u32 reserved_1: 27;
				};
				u32 val;
			} qos_urgovrd_cfg;
			union {
				struct {
					u32 rurglvl_ovrd_en: 1;
					u32 reserved_0: 3;
					u32 rurglvl_ovrd_val: 2;
					u32 reserved_1: 2;
					u32 wurglvl_ovrd_en: 1;
					u32 reserved_2: 3;
					u32 wurglvl_ovrd_val: 2;
					u32 reserved_3: 18;
				};
				u32 val;
			} urg_ovrd_cfg;
			union {
				struct {
					u32 rurglvl_latmod_en: 1;
					u32 reserved_0: 3;
					u32 rurglvl_lat_step_th: 12;
					u32 wurglvl_latmod_en: 1;
					u32 reserved_1: 3;
					u32 wurglvl_lat_step_th: 12;
				};
				u32 val;
			} urg_latmod_cfg;
			union {
				struct {
					u32 rurglvl_bwmod_en: 1;
					u32 reserved_0: 3;
					u32 rurglvl_bw_step_th: 12;
					u32 wurglvl_bwmod_en: 1;
					u32 reserved_1: 3;
					u32 wurglvl_bw_step_th: 12;
				};
				u32 val;
			} urg_bwmod_cfg;
			union {
				struct {
					u32 rdmo_limit_en: 1;
					u32 wrmo_limit_en: 1;
					u32 reserved_0: 30;
				};
				u32 val;
			} mo_limit_cfg;
			union {
				struct {
					u32 rdmo_limit_trtl0: 8;
					u32 rdmo_limit_trtl1: 8;
					u32 rdmo_limit_trtl2: 8;
					u32 rdmo_limit_trtl3: 8;
				};
				u32 val;
			} rdmo_limit_cfg;
			union {
				struct {
					u32 wrmo_limit_trtl0: 8;
					u32 wrmo_limit_trtl1: 8;
					u32 wrmo_limit_trtl2: 8;
					u32 wrmo_limit_trtl3: 8;
				};
				u32 val;
			} wrmo_limit_cfg;
			union {
				struct {
					u32 rdbw_limit_en: 1;
					u32 reserved_0: 3;
					u32 wrbw_limit_en: 1;
					u32 reserved_1: 27;
				};
				u32 val;
			} bw_limit_cfg;
			union {
				struct {
					u32 rdbw_slot_limit_trtl: 16;
					u32 rdbw_window_limit_trtl: 16;
				};
				u32 val;
			} rdbw_limit_ctrl[4];
			union {
				struct {
					u32 wrbw_slot_limit_trtl: 16;
					u32 wrbw_window_limit_trtl: 16;
				};
				u32 val;
			} wrbw_limit_ctrl[4];
			union {
				struct {
					u32 arqos_rgltr_en: 1;
					u32 reserved_0: 3;
					u32 arqos_rgltr_val: 2;
					u32 reserved_1: 2;
					u32 rgltr_rdbw_gap_en: 1;
					u32 reserved_2: 3;
					u32 rgltr_rdreq_gap: 8;
					u32 reserved_3: 12;
				};
				u32 val;
			} rgltr_rd_cfg;
			union {
				struct {
					u32 awqos_rgltr_en: 1;
					u32 reserved_0: 3;
					u32 awqos_rgltr_val: 2;
					u32 reserved_1: 2;
					u32 rgltr_wrbw_gap_en: 1;
					u32 reserved_2: 3;
					u32 rgltr_wrreq_gap: 8;
					u32 reserved_3: 12;
				};
				u32 val;
			} rgltr_wr_cfg;
			union {
				struct {
					u32 rgltr_rdbw_th_trtl: 16;
					u32 rgltr_wrbw_th_trtl: 16;
				};
				u32 val;
			} rgltr_bw_ctrl[4];
		};
		u32 val[QOS_POLICY_BLOCK_NUM_WORD];
	};
};

struct qcfg {
	union {
		struct {
			u32 vc0_map: 4;
			u32 vc1_map: 4;
			u32 vc2_map: 4;
			u32 vc3_map: 4;
			u32 vc4_map: 4;
			u32 vc5_map: 4;
			u32 vc6_map: 4;
			u32 reserved_0: 4;
		};
		u32 val;
	} vc_map_cfg;
	union {
		struct {
			u32 load_en: 1;
			u32 reserved_0: 3;
			u32 index_sel: 2;
			u32 reserved_1: 26;
		};
		u32 val;
	} qos_policy_cfg;
	union {
		struct {
			u32 cycles_per_slot: 10;
			u32 reserved_0: 6;
			u32 slots_per_window: 4;
			u32 reserved_1: 4;
			u32 slots_per_urgmod: 4;
			u32 reserved_2: 4;
		};
		u32 val;
	} bw_mon_cfg;
	union {
		struct {
			u32 rd_mon_vc_filter: 7;
			u32 reserved_0: 9;
			u32 wr_mon_vc_filter: 7;
			u32 reserved_1: 9;
		};
		u32 val;
	} vc_filter_cfg;
	u32 hw_policy_idx_map[NUM_QOS_POLICY];
};

struct qos_box_ops;

struct qos_box_config_delay {
	bool enable;
	u64 delay_ns;
	u64 last_ts;
};

struct qos_box_null_policy {
	bool enable;
	u8 idx;
	struct qos_box_policy policy;
};

struct qos_box_desc {
	enum qos_box_version version;
	u32 num_hw_vc;
};

struct qos_box_dev {
	struct list_head node;
	struct device *dev;
	u32 index;
	const char *name;
	/* sw scenario policy settings */
	struct qos_box_policy *scenario_arr[NUM_MEM_QOS_SCENARIO];
	/* base addr of qos_box CSRs */
	void __iomem *base_addr;
	/* qos_box per-platform descriptor */
	struct qos_box_desc desc;
	/* SW cache of qos_box CSRs */
	struct qcfg config;
	/* have VC_MAP_CFG init value */
	bool have_vc_map_cfg_init_val;
	/* protect internal struct/register access */
	spinlock_t lock;
	/* debugfs for single qos_box instance */
	struct qos_box_dbg *dbg;
	/* qos_box ops */
	struct qos_box_ops *ops;
	/* load_en delay */
	struct qos_box_config_delay load_en_delay;
	/* null policy */
	struct qos_box_null_policy null_policy;
	/* policy value of power-on-reset */
	struct qos_box_policy por_policy;
	/* slot duration */
	u32 slot_dur_ns;
	/* active scenario - SW view */
	u32 active_scenario;
	/* actual scenario in HW register */
	u32 hw_scenario;
	/* maintain RPM active status */
	bool is_rpm_active;
	struct work_struct debugfs_work;
};

int qos_box_setting_restore(struct qos_box_dev *qos_box_dev);
int qos_box_rpm_get(struct qos_box_dev *qos_box_dev);
void qos_box_rpm_put(struct qos_box_dev *qos_box_dev);

struct qos_box_ops {
	int (*select_config)(struct qos_box_dev *qos_box_dev, u32 use_case_idx);
};

extern struct platform_driver google_qos_box_platform_driver;

#endif /* _GOOGLE_QOS_BOX_H */
