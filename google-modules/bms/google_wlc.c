// SPDX-License-Identifier: GPL-2.0
/*
 * Google MPP Wireless Charging Driver
 *
 * Copyright 2023 Google LLC
 *
 */

#pragma clang diagnostic ignored "-Wenum-conversion"
#pragma clang diagnostic ignored "-Wswitch"

#include "google_wlc.h"

#include <linux/alarmtimer.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/completion.h>
#include <linux/pm.h>

#include <misc/gvotable.h>
#include <misc/logbuffer.h>

#include "google_bms_usecase.h"
#include "google_dc_pps.h"
#include "google_psy.h"
#include "google_wlc_chip.h"
#include "gbms_irq.h"

static int google_wlc_adjust_negotiated_power(struct google_wlc_data *charger);
static void google_wlc_stats_init(struct google_wlc_data *charger);
static void wlc_dump_charge_stats(struct google_wlc_data *charger);
static int wlc_chg_data_head_dump(char *buff, int max_size,
				  const struct google_wlc_stats *chg_data);
static int wlc_adapter_capabilities_dump(char *buff, int max_size,
					 const struct google_wlc_stats *chg_data);
static int wlc_soc_data_dump(char *buff, int max_size,
			     const struct google_wlc_stats *chg_data,
			     int index);
static void google_wlc_do_dploss_event(struct google_wlc_data *charger, int event);
static int google_wlc_set_dc_icl(struct google_wlc_data *charger, int icl);
static void google_wlc_set_eds_icl(struct google_wlc_data *charger, bool enable);
static bool google_wlc_eds_ready(struct google_wlc_data *charger);
static int set_eds_state(struct google_wlc_data *charger, enum eds_state state);
static void google_wlc_trigger_icl_ramp(struct google_wlc_data *charger, int delay);
static int check_mpp25_capabilities(struct google_wlc_data *charger);
static int google_wlc_ramp_down_icl(struct google_wlc_data *charger, const char *reason);
static int google_wlc_set_mode_gpio(struct google_wlc_data *charger, enum sys_op_mode mode);
static enum charger_switch_rc check_mpp25_eligible(struct google_wlc_data *charger);
static int dploss_try_cal_enter(struct google_wlc_data *charger);
static void google_wlc_setup_by_mode(struct google_wlc_data *charger, int mode);
static int google_wlc_max_power_by_mode_mw(struct google_wlc_data *charger, int mode);
static int google_wlc_max_current_by_mode_ma(struct google_wlc_data *charger, int mode);
static int google_wlc_max_voltage_by_mode_mv(struct google_wlc_data *charger, int mode);
static bool is_charger_mpp25_ok(struct wlc_chg_config *cap);
static void google_wlc_switch_powermode(struct google_wlc_data *charger,
					enum mpp_powermode pwrmode);
static int mpp25_setup(struct google_wlc_data *charger);
static int google_wlc_parse_mdis_table(struct device *dev, u32 *mdis, const char *of_name);
static void google_wlc_check_fsk_missing_offline(struct google_wlc_data *charger);
static int google_wlc_count_fsk_missing_offline(struct google_wlc_data *charger);
static int google_wlc_round_icl_to_target(struct google_wlc_data *charger, int icl);
static void google_wlc_wait_for_sending_ept(struct google_wlc_data *charger,
					    enum ept_reason reason);
static int google_wlc_update_adapter_type(struct google_wlc_data *charger);
static int google_wlc_thermal_icl_vote(struct google_wlc_data *charger);

static DEFINE_RATELIMIT_STATE(present_ratelimit, PRESENT_RATELIMIT_INTERVAL,
			      PRESENT_RATELIMIT_BURST);

/* Input buf should be len * 3 + 1, because: 2 hex character and then space and null at end */
size_t google_wlc_hex_str(const u8 *data, size_t len, char *buf, size_t max_buf, bool msbfirst)
{
	int i;
	int blen = 0;
	u8 val;

	for (i = 0; i < len; i++) {
		if (msbfirst)
			val = data[len - 1 - i];
		else
			val = data[i];
		blen += scnprintf(buf + (i * 3), max_buf - (i * 3), "%02x ", val);
	}
	return blen;
}

bool google_wlc_is_present(const struct google_wlc_data *charger)
{
	return charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED &&
	       charger->disable_state != WLC_HARD_DISABLE;
}

static void google_wlc_update_psy(struct google_wlc_data *charger)
{
	bool changed = false;
	int max_power, curr, volt;
	struct psy_info *psy = &charger->last_psy_info;
	union power_supply_propval prop;
	u8 mode;
	int ret;

	if (charger->online != psy->online) {
		changed = true;
		psy->online = charger->online;
	}
	ret = charger->chip->chip_get_sys_mode(charger, &mode);
	if (ret)
		mode = RX_MODE_AC_MISSING;

	max_power = google_wlc_max_power_by_mode_mw(charger, mode);
	if (max_power != -EAGAIN && max_power != psy->max_power) {
		psy->max_power = max_power;
		if (!charger->chgr_psy)
			charger->chgr_psy = power_supply_get_by_name(GOOGLE_WLCIN_MAINS_NAME);

		if (!charger->chgr_psy) {
			dev_err(charger->dev, "chgr psy not found");
			return;
		}

		/* Set invalid value to current first so the wrong power doesn't get calculated */
		prop.intval = -1;
		power_supply_set_property(charger->chgr_psy, POWER_SUPPLY_PROP_CURRENT_MAX, &prop);

		volt = MV_TO_UV(google_wlc_max_voltage_by_mode_mv(charger, mode));
		prop.intval = volt;
		power_supply_set_property(charger->chgr_psy, POWER_SUPPLY_PROP_VOLTAGE_MAX, &prop);
		curr = MA_TO_UA(google_wlc_max_current_by_mode_ma(charger, mode));
		prop.intval = curr;
		power_supply_set_property(charger->chgr_psy, POWER_SUPPLY_PROP_CURRENT_MAX, &prop);

		dev_info(charger->dev, "wlc psy set max power=%d, volt=%d, curr=%d",
			 psy->max_power, volt, curr);
	}
	if (changed) {
		dev_info(charger->dev, "wlc psy changed: online=%d", psy->online);
		power_supply_changed(charger->wlc_psy);
	}
}

bool mode_is_mpp(int mode)
{
	switch (mode) {
	case RX_MODE_WPC_MPP:
	case RX_MODE_WPC_MPP_NEGO:
	case RX_MODE_WPC_MPP_CPM:
	case RX_MODE_WPC_MPP_NPM:
	case RX_MODE_WPC_MPP_LPM:
	case RX_MODE_WPC_MPP_HPM:
		return true;
	default:
		return false;
	}
}

static bool mode_is_epp(int mode)
{
	if (mode == RX_MODE_WPC_EPP_NEGO || mode == RX_MODE_WPC_EPP)
		return true;

	return false;
}

static void google_wlc_check_mpp_initialized(struct google_wlc_data *charger)
{
	u32 opfreq;
	int ret;

	if (!charger->mpp_initialized && mode_is_mpp(charger->mode)) {
		ret = charger->chip->chip_get_opfreq(charger, &opfreq);
		if (ret == 0 && opfreq > GOOGLE_WLC_OPFREQ_THRES) {
			charger->mpp25.state = MPP25_CHECKING;
			mod_delayed_work(system_wq, &charger->charger_switch_ready_work, 0);
			charger->mpp_initialized = true;
			dev_dbg(charger->dev, "MPP initialized");
		}
	}
}

static bool wlc_is_ready_for_cloak(struct google_wlc_data *charger)
{
	switch (charger->mode) {
	case RX_MODE_WPC_MPP:
	case RX_MODE_WPC_MPP_CPM:
	case RX_MODE_WPC_MPP_NPM:
	case RX_MODE_WPC_MPP_LPM:
	case RX_MODE_WPC_MPP_HPM:
		break;
	default:
		return false;
	}

	google_wlc_check_mpp_initialized(charger);

	if (charger->mpp_initialized)
		return true;
	else
		return false;

}

static bool wlc_is_ready_for_pdet(const struct google_wlc_data *charger)
{
	switch (charger->mode) {
	case RX_MODE_WPC_BPP:
	case RX_MODE_WPC_EPP_NEGO:
	case RX_MODE_WPC_EPP:
	case RX_MODE_WPC_MPP_RESTRICTED:
		return true;
	default:
		return false;
	}
}

static bool is_usecase_wlc_dc(enum gsu_usecases uc)
{
	if (bms_usecase_is_uc_wireless(uc) && bms_usecase_is_uc_cp(uc))
		return true;
	return false;
}

static bool in_wlc_dc(const struct google_wlc_data *charger)
{
	return is_usecase_wlc_dc(charger->usecase);
}

static bool mpp25_running(int mpp25_state)
{
	return (mpp25_state == MPP25_DPLOSS_CALIBRATION || mpp25_state == MPP25_ENTER_HPM ||
		mpp25_state == MPP25_DPLOSS_CAL4 || mpp25_state == MPP25_ACTIVE);
}

static bool mpp25_ready(int mpp25_state)
{
	return (mpp25_state == MPP25_READY || mpp25_running(mpp25_state));
}

static bool is_fwtag_allow_update(struct google_wlc_data *charger)
{
	uint32_t fw_tag = 0, ver_tag = 0, curr_ver, store_ver;
	int ret;

	if (charger->fw_data.update_option != FWUPDATE_FORCE)
		return true;

	if (charger->fw_data.cur_ver.crc == 0)
		return true;

	ret = gbms_storage_read(GBMS_TAG_WLFW, &fw_tag, sizeof(fw_tag));
	if (ret < 0) {
		logbuffer_devlog(charger->fw_log, charger->dev, "fail to read fw_tag");
		return true;
	}

	curr_ver = charger->fw_data.cur_ver.major << 8 | charger->fw_data.cur_ver.minor;
	store_ver = fw_tag & 0xFFFF;
	if (curr_ver == store_ver)
		ver_tag = fw_tag >> 16;

	return charger->fw_data.ver_tag > ver_tag;
}

static int mpp_get_current_powermode(struct google_wlc_data *charger)
{
	int curr_powermode;
	u8 val8;
	int ret;

	ret = charger->chip->chip_get_sys_mode(charger, &val8);
	if (ret != 0)
		return MPP_POWERMODE_NONE;

	switch (charger->mode) {
	case RX_MODE_WPC_MPP_CPM:
		curr_powermode = MPP_POWERMODE_CONTINUOUS;
		break;
	case RX_MODE_WPC_MPP_HPM:
		curr_powermode = MPP_POWERMODE_HIGH;
		break;
	case RX_MODE_WPC_MPP_LPM:
		curr_powermode = MPP_POWERMODE_LIGHT;
		break;
	case RX_MODE_WPC_MPP_NPM:
		curr_powermode = MPP_POWERMODE_NOMINAL;
		break;
	case RX_MODE_WPC_MPP:
		curr_powermode = MPP_POWERMODE_DEFAULT;
		break;
	default:
		curr_powermode = MPP_POWERMODE_NONE;
		break;
	}
	return curr_powermode;
}

static int google_wlc_get_adapter_type(struct google_wlc_data *charger)
{
	switch (charger->mode) {
	case RX_MODE_WPC_BPP:
		charger->ad_type = CHG_EV_ADAPTER_TYPE_WPC_BPP;
		break;
	case RX_MODE_WPC_EPP:
		charger->ad_type = CHG_EV_ADAPTER_TYPE_WPC_EPP;
		break;
	case RX_MODE_WPC_MPP:
		charger->ad_type = CHG_EV_ADAPTER_TYPE_WPC_MPP;
		break;
	case RX_MODE_WPC_MPP_CPM:
	case RX_MODE_WPC_MPP_HPM:
	case RX_MODE_WPC_MPP_LPM:
	case RX_MODE_WPC_MPP_NPM:
	case RX_MODE_MPP_CLOAK:
		mutex_lock(&charger->stats_lock);
		if (charger->chg_data.adapter_type == 0)
			google_wlc_update_adapter_type(charger);

		if (charger->chg_data.adapter_type == AD_TYPE_WPC_MPP25)
			charger->ad_type = CHG_EV_ADAPTER_TYPE_WPC_MPP25;
		else
			charger->ad_type = CHG_EV_ADAPTER_TYPE_WPC_MPP;
		mutex_unlock(&charger->stats_lock);
		break;
	default:
		/* should not change charger->ad_type */
		break;
	}
	return charger->ad_type;
}

static void google_wlc_handle_retail_charge(struct google_wlc_data *charger)
{
	if (charger->feature & WLCF_RETAIL_CHARGE &&
	    charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED) {
		if (!charger->mpp25_disabled) {
			charger->mpp25_disabled = true;
			logbuffer_devlog(charger->log, charger->dev,
					 "MPP25: Disable due to retail mode");
		}

		if (!charger->dc_avail_votable)
			charger->dc_avail_votable = gvotable_election_get_handle("DC_AVAIL");

		if (charger->last_capacity < WLC_RETAIL_LOWSOC_THRESHOLD) {
			charger->retail_power_limit = false;
			if (charger->dc_avail_votable)
				gvotable_cast_int_vote(charger->dc_avail_votable, WLC_RETAIL_VOTER,
						       GBMS_ALL_CP_CHG_DISABLED, false);
			google_wlc_thermal_icl_vote(charger);
		} else {
			charger->retail_power_limit = true;
			if (charger->dc_avail_votable)
				gvotable_cast_int_vote(charger->dc_avail_votable, WLC_RETAIL_VOTER,
						       GBMS_ALL_CP_CHG_DISABLED, true);
			google_wlc_thermal_icl_vote(charger);
		}
	}
}

static void google_wlc_vote_csi_auth(struct google_wlc_data *charger, bool enable)
{
	if (!charger->csi_status_votable)
		charger->csi_status_votable = gvotable_election_get_handle(VOTABLE_CSI_STATUS);
	if (!charger->csi_status_votable)
		return;

	dev_info(charger->dev, "voting csi auth: %d", enable);
	gvotable_cast_compound_vote(charger->csi_status_votable,
				    "CSI_STATUS_ADA_AUTH",
				    0,
				    CSI_STATUS_Adapter_Auth,
				    enable);
}

static void google_wlc_set_qi_id(struct google_wlc_data *charger)
{
	charger->chip->chip_set_qi_id(charger, charger->qi_id);
}

static int feature_update(struct google_wlc_data *charger, u64 value)
{
	if (value & WLCF_QI_PASSED_FEATURE)
		charger->feature |= WLCF_QI_PASSED_FEATURE;
	else
		charger->feature &= ~WLCF_QI_PASSED_FEATURE;

	if (value & WLCF_DREAM_DEFEND)
		charger->feature |= WLCF_DREAM_DEFEND;
	else
		charger->feature &= ~WLCF_DREAM_DEFEND;

	/* keep until reboot */
	if (value & WLCF_RETAIL_CHARGE)
		charger->feature |= WLCF_RETAIL_CHARGE;

	google_wlc_handle_retail_charge(charger);

	if (charger->feature & WLCF_QI_PASSED_FEATURE) {
		logbuffer_devlog(charger->log, charger->dev, "Auth passed");
		mod_delayed_work(system_wq, &charger->charger_switch_ready_work,
						msecs_to_jiffies(0));
		set_eds_state(charger, EDS_AVAILABLE);
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_AUTH_VOTER,
					0, false);
		google_wlc_update_psy(charger);
		google_wlc_vote_csi_auth(charger, false);
		google_wlc_set_qi_id(charger);
	}

	if (value & WLCF_DREAM_DEFER)
		charger->feature |= WLCF_DREAM_DEFER;
	else
		charger->feature &= ~WLCF_DREAM_DEFER;

	return 0;
}

static bool wpc_auth_passed(struct google_wlc_data *charger)
{
	return (charger->auth_disable || (charger->feature & WLCF_QI_PASSED_FEATURE));
}

static void google_wlc_uevent(struct google_wlc_data *charger, u8 id)
{
	char source[UEVENT_ENVP_LEN];
	char *envp[] = {source, NULL};
	int ret = 0;
	bool skip = false;

	/*
	 * allow first WLC_ON and skip on subsequent ones when eds not ready
	 * and on repeated consecutive WLC_OFF
	 */

	if (id == UEVENT_WLC_OFF) {
		charger->eds_event = false;
		if (charger->last_charging_uevent == id)
			skip = true;
	} else if (id == UEVENT_WLC_ON && charger->last_charging_uevent != UEVENT_WLC_OFF) {
		if (charger->last_charging_uevent == id || !google_wlc_eds_ready(charger))
			skip = true;
	}

	if (!skip) {
		if (id == UEVENT_WLC_ON || id == UEVENT_WLC_OFF)
			charger->last_charging_uevent = id;
		scnprintf(source, sizeof(source), "SOURCE=%s", uevent_source_str[id]);
		ret = kobject_uevent_env(&charger->dev->kobj, KOBJ_CHANGE, envp);
	}
	logbuffer_devlog(charger->log, charger->dev,
			 "uevent %s, ret: %d, skip: %d", ret == 0 ? uevent_source_str[id] : "err",
			 ret, skip);
}

static int google_wlc_external_boost_en(struct google_wlc_data *charger, bool enable)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(charger->pdata->ext_bst_supply))
		return 0;

	mutex_lock(&charger->ext_boost_lock);
	if (enable == charger->ext_boost_enabled) {
		mutex_unlock(&charger->ext_boost_lock);
		return 0;
	}
	if (enable)
		ret = regulator_enable(charger->pdata->ext_bst_supply);
	else
		ret = regulator_disable(charger->pdata->ext_bst_supply);

	if (ret < 0)
		dev_err(charger->dev, "Error enabling regulator ret:%d\n", ret);
	else
		dev_info(charger->dev, "external boost en: %d", enable);

	charger->ext_boost_enabled = enable;
	mutex_unlock(&charger->ext_boost_lock);

	return ret;
}

static int google_wlc_set_vdden(struct google_wlc_data *charger, int value)
{
	if (IS_ERR_OR_NULL(charger->pdata->vdden_gpio))
		return -ENODEV;

	mutex_lock(&charger->vdden_lock);
	dev_info(charger->dev, "vdden en: %d", value);
	gpiod_set_value_cansleep(charger->pdata->vdden_gpio, value);
	mutex_unlock(&charger->vdden_lock);

	return 0;
}

static bool google_wlc_defer_online(struct google_wlc_data *charger)
{
	u8 mode = charger->mode;
	int ret = 0;

	if (!(charger->feature & WLCF_DREAM_DEFER))
		return false;

	if (charger->txsrc) {
		logbuffer_devlog(charger->log, charger->dev, "No online for battery mode");
		return true;
	}

	if (mode == RX_MODE_AC_MISSING)
		ret = charger->chip->chip_get_sys_mode(charger, &mode);

	if ((mode_is_mpp(mode) || ret != 0 || mode == RX_MODE_AC_MISSING)
	    && charger->last_opfreq < GOOGLE_WLC_OPFREQ_THRES && charger->tx_id == 0) {
		logbuffer_devlog(charger->log, charger->dev, "Defer online for mpp mode");
		return true;
	}
	return false;
}

static void google_wlc_set_online(struct google_wlc_data *charger)
{
	if (charger->online == 1)
		return;
	if (google_wlc_defer_online(charger))
		return;
	logbuffer_devlog(charger->log, charger->dev, "Set online");
	if (!wpc_auth_passed(charger))
		google_wlc_vote_csi_auth(charger, true);
	charger->online = 1;
	charger->ad_type = CHG_EV_ADAPTER_TYPE_WLC;
	charger->online_at = get_boot_sec();
	google_wlc_update_psy(charger);

	mutex_lock(&charger->stats_lock);
	google_wlc_stats_init(charger);
	mutex_unlock(&charger->stats_lock);

	charger->chip->chip_set_dynamic_mod(charger);

	if (!charger->hda_tz_votable)
		charger->hda_tz_votable = gvotable_election_get_handle(VOTABLE_HDA_TZ);
	if (charger->hda_tz_votable)
		gvotable_cast_int_vote(charger->hda_tz_votable, WLC_VOTER,
				       HDA_TZ_WLC_ADAPTER, true);
	schedule_delayed_work(&charger->charge_stats_hda_work,
			      msecs_to_jiffies(WLC_CHARGE_STATS_TIMEOUT_MS));
}

static void google_wlc_set_offline(struct google_wlc_data *charger)
{
	if (charger->online == 0)
		return;

	cancel_delayed_work(&charger->charger_switch_ready_work);
	logbuffer_devlog(charger->log, charger->dev, "Set offline");
	if (charger->disconnect_count > 0)
		charger->disconnect_count--;
	logbuffer_devlog(charger->log, charger->dev, "Disconnect count=%d, total=%d",
			 charger->disconnect_count, charger->disconnect_total_count);
	charger->online = 0;
	charger->online_at = 0;
	charger->disconnect_count = 0;
	charger->last_opfreq = 0;
	charger->cloak_err_count = 0;
	charger->run_fwupdate = 0;
	gvotable_cast_int_vote(charger->dc_avail_votable, WLC_VOTER,
				       GBMS_ACTIVE_CHG_DISABLE, false);
	if (charger->mpp25_disabled && charger->status == GOOGLE_WLC_STATUS_NOT_DETECTED)
		charger->mpp25_disabled = false;
	google_wlc_update_psy(charger);

	cancel_delayed_work(&charger->charge_stats_hda_work);
	wlc_dump_charge_stats(charger);

	gvotable_cast_int_vote(charger->hda_tz_votable, WLC_VOTER, 0, false);

	/* reset the mode_gpio pin */
	if (charger->fw_data.update_check_pending)
		mod_delayed_work(system_wq, &charger->wlc_fw_update_work,
				 msecs_to_jiffies(WLC_FW_CHECK_TIMEOUT_MS));

	if (charger->pdata->support_epp && !charger->lower_qi_version)
		gpiod_set_value_cansleep(charger->pdata->qi_version_gpio, 1);

	if (charger->manual_force_bpp)
		return;

	if (charger->pdata->support_epp) {
		if ((charger->mitigate_threshold > 0 &&
		    charger->last_capacity < charger->mitigate_threshold) ||
		    charger->mitigate_threshold == 0)
			google_wlc_set_mode_gpio(charger, RX_MODE_WPC_EPP);
	} else if (!charger->pdata->support_epp) {
		google_wlc_set_mode_gpio(charger, RX_MODE_WPC_MPP);
	}
	google_wlc_vote_csi_auth(charger, false);
}

static int google_wlc_has_dc_in(struct google_wlc_data *charger)
{
	union power_supply_propval prop;
	int ret;

	if (!charger->chgr_psy)
		charger->chgr_psy = power_supply_get_by_name(GOOGLE_WLCIN_MAINS_NAME);
	if (!charger->chgr_psy)
		return -EINVAL;

	ret = power_supply_get_property(charger->chgr_psy,
					POWER_SUPPLY_PROP_PRESENT, &prop);
	if (ret < 0) {
		dev_err(charger->dev,
			"Error getting charging status: %d\n", ret);
		return -EINVAL;
	}

	return prop.intval != 0;
}

static bool google_wlc_confirm_online(struct google_wlc_data *charger)
{
	union power_supply_propval prop;
	int ret;
	u8 mode_reg;

	ret = charger->chip->chip_get_sys_mode(charger, &mode_reg);
	if (ret == 0 && ((mode_reg != RX_MODE_AC_MISSING && mode_reg != RX_MODE_UNKNOWN) ||
	    !charger->mode_ready))
		return true;

	/* i2c is gone, but check dc too in case of transient i2c issue */
	if (!charger->chgr_psy)
		charger->chgr_psy = power_supply_get_by_name(GOOGLE_WLCIN_MAINS_NAME);

	if (!charger->chgr_psy)
		return false;

	/* Return true if dc is preesent */
	return power_supply_get_property(charger->chgr_psy, POWER_SUPPLY_PROP_PRESENT,
					 &prop) == 0 && prop.intval != 0;
}


static int google_wlc_set_dc_icl(struct google_wlc_data *charger, int icl)
{
	int ret;

	if (!charger->dc_icl_votable) {
		charger->dc_icl_votable =
			gvotable_election_get_handle(DC_ICL_VOTABLE_HANDLE);
		if (!charger->dc_icl_votable) {
			dev_err(charger->dev,
				"Could not get votable: DC_ICL\n");
			return -ENODEV;
		}
	}

	dev_dbg(charger->dev, "Voting ICL %duA\n", icl);

	ret = gvotable_cast_int_vote(charger->dc_icl_votable, WLC_VOTER,
				     icl, true);
	if (ret)
		dev_err(charger->dev, "Could not vote DC_ICL (%d)\n",
			ret);

	return ret;
}

static u32 google_wlc_map_eds_icl(struct google_wlc_data *charger, int capacity)
{
	int num = charger->pdata->mpp_eds_level_num, msc_last = 1;
	u32 val = 0;

	if (num == 0 || !mode_is_mpp(charger->mode))
		return val;

	if (!charger->msc_last_votable)
		charger->msc_last_votable = gvotable_election_get_handle(VOTABLE_MSC_LAST);
	if (charger->msc_last_votable)
		msc_last = gvotable_get_current_int_vote(charger->msc_last_votable);

	if (msc_last == 0)
		return val;

	for (int i = 0; i < num; i++) {
		if (capacity > charger->pdata->mpp_eds_soc[i])
			val = charger->pdata->mpp_eds_icl[i];
		else
			return val;
	}

	return val;
}

static void google_wlc_set_eds_icl(struct google_wlc_data *charger, bool enable)
{
	u32 icl = 0, pwr = 0;

	if (enable) {
		icl = google_wlc_map_eds_icl(charger, charger->last_capacity);
		if (!google_wlc_eds_ready(charger) || icl == 0)
			return;

		if (charger->chip->val_default_vout > 0)
			pwr = UA_TO_MA(icl) * (charger->chip->val_default_vout / 1000);
		else
			pwr = UA_TO_MA(icl) * (GOOGLE_WLC_VOUT_MIN / 1000);
	}

	gvotable_cast_long_vote(charger->icl_ramp_target_votable, EDS_VOTER, pwr, enable);

	if (enable) {
		if (charger->eds_stream == EDS_FW_UPDATE)
			mod_delayed_work(system_wq, &charger->eds_work,
					 msecs_to_jiffies(FW_EDS_INTERVAL_MS));
		else
			mod_delayed_work(system_wq, &charger->eds_work,
					 msecs_to_jiffies(AUTH_EDS_INTERVAL_MS));
	} else {
		cancel_delayed_work(&charger->eds_work);
	}
	charger->inlim_available = !enable;
}

static int google_wlc_exit_mpp25(struct google_wlc_data *charger)
{
	int ret = 0;
	bool npm = mpp_get_current_powermode(charger) == MPP_POWERMODE_HIGH &&
		   !charger->wlc_dc_skip_powermode && charger->status != GOOGLE_WLC_STATUS_CLOAK &&
		   charger->status != GOOGLE_WLC_STATUS_CLOAK_ENTERING;

	if (charger->mpp25.state == MPP25_OFF) {
		dev_info(charger->dev, "MPP25 already off");
		return 0;
	}

	cancel_delayed_work(&charger->mpp25_timeout_work);
	if (charger->mpp25.dploss_step != 0)
		google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
	if (charger->wait_for_cal_enter)
		complete(&charger->cal_enter_done);

	charger->mpp25.fod_cloak = FOD_CLOAK_NONE;
	charger->mpp25.cal_enter_ok = false;
	charger->mpp25.dploss_cal_ok = false;
	charger->mpp25.cal4_ok = false;
	if (charger->mpp25.state != MPP25_CHECKING)
		charger->mpp25.state = MPP25_OFF;
	google_wlc_update_psy(charger);
	if (charger->pdata->has_mpp25_wlc_dc) {
		charger->dc_data.swc_en_state = SWC_NOT_ENABLED;
		gvotable_cast_long_vote(charger->wlc_dc_power_votable, MODE_VOTER, 0, true);
	}
	if (npm) {
		google_wlc_switch_powermode(charger, MPP_POWERMODE_NOMINAL);
	} else {
		/* Reset mode's max power back to 15W */
		google_wlc_setup_by_mode(charger, charger->mode);
	}

	logbuffer_devlog(charger->log, charger->dev, "MPP25: Exit");
	__pm_relax(charger->mpp25_ws);
	return ret;

}

static void google_wlc_disable_current_charger(struct google_wlc_data *charger)
{
	if (!charger->dc_avail_votable)
		charger->dc_avail_votable = gvotable_election_get_handle("DC_AVAIL");
	if (charger->dc_avail_votable)
		gvotable_cast_int_vote(charger->dc_avail_votable, WLC_VOTER,
				       GBMS_ACTIVE_CHG_DISABLE, true);
}

/* Call with status_lock held */
static void google_wlc_disable_mpp25(struct google_wlc_data *charger, bool stay)
{
	if (!google_wlc_is_present(charger))
		return;

	if (!charger->pdata->has_mpp25_wlc_dc)
		stay = false;

	if (stay && charger->dc_data.swc_en_state != SWC_ENABLED) {
		dev_info(charger->dev, "MPP25: Re-enable SWC");
		if (GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_ENABLE_SWITCH_CAP,
				  CP_ENABLE_SWITCH_CAP) != 0) {
			stay = false;
			dev_err(charger->dev, "MPP25: Failed to re-enable SWC");
		} else {
			charger->dc_data.swc_en_state = SWC_ENABLED;
		}
	}

	if (charger->output_chg_config->mpp25_only && !stay)
		google_wlc_disable_current_charger(charger);
	if (!charger->mpp25_disabled) {
		charger->mpp25.cal_active = 0;
		charger->mpp25_disabled = true;
		if (stay)
			schedule_delayed_work(&charger->mpp25_init_work, 0);
	}
	if (charger->wait_for_cal_enter)
		complete(&charger->cal_enter_done);
	gvotable_cast_long_vote(charger->icl_ramp_target_votable, DPLOSS_VOTER, 0, false);
	logbuffer_devlog(charger->log, charger->dev, "MPP25: Set Disabled, stay: %d", stay);
}

static int google_wlc_wait_for_voltage(struct google_wlc_data *charger, int voltage)
{
	u32 vout;
	int ret;
	int retries = MPP25_VOUT_STABLE_CHECK_RETRIES;
	int thresh = MPP25_VOUT_STABLE_CHECK_THRESHOLD_MV;

	for (int i = 0; i < retries; i++) {
		ret = charger->chip->chip_get_vout(charger, &vout);

		if (ret != 0)
			return -EINVAL;

		if (!charger->wlc_charge_enabled && !in_wlc_dc(charger)) {
			dev_err(charger->dev, "Charging not enabled");
			return -EINVAL;
		}

		if (vout > voltage - thresh) {
			dev_info(charger->dev, "Voltage wait done, vout = %u", vout);
			return 0;
		}
		dev_dbg(charger->dev,
			"Waiting for voltage stable %d. vout = %u, target = %u", i, vout, voltage);
		msleep(WLC_CHARGER_STARTUP_CHECK_INTERVAL);
	}
	dev_err(charger->dev, "voltage not stable after %d checks, fail. vout = %u",
		retries, vout);
	return -EINVAL;
}

static int google_wlc_dploss_cal_ramp(struct google_wlc_data *charger)
{
	int ret = 0;
	int target, volt;
	struct mpp25_data *mpp25 = &charger->mpp25;
	const int step = mpp25->dploss_step;

	if (mpp25->state == MPP25_DPLOSS_CAL4 && step == 4) {
		target = charger->mpp25_dploss_cal4;
		volt = charger->mpp25_dploss_cal4_volt;
	} else if (mpp25->state == MPP25_DPLOSS_CALIBRATION &&
		step >= 1 && step <= MPP_DPLOSS_NUM_STEPS) {
		target = charger->pdata->dploss_steps[step - 1];
		volt = charger->pdata->dploss_volts[step - 1];
	} else {
		dev_err(charger->dev, "DPLOSS: step invalid %d for state %d",
			step, mpp25->state);
		if (charger->dc_data.swc_en_state == SWC_ENABLED)
			GPSY_SET_PROP(charger->wlc_dc_psy,
				      GBMS_PROP_MPP_DPLOSS_CALIBRATION_LIMIT, 0);
		return -EINVAL;
	}

	if (!charger->pdata->has_mpp25_wlc_dc) {
		charger->chip->chip_set_vout(charger, volt);
		charger->mpp25.dploss_comp = 0;
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, DPLOSS_VOTER,
					target, true);
		schedule_delayed_work(&charger->mpp25_init_work, 0);
		return ret;
	}
	ret = GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_MPP_DPLOSS_CALIBRATION_LIMIT, target);
	dev_info(charger->dev, "DPLOSS: Set calibration limit %d", target);
	if (ret != 0)
		dev_err(charger->dev, "DPLOSS: Error setting up cal ramp");
	if (target > 0 && charger->dc_data.swc_en_state != SWC_ENABLED) {
		ret = GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_ENABLE_SWITCH_CAP,
				    CP_ENABLE_SWITCH_CAP);
		if (ret != 0)
			dev_err(charger->dev, "DPLOSS: Error enabling swc");
		else
			charger->dc_data.swc_en_state = SWC_ENABLED;
	}
	return ret;
}

static void google_wlc_handle_chg_stable(struct google_wlc_data *charger,
					 enum google_wlc_chg_stable_type type,
					 int time)
{
	dev_info(charger->dev, "handle chg stable: count=%d, type=%d, time=%d\n",
		 charger->disconnect_total_count, type, time);

	if (charger->disconnect_total_count > 0) {
		if (delayed_work_pending(&charger->chg_stable_work) &&
		    charger->chg_stable_type == type) {
			dev_info(charger->dev,
				 "chg stable type %d unchanged and work pending, skip mod", type);
			return;
		}

		charger->chg_stable_type = type;
		__pm_stay_awake(charger->chg_stable_ws);
		mod_delayed_work(system_wq, &charger->chg_stable_work,
				 msecs_to_jiffies(time));
	}
}

static void google_wlc_do_dploss_event(struct google_wlc_data *charger, int event)
{
	const int last_event = charger->mpp25.last_dploss_event;
	struct mpp25_data *mpp25 = &charger->mpp25;
	int ret = 0;

	if (event != DPLOSS_CAL_RESPONSE)
		logbuffer_devlog(charger->log, charger->dev, "DPLOSS: event: %s->%s",
				 dploss_cal_event_str[last_event],
				 dploss_cal_event_str[event]);
	else
		logbuffer_devlog(charger->log, charger->dev, "DPLOSS: %s success",
				 dploss_cal_event_str[last_event]);

	switch (event) {
	case DPLOSS_CAL_START:
		mpp25->last_dploss_event = DPLOSS_CAL_START;
		if (mpp25->state == MPP25_DPLOSS_CAL4) {
			mpp25->dploss_step = 4;
			ret = google_wlc_dploss_cal_ramp(charger);
			if (ret < 0)
				goto err;
			break;
		}
		mpp25->dploss_step = 1;
		ret = google_wlc_dploss_cal_ramp(charger);
		if (ret < 0)
			goto err;
		break;
	case DPLOSS_CAL_ENTER:
		mpp25->last_dploss_event = DPLOSS_CAL_ENTER;
		charger->chip->chip_do_dploss_event(charger, event);
		break;
	case DPLOSS_CAL_EXTEND:
		if (last_event != DPLOSS_CAL_START)
			goto err;
		mpp25->last_dploss_event = DPLOSS_CAL_EXTEND;
		charger->chip->chip_do_dploss_event(charger, event);
		break;
	case DPLOSS_CAL_RESPONSE:
		mpp25->dploss_event_success = true;
		if (last_event == DPLOSS_CAL_ENTER) {
			mpp25->fod_cloak = FOD_CLOAK_NONE;
			mpp25->cal_enter_ok = true;
			if (charger->wait_for_cal_enter)
				complete(&charger->cal_enter_done);
		} else if (last_event == DPLOSS_CAL_EXTEND) {
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_CAPTURE);
		} else if (last_event == DPLOSS_CAL_CAPTURE) {
			dev_info(charger->dev, "DPLOSS: Cal capture %d is finished",
				 mpp25->dploss_step);
			const int max_cal = mpp25->state == MPP25_DPLOSS_CAL4 ?
								4 : MPP_DPLOSS_NUM_STEPS;

			if (mpp25->dploss_step >= max_cal) {
				google_wlc_do_dploss_event(charger, DPLOSS_CAL_COMMIT);
			} else {
				mpp25->dploss_step += 1;
				ret = google_wlc_dploss_cal_ramp(charger);
				if (ret < 0)
					goto err;
			}
		}  else if (last_event == DPLOSS_CAL_COMMIT) {
			dev_info(charger->dev, "DPLOSS: Cal commit accepted");
			if (mpp25->state == MPP25_DPLOSS_CAL4) {
				google_wlc_do_dploss_event(charger, DPLOSS_CAL_EXIT);
				break;
			}
			schedule_delayed_work(&charger->mpp25_init_work, 0);
		} else if (last_event == DPLOSS_CAL_EXIT) {
			mpp25->dploss_step = 0;
			mpp25->cal_active = 0;
			logbuffer_devlog(charger->log, charger->dev,
					 "DPLOSS: %s Cal success",
					 mpp25->state == MPP25_DPLOSS_CAL4 ? "4th " : "Initial");
			charger->mpp25.dploss_cal_ok = true;
			if (mpp25->state == MPP25_DPLOSS_CAL4)
				mpp25->cal4_ok = true;
			gvotable_cast_long_vote(charger->icl_ramp_target_votable,
				DPLOSS_VOTER, 0, false);
			schedule_delayed_work(&charger->mpp25_init_work, 0);
		} else {
			goto err;
		}
		break;
	case DPLOSS_CAL_CAPTURE:
		if (last_event != DPLOSS_CAL_START &&
		    ((last_event != DPLOSS_CAL_EXTEND && last_event != DPLOSS_CAL_CAPTURE) ||
			!mpp25->dploss_event_success))
			goto err;
		mpp25->last_dploss_event = DPLOSS_CAL_CAPTURE;
		charger->chip->chip_do_dploss_event(charger, event);
		break;
	case DPLOSS_CAL_COMMIT:
		if (last_event != DPLOSS_CAL_CAPTURE || !mpp25->dploss_event_success)
			goto err;
		mpp25->last_dploss_event = DPLOSS_CAL_COMMIT;
		reinit_completion(&charger->cal_renego_done);
		charger->wait_for_cal_renego = true;
		charger->chip->chip_do_dploss_event(charger, event);
		break;
	case DPLOSS_CAL_EXIT:
		if (last_event != DPLOSS_CAL_COMMIT || !mpp25->dploss_event_success)
			goto err;
		mpp25->last_dploss_event = DPLOSS_CAL_EXIT;
		charger->chip->chip_do_dploss_event(charger, event);
		break;
	case DPLOSS_CAL_ABORT:
		if (mpp25->state == MPP25_DPLOSS_CAL4) {
			dev_info(charger->dev, "MPP25: Cal 4 aborted, resume MPP25");
			mpp25->cal4_ok = true;
			schedule_delayed_work(&charger->mpp25_init_work, 0);
		}
		charger->chip->chip_do_dploss_event(charger, event);
		charger->mpp25.last_dploss_event = DPLOSS_CAL_NONE;
		charger->mpp25.dploss_event_success = false;
		charger->mpp25.dploss_step = 0;
		charger->mpp25.cal_active = 0;
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, DPLOSS_VOTER,
					0, false);
		break;
	default:
		goto err;
	}
	if (event != DPLOSS_CAL_RESPONSE)
		mpp25->dploss_event_success = false;
	return;
err:
	dev_err(charger->dev, "DPLOSS: Error during: %s->%s, ack=%d",
		dploss_cal_event_str[last_event], dploss_cal_event_str[event],
		!mpp25->dploss_event_success);
	if (mpp25->state == MPP25_DPLOSS_CAL4) {
		dev_err(charger->dev, "DPLOSS: Aborting 4th cal step due to error");
		google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
	} else {
		dev_err(charger->dev, "DPLOSS: Disabling MPP25 due to calibration error");
		google_wlc_disable_mpp25(charger, true);
	}
}

static void google_wlc_clear_target(struct google_wlc_data *charger)
{
	gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_AUTH_VOTER, 0, false);
	gvotable_cast_long_vote(charger->icl_ramp_target_votable, NEGO_VOTER, 0, false);
	gvotable_cast_long_vote(charger->wlc_dc_power_votable, NEGO_VOTER, 0, false);
	charger->nego_power = 0;
}

static int google_wlc_adjust_negotiated_power(struct google_wlc_data *charger)
{
	int power, ret;
	u8 val = 0;
	bool skip, changed = false;

	ret = charger->chip->chip_get_negotiated_power(charger, &power);
	if (ret) {
		dev_err(charger->dev, "Failed to read negotiated power\n");
		return ret;
	}

	if (power == 0 || (!mode_is_mpp(charger->mode) && !mode_is_epp(charger->mode))) {
		google_wlc_clear_target(charger);
		logbuffer_devlog(charger->log, charger->dev,
				"Negotiated power: none");
		return ret;
	}

	ret = charger->chip->chip_get_limit_rsn(charger, &val);
	if (ret)
		dev_warn(charger->dev, "Failed to read limit reason\n");
	else if (charger->limit_reason != val) {
		charger->limit_reason = val;
		changed = true;
	}

	if (power < GOOGLE_WLC_MPP_MAX_POWER &&
	    (val == POWER_LIMIT_NO_LIMIT || val == POWER_LIMIT_CAL_NOT_MET)) {
		__pm_stay_awake(charger->limit_reason_ws);
		mod_delayed_work(system_wq, &charger->limit_reason_work,
				 msecs_to_jiffies(LIMIT_REASON_RETRY_TIME_MS));
		charger->limit_reason_retry_count = 1;
	} else {
		charger->limit_reason_retry_count = LIMIT_REASON_RETRY_COUNT;
		cancel_delayed_work_sync(&charger->limit_reason_work);
		__pm_relax(charger->limit_reason_ws);
	}

	skip = (charger->skip_nego == SKIP_ALL_BUT_OT && val != POWER_LIMIT_OT) ||
	       (charger->skip_nego == SKIP_FO_ONLY && val == POWER_LIMIT_POSSIBLE_FO) ||
	       (charger->skip_nego == FORCE_25W_NEGO && val != POWER_LIMIT_OT);

	if (charger->skip_nego == FORCE_25W_NEGO)
		charger->nego_power = GOOGLE_WLC_MPP25_MAX_POWER;

	if (!skip && charger->nego_power != power) {
		charger->nego_power = power;
		changed = true;
	}

	if (changed && charger->pdata->batt_psy)
		power_supply_changed(charger->pdata->batt_psy);

	gvotable_cast_long_vote(charger->icl_ramp_target_votable, NEGO_VOTER,
				power, skip ? false : true);

	charger->icl_accuracy_comp = 0;

	if ((charger->pdata->has_mpp25_wlc_dc || charger->pdata->has_mpp15_wlc_dc) && !skip)
		gvotable_cast_long_vote(charger->wlc_dc_power_votable, NEGO_VOTER, power, true);

	logbuffer_devlog(charger->log, charger->dev,
			 "Negotiated power: %u, limit reason: %u, skip: %d, mode: %d",
			 charger->nego_power, val, skip, charger->skip_nego);
	google_wlc_update_psy(charger);
	return ret;
}

static void google_wlc_dream_defend(struct google_wlc_data *charger)
{

	if (charger->mode != RX_MODE_WPC_EPP)
		return;
	if (charger->last_capacity < 0 || charger->last_capacity > 100)
		return;
	charger->mitigate_threshold = charger->mitigate_threshold > 0 ?
		    charger->mitigate_threshold : charger->pdata->power_mitigate_threshold;
	if (charger->mitigate_threshold == 0 ||
	    charger->last_capacity < charger->mitigate_threshold)
		return;

	gvotable_cast_int_vote(charger->wlc_disable_votable, DD_VOTER, WLC_HARD_DISABLE, true);
	charger->wait_for_dd = true;
	msleep(2000);
	google_wlc_set_mode_gpio(charger, RX_MODE_WPC_BPP);
	gvotable_cast_int_vote(charger->wlc_disable_votable, DD_VOTER, 0, false);
	dev_info(charger->dev, "Force to BPP mode\n");
}

static void google_wlc_enable_csp_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
		struct google_wlc_data, csp_enable_work.work);
	int ret;

	mutex_lock(&charger->csp_status_lock);
	/* CSP already enabled */
	if (charger->send_csp)
		goto exit;

	if (charger->mode == RX_MODE_WPC_EPP_NEGO) {
		schedule_delayed_work(&charger->csp_enable_work,
				      msecs_to_jiffies(GOOGLE_WLC_CSP_ENABLE_DELAY_MS));
		goto exit;
	}
	/* Charger not in valid mode for sending CSP */
	if (charger->mode != RX_MODE_WPC_BPP && charger->mode != RX_MODE_WPC_EPP &&
	    !mode_is_mpp(charger->mode) && charger->mode != RX_MODE_WPC_MPP_RESTRICTED)
		goto exit;

	/* Send initial CSP and enable subsequent CSP */
	ret = charger->chip->chip_send_csp(charger);
	if (ret) {
		dev_err(charger->dev, "Failed to send CSP, reschedule initial CSP\n");
		schedule_delayed_work(&charger->csp_enable_work,
				      msecs_to_jiffies(GOOGLE_WLC_CSP_ENABLE_DELAY_MS));
		goto exit;
	}

	charger->send_csp = true;
	dev_dbg(charger->dev, "CSP Enabled\n");
exit:
	mutex_unlock(&charger->csp_status_lock);

	google_wlc_dream_defend(charger);
}

static void google_wlc_disable_csp(struct google_wlc_data *charger)
{
	cancel_delayed_work_sync(&charger->csp_enable_work);
	mutex_lock(&charger->csp_status_lock);
	if (charger->send_csp) {
		charger->send_csp = false;
		dev_dbg(charger->dev, "CSP Disabled\n");
	}
	mutex_unlock(&charger->csp_status_lock);
}

/* May be called with status_lock held */
static void google_wlc_trigger_icl_ramp(struct google_wlc_data *charger, int delay)
{
	__pm_stay_awake(charger->icl_ramp_ws);
	mod_delayed_work(system_wq, &charger->icl_ramp_work, msecs_to_jiffies(delay));
}

static int google_wlc_get_icl_loop_status(struct google_wlc_data *charger,
					  struct icl_loop_status *status)
{
	int ret;
	u32 reg;
	int icl_step = charger->output_chg_config->icl_step_size_ua;
	char reason[GVOTABLE_MAX_REASON_LEN] = "";

	if (icl_step <= 0)
		icl_step = DEFAULT_DC_ICL_STEP;

	ret = charger->chip->chip_get_iout(charger, &reg);
	if (ret != 0) {
		dev_err(charger->dev, "ICL ramp: Could not get iout\n");
		return -EINVAL;
	}
	status->current_now = MA_TO_UA(reg);

	ret = charger->chip->chip_get_vout(charger, &reg);
	if (ret != 0) {
		dev_err(charger->dev, "ICL ramp: Could not get vout\n");
		return -EINVAL;
	}
	status->vout = reg;

	ret = charger->chip->chip_get_vrect(charger, &reg);
	if (ret != 0) {
		dev_err(charger->dev, "ICL ramp: Could not get vrect\n");
		return -EINVAL;
	}
	status->vrect = reg;

	ret = charger->chip->chip_get_vrect_target(charger, &reg);

	if (ret != 0) {
		dev_err(charger->dev, "ICL ramp: Could not get vrect_target\n");
		return -EINVAL;
	}
	if (reg == 0)
		return -EINVAL;
	status->vrect_target = reg;

	if (charger->bpp_ramp_done_target)
		status->icl_target = charger->bpp_ramp_done_target;
	else
		status->icl_target =
			MA_TO_UA(charger->icl_ramp_target_mw * 1000 / status->vrect_target);

	/* check the real DC_ICL setting */
	status->icl_current_vote = gvotable_get_current_int_vote(charger->dc_icl_votable);
	gvotable_get_current_reason(charger->dc_icl_votable, reason, GVOTABLE_MAX_REASON_LEN);
	dev_info(charger->dev, "current dc_icl %d voted by %s", status->icl_current_vote, reason);
	/* Round it down to get the actual DC_ICL setting */
	status->icl_current_vote = status->icl_current_vote / icl_step * icl_step;
	if (status->icl_current_vote < charger->output_chg_config->init_cur_ua)
		status->icl_current_vote = charger->output_chg_config->init_cur_ua;
	return 0;
}

static void google_wlc_mpp_ramp_setup(struct google_wlc_data *charger,
					  struct icl_loop_status *status)
{
	int icl_step = charger->output_chg_config->icl_step_size_ua;
	int icl_rounding = charger->output_chg_config->icl_rounding;
	bool log = false;

	status->load_step = charger->output_chg_config->icl_step_size_ua;

	if (charger->fast_mpp_ramp)
		status->load_step = 100000;
	if (charger->force_icl_decrease) {
		status->load_step = (-1) * charger->output_chg_config->icl_step_size_ua;
	} else if (charger->rcs_backoff_count == RCS_LOAD_BACKOFF_IRQ_COUNT) {
		dev_info(charger->dev, "ICL backoff due to RCS. Decreasing ICL one step");
		charger->rcs_backoff_count -= 1;
		status->load_step = (-1) * charger->output_chg_config->icl_step_size_ua;
	} else if (charger->rcs_backoff_count > 0) {
		dev_info(charger->dev, "ICL hold due to RCS. Remaining count: %d",
			 charger->rcs_backoff_count);
		charger->rcs_backoff_count -= 1;
		status->load_step = 0;
	}

	if (status->vrect_target > MPP_NPM_MIN_VOLTAGE ||
	    charger->icl_ramp_target_mw > MPP_NPM_MAX_POWER_12V_THRESHOLD) {
		/* If vrect is ramping between 12 and 14V, treat it as 14V. Avoids ICL overshoot */
		if (status->vrect_target <= MPP_NPM_MAX_VOLTAGE)
			status->icl_target =
				MA_TO_UA(charger->icl_ramp_target_mw * 1000 / MPP_NPM_MAX_VOLTAGE);
		/* Also, adjust ICL at the higher power levels to account for losses */
		if (icl_rounding == ICL_TARGET_ROUND_UP_WITH_COMP &&
		    charger->icl_accuracy_comp == 0) {
			charger->icl_accuracy_comp = icl_step;
			log = true;
		}
	}

	status->icl_target = google_wlc_round_icl_to_target(charger, status->icl_target);

	/* Adjust ADC accuracy compensations to avoid overshooting at all times
	 * and avoid undershooting during dploss
	 */
	if (charger->mpp25.state == MPP25_DPLOSS_CALIBRATION ||
			charger->mpp25.state == MPP25_DPLOSS_CAL4) {
		if (charger->mpp25.dploss_comp == 0 && charger->icl_now == status->icl_target) {
			if (charger->icl_now < status->current_now - icl_step)
				charger->mpp25.dploss_comp = icl_step * (-1);
			else if (charger->icl_now > status->current_now + icl_step)
				charger->mpp25.dploss_comp = icl_step;
		}
		if (charger->mpp25.dploss_comp != 0) {
			dev_info(charger->dev,
				"DPLOSS: Apply %d uA accuracy compensation, target ICL: %d=>%d",
				charger->mpp25.dploss_comp, status->icl_target,
				status->icl_target + charger->mpp25.dploss_comp);
			status->icl_target = status->icl_target + charger->mpp25.dploss_comp;
		}
		return;
	}
	if (charger->icl_accuracy_comp == 0 && charger->icl_now == status->icl_target) {
		int max_icl = (charger->nego_power * 1000 / status->vrect_target) * 1000;

		dev_dbg(charger->dev, "iout: %d, max_icl: %d",
				status->current_now, max_icl);
		if (status->current_now > max_icl) {
			log = true;
			charger->icl_accuracy_comp = (-1) * icl_step;
		}
	}
	if (charger->icl_accuracy_comp != 0) {
		if (log)
			dev_info(charger->dev,
				 "Apply %d uA ICL accuracy compensation, target ICL: %d=>%d",
				 charger->icl_accuracy_comp, status->icl_target,
				 status->icl_target + charger->icl_accuracy_comp);
		status->icl_target = status->icl_target + charger->icl_accuracy_comp;
	}
}

static void google_wlc_epp_ramp_setup(struct google_wlc_data *charger,
				     struct icl_loop_status *status)
{
	status->load_step = 100000;
}

static void google_wlc_bpp_ramp_setup(struct google_wlc_data *charger,
				     struct icl_loop_status *status)
{
	/* Use 100mA as BPP/MPP_RESTRICTED load step */
	status->load_step = 100000;
}

static int google_wlc_round_icl_to_target(struct google_wlc_data *charger, int icl)
{
	int icl_step = charger->output_chg_config->icl_step_size_ua;
	int icl_rounding = charger->output_chg_config->icl_rounding;

	if (icl_step <= 0)
		icl_step = DEFAULT_DC_ICL_STEP;

	if (charger->icl_target_rounding_override)
		icl_rounding = charger->icl_target_rounding_override;

	if (icl_rounding == ICL_TARGET_ROUND_UP ||
		icl_rounding == ICL_TARGET_ROUND_UP_WITH_COMP)
		icl = ((icl + icl_step - 1) / icl_step) * icl_step;
	else if (icl_rounding == ICL_TARGET_ROUND_DOWN)
		icl = (icl / icl_step) * icl_step;
	else if (icl_rounding == ICL_TARGET_ROUND_NEAREST)
		icl = ((icl + icl_step/2) / icl_step) * icl_step;
	return icl;
}

static int google_icl_calculate_icl_target(struct google_wlc_data *charger,
					   struct icl_loop_status *status)
{
	u32 vrect, vout, new_icl_target;
	u64 pout;

	if (charger->pdata->iop_bpp_vout_tolerance <= 0)
		return 0;
	if (charger->chip->chip_get_vrect_target(charger, &vrect) != 0)
		vrect = 0;
	if (charger->chip->chip_get_vout(charger, &vout) != 0)
		vout = 0;
	if (charger->icl_now <= GOOGLE_WLC_RAMP_DONE_ICL_MIN_UA) {
		dev_info(charger->dev, "icl <= %duA, ramp done check is no need",
			 GOOGLE_WLC_RAMP_DONE_ICL_MIN_UA);
		charger->icl_loop_state = ICL_LOOP_DONE;
		return 0;
	}

	dev_dbg(charger->dev, "ramp done check: vout=%dmV iout=%dmA\n", vout, status->current_now);
	pout = UA_TO_MA(status->current_now) * vout;
	new_icl_target = MA_TO_UA(pout / vrect);
	new_icl_target = new_icl_target / DEFAULT_DC_ICL_STEP * DEFAULT_DC_ICL_STEP;

	if (new_icl_target < GOOGLE_WLC_RAMP_DONE_ICL_MIN_UA)
		new_icl_target = GOOGLE_WLC_RAMP_DONE_ICL_MIN_UA;

	if (vout < vrect - charger->pdata->iop_bpp_vout_tolerance) {
		dev_info(charger->dev, "ramp done check: Pout=%llu, new_icl=%d\n",
			 pout, new_icl_target);
		status->icl_next = new_icl_target;
		status->icl_target = new_icl_target;
		return new_icl_target;
	} else if (charger->bpp_ramp_done_target > 0) {
		charger->icl_loop_state = ICL_LOOP_DONE;
	}

	return 0;
}

static int google_wlc_do_ramp(struct google_wlc_data *charger,
					  struct icl_loop_status *status)
{
	int icl_step = charger->output_chg_config->icl_step_size_ua;
	int decrease_step = icl_step;
	int ramp_check_count = GOOGLE_WLC_RAMP_DONE_CHECK_NUM;

	if (charger->wait_for_icl_ramp)
		decrease_step = GOOGLE_WLC_RAMPDOWN_DECREASE_MULT * icl_step;

	if (charger->mode == RX_MODE_WPC_BPP)
		ramp_check_count = GOOGLE_WLC_BPP_RAMP_DONE_CHECK_NUM;
	if (charger->mode == RX_MODE_WPC_BPP && charger->pdata->iop_bpp_vout_tolerance > 0)
		ramp_check_count = GOOGLE_WLC_IOP_BPP_RAMP_DONE_CHECK_NUM;

	if (status->icl_current_vote < charger->icl_now) {
		/* DC_ICL voted by us is higher than the current active DC_ICL vote */
		if ((status->load_step < 0 || charger->icl_now > status->icl_target) &&
		     (status->icl_current_vote > status->icl_target ||
		      charger->force_icl_decrease)) {
			/* For decreasing load, start from current active DC_ICL vote */
			dev_info(charger->dev, "icl_now %d->%d to match real DC_ICL\n",
			charger->icl_now, status->icl_current_vote);
			charger->icl_now = status->icl_current_vote;
		} else if (charger->ramp_skip_check_count < GOOGLE_WLC_RAMP_SKIP_CHECK_NUM) {
			status->icl_next = charger->icl_now;
			charger->ramp_skip_check_count += 1;
			dev_info(charger->dev, "Ramp skip check: %d",
				 charger->ramp_skip_check_count);
			return 0;
		} else {
			/* Skip to the end of ramp early since we are limited by someone else */
			dev_info(charger->dev, "Skip remaining ramp, curr vote=%d, our vote=%d\n",
				status->icl_current_vote, charger->icl_now);
			google_wlc_set_dc_icl(charger, status->icl_target);
			charger->icl_now = status->icl_target;
			charger->icl_loop_state = ICL_LOOP_DONE;
			return 0;
		}
	} else {
		charger->ramp_skip_check_count = 0;
	}

	status->load_step = status->load_step * charger->iout_multiplier;

	if (charger->icl_now > status->icl_target && status->load_step > 0) {
		/* Adjust load step to negative if it isn't and we need to decrease icl */
		if (charger->icl_now - status->icl_target >
						decrease_step * charger->iout_multiplier)
			status->load_step = decrease_step * (-1) * charger->iout_multiplier;
		else
			status->load_step = status->icl_target - charger->icl_now;
	}
	status->icl_next = charger->icl_now + status->load_step;

	if (status->icl_next >= status->icl_target) {
		if (charger->icl_now == status->icl_target) {
			/* check for at least 3s during fast MPP ramp */
			if (charger->fast_mpp_ramp)
				ramp_check_count = 15;
			/* We are at max step and the intended next ramp doesn't drop us down */
			if (charger->ramp_done_check_count >= ramp_check_count) {
				charger->icl_loop_state = ICL_LOOP_DONE;
				return 0;
			}
			if (charger->pdata->iop_bpp_vout_tolerance > 0)
				charger->bpp_ramp_done_check = true;
			dev_info(charger->dev, "ramp done check: %d\n",
				 charger->ramp_done_check_count);
			charger->ramp_done_check_count += 1;
			charger->bpp_ramp_done_target =
					google_icl_calculate_icl_target(charger, status);
			if (charger->bpp_ramp_done_target > 0)
				return 0;
			status->icl_next = status->icl_target;
			return 0;
		}
		if (charger->icl_now < status->icl_target)
			status->icl_next = status->icl_target;
	}
	charger->ramp_done_check_count = 0;
	charger->bpp_ramp_done_check = false;

	if (status->icl_next < charger->output_chg_config->init_cur_ua)
		status->icl_next = charger->output_chg_config->init_cur_ua;

	status->icl_next = google_wlc_round_icl_to_target(charger, status->icl_next);
	return 0;
}

static void set_inlim_enabled(struct google_wlc_data *charger, bool en)
{
	if (IS_ERR_OR_NULL(charger->pdata->wcin_inlim_en_gpio) ||
	    (en && charger->status != GOOGLE_WLC_STATUS_CHARGING))
		return;

	if (!charger->inlim_available) {
		if (charger->inlim_setting) {
			dev_info(charger->dev, "INLIM disabled");
			gpiod_set_value_cansleep(charger->pdata->wcin_inlim_en_gpio, 0);
			charger->inlim_setting = 0;
		}
		return;
	}
	if (en != charger->inlim_setting)
		dev_info(charger->dev, "INLIM %d->%d", charger->inlim_setting, en);
	gpiod_set_value_cansleep(charger->pdata->wcin_inlim_en_gpio, en);
	charger->inlim_setting = en;
}

/* Acquires status_lock */
static void google_wlc_icl_ramp_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, icl_ramp_work.work);
	struct icl_loop_status status;
	int ret = 0;
	bool decrease = false;
	u32 opfreq = 0, ramp_retry_ms = GOOGLE_WLC_RAMP_RETRY_INTERVAL;

	mutex_lock(&charger->status_lock);

	if (charger->icl_ramp_disable || charger->status != GOOGLE_WLC_STATUS_CHARGING ||
	    charger->shutdown_notify) {
		charger->icl_loop_state = ICL_LOOP_INACTIVE;
		goto done;
	}

	charger->icl_loop_state = ICL_LOOP_ONGOING;

	ret = google_wlc_get_icl_loop_status(charger, &status);
	if (ret < 0)
		goto reschedule;

	if (mode_is_mpp(charger->mode))
		google_wlc_mpp_ramp_setup(charger, &status);
	else if (mode_is_epp(charger->mode))
		google_wlc_epp_ramp_setup(charger, &status);
	else if (charger->mode == RX_MODE_WPC_BPP || charger->mode == RX_MODE_WPC_MPP_RESTRICTED)
		google_wlc_bpp_ramp_setup(charger, &status);

	status.icl_target = google_wlc_round_icl_to_target(charger, status.icl_target);

	if (status.icl_target <= charger->output_chg_config->init_cur_ua)
		status.icl_target = charger->output_chg_config->init_cur_ua;

	if (status.icl_target < charger->icl_now)
		decrease = true;

	ret = google_wlc_do_ramp(charger, &status);
	if (charger->icl_loop_state == ICL_LOOP_DONE)
		goto done;

	if (charger->icl_loop_mode == ICL_LOOP_TIMER && !decrease)
		set_inlim_enabled(charger, false);

	if (charger->chip->chip_get_opfreq(charger, &opfreq) != 0)
		opfreq = 0;
	dev_info(charger->dev,
		 "ICL ramp work, icl_now=%d->%d, iout=%d, loop_mode = %d, vout=%d, vrect = %d, vrect_target = %d, target(mw) = %d, target(ma) = %d, load step = %d, opfreq = %u",
		 charger->icl_now, status.icl_next, status.current_now, charger->icl_loop_mode,
		 status.vout, status.vrect, status.vrect_target, charger->icl_ramp_target_mw,
		 status.icl_target, status.load_step, opfreq);
	/* Set next target ICL */
	if (ret == 0 && status.icl_next != charger->icl_now) {
		google_wlc_set_dc_icl(charger, status.icl_next);
		charger->icl_now = status.icl_next;
	}
	if (charger->icl_loop_mode == ICL_LOOP_INTERRUPT) {
		/* Interrupt based ramp up */
		cancel_delayed_work_sync(&charger->icl_ramp_timeout_work);
		__pm_relax(charger->icl_ramp_timeout_ws);
		if (decrease) {
			dev_dbg(charger->dev, "ICL decrease");
			goto reschedule;
		}
		set_inlim_enabled(charger, true);
		charger->chip->chip_enable_load_increase(charger, true);
		__pm_stay_awake(charger->icl_ramp_timeout_ws);
		schedule_delayed_work(&charger->icl_ramp_timeout_work,
				      msecs_to_jiffies(GOOGLE_WLC_ICL_RAMP_TIMEOUT_MS));
		__pm_relax(charger->icl_ramp_ws);
	} else if (charger->icl_loop_mode == ICL_LOOP_TIMER) {
		goto reschedule;
	}
	mutex_unlock(&charger->status_lock);
	return;
reschedule:
	set_inlim_enabled(charger, false);
	charger->chip->chip_enable_load_increase(charger, false);
	if (charger->bpp_ramp_done_check)
		ramp_retry_ms = GOOGLE_WLC_IOP_BPP_RAMP_DONE_CHECK_MS;
	dev_info(charger->dev, "Rescheduling ramp work in %d ms\n", ramp_retry_ms);
	schedule_delayed_work(&charger->icl_ramp_work,
				msecs_to_jiffies(ramp_retry_ms));
	mutex_unlock(&charger->status_lock);
	return;
done:
	charger->ramp_done_check_count = 0;
	charger->chip->chip_enable_load_increase(charger, false);
	if (charger->wait_for_icl_ramp)
		complete(&charger->icl_ramp_done);
	logbuffer_devlog(charger->log, charger->dev, "End ICL Ramp. ICL: %d, current: %d",
			charger->icl_now, status.current_now);
	cancel_delayed_work_sync(&charger->pla_ack_timeout_work);
	cancel_delayed_work_sync(&charger->icl_ramp_timeout_work);
	__pm_relax(charger->icl_ramp_timeout_ws);
	if (charger->status == GOOGLE_WLC_STATUS_CHARGING)
		google_wlc_handle_chg_stable(charger, CHG_STABLE_TYPE_CHARGING,
					     GOOGLE_WLC_CHG_STABLE_TIME_MS);
	if (charger->mode != RX_MODE_WPC_MPP_RESTRICTED)
		set_inlim_enabled(charger, true);
	__pm_relax(charger->icl_ramp_ws);
	mutex_unlock(&charger->status_lock);
	return;
}

static void google_wlc_icl_ramp_timeout_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, icl_ramp_timeout_work.work);

	dev_info(charger->dev, "ICL ramp timeout, release wakelock\n");
	__pm_relax(charger->icl_ramp_timeout_ws);
	return;

}

static void google_wlc_mpp_nego_timeout_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, mpp_nego_timeout_work.work);
	u8 mode;
	int ret;

	// Check the mode right before sending EPT in case it recovered
	mutex_lock(&charger->status_lock);
	ret = charger->chip->chip_get_sys_mode(charger, &mode);
	mutex_unlock(&charger->status_lock);
	if (ret < 0 || mode != RX_MODE_WPC_MPP_NEGO)
		return;

	logbuffer_devlog(charger->log, charger->dev, "MPP Nego timeout! Sending EPT restart.");
	google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
	schedule_delayed_work(&charger->psy_notifier_work, GOOGLE_WLC_NOTIFIER_DELAY_MS);
}

static int set_eds_state(struct google_wlc_data *charger, enum eds_state state)
{
	mutex_lock(&charger->eds_lock);

	switch (state) {
	case EDS_AVAILABLE:
		charger->tx_busy = false;
		charger->tx_done = true;
		charger->rx_done = true;
		google_wlc_set_eds_icl(charger, false);
		cancel_delayed_work(&charger->tx_work);
		charger->eds_state = state;
		sysfs_notify(&charger->dev->kobj, NULL, "txbusy");
		sysfs_notify(&charger->dev->kobj, NULL, "txdone");
		sysfs_notify(&charger->dev->kobj, NULL, "rxdone");
		__pm_relax(charger->eds_tx_ws);
		break;
	case EDS_SEND:
		if (charger->eds_state != EDS_AVAILABLE) {
			dev_warn(charger->dev, "eds not allowed to send, state: %d\n",
				 charger->eds_state);
			mutex_unlock(&charger->eds_lock);
			return -EAGAIN;
		}
		google_wlc_set_eds_icl(charger, true);
		charger->tx_busy = true;
		charger->tx_done = false;
		charger->rx_done = false;
		charger->eds_state = state;
		__pm_stay_awake(charger->eds_tx_ws);
		break;
	case EDS_SENT:
		charger->tx_busy = false;
		charger->tx_done = true;
		cancel_delayed_work(&charger->tx_work);
		charger->eds_state = state;
		sysfs_notify(&charger->dev->kobj, NULL, "txbusy");
		sysfs_notify(&charger->dev->kobj, NULL, "txdone");
		__pm_relax(charger->eds_tx_ws);
		break;
	case EDS_RECEIVED:
		charger->rx_done = true;
		charger->eds_state = state;
		sysfs_notify(&charger->dev->kobj, NULL, "rxdone");
		google_wlc_set_eds_icl(charger, false);
		break;
	case EDS_RESET:
		charger->tx_busy = false;
		charger->tx_done = true;
		charger->rx_done = true;
		google_wlc_set_eds_icl(charger, false);
		cancel_delayed_work(&charger->tx_work);
		if (charger->eds_total_count < 0xFFFF)
			charger->eds_error_count++;
		charger->eds_state = EDS_AVAILABLE;
		sysfs_notify(&charger->dev->kobj, NULL, "txbusy");
		sysfs_notify(&charger->dev->kobj, NULL, "txdone");
		sysfs_notify(&charger->dev->kobj, NULL, "rxdone");
		__pm_relax(charger->eds_tx_ws);
		break;
	default:
		break;
	}

	mutex_unlock(&charger->eds_lock);
	return 0;
}

static void google_wlc_abort_transfers(struct google_wlc_data *charger)
{
	/* Abort all transfers */
	set_eds_state(charger, EDS_AVAILABLE);
}

enum execution_mode {
	EXECUTION_MODE_DRY_RUN,
	EXECUTION_MODE_NORMAL,
};

static int google_wlc_mdis_into_power_limit(struct google_wlc_data *charger, int lvl,
					    enum execution_mode mode, int config_index)
{
	int wlc_pwr, ret = -ENODEV;
	int bpp_mdis_num, epp_mdis_num;
	int mpp_mdis_num = 0;
	u8 mode_reg;
	struct wlc_chg_config *config;
	u32 *mpp_pwr_table;

	if (config_index < 0 || config_index >= charger->pdata->chg_configs_num) {
		dev_err(charger->dev, "invalid config index: %d", config_index);
		return -1;
	}

	config = &charger->pdata->chg_configs[config_index];

	bpp_mdis_num = charger->pdata->bpp_mdis_num;
	epp_mdis_num = charger->pdata->epp_mdis_num;

	if (google_wlc_is_present(charger))
		ret = charger->chip->chip_get_sys_mode(charger, &mode_reg);

	if (ret < 0)
		mode_reg = RX_MODE_WPC_BPP;

	if (mode_is_mpp(mode_reg) || mode_reg == RX_MODE_MPP_CLOAK)
		mode_reg = RX_MODE_WPC_MPP;

	switch (mode_reg) {
	case RX_MODE_WPC_MPP:
	case RX_MODE_WPC_MPP_RESTRICTED:
		if (charger->pdata->mpp_mdis_num > 0) {
			mpp_mdis_num = charger->pdata->mpp_mdis_num;
			mpp_pwr_table = charger->pdata->mpp_mdis_pwr;
			/* if no chg_config mdis, don't return a wlc_pwr for non-zero index */
			if (config_index > 0 && config->mpp_mdis_num == 0)
				mpp_mdis_num = 0;
		} else if (config->mpp_mdis_num > 0) {
			mpp_mdis_num = config->mpp_mdis_num;
			mpp_pwr_table = config->mpp_mdis_pwr;
			if (mode == EXECUTION_MODE_NORMAL) {
				dev_dbg(charger->dev, "select mpp mdis table: %s",
					config->mpp_mdis_name);
				charger->mdis_name = config->mpp_mdis_name;
			}
		}
		if (mpp_mdis_num <= 0)
			wlc_pwr = -1;
		else {
			if (charger->retail_power_limit)
				lvl = mpp_mdis_num - 1;

			if (lvl >= mpp_mdis_num)
				wlc_pwr = 0;
			else
				wlc_pwr = mpp_pwr_table[lvl];
		}
		break;
	case RX_MODE_WPC_EPP_NEGO:
	case RX_MODE_WPC_EPP:
		if (epp_mdis_num <= 0)
			wlc_pwr = -1;
		else if (lvl >= epp_mdis_num)
			wlc_pwr = 0;
		else
			wlc_pwr = charger->pdata->epp_mdis_pwr[lvl];
		if (mode == EXECUTION_MODE_NORMAL)
			charger->mdis_name =  "epp_mdis_pwr";
		break;
	case RX_MODE_WPC_BPP:
		if (bpp_mdis_num <= 0)
			wlc_pwr = -1;
		else if (lvl >= bpp_mdis_num)
			wlc_pwr = 0;
		else
			wlc_pwr = charger->pdata->bpp_mdis_pwr[lvl];
		if (mode == EXECUTION_MODE_NORMAL)
			charger->mdis_name =  "bpp_mdis_pwr";
		break;
	default:
		/* Other modes default to BPP limits */
		if (bpp_mdis_num <= 0)
			wlc_pwr = -1;
		else if (lvl >= bpp_mdis_num)
			wlc_pwr = 0;
		else
			wlc_pwr = charger->pdata->bpp_mdis_pwr[lvl];
		if (mode == EXECUTION_MODE_NORMAL) {
			dev_info(charger->dev, "Mode %s default to BPP limits",
				 sys_op_mode_str[mode_reg]);
			charger->mdis_name =  "bpp_mdis_pwr";
		}
		break;
	}

	if (lvl == 0)
		wlc_pwr = -1;

	if (lvl == MDIS_LEVEL_MAX)
		wlc_pwr = 0;

	return wlc_pwr;
}

static int google_wlc_mdis_into_cp_fcc(struct google_wlc_data *charger, int lvl)
{
	int wlc_dc_mdis_num = charger->pdata->wlc_dc_mdis_num;
	int cp_fcc;

	if ((charger->pdata->has_mpp25_wlc_dc || charger->pdata->has_mpp15_wlc_dc) &&
	    wlc_dc_mdis_num > 0 && lvl < wlc_dc_mdis_num)
		cp_fcc = charger->pdata->wlc_dc_mdis_fcc[lvl];
	else
		cp_fcc = -1;

	if (lvl == 0)
		cp_fcc = -1;

	if (lvl == MDIS_LEVEL_MAX) {
		if (wlc_dc_mdis_num > 0)
			cp_fcc = 0;
	}

	return cp_fcc;
}

static void google_wlc_pri_chg_vote(struct google_wlc_data *charger)
{
	int wlc_dc_mdis_num = charger->pdata->wlc_dc_mdis_num;
	int wlc_pwr = -1, cp_fcc, lvl = charger->pri_chg_mdis_lvl;
	int ret, voltage_mv;
	bool charging = google_wlc_is_present(charger);
	bool active_fcc = false, active_icl = false;
	bool targeting_dc = charger->output_chg_config &&
		charger->output_chg_config->charger_type == CHARGER_TYPE_SWC;

	if (!charger->pri_chg_min_pwr_votable)
		charger->pri_chg_min_pwr_votable =
			gvotable_election_get_handle(VOTABLE_PRI_CHG_MIN_PWR);
	if (!charger->pri_chg_min_pwr_votable) {
		dev_warn_ratelimited(charger->dev, "PriC Failed to get " VOTABLE_PRI_CHG_MIN_PWR
				     " votable");
		return;
	}

	if (charging && targeting_dc && wlc_dc_mdis_num > 0) {
		if (!charger->pdata->batt_psy) {
			dev_warn_ratelimited(charger->dev, "batt_psy not available yet");
			return;
		}
		ret = GPSY_GET_PROP(charger->pdata->batt_psy, POWER_SUPPLY_PROP_VOLTAGE_MAX);

		cp_fcc = google_wlc_mdis_into_cp_fcc(charger, lvl);
		if (ret < 0) {
			dev_warn_ratelimited(charger->dev, "PriC Failed to get max voltage - %d",
					     ret);
			return;
		}
		voltage_mv = ret / 1000;
		if (cp_fcc == -1) {
			wlc_pwr = -1;
		} else {
			wlc_pwr = (cp_fcc / 1000) * voltage_mv;
			active_fcc = true;
		}
	} else if (charging) {
		wlc_pwr = google_wlc_mdis_into_power_limit(charger, lvl, EXECUTION_MODE_DRY_RUN,
							   charger->chg_config_index);
		if (wlc_pwr != -1)
			active_icl = true;
	}

	dev_info(charger->dev, "PriC MDIS lvl=%d icl=%d fcc=%d pwr=%d",
		 lvl, active_icl, active_fcc, wlc_pwr);

	gvotable_cast_int_vote(charger->pri_chg_min_pwr_votable,
			       "DC_FCC",
			       wlc_pwr,
			       active_fcc);
	gvotable_cast_int_vote(charger->pri_chg_min_pwr_votable,
			       "DC_ICL",
			       wlc_pwr,
			       active_icl);
}

static void google_wlc_vote_csi_thermals(struct google_wlc_data *charger, int wlc_pwr, int cp_fcc)
{
	int wlc_dc_mdis_num = charger->pdata->wlc_dc_mdis_num;
	bool charging = google_wlc_is_present(charger);
	bool active_fcc = false, active_icl = false;
	int fcc_pwr = -1, icl_pwr = -1;
	int ret;
	bool targeting_dc = charger->output_chg_config &&
		charger->output_chg_config->charger_type == CHARGER_TYPE_SWC;

	if (!charger->csi_status_votable)
		charger->csi_status_votable = gvotable_election_get_handle(VOTABLE_CSI_STATUS);
	if (!charger->csi_status_votable)
		return;

	if (charging && targeting_dc && wlc_dc_mdis_num > 0) {
		int voltage_mv;

		if (!charger->pdata->batt_psy) {
			dev_warn_ratelimited(charger->dev, "batt_psy not available yet");
			return;
		}

		ret = GPSY_GET_PROP(charger->pdata->batt_psy, POWER_SUPPLY_PROP_VOLTAGE_MAX);
		if (ret < 0) {
			dev_warn_ratelimited(charger->dev, "Failed to get max voltage - %d", ret);
			return;
		}
		voltage_mv = ret / 1000;

		if (cp_fcc == -1) {
			fcc_pwr = -1;
		} else {
			fcc_pwr = (cp_fcc / 1000) * voltage_mv;
			active_fcc = true;
		}
	} else if (charging) {
		if (wlc_pwr != -1) {
			icl_pwr = wlc_pwr;
			active_icl = true;
		}
	}

	gvotable_cast_compound_vote(charger->csi_status_votable,
				    "CSI_STATUS_THERM_DC_ICL",
				    icl_pwr,
				    CSI_STATUS_System_Thermals,
				    active_icl);
	gvotable_cast_compound_vote(charger->csi_status_votable,
				    "CSI_STATUS_THERM_DC_FCC",
				    fcc_pwr,
				    CSI_STATUS_System_Thermals,
				    active_fcc);
}

static int google_wlc_thermal_icl_vote(struct google_wlc_data *charger)
{
	int wlc_pwr, cp_fcc, lvl = charger->mdis_level;
	int wlc_dc_mdis_num = charger->pdata->wlc_dc_mdis_num;

	wlc_pwr = google_wlc_mdis_into_power_limit(charger, lvl, EXECUTION_MODE_NORMAL,
						   charger->chg_config_index);
	cp_fcc = google_wlc_mdis_into_cp_fcc(charger, lvl);

	if (lvl == MDIS_LEVEL_MAX) {
		gvotable_cast_int_vote(charger->wlc_disable_votable, WLC_MDIS_VOTER,
			       WLC_SOFT_DISABLE, true);
	} else {
		gvotable_cast_int_vote(charger->wlc_disable_votable, WLC_MDIS_VOTER,
			       0, false);
	}

	charger->mdis_limit = wlc_pwr;

	if (cp_fcc < 0)
		goto vote_pwr;

	if (in_wlc_dc(charger) && wlc_dc_mdis_num > 0) {
		charger->mdis_name =  "wlc_dc_cp_fcc";
		charger->mdis_limit = cp_fcc;
	}

	if (!charger->cp_fcc_votable) {
		charger->cp_fcc_votable = gvotable_election_get_handle("WLC_GCPM_FCC");
		if (IS_ERR(charger->cp_fcc_votable)) {
			dev_err(charger->dev,
				"Could not get votable: WLC_GCPM_FCC, ret=%ld\n",
				PTR_ERR(charger->cp_fcc_votable));
			goto vote_pwr;
		}
	}
vote_pwr:
	if (charger->cp_fcc_votable)
		gvotable_cast_int_vote(charger->cp_fcc_votable,
				       WLC_MDIS_VOTER, cp_fcc, cp_fcc >= 0);

	if (charger->retail_power_limit) {
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_RETAIL_VOTER,
					wlc_pwr / 1000, wlc_pwr >= 0);
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_MDIS_VOTER, 0,
					false);
	} else {
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_RETAIL_VOTER,
					0, false);
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_MDIS_VOTER,
					wlc_pwr / 1000, wlc_pwr >= 0);
	}
	if (charger->pdata->has_mpp25_wlc_dc || charger->pdata->has_mpp15_wlc_dc)
		gvotable_cast_long_vote(charger->wlc_dc_power_votable,
					WLC_MDIS_VOTER, wlc_pwr / 1000, wlc_pwr >= 0);
	if (wlc_pwr >= 0)
		charger->mdis_pwr_limit = wlc_pwr / 1000;
	else
		charger->mdis_pwr_limit = -1;
	if (lvl > 0 && cp_fcc >= 0) {
		dev_info(charger->dev, "mdis=%d, wlc_pwr=%d mW, cp_fcc=%d\n",
			lvl, wlc_pwr / 1000, cp_fcc);
	} else if (lvl > 0) {
		if (charger->mpp25.state == MPP25_DPLOSS_CALIBRATION ||
			charger->mpp25.state == MPP25_DPLOSS_CAL4) {
			int dploss_max;

			if (charger->mpp25.state == MPP25_DPLOSS_CALIBRATION)
				dploss_max = charger->pdata->dploss_steps[MPP_DPLOSS_NUM_STEPS - 1];
			else
				dploss_max = charger->mpp25_dploss_cal4;

			if (wlc_pwr / 1000 < dploss_max) {
				if (charger->mpp25.state == MPP25_DPLOSS_CALIBRATION) {
					dev_info(charger->dev,
						"DPLOSS: MDIS power too low during cal, disable");
					google_wlc_disable_mpp25(charger, true);
				} else {
					dev_info(charger->dev,
						"DPLOSS: MDIS power too low during 4th cal, abort");
					google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
				}
			}
		}
		dev_info(charger->dev, "mdis=%d, wlc_pwr=%d mW\n", lvl, wlc_pwr / 1000);
	} else {
		dev_info(charger->dev, "mdis=0");
	}

	google_wlc_vote_csi_thermals(charger, wlc_pwr, cp_fcc);
	google_wlc_pri_chg_vote(charger);
	google_wlc_update_psy(charger);

	return 0;
}

static void google_wlc_setup_by_mode(struct google_wlc_data *charger, int mode)
{
	google_wlc_thermal_icl_vote(charger);

	google_wlc_check_mpp_initialized(charger);

	if (mode_is_mpp(mode) && !charger->mpp_initialized)
		return;

	if (!charger->chg_mode_votable)
		charger->chg_mode_votable = gvotable_election_get_handle(GBMS_MODE_VOTABLE);
	if (charger->chg_mode_votable && charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED) {
		int rx_mode = (charger->stdby_mode || charger->run_fwupdate) ?
				GBMS_CHGR_MODE_WLC_RX_STDBY : GBMS_CHGR_MODE_WLC_RX;

		gvotable_cast_long_vote(charger->chg_mode_votable, WLC_VOTER,
			_bms_usecase_meta_async_set(rx_mode, 1), true);
	}

	if (mode_is_mpp(mode)) {
		if (!wpc_auth_passed(charger)) {
			gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_AUTH_VOTER,
				GOOGLE_WLC_PREAUTH_RAMP_TARGET, true);
		}
		if ((mode == RX_MODE_WPC_MPP_HPM || mode == RX_MODE_WPC_MPP_CPM) &&
		     is_charger_mpp25_ok(charger->output_chg_config) &&
		     charger->pdata->support_mpp25 &&
		     !charger->mpp25_disabled)
			gvotable_cast_long_vote(charger->icl_ramp_target_votable, MODE_VOTER,
				GOOGLE_WLC_MPP25_MAX_POWER, true);
		else if (mode != RX_MODE_WPC_MPP_NEGO)
			gvotable_cast_long_vote(charger->icl_ramp_target_votable, MODE_VOTER,
				GOOGLE_WLC_MPP_MAX_POWER, true);
	} else if (mode_is_epp(mode)) {
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, MODE_VOTER,
					charger->pdata->wlc_epp_max_power_mw, true);
		google_wlc_vote_csi_auth(charger, false);
	} else if (mode == RX_MODE_WPC_BPP || mode == RX_MODE_WPC_MPP_RESTRICTED) {
		if (charger->disconnect_count > BPP_MISCOMM_DISCONNECT_COUNT) {
			gvotable_cast_long_vote(charger->icl_ramp_target_votable, MODE_VOTER,
					GOOGLE_WLC_BPP_MISCOMM_POWER, true);
			logbuffer_devlog(charger->log, charger->dev,
					"disconnect_count=%d, bpp_power=%dmW",
					charger->disconnect_count, GOOGLE_WLC_BPP_MISCOMM_POWER);
		} else {
			gvotable_cast_long_vote(charger->icl_ramp_target_votable, MODE_VOTER,
					GOOGLE_WLC_BPP_MAX_POWER, true);
		}
		google_wlc_clear_target(charger);
		google_wlc_vote_csi_auth(charger, false);
	}
}

/* Acquires status_lock */
static int google_wlc_trigger_icl_by_mode(struct google_wlc_data *charger, int mode)
{
	int ret = 0;

	logbuffer_devlog(charger->log, charger->dev, "Charging start, Mode = %s",
			 sys_op_mode_str[mode]);

	if (mode_is_mpp(mode) && mode != RX_MODE_WPC_MPP_NEGO) {
		if (charger->fast_mpp_ramp) {
			charger->icl_loop_mode = ICL_LOOP_TIMER;
			google_wlc_trigger_icl_ramp(charger, GOOGLE_WLC_RAMP_RETRY_INTERVAL);
			goto exit;
		}
		charger->icl_loop_mode = ICL_LOOP_INTERRUPT;
		/* Released in icl_ramp_timeout_work */
		__pm_stay_awake(charger->icl_ramp_timeout_ws);
		schedule_delayed_work(&charger->icl_ramp_timeout_work,
				      msecs_to_jiffies(GOOGLE_WLC_ICL_RAMP_TIMEOUT_MS));
		charger->chip->chip_enable_load_increase(charger, true);
	} else if (mode_is_epp(mode)) {
		charger->icl_loop_mode = ICL_LOOP_TIMER;
		google_wlc_trigger_icl_ramp(charger, GOOGLE_WLC_RAMP_RETRY_INTERVAL);
	} else if (mode == RX_MODE_WPC_BPP || mode == RX_MODE_WPC_MPP_RESTRICTED) {
		charger->icl_loop_mode = ICL_LOOP_TIMER;
		google_wlc_trigger_icl_ramp(charger, GOOGLE_WLC_RAMP_RETRY_INTERVAL);
	} else if (mode == RX_MODE_MPP_CLOAK || mode == RX_MODE_WPC_MPP_NEGO) {
		dev_err(charger->dev, "Charging but mode still %s, wait for mode change",
			sys_op_mode_str[mode]);
		ret = 0;
	} else {
		dev_err(charger->dev, "Charging but invalid mode: %s\n", sys_op_mode_str[mode]);
		ret = -EINVAL;
	}
exit:
	charger->ramp_done_check_count = 0;
	return ret;
}

static int google_wlc_max_voltage_by_mode_mv(struct google_wlc_data *charger, int mode)
{
	u32 reg;

	if (!google_wlc_is_present(charger) || !charger->online) {
		charger->last_psy_info.voltage_max = 0;
		goto exit;
	}

	if (charger->pdata->has_mpp25_wlc_dc && (mpp25_ready(charger->mpp25.state) ||
	    is_charger_mpp25_ok(charger->output_chg_config))) {
		if (charger->dc_data.max_voltage_limit)
			charger->last_psy_info.voltage_max = charger->dc_data.max_voltage_limit;
		else
			charger->last_psy_info.voltage_max = charger->pdata->wlc_dc_max_voltage;
		goto exit;
	}

	switch (mode) {
	case RX_MODE_AC_MISSING:
	case RX_MODE_UNKNOWN:
		charger->last_psy_info.voltage_max = 0;
		break;
	case RX_MODE_WPC_EPP_NEGO:
	case RX_MODE_WPC_MPP_NEGO:
	case RX_MODE_MPP_CLOAK:
	case RX_MODE_PDET:
		/* keep previous setting */
		break;
	case RX_MODE_WPC_BPP:
	case RX_MODE_WPC_EPP:
	case RX_MODE_WPC_MPP_RESTRICTED:
		if (charger->chip->chip_get_vout_set(charger, &reg))
			charger->last_psy_info.voltage_max = 0;
		else
			charger->last_psy_info.voltage_max = reg;
		break;
	case RX_MODE_WPC_MPP:
	case RX_MODE_WPC_MPP_CPM:
	case RX_MODE_WPC_MPP_NPM:
	case RX_MODE_WPC_MPP_LPM:
		if (!wpc_auth_passed(charger))
			charger->last_psy_info.voltage_max = MPP_NPM_MIN_VOLTAGE;
		else if (mpp25_ready(charger->mpp25.state))
			charger->last_psy_info.voltage_max = MPP25_MAX_OUTPUT_VOLTAGE_MV;
		else
			charger->last_psy_info.voltage_max = MPP_NPM_MAX_VOLTAGE;
		break;
	case RX_MODE_WPC_MPP_HPM:
		charger->last_psy_info.voltage_max = MPP25_MAX_OUTPUT_VOLTAGE_MV;
		break;
	default:
		charger->last_psy_info.voltage_max = 0;
		break;
	}
exit:
	return charger->last_psy_info.voltage_max;
}

static int google_wlc_max_power_by_mode_mw(struct google_wlc_data *charger, int mode)
{
	int mpp_pwr;

	if (!google_wlc_is_present(charger) || !charger->online)
		return 0;
	switch (mode) {
	case RX_MODE_AC_MISSING:
	case RX_MODE_UNKNOWN:
		return 0;
	case RX_MODE_WPC_EPP_NEGO:
	case RX_MODE_WPC_MPP_NEGO:
	case RX_MODE_MPP_CLOAK:
	case RX_MODE_PDET:
		/* keep previous setting */
		return -EAGAIN;
	case RX_MODE_WPC_BPP:
	case RX_MODE_WPC_MPP_RESTRICTED:
		return GOOGLE_WLC_BPP_MAX_POWER;
	case RX_MODE_WPC_EPP:
		return charger->pdata->wlc_epp_max_power_mw;
	case RX_MODE_WPC_MPP:
	case RX_MODE_WPC_MPP_CPM:
	case RX_MODE_WPC_MPP_NPM:
	case RX_MODE_WPC_MPP_LPM:
	case RX_MODE_WPC_MPP_HPM:
		if (!wpc_auth_passed(charger) || !charger->mpp_initialized)
			mpp_pwr = GOOGLE_WLC_PREAUTH_RAMP_TARGET;
		else if (mpp25_ready(charger->mpp25.state) && charger->mpp25.state != MPP25_ACTIVE)
			mpp_pwr = GOOGLE_WLC_MPP25_MAX_POWER;
		else if (!mpp25_ready(charger->mpp25.state) && charger->mpp25.state != MPP25_OFF)
			mpp_pwr = GOOGLE_WLC_PREAUTH_RAMP_TARGET;
		else if (charger->nego_power > GOOGLE_WLC_MPP_MAX_POWER &&
				charger->mpp25.state != MPP25_ACTIVE)
			mpp_pwr = GOOGLE_WLC_MPP_MAX_POWER;
		else if (charger->nego_power > 0)
			mpp_pwr = charger->nego_power;
		else
			mpp_pwr = GOOGLE_WLC_MPP_MAX_POWER;
		return mpp_pwr;
	default:
		return 0;
	}
}

static int google_wlc_max_current_by_mode_ma(struct google_wlc_data *charger, int mode)
{
	int max_power = 0;
	int volt = 0;

	if (!google_wlc_is_present(charger) || !charger->online) {
		charger->last_psy_info.current_max = 0;
		goto exit;
	}

	volt = google_wlc_max_voltage_by_mode_mv(charger, mode);
	if (volt == 0) {
		charger->last_psy_info.current_max = 0;
		goto exit;
	}

	max_power = google_wlc_max_power_by_mode_mw(charger, mode);
	if (max_power == -EAGAIN)
		goto exit;
	else if (max_power == 0)
		charger->last_psy_info.current_max = 0;
	else
		charger->last_psy_info.current_max = max_power * 1000 / volt;

exit:
	return charger->last_psy_info.current_max;
}

static bool is_charger_mpp25_ok(struct wlc_chg_config *config)
{
	return (config->max_volt_uv >= MV_TO_UV(MPP25_MAX_OUTPUT_VOLTAGE_MV));
}

static int google_wlc_get_new_chg_index(struct google_wlc_data *charger,
						  enum gsu_usecases uc)
{
	int index = bms_usecase_get_cur_chg_sel();

	if (index > charger->pdata->chg_configs_num - 1 || index < 0)
		index = 0;

	/* for usb or transition uc, keep the old index until uc goes back to a wlc charging one */
	if (bms_usecase_is_uc_wired(uc) || uc == GSU_MODE_WLC_PRESENT)
		index = charger->chg_config_index;
	else if (!bms_usecase_is_uc_wireless(uc))
		index = 0;

	return index;
}

static int google_wlc_set_charging(struct google_wlc_data *charger)
{
	int ret;
	u8 mode;
	bool already_charging = (charger->status == GOOGLE_WLC_STATUS_CHARGING);

	ret = charger->chip->chip_get_sys_mode(charger, &mode);
	if (ret)
		return ret;
	charger->status = GOOGLE_WLC_STATUS_CHARGING;
	charger->mode = mode;
	google_wlc_external_boost_en(charger, true);
	google_wlc_setup_by_mode(charger, mode);
	google_wlc_set_online(charger);

	if (!charger->dc_icl_votable)
		charger->dc_icl_votable = gvotable_election_get_handle(DC_ICL_VOTABLE_HANDLE);
	if (charger->dc_icl_votable)
		gvotable_cast_int_vote(charger->dc_icl_votable, OCP_VOTER,
				       charger->pdata->soft_ocp_icl,
				       charger->pdata->soft_ocp_icl > 0);

	if (charger->boot_on_wlc) {
		google_wlc_set_dc_icl(charger, GOOGLE_WLC_BOOTUP_UA);
		charger->icl_now = GOOGLE_WLC_BOOTUP_UA;
		charger->boot_on_wlc = false;
	} else if (!already_charging) {
		charger->chip->chip_enable_interrupts(charger);
		google_wlc_set_dc_icl(charger, charger->output_chg_config->init_cur_ua);
		charger->icl_now = charger->output_chg_config->init_cur_ua;
		google_wlc_update_psy(charger);
	} else {
		dev_dbg(charger->dev, "already charging, don't reset icl or interrupts");
	}
	if (charger->wlc_charge_enabled) {
		__pm_stay_awake(charger->init_charging_ws);
		mod_delayed_work(system_wq, &charger->charging_init_work, 0);
	}

	gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_VOTER, 0, false);
	schedule_delayed_work(&charger->csp_enable_work,
			      msecs_to_jiffies(GOOGLE_WLC_CSP_ENABLE_DELAY_MS));
	return 0;

}

/* must be called with status_lock held */
static void google_wlc_exit_charging(struct google_wlc_data *charger)
{
	cancel_delayed_work(&charger->icl_ramp_work);
	if (charger->wait_for_icl_ramp)
		complete(&charger->icl_ramp_done);
	cancel_delayed_work_sync(&charger->icl_ramp_timeout_work);
	cancel_delayed_work_sync(&charger->pla_ack_timeout_work);
	cancel_delayed_work_sync(&charger->limit_reason_work);
	/* not using sync because mpp_nego_timeout_work also takes status_lock (deadlock) */
	cancel_delayed_work(&charger->mpp_nego_timeout_work);
	__pm_relax(charger->icl_ramp_ws);
	__pm_relax(charger->icl_ramp_timeout_ws);
	__pm_relax(charger->limit_reason_ws);
	charger->icl_loop_state = ICL_LOOP_INACTIVE;
	charger->icl_accuracy_comp = 0;
	charger->rcs_backoff_count = 0;
	charger->force_icl_decrease = false;
	google_wlc_set_dc_icl(charger, GOOGLE_WLC_DEFAULT_STARTUP_UA);
	set_eds_state(charger, EDS_AVAILABLE);
	charger->icl_now = GOOGLE_WLC_DEFAULT_STARTUP_UA;
	gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_VOTER,
				GOOGLE_WLC_STARTUP_MW, true);
	set_inlim_enabled(charger, false);
}

static void google_wlc_exit_power_transfer(struct google_wlc_data *charger)
{
	charger->charger_started = false;
	cancel_delayed_work(&charger->charging_init_work);
	charger->charging_initialized = false;
	__pm_relax(charger->init_charging_ws);
	google_wlc_disable_csp(charger);
	google_wlc_abort_transfers(charger);
	gvotable_cast_long_vote(charger->icl_ramp_target_votable, MODE_VOTER, 0, true);
	cancel_delayed_work(&charger->chg_stable_work);
	charger->chg_stable_type = CHG_STABLE_TYPE_NONE;
	__pm_relax(charger->chg_stable_ws);
}

static int google_wlc_count_fsk_missing_offline(struct google_wlc_data *charger)
{
	const ktime_t disconnect_time = get_boot_sec(),
	      fsk_start_time = disconnect_time - GOOGLE_WLC_FSK_MISSING_INT_CHECK_S;
	int fsk_count = 0, i = 0;

	if (charger->last_opfreq < GOOGLE_WLC_OPFREQ_THRES)
		return 0;

	for (i = 0; i < FSK_MISSING_HISTORY_SIZE; i++) {
		ktime_t event_time = charger->fsk_missing_history[i];

		if (event_time >= fsk_start_time)
			fsk_count++;
	}

	dev_info(charger->dev, "check_fsk_missing fsk_missing_count=%d", fsk_count);
	return fsk_count;
}

static void google_wlc_check_fsk_missing_offline(struct google_wlc_data *charger)
{
	const ktime_t now = get_boot_sec(), offline_time = now - charger->wlc_offline_last;

	if (charger->usb_connected || charger->force_bpp ||
	    gpiod_get_raw_value_cansleep(charger->pdata->mode_gpio) != 1)
		return;

	if (offline_time > GOOGLE_WLC_CHG_SESSION_CHECK_S) {
		charger->session_txid = 0;
		charger->wlc_offline_count = 0;
		charger->mpp25_disabled = false;
		gvotable_cast_int_vote(charger->dc_avail_votable, WLC_VOTER,
				       GBMS_ALL_CP_CHG_DISABLED, false);
	} else if (offline_time < GOOGLE_WLC_CHG_SESSION_CHECK_S && charger->session_txid > 0) {
		if (charger->fsk_missing_count > 0)
			charger->wlc_offline_count++;
	}
	dev_info(charger->dev, "check_fsk_missing: session_txid:0x%06x, wlc_offline_count=%d",
			charger->session_txid, charger->wlc_offline_count);
}

/* must be called with status_lock held */
static int google_wlc_set_status(struct google_wlc_data *charger, enum google_wlc_status status)
{
	int ret = 0;
	u8 val8;
	int changed = false;
	u32 disconnect_debounce;

	dev_dbg(charger->dev, "Setting status: from %s to %s",
		google_wlc_status_str[charger->status], google_wlc_status_str[status]);

	if (status != charger->status)
		changed = true;

	if ((charger->status == GOOGLE_WLC_STATUS_CHARGING ||
	     charger->status == GOOGLE_WLC_STATUS_CLOAK_ENTERING) &&
	    status != GOOGLE_WLC_STATUS_CHARGING && status != GOOGLE_WLC_STATUS_CLOAK_ENTERING)
		google_wlc_exit_charging(charger);

	if ((charger->status == GOOGLE_WLC_STATUS_CHARGING ||
	     charger->status == GOOGLE_WLC_STATUS_DC_CHARGING) &&
	    (status != GOOGLE_WLC_STATUS_CHARGING && status != GOOGLE_WLC_STATUS_DC_CHARGING))
		google_wlc_exit_power_transfer(charger);

	/* Check in case unexpected irqs or other signals come in during inhibit */
	if (status != GOOGLE_WLC_STATUS_NOT_DETECTED &&
	    charger->disable_state == WLC_HARD_DISABLE) {
		dev_err(charger->dev, "Attempted status change to %s during inhibit, rejected",
			google_wlc_status_str[status]);
		goto exit;
	}

	switch (status) {
	case GOOGLE_WLC_STATUS_CHARGING:
		ret = google_wlc_set_charging(charger);
		break;
	case GOOGLE_WLC_STATUS_DC_CHARGING:
		if (!charger->wlc_dc_psy)
			charger->wlc_dc_psy = power_supply_get_by_name("wlcin-mains");
		if (!charger->wlc_dc_psy) {
			dev_err(charger->dev, "Entered WLC-DC but no dc PSY, disable");
			google_wlc_disable_mpp25(charger, false);
		} else {
			gvotable_run_election(charger->wlc_dc_power_votable, true);
			if (charger->usecase_ready) {
				__pm_stay_awake(charger->init_charging_ws);
				mod_delayed_work(system_wq, &charger->charging_init_work, 0);
			}
		}
		break;
	case GOOGLE_WLC_STATUS_NOT_DETECTED:
		/* Don't do anything if status was already NOT_DETECTED */
		if (charger->status == GOOGLE_WLC_STATUS_NOT_DETECTED)
			break;
		complete(&charger->charger_disconnect);
		complete(&charger->cloak_completion);
		charger->mode = RX_MODE_AC_MISSING;
		charger->vout_ready = false;
		charger->cloak_enter_reason = 0;
		charger->wpc_auth_type = false;
		charger->ask_mod_mode = ASK_MOD_MODE_ADAPTIVE_LOAD_MOD;
		charger->mpp_initialized = false;
		charger->mpp15_wlc_dc_ready = false;
		charger->charger_switch_check_finished = false;
		/* keep defer online */
		if (charger->feature & WLCF_DREAM_DEFER)
			feature_update(charger, WLCF_DREAM_DEFER);
		else
			feature_update(charger, 0);
		google_wlc_external_boost_en(charger, false);
		if (charger->last_opfreq > GOOGLE_WLC_OPFREQ_THRES && !charger->usb_connected) {
			charger->fsk_missing_count = google_wlc_count_fsk_missing_offline(charger);
			if (charger->fsk_missing_count > FSK_MISSING_COUNT_MAX &&
			    charger->session_txid == 0)
				charger->session_txid = charger->tx_id;
			charger->wlc_offline_last = get_boot_sec();
			charger->fsk_missing_last = 0;
			dev_info(charger->dev,
				 "check_fsk_missing: NOT_DETECTED: fsk_missing_count=%d",
				 charger->fsk_missing_count);
		}
		charger->tx_id = 0;
		cancel_delayed_work(&charger->mpp25_timeout_work);
		cancel_delayed_work_sync(&charger->pla_ack_timeout_work);
		cancel_delayed_work_sync(&charger->set_iop_vout_work);
		cancel_delayed_work_sync(&charger->limit_reason_work);
		/* not using sync because mpp_nego_timeout_work also takes status_lock (deadlock) */
		cancel_delayed_work(&charger->mpp_nego_timeout_work);
		if (cancel_delayed_work(&charger->xid_work))
			__pm_relax(charger->xid_ws);
		if (charger->mpp25.state != MPP25_OFF)
			google_wlc_exit_mpp25(charger);
		if (charger->wait_for_mode > 0)
			complete(&charger->pwrmode_switch);
		memset(&charger->dc_data, 0, sizeof(struct wlc_dc_data));
		memset(&charger->mpp25, 0, sizeof(struct mpp25_data));
		memset(charger->tx_id_str, 0, sizeof(charger->tx_id_str));
		google_wlc_uevent(charger, UEVENT_WLC_OFF);
		google_wlc_clear_target(charger);
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_AUTH_VOTER,
					0, false);
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, NEGO_VOTER, 0, false);
		gvotable_cast_long_vote(charger->wlc_dc_power_votable, NEGO_VOTER, 0, false);
		charger->nego_power = 0;
		charger->limit_reason = 0;
		if (charger->disconnect_total_count < 0xFFFF)
			charger->disconnect_total_count++;
		charger->bpp_ramp_done_target = 0;
		charger->bpp_ramp_done_check = false;
		charger->qi_id = 0;
		charger->limit_reason_retry_count = 0;
		if (charger->retail_power_limit)
			gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_RETAIL_VOTER,
						0, false);
		charger->retail_power_limit = false;
		if (!charger->chg_mode_votable)
			charger->chg_mode_votable =
				gvotable_election_get_handle(GBMS_MODE_VOTABLE);
		if (charger->chg_mode_votable)
			gvotable_cast_long_vote(charger->chg_mode_votable,
				WLC_VOTER,
				_bms_usecase_meta_async_set(GBMS_CHGR_MODE_WLC_RX, 1),
				false);
		ratelimit_state_init(&present_ratelimit, PRESENT_RATELIMIT_INTERVAL,
				     PRESENT_RATELIMIT_BURST);

		if (charger->online || charger->status == GOOGLE_WLC_STATUS_CLOAK) {
			dev_info(charger->dev, "Schedule disconnect, status: %s",
				 google_wlc_status_str[charger->status]);
			__pm_stay_awake(charger->disconnect_ws);
			pm_system_wakeup();
			if (charger->wait_for_dd)
				disconnect_debounce = GOOGLE_WLC_DISCONNECT_DEBOUNCE_MS * 2;
			else
				disconnect_debounce = GOOGLE_WLC_DISCONNECT_DEBOUNCE_MS;
			schedule_delayed_work(&charger->disconnect_work,
					      msecs_to_jiffies(disconnect_debounce));
			charger->disconnect_count++;
		}
		google_wlc_update_psy(charger);
		break;
	case GOOGLE_WLC_STATUS_CLOAK_ENTERING:
		__pm_stay_awake(charger->presence_ws);
		pm_system_wakeup();
		schedule_delayed_work(&charger->present_check_work,
				      msecs_to_jiffies(GOOGLE_WLC_PRESENT_CHECK_INTERVAL_MS));
		ret = charger->chip->chip_set_cloak_mode(charger, true,
							 charger->cloak_enter_reason);
		if (ret) {
			dev_err(charger->dev, "Fail cloak enter(%d), reschedule disable_work\n",
				ret);
			mod_delayed_work(system_wq, &charger->disable_work,
				 msecs_to_jiffies(GOOGLE_WLC_CLOAK_DEBOUNCE_MS));
		}
		break;
	case GOOGLE_WLC_STATUS_CLOAK_EXITING:
		if (charger->status == GOOGLE_WLC_STATUS_CLOAK_ENTERING) {
			cancel_delayed_work(&charger->present_check_work);
			__pm_relax(charger->presence_ws);
		}
		ret = charger->chip->chip_get_cloak_reason(charger, &val8);
		if (ret == 0 && val8 == CLOAK_TX_INITIATED) {
			dev_info(charger->dev, "TX initiated cloak; stay in cloak mode\n");
			goto exit;
		}
		charger->cloak_enter_reason = 0;
		charger->chip->chip_set_cloak_mode(charger, false, 0);
		__pm_stay_awake(charger->notifier_ws);
		pm_system_wakeup();
		break;
	case GOOGLE_WLC_STATUS_CLOAK:
		dev_info(charger->dev, "Detected Cloak Mode");
		charger->cloak_enter_reason = 0;
		cancel_delayed_work(&charger->present_check_work);
		__pm_relax(charger->presence_ws);
		break;
	case GOOGLE_WLC_STATUS_DETECTED:
		__pm_stay_awake(charger->presence_ws);
		pm_system_wakeup();
		cancel_delayed_work(&charger->disconnect_work);
		__pm_relax(charger->disconnect_ws);

		schedule_delayed_work(&charger->present_check_work,
					   msecs_to_jiffies(GOOGLE_WLC_PRESENT_CHECK_INTERVAL_MS));
		google_wlc_uevent(charger, UEVENT_WLC_ON);
		if (!charger->usb_connected)
			google_wlc_set_online(charger);

		if (!IS_ERR_OR_NULL(charger->pdata->ap5v_gpio))
			gpiod_set_value_cansleep(charger->pdata->ap5v_gpio, 1);
		break;
	default:
		dev_err(charger->dev, "Unhandled status: %d", status);
	}

	if (ret) {
		dev_err(charger->dev, "Error during set charger status: %s, ret = %d\n",
			google_wlc_status_str[status], ret);
	} else if (changed) {
		charger->status = status;
		logbuffer_devlog(charger->log, charger->dev, "%s: Charger status set to %s",
				 __func__, google_wlc_status_str[charger->status]);
		if (status == GOOGLE_WLC_STATUS_CHARGING)
			google_wlc_uevent(charger, UEVENT_WLC_ON);
	}
exit:
	return ret;
}

static int google_wlc_handle_capacity(struct google_wlc_data *charger, int capacity_raw)
{
	int ret;

	if (charger->last_capacity == capacity_raw &&
	    (capacity_raw != 100 || !mode_is_epp(charger->mode)))
		return 0;

	google_wlc_dream_defend(charger);
	if (charger->pdata->support_epp && !charger->online &&
	    charger->last_capacity < charger->mitigate_threshold)
		if (!charger->manual_force_bpp)
			google_wlc_set_mode_gpio(charger, RX_MODE_WPC_EPP);

	if ((charger->last_capacity == capacity_raw) && capacity_raw >= 100)
		return 0;

	charger->last_capacity = capacity_raw;

	if (charger->online && charger->last_capacity >= 0 && charger->last_capacity <= 100)
		mod_delayed_work(system_wq, &charger->charge_stats_hda_work, 0);

	mutex_lock(&charger->status_lock);
	if (capacity_raw > WLC_DC_MAX_SOC && mpp25_running(charger->mpp25.state)) {
		logbuffer_devlog(charger->log, charger->dev, "WLC_DC: SOC High (%d), exit",
				 capacity_raw);
		google_wlc_disable_mpp25(charger, false);
	}

	if (!charger->mod_enable && charger->last_capacity >= charger->pdata->mod_soc &&
	    google_wlc_is_present(charger)) {
		charger->mod_enable = true;
		charger->chip->chip_set_dynamic_mod(charger);
	}
	if (charger->pdata->mod_soc > 0)
		charger->mod_enable = charger->last_capacity >= charger->pdata->mod_soc;

	google_wlc_handle_retail_charge(charger);
	mutex_unlock(&charger->status_lock);

	if (charger->fw_data.update_check_pending &&
	    charger->last_capacity > WLC_FWUPDATE_SOC_THRESHOLD)
		mod_delayed_work(system_wq, &charger->wlc_fw_update_work,
				 msecs_to_jiffies(WLC_FW_CHECK_TIMEOUT_MS));

	if (!charger->send_csp)
		return 0;

	ret = charger->chip->chip_send_csp(charger);
	if (ret)
		dev_err(&charger->client->dev, "Could not send csp: %d\n", ret);

	return ret;
}

static void google_wlc_notifier_check_dc(struct google_wlc_data *charger)
{
	int dc_in;
	bool present_check = true;

	mutex_lock(&charger->status_lock);
	dc_in = google_wlc_has_dc_in(charger);
	if (dc_in < 0 || (charger->online_disable && dc_in == 1)) {
		dev_info(&charger->client->dev, "reschedule notifier(%d)\n",
			 dc_in);
		schedule_delayed_work(&charger->psy_notifier_work,
			msecs_to_jiffies(GOOGLE_WLC_DCIN_RETRY_DELAY_MS));
		goto exit;
	}

	/* ignore dc_in during WLC-DC SWC charging */
	if (dc_in && in_wlc_dc(charger))
		goto exit;

	dev_info(&charger->client->dev, "dc status is %d\n", dc_in);

	if (charger->log)
		logbuffer_devlog(charger->log, charger->dev,
				"check_dc: online=%d dc present=%d",
				charger->online, dc_in);

	if (dc_in) {
		cancel_delayed_work(&charger->present_check_work);
		__pm_relax(charger->presence_ws);
		cancel_delayed_work(&charger->disconnect_work);
		__pm_relax(charger->disconnect_ws);
	} else {
		present_check = google_wlc_confirm_online(charger);
		if (!present_check && charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED) {
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_NOT_DETECTED);
			goto exit;
		}
	}

	if (dc_in && charger->mode == RX_MODE_MPP_CLOAK) {
		dev_info(charger->dev, "ignore dcin during cloak");
		goto exit;
	}

	if (charger->disable_state == WLC_SOFT_DISABLE) {
		if (dc_in) {
			if (charger->status == GOOGLE_WLC_STATUS_NOT_DETECTED)
				google_wlc_set_status(charger, GOOGLE_WLC_STATUS_DETECTED);
			dev_err(charger->dev, "DCIN while disabled, reschedule disable_work\n");
			mod_delayed_work(system_wq, &charger->disable_work,
				 msecs_to_jiffies(0));
		}
		goto exit;
	}
	if (charger->disable_state == WLC_CLOAK_ONLY && mode_is_mpp(charger->mode)) {
		if (dc_in && charger->status != GOOGLE_WLC_STATUS_CLOAK_ENTERING) {
			dev_err(charger->dev,
				"DCIN while wc_rx, set charging and reschedule disable_work\n");
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CHARGING);
			mod_delayed_work(system_wq, &charger->disable_work,
				 msecs_to_jiffies(GOOGLE_WLC_CLOAK_DEBOUNCE_MS));
		}
		goto exit;
	}

	if (dc_in)
		google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CHARGING);
	else if (charger->status != GOOGLE_WLC_STATUS_CLOAK && charger->online &&
		 charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED)
		google_wlc_set_status(charger, GOOGLE_WLC_STATUS_DETECTED);
exit:
	mutex_unlock(&charger->status_lock);
}

/* This function periodically runs when the charger VRECTON is detected, to
 * continue checking if the charger is there. If the charger is no longer there,
 * it sets online back to 0.
 */
static void google_wlc_present_check_work(struct work_struct *work)
{
	int res;
	u8 mode = 0;
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, present_check_work.work);

	if (charger->online_disable) {
		__pm_relax(charger->presence_ws);
		return;
	}
	mutex_lock(&charger->status_lock);
	if (charger->status != GOOGLE_WLC_STATUS_DETECTED &&
	    charger->status != GOOGLE_WLC_STATUS_CLOAK_ENTERING) {
		goto exit;
	}
	res = charger->chip->chip_get_sys_mode(charger, &mode);
	if (res != 0 || (charger->mode_ready && mode == RX_MODE_AC_MISSING)) {
		logbuffer_devlog(charger->log, charger->dev,
			"wlc i2c no longer available, online=%d -> 0",
			charger->online);
		if (charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED)
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_NOT_DETECTED);
		goto exit;
	}

	if (charger->mode == RX_MODE_MPP_CLOAK || charger->mode == RX_MODE_PDET) {
		u8 val8 = 0;

		if (charger->mode == RX_MODE_MPP_CLOAK)
			charger->chip->chip_get_cloak_reason(charger, &val8);
		if (val8 != CLOAK_TX_INITIATED &&
		    charger->disable_state == WLC_NOT_DISABLED &&
		    charger->mpp25.fod_cloak == FOD_CLOAK_NONE) {
			dev_info(charger->dev, "In cloak mode unexpectedly; exit cloak\n");
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK_EXITING);
		} else {
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK);
		}
		goto exit;
	}
	if (__ratelimit(&present_ratelimit))
		dev_info(charger->dev, "present_check: online=%d mode=%s", charger->online,
			 sys_op_mode_str[mode]);
	/* Continue to check present status */
	mod_delayed_work(system_wq, &charger->present_check_work,
			 msecs_to_jiffies(GOOGLE_WLC_PRESENT_CHECK_INTERVAL_MS));
	mutex_unlock(&charger->status_lock);
	/*
	 * __pm_relax(presence_ws) will be called when present_check_work is cancelled or when it
	 * exits after rescheduling
	 */
	return;
exit:
	__pm_relax(charger->presence_ws);
	dev_info(charger->dev, "present_check exit: online=%d status=%s",
		 charger->online, google_wlc_status_str[charger->status]);
	mutex_unlock(&charger->status_lock);
}

/*
 * This function gets scheduled when we detect the charger is removed. Debounce the removal
 */
static void google_wlc_disconnect_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, disconnect_work.work);

	mutex_lock(&charger->status_lock);
	charger->wait_for_dd = false;
	if (charger->status == GOOGLE_WLC_STATUS_NOT_DETECTED) {
		dev_info(charger->dev, "Disconnect confirmed");
		google_wlc_set_offline(charger);
		if (charger->usb_connected)
			gvotable_cast_int_vote(charger->wlc_disable_votable, USB_CONN_VOTER,
			       WLC_HARD_DISABLE, true);
	}
	__pm_relax(charger->disconnect_ws);
	mutex_unlock(&charger->status_lock);
}

static void google_wlc_soc_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, soc_work.work);
	union power_supply_propval prop = { };
	int ret, val;
	int soc_raw = -1;

	if (charger->shutdown_notify)
		return;

	if (!charger->pdata->batt_psy) {
		charger->pdata->batt_psy = devm_power_supply_get_by_phandle(charger->dev,
								     "google,fg-power-supply");
		if (IS_ERR_OR_NULL(charger->pdata->batt_psy)) {
			mod_delayed_work(system_wq, &charger->soc_work, msecs_to_jiffies(1000));
			dev_info(charger->dev, "wait for fg\n");
			return;
		}

		dev_info(charger->dev, "Reading SOC from %s\n",
			 charger->pdata->batt_psy->desc &&
			 charger->pdata->batt_psy->desc->name ?
			 charger->pdata->batt_psy->desc->name : "<>");
	}

	/* triggered from notifier_cb */

	val = GPSY_GET_INT_PROP(charger->pdata->batt_psy, GBMS_PROP_CAPACITY_RAW, &ret);
	if (ret == 0) {
		soc_raw = qnum_toint(qnum_from_q8_8(val));

		ret = power_supply_get_property(charger->pdata->batt_psy, POWER_SUPPLY_PROP_STATUS,
						&prop);
		if (ret == 0 && (prop.intval == POWER_SUPPLY_STATUS_FULL))
			soc_raw = 101;
	}
	dev_dbg(charger->dev, "soc_work: soc=%d, err=%d\n", soc_raw, ret);

	if (soc_raw >= 0 && soc_raw <= 101) {
		google_wlc_handle_capacity(charger, soc_raw);
	} else if (charger->last_capacity < 0) {
		dev_err(charger->dev, "soc_work: got invalid soc=%d, err=%d\n", soc_raw, ret);
		mod_delayed_work(system_wq, &charger->soc_work, msecs_to_jiffies(1000));
	}
}

static void google_wlc_notifier_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(
		work, struct google_wlc_data, psy_notifier_work.work);

	if (charger->shutdown_notify)
		goto out;

	if (charger->online_disable_override) {
		dev_info(&charger->client->dev, "online disabled: ignore notifier\n");
		goto out;
	}

	google_wlc_notifier_check_dc(charger);

out:
	__pm_relax(charger->notifier_ws);
}

static int google_wlc_notifier_cb(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct google_wlc_data *charger =
		container_of(nb, struct google_wlc_data, nb);
	const char *batt_name = IS_ERR_OR_NULL(charger->pdata->batt_psy) ||
				!charger->pdata->batt_psy->desc ?
				NULL : charger->pdata->batt_psy->desc->name;

	if (event != PSY_EVENT_PROP_CHANGED)
		goto out;

	if (strcmp(psy->desc->name, GOOGLE_WLCIN_MAINS_NAME) == 0) {
		/* "wlcin-mains" power supply, which corresponds to the  main charger,
		 * has changed
		 */
		charger->chgr_psy = psy;
		cancel_delayed_work_sync(&charger->psy_notifier_work);
		__pm_stay_awake(charger->notifier_ws);
		pm_system_wakeup();
		mod_delayed_work(system_wq, &charger->psy_notifier_work,
					msecs_to_jiffies(GOOGLE_WLC_NOTIFIER_DELAY_MS));
	} else if (batt_name && strcmp(psy->desc->name, batt_name) == 0) {
		schedule_delayed_work(&charger->soc_work, 0);
	}

out:
	return NOTIFY_OK;
}

/* must be called with status lock held */
static void google_wlc_wait_for_cloak(struct google_wlc_data *charger, const char *reason,
				     bool cloak_en)
{
	int ret = 0;
	u8 mode;

	ret = charger->chip->chip_get_sys_mode(charger, &mode);
	if (ret < 0) {
		dev_info(charger->dev, "Unable to read powermode");
		return;
	}
	/* if already in desired state or not one of mpp/cloak, just cast vote and return */
	if ((cloak_en && mode == RX_MODE_MPP_CLOAK) ||
	    (!cloak_en && mode != RX_MODE_MPP_CLOAK) ||
	    (!mode_is_mpp(mode) && mode != RX_MODE_MPP_CLOAK)) {
		gvotable_cast_int_vote(charger->wlc_disable_votable, reason,
		WLC_CLOAK_ONLY, cloak_en);
		return;
	}
	/* reset completion to 0 just in case but it should already be 0 */
	reinit_completion(&charger->cloak_completion);
	/* every function that changes this should be holding status_lock */
	if (cloak_en)
		charger->wait_for_cloak = true;
	if (!cloak_en)
		charger->wait_for_exit_cloak = true;
	ret = gvotable_cast_int_vote(charger->wlc_disable_votable, reason,
		WLC_CLOAK_ONLY, cloak_en);
	/* If callback didn't run, force re-run it */
	if (ret == 0)
		gvotable_run_election(charger->wlc_disable_votable, true);
	/* Release lock so the completion wait can return */
	mutex_unlock(&charger->status_lock);
	wait_for_completion_interruptible_timeout(&charger->cloak_completion,
		msecs_to_jiffies(WLC_DISABLE_TIMEOUT));
	charger->wait_for_cloak = false;
	charger->wait_for_exit_cloak = false;
	mutex_lock(&charger->status_lock);
	ret = charger->chip->chip_get_sys_mode(charger, &mode);
	if (ret < 0) {
		dev_info(charger->dev, "Unable to read powermode after cloak wait");
		return;
	}
	if (cloak_en && mode != RX_MODE_MPP_CLOAK) {
		dev_info(charger->dev, "Cloak enter fail, en = %d mode = %s\n",
		 cloak_en, sys_op_mode_str[mode]);
		mutex_unlock(&charger->status_lock);
		google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
		mutex_lock(&charger->status_lock);
		schedule_delayed_work(&charger->psy_notifier_work, GOOGLE_WLC_NOTIFIER_DELAY_MS);
		return;
	}
	if (!cloak_en && mode == RX_MODE_MPP_CLOAK) {
		dev_info(charger->dev, "Cloak exit fail, en = %d mode = %s\n",
		 cloak_en, sys_op_mode_str[mode]);
		mutex_unlock(&charger->status_lock);
		google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
		mutex_lock(&charger->status_lock);
		schedule_delayed_work(&charger->psy_notifier_work, GOOGLE_WLC_NOTIFIER_DELAY_MS);
		return;
	}
	dev_info(charger->dev, "Cloak wait is done, en = %d mode = %s\n",
		 cloak_en, sys_op_mode_str[mode]);
	return;
}

static void google_wlc_wait_for_sending_ept(struct google_wlc_data *charger, enum ept_reason reason)
{
	unsigned long timeout;
	bool is_connected;
	int ret;

	mutex_lock(&charger->status_lock);

	is_connected = charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED;

	if (!is_connected) {
		mutex_unlock(&charger->status_lock);
		return;
	}
	reinit_completion(&charger->charger_disconnect);

	ret = charger->chip->chip_send_ept(charger, reason);

	mutex_unlock(&charger->status_lock);
	if (ret == 0 && is_connected) {
		timeout = wait_for_completion_timeout(&charger->charger_disconnect,
				msecs_to_jiffies(WLC_DISCONNECT_TIMEOUT));
		if (timeout == 0)
			dev_warn(charger->dev, "Timeout waiting for EPT disconnect\n");
	}
	charger->vrect_count = 0;
}

/* must be called with status lock held */
static void google_wlc_switch_powermode(struct google_wlc_data *charger, enum mpp_powermode pwrmode)
{
	int target_mode;
	int ret;
	u8 mode;
	int retry = 10;

	do {
		ret = charger->chip->chip_get_sys_mode(charger, &mode);
		if (ret < 0) {
			dev_info(charger->dev, "Unable to read powermode");
			return;
		}
		if (mode != RX_MODE_WPC_MPP_NEGO)
			break;
		mutex_unlock(&charger->status_lock);
		retry -= 1;
		dev_info(charger->dev, "Charger is in mpp nego, release lock & wait 1s");
		msleep(1000);
		mutex_lock(&charger->status_lock);
	} while (retry > 0);

	if (mode != RX_MODE_WPC_MPP_NPM && mode != RX_MODE_WPC_MPP_HPM &&
	    mode != RX_MODE_WPC_MPP_LPM)
		return;
	if (pwrmode == MPP_POWERMODE_HIGH)
		target_mode = RX_MODE_WPC_MPP_HPM;
	else if (pwrmode == MPP_POWERMODE_NOMINAL)
		target_mode = RX_MODE_WPC_MPP_NPM;
	else
		return;
	if (target_mode == mode)
		return;
	dev_info(charger->dev, "Switching powermode to %s", mpp_powermode_str[pwrmode]);
	reinit_completion(&charger->pwrmode_switch);
	charger->wait_for_mode = target_mode;
	charger->pwrmode_switch_initial_done = false;
	charger->pwrmode_switch_renego_done = false;
	ret = charger->chip->chip_set_mpp_powermode(charger, pwrmode, true);
	if (ret != 0) {
		dev_info(charger->dev, "Fail to request %s",
			 mpp_powermode_str[pwrmode]);
		charger->wait_for_mode = -1;
		return;
	}
	mutex_unlock(&charger->status_lock);
	wait_for_completion_interruptible_timeout(&charger->pwrmode_switch,
		msecs_to_jiffies(MPP25_PM_SWITCH_TIMEOUT_MS));
	charger->wait_for_mode = -1;
	charger->pwrmode_switch_initial_done = false;
	charger->pwrmode_switch_renego_done = false;
	mutex_lock(&charger->status_lock);

	ret = charger->chip->chip_get_sys_mode(charger, &mode);
	if (ret < 0) {
		dev_err(charger->dev, "Unable to get mode");
	} else if (mode == target_mode) {
		dev_info(charger->dev, "Power mode switched to %s", mpp_powermode_str[pwrmode]);
	} else if (pwrmode != MPP_POWERMODE_HIGH && mode == RX_MODE_WPC_MPP_HPM) {
		logbuffer_devlog(charger->log, charger->dev,
				 "Unable to exit HPM, reset charger");
		mutex_unlock(&charger->status_lock);
		google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
		mutex_lock(&charger->status_lock);
		schedule_delayed_work(&charger->psy_notifier_work, GOOGLE_WLC_NOTIFIER_DELAY_MS);
	} else if (pwrmode == MPP_POWERMODE_HIGH && mode != RX_MODE_WPC_MPP_HPM) {
		logbuffer_devlog(charger->log, charger->dev,
				 "Unable to enter HPM, disable MPP25");
		google_wlc_disable_mpp25(charger, true);
	} else {
		dev_err(charger->dev,
			"Unable to switch power mode to %s", mpp_powermode_str[pwrmode]);
	}

}

static void google_wlc_update_usecase_cloak(struct google_wlc_data *charger,
					    enum gsu_usecases to_uc)
{
	if (bms_usecase_is_uc_wireless(to_uc) && !bms_usecase_is_uc_charging_enabled(to_uc) &&
	    charger->enable_eoc_cloak && to_uc != GSU_MODE_WLC_PRESENT &&
	    to_uc != GSU_MODE_WLC_RX_STDBY) {
		google_wlc_wait_for_cloak(charger, CHG_ENABLE_VOTER, true);
	} else if (bms_usecase_is_uc_wireless(to_uc) && to_uc != GSU_MODE_WLC_PRESENT) {
		google_wlc_wait_for_cloak(charger, CHG_ENABLE_VOTER, false);
	} else {
		gvotable_cast_int_vote(charger->wlc_disable_votable, CHG_ENABLE_VOTER,
				       WLC_CLOAK_ONLY, false);
	}
}

static void google_wlc_usecase_setup_cb(void *d, enum gsu_usecases from_uc,
				 enum gsu_usecases to_uc)
{
	struct google_wlc_data *charger = (struct google_wlc_data *)d;
	int ret = 0;
	const bool was_charging = bms_usecase_is_uc_wireless(from_uc) &&
			    bms_usecase_is_uc_charging_enabled(from_uc);
	const bool will_charge = bms_usecase_is_uc_wireless(to_uc) &&
				 bms_usecase_is_uc_charging_enabled(to_uc);
	bool ramp_down = false;
	int new_index, i;
	u8 mode;

	new_index = google_wlc_get_new_chg_index(charger, to_uc);

	dev_info(charger->dev, "usecase setup notification:%s(%d)->%s(%d)",
			 bms_usecase_to_str(from_uc), from_uc,
			 bms_usecase_to_str(to_uc), to_uc);

	mutex_lock(&charger->status_lock);
	charger->usecase_ready = false;
	charger->wlc_charge_enabled = false;
	if (bms_usecase_is_uc_wireless(from_uc)) {
		cancel_delayed_work(&charger->chg_stable_work);
		charger->chg_stable_type = CHG_STABLE_TYPE_NONE;
		__pm_relax(charger->chg_stable_ws);
	}
	if (bms_usecase_is_uc_wired(to_uc) && !charger->usb_connected) {
		if (charger->online &&
		    (mode_is_mpp(charger->mode) || charger->mode == RX_MODE_MPP_CLOAK)) {
			google_wlc_wait_for_cloak(charger, USB_CONN_VOTER, true);
		} else {
			gvotable_cast_int_vote(charger->wlc_disable_votable, USB_CONN_VOTER,
			       WLC_HARD_DISABLE, true);
		}
		charger->usb_connected = true;
		google_wlc_set_offline(charger);
		charger->mpp15_wlc_dc_ready = false;
		charger->mpp_initialized = false;
	} else if (!bms_usecase_is_uc_wired(to_uc) && charger->usb_connected) {
		dev_info(charger->dev, "Exit USB, re-enable normal WLC");
		if (google_wlc_is_present(charger)) {
			google_wlc_wait_for_cloak(charger, USB_CONN_VOTER, false);
			google_wlc_check_mpp_initialized(charger);
			google_wlc_set_online(charger);
		} else {
			gvotable_cast_int_vote(charger->wlc_disable_votable, USB_CONN_VOTER,
			       0, false);
		}
		charger->usb_connected = false;
	}

	ret = charger->chip->chip_get_sys_mode(charger, &mode);
	if (!google_wlc_is_present(charger) || ret != 0) {
		if (is_usecase_wlc_dc(from_uc)) {
			GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_ENABLE_SWITCH_CAP,
				      CP_DISABLE_SWITCH_CAP);
			charger->dc_data.swc_en_state = SWC_DISABLED;
		}
		mutex_unlock(&charger->status_lock);
		return;
	} else {
		charger->mode = mode;
		if (charger->no_charger_switch_cloak) {
			google_wlc_update_usecase_cloak(charger, to_uc);
		}
		if (was_charging && (new_index != charger->chg_config_index || !will_charge)) {
			if (mode_is_mpp(charger->mode) && charger->mpp_initialized &&
			    !charger->no_charger_switch_cloak)
				google_wlc_wait_for_cloak(charger, CHG_ENABLE_VOTER, true);
			else
				ramp_down = true;
		}
	}
	if (is_usecase_wlc_dc(from_uc) &&
	    (!is_usecase_wlc_dc(to_uc) || new_index != charger->chg_config_index)) {
		logbuffer_devlog(charger->log, charger->dev, "turn off swc due to exit wlc_dc");
		if (charger->no_charger_switch_cloak)
			GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_ENABLE_SWITCH_CAP,
				      CP_DISABLE_SWITCH_CAP);
		else
			GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_ENABLE_SWITCH_CAP,
				      CP_DISABLE_SWITCH_CAP_NO_RAMP_DOWN);
		i = GOOGLE_WLC_SWC_STATE_CHECK_COUNT;
		while (i > 0) {
			if (charger->dc_data.swc_on == false) {
				dev_info(charger->dev, "swc_on=%d", charger->dc_data.swc_on);
				break;
			}
			msleep(1000);
			i--;
		};
		if (i == 0) {
			mutex_unlock(&charger->status_lock);
			google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
			mutex_lock(&charger->status_lock);
			schedule_delayed_work(&charger->psy_notifier_work,
					      GOOGLE_WLC_NOTIFIER_DELAY_MS);
			dev_err(charger->dev, "fail to disable SWC, swc_on=%d",
				charger->dc_data.swc_on);
		}
		charger->dc_data.swc_en_state = SWC_DISABLED;
	}

	if (charger->no_charger_switch_cloak && bms_usecase_is_uc_standby(to_uc) &&
	    to_uc != GSU_MODE_WLC_PRESENT && to_uc != GSU_MODE_WLC_RX_STDBY)
		google_wlc_wait_for_cloak(charger, CHG_ENABLE_VOTER, true);

	mode = charger->mode;
	mutex_unlock(&charger->status_lock);
	if (ramp_down) {
		if (charger->output_chg_config->exit_by_ept && charger->mpp25.setup_done &&
		    mode != RX_MODE_MPP_CLOAK) {
			logbuffer_devlog(charger->log, charger->dev,
					 "exiting charger by EPT, mode=%s",
					 sys_op_mode_str[mode]);
			google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
			schedule_delayed_work(&charger->psy_notifier_work,
					      GOOGLE_WLC_NOTIFIER_DELAY_MS);
			charger->mpp25_disabled = true;
			logbuffer_devlog(charger->log, charger->dev,
					 "MPP25: Disable due to charger exit EPT");
		}
		google_wlc_ramp_down_icl(charger, CHG_ENABLE_VOTER);
	}
	if (charger->mode == RX_MODE_MPP_CLOAK) {
		google_wlc_set_dc_icl(charger, GOOGLE_WLC_DEFAULT_STARTUP_UA);
		charger->icl_now = GOOGLE_WLC_DEFAULT_STARTUP_UA;
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, CHG_ENABLE_VOTER,
			GOOGLE_WLC_CHG_SUSPEND_MW, true);
	}
}

static void google_wlc_usecase_transition_cb(void *d, enum gsu_usecases from_uc,
				 enum gsu_usecases to_uc)
{
	struct google_wlc_data *charger = (struct google_wlc_data *)d;
	int ret;
	struct wlc_chg_config *config;
	const bool should_charge = bms_usecase_is_uc_wireless(to_uc) &&
				   bms_usecase_is_uc_charging_enabled(to_uc) &&
				   !is_usecase_wlc_dc(to_uc);
	u8 mode;
	int config_index = 0;
	bool was_mpp25 = false;

	dev_info(charger->dev, "usecase transition notification:%s(%d)->%s(%d)",
			 bms_usecase_to_str(from_uc), from_uc,
			 bms_usecase_to_str(to_uc), to_uc);

	mutex_lock(&charger->status_lock);
	if (is_charger_mpp25_ok(charger->output_chg_config))
		was_mpp25 = true;

	config_index = google_wlc_get_new_chg_index(charger, to_uc);
	charger->output_chg_config = &charger->pdata->chg_configs[config_index];
	if (config_index == charger->chg_config_index)
		goto setup_charging;
	if (was_mpp25) {
		charger->mpp25_disabled = true;
		google_wlc_exit_mpp25(charger);
		logbuffer_devlog(charger->log, charger->dev, "MPP25: Disable due to exit usecase");
	}
	dev_info(charger->dev, "USECASE chg index: %d->%d", charger->chg_config_index,
			config_index);

	charger->chg_config_index = config_index;
	config = charger->output_chg_config;
	google_wlc_thermal_icl_vote(charger);

	google_wlc_check_mpp_initialized(charger);
	if (!charger->mpp_initialized)
		goto setup_wlc;

	if (config->mod_change_timing == ASK_MOD_TIMING_EARLY &&
	    charger->ask_mod_mode != config->mod_mode) {
		ret = charger->chip->chip_set_mod_mode(charger, config->mod_mode);
		if (ret == 0)
			charger->ask_mod_mode = charger->output_chg_config->mod_mode;
	}

	google_wlc_wait_for_cloak(charger, CHG_ENABLE_VOTER, false);

	charger->chip->chip_enable_auto_vout(charger, config->auto_vout);
	if (charger->output_chg_config->mod_change_timing == ASK_MOD_TIMING_POS_CAP_GPIO) {
		ret = charger->chip->chip_set_mod_mode(charger, ASK_MOD_MODE_POS_CAP_MOD);
		if (ret == 0)
			charger->ask_mod_mode = ASK_MOD_MODE_POS_CAP_MOD;

	}
	if (mpp25_ready(charger->mpp25.state) && is_charger_mpp25_ok(config)) {
		ret = mpp25_setup(charger);
		if (ret < 0) {
			dev_err(charger->dev, "MPP25 setup failed");
			goto setup_charging;
		}
	}
	if (config->charger_type == CHARGER_TYPE_SWC) {
		gvotable_cast_long_vote(charger->wlc_dc_power_votable, MODE_VOTER,
					GOOGLE_WLC_MPP_MAX_POWER, true);
		cancel_delayed_work(&charger->icl_ramp_work);
		charger->chip->chip_enable_load_increase(charger, false);
	}

	google_wlc_switch_powermode(charger, config->target_power_mode);
	charger->mpp25.setup_done = true;

setup_wlc:
	google_wlc_set_dc_icl(charger, config->init_cur_ua);
	charger->icl_now = config->init_cur_ua;
setup_charging:
	if (!should_charge) {
		google_wlc_set_dc_icl(charger, GOOGLE_WLC_DEFAULT_STARTUP_UA);
		charger->icl_now = GOOGLE_WLC_DEFAULT_STARTUP_UA;
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, CHG_ENABLE_VOTER,
			GOOGLE_WLC_CHG_SUSPEND_MW, true);
	}

	google_wlc_update_usecase_cloak(charger, to_uc);

	if (should_charge) {
		ret = charger->chip->chip_get_sys_mode(charger, &mode);
		if (ret == 0) {
			if (charger->mode != mode)
				google_wlc_setup_by_mode(charger, mode);
			charger->mode = mode;
		}
	}
	mutex_unlock(&charger->status_lock);
}

static void google_wlc_upload_fwlog(struct google_wlc_data *charger, struct wlc_fw_data data,
				    bool extension, enum fw_msg_type type,
				    enum gbms_fwupdate_msg_category category, bool event)
{
	if (extension)
		logbuffer_log(charger->fw_log, "0x%04X %X %X %X %X %X %X %X %X %llX %X %X %X %X",
			      MONITOR_TAG_WL, type, category, data.cur_ver.major,
			      data.cur_ver.minor, data.ver.major, data.ver.minor, data.status,
			      data.attempts, ktime_get_real_seconds(), data.data0, data.data1,
			      data.data2, data.data3);
	else
		logbuffer_log(charger->fw_log, "0x%04X %X %X %X %X %X %X %X %X %llX",
			      MONITOR_TAG_WL, type, category, data.cur_ver.major,
			      data.cur_ver.minor, data.ver.major, data.ver.minor, data.status,
			      data.attempts, ktime_get_real_seconds());

	/* event to trigger service flow */
	if (event)
		google_wlc_uevent(charger, UEVENT_FWUPDATE);
}

static void google_wlc_run_fwupdate(struct google_wlc_data *charger)
{
	int ret;

	msleep(100);
	ret = charger->chip->chip_fwupdate(charger, CRC_VERIFY_STEP);
	// Do not upload fwlog or run update for debug crc check cmd
	if (charger->debug_force_crc_check_only) {
		dev_info(charger->dev, "debug crcis set, skipping FW update");
		return;
	}
	if (ret) {
		charger->fw_data.cur_ver.major = -1;
		charger->fw_data.cur_ver.minor = -1;
		charger->fw_data.update_check_pending = true;
	}

	charger->fw_data.data0 = charger->fw_data.cur_ver.crc;
	charger->fw_data.data1 = charger->fw_data.cur_ver.project_id;
	charger->fw_data.ver.project_id = 0;
	/* it does not get firmware data during CRC, upload 0 to avoid confusion */
	charger->fw_data.ver.major = 0;
	charger->fw_data.ver.minor = 0;
	charger->fw_data.status = (ret == 0) ? FWUPDATE_STATUS_SUCCESS : ret;

	/* pending event until update result to prevent mishandling on service side */
	google_wlc_upload_fwlog(charger, charger->fw_data, true,
				FWU_MSG_WLC_TYPE_CRC_CHECK, FWU_MSG_CATEGORY_RX, false);

	/* FWUPDATE_ERROR_CRC by above will lead to a force update */
	ret = charger->chip->chip_fwupdate(charger, FW_UPDATE_STEP);
	charger->fw_data.attempts++;

	/* finish verification and no crc error */
	if (charger->fw_data.update_done && ret != -FWUPDATE_ERROR_CRC) {
		charger->fw_data.status = FWUPDATE_STATUS_SUCCESS;
	} else {
		/* update failed, retry waits until rescheduled */
		charger->fw_data.status = ret;
		charger->fw_data.update_check_pending = true;
	}

	if (ret == 0)
		charger->fw_data.data0 = charger->fw_data.ver.crc;

	charger->fw_data.data1 = charger->fw_data.ver.project_id;

	if (ret || charger->fw_data.needs_update) {
		google_wlc_upload_fwlog(charger, charger->fw_data, true,
					FWU_MSG_WLC_TYPE_RX, FWU_MSG_CATEGORY_RX, true);
		if (ret == 0 && charger->fw_data.update_done) {
			charger->fw_data.cur_ver.major = charger->fw_data.ver.major;
			charger->fw_data.cur_ver.minor = charger->fw_data.ver.minor;
			charger->fw_data.cur_ver.crc = charger->fw_data.ver.crc;
		}
	} else {
		google_wlc_uevent(charger, UEVENT_FWUPDATE);
	}
}

static void google_wlc_usecase_cb(void *d, enum gsu_usecases from_uc,
				 enum gsu_usecases to_uc)
{
	struct google_wlc_data *charger = (struct google_wlc_data *)d;
	const bool usb_connected = bms_usecase_is_uc_wired(to_uc);
	const bool not_charging = bms_usecase_is_uc_wireless(to_uc) &&
				  !bms_usecase_is_uc_charging_enabled(to_uc);
	const bool should_charge = bms_usecase_is_uc_wireless(to_uc) &&
				   bms_usecase_is_uc_charging_enabled(to_uc) &&
				   !is_usecase_wlc_dc(to_uc);

	logbuffer_devlog(charger->log, charger->dev,
			 "usecase notification:%s(%d)->%s(%d), chg: %d, usb: %d, wc_rx: %d",
			 bms_usecase_to_str(from_uc), from_uc,
			 bms_usecase_to_str(to_uc), to_uc,
			 should_charge, usb_connected, not_charging);
	mutex_lock(&charger->status_lock);
	charger->usecase = to_uc;
	charger->usecase_ready = true;
	if (is_usecase_wlc_dc(to_uc) && !is_usecase_wlc_dc(from_uc)) {
		google_wlc_set_status(charger, GOOGLE_WLC_STATUS_DC_CHARGING);
	} else if (!is_usecase_wlc_dc(to_uc) && is_usecase_wlc_dc(from_uc)) {
		__pm_stay_awake(charger->notifier_ws);
		pm_system_wakeup();
		schedule_delayed_work(&charger->psy_notifier_work,
				msecs_to_jiffies(GOOGLE_WLC_NOTIFIER_DELAY_MS));
		memset(&charger->dc_data, 0, sizeof(struct wlc_dc_data));
	}

	switch (to_uc) {
	case GSU_MODE_WLC_FWUPDATE:
		google_wlc_run_fwupdate(charger);
		gvotable_cast_int_vote(charger->chg_mode_votable, WLCFW_VOTER,
				       _bms_usecase_meta_async_set(GBMS_CHGR_MODE_WLC_FWUPDATE, 1),
				       false);
		__pm_relax(charger->fwupdate_ws);
		break;
	case GSU_MODE_STANDBY:
		/* wait for internal boost disabled */
		if (from_uc == GSU_MODE_WLC_FWUPDATE) {
			msleep(200);
			charger->online_disable = false;
		}
		if (!is_fwtag_allow_update(charger))
			break;
		if (charger->fw_data.update_check_pending)
			mod_delayed_work(system_wq, &charger->wlc_fw_update_work,
					 msecs_to_jiffies(WLC_FW_CHECK_TIMEOUT_MS));
		break;
	case GSU_MODE_INPUT_SUSPEND:
		gvotable_cast_int_vote(charger->wlc_disable_votable, USECASE_INPUT_SUSPEND_VOTER,
				       WLC_SOFT_DISABLE, true);
		break;
	case GSU_MODE_WLC_RX_STDBY:
		google_wlc_wait_for_cloak(charger, CHG_ENABLE_VOTER, false);
		break;
	default:
		break;
	}

	if (to_uc != GSU_MODE_INPUT_SUSPEND)
		gvotable_cast_int_vote(charger->wlc_disable_votable,
				       USECASE_INPUT_SUSPEND_VOTER, 0, false);

	if (should_charge)
		charger->wlc_charge_enabled = true;
	if (should_charge || in_wlc_dc(charger) || to_uc == GSU_MODE_WLC_RX_STDBY) {
		charger->charging_initialized = false;
		charger->charger_started = false;
		__pm_stay_awake(charger->init_charging_ws);
		mod_delayed_work(system_wq, &charger->charging_init_work, 0);
		if (charger->eds_event)
			google_wlc_uevent(charger, UEVENT_WLC_ON);
	}
	mutex_unlock(&charger->status_lock);
	return;
}

static int google_wlc_set_mode_gpio(struct google_wlc_data *charger, enum sys_op_mode mode)
{

	int gpio_mode = -1;

	if (IS_ERR_OR_NULL(charger->pdata->mode_gpio))
		return -EINVAL;

	if (mode == RX_MODE_WPC_MPP_RESTRICTED)
		charger->mpp_restricted_set = true;
	else
		charger->mpp_restricted_set = false;

	if (mode == RX_MODE_WPC_MPP_RESTRICTED ||
	    (charger->pdata->support_epp && mode == RX_MODE_WPC_BPP)) {
		gpio_mode = GPIO_VOL_LOW;
	} else if (mode_is_mpp(mode) || mode == RX_MODE_WPC_EPP) {
		gpio_mode = GPIO_VOL_HIGH;
	} else if (mode == RX_MODE_WPC_BPP) {
		gpio_mode = GPIO_NO_PULL;
		charger->force_bpp = true;
	} else {
		dev_err(charger->dev, "invalid mode request\n");
		return -EINVAL;
	}
	if (mode != RX_MODE_WPC_BPP) {
		charger->force_bpp = false;
		charger->iop_bpp = false;
	}

	if (gpio_mode == charger->gpio_mode)
		return 0;

	switch (gpio_mode) {
	case GPIO_VOL_LOW:
		gpiod_direction_output(charger->pdata->mode_gpio, 0);
		break;
	case GPIO_VOL_HIGH:
		gpiod_direction_output(charger->pdata->mode_gpio, 1);
		break;
	case GPIO_NO_PULL:
		gpiod_direction_input(charger->pdata->mode_gpio);
		break;
	default:
		dev_err(charger->dev, "invalid mode request\n");
		break;
	}

	dev_info(charger->dev, "Setting mode GPIO to %s", sys_op_mode_str[mode]);
	charger->gpio_mode = gpio_mode;
	return 0;
}

static int google_wlc_eds_reset(struct google_wlc_data *charger)
{
	set_eds_state(charger, EDS_RESET);
	return charger->chip->chip_eds_reset(charger);
}

static void google_wlc_tx_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, tx_work.work);
	dev_warn(charger->dev, "timeout waiting for tx complete\n");
	google_wlc_eds_reset(charger);
}

/* clear eds icl setting after timeout */
static void google_wlc_eds_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, eds_work.work);
	dev_warn(charger->dev, "timeout waiting for eds complete\n");
	google_wlc_eds_reset(charger);
}

static int google_wlc_send_eds(struct google_wlc_data *charger, u16 len, u32 timeout)
{
	int ret;

	timeout = timeout > MPP_TX_TIMEOUT_MS ? timeout : MPP_TX_TIMEOUT_MS;

	if (!charger->online)
		return -ENODEV;

	if (!mode_is_mpp(charger->mode) && (charger->mode != RX_MODE_WPC_EPP))
		return -EAGAIN;

	ret = charger->chip->chip_check_eds_status(charger);
	if (ret != 0)
		return ret;

	ret = set_eds_state(charger, EDS_SEND);
	if (ret != 0)
		return -EBUSY;

	charger->tx_len = len;
	ret = charger->chip->chip_send_eds(charger, charger->tx_buf, charger->tx_len,
					   charger->eds_stream);
	if (ret) {
		set_eds_state(charger, EDS_AVAILABLE);
		return ret;
	}
	mod_delayed_work(system_wq, &charger->tx_work, msecs_to_jiffies(timeout));

	if (charger->eds_total_count < 0xFFFF)
		charger->eds_total_count++;

	return 0;
}

static void google_wlc_register_usecase_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, register_usecase_work.work);
	struct bms_usecase_notify_cbs cbs;
	int ret;

	if (charger->shutdown_notify)
		return;

	cbs.uc_setup_cb = google_wlc_usecase_setup_cb;
	cbs.uc_transition_cb = google_wlc_usecase_transition_cb;
	cbs.uc_changed_cb = google_wlc_usecase_cb;

	ret = bms_usecase_register_notifiers((void *)charger, cbs, WLC_VOTER);
	if (ret == -EAGAIN) {
		dev_err(charger->dev, "Reschedule register usecase notifier: %d\n", ret);
		schedule_delayed_work(&charger->register_usecase_work,
				      msecs_to_jiffies(NOTIFIER_REGISTER_RETRY_MS));
		return;
	}
	dev_info(charger->dev, "Registered usecase notifier\n");
}

static int google_wlc_read_packet(struct google_wlc_data *charger, char *buf, size_t max)
{
	struct google_wlc_packet packet;
	size_t len = 0;
	size_t dlen;
	int ret;

	ret = charger->chip->chip_get_packet(charger, &packet, &dlen);
	if (ret == 0) {
		if (dlen > sizeof(packet.data))
			dlen = sizeof(packet.data);

		len += scnprintf(buf + len, max - len, "%02x ", packet.header);
		len += scnprintf(buf + len, max - len, "%02x ", packet.cmd);
		for (int i = 0; i < dlen; i++)
			len += scnprintf(buf + len, max - len, "%02x ", packet.data[i]);

		buf[max - 1] = '\0';
	}

	return ret;
}

/* SYSFS ATTRIBUTES */

static ssize_t addr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	ssize_t len;

	mutex_lock(&charger->fwupdate_lock);
	if (charger->fwupdate_mode)
		len = scnprintf(buf, PAGE_SIZE, "%08x\n", charger->addr_fw);
	else
		len =  scnprintf(buf, PAGE_SIZE, "%04x\n", charger->addr);
	mutex_unlock(&charger->fwupdate_lock);

	return len;
}

static ssize_t addr_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&charger->fwupdate_lock);
	if (charger->fwupdate_mode) {
		u32 addr_fw;

		ret = kstrtou32(buf, 16, &addr_fw);
		if (ret == 0) {
			charger->addr_fw = addr_fw;
			ret = count;
		}
	} else {
		u16 addr;

		ret = kstrtou16(buf, 16, &addr);
		if (ret == 0) {
			charger->addr = addr;
			ret = count;
		}
	}
	mutex_unlock(&charger->fwupdate_lock);

	return ret;
}

static DEVICE_ATTR_RW(addr);

static ssize_t count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%u\n", charger->count);
}

static ssize_t count_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret;
	u16 cnt;

	ret = kstrtou16(buf, 0, &cnt);
	if (ret < 0)
		return ret;

	charger->count = cnt;
	return count;
}

static DEVICE_ATTR_RW(count);

static ssize_t fwupdate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%u\n", charger->fwupdate_mode);
}

static ssize_t fwupdate_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	mutex_lock(&charger->fwupdate_lock);
	charger->fwupdate_mode = buf[0] == '1';
	mutex_unlock(&charger->fwupdate_lock);

	return count;
}

static DEVICE_ATTR_RW(fwupdate);

static ssize_t data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	u8 reg[256];
	int ret;
	int i;
	ssize_t len = 0;

	mutex_lock(&charger->fwupdate_lock);
	if (charger->fwupdate_mode && charger->count > 0) {
		ret = charger->chip->chip_fw_reg_read_n(charger, charger->addr_fw,
							buf, charger->count);
		mutex_unlock(&charger->fwupdate_lock);
		return ret;
	}
	mutex_unlock(&charger->fwupdate_lock);

	if (!charger->count || (charger->addr > (0xFFFF - charger->count)))
		return -EINVAL;

	ret = charger->chip->reg_read_n(charger, charger->addr, reg, charger->count);
	if (ret)
		return ret;

	for (i = 0; i < charger->count; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len, "%02x: %02x\n",
				 charger->addr + i, reg[i]);
	}
	return len;
}

static ssize_t data_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	u8 reg[256];
	int ret = 0;
	int i;
	char *data;
	char *tmp_buf;

	mutex_lock(&charger->fwupdate_lock);
	if (charger->fwupdate_mode && charger->count > 0) {
		ret = charger->chip->chip_fw_reg_write_n(charger, charger->addr_fw,
							buf, charger->count);
		mutex_unlock(&charger->fwupdate_lock);
		if (ret == 0)
			return count;
		return ret;
	}
	mutex_unlock(&charger->fwupdate_lock);

	if (!charger->count || (charger->addr > (0xFFFF - charger->count)))
		return -EINVAL;

	tmp_buf = kstrdup(buf, GFP_KERNEL);
	data = tmp_buf;
	if (!data)
		return -ENOMEM;
	for (i = 0; data && i < charger->count; ) {
		/* Data will keep getting split every loop until it is NULL */
		/* Calling strsep repeatedly will always result in the arg becoming NULL */
		char *d = strsep(&data, " ");

		if (*d) {
			ret = kstrtou8(d, 16, &reg[i]);
			if (ret)
				break;
			/* i represents how many register values we have found to write so far */
			i++;
		}
	}
	if ((i != charger->count) || ret) {
		ret = -EINVAL;
		goto out;
	}

	ret = charger->chip->reg_write_n(charger, charger->addr, reg, charger->count);
	if (ret)
		goto out;
	ret = count;

out:
	kfree(tmp_buf);
	return ret;
}

static DEVICE_ATTR_RW(data);

static ssize_t txlen_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret;
	u16 len;

	ret = kstrtou16(buf, 16, &len);
	if (ret < 0)
		return ret;

	if (!charger->wpc_auth_type)
		return -EOPNOTSUPP;

	ret = google_wlc_send_eds(charger, len, 0);

	return ret == 0 ? count : ret;
}

static DEVICE_ATTR_WO(txlen);

static ssize_t rxdone_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	buf[0] = charger->rx_done ? '1' : '0';
	buf[1] = 0;
	return 1;
}

static DEVICE_ATTR_RO(rxdone);

static ssize_t rxlen_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%hu\n", charger->rx_len);
}

static DEVICE_ATTR_RO(rxlen);

static ssize_t txdone_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	buf[0] = charger->tx_done ? '1' : '0';
	buf[1] = 0;
	return 1;
}

static DEVICE_ATTR_RO(txdone);


static ssize_t txbusy_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	buf[0] = charger->tx_busy ? '1' : '0';
	buf[1] = 0;
	return 1;
}

static DEVICE_ATTR_RO(txbusy);

static ssize_t ccreset_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret;

	ret = google_wlc_eds_reset(charger);

	return ret == 0 ? count : ret;
}

static DEVICE_ATTR_WO(ccreset);

static ssize_t stream_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret;
	u16 val;

	ret = kstrtou16(buf, 16, &val);
	if (ret < 0)
		return ret;

	switch (val) {
	case EDS_AUTH:
	case EDS_FW_UPDATE:
	case EDS_THERMAL:
		charger->eds_stream = val;
		break;
	default:
		charger->eds_stream = EDS_AUTH;
		break;
	}

	dev_dbg(charger->dev, "eds stream type: %d", val);

	return count;
}

static DEVICE_ATTR_WO(stream);

static ssize_t features_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	u64 id, value;
	int ret;

	ret = sscanf(buf, "%llx:%llx", &id, &value);
	if (ret != 2)
		return -EINVAL;

	logbuffer_devlog(charger->log, charger->dev, "%s: id=%llx, val=%llx", __func__, id, value);

	if (id != 1)
		ret = feature_update(charger, value);

	if (ret < 0)
		count = ret;

	return count;
}

static ssize_t features_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", charger->feature);
}

static DEVICE_ATTR_RW(features);

static bool google_wlc_eds_ready(struct google_wlc_data *charger)
{
	bool allow_eds_on_dc, allow_eds, allow_eds_on_stdby, support_eds, ret;
	const enum google_wlc_status status = charger->status;
	const enum sys_op_mode mode = charger->mode;
	const enum mpp25_state mpp25_state = charger->mpp25.state;

	allow_eds_on_dc = status == GOOGLE_WLC_STATUS_DC_CHARGING &&
			  (mpp25_state == MPP25_ACTIVE || mpp25_state == MPP25_OFF);
	allow_eds = bms_usecase_is_uc_charging_enabled(charger->usecase) &&
		    status == GOOGLE_WLC_STATUS_CHARGING &&
		    mode != RX_MODE_WPC_MPP_NEGO &&
		    mode != RX_MODE_WPC_EPP_NEGO &&
		    (!wpc_auth_passed(charger) ||
		     mpp25_state == MPP25_OFF ||
		     mpp25_state == MPP25_ACTIVE) &&
		    (charger->tx_id != 0 || mode != RX_MODE_WPC_EPP) &&
		    charger->last_capacity < 95;
	allow_eds_on_stdby = charger->usecase == GSU_MODE_WLC_RX_STDBY;
	support_eds = mode != RX_MODE_WPC_BPP &&
		      mode != RX_MODE_WPC_MPP_RESTRICTED;
	ret = !charger->auth_disable &&
		(allow_eds || allow_eds_on_dc || allow_eds_on_stdby) && support_eds;

	/* send event on next mode change if not ready */
	if (ret == false) {
		charger->eds_event = true;
		charger->last_charging_uevent = UEVENT_WLC_INIT;
	} else {
		charger->eds_event = false;
	}

	return ret;
}

static ssize_t wpc_ready_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%c\n", google_wlc_eds_ready(charger) ? 'Y' : 'N');
}

static DEVICE_ATTR_RO(wpc_ready);

static ssize_t authtype_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret;
	u16 type;

	ret = kstrtou16(buf, 16, &type);
	if (ret < 0)
		return -EINVAL;

	charger->wpc_auth_type = true;

	return count;
}

static DEVICE_ATTR_WO(authtype);

static ssize_t run_fwupdate_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	charger->run_fwupdate = buf[0] == '1';
	logbuffer_devlog(charger->fw_log, charger->dev,
			 "set run_fwupdate to %d", charger->run_fwupdate);
	if (charger->chg_mode_votable && charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED)
		gvotable_cast_long_vote(charger->chg_mode_votable, WLC_VOTER,
			_bms_usecase_meta_async_set(GBMS_CHGR_MODE_WLC_RX_STDBY, 1), true);

	return count;
}

static DEVICE_ATTR_WO(run_fwupdate);

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int count = 0;

	count += charger->chip->chip_add_info_string(charger, buf + count);

	return count;
}

static DEVICE_ATTR_RO(version);

static size_t google_wlc_add_buffer(char *buf, u32 val, size_t count, int ret,
			       const char *name, char *fmt)
{
	int added = 0;

	added += scnprintf(buf + count, PAGE_SIZE - count, "%s", name);
	count += added;
	if (ret)
		added += scnprintf(buf + count, PAGE_SIZE - count,
				   "err %d\n", ret);
	else
		added += scnprintf(buf + count, PAGE_SIZE - count, fmt, val);

	return added;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int count = 0;
	u32 val32 = 0;
	u16 val16 = 0;
	u8 mode = 0;
	u8 val8 = 0;
	int ret = 0;
	bool offline = false;
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "drv_status    : %s\n", google_wlc_status_str[charger->status]);
	if (!google_wlc_is_present(charger) && !charger->online_disable)
		offline = true;

	if (!offline)
		ret = charger->chip->chip_get_status(charger, &val16);
	count += google_wlc_add_buffer(buf, val16, count, ret,
					"chip_status   : ", "%04x\n");
	if (!offline)
		ret = charger->chip->chip_get_sys_mode(charger, &mode);
	count += scnprintf(buf + count, PAGE_SIZE - count,
					"mode_str      : %s\n", sys_op_mode_str[mode]);

	if (!offline)
		ret = charger->chip->chip_get_vout(charger, &val32);
	count += google_wlc_add_buffer(buf, MV_TO_UV(val32), count, ret,
					"vout          : ", "%u uV\n");

	if (!offline)
		ret = charger->chip->chip_get_iout(charger, &val32);
	count += google_wlc_add_buffer(buf, MA_TO_UA(val32), count, ret,
					"iout          : ", "%u uA\n");

	if (!offline)
		ret = charger->chip->chip_get_vrect(charger, &val32);
	count += google_wlc_add_buffer(buf, MV_TO_UV(val32), count, ret,
					"vrect         : ", "%u uV\n");

	if (!offline)
		ret = charger->chip->chip_get_temp(charger, &val32);
	count += google_wlc_add_buffer(buf, MILLIC_TO_C(val32), count, ret,
					"temp          : ", "%u deg C\n");

	if (!offline)
		ret = charger->chip->chip_get_opfreq(charger, &val32);
	count += google_wlc_add_buffer(buf, val32, count, ret,
					"opfreq        : ", "%u kHz\n");

	count += google_wlc_add_buffer(buf, charger->nego_power, count, ret,
					"nego_pwr      : ", "%u mW\n");
	if (mode_is_mpp(mode)) {
		charger->chip->chip_get_tx_kest(charger, &val32);
		count += google_wlc_add_buffer(buf, val32, count, ret,
					"K est         : ", "%u/1000\n");
	}
	if (!offline)
		ret = charger->chip->chip_get_limit_rsn(charger, &val8);
	count += google_wlc_add_buffer(buf, val8, count, ret,
					"limit_rsn     : ", "%u\n");
	if (charger->pdata->support_mpp25)
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"MPP25 state   : %s\n",
					mpp25_state_str[charger->mpp25.state]);
	if (charger->pdata->chg_configs_num > 0) {
		if (charger->disable_state != WLC_HARD_DISABLE)
			count += scnprintf(buf + count, PAGE_SIZE - count,
					"charger index : %d\n",
					charger->chg_config_index);
		else
			count += scnprintf(buf + count, PAGE_SIZE - count,
					"charger index : inhibit\n");
	}
	if (charger->pdata->chg_configs_num > 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"mdis table    : %s %d\n",
					charger->mdis_name ?
					charger->mdis_name : "none", charger->mdis_limit);

	return count;
}
static DEVICE_ATTR_RO(status);

static ssize_t mode_gpio_status_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int gpio_mode = -1;

	if (IS_ERR_OR_NULL(charger->pdata->mode_gpio))
		return -ENODEV;
	if (charger->force_bpp) {
		gpio_mode = GPIO_NO_PULL;
	} else {
		const int value = gpiod_get_raw_value_cansleep(charger->pdata->mode_gpio);

		if (value == 1)
			gpio_mode = GPIO_VOL_HIGH;
		else if (value == 0)
			gpio_mode = GPIO_VOL_LOW;
	}
	if (gpio_mode == -1)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%s\n", mode_gpio_str[gpio_mode]);
}

static ssize_t mode_gpio_status_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	bool force_bpp = false;

	if (IS_ERR_OR_NULL(charger->pdata->mode_gpio))
		return -ENODEV;

	if (buf[0] == '0' || strncmp(buf, "MPP_RES", 7) == 0) {
		google_wlc_set_mode_gpio(charger, RX_MODE_WPC_MPP_RESTRICTED);
	} else if (buf[0] == '1' || strncmp(buf, "MPP", 3) == 0 || strncmp(buf, "EPP", 3) == 0) {
		google_wlc_set_mode_gpio(charger, RX_MODE_WPC_MPP);
	} else if (buf[0] == '2' || strncmp(buf, "BPP", 3) == 0) {
		force_bpp = true;
		google_wlc_set_mode_gpio(charger, RX_MODE_WPC_BPP);
	} else {
		return -EINVAL;
	}

	charger->manual_force_bpp = force_bpp;

	return count;
}

static DEVICE_ATTR_RW(mode_gpio_status);

static int google_wlc_parse_buf(char *data, int num, u32 *val)
{
	int i = 0, ret;

	while (data && i < num) {
		char *d = strsep(&data, " ");

		if (*d) {
			ret = kstrtou32(d, 0, &val[i]);
			if (ret)
				break;
			i++;
		}
	}
	if (i != num || ret)
		return -EINVAL;

	return i;
}

static void trigger_rx_fwupdate(struct google_wlc_data *charger)
{
	charger->fw_data.update_done = false;
	charger->fw_data.update_support = (charger->pdata->bl_name != NULL &&
					   charger->pdata->fw_name != NULL);
	/* If the CRC is incorrect, the attempt should not be cleared to avoid a retry loop */
	if (charger->fw_data.cur_ver.crc > 0)
		charger->fw_data.attempts = 0;

	mod_delayed_work(system_wq, &charger->wlc_fw_update_work,
			 msecs_to_jiffies(WLC_FW_CHECK_TIMEOUT_MS));
}

static ssize_t ptmc_id_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	if (charger->ptmc == 0)
		charger->chip->chip_get_ptmc_id(charger, &charger->ptmc);

	return scnprintf(buf, PAGE_SIZE, "%04x\n", charger->ptmc);
}

static DEVICE_ATTR_RO(ptmc_id);

static ssize_t authstart_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	if (!google_wlc_eds_ready(charger))
		return scnprintf(buf, PAGE_SIZE, "%c\n", 'N');

	return scnprintf(buf, PAGE_SIZE, "%c\n",
			 charger->chip->chip_check_eds_status(charger) == -EBUSY ? 'B' : 'Y');
}

static ssize_t authstart_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	if (buf[0] != '1')
		return -EINVAL;

	/* put setting for authentication here */

	return count;
}

static DEVICE_ATTR_RW(authstart);

static ssize_t alignment_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 align_status_str[ALIGN_CENTERED]);
}

static DEVICE_ATTR_RO(alignment);

static ssize_t vrect_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret;
	u32 mv;

	ret = charger->chip->chip_get_vrect(charger, &mv);
	if (ret)
		mv = 0;

	return scnprintf(buf, PAGE_SIZE, "%d\n", mv);
}

static DEVICE_ATTR_RO(vrect);

static ssize_t charge_stats_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int i, len = -ENODATA;

	mutex_lock(&charger->stats_lock);

	if (charger->chg_data.cur_soc < 0)
		goto enodata_done;

	len = wlc_chg_data_head_dump(buf, PAGE_SIZE, &charger->chg_data);
	if (len < PAGE_SIZE - 1)
		buf[len++] = '\n';

	len += wlc_adapter_capabilities_dump(&buf[len], PAGE_SIZE - len, &charger->chg_data);
	if (len < PAGE_SIZE - 1)
		buf[len++] = '\n';

	for (i = 0; i < WLC_SOC_STATS_LEN; i++) {
		if (charger->chg_data.soc_data[i].elapsed_time == 0)
			continue;
		len += wlc_soc_data_dump(&buf[len], PAGE_SIZE - len,
					   &charger->chg_data, i);
		if (len < PAGE_SIZE - 1)
			buf[len++] = '\n';
	}

enodata_done:
	mutex_unlock(&charger->stats_lock);
	return len;
}

static ssize_t charge_stats_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	if (count < 1)
		return -ENODATA;

	mutex_lock(&charger->stats_lock);
	switch (buf[0]) {
	case 0:
	case '0':
		google_wlc_stats_init(charger);
		break;
	}
	mutex_unlock(&charger->stats_lock);

	return count;
}

static DEVICE_ATTR_RW(charge_stats);

static ssize_t has_wlc_dc_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->pdata->has_mpp25_wlc_dc);
}

/* write 1 to enable SWC charging
 * write 0 to disable SWC charging
 */
static ssize_t has_wlc_dc_store(struct device *dev,
		       struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	charger->pdata->has_mpp25_wlc_dc = buf[0] == '1';
	return count;
}

static DEVICE_ATTR_RW(has_wlc_dc);

static ssize_t operating_freq_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret, val;

	ret = charger->chip->chip_get_opfreq(charger, &val);

	val = (ret == 0) ? KHZ_TO_HZ(val) : ret;

	return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

static DEVICE_ATTR_RO(operating_freq);


static ssize_t rx_fwupdate_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret, val;

	if (charger->pdata->fwupdate_option == FWUPDATE_CRC_NOTSUPPORT)
		return count;

	ret = kstrtoint(buf, 0, &val);

	dev_dbg(charger->dev, "rx fwupdate store value %d", val);
	if (ret < FWUPDATE_CRC_NOTSUPPORT || val > FWUPDATE_FORCE_MANUAL)
		return -EINVAL;
	charger->fw_data.update_option = val;

	if (val == FWUPDATE_FORCE_MANUAL) {
		charger->fwupdate_state = FWU_STATE_NOT_STARTED;
		trigger_rx_fwupdate(charger);
	}

	return count;
}

static DEVICE_ATTR_WO(rx_fwupdate);

static ssize_t rx_vertag_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret, val;

	ret = kstrtoint(buf, 0, &val);

	charger->fw_data.ver_tag = val;

	/*
	 * if there is no CRC and update_option == FWUPDATE_FORCE,
	 * will trigger fw update without check fwtag
	 */
	if (charger->fw_data.cur_ver.crc == 0) {
		trigger_rx_fwupdate(charger);
		return count;
	}

	if (!is_fwtag_allow_update(charger))
		return count;

	ret = charger->chip->chip_fwupdate(charger, READ_REQ_FWVER);
	if (ret == 0 && charger->fw_data.update_option < FWUPDATE_FORCE &&
	    charger->fw_data.cur_ver.major == charger->fw_data.req_ver.major &&
	    charger->fw_data.cur_ver.minor == charger->fw_data.req_ver.minor)
		return count;

	trigger_rx_fwupdate(charger);

	return count;
}

static ssize_t rx_vertag_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	int ret;
	uint32_t fw_tag = 0;

	ret = gbms_storage_read(GBMS_TAG_WLFW, &fw_tag, sizeof(fw_tag));
	if (ret < 0)
		return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
	return scnprintf(buf, PAGE_SIZE, "0x%08x\n", fw_tag);
}

static DEVICE_ATTR_RW(rx_vertag);

static ssize_t thermal_control_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	char s[THERMAL_EDS_SIZE * 3 + 1];
	size_t max = sizeof(s);
	size_t len = 0;

	mutex_lock(&charger->eds_lock);

	if (charger->eds_state == EDS_AVAILABLE ||
	    (charger->eds_state == EDS_RECEIVED && charger->rx_thermal_len == 0)) {
		mutex_unlock(&charger->eds_lock);
		return scnprintf(buf, PAGE_SIZE, "%s\n", "no eds sent");
	}

	if (charger->eds_state != EDS_RECEIVED) {
		mutex_unlock(&charger->eds_lock);
		return scnprintf(buf, PAGE_SIZE, "%s\n", "eds is on transaction");
	}

	mutex_unlock(&charger->eds_lock);

	for (int i = 0; i < charger->rx_thermal_len; i++)
		len += scnprintf(s + len, max - len, "%02x ", charger->rx_thermal_buf[i]);

	charger->rx_thermal_len = 0;
	set_eds_state(charger, EDS_AVAILABLE);


	return scnprintf(buf, PAGE_SIZE, "%s\n", s);
}

static ssize_t thermal_control_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	u8 bytes[THERMAL_EDS_SIZE];
	int ret;
	u16 len = 0;
	char *data;
	char *tmp_buf;

	tmp_buf = kstrdup(buf, GFP_KERNEL);
	data = tmp_buf;
	if (!data)
		return -ENOMEM;

	while (data && len < count) {
		char *p = strsep(&data, " ");

		if (*p)
			ret = kstrtou8(p, 16, &bytes[len]);
		else
			ret = -EINVAL;
		if (ret)
			break;
		len++;
	}

	if (ret == 0) {
		charger->eds_stream = EDS_THERMAL;
		memcpy(charger->tx_buf, bytes, len);
		ret = google_wlc_send_eds(charger, len, 0);
	}
	kfree(tmp_buf);

	return ret == 0 ? count : ret;
}

static DEVICE_ATTR_RW(thermal_control);

static ssize_t fwupdate_data_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	struct wlc_fw_data fwdata;
	u32 values[FWUPDATE_DATA_MAXSIZE];
	char *data;
	char *tmp_buf;
	int ret, i = 0;

	tmp_buf = kstrdup(buf, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	data = tmp_buf;
	while (data && i < count && i < FWUPDATE_DATA_MAXSIZE) {
		char *p = strsep(&data, " ");

		if (*p)
			ret = kstrtoint(p, 0, &values[i]);
		else
			ret = -EINVAL;
		if (ret)
			break;
		i++;
	}
	if (ret == 0 && i < FWUPDATE_DATA_MINSIZE)
		ret = -EINVAL;

	if (ret == 0) {
		bool extension = false;
		enum fw_msg_type type = values[FWUPDATE_TYPE_OFFSET];
		enum gbms_fwupdate_msg_category category = values[FWUPDATE_CATEGORY_OFFSET];

		fwdata.cur_ver.major = values[FWUPDATE_CUR_MAJOR_OFFSET];
		fwdata.cur_ver.minor = values[FWUPDATE_CUR_MINOR_OFFSET];
		fwdata.ver.major = values[FWUPDATE_MAJOR_OFFSET];
		fwdata.ver.minor = values[FWUPDATE_MINOR_OFFSET];
		fwdata.status = values[FWUPDATE_STATUS_OFFSET];
		fwdata.attempts = values[FWUPDATE_ATTEMPTS_OFFSET];
		if (i == FWUPDATE_DATA_MAXSIZE) {
			extension = true;
			fwdata.data0 = values[FWUPDATE_DATA0_OFFSET];
			fwdata.data1 = values[FWUPDATE_DATA1_OFFSET];
			fwdata.data2 = values[FWUPDATE_DATA2_OFFSET];
			fwdata.data3 = values[FWUPDATE_DATA3_OFFSET];
		}
		fwdata.needs_update = true;
		google_wlc_upload_fwlog(charger, fwdata, extension, type, category, true);
	}
	kfree(tmp_buf);

	return ret == 0 ? count : ret;
}

static DEVICE_ATTR_WO(fwupdate_data);

/* call with stats_lock */
static void wlc_stats_update_compatibility(struct google_wlc_data *charger)
{
	/*
	 * report last value when offline
	 * disconnect_total_count: clear after successful ramping for each mode
	 */
	if (mode_is_mpp(charger->mode)) {
		charger->compatibility = COMPAT_MPP;
		if (charger->mpp25.state == MPP25_ACTIVE)
			charger->compatibility = COMPAT_MPP25;
	} else if (mode_is_epp(charger->mode)) {
		charger->compatibility = COMPAT_EPP;
	} else if (charger->mode == RX_MODE_WPC_MPP_RESTRICTED) {
		charger->compatibility = COMPAT_MPP_RESTRICTED;
	} else if (charger->mode == RX_MODE_WPC_BPP) {
		charger->compatibility = COMPAT_BPP;
	} else if (charger->mode == RX_MODE_MPP_CLOAK) {
		charger->compatibility = COMPAT_MPP;
	} else if (charger->mode == RX_MODE_PDET) {
		charger->compatibility = COMPAT_BPP;
	}

	if (charger->iop_bpp || charger->mpp_restricted_set)
		charger->compatibility = COMPAT_FORCED_BPP;
	else if (charger->disconnect_total_count > INCOMPAT_COUNT ||
		 charger->limit_reason == POWER_LIMIT_POSSIBLE_FO)
		charger->compatibility = COMPAT_NOT_SUPPORTED;
	else if (charger->bpp_ramp_done_target)
		charger->compatibility = COMPAT_LOWPOWER;

	if (!charger->csi_status_votable)
		charger->csi_status_votable = gvotable_election_get_handle(VOTABLE_CSI_STATUS);
	if (!charger->csi_status_votable)
		return;

	bool csi_low_power = charger->compatibility == COMPAT_FORCED_BPP ||
			     charger->compatibility == COMPAT_NOT_SUPPORTED ||
			     charger->compatibility == COMPAT_LOWPOWER;
	int power_uw = CSI_POWER_UNKNOWN;

	if (charger->compatibility == COMPAT_FORCED_BPP)
		power_uw = 5000000;
	else if (charger->limit_reason == POWER_LIMIT_POSSIBLE_FO && charger->nego_power > 0)
		power_uw = charger->nego_power * 1000;

	gvotable_cast_compound_vote(charger->csi_status_votable,
				    "CSI_STATUS_ADA_WLC_POWER",
				    power_uw,
				    CSI_STATUS_Adapter_Power,
				    csi_low_power);
}

static ssize_t compatibility_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	mutex_lock(&charger->stats_lock);
	wlc_stats_update_compatibility(charger);
	mutex_unlock(&charger->stats_lock);

	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->compatibility);
}

static DEVICE_ATTR_RO(compatibility);

static ssize_t qispec_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->pdata->qispec);
}

static DEVICE_ATTR_RO(qispec);

static ssize_t qiid_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->qi_id);
}

static ssize_t qiid_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	int ret;
	u16 val;

	ret = kstrtou16(buf, 0, &val);
	if (ret < 0)
		return -EINVAL;

	charger->qi_id = val;

	return count;
}

static DEVICE_ATTR_RW(qiid);

static ssize_t maxpower_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);
	struct mode_cap_data *mode_cap = &charger->mpp25.mode_capabilities;
	int max_power = 0;
	int type = AD_TYPE_WLC;

	mutex_lock(&charger->stats_lock);
	if (charger->online == 0)
		goto exit;

	if (charger->chg_data.adapter_type > 0)
		type = charger->chg_data.adapter_type;
	else if (google_wlc_update_adapter_type(charger) == 0)
		type = charger->chg_data.adapter_type;

	if (type == AD_TYPE_WPC_MPP25) {
		/* select max potential power */
		if (mode_cap->cpm.pot_pwr >= MPP25_MIN_POTENTIAL_POWER &&
		    mode_cap->cpm.pot_pwr > max_power)
			max_power = mode_cap->cpm.pot_pwr;

		if (mode_cap->hpm.pot_pwr >= MPP25_MIN_POTENTIAL_POWER &&
		    mode_cap->hpm.pot_pwr > max_power)
			max_power = mode_cap->hpm.pot_pwr;

		if (mode_cap->lpm.pot_pwr > max_power)
			max_power = mode_cap->lpm.pot_pwr;

		if (mode_cap->npm.pot_pwr > max_power)
			max_power = mode_cap->npm.pot_pwr;

		/* no potential power info, select by mode */
		if (max_power == 0)
			max_power = GOOGLE_WLC_MPP25_MAX_POWER;
	} else if (type == AD_TYPE_WPC_MPP) {
		max_power = GOOGLE_WLC_MPP_MAX_POWER;
	} else if (type == AD_TYPE_WPC_EPP || type == TXID_DD_TYPE || type == TXID_DD_TYPE2) {
		/* This supported chipset does not have HPP capability */
		max_power = GOOGLE_WLC_EPP_MAX_POWER;
	} else { /* BPP, MPP_RESTRICTED */
		max_power = GOOGLE_WLC_BPP_MAX_POWER;
	}

exit:
	mutex_unlock(&charger->stats_lock);

	return scnprintf(buf, PAGE_SIZE, "%d\n", max_power);
}

static DEVICE_ATTR_RO(maxpower);

static ssize_t negopower_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->nego_power);
}

static DEVICE_ATTR_RO(negopower);

static ssize_t txlimitreason_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->limit_reason);
}

static DEVICE_ATTR_RO(txlimitreason);

static ssize_t product_id_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->product_id);
}

static DEVICE_ATTR_RO(product_id);

static ssize_t txsrc_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%d\n", charger->txsrc);
}

static DEVICE_ATTR_RO(txsrc);

static struct attribute *google_wlc_attributes[] = {
	&dev_attr_addr.attr,
	&dev_attr_data.attr,
	&dev_attr_count.attr,
	&dev_attr_txlen.attr,
	&dev_attr_txbusy.attr,
	&dev_attr_txdone.attr,
	&dev_attr_rxlen.attr,
	&dev_attr_rxdone.attr,
	&dev_attr_ccreset.attr,
	&dev_attr_stream.attr,
	&dev_attr_features.attr,
	&dev_attr_authtype.attr,
	&dev_attr_run_fwupdate.attr,
	&dev_attr_wpc_ready.attr,
	&dev_attr_version.attr,
	&dev_attr_status.attr,
	&dev_attr_mode_gpio_status.attr,
	&dev_attr_ptmc_id.attr,
	&dev_attr_authstart.attr,
	&dev_attr_alignment.attr,
	&dev_attr_fwupdate.attr,
	&dev_attr_vrect.attr,
	&dev_attr_charge_stats.attr,
	&dev_attr_has_wlc_dc.attr,
	&dev_attr_operating_freq.attr,
	&dev_attr_rx_fwupdate.attr,
	&dev_attr_thermal_control.attr,
	&dev_attr_rx_vertag.attr,
	&dev_attr_fwupdate_data.attr,
	&dev_attr_compatibility.attr,
	&dev_attr_qispec.attr,
	&dev_attr_qiid.attr,
	&dev_attr_maxpower.attr,
	&dev_attr_negopower.attr,
	&dev_attr_txlimitreason.attr,
	&dev_attr_product_id.attr,
	&dev_attr_txsrc.attr,
	NULL
};

static ssize_t google_wlc_rxdata_read(struct file *filp, struct kobject *kobj,
				      struct bin_attribute *bin_attr,
				      char *buf, loff_t pos, size_t size)
{
	struct google_wlc_data *charger;

	charger = dev_get_drvdata(container_of(kobj, struct device, kobj));
	memcpy(buf, &charger->rx_buf[pos], size);
	set_eds_state(charger, EDS_AVAILABLE);
	return size;
}

static struct bin_attribute bin_attr_rxdata = {
	.attr = {
		.name = "rxdata",
		.mode = 0400,
	},
	.read = google_wlc_rxdata_read,
};

static ssize_t google_wlc_txdata_read(struct file *filp, struct kobject *kobj,
				      struct bin_attribute *bin_attr,
				      char *buf, loff_t pos, size_t size)
{
	struct google_wlc_data *charger;

	charger = dev_get_drvdata(container_of(kobj, struct device, kobj));
	memcpy(buf, &charger->tx_buf[pos], size);
	return size;
}

static ssize_t google_wlc_txdata_write(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t pos, size_t size)
{
	struct google_wlc_data *charger;

	charger = dev_get_drvdata(container_of(kobj, struct device, kobj));
	memcpy(&charger->tx_buf[pos], buf, size);
	return size;
}

static struct bin_attribute bin_attr_txdata = {
	.attr = {
		.name = "txdata",
		.mode = 0600,
	},
	.read = google_wlc_txdata_read,
	.write = google_wlc_txdata_write,
};

static struct bin_attribute *google_wlc_bin_attributes[] = {
	&bin_attr_txdata,
	&bin_attr_rxdata,
	NULL,
};

static const struct attribute_group google_wlc_attr_group = {
	.attrs = google_wlc_attributes,
	.bin_attrs = google_wlc_bin_attributes,
};

/* DEBUGFS ATTRIBUTES */
static int google_wlc_qi_version_gpio_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;
	int value;

	if (IS_ERR_OR_NULL(charger->pdata->qi_version_gpio))
		return -ENODEV;

	value = gpiod_get_value_cansleep(charger->pdata->qi_version_gpio);

	*val = value != 0;
	return 0;
}

static int google_wlc_qi_version_gpio_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (IS_ERR_OR_NULL(charger->pdata->qi_version_gpio))
		return -ENODEV;

	gpiod_set_value_cansleep(charger->pdata->qi_version_gpio, val != 0);
	charger->lower_qi_version = val == 0;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(qi_version_gpio_fops, google_wlc_qi_version_gpio_show,
			google_wlc_qi_version_gpio_store, "%lld\n");

static int google_wlc_crc_check_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->fw_data.cur_ver.crc;
	return 0;
}

static int google_wlc_crc_check_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > 0) {
		charger->debug_force_crc_check_only = true;
		charger->fw_data.cur_ver.crc = 0;
		trigger_rx_fwupdate(charger);
	} else {
		charger->debug_force_crc_check_only = false;
	}
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_crc_fops, google_wlc_crc_check_show, google_wlc_crc_check_store,
			"%04llx\n");

static int google_wlc_ept_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > EPT_NFC_TX)
		return -EINVAL;

	return charger->chip->chip_send_ept(charger, val);
}
DEFINE_SIMPLE_ATTRIBUTE(debug_ept_fops, NULL, google_wlc_ept_store, "%lld\n");

static int google_wlc_inhibit_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	if (!charger->pdata->inhibit_gpio)
		return -EINVAL;
	*val = gpiod_get_value_cansleep(charger->pdata->inhibit_gpio);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_inhibit_fops, google_wlc_inhibit_show, NULL, "%lld\n");

static int google_wlc_online_disable_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;
	*val = charger->online_disable;
	return 0;
}

static int google_wlc_online_disable_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	charger->online_disable = val;
	charger->online_disable_override = val > 0;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(online_disable_fops, google_wlc_online_disable_show,
			google_wlc_online_disable_store, "%lld\n");

static int google_wlc_auth_disable_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	charger->auth_disable = val;

	if (val == 1)
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_AUTH_VOTER,
					0, false);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(auth_disable_fops, NULL, google_wlc_auth_disable_store, "%lld\n");

static int google_wlc_skip_nego_power_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	charger->skip_nego = val;
	google_wlc_adjust_negotiated_power(charger);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(skip_nego_power_fops, NULL, google_wlc_skip_nego_power_store, "%lld\n");

static int google_wlc_vinv_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;
	int ret;
	u32 vinv;

	ret = charger->chip->chip_get_vinv(charger, &vinv);
	if (ret == 0)
		*val = vinv;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_vinv_fops, google_wlc_vinv_show, NULL, "%lld\n");

static int google_wlc_ap5v_gpio_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;
	int value;

	if (IS_ERR_OR_NULL(charger->pdata->ap5v_gpio))
		return -ENODEV;

	value = gpiod_get_value_cansleep(charger->pdata->ap5v_gpio);

	*val = value != 0;
	return 0;
}

static int google_wlc_ap5v_gpio_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (IS_ERR_OR_NULL(charger->pdata->ap5v_gpio))
		return -ENODEV;

	gpiod_set_value_cansleep(charger->pdata->ap5v_gpio, val != 0);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(ap5v_gpio_fops, google_wlc_ap5v_gpio_show,
						google_wlc_ap5v_gpio_store, "%lld\n");

static int google_wlc_vdden_gpio_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;
	int value;

	if (IS_ERR_OR_NULL(charger->pdata->vdden_gpio))
		return -ENODEV;

	mutex_lock(&charger->vdden_lock);
	value = gpiod_get_value_cansleep(charger->pdata->vdden_gpio);
	mutex_unlock(&charger->vdden_lock);

	*val = value != 0;
	return 0;
}

static int google_wlc_vdden_gpio_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (IS_ERR_OR_NULL(charger->pdata->vdden_gpio))
		return -ENODEV;

	mutex_lock(&charger->status_lock);
	google_wlc_external_boost_en(charger, val != 0);
	google_wlc_set_vdden(charger, val != 0);
	mutex_unlock(&charger->status_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vdden_gpio_fops, google_wlc_vdden_gpio_show,
						google_wlc_vdden_gpio_store, "%lld\n");

static int google_wlc_det_gpio_status_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;
	int value;

	if (IS_ERR_OR_NULL(charger->pdata->det_gpio))
		return -ENODEV;

	value = gpiod_get_value_cansleep(charger->pdata->det_gpio);

	*val = value;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(det_gpio_status_fops, google_wlc_det_gpio_status_show, NULL, "%lld\n");

static int google_wlc_de_rf_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->pdata->fod_rf;
	return 0;
}

static int google_wlc_de_rf_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > U8_MAX)
		return -EINVAL;

	charger->pdata->fod_rf = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(de_rf_fops, google_wlc_de_rf_show, google_wlc_de_rf_store, "%02llx\n");


static int google_wlc_de_qf_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->pdata->fod_qf;
	return 0;
}

static int google_wlc_de_qf_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > U8_MAX)
		return -EINVAL;

	charger->pdata->fod_qf = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(de_qf_fops, google_wlc_de_qf_show, google_wlc_de_qf_store, "%02llx\n");

static int google_wlc_de_fod_n_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->de_fod_n;
	return 0;
}

static int google_wlc_de_fod_n_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > GOOGLE_WLC_FOD_NUM_MAX || val > U8_MAX)
		return -EINVAL;

	charger->de_fod_n = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(de_fod_n_fops, google_wlc_de_fod_n_show,
						google_wlc_de_fod_n_store, "%llu\n");

static int google_wlc_de_bpp_mdis_n_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->pdata->bpp_mdis_num;
	return 0;
}

static int google_wlc_de_bpp_mdis_n_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > MDIS_LEVEL_MAX || val > U8_MAX)
		return -EINVAL;

	charger->pdata->bpp_mdis_num = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(de_bpp_mdis_n_fops, google_wlc_de_bpp_mdis_n_show,
						google_wlc_de_bpp_mdis_n_store, "%llu\n");

static int google_wlc_de_epp_mdis_n_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->pdata->epp_mdis_num;
	return 0;
}

static int google_wlc_de_epp_mdis_n_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > MDIS_LEVEL_MAX || val > U8_MAX)
		return -EINVAL;

	charger->pdata->epp_mdis_num = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(de_epp_mdis_n_fops, google_wlc_de_epp_mdis_n_show,
						google_wlc_de_epp_mdis_n_store, "%llu\n");

static int google_wlc_de_mpp_mdis_n_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	if (charger->pdata->mpp_mdis_num > 0)
		*val = charger->pdata->mpp_mdis_num;
	else if (charger->pdata->chg_configs_num > 0)
		*val = charger->pdata->chg_configs[charger->mpp_mdis_chg_idx].mpp_mdis_num;
	else
		*val = -EINVAL;

	return 0;
}

static int google_wlc_de_mpp_mdis_n_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > MDIS_LEVEL_MAX || val > U8_MAX)
		return -EINVAL;

	if (charger->pdata->mpp_mdis_num > 0)
		charger->pdata->mpp_mdis_num = (u8)val;
	else if (charger->pdata->chg_configs_num > 0)
		charger->pdata->chg_configs[charger->mpp_mdis_chg_idx].mpp_mdis_num = (u8)val;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(de_mpp_mdis_n_fops, google_wlc_de_mpp_mdis_n_show,
						google_wlc_de_mpp_mdis_n_store, "%llu\n");

static int google_wlc_de_wlc_dc_mdis_n_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->pdata->wlc_dc_mdis_num;
	return 0;
}

static int google_wlc_de_wlc_dc_mdis_n_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > MDIS_LEVEL_MAX || val > U8_MAX)
		return -EINVAL;

	charger->pdata->wlc_dc_mdis_num = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(de_wlc_dc_mdis_n_fops, google_wlc_de_wlc_dc_mdis_n_show,
						google_wlc_de_wlc_dc_mdis_n_store, "%llu\n");

static int google_wlc_de_num_dploss_points_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->pdata->dploss_points_num;
	return 0;
}

static int google_wlc_de_num_dploss_points_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	if (val > U8_MAX)
		return -EINVAL;

	charger->pdata->dploss_points_num = (u8)val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(de_num_dploss_points_fops, google_wlc_de_num_dploss_points_show,
						google_wlc_de_num_dploss_points_store, "%lld\n");

static ssize_t google_wlc_de_fod_show(struct file *filp,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	if (!charger->de_fod_n || (charger->de_fod_n > GOOGLE_WLC_FOD_NUM_MAX))
		return -EINVAL;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	for (i = 0; i < charger->de_fod_n && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
						"%02x ", charger->de_fod[i]);

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_fod_store(struct file *filp,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i = 0;
	int ret = 0;
	char *data;
	char *tmp_kernel_buff;

	if (count > PAGE_SIZE)
		return -EINVAL;
	if (!charger->de_fod_n || (charger->de_fod_n > GOOGLE_WLC_FOD_NUM_MAX))
		return -EINVAL;

	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	while (data && i < charger->de_fod_n) {
		char *d = strsep(&data, " ");

		if (*d) {
			ret = kstrtou8(d, 16, &charger->de_fod[i]);
			if (ret)
				break;
			i++;
		}
	}
	if ((i != charger->de_fod_n) || ret)
		count = -EINVAL;

	ret = count;
	kfree(tmp_kernel_buff);
	return ret;
}

BATTERY_DEBUG_ATTRIBUTE(de_fod_fops, google_wlc_de_fod_show,
						google_wlc_de_fod_store);

static ssize_t google_wlc_de_mpla_show(struct file *filp,
	char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	for (i = 0; i < charger->de_mpla_n && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
						"%02x ", charger->de_mpla[i]);

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_mpla_store(struct file *filp,
	const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i = 0;
	int ret = 0;
	char *data;
	char *tmp_kernel_buff;

	if (count > PAGE_SIZE)
		return -EINVAL;
	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	while (data && i < GOOGLE_WLC_MPLA_NUM_MAX) {
		char *d = strsep(&data, " ");

		if (*d) {
			ret = kstrtou8(d, 16, &charger->de_mpla[i]);
			if (ret)
				break;
			i++;
		}
	}
	if ((i != GOOGLE_WLC_MPLA_NUM_MAX) || ret)
		count = -EINVAL;

	charger->de_mpla_n = i;
	ret = count;
	kfree(tmp_kernel_buff);
	return ret;
}

BATTERY_DEBUG_ATTRIBUTE(de_mpla_fops, google_wlc_de_mpla_show,
						google_wlc_de_mpla_store);

static ssize_t google_wlc_de_rf_curr_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	for (i = 0; i < charger->de_rf_curr_n && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
						"%02x ", charger->de_rf_curr[i]);

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_rf_curr_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i = 0;
	int ret = 0;
	char *data;
	char *tmp_kernel_buff;

	if (count > PAGE_SIZE)
		return -EINVAL;
	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	while (data && i < GOOGLE_WLC_RF_CURR_NUM_MAX) {
		char *d = strsep(&data, " ");

		if (*d) {
			ret = kstrtou8(d, 16, &charger->de_rf_curr[i]);
			if (ret)
				break;
			i++;
		}
	}
	if ((i != GOOGLE_WLC_RF_CURR_NUM_MAX) || ret)
		count = -EINVAL;

	charger->de_rf_curr_n = i;
	ret = count;
	kfree(tmp_kernel_buff);
	return ret;
}

BATTERY_DEBUG_ATTRIBUTE(de_rf_curr_fops, google_wlc_de_rf_curr_show,
						google_wlc_de_rf_curr_store);

static ssize_t google_wlc_de_bpp_mdis_pwr_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	for (i = 0; i < charger->pdata->bpp_mdis_num && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
						"%d ", charger->pdata->bpp_mdis_pwr[i]);

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_bpp_mdis_pwr_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int ret;
	u32 val[MDIS_LEVEL_MAX];
	char *data;
	char *tmp_kernel_buff;

	if (count > PAGE_SIZE)
		return -EINVAL;
	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	ret = google_wlc_parse_buf(data, charger->pdata->bpp_mdis_num, val);

	if (ret != charger->pdata->bpp_mdis_num) {
		kfree(tmp_kernel_buff);
		return -EINVAL;
	}
	memcpy(charger->pdata->bpp_mdis_pwr, val, sizeof(charger->pdata->bpp_mdis_pwr));
	kfree(tmp_kernel_buff);
	return count;
}

BATTERY_DEBUG_ATTRIBUTE(de_bpp_mdis_pwr_fops, google_wlc_de_bpp_mdis_pwr_show,
						google_wlc_de_bpp_mdis_pwr_store);

static ssize_t google_wlc_de_epp_mdis_pwr_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	for (i = 0; i < charger->pdata->epp_mdis_num && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
						"%d ", charger->pdata->epp_mdis_pwr[i]);

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_epp_mdis_pwr_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int ret;
	u32 val[MDIS_LEVEL_MAX];
	char *data;
	char *tmp_kernel_buff;

	if (count > PAGE_SIZE)
		return -EINVAL;
	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	ret = google_wlc_parse_buf(data, charger->pdata->epp_mdis_num, val);

	if (ret != charger->pdata->epp_mdis_num) {
		kfree(tmp_kernel_buff);
		return -EINVAL;
	}
	memcpy(charger->pdata->epp_mdis_pwr, val, sizeof(charger->pdata->epp_mdis_pwr));
	kfree(tmp_kernel_buff);
	return count;
}

BATTERY_DEBUG_ATTRIBUTE(de_epp_mdis_pwr_fops, google_wlc_de_epp_mdis_pwr_show,
						google_wlc_de_epp_mdis_pwr_store);

static ssize_t google_wlc_de_mpp_mdis_pwr_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;
	int mpp_mdis_num;
	u32 *mpp_pwr_table;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	if (charger->pdata->mpp_mdis_num > 0) {
		mpp_mdis_num = charger->pdata->mpp_mdis_num;
		mpp_pwr_table = charger->pdata->mpp_mdis_pwr;
	} else if (charger->pdata->chg_configs_num > 0 &&
		   charger->pdata->chg_configs[charger->mpp_mdis_chg_idx].mpp_mdis_num > 0) {
		mpp_mdis_num = charger->pdata->chg_configs[charger->mpp_mdis_chg_idx].mpp_mdis_num;
		mpp_pwr_table = charger->pdata->chg_configs[charger->mpp_mdis_chg_idx].mpp_mdis_pwr;
	} else {
		return len;
	}

	for (i = 0; i < mpp_mdis_num && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len, "%d ", mpp_pwr_table[i]);

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_mpp_mdis_pwr_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int ret;
	u32 val[MDIS_LEVEL_MAX];
	char *data;
	char *tmp_kernel_buff;
	int mpp_mdis_num;
	u32 *mpp_pwr_table;

	if (count > PAGE_SIZE)
		return -EINVAL;
	if (charger->pdata->mpp_mdis_num > 0) {
		mpp_mdis_num = charger->pdata->mpp_mdis_num;
		mpp_pwr_table = charger->pdata->mpp_mdis_pwr;
	} else if (charger->pdata->chg_configs_num > 0 &&
		   charger->pdata->chg_configs[charger->mpp_mdis_chg_idx].mpp_mdis_num > 0) {
		mpp_mdis_num = charger->pdata->chg_configs[charger->mpp_mdis_chg_idx].mpp_mdis_num;
		mpp_pwr_table = charger->pdata->chg_configs[charger->mpp_mdis_chg_idx].mpp_mdis_pwr;
	} else {
		return count;
	}

	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	ret = google_wlc_parse_buf(data, mpp_mdis_num, val);

	if (ret != mpp_mdis_num) {
		kfree(tmp_kernel_buff);
		return -EINVAL;
	}
	memcpy(mpp_pwr_table, val, mpp_mdis_num * sizeof(u32));
	kfree(tmp_kernel_buff);
	return count;
}

BATTERY_DEBUG_ATTRIBUTE(de_mpp_mdis_pwr_fops, google_wlc_de_mpp_mdis_pwr_show,
						google_wlc_de_mpp_mdis_pwr_store);

static ssize_t google_wlc_de_mpp_mdis_chg_idx_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	if (charger->pdata->mpp_mdis_num > 0) {
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len, "MPP15 MDIS\n");
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);
		kfree(tmp_kernel_buff);
		return len;
	}

	if (charger->pdata->chg_configs_num < 0)
		return 0;

	for (i = 0; i < charger->pdata->chg_configs_num && PAGE_SIZE > len + 1; i++) {
		if (charger->mpp_mdis_chg_idx == i)
			len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
					 "*  %s\n", charger->pdata->chg_configs[i].mpp_mdis_name ?
					 charger->pdata->chg_configs[i].mpp_mdis_name : "none");
		else
			len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
					 "   %s\n", charger->pdata->chg_configs[i].mpp_mdis_name ?
					 charger->pdata->chg_configs[i].mpp_mdis_name : "none");
	}

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_mpp_mdis_chg_idx_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int ret, idx;

	ret = kstrtoint_from_user(user_buf, count, 10, &idx);
	if (ret)
		return ret;

	if (idx < 0 || idx >= charger->pdata->chg_configs_num)
		return -EINVAL;

	if (charger->pdata->mpp_mdis_num > 0)
		return count;

	if (charger->pdata->chg_configs_num < 0)
		return count;

	charger->mpp_mdis_chg_idx = idx;

	return count;
}

BATTERY_DEBUG_ATTRIBUTE(de_mpp_mdis_chg_idx_fops, google_wlc_de_mpp_mdis_chg_idx_show,
						google_wlc_de_mpp_mdis_chg_idx_store);

static ssize_t google_wlc_de_wlc_dc_mdis_fcc_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	for (i = 0; i < charger->pdata->wlc_dc_mdis_num && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
				 "%d ", charger->pdata->wlc_dc_mdis_fcc[i]);

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_wlc_dc_mdis_fcc_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int ret;
	u32 val[MDIS_LEVEL_MAX];
	char *data;
	char *tmp_kernel_buff;

	if (count > PAGE_SIZE)
		return -EINVAL;
	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	ret = google_wlc_parse_buf(data, charger->pdata->wlc_dc_mdis_num, val);

	if (ret != charger->pdata->wlc_dc_mdis_num) {
		kfree(tmp_kernel_buff);
		return -EINVAL;
	}
	memcpy(charger->pdata->wlc_dc_mdis_fcc, val,
		sizeof(charger->pdata->wlc_dc_mdis_fcc));
	kfree(tmp_kernel_buff);
	return count;
}

BATTERY_DEBUG_ATTRIBUTE(de_wlc_dc_mdis_fcc_fops, google_wlc_de_wlc_dc_mdis_fcc_show,
						google_wlc_de_wlc_dc_mdis_fcc_store);

static ssize_t google_wlc_de_dploss_steps_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	for (i = 0; i < MPP_DPLOSS_NUM_STEPS && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
				 "%d ", charger->pdata->dploss_steps[i]);
	if (len + 1 < PAGE_SIZE)
		tmp_kernel_buff[len++] = '\n';

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_dploss_steps_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int ret;
	u32 val[MPP_DPLOSS_NUM_STEPS];
	char *data;
	char *tmp_kernel_buff;

	if (count > PAGE_SIZE)
		return -EINVAL;
	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	ret = google_wlc_parse_buf(data, MPP_DPLOSS_NUM_STEPS, val);

	if (ret != MPP_DPLOSS_NUM_STEPS) {
		kfree(tmp_kernel_buff);
		return -EINVAL;
	}
	memcpy(charger->pdata->dploss_steps, val,
		sizeof(charger->pdata->dploss_steps));
	kfree(tmp_kernel_buff);
	return count;
}

BATTERY_DEBUG_ATTRIBUTE(de_dploss_steps_fops, google_wlc_de_dploss_steps_show,
						google_wlc_de_dploss_steps_store);
static ssize_t google_wlc_de_dploss_volts_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int i;
	ssize_t len = 0;
	char *tmp_kernel_buff;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	for (i = 0; i < MPP_DPLOSS_NUM_STEPS && PAGE_SIZE > len + 1; i++)
		len += scnprintf(tmp_kernel_buff + len, PAGE_SIZE - len,
				 "%d ", charger->pdata->dploss_volts[i]);
	if (len + 1 < PAGE_SIZE)
		tmp_kernel_buff[len++] = '\n';

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);

	return len;
}

static ssize_t google_wlc_de_dploss_volts_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	int ret;
	u32 val[MPP_DPLOSS_NUM_STEPS];
	char *data;
	char *tmp_kernel_buff;

	if (count > PAGE_SIZE)
		return -EINVAL;
	tmp_kernel_buff = kzalloc(count + 1, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	ret = simple_write_to_buffer(tmp_kernel_buff, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(tmp_kernel_buff);
		return -EFAULT;
	}

	data = tmp_kernel_buff;

	ret = google_wlc_parse_buf(data, MPP_DPLOSS_NUM_STEPS, val);

	if (ret != MPP_DPLOSS_NUM_STEPS) {
		kfree(tmp_kernel_buff);
		return -EINVAL;
	}
	memcpy(charger->pdata->dploss_volts, val,
		sizeof(charger->pdata->dploss_volts));
	kfree(tmp_kernel_buff);
	return count;
}

BATTERY_DEBUG_ATTRIBUTE(de_dploss_volts_fops, google_wlc_de_dploss_volts_show,
						google_wlc_de_dploss_volts_store);

static ssize_t google_wlc_packet_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	const size_t max = sizeof(struct google_wlc_packet) * 3 + 1;
	char buff[sizeof(struct google_wlc_packet) * 3 + 1];
	int ret;
	ssize_t len = 0;

	if (*ppos)
		return 0;

	if (charger->pkt_ready == -ETIME) {
		len = scnprintf(buff, max, "%s\n", "packet timeout, try to send again");
	} else if (charger->pkt_ready == -EBUSY) {
		len = scnprintf(buff, max, "%s\n", "packet not ready yet");
	} else {
		ret = google_wlc_read_packet(charger, buff, max);
		if (ret)
			len = scnprintf(buff, max, "%s\n", "packet reg read err");
		else
			len = scnprintf(buff, max, "%s\n", buff);
	}

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, buff, len);

	return len;
}

static ssize_t google_wlc_packet_store(struct file *filp,
	const char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	struct google_wlc_packet packet = {0};
	u8 bytes[sizeof(packet)];
	const size_t len = sizeof(packet);
	char *tmp;
	char *_buf;
	int ret, i = 0;

	if (count > len)
		return -EINVAL;

	_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!_buf)
		return -ENOMEM;

	ret = simple_write_to_buffer(_buf, count, ppos, user_buf, count);
	if (ret < 0) {
		kfree(_buf);
		return -EFAULT;
	}

	tmp = _buf;
	while (_buf && i < len) {
		char *p = strsep(&_buf, " ");

		if (*p)
			ret = kstrtou8(p, 16, &bytes[i]);
		else
			ret = -EINVAL;
		if (ret)
			break;
		i++;
	}
	if (ret == 0 && i >= 2) {
		packet.header = bytes[0];
		packet.cmd = bytes[1];
		memcpy(packet.data, bytes + 2, i - 2);
		ret = charger->chip->chip_send_packet(charger, packet);
		charger->pkt_ready = -EBUSY;
	} else if (ret == 0) {
		ret = -EINVAL;
	}
	kfree(tmp);

	return ret == 0 ? count : ret;
}

BATTERY_DEBUG_ATTRIBUTE(packet_fops, google_wlc_packet_show,
						google_wlc_packet_store);

static ssize_t google_wlc_fwupdate_state_show(struct file *filp,
	char __user *user_buf,
	size_t count,
	loff_t *ppos)
{
	struct google_wlc_data *charger = filp->private_data;
	char *tmp_kernel_buff;
	ssize_t len = 0;

	if (*ppos)
		return 0;

	tmp_kernel_buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp_kernel_buff)
		return -ENOMEM;

	if (charger->fw_data.update_option != FWUPDATE_FORCE_MANUAL &&
	    charger->fw_data.update_option != FWUPDATE_FORCE)
		charger->fwupdate_state = FWU_STATE_UNKNOWN;

	len = scnprintf(tmp_kernel_buff, PAGE_SIZE, "%s\n",
			google_wlc_fwupdate_state_str[charger->fwupdate_state]);

	if (len > 0)
		len = simple_read_from_buffer(user_buf, count, ppos, tmp_kernel_buff, len);

	kfree(tmp_kernel_buff);
	return len;
}

BATTERY_DEBUG_ATTRIBUTE(fwupdate_state_fops, google_wlc_fwupdate_state_show, NULL);

static int google_wlc_support_mpp25_show(void *data, u64 *val)
{
	struct google_wlc_data *charger = data;

	*val = charger->pdata->support_mpp25;
	return 0;
}

static int google_wlc_support_mpp25_store(void *data, u64 val)
{
	struct google_wlc_data *charger = data;

	charger->pdata->support_mpp25 = val;
	if (val == 0 && charger->mpp25.state != MPP25_OFF)
		google_wlc_disable_mpp25(charger, false);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(support_mpp25_fops, google_wlc_support_mpp25_show,
						google_wlc_support_mpp25_store, "%llu\n");

/* Acquires status_lock */
static void google_wlc_icl_ramp_target_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, icl_ramp_target_work.work);
	char reason[GVOTABLE_MAX_REASON_LEN] = "";
	int power_mw = gvotable_get_current_int_vote(charger->icl_ramp_target_votable);
	int decrease;

	if (charger->icl_ramp_target_mw == power_mw || charger->shutdown_notify) {
		__pm_relax(charger->icl_target_ws);
		return;
	}
	gvotable_get_current_reason(charger->icl_ramp_target_votable, reason,
				    GVOTABLE_MAX_REASON_LEN);
	logbuffer_devlog(charger->log, charger->dev,
			 "Changing ramp target from %d to %d voted by %s",
			 charger->icl_ramp_target_mw, power_mw, reason);
	mutex_lock(&charger->status_lock);

	if (power_mw < charger->icl_ramp_target_mw) {
		decrease = true;
		charger->rcs_backoff_count = 0;
	}

	charger->icl_ramp_target_mw = power_mw;
	charger->icl_accuracy_comp = 0;
	if (charger->status == GOOGLE_WLC_STATUS_CHARGING) {
		if (charger->icl_loop_mode == ICL_LOOP_INTERRUPT) {
			if (decrease) {
				google_wlc_trigger_icl_ramp(charger,
					GOOGLE_WLC_RAMP_RETRY_INTERVAL);
				charger->chip->chip_enable_load_increase(charger, false);
			} else {
				/* Released in icl_ramp_timeout_work */
				__pm_stay_awake(charger->icl_ramp_timeout_ws);
				schedule_delayed_work(&charger->icl_ramp_timeout_work,
				      msecs_to_jiffies(GOOGLE_WLC_ICL_RAMP_TIMEOUT_MS));
				charger->chip->chip_enable_load_increase(charger, true);
			}
			charger->ramp_done_check_count = 0;
		}
		if (charger->icl_loop_mode == ICL_LOOP_TIMER &&
		    charger->icl_loop_state == ICL_LOOP_DONE)
			google_wlc_trigger_icl_ramp(charger, GOOGLE_WLC_RAMP_RETRY_INTERVAL);
	}

	mutex_unlock(&charger->status_lock);
	__pm_relax(charger->icl_target_ws);
}

/* Return 1 if successful */
static int google_wlc_icl_ramp_vote_callback(struct gvotable_election *el,
					      const char *reason, void *vote)
{
	struct google_wlc_data *charger = gvotable_get_data(el);

	__pm_stay_awake(charger->icl_target_ws);
	mod_delayed_work(system_wq, &charger->icl_ramp_target_work, 0);

	return 1;
}

static void google_wlc_dc_power_limit_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, dc_power_limit_work.work);
	int ret;
	int power_mw = gvotable_get_current_int_vote(charger->wlc_dc_power_votable);

	if (!in_wlc_dc(charger) || power_mw <= 0 || charger->shutdown_notify)
		return;

	ret = GPSY_SET_PROP(charger->wlc_dc_psy, POWER_SUPPLY_PROP_INPUT_POWER_LIMIT, power_mw);

	if (ret) {
		logbuffer_devlog(charger->log, charger->dev,
			"WLC-DC: Unable to set power limit to SWC; Disable");
		google_wlc_disable_current_charger(charger);

	}
	charger->wlc_dc_to_ramp = false;
	logbuffer_devlog(charger->log, charger->dev, "WLC-DC: Max Power set to %d", power_mw);
}

static int google_wlc_dc_power_vote_callback(struct gvotable_election *el,
					      const char *reason, void *vote)
{
	struct google_wlc_data *charger = gvotable_get_data(el);
	int power_mw = GVOTABLE_PTR_TO_INT(vote);

	if (!in_wlc_dc(charger) || power_mw <= 0)
		return 0;

	mod_delayed_work(system_wq, &charger->dc_power_limit_work, 0);

	return 0;
}

static void google_wlc_disable_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, disable_work.work);
	int disable_vote = gvotable_get_current_int_vote(charger->wlc_disable_votable);
	int ret = 0;

	mutex_lock(&charger->status_lock);

	if (charger->shutdown_notify)
		goto exit;

	if (charger->wait_for_cloak && disable_vote != WLC_CLOAK_ONLY &&
	    disable_vote != WLC_SOFT_DISABLE) {
		complete(&charger->cloak_completion);
		charger->wait_for_cloak = false;
	}
	if (disable_vote != WLC_HARD_DISABLE && charger->disable_state == WLC_HARD_DISABLE) {
		gpiod_set_value_cansleep(charger->pdata->inhibit_gpio, 0);
		__pm_stay_awake(charger->notifier_ws);
		pm_system_wakeup();
		schedule_delayed_work(&charger->psy_notifier_work, 0);
	}

	switch (disable_vote) {
	case WLC_NOT_DISABLED:
		if (charger->disable_state == WLC_NOT_DISABLED)
			goto exit;
		logbuffer_devlog(charger->log, charger->dev, "WLC not disabled");
		if (charger->status == GOOGLE_WLC_STATUS_CLOAK) {
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK_EXITING);
		} else if (charger->status == GOOGLE_WLC_STATUS_CLOAK_ENTERING) {
			dev_info(charger->dev, "WLC enabled while in CLOAK_ENTERING. Sending EPT to recover.");
			charger->chip->chip_send_ept(charger, EPT_RESTART_POWER_TRANSFER);
		}
		break;
	case WLC_CLOAK_ONLY:
		if (!google_wlc_is_present(charger)) {
			dev_dbg(charger->dev, "Not present, skip disable\n");
			if (charger->wait_for_cloak) {
				complete(&charger->cloak_completion);
				charger->wait_for_cloak = false;
			}
			break;
		}
		if (charger->status == GOOGLE_WLC_STATUS_CLOAK)
			break;
		if (charger->wait_for_mode > 0 ||
		    (mode_is_mpp(charger->mode) && !wlc_is_ready_for_cloak(charger)) ||
		    charger->status == GOOGLE_WLC_STATUS_CLOAK_ENTERING) {
			dev_info(charger->dev, "Reschedule disable work\n");
			mod_delayed_work(system_wq, &charger->disable_work,
				 msecs_to_jiffies(GOOGLE_WLC_CLOAK_DEBOUNCE_MS));
			break;
		} else if (wlc_is_ready_for_cloak(charger)) {
			logbuffer_devlog(charger->log, charger->dev,
					 "WLC cloak only, prev mode = %s",
					 sys_op_mode_str[charger->mode]);
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK_ENTERING);
		}
		break;
	case WLC_SOFT_DISABLE:
		if (!google_wlc_is_present(charger)) {
			dev_dbg(charger->dev, "Not present, skip disable\n");
			break;
		}
		logbuffer_devlog(charger->log, charger->dev, "WLC soft disable, prev mode = %s",
				 sys_op_mode_str[charger->mode]);
		if (charger->status == GOOGLE_WLC_STATUS_CLOAK)
			break;
		if (charger->wait_for_mode > 0 ||
		    (!wlc_is_ready_for_cloak(charger) && !wlc_is_ready_for_pdet(charger)) ||
		    charger->status == GOOGLE_WLC_STATUS_CLOAK_ENTERING) {
			dev_info(charger->dev, "Reschedule disable work, mode=%d\n", charger->mode);
			mod_delayed_work(system_wq, &charger->disable_work,
				 msecs_to_jiffies(GOOGLE_WLC_CLOAK_DEBOUNCE_MS));
			break;
		}
		ret = google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK_ENTERING);
		if (ret != 0) {
			dev_info(charger->dev, "Cloak enter failed, retry disable work\n");
			mod_delayed_work(system_wq, &charger->disable_work,
				msecs_to_jiffies(GOOGLE_WLC_CLOAK_DEBOUNCE_MS));
		}
		break;
	case WLC_HARD_DISABLE:
		logbuffer_devlog(charger->log, charger->dev, "WLC hard disable");
		if (charger->pdata->inhibit_gpio > 0)
			gpiod_set_value_cansleep(charger->pdata->inhibit_gpio, 1);
		if (charger->status == GOOGLE_WLC_STATUS_CLOAK ||
		    charger->mode == RX_MODE_MPP_CLOAK ||
		    charger->mode == RX_MODE_PDET) {
			dev_info(charger->dev, "Cloak->Inhibit, exit cloak");
			ret = charger->chip->chip_set_cloak_mode(charger, false, 0);
		}
		break;
	}
exit:
	charger->disable_state = disable_vote;
	mutex_unlock(&charger->status_lock);
	__pm_relax(charger->wlc_disable_ws);
}

/* Return 1 if successful */
static int google_wlc_wlc_disable_vote_callback(struct gvotable_election *el,
					      const char *reason, void *vote)
{
	struct google_wlc_data *charger = gvotable_get_data(el);
	__pm_stay_awake(charger->wlc_disable_ws);
	mod_delayed_work(system_wq, &charger->disable_work, 0);

	return 1;
}

static void google_wlc_int_string(char *buf, int max_size, struct google_wlc_bits int_fields)
{
	int added = 0;

	if (int_fields.prop_received)
		added += scnprintf(buf + added, max_size - added, "prop_rcvd, ");
	if (int_fields.sadt_received)
		added += scnprintf(buf + added, max_size - added, "sadt_rcvd, ");
	if (int_fields.sadt_sent)
		added += scnprintf(buf + added, max_size - added, "sadt_sent, ");
	if (int_fields.stat_vout)
		added += scnprintf(buf + added, max_size - added, "vout, ");
	if (int_fields.stat_vrect)
		added += scnprintf(buf + added, max_size - added, "vrect, ");
	if (int_fields.operation_mode)
		added += scnprintf(buf + added, max_size - added, "mode, ");
	if (int_fields.over_voltage)
		added += scnprintf(buf + added, max_size - added, "OV, ");
	if (int_fields.over_current)
		added += scnprintf(buf + added, max_size - added, "OC, ");
	if (int_fields.over_temperature)
		added += scnprintf(buf + added, max_size - added, "over_temp, ");
	if (int_fields.sadt_error)
		added += scnprintf(buf + added, max_size - added, "sadt_err, ");
	if (int_fields.power_adjust)
		added += scnprintf(buf + added, max_size - added, "power_adjust, ");
	if (int_fields.load_decrease_alert)
		added += scnprintf(buf + added, max_size - added, "load_decrease_alert, ");
	if (int_fields.load_increase_alert)
		added += scnprintf(buf + added, max_size - added, "load_increase_alert, ");
	if (int_fields.fsk_timeout)
		added += scnprintf(buf + added, max_size - added, "fsk_timeout, ");
	if (int_fields.fsk_received)
		added += scnprintf(buf + added, max_size - added, "fsk_received, ");
	if (int_fields.dploss_cal_success)
		added += scnprintf(buf + added, max_size - added, "dploss_response, ");
	if (int_fields.dploss_cal_error)
		added += scnprintf(buf + added, max_size - added, "dploss_error, ");
	if (int_fields.rcs)
		added += scnprintf(buf + added, max_size - added, "rcs, ");
	if (int_fields.dploss_param_match)
		added += scnprintf(buf + added, max_size - added, "dploss_param_match, ");
	if (int_fields.dploss_param_error)
		added += scnprintf(buf + added, max_size - added, "dploss_param_error, ");
	if (int_fields.dploss_cal_retry)
		added += scnprintf(buf + added, max_size - added, "dploss_retry, ");
	if (int_fields.fsk_missing)
		added += scnprintf(buf + added, max_size - added, "fsk_missing, ");
	if (int_fields.dynamic_mod)
		added += scnprintf(buf + added, max_size - added, "dynamic_mod, ");
	if (int_fields.cloak_error)
		added += scnprintf(buf + added, max_size - added, "cloak_error, ");
	if (int_fields.vdd_err)
		added += scnprintf(buf + added, max_size - added, "vdd_error, ");
	if (int_fields.sys_mode)
		added += scnprintf(buf + added, max_size - added, "sys_mode, ");
	/* Cut off the last comma or return empty string if nothing found */
	if (added >= 2)
		buf[added-2] = '\0';
	else
		buf[0] = '\0';
}

static u32 google_wlc_check_iop_vout_by_mfg(struct google_wlc_data *charger)
{
	u16 ptmc;
	int i, ret;

	ret = charger->chip->chip_get_ptmc_id(charger, &ptmc);
	if (ret < 0) {
		dev_err(charger->dev, "fail to read ptmc, ret=%d", ret);
		return 0;
	}
	for (i = 0; i < charger->pdata->iop_vout_mfg_num; i++) {
		if (ptmc == charger->pdata->iop_vout_mfg[i]) {
			dev_info(&charger->client->dev, "mfg is 0x%04x\n",
					charger->pdata->iop_vout_mfg[i]);
			return charger->pdata->iop_vout_mv[i];
		}
	}

	return 0;
}

static void google_wlc_check_iop_adjust_vout(struct google_wlc_data *charger)
{
	u32 vout_mv;
	u8 qi_ver;
	int ret;

	ret = charger->chip->chip_get_tx_qi_ver(charger, &qi_ver);
	if (ret < 0) {
		dev_err(charger->dev, "fail to read qi_ver, ret=%d", ret);
		return;
	}

	if (qi_ver != 0x12)
		return;

	vout_mv = google_wlc_check_iop_vout_by_mfg(charger);
	if (vout_mv == 0)
		return;

	dev_info(charger->dev, "qi_ver=%x, set to %dmV for IOP", qi_ver, vout_mv);

	ret = charger->chip->chip_set_vout(charger, vout_mv);
	if (ret < 0)
		dev_err(charger->dev, "fail to set vout, ret=%d", ret);
}

static void google_wlc_set_iop_vout_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, set_iop_vout_work.work);

	google_wlc_check_iop_adjust_vout(charger);
}

static void google_wlc_cloak_err_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, cloak_err_work.work);
	u8 reason;

	charger->chip->chip_get_cloak_exit_reason(charger, &reason);

	if (reason == CLOAK_EXIT_FSK_FAIL ||
	    reason == CLOAK_PTX_POWER_DOWN_FAILURE ||
	    reason == CLOAK_PTX_FSK_MISSING) {
		charger->cloak_err_count++;
		dev_info(charger->dev, "cloak exit reason: %d, count=%d",
			 reason, charger->cloak_err_count);
		if (charger->cloak_err_count > charger->cloak_err_count_max) {
			google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
			charger->cloak_err_count = 0;
		}
	} else if (reason == CLOAK_EXIT_NORMAL) {
		charger->cloak_err_count = 0;
	} else {
		dev_info(charger->dev, "cloak exit reason: %d", reason);
		charger->cloak_err_count = 0;
		google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
	}
}

static void google_wlc_vdd_err_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, vdd_err_work.work);

	mutex_lock(&charger->ext_boost_lock);
	dev_warn(charger->dev, "vdd_error, ext_boost=%d",
		 charger->ext_boost_enabled);
	mutex_unlock(&charger->ext_boost_lock);

	google_wlc_wait_for_sending_ept(charger, EPT_RESTART_POWER_TRANSFER);
	schedule_delayed_work(&charger->psy_notifier_work,
			      GOOGLE_WLC_NOTIFIER_DELAY_MS);
}

static int google_wlc_get_xid(struct google_wlc_data *charger)
{
	u32 device_id, mfg_id, unique_id;
	int ret;

	ret = charger->chip->chip_get_mpp_xid(charger, &device_id, &mfg_id,
					      &unique_id, &charger->product_id);
	if (ret < 0)
		return ret;

	charger->tx_id = unique_id ? unique_id : device_id;
	if (charger->tx_id) {
		google_wlc_set_online(charger);

		if (charger->last_tx_id != charger->tx_id) {
			charger->last_tx_id = charger->tx_id;

			/* reset once tx change is identified */
			charger->disconnect_total_count = 0;
		}
	}

	return 0;
}

static void google_wlc_xid_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, xid_work.work);
	int ret;

	mutex_lock(&charger->status_lock);
	charger->xid_retry_count++;
	if (charger->xid_retry_count > GOOGLE_WLC_XID_RETRY_COUNT) {
		__pm_relax(charger->xid_ws);
		mutex_unlock(&charger->status_lock);
		return;
	}
	/* ensure ptmc before xid */
	ret = charger->chip->chip_get_ptmc_id(charger, &charger->ptmc);
	if (ret || charger->ptmc == 0)
		goto reschedule;

	google_wlc_get_xid(charger);

	if (charger->tx_id) {
		if (charger->txsrc)
			google_wlc_uevent(charger, UEVENT_TXSRC);
		__pm_relax(charger->xid_ws);
		mutex_unlock(&charger->status_lock);
		return;
	}

	if (charger->online || charger->status == GOOGLE_WLC_STATUS_NOT_DETECTED) {
		__pm_relax(charger->xid_ws);
		mutex_unlock(&charger->status_lock);
		return;
	}

reschedule:
	schedule_delayed_work(&charger->xid_work,
			      msecs_to_jiffies(GOOGLE_WLC_XID_DELAY_MS));
	mutex_unlock(&charger->status_lock);
}

static void google_wlc_mode_change_irq(struct google_wlc_data *charger)
{
	int ret;
	u8 val8;
	u8 mode;
	bool is_not_pdet = charger->mode != RX_MODE_PDET;
	bool is_cloak = charger->status == GOOGLE_WLC_STATUS_CLOAK ||
			charger->status == GOOGLE_WLC_STATUS_CLOAK_ENTERING ||
			charger->mode == RX_MODE_MPP_CLOAK;

	mutex_lock(&charger->status_lock);
	charger->mode_ready = true;
	ret = charger->chip->chip_get_sys_mode(charger, &mode);
	if (ret < 0) {
		dev_err(charger->dev, "irq_handler: Unable to read mode register\n");
		goto exit;
	}
	logbuffer_devlog(charger->log, charger->dev, "mode change irq to %s",
			 sys_op_mode_str[mode]);
	if (charger->wait_for_exit_cloak && mode != RX_MODE_MPP_CLOAK) {
		complete(&charger->cloak_completion);
		charger->wait_for_exit_cloak = false;
	}
	if (mode == RX_MODE_WPC_MPP_NEGO) {
		schedule_delayed_work(&charger->mpp_nego_timeout_work,
					msecs_to_jiffies(GOOGLE_WLC_MPP_NEGO_TIMEOUT_MS));
	} else {
		/* not using sync because mpp_nego_timeout_work also takes status_lock (deadlock) */
		cancel_delayed_work(&charger->mpp_nego_timeout_work);
	}
	switch (mode) {
	case RX_MODE_MPP_CLOAK:
		if (charger->wait_for_cloak) {
			complete(&charger->cloak_completion);
			charger->wait_for_cloak = false;
		}
		ret = charger->chip->chip_get_cloak_reason(charger, &val8);
		google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK);
		if (ret == 0 && val8 == CLOAK_FOD) {
			dev_info(charger->dev, "WLC-DC: FOD cloak");
			if (charger->mpp25.fod_cloak == FOD_CLOAK_STARTED)
				charger->mpp25.fod_cloak = FOD_CLOAK_ENTERED;
		} else if (ret == 0 && val8 == CLOAK_TX_INITIATED) {
			dev_info(charger->dev, "TX initiated cloak");
		} else if (charger->disable_state == WLC_NOT_DISABLED) {
			dev_info(charger->dev, "RX initiated Cloak irq unexpectedly; exit cloak\n");
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK_EXITING);
		}
		if (charger->pdata->has_mpp25_wlc_dc &&
		    (charger->mpp25.state == MPP25_ENTER_HPM ||
		    (charger->mpp25.state == MPP25_ACTIVE && charger->mpp25.back_npm &&
		     !charger->mpp25.pwrmode_ok))) {
			dev_info(charger->dev, "WLC-DC: %s transition, turn off SWC",
				 charger->mpp25.state == MPP25_ACTIVE ?
				 "HPM->NPM" : "NPM->HPM");
			GPSY_SET_PROP(charger->wlc_dc_psy,
				GBMS_PROP_ENABLE_SWITCH_CAP, CP_DISABLE_SWITCH_CAP_NO_RAMP_DOWN);
			charger->dc_data.swc_en_state = SWC_DISABLED;
		}
		if (charger->ask_mod_mode != ASK_MOD_MODE_ADAPTIVE_LOAD_MOD) {
			ret = charger->chip->chip_set_mod_mode(charger,
				ASK_MOD_MODE_ADAPTIVE_LOAD_MOD);
			if (ret == 0)
				charger->ask_mod_mode = ASK_MOD_MODE_ADAPTIVE_LOAD_MOD;
		}
		break;
	case RX_MODE_AC_MISSING:
		if (is_not_pdet && is_cloak) {
			u8 reason;

			charger->chip->chip_get_cloak_exit_reason(charger, &reason);
			if (reason != 0)
				dev_info(charger->dev, "cloak exit reason: %d", reason);
		}
		if (charger->status == GOOGLE_WLC_STATUS_CLOAK ||
		    charger->status == GOOGLE_WLC_STATUS_CLOAK_ENTERING ||
		    charger->mode == RX_MODE_MPP_CLOAK || charger->mode == RX_MODE_PDET)
			ret = charger->chip->chip_set_cloak_mode(charger, false, 0);
		if (charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED)
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_NOT_DETECTED);
		break;
	case RX_MODE_WPC_EPP_NEGO:
	case RX_MODE_WPC_EPP:
	case RX_MODE_WPC_BPP:
		if (charger->feature & WLCF_DREAM_DEFER)
			google_wlc_set_online(charger);
		if (mode == RX_MODE_WPC_BPP)
			charger->vrect_count = 0;
		if (charger->eds_event)
			google_wlc_uevent(charger, UEVENT_WLC_ON);
		charger->chip->chip_write_fod(charger, mode);
		fallthrough;
	case RX_MODE_WPC_MPP_RESTRICTED:
	case RX_MODE_WPC_MPP_NEGO:
	case RX_MODE_WPC_MPP:
	case RX_MODE_WPC_MPP_CPM:
	case RX_MODE_WPC_MPP_NPM:
	case RX_MODE_WPC_MPP_LPM:
	case RX_MODE_WPC_MPP_HPM:
		if (charger->wait_for_mode > 0) {
			if (charger->wait_for_mode == mode &&
					!charger->pwrmode_switch_initial_done)
				charger->pwrmode_switch_initial_done = true;
			else if (mode == RX_MODE_WPC_MPP_NEGO &&
					charger->pwrmode_switch_initial_done)
				charger->pwrmode_switch_renego_done = true;
			else if (charger->wait_for_mode == mode &&
					charger->pwrmode_switch_renego_done)
				complete(&charger->pwrmode_switch);
		}
		if (charger->eds_event)
			google_wlc_uevent(charger, UEVENT_WLC_ON);

		if (charger->mpp25.fod_cloak == FOD_CLOAK_ENTERED) {
			charger->mpp25.fod_cloak = FOD_CLOAK_RESEND;
			if (charger->wait_for_cal_enter)
				complete(&charger->cal_enter_done);
		}
		google_wlc_setup_by_mode(charger, mode);
		if (in_wlc_dc(charger) && charger->status != GOOGLE_WLC_STATUS_DC_CHARGING)
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_DC_CHARGING);
		else if (charger->status != GOOGLE_WLC_STATUS_CHARGING &&
			 charger->status != GOOGLE_WLC_STATUS_DETECTED)
			schedule_delayed_work(&charger->psy_notifier_work, 0);
		else if (mode != charger->mode && charger->wlc_charge_enabled) {
			__pm_stay_awake(charger->init_charging_ws);
			mod_delayed_work(system_wq, &charger->charging_init_work, 0);
		}
		if (charger->disable_state == WLC_SOFT_DISABLE ||
		    charger->disable_state == WLC_CLOAK_ONLY) {
			dev_err(charger->dev,
				"Mode change while disabled, reschedule disable_work\n");
			mod_delayed_work(system_wq, &charger->disable_work,
				 msecs_to_jiffies(0));
		}
		break;
	case RX_MODE_PDET:
		if (charger->disable_state == WLC_NOT_DISABLED &&
		    charger->status != GOOGLE_WLC_STATUS_CLOAK_ENTERING) {
			dev_info(charger->dev, "PDET irq unexpectedly; exit PDET\n");
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK_EXITING);
		} else {
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK);
		}
	}
	charger->mode = mode;
exit:
	mutex_unlock(&charger->status_lock);
}

static void google_wlc_rcs_irq(struct google_wlc_data *charger, int rcs)
{
	int ret;
	mutex_lock(&charger->status_lock);
	if (charger->status == GOOGLE_WLC_STATUS_CHARGING &&
		   (rcs == WPC_RCS_TX_MAX_POWER ||
		    rcs == WPC_RCS_TX_SYS_PROTECTION ||
		    rcs == WPC_RCS_TX_MAX_VOLTAGE)) {

		charger->rcs_backoff_count = RCS_LOAD_BACKOFF_IRQ_COUNT;
		google_wlc_trigger_icl_ramp(charger, 0);
		mutex_unlock(&charger->status_lock);
		return;
	}
	mutex_unlock(&charger->status_lock);

	if (charger->status != GOOGLE_WLC_STATUS_DC_CHARGING)
		return;
	if (rcs == WPC_RCS_TX_MAX_POWER || rcs == WPC_RCS_TX_SYS_PROTECTION) {
		GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_WLC_LOAD_DECREASE,
				WLC_DC_LOAD_DECREASE_STEP * 1000);
		charger->chip->chip_enable_load_increase(charger, true);
	}
	if (rcs == WPC_RCS_TX_MAX_VOLTAGE) {
		u32 vout_mv;

		ret = charger->chip->chip_get_vout_set(charger, &vout_mv);
		if (ret)
			return;
		charger->dc_data.max_voltage_limit = vout_mv - WLC_DC_LOAD_DECREASE_STEP;
		GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_TA_MAX_VOLTAGE,
				charger->dc_data.max_voltage_limit * 1000);
		dev_info(charger->dev, "decreased max voltage to %d",
				charger->dc_data.max_voltage_limit);

	}
}

static void google_wlc_vrect_init(struct google_wlc_data *charger)
{
	if ((charger->this_vrect - charger->last_vrect) > (WLC_DISCONNECT_TIMEOUT / 1000)) {
		charger->product_id = 0;
		charger->txsrc = 0;
		charger->ptmc = 0;
		if (charger->feature & WLCF_DREAM_DEFER) {
			__pm_stay_awake(charger->xid_ws);
			charger->xid_retry_count = 0;
			schedule_delayed_work(&charger->xid_work,
					      msecs_to_jiffies(GOOGLE_WLC_XID_DELAY_MS));
		}
	}
}

static void google_wlc_check_iop(struct google_wlc_data *charger)
{
	const u32 iop_fail_count_max = charger->iop_fail_count_max > 0 ?
		charger->iop_fail_count_max : GOOGLE_WLC_IOP_FAIL_COUNT_MAX;

	if (charger->last_capacity < 0 || charger->last_capacity > IOP_SOC_MAX) {
		charger->vrect_count = 0;
		return;
	}

	if ((charger->this_vrect - charger->last_vrect) > (GOOGLE_WLC_IOP_FAIL_TIMEOUT_MS / 1000) ||
	    charger->disable_state != WLC_NOT_DISABLED) {
		if (charger->vrect_count > 1)
			logbuffer_devlog(charger->log, charger->dev, "Reset IOP count");

		charger->vrect_count = 1;
	} else if (charger->txsrc) {
		// avoid forcing BPP during battery mode reping
		charger->vrect_count = 0;
	} else {
		charger->vrect_count++;
	}

	if (charger->vrect_count > iop_fail_count_max) {
		if (charger->pdata->support_epp) {
			logbuffer_devlog(charger->log, charger->dev,
					"Lower Qi version after IOP checked");
			gpiod_set_value_cansleep(charger->pdata->qi_version_gpio, 0);
		} else {
			logbuffer_devlog(charger->log, charger->dev, "Set BPP after IOP checked");
			google_wlc_set_mode_gpio(charger, RX_MODE_WPC_BPP);
		}
		google_wlc_clear_target(charger);
		charger->vrect_count = 0;
		charger->iop_bpp = true;
		schedule_delayed_work(&charger->check_iop_timeout_work,
				      msecs_to_jiffies(GOOGLE_WLC_IOP_FAIL_TIMEOUT_MS));
	}
}

static void google_wlc_fsk_missing_check(struct google_wlc_data *charger)
{
	const ktime_t now = get_boot_sec();

	if (charger->fsk_missing_last == 0 ||
	    now - charger->fsk_missing_last > GOOGLE_WLC_FSK_MISSING_INT_CHECK_S) {
		charger->history_index = 0;
		memset(charger->fsk_missing_history, 0, sizeof(charger->fsk_missing_history));
	}

	charger->fsk_missing_history[charger->history_index] = now;
	charger->history_index = (charger->history_index + 1) % FSK_MISSING_HISTORY_SIZE;
	charger->fsk_missing_last = now;
}

static void google_wlc_irq_handler(struct google_wlc_data *charger,
				    struct google_wlc_bits int_fields)
{
	int ret;
	u8 project_id, mode_reg;
	u32 val32;

	if (int_fields.stat_vrect) {
		mutex_lock(&charger->status_lock);
		charger->this_vrect = get_boot_sec();
		google_wlc_check_iop(charger);
		google_wlc_vrect_init(charger);
		charger->chip->chip_write_mpla(charger);
		charger->chip->chip_write_rf_curr(charger);
		charger->chip->chip_write_poweron_params(charger);
		ret = charger->chip->chip_get_opfreq(charger, &val32);
		if (charger->status == GOOGLE_WLC_STATUS_NOT_DETECTED) {
			logbuffer_devlog(charger->log, charger->dev, "VRECT 0->1");
			charger->last_charging_uevent = UEVENT_WLC_OFF;
		} else {
			logbuffer_devlog(charger->log, charger->dev,
					 "VRECT 1 irq, go to NOT_DETECTED->DETECTED");
			google_wlc_set_status(charger, GOOGLE_WLC_STATUS_NOT_DETECTED);
		}
		google_wlc_external_boost_en(charger, true);
		google_wlc_set_status(charger, GOOGLE_WLC_STATUS_DETECTED);
		if (ret == 0 && charger->last_opfreq < GOOGLE_WLC_OPFREQ_THRES &&
		    val32 > GOOGLE_WLC_OPFREQ_THRES && charger->disconnect_count > 0) {
			dev_info(charger->dev, "MPP 360k detected");
			charger->disconnect_count--;
			google_wlc_check_mpp_initialized(charger);
		}
		charger->last_opfreq = val32;
		dev_info(charger->dev, "Opfreq: %d", charger->last_opfreq);
		ret = charger->chip->chip_get_sys_mode(charger, &mode_reg);
		if (ret == 0 && mode_reg != RX_MODE_AC_MISSING && mode_reg != RX_MODE_UNKNOWN)
			charger->mode_ready = true;
		else
			charger->mode_ready = false;
		charger->last_vrect = charger->this_vrect;
		mutex_unlock(&charger->status_lock);
		charger->chip->chip_enable_interrupts(charger);
	}
	if (int_fields.stat_vout) {
		mutex_lock(&charger->status_lock);
		charger->vout_ready = true;
		if (mode_is_mpp(charger->mode)) {
			u32 device_id, mfg_id, unique_id, kest;
			u8 qi_ver;

			ret = charger->chip->chip_get_ptmc_id(charger, &charger->ptmc);
			ret = ret | charger->chip->chip_get_mpp_xid(charger, &device_id, &mfg_id,
								    &unique_id,
								    &charger->product_id);
			ret = ret | charger->chip->chip_get_tx_qi_ver(charger, &qi_ver);
			ret = ret | charger->chip->chip_get_tx_kest(charger, &kest);
			if (ret == 0) {
				logbuffer_devlog(charger->log, charger->dev,
						 "MPP TX info, PTMC: 0x%04x, XID_devid: 0x%06x, XID_mfgid: 0x%06x, unique_id:0x%08x, qi_ver:%x, kest: %d/1000, product_id: %d",
						 charger->ptmc, device_id, mfg_id, unique_id,
						 qi_ver, kest, charger->product_id);
				charger->tx_id = unique_id ? unique_id : device_id;
				if (charger->last_tx_id != charger->tx_id) {
					charger->last_tx_id = charger->tx_id;
					/* reset once tx change is identified */
					charger->disconnect_total_count = 0;
				}
			}
			schedule_delayed_work(&charger->pla_ack_timeout_work,
					      msecs_to_jiffies(WLC_PLA_ACK_TIMEOUT_MS));
			google_wlc_check_fsk_missing_offline(charger);
			if (!charger->dc_avail_votable)
				charger->dc_avail_votable =
					gvotable_election_get_handle("DC_AVAIL");
			if (charger->session_txid == charger->tx_id &&
			    charger->wlc_offline_count >= 3) {
				if (charger->wlc_offline_count > 3 && charger->dc_avail_votable) {
					gvotable_cast_int_vote(charger->dc_avail_votable,
							       WLC_VOTER, GBMS_ALL_CP_CHG_DISABLED,
							       true);
					dev_info(charger->dev, "check_fsk_missing: disable CP 3:1");
				}
				google_wlc_disable_mpp25(charger, false);
				dev_info(charger->dev, "check_fsk_missing: disable MPP25");
			} else if (charger->session_txid != charger->tx_id &&
				   charger->session_txid != 0) {
				charger->mpp25_disabled = false;
				charger->session_txid = 0;
				charger->wlc_offline_count = 0;
				gvotable_cast_int_vote(charger->dc_avail_votable, WLC_VOTER,
						       GBMS_ALL_CP_CHG_DISABLED, false);
			}
			charger->history_index = 0;
			memset(charger->fsk_missing_history, 0,
			       sizeof(charger->fsk_missing_history));
			dev_info(charger->dev,
				 "check_fsk_missing: session_txid:0x%06x, txid=0x%06x, wlc_offline_count=%d, mpp25_dis=%d",
				 charger->session_txid, charger->tx_id,
				 charger->wlc_offline_count, charger->mpp25_disabled);
		}
		ret = charger->chip->chip_get_project_id(charger, &project_id);
		if (ret == 0)
			dev_info(charger->dev, "Project ID: 0x%02x, (dt: 0x%02x)\n",
				 project_id, charger->pdata->project_id);
		charger->vrect_count = 0;
		mutex_unlock(&charger->status_lock);
	}
	if (int_fields.operation_mode)
		google_wlc_mode_change_irq(charger);
	if (int_fields.sadt_received) {
		size_t rxlen = 0;
		u8 stream = 0;

		ret = charger->chip->chip_recv_eds(charger, &rxlen, &stream);
		if (ret) {
			charger->rx_len = 0;
			charger->rx_thermal_len = 0;
			dev_err(charger->dev,
				"failed to read data: %d\n", ret);
		} else {
			if (stream == EDS_THERMAL)
				charger->rx_thermal_len = rxlen;
			else
				charger->rx_len = rxlen;
		}
		set_eds_state(charger, EDS_RECEIVED);
	}
	if (int_fields.sadt_error) {
		u8 err;

		ret = charger->chip->chip_get_adt_err(charger, &err);
		if (ret == 0)
			dev_err(charger->dev, "got sadt err: %d\n", err);
		charger->rx_len = 0;
		charger->rx_thermal_len = 0;
		google_wlc_eds_reset(charger);
	}
	if (int_fields.sadt_sent)
		set_eds_state(charger, EDS_SENT);
	if (int_fields.power_adjust) {
		mod_delayed_work(system_wq, &charger->set_iop_vout_work, msecs_to_jiffies(1000));
		google_wlc_adjust_negotiated_power(charger);
		if (charger->mpp25.state == MPP25_DPLOSS_CALIBRATION) {
			int dploss_max;

			dploss_max = charger->pdata->dploss_steps[MPP_DPLOSS_NUM_STEPS - 1];
			if (charger->nego_power >= dploss_max && charger->wait_for_cal_renego) {
				complete(&charger->cal_renego_done);
			} else if (charger->nego_power < dploss_max) {
				dev_info(charger->dev,
					 "DPLOSS: Nego power too low during cal, disable");
				google_wlc_disable_mpp25(charger, true);
			}
		} else if (charger->mpp25.state == MPP25_DPLOSS_CAL4 &&
			charger->nego_power < charger->mpp25_dploss_cal4) {
			dev_info(charger->dev,
					"DPLOSS: Nego power too low during 4th cal, abort");
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
		}
	}
	if (int_fields.load_decrease_alert) {
		charger->irq_load_decrease_count++;
		if (charger->status == GOOGLE_WLC_STATUS_DC_CHARGING) {
			GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_WLC_LOAD_DECREASE,
				      WLC_DC_LOAD_DECREASE_STEP * 1000);
			charger->chip->chip_enable_load_increase(charger, true);
		} else if (charger->icl_loop_mode == ICL_LOOP_INTERRUPT) {
			charger->force_icl_decrease = true;
			google_wlc_trigger_icl_ramp(charger, 0);
		}
	}
	if (int_fields.load_increase_alert) {
		charger->irq_load_decrease_count = 0;
		cancel_delayed_work_sync(&charger->pla_ack_timeout_work);
		if (charger->status == GOOGLE_WLC_STATUS_DC_CHARGING) {
			GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_WLC_LOAD_DECREASE, 0);
			charger->chip->chip_enable_load_increase(charger, false);
		} else if (charger->icl_loop_mode == ICL_LOOP_INTERRUPT) {
			charger->force_icl_decrease = false;
			google_wlc_trigger_icl_ramp(charger, 0);
		} else {
			charger->chip->chip_enable_load_increase(charger, false);
		}
	}
	if (int_fields.over_current) {
		if (charger->irq_error_count < 0xFFFF)
			charger->irq_error_count++;
		if (charger->status == GOOGLE_WLC_STATUS_CHARGING) {
			charger->force_icl_decrease = true;
			google_wlc_trigger_icl_ramp(charger, 0);
		}
	}
	if (int_fields.over_voltage) {
		if (charger->irq_error_count < 0xFFFF)
			charger->irq_error_count++;
		if (in_wlc_dc(charger) && charger->dc_data.swc_en_state == SWC_ENABLED) {
			int val;

			val = GPSY_GET_INT_PROP(charger->wlc_dc_psy, GBMS_PROP_ENABLE_SWITCH_CAP,
						&ret);
			if (ret == 0)
				dev_info(charger->dev, "WLC-DC: OV during SWC, SWC OK: %d", val);
		}
	}
	if (int_fields.dploss_cal_error) {
		if (charger->mpp25.cal_active) {
			if (charger->mpp25.state == MPP25_DPLOSS_CAL4) {
				dev_info(charger->dev, "MPP25: Error during Cal4, abort");
				google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
			} else {
				dev_info(charger->dev, "MPP25: Error during calibration, disable");
				google_wlc_disable_mpp25(charger, true);
			}
		}
	}
	if (int_fields.dploss_cal_success) {
		if (charger->mpp25.cal_active)
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_RESPONSE);
	}
	if (int_fields.dploss_param_match) {
		if (in_wlc_dc(charger) && charger->mpp25.state == MPP25_ENTER_HPM) {
			charger->dc_data.dploss_param_ok = true;
			schedule_delayed_work(&charger->mpp25_init_work, 0);
		}
	}
	if (int_fields.dploss_param_error) {
		if (charger->mpp25.state == MPP25_DPLOSS_CAL4) {
			if (charger->mpp25.cal_active) {
				dev_info(charger->dev, "MPP25: param error during Cal4, abort");
				google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
			}
		} else if (charger->mpp25.state == MPP25_ENTER_HPM) {
			dev_info(charger->dev, "MPP25: DPLOSS param error during HPM transition, disable");
			google_wlc_disable_mpp25(charger, false);
		}
	}
	if (int_fields.dploss_cal_retry) {
		mutex_lock(&charger->status_lock);
		if (charger->mpp25.cal_active &&
		    charger->mpp25.last_dploss_event == DPLOSS_CAL_ENTER) {
			if (charger->mpp25.fod_cloak == FOD_CLOAK_NONE) {
				charger->mpp25.fod_cloak = FOD_CLOAK_STARTED;
				dev_info(charger->dev, "Entering FOD cloak");
				charger->cloak_enter_reason = CLOAK_FOD;
				google_wlc_set_status(charger, GOOGLE_WLC_STATUS_CLOAK_ENTERING);
			} else if (charger->mpp25.fod_cloak == FOD_CLOAK_DONE) {
				dev_err(charger->dev, "Tried FOD cloak but still failed, disable");
				google_wlc_disable_mpp25(charger, true);
			}
		}
		mutex_unlock(&charger->status_lock);
	}
	if (int_fields.rcs) {
		u8 rcs;

		charger->chip->chip_get_tx_rcs(charger, &rcs);
		dev_info(charger->dev, "RCS IRQ, RCS val: %d", rcs);
		google_wlc_rcs_irq(charger, rcs);
	}
	if (int_fields.fsk_timeout || int_fields.fsk_received)
		charger->pkt_ready = int_fields.fsk_timeout ? -ETIME : 1;

	if (int_fields.fsk_received && charger->fsk_log) {
		const size_t max = sizeof(struct google_wlc_packet) * 3 + 1;
		char buf[sizeof(struct google_wlc_packet) * 3 + 1];

		ret = google_wlc_read_packet(charger, buf, max);
		if (ret == 0)
			dev_info(charger->dev, "fsk data: %s", buf);
	}
	if (int_fields.over_temperature && charger->irq_error_count < 0xFFFF)
		charger->irq_error_count++;
	if (int_fields.fsk_missing) {
		google_wlc_fsk_missing_check(charger);
		mutex_lock(&charger->status_lock);
		if (charger->inlim_setting == 1)
			set_inlim_enabled(charger, true);
		mutex_unlock(&charger->status_lock);
	}
	if (int_fields.cloak_error)
		schedule_delayed_work(&charger->cloak_err_work, 0);
	if (int_fields.vdd_err)
		schedule_delayed_work(&charger->vdd_err_work, 0);
}

static irqreturn_t google_wlc_irq_thread(int irq, void *irq_data)
{
	struct google_wlc_data *charger = irq_data;
	char buf[128];
	u32 int_val;
	int ret;
	struct google_wlc_bits int_fields;

	ret = charger->chip->chip_get_interrupts(charger, &int_val, &int_fields);
	if (ret < 0) {
		dev_err(charger->dev, "failed to get interrupts\n");
		return IRQ_HANDLED;
	}
	google_wlc_int_string(buf, sizeof(buf), int_fields);
	dev_info(charger->dev, "IRQ received:%08x, ints: %s", int_val, buf);
	ret = charger->chip->chip_clear_interrupts(charger, int_val);
	if (ret < 0)
		dev_err(charger->dev, "failed to clear interrupts\n");
	if (!charger->online_disable)
		google_wlc_irq_handler(charger, int_fields);
	return IRQ_HANDLED;
}

static irqreturn_t google_wlc_hybrid_irq_thread(int irq, void *irq_data)
{
	struct google_wlc_data *charger = irq_data;
	gbms_irq_t val;
	int ret;

	if (!bms_usecase_is_uc_wireless(charger->usecase))
		return IRQ_NONE;

	ret = gbms_irq_read(irq, &val);
	if (ret) {
		dev_warn(charger->dev, "receive invalid hybrid irq: %d\n", ret);
		return IRQ_NONE;
	}

	dev_dbg(charger->dev, "receive hybrid irq: %d\n", val);
	if ((val & GBMS_IRQ_VIN_UV_MASK) || (val & GBMS_IRQ_IIN_UC_MASK)) {
		ret = charger->chip->chip_set_mod_mode(charger, ASK_MOD_MODE_ADAPTIVE_LOAD_MOD);
		if (ret == 0)
			charger->ask_mod_mode = ASK_MOD_MODE_ADAPTIVE_LOAD_MOD;
		dev_warn(charger->dev, "Hybrid uc(uv) during charging, reset\n");
		google_wlc_set_dc_icl(charger, charger->output_chg_config->init_cur_ua);
		charger->icl_now = charger->output_chg_config->init_cur_ua;
		charger->charger_started = false;
		charger->charging_initialized = false;
		schedule_delayed_work(&charger->charging_init_work, WLC_INIT_CHECK_INTERVAL);
	}

	return IRQ_HANDLED;
}

static void google_wlc_stats_init(struct google_wlc_data *charger)
{
	struct google_wlc_stats *chg_data = &charger->chg_data;

	memset(chg_data, 0, sizeof(struct google_wlc_stats));
	chg_data->cur_soc = -1;
	charger->irq_load_decrease_count = 0;
}

static int wlc_stats_update_state(struct google_wlc_data *charger)
{
	struct google_wlc_stats *chg_data = &charger->chg_data;
	struct wlc_adapter_capabilities_1_fields *cap1 =
		(struct wlc_adapter_capabilities_1_fields *)&chg_data->adapter_capabilities[1];
	struct wlc_adapter_capabilities_2_fields *cap2 =
		(struct wlc_adapter_capabilities_2_fields *)&chg_data->adapter_capabilities[2];
	struct wlc_adapter_capabilities_4_fields *cap4 =
		(struct wlc_adapter_capabilities_4_fields *)&chg_data->adapter_capabilities[4];
	struct wlc_receiver_state_0_fields *rs0 =
		(struct wlc_receiver_state_0_fields *)&chg_data->receiver_state[0];
	struct wlc_receiver_state_1_fields *rs1 =
		(struct wlc_receiver_state_1_fields *)&chg_data->receiver_state[1];

	chg_data->adapter_capabilities[0] = charger->tx_id;

	cap1->qi_id = charger->qi_id;
	cap1->ptmc = charger->ptmc;

	if (charger->nego_power > cap2->nego_power)
		cap2->nego_power = charger->nego_power;

	cap2->limit_reason = charger->limit_reason;

	if (charger->mdis_level > cap2->mdis_level && charger->mdis_level <= MDIS_LEVEL_MAX)
		cap2->mdis_level = charger->mdis_level;

	chg_data->adapter_capabilities[3] = charger->feature;

	if (cap4->mpp25_state != MPP25_ACTIVE)
		cap4->mpp25_state = charger->mpp25.state;

	wlc_stats_update_compatibility(charger);
	cap4->compatibility = charger->compatibility;
	if (charger->wlc_offline_count >= 3)
		cap4->flag_fsk_fallback_15w = 1;
	if (charger->compatibility == COMPAT_FORCED_BPP)
		cap4->flag_iop_fallback_bpp = 1;

	rs0->eds_error_count = charger->eds_error_count;
	rs0->eds_total_count = charger->eds_total_count;
	rs1->irq_error_count = charger->irq_error_count;
	rs1->disconnect_total_count = charger->disconnect_total_count;

	return 0;
}

static int check_mpp25_capabilities(struct google_wlc_data *charger)
{
	struct mode_cap_data *mode_cap = &charger->mpp25.mode_capabilities;
	char str[5][64] = {"Notsupp", "Notsupp", "Notsupp", "Notsupp", "Mode capability checking"};
	int ret;
	bool support = false;

	ret = charger->chip->chip_get_mode_capabilities(charger, mode_cap);
	if (ret)
		return ret;

	if (charger->mode == RX_MODE_WPC_MPP_CPM || charger->mode == RX_MODE_MPP_CLOAK) {
		if (mode_cap->cpm.pot_pwr >= MPP25_MIN_POTENTIAL_POWER) {
			scnprintf(str[4], sizeof(str[4]), "CPM potential power OK: %d",
				  mode_cap->cpm.pot_pwr);
			support = true;
		} else if (charger->mode == RX_MODE_WPC_MPP_CPM) {
			scnprintf(str[4], sizeof(str[4]), "CPM Potential power too low: %d",
				   mode_cap->cpm.pot_pwr);
		}
	}
	if (charger->mode == RX_MODE_WPC_MPP_NPM || charger->mode == RX_MODE_MPP_CLOAK) {
		if (mode_cap->hpm.pot_pwr >= MPP25_MIN_POTENTIAL_POWER) {
			scnprintf(str[4], sizeof(str[4]), "HPM potential power OK: %d",
				  mode_cap->hpm.pot_pwr);
			support = true;
		} else if (charger->mode == RX_MODE_WPC_MPP_NPM) {
			scnprintf(str[4], sizeof(str[4]), "HPM Potential power too low: %d",
				  mode_cap->hpm.pot_pwr);
		}
	}
	if (charger->mode == RX_MODE_WPC_MPP_HPM)
		support = true;

	if (!support)
		ret = -EOPNOTSUPP;

	dev_info(charger->dev, "MPP25: %s", str[4]);
	if (mode_cap->cpm.supported)
		scnprintf(str[0], sizeof(str[0]), "%d-%d mV, %d mW",
			  mode_cap->cpm.min_volt, mode_cap->cpm.max_volt,
			  mode_cap->cpm.pot_pwr);
	if (mode_cap->lpm.supported)
		scnprintf(str[1], sizeof(str[1]), "%d-%d mV, %d mW",
			  mode_cap->lpm.min_volt, mode_cap->lpm.max_volt,
			  mode_cap->lpm.pot_pwr);
	if (mode_cap->npm.supported)
		scnprintf(str[2], sizeof(str[2]), "%d-%d mV, %d mW",
			  mode_cap->npm.min_volt, mode_cap->npm.max_volt,
			  mode_cap->npm.pot_pwr);
	if (mode_cap->hpm.supported)
		scnprintf(str[3], sizeof(str[3]), "%d-%d mV, %d mW",
			  mode_cap->hpm.min_volt, mode_cap->hpm.max_volt,
			  mode_cap->hpm.pot_pwr);
	dev_info(charger->dev, "MPP25: CPM: %s LPM: %s NPM: %s HPM: %s",
		 str[0], str[1], str[2], str[3]);

	return ret;
}

static int google_wlc_update_adapter_type(struct google_wlc_data *charger)
{
	u8 sys_mode, val = 0, type;
	bool mpp25w = false;
	int ret;

	ret = charger->chip->chip_get_sys_mode(charger, &sys_mode);
	if (ret != 0 || sys_mode <= 0)
		return -ENODEV;

	if (sys_mode == RX_MODE_WPC_MPP_HPM || check_mpp25_capabilities(charger) == 0)
		mpp25w = true;

	charger->chip->chip_get_tx_qi_ver(charger, &val);
	mpp25w = mpp25w && val >= 0x22;

	if (mode_is_epp(sys_mode) || charger->wait_for_dd)
		charger->chg_data.adapter_type = AD_TYPE_WPC_EPP;
	else if (sys_mode == RX_MODE_WPC_BPP)
		charger->chg_data.adapter_type = AD_TYPE_WPC_BPP;
	else if (mpp25w)
		charger->chg_data.adapter_type = AD_TYPE_WPC_MPP25;
	else if (mode_is_mpp(sys_mode) || sys_mode == RX_MODE_WPC_MPP_RESTRICTED ||
					 sys_mode == RX_MODE_MPP_CLOAK)
		charger->chg_data.adapter_type = AD_TYPE_WPC_MPP;
	else
		charger->chg_data.adapter_type = AD_TYPE_WLC;

	if (charger->chg_data.adapter_type != AD_TYPE_WPC_BPP &&
	    charger->chg_data.adapter_type != AD_TYPE_WPC_EPP)
		return 0;

	type = (charger->tx_id & TXID_TYPE_MASK) >> TXID_TYPE_SHIFT;

	if (type == TXID_DD_TYPE || type == TXID_DD_TYPE2)
		charger->chg_data.adapter_type = type;

	return 0;
}

static void wlc_update_head_stats(struct google_wlc_data *charger)
{
	u32 val;
	int ret;

	if (google_wlc_update_adapter_type(charger))
		return;

	ret = charger->chip->chip_get_opfreq(charger, &val);
	if (ret != 0)
		charger->chg_data.of_freq = -1;
	else
		charger->chg_data.of_freq = val;

	ret = charger->chip->chip_get_iout(charger, &val);
	if (ret != 0)
		charger->chg_data.cur_conf = -1;
	else if (val > charger->chg_data.cur_conf)
		charger->chg_data.cur_conf = val;

	ret = charger->chip->chip_get_vout_set(charger, &val);
	if (ret != 0)
		charger->chg_data.volt_conf = -1;
	else if (val > charger->chg_data.volt_conf)
		charger->chg_data.volt_conf = val;
}

static void wlc_update_soc_stats(struct google_wlc_data *charger,
				   int cur_soc)
{
	const ktime_t now = get_boot_sec();
	struct wlc_soc_data *soc_data;
	int vrect_mv, iout_ma, vout_mv, wlc_freq, temp;
	u32 val, cur_pout;
	int ret, interval_time = 0;
	u8 sys_mode;

	ret = charger->chip->chip_get_sys_mode(charger, &sys_mode);
	if (ret != 0)
		return;

	ret = charger->chip->chip_get_opfreq(charger, &val);
	if (ret == 0)
		wlc_freq = val;
	else
		wlc_freq = -1;

	ret = charger->chip->chip_get_temp(charger, &val);
	if (ret == 0)
		temp = MILLIC_TO_DECIC(val);
	else
		temp = -1;

	ret = charger->chip->chip_get_vrect(charger, &val);
	if (ret == 0)
		vrect_mv = val;
	else
		vrect_mv = -1;

	ret = charger->chip->chip_get_iout(charger, &val);
	if (ret == 0)
		iout_ma = val;
	else
		iout_ma = -1;

	ret = charger->chip->chip_get_vout_set(charger, &val);
	if (ret == 0)
		vout_mv = val;
	else
		vout_mv = -1;

	soc_data = &charger->chg_data.soc_data[cur_soc];

	if (vout_mv > charger->chg_data.volt_conf)
		charger->chg_data.volt_conf = vout_mv;

	if (iout_ma > charger->chg_data.cur_conf)
		charger->chg_data.cur_conf = iout_ma;

	if (vrect_mv > soc_data->vrect)
		soc_data->vrect = vrect_mv;

	if (iout_ma > soc_data->iout)
		soc_data->iout = iout_ma;

	soc_data->sys_mode = sys_mode;
	soc_data->die_temp = temp;
	soc_data->of_freq = wlc_freq;
	soc_data->alignment = 100;

	if (vrect_mv > 0 && iout_ma > 0)
		cur_pout = vrect_mv * iout_ma;
	else
		cur_pout = 0;
	if ((soc_data->pout_min == 0) || (soc_data->pout_min > charger->nego_power))
		soc_data->pout_min = charger->nego_power;
	if ((soc_data->pout_max == 0) || (soc_data->pout_max < charger->nego_power))
		soc_data->pout_max = charger->nego_power;

	if (soc_data->last_update != 0)
		interval_time = now - soc_data->last_update;

	soc_data->elapsed_time += interval_time;
	soc_data->pout_sum += cur_pout * interval_time;
	soc_data->last_update = now;
}

static int wlc_chg_data_head_dump(char *buff, int max_size,
				  const struct google_wlc_stats *chg_data)
{
	return scnprintf(buff, max_size, "A:%d,%d,%d,%d,%d",
			 chg_data->adapter_type, chg_data->cur_soc,
			 chg_data->volt_conf, chg_data->cur_conf,
			 chg_data->of_freq);
}

static int wlc_adapter_capabilities_dump(char *buff, int max_size,
					   const struct google_wlc_stats *chg_data)
{
	return scnprintf(buff, max_size, "D:%x,%x,%x,%x,%x, %x,%x",
			 chg_data->adapter_capabilities[0],
			 chg_data->adapter_capabilities[1],
			 chg_data->adapter_capabilities[2],
			 chg_data->adapter_capabilities[3],
			 chg_data->adapter_capabilities[4],
			 chg_data->receiver_state[0],
			 chg_data->receiver_state[1]);
}

static int wlc_soc_data_dump(char *buff, int max_size,
			       const struct google_wlc_stats *chg_data,
			       int index)
{
	return scnprintf(buff, max_size, "%d:%d, %d,%ld,%d, %d,%d, %d,%d,%d,%d",
			 index,
			 chg_data->soc_data[index].elapsed_time,
			 chg_data->soc_data[index].pout_min,
			 chg_data->soc_data[index].pout_sum /
			 chg_data->soc_data[index].elapsed_time / 1000,
			 chg_data->soc_data[index].pout_max,
			 chg_data->soc_data[index].of_freq,
			 chg_data->soc_data[index].alignment,
			 chg_data->soc_data[index].vrect,
			 chg_data->soc_data[index].iout,
			 chg_data->soc_data[index].die_temp,
			 chg_data->soc_data[index].sys_mode);
}

/* needs mutex_lock(&charger->stats_lock); */
static void wlc_dump_charge_stats(struct google_wlc_data *charger)
{
	char buff[128];
	int i = 0;

	if (charger->chg_data.cur_soc < 0)
		return;

	/* Dump the head */
	wlc_chg_data_head_dump(buff, sizeof(buff), &charger->chg_data);
	logbuffer_log(charger->log, "%s", buff);

	/* Dump the adapter capabilities */
	wlc_adapter_capabilities_dump(buff, sizeof(buff), &charger->chg_data);
	logbuffer_log(charger->log, "%s", buff);


	for (i = 0; i < WLC_SOC_STATS_LEN; i++) {
		if (charger->chg_data.soc_data[i].elapsed_time == 0)
			continue;
		wlc_soc_data_dump(buff, sizeof(buff), &charger->chg_data, i);
		logbuffer_log(charger->log, "%s", buff);
	}
}

#define WLC_CHARGE_STATS_DELAY_SEC 10
static void google_wlc_charge_stats_hda_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, charge_stats_hda_work.work);
	struct google_wlc_stats *chg_data = &charger->chg_data;
	const ktime_t now = get_boot_sec();
	const ktime_t start_time = chg_data->start_time;
	const ktime_t elap = now - chg_data->start_time;

	mutex_lock(&charger->stats_lock);

	if (charger->online == 0 ||
	    charger->last_capacity < 0 ||
	    charger->last_capacity > 100 ||
	    charger->shutdown_notify)
		goto unlock_done;

	/* Charge_stats buffer is Empty */
	if (chg_data->cur_soc < 0) {
		/* update the head */
		chg_data->cur_soc = charger->last_capacity;
		chg_data->last_soc = charger->last_capacity;
		wlc_update_head_stats(charger);
		chg_data->start_time = get_boot_sec();
	} else if (start_time && elap > WLC_CHARGE_STATS_DELAY_SEC) {
		/*
		 * Voltage, Current and sys_mode are not correct
		 * on wlc start. Debounce by 10 seconds.
		 */
		wlc_update_head_stats(charger);
		chg_data->start_time = 0;
	}

	wlc_stats_update_state(charger);

	/* FIXME; vote for different Tx charger type */
	/*
	 * if (!charger->hda_tz_votable)
	 *     charger->hda_tz_votable = gvotable_election_get_handle(VOTABLE_HDA_TZ);
	 */

	/* SOC changed, store data to the last one. */
	if (chg_data->last_soc != charger->last_capacity)
		wlc_update_soc_stats(charger, chg_data->last_soc);
	/* update currect_soc data */
	wlc_update_soc_stats(charger, charger->last_capacity);

	chg_data->last_soc = charger->last_capacity;

	schedule_delayed_work(&charger->charge_stats_hda_work,
			      msecs_to_jiffies(WLC_CHARGE_STATS_TIMEOUT_MS));

unlock_done:
	mutex_unlock(&charger->stats_lock);
}

static int wait_for_dploss_stable(struct google_wlc_data *charger)
{
	u32 iout, vout, vrect, prect, target, target_volts, count = 0;
	int ret;
	int retries = MPP25_DPLOSS_STABLE_CHECK_RETRIES;
	int dploss_thresh = charger->dploss_thresh ? charger->dploss_thresh :
			    MPP25_DPLOSS_STABLE_CHECK_THRESHOLD;
	const int step = charger->mpp25.dploss_step;

	if (charger->mpp25.state == MPP25_DPLOSS_CAL4 && step == 4) {
		target = charger->mpp25_dploss_cal4;
		target_volts = charger->mpp25_dploss_cal4_volt;
	} else if (charger->mpp25.state == MPP25_DPLOSS_CALIBRATION &&
		step >= 1 && step <= MPP_DPLOSS_NUM_STEPS) {
		target = charger->pdata->dploss_steps[step - 1];
		target_volts = charger->pdata->dploss_volts[step - 1];
	} else {
		dev_err(charger->dev, "DPLOSS wait: step invalid %d for state %d",
			step, charger->mpp25.state);
		return -EINVAL;
	}
	google_wlc_wait_for_voltage(charger, target_volts);

	for (int i = 0; i < retries; i++) {
		ret = 0;
		ret |= charger->chip->chip_get_vout(charger, &vout);
		ret |= charger->chip->chip_get_vrect(charger, &vrect);
		ret |= charger->chip->chip_get_iout(charger, &iout);
		if (ret != 0)
			return ret;
		prect = vout * iout / 1000;

		if (charger->status != GOOGLE_WLC_STATUS_CHARGING || charger->mpp25_disabled ||
		    (charger->mpp25.state != MPP25_DPLOSS_CALIBRATION &&
		     charger->mpp25.state != MPP25_DPLOSS_CAL4))
			return -EOPNOTSUPP;
		if (prect > target - dploss_thresh &&
		    prect < target + dploss_thresh)
			count++;
		else
			count = 0;
		if (count > MPP25_DPLOSS_STABLE_CHECK_GOAL) {
			dev_info(charger->dev,
				 "Wait done, vout = %u, vrect = %u, iout = %u, prect = %u",
				 vout, vrect, iout, prect);
			return 0;
		}
		dev_dbg(charger->dev,
			"Waiting for dploss stable %d. vout = %u, vrect = %u, iout = %u, prect = %u, stable count = %u, target = %u",
			i, vout, vrect, iout, prect, count, target);
		msleep(MPP25_DPLOSS_STABLE_CHECK_INTERVAL);
	}

	dev_info(charger->dev, "Dploss target not stable, checking fallback range for step %d.",
		step);

	if (charger->mpp25.state == MPP25_DPLOSS_CALIBRATION) {
		const int dploss_fallback_min_mw_steps[MPP_DPLOSS_NUM_STEPS] = {
			MPP25_DPLOSS_FALLBACK_MIN_STEP1,
			MPP25_DPLOSS_FALLBACK_MIN_STEP2,
			MPP25_DPLOSS_FALLBACK_MIN_STEP3,
		};
		const int dploss_fallback_max_mw_steps[MPP_DPLOSS_NUM_STEPS] = {
			MPP25_DPLOSS_FALLBACK_MAX_STEP1,
			MPP25_DPLOSS_FALLBACK_MAX_STEP2,
			MPP25_DPLOSS_FALLBACK_MAX_STEP3,
		};

		if (prect >= dploss_fallback_min_mw_steps[step - 1] &&
			prect < dploss_fallback_max_mw_steps[step - 1]) {
			dev_info(charger->dev, "prect (%u mW) is within fallback range for step %d. Proceeding.",
						prect, step);
			return 0;
		}
	} else { /* State must be MPP25_DPLOSS_CAL4 due to checks at function start */
		if (prect >= MPP25_DPLOSS_FALLBACK_MIN_STEP4 &&
			prect <= MPP25_DPLOSS_FALLBACK_MAX_STEP4) {
			dev_info(charger->dev, "prect (%u mW) is within fallback range for step %d. Proceeding.",
						prect, step);
			return 0;
		}
	}
	dev_err(charger->dev,
			"prect (%u mW) is out of fallback range for step %d, fail.",
			prect, step);
	return -EINVAL;
}

static void google_wlc_wait_for_charger_on(struct google_wlc_data *charger)
{
	u32 iout;
	int ret;
	int thresh = WLC_IOUT_VALID_LEVEL_MA;
	int retries = WLC_CHARGER_STARTUP_RETRIES;
	struct wlc_chg_config *config = charger->output_chg_config;

	for (int i = 0; i < retries; i++) {
		ret = charger->chip->chip_get_iout(charger, &iout);

		if (ret != 0) {
			dev_err(charger->dev, "Unable to read iout");
			return;
		}

		if (!charger->wlc_charge_enabled && !in_wlc_dc(charger)) {
			dev_err(charger->dev, "Charging not enabled");
			return;
		}

		if (iout > thresh) {
			dev_info(charger->dev, "IOUT wait done, iout = %u", iout);
			goto check_vout;
		}
		dev_dbg(charger->dev,
			"Waiting for charger startup %d. iout = %u", i, iout);
		msleep(WLC_CHARGER_STARTUP_CHECK_INTERVAL);
	}
	dev_err(charger->dev, "Charger not startup after %d checks", retries);
	return;

check_vout:
	if (config->mod_change_timing == ASK_MOD_TIMING_LATE) {
		ret = charger->chip->chip_set_mod_mode(charger, config->mod_mode);
		if (ret == 0)
			charger->ask_mod_mode = charger->output_chg_config->mod_mode;
	}

	if (config->charger_type == CHARGER_TYPE_BUCK && !config->auto_vout) {
		charger->chip->chip_set_vout(charger, UV_TO_MV(config->init_volt_uv));
		ret = google_wlc_wait_for_voltage(charger, UV_TO_MV(config->init_volt_uv));
	}
	if (ret == 0)
		charger->charger_started = true;
}

static void google_wlc_charging_init_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
		struct google_wlc_data, charging_init_work.work);
	struct wlc_chg_config *config = charger->output_chg_config;
	int ret;

restart:
	mutex_lock(&charger->status_lock);

	if (!google_wlc_is_present(charger) ||
	    charger->charging_initialized ||
	    charger->shutdown_notify)
		goto exit_unlock;

	if (!charger->wlc_charge_enabled && !in_wlc_dc(charger)) {
		dev_info(charger->dev, "Charging not enabled");
		goto exit_unlock;
	}

	if (charger->status != GOOGLE_WLC_STATUS_CHARGING &&
		charger->status != GOOGLE_WLC_STATUS_DC_CHARGING) {
		dev_info(charger->dev, "Reschedule charging_init");
		schedule_delayed_work(&charger->charging_init_work, WLC_INIT_CHECK_INTERVAL);
		goto exit_reschedule;
	}

	if (mpp25_ready(charger->mpp25.state) && is_charger_mpp25_ok(config)) {
		if (!charger->mpp25.cal_enter_ok) {
			dev_info(charger->dev, "cal enter was failed");
			google_wlc_disable_mpp25(charger, true);
			goto exit_unlock;
		}
		/* for SWC charger, charger starts up as part of dploss flow */
		if (config->charger_type == CHARGER_TYPE_SWC &&
		    (charger->mpp25.state == MPP25_READY || charger->mpp25.back_npm)) {
			__pm_stay_awake(charger->mpp25_ws);
			schedule_delayed_work(&charger->mpp25_init_work, 0);
		}
	}
	/* wait for charger init if it's not the default charger */
	if (!charger->charger_started && charger->chg_config_index != 0) {
		mutex_unlock(&charger->status_lock);
		if (config->charger_type == CHARGER_TYPE_SWC && !is_charger_mpp25_ok(config)) {
			ret = GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_ENABLE_SWITCH_CAP,
					    CP_ENABLE_SWITCH_CAP);
			if (ret != 0)
				dev_err(charger->dev, "Error enabling swc");
			else
				charger->dc_data.swc_en_state = SWC_ENABLED;
		}
		google_wlc_wait_for_charger_on(charger);
		if (!charger->charger_started || ret != 0) {
			dev_info(charger->dev, "Reschedule charging_init");
			schedule_delayed_work(&charger->charging_init_work,
					      WLC_INIT_CHECK_INTERVAL);
			goto exit_reschedule;
		}
		goto restart;
	}
	/* for buck charger, wait til charger starts before starting calibration */
	if (mpp25_ready(charger->mpp25.state) && is_charger_mpp25_ok(config) &&
	    config->charger_type == CHARGER_TYPE_BUCK) {
		__pm_stay_awake(charger->mpp25_ws);
		schedule_delayed_work(&charger->mpp25_init_work, 0);
	}

	gvotable_cast_long_vote(charger->icl_ramp_target_votable, CHG_ENABLE_VOTER, 0, false);

	if (charger->status == GOOGLE_WLC_STATUS_CHARGING)
		google_wlc_trigger_icl_by_mode(charger, charger->mode);
	charger->charging_initialized = true;
	dev_info(charger->dev, "Charging initialization finished");
	/* For DC charging, trigger chg stable work immediately since no complete signal */
	if (charger->status == GOOGLE_WLC_STATUS_DC_CHARGING)
		google_wlc_handle_chg_stable(charger, CHG_STABLE_TYPE_DC_CHARGING,
					     GOOGLE_WLC_DC_CHG_STABLE_TIME_MS);
exit_unlock:
	__pm_relax(charger->init_charging_ws);
exit_reschedule:
	mutex_unlock(&charger->status_lock);
}

/**
 * google_wlc_handle_mpp25_cal4 - Handles the 4th DP-Loss calibration.
 * @charger: Pointer to the main google_wlc_data struct.
 *
 * This helper function encapsulates the state machine for the 4th DP-Loss
 * calibration point. It checks if the calibration should run, initiates it,
 * or processes the next step if it's already in progress.
 *
 * Return: true if the calibration logic was handled and the calling work
 * function should exit, false otherwise.
 */
static bool google_wlc_handle_mpp25_cal4(struct google_wlc_data *charger)
{
	struct wlc_chg_config *config = charger->output_chg_config;

	if ((config->charger_type == CHARGER_TYPE_BUCK && charger->mpp25_dploss_cal4_volt == 0) ||
		charger->mpp25_dploss_cal4 == 0 || charger->mpp25.cal4_ok ||
		!charger->mpp25.dploss_cal_ok)
		return false;

	if (charger->mpp25.state != MPP25_DPLOSS_CAL4) {
		if (charger->mpp25_dploss_cal4 <= charger->nego_power) {
			charger->mpp25.cal_active = true;
			dev_info(charger->dev,
				"MPP25 4th Cal: Initiating. Target Power: %dmW, Target Voltage: %dmV",
				charger->mpp25_dploss_cal4,
				charger->mpp25_dploss_cal4_volt);
			charger->mpp25.state = MPP25_DPLOSS_CAL4;
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_START);
			schedule_delayed_work(&charger->mpp25_timeout_work,
				msecs_to_jiffies(MPP25_CALIBRATION_TIMEOUT_MS));
		} else {
			dev_info(charger->dev, "WLC-DC: Nego power too low for 4th cal, skip");
			charger->mpp25.state = MPP25_DPLOSS_CAL4;
			/*
			 * The abort handler will schedule mpp25_init_work
			 * and prevent retries.
			 */
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
		}
	} else if (charger->mpp25.last_dploss_event == DPLOSS_CAL_START &&
		config->charger_type == CHARGER_TYPE_BUCK) {
		int ret;

		/* Release lock while waiting */
		dev_info(charger->dev, "MPP25 4th Cal: Waiting for power to stabilize for buck charger");
		mutex_unlock(&charger->status_lock);
		ret = wait_for_dploss_stable(charger);
		mutex_lock(&charger->status_lock);
		dev_info(charger->dev, "MPP25 4th Cal: Power stabilization check finished, ret=%d",
			ret);
		if (ret == -EOPNOTSUPP) {
			dev_err(charger->dev, "MPP25 4th Cal: No longer in cal, stop waiting");
		} else if (ret < 0) {
			if (charger->charging_initialized) {
				dev_err(charger->dev, "MPP25 4th Cal: Error waiting for dploss stable, abort");
				google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
			}
		} else {
			// The DPLOSS_CAL_EXTEND response handler will trigger capture
			dev_info(charger->dev, "MPP25 4th Cal: Power stable. Sending DPLOSS_CAL_EXTEND");
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_EXTEND);
		}
	} else {
		/*
		 * In 4th cal state, but calibration is in an
		 * intermediate async step. Exit and wait for
		 * the next event to drive the state machine.
		 */
		dev_dbg(charger->dev, "MPP25 4th Cal: In async step, no action needed");
	}
	return true;
}

static void google_wlc_mpp25_init_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, mpp25_init_work.work);
	int ret = 0;
	int pwrmode = mpp_get_current_powermode(charger);
	struct wlc_chg_config *config = charger->output_chg_config;

	cancel_delayed_work(&charger->mpp25_timeout_work);

	mutex_lock(&charger->status_lock);
	if (!charger->mpp25.cal_enter_ok || !charger->pdata->support_mpp25 ||
	    !is_charger_mpp25_ok(config) || charger->mpp25_disabled ||
	    !mpp25_ready(charger->mpp25.state) || !google_wlc_is_present(charger) ||
	    charger->shutdown_notify) {
		dev_info(charger->dev, "Not valid state for MPP25, disable");
		goto error;
	}
	if (!charger->mpp25.pwrmode_ok && pwrmode != MPP_POWERMODE_CONTINUOUS &&
	    pwrmode != MPP_POWERMODE_HIGH && !charger->wlc_dc_skip_powermode &&
	    config->charger_type == CHARGER_TYPE_BUCK) {
		dev_info(charger->dev, "MPP25: Invalid powermode: %s, disable",
				mpp_powermode_str[pwrmode]);
		goto error;
	}
	if (config->charger_type != CHARGER_TYPE_SWC)
		charger->mpp25.pwrmode_ok = true;

	if (charger->mpp25.dploss_cal_ok || charger->wlc_dc_skip_dploss) {
		goto swc_powermode;
	}
	if (charger->mpp25.state != MPP25_DPLOSS_CALIBRATION) {
		schedule_delayed_work(&charger->mpp25_timeout_work,
				msecs_to_jiffies(MPP25_CALIBRATION_TIMEOUT_MS));
		charger->mpp25.state = MPP25_DPLOSS_CALIBRATION;
		google_wlc_do_dploss_event(charger, DPLOSS_CAL_START);
		goto exit_unlock;
	} else if (charger->mpp25.last_dploss_event == DPLOSS_CAL_START &&
		config->charger_type == CHARGER_TYPE_BUCK) {
		/* Release lock while waiting */
		mutex_unlock(&charger->status_lock);
		ret = wait_for_dploss_stable(charger);
		mutex_lock(&charger->status_lock);
		if (ret == -EOPNOTSUPP) {
			dev_err(charger->dev, "MPP25: No longer in cal, stop waiting");
			goto exit_unlock;
		} else if (ret < 0) {
			if (charger->charging_initialized) {
				dev_err(charger->dev, "MPP25: Error waiting for dploss stable");
				google_wlc_disable_mpp25(charger, true);
			}
			goto exit_unlock;
		}
		google_wlc_do_dploss_event(charger, DPLOSS_CAL_CAPTURE);
		goto exit_unlock;
	} else if (charger->mpp25.last_dploss_event == DPLOSS_CAL_CAPTURE &&
		   charger->mpp25.dploss_event_success &&
		   config->charger_type == CHARGER_TYPE_BUCK) {
		mutex_unlock(&charger->status_lock);
		ret = wait_for_dploss_stable(charger);
		mutex_lock(&charger->status_lock);
		if (ret < 0) {
			if (charger->charging_initialized) {
				dev_err(charger->dev, "MPP25: Power didn't stablilize at cal point");
				google_wlc_disable_mpp25(charger, true);
			}
			goto exit_unlock;
		}
		google_wlc_do_dploss_event(charger, DPLOSS_CAL_CAPTURE);
		goto exit_unlock;
	} else if (charger->mpp25.last_dploss_event == DPLOSS_CAL_COMMIT &&
		   charger->mpp25.dploss_event_success) {
		/* Release lock while waiting */
		mutex_unlock(&charger->status_lock);
		ret = wait_for_completion_interruptible_timeout(&charger->cal_renego_done,
			msecs_to_jiffies(MPP25_CAL_RENEGO_TIMEOUT));
		mutex_lock(&charger->status_lock);
		charger->wait_for_cal_renego = false;
		if (ret == 0) {
			dev_err(charger->dev, "Error! cal renego timeout\n");
			google_wlc_disable_mpp25(charger, true);
			goto exit_unlock;
		}
		if (ret < 0) {
			dev_err(charger->dev, "Error! cal renego completion cannot wait\n");
			google_wlc_disable_mpp25(charger, true);
			goto exit_unlock;
		}
		google_wlc_do_dploss_event(charger, DPLOSS_CAL_EXIT);
		goto exit_unlock;
	} else {
		dev_err(charger->dev, "Already in calibration");
		goto exit_unlock;
	}
swc_powermode:
	if (config->charger_type == CHARGER_TYPE_BUCK)
		goto dploss_4thcal_check;

	if (charger->mpp25.state == MPP25_ACTIVE && charger->mpp25.back_npm &&
	    !charger->mpp25.pwrmode_ok) {
		if (pwrmode != MPP_POWERMODE_NOMINAL)
			goto exit_unlock;
		charger->mpp25.pwrmode_ok = true;
		goto restart_swc;
	} else if (charger->mpp25.pwrmode_ok || pwrmode == MPP_POWERMODE_CONTINUOUS ||
	    pwrmode == MPP_POWERMODE_HIGH || charger->wlc_dc_skip_powermode) {
		charger->mpp25.pwrmode_ok = true;
		goto dploss_param;
	} else if (pwrmode == MPP_POWERMODE_NOMINAL) {
		if (!charger->dc_data.dploss_param_init_ok) {
			dev_info(charger->dev, "WLC-DC: NPM, wait 4 seconds");
			schedule_delayed_work(&charger->mpp25_init_work,
					msecs_to_jiffies(WLC_DC_DPLOSS_PARAM_INIT_DELAY));
			charger->dc_data.dploss_param_init_ok = true;
			goto exit_unlock;
		}
		dev_info(charger->dev, "WLC-DC: NPM, requesting switch to HPM");
		charger->mpp25.state = MPP25_ENTER_HPM;
		google_wlc_switch_powermode(charger, MPP_POWERMODE_HIGH);
		schedule_delayed_work(&charger->mpp25_init_work, 0);
		goto exit_unlock;
	} else {
		dev_info(charger->dev, "WLC-DC: Invalid powermode: %s, disable",
				mpp_powermode_str[pwrmode]);
		goto error;
	}
dploss_param:
	if (!charger->dc_data.dploss_param_ok && !charger->wlc_dc_skip_dploss_param &&
	    pwrmode != MPP_POWERMODE_CONTINUOUS) {
		dev_info(charger->dev, "WLC-DC: Wait for DPLOSS param");
		schedule_delayed_work(&charger->mpp25_timeout_work,
			msecs_to_jiffies(MPP25_PM_SWITCH_TIMEOUT_MS));
		goto exit_unlock;
	}
	charger->dc_data.dploss_param_ok = true;
	if (charger->mpp25.state != MPP25_ACTIVE)
		charger->wlc_dc_to_ramp = true;

	gvotable_cast_long_vote(charger->wlc_dc_power_votable, MODE_VOTER,
		GOOGLE_WLC_MPP25_MAX_POWER, true);
dploss_4thcal_check:
	if (google_wlc_handle_mpp25_cal4(charger))
		goto exit_unlock;
	if (config->charger_type == CHARGER_TYPE_BUCK)
		goto finish;
	ret = GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_MPP_DPLOSS_CALIBRATION_LIMIT, 0);
restart_swc:
	charger->dc_data.max_voltage_limit = 0;
	ret |= GPSY_SET_PROP(charger->wlc_dc_psy, GBMS_PROP_ENABLE_SWITCH_CAP,
			     CP_ENABLE_SWITCH_CAP);
	if (ret != 0) {
		dev_info(charger->dev, "WLC-DC: Fail to turn on SWC, disable");
		goto error;
	}
	charger->dc_data.swc_en_state = SWC_ENABLED;

finish:
	if (config->charger_type == CHARGER_TYPE_BUCK)
		charger->chip->chip_set_vout(charger, MPP25_HPM_MV);
	charger->mpp25.state = MPP25_ACTIVE;
	dev_info(charger->dev, "MPP25 initialization finished");
	mutex_unlock(&charger->status_lock);
	__pm_relax(charger->mpp25_ws);
	return;
exit_unlock:
	dev_dbg(charger->dev,
		"MPP25 init: state: %s, cal_ok: %d, pwrmode: %s, pwrmode_ok: %d, disabled: %d",
		mpp25_state_str[charger->mpp25.state],
		charger->mpp25.dploss_cal_ok,
		mpp_powermode_str[pwrmode],
		charger->mpp25.pwrmode_ok,
		charger->mpp25_disabled);
	mutex_unlock(&charger->status_lock);
	return;
error:
	if (!charger->mpp25_disabled)
		google_wlc_disable_mpp25(charger, true);
	charger->mpp25.dploss_step = 0;
	google_wlc_dploss_cal_ramp(charger);
	google_wlc_exit_mpp25(charger);
	__pm_relax(charger->mpp25_ws);
	mutex_unlock(&charger->status_lock);
}

static void google_mpp25_timeout_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, mpp25_timeout_work.work);

	mutex_lock(&charger->status_lock);
	if (charger->mpp25.cal_active) {
		if (charger->mpp25.state == MPP25_DPLOSS_CALIBRATION) {
			dev_err(charger->dev, "MPP25: Timeout during DPLOSS Cal");
			google_wlc_disable_mpp25(charger, true);
		} else if (charger->mpp25.state == MPP25_DPLOSS_CAL4) {
			dev_err(charger->dev, "MPP25: Timeout during 4th DPLOSS Cal, abort");
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
		} else {
			dev_err(charger->dev, "Unknown state while cal active %d",
				charger->mpp25.state);
		}
	} else if (charger->mpp25.state == MPP25_ENTER_HPM && !charger->dc_data.dploss_param_ok &&
		   !charger->wlc_dc_skip_dploss_param) {
		dev_err(charger->dev, "MPP25: Timeout getting dploss param, disable");
		google_wlc_disable_mpp25(charger, false);
	} else {
		dev_err(charger->dev, "Bug? Unknown timeout");
	}
	mutex_unlock(&charger->status_lock);
}

static void google_wlc_fw_update_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, wlc_fw_update_work.work);
	bool defer = false;

	if (charger->fw_data.update_option == FWUPDATE_CRC_NOTSUPPORT ||
	    charger->shutdown_notify)
		return;
	if (charger->online)
		defer = true;
	if (google_wlc_is_present(charger))
		defer = true;
	if (IS_ERR_OR_NULL(charger->pdata->vdden_gpio) && charger->usecase != GSU_MODE_STANDBY)
		defer = true;
	if (charger->last_capacity < WLC_FWUPDATE_SOC_THRESHOLD &&
	    charger->fw_data.update_option != FWUPDATE_FORCE_MANUAL)
		defer = true;
	if (defer) {
		dev_info(charger->dev, "defer wlc fw update, online: %d, present: %d, soc: %d",
			 charger->online, google_wlc_is_present(charger), charger->last_capacity);
		charger->fw_data.update_check_pending = true;
		return;
	}
	charger->fw_data.update_check_pending = false;
	if (charger->fw_data.update_option == FWUPDATE_FORCE_MANUAL)
		goto fw_check;
	if (charger->fw_data.attempts == 0 && charger->fw_data.cur_ver.crc == 0)
		goto fw_check;
	if (charger->fw_data.update_option == FWUPDATE_DISABLE)
		return;
	if (charger->fw_data.update_done)
		return;
	if (!charger->fw_data.update_support)
		return;
	if (charger->fw_data.attempts > WLC_FWUPDATE_RETRIES_MAX)
		return;

fw_check:
	dev_info(charger->dev, "wlc_fw_update check: online=%d,option=%d,done=%d,support=%d,soc=%d",
		charger->online, charger->fw_data.update_option,
		charger->fw_data.update_done, charger->fw_data.update_support,
		charger->last_capacity);

	if (!IS_ERR_OR_NULL(charger->pdata->vdden_gpio)) {
		mutex_lock(&charger->status_lock);
		/* double check online again inside status_lock */
		if (charger->online || google_wlc_is_present(charger)) {
			dev_info(charger->dev, "defer wlc fw update, online: %d present: %d",
				 charger->online, google_wlc_is_present(charger));
			mutex_unlock(&charger->status_lock);
			charger->fw_data.update_check_pending = true;
			return;
		}
		__pm_stay_awake(charger->fwupdate_ws);
		charger->online_disable = true;
		google_wlc_external_boost_en(charger, true);
		google_wlc_set_vdden(charger, 1);
		google_wlc_run_fwupdate(charger);
		google_wlc_set_vdden(charger, 0);
		google_wlc_external_boost_en(charger, false);
		charger->online_disable = false;
		__pm_relax(charger->fwupdate_ws);
		mutex_unlock(&charger->status_lock);
		return;
	}
	if (!charger->chg_mode_votable)
		charger->chg_mode_votable = gvotable_election_get_handle(GBMS_MODE_VOTABLE);
	if (!charger->chg_mode_votable) {
		logbuffer_devlog(charger->fw_log, charger->dev, "cannot find chg_mode_votable");
		return;
	}
	__pm_stay_awake(charger->fwupdate_ws);
	charger->online_disable = true;
	gvotable_cast_int_vote(charger->chg_mode_votable, WLCFW_VOTER,
			       _bms_usecase_meta_async_set(GBMS_CHGR_MODE_WLC_FWUPDATE, 1),
			       true);
}

static void google_pla_ack_timeout_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, pla_ack_timeout_work.work);

	if (charger->wlc_charge_enabled && !charger->fast_mpp_ramp &&
	    mode_is_mpp(charger->mode) && charger->status == GOOGLE_WLC_STATUS_CHARGING) {
		dev_err(charger->dev, "No PLA ACK received, set MPP Restricted");
		google_wlc_set_mode_gpio(charger, RX_MODE_WPC_MPP_RESTRICTED);
	}
}

static void google_check_iop_timeout_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, check_iop_timeout_work.work);

	if (charger->online)
		return;

	dev_info(charger->dev, "Recover IOP settings");

	if (charger->pdata->support_epp) {
		if (!charger->lower_qi_version)
			gpiod_set_value_cansleep(charger->pdata->qi_version_gpio, 1);
	} else {
		if (!charger->manual_force_bpp)
			google_wlc_set_mode_gpio(charger, RX_MODE_WPC_MPP);
	}
}

static void google_wlc_chg_stable_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, chg_stable_work.work);

	mutex_lock(&charger->status_lock);
	if (charger->status == GOOGLE_WLC_STATUS_CHARGING ||
	    charger->status == GOOGLE_WLC_STATUS_DC_CHARGING) {
		dev_info(charger->dev, "charging stabilized");
		charger->disconnect_total_count = 0;
	}
	charger->chg_stable_type = CHG_STABLE_TYPE_NONE;
	__pm_relax(charger->chg_stable_ws);
	mutex_unlock(&charger->status_lock);
}

static void google_limit_reason_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, limit_reason_work.work);
	u8 limit_reason;
	int ret;

	if (charger->shutdown_notify)
		return;

	dev_info(charger->dev, "limit reason retry work");
	ret = charger->chip->chip_get_limit_rsn(charger, &limit_reason);
	if ((ret || limit_reason == 0) &&
	    charger->limit_reason_retry_count < LIMIT_REASON_RETRY_COUNT) {
		schedule_delayed_work(&charger->limit_reason_work,
				      msecs_to_jiffies(LIMIT_REASON_RETRY_TIME_MS));
		charger->limit_reason_retry_count++;
	} else {
		if (ret == 0 && charger->limit_reason != limit_reason) {
			charger->limit_reason = limit_reason;
			if (charger->pdata->batt_psy)
				power_supply_changed(charger->pdata->batt_psy);
			dev_info(charger->dev, "limit reason updated to %u", limit_reason);
		}
		charger->limit_reason_retry_count = LIMIT_REASON_RETRY_COUNT;
		__pm_relax(charger->limit_reason_ws);
	}
}

static int google_wlc_get_property(struct power_supply *psy,
				   enum power_supply_property prop,
				   union power_supply_propval *val)
{
	struct google_wlc_data *charger = power_supply_get_drvdata(psy);
	int ret = 0, rc;
	u32 reg;

	val->intval = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = google_wlc_is_present(charger);
		if (val->intval < 0)
			val->intval = 0;
		if (charger->online)
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		if (charger->online_disable) {
			val->intval = 0;
			break;
		}
		val->intval = charger->online;
		if (val->intval == 1 && !google_wlc_confirm_online(charger)) {
			if (charger->status != GOOGLE_WLC_STATUS_NOT_DETECTED &&
			    charger->status != GOOGLE_WLC_STATUS_DETECTED) {
				dev_info(charger->dev, "Online but not detected\n");
				__pm_stay_awake(charger->notifier_ws);
				pm_system_wakeup();
				schedule_delayed_work(&charger->psy_notifier_work, 0);
			}
		}
		if (!charger->mpp_initialized)
			break;
		if ((charger->pdata->has_mpp25_wlc_dc && mpp25_ready(charger->mpp25.state)) ||
		    (charger->pdata->has_mpp15_wlc_dc && charger->mpp15_wlc_dc_ready))
			val->intval = PPS_PSY_PROG_ONLINE;

		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = MV_TO_UV(google_wlc_max_voltage_by_mode_mv(charger, charger->mode));
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = 5000000; /* 5V */
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = MA_TO_UA(google_wlc_max_current_by_mode_ma(charger, charger->mode));
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!google_wlc_is_present(charger)) {
			val->intval = 0;
			break;
		}
		ret = charger->chip->chip_get_vout(charger, &reg);
		val->intval = ret ?: MV_TO_UV(reg);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (!google_wlc_is_present(charger)) {
			val->intval = 0;
			break;
		}
		ret = charger->chip->chip_get_iout(charger, &reg);
		val->intval = ret ?: MA_TO_UA(reg);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (!google_wlc_is_present(charger)) {
			val->intval = 0;
			break;
		}
		ret = charger->chip->chip_get_temp(charger, &reg);
		/* Return value in deciC */
		val->intval = ret ?: MILLIC_TO_DECIC(reg);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = charger->last_capacity;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = NULL;
		if (!google_wlc_is_present(charger) && !charger->online)
			break;

		val->strval = "00000000";

		rc = charger->chip->chip_get_ptmc_id(charger, &charger->ptmc);
		if ((rc || charger->ptmc == 0) && !charger->vout_ready)
			break;

		if (charger->chip->chip_get_txid_str(charger) != NULL) {
			val->strval = (char *)charger->tx_id_str;
		} else {
			mutex_lock(&charger->status_lock);
			rc = google_wlc_get_xid(charger);
			if (rc < 0) {
				mutex_unlock(&charger->status_lock);
				break;
			}
			scnprintf(charger->tx_id_str, sizeof(charger->tx_id_str), "%08x",
				  charger->tx_id);
			val->strval = (char *)charger->tx_id_str;
			mutex_unlock(&charger->status_lock);
		}
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (in_wlc_dc(charger)) {
			union power_supply_propval prop;

			power_supply_get_property(charger->wlc_dc_psy,
					POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &prop);
			val->intval = prop.intval;
			break;
		}
		val->intval = gvotable_get_current_int_vote(charger->dc_icl_votable);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static enum charger_switch_rc check_mpp25_eligible(struct google_wlc_data *charger)
{
	int ret;
	u8 val8;

	if (charger->mode == RX_MODE_WPC_MPP_RESTRICTED && charger->mpp_restricted_set) {
		dev_info(charger->dev, "MPP25: Mode was set to MPP Restricted");
		return CHARGER_SWITCH_MPP25_NOTSUPP;
	}

	if (charger->mode == RX_MODE_AC_MISSING || charger->status != GOOGLE_WLC_STATUS_CHARGING ||
	    charger->mode == RX_MODE_WPC_MPP_RESTRICTED || charger->mode == RX_MODE_WPC_MPP_NEGO ||
	    charger->mode == RX_MODE_WPC_MPP_HPM || charger->mode == RX_MODE_MPP_CLOAK) {
		if (__ratelimit(&present_ratelimit))
			dev_info(charger->dev, "MPP25: Charger still starting");
		return CHARGER_SWITCH_RESCHEDULE;
	}
	if (!mode_is_mpp(charger->mode)) {
		dev_info(charger->dev, "MPP25: Not in MPP");
		return CHARGER_SWITCH_MPP25_NOTSUPP;
	}
	if (!charger->mpp_initialized) {
		dev_info(charger->dev, "MPP25: MPP still starting");
		return CHARGER_SWITCH_RESCHEDULE;
	}

	if (!charger->pdata->has_mpp25_wlc_dc && !charger->pdata->support_mpp25) {
		dev_dbg(charger->dev, "MPP25: Not supported by DT");
		return CHARGER_SWITCH_MPP25_NOTSUPP;
	}

	if (charger->mpp25_disabled) {
		dev_info(charger->dev, "MPP25: Disabled");
		return CHARGER_SWITCH_MPP25_NOTSUPP;
	}

	if (charger->pdata->has_mpp25_wlc_dc && charger->last_capacity > WLC_DC_MAX_SOC) {
		dev_info(charger->dev, "MPP25: Battery SOC(%d) too high", charger->last_capacity);
		return CHARGER_SWITCH_MPP25_NOTSUPP;
	}
	if (!wpc_auth_passed(charger)) {
		dev_info(charger->dev, "MPP25: Wait for auth");
		return CHARGER_SWITCH_WAIT_AUTH;
	}
	if (!charger->wlc_dc_skip_qi_ver && !charger->mpp25.qi_ver_ok) {
		ret = charger->chip->chip_get_tx_qi_ver(charger, &val8);
		if (ret) {
			dev_info(charger->dev, "MPP25: Failed to read Qi version");
			return CHARGER_SWITCH_RESCHEDULE;
		}
		if (val8 < 0x22) {
			dev_info(charger->dev, "MPP25: Qi version < 2.2");
			return CHARGER_SWITCH_MPP25_NOTSUPP;
		}
	}
	charger->mpp25.qi_ver_ok = true;
	if (charger->mode != RX_MODE_WPC_MPP_CPM && charger->mode != RX_MODE_WPC_MPP_NPM) {
		dev_info(charger->dev, "MPP25: Mode not supported");
		return CHARGER_SWITCH_MPP25_NOTSUPP;
	}
	if (!charger->wlc_dc_skip_mated_q && !charger->mpp25.mated_q_ok) {
		ret = charger->chip->chip_get_mated_q(charger, &val8);
		if (ret) {
			dev_info(charger->dev, "MPP25: Failed to read Mated Q");
			return CHARGER_SWITCH_RESCHEDULE;
		}
		if (val8 != MATED_Q_NO_FOD) {
			dev_info(charger->dev, "MPP25: Invalid Mated Q result: %d", val8);
			return CHARGER_SWITCH_MPP25_NOTSUPP;
		}
		dev_info(charger->dev, "MPP25: valid Mated Q result: %d", val8);
	}
	charger->mpp25.mated_q_ok = true;

	if (!charger->skip_nego && !charger->mpp25.nego_pwr_ok) {
		if (charger->nego_power < GOOGLE_WLC_MPP_MAX_POWER) {
			dev_info(charger->dev, "MPP25: Nego power low: %d", charger->nego_power);
			return CHARGER_SWITCH_MPP25_NOTSUPP;
		}
	}
	charger->mpp25.nego_pwr_ok = true;

	if (!charger->wlc_dc_skip_pot_pwr && !charger->mpp25.pot_pwr_ok) {
		ret = check_mpp25_capabilities(charger);
		if (ret)
			return ret == -EAGAIN ? CHARGER_SWITCH_RESCHEDULE :
					CHARGER_SWITCH_MPP25_NOTSUPP;
	}
	charger->mpp25.pot_pwr_ok = true;

	if (!charger->wlc_dc_skip_pwr_limit_check && !charger->mpp25.power_limit_reason_ok) {
		if (charger->limit_reason_retry_count < LIMIT_REASON_RETRY_COUNT)
			return CHARGER_SWITCH_RESCHEDULE;

		ret = charger->chip->chip_get_limit_rsn(charger, &val8);
		if (ret) {
			dev_info(charger->dev, "MPP25: Failed to read Power Limit Reason");
			return CHARGER_SWITCH_RESCHEDULE;
		}
		charger->limit_reason = val8;
		if (val8 != POWER_LIMIT_NO_LIMIT && val8 != POWER_LIMIT_CAL_NOT_MET &&
		    val8 != POWER_LIMIT_CAL_LIMIT) {
			dev_info(charger->dev, "MPP25: Power limited by reason: %d", val8);
			return CHARGER_SWITCH_MPP25_NOTSUPP;
		}
		dev_info(charger->dev, "MPP25: Power limited OK: %d", val8);

	}
	charger->mpp25.power_limit_reason_ok = true;

	return CHARGER_SWITCH_OK;
}

/* call with status_lock */
static int dploss_try_cal_enter(struct google_wlc_data *charger)
{
	int ret;

	dev_info(charger->dev, "MPP25: Send CAL_ENTER");
	charger->mpp25.cal_active = true;
	reinit_completion(&charger->cal_enter_done);
	charger->wait_for_cal_enter = true;
	google_wlc_do_dploss_event(charger, DPLOSS_CAL_ENTER);
	mutex_unlock(&charger->status_lock);
	ret = wait_for_completion_interruptible_timeout(&charger->cal_enter_done,
		msecs_to_jiffies(MPP25_CAL_ENTER_TIMEOUT));
	mutex_lock(&charger->status_lock);
	charger->wait_for_cal_enter = false;
	if (ret == 0) {
		dev_err(charger->dev, "Error! cal enter timeout\n");
		return -EINVAL;
	}
	if (ret < 0) {
		dev_err(charger->dev, "Error! cal enter completion cannot wait\n");
		return -EINVAL;
	}
	if (charger->mpp25_disabled) {
		dev_info(charger->dev, "Exit cal_enter wait");
		return -EINVAL;
	}
	return 0;
}

static int google_wlc_ramp_down_icl(struct google_wlc_data *charger, const char *reason)
{
	int ret;

	if (charger->icl_now <= charger->output_chg_config->init_cur_ua)
		return 0;

	mutex_lock(&charger->completion_lock);
	charger->wait_for_icl_ramp = true;
	reinit_completion(&charger->icl_ramp_done);
	ret = gvotable_cast_long_vote(charger->icl_ramp_target_votable, reason,
			GOOGLE_WLC_STARTUP_MW, true);
	/* if callback ran, then wait */
	if (ret == 1) {
		ret = wait_for_completion_interruptible_timeout(&charger->icl_ramp_done,
			msecs_to_jiffies(GOOGLE_WLC_ICL_RAMP_TIMEOUT_MS));
		charger->wait_for_icl_ramp = false;
		if (ret < 0) {
			dev_err(charger->dev, "Error! ramp down completion cannot wait\n");
			mutex_unlock(&charger->completion_lock);
			return -EINVAL;
		}
	}
	mutex_unlock(&charger->completion_lock);
	return 0;
}

/* call with status_lock */
static int mpp25_setup(struct google_wlc_data *charger)
{
	int ret;

	if (!charger->wlc_dc_skip_dploss && !charger->mpp25.cal_enter_ok) {

		if (!charger->pdata->dploss_steps[0]) {
			dev_info(charger->dev, "MPP25: No calibration steps found");
			return -EOPNOTSUPP;
		}
		if (charger->pdata->dploss_steps[MPP_DPLOSS_NUM_STEPS - 1] > charger->nego_power) {
			dev_info(charger->dev, "MPP25: Max cal power higher than nego power");
			return -EOPNOTSUPP;
		}
		if (charger->pdata->dploss_steps[MPP_DPLOSS_NUM_STEPS - 1] >
		    charger->mdis_pwr_limit) {
			dev_info(charger->dev, "MPP25: Max cal power higher than mdis power");
			return -EOPNOTSUPP;
		}

		__pm_stay_awake(charger->mpp25_ws);
		schedule_delayed_work(&charger->mpp25_timeout_work,
			msecs_to_jiffies(MPP25_CALIBRATION_TIMEOUT_MS));
		ret = dploss_try_cal_enter(charger);
		if (ret < 0)
			goto notsupp;
		if (!charger->mpp25.cal_enter_ok &&
		    charger->mpp25.fod_cloak == FOD_CLOAK_RESEND) {
			charger->mpp25.fod_cloak = FOD_CLOAK_DONE;
			ret = dploss_try_cal_enter(charger);
			if (ret < 0 || !charger->mpp25.cal_enter_ok)
				goto notsupp;
		}
	}
	charger->mpp25.cal_enter_ok = true;
	logbuffer_devlog(charger->log, charger->dev, "MPP25: Setup done");
	return 0;
notsupp:
	__pm_relax(charger->mpp25_ws);
	return -EOPNOTSUPP;

}

static enum charger_switch_rc mpp25_pre_setup(struct google_wlc_data *charger)
{
	int ret;

	if (charger->mpp25.state == MPP25_OFF)
		charger->mpp25.state = MPP25_CHECKING;

	if (charger->mpp25.state == MPP25_CHECKING) {
		ret = check_mpp25_eligible(charger);
		if (ret == CHARGER_SWITCH_MPP25_NOTSUPP)
			charger->mpp25.state = MPP25_OFF;
		if (ret)
			return ret;
		charger->mpp25.state = MPP25_PREPARING;
	}
	mutex_lock(&charger->eds_lock);
	if (charger->eds_state != EDS_AVAILABLE ||
	    charger->chip->chip_check_eds_status(charger) == -EBUSY) {
		logbuffer_devlog(charger->log, charger->dev, "MPP25: Wait for EDS finish");
		mutex_unlock(&charger->eds_lock);
		return CHARGER_SWITCH_RESCHEDULE;
	}
	mutex_unlock(&charger->eds_lock);
	charger->mpp25.state = MPP25_READY;
	logbuffer_devlog(charger->log, charger->dev, "MPP25: Ready");
	google_wlc_update_psy(charger);
	return CHARGER_SWITCH_OK;
}

static void mpp15_wlc_dc_check(struct google_wlc_data *charger)
{
	if (!charger->pdata->has_mpp15_wlc_dc) {
		dev_dbg(charger->dev, "MPP15_DC: Not supported by DT");
		return;
	}
	if (!mode_is_mpp(charger->mode)) {
		dev_info(charger->dev, "MPP15_DC: Not in MPP");
		return;
	}
	dev_info(charger->dev, "MPP15_DC: OK");
	charger->mpp15_wlc_dc_ready = true;
}

static void google_charger_switch_ready_work(struct work_struct *work)
{
	struct google_wlc_data *charger = container_of(work,
			struct google_wlc_data, charger_switch_ready_work.work);
	int ret = 0;

	mutex_lock(&charger->status_lock);
	if (!charger->online || charger->shutdown_notify)
		goto exit_disable;
	ret = mpp25_pre_setup(charger);
	if (ret == CHARGER_SWITCH_RESCHEDULE || ret == CHARGER_SWITCH_WAIT_AUTH) {
		int delta = get_boot_sec() - charger->online_at;

		if (charger->online_at > 0 && delta > CHARGER_SWITCH_CHECK_TIMEOUT_S) {
			dev_err(charger->dev, "charger_switch timeout: delta=%d", delta);
			goto exit_disable;
		}
		if (ret == CHARGER_SWITCH_RESCHEDULE)
			schedule_delayed_work(&charger->charger_switch_ready_work,
				msecs_to_jiffies(MPP25_CHECK_DELAY_MS));
		else if (ret == CHARGER_SWITCH_WAIT_AUTH)
			schedule_delayed_work(&charger->charger_switch_ready_work,
				msecs_to_jiffies(CHARGER_SWITCH_CHECK_TIMEOUT_S * 1000));
	} else {
		mpp15_wlc_dc_check(charger);
	}
	if (ret == CHARGER_SWITCH_MPP25_NOTSUPP)
		goto exit_disable;
	if (ret == CHARGER_SWITCH_OK)
		charger->charger_switch_check_finished = true;

	mutex_unlock(&charger->status_lock);
	dev_dbg(charger->dev, "charger_switch_ready_work: ret=%d", ret);
	return;
exit_disable:
	charger->mpp25.state = MPP25_OFF;
	charger->charger_switch_check_finished = true;
	dev_dbg(charger->dev, "charger_switch_ready_work: exit, ret=%d", ret);
	google_wlc_update_psy(charger);
	mutex_unlock(&charger->status_lock);
}

static int google_wlc_back_npm(struct google_wlc_data *charger, int mv)
{
	union power_supply_propval prop;
	int ret, iin_cc, pwrmode, pwr;

	if (charger->mpp25.state != MPP25_ACTIVE || charger->wlc_dc_to_ramp ||
	    charger->mpp25.back_npm)
		return -EAGAIN;

	pwrmode = mpp_get_current_powermode(charger);
	if (pwrmode != MPP_POWERMODE_HIGH)
		return -EAGAIN;

	ret = power_supply_get_property(charger->wlc_dc_psy,
					POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &prop);
	if (ret != 0 || prop.intval <= 0) {
		dev_err(charger->dev, "could not read input current limit: ret=%d, prop=%d",
			ret, prop.intval);
		return -EAGAIN;
	}

	iin_cc = UA_TO_MA(prop.intval);
	pwr = charger->mpp25.mode_capabilities.npm.pot_pwr > 0 ?
	      charger->mpp25.mode_capabilities.npm.pot_pwr : GOOGLE_WLC_MPP_NPM_MAX_POWER;
	dev_dbg(charger->dev, "iin_cc: %d mA, vout: %d mV, npm pmax: %d", iin_cc, mv, pwr);

	if ((iin_cc * mv / 1000) <= pwr || charger->nego_power <= pwr) {
		charger->mpp25.pwrmode_ok = false;
		charger->mpp25.back_npm = true;
		dev_info(charger->dev, "Request to NPM");
		charger->chip->chip_set_mpp_powermode(charger, MPP_POWERMODE_NOMINAL, true);
		return 0;
	}

	return -EAGAIN;
}

static int google_wlc_set_vout_for_wlc_dc(struct google_wlc_data *chgr, u32 mv)
{
	u32 vout;
	u32 vrect;
	u32 iout;
	s8 xce;
	u32 prev_vout_set;
	u32 pout, pout_low = 0, vout_delta = 0;
	int ret;
	const int xce_thresh = (chgr->wlc_dc_xce_thresh > 0) ?
				 chgr->wlc_dc_xce_thresh : chgr->pdata->wlc_dc_xce_thresh;

	if (mpp25_running(chgr->mpp25.state) && google_wlc_back_npm(chgr, mv) == 0)
		return -EAGAIN;

	if (mv < GOOGLE_WLC_VOUT_MIN || mv > GOOGLE_WLC_VOUT_MAX ||
	    mv > chgr->output_chg_config->max_volt_uv) {
		dev_info(chgr->dev, "Invalid vout setting: %d, skip", mv);
		return -EINVAL;
	}

	ret = chgr->chip->chip_get_xce(chgr, &xce);
	ret |= chgr->chip->chip_get_vout(chgr, &vout);
	ret |= chgr->chip->chip_get_vrect(chgr, &vrect);
	ret |= chgr->chip->chip_get_vout_set(chgr, &prev_vout_set);
	ret |= chgr->chip->chip_get_iout(chgr, &iout);
	if (ret != 0)
		return ret;
	pout = vout * iout / 1000;

	if ((xce > xce_thresh && mv > prev_vout_set &&
	     prev_vout_set != chgr->chip->val_default_vout)) {
		dev_info(chgr->dev, "Fail to set vout %d->%d due to XCE %d, vout: %d, vrect: %d, iout: %d, pout: %d",
			 prev_vout_set, mv, xce, vout, vrect, iout, pout);
		return -EAGAIN;
	}

	if (chgr->nego_power > chgr->wlc_dc_max_pout_delta)
		pout_low = chgr->nego_power - chgr->wlc_dc_max_pout_delta;

	if (prev_vout_set > vout)
		vout_delta = prev_vout_set - vout;

	if (pout > pout_low && mv > prev_vout_set && vout_delta > chgr->wlc_dc_max_vout_delta) {
		dev_info(chgr->dev, "Fail to set vout %d->%d due to vout delta %d, vout: %d, vrect: %d, iout: %d, xce: %d, pout: %d",
			prev_vout_set, mv, vout_delta, vout, vrect, iout, xce, pout);
		return -EAGAIN;
	}

	dev_info(chgr->dev,
		 "Setting vout to %d. vout: %d, vrect: %d, prev vout_set: %d, iout: %d, xce: %d, pout: %d\n",
		 mv, vout, vrect, prev_vout_set, iout, xce, pout);
	return chgr->chip->chip_set_vout(chgr, mv);
}

static int google_wlc_set_property(struct power_supply *psy,
				    enum power_supply_property prop,
				    const union power_supply_propval *val)
{
	struct google_wlc_data *charger = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval == 0)
			gvotable_cast_int_vote(charger->wlc_disable_votable, WLC_USER_VOTER,
					       WLC_HARD_DISABLE, true);
		else if (val->intval == 1)
			gvotable_cast_int_vote(charger->wlc_disable_votable, WLC_USER_VOTER,
					       WLC_HARD_DISABLE, false);
		if (val->intval == PPS_PSY_PROG_ONLINE) {
			if (charger->pdata->support_epp) {
				ret = -EOPNOTSUPP;
			} else if (!charger->charger_switch_check_finished) {
				ret = -EAGAIN;
			} else if (charger->pdata->has_mpp25_wlc_dc &&
					charger->mpp25.state != MPP25_OFF) {
				if (mpp25_ready(charger->mpp25.state))
					ret = 1;
				else
					ret = -EAGAIN;
			} else {
				if (charger->pdata->has_mpp15_wlc_dc &&
				    charger->mpp15_wlc_dc_ready)
					ret = 1;
				else
					ret = -EOPNOTSUPP;
			}
			dev_dbg(charger->dev, "return from set online: %d", ret);
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (val->intval < 0) {
			ret = -EINVAL;
			break;
		}
		if (!charger->dc_icl_votable) {
			ret = -EAGAIN;
			break;
		}
		ret = gvotable_cast_int_vote(charger->dc_icl_votable, WLC_USER_VOTER,
					     val->intval, true);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!in_wlc_dc(charger) ||
		    (!mpp25_ready(charger->mpp25.state) && !charger->mpp15_wlc_dc_ready)) {
			dev_info(charger->dev, "Not in WLC_DC, reject vout setting");
			return -EINVAL;
		}
		ret = google_wlc_set_vout_for_wlc_dc(charger, UV_TO_MV(val->intval));
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int google_wlc_prop_is_writeable(struct power_supply *psy,
					 enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return 1;
	default:
		break;
	}
	return 0;
}

static int google_wlc_gbms_get_property(struct power_supply *psy,
				   enum gbms_property prop,
				   union gbms_propval *val)
{
	struct google_wlc_data *charger = power_supply_get_drvdata(psy);
	int ret = 0;
	u32 reg;

	switch (prop) {
	case GBMS_PROP_WLC_VRECT:
		if (!google_wlc_is_present(charger)) {
			val->prop.intval = 0;
			break;
		}
		ret = charger->chip->chip_get_vrect(charger, &reg);
		val->prop.intval = ret ? 0 : MV_TO_UV(reg); /* mV to uV */
		break;
	case GBMS_PROP_WLC_OP_FREQ:
		if (!google_wlc_is_present(charger)) {
			val->prop.intval = 0;
			break;
		}
		ret = charger->chip->chip_get_opfreq(charger, &reg);
		val->prop.intval = ret ? 0 : KHZ_TO_HZ(reg);
		break;
	case GBMS_PROP_WLC_VCPOUT:
		/* Not supported */
		val->prop.intval = 0;
		break;
	case GBMS_PROP_WLC_ICL_LEVEL:
		val->prop.intval = charger->mdis_level;
		break;
	case GBMS_PROP_ADAPTER_DETAILS:
		val->prop.intval = google_wlc_get_adapter_type(charger);
		break;

	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int google_wlc_gbms_set_property(struct power_supply *psy,
				   enum gbms_property prop,
				   const union gbms_propval *val)
{
	struct google_wlc_data *charger = power_supply_get_drvdata(psy);
	int limit = 0, ret = 0;

	switch (prop) {
	case GBMS_PROP_MPP_DPLOSS_CALIBRATION_LIMIT:
		if (!charger->mpp25.cal_active || charger->mpp25.dploss_step <= 0) {
			dev_info(charger->dev,
				 "Got calibration limit when not in cal, ignore. active=%d, step=%d",
				 charger->mpp25.cal_active, charger->mpp25.dploss_step);
			break;
		}
		if (charger->mpp25.state == MPP25_DPLOSS_CAL4) {
			if (val->prop.intval == charger->mpp25_dploss_cal4) {
				google_wlc_do_dploss_event(charger, DPLOSS_CAL_EXTEND);
			} else {
				dev_info(charger->dev, "Got cal limit %d in 4th cal, abort cal",
					 val->prop.intval);
				google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
			}
			break;
		}

		if (charger->mpp25.dploss_step <= MPP_DPLOSS_NUM_STEPS)
			limit = charger->pdata->dploss_steps[charger->mpp25.dploss_step - 1];
		else
			dev_err(charger->dev, "WLC-DC: invalid cal step: %d",
				charger->mpp25.dploss_step);

		if (val->prop.intval == limit && limit != 0) {
			dev_info(charger->dev, "WLC-DC: Got cal limit %d, continue cal",
				val->prop.intval);
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_CAPTURE);
		} else if (val->prop.intval == 0) {
			dev_err(charger->dev, "WLC-DC: Got cal limit 0, exit cal");
			google_wlc_do_dploss_event(charger, DPLOSS_CAL_ABORT);
			google_wlc_disable_mpp25(charger, true);
		} else {
			dev_err(charger->dev,
				"WLC-DC: Current cal limit %d, received cal limit %d",
				limit, val->prop.intval);
		}
		break;
	case GBMS_PROP_WLC_ICL_LEVEL:
		charger->mdis_level = val->int64val;
		google_wlc_thermal_icl_vote(charger);
		break;
	case GBMS_PROP_CHG_INDEX_POWER:
		ret = google_wlc_mdis_into_power_limit(charger, charger->mdis_level,
						       EXECUTION_MODE_DRY_RUN, val->int64val);
		break;
	case GBMS_PROP_PRI_CHG_MDIS:
		charger->pri_chg_mdis_lvl = val->int64val;
		google_wlc_pri_chg_vote(charger);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int google_wlc_gbms_prop_is_writeable(struct power_supply *psy,
					enum gbms_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_CAPACITY:
	case GBMS_PROP_WLC_ICL_LEVEL:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case GBMS_PROP_MPP_DPLOSS_CALIBRATION_LIMIT:
	case GBMS_PROP_ENABLE_SWITCH_CAP:
	case GBMS_PROP_CHG_INDEX_POWER:
	case GBMS_PROP_PRI_CHG_MDIS:
		return 1;
	default:
		break;
	}
	return 0;
}

static enum power_supply_property google_wlc_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static const struct gbms_desc google_wlc_psy_desc = {
	.psy_dsc.name = "wireless",
	.psy_dsc.type = POWER_SUPPLY_TYPE_WIRELESS,
	.psy_dsc.properties = google_wlc_props,
	.psy_dsc.num_properties = ARRAY_SIZE(google_wlc_props),
	.psy_dsc.get_property = google_wlc_get_property,
	.psy_dsc.set_property = google_wlc_set_property,
	.psy_dsc.property_is_writeable = google_wlc_prop_is_writeable,
	.get_property = google_wlc_gbms_get_property,
	.set_property = google_wlc_gbms_set_property,
	.property_is_writeable = google_wlc_gbms_prop_is_writeable,
	.psy_dsc.no_thermal = true,
	.forward = true,
};

static int google_wlc_gpio_get_direction(struct gpio_chip *chip,
				    unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int google_wlc_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return 0;
}

static void google_wlc_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct google_wlc_data *charger = gpiochip_get_data(chip);
	int ret = 0;
	int mod_timing = charger->output_chg_config->mod_change_timing;

	switch (offset) {
	case GOOGLE_WLC_OUTPUT_ENABLE:
		if (value != charger->output_enable)
			logbuffer_devlog(charger->log, charger->dev, "output_enable=%d->%d",
					 charger->output_enable, value);
		charger->output_enable = value;
		gvotable_cast_int_vote(charger->wlc_disable_votable, OUTPUT_DISABLE_VOTER,
			       WLC_SOFT_DISABLE, !charger->output_enable);
		break;
	case GOOGLE_WLC_SWC_ON_GPIO:
		if (!mode_is_mpp(charger->mode) && charger->mode != RX_MODE_MPP_CLOAK) {
			if (value == 0) {
				charger->dc_data.swc_on = false;
				dev_info(charger->dev, "Received SWC off GPIO");
			}
			break;
		}
		if (value == 1) {
			charger->dc_data.swc_on = true;
			dev_info(charger->dev, "Received SWC on GPIO");
			if (mod_timing == ASK_MOD_TIMING_GPIO ||
			    mod_timing == ASK_MOD_TIMING_POS_CAP_GPIO) {
				int mod = charger->output_chg_config->mod_mode;

				ret = charger->chip->chip_set_mod_mode(charger, mod);
				if (ret)
					dev_err(charger->dev, "Could not change ASK mode");
				else
					charger->ask_mod_mode = mod;
			}
			mod_delayed_work(system_wq, &charger->charging_init_work, 0);
		} else if (value == 0) {
			int mod_mode = ASK_MOD_MODE_ADAPTIVE_LOAD_MOD;

			if (mod_timing == ASK_MOD_TIMING_POS_CAP_GPIO &&
			    charger->mode != RX_MODE_MPP_CLOAK)
				mod_mode = ASK_MOD_MODE_POS_CAP_MOD;
			else
				mod_mode = ASK_MOD_MODE_ADAPTIVE_LOAD_MOD;

			ret = charger->chip->chip_set_mod_mode(charger, mod_mode);
			if (ret)
				dev_err(charger->dev, "Could not change ASK mode");
			else
				charger->ask_mod_mode = mod_mode;
			charger->dc_data.swc_on = false;
			dev_info(charger->dev, "Received SWC off GPIO");
		}
		break;
	case GOOGLE_WLC_ONLINE_SPOOF:
		/* Spoof the online signal during cloak mode */
		if (value != charger->online_spoof)
			logbuffer_devlog(charger->log, charger->dev, "online_spoof=%d->%d",
					 charger->online_spoof, value);
		charger->online_spoof = value;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret < 0)
		dev_err(&charger->client->dev, "GPIO%d: value=%d ret:%d\n",
			offset, value, ret);
}

#if IS_ENABLED(CONFIG_GPIOLIB)
static void google_wlc_gpio_init(struct google_wlc_data *charger)
{
	charger->gpio.owner = THIS_MODULE;
	charger->gpio.label = "google_wlc_gpio";
	charger->gpio.get_direction = google_wlc_gpio_get_direction;
	charger->gpio.get = google_wlc_gpio_get;
	charger->gpio.set = google_wlc_gpio_set;
	charger->gpio.base = -1;
	charger->gpio.ngpio = GOOGLE_WLC_NUM_GPIOS;
	charger->gpio.can_sleep = true;
}
#endif
/* Returns length of fod array found */
static int google_wlc_parse_config(struct device *dev, u8 **fod, char *of_name)
{
	int ret;
	struct device_node *node = dev->of_node;
	int fod_num;
	char buf[GOOGLE_WLC_MAX_FOD_NUM * 3 + 1];

	fod_num = of_property_count_elems_of_size(node, of_name, sizeof(u8));
	if (fod_num <= 0) {
		dev_err(dev, "No dt %s provided (%d)\n", of_name, fod_num);
		return 0;
	}
	if (fod_num > GOOGLE_WLC_MAX_FOD_NUM) {
		dev_err(dev, "Too many DT FODs detected. num: %d, max: %d", fod_num,
			GOOGLE_WLC_MAX_FOD_NUM);
		return 0;
	}

	*fod = devm_kzalloc(dev, fod_num, GFP_KERNEL);
	if (*fod == NULL) {
		dev_err(dev, "Failed to allocate FOD array\n");
		return 0;
	}
	ret = of_property_read_u8_array(node, of_name, *fod, fod_num);
	if (ret != 0) {
		dev_err(dev, "Failed to read fods from dt\n");
		return 0;
	}

	google_wlc_hex_str(*fod, fod_num, buf, fod_num * 3 + 1, false);
	dev_info(dev, "dt %s: %s (%d)\n", of_name, buf, fod_num);
	return fod_num;
}

static int google_wlc_parse_chg_config(struct device *dev, struct device_node *config_node,
				       struct wlc_chg_config *config)
{
	int ret = 0;
	int mdis_ret = 0;

	ret |= of_property_read_u32(config_node, "target-power-mode", &config->target_power_mode);
	ret |= of_property_read_u32(config_node, "charger-type", &config->charger_type);
	ret |= of_property_read_u32(config_node, "mod-mode", &config->mod_mode);
	ret |= of_property_read_u32(config_node, "mod-change-timing", &config->mod_change_timing);
	ret |= of_property_read_u32(config_node, "initial-current-ua", &config->init_cur_ua);
	ret |= of_property_read_u32(config_node, "icl-step-size-ua", &config->icl_step_size_ua);
	ret |= of_property_read_u32(config_node, "icl-rounding", &config->icl_rounding);
	ret |= of_property_read_u32(config_node, "initial-voltage-uv", &config->init_volt_uv);
	ret |= of_property_read_u32(config_node, "max-voltage-uv", &config->max_volt_uv);
	config->auto_vout = of_property_read_bool(config_node, "use-auto-vout");
	config->mpp25_only = of_property_read_bool(config_node, "mpp25-only");
	config->exit_by_ept = of_property_read_bool(config_node, "exit-by-ept");
	mdis_ret = of_property_read_string(config_node, "mpp-mdis-name", &config->mpp_mdis_name);
	if (mdis_ret == 0) {
		config->mpp_mdis_num = google_wlc_parse_mdis_table(dev, config->mpp_mdis_pwr,
								   config->mpp_mdis_name);
	}

	return ret;
}

static int google_wlc_parse_mdis_table(struct device *dev, u32 *mdis, const char *of_name)
{
	int ret, mdis_num, len = 0;
	struct device_node *node = dev->of_node;
	char buf[9 * MDIS_LEVEL_MAX + 1];

	mdis_num = of_property_count_elems_of_size(node, of_name, sizeof(u32));
	if (mdis_num <= 0)
		return 0;

	if (mdis_num > MDIS_LEVEL_MAX) {
		dev_err(dev,
			"Incorrect num of %s: %d, using first %d\n",
			of_name, mdis_num, MDIS_LEVEL_MAX);
		mdis_num = MDIS_LEVEL_MAX;
	}
	dev_info(dev, "dt %s:", of_name);
	ret = of_property_read_u32_array(node, of_name, mdis, mdis_num);
	if (ret == 0) {
		for (int i = 0; i < mdis_num; i++)
			len += scnprintf(buf + len, sizeof(buf) - len, "%d ", mdis[i]);
		dev_info(dev, "%s", buf);
	} else {
		mdis_num = 0;
	}

	return mdis_num;

}

static int google_wlc_parse_iop_setting(struct device *dev, u16 *mfg, u32 *vout,
		char *of_mfg_name, char *of_vout_name)
{
	int ret, mfg_num, vout_num;
	struct device_node *node = dev->of_node;

	mfg_num = of_property_count_elems_of_size(node, of_mfg_name, sizeof(u16));
	vout_num = of_property_count_elems_of_size(node, of_vout_name, sizeof(u32));
	if (mfg_num <= 0 || vout_num <= 0)
		return 0;

	if (mfg_num > IOP_MFG_NUM_MAX || vout_num > IOP_MFG_NUM_MAX) {
		dev_err(dev,
			"Incorrect num of %s: %d, %s: %d, using first %d\n",
			of_mfg_name, mfg_num, of_vout_name, vout_num, IOP_MFG_NUM_MAX);
		mfg_num = IOP_MFG_NUM_MAX;
		vout_num = IOP_MFG_NUM_MAX;
	}

	if (mfg_num <= vout_num)
		vout_num = mfg_num;
	else
		mfg_num = vout_num;

	ret = of_property_read_u16_array(node, of_mfg_name, mfg, mfg_num);
	ret = ret | of_property_read_u32_array(node, of_vout_name, vout, vout_num);
	if (ret == 0) {
		for (int i = 0; i < mfg_num; i++)
			dev_info(dev, "%s: 0x%04x %s: %d\n",
					of_mfg_name, mfg[i], of_vout_name, vout[i]);
	} else {
		mfg_num = 0;
	}

	return mfg_num;

}

static int google_wlc_parse_dt(struct device *dev,
				struct google_wlc_platform_data *pdata)
{
	struct gpio_desc *gpio;
	int ret = 0;
	struct device_node *node = dev->of_node;
	u8 val8;
	u32 val;
	int option;

	/* Main IRQ */
	gpio = devm_gpiod_get(dev, "irq", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(gpio)) {
		dev_err(dev, "unable to read irq_gpio from dt: %ld\n", PTR_ERR(gpio));
		return PTR_ERR(gpio);
	}
	pdata->irq_gpio = gpio;
	ret = gpiod_to_irq(pdata->irq_gpio);
	if (ret < 0)
		return ret;
	pdata->irq_int = ret;

	/*
	 * this irq is for handling hybrid error
	 * not necessarily supported by every device
	 * don't return error except -EPROBE_DEFER
	 */
	ret = of_irq_get(node, 0);
	if (ret == -EPROBE_DEFER)
		return ret;
	pdata->irq_hybrid = ret;

	dev_info(dev, "int gpio:%d, gpio_irq:%d, hybrid_irq:%d\n", desc_to_gpio(pdata->irq_gpio),
		 pdata->irq_int, pdata->irq_hybrid);

	gpio = devm_gpiod_get_optional(dev, "ap5v", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(gpio)) {
		if (PTR_ERR(gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		pdata->ap5v_gpio = gpio;
		dev_info(dev, "ap5v gpio:%d\n", desc_to_gpio(pdata->ap5v_gpio));
	}

	gpio = devm_gpiod_get_optional(dev, "vdden", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(gpio)) {
		if (PTR_ERR(gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		pdata->vdden_gpio = gpio;
		dev_info(dev, "vdden gpio:%d\n", desc_to_gpio(pdata->vdden_gpio));
	}

	gpio = devm_gpiod_get(dev, "inhibit", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(gpio)) {
		dev_err(dev, "unable to read inhibit_gpio from dt: %ld\n", PTR_ERR(gpio));
		return PTR_ERR(gpio);
	}
	pdata->inhibit_gpio = gpio;
	dev_info(dev, "inhibit gpio:%d\n", desc_to_gpio(pdata->inhibit_gpio));

	gpio = devm_gpiod_get(dev, "mode", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(gpio)) {
		dev_err(dev, "unable to read mode_gpio from dt: %ld\n", PTR_ERR(gpio));
		if (PTR_ERR(gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		pdata->mode_gpio = gpio;
		dev_info(dev, "mode gpio:%d\n", desc_to_gpio(pdata->mode_gpio));
	}

	gpio = devm_gpiod_get(dev, "det", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(gpio)) {
		if (PTR_ERR(gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		pdata->det_gpio = gpio;
		dev_info(dev, "det gpio:%d\n", desc_to_gpio(pdata->det_gpio));
	}

	gpio = devm_gpiod_get(dev, "qi_version", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(gpio)) {
		if (PTR_ERR(gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		pdata->qi_version_gpio = gpio;
		dev_info(dev, "qi_version  gpio:%d\n", desc_to_gpio(pdata->qi_version_gpio));
	}

	pdata->wcin_inlim_en_gpio = devm_gpiod_get_optional(dev, "google,wcin_inlim_en",
							    GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(pdata->wcin_inlim_en_gpio)) {
		if (PTR_ERR(pdata->wcin_inlim_en_gpio) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
	} else {
		dev_info(dev, "WCIN_INLIM_EN gpio: %d\n", desc_to_gpio(pdata->wcin_inlim_en_gpio));
	}

	pdata->batt_psy = devm_power_supply_get_by_phandle(dev, "google,fg-power-supply");
	if (IS_ERR_OR_NULL(pdata->batt_psy)) {
		dev_info(dev, "unable to find fuel gauge psy\n");
		pdata->batt_psy = NULL;
	}

	ret = of_property_read_u8(node, "google,project_id", &val8);
	if (ret == 0)
		pdata->project_id = val8;

	/* mdis table */
	pdata->bpp_mdis_num = google_wlc_parse_mdis_table(dev, pdata->bpp_mdis_pwr, "bpp_mdis_pwr");
	pdata->epp_mdis_num = google_wlc_parse_mdis_table(dev, pdata->epp_mdis_pwr, "epp_mdis_pwr");
	pdata->mpp_mdis_num = google_wlc_parse_mdis_table(dev, pdata->mpp_mdis_pwr, "mpp_mdis_pwr");
	pdata->wlc_dc_mdis_num = google_wlc_parse_mdis_table(dev, pdata->wlc_dc_mdis_fcc,
							     "wlc_dc_mdis_fcc");

	pdata->bpp_fod_num = google_wlc_parse_config(dev, &pdata->bpp_fods, "google,bpp_fods");
	pdata->epp_fod_num = google_wlc_parse_config(dev, &pdata->epp_fods, "google,epp_fods");
	ret = of_property_read_u8(node, "google,fod_qf", &val8);
	if (ret == 0)
		pdata->fod_qf = val8;
	ret = of_property_read_u8(node, "google,fod_rf", &val8);
	if (ret == 0)
		pdata->fod_rf = val8;
	dev_info(dev, "fod_qf: %02x, fod_rf: %02x\n", pdata->fod_qf, pdata->fod_rf);

	ret = of_property_read_u32(node, "google,soft_ocp_icl_ma", &val);
	if (ret == 0)
		pdata->soft_ocp_icl = val;
	else
		pdata->soft_ocp_icl = GOOGLE_WLC_RX_ILIM_MAX_UA;
	pdata->mpp_mpla_num = google_wlc_parse_config(dev, &pdata->mpp_mplas, "google,mpp_mpla");
	pdata->rf_curr_num = google_wlc_parse_config(dev, &pdata->rf_currs, "google,rf_currs");

	ret = of_property_read_u32(node, "google,support-mpp25", &val);
	if (ret == 0)
		pdata->support_mpp25 = !!val;
	else
		pdata->support_mpp25 = 0;
	dev_info(dev, "support_mpp25:%d\n", pdata->support_mpp25);

	ret = of_property_read_u32(node, "google,has-mpp25-wlc-dc", &val);
	if (ret == 0)
		pdata->has_mpp25_wlc_dc = !!val;
	else
		pdata->has_mpp25_wlc_dc = 0;
	dev_info(dev, "has_mpp25_wlc_dc:%d\n", pdata->has_mpp25_wlc_dc);

	if (pdata->has_mpp25_wlc_dc)
		pdata->support_mpp25 = true;

	ret = of_property_read_u32(node, "google,has-mpp15-wlc-dc", &val);
	if (ret == 0)
		pdata->has_mpp15_wlc_dc = !!val;
	else
		pdata->has_mpp15_wlc_dc = 0;
	dev_info(dev, "has_mpp15_wlc_dc:%d\n", pdata->has_mpp15_wlc_dc);

	ret = of_property_read_u32(node, "google,wlc_dc_max_voltage", &val);
	if (ret == 0)
		pdata->wlc_dc_max_voltage = val;
	else
		pdata->wlc_dc_max_voltage = GOOGLE_WLC_DC_MAX_VOLT_DEFAULT_MV;

	pdata->support_txid = of_property_read_bool(node, "google,wlc_tx_id");
	pdata->support_epp = of_property_read_bool(node, "google,wlc_epp");
	pdata->eoc_cloak_en = of_property_read_bool(node, "google,wlc_eoc_cloak");
	pdata->no_charger_switch_cloak = of_property_read_bool(node,
							       "google,charger-switch-skip-cloak");

	val = of_property_count_elems_of_size(node, "google,dploss_steps", sizeof(u32));
	if (val != MPP_DPLOSS_NUM_STEPS) {
		dev_info(dev, "Invalid dploss steps provided\n");
	} else {
		ret = of_property_read_u32_array(node, "google,dploss_steps",
						(u32 *)&pdata->dploss_steps, MPP_DPLOSS_NUM_STEPS);
		if (ret == 0)
			dev_info(dev, "dploss steps: [%d, %d, %d]", pdata->dploss_steps[0],
				 pdata->dploss_steps[1], pdata->dploss_steps[2]);
		ret = of_property_read_u32_array(node, "google,dploss-volts",
						(u32 *)&pdata->dploss_volts, MPP_DPLOSS_NUM_STEPS);
		if (ret == 0)
			dev_info(dev, "dploss volts: [%d, %d, %d]", pdata->dploss_volts[0],
				 pdata->dploss_volts[1], pdata->dploss_volts[2]);
		ret = of_property_read_u32(node, "google,dploss_points", &val);
		if (ret == 0) {
			pdata->dploss_points_num = val;
			dev_info(dev, "dploss cal points:%d\n", pdata->dploss_points_num);
		}
	}
	ret = of_property_read_u8(node, "google,wlc_mod_depth_max", &val8);
	if (ret == 0)
		pdata->mod_depth_max = val8;

	ret = of_property_read_u32(node, "google,wlc_mod_soc", &val);
	if (ret == 0)
		pdata->mod_soc = val;

	dev_info(dev, "dynamic mod depth max: %d, start soc: %d\n", pdata->mod_depth_max,
		 pdata->mod_soc);

	pdata->mpp_eds_level_num = of_property_count_elems_of_size(node, "mpp_eds_soc",
								   sizeof(u32));
	val = of_property_count_elems_of_size(node, "mpp_eds_icl", sizeof(u32));
	if (val != pdata->mpp_eds_level_num || val > EDS_ICL_LEVEL_MAX) {
		dev_info(dev, "Invalid mpp icl levels\n");
		pdata->mpp_eds_level_num = 0;
	} else {
		ret = of_property_read_u32_array(node, "mpp_eds_soc",
						 (u32 *)&pdata->mpp_eds_soc, val);
		ret |= of_property_read_u32_array(node, "mpp_eds_icl",
						  (u32 *)&pdata->mpp_eds_icl, val);
		if (ret) {
			pdata->mpp_eds_level_num = 0;
		} else {
			for (int i = 0; i < val; i++)
				dev_info(dev, "eds icl level [%d] for soc %d: %d\n", i,
					 pdata->mpp_eds_soc[i], pdata->mpp_eds_icl[i]);
		}
	}

	ret = of_property_read_s32(node, "google,rx_fwupdate_option", &option);
	if (ret)
		pdata->fwupdate_option = FWUPDATE_CRC_NOTSUPPORT;
	else
		pdata->fwupdate_option = option;

	ret = of_property_read_u32(node, "google,power_mitigate_threshold", &val);
	if (ret)
		pdata->power_mitigate_threshold = 0;
	else
		pdata->power_mitigate_threshold = val;

	pdata->ext_bst_supply = devm_regulator_get_optional(dev, "bms-external-boost");

	ret = of_property_read_u32(node, "google,wlc_qispec", &val);
	if (ret == 0)
		pdata->qispec = val;
	if (pdata->qispec > 0)
		dev_info(dev, "qi spec: %X\n", pdata->qispec);
	else if (pdata->qispec == 0)
		dev_info(dev, "qi spec unknown");
	else
		dev_info(dev, "qi spec unsupported: %d\n", pdata->qispec);

	pdata->iop_vout_mfg_num = google_wlc_parse_iop_setting(dev,
			pdata->iop_vout_mfg, pdata->iop_vout_mv,
			"google,iop_vout_mfg", "google,iop_vout_mv");

	ret = of_property_read_u32(node, "google,cloak_ping_delay_ms", &val);
	if (ret)
		pdata->cloak_ping_delay_ms = 0;
	else
		pdata->cloak_ping_delay_ms = val;

	ret = of_property_read_u32(node, "google,wlc-dc-xce-thresh", &val);
	if (ret)
		pdata->wlc_dc_xce_thresh = WLC_DC_XCE_THRESH_DEFAULT;
	else
		pdata->wlc_dc_xce_thresh = val;

	ret = of_property_read_u32(node, "google,wlc-dc-max-vout-delta", &val);
	if (ret)
		pdata->wlc_dc_max_vout_delta = WLC_DC_MAX_VOUT_DELTA_DEFAULT;
	else
		pdata->wlc_dc_max_vout_delta = val;

	ret = of_property_read_u32(node, "google,wlc-dc-max-pout-delta", &val);
	if (ret)
		pdata->wlc_dc_max_pout_delta = WLC_DC_MAX_POUT_DELTA_DEFAULT;
	else
		pdata->wlc_dc_max_pout_delta = val;

	ret = of_count_phandle_with_args(node, "google,wlc-chg-configs", NULL);

	if (ret > 0) {
		struct device_node *config_node;

		pdata->chg_configs_num = ret;
		pdata->chg_configs = devm_kzalloc(dev,
			pdata->chg_configs_num * sizeof(struct wlc_chg_config), GFP_KERNEL);
		if (!pdata->chg_configs)
			return -ENOMEM;
		for (int i = 0; i < pdata->chg_configs_num; i++) {
			config_node = of_parse_phandle(node, "google,wlc-chg-configs", i);
			if (!config_node) {
				dev_err(dev, "failed to parse chg configs phandle");
				devm_kfree(dev, pdata->chg_configs);
				goto chg_config_error;
			}
			ret = google_wlc_parse_chg_config(dev, config_node, &pdata->chg_configs[i]);
			if (ret != 0) {
				dev_err(dev, "failed to parse chg configs, ret=%d", ret);
				devm_kfree(dev, pdata->chg_configs);
				goto chg_config_error;
			}
			dev_info(dev,
				"chg_config index: %d, pwrmode:%s, mod: %s, mod timing: %d, icl_min: %d, icl_round: %d, init_volt: %d, auto_vout: %d, chg_type: %d, icl_step: %d, max_volt: %d, mpp_mdis_name: %s",
				i,
				mpp_powermode_str[pdata->chg_configs[i].target_power_mode],
				ask_mod_str[pdata->chg_configs[i].mod_mode],
				pdata->chg_configs[i].mod_change_timing,
				pdata->chg_configs[i].init_cur_ua,
				pdata->chg_configs[i].icl_rounding,
				pdata->chg_configs[i].init_volt_uv,
				pdata->chg_configs[i].auto_vout,
				pdata->chg_configs[i].charger_type,
				pdata->chg_configs[i].icl_step_size_ua,
				pdata->chg_configs[i].max_volt_uv,
				pdata->chg_configs[i].mpp_mdis_name ?
					pdata->chg_configs[i].mpp_mdis_name : "none");
		}
	} else {
chg_config_error:
		dev_info(dev, "Invalid chg_configs, fill in 0 with default");
		pdata->chg_configs_num = 1;
		pdata->chg_configs = devm_kzalloc(dev, sizeof(struct wlc_chg_config),
						  GFP_KERNEL);
		if (!pdata->chg_configs)
			return -ENOMEM;
		pdata->chg_configs[0].target_power_mode = MPP_POWERMODE_NOMINAL;
		pdata->chg_configs[0].mod_mode = ASK_MOD_MODE_ADAPTIVE_LOAD_MOD;
		pdata->chg_configs[0].mod_change_timing = ASK_MOD_TIMING_EARLY;
		pdata->chg_configs[0].init_cur_ua = GOOGLE_WLC_DEFAULT_STARTUP_UA;
		pdata->chg_configs[0].icl_rounding = ICL_TARGET_ROUND_UP_WITH_COMP;
		pdata->chg_configs[0].init_volt_uv = MV_TO_UV(GOOGLE_WLC_VOUT_MIN);
		pdata->chg_configs[0].auto_vout = true;
		pdata->chg_configs[0].charger_type = CHARGER_TYPE_BUCK;
		pdata->chg_configs[0].icl_step_size_ua = DEFAULT_DC_ICL_STEP;
		pdata->chg_configs[0].max_volt_uv = MV_TO_UV(MPP_NPM_MAX_VOLTAGE);
		dev_info(dev,
			"default chg_config: pwrmode:%s, mod: %s, mod timing: %d, icl_min: %d, icl_round: %d, init_volt: %d, auto_vout: %d, chg_type: %d, icl_step: %d, max_volt: %d",
			mpp_powermode_str[pdata->chg_configs[0].target_power_mode],
			ask_mod_str[pdata->chg_configs[0].mod_mode],
			pdata->chg_configs[0].mod_change_timing,
			pdata->chg_configs[0].init_cur_ua,
			pdata->chg_configs[0].icl_rounding,
			pdata->chg_configs[0].init_volt_uv,
			pdata->chg_configs[0].auto_vout,
			pdata->chg_configs[0].charger_type,
			pdata->chg_configs[0].icl_step_size_ua,
			pdata->chg_configs[0].max_volt_uv);
	}

	ret = of_property_read_u32(node, "google,wlc_iop_bpp_vout_tolerance_mv", &val);
	if (ret == 0)
		pdata->iop_bpp_vout_tolerance = val;

	ret = of_property_read_u32(node, "google,wlc-max-epp-power-mw", &val);
	if (ret == 0)
		pdata->wlc_epp_max_power_mw = val;
	else
		pdata->wlc_epp_max_power_mw = GOOGLE_WLC_EPP_MAX_POWER;

	return 0;
}

static int google_wlc_probe(struct i2c_client *client)
{
	struct device_node *dn, *of_node = client->dev.of_node;
	struct google_wlc_data *charger;
	struct google_wlc_platform_data *pdata = client->dev.platform_data;
	struct power_supply_config psy_cfg = {};
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	bool present_check;
	int ret;

	dev_info(&client->dev, "Probe start\n");
	ret = i2c_check_functionality(client->adapter,
				      I2C_FUNC_SMBUS_BYTE_DATA |
					      I2C_FUNC_SMBUS_WORD_DATA |
					      I2C_FUNC_SMBUS_I2C_BLOCK);
	if (ret < 0) {
		ret = i2c_get_functionality(client->adapter);
		dev_err(&client->dev, "I2C adapter not compatible %x\n", ret);
		return -EINVAL;
	}

	if (of_node) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = google_wlc_parse_dt(&client->dev, pdata);
		if (ret == -EPROBE_DEFER) {
			dev_info(&client->dev, "Defer probe\n");
			return ret;
		}
		if (ret) {
			dev_err(&client->dev, "Failed to parse dt\n");
			return ret;
		}
	}

	charger = devm_kzalloc(&client->dev, sizeof(*charger), GFP_KERNEL);
	if (charger == NULL)
		return -ENOMEM;

	charger->chip_id = id->driver_data;
	dev_info(&client->dev, "Chip ID: 0x%04x\n", charger->chip_id);

	i2c_set_clientdata(client, charger);
	charger->dev = &client->dev;
	charger->dev->init_name = "i2c-google_wlc";
	charger->client = client;
	charger->pdata = pdata;
	charger->chip = NULL;
	charger->count = 1;
	charger->online = 0;
	charger->last_capacity = -1;
	charger->iout_multiplier = 1;
	charger->disconnect_count = 0;
	charger->last_opfreq = 0;
	charger->enable_eoc_cloak = charger->pdata->eoc_cloak_en;
	charger->inlim_available = true;
	charger->fw_data.update_option = charger->pdata->fwupdate_option;
	charger->ask_mod_mode = ASK_MOD_MODE_ADAPTIVE_LOAD_MOD;
	charger->mpp_initialized = false;
	charger->output_chg_config = &charger->pdata->chg_configs[0];
	charger->mdis_pwr_limit = -1;
	charger->no_charger_switch_cloak = charger->pdata->no_charger_switch_cloak;
	charger->wlc_dc_xce_thresh = charger->pdata->wlc_dc_xce_thresh;
	charger->wlc_dc_max_vout_delta = charger->pdata->wlc_dc_max_vout_delta;
	charger->wlc_dc_max_pout_delta = charger->pdata->wlc_dc_max_pout_delta;
	charger->cloak_err_count_max = WLC_CLOAK_ERROR_COUNT_MAX;

	if (charger->pdata->mod_soc == 0)
		charger->mod_enable = true;
	if (!IS_ERR_OR_NULL(charger->pdata->mode_gpio))
		charger->gpio_mode = GPIO_VOL_HIGH;
	if (charger->pdata->cloak_ping_delay_ms > 0)
		charger->cloak_ping_delay_ms = charger->pdata->cloak_ping_delay_ms;

	mutex_init(&charger->io_lock);
	mutex_init(&charger->status_lock);
	mutex_init(&charger->csp_status_lock);
	mutex_init(&charger->eds_lock);
	mutex_init(&charger->fwupdate_lock);
	mutex_init(&charger->cmd_lock);
	mutex_init(&charger->stats_lock);
	mutex_init(&charger->completion_lock);
	mutex_init(&charger->ext_boost_lock);
	mutex_init(&charger->vdden_lock);

	init_completion(&charger->cloak_completion);
	init_completion(&charger->cal_renego_done);
	init_completion(&charger->cal_enter_done);
	init_completion(&charger->icl_ramp_done);
	init_completion(&charger->pwrmode_switch);
	init_completion(&charger->charger_disconnect);

	charger->debug_entry = debugfs_create_dir("google_wlc", 0);

	ret = google_wlc_chip_init(charger);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to init MPP chip: %d\n", ret);
		return ret;
	}

	if (!IS_ERR_OR_NULL(charger->pdata->ap5v_gpio))
		gpiod_set_value_cansleep(charger->pdata->ap5v_gpio, 1);

	psy_cfg.drv_data = charger;
	psy_cfg.of_node = charger->dev->of_node;
	charger->wlc_psy = devm_power_supply_register(
		charger->dev, &google_wlc_psy_desc.psy_dsc, &psy_cfg);
	if (IS_ERR(charger->wlc_psy)) {
		dev_err(&client->dev, "Fail to register supply: %d\n", ret);
		return PTR_ERR(charger->wlc_psy);
	}

	bin_attr_rxdata.size = charger->chip->rx_buf_size;
	bin_attr_txdata.size = charger->chip->tx_buf_size;
	ret = sysfs_create_group(&charger->dev->kobj, &google_wlc_attr_group);

	charger->log = logbuffer_register("wireless");
	if (IS_ERR(charger->log)) {
		ret = PTR_ERR(charger->log);
		dev_err(charger->dev,
			"failed to obtain logbuffer instance, ret=%d\n", ret);
		charger->log = NULL;
	}

	charger->fw_log = logbuffer_register("wlc_fw_update");
	if (IS_ERR(charger->fw_log)) {
		ret = PTR_ERR(charger->fw_log);
		dev_err(charger->dev, "failed to obtain logbuffer instance, ret=%d\n", ret);
		charger->fw_log = NULL;
	}

	INIT_DELAYED_WORK(&charger->psy_notifier_work, google_wlc_notifier_work);
	INIT_DELAYED_WORK(&charger->icl_ramp_work, google_wlc_icl_ramp_work);
	INIT_DELAYED_WORK(&charger->icl_ramp_timeout_work, google_wlc_icl_ramp_timeout_work);
	INIT_DELAYED_WORK(&charger->mpp_nego_timeout_work, google_wlc_mpp_nego_timeout_work);
	INIT_DELAYED_WORK(&charger->present_check_work, google_wlc_present_check_work);
	INIT_DELAYED_WORK(&charger->soc_work, google_wlc_soc_work);
	INIT_DELAYED_WORK(&charger->csp_enable_work, google_wlc_enable_csp_work);
	INIT_DELAYED_WORK(&charger->disconnect_work, google_wlc_disconnect_work);
	INIT_DELAYED_WORK(&charger->disable_work, google_wlc_disable_work);
	INIT_DELAYED_WORK(&charger->tx_work, google_wlc_tx_work);
	INIT_DELAYED_WORK(&charger->register_usecase_work, google_wlc_register_usecase_work);
	INIT_DELAYED_WORK(&charger->charge_stats_hda_work, google_wlc_charge_stats_hda_work);
	INIT_DELAYED_WORK(&charger->icl_ramp_target_work, google_wlc_icl_ramp_target_work);
	INIT_DELAYED_WORK(&charger->dc_power_limit_work, google_wlc_dc_power_limit_work);
	INIT_DELAYED_WORK(&charger->mpp25_init_work, google_wlc_mpp25_init_work);
	INIT_DELAYED_WORK(&charger->charging_init_work, google_wlc_charging_init_work);
	INIT_DELAYED_WORK(&charger->mpp25_timeout_work, google_mpp25_timeout_work);
	INIT_DELAYED_WORK(&charger->wlc_fw_update_work, google_wlc_fw_update_work);
	INIT_DELAYED_WORK(&charger->eds_work, google_wlc_eds_work);
	INIT_DELAYED_WORK(&charger->pla_ack_timeout_work, google_pla_ack_timeout_work);
	INIT_DELAYED_WORK(&charger->check_iop_timeout_work, google_check_iop_timeout_work);
	INIT_DELAYED_WORK(&charger->set_iop_vout_work, google_wlc_set_iop_vout_work);
	INIT_DELAYED_WORK(&charger->charger_switch_ready_work, google_charger_switch_ready_work);
	INIT_DELAYED_WORK(&charger->chg_stable_work, google_wlc_chg_stable_work);
	INIT_DELAYED_WORK(&charger->limit_reason_work, google_limit_reason_work);
	INIT_DELAYED_WORK(&charger->cloak_err_work, google_wlc_cloak_err_work);
	INIT_DELAYED_WORK(&charger->vdd_err_work, google_wlc_vdd_err_work);
	INIT_DELAYED_WORK(&charger->xid_work, google_wlc_xid_work);

	mutex_lock(&charger->stats_lock);
	google_wlc_stats_init(charger);
	mutex_unlock(&charger->stats_lock);

	charger->icl_ramp_target_votable = gvotable_create_int_election(
		NULL, gvotable_comparator_int_min,
		google_wlc_icl_ramp_vote_callback, charger);
	if (IS_ERR_OR_NULL(charger->icl_ramp_target_votable)) {
		dev_err(charger->dev, "failed to create icl ramp target votable\n");
		charger->icl_ramp_target_votable = NULL;
	} else {
		gvotable_set_vote2str(charger->icl_ramp_target_votable, gvotable_v2s_int);
		gvotable_election_set_name(charger->icl_ramp_target_votable, "WLC_PWR_TARGET");
	}

	charger->wlc_dc_power_votable = gvotable_create_int_election(
		NULL, gvotable_comparator_int_min,
		google_wlc_dc_power_vote_callback, charger);
	if (IS_ERR_OR_NULL(charger->wlc_dc_power_votable)) {
		dev_err(charger->dev, "failed to create wlc-dc power votable\n");
		charger->wlc_dc_power_votable = NULL;
	} else {
		gvotable_set_vote2str(charger->wlc_dc_power_votable, gvotable_v2s_int);
		gvotable_election_set_name(charger->wlc_dc_power_votable, "WLC_DC_PWR");
	}

	charger->wlc_disable_votable = gvotable_create_int_election(NULL,
						gvotable_comparator_int_max,
						google_wlc_wlc_disable_vote_callback, charger);
	if (IS_ERR_OR_NULL(charger->wlc_disable_votable)) {
		dev_err(charger->dev, "failed to create wlc disable votable\n");
		charger->wlc_disable_votable = NULL;
	} else {
		gvotable_set_vote2str(charger->wlc_disable_votable, gvotable_v2s_int);
		gvotable_election_set_name(charger->wlc_disable_votable, "WLC_DISABLE");
		gvotable_cast_long_vote(charger->wlc_disable_votable, WLC_VOTER,
					WLC_NOT_DISABLED, true);
	}

	mutex_lock(&charger->status_lock);
	charger->status = GOOGLE_WLC_STATUS_NOT_DETECTED;

	device_init_wakeup(charger->dev, true);
	charger->icl_ramp_ws = wakeup_source_register(NULL, "google_wlc_icl_ramp");
	charger->icl_ramp_timeout_ws = wakeup_source_register(NULL, "google_wlc_icl_ramp_timeout");
	charger->notifier_ws = wakeup_source_register(NULL, "google_wlc_notifier");
	charger->presence_ws = wakeup_source_register(NULL, "google_wlc_present_check");
	charger->disconnect_ws = wakeup_source_register(NULL, "google_wlc_disconnect_check");
	charger->fwupdate_ws = wakeup_source_register(NULL, "google_wlc_fwupdate");
	charger->mpp25_ws = wakeup_source_register(NULL, "google_mpp25");
	charger->init_charging_ws = wakeup_source_register(NULL, "google_wlc_init_charging");
	charger->wlc_disable_ws = wakeup_source_register(NULL, "google_wlc_disable");
	charger->icl_target_ws = wakeup_source_register(NULL, "google_wlc_icl_target");
	charger->eds_tx_ws = wakeup_source_register(NULL, "google_wlc_eds_tx");
	charger->limit_reason_ws = wakeup_source_register(NULL, "google_wlc_limit_reason");
	charger->xid_ws = wakeup_source_register(NULL, "google_wlc_xid");
	charger->chg_stable_ws = wakeup_source_register(NULL, "google_wlc_chg_stable");

	ret = devm_request_threaded_irq(
		&client->dev, charger->pdata->irq_int, NULL,
		google_wlc_irq_thread, IRQF_TRIGGER_LOW,
		"wlc-irq", charger);
	if (ret) {
		dev_err(&client->dev, "Failed to request IRQ\n");
		return ret;
	}

	if (charger->pdata->irq_hybrid >= 0) {
		ret = devm_request_threaded_irq(&client->dev, charger->pdata->irq_hybrid, NULL,
						google_wlc_hybrid_irq_thread,
						IRQF_SHARED | IRQF_ONESHOT,
						"wlc-hybrid-irq", charger);
		if (ret < 0)
			dev_warn(&client->dev, "Failed to request hybrid IRQ, ret: %d\n", ret);
	}
	present_check = google_wlc_confirm_online(charger);

	if (present_check) {
		charger->boot_on_wlc = true;
		google_wlc_set_status(charger, GOOGLE_WLC_STATUS_DETECTED);
		google_wlc_adjust_negotiated_power(charger);
	}
	mutex_unlock(&charger->status_lock);
	/* Check and clear interrupts */
	if (present_check)
		google_wlc_irq_thread(charger->pdata->irq_int, charger);

	enable_irq_wake(charger->pdata->irq_int);

#if IS_ENABLED(CONFIG_GPIOLIB)
	google_wlc_gpio_init(charger);
	charger->gpio.parent = &client->dev;
	/* balance of_node_put() in of_find_node_by_name() */
	of_node_get(client->dev.of_node);
	dn = of_find_node_by_name(client->dev.of_node,
				charger->gpio.label);
	if (!dn)
		dev_err(&client->dev, "Failed to find %s DT node\n",
			charger->gpio.label);

	charger->gpio.fwnode = of_node_to_fwnode(dn);

	ret = devm_gpiochip_add_data(&client->dev, &charger->gpio, charger);
	dev_info(&client->dev, "%d GPIOs registered ret:%d\n",
			charger->gpio.ngpio, ret);

#endif

	logbuffer_devlog(charger->log, charger->dev, "present check=%d",
			present_check);

	if (charger->pdata->batt_psy)
		schedule_delayed_work(&charger->soc_work, 0);

	/*
	 * Register notifier so we can detect changes on DC_IN
	 */
	charger->nb.notifier_call = google_wlc_notifier_cb;
	power_supply_reg_notifier(&charger->nb);

	if (!charger->boot_on_wlc) {
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, WLC_VOTER,
					GOOGLE_WLC_STARTUP_MW, true);
		google_wlc_set_dc_icl(charger, GOOGLE_WLC_DEFAULT_STARTUP_UA);
	} else {
		google_wlc_set_dc_icl(charger, GOOGLE_WLC_BOOTUP_UA);
	}
	if (!charger->dc_icl_votable)
		charger->dc_icl_votable = gvotable_election_get_handle(DC_ICL_VOTABLE_HANDLE);
	if (charger->dc_icl_votable)
		ret = gvotable_cast_int_vote(charger->dc_icl_votable, OCP_VOTER,
					     charger->pdata->soft_ocp_icl,
					     charger->pdata->soft_ocp_icl > 0);
	if (ret)
		dev_err(&client->dev, "Fail to set soft OCP ICL, ret=%d\n", ret);

	google_wlc_notifier_check_dc(charger);

	if (!charger->boot_on_wlc)
		gvotable_cast_long_vote(charger->icl_ramp_target_votable, CHG_ENABLE_VOTER,
					GOOGLE_WLC_CHG_SUSPEND_MW, true);

	schedule_delayed_work(&charger->register_usecase_work, 0);

	debugfs_create_file("ept", 0200, charger->debug_entry, charger, &debug_ept_fops);
	debugfs_create_file("inhibit", 0444, charger->debug_entry, charger, &debug_inhibit_fops);
	debugfs_create_file("online_disable", 0644, charger->debug_entry, charger,
			    &online_disable_fops);
	debugfs_create_u32("icl_ramp_disable", 0644, charger->debug_entry,
			   &charger->icl_ramp_disable);
	debugfs_create_u32("enable_i2c_debug", 0644, charger->debug_entry,
			   &charger->enable_i2c_debug);
	debugfs_create_file("auth_disable", 0644, charger->debug_entry,
			    charger, &auth_disable_fops);
	debugfs_create_u32("enable_eoc_cloak", 0644, charger->debug_entry,
			   &charger->enable_eoc_cloak);
	debugfs_create_u32("fast_mpp_ramp", 0644, charger->debug_entry,
			   &charger->fast_mpp_ramp);
	debugfs_create_file("skip_nego_power", 0644, charger->debug_entry,
			    charger, &skip_nego_power_fops);
	debugfs_create_u32("wlc_dc_skip_qi_ver", 0644, charger->debug_entry,
			   &charger->wlc_dc_skip_qi_ver);
	debugfs_create_u32("wlc_dc_skip_mated_q", 0644, charger->debug_entry,
			   &charger->wlc_dc_skip_mated_q);
	debugfs_create_u32("wlc_dc_skip_pot_pwr", 0644, charger->debug_entry,
			   &charger->wlc_dc_skip_pot_pwr);
	debugfs_create_u32("wlc_dc_skip_pwr_limit_check", 0644, charger->debug_entry,
			   &charger->wlc_dc_skip_pwr_limit_check);
	debugfs_create_u32("wlc_dc_skip_powermode", 0644, charger->debug_entry,
			   &charger->wlc_dc_skip_powermode);
	debugfs_create_u32("wlc_dc_skip_dploss", 0644, charger->debug_entry,
			   &charger->wlc_dc_skip_dploss);
	debugfs_create_u32("wlc_dc_skip_dploss_param", 0644, charger->debug_entry,
			   &charger->wlc_dc_skip_dploss_param);
	debugfs_create_u32("mpp25_dploss_cal4", 0644, charger->debug_entry,
			   &charger->mpp25_dploss_cal4);
	debugfs_create_u32("mpp25_dploss_cal4_volt", 0644, charger->debug_entry,
			   &charger->mpp25_dploss_cal4_volt);
	debugfs_create_file("vinv", 0444, charger->debug_entry, charger, &debug_vinv_fops);
	debugfs_create_u32("wlc_dc_max_volt", 0644, charger->debug_entry,
			   &charger->pdata->wlc_dc_max_voltage);
	debugfs_create_u32("qi22_write_mpla2", 0644, charger->debug_entry,
			   &charger->qi22_write_mpla2);
	debugfs_create_u16("mpla2_alpha_fm_itx", 0644, charger->debug_entry,
			   &charger->mpla2_alpha_fm_itx);
	debugfs_create_u32("mpla2_alpha_fm_vrect", 0644, charger->debug_entry,
			   &charger->mpla2_alpha_fm_vrect);
	debugfs_create_u16("mpla2_alpha_fm_irect", 0644, charger->debug_entry,
			   &charger->mpla2_alpha_fm_irect);
	debugfs_create_u32("wlc_dc_max_vout_delta", 0644, charger->debug_entry,
			   &charger->wlc_dc_max_vout_delta);
	debugfs_create_u32("wlc_dc_max_pout_delta", 0644, charger->debug_entry,
			   &charger->wlc_dc_max_pout_delta);
	debugfs_create_u32("wlc_dd_thres", 0644, charger->debug_entry,
			   &charger->mitigate_threshold);
	debugfs_create_file("ap5v_gpio", 0644, charger->debug_entry, charger,
						&ap5v_gpio_fops);
	debugfs_create_file("vdden_gpio", 0644, charger->debug_entry, charger,
						&vdden_gpio_fops);
	debugfs_create_file("det_gpio_status", 0444, charger->debug_entry, charger,
						&det_gpio_status_fops);
	debugfs_create_file("de_rf", 0644, charger->debug_entry, charger,
						&de_rf_fops);
	debugfs_create_file("de_qf", 0644, charger->debug_entry, charger,
						&de_qf_fops);
	debugfs_create_file("de_fod_n", 0644, charger->debug_entry, charger,
					    &de_fod_n_fops);
	debugfs_create_file("de_fod", 0644, charger->debug_entry, charger,
					    &de_fod_fops);
	debugfs_create_file("de_mpla", 0644, charger->debug_entry, charger,
						&de_mpla_fops);
	debugfs_create_file("de_rf_curr", 0644, charger->debug_entry, charger,
						&de_rf_curr_fops);
	debugfs_create_file("de_bpp_mdis_n", 0644, charger->debug_entry, charger,
						&de_bpp_mdis_n_fops);
	debugfs_create_file("de_bpp_mdis_pwr", 0644, charger->debug_entry, charger,
						&de_bpp_mdis_pwr_fops);
	debugfs_create_file("de_epp_mdis_n", 0644, charger->debug_entry, charger,
						&de_epp_mdis_n_fops);
	debugfs_create_file("de_epp_mdis_pwr", 0644, charger->debug_entry, charger,
						&de_epp_mdis_pwr_fops);
	debugfs_create_file("de_mpp_mdis_n", 0644, charger->debug_entry, charger,
						&de_mpp_mdis_n_fops);
	debugfs_create_file("de_mpp_mdis_pwr", 0644, charger->debug_entry, charger,
						&de_mpp_mdis_pwr_fops);
	debugfs_create_file("de_mpp_mdis_chg_idx", 0644, charger->debug_entry, charger,
						&de_mpp_mdis_chg_idx_fops);
	debugfs_create_file("de_wlc_dc_mdis_n", 0644, charger->debug_entry, charger,
						&de_wlc_dc_mdis_n_fops);
	debugfs_create_file("de_wlc_dc_mdis_fcc", 0644, charger->debug_entry, charger,
						&de_wlc_dc_mdis_fcc_fops);
	debugfs_create_file("de_dploss_steps", 0644, charger->debug_entry, charger,
						&de_dploss_steps_fops);
	debugfs_create_file("de_dploss_volts", 0644, charger->debug_entry, charger,
						&de_dploss_volts_fops);
	debugfs_create_file("de_num_dploss_points", 0644, charger->debug_entry, charger,
						&de_num_dploss_points_fops);
	debugfs_create_file("packet", 0644, charger->debug_entry, charger,
						&packet_fops);
	debugfs_create_bool("erase_fw", 0644, charger->debug_entry, &charger->fw_data.erase_fw);
	debugfs_create_file("fwupdate_state", 0444, charger->debug_entry, charger,
						&fwupdate_state_fops);
	debugfs_create_file("support_mpp25", 0644, charger->debug_entry, charger,
						&support_mpp25_fops);
	debugfs_create_u32("icl_target_rounding", 0644, charger->debug_entry,
			   &charger->icl_target_rounding_override);
	debugfs_create_u32("dploss_thresh", 0644, charger->debug_entry,
			   &charger->dploss_thresh);
	debugfs_create_file("qi_version_gpio", 0644, charger->debug_entry,
						charger, &qi_version_gpio_fops);
	debugfs_create_u32("cloak_ping_delay_ms", 0644, charger->debug_entry,
			   &charger->cloak_ping_delay_ms);
	debugfs_create_bool("stdby_mode", 0644, charger->debug_entry,
			    &charger->stdby_mode);
	debugfs_create_u32("no_charger_switch_cloak", 0644, charger->debug_entry,
			   &charger->no_charger_switch_cloak);
	debugfs_create_u32("wlc_dc_xce_thresh", 0644, charger->debug_entry,
			   &charger->wlc_dc_xce_thresh);
	debugfs_create_file("crc_check", 0644, charger->debug_entry, charger, &debug_crc_fops);
	debugfs_create_u32("wlc_iop_fail_max", 0644, charger->debug_entry,
			   &charger->iop_fail_count_max);
	debugfs_create_u32("wlc_cloak_err_max", 0644, charger->debug_entry,
			   &charger->cloak_err_count_max);

	dev_info(&client->dev, "Probe complete\n");

	if (is_fwtag_allow_update(charger))
		mod_delayed_work(system_wq, &charger->wlc_fw_update_work,
				 msecs_to_jiffies(WLC_FW_CHECK_TIMEOUT_MS));
	return 0;
}

static void google_wlc_remove(struct i2c_client *client)
{
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	free_irq(charger->pdata->irq_int, charger);
	if (charger->pdata->irq_hybrid >= 0)
		free_irq(charger->pdata->irq_hybrid, charger);
	power_supply_unreg_notifier(&charger->nb);
	cancel_delayed_work_sync(&charger->psy_notifier_work);
	cancel_delayed_work_sync(&charger->icl_ramp_work);
	cancel_delayed_work_sync(&charger->icl_ramp_timeout_work);
	cancel_delayed_work_sync(&charger->mpp_nego_timeout_work);
	cancel_delayed_work_sync(&charger->present_check_work);
	cancel_delayed_work_sync(&charger->soc_work);
	cancel_delayed_work_sync(&charger->csp_enable_work);
	cancel_delayed_work_sync(&charger->disconnect_work);
	cancel_delayed_work_sync(&charger->disable_work);
	cancel_delayed_work_sync(&charger->tx_work);
	cancel_delayed_work_sync(&charger->charge_stats_hda_work);
	cancel_delayed_work_sync(&charger->mpp25_init_work);
	cancel_delayed_work_sync(&charger->charging_init_work);
	cancel_delayed_work_sync(&charger->mpp25_timeout_work);
	cancel_delayed_work_sync(&charger->wlc_fw_update_work);
	cancel_delayed_work_sync(&charger->eds_work);
	cancel_delayed_work_sync(&charger->pla_ack_timeout_work);
	cancel_delayed_work_sync(&charger->check_iop_timeout_work);
	cancel_delayed_work_sync(&charger->set_iop_vout_work);
	cancel_delayed_work_sync(&charger->charger_switch_ready_work);
	cancel_delayed_work_sync(&charger->chg_stable_work);
	cancel_delayed_work_sync(&charger->limit_reason_work);
	cancel_delayed_work_sync(&charger->cloak_err_work);
	cancel_delayed_work_sync(&charger->vdd_err_work);
	cancel_delayed_work_sync(&charger->xid_work);
	if (!IS_ERR_OR_NULL(charger->pdata->batt_psy))
		power_supply_put(charger->pdata->batt_psy);
	if (!IS_ERR_OR_NULL(charger->chgr_psy))
		power_supply_put(charger->chgr_psy);
	device_init_wakeup(charger->dev, false);
	mutex_destroy(&charger->io_lock);
	mutex_destroy(&charger->status_lock);
	mutex_destroy(&charger->csp_status_lock);
	mutex_destroy(&charger->eds_lock);
	mutex_destroy(&charger->fwupdate_lock);
	mutex_destroy(&charger->cmd_lock);
	mutex_destroy(&charger->stats_lock);
	mutex_destroy(&charger->completion_lock);
	sysfs_remove_group(&charger->dev->kobj, &google_wlc_attr_group);
	gvotable_destroy_election(charger->icl_ramp_target_votable);
	gvotable_destroy_election(charger->wlc_disable_votable);
	gvotable_destroy_election(charger->wlc_dc_power_votable);
	logbuffer_unregister(charger->log);
	logbuffer_unregister(charger->fw_log);
	of_node_put(charger->dev->of_node);
	kfree(charger->i2c_debug_buf);
}

static void google_wlc_shutdown(struct i2c_client *client)
{
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	charger->shutdown_notify = true;
	dev_info(&client->dev, "%s notify\n", __func__);

	if (charger->pdata->inhibit_gpio >= 0)
		gpiod_set_value_cansleep(charger->pdata->inhibit_gpio, 1);

	free_irq(charger->pdata->irq_int, charger);
	if (charger->pdata->irq_hybrid >= 0)
		free_irq(charger->pdata->irq_hybrid, charger);
	power_supply_unreg_notifier(&charger->nb);
	cancel_delayed_work_sync(&charger->psy_notifier_work);
	cancel_delayed_work_sync(&charger->icl_ramp_work);
	cancel_delayed_work_sync(&charger->icl_ramp_timeout_work);
	cancel_delayed_work_sync(&charger->mpp_nego_timeout_work);
	cancel_delayed_work_sync(&charger->present_check_work);
	cancel_delayed_work_sync(&charger->soc_work);
	cancel_delayed_work_sync(&charger->csp_enable_work);
	cancel_delayed_work_sync(&charger->disconnect_work);
	cancel_delayed_work_sync(&charger->disable_work);
	cancel_delayed_work_sync(&charger->tx_work);
	cancel_delayed_work_sync(&charger->charge_stats_hda_work);
	cancel_delayed_work_sync(&charger->mpp25_init_work);
	cancel_delayed_work_sync(&charger->charging_init_work);
	cancel_delayed_work_sync(&charger->mpp25_timeout_work);
	cancel_delayed_work_sync(&charger->wlc_fw_update_work);
	cancel_delayed_work_sync(&charger->pla_ack_timeout_work);
	cancel_delayed_work_sync(&charger->check_iop_timeout_work);
	cancel_delayed_work_sync(&charger->charger_switch_ready_work);
	cancel_delayed_work_sync(&charger->chg_stable_work);
	cancel_delayed_work_sync(&charger->limit_reason_work);
	cancel_delayed_work_sync(&charger->cloak_err_work);
	cancel_delayed_work_sync(&charger->vdd_err_work);
	cancel_delayed_work_sync(&charger->xid_work);
	if (!IS_ERR_OR_NULL(charger->pdata->batt_psy))
		power_supply_put(charger->pdata->batt_psy);
	if (!IS_ERR_OR_NULL(charger->chgr_psy))
		power_supply_put(charger->chgr_psy);
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int google_wlc_pm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	/* Prevent i2c during suspend */
	mutex_lock(&charger->io_lock);

	return 0;
}

static int google_wlc_pm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct google_wlc_data *charger = i2c_get_clientdata(client);

	mutex_unlock(&charger->io_lock);

	return 0;
}
#endif

#if IS_ENABLED(CONFIG_PM)
static const struct dev_pm_ops google_wlc_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(google_wlc_pm_suspend, google_wlc_pm_resume)
};
#endif

static const struct i2c_device_id google_wlc_id_table[] = {
	{ "ra9582", RA9582_CHIP_ID },
	{ "cps4041", CPS4041_CHIP_ID},
	{ "wlc", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, google_wlc_id_table);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id google_wlc_match_table[] = {
	{ .compatible = "google,wlc",},
	{ .compatible = "renesas,ra9582",},
	{ .compatible = "cps,cps4041",},
	{},
};
#else
#define google_wlc_match_table NULL
#endif

static struct i2c_driver google_wlc_driver = {
	.driver = {
	    .name = "google_wlc",
	    .owner = THIS_MODULE,
	    .of_match_table = google_wlc_match_table,
	    .pm		= &google_wlc_pm_ops,
	    .probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = google_wlc_probe,
	.remove = google_wlc_remove,
	.shutdown = google_wlc_shutdown,
	.id_table = google_wlc_id_table,
};
module_i2c_driver(google_wlc_driver);
MODULE_DESCRIPTION("Google Wireless Power Receiver Driver");
MODULE_AUTHOR("Alice Sheng <alicesheng@google.com>");
MODULE_LICENSE("GPL");
