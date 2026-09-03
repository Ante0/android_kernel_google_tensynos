/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024 Google LLC */
#ifndef _GOOGLE_DVFS_HELPER_H
#define _GOOGLE_DVFS_HELPER_H

#include <linux/platform_device.h>
#include <linux/types.h>

#define MAX_DVFS_NAME_LEN (8)
#define FREQ_HZ_TO_KHZ(freq) ((freq) / 1000)
#define FREQ_HZ_TO_MHZ(freq) ((freq) / 1000000)
#define FREQ_KHZ_TO_MHZ(freq) ((freq) / 1000)
#define FREQ_KHZ_TO_HZ(freq) ((freq) * 1000)
#define FREQ_MHZ_TO_KHZ(freq) ((freq) * 1000)
#define FREQ_MHZ_TO_HZ(freq) (((u64)freq) * 1000000)
#define VOLT_MV_TO_UV(volt) (((u32)volt) * 1000)

struct dvfs_domain_info {
	char name[MAX_DVFS_NAME_LEN];
	u8 pwrblk_id;
	u8 num_levels;
	s8 max_supported_level;
	s8 min_supported_level;
	struct dvfs_opp_info *table;
};

int dvfs_helper_get_domain_info(struct device * const dev, struct device_node * const node,
				struct dvfs_domain_info **info);
int dvfs_helper_add_opps_to_device(struct device *dev, struct device_node *node);
int dvfs_helper_add_opps_to_device_by_name(struct device * const dev, const char *name);
const char *dvfs_helper_domain_id_to_name(u16 domain_idx);
s64 dvfs_helper_round_rate(struct dvfs_domain_info const * const di, const u64 Hz);
s64 dvfs_helper_lvl_to_freq_exact(struct dvfs_domain_info const * const di, const unsigned int lvl);
int dvfs_helper_freq_to_lvl_exact(struct dvfs_domain_info const * const di, const u64 Hz);
s64 dvfs_helper_lvl_to_freq_ceil(struct dvfs_domain_info const * const di, const unsigned int lvl);
int dvfs_helper_freq_to_lvl_ceil(struct dvfs_domain_info const * const di, const u64 Hz);
s64 dvfs_helper_lvl_to_freq_floor(struct dvfs_domain_info const * const di, const unsigned int lvl);
int dvfs_helper_freq_to_lvl_floor(struct dvfs_domain_info const * const di, const u64 Hz);
s64 dvfs_helper_get_domain_opp_frequency_mhz(u16 domain_idx, const unsigned int lvl);
struct dvfs_domain_info *dvfs_helper_get_domain(const char *name);
struct dvfs_domain_info *dvfs_helper_get_domain_from_id(u16 domain_idx);
bool dvfs_helper_ready(void);
#endif /* _GOOGLE_DVFS_HELPER_H */
