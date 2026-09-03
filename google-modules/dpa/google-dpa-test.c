// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <soc/google/google_dpa.h>

struct google_dpa_test_data {
	struct google_dpa *dpa;
};

static int google_dpa_test_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_dpa_test_data *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->dpa = google_dpa_get(dev);
	if (IS_ERR(data->dpa))
		return dev_err_probe(dev, PTR_ERR(data->dpa), "Failed to get DPA.");

	return 0;
}

static void google_dpa_test_remove(struct platform_device *pdev)
{
	struct google_dpa_test_data *data = platform_get_drvdata(pdev);

	google_dpa_put(data->dpa);
}

static const struct of_device_id google_dpa_test_of_match[] = {
	{ .compatible = "google,dpa-test" },
	{ },
};
MODULE_DEVICE_TABLE(of, google_dpa_test_of_match);

static struct platform_driver google_dpa_test_driver = {
	.probe = google_dpa_test_probe,
	.remove = google_dpa_test_remove,
	.driver = {
		.name = "google_dpa_test",
		.owner = THIS_MODULE,
		.of_match_table = google_dpa_test_of_match,
	},
};
module_platform_driver(google_dpa_test_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google DPA test driver");
