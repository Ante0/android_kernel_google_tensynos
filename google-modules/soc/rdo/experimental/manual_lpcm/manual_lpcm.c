// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#define LPCM_OPS_PROP "manual-lpcm-ops"
#define LPCM_VALS_PROP "manual-lpcm-vals"

#define LPCM_POLL_MAX 20

struct manual_lpcm {
	struct device *dev;
	void __iomem *base;
};

static int process_op(struct manual_lpcm *lpcm, const char *op, u32 offset,
		      u32 val, u32 mask)
{
	struct device *dev = lpcm->dev;
	u32 tmp = 0;
	int retry = 0;

	if (strcmp("write", op) == 0) {
		tmp = readl(lpcm->base + offset);
		tmp = (tmp & ~mask) | (val & mask);
		dev_info(dev, "Write offset=0x%x val=0x%x\n", offset, tmp);
		writel(tmp, lpcm->base + offset);
	} else if (strcmp("poll", op) == 0) {
		dev_info(dev, "Poll offset=0x%x val=0x%x mask=0x%x\n", offset,
			 val, mask);
		val &= mask;
		retry = 0;
		while (retry < LPCM_POLL_MAX) {
			tmp = readl(lpcm->base + offset);
			dev_info(dev, "Polled value %x\n", tmp);
			if ((tmp & mask) == val)
				break;
			retry++;
			msleep(100);
		}
		if (retry >= LPCM_POLL_MAX) {
			dev_err(dev,
				"Poll timeout, offset=0x%x val=0x%x mask=0x%x\n",
				offset, val, mask);
			return -EIO; // TODO: better error code
		}
		dev_info(dev, "Poll done\n");
	} else {
		dev_err(dev, "Invalid op %s\n", op);
		return -EINVAL;
	}
	return 0;
}

static int process_ops(struct manual_lpcm *lpcm)
{
	struct device *dev = lpcm->dev;
	struct device_node *np = lpcm->dev->of_node;
	int idx = 0, ret = 0;
	struct property *prop = NULL;
	const char *op = NULL;
	u32 offset = 0, val = 0, mask = 0;

	of_property_for_each_string(np, LPCM_OPS_PROP, prop, op) {
		if (of_property_read_u32_index(np, LPCM_VALS_PROP, idx,
					       &offset)) {
			dev_err(dev, "Unable to get %d-th offset\n", idx);
			return ret;
		}
		if (of_property_read_u32_index(np, LPCM_VALS_PROP, idx + 1,
					       &val)) {
			dev_err(dev, "Unable to get %d-th val\n", idx);
			return ret;
		}
		if (of_property_read_u32_index(np, LPCM_VALS_PROP, idx + 2,
					       &mask)) {
			dev_err(dev, "Unable to get %d-th mask\n", idx);
			return ret;
		}

		idx += 3;
		ret = process_op(lpcm, op, offset, val, mask);
		if (ret)
			return ret;
	}
	return 0;
}

static int manual_lpcm_probe(struct platform_device *pdev)
{
	int ret;
	struct manual_lpcm *lpcm = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	lpcm = devm_kzalloc(dev, sizeof(*lpcm), GFP_KERNEL);
	if (!lpcm)
		return -ENOMEM;

	lpcm->dev = dev;
	lpcm->base = of_iomap(np, 0);
	if (IS_ERR(lpcm->base))
		return PTR_ERR(lpcm->base);

	ret = process_ops(lpcm);

	// Free-up resources.
	iounmap(lpcm->base);
	return ret;

}

static void manual_lpcm_remove(struct platform_device *pdev)
{
}

static const struct of_device_id manual_lpcm_of_match_table[] = {
	{ .compatible = "google,manual-lpcm" },
	{},
};
MODULE_DEVICE_TABLE(of, manual_lpcm_of_match_table);

static struct platform_driver manual_lpcm_driver = {
	.probe = manual_lpcm_probe,
	.remove = manual_lpcm_remove,
	.driver = {
		.name = "google-manual-lpcm",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(manual_lpcm_of_match_table),
	},
};

module_platform_driver(manual_lpcm_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google LPCM driver with manual sequence");
MODULE_LICENSE("GPL");
