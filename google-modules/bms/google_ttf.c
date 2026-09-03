/*
 * Copyright 2018 Google, LLC
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

#pragma clang diagnostic ignored "-Wformat"

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <misc/logbuffer.h>
#include "google_bms.h"
#include "google_psy.h"
#include "qmath.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif

#define ELAP_LIMIT_S 60
#define USE_INSTANTANEOUS_PWR -1

void ttf_log(const struct batt_ttf_stats *stats, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	logbuffer_vlog(stats->ttf_log, fmt, args);
	va_end(args);
}

/* actual adapter current capability for this charging event
 * NOTE: peformance for a tier are known only after entering the tier
 */
static int ttf_pwr_icl(const struct gbms_ce_tier_stats *ts,
		       const union gbms_ce_adapter_details *ad)
{
	int elap, amperage;

	elap = ts->time_fast + ts->time_taper;
	if (elap <= ELAP_LIMIT_S)
		amperage = ad->ad_amperage * 100;
	else
		amperage = div_s64(ts->icl_sum, (elap + ts->time_other));

	return amperage;
}

/* NOTE: the current in taper might need to be accounted in a different way */
int ttf_pwr_ibatt(const struct gbms_ce_tier_stats *ts)
{
	int avg_ibatt, elap, sign = 1;

	elap = ts->time_fast + ts->time_taper;
	/* averages are not reliable until after some time in tier */
	if (elap <= ELAP_LIMIT_S) {
		pr_debug("%s: limit=%d elap=%d (%d+%d) o=%d\n", __func__,
			 elap, ELAP_LIMIT_S, ts->time_fast, ts->time_taper,
			 ts->time_other);
		return 0;
	}

	/* actual, called only when avg_ibatt in tier indicates charging */
	avg_ibatt = div_s64(ts->ibatt_sum, (elap + ts->time_other));
	if (avg_ibatt < 0)
		sign = -1;

	pr_debug("%s: elap=%d (%d+%d+%d) sum=%ld avg_ibatt=%d\n", __func__,
		 elap, ts->time_fast, ts->time_taper, ts->time_other,
		 ts->ibatt_sum, avg_ibatt * sign);

	return avg_ibatt * sign;
}

/* nominal voltage tier index for this soc */
static int ttf_pwr_vtier_idx(const struct batt_ttf_stats *stats, int soc)
{
	int i;

	for (i = 1; i < GBMS_STATS_TIER_COUNT; i++)
		if (soc < stats->tier_stats[i].soc_in >> 8)
			break;

	return i - 1;
}

/*
 * reference or current average current demand for a soc at max rate.
 * NOTE: always <= cc_max for reference temperature
 */

static bool ttf_cc_check(const struct ttf_soc_stats *soc_stats, int soc)
{
	/* delta_cc shouldn't be negative */
	return soc_stats->cc[soc] && soc_stats->cc[soc + 1] &&
	       soc_stats->cc[soc] < soc_stats->cc[soc + 1];
}

int ttf_ref_cc(const struct batt_ttf_stats *stats, int soc)
{
	const struct ttf_soc_stats *sstat = NULL;
	int delta_cc;

	/* out of range */
	if (soc + 1 >= GBMS_SOC_STATS_LEN)
		return 0;

	/* soc average current demand */
	if (ttf_cc_check(&stats->soc_stats, soc) && stats->soc_stats.elap[soc])
		sstat = &stats->soc_stats;
	else if (ttf_cc_check(&stats->soc_ref, soc) && stats->soc_ref.elap[soc])
		sstat = &stats->soc_ref;
	else
		return 0;

	delta_cc = (sstat->cc[soc + 1] - sstat->cc[soc]);

	pr_debug("%s %d: delta_cc=%d elap=%ld\n", __func__, soc,
		delta_cc, sstat->elap[soc]);

	return div64_u64((delta_cc * 3600), sstat->elap[soc]);
}

/* assumes that health is active for any soc greater than CHG_HEALTH_REST_SOC */
static int ttf_pwr_health(const struct gbms_charging_event *ce_data, int soc)
{
	return CHG_HEALTH_REST_IS_ACTIVE(&ce_data->ce_health) &&
	       soc >= CHG_HEALTH_REST_SOC(&ce_data->ce_health);
}

static int ttf_pwr_health_pause(const struct gbms_charging_event *ce_data, int soc)
{
	return CHG_HEALTH_REST_IS_PAUSE(&ce_data->ce_health) &&
	       soc >= CHG_HEALTH_REST_SOC(&ce_data->ce_health);
}

#define CHARGER_EFFICIENCY 95 /* TODO: use real efficiency */
/*
 * equivalent icl: minimum between actual input current limit and battery
 * everage current _while_in_tier. actual_icl will be lower in high current
 * tiers for bad cables, ibatt is affected by temperature tier and sysload.
 */
static int ttf_pwr_equiv_icl(const struct gbms_charging_event *ce_data,
			     int vbatt_idx, int soc, int fcc_now, int mdis_pwr_uw)
{
	const struct gbms_chg_profile *profile = ce_data->chg_profile;
	const int volt_limit = profile->volt_limits[vbatt_idx] == 0 ?
			       profile->volt_limits[profile->volt_nb_limits - 1] :
			       profile->volt_limits[vbatt_idx];
	const int aratio = (ce_data->adapter_details.ad_voltage * 10000) / (volt_limit / 1000);
	const struct gbms_ce_tier_stats *tier_stats;
	const u32 capacity_ma = profile->capacity_ma;
	const int rest_rate = ce_data->ce_health.rest_rate;
	int equiv_icl, act_icl, act_ibatt, health_ibatt = -1;
	/* power limit assumed max battery voltage */
	const int vbatt_max_ma = profile->volt_limits[profile->volt_nb_limits - 1] / 1000;
	const int mdis_fcc = mdis_pwr_uw == CSI_POWER_UNKNOWN ?
			     -1 :
			     mdis_pwr_uw / vbatt_max_ma;

	/* Health collects in ce_data->health_stats vtier */
	if (ttf_pwr_health(ce_data, soc)) {
		health_ibatt = ce_data->ce_health.rest_cc_max / 1000;
		tier_stats = &ce_data->health_stats;
	} else if (ttf_pwr_health_pause(ce_data, soc)) {
		/* use ACTIVE current in PAUSE stat for ttf calculation */
		health_ibatt = (capacity_ma * rest_rate * 10) / 1000;
		/* use ACTIVE tier in PAUSE stat for ttf calculation */
		tier_stats = &ce_data->health_stats;
	} else {
		tier_stats = &ce_data->tier_stats[vbatt_idx];
	}

	/*
	 * actual adapter capabilities at adapter voltage for vtier
	 * NOTE: demand and cable might cause voltage and icl do droop
	 */
	act_icl = ttf_pwr_icl(tier_stats, &ce_data->adapter_details);
	if (act_icl <= 0) {
		pr_debug("%s: negative,null act_icl=%d\n", __func__, act_icl);
		return -EINVAL;
	}

	/* scale icl (at adapter voltage) to vtier */
	equiv_icl = (act_icl * aratio * CHARGER_EFFICIENCY) / 10000;
	pr_debug("%s: act_icl=%d aratio=%d equiv_icl=%d\n",
		 __func__, act_icl, aratio, equiv_icl);

	/* actual ibatt in this tier: act_ibatt==0 when too early to tell */
	act_ibatt = ttf_pwr_ibatt(tier_stats);
	if (act_ibatt < 0) {
		pr_debug("%s: discharging ibatt=%d\n", __func__, act_ibatt);
		return -EINVAL;
	} else if (act_ibatt == 0) {
		/* Set -1 when ttf_pwr_ibatt is not ready, as 0 could be valid mdis_fcc limit */
		act_ibatt = -1;
	}

	/* act_ibatt = min(act_ibatt, health_ibatt, fcc_now, mdis_fcc) */
	if (act_ibatt == -1 && health_ibatt > 0)
		act_ibatt = health_ibatt;
	if (fcc_now > 0 && (act_ibatt == -1 || fcc_now < act_ibatt))
		act_ibatt = fcc_now;
	if (mdis_fcc >= 0 && (act_ibatt == -1 || mdis_fcc < act_ibatt))
		act_ibatt = mdis_fcc;

	/* assume that can deliver equiv_icl when act_ibatt == -1 */
	if (act_ibatt >= 0 && act_ibatt < equiv_icl) {
		pr_debug("%s: sysload ibatt=%d, reduce icl %d->%d\n",
			 __func__, act_ibatt, equiv_icl, act_ibatt);
		equiv_icl = act_ibatt;
	}

	pr_debug("%s: equiv_icl=%d\n", __func__, equiv_icl);
	return equiv_icl;
}

/*
 * time scaling factor for available power and SOC demand.
 * NOTE: usually called when soc < ssoc_in && soc > ce_data->last_soc
 * TODO: this is very inefficient
 *
 * @pwr  If positive, the available power is used to calculate the ratio; otherwise, the
 *       instantaneous power is used.
 */
static int ttf_pwr_ratio(const struct batt_ttf_stats *stats,
			 const struct gbms_charging_event *ce_data,
			 int soc, int vbatt_idx, int pwr)
{
	const struct gbms_chg_profile *profile = ce_data->chg_profile;
	int cc_max, temp_idx;
	int avg_cc, equiv_icl;
	int ratio;

	/* regular charging tier */
	if (vbatt_idx < 0)
		return -EINVAL;

	/* TODO: compensate with average increase/decrease of temperature? */
	temp_idx = ce_data->tier_stats[vbatt_idx].temp_idx;
	if (temp_idx == -1) {
		int64_t t_avg = 0;
		const int elap = ce_data->tier_stats[vbatt_idx].time_fast +
		 		 ce_data->tier_stats[vbatt_idx].time_taper +
		 		 ce_data->tier_stats[vbatt_idx].time_other;

		if (ce_data->tier_stats[vbatt_idx].temp_sum != 0 || elap == 0)
			t_avg = ce_data->tier_stats[vbatt_idx].temp_in;
		if (t_avg == 0)
			t_avg = 250;

		/* average temperature in tier for charge tier index */
		temp_idx = gbms_msc_temp_idx(profile, t_avg);
		pr_debug("%s %d: temp_idx=%d t_avg=%ld sum=%ld elap=%d\n",
			__func__, soc, temp_idx, t_avg,
			ce_data->tier_stats[vbatt_idx].temp_sum,
			elap);

		if (temp_idx < 0)
			return -EINVAL;
	}

	/* max tier demand for voltage tier at this temperature index */
	vbatt_idx = vbatt_idx > profile->volt_nb_limits - 1 ?
		    profile->volt_nb_limits - 1 : vbatt_idx;
	cc_max = GBMS_CCCM_LIMITS(profile, temp_idx, vbatt_idx) / 1000;
	/* statistical current demand for soc (<= cc_max) */
	avg_cc = ttf_ref_cc(stats, soc);
	if (avg_cc <= 0) {
		/* default to cc_max if we have no data */
		pr_debug("%s %d: demand use default avg_cc=%d->%d\n",
			__func__, soc, avg_cc, cc_max);
		avg_cc = cc_max;
	}

	if (pwr == 0)
		return -EINVAL;

	/* If power was provided, calculate ratio based on powers */
	if (pwr > 0) {
		const int volt_limit = profile->volt_limits[vbatt_idx] == 0 ?
				       profile->volt_limits[profile->volt_nb_limits - 1] :
				       profile->volt_limits[vbatt_idx];
		int ref_pwr_uw = (volt_limit / 1000) * avg_cc;
		int pwr_mw = ((pwr / 1000) * CHARGER_EFFICIENCY / 100);

		ratio = ref_pwr_uw / pwr_mw / 10;
		pr_debug("%s %d: ref_pwr_uw=%d, pwr_mw=%d, ratio=%d\n",
			 __func__, soc, ref_pwr_uw, pwr_mw, ratio);

		if (ratio < 100)
			ratio = 100;

		return ratio;
	}

	/* statistical or reference max power demand for the tier at */
	pr_debug("%s %d:%d,%d: avg_cc=%d cc_max=%d\n",
		 __func__, soc, temp_idx, vbatt_idx,
		 avg_cc, cc_max);

	/* equivalent input current for adapter at vtier */
	equiv_icl = ttf_pwr_equiv_icl(ce_data, vbatt_idx, soc, stats->fcc_now / 1000,
				      READ_ONCE(stats->mdis_pwr_uw));
	if (equiv_icl <= 0) {
		pr_debug("%s %d: negative, null act_icl=%d\n",
			 __func__, soc, equiv_icl);
		return -EINVAL;
	}

	/* lower to cc_max if in HOT and COLD */
	if (cc_max < equiv_icl) {
		pr_debug("%s %d: reduce act_icl=%d to cc_max=%d\n",
			 __func__, soc, equiv_icl, cc_max);
		equiv_icl = cc_max;
	}

	/*
	 * This is the trick that makes everything work:
	 *   equiv_icl = min(act_icl, act_ibatt, cc_max)
	 *
	 * act_icl = adapter max or adapter actual icl (due to bad cable,
	 *           AC enabled or temperature shift) scaled to vtier
	 * act_ibatt = measured for
	 *   at reference temperature or actual < cc_max due to sysload
	 * cc_max = cc_max from profile (lower than ref for  HOT or COLD)
	 *
	 */

	/* ratio for elap time: it doesn't work if reference is not maximal */
	if (equiv_icl == 0)
		return -EINVAL;

	if (equiv_icl < avg_cc)
		ratio = (avg_cc * 100) / equiv_icl;
	else
		ratio = 100;

	pr_debug("%s %d: equiv_icl=%d, avg_cc=%d ratio=%d\n",
		 __func__, soc, equiv_icl, avg_cc, ratio);

	return ratio;
}

/* SOC estimates ---------------------------------------------------------  */

/* reference of current elap for a soc at max rate */
static int ttf_ref_elap(const struct batt_ttf_stats *stats, int soc)
{
	ktime_t elap;

	if (soc < 0 || soc >= 100)
		return 0;

	elap = stats->soc_stats.elap[soc];
	if (elap == 0)
		elap = stats->soc_ref.elap[soc];

	return elap;
}

/* elap time for a single soc% */
static int ttf_elap(ktime_t *estimate, const struct batt_ttf_stats *stats,
		    const struct gbms_charging_event *ce_data,
		    int soc, int tier_idx, int pwr)
{
	ktime_t elap;
	int ratio;

	/* cannot really return 0 elap unless the data is corrupted */
	elap = ttf_ref_elap(stats, soc);
	if (elap == 0) {
		pr_debug("%s %d: zero elap\n", __func__, soc);
		return -EINVAL;
	}

	ratio = ttf_pwr_ratio(stats, ce_data, soc, tier_idx, pwr);
	if (ratio < 0) {
		pr_debug("%s %d: negative ratio=%d\n", __func__, soc, ratio);
		return -EINVAL;
	}

	*estimate = elap * ratio;

	pr_debug("%s: soc=%d estimate=%lld elap=%lld ratio=%d\n",
		 __func__, soc, *estimate, elap, ratio);

	return ratio;
}

struct pri_chg_state {
	ktime_t base_time;
	ktime_t pri_time;
	int base_soc;
	int chg_time;
};

#define PRI_CHG_NONE_WINDOW -1

static inline ktime_t pri_chg_scale_elap(ktime_t elap, int frac)
{
	return (elap * (100 - frac)) / 100;
}

static int pri_chg_soc_step(struct batt_ttf_stats *stats, const struct gbms_charging_event *ce_data,
			    struct pri_chg_state *state, int soc, int frac, int min_pwr)
{
	ktime_t base_elap = -1;
	ktime_t pri_elap = -1;
	ktime_t next_soc_elap = -1;
	int next_soc_frac = min(50 - frac, 0);
	int base_ratio = -1;
	int pri_ratio = -1;
	int vbatt_idx = ttf_pwr_vtier_idx(stats, soc);
	int ret = -1;

	pri_ratio = ttf_elap(&pri_elap, stats, ce_data, soc, vbatt_idx, min_pwr);
	if (pri_ratio < 0)
		return pri_ratio;

	/* Update baseline till end not reached */
	if (state->base_soc == -1) {
		base_ratio = ttf_elap(&base_elap, stats, ce_data, soc, vbatt_idx,
				      USE_INSTANTANEOUS_PWR);
		if (base_ratio < 0)
			return base_ratio;

		/* Priority charging give no benefits over normal */
		if (state->chg_time == PRI_CHG_NONE_WINDOW && base_ratio <= pri_ratio) {
			/* TODO: b/476420909 */
			state->chg_time = state->base_time;
			state->base_soc = soc;
		} else {
			/* Check if base charging reaches the end */
			next_soc_elap = pri_chg_scale_elap(base_elap, next_soc_frac);

			if (state->chg_time != PRI_CHG_NONE_WINDOW &&
			    state->chg_time <= state->base_time + next_soc_elap)
				state->base_soc = soc;

			state->base_time += pri_chg_scale_elap(base_elap, frac);
		}
	}

	/* Check if priority charging reaches the end */
	next_soc_elap = pri_chg_scale_elap(pri_elap, next_soc_frac);

	if (state->chg_time != PRI_CHG_NONE_WINDOW &&
	    state->chg_time < state->pri_time + next_soc_elap)
		ret = soc - state->base_soc;

	state->pri_time += pri_chg_scale_elap(pri_elap, frac);

	pr_debug("%s: [%2d.%02d%%] ratio: %3d %3d ; time: %7d %7d / %7d ; soc: %d\n", __func__,
		 soc, frac, pri_ratio, base_ratio,
		 state->pri_time, state->base_time,
		 state->chg_time, state->base_soc);
	return ret;
}

static int pri_chg_estimation_loop(struct batt_ttf_stats *stats,
				   const struct gbms_charging_event *ce_data,
				   struct pri_chg_state *state, qnum_t start_soc, qnum_t end_soc,
				   int min_pwr)
{
	int frac, ret, i = 0;

	/* FIRST: 100 - first 2 digits of the fractional part of soc if any */
	frac = (int)qnum_nfracdgt(start_soc, 2);
	i = qnum_toint(start_soc);
	if (frac) {
		ret = pri_chg_soc_step(stats, ce_data, state, i, frac, min_pwr);
		if (ret >= 0 || ret < -1)
			return ret;

		i += 1;
	}

	/* accumulate ttf_elap starting from i + 1 until end */
	for (; i < qnum_toint(end_soc); i++) {
		ret = pri_chg_soc_step(stats, ce_data, state, i, 0, min_pwr);
		if (ret >= 0 || ret < -1)
			return ret;
	}

	/* LAST: first 2 digits of the fractional part of soc if any */
	frac = (int)qnum_nfracdgt(end_soc, 2);
	if (frac) {
		ret = pri_chg_soc_step(stats, ce_data, state, i, frac, min_pwr);
		if (ret >= 0 || ret < -1)
			return ret;
	}

	/* end_soc cannot be 100 here, as pri_chg_soc_step will error then */
	if (state->chg_time != PRI_CHG_NONE_WINDOW && state->chg_time < state->pri_time)
		ret = qnum_toint(end_soc) + 1 - state->base_soc;

	return ret;
}

#define SOC_ROUND_BASE	0.5

/* Calculates SOC gain achievable when utilizing the adapter's maximum power capacity, while
 * adhering to the charging tables
 * NOTE: use charging_window_s = -1 to find optimal charging window
 *
 * @stats: time to full statistics
 * @ce_data: charging event data
 * @start_soc: start SOC for calculations
 * @end_soc: last SOC for calculations
 * @charging_window_s: time limit for calculations, when provided -1 algorithm finds
 *			optimal charging window
 * @min_pwr: min guaranteed power for priority charging
 * @result: [out] output parameter, containing delta soc results
 *
 * @return <0 on error, 0 on success
 *
 * requires mutex_lock(&batt_drv->stats_lock);
 *
 */
int pri_chg_delta_soc(struct batt_ttf_stats *stats, const struct gbms_charging_event *ce_data,
		      qnum_t start_soc, qnum_t end_soc, int charging_window_s, int min_pwr,
		      struct priority_charging_result *result)
{
	struct pri_chg_state state = {
		.base_time = 0,
		.pri_time = 0,
		.base_soc = -1,
		.chg_time = charging_window_s,
	};
	int ret;

	if (charging_window_s != PRI_CHG_NONE_WINDOW)
		state.chg_time = charging_window_s * 100;

	if (end_soc > qnum_rconst(100) || end_soc < start_soc)
		return -EINVAL;

	if (end_soc == start_soc)
		return -EINVAL;

	ret = pri_chg_estimation_loop(stats, ce_data, &state, start_soc, end_soc, min_pwr);
	if (ret < -1)
		return ret;
	if (ret >= 0)
		goto success_exit;

	/*
	 * Reached max soc but baseline time was not reached.
	 * Use estimated priority charging time as time limit for second pass.
	 */
	state.chg_time = state.pri_time;
	state.base_time = 0;
	state.pri_time = 0,
	state.base_soc = -1,

	ret = pri_chg_estimation_loop(stats, ce_data, &state, start_soc, end_soc, min_pwr);
	if (ret < 0) {
		pr_err("failed to evaluate delta soc: %d", ret);
		return -1;
	}

success_exit:
	if (ret == 0) {
		result->delta_soc = 0;
		result->charging_window_s = 0;
		result->baseline_soc = -1;
		return 0;
	}

	if (state.chg_time != PRI_CHG_NONE_WINDOW)
		result->charging_window_s = state.chg_time / 100;
	else
		result->charging_window_s = charging_window_s;
	result->baseline_soc = state.base_soc;
	result->delta_soc = ret;
	return 0;
}

/*
 * time to full from SOC% using the actual stats
 * NOTE: prediction is based stats and corrected with the ce_data
 * NOTE: usually called with soc > ce_data->last_soc
 */
int ttf_soc_estimate(ktime_t *res, struct batt_ttf_stats *stats,
		     const struct gbms_charging_event *ce_data,
		     qnum_t soc, qnum_t last, int tier_idx)
{
	int ssoc_in;
	ktime_t elap, estimate = 0;
	int i = 0, ratio, frac, max_ratio = 0;

	mutex_lock(&stats->ttf_lock);

	ssoc_in = ce_data->charging_stats.ssoc_in;

	if (last > qnum_rconst(100) || last < soc) {
		mutex_unlock(&stats->ttf_lock);
		return -EINVAL;
	}

	if (last == soc) {
		*res = 0;
		mutex_unlock(&stats->ttf_lock);
		return 0;
	}

	if (qnum_toint(soc) == qnum_toint(last)) {
		frac = (int)qnum_nfracdgt(last, 2) - (int)qnum_nfracdgt(soc, 2);
		ratio = ttf_elap(&elap, stats, ce_data, qnum_toint(soc), tier_idx,
				 USE_INSTANTANEOUS_PWR);
		if (ratio < 0) {
			mutex_unlock(&stats->ttf_lock);
			return ratio;
		}

		estimate = ktime_divns((elap * frac), 100);
		*res = ktime_divns(estimate, 100);
		mutex_unlock(&stats->ttf_lock);
		return ratio;
	}

	/* FIRST: 100 - first 2 digits of the fractional part of soc if any */
	frac = (int)qnum_nfracdgt(soc, 2);
	if (frac) {

		ratio = ttf_elap(&elap, stats, ce_data, qnum_toint(soc), tier_idx,
				 USE_INSTANTANEOUS_PWR);
		if (ratio < 0) {
			mutex_unlock(&stats->ttf_lock);
			return ratio;
		}
		if (ratio >= 0)
			estimate += (elap * (100 - frac)) / 100;
		if (ratio > max_ratio)
			max_ratio = ratio;

		i += 1;
	}

	/* accumulate ttf_elap starting from i + 1 until end */
	for (i += qnum_toint(soc); i < qnum_toint(last); i++) {

		if (i >= ssoc_in && i < ce_data->last_soc) {
			/* use real data if within charging event */
			elap = ce_data->soc_stats.elap[i] * 100;
		} else {
			/* future (and soc before ssoc_in) */
			ratio = ttf_elap(&elap, stats, ce_data, i, tier_idx,
					 USE_INSTANTANEOUS_PWR);
			if (ratio < 0) {
				mutex_unlock(&stats->ttf_lock);
				return ratio;
			}
			if (ratio > max_ratio)
				max_ratio = ratio;
		}

		estimate += elap;
	}

	/* LAST: first 2 digits of the fractional part of soc if any */
	frac = (int)qnum_nfracdgt(last, 2);
	if (frac) {
		ratio = ttf_elap(&elap, stats, ce_data, qnum_toint(last), tier_idx,
				 USE_INSTANTANEOUS_PWR);
		if (ratio < 0) {
			mutex_unlock(&stats->ttf_lock);
			return ratio;
		}
		if (ratio >= 0)
			estimate += ktime_divns((elap * frac), 100);
		if (ratio > max_ratio)
			max_ratio = ratio;
	}

	*res = ktime_divns(estimate, 100);

	mutex_unlock(&stats->ttf_lock);

	return max_ratio;
}

static int ttf_cstr(char *buff, int size, const struct ttf_soc_stats *soc_stats,
		    const struct ttf_soc_stats *soc_ref, int start, int end,
		    int const split, const char type)
{
	const bool combine = soc_ref == NULL ? false : true;
	int i, cc, len = 0;
	ktime_t elap;

	for (i = start; i <= end; i++) {
		if (i % split == 0 || i == start) {
			len += scnprintf(&buff[len], size - len, &type);
			if (split == 10)
				len += scnprintf(&buff[len], size - len, "%d", i / 10);
			len += scnprintf(&buff[len], size - len, ":");
		}
		if (type == 'T') {
			elap = soc_stats->elap[i];
			if (combine && elap == 0)
				elap = soc_ref->elap[i];
			len += scnprintf(&buff[len], size - len, " %4ld", elap);
		} else if (type == 'C') {
			cc = soc_stats->cc[i];
			if (combine && cc == 0)
				cc = soc_ref->cc[i];
			len += scnprintf(&buff[len], size - len, " %4d", cc);
		}

		if (i != end && (i + 1) % split == 0)
			len += scnprintf(&buff[len], size - len, "\n");
	}

	len += scnprintf(&buff[len], size - len, "\n");

	return len;
}

int ttf_soc_cstr(char *buff, int size, const struct ttf_soc_stats *soc_stats,
		 int start, int end)
{
	int len = 0, split = 100;

	if (start < 0 || start >= GBMS_SOC_STATS_LEN ||
	    end < 0 || end >= GBMS_SOC_STATS_LEN ||
	    start > end)
		return 0;

	len += scnprintf(&buff[len], size - len, "\n");

	/* only one way to print data @ 100 */
	if (end == 100 && start != 100)
		end = 99;
	/* std newline every 10 entries */
	if (start == 0 && end == 99)
		split = 10;

	/* dump elap time as T: */
	len += ttf_cstr(&buff[len], size - len, soc_stats, NULL, start, end, split, 'T');

	/* dump coulumb count as C: */
	len += ttf_cstr(&buff[len], size - len, soc_stats, NULL, start, end, split, 'C');

	return len;
}

int ttf_soc_cstr_combine(char *buff, int size, const struct ttf_soc_stats *soc_ref,
			 const struct ttf_soc_stats *soc_stats)
{
	int len = 0;

	len += scnprintf(&buff[len], size - len, "\n");

	/* dump elap time as T: */
	len += ttf_cstr(&buff[len], size - len, soc_stats, soc_ref, 0, 99, 10, 'T');

	/* dump coulumb count as C: */
	len += ttf_cstr(&buff[len], size - len, soc_stats, soc_ref, 0, 99, 10, 'C');

	return len;
}

/* TODO: tune these values */

/* discard updates for adapters that have less than 80% of nominal */
#define TTF_SOC_QUAL_ELAP_RATIO_MAX	200
/* cap updates of cc to no more of +-*_CUR_ABS_MAX from previous */
#define TTF_SOC_QUAL_ELAP_DELTA_CUR_ABS_MAX	60
/* cap udpdates to cc max to no more of +-20% of reference */
#define TTF_SOC_QUAL_ELAP_DELTA_REF_PCT_MAX	20

/* return the weight to apply to this change */
static ktime_t ttf_soc_qual_elap(const struct batt_ttf_stats *stats,
				 const struct gbms_charging_event *ce_data,
				 int i, int tier_idx)
{
	const struct ttf_soc_stats *src = &ce_data->soc_stats;
	const struct ttf_soc_stats *dst = &stats->soc_stats;
	const int limit = TTF_SOC_QUAL_ELAP_RATIO_MAX;
	const int max_elap = ktime_divns(((100 + TTF_SOC_QUAL_ELAP_DELTA_REF_PCT_MAX) *
			     stats->soc_ref.elap[i]), 100);
	const int min_elap = ktime_divns(((100 - TTF_SOC_QUAL_ELAP_DELTA_REF_PCT_MAX) *
			     stats->soc_ref.elap[i]), 100);
	ktime_t elap, elap_new, elap_cur;
	int ratio;

	if (!src->elap[i])
		return 0;

	/* weight the adapter, discard if ratio is too high (poor adapter) */
	ratio = ttf_pwr_ratio(stats, ce_data, i, tier_idx, -1);
	if (ratio <= 0 || ratio > limit) {
		pr_debug("%d: ratio=%d limit=%d\n", i, ratio, limit);
		return 0;
	}

	elap_new = ktime_divns((src->elap[i] * 100), ratio);
	elap_cur = dst->elap[i];
	if (!elap_cur)
		elap_cur = stats->soc_ref.elap[i];
	elap = ktime_divns((elap_cur + elap_new), 2);

	/* bounds check to previous */
	if (elap > (elap_cur + TTF_SOC_QUAL_ELAP_DELTA_CUR_ABS_MAX))
		elap = elap_cur + TTF_SOC_QUAL_ELAP_DELTA_CUR_ABS_MAX;
	else if (elap < (elap_cur - TTF_SOC_QUAL_ELAP_DELTA_CUR_ABS_MAX))
		elap = elap_cur - TTF_SOC_QUAL_ELAP_DELTA_CUR_ABS_MAX;

	/* bounds check to reference */
	if (elap > max_elap)
		elap = max_elap;
	else if (elap < min_elap)
		elap = min_elap;

	pr_debug("%d: dst->elap=%ld, ref_elap=%ld, elap=%ld, src_elap=%ld ratio=%d, min=%d max=%d\n",
		 i, dst->elap[i], stats->soc_ref.elap[i], elap, src->elap[i],
		 ratio, min_elap, max_elap);

	return elap;
}

/* cap updates of cc to no more of +-*_CUR_ABS_MAX from previous */
#define TTF_SOC_QUAL_CC_DELTA_CUR_ABS_MAX	40
/* cap udpdates to cc max to no more of +-20% of reference */
#define TTF_SOC_QUAL_CC_DELTA_REF_PCT_MAX	20

static int ttf_soc_qual_cc(const struct batt_ttf_stats *stats,
			   const struct gbms_charging_event *ce_data,
			   int i)
{
	const struct ttf_soc_stats *src = &ce_data->soc_stats;
	const struct ttf_soc_stats *dst = &stats->soc_stats;
	const int max_cc = ((100 + TTF_SOC_QUAL_CC_DELTA_REF_PCT_MAX) *
			   stats->soc_ref.cc[i]) / 100;
	const int min_cc = ((100 - TTF_SOC_QUAL_CC_DELTA_REF_PCT_MAX) *
			   stats->soc_ref.cc[i]) / 100;
	int cc, cc_cur;

	if (!src->cc[i])
		return 0;

	cc_cur = dst->cc[i];
	if (cc_cur <= 0)
		cc_cur = stats->soc_ref.cc[i];

	cc = (cc_cur + src->cc[i]) / 2;

	/* bounds check to previous */
	if (cc > cc_cur + TTF_SOC_QUAL_CC_DELTA_CUR_ABS_MAX)
		cc = cc_cur + TTF_SOC_QUAL_CC_DELTA_CUR_ABS_MAX;
	else if (cc < cc_cur  -TTF_SOC_QUAL_CC_DELTA_CUR_ABS_MAX)
		cc = cc_cur - TTF_SOC_QUAL_CC_DELTA_CUR_ABS_MAX;

	/* bounds check to reference */
	if (cc > max_cc)
		cc = max_cc;
	else if (cc < min_cc)
		cc = min_cc;

	pr_debug("%d: cc_cur=%d, ref_cc=%d src->cc=%d, cc=%d\n",
		 i, cc_cur, stats->soc_ref.cc[i], src->cc[i], cc);

	return cc;
}

/* update soc_stats using the charging event
 * NOTE: first_soc and last_soc are inclusive, will skip socs that have no
 * elap and no cc.
 */
static void ttf_soc_update(struct batt_ttf_stats *stats,
			   const struct gbms_charging_event *ce_data,
			   int first_soc, int last_soc)
{
	const struct ttf_soc_stats *src = &ce_data->soc_stats;
	int i;

	for (i = first_soc; i <= last_soc; i++) {
		ktime_t elap;
		int cc;

		/* need to have data on both */
		if (!src->elap[i] || !src->cc[i])
			continue;

		/* average the elap time at soc */
		elap = ttf_soc_qual_elap(stats, ce_data, i, ttf_pwr_vtier_idx(stats, i));
		if (elap)
			stats->soc_stats.elap[i] = elap;

		/* average the coulumb count at soc */
		cc = ttf_soc_qual_cc(stats, ce_data, i);
		if (cc)
			stats->soc_stats.cc[i] = cc;
	}

	/* need to manually update cc[99] by adding ref_cc because it's not inclusive */
	if (last_soc == 98 && ttf_cc_check(&stats->soc_ref, 98))
		stats->soc_stats.cc[99] = stats->soc_stats.cc[98] +
					  (stats->soc_ref.cc[99] - stats->soc_ref.cc[98]);
}

void ttf_soc_init(struct ttf_soc_stats *dst)
{
	memset(dst, 0, sizeof(*dst));
}

/* Tier estimates ---------------------------------------------------------  */

#define TTF_STATS_FMT "[%d,%d %d %ld]"

#define BATT_TTF_TS_VALID(ts) \
	(ts->cc_total != 0 && ts->avg_time != 0)

/* TODO: adjust for adapter capability */
static ktime_t ttf_tier_accumulate(const struct ttf_tier_stat *ts, int vbatt_idx,
				  const struct batt_ttf_stats *stats)
{
	ktime_t estimate = 0;

	if (vbatt_idx >= GBMS_STATS_TIER_COUNT)
		return 0;

	for (; vbatt_idx < GBMS_STATS_TIER_COUNT; vbatt_idx++) {

		/* no data in this tier, sorry */
		if (!BATT_TTF_TS_VALID(ts))
			return -ENODATA;

		estimate += ts[vbatt_idx].avg_time;
	}

	return estimate;
}

static int ttf_tier_sscan(struct batt_ttf_stats *stats, const char *buff, size_t size)
{
	int j, len = 0;

	memset(&stats->tier_stats, 0, sizeof(stats->tier_stats));

	while (buff[len] != '[' && len < size)
		len++;

	for (j = 0; j < GBMS_STATS_TIER_COUNT; j++) {
		sscanf(&buff[len], TTF_STATS_FMT, &stats->tier_stats[j].soc_in,
		       &stats->tier_stats[j].cc_in,
		       &stats->tier_stats[j].cc_total,
		       &stats->tier_stats[j].avg_time);

		len += sizeof(TTF_STATS_FMT) - 1;
	}

	return 0;
}

int ttf_tier_cstr(char *buff, int size, const struct ttf_tier_stat *tier_stats)
{
	int len = 0;

	len += scnprintf(&buff[len], size - len,
			 TTF_STATS_FMT,
			 tier_stats->soc_in >> 8,
			 tier_stats->cc_in,
			 tier_stats->cc_total,
			 tier_stats->avg_time);
	return len;
}

/* average soc_in, cc_in, cc_total an and avg time for charge tier */
static void ttf_tier_update_stats(struct ttf_tier_stat *ttf_ts,
			    	  const struct gbms_ce_tier_stats *chg_ts,
				  bool force)
{
	int elap;

	if (!force) {
		if (chg_ts->cc_total == 0)
			return;

		/* TODO: check dsg, qualify with adapter? */
	}

	/* TODO: check with -1 */
	if (ttf_ts->soc_in == 0)
		ttf_ts->soc_in = chg_ts->soc_in;
	 ttf_ts->soc_in = (ttf_ts->soc_in + chg_ts->soc_in) / 2;

	/* TODO: check with -1 */
	if (ttf_ts->cc_in == 0)
		ttf_ts->cc_in = chg_ts->cc_in;
	 ttf_ts->cc_in = (ttf_ts->cc_in + chg_ts->cc_in) / 2;

	if (ttf_ts->cc_total == 0)
		 ttf_ts->cc_total = chg_ts->cc_total;
	 ttf_ts->cc_total = (ttf_ts->cc_total + chg_ts->cc_total) / 2;

	/* */
	elap = chg_ts->time_fast + chg_ts->time_taper + chg_ts->time_other;
	if (ttf_ts->avg_time == 0)
		ttf_ts->avg_time = elap;

	/* qualify time with ratio */
	ttf_ts->avg_time =(ttf_ts->avg_time + elap) / 2;
}

/* updated tier stats using the charging event
 * NOTE: the ce has data from 1+ charging voltage and temperature tiers */
static void ttf_tier_update(struct batt_ttf_stats *stats,
			    const struct gbms_charging_event *data,
			    bool force)
{
	int i;

	for (i = 0; i < GBMS_STATS_TIER_COUNT; i++) {
		const bool last_tier = i == (GBMS_STATS_TIER_COUNT - 1);
		const struct gbms_ce_tier_stats *chg_ts = &data->tier_stats[i];
		const struct gbms_ce_stats *chg_s = &data->charging_stats;
		long elap;

		/* skip data that has a temperature switch */
		if (chg_ts->temp_idx == -1)
			continue;
		/* or entries that have no actual charging */
		elap = chg_ts->time_fast + chg_ts->time_taper;
		if (!elap)
			continue;
		/* update first tier stats only at low soc_in */
		if (!force && i == 0 && (chg_ts->soc_in >> 8) > 1)
			continue;
		/* update last tier stats only at full */
		if (!force && last_tier && chg_s->ssoc_out != 100)
			continue;

		/*  */
		ttf_tier_update_stats(&stats->tier_stats[i], chg_ts, false);
	}

}

/* tier estimates only, */
int ttf_tier_estimate(ktime_t *res, const struct batt_ttf_stats *stats,
		      int temp_idx, int vbatt_idx,
		      int capacity, int full_capacity)
{
	ktime_t estimate = 0;
	const struct ttf_tier_stat *ts;

	/* tier estimates, only when in tier */
	if (vbatt_idx == -1 && temp_idx == -1)
		return -EINVAL;

	ts = &stats->tier_stats[vbatt_idx];
	if (!ts || !BATT_TTF_TS_VALID(ts))
		return -ENODATA;

	/* accumulate next tier */
	estimate = ttf_tier_accumulate(ts, vbatt_idx + 1, stats);
	if (estimate < 0)
		return -ENODATA;

	/* eyeball current tier
	 * estimate =
	 * 	(ts->cc_in + ts->cc_total - capacity) *
	 * 	rs->avg_time) / ts->cc_total
	 */

	/* TODO: adjust for crossing thermals? */

	*res = estimate;
	return 0;
}

/* ----------------------------------------------------------------------- */

/* QUAL DELTA >= 3 */
#define TTF_STATS_QUAL_DELTA_MIN	3
#define TTF_STATS_QUAL_DELTA		TTF_STATS_QUAL_DELTA_MIN

static int ttf_soc_cstr_elap(char *buff, int size,
			     const struct ttf_soc_stats *soc_stats,
			     int start, int end)
{
	int i, len = 0;

	len += scnprintf(&buff[len], size - len, "T%d:", start);
	for (i = start; i < end; i++)
		len += scnprintf(&buff[len], size - len, " %4ld", soc_stats->elap[i]);

	return len;
}

static int ttf_soc_cstr_cc(char *buff, int size,
			   const struct ttf_soc_stats *soc_stats,
			   int start, int end)
{
	int i, len = 0;

	len += scnprintf(&buff[len], size - len, "C%d:", start);
	for (i = start; i < end; i++)
		len += scnprintf(&buff[len], size - len, " %4d", soc_stats->cc[i]);

	return len;
}

/* update ttf tier and soc stats using the charging event.
 * call holding stats->lock
 */
void ttf_stats_update(struct batt_ttf_stats *stats,
		      struct gbms_charging_event *ce_data,
		      bool force)
{
	int first_soc = ce_data->charging_stats.ssoc_in;
	const int last_soc = ce_data->last_soc;
	const int delta_soc = last_soc - first_soc;
	const int limit = force ? TTF_STATS_QUAL_DELTA_MIN : TTF_STATS_QUAL_DELTA;
	const int tmp_size = PAGE_SIZE;
	char *tmp;

	/* skip data short periods */
	if (delta_soc < limit) {
		ttf_log(stats, "no updates delta_soc=%d, limit=%d, force=%d",
			delta_soc, limit, force);
		return;
	}

	/* ignore first nozero and last entry because they are partial */
	for ( ; first_soc <= last_soc; first_soc++)
		if (ce_data->soc_stats.elap[first_soc] != 0)
			break;

	ttf_soc_update(stats, ce_data, first_soc + 1, last_soc - 1);
	ttf_tier_update(stats, ce_data, force);

	/* dump update stats to logbuffer */
	tmp = kzalloc(tmp_size, GFP_KERNEL);
	if (tmp) {
		const int split = 10;
		int i;

		for (i = first_soc + 1;i < last_soc - 1; i += split) {
			int end_soc = i + split;

			if (end_soc > last_soc - 1)
				end_soc = last_soc - 1;

			ttf_soc_cstr_elap(tmp, tmp_size, &stats->soc_stats, i, end_soc);
			ttf_log(stats, "%s", tmp);
			ttf_soc_cstr_cc(tmp, tmp_size, &stats->soc_stats, i,  end_soc);
			ttf_log(stats, "%s", tmp);
		}

		kfree(tmp);
	}
}

static int ttf_as_default(struct ttf_adapter_stats *as, int i, int table_i)
{
	while (i > as->soc_table[table_i] && table_i < as->table_count)
		table_i++;

	return table_i;
}

static int ttf_init_soc_parse_dt(struct batt_ttf_stats *stats, struct device_node *node,
				 int capacity_ma)
{
	const int cc = (capacity_ma * 100) / GBMS_SOC_STATS_LEN;
	int table_count, ret, i, table_i = 0, elap_table_count;
	struct ttf_adapter_stats as;

	table_count = of_property_count_elems_of_size(node, "google,ttf-soc-table", sizeof(u32));
	elap_table_count = of_property_count_elems_of_size(node, "google,ttf-elap-table",
							   sizeof(u32));
	if (table_count <= 0)
		return -EINVAL;
	if (table_count != elap_table_count) {
		pr_err("ttf soc and elap table sizes do not match\n");
		return -EINVAL;
	}

	as.soc_table = kzalloc(table_count * 2 * sizeof(u32), GFP_KERNEL);
	if (!as.soc_table)
		return -ENOMEM;

	ret = of_property_read_u32_array(node, "google,ttf-soc-table", as.soc_table, table_count);
	if (ret < 0) {
		pr_err("cannot read google,ttf-soc-table %d\n", ret);
		kfree(as.soc_table);
		return ret;
	}

	as.elap_table = &as.soc_table[table_count];
	ret = of_property_read_u32_array(node, "google,ttf-elap-table",
					 as.elap_table, table_count);
	if (ret < 0) {
		pr_err("cannot read google,ttf-elap-table %d\n", ret);
		kfree(as.soc_table);
		return ret;
	}

	as.table_count = table_count;

	/* initialize the reference stats for the reference soc estimates */
	for (i = 0; i < GBMS_SOC_STATS_LEN; i++) {
		table_i = ttf_as_default(&as, i, table_i);

		stats->soc_ref.elap[i] = as.elap_table[table_i];

		/* assume same cc for each soc */
		stats->soc_ref.cc[i] = (cc * i) / 100;
	}

	kfree(as.soc_table);

	return 0;
}

int ttf_stats_sscan(struct batt_ttf_stats *stats, const char *buff, size_t size)
{
	/* TODO: scan ttf_soc_* data as well */

	return ttf_tier_sscan(stats, buff, size);
}

void ttf_tier_reset(struct batt_ttf_stats *stats)
{
	int i;

	for (i = 0; i < GBMS_STATS_TIER_COUNT; i++) {
		stats->tier_stats[i].cc_in = 0;
		stats->tier_stats[i].cc_total = 0;
		stats->tier_stats[i].avg_time = 0;
	}
}

static int ttf_init_tier_parse_dt(struct batt_ttf_stats *stats, struct device_node *node,
				  int capacity_ma)
{
	int i, count, ret;
	int16_t soc_in_diff;
	u32 tier_table[GBMS_STATS_TIER_COUNT];

	count = of_property_count_elems_of_size(node, "google,ttf-tier-table", sizeof(u32));
	if (count != GBMS_STATS_TIER_COUNT)
		return -EINVAL;

	ret = of_property_read_u32_array(node, "google,ttf-tier-table", tier_table, count);
	if (ret < 0) {
		pr_err("cannot read google,ttf-tier-table %d\n", ret);
		return ret;
	}

	for (i = 0; i < GBMS_STATS_TIER_COUNT; i++)
		stats->tier_stats[i].soc_in = tier_table[i] << 8;

	ttf_tier_reset(stats);

	soc_in_diff = stats->tier_stats[2].soc_in - stats->tier_stats[1].soc_in;

	/* TODO: use the soc stats to calculate cc_in */
	stats->tier_stats[0].cc_in = 0;
	stats->tier_stats[1].cc_in = (capacity_ma * stats->tier_stats[1].soc_in) / 100;
	stats->tier_stats[2].cc_in = (capacity_ma * stats->tier_stats[2].soc_in) / 100;

	/* TODO: use the soc stats to calculate cc_total */
	stats->tier_stats[0].cc_total = 0;
	stats->tier_stats[1].cc_total = capacity_ma * soc_in_diff / 100;
	stats->tier_stats[2].cc_total = capacity_ma - stats->tier_stats[2].cc_in;

	return 0;
}

/* clone and clear the stats */
struct batt_ttf_stats *ttf_stats_dup(struct batt_ttf_stats *dst, const struct batt_ttf_stats *src)
{
	memcpy(dst, src, sizeof(*dst));
	memset(&dst->soc_stats, 0, sizeof(dst->soc_stats));
	memset(&dst->tier_stats, 0, sizeof(dst->tier_stats));
	return dst;
}

/* must come after charge profile */
#define TTF_REPORT_MAX_RATIO	300
int ttf_stats_init(struct batt_ttf_stats *stats, struct device_node *node, int capacity_ma)
{
	u32 value;
	int ret;

	memset(stats, 0, sizeof(*stats));
	stats->ttf_fake = -1;
	stats->mdis_pwr_uw = -1;
	mutex_init(&stats->ttf_lock);

	/* reference adapter */
	ret = of_property_read_u32(node, "google,ttf-adapter", &value);
	if (ret < 0)
		return ret;

	stats->ref_watts = value;

	/* reference temperature  */
	ret = of_property_read_u32(node, "google,ttf-temp-idx", &value);
	if (ret < 0)
		return ret;

	stats->ref_temp_idx = value;

	/* max ratio to report ttf */
	ret = of_property_read_u32(node, "google,ttf-report-max-ratio", &value);
	if (ret < 0)
		value = TTF_REPORT_MAX_RATIO;

	stats->report_max_ratio = value;

	/* reference soc estimates */
	ret = ttf_init_soc_parse_dt(stats, node, capacity_ma);
	if (ret < 0)
		return ret;

	/* reference tier-based statistics */
	ret = ttf_init_tier_parse_dt(stats, node, capacity_ma);
	if (ret < 0)
		return ret;

	return 0;
}

/* tier and soc details */
ssize_t ttf_dump_details(char *buf, int max_size,
			 const struct batt_ttf_stats *ttf_stats,
			 int last_soc)
{
	int i, len = 0;

	/* interleave tier with SOC data */
	for (i = 0; i < GBMS_STATS_TIER_COUNT; i++) {
		int next_soc_in;

		len += scnprintf(&buf[len], max_size - len, "%d: ", i);
		len += ttf_tier_cstr(&buf[len], max_size - len, &ttf_stats->tier_stats[i]);
		len += scnprintf(&buf[len], max_size - len, "\n");

		/* continue only first */
		if (ttf_stats->tier_stats[i].avg_time == 0)
			continue;

		if (i == GBMS_STATS_TIER_COUNT - 1) {
			next_soc_in = -1;
		} else {
			next_soc_in = ttf_stats->tier_stats[i + 1].soc_in >> 8;
			if (next_soc_in == 0)
				next_soc_in = -1;
		}

		if (next_soc_in == -1)
			next_soc_in = last_soc - 1;

		len += ttf_soc_cstr(&buf[len], max_size - len, &ttf_stats->soc_stats,
				    ttf_stats->tier_stats[i].soc_in >> 8, next_soc_in);
	}

	return len;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
#define TTF_MAX_DEBUG_INPUT_SIZE 1024
#define TTF_TEST_STEPS_MAX 32

static ssize_t debug_ttf_elap_write(struct file *filp, const char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct batt_ttf_stats *stats = filp->private_data;
	char *buf;
	int i, consumed, soc, step_idx = 0;
	long long elap;
	const char *next;
	ssize_t ret;
	struct ttf_test_step {
		int soc;
		long long elap;
	} steps[TTF_TEST_STEPS_MAX];
	int step_count = 0;
	long long current_elap = 0;
	int prev_soc = -1;

	/* Limit input size to prevent excessive memory allocation */
	if (count > TTF_MAX_DEBUG_INPUT_SIZE)
		return -EINVAL;

	buf = memdup_user_nul(user_buf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	next = buf;
	while (step_count < ARRAY_SIZE(steps)) {
		/* skip spaces */
		while (*next == ' ' || *next == '\n' || *next == '\t')
			next++;
		if (*next == '\0')
			break;

		consumed = 0;
		if (sscanf(next, "%d:%lld%n", &soc, &elap, &consumed) != 2 || consumed == 0) {
			ret = -EINVAL;
			goto out;
		}
		if (soc < 0 || soc > 100 || soc <= prev_soc) {
			ret = -EINVAL;
			goto out;
		}
		steps[step_count].soc = soc;
		steps[step_count].elap = elap;
		prev_soc = soc;
		step_count++;
		next += consumed;
	}

	if (step_count == 0) {
		ret = -EINVAL;
		goto out;
	}

	mutex_lock(&stats->ttf_lock);
	for (i = 0; i < GBMS_SOC_STATS_LEN; i++) {
		if (step_idx < step_count && i >= steps[step_idx].soc) {
			current_elap = steps[step_idx].elap;
			step_idx++;
		}
		stats->soc_stats.elap[i] = current_elap;
	}
	mutex_unlock(&stats->ttf_lock);

	ret = count;
out:
	kfree(buf);
	return ret;
}
BATTERY_DEBUG_ATTRIBUTE(debug_ttf_elap_fops, NULL, debug_ttf_elap_write);

static ssize_t debug_ttf_capacity_write(struct file *filp, const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct batt_ttf_stats *stats = filp->private_data;
	int capacity_ma, cc, i, ret;

	/* Use helper to avoid manual copy and allocation */
	ret = kstrtoint_from_user(user_buf, count, 10, &capacity_ma);
	if (ret)
		return ret;

	if (capacity_ma <= 0)
		return -EINVAL;

	cc = (capacity_ma * 100) / GBMS_SOC_STATS_LEN;

	mutex_lock(&stats->ttf_lock);
	/* Matches `ttf_init_soc_parse_dt` calculation for consistency */
	for (i = 0; i < GBMS_SOC_STATS_LEN; i++)
		stats->soc_stats.cc[i] = (cc * i) / 100;
	mutex_unlock(&stats->ttf_lock);

	return count;
}
BATTERY_DEBUG_ATTRIBUTE(debug_ttf_capacity_fops, NULL, debug_ttf_capacity_write);

void ttf_init_debugfs(struct dentry *parent, struct batt_ttf_stats *stats)
{
	struct dentry *dir;

	if (IS_ERR_OR_NULL(parent) || !stats)
		return;

	dir = debugfs_create_dir("ttf", parent);
	if (IS_ERR_OR_NULL(dir))
		return;

	debugfs_create_file("capacity", 0200, dir, stats, &debug_ttf_capacity_fops);
	debugfs_create_file("elap", 0200, dir, stats, &debug_ttf_elap_fops);
}
#endif