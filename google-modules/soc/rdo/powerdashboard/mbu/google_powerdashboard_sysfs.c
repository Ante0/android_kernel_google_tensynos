// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */
#include "../google_powerdashboard_iface.h"
#include "../google_powerdashboard_helper.h"
#include "google_powerdashboard_impl.h"

#include <linux/platform_device.h>
#include <dvfs-helper/google_dvfs_helper.h>
#include "soc/google/google_gtc.h"

#define DEFINE_PWRBLK_NODE(a, b, c) SYSFS_PWRBLK_NODE(PD_PWRBLK_ID(a), b, c);
#define DEFINE_LPCM_NODE(a, b) SYSFS_LPCM_NODE(PD_LPCM_ID(a), b, 1);
#define DEFINE_PWRBLK_ATTR(a, b, c) static DEVICE_ATTR_RO(pwrblk_##b);
#define DEFINE_LPCM_ATTR(_, b) static DEVICE_ATTR_RO(lpcm_##b);

MBU_PWRBLK_X_MACRO_TABLE(DEFINE_PWRBLK_NODE)


MBU_MEDIUM_LPCM_X_MACRO_TABLE(DEFINE_LPCM_NODE)
MBU_LARGE_LPCM_X_MACRO_TABLE(DEFINE_LPCM_NODE)

MBU_PWRBLK_X_MACRO_TABLE(DEFINE_PWRBLK_ATTR)
MBU_MEDIUM_LPCM_X_MACRO_TABLE(DEFINE_LPCM_ATTR)
MBU_LARGE_LPCM_X_MACRO_TABLE(DEFINE_LPCM_ATTR)

#define ASSIGN_PWRBLK_ATTR(a, b, c) (&dev_attr_pwrblk_##b.attr),
#define ASSIGN_LPCM_ATTR(a, b) (&dev_attr_lpcm_##b.attr),

struct attribute *google_powerdashboard_sswrp_attrs[] = {
	MBU_PWRBLK_X_MACRO_TABLE(ASSIGN_PWRBLK_ATTR)
	MBU_MEDIUM_LPCM_X_MACRO_TABLE(ASSIGN_LPCM_ATTR)
	MBU_LARGE_LPCM_X_MACRO_TABLE(ASSIGN_LPCM_ATTR)
	NULL,
};

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
		len += sysfs_emit_at(buf, len, "entry_count: %u\n",
				     powerdashboard_iface.sections
					     .platform_power_section
					     ->plat_power_res[i]
					     .entry_count);
		len += sysfs_emit_at(buf, len, "time_in_state: %llu\n",
				     powerdashboard_iface.sections
					     .platform_power_section
					     ->plat_power_res[i]
					     .time_in_state);
		len += sysfs_emit_at(buf, len,
				     "last_entry_timestamp: %llu\n",
				     powerdashboard_iface.sections
					     .platform_power_section
					     ->plat_power_res[i]
					     .last_entry_ts);
		len += sysfs_emit_at(buf, len,
				     "last_exit_timestamp: %llu\n",
				     powerdashboard_iface.sections
					     .platform_power_section
					     ->plat_power_res[i]
					     .last_exit_ts);
		len += sysfs_emit_at(buf, len, "\n");
	}

	return len;
}

static DEVICE_ATTR_RO(platform_power_residency);

static ssize_t blocker_stats_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	ssize_t len = 0;
	int state, triplet;
	struct pd_power_state_blocker_stats *stats;
	struct pd_power_state_blockers_section *blockers_section =
		(struct pd_power_state_blockers_section *)powerdashboard_iface
			.sections.power_state_blockers_section;

	if (google_read_section(pd, PD_POWER_STATE_BLOCKERS))
		return -EBUSY;

	for (state = 0; state < SOC_POWER_STATE_NUM; ++state) {
		len += sysfs_emit_at(buf, len, "%s Total time blocked: %llu\n",
				     powerdashboard_iface.attrs
					     .platform_power_state_names
					     ->values[state],
				     blockers_section
					     ->total_time_blocked[state]);
	}

	len += sysfs_emit_at(buf, len, "\n");

	for (triplet = 0;
	     triplet < powerdashboard_iface.attrs.blocker_names->count;
	     ++triplet) {
		stats = &blockers_section->triplet_stats[triplet];
		len += sysfs_emit_at(buf, len,
				     "= %s\n"
				     "  Blkts: %llu\n"
				     "  Blktime: %llu\n"
				     "  Curblk: %s\n\n",
				     powerdashboard_iface.attrs.blocker_names
					     ->values[triplet],
				     stats->last_blocked_ts,
				     stats->time_blocked,
				     stats->is_currently_blocking ? "Y" :
								    "N");
	}

	return len;
}

static ssize_t current_blockers_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	ssize_t len = 0;
	int triplet;
	struct pd_power_state_blocker_stats *stats;
	struct pd_power_state_blockers_section *blockers_section =
		(struct pd_power_state_blockers_section *)powerdashboard_iface
			.sections.power_state_blockers_section;

	if (google_read_section(pd, PD_POWER_STATE_BLOCKERS))
		return -EBUSY;

	for (triplet = 0;
	     triplet < powerdashboard_iface.attrs.blocker_names->count;
	     ++triplet) {
		stats = &blockers_section->triplet_stats[triplet];
		if (stats->is_currently_blocking) {
			len += sysfs_emit_at(buf, len, "%s\n",
					     powerdashboard_iface.attrs
						     .blocker_names
						     ->values[triplet]);
		}
	}
	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

static ssize_t google_power_state_simplified_blockers_show(struct google_powerdashboard *pd,
							   char *buf,
							   int state)
{
	ssize_t len = 0;
	int client;
	struct pd_power_state_blocker_stats *stats;
	struct pd_power_state_blockers_section *blockers_section =
		(struct pd_power_state_blockers_section *)powerdashboard_iface
			.sections.power_state_blockers_section;

	for (client = 0; client < PD_BLOCKER_CLIENT_COUNT; ++client) {
		stats = &blockers_section->simplified_stats[state][client];
		len += sysfs_emit_at(buf, len,
					"= %s %s\n"
					"  Blkts: %llu\n"
					"  Blktime: %llu\n"
					"  Curblk: %s\n\n",
					powerdashboard_iface.attrs
						.platform_power_state_names
						->values[state],
					powerdashboard_iface.attrs
						.blocker_client_names
						->values[client],
					stats->last_blocked_ts,
					stats->time_blocked,
					stats->is_currently_blocking ?
						"Y" :
						"N");
	}


	return len;
}

static DEVICE_ATTR_RO(blocker_stats);
static DEVICE_ATTR_RO(current_blockers);

POWER_STATE_SIMPLIFIED_BLOCKERS_NODE(dormant_suspend, SOC_POWER_STATE_DORMANT_SUSPEND)
POWER_STATE_SIMPLIFIED_BLOCKERS_NODE(ambient_mc_on, SOC_POWER_STATE_AMBIENT_MC_ON)
POWER_STATE_SIMPLIFIED_BLOCKERS_NODE(ambient_od_af, SOC_POWER_STATE_AMBIENT_OD_AF)
POWER_STATE_SIMPLIFIED_BLOCKERS_NODE(ambient_suspend, SOC_POWER_STATE_AMBIENT_SUSPEND)

static DEVICE_ATTR_RO(simplified_blockers_dormant_suspend);
static DEVICE_ATTR_RO(simplified_blockers_ambient_mc_on);
static DEVICE_ATTR_RO(simplified_blockers_ambient_od_af);
static DEVICE_ATTR_RO(simplified_blockers_ambient_suspend);

ssize_t google_lpcm_show(struct google_powerdashboard *pd, char *buf, int id,
			 int lpcm_num)
{
	struct pd_lpcm_res *lpcm_res =
		powerdashboard_iface.sections.lpcm_section->lpcm_residencies[id];
	ssize_t len = 0;
	int i, j;
	u64 curr_time = goog_gtc_get_counter();
	u64 last_entry_ts;
	u64 total_ticks;
	s64 freq_exact;

	for (i = 0; i < lpcm_num; ++i) {
		len += sysfs_emit_at(buf, len, "lpcm[%d/%d]\n", i + 1,
				     lpcm_num);
		len += sysfs_emit_at(buf, len, "current_pf_state: %u\n",
				     lpcm_res[i].curr_pf_state);
		len += sysfs_emit_at(buf, len, "num_pf_states: %u\n",
				     lpcm_res[i].num_pf_states);
		for (j = 0; j < lpcm_res[i].num_pf_states; ++j) {
			struct dvfs_domain_info const * const di =
				dvfs_helper_get_domain_from_id(lpcm_res[i].pwrblk);
			if (!di)
				continue;

			freq_exact = dvfs_helper_lvl_to_freq_exact(di, j);
			if (freq_exact < 0)
				continue;

			last_entry_ts = lpcm_res[i].pf_state_res[j].last_entry_ts;
			total_ticks = lpcm_res[i].pf_state_res[j].time_in_state;
			if (lpcm_res[i].curr_pf_state == j && last_entry_ts > 0)
				total_ticks += (curr_time - last_entry_ts);

			len += sysfs_emit_at(buf, len, "pf_state_freq_hz: %lld\n", freq_exact);
			len += sysfs_emit_at(buf, len, "pf_entry_count: %llu\n",
					     (u64)lpcm_res[i]
						     .pf_state_res[j]
						     .entry_count);
			len += sysfs_emit_at(buf, len,
					     "pf_time_in_state: %llu (us)\n",
					      get_ms_from_ticks(total_ticks) * 1000);
			len += sysfs_emit_at(buf, len, "\n");
		}
	}
	return len;
}

FABRIC_ACG_APG_RES_NODE(fabmed_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabmed_acc_val_apg);
FABRIC_ACG_APG_RES_NODE(fabdisp_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabdisp_acc_val_apg);
FABRIC_ACG_APG_RES_NODE(fabstby_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabstby_acc_val_apg);
FABRIC_ACG_APG_RES_NODE(fabsyss_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabsyss_acc_val_apg);
FABRIC_ACG_APG_RES_NODE(fabhbw_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(fabhbw_acc_val_apg);
FABRIC_ACG_APG_RES_NODE(memss01_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(memss01_acc_val_apg);
FABRIC_ACG_APG_RES_NODE(memss23_acc_val_acg);
FABRIC_ACG_APG_RES_NODE(memss23_acc_val_apg);

GMC_ACG_APG_RES_NODE(gmc0_acc_val_acg);
GMC_ACG_APG_RES_NODE(gmc0_acc_val_apg);
GMC_ACG_APG_RES_NODE(gmc1_acc_val_acg);
GMC_ACG_APG_RES_NODE(gmc1_acc_val_apg);
GMC_ACG_APG_RES_NODE(gmc2_acc_val_acg);
GMC_ACG_APG_RES_NODE(gmc2_acc_val_apg);
GMC_ACG_APG_RES_NODE(gmc3_acc_val_acg);
GMC_ACG_APG_RES_NODE(gmc3_acc_val_apg);

static DEVICE_ATTR_RO(csr_fabmed_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabmed_acc_val_apg);
static DEVICE_ATTR_RO(csr_fabdisp_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabdisp_acc_val_apg);
static DEVICE_ATTR_RO(csr_fabstby_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabstby_acc_val_apg);
static DEVICE_ATTR_RO(csr_fabsyss_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabsyss_acc_val_apg);
static DEVICE_ATTR_RO(csr_fabhbw_acc_val_acg);
static DEVICE_ATTR_RO(csr_fabhbw_acc_val_apg);
static DEVICE_ATTR_RO(csr_memss01_acc_val_acg);
static DEVICE_ATTR_RO(csr_memss01_acc_val_apg);
static DEVICE_ATTR_RO(csr_memss23_acc_val_acg);
static DEVICE_ATTR_RO(csr_memss23_acc_val_apg);

static DEVICE_ATTR_RO(csr_gmc0_acc_val_acg);
static DEVICE_ATTR_RO(csr_gmc0_acc_val_apg);
static DEVICE_ATTR_RO(csr_gmc1_acc_val_acg);
static DEVICE_ATTR_RO(csr_gmc1_acc_val_apg);
static DEVICE_ATTR_RO(csr_gmc2_acc_val_acg);
static DEVICE_ATTR_RO(csr_gmc2_acc_val_apg);
static DEVICE_ATTR_RO(csr_gmc3_acc_val_acg);
static DEVICE_ATTR_RO(csr_gmc3_acc_val_apg);

struct attribute *google_powerdashboard_power_state_attrs[] = {
	&dev_attr_platform_power_residency.attr,
	&dev_attr_blocker_stats.attr,
	&dev_attr_current_blockers.attr,
	&dev_attr_simplified_blockers_dormant_suspend.attr,
	&dev_attr_simplified_blockers_ambient_mc_on.attr,
	&dev_attr_simplified_blockers_ambient_od_af.attr,
	&dev_attr_simplified_blockers_ambient_suspend.attr,
	&dev_attr_csr_fabmed_acc_val_acg.attr,
	&dev_attr_csr_fabmed_acc_val_apg.attr,
	&dev_attr_csr_fabdisp_acc_val_acg.attr,
	&dev_attr_csr_fabdisp_acc_val_apg.attr,
	&dev_attr_csr_fabstby_acc_val_acg.attr,
	&dev_attr_csr_fabstby_acc_val_apg.attr,
	&dev_attr_csr_fabsyss_acc_val_acg.attr,
	&dev_attr_csr_fabsyss_acc_val_apg.attr,
	&dev_attr_csr_fabhbw_acc_val_acg.attr,
	&dev_attr_csr_fabhbw_acc_val_apg.attr,
	&dev_attr_csr_memss01_acc_val_acg.attr,
	&dev_attr_csr_memss01_acc_val_apg.attr,
	&dev_attr_csr_memss23_acc_val_acg.attr,
	&dev_attr_csr_memss23_acc_val_apg.attr,
	&dev_attr_csr_gmc0_acc_val_acg.attr,
	&dev_attr_csr_gmc0_acc_val_apg.attr,
	&dev_attr_csr_gmc1_acc_val_acg.attr,
	&dev_attr_csr_gmc1_acc_val_apg.attr,
	&dev_attr_csr_gmc2_acc_val_acg.attr,
	&dev_attr_csr_gmc2_acc_val_apg.attr,
	&dev_attr_csr_gmc3_acc_val_acg.attr,
	&dev_attr_csr_gmc3_acc_val_apg.attr,
	NULL
};

static ssize_t vote_residency_show(struct device *dev,
				   struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	ssize_t len = 0;
	enum pd_voter voter;
	enum pd_vote vote;
	struct pd_vote_section *vote_section =
		powerdashboard_iface.sections.vote_section;
	struct pd_residency_time_state *stats;

	if (google_read_section(pd, PD_VOTE))
		return sysfs_emit(buf, "Read/Write Collision\n");

	for (voter = 0; voter < PD_VOTER_COUNT; ++voter) {
		for (vote = 0; vote < PD_VOTE_COUNT; ++vote) {
			stats = &vote_section->vote_res[voter * PD_VOTE_COUNT +
							vote];
			len += sysfs_emit_at(
				buf, len,
				"Voter: %s, Resource: %s\n"
				"  Cumulative time voted: %llu\n"
				"  Vote Count: %u\n"
				"  Last vote added timestamp: %llu\n"
				"  Last vote removed timestamp: %llu\n\n",
				powerdashboard_iface.attrs.voter_names
					->values[voter],
				powerdashboard_iface.attrs.voted_resource_names
					->values[vote],
				stats->time_in_state, stats->entry_count,
				stats->last_entry_ts, stats->last_exit_ts);
		}
	}

	return len;
}

static DEVICE_ATTR_RO(vote_residency);

struct attribute *google_powerdashboard_soc_specific_attrs[] = {
	&dev_attr_vote_residency.attr, NULL
};
