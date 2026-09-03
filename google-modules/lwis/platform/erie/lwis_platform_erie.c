// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Erie Platform-Specific Functions
 *
 * Copyright (c) 2024 Google, LLC
 */

#include "lwis_platform_erie.h"

#include "lwis_commands.h"
#include "lwis_device.h"
#include "lwis_device_ioreg.h"
#include "lwis_device_top.h"
#include "lwis_platform.h"
#include "lwis_debug.h"
#include "lwis_trace.h"

#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/minmax.h>
#include <linux/pm_domain.h>
#include <linux/string.h>
#include <linux/units.h>
#include <interconnect/google_irm_api.h>
#include <mem-qos/google_qos_box_reg_api.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/perf_domain.h>
#include <soc/google/google-cdd.h>
#include <soc/google/pt.h>
#include <uapi/drm/drm_fourcc.h>

/* QOS Voting Entity */
#define VOTE_GMC fourcc_code('G', 'M', 'C', ' ')
#define VOTE_GSLC fourcc_code('G', 'S', 'L', 'C')

/* Compare the set_val and irm_reg_read_val */
#define IRM_REG_VAL_CMP(qos_set_val, irm_rd_val) ((qos_set_val) == (irm_rd_val) ? 0 : 1)

/* CLAMP_VALUE used for clamp irm DRIVER VALUE to max_val if aggregated votes > max_val */
#define CLAMP_VALUE(input, max_val) (((input) > (max_val)) ? (max_val) : ((input) & (max_val)))

#define KHZ_TO_HZ(n) ((n) * HZ_PER_KHZ)

/*
 * Platform specific map between enum sswrap_key and device name
 * sswrap_key worked as hash_key by hashtable lwis_dev_freq_hash_table and
 * lwis_aggregated_icc_paths_hash_table for hashing purpose.
 */
static const struct lwis_dev_sswrap_to_name_map {
	enum lwis_device_sswrap_key sswrap_key;
	const char *dev_name;
} sswrap_map[] = { { SSWRP_ISPBE, "ispbe-msa-tnr" }, { SSWRP_ISPBE, "ispbe-msa-hdr" },
		   { SSWRP_ISPBE, "ispbe-btr" },     { SSWRP_ISPBE, "ispbe-yuv" },
		   { SSWRP_GCV, "gcv-gse" },	     { SSWRP_GCV, "gcv-gwe" },
		   { SSWRP_GCV, "gcv-cre" },	     { SSWRP_ISPFE, "ispfe-top" },
		   { SSWRP_ISPFE, "ispfe-csisfe" },  { SSWRP_ISPFE, "ispfe-core0" },
		   { SSWRP_ISPFE, "ispfe-core1" },   { SSWRP_ISPFE, "ispfe-core2" },
		   { SSWRP_UNKNOWN, NULL } };

/*
 * Platform specific map between enum type name_key and device name
 * name_key worked as hash_key by hashtable lwis_dev_icc_paths_hash_table
 * hashing purpose.
 */
static const struct lwis_dev_key_to_name_map {
	enum lwis_device_name_key dev_name_key;
	enum lwis_device_sswrap_key sswrap_key;
	const char *dev_name;
} dev_name_map[] = { { ISPBE_MSA_TNR, SSWRP_ISPBE, "ispbe-msa-tnr" },
		     { ISPBE_MSA_HDR, SSWRP_ISPBE, "ispbe-msa-hdr" },
		     { ISPBE_BTR, SSWRP_ISPBE, "ispbe-btr" },
		     { ISPBE_YUV, SSWRP_ISPBE, "ispbe-yuv" },

		     { GCV_GSE, SSWRP_GCV, "gcv-gse" },
		     { GCV_GWE, SSWRP_GCV, "gcv-gwe" },
		     { GCV_CRE, SSWRP_GCV, "gcv-cre" },

		     { ISPFE_TOP, SSWRP_ISPFE, "ispfe-top" },
		     { ISPFE_CSISFE, SSWRP_ISPFE, "ispfe-csisfe" },
		     { ISPFE_CORE0, SSWRP_ISPFE, "ispfe-core0" },
		     { ISPFE_CORE1, SSWRP_ISPFE, "ispfe-core1" },
		     { ISPFE_CORE2, SSWRP_ISPFE, "ispfe-core2" },

		     { DEV_UNKNOWN, SSWRP_UNKNOWN, NULL } };

static const struct lwis_clock_qos_family_name_map {
	enum lwis_device_sswrap_key sswrap_key;
	int qos_update_sync_mask;
	const char *qos_family_name;
} freq_update_qos_family_name_map[] = {
	{ SSWRP_ISPBE, LWIS_DEVFREQ_CONSTRAINT_SYNC_ISPBE, "ispbe" },
	{ SSWRP_GCV, LWIS_DEVFREQ_CONSTRAINT_SYNC_GCV, "gcv" },
	{ SSWRP_ISPFE, LWIS_DEVFREQ_CONSTRAINT_SYNC_ISPFE, "ispfe" },
	{ SSWRP_UNKNOWN, 0, NULL }
};

static const struct lwis_bandwidth_qos_family_name_map {
	enum lwis_device_name_key dev_name_key;
	enum lwis_device_sswrap_key sswrap_key;
	int qos_update_sync_mask;
	const char *qos_family_name;
} bw_update_qos_family_name_map[] = {
	{ ISPBE_MSA_TNR, SSWRP_ISPBE, LWIS_QOS_FAMILY_SYNC_ISPBE_MSA_TNR, "ispbe_msa_tnr" },
	{ ISPBE_MSA_HDR, SSWRP_ISPBE, LWIS_QOS_FAMILY_SYNC_ISPBE_MSA_HDR, "ispbe_msa_hdr" },
	{ ISPBE_BTR, SSWRP_ISPBE, LWIS_QOS_FAMILY_SYNC_ISPBE_BTR, "ispbe_btr" },
	{ ISPBE_YUV, SSWRP_ISPBE, LWIS_QOS_FAMILY_SYNC_ISPBE_YUV, "ispbe_yuv" },

	{ GCV_GSE, SSWRP_GCV, LWIS_QOS_FAMILY_SYNC_GCV_GSE, "gcv_gse" },
	{ GCV_GWE, SSWRP_GCV, LWIS_QOS_FAMILY_SYNC_GCV_GWE, "gcv_gwe" },
	{ GCV_CRE, SSWRP_GCV, LWIS_QOS_FAMILY_SYNC_GCV_CRE, "gcv_cre" },

	{ ISPFE_TOP, SSWRP_ISPFE, LWIS_QOS_FAMILY_SYNC_ISPFE_TOP, "ispfe_top" },
	{ ISPFE_CSISFE, SSWRP_ISPFE, LWIS_QOS_FAMILY_SYNC_ISPFE_CSISFE, "ispfe_csisfe" },
	{ ISPFE_CORE0, SSWRP_ISPFE, LWIS_QOS_FAMILY_SYNC_ISPFE_CORE0, "ispfe_core0" },
	{ ISPFE_CORE1, SSWRP_ISPFE, LWIS_QOS_FAMILY_SYNC_ISPFE_CORE1, "ispfe_core1" },
	{ ISPFE_CORE2, SSWRP_ISPFE, LWIS_QOS_FAMILY_SYNC_ISPFE_CORE2, "ispfe_core2" },

	{ DEV_UNKNOWN, SSWRP_UNKNOWN, 0, NULL }
};

static const char *mbfs_client_dname_map[NUM_HLS] = {
	[FABHBW] = "fabhbw", [FABMED] = "fabmed", [MEMSS] = "memss", [GMC] = "gmc",
	[ISPBE] = "ispbe",   [ISPFE] = "ispfe",	  [GCV] = "gcv"
};

/*
 * Map for quick access to devfreq MBFS handle index for a given requesting entity.
 * Number of entries should match LWIS_DEVFRQ_OP_LEVEL_HDLS.
 */
static const struct lwis_dname_mbfs_hdl_map {
	const char *requesting_entity;
	enum mbfs_client_handler_idx hdl_idx;
} dname_mbfs_hdl_map[] = {
	{ "ispbe", ISPBE },
	{ "ispfe", ISPFE },
	{ "gcv", GCV },
};

bool lwis_erie_debug;
module_param(lwis_erie_debug, bool, 0644);

static enum lwis_device_sswrap_key sswrap_key_int32_to_enum(int32_t dev_sswrap_key)
{
	/* The following enum mapping refer to the `enum lwis_device_sswrap_key` definition */
	switch (dev_sswrap_key) {
	case 0:
		return SSWRP_UNKNOWN;
	case 1:
		return SSWRP_ISPFE;
	case 2:
		return SSWRP_ISPBE;
	case 3:
		return SSWRP_GCV;
	default:
		pr_err("Invalid dev_sswrap_key = %d\n", dev_sswrap_key);
		return SSWRP_UNKNOWN;
	}
}

/*
 *  fetch_name_idx: Fetch the map idex based on name_map_type and name_str.
 */
static int fetch_name_idx(const char *name_str, enum name_map_type type)
{
	int idx;

	switch (type) {
	case STRUCT_DEV_NAME_MAP:
		for (idx = 0; dev_name_map[idx].dev_name; ++idx) {
			if (strcmp(name_str, dev_name_map[idx].dev_name) == 0)
				break;
		}
		break;
	case STRUCT_FREQ_UPDATE_QOS_FAMILY_NAME_MAP:
		for (idx = 0; freq_update_qos_family_name_map[idx].qos_family_name; ++idx) {
			if (strcmp(name_str,
				   freq_update_qos_family_name_map[idx].qos_family_name) == 0)
				break;
		}
		break;
	case STRUCT_BW_UPDATE_QOS_FAMILY_NAME_MAP:
		for (idx = 0; bw_update_qos_family_name_map[idx].qos_family_name; ++idx) {
			if (strcmp(name_str, bw_update_qos_family_name_map[idx].qos_family_name) ==
			    0)
				break;
		}
		break;
	case STRUCT_SSWRP_MAP:
		for (idx = 0; sswrap_map[idx].dev_name; ++idx) {
			if (strcmp(name_str, sswrap_map[idx].dev_name) == 0)
				break;
		}
		break;
	default:
		pr_warn("Invalid name_map_type\n");
		idx = 0;
	}

	return idx;
}

static void detach_power_domain(struct device *dev, struct lwis_platform *platform, int end_idx)
{
	int i;

	/* pd_links and pd_devs only exist if the device has multiple power domains. */
	if (!platform || !platform->pd_links || !platform->pd_devs)
		return;

	/* The parent power domain(example ISP_FE TOP) needs to be detached at the end
	 * after the child domains(example ISPFE PIPE and CSIS child domains) have been detached.
	 * Therefore, general order is to detach the most recently attached power domain first.
	 */
	for (i = end_idx - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(platform->pd_links[i]))
			device_link_del(platform->pd_links[i]);
		if (!IS_ERR_OR_NULL(platform->pd_devs[i]))
			dev_pm_domain_detach(platform->pd_devs[i], /* power_off */ true);
	}
}

static int attach_power_domain(struct lwis_device *lwis_dev)
{
	int i;
	int ret = 0;
	int count = 0;
	struct lwis_platform *platform = NULL;
	struct device *dev = NULL;

	if (!lwis_dev)
		return -ENODEV;

	platform = lwis_dev->platform;
	dev = lwis_dev->k_dev;

	count = of_count_phandle_with_args(dev->of_node, "power-domains", NULL);

	platform->num_pds = count < 0 ? 0 : count;

	/*
	 * The linux genpd framework automatically attaches the domain to the device if
	 * a device only has a single domain specified in the device tree. This is a legacy
	 * behavior which is inconsistent with when there are multiple domains specified.
	 * If there are multiple domains specified, then linux doesn't attach any pd
	 * and all of the pds need to be attached individually.
	 */
	if (platform->num_pds <= 1) {
		dev_info(lwis_dev->dev, "Found %d power-domains. Short-circuiting\n",
			 platform->num_pds);
		return 0;
	}

	platform->pd_devs =
		devm_kmalloc_array(dev, platform->num_pds, sizeof(*platform->pd_devs), GFP_KERNEL);
	if (!platform->pd_devs)
		return -ENOMEM;
	platform->pd_links =
		devm_kmalloc_array(dev, platform->num_pds, sizeof(*platform->pd_links), GFP_KERNEL);
	if (!platform->pd_links)
		return -ENOMEM;

	for (i = 0; i < platform->num_pds; i++) {
		platform->pd_devs[i] = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR(platform->pd_devs[i])) {
			dev_err(lwis_dev->dev, "Failed to attach power domain at index %d\n", i);
			ret = PTR_ERR(platform->pd_devs[i]);
			detach_power_domain(dev, platform, i);
			goto devlink_err;
		}
		platform->pd_links[i] = device_link_add(dev, platform->pd_devs[i],
							DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS);
		if (!platform->pd_links[i]) {
			dev_err(platform->pd_devs[i], "Failed to add dev link at index %d!\n", i);
			ret = -ENODEV;
			goto devlink_err;
		}
	}

	return 0;
devlink_err:
	--i;
	for (; i >= 0; --i)
		device_link_del(platform->pd_links[i]);
	return ret;
}

static int iommu_fault_handler(struct iommu_domain *domain, struct device *dev, unsigned long iova,
			       int flags, void *handler_token)

{
	struct lwis_device *lwis_dev = (struct lwis_device *)handler_token;
	struct lwis_mem_page_fault_event_payload event_payload;

	pr_err("############ LWIS IOMMU PAGE FAULT ############\n");
	pr_err("\n");
	dev_err(lwis_dev->dev, "IOMMU page request fault!\n");
	dev_err(lwis_dev->dev, "da 0x%08lX flags 0x%08X\n", iova, flags);
	pr_err("\n");
	lwis_debug_print_transaction_info(lwis_dev);
	pr_err("\n");
	lwis_debug_print_register_io_history(lwis_dev);
	pr_err("\n");
	lwis_debug_print_event_states_info(lwis_dev, /*lwis_event_dump_cnt=*/-1);
	pr_err("\n");
	lwis_debug_print_buffer_info(lwis_dev);
	pr_err("\n");
	pr_err("###############################################\n");

	event_payload.fault_address = iova;
	event_payload.fault_flags = flags;
	lwis_device_error_event_emit(lwis_dev, LWIS_ERROR_EVENT_ID_MEMORY_PAGE_FAULT,
				     &event_payload, sizeof(event_payload));

#ifdef ENABLE_PAGE_FAULT_PANIC
	return -EFAULT;
#else
	return -EAGAIN;
#endif /* ENABLE_PAGE_FAULT_PANIC */
}

static int mbfs_client_handler_reuse_or_probe_via_domain_name(struct lwis_device *lwis_dev,
							      union mbfs_client_handle *client_h,
							      const char *domain_name,
							      enum mbfs_verification_type type)
{
	char mbfs_domain_path[64] = {};
	enum mbfs_error_code mbfs_ret;

	if (!is_mbfs_handle_invalid(*client_h))
		return 0;

	if (type == BANDWIDTH) {
		/*
		 * agg.max.gov indicates the max OPP level that MIPM
		 * is voting for via max aggregated IRM vote.
		 */
		scnprintf(mbfs_domain_path, sizeof(mbfs_domain_path),
			  "cpm/perf/domains/%s/agg.max.gov", domain_name);
	} else {
		/*
		 * agg.max.devfreq indicates the max OPP index that
		 * the Linux devfreq is voting for.
		 */
		scnprintf(mbfs_domain_path, sizeof(mbfs_domain_path),
			  "cpm/perf/domains/%s/agg.max.devfreq", domain_name);
	}

	mbfs_ret = mbfs_get_handle(mbfs_domain_path, client_h);
	if (mbfs_ret != MBFS_OK) {
		dev_err(lwis_dev->dev, "Failed to get %s mbfs handler: %s\n", domain_name,
			get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	return 0;
}

static int mbfs_verify_opp_level_via_domain_name(struct lwis_device *lwis_dev,
						 const char *domain_name,
						 union mbfs_client_handle client_h,
						 const uint64_t expected_opp_level)
{
	enum mbfs_error_code mbfs_ret;
	union val64 mbfs_op_level;

	mbfs_ret = mbfs_read_file(client_h, &mbfs_op_level);
	if (mbfs_ret != MBFS_OK) {
		dev_err(lwis_dev->dev, "MBFS opp level read failed on handler(%s): err_str(%s)\n",
			domain_name, get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	/*
	 * We expect MBFS read op level value to be smaller than or equal to the expected op level.
	 * A lower opp level value represents a higher frequency.
	 */
	if (mbfs_op_level.number > expected_opp_level) {
		dev_err(lwis_dev->dev,
			"MBFS got unexpected opp_level(%llu): expected_opp_level(%llu)",
			mbfs_op_level.number, expected_opp_level);
		return -EIO;
	}

	return 0;
}

static int get_icc_paths_by_name(struct lwis_device *lwis_dev, struct google_icc_path **path,
				 const char *name)
{
	*path = google_devm_of_icc_get(lwis_dev->k_dev, name);
	if (IS_ERR_OR_NULL(*path)) {
		dev_err(lwis_dev->dev, "google_devm_of_icc_get() failed: %s, ret = %ld\n", name,
			PTR_ERR(*path));
		return -ENODEV;
	}
	if (lwis_erie_debug)
		dev_info(lwis_dev->dev, "Successfully retrieved ICC paths for %s\n", name);

	return 0;
}

static int devfreq_request_prepare(struct lwis_device *lwis_dev,
				   struct lwis_qos_setting *qos_setting,
				   enum lwis_device_sswrap_key sswrap_key)
{
	int64_t new_freq = qos_setting->frequency_hz;
	struct lwis_aggregated_dev_freq_entry *dev_freq_ptr;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;

	mutex_lock(&platform_top_dev->dev_freq_lock);
	hash_for_each_possible(platform_top_dev->lwis_dev_freq_hash_table, dev_freq_ptr, node,
			       sswrap_key) {
		if (dev_freq_ptr->sswrap_key == sswrap_key &&
		    dev_freq_ptr->aggregated_dev_freq_req.df != NULL) {
			dev_freq_ptr->aggregated_dev_min_freq =
				max(dev_freq_ptr->aggregated_dev_min_freq, new_freq);
			mutex_unlock(&platform_top_dev->dev_freq_lock);
			return 0;
		}
	}
	mutex_unlock(&platform_top_dev->dev_freq_lock);

	return -ENODEV;
}

static int platform_update_clock(struct lwis_device *lwis_dev, struct lwis_device *target_dev,
				 struct lwis_qos_setting *qos_setting)
{
	int ret;
	int qfn_idx;
	enum lwis_device_sswrap_key sswrap_key = SSWRP_UNKNOWN;

	if (lwis_erie_debug) {
		dev_info(lwis_dev->dev, "Clk update with %s on %s\n", qos_setting->qos_family_name,
			 target_dev->name);
	}

	qfn_idx = fetch_name_idx(qos_setting->qos_family_name,
				 STRUCT_FREQ_UPDATE_QOS_FAMILY_NAME_MAP);
	sswrap_key = freq_update_qos_family_name_map[qfn_idx].sswrap_key;

	if (sswrap_key == SSWRP_UNKNOWN) {
		dev_err(lwis_dev->dev, "Could not find clock %s to update\n",
			qos_setting->qos_family_name);
		return -ENODEV;
	}

	/* erie rely on devfreq for clock control */
	ret = devfreq_request_prepare(target_dev, qos_setting, sswrap_key);
	if (ret) {
		dev_err(lwis_dev->dev, "Error in devfreq %s is NULL on target device %s\n",
			qos_setting->qos_family_name, target_dev->name);
		return -ENODEV;
	}

	return ret;
}

static struct devfreq *get_devfreq_dev_via_devname(const char *dev_name)
{
	char *dup_dev_name;
	char *domain_name;
	char *sepstr;
	char delim[] = "-";
	struct devfreq *lwis_devfrq;

	dup_dev_name = kstrdup(dev_name, GFP_KERNEL);
	if (!dup_dev_name)
		return ERR_PTR(-ENOMEM);

	sepstr = dup_dev_name;
	domain_name = strsep(&sepstr, delim);
	lwis_devfrq = gs_perf_domain_find_devfreq(domain_name, GS_RECOMMENDED_DEVFREQ);

	kfree(dup_dev_name);
	return lwis_devfrq;
}

static int add_devfreq_request(struct lwis_device *lwis_dev, struct lwis_devfreq *lwis_df_req)
{
	/* Get the devfreq device from devicetree */
	lwis_df_req->df = get_devfreq_dev_via_devname(lwis_dev->name);
	if (IS_ERR(lwis_df_req->df)) {
		dev_err(lwis_dev->dev, "Failed to get the devfreq(lwis_df) source\n");
		return -ENODEV;
	}

	return google_pm_qos_add_devfreq_request(lwis_df_req->df, &lwis_df_req->df_req,
						 DEV_PM_QOS_MIN_FREQUENCY,
						 PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
}

static int top_device_unprobe(struct lwis_device *lwis_dev)
{
	int i;
	struct lwis_top_device *lwis_top_dev;
	struct lwis_platform_top_device *platform_top_dev;
	struct hlist_node *tmp_node;
	struct lwis_aggregated_dev_freq_entry *dev_freq_ptr;
	struct lwis_dev_icc_paths_entry *dev_icc_paths_ptr;
	struct lwis_aggregated_icc_paths_entry *aggregated_icc_paths_ptr;

	lwis_top_dev = container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	if (!lwis_top_dev)
		return -ENODEV;

	platform_top_dev = lwis_top_dev->platform_top_dev;
	if (!platform_top_dev) {
		pr_err("platform_top_dev is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&platform_top_dev->dev_freq_lock);
	/* Cleanup all lwis_aggregated_dev_freq_entry nodes */
	hash_for_each_safe(platform_top_dev->lwis_dev_freq_hash_table, i, tmp_node, dev_freq_ptr,
			   node) {
		if (dev_freq_ptr == NULL || dev_freq_ptr->aggregated_dev_freq_req.df == NULL)
			continue;

		/* devfreq lwis device (ispbe/ispfe/gcv) unregister user_min_freq_req. */
		google_pm_qos_remove_devfreq_request(dev_freq_ptr->aggregated_dev_freq_req.df,
						     &dev_freq_ptr->aggregated_dev_freq_req.df_req);
		hash_del(&dev_freq_ptr->node);
		kfree(dev_freq_ptr);
	}
	mutex_unlock(&platform_top_dev->dev_freq_lock);

	mutex_lock(&platform_top_dev->icc_path_lock);
	/* Cleanup all lwis_dev_icc_paths_entry nodes */
	hash_for_each_safe(platform_top_dev->lwis_dev_icc_paths_hash_table, i, tmp_node,
			   dev_icc_paths_ptr, node) {
		hash_del(&dev_icc_paths_ptr->node);
		kfree(dev_icc_paths_ptr);
	}

	/* Cleanup all lwis_aggregated_icc_paths_entry nodes */
	hash_for_each_safe(platform_top_dev->lwis_aggregated_icc_paths_hash_table, i, tmp_node,
			   aggregated_icc_paths_ptr, node) {
		hash_del(&aggregated_icc_paths_ptr->node);
		kfree(aggregated_icc_paths_ptr);
	}
	mutex_unlock(&platform_top_dev->icc_path_lock);

	return 0;
}

static int top_device_delete_node(struct lwis_device *lwis_dev)
{
	int idx;
	struct lwis_top_device *lwis_top_dev;
	struct lwis_platform_top_device *platform_top_dev;
	struct hlist_node *tmp_node;
	struct lwis_aggregated_dev_freq_entry *dev_freq_ptr;
	struct lwis_dev_icc_paths_entry *dev_icc_paths_ptr;
	struct lwis_aggregated_icc_paths_entry *aggregated_icc_paths_ptr;
	struct lwis_ioreg_device *ioreg_dev;
	enum lwis_device_sswrap_key sswrap_key;
	enum lwis_device_name_key dev_name_key;

	if (!lwis_dev->top_dev)
		return -ENODEV;

	lwis_top_dev = container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	if (!lwis_top_dev)
		return -ENODEV;

	platform_top_dev = lwis_top_dev->platform_top_dev;
	if (!platform_top_dev) {
		dev_err(lwis_dev->dev, "platform_top_dev is NULL\n");
		return -ENODEV;
	}

	ioreg_dev = container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	sswrap_key = sswrap_key_int32_to_enum(ioreg_dev->sswrap_key);
	idx = fetch_name_idx(lwis_dev->name, STRUCT_DEV_NAME_MAP);
	dev_name_key = dev_name_map[idx].dev_name_key;
	if (sswrap_key == SSWRP_UNKNOWN)
		return 0;

	mutex_lock(&platform_top_dev->dev_freq_lock);
	/* Cleanup the lwis_aggregated_dev_freq_entry node */
	hash_for_each_possible_safe(platform_top_dev->lwis_dev_freq_hash_table, dev_freq_ptr,
				    tmp_node, node, sswrap_key) {
		if ((dev_freq_ptr->sswrap_key == sswrap_key) && (dev_freq_ptr->dev_cnt >= 1) &&
		    dev_freq_ptr->aggregated_dev_freq_req.df) {
			if (dev_freq_ptr->dev_cnt > 1)
				dev_freq_ptr->dev_cnt--;
			else if (dev_freq_ptr->dev_cnt == 1) {
				google_pm_qos_remove_devfreq_request(
					dev_freq_ptr->aggregated_dev_freq_req.df,
					&dev_freq_ptr->aggregated_dev_freq_req.df_req);
				hash_del(&dev_freq_ptr->node);
				kfree(dev_freq_ptr);
			}
			break;
		}
	}
	mutex_unlock(&platform_top_dev->dev_freq_lock);

	mutex_lock(&platform_top_dev->icc_path_lock);
	/* Cleanup the lwis_dev_icc_paths_entry node */
	hash_for_each_possible_safe(platform_top_dev->lwis_dev_icc_paths_hash_table,
				    dev_icc_paths_ptr, tmp_node, node, dev_name_key) {
		if (dev_icc_paths_ptr->name_key == dev_name_key) {
			hash_del(&dev_icc_paths_ptr->node);
			kfree(dev_icc_paths_ptr);
			break;
		}
	}

	/* Cleanup the lwis_aggregated_icc_paths_entry node */
	hash_for_each_possible_safe(platform_top_dev->lwis_aggregated_icc_paths_hash_table,
				    aggregated_icc_paths_ptr, tmp_node, node,
				    ioreg_dev->sswrap_key) {
		if (aggregated_icc_paths_ptr->sswrap_key == ioreg_dev->sswrap_key &&
		    aggregated_icc_paths_ptr->dev_cnt >= 1) {
			if (aggregated_icc_paths_ptr->dev_cnt > 1)
				aggregated_icc_paths_ptr->dev_cnt--;
			else {
				hash_del(&aggregated_icc_paths_ptr->node);
				kfree(aggregated_icc_paths_ptr);
			}
			break;
		}
	}
	mutex_unlock(&platform_top_dev->icc_path_lock);

	if (hash_empty(platform_top_dev->lwis_aggregated_icc_paths_hash_table))
		for (int idx = 0; idx < NUM_HLS; ++idx)
			platform_top_dev->lwis_max_opp_h[idx] = MBFS_INVALID_HANDLE;

	return 0;
}

int lwis_platform_unprobe(struct lwis_device *lwis_dev)
{
	int ret = 0;
	struct lwis_platform *platform;

	if (!lwis_dev)
		return -ENODEV;

	platform = lwis_dev->platform;
	if (!platform)
		return -ENODEV;

	if (lwis_dev->type == DEVICE_TYPE_TOP) {
		/* Call platform-specific top dev unprobe function */
		ret = top_device_unprobe(lwis_dev);
	} else if (lwis_dev->type == DEVICE_TYPE_IOREG)
		ret = top_device_delete_node(lwis_dev);

	/* Detach all power domains */
	detach_power_domain(lwis_dev->k_dev, platform, platform->num_pds);

	return ret;
}

/* The following probe only needed by top device. */
static int top_device_probe(struct lwis_device *lwis_dev)
{
	struct lwis_platform_top_device *platform_top_dev = NULL;
	struct device *dev = &lwis_dev->plat_dev->dev;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev, struct lwis_top_device, base_dev);

	/* Allocate platform top device data construct */
	platform_top_dev = devm_kzalloc(dev, sizeof(struct lwis_platform_top_device), GFP_KERNEL);
	if (!platform_top_dev)
		return -ENOMEM;

	/* Initialize mutexes */
	mutex_init(&platform_top_dev->dev_freq_lock);
	mutex_init(&platform_top_dev->icc_path_lock);

	/* Empty hash table for lwis platform dev_freqs */
	hash_init(platform_top_dev->lwis_dev_freq_hash_table);

	/* Empty hash table for lwis platform device icc paths */
	hash_init(platform_top_dev->lwis_dev_icc_paths_hash_table);

	/* Empty hash table for lwis platform aggregated device icc paths */
	hash_init(platform_top_dev->lwis_aggregated_icc_paths_hash_table);

	lwis_top_dev->platform_top_dev = platform_top_dev;

	/* Init mbfs client ispbe, ispfe, gcv, fabhbw, fabmed, memss and gmc's handlers */
	for (int idx = 0; idx < NUM_HLS; ++idx)
		platform_top_dev->lwis_max_opp_h[idx] = MBFS_INVALID_HANDLE;

	return 0;
}

int lwis_platform_probe(struct lwis_device *lwis_dev)
{
	int ret = 0;
	struct lwis_platform *platform;

	platform = devm_kzalloc(lwis_dev->k_dev, sizeof(struct lwis_platform), GFP_KERNEL);
	if (IS_ERR_OR_NULL(platform))
		return -ENOMEM;
	lwis_dev->platform = platform;

	/* Enable runtime power management for the platform device */
	pm_runtime_enable(lwis_dev->k_dev);

	ret = attach_power_domain(lwis_dev);
	if (ret < 0) {
		dev_err(lwis_dev->dev, "Failed to attach power domains\n");
		return ret;
	}

	/* Only IOREG devices will access DMA resources */
	if (lwis_dev->type == DEVICE_TYPE_IOREG) {
		ret = dma_set_mask_and_coherent(lwis_dev->dev, DMA_BIT_MASK(36));
		if (ret)
			dev_warn(lwis_dev->dev, "Failed (%d) to setup dma mask\n", ret);
	}

	if (lwis_dev->type == DEVICE_TYPE_TOP) {
		/* Call platform-specific top dev probe function */
		ret = top_device_probe(lwis_dev);
		if (ret < 0) {
			dev_err(lwis_dev->dev, "Error in platform-specific top dev probe: %d\n",
				ret);
			return ret;
		}
	} else if (lwis_dev->type == DEVICE_TYPE_IOREG) {
		struct lwis_ioreg_device *ioreg_dev;

		ioreg_dev = container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
		/* Parse the sswrap_key based on lwis_dev->name */
		ioreg_dev->sswrap_key =
			(int32_t)sswrap_map[fetch_name_idx(lwis_dev->name, STRUCT_SSWRP_MAP)]
				.sswrap_key;
	}

	return ret;
}

/*
 * Clocks have a dependency on the power domain that they live in, which means that clock rates
 * will reset to their default values between power cycles. However, upstream kernel does not
 * recalculate the clock rates between power cycles. This means by the time a power domain powers
 * off and then powers back on, kernel has a wrong understanding about the rates for clocks that
 * depend on that power domain. To fix this issue, we need to invalidate the clock cache by calling
 * clk_get_rate, which recalculates the rate.
 */
static void invalidate_clock_cache(struct lwis_device *lwis_dev)
{
	int i;
	unsigned long rate;

	if (!lwis_dev->clocks)
		return;

	for (i = 0; i < lwis_dev->clocks->count; ++i) {
		rate = clk_get_rate(lwis_dev->clocks->clk[i].clk);
		if (rate == 0)
			dev_dbg(lwis_dev->k_dev, "Failed to invalidate clk[%d]", i);
	}
}

int lwis_platform_device_enable(struct lwis_device *lwis_dev)
{
	int ret;
	int iommus_len = 0;

	if (!lwis_dev)
		return -ENODEV;

	if (!lwis_dev->platform)
		return -ENODEV;

	ret = pm_runtime_get_sync(lwis_dev->k_dev);
	if (ret < 0) {
		dev_err(lwis_dev->dev, "Failed to increment the usage counter for %s",
			lwis_dev->name);
		return ret;
	}

	if (lwis_dev->type != DEVICE_TYPE_DPM)
		invalidate_clock_cache(lwis_dev);

	if (of_find_property(lwis_dev->k_dev->of_node, "iommus", &iommus_len) && iommus_len) {
		iommu_set_fault_handler(iommu_get_domain_for_dev(lwis_dev->k_dev),
					iommu_fault_handler, lwis_dev);
	}

	return 0;
}

int lwis_platform_device_disable(struct lwis_device *lwis_dev)
{
	int ret = 0;
	int iommus_len = 0;

	if (!lwis_dev)
		return -ENODEV;

	if (!lwis_dev->platform)
		return -ENODEV;
	/*
	 * We can't remove fault handlers, so there's no call corresponding
	 * to the iommu_register_device_fault_handler above
	 */

	lwis_platform_remove_qos(lwis_dev);

	if (of_find_property(lwis_dev->k_dev->of_node, "iommus", &iommus_len) && iommus_len) {
		/* Deactivate IOMMU */
		// iommu_unregister_device_fault_handler(lwis_dev->k_dev);
	}

	ret = pm_runtime_put_sync(lwis_dev->k_dev);
	if (ret < 0) {
		dev_err(lwis_dev->dev, "Failed to decrement the usage counter for %s",
			lwis_dev->name);
		return ret;
	}

	return ret;
}

int lwis_platform_update_qos(struct lwis_device *lwis_dev, int value, int32_t clock_family)
{
	return 0;
}

static int set_icc_update(struct lwis_device *lwis_dev,
			  struct google_icc_path *icc_path_for_lwis_device,
			  struct lwis_qos_setting *qos_setting)
{
	int ret = 0;

	if ((!icc_path_for_lwis_device) || (!qos_setting) || (!lwis_dev))
		return -ENODEV;

	if (qos_setting->qos_voting_entity != VOTE_GMC) {
		dev_err(lwis_dev->dev, "google_icc_set_read_bw failed for invalid voting entity\n");
		return -EINVAL;
	}

	ret = google_icc_set_read_bw_gmc(icc_path_for_lwis_device, qos_setting->read_avg_bw,
					 qos_setting->read_peak_bw,
					 /* rt_bw */ qos_setting->read_rt_bw, qos_setting->read_vc);

	if (ret) {
		dev_err(lwis_dev->dev, "google_icc_set_read_bw failed for: %s\n",
			qos_setting->qos_family_name);
		return ret;
	}

	/* No vote while read_latency = 0 */
	if (qos_setting->read_latency != 0)
		ret = google_icc_set_read_latency_gmc(icc_path_for_lwis_device,
						      qos_setting->read_latency,
						      qos_setting->read_ltv, qos_setting->read_vc);

	if (ret) {
		dev_err(lwis_dev->dev, "google_icc_set_read_latency failed for: %s\n",
			qos_setting->qos_family_name);
		return ret;
	}

	ret = google_icc_set_write_bw_gmc(icc_path_for_lwis_device, qos_setting->write_avg_bw,
					  qos_setting->write_peak_bw,
					  /* rt_bw */ qos_setting->write_rt_bw,
					  qos_setting->write_vc);
	if (ret) {
		dev_err(lwis_dev->dev, "google_icc_set_write_bw failed for: %s\n",
			qos_setting->qos_family_name);
		return ret;
	}

	ret = google_icc_update_constraint(icc_path_for_lwis_device);
	if (ret) {
		dev_err(lwis_dev->dev,
			"google_icc_update_constraint sync failed on aggregated node\n");
		return ret;
	}

	return ret;
}

static int platform_update_bandwidth(struct lwis_device *lwis_dev, struct lwis_device *target_dev,
				     struct lwis_qos_setting *qos_setting)
{
	int ret = 0, qfn_idx;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;
	enum lwis_device_name_key dev_name_key;
	struct lwis_dev_icc_paths_entry *dev_icc_paths_ptr;
	char trace_name[LWIS_MAX_NAME_STRING_LEN];

	if (lwis_erie_debug) {
		dev_info(lwis_dev->dev, "Bandwidth update with %s on %s\n",
			 qos_setting->qos_family_name, target_dev->name);
	}

	qfn_idx =
		fetch_name_idx(qos_setting->qos_family_name, STRUCT_BW_UPDATE_QOS_FAMILY_NAME_MAP);
	dev_name_key = bw_update_qos_family_name_map[qfn_idx].dev_name_key;

	if (dev_name_key == DEV_UNKNOWN) {
		dev_err(lwis_dev->dev, "Cannot find qos_family_name %s for bandwidth update\n",
			qos_setting->qos_family_name);
		return -ENODEV;
	}

	mutex_lock(&platform_top_dev->icc_path_lock);
	hash_for_each_possible(platform_top_dev->lwis_dev_icc_paths_hash_table, dev_icc_paths_ptr,
			       node, dev_name_key) {
		if (dev_icc_paths_ptr->name_key == dev_name_key) {
			scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "set_icc_update_%s",
				  target_dev->name);
			LWIS_ATRACE_FUNC_BEGIN(lwis_dev, trace_name);
			ret = set_icc_update(lwis_dev, dev_icc_paths_ptr->dev_icc_path,
					     qos_setting);
			LWIS_ATRACE_FUNC_END(lwis_dev, trace_name);
			break;
		}
	}
	mutex_unlock(&platform_top_dev->icc_path_lock);

	if (ret) {
		pr_err("BW update failed %s\n", qos_setting->qos_family_name);
		return -ENODEV;
	}

	return 0;
}

int lwis_platform_dpm_update_qos(struct lwis_device *lwis_dev, struct lwis_device *target_dev,
				 struct lwis_qos_setting *qos_setting)
{
	int ret = 0;

	/* Bypass SLC qos bw vote on Erie platform */
	if (qos_setting->frequency_hz < 0 && qos_setting->qos_voting_entity == VOTE_GSLC) {
		dev_warn(lwis_dev->dev, "No GSLC vote on target platform and bypass to next\n");
		return ret;
	}

	if ((!lwis_dev) || (!qos_setting) || (!target_dev))
		return -ENODEV;

	if (strlen(qos_setting->qos_family_name) <= 0) {
		dev_err(lwis_dev->dev, "Invalid Platform QOS Update\n");
		return -EINVAL;
	}

	if (lwis_erie_debug) {
		dev_info(
			lwis_dev->dev,
			"Platform DPM update CLK/QOS with qos_family_name(%s) on target device(%s)\n",
			qos_setting->qos_family_name, target_dev->name);
	}

	if (qos_setting->frequency_hz >= 0)
		ret = platform_update_clock(lwis_dev, target_dev, qos_setting);
	else
		ret = platform_update_bandwidth(lwis_dev, target_dev, qos_setting);

	return ret;
}

static void
platform_cal_expected_qos_settings(struct lwis_qos_setting *qos_setting,
				   u32 (*expected_qos_settings)[LWIS_STORE_IRM_REG_NUM][NUM_VC])
{
	int rd_vc, wr_vc;

	rd_vc = qos_setting->read_vc;
	wr_vc = qos_setting->write_vc;
	(*expected_qos_settings)[LWIS_STORE_IRM_REG_RD_BW_AVG][rd_vc] =
		(u32)CLAMP_VALUE((u64)qos_setting->read_avg_bw, LWIS_IRM_REG_BW_MASK);
	(*expected_qos_settings)[LWIS_STORE_IRM_REG_RD_PEAK_AVG][rd_vc] =
		(u32)CLAMP_VALUE((u64)qos_setting->read_peak_bw, LWIS_IRM_REG_BW_MASK);
	(*expected_qos_settings)[LWIS_STORE_IRM_REG_RD_BW_RT][rd_vc] =
		(u32)CLAMP_VALUE((u64)qos_setting->read_rt_bw, LWIS_IRM_REG_BW_MASK);
	/*
	 * b/336533522: lwis and userspace need treat no vote while read_latency = 0 based on
	 * current RDO - IRM/QoS Box Kernel Software Interfaces Design
	 */
	if (qos_setting->read_latency != 0) {
		(*expected_qos_settings)[LWIS_STORE_IRM_REG_RD_LATENCY][rd_vc] =
			(u32)CLAMP_VALUE((u64)qos_setting->read_latency, LWIS_IRM_REG_LATENCY_MASK);
	}
	(*expected_qos_settings)[LWIS_STORE_IRM_REG_WR_BW_AVG][wr_vc] =
		(u32)CLAMP_VALUE((u64)qos_setting->write_avg_bw, LWIS_IRM_REG_BW_MASK);
	(*expected_qos_settings)[LWIS_STORE_IRM_REG_WR_PEAK_AVG][wr_vc] =
		(u32)CLAMP_VALUE((u64)qos_setting->write_peak_bw, LWIS_IRM_REG_BW_MASK);
	(*expected_qos_settings)[LWIS_STORE_IRM_REG_WR_BW_RT][rd_vc] =
		(u32)CLAMP_VALUE((u64)qos_setting->write_rt_bw, LWIS_IRM_REG_BW_MASK);
}

/*
 * lwis_platform_refresh_expected_qos_settings: Generates the expected qos settings from user
 * space to irm register. Needed by irm register verification.
 */
void lwis_platform_refresh_expected_qos_settings(struct lwis_device *lwis_dev,
						 struct lwis_qos_setting *qos_setting)
{
	/*
	 * We need calculate the `dev_icc_paths_ptr->expected_dev_qos_settings`
	 * even for lwis_erie_debug is not enabled, else it may cause the irm register
	 * verification failure if module parameter lwis_erie_debug disabled and enabled.
	 */
	int qfn_idx;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;
	enum lwis_device_name_key dev_name_key;
	struct lwis_dev_icc_paths_entry *dev_icc_paths_ptr;

	/* Bypass SLC qos bw vote on Erie platform */
	if (qos_setting->qos_voting_entity == VOTE_GSLC)
		return;

	qfn_idx =
		fetch_name_idx(qos_setting->qos_family_name, STRUCT_BW_UPDATE_QOS_FAMILY_NAME_MAP);
	dev_name_key = bw_update_qos_family_name_map[qfn_idx].dev_name_key;
	if (dev_name_key == DEV_UNKNOWN) {
		dev_err(lwis_dev->dev, "Cannot find qos_family_name %s for bandwidth update\n",
			qos_setting->qos_family_name);
		return;
	}

	if (qos_setting->qos_voting_entity == VOTE_GMC) {
		mutex_lock(&platform_top_dev->icc_path_lock);
		hash_for_each_possible(platform_top_dev->lwis_dev_icc_paths_hash_table,
				       dev_icc_paths_ptr, node, dev_name_key) {
			if (dev_icc_paths_ptr->name_key == dev_name_key) {
				platform_cal_expected_qos_settings(
					qos_setting, &dev_icc_paths_ptr->expected_dev_qos_settings);
				mutex_unlock(&platform_top_dev->icc_path_lock);
				return;
			}
		}
		mutex_unlock(&platform_top_dev->icc_path_lock);
	} else {
		dev_err(lwis_dev->dev, "%s qos voting entity %d is invalid\n", lwis_dev->name,
			qos_setting->qos_voting_entity);
	}
}

/*
 * lwis_get_sync_update_device_mask:
 * Generates a mask to commit the aggregate constraint update
 * for all the devices that exist in the QOS update array.
 */
void lwis_get_sync_update_device_mask(struct lwis_device *lwis_dev,
				      struct lwis_qos_setting *qos_setting, int *sync_update)
{
	int qfn_idx;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;
	enum lwis_device_name_key dev_name_key;
	int qos_update_sync_mask = 0;
	struct lwis_dev_icc_paths_entry *dev_icc_paths_ptr;

	/* Bypass SLC qos bw vote on Erie platform */
	if (qos_setting->qos_voting_entity == VOTE_GSLC)
		return;

	qfn_idx =
		fetch_name_idx(qos_setting->qos_family_name, STRUCT_BW_UPDATE_QOS_FAMILY_NAME_MAP);
	dev_name_key = bw_update_qos_family_name_map[qfn_idx].dev_name_key;
	qos_update_sync_mask |= bw_update_qos_family_name_map[qfn_idx].qos_update_sync_mask;

	/*
	 * 0x1FFE is the range of BIT(1)~BIT(12) for GMC vote
	 */
	if (qos_setting->qos_voting_entity == VOTE_GMC) {
		mutex_lock(&platform_top_dev->icc_path_lock);
		hash_for_each_possible(platform_top_dev->lwis_dev_icc_paths_hash_table,
				       dev_icc_paths_ptr, node, dev_name_key) {
			if (dev_icc_paths_ptr->name_key == dev_name_key) {
				*sync_update |= qos_update_sync_mask & GENMASK(12, 1);
				mutex_unlock(&platform_top_dev->icc_path_lock);
				return;
			}
		}
		mutex_unlock(&platform_top_dev->icc_path_lock);
	} else {
		dev_err(lwis_dev->dev, "%s qos voting entity %d is invalid\n", lwis_dev->name,
			qos_setting->qos_voting_entity);
	}
}

/*
 * lwis_platform_dpm_sync_update_qos:
 * sync the constraints to the device from all its subdevice IPs.
 * This call is executed just once after the
 * entire bandwidth QOS setting array has been processed and the constraints are
 * set to their respective IRM SSWRPS registers. This ensures there is
 * just one system call per device(ISPBE,ISPFE and GCV).
 */
int lwis_platform_dpm_sync_update_qos(struct lwis_device *lwis_dev, int sync_update)
{
	int ret = 0;
	int is_error = 0;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;
	enum lwis_device_sswrap_key sswrap_key = SSWRP_UNKNOWN;
	struct lwis_aggregated_icc_paths_entry *aggregated_icc_paths_ptr;
	uint32_t not_accessed = BIT(sizeof(enum lwis_device_sswrap_key) - 1) - 1;
	char trace_name[LWIS_MAX_NAME_STRING_LEN];

	if (lwis_erie_debug)
		dev_info(lwis_dev->dev, "Constraint sync_update is 0x%x\n", sync_update);

	for (int i = 0; i < ARRAY_SIZE(bw_update_qos_family_name_map); ++i) {
		sswrap_key = bw_update_qos_family_name_map[i].sswrap_key;
		if ((bw_update_qos_family_name_map[i].qos_update_sync_mask & sync_update) &&
		    (not_accessed & BIT(sswrap_key - 1))) {
			not_accessed ^= BIT(sswrap_key - 1);
			mutex_lock(&platform_top_dev->icc_path_lock);
			hash_for_each_possible(
				platform_top_dev->lwis_aggregated_icc_paths_hash_table,
				aggregated_icc_paths_ptr, node, sswrap_key) {
				if (aggregated_icc_paths_ptr->sswrap_key == sswrap_key) {
					/*
					 * Update GMC vote to aggregated device
					 * IRM registers and trigger to CPM MIPM
					 */
					scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN,
						  "google_icc_update_constraint_%s",
						  bw_update_qos_family_name_map[i].qos_family_name);
					LWIS_ATRACE_FUNC_BEGIN(lwis_dev, trace_name);
					ret = google_icc_update_constraint(
						aggregated_icc_paths_ptr->aggregated_icc_path);
					LWIS_ATRACE_FUNC_END(lwis_dev, trace_name);
					break;
				}
			}
			mutex_unlock(&platform_top_dev->icc_path_lock);
		}

		if (ret) {
			is_error = ret;
			dev_err(lwis_dev->dev,
				"google_icc_update_constraint sync failed on %s aggregate update\n",
				bw_update_qos_family_name_map[i].qos_family_name);
		}
	}

	return is_error;
}

void lwis_get_devfreq_sync_update_device_mask(struct lwis_device *lwis_dev,
					      struct lwis_qos_setting *qos_setting,
					      int *devfreq_sync_update)
{
	int qfn_idx;
	int qos_update_sync_mask = 0;
	struct lwis_aggregated_dev_freq_entry *dev_freq_ptr;
	enum lwis_device_sswrap_key sswrap_key = SSWRP_UNKNOWN;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;

	qfn_idx = fetch_name_idx(qos_setting->qos_family_name,
				 STRUCT_FREQ_UPDATE_QOS_FAMILY_NAME_MAP);
	sswrap_key = freq_update_qos_family_name_map[qfn_idx].sswrap_key;
	qos_update_sync_mask |= freq_update_qos_family_name_map[qfn_idx].qos_update_sync_mask;

	if (sswrap_key == SSWRP_UNKNOWN)
		goto exit;

	mutex_lock(&platform_top_dev->dev_freq_lock);
	hash_for_each_possible(platform_top_dev->lwis_dev_freq_hash_table, dev_freq_ptr, node,
			       sswrap_key) {
		if (dev_freq_ptr->sswrap_key == sswrap_key) {
			*devfreq_sync_update |= qos_update_sync_mask;
			mutex_unlock(&platform_top_dev->dev_freq_lock);
			return;
		}
	}
	mutex_unlock(&platform_top_dev->dev_freq_lock);

exit:
	dev_err(lwis_dev->dev, "Cannot find qos_family_name %s for frequency mask update\n",
		qos_setting->qos_family_name);
}

static int devfreq_aggregated_update(struct lwis_device *lwis_dev, struct lwis_devfreq *lwis_df_req,
				     int64_t aggregated_dev_min_freq)
{
	int ret;
	int is_error = 0;

	/* To update ispbe_df_req/ispfe_df_req/gcv_df_req request */
	ret = dev_pm_qos_update_request(&lwis_df_req->df_req,
					DIV_ROUND_UP(aggregated_dev_min_freq, HZ_PER_KHZ));
	if (ret != 0 && ret != 1) {
		is_error = ret;
		dev_err(lwis_dev->dev,
			"dev_pm_qos_update_request failed on devfreq min_freq aggregate update\n");
	}

	if (lwis_erie_debug) {
		if (ret == 0) {
			dev_info(lwis_dev->dev, "Dev PM qos value no change\n");
		} else if (ret == 1) {
			dev_info(lwis_dev->dev, "Dev PM qos value successfully changed\n");
		} else if (ret == -EINVAL) {
			dev_err(lwis_dev->dev,
				"dev_pm_qos_update_request failed on ISPBE devfreq min_freq aggregate update with wrong parameters\n");
		} else if (ret == -ENODEV) {
			dev_err(lwis_dev->dev,
				"dev_pm_qos_update_request failed on device has been removed from the system\n");
		}
	}

	return is_error;
}

/*
 * lwis_platform_dpm_devfreq_sync_update_qos:
 * sync the constraints to the device from all its subdevice IPs.
 * This call is executed just once after the
 * entire freq QOS setting array has been processed and the constraints are
 * set to their respective devfreq dev. This ensures there is
 * just one system call per device(ISPBE,ISPFE and GCV).
 */
int lwis_platform_dpm_devfreq_sync_update_qos(struct lwis_device *lwis_dev, int devfreq_sync_update)
{
	int ret = 0, is_error = 0;
	struct lwis_aggregated_dev_freq_entry *dev_freq_ptr;
	enum lwis_device_sswrap_key sswrap_key = SSWRP_UNKNOWN;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;

	if (lwis_erie_debug)
		dev_info(lwis_dev->dev, "Constraint dev_pm_qos_update_request is 0x%x\n",
			 devfreq_sync_update);

	for (int i = 0; i < ARRAY_SIZE(freq_update_qos_family_name_map); ++i) {
		sswrap_key = freq_update_qos_family_name_map[i].sswrap_key;
		if (freq_update_qos_family_name_map[i].qos_update_sync_mask & devfreq_sync_update) {
			mutex_lock(&platform_top_dev->dev_freq_lock);
			hash_for_each_possible(platform_top_dev->lwis_dev_freq_hash_table,
					       dev_freq_ptr, node, sswrap_key) {
				if (dev_freq_ptr->sswrap_key == sswrap_key) {
					/* To update ISPBE/ISPFE/GCV min_freq request */
					ret = devfreq_aggregated_update(
						lwis_dev, &dev_freq_ptr->aggregated_dev_freq_req,
						dev_freq_ptr->aggregated_dev_min_freq);
					break;
				}
			}
			mutex_unlock(&platform_top_dev->dev_freq_lock);
		}

		if (ret) {
			is_error = ret;
			dev_err(lwis_dev->dev, "%s failed on %s min_freq request\n", __func__,
				freq_update_qos_family_name_map[i].qos_family_name);
		}
	}

	return is_error;
}

/*
 * lwis_platform_query_mbfs_opp_level_verify:
 * Confirm the opp level with domain name fabhw, fabmed, mess and gmc via MBFS API.
 * This validates the bandwidth that was voted via MIPM.
 */
static int lwis_platform_query_mbfs_opp_level_verify(struct lwis_device *lwis_dev,
						     uint64_t expected_opp_level)
{
	int ret, error = 0;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;

	/*
	 * Memory specific SSWRPs for verification are defined in the
	 * beginning of mbfs_client_handler_idx.
	 */
	for (int idx = 0; idx < LWIS_MEM_OP_LEVEL_HDLS; ++idx) {
		/* Probe the mbfs client fabhbw, fabmed, memss and gmc's handlers at first access */
		ret = mbfs_client_handler_reuse_or_probe_via_domain_name(
			lwis_dev, &(platform_top_dev->lwis_max_opp_h[idx]),
			/* domain_name */ mbfs_client_dname_map[idx], BANDWIDTH);
		if (ret)
			return -EIO;

		ret = mbfs_verify_opp_level_via_domain_name(
			lwis_dev, /* domain_name */ mbfs_client_dname_map[idx],
			platform_top_dev->lwis_max_opp_h[idx], expected_opp_level);

		if (ret)
			error = ret;
	}

	return error;
}

static int platform_irm_register_verify(struct lwis_device *lwis_dev,
					u32 *expected_constrainted_qos_settings,
					uint32_t client_idx, bool *max_bw_opp_level_verified)
{
	int ret = 0;
	struct device *dev = lwis_dev->dev;
	u32 rd_bw_avg_val, rd_bw_peak_val, rd_latency_val, rd_bw_rt_val, wr_bw_avg_val,
		wr_bw_peak_val, wr_bw_rt_val;

	/* Verify google_icc_set_read_bw & google_icc_set_read_latency */
	rd_bw_avg_val = irm_register_read(dev, client_idx, DVFS_REQ_RD_BW_AVG_GMC);
	rd_bw_peak_val = irm_register_read(dev, client_idx, DVFS_REQ_RD_BW_PEAK_GMC);
	rd_latency_val = irm_register_read(dev, client_idx, DVFS_REQ_LATENCY_GMC);
	rd_bw_rt_val = irm_register_read(dev, client_idx, DVFS_REQ_RD_BW_RT_GMC);
	/* Verify google_icc_set_write_bw */
	wr_bw_avg_val = irm_register_read(dev, client_idx, DVFS_REQ_WR_BW_AVG_GMC);
	wr_bw_peak_val = irm_register_read(dev, client_idx, DVFS_REQ_WR_BW_PEAK_GMC);
	wr_bw_rt_val = irm_register_read(dev, client_idx, DVFS_REQ_WR_BW_RT_GMC);

	if (IRM_REG_VAL_CMP(expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_RD_BW_AVG],
			    rd_bw_avg_val)) {
		dev_err(lwis_dev->dev, "IRM read got rd_bw_avg_val(%u), expected %u", rd_bw_avg_val,
			expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_RD_BW_AVG]);
		ret = -EIO;
	}
	if (IRM_REG_VAL_CMP(expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_RD_PEAK_AVG],
			    rd_bw_peak_val)) {
		dev_err(lwis_dev->dev, "IRM read got rd_bw_peak_val(%u), expected %u",
			rd_bw_peak_val,
			expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_RD_PEAK_AVG]);
		ret = -EIO;
	}
	if (IRM_REG_VAL_CMP(expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_RD_LATENCY],
			    rd_latency_val)) {
		dev_err(lwis_dev->dev, "IRM read got rd_latency_val(%u), expected %u",
			rd_latency_val,
			expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_RD_LATENCY]);
		ret = -EIO;
	}
	if (IRM_REG_VAL_CMP(expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_RD_BW_RT],
			    rd_bw_rt_val)) {
		dev_err(lwis_dev->dev, "IRM read got rd_bw_rt_val(%u), expected %u", rd_bw_rt_val,
			expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_RD_BW_RT]);
		ret = -EIO;
	}

	if (IRM_REG_VAL_CMP(expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_WR_BW_AVG],
			    wr_bw_avg_val)) {
		dev_err(lwis_dev->dev, "IRM read got wr_bw_avg_val(%u), expected %u", wr_bw_avg_val,
			expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_WR_BW_AVG]);
		ret = -EIO;
	}
	if (IRM_REG_VAL_CMP(expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_WR_PEAK_AVG],
			    wr_bw_peak_val)) {
		dev_err(lwis_dev->dev, "IRM read got wr_bw_peak_val(%u), expected %u",
			wr_bw_peak_val,
			expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_WR_PEAK_AVG]);
		ret = -EIO;
	}
	if (IRM_REG_VAL_CMP(expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_WR_BW_RT],
			    wr_bw_rt_val)) {
		dev_err(lwis_dev->dev, "IRM read got wr_bw_rt_val(%u), expected %u", wr_bw_rt_val,
			expected_constrainted_qos_settings[LWIS_STORE_IRM_REG_WR_BW_RT]);
		ret = -EIO;
	}

	/*
	 * Validate the fab/gmc opp level is max (0)
	 * when the isp dev bw set to max via icc path
	 * Use bool max_bw_opp_level_verified to control only one time opp level verification.
	 */
	if (!(*max_bw_opp_level_verified) && (!IRM_REG_VAL_CMP(rd_bw_avg_val, MAX_ISPDEV_BW) ||
					      !IRM_REG_VAL_CMP(wr_bw_avg_val, MAX_ISPDEV_BW))) {
		lwis_platform_query_mbfs_opp_level_verify(lwis_dev, 0);
		*max_bw_opp_level_verified = true;
	}

	return ret;
}

static void
get_constraint_qos_settings(u32 (*constrainted_qos_settings)[LWIS_STORE_IRM_REG_NUM],
			    u32 (*expected_qos_settings)[LWIS_STORE_IRM_REG_NUM][NUM_VC])
{
	for (int vc = 0; vc < NUM_VC; vc++) {
		(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_RD_BW_AVG] = (u32)CLAMP_VALUE(
			(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_RD_BW_AVG] +
				(*expected_qos_settings)[LWIS_STORE_IRM_REG_RD_BW_AVG][vc],
			LWIS_IRM_REG_BW_MASK);
		(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_RD_PEAK_AVG] = (u32)CLAMP_VALUE(
			(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_RD_PEAK_AVG] +
				(*expected_qos_settings)[LWIS_STORE_IRM_REG_RD_PEAK_AVG][vc],
			LWIS_IRM_REG_BW_MASK);
		(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_RD_LATENCY] = (u32)(min(
			((*expected_qos_settings)[LWIS_STORE_IRM_REG_RD_LATENCY][vc] == 0 ?
				 (*constrainted_qos_settings)[LWIS_STORE_IRM_REG_RD_LATENCY] :
				 (*expected_qos_settings)[LWIS_STORE_IRM_REG_RD_LATENCY][vc]),
			((*constrainted_qos_settings)[LWIS_STORE_IRM_REG_RD_LATENCY] == 0 ?
				 LWIS_IRM_REG_LATENCY_MASK :
				 (*constrainted_qos_settings)[LWIS_STORE_IRM_REG_RD_LATENCY])));
		(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_WR_BW_AVG] = (u32)CLAMP_VALUE(
			(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_WR_BW_AVG] +
				(*expected_qos_settings)[LWIS_STORE_IRM_REG_WR_BW_AVG][vc],
			LWIS_IRM_REG_BW_MASK);
		(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_WR_PEAK_AVG] = (u32)CLAMP_VALUE(
			(*constrainted_qos_settings)[LWIS_STORE_IRM_REG_WR_PEAK_AVG] +
				(*expected_qos_settings)[LWIS_STORE_IRM_REG_WR_PEAK_AVG][vc],
			LWIS_IRM_REG_BW_MASK);
	}
}

/*
 * lwis_platform_query_irm_register_verify:
 * sync the constraints to the device from all its subdevice IPs.
 * This call is executed just once after the
 * entire QOS setting array has been processed and the constraints are
 * set to their respective IRM SSWRPS registers. This ensures there is
 * just one system call per device(ISPBE,ISPFE and GCV).
 */
int lwis_platform_query_irm_register_verify(struct lwis_device *lwis_dev, int sync_update)
{
	int is_error = 0;
	int ret, i;
	struct lwis_dev_icc_paths_entry *dev_icc_paths_ptr;
	struct hlist_node *tmp_node;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;
	bool max_bw_opp_level_verified = false;

	if (!lwis_erie_debug)
		return 0;

	/* GENMASK(4, 1) = 0x1E is the mask of BIT(1)~BIT(3) for ISP_SET_1 devices*/
	if (sync_update & GENMASK(4, 1)) {
		u32 constrainted_qos_settings[LWIS_STORE_IRM_REG_NUM] = { 0 };

		/* Aggregated bandwidth vote for all ispbe devices */
		mutex_lock(&platform_top_dev->icc_path_lock);
		hash_for_each_safe(platform_top_dev->lwis_dev_icc_paths_hash_table, i, tmp_node,
				   dev_icc_paths_ptr, node) {
			if (dev_icc_paths_ptr->name_key >= ISPBE_MSA_TNR &&
			    dev_icc_paths_ptr->name_key <= ISPBE_YUV) {
				get_constraint_qos_settings(
					&constrainted_qos_settings,
					&dev_icc_paths_ptr->expected_dev_qos_settings);
			}
		}
		mutex_unlock(&platform_top_dev->icc_path_lock);

		ret = platform_irm_register_verify(lwis_dev, constrainted_qos_settings,
						   IRM_IDX_ISP_SET_1, &max_bw_opp_level_verified);
		if (ret == -EIO)
			is_error = ret;
	}

	/* GENMASK(7, 5) = 0xE0 is the mask of BIT(5)~BIT(7) for GCV devices */
	if (sync_update & GENMASK(7, 5)) {
		u32 constrainted_qos_settings[LWIS_STORE_IRM_REG_NUM] = { 0 };

		/* Aggregated bandwidth vote for all fe devices */
		mutex_lock(&platform_top_dev->icc_path_lock);
		hash_for_each_safe(platform_top_dev->lwis_dev_icc_paths_hash_table, i, tmp_node,
				   dev_icc_paths_ptr, node) {
			if (dev_icc_paths_ptr->name_key >= GCV_GSE &&
			    dev_icc_paths_ptr->name_key <= GCV_CRE) {
				get_constraint_qos_settings(
					&constrainted_qos_settings,
					&dev_icc_paths_ptr->expected_dev_qos_settings);
			}
		}
		mutex_unlock(&platform_top_dev->icc_path_lock);

		ret = platform_irm_register_verify(lwis_dev, constrainted_qos_settings, IRM_IDX_GCV,
						   &max_bw_opp_level_verified);
		if (ret == -EIO)
			is_error = ret;
	}

	/* GENMASK(12, 8) = 0x1F00 is the mask of BIT(8)~BIT(12) for ISP_SET_0 devices */
	if (sync_update & GENMASK(12, 8)) {
		u32 constrainted_qos_settings[LWIS_STORE_IRM_REG_NUM] = { 0 };

		/* Aggregated bandwidth vote for all gcv devices */
		mutex_lock(&platform_top_dev->icc_path_lock);
		hash_for_each_safe(platform_top_dev->lwis_dev_icc_paths_hash_table, i, tmp_node,
				   dev_icc_paths_ptr, node) {
			if (dev_icc_paths_ptr->name_key >= ISPFE_TOP &&
			    dev_icc_paths_ptr->name_key <= ISPFE_CORE2) {
				get_constraint_qos_settings(
					&constrainted_qos_settings,
					&dev_icc_paths_ptr->expected_dev_qos_settings);
			}
		}
		mutex_unlock(&platform_top_dev->icc_path_lock);

		ret = platform_irm_register_verify(lwis_dev, constrainted_qos_settings,
						   IRM_IDX_ISP_SET_0, &max_bw_opp_level_verified);
		if (ret == -EIO)
			is_error = ret;
	}

	return is_error;
}

/* Platform use devfreq for clock vote */
uint32_t lwis_platform_dpm_read_clock(struct lwis_device *lwis_dev)
{
	uint32_t clock = 0;
	struct lwis_top_device *lwis_top_dev;
	struct lwis_platform_top_device *platform_top_dev;
	struct lwis_ioreg_device *ioreg_dev;
	struct lwis_aggregated_dev_freq_entry *dev_freq_ptr;
	struct hlist_node *tmp_node;
	enum lwis_device_sswrap_key sswrap_key;

	lwis_top_dev = container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	platform_top_dev = lwis_top_dev->platform_top_dev;
	if (!platform_top_dev) {
		dev_err(lwis_dev->dev, "platform_top_dev is NULL\n");
		goto exit;
	}

	ioreg_dev = container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	sswrap_key = sswrap_key_int32_to_enum(ioreg_dev->sswrap_key);
	if (sswrap_key == SSWRP_UNKNOWN)
		goto exit;

	mutex_lock(&platform_top_dev->dev_freq_lock);
	hash_for_each_possible_safe(platform_top_dev->lwis_dev_freq_hash_table, dev_freq_ptr,
				    tmp_node, node, sswrap_key) {
		if (dev_freq_ptr->sswrap_key == sswrap_key) {
			clock = (uint32_t)KHZ_TO_HZ(dev_pm_qos_read_value(
				dev_freq_ptr->aggregated_dev_freq_req.df->dev.parent,
				DEV_PM_QOS_MIN_FREQUENCY));
			mutex_unlock(&platform_top_dev->dev_freq_lock);
			return clock;
		}
	}
	mutex_unlock(&platform_top_dev->dev_freq_lock);

exit:
	return 0;
}

static int devfreq_aggregated_verify(struct lwis_device *lwis_dev, struct lwis_devfreq *lwis_df_req,
				     int64_t aggregated_dev_min_freq)
{
	s32 min_freq = 0;
	/* Get the current aggregated ISPBE/ISPFE/GCV min_freq value */
	min_freq = dev_pm_qos_read_value(lwis_df_req->df->dev.parent, DEV_PM_QOS_MIN_FREQUENCY);
	/*
	 * Devfreq read val may larger than qos_set_val, while there's a
	 * performance governor set its min freq baseline.
	 */
	if ((s32)min_freq < (s32)DIV_ROUND_UP(aggregated_dev_min_freq, HZ_PER_KHZ)) {
		dev_err(lwis_dev->dev, "Devfreq aggregated read val(%d), devfreq set val(%lld)",
			min_freq, DIV_ROUND_UP(aggregated_dev_min_freq, HZ_PER_KHZ));

		return -EIO;
	}

	return 0;
}

int lwis_platform_query_devfreq_verify(struct lwis_device *lwis_dev, int devfreq_sync_update)
{
	int ret = 0, is_error = 0;
	enum lwis_device_sswrap_key sswrap_key = SSWRP_UNKNOWN;
	struct lwis_aggregated_dev_freq_entry *dev_freq_ptr;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;

	for (int i = 0; i < ARRAY_SIZE(freq_update_qos_family_name_map); ++i) {
		sswrap_key = freq_update_qos_family_name_map[i].sswrap_key;
		if (freq_update_qos_family_name_map[i].qos_update_sync_mask & devfreq_sync_update) {
			mutex_lock(&platform_top_dev->dev_freq_lock);
			hash_for_each_possible(platform_top_dev->lwis_dev_freq_hash_table,
					       dev_freq_ptr, node, sswrap_key) {
				if (dev_freq_ptr->sswrap_key == sswrap_key) {
					if (lwis_erie_debug)
						ret = devfreq_aggregated_verify(
							lwis_dev,
							&dev_freq_ptr->aggregated_dev_freq_req,
							dev_freq_ptr->aggregated_dev_min_freq);

					/*
					 * Clear ispfe/ispbe/gcv ggregated_dev_min_freq
					 * for next vote
					 */
					dev_freq_ptr->aggregated_dev_min_freq = 0;
					break;
				}
			}
			mutex_unlock(&platform_top_dev->dev_freq_lock);
		}
		if (ret) {
			is_error = ret;
			dev_err(lwis_dev->dev,
				"devfreq_aggregated_update failed on %s min_freq request\n",
				freq_update_qos_family_name_map[i].qos_family_name);
		}
	}

	return is_error;
}

int lwis_platform_check_qos_box_probed(struct device *dev, const char *lwis_device_name)
{
	int ret = 0;

	if (strcmp(lwis_device_name, "isp-fe") == 0) {
		struct qos_box_dev *qos_box_ispfe_dev1, *qos_box_ispfe_dev2, *qos_box_ispfe_dev3;
		u32 vc_map_cfg_val1, vc_map_cfg_val2, vc_map_cfg_val3;

		/* Defer probe ISPFE device if qos_box device not probed yet. */
		qos_box_ispfe_dev1 = get_qos_box_dev_by_name(dev, "ispfe_data1");
		qos_box_ispfe_dev2 = get_qos_box_dev_by_name(dev, "ispfe_data2");
		qos_box_ispfe_dev3 = get_qos_box_dev_by_name(dev, "ispfe_data3");

		if (IS_ERR(qos_box_ispfe_dev1)) {
			ret = PTR_ERR(qos_box_ispfe_dev1);
			dev_err(dev, "get_qos_box_dev_by_name (%s) err = %d\n", "ispfe_data1", ret);
			return ret;
		}
		if (IS_ERR(qos_box_ispfe_dev2)) {
			ret = PTR_ERR(qos_box_ispfe_dev2);
			dev_err(dev, "get_qos_box_dev_by_name (%s) err = %d\n", "ispfe_data2", ret);
			return ret;
		}
		if (IS_ERR(qos_box_ispfe_dev3)) {
			ret = PTR_ERR(qos_box_ispfe_dev3);
			dev_err(dev, "get_qos_box_dev_by_name (%s) err = %d\n", "ispfe_data3", ret);
			return ret;
		}
		google_qos_box_vc_map_cfg_read(qos_box_ispfe_dev1, &vc_map_cfg_val1);
		google_qos_box_vc_map_cfg_read(qos_box_ispfe_dev2, &vc_map_cfg_val2);
		google_qos_box_vc_map_cfg_read(qos_box_ispfe_dev3, &vc_map_cfg_val3);

		if (lwis_erie_debug) {
			dev_info(
				dev,
				"At LWIS platform probe: Configurred ISPFE AXI 1, 2 QoS box vc_map_cfg_val1 = %u, vc_map_cfg_val2 = %u, vc_map_cfg_val3 = %u",
				vc_map_cfg_val1, vc_map_cfg_val2, vc_map_cfg_val3);
		}
	}

	return 0;
}

static int update_platform_top_dev_freq_table(struct lwis_platform_top_device *platform_top_dev,
					      struct lwis_device *lwis_dev,
					      enum lwis_device_sswrap_key sswrap_key,
					      bool *qos_box_probed)
{
	int ret;
	struct lwis_aggregated_dev_freq_entry *dev_freq_ptr;
	struct lwis_aggregated_dev_freq_entry *aggregated_dev_freq_entry = NULL;

	mutex_lock(&platform_top_dev->dev_freq_lock);
	hash_for_each_possible(platform_top_dev->lwis_dev_freq_hash_table, dev_freq_ptr, node,
			       sswrap_key) {
		if (dev_freq_ptr->sswrap_key == sswrap_key) {
			dev_freq_ptr->dev_cnt++;
			mutex_unlock(&platform_top_dev->dev_freq_lock);
			return 0;
		}
	}
	mutex_unlock(&platform_top_dev->dev_freq_lock);

	aggregated_dev_freq_entry =
		kmalloc(sizeof(struct lwis_aggregated_dev_freq_entry), GFP_KERNEL);
	if (!aggregated_dev_freq_entry)
		return -ENOMEM;

	aggregated_dev_freq_entry->sswrap_key = sswrap_key;
	aggregated_dev_freq_entry->dev_cnt = 1;
	aggregated_dev_freq_entry->aggregated_dev_min_freq = 0;
	/* devfreq lwis device (ispbe/ispfe/gcv) register user_min_freq_req. */
	ret = add_devfreq_request(lwis_dev, &aggregated_dev_freq_entry->aggregated_dev_freq_req);
	if (ret < 0) {
		kfree(aggregated_dev_freq_entry);
		return ret;
	}
	*qos_box_probed = true;
	mutex_lock(&platform_top_dev->dev_freq_lock);
	hash_add(platform_top_dev->lwis_dev_freq_hash_table, &aggregated_dev_freq_entry->node,
		 aggregated_dev_freq_entry->sswrap_key);
	mutex_unlock(&platform_top_dev->dev_freq_lock);

	return 0;
}

/*
 * platform_top_dev_pasrse_dt: Parse the interconnect-names from dt.
 * idx = 0: parsing the device interconnect-names.
 * idx = 2: parsing the aggregated interconnect-names.
 */
static int platform_top_dev_pasrse_dt(struct lwis_device *lwis_dev, char *icc_path_name,
				      bool aggregated_parse)
{
	const char *icc_path_str;
	struct device_node *dev_node = lwis_dev->k_dev->of_node;
	int count;
	int ret;

	count = of_property_count_strings(dev_node, "interconnect-names");
	/* No interconnect-names found, just return */
	if (count <= 0) {
		icc_path_name[0] = '\0';
		dev_warn(lwis_dev->dev, "No interconnect-names found");
		return 0;
	}
	ret = of_property_read_string_index(dev_node, "interconnect-names",
					    aggregated_parse ? count / 2 : 0, &icc_path_str);
	if (ret < 0) {
		dev_err(lwis_dev->dev, "Error get icc_path_str (%d)\n", ret);
		goto exit;
	}
	strscpy(icc_path_name, icc_path_str, LWIS_MAX_NAME_STRING_LEN);

exit:
	return ret;
}

static int update_platform_top_dev_aggregated_icc_paths_table(
	struct lwis_platform_top_device *platform_top_dev, struct lwis_device *lwis_dev,
	enum lwis_device_sswrap_key sswrap_key)
{
	int ret;
	struct lwis_aggregated_icc_paths_entry *aggregated_icc_paths_ptr;
	struct lwis_aggregated_icc_paths_entry *aggregated_icc_paths_entry;

	mutex_lock(&platform_top_dev->icc_path_lock);
	hash_for_each_possible(platform_top_dev->lwis_aggregated_icc_paths_hash_table,
			       aggregated_icc_paths_ptr, node, sswrap_key) {
		if (aggregated_icc_paths_ptr->sswrap_key == sswrap_key) {
			aggregated_icc_paths_ptr->dev_cnt++;
			mutex_unlock(&platform_top_dev->icc_path_lock);
			return 0;
		}
	}
	mutex_unlock(&platform_top_dev->icc_path_lock);

	aggregated_icc_paths_entry = kmalloc(sizeof(*aggregated_icc_paths_entry), GFP_KERNEL);
	if (!aggregated_icc_paths_entry)
		return -ENOMEM;

	aggregated_icc_paths_entry->sswrap_key = sswrap_key;
	aggregated_icc_paths_entry->dev_cnt = 1;
	/* Parse Aggregated device interconnect-names */
	ret = platform_top_dev_pasrse_dt(lwis_dev,
					 aggregated_icc_paths_entry->aggregated_icc_path_name,
					 /* aggregated_parse = */ true);
	if (ret < 0 || *aggregated_icc_paths_entry->aggregated_icc_path_name == '\0') {
		kfree(aggregated_icc_paths_entry);
		return ret;
	}
	ret = get_icc_paths_by_name(lwis_dev, &aggregated_icc_paths_entry->aggregated_icc_path,
				    aggregated_icc_paths_entry->aggregated_icc_path_name);
	if (ret < 0) {
		kfree(aggregated_icc_paths_entry);
		return ret;
	}

	mutex_lock(&platform_top_dev->icc_path_lock);
	hash_add(platform_top_dev->lwis_aggregated_icc_paths_hash_table,
		 &aggregated_icc_paths_entry->node, aggregated_icc_paths_entry->sswrap_key);
	mutex_unlock(&platform_top_dev->icc_path_lock);

	return 0;
}

static int
update_platform_top_dev_icc_paths_table(struct lwis_platform_top_device *platform_top_dev,
					struct lwis_device *lwis_dev,
					enum lwis_device_name_key dev_name_key)
{
	struct lwis_dev_icc_paths_entry *dev_icc_paths_entry;
	int ret;

	dev_icc_paths_entry = kmalloc(sizeof(*dev_icc_paths_entry), GFP_KERNEL);
	if (!dev_icc_paths_entry)
		return -ENOMEM;

	dev_icc_paths_entry->name_key = dev_name_key;
	memset(dev_icc_paths_entry->expected_dev_qos_settings, 0,
	       sizeof(dev_icc_paths_entry->expected_dev_qos_settings));
	/* Parse Lwis Device interconnect-names from dt */
	ret = platform_top_dev_pasrse_dt(lwis_dev, dev_icc_paths_entry->dev_icc_path_name,
					 /* aggregated_parse= */ false);
	if (ret < 0 || *dev_icc_paths_entry->dev_icc_path_name == '\0') {
		kfree(dev_icc_paths_entry);
		return ret;
	}
	ret = get_icc_paths_by_name(lwis_dev, &dev_icc_paths_entry->dev_icc_path,
				    dev_icc_paths_entry->dev_icc_path_name);
	if (ret < 0) {
		kfree(dev_icc_paths_entry);
		return ret;
	}

	mutex_lock(&platform_top_dev->icc_path_lock);
	hash_add(platform_top_dev->lwis_dev_icc_paths_hash_table, &dev_icc_paths_entry->node,
		 dev_icc_paths_entry->name_key);
	mutex_unlock(&platform_top_dev->icc_path_lock);

	return 0;
}

/*
 * lwis_platform_update_top_dev: Populate the platform_top_dev info via
 * devices probed before.
 */
int lwis_platform_update_top_dev(struct lwis_device *top_dev, struct lwis_device *lwis_dev,
				 bool *qos_box_probed)
{
	int ret;
	int idx;
	struct lwis_top_device *lwis_top_dev =
		container_of(top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;
	struct lwis_ioreg_device *ioreg_dev =
		container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	enum lwis_device_sswrap_key sswrap_key = sswrap_key_int32_to_enum(ioreg_dev->sswrap_key);
	enum lwis_device_name_key dev_name_key;

	idx = fetch_name_idx(lwis_dev->name, STRUCT_DEV_NAME_MAP);
	dev_name_key = dev_name_map[idx].dev_name_key;
	if (ioreg_dev == NULL || sswrap_key == SSWRP_UNKNOWN || dev_name_key == DEV_UNKNOWN)
		return 0;

	if (platform_top_dev == NULL) {
		dev_warn(lwis_dev->dev, "Platform top dev has not probed yet");
		return 0;
	}

	/* Populate hasbtable: lwis_dev_freq_hash_table */
	ret = update_platform_top_dev_freq_table(platform_top_dev, lwis_dev, sswrap_key,
						 qos_box_probed);
	if (ret < 0)
		goto exit;

	/* Populate hasbtable: lwis_dev_icc_paths_hash_table */
	ret = update_platform_top_dev_icc_paths_table(platform_top_dev, lwis_dev, dev_name_key);
	if (ret < 0)
		goto exit;

	/* Populate hasbtable: lwis_aggregated_icc_paths_hash_table */
	ret = update_platform_top_dev_aggregated_icc_paths_table(platform_top_dev, lwis_dev,
								 sswrap_key);
	if (ret < 0)
		goto exit;

exit:
	return ret;
}

int lwis_platform_remove_qos(struct lwis_device *lwis_dev)
{
	return 0;
}

int lwis_platform_update_bts(struct lwis_device *lwis_dev, int block, unsigned int bw_kb_peak,
			     unsigned int bw_kb_read, unsigned int bw_kb_write,
			     unsigned int bw_kb_rt)
{
	return 0;
}

int lwis_platform_set_default_irq_affinity(unsigned int irq)
{
	const int cpu = 0x2;

	return irq_set_affinity_hint(irq, cpumask_of(cpu));
}

int lwis_platform_get_default_pt_id(void)
{
	return PT_PTID_INVALID;
}

void lwis_platform_set_device_state(struct lwis_device *lwis_dev, bool camera_up)
{
	static uint32_t camera_stat;
	int64_t set_cam_stat;

	/* Skip the virtual and invalid device type. */
	if (lwis_dev->type == DEVICE_TYPE_TEST || lwis_dev->type >= NUM_DEVICE_TYPES ||
	    lwis_dev->type < DEVICE_TYPE_TOP)
		return;

	set_cam_stat = camera_stat;
	if (camera_up)
		set_cam_stat++;
	else
		set_cam_stat--;

	/* cam_stat validation */
	if (set_cam_stat > U32_MAX || set_cam_stat < 0) {
		dev_err(lwis_dev->dev, "Invalid camera_stat(%lld)", (int64_t)set_cam_stat);
		set_cam_stat = U32_MAX;
		camera_stat = 0;
	} else
		camera_stat = set_cam_stat;

	google_cdd_set_system_dev_stat(CDD_SYSTEM_DEVICE_CAMERA, (uint32_t)set_cam_stat);
}

bool lwis_platform_is_batch_register_io_supported(void)
{
	/* Support for this feature begins with this platform device. */
	return true;
}

int lwis_platform_dpm_op_level_get(struct lwis_device *lwis_dev, struct lwis_dpm_op_level *op_level)
{
	int ret = 0;
	enum mbfs_error_code mbfs_ret;
	union val64 mbfs_op_level;
	struct lwis_top_device *lwis_top_dev =
		container_of(lwis_dev->top_dev, struct lwis_top_device, base_dev);
	struct lwis_platform_top_device *platform_top_dev = lwis_top_dev->platform_top_dev;
	int mbfs_hdl_idx = NUM_HLS;

	for (int idx = 0; idx < LWIS_DEVFRQ_OP_LEVEL_HDLS; ++idx) {
		if (strcmp(op_level->entity, dname_mbfs_hdl_map[idx].requesting_entity) == 0) {
			mbfs_hdl_idx = dname_mbfs_hdl_map[idx].hdl_idx;
			break;
		}
	}

	if (mbfs_hdl_idx >= NUM_HLS) {
		dev_err(lwis_dev->dev, "MBFS index invalid %d", mbfs_hdl_idx);
		return -EINVAL;
	}

	/* Probe the mbfs client ispbe, ispfe or gcv's handlers at first access */
	ret = mbfs_client_handler_reuse_or_probe_via_domain_name(
		lwis_dev, &(platform_top_dev->lwis_max_opp_h[mbfs_hdl_idx]),
		mbfs_client_dname_map[mbfs_hdl_idx], CLOCK);
	if (ret)
		return -EIO;

	mbfs_ret = mbfs_read_file(platform_top_dev->lwis_max_opp_h[mbfs_hdl_idx], &mbfs_op_level);
	if (mbfs_ret != MBFS_OK) {
		dev_err(lwis_dev->dev, "MBFS opp level read failed on handler(%s): err_str(%s)\n",
			mbfs_client_dname_map[mbfs_hdl_idx], get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	op_level->level = mbfs_op_level.number;

	if (lwis_erie_debug) {
		dev_info(lwis_dev->dev, "Current op level for %s is %llu",
			 mbfs_client_dname_map[mbfs_hdl_idx], op_level->level);
	}

	return ret;
}
