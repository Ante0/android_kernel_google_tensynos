// SPDX-License-Identifier: GPL-2.0
/*
 * Max77779 Usecase state machine V1
 *
 * Copyright 2023 Google, LLC
 *
 */


#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include "google_bms.h"
#include "max77779.h"
#include "max77779_charger.h"
#include "max77779_usecase_v1.h"
#include <misc/gvotable.h>

/* ----------------------------------------------------------------------- */

static int max77779_usecase_v1_wlc_en_with_defender_reason(
	struct max77779_uc_v1_data *uc_data,
	enum wlc_state_t state,
	bool defender_enabled)
{
	const int wlc_on = state == WLC_ENABLED;
	int ret;
	struct gvotable_election *wlc_defender_enabled_votable = NULL;
	const bool defender_enabled_vote = (state == WLC_DISABLED) && defender_enabled;

	pr_debug("%s: wlc_en=%d wlc_on=%d wlc_state=%d def_en %d\n", __func__,
		 (IS_ERR_OR_NULL(uc_data->wlc_en)
		 ? (int)PTR_ERR(uc_data->wlc_en)
		 : desc_to_gpio(uc_data->wlc_en)),
		 wlc_on, state, defender_enabled);

	if (IS_ERR_OR_NULL(uc_data->wlc_en))
		return 0;

	if (state == WLC_SPOOFED && uc_data->wlc_spoof_vbyp > 0) {
		ret = max77779_external_chg_reg_write(uc_data->core, MAX77779_CHG_CNFG_11,
					     uc_data->wlc_spoof_vbyp);
		pr_debug("%s: MAX77779_CHG_CNFG_11 write to %02x (ret = %d)\n",
			 __func__, uc_data->wlc_spoof_vbyp, ret);
	}

	if (uc_data->slow_wlc_ilim)
		max77779_external_chg_reg_update(uc_data->core, MAX77779_CHG_CNFG_10,
					MAX77779_CHG_CNFG_10_CHGIN_ILIM_SPEED_MASK,
					_max77779_chg_cnfg_10_chgin_ilim_speed_set(0, wlc_on));

	if (state == WLC_SPOOFED && !IS_ERR_OR_NULL(uc_data->wlc_spoof_gpio))
		gpiod_set_value_cansleep(uc_data->wlc_spoof_gpio, 1);

	wlc_defender_enabled_votable = gvotable_election_get_handle(WLC_DEFENDER_VOTABLE);
	if (wlc_defender_enabled_votable) {
		ret = gvotable_cast_bool_vote(wlc_defender_enabled_votable,
				DEFENDER_ENABLED_VOTER, defender_enabled_vote);
		pr_debug("%s: casted %s defender_enabled_vote %d ret %d\n",
			__func__, DEFENDER_ENABLED_VOTER, defender_enabled_vote, ret);
	}
	gpiod_set_value_cansleep(uc_data->wlc_en, wlc_on);

	return 0;
}

static int max77779_usecase_v1_wlc_en(struct max77779_uc_v1_data *uc_data,
									  enum wlc_state_t state)
{
	return max77779_usecase_v1_wlc_en_with_defender_reason(uc_data, state, false);
}

/* RTX reverse wireless charging */
static int max77779_usecase_v1_wlc_tx_enable(struct max77779_uc_v1_data *uc_data,
					     int use_case, bool enable)
{
	int ret = 0;

	pr_debug("%s: use_case:%d enable:%d\n", __func__, use_case, enable);

	if (!enable) {
		ret = max77779_external_chg_reg_write(uc_data->core, MAX77779_CHG_CNFG_11, 0x0);
		if (ret < 0)
			pr_err("%s: fail to reset MAX77779_CHG_REVERSE_BOOST_VOUT\n",
				__func__);

		ret = max77779_usecase_v1_wlc_en(uc_data, WLC_DISABLED);
		if (ret < 0)
			pr_err("%s: cannot disable WLC (%d)\n", __func__, ret);

		return ret;
	}

	ret = max77779_usecase_v1_wlc_en(uc_data, WLC_ENABLED);
	if (ret < 0)
		pr_err("%s: cannot enable WLC (%d)\n", __func__, ret);

	if (!IS_ERR_OR_NULL(uc_data->rtx_ready))
		gpiod_set_value_cansleep(uc_data->rtx_ready, 1);

	return ret;
}
static int max77779_usecase_v1_wlc_tx_config(struct max77779_uc_v1_data *uc_data,
					     int use_case)
{
	u8 val;
	int ret = 0;

	/* We need to configure max77779 */
	if (use_case == GSU_MODE_WLC_TX) {
		ret = max77779_external_chg_reg_write(uc_data->core,
							MAX77779_CHG_CNFG_11,
							MAX77779_CHG_REVERSE_BOOST_VOUT_7V);
		if (ret < 0)
			pr_err("fail to configure MAX77779_CHG_REVERSE_BOOST_VOUT\n");
	} else {
		ret = max77779_external_chg_reg_write(uc_data->core,
							MAX77779_CHG_CNFG_11,
							0x0);
		if (ret < 0)
			pr_err("fail to reset MAX77779_CHG_REVERSE_BOOST_VOUT\n");
	}
	/* Set WCSM to 1.4A */
	ret = max77779_external_chg_reg_read(uc_data->core, MAX77779_CHG_CNFG_05, &val);
	if (ret < 0)
		pr_err("%s: fail to read MAX77779_CHG_CNFG_05 ret:%d\n", __func__, ret);

	ret = max77779_external_chg_reg_write(uc_data->core, MAX77779_CHG_CNFG_05,
		_max77779_chg_cnfg_05_wcsm_ilim_set(val,
				MAX77779_CHG_CNFG_05_WCSM_ILIM_1400_MA));
	if (ret < 0) {
		pr_err("%s: fail to write MAX77779_CHG_CNFG_05 ret:%d\n", __func__, ret);
		return ret;
	}

	return ret;
}

static int max77779_usecase_v1_pogo_vout_enable(struct max77779_uc_v1_data *uc_data,
						bool enable, bool otg)
{
	pr_debug("%s: enable: %d, otg: %d\n", __func__, enable, otg);

	if (enable && otg) {
		if (!IS_ERR_OR_NULL(uc_data->pogo_vout_en))
			gpiod_set_value_cansleep(uc_data->pogo_vout_en, 0);

		if (!IS_ERR_OR_NULL(uc_data->ext_bst_ctl))
			gpiod_set_value_cansleep(uc_data->ext_bst_ctl, 1);

		return 0;
	}
	if (!otg && !IS_ERR_OR_NULL(uc_data->ext_bst_ctl))
		gpiod_set_value_cansleep(uc_data->ext_bst_ctl, 0);

	if (enable) {
		if (!IS_ERR_OR_NULL(uc_data->bst_on))
			gpiod_set_value_cansleep(uc_data->bst_on, true);

		if (!IS_ERR_OR_NULL(uc_data->pogo_vout_en))
			gpiod_set_value_cansleep(uc_data->pogo_vout_en, true);
	} else {
		if (!IS_ERR_OR_NULL(uc_data->pogo_vout_en))
			gpiod_set_value_cansleep(uc_data->pogo_vout_en, false);

		if (!IS_ERR_OR_NULL(uc_data->bst_on))
			gpiod_set_value_cansleep(uc_data->bst_on, false);
	}

	return 0;
}

static int max77779_usecase_v1_otg_mode(struct max77779_uc_v1_data *uc_data, int to)
{
	int ret = -EINVAL;

	pr_debug("%s: to=%d\n", __func__, to);

	if (to == GSU_MODE_USB_OTG) {
		ret = max77779_external_chg_mode_write(uc_data->core,
						       MAX77779_CHGR_MODE_ALL_OFF);
	}

	return ret;
}

static int max77779_usecase_v1_otg_enable(struct max77779_uc_v1_data *uc_data,
					  bool enable)
{
	int ret;

	pr_debug("%s: enable:%d\n", __func__, enable);

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

		/*
		 * if ext_rx_otg is enabled, turn off external boost
		 * except when wlcrx/otg are using it
		 */
		if (!IS_ERR_OR_NULL(uc_data->bst_on))
			gpiod_set_value_cansleep(uc_data->bst_on, uc_data->ext_bst_on);

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

static int max77779_usecase_v1_wlcrx_ext_bst_enable(struct max77779_uc_v1_data *uc_data,
						    bool enable)
{
	if (!uc_data->ext_rx_otg)
		return 0;

	pr_debug("%s: enable:%d\n", __func__, enable);

	if (!IS_ERR_OR_NULL(uc_data->bst_on))
		gpiod_set_value_cansleep(uc_data->bst_on, enable);

	return 0;
}

static bool wlcrx_otg_ext_bst_enabled(struct max77779_uc_v1_data *uc_data,
				      const int use_case, const int from_uc)
{
	if (!uc_data->ext_rx_otg)
		return false;

	switch (use_case) {
	case GSU_MODE_USB_OTG:
	case GSU_MODE_WLC_RX:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_SPOOFED:
		if (from_uc == GSU_MODE_STANDBY ||
		    from_uc == GSU_MODE_STANDBY_BUCK_ON ||
		    from_uc == GSU_MODE_INPUT_SUSPEND ||
		    from_uc == GSU_MODE_USB_OTG_WLC_RX ||
		    from_uc == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED)
			return true;
	break;

	case GSU_MODE_USB_OTG_WLC_RX:
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
		if (from_uc == GSU_MODE_USB_OTG ||
		    from_uc == GSU_MODE_WLC_RX ||
		    from_uc == GSU_MODE_WLC_RX_CHARGE_ENABLED ||
		    from_uc == GSU_MODE_WLC_RX_SPOOFED)
			return true;
	break;

	default:
		return false;
	}

	return false;
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

static int max77779_usecase_v1_standby_to_otg(struct max77779_uc_v1_data *uc_data,
					      int use_case)
{
	int ret;

	ret = max77779_usecase_v1_otg_enable(uc_data, true);

	if (ret == 0)
		usleep_range(5 * USEC_PER_MSEC, 5 * USEC_PER_MSEC + 100);
	/*
	 * Assumption: max77779_usecase_v1_to_usecase() will write back cached values to
	 * CHG_CNFG_00.Mode. At the moment, the cached value at
	 * max77779_mode_callback is 0. If the cached value changes to something
	 * other than 0, then, the code has to be revisited.
	 */

	return ret;
}

/* was b/179816224 WLC_RX -> WLC_RX + OTG (Transition #10) */
static int max77779_usecase_v1_wlcrx_to_wlcrx_otg(struct max77779_uc_v1_data *uc_data)
{
	pr_warn("%s: disabled\n", __func__);
	return 0;
}

static int max77779_usecase_v1_to_otg_usecase(struct max77779_uc_v1_data *uc_data,
					      int use_case, int from_uc)
{
	int ret = 0;

	switch (from_uc) {
	/* 9: stby to USB OTG */
	/* 10: stby to USB_OTG_FRS */
	case GSU_MODE_STANDBY:
	case GSU_MODE_STANDBY_BUCK_ON:
	case GSU_MODE_INPUT_SUSPEND:
		if (use_case != GSU_MODE_USB_OTG_FRS) {
			ret = max77779_usecase_v1_standby_to_otg(uc_data, use_case);
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
			ret = max77779_usecase_v1_otg_enable(uc_data, true);
	break;


	case GSU_MODE_WLC_TX:
	break;

	case GSU_MODE_WLC_RX:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_SPOOFED:
	case GSU_MODE_DOCK:
	case GSU_MODE_DOCK_CHARGE_ENABLED:
		if (use_case == GSU_MODE_USB_OTG_WLC_RX ||
		    use_case == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED ||
		    use_case == GSU_MODE_USB_OTG_WLC_DC) {
			if (uc_data->rx_otg_en) {
				ret = max77779_usecase_v1_standby_to_otg(uc_data, use_case);
			} else {
				ret = max77779_usecase_v1_wlcrx_to_wlcrx_otg(uc_data);
			}
		}
	break;

	case GSU_MODE_USB_OTG:
		/* b/179816224: OTG -> WLC_RX + OTG */
		if (use_case == GSU_MODE_USB_OTG_WLC_RX ||
		    use_case == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED) {
			/* pvp TODO: Is it just WLC Rx enable? */
		}
	break;
	case GSU_MODE_USB_OTG_WLC_RX:
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_USB_OTG_WLC_DC:
		if (use_case == GSU_MODE_USB_OTG_FRS)
			return -EINVAL;
	break;
	case GSU_MODE_USB_OTG_FRS:
		if (use_case == GSU_MODE_USB_OTG_WLC_RX ||
		    use_case == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED ||
		    use_case == GSU_MODE_USB_OTG_WLC_DC)
			return -EINVAL;
	break;
	case GSU_MODE_POGO_VOUT:
		if (use_case == GSU_MODE_USB_OTG_POGO_VOUT) {
			ret = max77779_external_chg_reg_write(uc_data->core,
							      MAX77779_CHG_CNFG_00,
							      MAX77779_CHGR_MODE_BOOST_UNO_ON);

			msleep(40);
			ret = max77779_usecase_v1_pogo_vout_enable(uc_data, true, true);
		}
	break;
	case GSU_MODE_USB_OTG_POGO_VOUT:
	break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

/* handles the transition data->use_case ==> use_case */
static int max77779_usecase_v1_to_usecase(struct max77779_uc_v1_data *uc_data,
					  int use_case, int from_uc)
{
	bool rtx_avail = false;
	int ret = 0;

	/* read ext bst state for wlcrx/otg usecases */
	uc_data->ext_bst_on = wlcrx_otg_ext_bst_enabled(uc_data, use_case, from_uc);

	switch (use_case) {
	case GSU_MODE_USB_OTG:
	case GSU_MODE_USB_OTG_FRS:
	case GSU_MODE_USB_OTG_WLC_RX:
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_USB_OTG_POGO_VOUT:
	case GSU_MODE_USB_OTG_WLC_DC:
		ret = max77779_usecase_v1_to_otg_usecase(uc_data, use_case, from_uc);
		break;
	case GSU_MODE_WLC_TX:
		rtx_avail = true;
		ret = max77779_usecase_v1_wlc_tx_config(uc_data, use_case);
		break;
	case GSU_MODE_WLC_DC:
	case GSU_MODE_WLC_RX:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_SPOOFED:
	case GSU_MODE_DOCK:
	case GSU_MODE_DOCK_CHARGE_ENABLED:
		if (from_uc == GSU_MODE_USB_OTG_WLC_RX ||
		    from_uc == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED ||
		    from_uc == GSU_MODE_USB_OTG_WLC_DC) {
			if (uc_data->ext_otg_only)
				ret = max77779_usecase_v1_otg_enable(uc_data, false);
			else
				ret = max77779_usecase_v1_otg_mode(uc_data, GSU_MODE_USB_OTG);
		} else if (from_uc == GSU_MODE_STANDBY ||
			   from_uc == GSU_MODE_STANDBY_BUCK_ON ||
			   from_uc == GSU_MODE_INPUT_SUSPEND) {
			ret = max77779_usecase_v1_wlcrx_ext_bst_enable(uc_data, true);
		}
		break;
	case GSU_MODE_USB_CHG:
	case GSU_MODE_USB_CHG_CHARGE_ENABLED:
		if ((from_uc == GSU_MODE_USB_CHG_POGO_VOUT) ||
		    (from_uc == GSU_MODE_USB_CHG_POGO_VOUT_CHARGE_ENABLED)) {
			ret = max77779_usecase_v1_pogo_vout_enable(uc_data, false, false);
			if (ret < 0)
				pr_err("%s: cannot tun off pogo_vout (%d)\n", __func__, ret);
		}
		fallthrough;
	case GSU_MODE_USB_DC:
		rtx_avail = false;
		break;
	case GSU_MODE_STANDBY:
	case GSU_MODE_STANDBY_BUCK_ON:
	case GSU_MODE_INPUT_SUSPEND:
		/* from POGO_VOUT to STBY */
		if ((from_uc == GSU_MODE_POGO_VOUT) ||
		    (from_uc == GSU_MODE_USB_OTG_POGO_VOUT) ||
		    (from_uc == GSU_MODE_USB_CHG_POGO_VOUT) ||
		    (from_uc == GSU_MODE_USB_CHG_POGO_VOUT_CHARGE_ENABLED)) {
			ret = max77779_usecase_v1_pogo_vout_enable(uc_data, false, false);
			if (ret < 0)
				pr_err("%s: cannot tun off pogo_vout (%d)\n", __func__, ret);
		} else if ((from_uc != GSU_MODE_USB_OTG_FRS) && bms_usecase_is_uc_otg(from_uc)) {
			max77779_usecase_v1_otg_enable(uc_data, false);
		} else if (from_uc == GSU_MODE_WLC_FWUPDATE) {
			max77779_usecase_wlc_fw_update_enable(&uc_data->common_data, false);
		} else if (from_uc == GSU_MODE_WLC_RX ||
			   from_uc == GSU_MODE_WLC_RX_CHARGE_ENABLED ||
			   from_uc == GSU_MODE_WLC_RX_SPOOFED) {
			max77779_usecase_v1_wlcrx_ext_bst_enable(uc_data, false);
		}

		if ((from_uc == GSU_MODE_WLC_TX) && !IS_ERR_OR_NULL(uc_data->rtx_ready))
			gpiod_set_value_cansleep(uc_data->rtx_ready, 0);

		/* rtx not avail for GSU_MODE_STANDBY_BUCK_ON */
		if (use_case == GSU_MODE_STANDBY_BUCK_ON)
			break;
		fallthrough;
	case GSU_RAW_MODE:
		/* just write the value to the register (it's in stby) */
		rtx_avail = true;
		break;
	case GSU_MODE_USB_WLC_RX:
		break;
	case GSU_MODE_WLC_FWUPDATE:
		rtx_avail = false;
		max77779_usecase_wlc_fw_update_enable(&uc_data->common_data, true);
		break;
	case GSU_MODE_POGO_VOUT:
		ret = max77779_usecase_v1_pogo_vout_enable(uc_data, true, false);

		/* wait for ext boost ready */
		if (ret == 0 && from_uc == GSU_MODE_USB_OTG_POGO_VOUT)
			msleep(4);
		break;
	case GSU_MODE_USB_CHG_POGO_VOUT:
	case GSU_MODE_USB_CHG_POGO_VOUT_CHARGE_ENABLED:
		ret = max77779_usecase_v1_pogo_vout_enable(uc_data, true, false);
		break;
	default:
		break;
	}

	if (!IS_ERR_OR_NULL(uc_data->rtx_available))
		gpiod_set_value_cansleep(uc_data->rtx_available, rtx_avail);

	return ret;
}

/* finish usecase configuration after max77779 mode register is set */
static int max77779_usecase_v1_finish_usecase(struct max77779_uc_v1_data *uc_data,
					      int use_case, int from_uc)
{
	int ret = 0;

	switch (use_case) {
	case GSU_MODE_WLC_TX:
		/* p9412 will not be in RX when powered from EXT */
		ret = max77779_usecase_v1_wlc_tx_enable(uc_data, use_case, true);
		if (ret < 0)
			return ret;
		break;
	default:
		if (from_uc == GSU_MODE_WLC_TX) {
			/* p9412 is already off from insel */
			ret = max77779_usecase_v1_wlc_tx_enable(uc_data, use_case, false);
			if (ret < 0)
				return ret;
			/* re-enable wlc in case of rx */
			ret = max77779_usecase_v1_wlc_en(uc_data, WLC_ENABLED);
			if (ret < 0)
				return ret;
		}
		break;
	}

	return ret;
}

#define cb_data_is_inflow_off(cb_data) \
	((cb_data)->chgin_off && (cb_data)->wlcin_off)

/*
 * adjust *INSEL (only one source can be enabled at a given time)
 * NOTE: providing compatibility with input_suspend makes this more complex
 * that it needs to be.
 * TODO(b/) sequoia has back to back FETs to isolate WLC from USB
 * and we likely don't need all this logic here.
 */
static int max77779_usecase_v1_set_insel(struct max77779_uc_v1_data *uc_data,
					 const struct bms_usecase_foreach_cb_data *cb_data,
					 int from_uc, int use_case)
{
	const u8 insel_mask = MAX77779_CHG_CNFG_12_CHGINSEL_MASK |
			      MAX77779_CHG_CNFG_12_WCINSEL_MASK;
	int wlc_on = cb_data->wlc_tx && !cb_data->dc_on;
	bool force_wlc = false;
	u8 insel_value = 0;
	int ret;

	if (cb_data->usb_wlc) {
		insel_value |= MAX77779_CHG_CNFG_12_WCINSEL;
		force_wlc = true;
	} else if (cb_data_is_inflow_off(cb_data)) {
		/*
		 * input_suspend masks both inputs but must still allow
		 * TODO: use a separate use case for usb + wlc
		 */
		 force_wlc = true;
	} else if (cb_data->buck_on && !cb_data->chgin_off) {
		insel_value |= MAX77779_CHG_CNFG_12_CHGINSEL;
	} else if (cb_data->wlc_rx && !cb_data->wlcin_off) {

		/* always disable WLC when USB is present */
		if (!cb_data->buck_on)
			insel_value |= MAX77779_CHG_CNFG_12_WCINSEL;
		else
			force_wlc = true;

	} else {
		/* disconnected, do not enable chgin if in input_suspend */
		if (!cb_data->chgin_off)
			insel_value |= MAX77779_CHG_CNFG_12_CHGINSEL;

		/* disconnected, do not enable wlc_in if in input_suspend */
		if (!cb_data->buck_on && (!cb_data->wlcin_off || cb_data->wlc_tx))
			insel_value |= MAX77779_CHG_CNFG_12_WCINSEL;

		force_wlc = true;
	}

	if (cb_data->pogo_vout) {
		/* always disable WCIN when pogo power out */
		insel_value &= ~MAX77779_CHG_CNFG_12_WCINSEL;
	} else if (cb_data->pogo_vin && !cb_data->wlcin_off) {
		/* always disable USB when Dock is present */
		insel_value &= ~MAX77779_CHG_CNFG_12_CHGINSEL;
		insel_value |= MAX77779_CHG_CNFG_12_WCINSEL;
	}

	if (from_uc != use_case || force_wlc || wlc_on) {
		enum wlc_state_t state;
		wlc_on = wlc_on || (insel_value & MAX77779_CHG_CNFG_12_WCINSEL) != 0;

		/* b/182973431 disable WLC_IC while CHGIN, rtx will enable WLC later */
		if (wlc_on)
			state = WLC_ENABLED;
		else if (uc_data->wlc_spoof)
			state = WLC_SPOOFED;
		else
			state = WLC_DISABLED;

		ret = max77779_usecase_v1_wlc_en_with_defender_reason(uc_data, state,
				cb_data->defender_enabled);

		if (ret < 0)
			dev_err(uc_data->dev, "%s: error wlc_en=%d ret:%d\n", __func__,
				wlc_on, ret);
	} else {
		u8 value = 0;

		wlc_on = max77779_external_chg_insel_read(uc_data->core, &value);
		if (wlc_on == 0)
			wlc_on = (value & MAX77779_CHG_CNFG_12_WCINSEL) != 0;
	}

	/* changing [CHGIN|WCIN]_INSEL: works when protection is disabled  */
	ret = max77779_external_chg_insel_write(uc_data->core, insel_mask, insel_value);

	dev_dbg(uc_data->dev, "%s: usecase=%d->%d mask=%x insel=%x wlc_on=%d force_wlc=%d (%d)\n",
		__func__, from_uc, use_case, insel_mask, insel_value, wlc_on,
		force_wlc, ret);

	return ret;
}

static int max77779_usecase_v1_get_charger_mode_from_uc(struct bms_usecase_entry *entry,
							struct max77779_uc_v1_data *uc_data)
{
	u8 reg, mode;
	bool dc_on = false;
	int use_case = entry->usecase;

	switch (use_case) {
	/* OTG */
	case GSU_MODE_USB_OTG_POGO_VOUT:
		mode = MAX77779_CHGR_MODE_BOOST_UNO_ON;
		break;
	case GSU_MODE_USB_OTG_FRS:
		mode = MAX77779_CHGR_MODE_OTG_BOOST_ON;
		break;
	case GSU_MODE_USB_OTG:
		if (!IS_ERR_OR_NULL(uc_data->ext_bst_ctl))
			mode = MAX77779_CHGR_MODE_ALL_OFF;
		else
			mode = MAX77779_CHGR_MODE_OTG_BOOST_ON;
		break;
	case GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED:
		if (!IS_ERR_OR_NULL(uc_data->ext_bst_ctl))
			mode = MAX77779_CHGR_MODE_CHGR_BUCK_ON;
		else
			mode = MAX77779_CHGR_MODE_CHGR_OTG_BUCK_BOOST_ON;
		break;
	case GSU_MODE_USB_OTG_WLC_RX:
		if (!IS_ERR_OR_NULL(uc_data->ext_bst_ctl))
			mode = MAX77779_CHGR_MODE_BUCK_ON;
		else
			mode = MAX77779_CHGR_MODE_CHGR_OTG_BUCK_BOOST_ON;
		break;
	case GSU_MODE_WLC_RX_STDBY:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		break;
	case GSU_MODE_USB_OTG_WLC_DC:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		dc_on = true;
		break;
	/* NON-OTG */
	case GSU_MODE_USB_CHG_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_USB_CHG_POGO_VOUT_CHARGE_ENABLED:
	case GSU_MODE_USB_WLC_RX:
	case GSU_MODE_DOCK_CHARGE_ENABLED:
		mode = MAX77779_CHGR_MODE_CHGR_BUCK_ON;
		break;
	case GSU_MODE_STANDBY:
	case GSU_MODE_STANDBY_BUCK_ON:
	case GSU_MODE_POGO_VOUT:
	case GSU_MODE_INPUT_SUSPEND:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		break;
	case GSU_MODE_DOCK:
	case GSU_MODE_USB_CHG:
	case GSU_MODE_WLC_RX:
	case GSU_MODE_USB_CHG_POGO_VOUT:
		mode = MAX77779_CHGR_MODE_BUCK_ON;
		break;
	case GSU_MODE_WLC_TX:
		mode = MAX77779_CHGR_MODE_BOOST_UNO_ON;
		break;
	case GSU_MODE_WLC_DC:
		mode = MAX77779_CHGR_MODE_ALL_OFF;
		dc_on = true;
		break;
	case GSU_MODE_WLC_RX_SPOOFED:
		mode = MAX77779_CHGR_MODE_BOOST_ON;
		break;
	case GSU_MODE_USB_DC:
		if (uc_data->chrg_byp_en)
			mode = MAX77779_CHGR_MODE_ALLOW_BYP;
		else
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
static int max77779_usecase_v1_get_otg_usecase(struct max77779_uc_v1_data *uc_data,
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

	if (cb_data->pogo_vout) {
		usecase = GSU_MODE_USB_OTG_POGO_VOUT;
	} else if (!cb_data->wlc_rx && !cb_data->wlc_tx) {
		/* 9: USB_OTG or  10: USB_OTG_FRS */
		if (cb_data->frs_on)
			usecase = GSU_MODE_USB_OTG_FRS;
		else
			usecase = GSU_MODE_USB_OTG;

		dc_on = false;
	} else if (cb_data->wlc_tx) {
		/* GSU_MODE_USB_OTG_WLC_TX not supported */
		return -EINVAL;
	} else if (cb_data->wlc_rx) {
		if (dc_on)
			usecase = GSU_MODE_USB_OTG_WLC_DC;
		else if (chgr_on)
			usecase = GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED;
		else
			usecase = GSU_MODE_USB_OTG_WLC_RX;
	} else {
		return -EINVAL;
	}

	return usecase;
}

static void max77779_usecase_v1_fixups(struct max77779_uc_v1_data *uc_data,
				       struct bms_usecase_foreach_cb_data *cb_data)
{
	/*
	 * force FRS if ext boost or NBC is not enabled
	 */
	const bool use_internal_bst = uc_data->vin_is_valid < 0 &&
				      IS_ERR_OR_NULL(uc_data->ext_bst_ctl);
	struct max77779_chgr_data *data = dev_get_drvdata(uc_data->core);

	/* USB will disable wlc_rx, tx */
	if (cb_data->buck_on && !uc_data->dcin_is_dock) {
		cb_data->wlc_rx = 0;
		cb_data->wlc_tx = 0;
	} else {
		cb_data->wlc_rx = (cb_data->wlc_rx && !data->wcin_input_suspend) ||
				  (uc_data->wlc_spoof && uc_data->wlc_spoof_vbyp);
	}

	cb_data->wlcin_off = !!data->wcin_input_suspend;

	dev_dbg(uc_data->dev, "%s: wcin_is_online=%d data->wcin_input_suspend=%d data->wlc_spoof=%d\n",
		__func__, max77779_wcin_is_online(data), data->wcin_input_suspend,
		uc_data->wlc_spoof);

	/* GSU_MODE_USB_OTG_WLC_DC not supported*/
	if (cb_data->dc_on && !uc_data->otg_wlc_dc_en && cb_data->wlc_rx)
		cb_data->otg_on = 0;

	if (cb_data->otg_on && use_internal_bst)
		cb_data->frs_on = cb_data->otg_on;
}

/*
 * Determines the use case to switch to. This is device/system dependent and
 * will likely be factored to a separate file (compile module).
 */
static int max77779_usecase_v1_get_usecase(void *uc_d, struct bms_usecase_foreach_cb_data *cb_data)
{
	struct max77779_uc_v1_data *uc_data = uc_d;
	const int buck_on = cb_data->chgin_off ? 0 : cb_data->buck_on;
	const int chgr_on = max77779_usecase_cb_data_is_chgr_on(cb_data);
	int dc_on = cb_data->dc_on; /* && !cb_data->charge_done */
	int usecase;

	if (max77779_usecase_is_debounce(&uc_data->common_data, cb_data)) {
		dev_dbg(uc_data->dev, "%s: DEBOUNCE callback\n", __func__);
		return BMS_USECASE_NO_HOPS;
	}

	max77779_usecase_v1_fixups(uc_data, cb_data);

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
		usecase = max77779_usecase_v1_get_otg_usecase(uc_data, cb_data);
	} else if (cb_data->usb_wlc)
		/* USB+WLC for factory and testing */
		usecase = GSU_MODE_USB_WLC_RX;
	else if (cb_data->pogo_vout) {
		if (!buck_on)
			usecase = GSU_MODE_POGO_VOUT;
		else if (chgr_on)
			usecase = GSU_MODE_USB_CHG_POGO_VOUT_CHARGE_ENABLED;
		else
			usecase = GSU_MODE_USB_CHG_POGO_VOUT;
	} else if (!buck_on && !cb_data->wlc_rx) {
		if (cb_data->buck_on)
			usecase = GSU_MODE_STANDBY_BUCK_ON;
		else if (cb_data->wlc_tx) /* Rtx using the internal battery */
			usecase = GSU_MODE_WLC_TX;
		else if (cb_data->wlcin_off && cb_data->chgin_off)
			usecase = GSU_MODE_INPUT_SUSPEND;
		else
			usecase = GSU_MODE_STANDBY;
	} else if (cb_data->wlc_tx) {
		/* above checks that buck_on is false */
		usecase = GSU_MODE_WLC_TX;
	} else if (cb_data->wlc_rx) {
		dc_on = (dc_on == GBMS_CHGR_SEL_WIRELESS);
		/* will be in mode 4 if in stby unless dc is enabled */
		if (chgr_on)
			usecase = GSU_MODE_WLC_RX_CHARGE_ENABLED;
		else if (cb_data->wlc_rx_stby)
			usecase = GSU_MODE_WLC_RX_STDBY;
		else
			usecase = GSU_MODE_WLC_RX;

		/* wired input should be disabled here */
		if (dc_on)
			usecase = GSU_MODE_WLC_DC;

		if (uc_data->dcin_is_dock)
			usecase = chgr_on ? GSU_MODE_DOCK_CHARGE_ENABLED : GSU_MODE_DOCK;

		if (uc_data->wlc_spoof && uc_data->wlc_spoof_vbyp)
			usecase = GSU_MODE_WLC_RX_SPOOFED;
	} else {
		dc_on = (dc_on == GBMS_CHGR_SEL_USB);
		/* MODE_BUCK_ON is inflow */
		if (chgr_on)
			usecase = GSU_MODE_USB_CHG_CHARGE_ENABLED;
		else
			usecase = GSU_MODE_USB_CHG;

		/*
		 * NOTE: OTG cases handled in max77779_get_otg_usecase()
		 * NOTE: usecases with !(buck|wlc)_on same as.
		 * NOTE: mode=0 if standby, mode=5 if charging, mode=0xa on otg
		 * TODO: handle rTx + DC and some more.
		 */
		if (dc_on && cb_data->wlc_rx)
			/* WLC_DC->WLC_DC+USB -> ignore dc_on */
			goto done;
		else if (dc_on)
			usecase = GSU_MODE_USB_DC;
		else if (cb_data->stby_on && !chgr_on)
			usecase = cb_data->buck_on ? GSU_MODE_STANDBY_BUCK_ON : GSU_MODE_STANDBY;
	}

done:
	return usecase;
}

static void max77779_usecase_v1_dump_usecase_config(struct max77779_uc_v1_data *uc_data)
{
	dev_info(uc_data->dev, "bst_on:%d, ext_bst_ctl: %d, ext_bst_mode:%d, ext_bst_supply:%d\n",
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
	dev_info(uc_data->dev, "wlc_en:%d, chrg_byp_en:%d rtx_ready:%d\n",
		 (IS_ERR_OR_NULL(uc_data->wlc_en)
		 ? (int)PTR_ERR(uc_data->wlc_en)
		 : desc_to_gpio(uc_data->wlc_en)),
		 uc_data->chrg_byp_en,
		 (IS_ERR_OR_NULL(uc_data->rtx_ready)
		 ? (int)PTR_ERR(uc_data->rtx_ready)
		 : desc_to_gpio(uc_data->rtx_ready)));
	dev_info(uc_data->dev, "rtx_available:%d, rx_to_rx_otg:%d ext_otg_only:%d wlc_spoof_gpio:%d\n",
		 (IS_ERR_OR_NULL(uc_data->rtx_available)
		 ? (int)PTR_ERR(uc_data->rtx_available)
		 : desc_to_gpio(uc_data->rtx_available)),
		 uc_data->rx_otg_en, uc_data->ext_otg_only,
		 (IS_ERR_OR_NULL(uc_data->wlc_spoof_gpio)
		 ? (int)PTR_ERR(uc_data->wlc_spoof_gpio)
		 : desc_to_gpio(uc_data->wlc_spoof_gpio)));
	dev_info(uc_data->dev, "pogo_vout_en:%d\n",
		 (IS_ERR_OR_NULL(uc_data->pogo_vout_en)
		 ? (int)PTR_ERR(uc_data->pogo_vout_en)
		 : desc_to_gpio(uc_data->pogo_vout_en)));
}

/* switch to a use case, handle the transitions */
static int max77779_usecase_v1_set_usecase(struct max77779_uc_v1_data *uc_data,
					   struct bms_usecase_foreach_cb_data *cb_data,
					   int use_case)
{
	const int from_uc = bms_usecase_get_usecase();
	int ret;

	/* Need this only for usecases that control the switches */
	if (uc_data->init_done <= 0) {
		uc_data->init_done = max77779_usecase_v1_setup_usecases((void **)&uc_data, NULL);
		if (uc_data->init_done <= 0)
			return -EAGAIN;
		max77779_usecase_v1_dump_usecase_config(uc_data);
	}

	/* always fix/adjust insel (solves multiple input_suspend) */
	ret = max77779_usecase_v1_set_insel(uc_data, cb_data, from_uc, use_case);
	if (ret < 0) {
		dev_err(uc_data->dev, "use_case=%d->%d set_insel failed ret:%d\n",
			from_uc, use_case, ret);
		return ret;
	}

	/* usbchg+wlctx will call _set_insel() multiple times. */
	if (from_uc == use_case)
		goto exit_done;

	/* transition from data->use_case to use_case */
	ret = max77779_usecase_v1_to_usecase(uc_data, use_case, from_uc);
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

	ret = max77779_usecase_v1_finish_usecase(uc_data, use_case, from_uc);
	if (ret < 0 && ret != -EAGAIN)
		dev_err(uc_data->dev, "Error finishing usecase config ret:%d\n", ret);

	return ret;
}

static int max77779_usecase_v1_completion_cb(void *d, struct bms_usecase_entry *entry)
{
	struct max77779_uc_v1_data *uc_data = d;
	struct bms_usecase_foreach_cb_data *cb_data = entry->cb_data;
	const int from_uc = bms_usecase_get_usecase();
	int ret;
	uint8_t reg;

	ret = max77779_external_chg_reg_read(uc_data->core, MAX77779_CHG_CNFG_00, &reg);
	if (ret < 0)
		dev_err(uc_data->dev, "Error reading mode reg ret:%d\n", ret);

	uc_data->reg = max77779_usecase_v1_get_charger_mode_from_uc(entry, uc_data);

	dev_info(uc_data->dev, "%s:%s use_case=%s(%d)->%s(%d) CHG_CNFG_00=%x->%x\n",
		__func__, cb_data->reason ? cb_data->reason : "<>",
		bms_usecase_to_str(from_uc), from_uc,
		bms_usecase_to_str(entry->usecase), entry->usecase,
		ret ? ret : reg, uc_data->reg);

	/* state machine that handle transition between states */
	return max77779_usecase_v1_set_usecase(uc_data, cb_data, entry->usecase);
}

/* lazy init on the switches */


static bool max77779_usecase_v1_setup_usecases_done(struct max77779_uc_v1_data *uc_data)
{
	return (PTR_ERR(uc_data->wlc_en) != -EPROBE_DEFER) &&
	       (PTR_ERR(uc_data->bst_on) != -EPROBE_DEFER) &&
	       (PTR_ERR(uc_data->ext_bst_mode) != -EPROBE_DEFER) &&
	       (PTR_ERR(uc_data->ext_bst_ctl) != -EPROBE_DEFER) &&
	       (PTR_ERR(uc_data->rtx_ready) != -EPROBE_DEFER) &&
	       (PTR_ERR(uc_data->wlc_spoof_gpio) != -EPROBE_DEFER) &&
	       (PTR_ERR(uc_data->rtx_available) != -EPROBE_DEFER);

	/* TODO: handle platform specific differences.. */
}

static int max77779_usecase_v1_usecase_hops(void *d, int from_uc, int to_uc)
{
	bool from_otg = false;
	bool need_stby = false;
	int hop = BMS_USECASE_NO_HOPS;
	struct max77779_uc_v1_data *uc_data = d;

	switch (from_uc) {
	case GSU_MODE_USB_CHG:
	case GSU_MODE_USB_CHG_CHARGE_ENABLED:
		if (to_uc == GSU_MODE_USB_OTG) {
			need_stby = uc_data->ext_bst_ctl >= 0;
			break;
		}

		need_stby = to_uc != GSU_MODE_USB_CHG &&
			    to_uc != GSU_MODE_USB_CHG_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_DOCK &&
			    to_uc != GSU_MODE_DOCK_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_DC &&
			    to_uc != GSU_MODE_USB_OTG_FRS;
		break;
	case GSU_MODE_WLC_RX:
	case GSU_MODE_WLC_RX_CHARGE_ENABLED:
	case GSU_MODE_WLC_RX_SPOOFED:
		/* HPP supported by device handled by wlc driver */
		need_stby = to_uc != GSU_MODE_WLC_RX &&
			    to_uc != GSU_MODE_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_WLC_RX_SPOOFED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_WLC_DC &&
			    to_uc != GSU_MODE_USB_OTG_WLC_DC;
		break;
	case GSU_MODE_WLC_TX:
		need_stby = true;
		break;
	case GSU_MODE_USB_OTG:
		from_otg = true;
		if (to_uc == GSU_MODE_USB_OTG_WLC_RX ||
		    to_uc == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED ||
		    to_uc == GSU_MODE_USB_OTG_WLC_DC)
			break;

		need_stby = true;
		break;
	case GSU_MODE_USB_OTG_FRS:
		from_otg = true;
		if (to_uc == GSU_MODE_USB_OTG_WLC_RX ||
		    to_uc == GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED ||
		    to_uc == GSU_MODE_USB_OTG_WLC_DC) {
			need_stby = uc_data->ext_bst_ctl >= 0;
			break;
		}

		need_stby = to_uc != GSU_MODE_USB_CHG &&
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
			    to_uc != GSU_MODE_WLC_RX_SPOOFED &&
			    to_uc != GSU_MODE_DOCK &&
			    to_uc != GSU_MODE_DOCK_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_WLC_RX &&
			    to_uc != GSU_MODE_WLC_DC &&
			    to_uc != GSU_MODE_USB_OTG_WLC_DC;
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
			    to_uc != GSU_MODE_WLC_RX_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_WLC_RX_SPOOFED;
		break;
	case GSU_RAW_MODE:
	case GSU_MODE_FWUPDATE:
	case GSU_MODE_WLC_FWUPDATE:
		need_stby = true;
		break;
	case GSU_MODE_USB_OTG_POGO_VOUT:
		from_otg = true;
		need_stby = to_uc != GSU_MODE_POGO_VOUT &&
			    to_uc != GSU_MODE_USB_OTG;
		break;
	case GSU_MODE_POGO_VOUT:
		need_stby = to_uc != GSU_MODE_USB_CHG_POGO_VOUT &&
			    to_uc != GSU_MODE_USB_CHG_POGO_VOUT_CHARGE_ENABLED &&
			    to_uc != GSU_MODE_USB_OTG_POGO_VOUT;
		break;
	case GSU_MODE_USB_CHG_POGO_VOUT:
	case GSU_MODE_USB_CHG_POGO_VOUT_CHARGE_ENABLED:
		need_stby = to_uc != GSU_MODE_POGO_VOUT;
		break;
	case GSU_MODE_STANDBY:
	case GSU_MODE_STANDBY_BUCK_ON:
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

	dev_info(uc_data->dev, "%s: use_case=%s(%d)->%s(%d) from_otg=%d need_stby=%d hop:%s\n",
		 __func__,
		 bms_usecase_to_str(from_uc), from_uc,
		 bms_usecase_to_str(to_uc), to_uc,
		 from_otg, need_stby, bms_usecase_to_str(hop));

	if (hop != BMS_USECASE_NO_HOPS)
		return hop;

	return need_stby ? GSU_MODE_STANDBY : BMS_USECASE_NO_HOPS;
}

static int max77779_usecase_v1_usecase_init(struct max77779_uc_v1_data *uc_data)
{
	struct bms_usecase_chg_data *chg_data;
	int ret;

	chg_data = devm_kzalloc(uc_data->dev, sizeof(struct bms_usecase_chg_data), GFP_KERNEL);
	if (!chg_data) {
		dev_err(uc_data->dev, "Error allocating bms_usecase_chg_data!!!\n");
		return -ENOMEM;
	}

	chg_data->dev = uc_data->dev;
	chg_data->num_devices = 1;
	chg_data->uc_data = (void *)uc_data;
	chg_data->cb_get_hops = max77779_usecase_v1_usecase_hops;
	chg_data->cb_get_usecase = max77779_usecase_v1_get_usecase;
	ret = bms_usecase_init(chg_data);
	if (ret < 0)
		return ret;

	ret = bms_usecase_register_completion_cb(uc_data, NULL, max77779_usecase_v1_completion_cb);
	if (ret < 0)
		dev_err(uc_data->dev, "Failed to register max77779 completion cb(%d)\n", ret);

	return ret;
}

static int max77779_usecase_v1_wlc_spoof_callback(struct gvotable_election *el,
						  const char *reason, void *value)
{
	struct max77779_uc_v1_data *uc_data = gvotable_get_data(el);
	struct max77779_chgr_data *data = dev_get_drvdata(uc_data->core);
	int spoof = (long)value > 0;
	bool wlc_rx;

	wlc_rx = (max77779_wcin_is_online(data) && !data->wcin_input_suspend);

	uc_data->wlc_spoof = spoof && wlc_rx;

	dev_info(uc_data->dev, "%s:wlc_spoof=%d\n", __func__, uc_data->wlc_spoof);

	return 0;
}

static int max77779_usecase_v1_setup_default_usecase(void **uc_d, struct device *dev)
{
	int ret;
	u32 spoof_vbyp;
	struct max77779_uc_v1_data *uc_data;

	*uc_d = devm_kzalloc(dev, sizeof(*uc_data), GFP_KERNEL);
	if (!*uc_d) {
		dev_err(dev, "Error allocating uc_data!!!\n");
		return -ENOMEM;
	}

	uc_data = *uc_d;
	uc_data->dev = dev;
	uc_data->core = dev->parent;

	/* external boost */
	uc_data->bst_on = ERR_PTR(-EPROBE_DEFER);
	uc_data->ext_bst_ctl = ERR_PTR(-EPROBE_DEFER);
	uc_data->ext_bst_mode = ERR_PTR(-EPROBE_DEFER);
	uc_data->ext_bst_supply = ERR_PTR(-EPROBE_DEFER);
	uc_data->pogo_vout_en = ERR_PTR(-EPROBE_DEFER);

	uc_data->wlc_en = ERR_PTR(-EPROBE_DEFER);
	uc_data->rtx_ready = ERR_PTR(-EPROBE_DEFER);
	uc_data->rtx_available = ERR_PTR(-EPROBE_DEFER);

	uc_data->wlc_spoof_gpio = ERR_PTR(-EPROBE_DEFER);

	mutex_init(&uc_data->ext_bst_lock);
	uc_data->wlc_spoof_vbyp = 0;
	uc_data->init_done = false;
	uc_data->mode_cb_debounce = true;

	/* OPTIONAL: wlc-spoof-vol */
	ret = of_property_read_u32(uc_data->dev->of_node, MAX77779_WLC_SPOOF_VBYP_OF_STRING,
				   &spoof_vbyp);
	if (ret < 0) {
		uc_data->wlc_spoof_vbyp = 0;
	} else {
		uc_data->wlc_spoof_vbyp = spoof_vbyp;

		uc_data->wlc_spoof_votable =
			gvotable_create_bool_election(NULL,
						max77779_usecase_v1_wlc_spoof_callback,
						uc_data);
		if (!uc_data->wlc_spoof_votable) {
			dev_err(uc_data->dev, "no wlc_spoof votable\n");
			return -ENXIO;
		}

		gvotable_set_vote2str(uc_data->wlc_spoof_votable, gvotable_v2s_int);
		gvotable_election_set_name(uc_data->wlc_spoof_votable, "WLC_SPOOF");
	}

	max77779_external_chg_reg_update(uc_data->core, MAX77779_CHG_CNFG_12,
					MAX77779_CHG_CNFG_12_WCIN_REG_MASK,
					_max77779_chg_cnfg_12_wcin_reg_set(0, 0x0));

	ret = max77779_usecase_common_data_init(&uc_data->common_data, dev);
	if (ret < 0) {
		dev_err(dev, "Error initing common data ret:%d\n", ret);
		return ret;
	}

	return max77779_usecase_v1_usecase_init(uc_data);
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
int max77779_usecase_v1_setup_usecases(void **uc_d, struct device *dev)
{
	struct max77779_chgr_data *data;
	struct max77779_uc_v1_data *uc_data = *uc_d;

	if (!uc_data)
		return max77779_usecase_v1_setup_default_usecase(uc_d, dev);

	data = dev_get_drvdata(uc_data->core);

	/* Use the parent device (max77779_charger) for DTS config for compat */

	/* control external boost if present */
	if (PTR_ERR(uc_data->bst_on) == -EPROBE_DEFER) {
		uc_data->bst_on = devm_gpiod_get_optional(uc_data->core, "max77779,bst-on", 0);
		if (!IS_ERR_OR_NULL(uc_data->bst_on))
			gpiod_direction_output(uc_data->bst_on, 0);
	}
	if (PTR_ERR(uc_data->ext_bst_ctl) == -EPROBE_DEFER)
		uc_data->ext_bst_ctl = devm_gpiod_get_optional(uc_data->core, "max77779,extbst-ctl",
							       0);
	if (PTR_ERR(uc_data->ext_bst_mode) == -EPROBE_DEFER) {
		uc_data->ext_bst_mode = devm_gpiod_get_optional(uc_data->core,
								"max77779,extbst-mode", 0);
		if (!IS_ERR_OR_NULL(uc_data->ext_bst_mode))
			gpiod_set_value_cansleep(uc_data->ext_bst_mode, 0);
	}

	if (PTR_ERR(uc_data->ext_bst_supply) == -EPROBE_DEFER)
		uc_data->ext_bst_supply = devm_regulator_get_optional(uc_data->dev,
								      "max77779,extbst");

	/*  wlc_rx: disable when chgin, CPOUT is safe */
	if (PTR_ERR(uc_data->wlc_en) == -EPROBE_DEFER)
		uc_data->wlc_en = devm_gpiod_get_optional(uc_data->core, "max77779,wlc-en",
							  GPIOD_ASIS
							  | GPIOD_FLAGS_BIT_NONEXCLUSIVE);

	/*  wlc_rx thermal throttle -> spoof online */
	if (PTR_ERR(uc_data->wlc_spoof_gpio) == -EPROBE_DEFER)
		uc_data->wlc_spoof_gpio = devm_gpiod_get_optional(uc_data->core,
								  "max77779,wlc-spoof",
								  GPIOD_ASIS);

	/* OPTIONAL: support wlc_rx -> wlc_rx+otg */
	uc_data->rx_otg_en = of_property_read_bool(uc_data->core->of_node,
						   "max77779,rx-to-rx-otg-en");

	uc_data->otg_wlc_dc_en = of_property_read_bool(uc_data->core->of_node,
						       "max77779,wlc-dc-otg-en");

	/* OPTIONAL: support external boost OTG only */
	uc_data->ext_otg_only = of_property_read_bool(uc_data->core->of_node,
						      "max77779,ext-otg-only");

	/* OPTIONAL: support chrg mode 0x1 during PPS */
	uc_data->chrg_byp_en = of_property_read_bool(uc_data->core->of_node,
						     "max77779,chrg-byp-en");

	/* OPTIONAL: set ILIM speed to slow for WLC */
	uc_data->slow_wlc_ilim = of_property_read_bool(uc_data->core->of_node,
						       "max77779,slow-wlc-ilim");

	/* OPTIONAL: support external boost for WLC_RX and OTG */
	uc_data->ext_rx_otg = of_property_read_bool(uc_data->core->of_node,
						    "max77779,ext-rx-otg");

	if (PTR_ERR(uc_data->rtx_ready) == -EPROBE_DEFER)
		uc_data->rtx_ready = devm_gpiod_get_optional(uc_data->core, "max77779,rtx-ready",
							     GPIOD_ASIS);

	if (PTR_ERR(uc_data->rtx_available) == -EPROBE_DEFER)
		uc_data->rtx_available = devm_gpiod_get_optional(uc_data->core,
								 "max77779,rtx-available",
								 GPIOD_ASIS);

	if (PTR_ERR(uc_data->pogo_vout_en) == -EPROBE_DEFER) {
		uc_data->pogo_vout_en = devm_gpiod_get_optional(uc_data->core,
								"max77779,pogo-vout-sw-en",
								GPIOD_ASIS);
		if (!IS_ERR_OR_NULL(uc_data->pogo_vout_en))
			gpiod_direction_output(uc_data->pogo_vout_en, 0);
	}

	return max77779_usecase_v1_setup_usecases_done(uc_data);
}
EXPORT_SYMBOL_GPL(max77779_usecase_v1_setup_usecases);

int max77779_usecase_v1_usecase_remove(void *uc_d)
{
	struct max77779_uc_v1_data *uc_data = uc_d;

	if (uc_data->wlc_spoof_votable && (uc_data->wlc_spoof_votable != ERR_PTR(-EPROBE_DEFER)))
		gvotable_destroy_election(uc_data->wlc_spoof_votable);

	return 0;
}
EXPORT_SYMBOL_GPL(max77779_usecase_v1_usecase_remove);
