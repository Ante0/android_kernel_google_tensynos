// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */

#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <drivers/hwtracing/coresight/coresight-etm4x.h>

#include "generic_source.h"

DEFINE_CORESIGHT_DEVLIST(soc_source_devs, "soc_source");

static int soc_source_enable(struct coresight_device *csdev,
			     struct perf_event *event, enum cs_mode mode,
			     struct coresight_trace_id_map *id_map)
{
	if (!coresight_take_mode(csdev, mode))
		return -EBUSY;
	dev_info(&csdev->dev, "%s enabled\n", __func__);
	return 0;
}

static void soc_source_disable(struct coresight_device *csdev,
			       struct perf_event *event)
{
	coresight_set_mode(csdev, CS_MODE_DISABLED);
	dev_info(&csdev->dev, "%s disabled\n", __func__);
}

static const struct coresight_ops_source soc_source_ops = {
	.enable = soc_source_enable,
	.disable = soc_source_disable,
};

static const struct coresight_ops soc_cs_ops = {
	.source_ops = &soc_source_ops,
};

static int trace_platdev_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct coresight_platform_data *pdata = NULL;
	struct soc_source_drvdata *drvdata;
	struct coresight_desc desc = {0};
	struct device *dev = &pdev->dev;
	const char *name;

	dev_info(dev, "trace: platform device probe %s\n", pdev->name);

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	dev_set_drvdata(dev, drvdata);

	ret = of_property_read_string(dev->of_node, "trace-name", &name);
	if (ret) {
		dev_warn(dev, "Get trace name property failed: %d\n", ret);
		desc.name = coresight_alloc_device_name(&soc_source_devs, dev);
		if (!desc.name) {
			dev_err(dev, "no memory for name\n");
			return -ENOMEM;
		}
	} else {
		dev_info(dev, "Trace name property = %s\n", name);
		desc.name = name;
	}

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata)) {
		dev_err(dev, "coresight get platform data error %ld\n", PTR_ERR(pdata));
		return PTR_ERR(pdata);
	}
	dev->platform_data = pdata;

	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_OTHERS;
	desc.ops = &soc_cs_ops;
	desc.pdata = pdata;
	desc.dev = dev;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev)) {
		dev_err(dev, "soc source register failed: %ld\n", PTR_ERR(drvdata->csdev));
		return PTR_ERR(drvdata->csdev);
	}

	dev_info(dev, "trace: platdev probe finish\n");

	return ret;
}

static void trace_platdev_remove(struct platform_device *pdev)
{
	struct soc_source_drvdata *drvdata = dev_get_drvdata(&pdev->dev);

	dev_info(&pdev->dev, "Remove soc_source device");
	coresight_unregister(drvdata->csdev);
}

static const struct of_device_id trace_of_ids[] = {
	{ .compatible = "google,soc-trace-source", },
	{},
};

MODULE_DEVICE_TABLE(of, trace_of_ids);

static struct platform_driver trace_platdev_driver = {
	.probe = trace_platdev_probe,
	.remove = trace_platdev_remove,
	.driver = {
		.name = "google,soc-trace-source",
		.of_match_table = trace_of_ids,
	}
};

module_platform_driver(trace_platdev_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google soc trace driver");
MODULE_LICENSE("GPL");
