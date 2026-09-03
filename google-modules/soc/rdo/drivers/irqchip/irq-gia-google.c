// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024-2025 Google LLC
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/ioport.h>

#include "irq-gia-lib.h"

#define MAX_OUT_IRQS 32

static const struct gia_type_data wide_level_gia_data = {
	/* Offsets */
	.status_offset = 0,
	.status_overflow_offset = -1,
	.enable_offset = 4,
	.mask_offset = 8,
	.trigger_offset = 12,
	.hybrid_offset = -1,

	/* Features */
	.clear_status_reg = false,
	.check_overflow_reg = false,
	.wide_gia = true,
};

static const struct gia_type_data level_gia_data = {
	/* Offsets */
	.status_offset = 0,
	.status_overflow_offset = -1,
	.enable_offset = 4,
	.mask_offset = 8,
	.trigger_offset = 12,
	.hybrid_offset = -1,

	/* Features */
	.clear_status_reg = false,
	.check_overflow_reg = false,
	.wide_gia = false,
};

static const struct gia_type_data level_sgia_data = {
	/* Offsets */
	.status_offset = 0,
	.status_overflow_offset = -1,
	.enable_offset = 8,
	.mask_offset = 12,
	.trigger_offset = 16,
	.hybrid_offset = 20,

	/* Features */
	.clear_status_reg = false,
	.check_overflow_reg = false,
	.wide_gia = false,
};


static const struct gia_type_data pulse_gia_data = {
	/* Offsets */
	.status_offset = 0,
	.status_overflow_offset = 4,
	.enable_offset = 8,
	.mask_offset = 12,
	.trigger_offset = 16,
	.hybrid_offset = -1,

	/* Features */
	.clear_status_reg = true,
	.check_overflow_reg = true,
	.wide_gia = false,
	.edge_detection = true,
};

static const struct of_device_id irq_gia_google_of_match[] = {
	{ .compatible = "google,wide-level-gia", .data = &wide_level_gia_data },
	{ .compatible = "google,level-gia", .data = &level_gia_data },
	{ .compatible = "google,level-sgia", .data = &level_sgia_data },
	{ .compatible = "google,pulse-gia", .data = &pulse_gia_data },
	{ .compatible = "google,pulse-sgia", .data = &pulse_gia_data },
	{}
};

static inline int irq_gia_google_check_wide(struct gia_device_data *gdd)
{
	int ret;

	if (gdd->type_data->wide_gia) {
		/*
		 * "nr-irq-chips" tells about how many sets of registers needed to describe this GIA
		 * instance. For example, a GIA aggregating 50 interrupts needs, 2 sets of
		 * registers. And each set can handle 32 interrupts (max).
		 */
		ret = of_property_read_u32(gdd->dev->of_node, "nr-irq-chips", &gdd->nr_irq_chips);
		if (ret < 0) {
			dev_err(gdd->dev, "failed to get nr-irq-chips with error %d\n", ret);
			return ret;
		}

		/*
		 * "next-bank-offset" tells if the GIA is wide, then what's the offset difference
		 * between first registers of any 2 consecutive chips.
		 */
		ret = of_property_read_u32(gdd->dev->of_node, "next-bank-offset",
					   &gdd->next_bank_base_offset);
		if (ret < 0) {
			dev_err(gdd->dev, "failed to get next-bank-offset with error %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int irq_gia_google_parse_out_interrupts(struct gia_device_data *gdd)
{
	struct irq_data *d;

	gdd->nr_out_irq = platform_irq_count(gdd->pdev);
	if (gdd->nr_out_irq <= 0) {
		dev_err(gdd->dev, "IRQ parsing failed with error %d\n", gdd->nr_out_irq);
		return gdd->nr_out_irq;
	} else if (gdd->nr_out_irq > MAX_OUT_IRQS) {
		dev_err(gdd->dev, "%d interrupts defined, max allowed is IRQS_PER_REG\n",
			gdd->nr_out_irq);
		return -EINVAL;
	}

	gdd->virqs = devm_kzalloc(gdd->dev, sizeof(unsigned int) * gdd->nr_out_irq, GFP_KERNEL);
	if (IS_ERR_OR_NULL(gdd->virqs))
		return PTR_ERR(gdd->virqs);

	for (int i = 0; i < gdd->nr_out_irq; i++) {
		gdd->virqs[i] = irq_of_parse_and_map(gdd->dev->of_node, i);
		if (gdd->virqs[i] == 0) {
			/*
			 * Log the error, every time. Idea is to catch excessive probe deferrals (if
			 * any). Other more important reason is, there could be some other valid
			 * reason too. In that case, we might see this over and over again in logs,
			 * simply because we are deferring the probe of this dev, not cancelling it.
			 */
			dev_err(gdd->dev, "%d-th irq mapping failed with virq = 0\n", i);
			return -EPROBE_DEFER;
		}
		d = irq_get_irq_data(gdd->virqs[i]);
		/*
		 * virqs[i] 's irq_data and irq_domain would be the upstream one. Link the source
		 * GIA with the current irq. This will actually help to traverse downstream, to be
		 * useful in affinity updates in downstream direction.
		 */
		d->chip_data = gdd;
		dev_dbg(gdd->dev, "GIA - hwirq %d - virq %d\n", i, gdd->virqs[i]);
	}

	return 0;
}

static int irq_gia_google_probe(struct platform_device *pdev)
{
	struct gia_device_data *gdd;
	const struct of_device_id *match;
	int ret;

	gdd = devm_kzalloc(&pdev->dev, sizeof(*gdd), GFP_KERNEL);
	if (!gdd)
		return -ENOMEM;

	platform_set_drvdata(pdev, gdd);

	gdd->pdev = pdev;
	gdd->dev = &pdev->dev;
	match = of_match_device(irq_gia_google_of_match, gdd->dev);
	gdd->type_data = (struct gia_type_data *)match->data;

	gdd->io_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gdd->io_base))
		return PTR_ERR(gdd->io_base);

	ret = irq_gia_google_parse_out_interrupts(gdd);
	if (ret)
		return ret;

	/* Most GIAs aggregate <= 32 interrupts. That's default unless wide GIA is defined */
	gdd->nr_irq_chips = 1;

	ret = irq_gia_google_check_wide(gdd);
	if (ret < 0)
		return ret;

	return gia_init(gdd);
}

static void irq_gia_google_remove(struct platform_device *pdev)
{
	struct gia_device_data *gdd = dev_get_drvdata(&pdev->dev);

	gia_exit(gdd);
	for (int i = 0; i < gdd->nr_out_irq; i++)
		irq_dispose_mapping(gdd->virqs[i]);
}

static struct platform_driver irq_gia_google_driver = {
	.driver = {
		.name = "irq-gia-google",
		.of_match_table = irq_gia_google_of_match,
	},
	.probe = irq_gia_google_probe,
	.remove = irq_gia_google_remove,
};

static int __init irq_gia_google_init(void)
{
	return platform_driver_register(&irq_gia_google_driver);
}
module_init(irq_gia_google_init);

static void __exit irq_gia_google_exit(void)
{
	platform_driver_unregister(&irq_gia_google_driver);
}
module_exit(irq_gia_google_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GIA IRQ Driver");
MODULE_LICENSE("GPL");
