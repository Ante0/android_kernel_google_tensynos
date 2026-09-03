/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Google LWIS Dublin Platform-Specific Functions
 *
 * Copyright (c) 2023 Google, LLC
 */

#ifndef LWIS_PLATFORM_DUBLIN_H_
#define LWIS_PLATFORM_DUBLIN_H_

#include <linux/hashtable.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>

#include "lwis_platform.h"

struct lwis_platform {
	/* Information about power domains */
	int num_pds;
	struct device **pd_devs;
	struct device_link **pd_links;
};

/* QOS Family Name maske for QOS array request on target device */
#define LWIS_QOS_FAMILY_SYNC_BE_MSA BIT(1)
#define LWIS_QOS_FAMILY_SYNC_BE_BTR BIT(2)
#define LWIS_QOS_FAMILY_SYNC_BE_YUV BIT(3)
#define LWIS_QOS_FAMILY_SYNC_GSW_GSE BIT(4)
#define LWIS_QOS_FAMILY_SYNC_GSW_GWE BIT(5)
#define LWIS_QOS_FAMILY_SYNC_FE BIT(6)
#define LWIS_QOS_FAMILY_SYNC_BE_MSA_GSLC BIT(7)
#define LWIS_QOS_FAMILY_SYNC_BE_BTR_GSLC BIT(8)
#define LWIS_QOS_FAMILY_SYNC_BE_YUV_GSLC BIT(9)
#define LWIS_QOS_FAMILY_SYNC_GSW_GSE_GSLC BIT(10)
#define LWIS_QOS_FAMILY_SYNC_GSW_GWE_GSLC BIT(11)
#define LWIS_QOS_FAMILY_SYNC_FE_GSLC BIT(12)
#define LWIS_QOS_FAMILY_SYNC_NUM BIT(13)

#define LWIS_STORE_IRM_REG_RD_BW_AVG 0
#define LWIS_STORE_IRM_REG_RD_PEAK_AVG 1
#define LWIS_STORE_IRM_REG_RD_LATENCY 2
#define LWIS_STORE_IRM_REG_RD_BW_RT 3
#define LWIS_STORE_IRM_REG_WR_BW_AVG 4
#define LWIS_STORE_IRM_REG_WR_PEAK_AVG 5
#define LWIS_STORE_IRM_REG_WR_BW_RT 6
#define LWIS_STORE_IRM_REG_NUM 7

#define LWIS_QOS_FAMILY_BE_MSA 0
#define LWIS_QOS_FAMILY_BE_BTR 1
#define LWIS_QOS_FAMILY_BE_YUV 2
#define LWIS_QOS_FAMILY_GSW_GSE 3
#define LWIS_QOS_FAMILY_GSW_GWE 4
#define LWIS_QOS_FAMILY_FE 5
#define LWIS_QOS_FAMILY_BE_MSA_GSLC 6
#define LWIS_QOS_FAMILY_BE_BTR_GSLC 7
#define LWIS_QOS_FAMILY_BE_YUV_GSLC 8
#define LWIS_QOS_FAMILY_GSW_GSE_GSLC 9
#define LWIS_QOS_FAMILY_GSW_GWE_GSLC 10
#define LWIS_QOS_FAMILY_FE_GSLC 11
#define LWIS_QOS_FAMILY_NUM 12

/* irm register bw clamp value 0xFFFF  */
#define LWIS_IRM_REG_BW_MASK 0xFFFF
/* irm register latency clamp value 0xFFFE  */
#define LWIS_IRM_REG_LATENCY_MASK 0xFFFE

#define NUM_VC 5

struct lwis_devfreq {
	struct devfreq *df;
	struct dev_pm_qos_request df_req;
};

#define LWIS_DEVFREQ_CONSTRAINT_SYNC_ISPBE BIT(1)
#define LWIS_DEVFREQ_CONSTRAINT_SYNC_ISPFE BIT(2)
#define LWIS_DEVFREQ_CONSTRAINT_SYNC_GSW BIT(3)

#define LWIS_AGGREGATED_ISPBE_DF 0
#define LWIS_AGGREGATED_ISPFE_DF 1
#define LWIS_AGGREGATED_GSW_DF 2
#define LWIS_AGGREGATED_DF_NUM 3

/*
 * enum lwis_device_sswrap_key
 * sswrap_key used by lwis device for aggregately bandwidth/frequency vote.
 */
enum lwis_device_sswrap_key { SSWRAP_UNKNOWN = 0, SSWRAP_ISPFE, SSWRAP_ISPBE, SSWRAP_GSW };

/*
 * enum lwis_device_name_key
 * dev_name_key used by lwis device for lwis device level vote.
 */
enum lwis_device_name_key { DEV_UNKNOWN = 0, BE_MSA, BE_BTR, BE_YUV, GSW_GSE, GSW_GWE, ISP_FE };

enum name_map_type {
	/* refer to struct lwis_dev_key_to_name_map and used by dev_name_map */
	STRUCT_DEV_NAME_MAP,
	/* refer to: struct lwis_qos_family_name_map and used by bw_update_qos_family_name_map */
	STRUCT_BW_UPDATE_QOS_FAMILY_NAME_MAP,
	/* refer to: struct lwis_qos_family_name_map and used by freq_update_qos_family_name_map */
	STRUCT_FREQ_UPDATE_QOS_FAMILY_NAME_MAP,
	/* refer to: struct lwis_dev_sswrap_to_name_map and used by sswrap_map */
	STRUCT_SSWRAP_MAP,
};

#define LWIS_DEV_HASH_BITS 8
struct lwis_platform_top_device {
	/* Mutex used at probe time for synchronize access to lwis_dev_freq_hash_table structs */
	struct mutex dev_freq_lock;
	/* Hash table of lwis dev_freqs keyed by sswrap_key */
	DECLARE_HASHTABLE(lwis_dev_freq_hash_table, LWIS_DEV_HASH_BITS);
	/*
	 * Mutex used at probe time synchronize access to lwis_dev_icc_paths_hash_table
	 * or lwis_aggregated_icc_paths_hash_table struct
	 */
	struct mutex icc_path_lock;
	/* Hash table of lwis device icc paths keyed by lwis device name_key */
	DECLARE_HASHTABLE(lwis_dev_icc_paths_hash_table, LWIS_DEV_HASH_BITS);
	/* Hash table of lwis aggregated device icc paths keyed by sswrap_key */
	DECLARE_HASHTABLE(lwis_aggregated_icc_paths_hash_table, LWIS_DEV_HASH_BITS);
};

/* Entry of lwis_dev_freq_hash_table */
struct lwis_aggregated_dev_freq_entry {
	enum lwis_device_sswrap_key sswrap_key;
	int dev_cnt;
	int64_t aggregated_dev_min_freq;
	/* This stores the aggregated lwis devfreq for ispfe/ispbe/gsw
	 * that need runtime QOS update.
	 */
	struct lwis_devfreq aggregated_dev_freq_req;
	struct hlist_node node;
};

/* Entry of lwis_dev_icc_paths_hash_table */
struct lwis_dev_icc_paths_entry {
	enum lwis_device_name_key name_key;
	u32 expected_dev_qos_settings[2][LWIS_STORE_IRM_REG_NUM][NUM_VC];
	char dev_icc_path_name[LWIS_MAX_NAME_STRING_LEN];
	/* ICC path which represents constraints of LWIS device to GMC and GSLC */
	struct google_icc_path *dev_icc_path;
	struct hlist_node node;
};

/* Entry of lwis_aggregated_icc_paths_hash_table */
struct lwis_aggregated_icc_paths_entry {
	enum lwis_device_sswrap_key sswrap_key;
	int dev_cnt;
	char aggregated_icc_path_name[LWIS_MAX_NAME_STRING_LEN];
	/* ICC path which represents constraints of LWIS device to GMC and GSLC */
	struct google_icc_path *aggregated_icc_path;
	struct hlist_node node;
};

#endif /* LWIS_PLATFORM_DUBLIN_H_ */
