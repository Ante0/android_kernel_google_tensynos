// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

#include "google_dpa_internal.h"
#include "google_dpa_rpc_internal.h"
#include "google_dpa_rpc_sysfs.h"
#include "services/google_dpa_services.h"

static ssize_t rpc_check_health_show(struct device *dev, struct device_attribute *attr, char *buf,
				     PwRpcClient *client)
{
	int check_health_ret;
	ssize_t ret;

	check_health_ret = dpa_rpc_check_health(dev, client);

	if (check_health_ret) {
		dev_err(dev, "health check failed with error: %d", check_health_ret);
		ret = sysfs_emit(buf, "unhealthy\n");
	} else {
		ret = sysfs_emit(buf, "healthy\n");
	}

	return ret;
}

static ssize_t ncp_check_health_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return rpc_check_health_show(dev, attr, buf, google_dpa_rpc_ncp_client());
}

static struct device_attribute attr_ncp_health =
	__ATTR(check_health, 0400, ncp_check_health_show, NULL);

static struct attribute *google_dpa_rpc_ncp_client_attrs[] = {
	&attr_ncp_health.attr,
	NULL,
};

static ssize_t nep_check_health_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return rpc_check_health_show(dev, attr, buf, google_dpa_rpc_nep_client());
}

static struct device_attribute attr_nep_health =
	__ATTR(check_health, 0400, nep_check_health_show, NULL);

static struct attribute *google_dpa_rpc_nep_client_attrs[] = {
	&attr_nep_health.attr,
	NULL,
};

static struct attribute_group google_dpa_rpc_ncp_client_attr_group = {
	.attrs = google_dpa_rpc_ncp_client_attrs,
	.name = "rpc-ncp",
};

static struct attribute_group google_dpa_rpc_nep_client_attr_group = {
	.attrs = google_dpa_rpc_nep_client_attrs,
	.name = "rpc-nep",
};

int google_dpa_rpc_sysfs_init(struct google_dpa *dpa)
{
	int ret = 0;

	ret = devm_device_add_group(dpa->dev, &google_dpa_rpc_ncp_client_attr_group);
	if (ret)
		return ret;

	ret = devm_device_add_group(dpa->dev, &google_dpa_rpc_nep_client_attr_group);
	if (ret)
		return ret;

	return 0;
}
