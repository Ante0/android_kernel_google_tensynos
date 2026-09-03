/*
 * Google Battery Management System
 *
 * Copyright (C) 2018 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define gbms_owner(p)	((p)->owner_name ? (p)->owner_name : "google_bms")

#define gbms_info(p, fmt, ...)	\
	pr_info("%s: " fmt, gbms_owner(p), ##__VA_ARGS__)
#define gbms_warn(p, fmt, ...)	\
	pr_warn("%s: " fmt, gbms_owner(p), ##__VA_ARGS__)
#define gbms_err(p, fmt, ...)	\
	pr_err("%s: " fmt, gbms_owner(p), ##__VA_ARGS__)

#pragma clang diagnostic ignored "-Wenum-conversion"

#include <linux/cleanup.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <misc/logbuffer.h>

#include "google_psy.h"
#include "google_bms.h"

/* sync from google/logbuffer.c */
#define LOG_BUFFER_ENTRY_SIZE   256

#define GBMS_DEFAULT_FV_UV_RESOLUTION   25000
#define GBMS_DEFAULT_FV_UV_MARGIN_DPCT  1020
#define GBMS_DEFAULT_FV_DC_RATIO        20
#define GBMS_DEFAULT_CV_DEBOUNCE_CNT    3
#define GBMS_DEFAULT_CV_UPDATE_INTERVAL 2000
#define GBMS_DEFAULT_CV_TIER_OV_CNT     10
#define GBMS_DEFAULT_CV_TIER_SWITCH_CNT 3
#define GBMS_DEFAULT_CV_OTV_MARGIN      0

#define GBMS_STORAGE_READ_DELAY_MS	1000
#define GBMS_STORAGE_READ_RETRIES	20

/*************************
 * WCIN PSY REGISTRATION   *
 *************************/

const enum power_supply_property google_wcin_props[GOOGLE_WLCIN_PROP_SIZE] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	/* Increment GOOGLE_WLCIN_PROP_SIZE if adding new value */
};
EXPORT_SYMBOL(google_wcin_props);

/* same as POWER_SUPPLY_CHARGE_TYPE_TEXT */
static const char *psy_chgt_str[] = {
	[POWER_SUPPLY_CHARGE_TYPE_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_CHARGE_TYPE_NONE]		= "N/A",
	[POWER_SUPPLY_CHARGE_TYPE_TRICKLE]	= "Trickle",
	[POWER_SUPPLY_CHARGE_TYPE_FAST]		= "Fast",
	[POWER_SUPPLY_CHARGE_TYPE_STANDARD]	= "Standard",
	[POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE]	= "Adaptive",
	[POWER_SUPPLY_CHARGE_TYPE_CUSTOM]	= "Custom",
	[POWER_SUPPLY_CHARGE_TYPE_LONGLIFE]	= "Long Life",
	[POWER_SUPPLY_CHARGE_TYPE_TAPER_EXT]	= "Taper",
};

const char *gbms_chg_type_s(int cgh_type)
{
	if (cgh_type < 0 || cgh_type > ARRAY_SIZE(psy_chgt_str))
		return "<err>";
	return psy_chgt_str[cgh_type];
}
EXPORT_SYMBOL_GPL(gbms_chg_type_s);

/* same as POWER_SUPPLY_STATUS_TEXT */
static const char *psy_chgs_str[] = {
	"Unknown", "Charging", "Discharging", "Not Charging", "Full"
};

const char *gbms_chg_status_s(int chg_status)
{
	if (chg_status < 0 || chg_status > ARRAY_SIZE(psy_chgs_str))
		return "<err>";
	return psy_chgs_str[chg_status];
}
EXPORT_SYMBOL_GPL(gbms_chg_status_s);


const char *gbms_chg_ev_adapter_s(int adapter)
{
	static char *chg_ev_adapter_type_str[] = {
		FOREACH_CHG_EV_ADAPTER(CHG_EV_ADAPTER_STRING)
	};

	if (adapter < 0 || adapter > ARRAY_SIZE(chg_ev_adapter_type_str))
		return "<err>";
	return chg_ev_adapter_type_str[adapter];
}
EXPORT_SYMBOL_GPL(gbms_chg_ev_adapter_s);

/* Convert gbms_msc_states_t to letter code */
static const char *gbms_get_code(const int index)
{
	const static char *codes[] = {"n", "s", "d", "l", "v", "vo", "p", "f",
				      "t", "dl", "st", "tc", "r", "w", "rs",
				      "n", "ny", "do", "h", "hp", "ha"};
	const int len = ARRAY_SIZE(codes);

	return (index >= 0 && index < len) ? codes[index] : "?";
}

struct device_node *gbms_batt_id_node(struct device_node *config_node)
{
	struct device_node *child_node;
	int retry_cnt = GBMS_STORAGE_READ_RETRIES;
	int ret = 0;
	u32 batt_id, gbatt_id;

	do {
		ret = gbms_storage_read(GBMS_TAG_BRID, &batt_id, sizeof(batt_id));
		if (ret != -EPROBE_DEFER)
			break;

		msleep(GBMS_STORAGE_READ_DELAY_MS);
	} while (ret < 0 && retry_cnt-- > 0);

	if (ret < 0) {
		pr_warn("Failed to get batt_id (%d)\n", ret);
		return of_node_get(config_node);
	}

	for_each_child_of_node(config_node, child_node) {
		ret = of_property_read_u32(child_node, "google,batt-id",
					   &gbatt_id);
		if (ret != 0)
			continue;

		if (batt_id == gbatt_id)
			return child_node;
	}

	return of_node_get(config_node);
}
EXPORT_SYMBOL_GPL(gbms_batt_id_node);

/* convert C rates to current. Caller can account for tolerances reducing
 * battery_capacity. cc_ua_resolution is used to create discrete steps.
 * NOTE: the call covert C rates to chanrge currents IN PLACE, ie you cannot
 * call this twice.
 */
void gbms_init_chg_table(struct gbms_chg_profile *profile,
			 struct device_node *node, u32 capacity_ma)
{
	u32 ccm;
	int vi, ti, ret = 0;
	const int cc_ua_step = profile->cc_ua_resolution;
	int temp_nb_count = profile->temp_nb_limits - 1;
	u32 cccm_array_size = (profile->temp_nb_limits - 1)
			       * profile->volt_nb_limits;
	struct device_node *batt_id_node __free(device_node) = NULL;

	profile->capacity_ma = capacity_ma;

	/* aact profile is updated from server side */
	if (profile->aact_init_profile && profile->aact_update_profile) {
		temp_nb_count = (profile->temp_nb_limits - 1) * GBMS_AACT_NB_LIMITS(profile);
		cccm_array_size = temp_nb_count * profile->volt_nb_limits;
		memcpy(profile->cccm_limits, profile->aact_cccm_limits,
		       sizeof(s32) * cccm_array_size);
		goto chg_table;
	}

	batt_id_node = gbms_batt_id_node(node);

	/* load default profile */
	if (!profile->aact_init_profile) {
		ret = of_property_read_u32_array(node, "google,chg-cc-limits",
						 profile->cccm_limits,
						 cccm_array_size);
	} else {
		char *aact_cc_limits;

		temp_nb_count = (profile->temp_nb_limits - 1) * GBMS_AACT_NB_LIMITS(profile);
		cccm_array_size = temp_nb_count * profile->volt_nb_limits;

		aact_cc_limits = aact_all_limits.cc_limits[GBMS_AACT_IDX(profile)];

		ret = of_property_read_u32_array(batt_id_node, aact_cc_limits,
						 profile->cccm_limits,
						 cccm_array_size);
		if (ret < 0)
			ret = of_property_read_u32_array(node, aact_cc_limits,
							 profile->cccm_limits,
							 cccm_array_size);
	}

	if (ret < 0)
		pr_warn("unable to get default cccm_limits.\n");

chg_table:
	/* chg-battery-capacity is in mAh, chg-cc-limits relative to 100 */
	for (ti = 0; ti < temp_nb_count; ti++) {
		for (vi = 0; vi < profile->volt_nb_limits; vi++) {
			ccm = GBMS_CCCM_LIMITS_GET(profile, ti, vi);
			ccm *= capacity_ma * 10;

			/* round to the nearest resolution */
			if (cc_ua_step)
				ccm = DIV_ROUND_CLOSEST(ccm, cc_ua_step)
					* cc_ua_step;

			GBMS_CCCM_LIMITS_SET(profile, ti, vi) = ccm;
		}
	}
}
EXPORT_SYMBOL_GPL(gbms_init_chg_table);

/* configure standard device charge profile properties */
static int gbms_read_cccm_limits(struct gbms_chg_profile *profile,
				 struct device_node *node)
{
	int ret;
	struct device_node *batt_id_node __free(device_node) = NULL;

	profile->temp_nb_limits =
	    of_property_count_elems_of_size(node, "google,chg-temp-limits",
					    sizeof(u32));
	if (profile->temp_nb_limits <= 0) {
		ret = profile->temp_nb_limits;
		gbms_err(profile, "cannot read chg-temp-limits, ret=%d\n", ret);
		return -EINVAL;
	}
	if (profile->temp_nb_limits > GBMS_CHG_TEMP_NB_LIMITS_MAX) {
		gbms_err(profile, "chg-temp-nb-limits exceeds driver max: %d\n",
		       GBMS_CHG_TEMP_NB_LIMITS_MAX);
		return -EINVAL;
	}
	ret = of_property_read_u32_array(node, "google,chg-temp-limits",
					 (u32 *)profile->temp_limits,
					 profile->temp_nb_limits);
	if (ret < 0) {
		gbms_err(profile, "cannot read chg-temp-limits table, ret=%d\n",
			 ret);
		return ret;
	}

	batt_id_node = gbms_batt_id_node(node);

	profile->volt_nb_limits =
	    of_property_count_elems_of_size(batt_id_node, "google,chg-cv-limits",
					    sizeof(u32));
	/* google,chg-cv-limits does not exist in the child_node */
	if (profile->volt_nb_limits <= 0)
		profile->volt_nb_limits =
		    of_property_count_elems_of_size(node, "google,chg-cv-limits",
						    sizeof(u32));
	if (profile->volt_nb_limits <= 0) {
		ret = profile->volt_nb_limits;
		gbms_err(profile, "cannot read chg-cv-limits, ret=%d\n", ret);
		return -EINVAL;
	}
	if (profile->volt_nb_limits > GBMS_CHG_VOLT_NB_LIMITS_MAX) {
		gbms_err(profile, "chg-cv-nb-limits exceeds driver max: %d\n",
		       GBMS_CHG_VOLT_NB_LIMITS_MAX);
		return -EINVAL;
	}
	ret = of_property_read_u32_array(batt_id_node, "google,chg-cv-limits",
					 (u32 *)profile->volt_limits,
					 profile->volt_nb_limits);
	/* google,chg-cv-limits does not exist in the child_node */
	if (ret < 0)
		ret = of_property_read_u32_array(node, "google,chg-cv-limits",
						 (u32 *)profile->volt_limits,
						 profile->volt_nb_limits);
	if (ret < 0) {
		gbms_err(profile, "cannot read chg-cv-limits table, ret=%d\n",
			 ret);
		return ret;
	}

	memset(profile->topoff_limits, 0, sizeof(profile->topoff_limits));
	profile->topoff_nb_limits =
	    of_property_count_elems_of_size(node, "google,chg-topoff-limits",
					    sizeof(u32));
	if (profile->topoff_nb_limits > 0) {
		if (profile->topoff_nb_limits > GBMS_CHG_TOPOFF_NB_LIMITS_MAX) {
			gbms_err(profile, "chg-topoff-nb-limits exceeds driver max:%d\n",
			       GBMS_CHG_TOPOFF_NB_LIMITS_MAX);
			return -EINVAL;
		}
		ret = of_property_read_u32_array(node, "google,chg-topoff-limits",
						(u32 *)profile->topoff_limits,
						profile->topoff_nb_limits);
		if (ret < 0) {
			gbms_err(profile, "cannot read chg-topoff-limits table, ret=%d\n",
				 ret);
			return ret;
		}
		gbms_info(profile, "dynamic topoff enabled\n");
	}

	return 0;
}

int gbms_read_aacr_limits(struct gbms_chg_profile *profile,
			  const struct device_node *node)
{
	int ret = 0, cycle_nb_limits = 0, fade10_nb_limits = 0;

	if (!profile || !node)
		return -ENODEV;

	ret = of_property_count_elems_of_size(node,
					      "google,aacr-ref-cycles", sizeof(u32));
	if (ret < 0)
		goto no_data;

	cycle_nb_limits = ret;

	ret = of_property_count_elems_of_size(node,
					      "google,aacr-ref-fade10", sizeof(u32));
	if (ret < 0)
		goto no_data;

	fade10_nb_limits = ret;

	if (cycle_nb_limits != fade10_nb_limits ||
	    cycle_nb_limits > GBMS_AACR_DATA_MAX ||
	    cycle_nb_limits == 0) {
		gbms_warn(profile, "aacr not enabled, cycle_nb:%d, fade10_nb:%d, max:%d",
			  cycle_nb_limits, fade10_nb_limits, GBMS_AACR_DATA_MAX);
		profile->aacr_nb_limits = 0;
		return -ERANGE;
	}

	ret = of_property_read_u32_array(node, "google,aacr-ref-cycles",
					 (u32 *)profile->aacr_reference_cycles, cycle_nb_limits);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32_array(node, "google,aacr-ref-fade10",
					 (u32 *)profile->aacr_reference_fade10, fade10_nb_limits);
	if (ret < 0)
		return ret;

	profile->aacr_nb_limits = cycle_nb_limits;

	return 0;

no_data:
	profile->aacr_nb_limits = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(gbms_read_aacr_limits);

/* return the pct amount of capacity fade at cycles or negative if not enabled */
int gbms_aacr_fade10(const struct gbms_chg_profile *profile, int cycles)
{
	int cycle_s = 0, fade_s = 0;
	int idx, cycle_f, fade_f;

	if (profile->aacr_nb_limits == 0 || cycles < 0)
		return -EINVAL;

	for (idx = 0; idx < profile->aacr_nb_limits - 1; idx++)
		if (cycles < profile->aacr_reference_cycles[idx])
			break;

	/* Interpolation */
	cycle_f = profile->aacr_reference_cycles[idx];
	fade_f = profile->aacr_reference_fade10[idx];
	if (idx > 0) {
		cycle_s = profile->aacr_reference_cycles[idx - 1];
		fade_s = profile->aacr_reference_fade10[idx - 1];
	}

	return (cycles - cycle_s) * (fade_f - fade_s) / (cycle_f - cycle_s) + fade_s;
}
EXPORT_SYMBOL_GPL(gbms_aacr_fade10);

int gbms_read_aafv_limits(struct gbms_chg_profile *profile,
			  struct device_node *node)
{
	int ret = 0, cycle_nb_limits = 0, offset_nb_limits = 0;

	if (!profile || !node)
		return -ENODEV;

	ret = of_property_count_elems_of_size(node, "google,aafv-ref-cycles", sizeof(u32));
	if (ret < 0)
		goto no_data;

	cycle_nb_limits = ret;

	ret = of_property_count_elems_of_size(node, "google,aafv-ref-offset", sizeof(u32));
	if (ret < 0)
		goto no_data;

	offset_nb_limits = ret;

	if (cycle_nb_limits != offset_nb_limits ||
	    cycle_nb_limits > GBMS_AAFV_DATA_MAX ||
	    cycle_nb_limits == 0) {
		gbms_warn(profile, "aafv not enabled, cycle_nb:%d, offset_nb:%d, max:%d",
			  cycle_nb_limits, offset_nb_limits, GBMS_AAFV_DATA_MAX);
		profile->aafv_nb_limits = 0;
		return -ERANGE;
	}

	ret = of_property_read_u32_array(node, "google,aafv-ref-cycles",
					 (u32 *)profile->aafv_cycles, cycle_nb_limits);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32_array(node, "google,aafv-ref-offset",
					 (u32 *)profile->aafv_offsets, offset_nb_limits);
	if (ret < 0)
		return ret;

	profile->aafv_nb_limits = cycle_nb_limits;

	return 0;

no_data:
	profile->aafv_nb_limits = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(gbms_read_aafv_limits);

/*
 * for example:
 *     google,aafv-ref-cycles = <200 400 600 800 1000>;
 *     google,aafv-ref-offset = < 20  40  60  80  100>;
 *
 *   0 ~ 199 cycles: return 0 (mV)
 * 200 ~ 399 cycles: return 20 (mV)
 * 400 ~ 599 cycles: return 40 (mV)
 * 600 ~ 799 cycles: return 60 (mV)
 * 800 ~ 999 cycles: return 80 (mV)
 * 1000+     cycles: return 100 (mV)
 */
int gbms_aafv_get_offset(const struct gbms_chg_profile *profile, const int cycles)
{
	int idx, offset = 0;

	if (profile->aafv_nb_limits == 0 || cycles < 0)
		return 0;

	for (idx = 0; idx < profile->aafv_nb_limits; ++idx) {
		if (cycles >= profile->aafv_cycles[idx])
			offset = profile->aafv_offsets[idx];
		else
			break;
	}

	return offset;
}
EXPORT_SYMBOL_GPL(gbms_aafv_get_offset);

bool gbms_aafv_offset_is_valid(const struct gbms_chg_profile *profile,
			       const u32 offset, const u32 len)
{
	u32 last_fv, penultimate_fv, delta;

	/* The last updated fv cannot be less than the second to last fv */
	last_fv = profile->last_volt;
	penultimate_fv = (profile->volt_nb_limits > 1) ?
			profile->volt_limits[profile->volt_nb_limits - 2] : 0;
	delta = (last_fv - penultimate_fv) / 1000;

	return offset >= 0 && offset < delta && len > 0;
}
EXPORT_SYMBOL_GPL(gbms_aafv_offset_is_valid);

int gbms_aafv_get_last_entry(const struct gbms_chg_profile *profile)
{
	return profile->aafv_nb_limits > 1 ? profile->aafv_cycles[profile->aafv_nb_limits - 2] : 0;
}
EXPORT_SYMBOL_GPL(gbms_aafv_get_last_entry);

int gbms_read_aacc_weights(struct gbms_chg_profile *profile, struct device_node *node,
			   bool is_charge, enum aacc_mode mode)
{
	const int dir_idx = is_charge ? 0 : 1;
	const int mode_idx = (mode == AACC_MODE_TD) ? 1 : 0;
	struct aacc_weight_profile * const aacc_p_map[2][2] = {
		{&profile->aacc_cycles.chg, &profile->aacc_cycles.dsg},
		{&profile->aacc_cycles.td_chg, &profile->aacc_cycles.td_dsg},
	};
	const char * const prop_temp_map[2][2] = {
		{"google,aacc-chg-temp", "google,aacc-dsg-temp"},
		{"google,aacc-td-chg-temp", "google,aacc-td-dsg-temp"},
	};
	const char * const prop_weight_map[2][2] = {
		{"google,aacc-chg-weights", "google,aacc-dsg-weights"},
		{"google,aacc-td-chg-weights", "google,aacc-td-dsg-weights"},
	};
	const char * const err_tag_map[2][2] = {
		{"aacc-chg", "aacc-dsg"},
		{"aacc-td-chg", "aacc-td-dsg"},
	};
	struct aacc_weight_profile *aacc_p;
	const char *prop_temp, *prop_weight, *err_tag;
	u32 weight_size, mem_size;
	int ret;

	if (!profile || !node)
		return -ENODEV;

	/*
	 * select target fields and property names based on mode and direction
	 * dir_idx : 0 for charge, 1 for discharge
	 * mode_idx: 0 for AACC_MODE_DEFAULT, 1 for AACC_MODE_TD
	 */
	aacc_p = aacc_p_map[mode_idx][dir_idx];
	prop_temp = prop_temp_map[mode_idx][dir_idx];
	prop_weight = prop_weight_map[mode_idx][dir_idx];
	err_tag = err_tag_map[mode_idx][dir_idx];

	aacc_p->temp_nb_limits =
	    of_property_count_elems_of_size(node, prop_temp, sizeof(u32));
	if (aacc_p->temp_nb_limits <= 0) {
		ret = aacc_p->temp_nb_limits;
		gbms_err(profile, "cannot read %s-temp, ret=%d\n", err_tag, ret);
		return -EINVAL;
	}
	if (aacc_p->temp_nb_limits > GBMS_AACC_TEMP_NB_MAX) {
		gbms_err(profile, "%s-temp-nb-limits exceeds driver max: %d\n",
			 err_tag, GBMS_AACC_TEMP_NB_MAX);
		return -EINVAL;
	}
	ret = of_property_read_u32_array(node, prop_temp,
					 (u32 *)aacc_p->temp_limits,
					 aacc_p->temp_nb_limits);
	if (ret < 0) {
		gbms_err(profile, "cannot read %s-temp limits, ret=%d\n", err_tag, ret);
		return -EINVAL;
	}

	weight_size = (aacc_p->temp_nb_limits - 1) * GBMS_AACC_SOC_SIZE;
	mem_size = sizeof(s32) * weight_size;

	aacc_p->weight_limits = kzalloc(mem_size, GFP_KERNEL);
	if (!aacc_p->weight_limits)
		return -ENOMEM;

	ret = of_property_read_u32_array(node, prop_weight,
					 aacc_p->weight_limits,
					 weight_size);
	if (ret < 0) {
		gbms_err(profile, "cannot read %s-cc-limits table, ret=%d\n", err_tag, ret);
		kfree(aacc_p->weight_limits);
		aacc_p->weight_limits = 0;
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_read_aacc_weights);

int gbms_aacc_temp_idx(const struct gbms_chg_profile *profile, int temp, bool is_charge,
		       enum aacc_mode mode)
{
	const struct aacc_weight_profile *temp_data;
	int temp_idx = 0;

	if (is_charge)
		temp_data = (mode == AACC_MODE_TD) ?
			&profile->aacc_cycles.td_chg : &profile->aacc_cycles.chg;
	else
		temp_data = (mode == AACC_MODE_TD) ?
			&profile->aacc_cycles.td_dsg : &profile->aacc_cycles.dsg;

	/*
	 * needs to limit under table size after the last ++
	 * ex. temp_nb_limits=4 make 3 temp range from 0 to 2
	 * so we need to limit in temp_nb_limits - 2
	 */
	while (temp_idx < temp_data->temp_nb_limits - 2 &&
		temp >= temp_data->temp_limits[temp_idx + 1]) {
		temp_idx++;
	}

	return temp_idx;
}
EXPORT_SYMBOL_GPL(gbms_aacc_temp_idx);

int gbms_read_aacv_limits(struct gbms_chg_profile *profile,
			  struct device_node *node)
{
	int ret = 0, cycle_nb_limits = 0, offset_nb_limits = 0;

	if (!profile || !node)
		return -ENODEV;

	ret = of_property_count_elems_of_size(node, "google,aacv-ref-cycles", sizeof(u32));
	if (ret < 0)
		goto no_data;

	cycle_nb_limits = ret;

	ret = of_property_count_elems_of_size(node, "google,aacv-ref-offset", sizeof(u32));
	if (ret < 0)
		goto no_data;

	offset_nb_limits = ret;

	if (cycle_nb_limits != offset_nb_limits ||
	    cycle_nb_limits > GBMS_AACV_DATA_MAX ||
	    cycle_nb_limits == 0) {
		gbms_warn(profile, "aacv not supported, cycle_nb:%d, offset_nb:%d, max:%d",
			  cycle_nb_limits, offset_nb_limits, GBMS_AACV_DATA_MAX);
		profile->aacv_nb_limits = 0;
		return -ERANGE;
	}

	ret = of_property_read_u32_array(node, "google,aacv-ref-cycles",
					 (u32 *)profile->aacv_cycles, cycle_nb_limits);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32_array(node, "google,aacv-ref-offset",
					 (u32 *)profile->aacv_offsets, offset_nb_limits);
	if (ret < 0)
		return ret;

	profile->aacv_nb_limits = cycle_nb_limits;

	return 0;

no_data:
	profile->aacv_nb_limits = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(gbms_read_aacv_limits);

int gbms_aacv_get_offset(const struct gbms_chg_profile *profile, const int cycles)
{
	int idx, offset = 0;

	if (profile->aacv_nb_limits == 0 || cycles < 0)
		return 0;

	for (idx = 0; idx < profile->aacv_nb_limits; ++idx) {
		if (cycles >= profile->aacv_cycles[idx])
			offset = profile->aacv_offsets[idx];
		else
			break;
	}

	return offset;
}
EXPORT_SYMBOL_GPL(gbms_aacv_get_offset);

int gbms_init_chg_profile_internal(struct gbms_chg_profile *profile,
			  struct device_node *node,
			  const char *owner_name)
{
	int ret, vi;
	u32 cccm_array_size, mem_size;

	profile->owner_name = owner_name;

	ret = gbms_read_cccm_limits(profile, node);
	if (ret < 0)
		return ret;

	cccm_array_size = (profile->temp_nb_limits - 1)
			  * profile->volt_nb_limits;
	mem_size = sizeof(s32) * cccm_array_size;

	profile->cccm_limits = kzalloc(mem_size, GFP_KERNEL);
	if (!profile->cccm_limits)
		return -ENOMEM;

	/* load C rates into profile->cccm_limits */
	ret = of_property_read_u32_array(node, "google,chg-cc-limits",
					 profile->cccm_limits,
					 cccm_array_size);
	if (ret < 0) {
		gbms_err(profile, "cannot read chg-cc-limits table, ret=%d\n",
			 ret);
		kfree(profile->cccm_limits);
		profile->cccm_limits = 0;
		return -EINVAL;
	}

	/* for irdrop compensation in taper step */
	ret = of_property_read_u32(node, "google,fv-uv-resolution",
				   &profile->fv_uv_resolution);
	if (ret < 0)
		profile->fv_uv_resolution = GBMS_DEFAULT_FV_UV_RESOLUTION;

	/* FCC limit resolution */
	ret = of_property_read_u32(node, "google,chg-cc-ua-resolution",
				   &profile->cc_ua_resolution);
	if (ret < 0)
		profile->cc_ua_resolution = profile->fv_uv_resolution;

	/* how close to tier voltage is close enough */
	ret = of_property_read_u32(node, "google,cv-range-accuracy",
				   &profile->cv_range_accuracy);
	if (ret < 0)
		profile->cv_range_accuracy = profile->fv_uv_resolution / 2;

	/* IEEE1725, default to 1020, cap irdrop offset */
	ret = of_property_read_u32(node, "google,fv-uv-margin-dpct",
				   &profile->fv_uv_margin_dpct);
	if (ret < 0)
		profile->fv_uv_margin_dpct = GBMS_DEFAULT_FV_UV_MARGIN_DPCT;

	ret = of_property_read_u32(node, "google,fv-dc-ratio",
				   &profile->fv_dc_ratio);
	if (ret < 0)
		profile->fv_dc_ratio = GBMS_DEFAULT_FV_DC_RATIO;

	/* debounce tier switch */
	ret = of_property_read_u32(node, "google,cv-debounce-cnt",
				   &profile->cv_debounce_cnt);
	if (ret < 0)
		profile->cv_debounce_cnt = GBMS_DEFAULT_CV_DEBOUNCE_CNT;

	/* how fast to poll in taper */
	ret = of_property_read_u32(node, "google,cv-update-interval",
				   &profile->cv_update_interval);
	if (ret < 0)
		profile->cv_update_interval = GBMS_DEFAULT_CV_UPDATE_INTERVAL;

	/* tier over voltage penalty */
	ret = of_property_read_u32(node, "google,cv-tier-ov-cnt",
				   &profile->cv_tier_ov_cnt);
	if (ret < 0)
		profile->cv_tier_ov_cnt = GBMS_DEFAULT_CV_TIER_OV_CNT;

	/* how many samples under next tier to wait before switching */
	ret = of_property_read_u32(node, "google,cv-tier-switch-cnt",
				   &profile->cv_tier_switch_cnt);
	if (ret < 0)
		profile->cv_tier_switch_cnt = GBMS_DEFAULT_CV_TIER_SWITCH_CNT;

	/* allow being "a little" over tier voltage, experimental */
	ret = of_property_read_u32(node, "google,cv-otv-margin",
				   &profile->cv_otv_margin);
	if (ret < 0)
		profile->cv_otv_margin = GBMS_DEFAULT_CV_OTV_MARGIN;

	/* for particular hw stage */
	profile->enable_switch_chg_profile = of_property_read_bool(node,
					     "google,enable-switch-chg-profile");

	/* sanity on voltages (should warn?) */
	for (vi = 0; vi < profile->volt_nb_limits; vi++)
		profile->volt_limits[vi] = profile->volt_limits[vi] /
		    profile->fv_uv_resolution * profile->fv_uv_resolution;

	/* save the last vtier in last_volt */
	profile->last_volt = profile->volt_limits[profile->volt_nb_limits - 1];

	/* reset AACT */
	profile->aact_init_profile = false;
	profile->aact_update_profile = false;

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_init_chg_profile_internal);

void gbms_free_chg_profile(struct gbms_chg_profile *profile)
{
	kfree(profile->cccm_limits);
	profile->cccm_limits = 0;
}
EXPORT_SYMBOL_GPL(gbms_free_chg_profile);

int gbms_read_chg_aact_ecc(struct gbms_chg_profile *profile, struct device_node *node)
{
	struct device_node *batt_id_node __free(device_node) = NULL;
	int ret;

	batt_id_node = gbms_batt_id_node(node);

	profile->aact_support_multiple_profiles =
		of_property_read_bool(batt_id_node,
				      "google,aact-support-multiple-profiles");
	if (!profile->aact_support_multiple_profiles)
		profile->aact_support_multiple_profiles =
			of_property_read_bool(node, "google,aact-support-multiple-profiles");

	profile->aact_nb_limits =
	    of_property_count_elems_of_size(batt_id_node, "google,chg-aact-ecc",
					    sizeof(u32));
	if (profile->aact_nb_limits <= 0)
		profile->aact_nb_limits =
		    of_property_count_elems_of_size(node, "google,chg-aact-ecc",
						    sizeof(u32));
	if (profile->aact_nb_limits <= 0) {
		ret = profile->aact_nb_limits;
		gbms_err(profile, "cannot read chg-aact-ecc, ret=%d\n", ret);
		return -EINVAL;
	}
	if (profile->aact_nb_limits > GBMS_AACT_NB_LIMITS_MAX) {
		gbms_err(profile, "chg-aact-ecc exceeds driver max: %d\n",
			 GBMS_AACT_NB_LIMITS_MAX);
		return -EINVAL;
	}
	ret = of_property_read_u32_array(batt_id_node, "google,chg-aact-ecc",
					 (u32 *)profile->aact_limits,
					 profile->aact_nb_limits);
	if (ret < 0)
		ret = of_property_read_u32_array(node, "google,chg-aact-ecc",
						 (u32 *)profile->aact_limits,
						 profile->aact_nb_limits);
	if (ret < 0) {
		gbms_err(profile, "cannot read aact-cv-limits table, ret=%d\n", ret);
		return ret;
	}

	profile->aact_load_chg_ecc = true;
	return 0;
}
EXPORT_SYMBOL_GPL(gbms_read_chg_aact_ecc);

static int gbms_read_aact_cccm_limits(struct gbms_chg_profile *profile,
				      struct device_node *node)
{
	struct device_node *batt_id_node __free(device_node) = NULL;
	char *aact_temp_limits, *aact_cv_limits;
	int ret;

	if (!profile->aact_load_chg_ecc) {
		ret = gbms_read_chg_aact_ecc(profile, node);
		if (ret < 0)
			return ret;
	}

	batt_id_node = gbms_batt_id_node(node);

	aact_temp_limits = aact_all_limits.temp_limits[GBMS_AACT_IDX(profile)];
	aact_cv_limits = aact_all_limits.cv_limits[GBMS_AACT_IDX(profile)];

	profile->aact_temp_nb_limits =
	    of_property_count_elems_of_size(batt_id_node, aact_temp_limits,
					    sizeof(u32));
	if (profile->aact_temp_nb_limits <= 0)
		profile->aact_temp_nb_limits =
		    of_property_count_elems_of_size(node, aact_temp_limits,
						    sizeof(u32));
	if (profile->aact_temp_nb_limits <= 0) {
		ret = profile->aact_temp_nb_limits;
		gbms_err(profile, "cannot read aact-temp-limits, ret=%d\n", ret);
		return -EINVAL;
	}
	if (profile->aact_temp_nb_limits > GBMS_CHG_TEMP_NB_LIMITS_MAX) {
		gbms_err(profile, "aact-temp-nb-limits exceeds driver max: %d\n",
			 GBMS_CHG_TEMP_NB_LIMITS_MAX);
		return -EINVAL;
	}
	ret = of_property_read_u32_array(batt_id_node, aact_temp_limits,
					 (u32 *)profile->aact_temp_limits,
					 profile->aact_temp_nb_limits);
	if (ret < 0)
		ret = of_property_read_u32_array(node, aact_temp_limits,
						 (u32 *)profile->aact_temp_limits,
						 profile->aact_temp_nb_limits);
	if (ret < 0) {
		gbms_err(profile, "cannot read aact-temp-limits table, ret=%d\n", ret);
		return ret;
	}

	profile->aact_volt_nb_limits =
	    of_property_count_elems_of_size(batt_id_node, aact_cv_limits,
					    sizeof(u32));
	if (profile->aact_volt_nb_limits <= 0)
		profile->aact_volt_nb_limits =
		    of_property_count_elems_of_size(node, aact_cv_limits,
						    sizeof(u32));
	if (profile->aact_volt_nb_limits <= 0) {
		ret = profile->aact_volt_nb_limits;
		gbms_err(profile, "cannot read aact-cv-limits, ret=%d\n", ret);
		return -EINVAL;
	}
	if (profile->aact_volt_nb_limits > GBMS_CHG_VOLT_NB_LIMITS_MAX) {
		gbms_err(profile, "aact-cv-nb-limits exceeds driver max: %d\n",
			 GBMS_CHG_VOLT_NB_LIMITS_MAX);
		return -EINVAL;
	}
	ret = of_property_read_u32_array(batt_id_node, aact_cv_limits,
					 (u32 *)profile->aact_volt_limits,
					 profile->aact_volt_nb_limits);
	if (ret < 0)
		ret = of_property_read_u32_array(node, aact_cv_limits,
						 (u32 *)profile->aact_volt_limits,
						 profile->aact_volt_nb_limits);
	if (ret < 0) {
		gbms_err(profile, "cannot read aact-cv-limits table, ret=%d\n", ret);
		return ret;
	}

	return 0;
}

int gbms_init_aact_profile_internal(struct gbms_chg_profile *profile,
				    struct device_node *node,
				    const char *owner_name)
{
	int ret;
	u32 cccm_array_size, mem_size;
	char *aact_cc_limits;
	struct device_node *batt_id_node __free(device_node) = NULL;

	profile->owner_name = owner_name;

	/* Don't reinit aact_profile that has been updated in aact_profile_store */
	if (profile->aact_update_profile)
		return 0;

	ret = gbms_read_aact_cccm_limits(profile, node);
	if (ret < 0)
		return ret;

	cccm_array_size = (profile->aact_temp_nb_limits - 1)
			  * profile->aact_volt_nb_limits
			  * GBMS_AACT_NB_LIMITS(profile);
	mem_size = sizeof(s32) * cccm_array_size;

	profile->aact_cccm_limits = kzalloc(mem_size, GFP_KERNEL);
	if (!profile->aact_cccm_limits)
		return -ENOMEM;

	batt_id_node = gbms_batt_id_node(node);

	/* load C rates into profile->aact_cccm_limits */
	aact_cc_limits = aact_all_limits.cc_limits[GBMS_AACT_IDX(profile)];
	ret = of_property_read_u32_array(batt_id_node, aact_cc_limits,
					 profile->aact_cccm_limits,
					 cccm_array_size);
	if (ret < 0)
		ret = of_property_read_u32_array(node, aact_cc_limits,
						 profile->aact_cccm_limits,
						 cccm_array_size);
	if (ret < 0) {
		gbms_err(profile, "cannot read aact-cc-limits table, ret=%d\n", ret);
		kfree(profile->cccm_limits);
		profile->cccm_limits = 0;
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_init_aact_profile_internal);

int gbms_update_chg_profile_from_aact(struct gbms_chg_profile *profile)
{
	u32 cccm_array_size, mem_size;
	u32 temp_size = sizeof(s32) * profile->aact_temp_nb_limits;
	u32 volt_size = sizeof(s32) * profile->aact_volt_nb_limits;

	cccm_array_size = (profile->aact_temp_nb_limits - 1)
			  * profile->aact_volt_nb_limits
			  * GBMS_AACT_NB_LIMITS(profile);
	mem_size = sizeof(s32) * cccm_array_size;

	profile->cccm_limits = kzalloc(mem_size, GFP_KERNEL);
	if (!profile->cccm_limits)
		return -ENOMEM;

	/* copy aact_profile back to charge_profile */
	memcpy(profile->cccm_limits, profile->aact_cccm_limits, mem_size);
	memcpy(profile->temp_limits, profile->aact_temp_limits, temp_size);
	memcpy(profile->volt_limits, profile->aact_volt_limits, volt_size);
	profile->temp_nb_limits = profile->aact_temp_nb_limits;
	profile->volt_nb_limits = profile->aact_volt_nb_limits;
	/* save the last vtier in last_volt */
	profile->last_volt = profile->volt_limits[profile->volt_nb_limits - 1];
	profile->aact_init_profile = true;

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_update_chg_profile_from_aact);

int gbms_aact_get_index(const struct gbms_chg_profile *profile, const int cycles)
{
	int idx = 0;

	/* ecc list has not been loaded or updated yet */
	if (!profile->aact_load_chg_ecc && !profile->aact_update_profile)
		return 0;

	while (idx <= profile->aact_nb_limits - 1 && cycles >= profile->aact_limits[idx])
		idx++;

	return idx - 1;
}
EXPORT_SYMBOL_GPL(gbms_aact_get_index);

/* NOTE: I should really pass the scale */
void gbms_dump_raw_profile(char *buff, size_t len, const struct gbms_chg_profile *profile, int scale)
{
	const int tscale = (scale == 1) ? 1 : 10;
	int ti, vi, count = 0;

	count += scnprintf(buff + count, len - count, "Profile constant charge limits:\n");
	count += scnprintf(buff + count, len - count, "|T \\ V");
	for (vi = 0; vi < profile->volt_nb_limits; vi++) {
		count += scnprintf(buff + count, len - count, "  %4d",
				   profile->volt_limits[vi] / scale);
	}
	count += scnprintf(buff + count, len - count, "\n");

	for (ti = 0; ti < profile->temp_nb_limits - 1; ti++) {
		count += scnprintf(buff + count, len - count, "|%2d:%2d",
				   profile->temp_limits[ti] / tscale,
				   profile->temp_limits[ti + 1] / tscale);
		for (vi = 0; vi < profile->volt_nb_limits; vi++) {
			count += scnprintf(buff + count, len - count, "  %4d",
					   GBMS_CCCM_LIMITS(profile, ti, vi)
					   / scale);
		}
		count += scnprintf(buff + count, len - count, "\n");
	}
}
EXPORT_SYMBOL_GPL(gbms_dump_raw_profile);

/*
 * When charging in DC, the fv_max will be (FV + cc_max * fv_dc_ratio).
 * DC can be detected by cc_ua non-zero.
 * The gap between Vchg and Vbat is caused by the ibat and the impedance.
 * The fv_dc_ratio is used to simulate the impedance to get the proper fv, so
 * the vbat will not over the otv threshold.
 */
int gbms_msc_round_fv_uv(const struct gbms_chg_profile *profile,
			   int vtier, int fv_uv, int cc_ua, bool allow_higher_fv)
{
	int result;
	const unsigned int fv_uv_max = (vtier / 1000) * profile->fv_uv_margin_dpct;
	const unsigned int dc_fv_uv_max = vtier + (cc_ua / 1000) * profile->fv_dc_ratio;
	const unsigned int last_fv = profile->volt_limits[profile->volt_nb_limits - 1];
	unsigned int fv_max;

	if (cc_ua == 0)
		fv_max = fv_uv_max;
	else if (!allow_higher_fv && dc_fv_uv_max >= last_fv)
		fv_max = last_fv - profile->fv_uv_resolution;
	else
		fv_max = dc_fv_uv_max;

	if (fv_max != 0 && fv_uv > fv_max)
		fv_uv = fv_max;

	fv_uv += profile->fv_uv_resolution / 2;
	result = fv_uv - (fv_uv % profile->fv_uv_resolution);

	if (fv_max != 0)
		gbms_info(profile, "MSC_ROUND: fv_uv=%d vtier=%d dc_fv_uv_max=%d fv_max=%d -> %d\n",
			  fv_uv, vtier, dc_fv_uv_max, fv_max, result);

	return result;
}
EXPORT_SYMBOL_GPL(gbms_msc_round_fv_uv);

/* Find last voltage tier with non zero current */
int gbms_msc_get_last_voltage_idx(const struct gbms_chg_profile *profile, const int temp_idx)
{
	int vbatt_idx = profile->volt_nb_limits - 1;

	while(vbatt_idx > 0 && !GBMS_CCCM_LIMITS(profile, temp_idx, vbatt_idx))
		vbatt_idx--;

	return vbatt_idx;
}
EXPORT_SYMBOL_GPL(gbms_msc_get_last_voltage_idx);

/* skip tiers that have same c-rate */
int gbms_msc_voltage_idx_merge_tiers(const struct gbms_chg_profile *profile,
			  int vbatt_idx, int temp_idx)
{
	int cc_max;

	if (!profile)
		return 0;

	if (temp_idx < 0 || temp_idx >= profile->temp_nb_limits)
		return vbatt_idx;

	cc_max = GBMS_CCCM_LIMITS(profile, temp_idx, vbatt_idx);
	while (vbatt_idx < profile->volt_nb_limits - 2 &&
		cc_max == GBMS_CCCM_LIMITS(profile, temp_idx, vbatt_idx + 1))
			vbatt_idx++;

	return vbatt_idx;
}
EXPORT_SYMBOL_GPL(gbms_msc_voltage_idx_merge_tiers);

/* charge profile idx based on the battery temperature
 * TODO: return -1 when temperature is lower than profile->temp_limits[0] or
 * higher than profile->temp_limits[profile->temp_nb_limits - 1]
 */
int gbms_msc_temp_idx(const struct gbms_chg_profile *profile, int temp)
{
	int temp_idx = 0;

	/*
	 * needs to limit under table size after the last ++
	 * ex. temp_nb_limits=7 make 6 temp range from 0 to 5
	 * so we need to limit in temp_nb_limits - 2
	 */
	while (temp_idx < profile->temp_nb_limits - 2 &&
	       temp >= profile->temp_limits[temp_idx + 1])
		temp_idx++;

	return temp_idx;
}
EXPORT_SYMBOL_GPL(gbms_msc_temp_idx);

/* Compute the step index given the battery voltage
 * When selecting an index need to make sure that headroom for the tier voltage
 * will allow to send to the battery _at least_ next tier max FCC current and
 * well over charge termination current.
 */
int gbms_msc_voltage_idx(const struct gbms_chg_profile *profile, int vbatt)
{
	int vbatt_idx = 0;

	while (vbatt_idx < profile->volt_nb_limits - 1 &&
	       vbatt > profile->volt_limits[vbatt_idx])
		vbatt_idx++;

	/* assumes that 3 times the hardware resolution is ok
	 * TODO: make it configurable? tune?
	 */
	if (vbatt_idx != profile->volt_nb_limits - 1) {
		const int vt = profile->volt_limits[vbatt_idx];
		const int headr = profile->fv_uv_resolution * 3;

		if ((vt - vbatt) < headr)
			vbatt_idx += 1;
	}

	return vbatt_idx;
}
EXPORT_SYMBOL_GPL(gbms_msc_voltage_idx);

uint8_t gbms_gen_chg_flags(int chg_status, int chg_type)
{
	uint8_t flags = 0;

	if (chg_status != POWER_SUPPLY_STATUS_DISCHARGING) {
		flags |= GBMS_CS_FLAG_BUCK_EN;

		/* FULL makes sense only when charging is enabled */
		if (chg_status == POWER_SUPPLY_STATUS_FULL)
			flags |= GBMS_CS_FLAG_DONE;
	}
	if (chg_type == POWER_SUPPLY_CHARGE_TYPE_FAST)
		flags |= GBMS_CS_FLAG_CC;
	if (chg_type == POWER_SUPPLY_CHARGE_TYPE_TAPER_EXT)
		flags |= GBMS_CS_FLAG_CV;

	return flags;
}
EXPORT_SYMBOL_GPL(gbms_gen_chg_flags);

static int gbms_gen_state(union gbms_charger_state *chg_state,
			  struct power_supply *chg_psy)
{
	int vchrg, chg_type, chg_status, ioerr;

	/* TODO: if (chg_drv->chg_mode == CHG_DRV_MODE_NOIRDROP) vchrg = 0; */
	/* Battery needs to know charger voltage and state to run the irdrop
	 * compensation code, can disable here sending a 0 vchgr
	 */
	vchrg = GPSY_GET_PROP(chg_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW);
	chg_type = GPSY_GET_PROP(chg_psy, POWER_SUPPLY_PROP_CHARGE_TYPE);
	chg_status = GPSY_GET_INT_PROP(chg_psy, POWER_SUPPLY_PROP_STATUS,
						&ioerr);
	if (vchrg < 0 || chg_type < 0 || ioerr < 0) {
		pr_err("MSC_CHG error vchrg=%d chg_type=%d chg_status=%d\n",
			vchrg, chg_type, chg_status);
		return -EINVAL;
	}

	chg_state->f.chg_status = chg_status;
	chg_state->f.chg_type = chg_type;
	chg_state->f.flags = gbms_gen_chg_flags(chg_state->f.chg_status,
						chg_state->f.chg_type);
	chg_state->f.vchrg = vchrg / 1000; /* vchrg is in uA, f.vchrg us mA */

	return 0;
}

/* read or generate charge state */
int gbms_read_charger_state(union gbms_charger_state *chg_state,
			    struct power_supply *chg_psy)
{
	int64_t val;
	int ret;

	val = GPSY_GET_INT64_PROP(chg_psy, GBMS_PROP_CHARGE_CHARGER_STATE,
				  &ret);
	if (ret == 0) {
		chg_state->v = val;
	} else if (ret == -EAGAIN) {
		return ret;
	} else {
		int ichg;

		ret = gbms_gen_state(chg_state, chg_psy);
		if (ret < 0)
			return ret;

		ichg = GPSY_GET_PROP(chg_psy, POWER_SUPPLY_PROP_CURRENT_NOW);
		if (ichg > 0)
			chg_state->f.icl = ichg / 1000;

		pr_info("MSC_CHG chg_state=%lx [0x%x:%d:%d:%d] ichg=%d\n",
				(unsigned long)chg_state->v,
				chg_state->f.flags,
				chg_state->f.chg_type,
				chg_state->f.chg_status,
				chg_state->f.vchrg,
				ichg);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_read_charger_state);

/* ------------------------------------------------------------------------- */

/* convert cycle counts array to string */
int gbms_cycle_count_cstr_bc(char *buf, size_t size,
			     const u16 *ccount, int bcnt)
{
	int len = 0, i;

	for (i = 0; i < bcnt; i++)
		len += scnprintf(buf + len, size - len, "%d ", ccount[i]);
	buf[len - 1] = '\n';

	return len;
}
EXPORT_SYMBOL_GPL(gbms_cycle_count_cstr_bc);

/* parse the result of gbms_cycle_count_cstr_bc() back to array */
int gbms_cycle_count_sscan_bc(u16 *ccount, int bcnt, const char *buff)
{
	int i, val[10];

	/* sscanf has 10 fixed conversions */
	if (bcnt != 10)
		return -ERANGE;

	if (sscanf(buff, "%d %d %d %d %d %d %d %d %d %d",
			&val[0], &val[1], &val[2], &val[3], &val[4],
			&val[5], &val[6], &val[7], &val[8], &val[9])
			!= bcnt)
		return -EINVAL;

	for (i = 0; i < bcnt ; i++)
		if (val[i] >= 0 && val[i] < U16_MAX)
			ccount[i] = val[i];

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_cycle_count_sscan_bc);

/* ------------------------------------------------------------------------- */

#define gbms_desc_from_psy(psy) \
	container_of(psy->desc, struct gbms_desc, psy_dsc)

int gbms_set_property(struct power_supply *psy, enum gbms_property psp,
		      const union gbms_propval *val)
{
	struct gbms_desc *dsc;
	int ret;

	if (!psy)
		return -EINVAL;

	dsc = gbms_desc_from_psy(psy);
	if (dsc->set_property) {
		const bool writable = (dsc->property_is_writeable) ?
				      dsc->property_is_writeable(psy, psp) :
				      false;
		if (writable) {
			ret = dsc->set_property(psy, psp, val);
			if (ret != -ENODATA)
				return ret;
		} else {
			pr_debug("psp=%d for '%s' is not writeable\n",
				 psp, psy->desc->name);
		}
	}

	if (!dsc->forward)
		return -ENODEV;

	pr_debug("set %d for '%s' to %d\n", psp, psy->desc->name,
		 val->prop.intval);

	ret = power_supply_set_property(psy, psp, &val->prop);
	if (ret < 0) {
		pr_err("failed to psp=%d for '%s', ret=%d\n",
		       psp, psy->desc->name, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gbms_set_property);

int gbms_get_property(struct power_supply *psy, enum gbms_property psp,
		      union gbms_propval *val)
{
	struct gbms_desc *dsc;
	int ret;

	if (!psy)
		return -EINVAL;

	dsc = gbms_desc_from_psy(psy);
	if (dsc->get_property) {
		ret = dsc->get_property(psy, psp, val);
		/*
		 * -EAGAIN is a valid error (e.g. driver not ready yet) and
		 * must not be overwritten by the generic fallback below.
		 */
		if (ret == 0 || ret == -EAGAIN)
			return ret;
	}

	if (!dsc->forward)
		return -ENODEV;

	ret = power_supply_get_property(psy, psp, &val->prop);
	if (ret < 0)
		pr_err("failed to get psp=%d from '%s', ret=%d\n",
		       psp, psy->desc->name, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(gbms_get_property);

void gbms_logbuffer_prlog(struct logbuffer *log, int level, int debug_no_logbuffer,
			  int debug_printk_prlog, const char *f, ...)
{
	va_list args;

	va_start(args, f);

	if (!debug_no_logbuffer)
		logbuffer_vlog(log, f, args);

	if (level <= debug_printk_prlog)
		vprintk(f, args);

	va_end(args);
}
EXPORT_SYMBOL_GPL(gbms_logbuffer_prlog);

void gbms_logbuffer_devlog(struct logbuffer *log, struct device *dev, int level, int debug_no_logbuffer,
			  int debug_printk_prlog, const char *f, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, f);

	vaf.fmt = f;
	vaf.va = &args;

	if (!debug_no_logbuffer)
		logbuffer_vlog(log, f, args);

	if (level <= debug_printk_prlog)
		dev_printk_emit(level, dev, "%s %s: %pV", dev_driver_string(dev), dev_name(dev), &vaf);

	va_end(args);
}
EXPORT_SYMBOL_GPL(gbms_logbuffer_devlog);

bool chg_state_is_disconnected(const union gbms_charger_state *chg_state)
{
	return (((chg_state->f.flags & GBMS_CS_FLAG_BUCK_EN) == 0) &&
	       (chg_state->f.chg_status == POWER_SUPPLY_STATUS_DISCHARGING ||
	       chg_state->f.chg_status == POWER_SUPPLY_STATUS_UNKNOWN)) ||
	       (chg_state->f.flags & GBMS_CS_FLAG_INPUT_SUSPEND);
}
EXPORT_SYMBOL_GPL(chg_state_is_disconnected);

/* Tier stats common routines */
void gbms_tier_stats_init(struct gbms_ce_tier_stats *stats, int8_t idx)
{
	stats->vtier_idx = idx;
	stats->temp_idx = -1;
	stats->soc_in = -1;
	stats->temp_min = GBMS_TIER_TEMP_MIN_DEFAULT;
	stats->temp_max = GBMS_TIER_TEMP_MAX_DEFAULT;
	stats->last_update_sec = -1;
	stats->reentry_count = 0;
	memset(stats->soc_in_repeated, 0, sizeof(stats->soc_in_repeated));
}
EXPORT_SYMBOL_GPL(gbms_tier_stats_init);

/* call holding stats_lock */
void gbms_chg_stats_tier(struct gbms_ce_tier_stats *tier,
				int msc_state,
				ktime_t elap)
{
	if (msc_state < 0 || msc_state >= MSC_STATES_COUNT)
		return;

	tier->msc_cnt[msc_state] += 1;
	tier->msc_elap[msc_state] += elap;
}
EXPORT_SYMBOL_GPL(gbms_chg_stats_tier);

void gbms_stats_update_tier(u32 now, int temp_idx, int ibatt_ma, int temp, ktime_t elap,
			     int cc, union gbms_charger_state *chg_state,
			     enum gbms_msc_states_t msc_state, int soc_in,
			     int vin_mv, int iin_ma, int vbatt_mv,
			     struct gbms_ce_tier_stats *tier)
{
	const uint16_t icl_settled = chg_state->f.icl;

	/*
	 * book time to previous msc_state for this tier, there is an
	 * interesting wrinkle here since some tiers (health, full, etc)
	 * might be entered and exited multiple times.
	 */
	gbms_chg_stats_tier(tier, msc_state, elap);
	tier->sample_count += 1;

	if (tier->soc_in == -1) {
		tier->temp_idx = temp_idx;

		tier->temp_in = temp;
		tier->temp_min = temp;
		tier->temp_max = temp;

		tier->ibatt_min = ibatt_ma;
		tier->ibatt_max = ibatt_ma;

		tier->icl_min = icl_settled;
		tier->icl_max = icl_settled;

		tier->vin_min = vin_mv;
		tier->vin_max = vin_mv;

		tier->iin_min = iin_ma;
		tier->iin_max = iin_ma;

		tier->vbatt_min = vbatt_mv;
		tier->vbatt_max = vbatt_mv;

		tier->soc_in = soc_in;
		tier->cc_in = cc;
		tier->cc_total = 0;
		tier->last_update_sec = now;
		return;
	}

	/* Detect Re-entry */
	if (tier->last_update_sec < now - elap) {
		if (tier->reentry_count < MAX_VTIER_REENTRIES) {
			tier->soc_in_repeated[tier->reentry_count] = soc_in;
			tier->reentry_count++;
		}
	}
	tier->last_update_sec = now;

	/* crossed temperature tier */
	if (temp_idx != tier->temp_idx)
		tier->temp_idx = -1;

	if (chg_state->f.chg_type == POWER_SUPPLY_CHARGE_TYPE_FAST) {
		tier->time_fast += elap;
	} else if (chg_state->f.chg_type == POWER_SUPPLY_CHARGE_TYPE_TAPER_EXT) {
		tier->time_taper += elap;
	} else {
		tier->time_other += elap;
	}

	/*
	 * averages: temp < 100. icl_settled < 3000, sum(ibatt)
	 * is bound to battery capacity, elap in seconds, sums
	 * are stored in an s64. For icl_settled I need a tier
	 * to last for more than ~97M years.
	 */
	if (temp < tier->temp_min)
		tier->temp_min = temp;
	if (temp > tier->temp_max)
		tier->temp_max = temp;
	tier->temp_sum += temp * elap;

	if (icl_settled < tier->icl_min)
		tier->icl_min = icl_settled;
	if (icl_settled > tier->icl_max)
		tier->icl_max = icl_settled;
	tier->icl_sum += icl_settled * elap;

	if (ibatt_ma < tier->ibatt_min)
		tier->ibatt_min = ibatt_ma;
	if (ibatt_ma > tier->ibatt_max)
		tier->ibatt_max = ibatt_ma;
	tier->ibatt_sum += ibatt_ma * elap;

	if (vin_mv < tier->vin_min)
		tier->vin_min = vin_mv;
	if (vin_mv > tier->vin_max)
		tier->vin_max = vin_mv;
	tier->vin_sum += vin_mv * elap;

	if (iin_ma < tier->iin_min)
		tier->iin_min = iin_ma;
	if (iin_ma > tier->iin_max)
		tier->iin_max = iin_ma;
	tier->iin_sum += iin_ma * elap;

	if (vbatt_mv < tier->vbatt_min)
		tier->vbatt_min = vbatt_mv;
	if (vbatt_mv > tier->vbatt_max)
		tier->vbatt_max = vbatt_mv;
	tier->vbatt_sum += vbatt_mv * elap;

	tier->cc_total = cc - tier->cc_in;
}
EXPORT_SYMBOL_GPL(gbms_stats_update_tier);

/* Log only when elap != 0 */
int gbms_tier_stats_cstr(char *buff, int size,
			 const struct gbms_ce_tier_stats *tier_stat,
			 bool verbose)
{
	const qnum_t q_soc = qnum_from_q8_8(tier_stat->soc_in);
	const long elap = tier_stat->time_fast + tier_stat->time_taper +
			  tier_stat->time_other;

	long temp_avg, ibatt_avg, icl_avg;
	long vin_avg, iin_avg, vbatt_avg;
	int j, len = 0;

	if (elap) {
		temp_avg = div_s64(tier_stat->temp_sum, elap);
		ibatt_avg = div_s64(tier_stat->ibatt_sum, elap);
		icl_avg = div_s64(tier_stat->icl_sum, elap);
		vin_avg = div_s64(tier_stat->vin_sum, elap);
		iin_avg = div_s64(tier_stat->iin_sum, elap);
		vbatt_avg = div_s64(tier_stat->vbatt_sum, elap);
	} else {
		temp_avg = 0;
		ibatt_avg = 0;
		icl_avg = 0;
		vin_avg = 0;
		iin_avg = 0;
		vbatt_avg = 0;
	}

	len += scnprintf(&buff[len], size - len, "\n%d%c ",
		tier_stat->vtier_idx,
		(verbose) ? ':' : ',');

	char re_str[128] = "";
	int re_len = 0;

	re_len += scnprintf(re_str + re_len, sizeof(re_str) - re_len,
			    "%d.%02d", qnum_toint(q_soc), qnum_fracdgt(q_soc));

	for (j = 0; j < tier_stat->reentry_count; j++) {
		const qnum_t q_re_soc = qnum_from_q8_8(tier_stat->soc_in_repeated[j]);

		re_len += scnprintf(re_str + re_len, sizeof(re_str) - re_len,
				    ",%d.%02d", qnum_toint(q_re_soc), qnum_fracdgt(q_re_soc));
	}

	len += scnprintf(&buff[len], size - len,
		"%d.%02d,%d,%d, %d,%d,%d, %d,%ld,%d, %d,%ld,%d, %d,%ld,%d, %d,%ld,%d, %d,%ld,%d, %d,%ld,%d, %s ",
		qnum_toint(q_soc),
		qnum_fracdgt(q_soc),
		tier_stat->cc_in,
		tier_stat->temp_in,
		tier_stat->time_fast,
		tier_stat->time_taper,
		tier_stat->time_other,
		tier_stat->temp_min,
		temp_avg,
		tier_stat->temp_max,
		tier_stat->ibatt_min,
		ibatt_avg,
		tier_stat->ibatt_max,
		tier_stat->icl_min,
		icl_avg,
		tier_stat->icl_max,
		tier_stat->vin_min,
		vin_avg,
		tier_stat->vin_max,
		tier_stat->iin_min,
		iin_avg,
		tier_stat->iin_max,
		tier_stat->vbatt_min,
		vbatt_avg,
		tier_stat->vbatt_max,
		re_str);

	if (!verbose || !elap)
		return len;

	/* time spent in every multi step charging state */
	len += scnprintf(&buff[len], size - len, "\n%d:",
			tier_stat->vtier_idx);

	for (j = 0; j < MSC_STATES_COUNT; j++)
		len += scnprintf(&buff[len], size - len, " %s=%d",
			gbms_get_code(j), tier_stat->msc_elap[j]);

	/* count spent in each step charging state */
	len += scnprintf(&buff[len], size - len, "\n%d:",
			tier_stat->vtier_idx);

	for (j = 0; j < MSC_STATES_COUNT; j++)
		len += scnprintf(&buff[len], size - len, " %s=%d",
			gbms_get_code(j), tier_stat->msc_cnt[j]);

	return len;
}
EXPORT_SYMBOL_GPL(gbms_tier_stats_cstr);

void gbms_log_cstr_handler(struct logbuffer *log, char *buf, int len)
{
	int i, j = 0;
	char tmp[LOG_BUFFER_ENTRY_SIZE];

	buf[len] = '\n';
	for (i = 0; i <= len; i++) {
		if (buf[i] == '\n') {
			tmp[j] = '\0';
			/* skip first blank line */
			if (i != 0)
				logbuffer_log(log, "%s", tmp);
			j = 0;
		} else if (j >= LOG_BUFFER_ENTRY_SIZE - 1) {
			tmp[j] = '\0';
			logbuffer_log(log, "%s", tmp);
			i--;
			j = 0;
		} else {
			tmp[j] = buf[i];
			j++;
		}
	}
}
EXPORT_SYMBOL_GPL(gbms_log_cstr_handler);

int gbms_decode_eeprom_sn(char *decode_sn, const size_t max_len)
{
	const int pack_barcode_len = 23; // 0x0 - 0x16
	char sn[GBMS_MINF_LEN + 1] = { 0 };
	u16 tmp, pcb_sn;
	int ret, count, date;

	ret = gbms_storage_read(GBMS_TAG_MINF, sn, GBMS_MINF_LEN);
	if (ret < 0)
		return ret;

	/* Pack barcode area data in ASCII format */
	memcpy(decode_sn, sn, pack_barcode_len);

	/*
	 * decode address
	 * sn[23] - cell type
	 * sn[24] - Stage
	 * sn[25-26] - SMT date
	 * sn[27-28] - PCB serial number
	 * sn[29] - PCB vendor (ASCII format)
	 */
	count = pack_barcode_len;
	pcb_sn = (sn[27] << 8) | sn[28];
	tmp = (sn[25] << 8) | sn[26];
	date = ((((tmp >> 9) & 0x3f) + 1980) * 10000) + ((tmp >> 5) & 0xf) * 100 + (tmp & 0x1F);
	count += scnprintf(decode_sn + count, max_len, "%02d%02d%d%04X%c",
			   sn[23], sn[24], date, pcb_sn, sn[29]);
	decode_sn[count] = '\0';

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_decode_eeprom_sn);

int gbms_wcin_mains_prop_is_writeable(struct power_supply *psy, enum gbms_property psp)
{
	switch (psp) {
	case GBMS_PROP_CHARGING_ENABLED:
	case GBMS_PROP_CHARGE_DISABLE:
	case GBMS_PROP_ENABLE_SWITCH_CAP:
	case GBMS_PROP_WLC_LOAD_DECREASE:
	case GBMS_PROP_TA_MAX_VOLTAGE:
	case GBMS_PROP_MPP_DPLOSS_CALIBRATION_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_wcin_mains_prop_is_writeable);

int google_wcin_mains_prop_is_writeable(struct power_supply *psy, enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(google_wcin_mains_prop_is_writeable);
