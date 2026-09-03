/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS Erie Platform-Specific Functions
 *
 * Copyright (c) 2024 Google, LLC
 */

#ifndef LWIS_PLATFORM_ERIE_H_
#define LWIS_PLATFORM_ERIE_H_

#include <interconnect/google_icc_helper.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/hashtable.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <perf/mbfs.h>

#include "lwis_platform.h"

struct lwis_platform {
	/* Information about power domains */
	int num_pds;
	struct device **pd_devs;
	struct device_link **pd_links;
};

/* ICC path which represents constraints of LWIS device to GMC */
struct lwis_google_icc_paths {
	struct google_icc_path *path_ispbe_btr;
	struct google_icc_path *path_ispbe_msa;
	struct google_icc_path *path_ispbe_yuv;
	struct google_icc_path *path_gsw_gse;
	struct google_icc_path *path_gsw_gwe;
	struct google_icc_path *path_ispfe;
	struct google_icc_path *path_ispbe_aggregate;
	struct google_icc_path *path_ispfe_aggregate;
	struct google_icc_path *path_gsw_aggregate;
};

/* QOS Family Name maske for QOS array request on target device */
#define LWIS_QOS_FAMILY_SYNC_ISPBE_MSA_TNR BIT(1)
#define LWIS_QOS_FAMILY_SYNC_ISPBE_MSA_HDR BIT(2)
#define LWIS_QOS_FAMILY_SYNC_ISPBE_BTR BIT(3)
#define LWIS_QOS_FAMILY_SYNC_ISPBE_YUV BIT(4)
#define LWIS_QOS_FAMILY_SYNC_GCV_GSE BIT(5)
#define LWIS_QOS_FAMILY_SYNC_GCV_GWE BIT(6)
#define LWIS_QOS_FAMILY_SYNC_GCV_CRE BIT(7)
#define LWIS_QOS_FAMILY_SYNC_ISPFE_TOP BIT(8)
#define LWIS_QOS_FAMILY_SYNC_ISPFE_CSISFE BIT(9)
#define LWIS_QOS_FAMILY_SYNC_ISPFE_CORE0 BIT(10)
#define LWIS_QOS_FAMILY_SYNC_ISPFE_CORE1 BIT(11)
#define LWIS_QOS_FAMILY_SYNC_ISPFE_CORE2 BIT(12)

#define LWIS_STORE_IRM_REG_RD_BW_AVG 0
#define LWIS_STORE_IRM_REG_RD_PEAK_AVG 1
#define LWIS_STORE_IRM_REG_RD_LATENCY 2
#define LWIS_STORE_IRM_REG_RD_BW_RT 3
#define LWIS_STORE_IRM_REG_WR_BW_AVG 4
#define LWIS_STORE_IRM_REG_WR_PEAK_AVG 5
#define LWIS_STORE_IRM_REG_WR_BW_RT 6
#define LWIS_STORE_IRM_REG_NUM 7

/* irm register bw clamp value 0xFFFF  */
#define LWIS_IRM_REG_BW_MASK 0xFFFF
/* irm register latency clamp value 0xFFFE  */
#define LWIS_IRM_REG_LATENCY_MASK 0xFFFE

#define NUM_VC 5

/* Maximum valid value of IRM register bandwidth is 0xFFFF */
#define MAX_ISPDEV_BW 0xFFFF

struct lwis_devfreq {
	struct devfreq *df;
	struct dev_pm_qos_request df_req;
};

#define LWIS_DEVFREQ_CONSTRAINT_SYNC_ISPBE BIT(1)
#define LWIS_DEVFREQ_CONSTRAINT_SYNC_ISPFE BIT(2)
#define LWIS_DEVFREQ_CONSTRAINT_SYNC_GCV BIT(3)

#define LWIS_AGGREGATED_ISPBE_DF 0
#define LWIS_AGGREGATED_ISPFE_DF 1
#define LWIS_AGGREGATED_GCV_DF 2
#define LWIS_AGGREGATED_DF_NUM 3

/* Defines the number of handles of memory specific SSWRPs to validate bandwidth votes */
#define LWIS_MEM_OP_LEVEL_HDLS 4
/* Defines the number of handles for devfreq specific SSWRPs to validate clock votes */
#define LWIS_DEVFRQ_OP_LEVEL_HDLS 3

/*
 * enum lwis_device_sswrap_key
 * sswrap_key used by lwis device for aggregately bandwidth/frequency vote.
 */
enum lwis_device_sswrap_key { SSWRP_UNKNOWN = 0, SSWRP_ISPFE, SSWRP_ISPBE, SSWRP_GCV };

/*
 * enum lwis_device_name_key
 * dev_name_key used by lwis device for lwis device level vote.
 */
enum lwis_device_name_key {
	DEV_UNKNOWN = 0,
	ISPBE_MSA_TNR,
	ISPBE_MSA_HDR,
	ISPBE_BTR,
	ISPBE_YUV,
	GCV_GSE,
	GCV_GWE,
	GCV_CRE,
	ISPFE_TOP,
	ISPFE_CSISFE,
	ISPFE_CORE0,
	ISPFE_CORE1,
	ISPFE_CORE2
};

enum name_map_type {
	/* refer to struct lwis_dev_key_to_name_map and used by dev_name_map */
	STRUCT_DEV_NAME_MAP,
	/* refer to: struct lwis_qos_family_name_map and used by bw_update_qos_family_name_map */
	STRUCT_BW_UPDATE_QOS_FAMILY_NAME_MAP,
	/* refer to: struct lwis_qos_family_name_map and used by freq_update_qos_family_name_map */
	STRUCT_FREQ_UPDATE_QOS_FAMILY_NAME_MAP,
	/* refer to: struct lwis_dev_sswrap_to_name_map and used by sswrap_map */
	STRUCT_SSWRP_MAP,
};

/*
 * Idxs for different SSWRPS whose operating levels are monitored
 * via Mailbox File System(mbfs) APIs.
 * SSWRPs allowed to request operating level read back to user.
 * SSWRPS whose operating levels are changed using devfreq interface.
 */
enum mbfs_client_handler_idx { FABHBW = 0, FABMED, MEMSS, GMC, ISPBE, ISPFE, GCV, NUM_HLS };

/*
 * Type of vote to verify using MBFS.
 * This is used to determine the domaine name path
 */
enum mbfs_verification_type { BANDWIDTH = 0, CLOCK };

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

	/* Array to store the ispbe, ispfe, gcv, fabhbw, fabmed, memss and gmc mbfs handler */
	union mbfs_client_handle lwis_max_opp_h[NUM_HLS];
};

/* Entry of lwis_dev_freq_hash_table */
struct lwis_aggregated_dev_freq_entry {
	enum lwis_device_sswrap_key sswrap_key;
	int dev_cnt;
	int64_t aggregated_dev_min_freq;
	/*
	 * This stores the aggregated lwis devfreq for ispfe/ispbe/gcv
	 * that need runtime QOS update.
	 */
	struct lwis_devfreq aggregated_dev_freq_req;
	struct hlist_node node;
};

/* Entry of lwis_dev_icc_paths_hash_table */
struct lwis_dev_icc_paths_entry {
	enum lwis_device_name_key name_key;
	u32 expected_dev_qos_settings[LWIS_STORE_IRM_REG_NUM][NUM_VC];
	char dev_icc_path_name[LWIS_MAX_NAME_STRING_LEN];
	/* ICC path which represents constraints of LWIS device to GMC */
	struct google_icc_path *dev_icc_path;
	struct hlist_node node;
};

/* Entry of lwis_aggregated_icc_paths_hash_table */
struct lwis_aggregated_icc_paths_entry {
	enum lwis_device_sswrap_key sswrap_key;
	int dev_cnt;
	char aggregated_icc_path_name[LWIS_MAX_NAME_STRING_LEN];
	/* ICC path which represents constraints of LWIS device to GMC */
	struct google_icc_path *aggregated_icc_path;
	struct hlist_node node;
};

#endif /* LWIS_PLATFORM_ERIE_H_ */
