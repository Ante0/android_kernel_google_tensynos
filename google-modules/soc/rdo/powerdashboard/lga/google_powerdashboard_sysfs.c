// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */
#include "../google_powerdashboard_iface.h"
#include "../google_powerdashboard_helper.h"
#include "google_powerdashboard_impl.h"

#include "soc/google/google_gtc.h"

#define DEFINE_PWRBLK_NODE(a, b, c) SYSFS_PWRBLK_NODE(PD_PWRBLK_ID(a), b, c);
#define DEFINE_LPCM_NODE(a, b) SYSFS_LPCM_NODE(PD_LPCM_ID(a), b, PWRBLK_SINGLE_LPCM);
#define DEFINE_PWRBLK_ATTR(a, b, c) static DEVICE_ATTR_RO(pwrblk_##b);
#define DEFINE_LPCM_ATTR(a, b) static DEVICE_ATTR_RO(lpcm_##b);

LGA_PWRBLK_X_MACRO_TABLE(DEFINE_PWRBLK_NODE)
LGA_LPCM_X_MACRO_TABLE(DEFINE_LPCM_NODE)

LGA_PWRBLK_X_MACRO_TABLE(DEFINE_PWRBLK_ATTR)
LGA_LPCM_X_MACRO_TABLE(DEFINE_LPCM_ATTR)

#define ASSIGN_PWRBLK_ATTR(a, b, c) (&dev_attr_pwrblk_##b.attr),
#define ASSIGN_LPCM_ATTR(a, b) (&dev_attr_lpcm_##b.attr),

struct attribute *google_powerdashboard_sswrp_attrs[] = {
	LGA_PWRBLK_X_MACRO_TABLE(ASSIGN_PWRBLK_ATTR)
	LGA_LPCM_X_MACRO_TABLE(ASSIGN_LPCM_ATTR)
	NULL,
};

POWER_STATE_BLOCKERS_NODE(dormant_suspend, SOC_POWER_STATE_DORMANT_SUSPEND);
POWER_STATE_BLOCKERS_NODE(ambient_sec_acc, SOC_POWER_STATE_AMBIENT_SEC_ACC);
POWER_STATE_BLOCKERS_NODE(ambient_suspend, SOC_POWER_STATE_AMBIENT_SUSPEND);

FABRIC_ACG_APG_RES_NODE(fabmed_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabrt_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabstby_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabsyss_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabhbw_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabmem_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(gslc01_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(gslc23_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(gslc01_acc_val_apg);
FABRIC_ACG_APG_RES_NODE(gslc23_acc_val_apg);

GMC_ACG_APG_RES_NODE(gmc_acc_val_acg);
GMC_ACG_APG_RES_NODE(gmc_acc_val_apg);

static DEVICE_ATTR_RO(blockers_dormant_suspend);
static DEVICE_ATTR_RO(blockers_ambient_suspend);
static DEVICE_ATTR_RO(blockers_ambient_sec_acc);
static DEVICE_ATTR_RO(csr_fabmed_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabrt_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabstby_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabsyss_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabhbw_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabmem_acc_val_acg);
static DEVICE_ATTR_RO(csr_gslc01_acc_val_acg);
static DEVICE_ATTR_RO(csr_gslc23_acc_val_acg);
static DEVICE_ATTR_RO(csr_gslc01_acc_val_apg);
static DEVICE_ATTR_RO(csr_gslc23_acc_val_apg);
static DEVICE_ATTR_RO(csr_gmc_acc_val_acg);
static DEVICE_ATTR_RO(csr_gmc_acc_val_apg);

static ssize_t platform_power_residency_show(struct device *dev,
					     struct device_attribute *devattr,
					     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	ssize_t len = 0;
	int i;

	if (google_read_section(pd, PD_PLATFORM_POWER))
		return sysfs_emit(buf, "Read/Write Collision\n");

	for (i = 0;
	     i < powerdashboard_iface.attrs.platform_power_state_names->count;
	     ++i) {
		len += sysfs_emit_at(buf, len, "power_state: %s\n",
				     powerdashboard_iface.attrs
					     .platform_power_state_names
					     ->values[i]);
		len += sysfs_emit_at(buf, len, "entry_count: %llu\n",
				     powerdashboard_iface.sections
					     .platform_power_section
					     ->plat_power_res[i]
					     .entry_count);
		len += sysfs_emit_at(
			buf, len, "time_in_state_ms: %llu\n",
			get_ms_from_ticks(powerdashboard_iface.sections
						  .platform_power_section
						  ->plat_power_res[i]
						  .time_in_state));
		len += sysfs_emit_at(buf, len,
				     "last_entry_timestamp_ticks: %llu\n",
				     powerdashboard_iface.sections
					     .platform_power_section
					     ->plat_power_res[i]
					     .last_entry_ts);
		len += sysfs_emit_at(buf, len,
				     "last_exit_timestamp_ticks: %llu\n",
				     powerdashboard_iface.sections
					     .platform_power_section
					     ->plat_power_res[i]
					     .last_exit_ts);
		len += sysfs_emit_at(buf, len, "\n");
	}

	return len;
}

static DEVICE_ATTR_RO(platform_power_residency);

struct attribute *google_powerdashboard_power_state_attrs[] = {
	&dev_attr_blockers_dormant_suspend.attr,
	&dev_attr_blockers_ambient_sec_acc.attr,
	&dev_attr_blockers_ambient_suspend.attr,
	&dev_attr_platform_power_residency.attr,
	&dev_attr_csr_fabmed_acc_val_acg.attr,
	&dev_attr_csr_fabrt_acc_val_acg.attr,
	&dev_attr_csr_fabstby_acc_val_acg.attr,
	&dev_attr_csr_fabsyss_acc_val_acg.attr,
	&dev_attr_csr_fabhbw_acc_val_acg.attr,
	&dev_attr_csr_fabmem_acc_val_acg.attr,
	&dev_attr_csr_gslc01_acc_val_acg.attr,
	&dev_attr_csr_gslc23_acc_val_acg.attr,
	&dev_attr_csr_gslc01_acc_val_apg.attr,
	&dev_attr_csr_gslc23_acc_val_apg.attr,
	&dev_attr_csr_gmc_acc_val_acg.attr,
	&dev_attr_csr_gmc_acc_val_apg.attr,
	NULL
};


static ssize_t blocker_stats_sysfs_helper(struct pd_power_state_blocker_stats blocker_stats,
		const char *blocker_str, enum blocker_modes mode, char *buf, ssize_t len)
{
	u64 blocked_count, last_blocked_ts;

	blocked_count = blocker_stats.blocked_count;
	last_blocked_ts = blocker_stats.last_blocked_ts;

	switch (mode) {
	case BLOCKER_FABSTBY_VOTE:
		len += sysfs_emit_at(buf, len, "Blocker: FABSTBY %s\n", blocker_str);
		break;
	case BLOCKER_DRAM_VOTE:
		len += sysfs_emit_at(buf, len, "Blocker: DRAM %s\n", blocker_str);
		break;
	default:
		len += sysfs_emit_at(buf, len, "Blocker: %s\n", blocker_str);
		break;
	}

	len += sysfs_emit_at(buf, len, "Blocked Count: %llu\n", blocked_count);
	len += sysfs_emit_at(buf, len, "Last Blocked Timestamp (ticks): %llu\n", last_blocked_ts);

	return len;
}

ssize_t google_power_state_blockers_show(struct google_powerdashboard *pd, char *buf,
				unsigned int state)
{
	struct pd_power_state_blockers_section *blockers_section =
		(struct pd_power_state_blockers_section *)
		powerdashboard_iface.sections.power_state_blockers_section;
	struct pd_power_state_blocker_stats kernel_blocker_stats;
	struct pd_power_state_kernel_blockers kernel_blockers;
	struct pd_power_state_sswrp_blockers sswrp_blocker;
	u64 curr_time, blocked_time, last_blocked_ts;
	u32 is_currently_blocking;
	int j, blocker_id;
	ssize_t len = 0;
	enum blocker_modes mode = BLOCKER_SSWRP;

	curr_time = goog_gtc_get_counter();

	len += sysfs_emit_at(buf, len, "\n[%s]\n\n",
		powerdashboard_iface.attrs.platform_power_state_names->values[state]);

	/* Current SSWRP Blockers */
	sswrp_blocker = blockers_section->sswrp_blockers[state];
	len += sysfs_emit_at(buf, len, "Current Blockers:\n");

	for (j = 0; j < SOC_POWER_PRE_LPB_NUM_SSWRPS; j++) {
		blocker_id = powerdashboard_iface.attrs.precondition_blocker_lpb_ids->values[j];
		if (sswrp_blocker.blocker_stats[j].is_currently_blocking) {
			if (j == SOC_POWER_PRE_FABSTBY_AOC_ID ||
				j == SOC_POWER_PRE_FABSTBY_GSA_ID) {
				len += sysfs_emit_at(buf, len, " FABSTBY %s\n",
				powerdashboard_iface.attrs.sswrp_names->values[blocker_id]);
			} else if (j == SOC_POWER_PRE_DRAM_AOC_ID ||
						j == SOC_POWER_PRE_DRAM_GSA_ID) {
				len += sysfs_emit_at(buf, len, " DRAM %s\n",
				powerdashboard_iface.attrs.sswrp_names->values[blocker_id]);
			} else {
				len += sysfs_emit_at(buf, len, " %s\n",
				powerdashboard_iface.attrs.sswrp_names->values[blocker_id]);
			}
		}
	}
	len += sysfs_emit_at(buf, len, "\n\n");

	/* Kernel Blocker Stats */
	if (powerdashboard_constants.ip_idle_idx_num > SOC_PWR_STATE_NUM_KERNEL_BLOCKERS)
		len += sysfs_emit_at(buf, len,
			"FW UPDATE REQD: Update number of kernel blockers\n\n");

	if (state == SOC_POWER_STATE_DORMANT_SUSPEND) {
		kernel_blockers = blockers_section->kernel_blockers;
		for (j = 1; j < powerdashboard_constants.ip_idle_idx_num; j++) {
			if (j >= SOC_PWR_STATE_NUM_KERNEL_BLOCKERS)
				break;
			kernel_blocker_stats = kernel_blockers.blocker_stats[j];
			len = blocker_stats_sysfs_helper(kernel_blocker_stats,
					powerdashboard_iface.attrs.ip_idle_id_names->values[j],
						mode, buf, len);
			len += sysfs_emit_at(buf, len, "\n");
		}
	}

	/* SSWRP Blocker Stats */
	for (j = 0; j < SOC_POWER_PRE_LPB_NUM_SSWRPS; j++) {
		blocker_id = powerdashboard_iface.attrs.precondition_blocker_lpb_ids->values[j];
		blocked_time = sswrp_blocker.blocker_stats[j].time_blocked;
		last_blocked_ts = sswrp_blocker.blocker_stats[j].last_blocked_ts;
		is_currently_blocking =
			sswrp_blocker.blocker_stats[j].is_currently_blocking;
		mode = BLOCKER_SSWRP;

		if (j == SOC_POWER_PRE_FABSTBY_AOC_ID ||
			j == SOC_POWER_PRE_FABSTBY_GSA_ID) {
			mode = BLOCKER_FABSTBY_VOTE;
		} else if (j == SOC_POWER_PRE_DRAM_AOC_ID ||
					j == SOC_POWER_PRE_DRAM_GSA_ID) {
			mode = BLOCKER_DRAM_VOTE;
		}

		len = blocker_stats_sysfs_helper(sswrp_blocker.blocker_stats[j],
			powerdashboard_iface.attrs.sswrp_names->values[blocker_id],
				mode, buf, len);
		if (is_currently_blocking)
			blocked_time += (curr_time - last_blocked_ts);
		len += sysfs_emit_at(buf, len,
				"Blocked Time (ms): %llu\n",
				get_ms_from_ticks(blocked_time));
		len += sysfs_emit_at(buf, len,
				"Currently blocking: %s\n\n",
				is_currently_blocking ? "Yes" : "No");
	}

	return len;
}

ssize_t google_lpcm_show(struct google_powerdashboard *pd, char *buf,
			 int id, int lpcm_num)
{
	struct pd_lpcm_res *lpcm_res =
		powerdashboard_iface.sections.lpcm_section->lpcm_residencies[id];
	ssize_t len = 0;
	int i, j;

	for (i = 0; i < lpcm_num; ++i) {
		len += sysfs_emit_at(buf, len, "lpcm[%d/%d]\n", i + 1,
				     lpcm_num);
		len += sysfs_emit_at(buf, len, "current_pf_state: %u\n",
				     lpcm_res[i].curr_pf_state);
		len += sysfs_emit_at(buf, len, "num_pf_states: %u\n",
				     lpcm_res[i].num_pf_states);
		for (j = 0; j < lpcm_res[i].num_pf_states; ++j) {
			len += sysfs_emit_at(buf, len, "pf_state: %d\n", j);
			len += sysfs_emit_at(buf, len, "pf_entry_count: %llu\n",
					     (u64)lpcm_res[i]
						     .pf_state_res[j]
						     .entry_count);
			len += sysfs_emit_at(buf, len,
					     "pf_time_in_state: %llu (us)\n",
					     lpcm_res[i]
						     .pf_state_res[j]
						     .time_in_state);
			len += sysfs_emit_at(buf, len, "\n");
		}
	}
	return len;
}

static ssize_t rail_voltage_show(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	ssize_t len = 0;
	int i;

	if (google_read_section(pd, PD_RAIL))
		return sysfs_emit(buf, "Read/Write Collision\n");

	for (i = 0; i < powerdashboard_iface.attrs.rail_names->count; ++i) {
		len += sysfs_emit_at(buf, len, "rail_name: %s\n",
				     powerdashboard_iface.attrs.rail_names
					     ->values[i]);
		len += sysfs_emit_at(buf, len, "current_voltage: %u\n",
				     powerdashboard_iface.sections.rail_section
					     ->curr_voltage[i]);
	}

	return len;
}

static DEVICE_ATTR_RO(rail_voltage);

struct attribute *google_powerdashboard_soc_specific_attrs[] = {
	&dev_attr_rail_voltage.attr,
	NULL,
};

