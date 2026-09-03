// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_bcl_votable.c Google bcl votable driver
 *
 * Copyright (c) 2023, Google LLC. All rights reserved.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <misc/gvotable.h>
#include "bcl.h"

#include "core_pmic/core_pmic_defs.h"
#include "ifpmic/max77759/max77759_irq.h"
#include "ifpmic/max77779/max77779_irq.h"
#include "soc/soc_defs.h"

#define BCL_WLC "BCL_WLC"
#define BCL_USB "BCL_USB"
#define BCL_USB_OTG "BCL_USB_OTG"

enum {
	USB_PLUGGED,
	USB_UNPLUGGED,
};

static int get_batoilo_limit_mA(struct bcl_device *bcl_dev, int index, u8 level)
{
	if (index == 0)
		return level * BO_STEP +
		       bcl_dev->batt_irq_conf1.batoilo_lower_limit;
	return level * BO_STEP + bcl_dev->batt_irq_conf2.batoilo_lower_limit;
}

static int google_bcl_wlc_votable_callback(struct gvotable_election *el,
					   const char *reason, void *value)
{
	struct bcl_device *bcl_dev = gvotable_get_data(el);
	int ret = 0;
	bool wlc_tx_enable;
	u8 new_oilo_trig_lvls[2];

	if (!bcl_dev->ifpmic_ops)
		return -ENODEV;

	/* Ensure BCL driver is initialized before receiving callback. */
	if (!smp_load_acquire(&bcl_dev->initialized))
		return -EINVAL;

	if (IS_ENABLED(CONFIG_GOOGLE_BCL_MAX77779) &&
	    bcl_dev->ifpmic == MAX77779) {
		wlc_tx_enable = (long)value == WLC_ENABLED_TX;

		if (wlc_tx_enable) {
			new_oilo_trig_lvls[0] =
				bcl_dev->batt_irq_conf1.batoilo_wlc_trig_lvl;
			new_oilo_trig_lvls[1] =
				bcl_dev->batt_irq_conf2.batoilo_wlc_trig_lvl;
		} else {
			new_oilo_trig_lvls[0] =
				bcl_dev->batt_irq_conf1.batoilo_trig_lvl;
			new_oilo_trig_lvls[1] =
				bcl_dev->batt_irq_conf2.batoilo_trig_lvl;
		}

		ret = bcl_dev->ifpmic_ops->set_oilo1(
			bcl_dev->ifpmic_irq_dev,
			get_batoilo_limit_mA(bcl_dev, 0,
					     new_oilo_trig_lvls[0]));
		if (ret) {
			dev_err(bcl_dev->device,
				"WLC: BATOILO1 cannot be adjusted\n");
			return ret;
		}

		ret = bcl_dev->ifpmic_ops->set_oilo2(
			bcl_dev->ifpmic_irq_dev,
			get_batoilo_limit_mA(bcl_dev, 1,
					     new_oilo_trig_lvls[1]));
		if (ret) {
			dev_err(bcl_dev->device,
				"WLC: BATOILO2 cannot be adjusted\n");
			return ret;
		}
	}
	/* b/335695535 outlines max77759 configuration */

	return ret;
}

static int google_bcl_usb_votable_callback(struct gvotable_election *el,
					   const char *reason, void *value)
{
	int ret = 0;
	struct bcl_device *bcl_dev = gvotable_get_data(el);
	const bool is_usb_plugged = (long)value == USB_PLUGGED;
	struct power_supply *psy_to_check;
	union power_supply_propval batt_status = {};
	u8 new_oilo_trig_lvls[2], new_scratch;

	if (!bcl_dev->ifpmic_ops)
		return -ENODEV;

	/* Ensure BCL driver is initialized before receiving callback. */
	if (!smp_load_acquire(&bcl_dev->initialized))
		return -EINVAL;

	psy_to_check = (bcl_dev->usb_otg_conf && bcl_dev->otg_psy) ?
			       bcl_dev->otg_psy :
			       bcl_dev->main_charger_psy;

	ret = power_supply_get_property(psy_to_check, POWER_SUPPLY_PROP_STATUS,
					&batt_status);
	if (ret) {
		dev_err(bcl_dev->device,
			"Failed to get charger/otg status: %d\n", ret);
		return -EINVAL;
	}

	if (batt_status.intval == POWER_SUPPLY_STATUS_DISCHARGING) {
		ret = get_scratch_value(bcl_dev, is_usb_plugged, &new_scratch);
		if (ret == 0)
			core_pmic_set_scratch_pad(bcl_dev, new_scratch);
	}

	if (is_usb_plugged &&
	    (batt_status.intval == POWER_SUPPLY_STATUS_DISCHARGING)) {
		new_oilo_trig_lvls[0] =
			bcl_dev->batt_irq_conf1.batoilo_otg_trig_lvl;
		new_oilo_trig_lvls[1] =
			bcl_dev->batt_irq_conf2.batoilo_otg_trig_lvl;
	} else if (is_usb_plugged) {
		new_oilo_trig_lvls[0] =
			bcl_dev->batt_irq_conf1.batoilo_usb_trig_lvl;
		new_oilo_trig_lvls[1] =
			bcl_dev->batt_irq_conf2.batoilo_usb_trig_lvl;
	} else {
		new_oilo_trig_lvls[0] =
			bcl_dev->batt_irq_conf1.batoilo_trig_lvl;
		new_oilo_trig_lvls[1] =
			bcl_dev->batt_irq_conf2.batoilo_trig_lvl;
	}

	ret = bcl_dev->ifpmic_ops->set_oilo1(
		bcl_dev->ifpmic_irq_dev,
		get_batoilo_limit_mA(bcl_dev, 0, new_oilo_trig_lvls[0]));
	if (ret) {
		dev_err(bcl_dev->device, "USB: BATOILO1 cannot be adjusted\n");
		return ret;
	}

	if (bcl_dev->ifpmic == MAX77779) {
		ret = bcl_dev->ifpmic_ops->set_oilo2(
			bcl_dev->ifpmic_irq_dev,
			get_batoilo_limit_mA(bcl_dev, 1,
					     new_oilo_trig_lvls[1]));
		if (ret)
			dev_err(bcl_dev->device,
				"USB: BATOILO2 cannot be adjusted\n");
	}

	return ret;
}

int google_bcl_setup_votable(struct bcl_device *bcl_dev)
{
	struct power_supply *wlc_psy;
	union power_supply_propval wlc_online = {};
	int ret;

	bcl_dev->toggle_wlc = gvotable_create_bool_election(NULL, google_bcl_wlc_votable_callback,
							    bcl_dev);
	if (IS_ERR_OR_NULL(bcl_dev->toggle_wlc)) {
		ret = PTR_ERR(bcl_dev->toggle_wlc);
		dev_err(bcl_dev->device, "no toggle_wlc votable (%d)\n", ret);
		return ret;
	}
	gvotable_set_vote2str(bcl_dev->toggle_wlc, gvotable_v2s_int);
	gvotable_election_set_name(bcl_dev->toggle_wlc, BCL_WLC);
	gvotable_set_default(bcl_dev->toggle_wlc,
			     (void *)(long)WLC_DISABLED_TX);

	bcl_dev->toggle_usb = gvotable_create_bool_election(NULL, google_bcl_usb_votable_callback,
							    bcl_dev);
	if (IS_ERR_OR_NULL(bcl_dev->toggle_usb)) {
		ret = PTR_ERR(bcl_dev->toggle_usb);
		gvotable_destroy_election(bcl_dev->toggle_wlc);
		dev_err(bcl_dev->device, "no toggle_usb votable (%d)\n", ret);
		return ret;
	}
	gvotable_set_vote2str(bcl_dev->toggle_usb, gvotable_v2s_int);
	gvotable_election_set_name(bcl_dev->toggle_usb, BCL_USB);
	gvotable_set_default(bcl_dev->toggle_usb, (void *)(long)USB_UNPLUGGED);

	wlc_psy = power_supply_get_by_name("wireless");
	if (wlc_psy) {
		memset(&wlc_online, 0, sizeof(wlc_online));
		ret = power_supply_get_property(
			wlc_psy, POWER_SUPPLY_PROP_ONLINE, &wlc_online);
		if (ret == 0) {
			gvotable_cast_vote(
				bcl_dev->toggle_wlc, "BCL_DEV_VOTER", (void *)0,
				(void *)(long)(wlc_online.intval ?
						       WLC_ENABLED_TX :
						       WLC_DISABLED_TX));
		}
		power_supply_put(wlc_psy);
	}

	return 0;
}

void google_bcl_remove_votable(struct bcl_device *bcl_dev)
{
	if (!IS_ERR_OR_NULL(bcl_dev->toggle_wlc))
		gvotable_destroy_election(bcl_dev->toggle_wlc);
	if (!IS_ERR_OR_NULL(bcl_dev->toggle_usb))
		gvotable_destroy_election(bcl_dev->toggle_usb);
}
