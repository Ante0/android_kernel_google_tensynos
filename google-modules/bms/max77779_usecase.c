// SPDX-License-Identifier: GPL-2.0
/*
 * Max77779 Usecase State machine
 *
 * Copyright 2024 Google LLC
 *
 */

#include <linux/of.h>

#include "max77779.h"
#include "max77779_usecase_v1.h"
#include "max77779_usecase_v2.h"

static int max77779_usecase_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct max77779_usecase_data *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->dev->init_name = "max77779_usecase";

	ret = of_property_read_u32(dev->of_node, "max77779,usecase-version", &data->ver);
	if (ret < 0)
		data->ver = MAX77779_USECASE_VERSION_DEFAULT;

	switch (data->ver) {
	case MAX77779_USECASE_VERSION_DEFAULT:
		ret = max77779_usecase_v1_setup_usecases(&data->uc_data, dev);
		break;
	case MAX77779_USECASE_VERSION_2:
		ret = max77779_usecase_v2_setup_usecases(&data->uc_data, dev);
		break;
	default:
		dev_err(dev, "Invalid usecase version:%d\n", data->ver);
		ret = -EINVAL;
	}

	if (ret == 0)
		platform_set_drvdata(pdev, data);

	return ret;
}

static void max77779_usecase_remove(struct platform_device *pdev)
{
	struct max77779_usecase_data *data = platform_get_drvdata(pdev);

	switch (data->ver) {
	case MAX77779_USECASE_VERSION_DEFAULT:
		max77779_usecase_v1_usecase_remove(data->uc_data);
		break;
	case MAX77779_USECASE_VERSION_2:
		max77779_usecase_v2_usecase_remove(data->uc_data);
		break;
	default:
		dev_err(data->dev, "Invalid usecase version:%d\n", data->ver);
	}
}

int max77779_usecase_common_data_init(struct max77779_usecase_data *data, struct device *dev)
{
	data->dev = dev;
	data->core = dev->parent;
	data->mode_cb_debounce = true;

	return 0;
}
EXPORT_SYMBOL_GPL(max77779_usecase_common_data_init);

/*
 * It could use cb_data->charge_done to turn off charging.
 * TODO: change chgr_on=>2 to (cc_max && chgr_ena)
 */
bool max77779_usecase_cb_data_is_chgr_on(const struct bms_usecase_foreach_cb_data *cb_data)
{
	return cb_data->stby_on || cb_data->charge_off ? 0 : (cb_data->chgr_on >= 2);
}
EXPORT_SYMBOL_GPL(max77779_usecase_cb_data_is_chgr_on);

int max77779_usecase_wlc_fw_update_enable(struct max77779_usecase_data *data, bool enable)
{
	return max77779_external_chg_reg_write(data->core,
					       MAX77779_CHG_CNFG_11,
					       enable ? MAX77779_CHG_REVERSE_BOOST_VOUT_6V : 0x0);
}
EXPORT_SYMBOL_GPL(max77779_usecase_wlc_fw_update_enable);

bool max77779_usecase_is_debounce(struct max77779_usecase_data *data,
				  struct bms_usecase_foreach_cb_data *cb_data)
{
	bool debounce;

	debounce = data->mode_cb_debounce && !(cb_data->chgr_on == 2) &&
		!cb_data->stby_on && !cb_data->use_raw && !cb_data->fwupdate_on &&
		(!cb_data->chgin_off || (cb_data->chgin_off && cb_data->wlc_rx)) &&
		(!cb_data->wlcin_off || (cb_data->wlcin_off && cb_data->buck_on)) &&
		(!cb_data->otg_on || (cb_data->otg_on && cb_data->wlc_rx)) &&
		!cb_data->usb_wlc && !cb_data->frs_on;

	if (!debounce)
		data->mode_cb_debounce = false;

	return debounce;
}
EXPORT_SYMBOL_GPL(max77779_usecase_is_debounce);

static const struct of_device_id max77779_usecase_of_match[] = {
	{.compatible = "max77779,usecase"},
	{},
};
MODULE_DEVICE_TABLE(of, max77779_usecase_of_match);


static struct platform_driver max77779_usecase_driver = {
	.driver = {
		   .name = "max77779-usecase",
		   .owner = THIS_MODULE,
		   .of_match_table = max77779_usecase_of_match,
		   .probe_type = PROBE_PREFER_ASYNCHRONOUS,
		   },
	.probe = max77779_usecase_probe,
	.remove = max77779_usecase_remove,
};

module_platform_driver(max77779_usecase_driver);

MODULE_DESCRIPTION("MAX77779 Usecase Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(MAX77779_USECASE);
