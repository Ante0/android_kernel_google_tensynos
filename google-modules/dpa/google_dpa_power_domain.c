// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/device.h>
#include <linux/of.h>
#include <linux/pm_domain.h>

#include "google_dpa_power_domain.h"

static int google_dpa_validate_power_domains(struct device *dev)
{
	struct device_node *node = dev->of_node;
	struct device_node *pd1_node, *pd2_node;
	int ret = 0;
	int num_pd;

	num_pd = of_count_phandle_with_args(dev->of_node, "power-domains", NULL);
	if (num_pd < 0) {
		dev_err(dev, "Failed to parse power-domains property\n");
		return num_pd;
	}
	if (num_pd != 2) {
		dev_err(dev,
			"The number of power-domain shall be two and these two has to be the same. This is for avoid turning on DPA power-domain during probe/remove.\n");
		return -EINVAL;
	}

	pd1_node = of_parse_phandle(node, "power-domains", 0);
	if (IS_ERR_OR_NULL(pd1_node)) {
		dev_err(dev, "Failed to parse first power domain.\n");
		return pd1_node ? PTR_ERR(pd1_node) : -EINVAL;
	}
	pd2_node = of_parse_phandle(node, "power-domains", 1);
	if (IS_ERR_OR_NULL(pd2_node)) {
		dev_err(dev, "Failed to parse second power domain.\n");
		ret = pd2_node ? PTR_ERR(pd2_node) : -EINVAL;
		goto put_pd1;
	}

	ret = pd1_node == pd2_node ? 0 : -EINVAL;
	if (ret)
		dev_err(dev,
			"The number of power-domain shall be two and these two has to be the same. This is for avoid turning on DPA power-domain during probe/remove.\n");

	of_node_put(pd2_node);
put_pd1:
	of_node_put(pd1_node);

	return ret;
}

int google_dpa_attach_power_domain(struct device *dev, struct device **out_pd_vdev,
				   struct device_link **out_link)
{
	struct device *pd_vdev;
	struct device_link *link;
	int ret;

	ret = google_dpa_validate_power_domains(dev);
	if (ret)
		return ret;
	/*
	 * This is a workaround for a HW limitation.
	 * The allowed DPA power transition is OFF->WAIT->ON->OFF.
	 * The initial power on is OFF->WAIT.
	 * We cannot turn off DPA until we release NCP from the wait state.
	 * Linux kernel turns on the power-domain if there is only one
	 * power-domain that attach to the device. By attaching multiple
	 * power-domains, Linux kernel does not power on the power domains.
	 */
	pd_vdev = dev_pm_domain_attach_by_id(dev, 0);
	if (IS_ERR_OR_NULL(pd_vdev)) {
		dev_err(dev, "Failed to attach to DPA power domain\n");
		return pd_vdev ? PTR_ERR(pd_vdev) : -EINVAL;
	}
	*out_pd_vdev = pd_vdev;

	link = device_link_add(dev, pd_vdev, DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
	if (!link) {
		dev_err(dev,
			"Failed to create devlink between device and DPA PD virtual device.\n");
		ret = -EINVAL;
		goto detach_pd;
	}
	*out_link = link;

	return 0;

detach_pd:
	dev_pm_domain_detach(pd_vdev, true);

	return ret;
}

void google_dpa_detach_power_domain(struct device *pd_vdev, struct device_link *link)
{
	device_link_del(link);
	dev_pm_domain_detach(pd_vdev, true);
}
