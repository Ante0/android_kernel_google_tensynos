// SPDX-License-Identifier: GPL-2.0
/*
 * Max77779 Usecase state machine V2
 *
 * Copyright 2025 Google, LLC
 *
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include "google_bms.h"
#include "max77779.h"
#include "max77779_charger.h"

#include "max77779_usecase_v2.h"

static int max77779_usecase_v2_otg_enable(struct max77779_uc_v2_data *uc_data, bool enable)
{
	int ret;

	dev_info_ratelimited(uc_data->dev, "%s: enable:%d\n", __func__, enable);

	if (enable) {
		if (!IS_ERR_OR_NULL(uc_data->ext_bst_supply)) {
			mutex_lock(&uc_data->ext_bst_lock);
			ret = regulator_enable(uc_data->ext_bst_supply);
			mutex_unlock(&uc_data->ext_bst_lock);
			if (ret < 0) {
				dev_err(uc_data->dev, "Error enabling regulator ret:%d\n", ret);
				return ret;
			}
		}

		if (!IS_ERR_OR_NULL(uc_data->bst_on))
			gpiod_set_value_cansleep(uc_data->bst_on, 1);

		usleep_range(5 * USEC_PER_MSEC, 5 * USEC_PER_MSEC + 100);

		if (!IS_ERR_OR_NULL(uc_data->ext_bst_ctl))
			gpiod_set_value_cansleep(uc_data->ext_bst_ctl, 1);

	} else {
		if (!IS_ERR_OR_NULL(uc_data->ext_bst_ctl))
			gpiod_set_value_cansleep(uc_data->ext_bst_ctl, 0);

		usleep_range(5 * USEC_PER_MSEC, 5 * USEC_PER_MSEC + 100);

		if (!IS_ERR_OR_NULL(uc_data->bst_on))
			gpiod_set_value_cansleep(uc_data->bst_on, 0);

		if (!IS_ERR_OR_NULL(uc_data->ext_bst_supply)) {
			mutex_lock(&uc_data->ext_bst_lock);
			ret = regulator_disable(uc_data->ext_bst_supply);
			mutex_unlock(&uc_data->ext_bst_lock);
			if (ret < 0) {
				dev_err(uc_data->dev, "Error disabling regulator ret:%d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

/*
 * Case	USB_chg USB_otg	WLC_chg	WLC_TX	PMIC_Charger	Name
 * -------------------------------------------------------------------------------------
 * 7	0	1	1	0	IF-PMIC-WCIN	USB_OTG_WLC_RX
 * 9	0	1	0	0	0		USB_OTG / USB_OTG_FRS
 * -------------------------------------------------------------------------------------
 * WLC_chg = 0 off, 1 = on, 2 = PPS
 *
 * NOTE: do not call with (cb_data->wlc_rx && cb_data->wlc_tx)
 */

static int max77779_usecase_v2_standby_to_otg(struct max77779_uc_v2_data *uc_data, int use_case)
{
	int ret;

	ret = max77779_usecase_v2_otg_enable(uc_data, true);

	if (ret == 0)
		usleep_range(5 * USEC_PER_MSEC, 5 * USEC_PER_MSEC + 100);
	/*
	 * Assumption: max77779_usecase_v2_to_usecase() will write back cached values to
	 * CHG_CNFG_00.Mode. At the moment, the cached value at
	 * max77779_mode_callback is 0. If the cached value changes to something
	 * other than 0, then, the code has to be revisited.
	 */

	return ret;
}

static int max77779_usecase_v2_to_otg_usecase(struct max77779_uc_v2_data *uc_data, int use_case,
					      int from_uc)
{
	int ret = 0;

	switch (from_uc) {
	/* 9: stby to USB OTG */
	/* 10: stby to USB_OTG_FRS */
	case GSU_MODE_STANDBY:
	case GSU_MODE_STANDBY_BUCK_ON:
	case GSU_MODE_WLC_PRESENT:
	case GSU_MODE_INPUT_SUSPEND:
	case GSU_MODE_WLC_RX:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_HYBRID:
	case GSU_MODE_WLC_DC:
		if (use_case != GSU_MODE_USB_OTG_FRS) {
			ret = max77779_usecase_v2_standby_to_otg(uc_data, use_case);
			if (ret < 0) {
				dev_err(uc_data->dev, "%s: cannot enable OTG ret:%d\n", __func__,
					ret);
				return ret;
			}
		}
		break;
	case GSU_MODE_USB_CHG:
	case GSU_MODE_USB_CHG_CHARGE_ENABLED:
		/* need to go through stby out of this */
		if (use_case != GSU_MODE_USB_OTG && use_case != GSU_MODE_USB_OTG_FRS)
			return -EINVAL;
		else if (use_case == GSU_MODE_USB_OTG)
			ret = max77779_usecase_v2_otg_enable(uc_data, true);
	break;
	case GSU_MODE_USB_OTG:
		break;
	case GSU_MODE_USB_OTG_WLC_RX:
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_USB_OTG_WLC_RX_HYBRID:
	case GSU_MODE_USB_OTG_WLC_DC:
		if (use_case == GSU_MODE_USB_OTG_FRS)
			return -EINVAL;
		break;
	case GSU_MODE_USB_OTG_FRS:
		if (use_case == GSU_MODE_USB_OTG_WLC_RX ||
		    use_case == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED)
			return -EINVAL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

/* handles the transition data->use_case ==> use_case */
static int max77779_usecase_v2_to_usecase(struct max77779_uc_v2_data *uc_data, int use_case,
					  int from_uc)
{
	int ret = 0;

	switch (use_case) {
	case GSU_MODE_USB_OTG:
	case GSU_MODE_USB_OTG_FRS:
	case GSU_MODE_USB_OTG_WLC_RX:
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_USB_OTG_WLC_RX_HYBRID:
	case GSU_MODE_USB_OTG_WLC_DC:
		ret = max77779_usecase_v2_to_otg_usecase(uc_data, use_case, from_uc);
		break;
	case GSU_MODE_WLC_RX:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_HYBRID:
	case GSU_MODE_WLC_DC:
		if (bms_usecase_is_uc_otg(from_uc) && (from_uc != GSU_MODE_USB_OTG_FRS))
			ret = max77779_usecase_v2_otg_enable(uc_data, false);
		break;
	case GSU_MODE_STANDBY:
	case GSU_MODE_STANDBY_BUCK_ON:
	case GSU_MODE_INPUT_SUSPEND:
	case GSU_MODE_WLC_PRESENT:
		if ((from_uc != GSU_MODE_USB_OTG_FRS) && bms_usecase_is_uc_otg(from_uc))
			max77779_usecase_v2_otg_enable(uc_data, false);
		else if (from_uc == GSU_MODE_WLC_FWUPDATE)
			max77779_usecase_wlc_fw_update_enable(&uc_data->common_data, false);
		break;
	case GSU_MODE_WLC_FWUPDATE:
		max77779_usecase_wlc_fw_update_enable(&uc_data->common_data, true);
		break;
	default:
		break;
	}

	return ret;
}

/* finish usecase configuration after max77779 mode register is set */
static int max77779_usecase_v2_finish_usecase(struct max77779_uc_v2_data *uc_data, int use_case,
					      int from_uc)
{
	return 0;
}

/*
 * adjust *INSEL (only one source can be enabled at a given time)
 * NOTE: providing compatibility with input_suspend makes this more complex
 * that it needs to be.
 * TODO(b/) sequoia has back to back FETs to isolate WLC from USB
 * and we likely don't need all this logic here.
 */
static int max77779_usecase_v2_set_insel(struct max77779_uc_v2_data *uc_data,  int use_case)
{
	const u8 insel_mask = MAX77779_CHG_CNFG_12_CHGINSEL_MASK |
			      MAX77779_CHG_CNFG_12_WCINSEL_MASK;
	u8 insel_value = 0;
	int ret;

	switch (use_case) {
	/* automatically configure */
	case GSU_MODE_FWUPDATE:
	case GSU_MODE_WLC_FWUPDATE:
		return 0;
	/* normal usecases */
	case GSU_RAW_MODE:
	case GSU_MODE_STANDBY:
	case GSU_MODE_INPUT_SUSPEND:
		insel_value = MAX77779_CHG_CNFG_12_WCINSEL | MAX77779_CHG_CNFG_12_CHGINSEL;
		break;
	case GSU_MODE_USB_CHG:
	case GSU_MODE_USB_CHG_CHARGE_ENABLED:
	case GSU_MODE_USB_DC:
	case GSU_MODE_STANDBY_BUCK_ON:
	case GSU_MODE_WLC_PRESENT:
	case GSU_MODE_USB_CHG_HYBRID:
		insel_value = MAX77779_CHG_CNFG_12_CHGINSEL;
		break;
	case GSU_MODE_WLC_RX:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_HYBRID:
	case GSU_MODE_USB_WLC_RX:
	case GSU_MODE_WLC_DC:
	case GSU_MODE_USB_OTG_WLC_RX:
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_USB_OTG_WLC_RX_HYBRID:
	case GSU_MODE_USB_OTG_WLC_DC:
		insel_value = MAX77779_CHG_CNFG_12_WCINSEL;
		break;
	}

	/* changing [CHGIN|WCIN]_INSEL: works when protection is disabled  */
	ret = max77779_external_chg_insel_write(uc_data->core, insel_mask, insel_value);

	dev_dbg(uc_data->dev, "%s: usecase=%d mask=%x insel=%x (%d)\n",
		__func__, use_case, insel_mask, insel_value, ret);

	return ret;
}

static int max77779_usecase_v2_get_charger_mode_from_uc(struct bms_usecase_entry *entry,
							struct max77779_uc_v2_data *uc_data)
{
	u8 reg, mode;
	bool dc_on = false;
	int use_case = entry->usecase;

	switch (use_case) {
	/* OTG */
	case GSU_MODE_USB_OTG_WLC_RX_HYBRID:
	case GSU_MODE_USB_OTG_WLC_DC:
		dc_on = true;
		fallthrough;
	case GSU_MODE_USB_OTG:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		break;
	case GSU_MODE_USB_OTG_FRS:
		mode = MAX77779_CHGR_MODE_OTG_BOOST_ON;
		break;
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
		mode = MAX77779_CHGR_MODE_CHGR_BUCK_ON;
		break;
	case GSU_MODE_USB_OTG_WLC_RX:
		mode = MAX77779_CHGR_MODE_BUCK_ON;
		break;
	/* NON-OTG */
	case GSU_MODE_USB_CHG_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_USB_WLC_RX:
		mode = MAX77779_CHGR_MODE_CHGR_BUCK_ON;
		break;
	case GSU_MODE_STANDBY:
	case GSU_MODE_STANDBY_BUCK_ON:
	case GSU_MODE_WLC_PRESENT:
	case GSU_MODE_INPUT_SUSPEND:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		break;
	case GSU_MODE_USB_CHG:
	case GSU_MODE_WLC_RX:
		mode = MAX77779_CHGR_MODE_BUCK_ON;
		break;
	case GSU_MODE_WLC_RX_STDBY:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		break;
	case GSU_MODE_WLC_TX:
		mode = MAX77779_CHGR_MODE_BOOST_UNO_ON;
		break;
	case GSU_MODE_WLC_RX_HYBRID:
	case GSU_MODE_WLC_DC:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		dc_on = true;
		break;
	case GSU_MODE_USB_CHG_HYBRID:
	case GSU_MODE_USB_DC:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		dc_on = true;
		break;
	case GSU_MODE_FWUPDATE:
		mode = MAX77779_CHGR_MODE_BOOST_ON;
		break;
	case GSU_MODE_WLC_FWUPDATE:
		mode = MAX77779_CHGR_MODE_BOOST_UNO_ON;
		break;
	case GSU_RAW_MODE:
		return _bms_usecase_mode_get(entry->cb_data->raw_value);
	default:
		dev_err(uc_data->dev, "Error getting mode from usecase:%d\n", use_case);
		mode = MAX77779_CHGR_MODE_ALL_OFF;
	}

	reg = _max77779_chg_cnfg_00_cp_en_set(0, dc_on);
	return _max77779_chg_cnfg_00_mode_set(reg, mode);
}

/*
 * Case	USB_chg USB_otg	WLC_chg	WLC_TX	PMIC_Charger	Ext_B	Name
 * -------------------------------------------------------------------------------------
 * 7	0	1	1	0	IF-PMIC-WCIN	1	USB_OTG_WLC_RX
 * 9	0	1	0	0	0		1	USB_OTG
 * 10   0       1       0       0       OTG_5V          0	USB_OTG_FRS
 * -------------------------------------------------------------------------------------
 * Ext_Boost = 0 off, 1 = OTG 5V
 * WLC_chg = 0 off, 1 = on, 2 = PPS
 *
 * NOTE: do not call with (cb_data->wlc_rx && cb_data->wlc_tx)
 */
static int max77779_usecase_v2_get_otg_usecase(struct max77779_uc_v2_data *uc_data,
					       struct bms_usecase_foreach_cb_data *cb_data)
{
	const int chgr_on = max77779_usecase_cb_data_is_chgr_on(cb_data);
	bool dc_on = cb_data->dc_on; /* && !cb_data->charge_done */
	int usecase;

	/* invalid, cannot do OTG stuff with USB power */
	if (cb_data->buck_on) {
		dev_err(uc_data->dev, "%s: buck_on with OTG\n", __func__);
		return -EINVAL;
	}

	if (!cb_data->wlc_rx && !cb_data->wlc_tx) {
		/* 9: USB_OTG or  10: USB_OTG_FRS */
		if (cb_data->frs_on)
			usecase = GSU_MODE_USB_OTG_FRS;
		else
			usecase = GSU_MODE_USB_OTG;
		/* b/188730136  OTG cases with DC on */
		if (dc_on)
			dev_err(uc_data->dev, "%s: TODO enable pps+OTG\n", __func__);
	} else if (cb_data->wlc_rx) {
		if (chgr_on) {
			if (cb_data->chg_sel == MAX77779_UC_V2_CHG_SEL_HYBRID)
				usecase = GSU_MODE_USB_OTG_WLC_RX_HYBRID;
			else
				usecase = GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED;
		} else {
			usecase = GSU_MODE_USB_OTG_WLC_RX;
		}

		if (dc_on)
			usecase = GSU_MODE_USB_OTG_WLC_DC;
	} else {
		return -EINVAL;
	}

	return usecase;
}

static bool max77779_usecase_v2_is_chg_matching(struct bms_usecase_foreach_cb_data *cb_data)
{
	const enum gbms_chg_select chg_type = _bms_usecase_chg_sel_type_get(cb_data->chg_sel);

	if (cb_data->use_raw)
		return false;

	return (cb_data->buck_on && (chg_type == GBMS_CHGR_SEL_USB)) ||
	       (cb_data->wlc_rx && (chg_type == GBMS_CHGR_SEL_WIRELESS));
}

static void max77779_usecase_v2_fixups(struct max77779_uc_v2_data *uc_data,
				       struct bms_usecase_foreach_cb_data *cb_data)
{
	struct max77779_chgr_data *data = dev_get_drvdata(uc_data->core);

	cb_data->wlcin_off = !!data->wcin_input_suspend;

	dev_dbg(uc_data->dev, "%s: wcin_is_online=%d data->wcin_input_suspend=%d\n",
		__func__, max77779_wcin_is_online(data), data->wcin_input_suspend);

	cb_data->wlc_rx = cb_data->buck_on ? 0 : cb_data->wlc_rx && !cb_data->wlcin_off;

	/* must be after usb masking wlc_rx */
	cb_data->dc_on = cb_data->wlc_rx ? (cb_data->dc_on == GBMS_CHGR_SEL_WIRELESS) :
					   (cb_data->dc_on == GBMS_CHGR_SEL_USB);

	/* pre-process cb_data->chg_sel to be the charger index for the uc state machine */
	cb_data->chg_sel = max77779_usecase_v2_is_chg_matching(cb_data) ?
			   _bms_usecase_chg_sel_index_get(cb_data->chg_sel) : 0;
}

/*
 * Determines the use case to switch to. This is device/system dependent and
 * will likely be factored to a separate file (compile module).
 */
static int max77779_usecase_v2_get_usecase(void *uc_d, struct bms_usecase_foreach_cb_data *cb_data)
{
	struct max77779_uc_v2_data *uc_data = uc_d;
	int usecase = GSU_MODE_STANDBY;

	if (max77779_usecase_is_debounce(&uc_data->common_data, cb_data)) {
		dev_dbg(uc_data->dev, "%s: DEBOUNCE callback\n", __func__);
		return BMS_USECASE_NO_HOPS;
	}

	max77779_usecase_v2_fixups(uc_data, cb_data);

	/* buck_on is wired, wlc_rx is wireless, might still need rTX */
	if (unlikely(cb_data->fwupdate_on)) {
		if (GSU_MODE_FWUPDATE_MASK & cb_data->fwupdate_on)
			usecase = GSU_MODE_FWUPDATE;
		else
			usecase = GSU_MODE_WLC_FWUPDATE;
	} else if (cb_data->use_raw) {
		usecase = GSU_RAW_MODE;
	} else if (cb_data->otg_on || cb_data->frs_on) {
		/* OTG modes override the others, might need to move under usb_wlc */
		usecase = max77779_usecase_v2_get_otg_usecase(uc_data, cb_data);
	} else if (cb_data->usb_wlc) {
		/* USB+WLC for factory and testing */
		usecase = GSU_MODE_USB_WLC_RX;
	} else if (cb_data->buck_on) {
		/* MODE_BUCK_ON is inflow */
		if (max77779_usecase_cb_data_is_chgr_on(cb_data))
			usecase = (cb_data->chg_sel == MAX77779_UC_V2_CHG_SEL_HYBRID) ?
				GSU_MODE_USB_CHG_HYBRID : GSU_MODE_USB_CHG_CHARGE_ENABLED;
		else
			usecase = GSU_MODE_USB_CHG;

		if (cb_data->dc_on)
			usecase = GSU_MODE_USB_DC;
		else if (cb_data->stby_on && !max77779_usecase_cb_data_is_chgr_on(cb_data))
			usecase = GSU_MODE_STANDBY_BUCK_ON;
	} else if (cb_data->wlc_rx) {
		if (max77779_usecase_cb_data_is_chgr_on(cb_data)) {
			switch (cb_data->chg_sel) {
			case MAX77779_UC_V2_CHG_SEL_DEFAULT:
				usecase = GSU_MODE_WLC_RX_CHARGE_ENABLED;
				break;
			case MAX77779_UC_V2_CHG_SEL_HYBRID:
				usecase = GSU_MODE_WLC_RX_HYBRID;
				break;
			default:
				usecase = GSU_MODE_WLC_PRESENT;
			}
		} else if (cb_data->wlc_rx_stby) {
			usecase = GSU_MODE_WLC_RX_STDBY;
		} else {
			usecase = GSU_MODE_WLC_RX;
		}

		/* wired input should be disabled here */
		if (cb_data->dc_on)
			usecase = GSU_MODE_WLC_DC;
	} else if (cb_data->wlcin_off && cb_data->chgin_off) {
		usecase = GSU_MODE_INPUT_SUSPEND;
	}

	return usecase;
}

static void max77779_usecase_v2_dump_usecase_config(struct max77779_uc_v2_data *uc_data)
{
	dev_info(uc_data->dev, "bst_on:%d, ext_bst_ctl: %d, ext_bst_mode:%d ext_bst_supply:%d\n",
		 (IS_ERR_OR_NULL(uc_data->bst_on)
		 ? (int)PTR_ERR(uc_data->bst_on)
		 : desc_to_gpio(uc_data->bst_on)),
		 (IS_ERR_OR_NULL(uc_data->ext_bst_ctl)
		 ? (int)PTR_ERR(uc_data->ext_bst_ctl)
		 : desc_to_gpio(uc_data->ext_bst_ctl)),
		 (IS_ERR_OR_NULL(uc_data->ext_bst_mode)
		 ? (int)PTR_ERR(uc_data->ext_bst_mode)
		 : desc_to_gpio(uc_data->ext_bst_mode)),
		 (IS_ERR_OR_NULL(uc_data->ext_bst_supply)
		 ? (int)PTR_ERR(uc_data->ext_bst_supply)
		 : 1));
}

/*
 * Return usecase init status
 * 0: init not complete
 * 1: init complete
 * <0: error
 *
 * Must be called two times
 * Init: Pass in uninitialized uc_d and dev (initializes structure)
 * Delayed Init: Pass in initialized uc_d and a NULL pointer (populates structure)
 */
static int max77779_usecase_v2_set_usecase(struct max77779_uc_v2_data *uc_data, int use_case)
{
	const int from_uc = bms_usecase_get_usecase();
	int ret;

	/* Need this only for usecases that control the switches */
	if (uc_data->init_done <= 0) {
		uc_data->init_done = max77779_usecase_v2_setup_usecases((void **)&uc_data, NULL);
		if (uc_data->init_done <= 0)
			return -EAGAIN;
		max77779_usecase_v2_dump_usecase_config(uc_data);
	}

	/* always fix/adjust insel (solves multiple input_suspend) */
	ret = max77779_usecase_v2_set_insel(uc_data, use_case);
	if (ret < 0) {
		dev_err(uc_data->dev, "use_case=%d->%d set_insel failed ret:%d\n",
			from_uc, use_case, ret);
		return ret;
	}

	if (from_uc == use_case)
		goto exit_done;

	/* transition from data->use_case to use_case */
	ret = max77779_usecase_v2_to_usecase(uc_data, use_case, from_uc);
	if (ret < 0) {
		dev_err(uc_data->dev, "use_case=%d->%d to_usecase failed ret:%d\n",
			from_uc, use_case, ret);
		return ret;
	}

exit_done:
	/* finally set mode register */
	ret = max77779_external_chg_mode_write(uc_data->core, uc_data->reg);
	dev_dbg(uc_data->dev, "%s: CHARGER_MODE=%x ret:%x\n", __func__, uc_data->reg, ret);
	if (ret < 0) {
		dev_err(uc_data->dev, "use_case=%d->%d CNFG_00=%x failed ret:%d\n",
			from_uc, use_case, uc_data->reg, ret);
		return ret;
	}

	ret = max77779_usecase_v2_finish_usecase(uc_data, use_case, from_uc);
	if (ret < 0 && ret != -EAGAIN)
		dev_err(uc_data->dev, "Error finishing usecase config ret:%d\n", ret);

	return ret;
}

static int max77779_usecase_v2_handle_usecase(void *d, struct bms_usecase_entry *entry)
{
	struct max77779_uc_v2_data *uc_data = d;
	struct bms_usecase_foreach_cb_data *cb_data = entry->cb_data;
	const int from_uc = bms_usecase_get_usecase();
	int ret;
	uint8_t reg;

	ret = max77779_external_chg_reg_read(uc_data->core, MAX77779_CHG_CNFG_00, &reg);
	if (ret < 0)
		dev_err(uc_data->dev, "Error reading mode reg ret:%d\n", ret);

	uc_data->reg = max77779_usecase_v2_get_charger_mode_from_uc(entry, uc_data);

	dev_info(uc_data->dev, "%s:%s use_case=%s(%d)->%s(%d) CHG_CNFG_00=%x->%x\n",
		__func__, cb_data->reason ? cb_data->reason : "<>",
		bms_usecase_to_str(from_uc), from_uc,
		bms_usecase_to_str(entry->usecase), entry->usecase,
		ret ? ret : reg, uc_data->reg);

	/* state machine that handle transition between states */
	return max77779_usecase_v2_set_usecase(uc_data, entry->usecase);
}

static int max77779_usecase_v2_from_uc_completion_cb(void *d, struct bms_usecase_entry *entry)
{
	const struct bms_usecase_foreach_cb_data *cb_data = entry->cb_data;

	/* transitioning to a max77779 usecase */
	if (cb_data->chg_sel == MAX77779_UC_V2_CHG_SEL_DEFAULT)
		return 0;

	/* state machine that handle transition between states */
	return max77779_usecase_v2_handle_usecase(d, entry);
}

static int max77779_usecase_v2_to_uc_completion_cb(void *d, struct bms_usecase_entry *entry)
{
	const struct bms_usecase_foreach_cb_data *cb_data = entry->cb_data;

	/*
	 * transitioning to a non-max77779 usecase
	 * handled in max77779_usecase_v2_from_uc_completion_cb
	 */
	if (cb_data->chg_sel != MAX77779_UC_V2_CHG_SEL_DEFAULT)
		return 0;

	return max77779_usecase_v2_handle_usecase(d, entry);
}

/* lazy init on the switches */


static bool max77779_usecase_v2_setup_usecases_done(struct max77779_uc_v2_data *uc_data)
{
	return (PTR_ERR(uc_data->bst_on) != -EPROBE_DEFER) &&
	       (PTR_ERR(uc_data->ext_bst_mode) != -EPROBE_DEFER) &&
	       (PTR_ERR(uc_data->ext_bst_ctl) != -EPROBE_DEFER);
	/* TODO: handle platform specific differences.. */
}

static int max77779_usecase_v2_usecase_hops(void *d, int from_uc, int to_uc)
{
	bool from_otg = false;
	bool need_stby = false;
	int hop = BMS_USECASE_NO_HOPS;
	struct max77779_uc_v2_data *uc_data = d;

	switch (from_uc) {
	case GSU_MODE_USB_CHG:
	case GSU_MODE_USB_CHG_CHARGE_ENABLED:
		if (to_uc == GSU_MODE_USB_OTG) {
			need_stby = uc_data->ext_bst_ctl >= 0;
			break;
		}

		need_stby = to_uc != GSU_MODE_USB_CHG &&
			    to_uc != GSU_MODE_USB_CHG_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_DC &&
			    to_uc != GSU_MODE_USB_OTG_FRS &&
			    to_uc != GSU_MODE_USB_CHG_HYBRID;
		break;
	case GSU_MODE_USB_CHG_HYBRID:
		need_stby = to_uc != GSU_MODE_USB_CHG_HYBRID &&
			    to_uc != GSU_MODE_USB_CHG &&
			    to_uc != GSU_MODE_USB_CHG_CHARGE_ENABLED;
		break;
	case GSU_MODE_WLC_RX:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
		/* HPP supported by device handled by wlc driver */
		need_stby = to_uc != GSU_MODE_WLC_RX &&
			    to_uc != GSU_MODE_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_WLC_DC &&
			    to_uc != GSU_MODE_USB_OTG_WLC_DC &&
			    to_uc != GSU_MODE_WLC_RX_HYBRID &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_HYBRID;
		break;
	case GSU_MODE_WLC_RX_HYBRID:
		need_stby = to_uc != GSU_MODE_WLC_RX_HYBRID &&
			    to_uc != GSU_MODE_WLC_RX &&
			    to_uc != GSU_MODE_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_HYBRID;
		break;
	case GSU_MODE_USB_OTG:
		from_otg = true;
		need_stby = to_uc != GSU_MODE_USB_OTG &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_HYBRID &&
			    to_uc != GSU_MODE_USB_OTG_WLC_DC;
		break;
	case GSU_MODE_USB_OTG_FRS:
		from_otg = true;
		if (to_uc == GSU_MODE_USB_OTG_WLC_RX ||
		    to_uc == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED) {
			need_stby = uc_data->ext_bst_ctl >= 0;
			break;
		}

		need_stby = to_uc != GSU_MODE_USB_OTG_FRS &&
			    to_uc != GSU_MODE_USB_CHG &&
			    to_uc != GSU_MODE_USB_CHG_CHARGE_ENABLED;
		break;
	case GSU_MODE_USB_OTG_WLC_RX:
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
		from_otg = true;
		if (to_uc == GSU_MODE_USB_OTG_FRS) {
			need_stby = uc_data->ext_bst_ctl >= 0;
			break;
		}

		need_stby = to_uc != GSU_MODE_WLC_RX &&
			    to_uc != GSU_MODE_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX &&
			    to_uc != GSU_MODE_WLC_DC &&
			    to_uc != GSU_MODE_USB_OTG_WLC_DC &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_HYBRID;
		break;
	case GSU_MODE_USB_OTG_WLC_RX_HYBRID:
		from_otg = true;
		need_stby = to_uc != GSU_MODE_WLC_RX &&
			    to_uc != GSU_MODE_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_HYBRID &&
			    to_uc != GSU_MODE_WLC_RX_HYBRID &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG;
		break;
	case GSU_MODE_USB_DC:
		need_stby = to_uc != GSU_MODE_USB_DC &&
			    to_uc != GSU_MODE_USB_CHG &&
			    to_uc != GSU_MODE_USB_CHG_CHARGE_ENABLED;
		break;
	case GSU_MODE_USB_OTG_WLC_DC:
		from_otg = true;
		fallthrough;
	case GSU_MODE_WLC_DC:
		need_stby = to_uc != GSU_MODE_WLC_DC &&
			    to_uc != GSU_MODE_USB_OTG_WLC_DC &&
			    to_uc != GSU_MODE_USB_OTG &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX &&
			    to_uc != GSU_MODE_WLC_RX &&
			    to_uc != GSU_MODE_WLC_RX_CHARGE_ENABLED;
		break;
	case GSU_RAW_MODE:
	case GSU_MODE_FWUPDATE:
	case GSU_MODE_WLC_FWUPDATE:
		need_stby = true;
		break;
	default:
		break;
	}

	if (bms_usecase_is_uc_standby(to_uc) || bms_usecase_is_uc_standby(from_uc))
		need_stby = false;
	else if (to_uc == GSU_RAW_MODE || to_uc == GSU_MODE_USB_WLC_RX)
		need_stby = true;

	/*
	 * to_uc can not be GSU_MODE_STANDBY_BUCK_ON and need_stby be true, or UC state machine
	 * will loop
	 */
	if (need_stby && bms_usecase_is_uc_wired(to_uc))
		hop = GSU_MODE_STANDBY_BUCK_ON;
	else if (need_stby && bms_usecase_is_uc_wireless(to_uc))
		hop = GSU_MODE_WLC_PRESENT;

	dev_info(uc_data->dev, "%s: use_case=%s(%d)->%s(%d) from_otg=%d need_stby=%d hop:%s\n",
		 __func__,
		 bms_usecase_to_str(from_uc), from_uc,
		 bms_usecase_to_str(to_uc), to_uc,
		 from_otg, need_stby, bms_usecase_to_str(hop));

	if (hop != BMS_USECASE_NO_HOPS)
		return hop;

	return need_stby ? GSU_MODE_STANDBY : BMS_USECASE_NO_HOPS;
}

static ssize_t ext_bst_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct max77779_usecase_data *data = dev_get_drvdata(dev);
	struct max77779_uc_v2_data *uc_data = data->uc_data;
	int ret;
	bool enabled = false;

	ret = kstrtobool(buf, &enabled);
	if (ret)
		return ret;

	mutex_lock(&uc_data->ext_bst_lock);
	if (enabled && !uc_data->fs_ext_bst_state) {
		ret = regulator_enable(uc_data->ext_bst_supply);
		uc_data->fs_ext_bst_state = true;
	} else if (!enabled && uc_data->fs_ext_bst_state) {
		ret = regulator_disable(uc_data->ext_bst_supply);
		uc_data->fs_ext_bst_state = false;
	}
	mutex_unlock(&uc_data->ext_bst_lock);

	return ret ? ret : count;
}

static ssize_t ext_bst_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct max77779_usecase_data *data = dev_get_drvdata(dev);
	struct max77779_uc_v2_data *uc_data = data->uc_data;

	return sysfs_emit(buf, "%s\n", regulator_is_enabled(uc_data->ext_bst_supply) ?
			  "enabled" : "disabled");
}
static DEVICE_ATTR_RW(ext_bst);

static struct attribute *max77779_usecase_v2_attrs[] = {
	&dev_attr_ext_bst.attr,
	NULL
};

static const struct attribute_group max77779_usecase_v2_attrs_grp = {
	.attrs = max77779_usecase_v2_attrs,
};

static int max77779_usecase_v2_init_fs(struct max77779_uc_v2_data *uc_data)
{
	return sysfs_create_group(&uc_data->dev->kobj, &max77779_usecase_v2_attrs_grp);
}

static int max77779_usecase_v2_usecase_init(struct max77779_uc_v2_data *uc_data)
{
	struct bms_usecase_chg_data *chg_data;
	int ret;

	chg_data = devm_kzalloc(uc_data->dev, sizeof(struct bms_usecase_chg_data), GFP_KERNEL);
	if (!chg_data)
		return -ENOMEM;

	chg_data->dev = uc_data->dev;
	chg_data->num_devices = 2;
	chg_data->uc_data = (void *)uc_data;
	chg_data->cb_get_hops = max77779_usecase_v2_usecase_hops;
	chg_data->cb_get_usecase = max77779_usecase_v2_get_usecase;

	ret = bms_usecase_init(chg_data);
	if (ret < 0)
		return ret;

	ret = bms_usecase_register_completion_cb(uc_data, max77779_usecase_v2_from_uc_completion_cb,
						 max77779_usecase_v2_to_uc_completion_cb);
	if (ret < 0) {
		dev_err(uc_data->dev, "Failed to register max77779 completion cb(%d)\n", ret);
		return ret;
	}

	return max77779_usecase_v2_init_fs(uc_data);
}

static int max77779_usecase_v2_setup_default_usecase(void **uc_d, struct device *dev)
{
	int ret;
	struct max77779_uc_v2_data *uc_data;
	struct regulator *ext_bst_supply = devm_regulator_get_optional(dev, "max77779,extbst");

	if (PTR_ERR(ext_bst_supply) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	*uc_d = devm_kzalloc(dev, sizeof(*uc_data), GFP_KERNEL);
	if (!*uc_d) {
		dev_err(dev, "Error allocating bms_usecase_chg_data!!!\n");
		return -ENOMEM;
	}

	uc_data = *uc_d;
	uc_data->dev = dev;
	uc_data->core = dev->parent;

	/* external boost */
	uc_data->bst_on = ERR_PTR(-EPROBE_DEFER);
	uc_data->ext_bst_ctl = ERR_PTR(-EPROBE_DEFER);
	uc_data->ext_bst_mode = ERR_PTR(-EPROBE_DEFER);
	uc_data->ext_bst_supply = ext_bst_supply;

	mutex_init(&uc_data->ext_bst_lock);
	uc_data->init_done = false;

	max77779_external_chg_reg_update(uc_data->core, MAX77779_CHG_CNFG_12,
					MAX77779_CHG_CNFG_12_WCIN_REG_MASK,
					_max77779_chg_cnfg_12_wcin_reg_set(0, 0x0));

	ret = max77779_usecase_common_data_init(&uc_data->common_data, dev);
	if (ret < 0) {
		dev_err(dev, "Error initing common data ret:%d\n", ret);
		return ret;
	}

	return max77779_usecase_v2_usecase_init(uc_data);
}


/*
 * Return usecase init status
 * 0: init not complete
 * 1: init complete
 * <0: error
 */
int max77779_usecase_v2_setup_usecases(void **uc_d, struct device *dev)
{
	struct max77779_uc_v2_data *uc_data = *uc_d;

	if (!uc_data)
		return max77779_usecase_v2_setup_default_usecase(uc_d, dev);

	/* control external boost if present */
	if (PTR_ERR(uc_data->bst_on) == -EPROBE_DEFER) {
		uc_data->bst_on = devm_gpiod_get_optional(uc_data->dev, "max77779,bst-on", 0);
		if (!IS_ERR_OR_NULL(uc_data->bst_on))
			gpiod_direction_output(uc_data->bst_on, 0);
	}
	if (PTR_ERR(uc_data->ext_bst_ctl) == -EPROBE_DEFER)
		uc_data->ext_bst_ctl = devm_gpiod_get_optional(uc_data->dev, "max77779,extbst-ctl",
							       0);
	if (PTR_ERR(uc_data->ext_bst_mode) == -EPROBE_DEFER) {
		uc_data->ext_bst_mode = devm_gpiod_get_optional(uc_data->dev,
								"max77779,extbst-mode", 0);
		if (!IS_ERR_OR_NULL(uc_data->ext_bst_mode))
			gpiod_set_value_cansleep(uc_data->ext_bst_mode, 0);
	}

	return max77779_usecase_v2_setup_usecases_done(uc_data);
}
EXPORT_SYMBOL_GPL(max77779_usecase_v2_setup_usecases);

int max77779_usecase_v2_usecase_remove(void *uc_data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(max77779_usecase_v2_usecase_remove);
