// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 *
 */

#include "ifpmic_defs.h"
#include "ifpmic_class.h"
#include "max77759/max77759_irq.h"
#include "max77779/max77779_irq.h"
#include <max77759_regs.h>
#include <max77779_regs.h>
#include <max77779.h>
#include <max777x9_bcl.h>

int ifpmic_setup_dev(struct bcl_device *bcl_dev)
{
	u32 ifpmic = M77759;
	u8 regval;
	struct device_node *np = bcl_dev->device->of_node;
	int ret;

	of_property_read_u32(np, "google,ifpmic", &ifpmic);
	bcl_dev->ifpmic = (ifpmic == M77759) ? MAX77759 : MAX77779;

	if (!of_property_present(np, "google,charger"))
		return dev_err_probe(bcl_dev->device, -ENODEV,
				     "Cannot find Charger I2C\n");
	bcl_dev->intf_pmic_dev = max77779_get_dev(bcl_dev->device, "google,charger");

	bcl_dev->ifpmic_irq_drv_en = of_property_read_bool(np, "ifpmic_irq_drv_en");

	if (!bcl_dev->intf_pmic_dev)
		return dev_err_probe(bcl_dev->device, -EPROBE_DEFER,
				     "Cannot find Charger I2C/SPMI\n");

	if (IS_ENABLED(CONFIG_GOOGLE_BCL_MAX77759))
		ret = max77759_external_reg_read(bcl_dev->intf_pmic_dev,
						 MAX77759_CHG_CNFG_14, &regval);
	else
		ret = max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
						     MAX77779_BAT_OILO1_CNFG_0, &regval);

	/*
	 * Errors from the above test read presumably happen because the
	 * charger driver hasn't fully finished probing. Convert all errors
	 * to -EPROBE_DEFER.
	 */
	if (ret)
		return dev_err_probe(bcl_dev->device, -EPROBE_DEFER,
				     "Test register read returned err %d\n", ret);

	return 0;
}

int ifpmic_setup(struct bcl_device *bcl_dev, struct platform_device *pdev)
{
	if (IS_ENABLED(CONFIG_GOOGLE_BCL_MAX77759)) {
		if (max77759_ifpmic_setup(bcl_dev, pdev) < 0)
			return -ENODEV;
	} else {
		if (max77779_ifpmic_setup(bcl_dev, pdev) < 0)
			return -ENODEV;
	}

	return 0;
}

int ifpmic_init_fs(struct bcl_device *bcl_dev)
{
	int ret;

	ret = ifpmic_class_create();
	if (ret) {
		dev_err(bcl_dev->device, "Failed to create class(pmic) %d\n", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_GOOGLE_BCL_MAX77759))
		bcl_dev->mitigation_dev = ifpmic_device_create_with_groups(NULL, bcl_dev,
									   mitigation_mw_groups,
									   "mitigation");
	else
		bcl_dev->mitigation_dev = ifpmic_device_create_with_groups(NULL, bcl_dev,
									   mitigation_sq_groups,
									   "mitigation");
	if (IS_ERR(bcl_dev->mitigation_dev)) {
		ifpmic_class_destroy();
		return -ENODEV;
	}

	return 0;
}

void ifpmic_destroy_fs(struct bcl_device *bcl_dev)
{
	if (!IS_ERR_OR_NULL(bcl_dev->mitigation_dev))
		ifpmic_device_destroy(bcl_dev->mitigation_dev->devt);

	ifpmic_class_destroy();
}

bool ifpmic_retrieve_batoilo_asserted(struct device *dev, enum IFPMIC ifpmic)
{
	int ret, assert;
	u8 regval;

	if (IS_ENABLED(CONFIG_GOOGLE_BCL_MAX77759))
		return true;

	ret = max77779_external_chg_reg_read(dev, MAX77779_CHG_DETAILS_01, &regval);
	if (ret < 0) {
		dev_err(dev, "IRQ read: %d, fail\n", regval);
		return false;
	}
	assert = _max77779_chg_details_01_bat_dtls_get(regval);
	if (assert == BAT_DTLS_OILO_ASSERTED)
		return true;
	return false;
}

void ifpmic_teardown(struct bcl_device *bcl_dev)
{
	if (bcl_dev->rd_fg_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->rd_fg_work);
}
