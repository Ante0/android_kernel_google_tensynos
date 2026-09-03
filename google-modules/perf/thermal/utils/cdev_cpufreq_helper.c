// SPDX-License-Identifier: GPL-2.0-only
/*
 * cdev_cpufreq_helper.c Helper functions for cpufreq cooling devices.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <kunit/static_stub.h>

#include "cdev_cpufreq_helper.h"

static struct device *__cdev_fetch_cpu_dev(unsigned int cpu)
{
	struct device *cpu_dev;

	if (!cpu_possible(cpu)) {
		pr_err("Invalid CPU:%d\n", cpu);
		return ERR_PTR(-EINVAL);
	}

	cpu_dev = cdev_get_cpu_device(cpu);
	if (!cpu_dev) {
		pr_err("No CPU dev for cpu:%d\n", cpu);
		return ERR_PTR(-ENODEV);
	}
	return cpu_dev;
}

int cdev_cpufreq_get_opp_count(unsigned int cpu)
{
	struct device *cpu_dev;

	cpu_dev = __cdev_fetch_cpu_dev(cpu);
	if (IS_ERR(cpu_dev))
		return PTR_ERR(cpu_dev);

	return cdev_dev_pm_opp_get_opp_count(cpu_dev);
}

int cdev_cpufreq_update_opp_table(unsigned int cpu,
				  enum hw_dev_type cdev_id,
				  struct cdev_opp_table *cdev_table,
				  unsigned int num_opp)
{
	int opp_ct;
	struct device *cpu_dev;
	int ret = 0, i, j, power;
	unsigned long freq = 0;
	struct dev_pm_opp *opp;

	if (!cdev_table || !num_opp || cdev_id >= HW_CDEV_MAX)
		return -EINVAL;

	cpu_dev = __cdev_fetch_cpu_dev(cpu);
	if (IS_ERR(cpu_dev))
		return PTR_ERR(cpu_dev);

	opp_ct = cdev_dev_pm_opp_get_opp_count(cpu_dev);
	if (opp_ct <= 0) {
		ret = (opp_ct == 0) ? -ENODEV : opp_ct;
		pr_err("No OPP table found for cpu:%d. err:%d\n",
		       cpu, ret);
		return ret;
	}
	if (opp_ct != num_opp) {
		pr_err("Invalid opp input:%d. Actual opp count:%d\n", num_opp, opp_ct);
		return -EINVAL;
	}

	ret = cdev_msg_tmu_get_power_table(cdev_id, 0, &power, &opp_ct);
	if (ret) {
		pr_err("Fetching power table error:%d cdev:%d.\n", ret, cdev_id);
		return ret;;
	}
	// Firmware gives the max cdev state as output. Increment to get the max OPP levels.
	opp_ct++;
	if (opp_ct < num_opp) {
		pr_err("Invalid OPP count %d in firmware for CPU:%d.\n", opp_ct, cpu);
		return -EINVAL;
	}

	for (i = 0, freq = 0, j = opp_ct - 1;
	     i < num_opp && j >= 0; i++, freq++, j--) {
		opp = cdev_dev_pm_opp_find_freq_ceil(cpu_dev, &freq);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			pr_err("Error fetching OPP freq:%lu for cpu:%d. err:%d\n", freq, cpu, ret);
			return ret;
		}
		cdev_table[i].freq = freq / HZ_PER_KHZ;
		cdev_table[i].voltage = cdev_dev_pm_opp_get_voltage(opp);
		cdev_dev_pm_opp_put(opp);

		ret = cdev_msg_tmu_get_power_table(cdev_id, j, &power, NULL);
		if (ret) {
			pr_err("Error fetching power for OPP idx:%d. ret = %d\n.",
			       j, ret);
			return ret;
		}
		cdev_table[i].power = power * MICROWATT_PER_MILLIWATT;
		pr_debug("idx:%d freq:%lu volt:%u power:%u\n", i, freq,
		       cdev_table[i].voltage,
		       cdev_table[i].power);
	}

	return 0;
}
