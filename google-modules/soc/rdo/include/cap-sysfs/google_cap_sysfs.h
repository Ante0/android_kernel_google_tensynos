/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024 Google LLC */

#ifndef _GOOGLE_CAP_SYSFS_H
#define _GOOGLE_CAP_SYSFS_H

#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/build_bug.h>
#include <linux/types.h>

#define READ_TIME 5
#define MAX_DVFS_LEVELS 24
#define GTC_TICKS_PER_MS 38400
#define KERNEL_STATSBUF_SHMEM_LAYOUT_VERSION 1

/*
 * Following struct must match the ones in
 * "interfaces/protocols/cap/include/interfaces/protocols/cap/statsbuf.h"
 */

/*
 * This structure describe the cap stats metadata
 *
 * @stats_offset: stats offset from the start of the struct where this struct is embedded
 * @stats_size: size of individual stats
 * @names_offset: string names offset from the start of the struct where this struct is embedded
 * @names_size: total size of string names buffer
 * @count: total number of stats
 */
struct cap_statsbuf_metadata {
	u32 stats_offset;
	u32 stats_size;
	u32 names_offset;
	u32 names_size;
	u32 count;
} __packed __aligned(4);
static_assert(sizeof(struct cap_statsbuf_metadata) == 20);

/*
 * This structure describe the shared memory layout between CAP and kernel
 * for stats sharing purpose.
 *
 * @layout_version: Version number of this structure/layout
 * @power_state_stats_md: metadata for power state stats
 * @dvfs_stats_md: metadata for dvfs stats
 * @aging_clc_stats_md: metadata for aging_clc stats
 *
 */
struct cap_statsbuf_shmem_header {
	u32 layout_version;
	struct cap_statsbuf_metadata power_state_stats_md;
	struct cap_statsbuf_metadata dvfs_stats_md;
	struct cap_statsbuf_metadata aging_clc_stats_md;
	/* Add entries for future arrays here */
} __packed __aligned(8);
static_assert(sizeof(struct cap_statsbuf_shmem_header) == 64);

struct common_power_state_stats {
	u64 last_entry_start_ts;
	u64 last_entry_end_ts;
	u64 last_exit_start_ts;
	u64 last_exit_end_ts;
	u64 last_entry_sched_ts;
	u64 last_exit_sched_ts;
	u64 state_entry_count;
	u64 total_time_in_state_ticks;
} __packed __aligned(8);
static_assert(sizeof(struct common_power_state_stats) == 64);

struct cap_statsbuf_power_state_stats {
	struct common_power_state_stats stats;
	u16 power_state_id;
} __packed __aligned(8);
static_assert(sizeof(struct cap_statsbuf_power_state_stats) == 72);


struct cap_statsbuf_dvfs_level_stats {
	u64 entry_count;
	u64 total_residency_ticks;
	u64 last_entry_ts;
} __packed __aligned(8);
static_assert(sizeof(struct cap_statsbuf_dvfs_level_stats) == 24);

struct cap_statsbuf_dvfs_stats {
	struct cap_statsbuf_dvfs_level_stats domain_level_stats[MAX_DVFS_LEVELS];
	u8 domain_current_level;
	u8 domain_target_level;
	u16 domain_id;
} __packed __aligned(8);
static_assert(sizeof(struct cap_statsbuf_dvfs_stats) == 584);

enum stats_type {
	TYPE_CAP_CPU_POWER_STATS,
	TYPE_CAP_DVFS_STATS,
	TYPE_CAP_CLC_AGING_STATS,
	TYPE_TFA_CPU_POWER_STATS,
};

enum aging_clc_status {
	NO_MEASURE,
	CLC_DISABLED,
	SENSOR_READ_FAILURE,
	CLUSTER_NOT_READY,
	SENSOR_READ_OUT_OF_RANGE,
	MEASUREMENT_EXPIRED,
	INVALID_VOLTAGE,
	SUCCESS,
} __packed;

struct cap_statsbuf_clc_aging_stats {
	u32 last_measurement_ts;
	s32 last_aging_sensor_measurement;
	u8 error_count;
	enum aging_clc_status last_status_code;
	u8 rail_id;
} __packed __aligned(8);
static_assert(sizeof(struct cap_statsbuf_clc_aging_stats) == 16);

struct stats_region {
	struct list_head list;
	void *__iomem start_addr;
	u32 idx_size;
	u32 number;
	int stats_type;
	u32 *ids;
	char *names_buf;
	const char **names;
	void *read_first;
	void *read_second;
};

struct dvfs_attribute {
	struct list_head list;
	struct stats_region *dvfs_stats_region;
	struct device_attribute *dev_attr;
	int first_available_level;
	int last_available_level;
	int id;
};

struct aging_clc_attribute {
	struct list_head list;
	struct stats_region *aging_clc_region;
	struct kobject *kobj;
	int id;
};

struct timestamps_buffer {
	u64 last_entry_start_ts;
	u64 last_entry_end_ts;
	u64 last_exit_start_ts;
	u64 last_exit_end_ts;
	u64 last_entry_sched_ts;
	u64 last_exit_sched_ts;
} __packed __aligned(8);
static_assert(sizeof(struct timestamps_buffer) == 48);

int read_cpu_pd_latency_stats(u32 power_state,
			struct timestamps_buffer *ts_buff,
			enum stats_type s_type);

#endif // _GOOGLE_CAP_SYSFS_H
