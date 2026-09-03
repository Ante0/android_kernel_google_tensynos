// SPDX-License-Identifier: GPL-2.0-only
/*
 * devfreq_tj_cdev_helper.c helper to register the devfreq tj cooling device.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "devfreq_tj_cdev_helper.h"
#include "devfreq_tj_cdev_helper_mock.h"

static char cdev_names[HW_CDEV_MAX][THERMAL_NAME_LENGTH] = {
	[HW_CDEV_GPU] "gpu-tj",
	[HW_CDEV_TPU] "tpu-tj",
	[HW_CDEV_AUR] "aurora-tj",
};

static int devfreq_tj_get_max_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	struct devfreq_tj_cdev *cdev_tj = cdev->devdata;

	*state = cdev_tj->cdev.num_opps - 1;
	return 0;
}

static int devfreq_tj_get_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	struct devfreq_tj_cdev *cdev_tj = cdev->devdata;

	*state = cdev_tj->cur_cdev_state;
	return 0;
}

static int devfreq_tj_set_cur_state(struct thermal_cooling_device *cdev,
					 unsigned long state)
{
	return -EOPNOTSUPP;
}

static const struct thermal_cooling_device_ops devfreq_tj_ops = {
	.get_cur_state = devfreq_tj_get_cur_state,
	.set_cur_state = devfreq_tj_set_cur_state,
	.get_max_state = devfreq_tj_get_max_state,
};

void devfreq_tj_cdev_cleanup(struct devfreq_tj_cdev *cdev_tj)
{
	devfreq_tj_cdev_devfreq_exit(&cdev_tj->cdev);
	if (cdev_tj->nb.notifier_call) {
		devfreq_tj_cpm_mbox_unregister_notification(cdev_tj->cdev_id, &cdev_tj->nb);
		cdev_tj->nb.notifier_call = NULL;
	}
}

int __devfreq_tj_cdev_cb(struct notifier_block *nb, unsigned long val, void *data)
{
	int i;
	struct devfreq_tj_cdev *cdev_tj = container_of(nb, struct devfreq_tj_cdev, nb);
	unsigned long freq;
	u32 *freq_req = data;

	if (!is_thermal_cpm_throttle_message(data))
		return 0;

	mutex_lock(&cdev_tj->lock);
	for (i = 1; i < cdev_tj->cdev.num_opps; i++) {
		if (cdev_tj->cdev.opp_table[i].freq > freq_req[1])
			break;
	}
	freq = cdev_tj->cdev.opp_table[i - 1].freq;
	cdev_tj->cur_cdev_state = cdev_tj->cdev.num_opps - i;
	dev_dbg(cdev_tj->dev, "request:%u freq limit:%lu state:%lu\n",
		 freq_req[1], freq, cdev_tj->cur_cdev_state);
	devfreq_tj_cdev_pm_qos_update_request(&cdev_tj->cdev, freq);
	mutex_unlock(&cdev_tj->lock);

	return 0;
}

void __devfreq_tj_cdev_success(struct cdev_devfreq_data *cdev)
{
	struct devfreq_tj_cdev *cdev_tj = container_of(cdev, struct devfreq_tj_cdev, cdev);
	struct thermal_cooling_device *thermal_cdev;
	int ret = 0;

	cdev_tj->nb.notifier_call = __devfreq_tj_cdev_cb;
	ret = devfreq_tj_cpm_mbox_register_notification(cdev_tj->cdev_id, &cdev_tj->nb);
	if (ret) {
		dev_err(cdev_tj->dev,
			"Error registering for CPM callback. err:%d\n", ret);
		goto success_cb_failure;
	}

	thermal_cdev = devfreq_tj_of_cooling_device_register(
			cdev_tj->dev,
			dev_of_node(cdev_tj->dev),
			cdev_names[cdev_tj->cdev_id],
			cdev_tj,
			&devfreq_tj_ops);
	if (IS_ERR(thermal_cdev)) {
		dev_err(cdev_tj->dev,
			"Error registering cdev:%s. err:%ld,\n",
			cdev_names[cdev_tj->cdev_id], PTR_ERR(thermal_cdev));
		goto success_cb_failure;
	}
	dev_info(cdev_tj->dev, "cdev:%s registered.\n", cdev_names[cdev_tj->cdev_id]);

	return;

success_cb_failure:
	cdev_tj->nb.notifier_call = NULL;
	devfreq_tj_cdev_cleanup(cdev_tj);
	devfreq_tj_fatal_error();
}

void __devfreq_tj_cdev_exit(struct cdev_devfreq_data *cdev)
{
	struct devfreq_tj_cdev *cdev_tj = container_of(cdev, struct devfreq_tj_cdev, cdev);

	devfreq_tj_cdev_cleanup(cdev_tj);
	devfreq_tj_fatal_error();
}

int devfreq_tj_cdev_probe_helper(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev;
	struct device_node *np;
	struct devfreq_tj_cdev *cdev_tj;

	dev = &pdev->dev;
	np = dev_of_node(dev);

	cdev_tj = devm_kzalloc(dev, sizeof(*cdev_tj), GFP_KERNEL);
	if (!cdev_tj)
		return -ENOMEM;
	cdev_tj->dev = dev;
	mutex_init(&cdev_tj->lock);

	ret = devfreq_tj_property_read_u32(np, "thermal-hw-cdev-id", &cdev_tj->cdev_id);
	if (ret) {
		dev_err(dev,
			"Error reading property thermal-hw-cdev-id. ret:%d\n",
			ret);
		return ret;
	}
	if (cdev_tj->cdev_id != HW_CDEV_GPU) {
		dev_err(dev, "Invalid HW cdev ID:%d.", cdev_tj->cdev_id);
		return -EINVAL;
	}

	cdev_tj->np = devfreq_tj_parse_phandle(np, "thermal-devfreq", 0);
	if (!cdev_tj->np) {
		dev_err(dev, "Error reading property thermal-devfreq.\n");
		return -EINVAL;
	}

	ret = devfreq_tj_cdev_devfreq_init(&cdev_tj->cdev, cdev_tj->np, cdev_tj->cdev_id,
					   __devfreq_tj_cdev_success,
					   __devfreq_tj_cdev_exit);
	if (ret) {
		dev_err(dev, "Setup error. ret:%d.\n", ret);
		return ret;
	}
	platform_set_drvdata(pdev, cdev_tj);

	return ret;
}
