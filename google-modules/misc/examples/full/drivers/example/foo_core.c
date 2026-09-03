// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <example/foo.h>
#include <uapi/example/foo_info.h>
#include <dt-bindings/example/google,foo.h>
#include "foo_internal.h"

void foo_api_func(void)
{
	pr_info("foo: api func called\n");
}
EXPORT_SYMBOL_GPL(foo_api_func);

static int foo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "foo: probe\n");
	dev_info(dev, "foo: exported val = %d\n", FOO_EXPORTED_VAL);
	dev_info(dev, "foo: uapi val = %d\n", FOO_UAPI_VAL);
	dev_info(dev, "foo: dt val = %d\n", FOO_DT_VAL);
	foo_internal_func();

	return 0;
}

static void foo_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "foo: remove\n");
}

static const struct of_device_id foo_of_match[] = {
	{ .compatible = "google,foo", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, foo_of_match);

static struct platform_driver foo_driver = {
	.probe = foo_probe,
	.remove = foo_remove,
	.driver = {
		.name = "google,foo",
		.of_match_table = foo_of_match,
	},
};

module_platform_driver(foo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Example Author");
MODULE_DESCRIPTION("Example foo module");
