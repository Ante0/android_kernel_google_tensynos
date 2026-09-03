// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for PE26100 buck charger
 */


#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "google_bms.h"
#include "pe26100_driver.h"
#include "pe26100_buck_charger.h"
#include <misc/gvotable.h>

#pragma clang diagnostic ignored "-Wenum-conversion"
#pragma clang diagnostic ignored "-Wswitch"

#define PE26100_BUCK_4L_VIN_MAX_TA (19500000)

#define PE26100_BUCK_5V_TA_ILIM_START 2328000
#define PE26100_BUCK_5V_TA_ILIM_STOP 2856000
#define PE26100_BUCK_5V_TA_ILIM_STEP 48000

#define PE26100_BUCK_VBATT_STEP_SIZE 10000 /* 10mv step size */
#define PE26100_BUCK_VBATT_OFFSET 2560000 /* 2.56V offset */
#define PE26100_BUCK_MAX_VBATT_VAL 0xC7 /* 4.55V max */

#define PE26100_BUCK_IOUT_STEP_SIZE 30000

#define PE26100_BUCK_MAX_IOUT_REG_VAL 0xC8 /* 6.0A max */
#define PE26100_BUCK_MAX_IOUT_OCW_VAL 0xD2 /* 6.3A max */
#define PE26100_BUCK_MAX_IOUT_OCF_VAL 0xDC /* 6.6A max */

#define PE26100_BUCK_IIN_REG_STEP_SIZE 24000
#define PE26100_BUCK_MAX_IIN_REG_THRESHOLD 95 /* iin * 95% */
#define PE26100_BUCK_MAX_IIN_OCW_THRESHOLD 110 /* iin * 110% */
#define PE26100_BUCK_MAX_IIN_OCF_THRESHOLD 120 /* iin * 120% */

#define PE26100_BUCK_IIN_ML4_INIT_CURRENT 300000 /* 0.3A */

#define PE26100_BUCK_MAX_IIN_OCF_VAL 0xD0 /* 5.0A */
#define PE26100_BUCK_MAX_IIN_OCF_MAX_THRESHOLD 100 /* 5.0A * 100% = 5.0A */
#define PE26100_BUCK_MAX_IIN_OCW_MAX_THRESHOLD 97 /* 5.0A * 97% = 4.85A */
#define PE26100_BUCK_MAX_IIN_REG_MAX_THRESHOLD 93 /* 5.0A * 93% = 4.65A */

static int pe26100_buck_get_regulation_voltage_uv(struct pe26100_buck_charger *data,
						  int *voltage_uv,
						  const enum pe26100_buck_mode buck_mode)
{
	uint8_t value;
	int ret;

	if (!pe26100_buck_is_online(buck_mode)) {
		*voltage_uv = 0;
		return 0;
	}

	ret = pe26100_chg_reg_read(data->core, PE26100_CHG_VBATT_REG, &value);
	if (ret < 0)
		return ret;

	*voltage_uv = (value * PE26100_BUCK_VBATT_STEP_SIZE) + PE26100_BUCK_VBATT_OFFSET;
	return 0;
}

/* requires &data->charge_lock to be held */
static int pe26100_buck_apply_regulation_voltage_locked(struct pe26100_buck_charger *data)
{
	const int voltage_uv = data->fv_uv;
	uint8_t reg;

	if (voltage_uv < 0)
		reg = 0;
	else
		reg = min(((voltage_uv - PE26100_BUCK_VBATT_OFFSET) / PE26100_BUCK_VBATT_STEP_SIZE),
			  PE26100_BUCK_MAX_VBATT_VAL);

	return pe26100_chg_reg_write(data->core, PE26100_CHG_VBATT_REG, reg);
}

static int pe26100_buck_set_regulation_voltage(struct pe26100_buck_charger *data,
					       int voltage_uv)
{
	int ret = 0;

	mutex_lock(&data->charge_lock);

	data->fv_uv = voltage_uv;

	if (pe26100_buck_is_online(PE26100_BUCK_MODE_WIRED))
		ret = pe26100_buck_apply_regulation_voltage_locked(data);

	mutex_unlock(&data->charge_lock);

	return ret;
}

/* set charging current to 0 to disable charging (REGULATOR=off) */
/* requires &data->charge_lock to be held */
static int pe26100_buck_apply_charger_current_max_ua_locked(struct pe26100_buck_charger *data)
{
	uint8_t cc_max_reg, saved_cc_max_reg;
	const int current_ua = data->cc_max;
	int ret;

	ret = pe26100_chg_reg_read(data->core, PE26100_CHG_IOUT_REG, &saved_cc_max_reg);
	if (ret) {
		dev_err(data->dev, "Error reading PE26100_CHG_IOUT_REG %d\n", ret);
		return ret;
	}

	if (current_ua < 0)
		cc_max_reg = 0;
	else
		cc_max_reg = min(current_ua / PE26100_BUCK_IOUT_STEP_SIZE,
				 PE26100_BUCK_MAX_IOUT_REG_VAL);

	dev_dbg(data->dev, "setting cc_max:%d reg:0x%x ocw:0x%x ocf:0x%x\n", data->cc_max,
		cc_max_reg, PE26100_BUCK_MAX_IOUT_OCW_VAL, PE26100_BUCK_MAX_IOUT_OCF_VAL);

	if (cc_max_reg > saved_cc_max_reg) {
		ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IOUT_OC,
					    PE26100_BUCK_MAX_IOUT_OCF_VAL);
		if (ret == 0)
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IOUT_OC_WARN,
						    PE26100_BUCK_MAX_IOUT_OCW_VAL);
		if (ret == 0)
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IOUT_REG, cc_max_reg);
	} else {
		ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IOUT_REG, cc_max_reg);
		if (ret == 0)
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IOUT_OC_WARN,
						    PE26100_BUCK_MAX_IOUT_OCW_VAL);
		if (ret == 0)
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IOUT_OC,
						    PE26100_BUCK_MAX_IOUT_OCF_VAL);
	}

	return ret;
}

static int pe26100_buck_set_charger_current_max_ua(struct pe26100_buck_charger *data,
						   int current_ua)
{
	int ret = 0;

	mutex_lock(&data->charge_lock);

	current_ua = (current_ua == GBMS_MSC_FCC_CHARGE_OFF) ? 0 : current_ua;

	data->cc_max = current_ua;

	if (pe26100_buck_is_online(PE26100_BUCK_MODE_WIRED))
		ret = pe26100_buck_apply_charger_current_max_ua_locked(data);

	mutex_unlock(&data->charge_lock);

	return ret;
}

static int pe26100_buck_get_charger_current_max_ua(struct pe26100_buck_charger *data,
						   int *current_ua,
						   const enum pe26100_buck_mode buck_mode)
{
	uint8_t reg;
	int ret;

	if (!pe26100_buck_is_online(buck_mode)) {
		*current_ua = 0;
		return 0;
	}

	ret = pe26100_chg_reg_read(data->core, PE26100_CHG_IOUT_REG, &reg);
	if (ret < 0)
		return ret;

	*current_ua = reg * PE26100_BUCK_IOUT_STEP_SIZE;

	return 0;
}

static int pe26100_buck_get_iin_max_ua(struct pe26100_buck_charger *data, int *ilim_ua,
				       const enum pe26100_buck_mode buck_mode)
{
	uint8_t reg;
	int ret;

	if (!pe26100_buck_is_online(buck_mode)) {
		*ilim_ua = 0;
		return 0;
	}

	ret = pe26100_chg_reg_read(data->core, PE26100_CHG_IIN_REG, &reg);
	if (ret < 0)
		return ret;

	*ilim_ua = reg * PE26100_BUCK_IIN_REG_STEP_SIZE;

	return 0;
}

/* requires &data->charge_lock to be held */
static int pe26100_buck_apply_ilim_max_ua_locked(struct pe26100_buck_charger *data,
						 const enum pe26100_buck_mode buck_mode)
{
	int ret, saved_ilim;
	uint8_t ilim_reg, ilim_ocw_reg, ilim_ocf_reg, saved_ilim_reg;
	int ilim_threshold = PE26100_BUCK_MAX_IIN_REG_THRESHOLD;
	bool use_aicl = false;

	switch (buck_mode) {
	case PE26100_BUCK_MODE_WIRED:
		if (data->input_uv < PE26100_BUCK_MAX_INPUT_VOLTAGE) {
			saved_ilim = PE26100_BUCK_5V_TA_ILIM_START + data->aicl_ilim_offset;
			use_aicl = true;
		} else {
			saved_ilim = data->ilim;
		}
		break;
	case PE26100_BUCK_MODE_WIRELESS:
		saved_ilim = data->init_complete ? data->wlc_ilim :
			     PE26100_BUCK_IIN_ML4_INIT_CURRENT;
		ilim_threshold = 100;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ret = pe26100_chg_reg_read(data->core, PE26100_CHG_IIN_REG, &saved_ilim_reg);
	if (ret) {
		dev_err(data->dev, "Error reading PE26100_CHG_IIN_REG %d\n", ret);
		return ret;
	}

	if (saved_ilim < 0) {
		ilim_reg = 0;
		ilim_ocw_reg = 0;
		ilim_ocf_reg = 0;
	} else if (use_aicl) {
		ilim_reg = saved_ilim / PE26100_BUCK_IIN_REG_STEP_SIZE;
		/* ocw and ocf configured to use 3A in 5V TA case */
		ilim_ocw_reg = data->ilim / PE26100_BUCK_IIN_REG_STEP_SIZE *
			       PE26100_BUCK_MAX_IIN_OCW_THRESHOLD / 100;
		ilim_ocf_reg = data->ilim / PE26100_BUCK_IIN_REG_STEP_SIZE *
			       PE26100_BUCK_MAX_IIN_OCF_THRESHOLD / 100;
	} else {
		ilim_reg = min((saved_ilim / PE26100_BUCK_IIN_REG_STEP_SIZE *
				ilim_threshold / 100),
				(PE26100_BUCK_MAX_IIN_OCF_VAL *
				 PE26100_BUCK_MAX_IIN_REG_MAX_THRESHOLD / 100));
		ilim_ocw_reg = min((saved_ilim / PE26100_BUCK_IIN_REG_STEP_SIZE *
				    PE26100_BUCK_MAX_IIN_OCW_THRESHOLD / 100),
				   (PE26100_BUCK_MAX_IIN_OCF_VAL *
				    PE26100_BUCK_MAX_IIN_OCW_MAX_THRESHOLD / 100));
		ilim_ocf_reg = min((saved_ilim / PE26100_BUCK_IIN_REG_STEP_SIZE *
				   PE26100_BUCK_MAX_IIN_OCF_THRESHOLD / 100),
				   (PE26100_BUCK_MAX_IIN_OCF_VAL *
				    PE26100_BUCK_MAX_IIN_OCF_MAX_THRESHOLD / 100));
	}

	dev_dbg(data->dev, "saved_ilim:%d ilim_reg:0x%x ilim_ocw_reg:0x%x ilim_ocf_reg:0x%x aicl_ilim_offset:%d\n",
		 saved_ilim, ilim_reg, ilim_ocw_reg, ilim_ocf_reg, data->aicl_ilim_offset);

	if (ilim_reg > saved_ilim_reg) {
		if (buck_mode == PE26100_BUCK_MODE_WIRED)
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IIN_OC, ilim_ocf_reg);
		if (ret == 0)
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IIN_OC_WARN,
						    ilim_ocw_reg);
		if (ret == 0)
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IIN_REG, ilim_reg);
	} else {
		ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IIN_REG, ilim_reg);
		if (ret == 0)
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IIN_OC_WARN,
						    ilim_ocw_reg);
		if ((buck_mode == PE26100_BUCK_MODE_WIRED) && (ret == 0))
			ret = pe26100_chg_reg_write(data->core, PE26100_CHG_IIN_OC, ilim_ocf_reg);
	}

	return ret;
}

/* enable autoibus and charger mode */
static int pe26100_buck_set_ilim_max_ua(struct pe26100_buck_charger *data, int ilim_ua,
					const enum pe26100_buck_mode buck_mode)
{
	int ret = 0;

	mutex_lock(&data->charge_lock);

	switch (buck_mode) {
	case PE26100_BUCK_MODE_WIRED:
		data->ilim = ilim_ua;
		break;
	case PE26100_BUCK_MODE_WIRELESS:
		data->wlc_ilim = ilim_ua;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto unlock;
	}

	if (pe26100_buck_is_online(buck_mode))
		ret = pe26100_buck_apply_ilim_max_ua_locked(data, buck_mode);
unlock:
	mutex_unlock(&data->charge_lock);

	return ret;
}

static int pe26100_buck_get_charge_type(struct pe26100_buck_charger *data)
{
	int ret;
	uint8_t reg;

	if (!pe26100_buck_is_online(PE26100_BUCK_MODE_WIRED) &&
	    !pe26100_buck_is_online(PE26100_BUCK_MODE_WIRELESS))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	ret = pe26100_chg_reg_read(data->core, PE26100_CHG_IC_STATUS2, &reg);
	if (ret < 0)
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;

	if (!(reg & PE26100_CHG_IC_STATUS2_CC_CV_BIT))
		return POWER_SUPPLY_CHARGE_TYPE_TAPER_EXT;

	return POWER_SUPPLY_CHARGE_TYPE_FAST;
}

static int pe26100_buck_get_status(struct pe26100_buck_charger *data)
{
	return pe26100_buck_is_online(PE26100_BUCK_MODE_WIRED) ||
	       pe26100_buck_is_online(PE26100_BUCK_MODE_WIRELESS) ?
	       POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int pe26100_buck_read_vbatt(struct pe26100_buck_charger *data, int *vbatt,
				   enum pe26100_buck_mode buck_mode)
{
	if (!pe26100_buck_is_online(buck_mode)) {
		*vbatt = 0;
		return 0;
	}

	return pe26100_chg_read_vbatt(data->core, vbatt);
}

static int pe26100_buck_current_now(struct pe26100_buck_charger *data, int *iic,
				    enum pe26100_buck_mode buck_mode)
{
	if (!pe26100_buck_is_online(buck_mode)) {
		*iic = 0;
		return 0;
	}

	return pe26100_chg_current_now(data->core, iic, PE26100_CHG_MODE_BUCK);
}

/* -------------------------------------------------------------------------------------------*/

static int pe26100_buck_psy_is_writeable(struct power_supply *psy,
					 enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX: /* input voltage limit */
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static int pe26100_buck_psy_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *pval)
{
	const enum pe26100_buck_mode buck_mode = PE26100_BUCK_MODE_WIRED;
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int rc, ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		pval->intval = pe26100_buck_get_charge_type(data);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = pe26100_buck_get_charger_current_max_ua(data, &pval->intval, buck_mode);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		pval->intval = data->input_uv;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = pe26100_buck_get_regulation_voltage_uv(data, &pval->intval, buck_mode);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		pval->intval = pe26100_chg_is_online(data->core, PE26100_CHG_MODE_BUCK);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		pval->intval = pe26100_chg_is_online(data->core, PE26100_CHG_MODE_BUCK);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = pe26100_buck_get_iin_max_ua(data, &pval->intval, buck_mode);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		pval->intval = pe26100_buck_get_status(data);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = pe26100_buck_read_vbatt(data, &pval->intval, buck_mode);
		if (rc < 0)
			pval->intval = rc;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = pe26100_buck_current_now(data, &pval->intval, buck_mode);
		if (rc < 0)
			pval->intval = rc;
		break;
	default:
		dev_dbg(data->dev, "property (%d) unsupported.\n", psp);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int pe26100_buck_psy_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *pval)
{
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int ret = 0;
	bool changed = false;
	int input_uv;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = pe26100_buck_set_ilim_max_ua(data, pval->intval, PE26100_BUCK_MODE_WIRED);
		dev_dbg(data->dev, "%s: icl=%d (%d)\n", __func__, pval->intval, ret);
		break;
	/* Charge current is set to 0 to EOC */
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	{
		ret = pe26100_buck_set_charger_current_max_ua(data, pval->intval);
		dev_dbg(data->dev, "%s: charge_current=%d (%d)\n",
			__func__, pval->intval, ret);
	}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		input_uv = min(pval->intval, PE26100_BUCK_MAX_INPUT_VOLTAGE);

		changed = (data->input_uv != input_uv);
		data->input_uv = input_uv;
		if (changed && (data->input_uv == PE26100_BUCK_MAX_INPUT_VOLTAGE)) {
			pe26100_buck_charger_schedule_aicl_work(false);

			/* re-apply ilim in case TA voltage increased above AICL threshold */
			ret = pe26100_buck_apply_ilim_max_ua_locked(data, PE26100_BUCK_MODE_WIRED);
			if (ret)
				dev_err(data->dev, "Error applying ilim:%d\n", ret);
		}
		dev_dbg(data->dev, "%s: input_voltage=%d (applied=%d)\n", __func__, pval->intval,
			input_uv);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = pe26100_buck_set_regulation_voltage(data, pval->intval);
		dev_dbg(data->dev, "%s: charge_voltage=%d (%d)\n",
			__func__, pval->intval, ret);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		dev_dbg(data->dev, "%s: online=%d (%d)\n",
			__func__, pval->intval, ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (changed && pe26100_buck_is_online(PE26100_BUCK_MODE_WIRED))
		power_supply_changed(data->psy);

	return ret;
}

/* -------------------------------------------------------------------------------------------*/

static int pe26100_buck_get_chg_chgr_state(struct pe26100_buck_charger *data,
					   union gbms_charger_state *chg_state)
{
	int vchrg, ret;

	chg_state->v = 0;
	chg_state->f.chg_status = pe26100_buck_get_status(data);
	chg_state->f.chg_type = pe26100_buck_get_charge_type(data);
	chg_state->f.flags = gbms_gen_chg_flags(chg_state->f.chg_status,
						chg_state->f.chg_type);

	ret = pe26100_chg_read_vbatt(data->core, &vchrg);
	if (!ret)
		chg_state->f.vchrg = vchrg / 1000;

	if (chg_state->f.chg_status != POWER_SUPPLY_STATUS_NOT_CHARGING) {
		int rc, iin;

		rc = pe26100_buck_get_iin_max_ua(data, &iin, data->cur_buck_mode);
		if (!ret)
			chg_state->f.icl = iin / 1000;
	}

	return 0;
}

static int pe26100_buck_set_charge_enabled(struct pe26100_buck_charger *data, bool enable)
{

	/* ->charge_done is reset in max77779_enable_sw_recharge() */
	dev_dbg(data->dev, "%s enabled=%d\n", __func__, enable);

	return pe26100_chg_set_charge_enabled(data->core, enable, PE26100_CHG_MODE_BUCK);
}

static int pe26100_buck_gbms_psy_set_property(struct power_supply *psy,
					      enum gbms_property psp,
					      const union gbms_propval *pval)
{
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	/* called from google_cpm when switching chargers */
	case GBMS_PROP_CHARGING_ENABLED:
		ret = pe26100_buck_set_charge_enabled(data, pval->prop.intval);
		dev_dbg(data->dev, "%s: charging_enabled=%d (%d)\n",
			__func__, pval->prop.intval, ret);
		break;
	/* called from google_charger on disconnect */
	case GBMS_PROP_CHARGE_DISABLE:
		ret = pe26100_chg_set_charge_disabled(data->core, pval->prop.intval);
		pe26100_buck_vote_dc_avail(0, 1);
		dev_dbg(data->dev, "%s: charge_disable=%d (%d)\n",
			__func__, pval->prop.intval, ret);
		break;
	default:
		dev_dbg(data->dev, "%s: route to pe26100_buck_psy_set_property, psp:%d\n", __func__,
			psp);
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int pe26100_buck_gbms_psy_get_property(struct power_supply *psy,
					      enum gbms_property psp,
					      union gbms_propval *pval)
{
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	union gbms_charger_state chg_state;
	int rc, ret = 0;

	switch (psp) {
	case GBMS_PROP_CHARGE_DISABLE:
		rc = pe26100_chg_get_charge_enabled(data->core, &pval->prop.intval,
						    PE26100_CHG_MODE_BUCK);
		if (rc == 0)
			pval->prop.intval = !pval->prop.intval;
		else
			pval->prop.intval = rc;
		break;
	case GBMS_PROP_CHARGING_ENABLED:
		ret = pe26100_chg_get_charge_enabled(data->core, &pval->prop.intval,
						     PE26100_CHG_MODE_BUCK);
		break;
	case GBMS_PROP_CHARGE_CHARGER_STATE:
		rc = pe26100_buck_get_chg_chgr_state(data, &chg_state);
		if (rc == 0)
			pval->int64val = chg_state.v;
		break;
	case GBMS_PROP_INPUT_CURRENT_LIMITED:
		pval->prop.intval = false;
		break;
	default:
		dev_dbg(data->dev, "%s: route to pe26100_buck_psy_get_property, psp:%d\n", __func__,
			psp);
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int pe26100_buck_gbms_psy_is_writeable(struct power_supply *psy,
					      enum gbms_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX: /* input voltage limit */
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case GBMS_PROP_CHARGING_ENABLED:
	case GBMS_PROP_CHARGE_DISABLE:
		return 1;
	default:
		break;
	}

	return 0;
}

/*
 * TODO: POWER_SUPPLY_PROP_RERUN_AICL, POWER_SUPPLY_PROP_TEMP
 * POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX
 * POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX
 */
static enum power_supply_property pe26100_buck_psy_props[] = {
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,		/* input max_voltage */
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
};

static struct gbms_desc pe26100_buck_psy_desc = {
	.psy_dsc.name = "pe26100-buck-charger",
	.psy_dsc.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.psy_dsc.properties = pe26100_buck_psy_props,
	.psy_dsc.num_properties = ARRAY_SIZE(pe26100_buck_psy_props),
	.psy_dsc.get_property = pe26100_buck_psy_get_property,
	.psy_dsc.set_property = pe26100_buck_psy_set_property,
	.psy_dsc.property_is_writeable = pe26100_buck_psy_is_writeable,
	.get_property = pe26100_buck_gbms_psy_get_property,
	.set_property = pe26100_buck_gbms_psy_set_property,
	.property_is_writeable = pe26100_buck_gbms_psy_is_writeable,
	.forward = true,
};

/* -------------------------------------------------------------------------------------------*/

void pe26100_buck_charger_schedule_aicl_work(bool enable)
{
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int ret;
	bool relax = false;

	mutex_lock(&data->charge_lock);
	if (enable && !data->aicl_enabled) {
		union power_supply_propval val = {0};
		uint8_t reg;

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
		if (ret)
			dev_err(data->dev, "Unable to read voltage_max:%d\n", ret);
		else if (val.intval < PE26100_BUCK_MAX_INPUT_VOLTAGE) {
			/*
			 * linux interrupt still disabled here
			 * Rest of WARN are not handled here but should retrigger again later
			 */
			ret = pe26100_chg_reg_read(data->core, PE26100_CHG_FLT_STATUS1, &reg);
			if (ret || reg) {
				dev_err(data->dev, "Error FAULT encountered ret:%d reg:%x\n", ret,
					reg);
				pe26100_buck_vote_dc_avail(GBMS_ALL_SEC_CHG_DISABLED, 0);
				goto unlock;
			}
			ret = pe26100_chg_reg_read(data->core, PE26100_CHG_WARN_STATUS1, &reg);
			if (ret || reg & PE26100_CHG_WARN_STATUS1_VIN_UV_WARN_MASK) {
				dev_warn(data->dev, "Error UV WARN not starting AICL ret:%d reg:%x\n",
					 ret, reg);
				pe26100_chg_reg_update(data->core, PE26100_CHG_WARN_MASK1,
					     PE26100_CHG_WARN_MASK1_VIN_UV_WARN_MASK,
					     PE26100_CHG_WARN_MASK1_VIN_UV_WARN_MASK);
				goto unlock;
			}
			__pm_stay_awake(data->aicl_wake_lock);
			data->aicl_enabled = true;
			data->aicl_status = PE26100_BUCK_AICL_STATUS_RUNNING;
			mod_delayed_work(system_wq, &data->aicl_work, 0);
			dev_info(data->dev, "AICL enabled\n");
		}
	} else if (!enable && data->aicl_enabled) {
		cancel_delayed_work(&data->aicl_work);
		data->aicl_ilim_offset = 0;
		data->aicl_enabled = false;
		data->aicl_status = PE26100_BUCK_AICL_STATUS_DEFAULT;
		relax = true;
		dev_info(data->dev, "AICL disabled\n");
	}
unlock:
	mutex_unlock(&data->charge_lock);

	if (relax)
		__pm_relax(data->aicl_wake_lock);
}
EXPORT_SYMBOL_GPL(pe26100_buck_charger_schedule_aicl_work);

int pe26100_buck_vote_dc_avail(int vote, int enable)
{
	int ret = 0;
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);

	if (!enable)
		pe26100_dump_all_regs(data->core);

	if (!data->dc_avail_votable)
		data->dc_avail_votable = gvotable_election_get_handle(VOTABLE_DC_CHG_AVAIL);

	if (data->dc_avail_votable) {
		ret = gvotable_cast_int_vote(data->dc_avail_votable, "PE26100-BUCK",
					     vote, !enable);
		if (ret < 0)
			dev_err(data->dev, "Unable to cast vote for DC Chg avail (%d)\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_buck_vote_dc_avail);

int pe26100_buck_is_online(const enum pe26100_buck_mode buck_mode)
{
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	bool online;

	mutex_lock(&data->mode_lock);
	online = data->cur_buck_mode == buck_mode;
	mutex_unlock(&data->mode_lock);

	return online;
}
EXPORT_SYMBOL_GPL(pe26100_buck_is_online);

int pe26100_buck_set_online(bool online, const enum pe26100_buck_mode buck_mode)
{
	int ret = 0;
	enum pe26100_buck_mode cur_mode;
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);

	mutex_lock(&data->mode_lock);
	cur_mode = data->cur_buck_mode;

	if ((cur_mode == buck_mode) || (cur_mode == PE26100_BUCK_MODE_INVALID)) {
		data->cur_buck_mode = online ? buck_mode : PE26100_BUCK_MODE_INVALID;
		goto unlock;
	}

	dev_err(data->dev, "%s: Error: mode is already set cur_mode:%d mode:%d online:%d\n",
		__func__, cur_mode, buck_mode, online);
	ret = -EPERM;

unlock:
	mutex_unlock(&data->mode_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_buck_set_online);

/* set charging current to 0 to disable charging (REGULATOR=off) */
int pe26100_buck_apply_charger_current_max_ua(void)
{
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int ret;

	mutex_lock(&data->charge_lock);
	ret = pe26100_buck_apply_charger_current_max_ua_locked(data);
	mutex_unlock(&data->charge_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_buck_apply_charger_current_max_ua);

int pe26100_buck_apply_regulation_voltage(void)
{
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int ret;

	mutex_lock(&data->charge_lock);
	ret = pe26100_buck_apply_regulation_voltage_locked(data);
	mutex_unlock(&data->charge_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_buck_apply_regulation_voltage);

int pe26100_buck_apply_ilim_max_ua(const enum pe26100_buck_mode buck_mode)
{
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int ret;

	mutex_lock(&data->charge_lock);
	ret = pe26100_buck_apply_ilim_max_ua_locked(data, buck_mode);
	mutex_unlock(&data->charge_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_buck_apply_ilim_max_ua);

void pe26100_buck_enable_irq(bool enable)
{
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);

	dev_dbg(data->dev, "%s: enable:%d->%d\n", __func__, data->irq_enabled, enable);

	if (enable && !data->irq_enabled) {
		enable_irq(data->irq_int);
		enable_irq_wake(data->irq_int);
		data->irq_enabled = true;
	} else if (!enable && data->irq_enabled) {
		disable_irq_wake(data->irq_int);
		disable_irq(data->irq_int);
		data->irq_enabled = false;
	}
}
EXPORT_SYMBOL_GPL(pe26100_buck_enable_irq);

void pe26100_buck_set_init_complete(bool complete)
{
	struct power_supply *psy = power_supply_get_by_name(pe26100_buck_psy_desc.psy_dsc.name);
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);

	mutex_lock(&data->charge_lock);
	data->init_complete = complete;
	mutex_unlock(&data->charge_lock);
}
EXPORT_SYMBOL_GPL(pe26100_buck_set_init_complete);

/*
 * requires mutex_lock(&data->charge_lock)
 * return: 1 = changed, 0 = unchanged
 */
static int pe26100_buck_adjust_aicl_offset(struct pe26100_buck_charger *data, int ilim_offset)
{
	const int next_ilim = PE26100_BUCK_5V_TA_ILIM_START + data->aicl_ilim_offset + ilim_offset;

	if (ilim_offset == 0 || (next_ilim < PE26100_BUCK_5V_TA_ILIM_START) ||
	    (next_ilim > PE26100_BUCK_5V_TA_ILIM_STOP))
		return 0;

	data->aicl_ilim_offset += ilim_offset;
	pe26100_buck_apply_ilim_max_ua_locked(data, PE26100_BUCK_MODE_WIRED);

	return 1;
}

static void pe26100_buck_charger_aicl_work(struct work_struct *work)
{
	struct pe26100_buck_charger *data = container_of(work, struct pe26100_buck_charger,
							 aicl_work.work);
	int vin_adc, ret;
	bool reschedule = false;

	ret = pe26100_chg_read_vin(data->core, &vin_adc);
	if (ret)
		dev_warn(data->dev, "Error reading vin_adc ret:%d\n", ret);

	mutex_lock(&data->charge_lock);
	if (data->aicl_status != PE26100_BUCK_AICL_STATUS_RUNNING)
		goto unlock;
	reschedule = pe26100_buck_adjust_aicl_offset(data, PE26100_BUCK_5V_TA_ILIM_STEP);

	dev_info(data->dev, "Running AICL work vin:%d aicl_offset:%d\n", vin_adc,
		 data->aicl_ilim_offset);

	if (reschedule) {
		mod_delayed_work(system_wq, &data->aicl_work, /*  100ms for ES7, 200ms for ES8 */
				 msecs_to_jiffies(pe26100_is_es8_compat(data->core) ? 200 : 100));
	} else {
		ret = pe26100_chg_reg_update(data->core, PE26100_CHG_WARN_MASK1,
					     PE26100_CHG_WARN_MASK1_VIN_UV_WARN_MASK,
					     PE26100_CHG_WARN_MASK1_VIN_UV_WARN_MASK);
		if (ret)
			dev_warn(data->dev, "Error writing to vin mask ret:%d\n", ret);
		data->aicl_status = PE26100_BUCK_AICL_STATUS_COMPLETE;
	}

unlock:
	mutex_unlock(&data->charge_lock);
}

static int pe26100_wlcin_set_icl(struct pe26100_buck_charger *data, int value)
{
	const bool mdis_triggered = (value == GOOGLE_WLCIN_MDIS_DISABLE);

	value = mdis_triggered ? 0 : value;

	return pe26100_buck_set_ilim_max_ua(data, value, PE26100_BUCK_MODE_WIRELESS);
}

/* -------------------------------------------------------------------------------------------*/

static int pe26100_buck_wcin_get_prop(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *pval)
{
	const enum pe26100_buck_mode buck_mode = PE26100_BUCK_MODE_WIRELESS;
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_PRESENT:
		/* The main charger controls the online/present status of WLC */
		pval->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = pe26100_buck_get_regulation_voltage_uv(data, &pval->intval, buck_mode);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = pe26100_buck_get_iin_max_ua(data, &pval->intval, buck_mode);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = pe26100_buck_read_vbatt(data, &pval->intval, buck_mode);
		if (rc < 0)
			pval->intval = rc;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		rc = pe26100_buck_current_now(data, &pval->intval, buck_mode);
		if (rc < 0)
			pval->intval = rc;
		break;
	default:
		return -EINVAL;
	}

	if (rc < 0) {
		dev_dbg(data->dev, "Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int pe26100_buck_wcin_set_prop(struct power_supply *psy,
				      enum power_supply_property psp,
				      const union power_supply_propval *val)
{
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = pe26100_wlcin_set_icl(data, val->intval);
		dev_dbg(data->dev, "%s: icl=%d (%d)\n", __func__, val->intval, ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int pe26100_buck_gbms_wcin_get_prop(struct power_supply *psy,
					   enum gbms_property psp,
					   union gbms_propval *val)
{
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);

	dev_dbg(data->dev, "%s: route to pe26100_buck_wcin_get_prop, psp:%d\n", __func__, psp);

	return -ENODATA;
}

static int pe26100_buck_gbms_wcin_set_prop(struct power_supply *psy,
					   enum gbms_property psp,
					   const union gbms_propval *val)
{
	struct pe26100_buck_charger *data = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	/* called from google_cpm when switching chargers */
	case GBMS_PROP_CHARGING_ENABLED:
		rc = pe26100_chg_set_charge_enabled(data->core, val->prop.intval,
						    PE26100_CHG_MODE_BUCK);
		dev_dbg(data->dev, "%s: charging_enabled=%d (%d)\n",
			__func__, val->prop.intval, rc);
		break;
	case GBMS_PROP_CHARGE_DISABLE:
		/* main charger handles wlc disable flow*/
		break;
	default:
		dev_dbg(data->dev, "%s: route to pe26100_buck_wcin_set_prop, psp:%d\n", __func__,
			psp);
		return -ENODATA;
	}

	return rc;
}

static struct gbms_desc pe26100_buck_wcin_psy_desc = {
	.psy_dsc.name = "wlcin-pe26100",
	.psy_dsc.type = POWER_SUPPLY_TYPE_WIRELESS,
	.psy_dsc.properties = google_wcin_props,
	.psy_dsc.num_properties = GOOGLE_WLCIN_PROP_SIZE,
	.psy_dsc.get_property = pe26100_buck_wcin_get_prop,
	.psy_dsc.set_property = pe26100_buck_wcin_set_prop,
	.psy_dsc.property_is_writeable = google_wcin_mains_prop_is_writeable,
	.get_property = pe26100_buck_gbms_wcin_get_prop,
	.set_property = pe26100_buck_gbms_wcin_set_prop,
	.property_is_writeable = gbms_wcin_mains_prop_is_writeable,
	.forward = true,
};

static int pe26100_buck_init_wcin_psy(struct pe26100_buck_charger *data)
{
	static char *wlcin_mains_name[] = { GOOGLE_WLCIN_MAINS_NAME };
	struct power_supply_config chgr_psy_cfg = { 0 };
	struct device *dev = data->dev;
	int ret;

	if (!data->wcin_name) {
		ret = of_property_read_string(dev->of_node, "pe26100,wlcin-psy-name",
					      &data->wcin_name);
		if (ret == 0) {
			pe26100_buck_wcin_psy_desc.psy_dsc.name = devm_kstrdup(dev, data->wcin_name,
									       GFP_KERNEL);
			if (!pe26100_buck_wcin_psy_desc.psy_dsc.name)
				return -ENOMEM;
		} else {
			data->wcin_name = pe26100_buck_wcin_psy_desc.psy_dsc.name;
		}
	}

	if (!data->wcin_psy) {
		chgr_psy_cfg.drv_data = data;
		chgr_psy_cfg.of_node = dev->of_node;
		chgr_psy_cfg.supplied_to = wlcin_mains_name;
		chgr_psy_cfg.num_supplicants = ARRAY_SIZE(wlcin_mains_name);

		data->wcin_psy = devm_power_supply_register(dev,
							    &pe26100_buck_wcin_psy_desc.psy_dsc,
							    &chgr_psy_cfg);
		if (IS_ERR(data->wcin_psy)) {
			dev_err(dev, "Failed to register psy rc = %ld\n",
				PTR_ERR(data->wcin_psy));
			return PTR_ERR(data->wcin_psy);
		}
	}

	return 0;
}

/* -------------------------------------------------------------------------------------------*/


static void pe26100_buck_handle_irq_vin_uv(struct pe26100_buck_charger *data, bool vin_uv_triggered)
{
	int ret;

	if (!vin_uv_triggered)
		return;

	mutex_lock(&data->charge_lock);
	if (!data->aicl_enabled || data->aicl_status != PE26100_BUCK_AICL_STATUS_RUNNING)
		goto unlock;

	data->aicl_status = PE26100_BUCK_AICL_STATUS_CANCELLED;
	pe26100_buck_adjust_aicl_offset(data, -PE26100_BUCK_5V_TA_ILIM_STEP);
	cancel_delayed_work(&data->aicl_work);

	/* mask all when done */
	ret = pe26100_chg_reg_write(data->core, PE26100_CHG_WARN_MASK1, 0xFF);
	if (ret)
		dev_warn(data->dev, "Error writing to vin mask ret:%d\n", ret);

unlock:
	mutex_unlock(&data->charge_lock);
}

static void pe26100_buck_save_irq_state(struct pe26100_buck_charger *data, uint8_t *reg)
{
	data->irq_data = 0;

	if (reg[0] & PE26100_CHG_FLT_STATUS1_VIN_UV_MASK)
		data->irq_data |= GBMS_IRQ_VIN_UV_MASK;

	if (reg[0] & PE26100_CHG_FLT_STATUS1_VIN_OV_MASK)
		data->irq_data |= GBMS_IRQ_VIN_OV_MASK;

	if (reg[1] & PE26100_CHG_FLT_STATUS2_VOUT_UV_MASK)
		data->irq_data |= GBMS_IRQ_VOUT_UV_MASK;

	if (reg[1] & PE26100_CHG_FLT_STATUS2_VOUT_OV_MASK)
		data->irq_data |= GBMS_IRQ_VOUT_OV_MASK;

	if (reg[0] & PE26100_CHG_FLT_STATUS1_IIN_UC_MASK)
		data->irq_data |= GBMS_IRQ_IIN_UC_MASK;

	if (reg[0] & PE26100_CHG_FLT_STATUS1_IIN_OC_MASK)
		data->irq_data |= GBMS_IRQ_IIN_OC_MASK;

	if (reg[0] & PE26100_CHG_FLT_STATUS1_IOUT_OC_MASK)
		data->irq_data |= GBMS_IRQ_IOUT_OC_MASK;

	if (reg[4] & PE26100_CHG_IC_STATUS1_IC_OCP_MASK)
		data->irq_data |= GBMS_IRQ_IC_OCP_MASK;
}

#define PE26100_BUCK_NUM_NESTED_IRQS 2
#define PE26100_BUCK_NUM_IRQ_FIELDS 6
static irqreturn_t pe26100_buck_chgr_irq(int irq, void *d)
{
	struct pe26100_buck_charger *data = d;
	int ret, i, sub_irq;
	uint8_t reg[PE26100_BUCK_NUM_IRQ_FIELDS];

	if (pe26100_buck_is_online(PE26100_BUCK_MODE_INVALID))
		return IRQ_NONE;

	ret = pe26100_chg_reg_readn(data->core, PE26100_CHG_FLT_STATUS1, reg,
				    PE26100_BUCK_NUM_IRQ_FIELDS);
	if (ret) {
		dev_warn_ratelimited(data->dev, "Error reading irq ret:%d\n", ret);
		return IRQ_NONE;
	}

	dev_info_ratelimited(data->dev, "IRQ FLT1:0x%x FLT2:0x%x WRN1:0x%x WRN2:0x%x IC_STATUS1:0x%x IC_STATUS2:0x%x\n",
			     reg[0], reg[1], reg[2], reg[3], reg[4], reg[5]);

	pe26100_buck_handle_irq_vin_uv(data, reg[2] & PE26100_CHG_WARN_STATUS1_VIN_UV_WARN_MASK);
	if (pe26100_buck_is_online(PE26100_BUCK_MODE_WIRED) &&
	    (reg[0] || reg[1] || (reg[4] & PE26100_CHG_IC_STATUS1_SHDN_MASK))) {
		pe26100_buck_vote_dc_avail(GBMS_ALL_SEC_CHG_DISABLED, 0);
		return IRQ_HANDLED;
	}

	pe26100_buck_save_irq_state(data, reg);
	for (i = 0; i < PE26100_BUCK_NUM_NESTED_IRQS; i++) {
		sub_irq = irq_find_mapping(data->domain, i);
		if (sub_irq && !(data->mask & (1 << i)))
			handle_nested_irq(sub_irq);
	}

	return IRQ_HANDLED;
}

static void pe26100_buck_bus_lock(struct irq_data *d)
{
	struct pe26100_buck_charger *data = irq_data_get_irq_chip_data(d);

	mutex_lock(&data->irq_lock);
}

static void pe26100_buck_bus_sync_unlock(struct irq_data *d)
{
	struct pe26100_buck_charger *data = irq_data_get_irq_chip_data(d);

	mutex_unlock(&data->irq_lock);
}

static void pe26100_buck_irq_mask(struct irq_data *d)
{
	struct pe26100_buck_charger *data = irq_data_get_irq_chip_data(d);

	data->mask |= BIT(d->hwirq);
}

static int pe26100_read_irq(int irq, gbms_irq_t *val)
{
	struct pe26100_buck_charger *data = irq_get_chip_data(irq);

	*val = data->irq_data;

	return 0;
}

static void pe26100_buck_irq_unmask(struct irq_data *d)
{
	struct pe26100_buck_charger *data = irq_data_get_irq_chip_data(d);

	data->mask &= ~BIT(d->hwirq);
}

static void pe26100_buck_irq_disable(struct irq_data *d)
{
	pe26100_buck_irq_mask(d);
}

static void pe26100_buck_irq_enable(struct irq_data *d)
{
	pe26100_buck_irq_unmask(d);
}

static struct irq_chip pe26100_buck_irq_chip = {
	.name = "pe26100-buck-charger",
	.irq_enable = pe26100_buck_irq_enable,
	.irq_disable = pe26100_buck_irq_disable,
	.irq_mask = pe26100_buck_irq_mask,
	.irq_unmask = pe26100_buck_irq_unmask,
	.irq_bus_lock = pe26100_buck_bus_lock,
	.irq_bus_sync_unlock = pe26100_buck_bus_sync_unlock,
};

static int pe26100_buck_irq_setup(struct pe26100_buck_charger *data)
{
	struct device *dev = data->dev;
	int i, irq;

	data->mask = 0xF;
	data->domain = irq_domain_create_linear(dev_fwnode(dev), PE26100_BUCK_NUM_NESTED_IRQS,
						&irq_domain_simple_ops, data);
	if (!data->domain) {
		dev_err(data->dev, "Unable to get irq domain\n");
		return -ENODEV;
	}

	for (i = 0; i < PE26100_BUCK_NUM_NESTED_IRQS; i++) {
		irq = irq_create_mapping(data->domain, i);
		if (!irq) {
			dev_err(dev, "failed irq create map\n");
			return -EINVAL;
		}
		gbms_irq_register(data->dev, irq, pe26100_read_irq);
		irq_set_chip_data(irq, data);
		irq_set_chip_and_handler(irq, &pe26100_buck_irq_chip, handle_simple_irq);
	}

	return 0;
}

static void pe26100_buck_charger_init_work(struct work_struct *work)
{
	struct pe26100_buck_charger *data = container_of(work, struct pe26100_buck_charger,
							 init_work.work);
	int ret;

	ret = pe26100_buck_init_wcin_psy(data);
	if (ret) {
		dev_err_ratelimited(data->dev, "Error registering PE26100 buck charger wcin (%d)\n",
				   ret);
		schedule_delayed_work(&data->init_work, msecs_to_jiffies(100));
	}

	if (!data->mode_votable)
		data->mode_votable = gvotable_election_get_handle(GBMS_MODE_VOTABLE);
	if (!data->mode_votable) {
		dev_warn_ratelimited(data->dev, "no mode votable\n");
		schedule_delayed_work(&data->init_work, msecs_to_jiffies(100));
	}
}

static int pe26100_buck_charger_probe(struct platform_device *pdev)
{
	struct power_supply_config chgr_psy_cfg = { 0 };
	struct device *dev = &pdev->dev;
	struct pe26100_buck_charger *data;
	int ret;
	const char *tmp;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_DELAYED_WORK(&data->init_work, pe26100_buck_charger_init_work);
	INIT_DELAYED_WORK(&data->aicl_work, pe26100_buck_charger_aicl_work);

	data->dev = dev;
	data->dev->init_name = "pe26100_buck_charger";
	data->core = dev->parent;
	platform_set_drvdata(pdev, data);

	data->aicl_wake_lock = wakeup_source_register(NULL, "pe26100-buck-aicl");
	if (!data->aicl_wake_lock) {
		dev_err(dev, "Failed to register wakeup source\n");
		return -ENODEV;
	}

	mutex_init(&data->mode_lock);
	mutex_init(&data->charge_lock);
	mutex_init(&data->irq_lock);

	/* NOTE: only one instance */
	ret = of_property_read_string(dev->of_node, "pe26100-buck-charger,psy-name", &tmp);
	if (ret == 0)
		pe26100_buck_psy_desc.psy_dsc.name = devm_kstrdup(dev, tmp, GFP_KERNEL);

	chgr_psy_cfg.drv_data = data;
	chgr_psy_cfg.supplied_to = NULL;
	chgr_psy_cfg.num_supplicants = 0;
	data->psy = devm_power_supply_register(dev, &pe26100_buck_psy_desc.psy_dsc,
		&chgr_psy_cfg);
	if (IS_ERR(data->psy)) {
		dev_err(dev, "Failed to register psy rc = %ld\n",
			PTR_ERR(data->psy));
		ret = PTR_ERR(data->psy);
		goto err;
	}

	data->irq_gpio = devm_gpiod_get_optional(dev, "pe26100-buck-charger,irq",
						 GPIOD_IN | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (data->irq_gpio) {
		data->irq_int = gpiod_to_irq(data->irq_gpio);

		ret = devm_request_threaded_irq(data->dev, data->irq_int, NULL,
						pe26100_buck_chgr_irq,
						IRQF_TRIGGER_LOW |
						IRQF_SHARED |
						IRQF_ONESHOT,
						"pe26100-buck-charger",
						data);
		if (ret) {
			dev_err(data->dev, "Error registering irq ret:%d\n", ret);
			goto err;
		}

		ret = pe26100_buck_irq_setup(data);
		if (ret) {
			dev_err(data->dev, "Error setting up nested_irq\n");
			goto err;
		}

		device_init_wakeup(dev, true);
		disable_irq(data->irq_int);
	}

	schedule_delayed_work(&data->init_work, msecs_to_jiffies(100));

	return 0;

err:
	mutex_destroy(&data->charge_lock);
	mutex_destroy(&data->mode_lock);

	return ret;
}

static void pe26100_buck_charger_remove(struct platform_device *pdev)
{

}

static const struct platform_device_id pe26100_buck_charger_id[] = {
	{ "pe26100-buck-charger", 0},
	{},
};

MODULE_DEVICE_TABLE(platform, pe26100_buck_charger_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id pe26100_buck_charger_match_table[] = {
	{ .compatible = "pe26100-bc",},
	{ },
};
#endif

static struct platform_driver pe26100_buck_charger_driver = {
	.probe = pe26100_buck_charger_probe,
	.remove = pe26100_buck_charger_remove,
	.id_table = pe26100_buck_charger_id,
	.driver = {
		.name = "pe26100-buck-charger",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = pe26100_buck_charger_match_table,
#endif
	},
};

module_platform_driver(pe26100_buck_charger_driver);

MODULE_DESCRIPTION("PE26100 Buck Charger driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
