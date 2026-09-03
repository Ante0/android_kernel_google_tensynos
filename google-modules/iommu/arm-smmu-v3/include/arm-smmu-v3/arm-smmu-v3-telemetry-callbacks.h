/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM_SMMU_V3_TELEMETRY_CALLBACKS_H
#define __ARM_SMMU_V3_TELEMETRY_CALLBACKS_H

#include <linux/types.h>

struct sz_stats {
	u32 non_cont[4];
	u32 cont[4];
	u32 visited_tables;
	unsigned long next_iova;
	u32 pgtables_used;
	u32 empty_pgtables;
};

struct arm_smmu_v3_telemetry_cb {
	bool (*is_enabled)(void);
	void (*s1_pages_tel)(int count);
	void (*atomic_pages_tel)(int count);
};

#endif /* __ARM_SMMU_V3_TELEMETRY_CALLBACKS_H */
