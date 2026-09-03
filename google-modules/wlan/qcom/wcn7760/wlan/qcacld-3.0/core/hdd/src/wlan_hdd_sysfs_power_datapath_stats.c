/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_sysfs_power_datapath_stats.c
 *
 * Implementation for creating sysfs files for power and datapath statistics
 */

#include <wlan_hdd_includes.h>
#include <wlan_hdd_sysfs.h>
#include "osif_vdev_sync.h"
#include <wlan_hdd_sysfs_power_datapath_stats.h>
#include <wmi_unified_param.h>
#include <wlan_lmac_if_def.h>
#include <wlan_cp_stats_mc_ucfg_api.h>
#include <wlan_cp_stats_mc_defs.h>
#include <wlan_cp_stats_utils_api.h>

/**
 * SAFE_SCNPRINTF - Safely append formatted string to buffer with
 *                  bounds checking
 * @buf: Output buffer
 * @size: Total buffer size
 * @offset: Current offset (will be updated)
 * @fmt: Format string
 * @...: Format arguments
 *
 * Safely appends formatted string to buffer, checking bounds and
 * handling errors. Returns 0 on success, -1 on error.
 */
#define SAFE_SCNPRINTF(buf, size, offset, fmt, ...) \
({ \
	int _ret = 0; \
	if ((offset) < (size)) { \
		int _n = scnprintf((buf) + (offset), (size) - (offset), \
				   fmt, ##__VA_ARGS__); \
		if (_n > 0 && (offset) + _n <= (size)) { \
			(offset) += _n; \
		} else { \
			_ret = -1; \
		} \
	} else { \
		_ret = -1; \
	} \
	_ret; \
})

#ifdef WLAN_FEATURE_POWER_STATISTICS
/**
 * get_rate_name() - Convert rate index to human readable string
 * @rate_idx: Rate index from firmware
 *
 * Return: Rate name string or "UNKNOWN"
 */
static const char *get_rate_name(uint32_t rate_idx)
{
	static const char * const rate_names[] = {
		"CCK_1M", "CCK_2M", "CCK_5.5M", "CCK_11M",
		"OFDM_6M", "OFDM_9M", "OFDM_12M", "OFDM_18M",
		"OFDM_24M", "OFDM_36M", "OFDM_48M", "OFDM_54M",
		"MCS0", "MCS1", "MCS2", "MCS3", "MCS4", "MCS5", "MCS6", "MCS7",
		"MCS8", "MCS9", "MCS10", "MCS11", "MCS12", "MCS13"
	};

	if (rate_idx < QDF_ARRAY_SIZE(rate_names))
		return rate_names[rate_idx];
	return "UNKNOWN";
}

/**
 * get_band_str() - Convert band index to human readable string
 * @band: Band index from firmware
 *
 * Return: Band name string
 */
static const char *get_band_str(uint8_t band)
{
	switch (band) {
	case 0: return "2.4GHz";
	case 1: return "5GHz";
	case 2: return "6GHz";
	default: return "Unknown";
	}
}

/**
 * get_bw_str() - Convert bandwidth index to human readable string
 * @bw: Bandwidth index from firmware
 *
 * Return: Bandwidth string
 */
static const char *get_bw_str(uint8_t bw)
{
	switch (bw) {
	case 0: return "20MHz";
	case 1: return "40MHz";
	case 2: return "80MHz";
	case 3: return "160MHz";
	case 4: return "80+80MHz";
	case 5: return "5MHz";
	case 6: return "10MHz";
	case 7: return "165MHz";
	case 8: return "160+160MHz";
	case 9: return "320MHz";
	default: return "Unknown";
	}
}

/**
 * __hdd_sysfs_power_datapath_stats_show() - Display statistics from
 *                                            pdev CP stats
 * @net_dev: network device
 * @buf: output buffer
 *
 * Return: number of bytes written
 */
static ssize_t
__hdd_sysfs_power_datapath_stats_show(struct net_device *net_dev, char *buf)
{
	struct hdd_adapter *adapter;
	struct hdd_context *hdd_ctx;
	struct wlan_objmgr_vdev *vdev;
	struct wlan_objmgr_pdev *pdev;
	struct cp_stats_power_datapath_info cp_stats;
	QDF_STATUS status;
	ssize_t ret = 0;
	uint32_t i, j;
	struct cp_stats_tx_rate_info *rate;

	adapter = netdev_priv(net_dev);
	if (hdd_validate_adapter(adapter))
		return -EINVAL;

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	if (!wlan_hdd_validate_modules_state(hdd_ctx))
		return -EINVAL;

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(hdd_ctx->psoc,
						    adapter->deflink->vdev_id,
						    WLAN_CP_STATS_ID);
	if (!vdev) {
		ret = scnprintf(buf, PAGE_SIZE, "Failed to get vdev\n");
		return ret;
	}

	pdev = wlan_vdev_get_pdev(vdev);
	if (!pdev) {
		ret = scnprintf(buf, PAGE_SIZE, "Failed to get pdev\n");
		goto release_vdev;
	}

	qdf_mem_zero(&cp_stats, sizeof(cp_stats));

	status = ucfg_cp_stats_get_power_datapath_stats(pdev, &cp_stats);

	if (QDF_IS_STATUS_ERROR(status)) {
		if (status == QDF_STATUS_E_EMPTY) {
			ret = scnprintf(buf, PAGE_SIZE,
					"No statistics available");
		} else {
			ret = scnprintf(buf, PAGE_SIZE,
					"Failed to retrieve statistics: %d\n",
					status);
		}
	goto release_vdev;
	}

	/* Reset offset for SAFE_SCNPRINTF usage */
	ret = 0;

	/* Display header */
	if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
			   "=== Power & Datapath Statistics ===\n") < 0)
		goto release_vdev;

	if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
			   "Status: %s (%u)\n",
			   cp_stats.status == 0 ? "Success" : "Error",
			   cp_stats.status) < 0)
		goto release_vdev;

	/* Display power statistics */
	if (cp_stats.num_power_stats > 0 && cp_stats.power_stats) {
		if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
				   "\n--- Power Statistics ---\n") < 0)
			goto release_vdev;

		if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
				   "Total Power Stat Entries: %u\n",
				   cp_stats.num_power_stats) < 0)
			goto release_vdev;

		/* Loop through all power stat entries (one per core) */
		for (i = 0; i < cp_stats.num_power_stats; i++) {
			if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
					   "\n[Core %u]\n",
					   cp_stats.power_stats[i].core_index) < 0)
				goto release_vdev;

			if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
					   "Radio On Time:        %u ms\n",
					   cp_stats.power_stats[i].radio_on_time) < 0)
				goto release_vdev;

			if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
					   "Radio Off Time:       %u ms\n",
					   cp_stats.power_stats[i].radio_off_time) < 0)
				goto release_vdev;

			if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
					   "WLAN Power On Time:   %u ms\n",
					   cp_stats.power_stats[i].wlan_pwr_on_time) < 0)
				goto release_vdev;

			if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
					   "TX Time:              %u ms\n",
					   cp_stats.power_stats[i].tx_time) < 0)
				goto release_vdev;

			if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
					   "RX Time:              %u ms\n",
					   cp_stats.power_stats[i].rx_time) < 0)
				goto release_vdev;

			if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
					   "Sleep Levels Count:   %u\n",
					   cp_stats.power_stats[i].sleep_levels_num) < 0)
				goto release_vdev;

			for (j = 0;
			     j < cp_stats.power_stats[i].sleep_levels_num &&
			     j < WMI_MAX_SLEEP_LEVELS; j++) {
				if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
						   "  Level %u Sleep Time: %u ms\n",
						   j,
						   cp_stats.power_stats[i].sleep_time_per_levels[j]) < 0)
					goto release_vdev;
			}
		}
	} else {
		if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
				   "\n--- Power Statistics ---\n") < 0)
			goto release_vdev;

		if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
				   "Not available\n") < 0)
			goto release_vdev;
	}

	/* Display datapath statistics */
	if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
			   "\n--- Datapath Statistics ---\n") < 0)
		goto release_vdev;

	if (cp_stats.num_tx_rate_stats > 0 && cp_stats.tx_rate_stats) {
		if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
				   "Total TX Rate Entries: %u\n",
				   cp_stats.num_tx_rate_stats) < 0)
			goto release_vdev;

		if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
				   "\nTX Rates:\n") < 0)
			goto release_vdev;

		for (i = 0; i < cp_stats.num_tx_rate_stats; i++) {
			rate = &cp_stats.tx_rate_stats[i];

			if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
					   "  [%u] Core:%u, %s, %s, %s, NSS%u: %u pkts, %u retries\n",
					   i + 1,
					   rate->core_index,
					   get_rate_name(rate->rate_index),
					   get_band_str(rate->band),
					   get_bw_str(rate->bw),
					   rate->nss + 1,
					   rate->count,
					   rate->tx_retry_count) < 0)
				goto release_vdev;
		}
	} else {
		if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
				   "Not available\n") < 0)
			goto release_vdev;
	}

	if (SAFE_SCNPRINTF(buf, PAGE_SIZE, ret,
			   "===================================\n") < 0)
		goto release_vdev;

release_vdev:
	wlan_objmgr_vdev_release_ref(vdev, WLAN_CP_STATS_ID);
	return ret;
}
#else
static ssize_t
__hdd_sysfs_power_datapath_stats_show(struct net_device *net_dev, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
			 "Power datapath stats feature not enabled\n");
}
#endif

/**
 * hdd_sysfs_power_datapath_stats_show() - Sysfs show function
 * @dev: device
 * @attr: device attribute
 * @buf: output buffer
 *
 * Return: number of bytes written
 */
static ssize_t
hdd_sysfs_power_datapath_stats_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct net_device *net_dev = container_of(dev, struct net_device, dev);
	struct osif_vdev_sync *vdev_sync;
	ssize_t err_size;

	err_size = osif_vdev_sync_op_start(net_dev, &vdev_sync);
	if (err_size)
		return err_size;

	err_size = __hdd_sysfs_power_datapath_stats_show(net_dev, buf);

	osif_vdev_sync_op_stop(vdev_sync);

	return err_size;
}

/**
 * __hdd_sysfs_power_datapath_stats_store() - Process user commands
 * @net_dev: network device
 * @buf: input buffer
 * @count: buffer length
 *
 * Return: number of bytes processed
 */
static ssize_t
__hdd_sysfs_power_datapath_stats_store(struct net_device *net_dev,
				       const char *buf, size_t count)
{
	struct hdd_adapter *adapter;
	struct hdd_context *hdd_ctx;
	char buf_local[64];
	char *sptr, *token;
	uint32_t operation = 0;
	uint32_t stats_type = 0;
	uint32_t core_index = 0;
	QDF_STATUS status;
	int ret;
	struct wlan_objmgr_vdev *vdev;
	uint32_t pdev_id;
	struct request_info info;

	adapter = netdev_priv(net_dev);
	if (hdd_validate_adapter(adapter))
		return -EINVAL;

	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	if (!wlan_hdd_validate_modules_state(hdd_ctx))
		return -EINVAL;

	ret = hdd_sysfs_validate_and_copy_buf(buf_local, sizeof(buf_local),
					      buf, count);
	if (ret) {
		hdd_err_rl("Invalid input");
		return ret;
	}

	sptr = buf_local;

	/* Get first non-empty token (skip leading whitespace) */
	do {
		token = strsep(&sptr, " \t\n");
	} while (token && !*token);

	if (!token) {
		hdd_err_rl("No command found");
		return -EINVAL;
	}

	/* Parse command: "disable|enable|retrieve" */
	if (!strncmp(token, "retrieve", 8)) {
		operation = 2;
	} else if (!strncmp(token, "disable", 7) ||
		   !strncmp(token, "enable", 6)) {
		hdd_err_rl("Operation '%s' not supported", token);
		return -EOPNOTSUPP;
	} else {
		hdd_err_rl("Invalid operation: %s", token);
		return -EINVAL;
	}

	/* Parse stats type: "power|datapath|all" */
	/* Get next non-empty token (skip whitespace between tokens) */
	do {
		token = strsep(&sptr, " \t\n");
	} while (token && !*token);

	if (!token) {
		hdd_err_rl("Missing stats type");
		return -EINVAL;
	}

	if (!strncmp(token, "power", 5)) {
		stats_type = 0x01; /* WMI_PDEV_POWER_STATS_TYPE */
	} else if (!strncmp(token, "datapath", 8)) {
		stats_type = 0x02; /* WMI_PDEV_DATAPATH_STATS_TYPE */
	} else if (!strncmp(token, "all", 3)) {
		stats_type = 0x03; /* WMI_PDEV_POWER_DATAPATH_STATS_ALL */
	} else {
		hdd_err_rl("Invalid stats type: %s", token);
		return -EINVAL;
	}

	/* Parse core_index (mandatory): "0|0xFF" */
	/* Get next non-empty token (skip whitespace between tokens) */
	do {
		token = strsep(&sptr, " \t\n");
	} while (token && !*token);

	if (!token) {
		hdd_err_rl("Missing core_index parameter");
		hdd_err_rl("Usage: echo 'retrieve <stats> <core_idx>' > power_datapath_stats");
		hdd_err_rl("  stats: power, datapath, all");
		hdd_err_rl("  core_idx: 0(MAC0), 1(MAC1), 0xFF or 255 (all MACs)");
		return -EINVAL;
	}

	/* Parse core_index - support both hex (0xFF) and decimal (255) */
	ret = kstrtou32(token, 0, &core_index);  /* base 0 = auto-detect */
	if (ret) {
		hdd_err_rl("Invalid core_index format: %s", token);
		return -EINVAL;
	}

	/* Validate core_index value */
	if (core_index != 0 && core_index != 1 && core_index != 0xFF) {
		hdd_err_rl("Invalid core_index value: 0x%X", core_index);
		hdd_err_rl("Valid values: 0 (MAC0), 1 (MAC1), 0xFF (all MACs)");
		return -EINVAL;
	}

	/* Send command to firmware via CP stats UCFG API */

	vdev = wlan_objmgr_get_vdev_by_id_from_psoc(hdd_ctx->psoc,
						    adapter->deflink->vdev_id,
						    WLAN_OSIF_STATS_ID);
	if (!vdev) {
		hdd_err_rl("Vdev is NULL");
		return -EINVAL;
	}

	pdev_id = wlan_objmgr_pdev_get_pdev_id(wlan_vdev_get_pdev(vdev));

	/* Send request via CP stats UCFG API with all parsed parameters */
	/* Initialize info structure */
	qdf_mem_zero(&info, sizeof(info));

	info.vdev_id = wlan_vdev_get_id(vdev);
	info.pdev_id = pdev_id;
#ifdef WLAN_FEATURE_POWER_STATISTICS
	info.power_dp_operation = operation;
	info.power_dp_stats_type = stats_type;
	info.power_dp_core_index = core_index;
#endif
	status =
	ucfg_send_power_datapath_stats_request(vdev,
					       TYPE_POWER_DATAPATH_STATS,
					       &info);

	wlan_objmgr_vdev_release_ref(vdev, WLAN_OSIF_STATS_ID);

	if (QDF_IS_STATUS_ERROR(status)) {
		if (status == QDF_STATUS_E_INVAL) {
			hdd_err_rl("Stats request already in progress");
			return -EBUSY;
		}
		if (status == QDF_STATUS_E_AGAIN) {
			hdd_err_rl("CP stats suspended");
			return -EAGAIN;
		}
		hdd_err_rl("Failed to send CP stats request: %d", status);
		return -EIO;
	}

	hdd_debug("CP stats request sent: op=%u, type=%u, core_idx=0x%X",
		  operation, stats_type, core_index);

	return count;
}

/**
 * hdd_sysfs_power_datapath_stats_store() - Sysfs store function
 * @dev: device
 * @attr: device attribute
 * @buf: input buffer
 * @count: buffer length
 *
 * Return: number of bytes processed
 */
static ssize_t
hdd_sysfs_power_datapath_stats_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct net_device *net_dev = container_of(dev, struct net_device, dev);
	struct osif_vdev_sync *vdev_sync;
	ssize_t err_size;

	err_size = osif_vdev_sync_op_start(net_dev, &vdev_sync);
	if (err_size)
		return err_size;

	err_size = __hdd_sysfs_power_datapath_stats_store(net_dev, buf, count);

	osif_vdev_sync_op_stop(vdev_sync);

	return err_size;
}

static DEVICE_ATTR(power_datapath_stats, 0660,
		   hdd_sysfs_power_datapath_stats_show,
		   hdd_sysfs_power_datapath_stats_store);

static struct attribute *power_datapath_stats_attrs[] = {
	&dev_attr_power_datapath_stats.attr,
	NULL,
};

static struct attribute_group power_datapath_stats_attr_group = {
	.name = "power_datapath_stats",
	.attrs = power_datapath_stats_attrs,
};

int hdd_sysfs_power_datapath_stats_create(struct hdd_adapter *adapter)
{
	int ret;

	if (!adapter) {
		hdd_err("Adapter is NULL");
		return -EINVAL;
	}

	ret = sysfs_create_group(&adapter->dev->dev.kobj,
				 &power_datapath_stats_attr_group);
	if (ret) {
		hdd_err("Failed to create sysfs group: %d", ret);
		return ret;
	}

	return 0;
}

void hdd_sysfs_power_datapath_stats_destroy(struct hdd_adapter *adapter)
{
	if (!adapter) {
		hdd_err("Adapter is NULL");
		return;
	}

	sysfs_remove_group(&adapter->dev->dev.kobj,
			   &power_datapath_stats_attr_group);
}
