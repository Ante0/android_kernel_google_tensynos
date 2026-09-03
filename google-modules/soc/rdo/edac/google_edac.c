// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "edac_device.h"
#include "edac_mc.h"
#include "google_edac.h"

struct google_edac {
	struct device *dev;
	struct edac_device_ctl_info *edac_ctl;
};

static const char *const hayes_selr_ram[] = { "DSU", "L1", "L2" };
static const char *const hunter_selr_ram[] = { "DSU", "Core" };

DEF_READ_SYSREG(erridr, s3_0_c5_c3_0);
DEF_WRITE_SYSREG(erridr, s3_0_c5_c3_0);
DEF_READ_SYSREG(errselr, s3_0_c5_c3_1);
DEF_WRITE_SYSREG(errselr, s3_0_c5_c3_1);
DEF_READ_SYSREG(erxstatus, s3_0_c5_c4_2);
DEF_WRITE_SYSREG(erxstatus, s3_0_c5_c4_2);
DEF_READ_SYSREG(erxmisc0, s3_0_c5_c5_0);
DEF_WRITE_SYSREG(erxmisc0, s3_0_c5_c5_0);
DEF_READ_SYSREG(erxmisc1, s3_0_c5_c5_1);
DEF_WRITE_SYSREG(erxmisc1, s3_0_c5_c5_1);

static void dump_hayes_error(void)
{
	u64 erxmisc0 = read_erxmisc0_el1();
	u64 erxmisc1 = read_erxmisc1_el1();
	u64 array;

	/*
	 * erxmisc0[0] indicate Instruction or Data cache
	 * 0b0 -> Data or unified cache, 0b1 -> Instruction cache.
	 */
	edac_printk(KERN_CRIT, GOOGLE_EDAC, "[HAYES] %s\n",
		    FIELD_GET(HAYES_ERXMISC0_IND, erxmisc0) ? "Data or unified cache" :
						   "Instruction cache");

	/* erxmisc0[3:1] indicate Cache level, 0b000 -> L1 cache, 0b001 -> L2 cache */
	edac_printk(KERN_CRIT, GOOGLE_EDAC, "[HAYES] Cache Level: %llu\n",
		    FIELD_GET(HAYES_ERXMISC0_LVL, erxmisc0) + 1);

	/* erxmisc0[18:6] indicate the set that contains error */
	edac_printk(KERN_CRIT, GOOGLE_EDAC, "[HAYES] %llu set contains error\n",
		    FIELD_GET(HAYES_ERXMISC0_SETMASK, erxmisc0));

	/* erxmisc0[31:29] indicate the most significant bits of the way that contains error */
	edac_printk(KERN_CRIT, GOOGLE_EDAC, "[HAYES] %llu way contains error\n",
		    FIELD_GET(HAYES_ERXMISC0_WAYMASK, erxmisc0));

	/* erxmisc1[3:0] specific RAM array containing the error */
	array = FIELD_GET(HAYES_ERXMISC1_ARRAYMASK, erxmisc1);
	switch (array) {
	case HAYES_L2_CACHE_DATA_RAM:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HAYES] L2 cache Data Rams Array contains error\n");
		break;
	case HAYES_L2_CACHE_TAG_RAM:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HAYES] L2 cache Tag Rams Array contains error\n");
		break;
	case HAYES_L2_CACHE_L2DB_RAM:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HAYES] L2 cache L2DB Rams Array contains error\n");
		break;
	case HAYES_L2_CACHE_DUP_L1_DCACHE_TAG_RAM:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HAYES] L2 cache duplicate L1 D-cache tag Rams contains error\n");
		break;
	case HAYES_L2_TLB_RAM:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HAYES] L2 TLB Rams contains error\n");
		break;
	}

	/* erxmisc1[12:8] indicate the Ram bank within the array containing the error */
	edac_printk(KERN_CRIT, GOOGLE_EDAC,
		    "[HAYES] %llu bank contains error\n",
		    FIELD_GET(HAYES_ERXMISC1_BANKMASK, erxmisc1));

	/* erxmisc1[17:16] indicate the sub-bank of the Ram bank containing the error */
	edac_printk(KERN_CRIT, GOOGLE_EDAC,
		    "[HAYES] %llu sun-bank contains error\n",
		    FIELD_GET(HAYES_ERXMISC1_SUBBANKMASK, erxmisc1));

	/* erxmisc1[25:20] indicate the Ram row containing the error */
	edac_printk(KERN_CRIT, GOOGLE_EDAC, "[HAYES] %llu row contains error\n",
		    FIELD_GET(HAYES_ERXMISC1_ENTRYMASK, erxmisc1));
}

static void dump_hunter_error(void)
{
	u64 erxmisc0 = read_erxmisc0_el1();
	u64 unit;

	unit = FIELD_GET(HUNTER_ERXMISC0_UNITMASK, erxmisc0);
	/* erxmisc0[3:0] indicate the unit contains error */
	switch (unit) {
	case HUNTER_L1_INSTRUCTION_CACHE:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HUNTER] L1 Instruction Cache contains error\n");
		break;
	case HUNTER_L2_TLB:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HUNTER] L2 TLB contains error\n");
		break;
	case HUNTER_L1_DATA_CACHE:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HUNTER] L1 Data Cache contains error\n");
		break;
	case HUNTER_L2_CACHE:
		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "[HUNTER] L2 Cache contains error\n");
		break;
	}

	/*
	 * erxmisc1[5:4] indicate Ram array containing the error
	 * encoding is dependent on the unit
	 */
	edac_printk(KERN_CRIT, GOOGLE_EDAC,
		    "[Hunter] %llu Array contains error\n",
		    FIELD_GET(HUNTER_ERXMISC0_ARRAYMASK, erxmisc0));

	/*
	 * erxmisc1[18:6] indicate Ram index containing the error
	 * encoding is dependent on the unit
	 */
	edac_printk(KERN_CRIT, GOOGLE_EDAC,
		    "[Hunter] %llu Index contains error\n",
		    FIELD_GET(HUNTER_ERXMISC0_INDEXMASK, erxmisc0));

	/*
	 * erxmisc1[22:19] indicate Ram subarray containing the error
	 * encoding is dependent on the unit
	 */
	edac_printk(KERN_CRIT, GOOGLE_EDAC,
		    "[Hunter] %llu Subarray contains error\n",
		    FIELD_GET(HUNTER_ERXMISC0_SUBARRAYMASK, erxmisc0));

	/*
	 * erxmisc1[24:23] indicate Ram bank containing the error
	 * encoding is dependent on the unit
	 */
	edac_printk(KERN_CRIT, GOOGLE_EDAC,
		    "[Hunter] %llu Bank contains error\n",
		    FIELD_GET(HUNTER_ERXMISC0_BANKMASK, erxmisc0));

	/*
	 * erxmisc1[27:25] indicate Ram subbank containing the error
	 * encoding is dependent on the unit
	 */
	edac_printk(KERN_CRIT, GOOGLE_EDAC,
		    "[Hunter] %llu Subbank contains error\n",
		    FIELD_GET(HUNTER_ERXMISC0_SUBBANKMASK, erxmisc0));

	/* erxmisc1[31:28] indicate Ram way containing the error
	 * encoding is dependent on the unit
	 */
	edac_printk(KERN_CRIT, GOOGLE_EDAC,
		    "[Hunter] %llu Way contains error\n",
		    FIELD_GET(HUNTER_ERXMISC0_WAYMASK, erxmisc0));
}

/*
 * According to arm trm
 * Write ones to all the W1C fields that are nonzero in the read value
 * Write zero to all the W1C fields that are zero in the read value
 * Write zero to all the RW fields
 * 18 - 0 bit field is RW, other are W1C field
 */
static void clear_erxstatus(u64 erxstatus)
{
	erxstatus &= CLEAR_ERXSTATUSMASK;
	write_erxstatus_el1(erxstatus);
}

static void clear_erxmisc(void)
{
	write_erxmisc0_el1(0);
	write_erxmisc1_el1(0);
}

static irqreturn_t google_edac_isr(int irq, void *data)
{
	u64 erridr = read_erridr_el1();
	u64 errselr, erxstatus;
	int partnum = read_cpuid_part_number();
	u64 i;

	for (i = 0; i < erridr; i++) {
		errselr = read_errselr_el1();
		errselr |= i;
		write_errselr_el1(errselr);

		edac_printk(KERN_CRIT, GOOGLE_EDAC,
			    "Contain errors from [%s] RAMs\n",
			    partnum == HAYES ? hayes_selr_ram[i] :
					       hunter_selr_ram[i]);

		/* Need to write write_errselr_el1 finish to get the right error ram */
		isb();

		erxstatus = read_erxstatus_el1();
		if (!FIELD_GET(ERXSTATUS_VALID, erxstatus)) {
			edac_printk(KERN_CRIT, GOOGLE_EDAC,
				    "[VALID] ERXSTATUS not valid\n");
			goto invalid;
		}
		if (FIELD_GET(ERXSTATUS_UE, erxstatus)) {
			edac_printk(KERN_CRIT, GOOGLE_EDAC,
				    "[Uncorrected Error] At least one detected error was not corrected and not deferred.\n");
		}
		if (FIELD_GET(ERXSTATUS_OF, erxstatus)) {
			edac_printk(KERN_CRIT, GOOGLE_EDAC,
				    "[OverFlow] Indicates that multiple errors have been detected,\n");
		}
		if (FIELD_GET(ERXSTATUS_MV, erxstatus)) {
			edac_printk(KERN_CRIT, GOOGLE_EDAC,
				    "[MV] Miscellaneous Registers Valid.\n");
			if (partnum == HAYES) {
				dump_hayes_error();
			} else if (partnum == HUNTER) {
				dump_hunter_error();
			} else {
				edac_printk(KERN_CRIT, GOOGLE_EDAC,
					    "Does not support %d Cpu part number\n",
					    partnum);
				goto invalid;
			}
		}
		if (FIELD_GET(ERXSTATUS_CE, erxstatus)) {
			edac_printk(KERN_CRIT, GOOGLE_EDAC,
				    "[Corrected Error] At least one error was corrected.\n");
		}
		if (FIELD_GET(ERXSTATUS_DE, erxstatus)) {
			edac_printk(KERN_CRIT, GOOGLE_EDAC,
				    "[Deferred Error] At least one error was not corrected and deferred.\n");
		}
invalid:
		clear_erxstatus(erxstatus);
		clear_erxmisc();
	}

	return IRQ_HANDLED;
}

static int google_edac_probe_irq(struct platform_device *pdev,
				 const char *isr_handler_name,
				 irq_handler_t isr, int cpu)
{
	struct device *dev = &pdev->dev;
	struct cpumask cpu_mask;
	int irq;
	int ret;

	irq = platform_get_irq_byname(pdev, isr_handler_name);
	if (irq < 0) {
		dev_err(dev, "failed to get irq %s!\n", isr_handler_name);
		return irq;
	}

	ret = devm_request_threaded_irq(dev, irq, isr, NULL, IRQF_NOBALANCING,
					dev_name(dev), NULL);
	if (ret < 0) {
		dev_err(dev, "failed to request irq %d, isr_handler_name: %s\n",
			irq, isr_handler_name);
		return ret;
	}

	cpumask_set_cpu(cpu, &cpu_mask);
	irq_set_affinity(irq, &cpu_mask);

	return 0;
}

static int google_edac_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_edac *edac;
	char *irqname;
	int cpu;
	int ret;

	edac = devm_kzalloc(dev, sizeof(*edac), GFP_KERNEL);
	if (!edac)
		return -ENOMEM;
	edac->dev = dev;

	edac->edac_ctl = edac_device_alloc_ctl_info(0, "cpu",
						    num_possible_cpus(), "L", 3,
						    1, NULL, 0,
						    edac_device_alloc_index());
	if (!edac->edac_ctl) {
		dev_err(dev, "Edac init failed\n");
		return -ENOMEM;
	}

	edac->edac_ctl->dev = dev;
	edac->edac_ctl->mod_name = dev_name(dev);
	edac->edac_ctl->dev_name = dev_name(dev);
	edac->edac_ctl->ctl_name = "edac";

	platform_set_drvdata(pdev, edac);

	ret = edac_device_add_device(edac->edac_ctl);
	if (ret)
		goto free_ctl;

	for_each_online_cpu(cpu) {
		irqname = devm_kasprintf(dev, GFP_KERNEL, "FHI%d", cpu);
		if (!irqname) {
			ret = -ENOMEM;
			goto free_device;
		}

		ret = google_edac_probe_irq(pdev, irqname, google_edac_isr,
					    cpu);
		if (ret < 0)
			goto free_device;

		devm_kfree(dev, irqname);

		irqname = devm_kasprintf(dev, GFP_KERNEL, "ERI%d", cpu);
		if (!irqname) {
			ret = -ENOMEM;
			goto free_device;
		}

		ret = google_edac_probe_irq(pdev, irqname, google_edac_isr,
					    cpu);
		if (ret < 0)
			goto free_device;

		devm_kfree(dev, irqname);
	}

	return 0;

free_device:
	edac_device_del_device(dev);
	devm_kfree(dev, irqname);
free_ctl:
	edac_device_free_ctl_info(edac->edac_ctl);
	return ret;
}

static void google_edac_platform_remove(struct platform_device *pdev)
{
	struct google_edac *edac = platform_get_drvdata(pdev);

	edac_device_del_device(edac->dev);
	edac_device_free_ctl_info(edac->edac_ctl);
}

static const struct of_device_id google_edac_of_match_table[] = {
	{ .compatible = "google,edac" },
	{},
};
MODULE_DEVICE_TABLE(of, google_edac_of_match_table);

static struct platform_driver google_edac_platform_driver = {
	.probe = google_edac_platform_probe,
	.remove = google_edac_platform_remove,
	.driver = {
		.name = "google-edac",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_edac_of_match_table),
	},
};
module_platform_driver(google_edac_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google edac driver");
MODULE_LICENSE("GPL");
