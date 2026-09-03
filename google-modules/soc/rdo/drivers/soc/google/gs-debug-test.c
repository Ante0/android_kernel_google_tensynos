// SPDX-License-Identifier: GPL-2.0
/*
 * gs-debug-test.c - GS Debug Feature Test Driver
 *
 * Copyright 2024 Google LLC
 */

#define pr_fmt(fmt) "GS DEBUG TEST: %s() " fmt, __func__

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <soc/google/debug-test.h>
#include <soc/google/google-cdd.h>
#include <soc/google/google-smc.h>

/* timeout for dog bark/bite */
#define DELAY_TIME 30000

struct debug_test_desc {
	int enabled;
	int nr_cpu;
	int nr_little_cpu;
	int nr_mid_cpu;
	int nr_big_cpu;
	int little_cpu_start;
	int mid_cpu_start;
	int big_cpu_start;
	void __iomem *cpm_sw_crst_ctrl;
};

static struct debug_test_desc gs_debug_desc;

static void simulate_HARD_LOCKUP_handler(void *info)
{
	pr_info("Lockup CPU%d\n", raw_smp_processor_id());
	asm("b .");
}

static void simulate_HARD_LOCKUP(char *arg)
{
	int cpu;
	int start = -1;
	int end;
	int curr_cpu;

	pr_crit("called!\n");

	if (!arg) {
		start = 0;
		end = gs_debug_desc.nr_cpu;

		local_irq_disable();
		curr_cpu = raw_smp_processor_id();

		for (cpu = start; cpu < end; cpu++) {
			if (cpu == curr_cpu)
				continue;
			smp_call_function_single(cpu, simulate_HARD_LOCKUP_handler, 0, 0);
		}
		asm("b .");
		return;
	}

	if (!strcmp(arg, "LITTLE")) {
		if (gs_debug_desc.little_cpu_start < 0 || gs_debug_desc.nr_little_cpu < 0) {
			pr_err("no little cpu info\n");
			return;
		}
		start = gs_debug_desc.little_cpu_start;
		end = start + gs_debug_desc.nr_little_cpu - 1;
	} else if (!strcmp(arg, "MID")) {
		if (gs_debug_desc.mid_cpu_start < 0 || gs_debug_desc.nr_mid_cpu < 0) {
			pr_err("no mid cpu info\n");
			return;
		}
		start = gs_debug_desc.mid_cpu_start;
		end = start + gs_debug_desc.nr_mid_cpu - 1;
	} else if (!strcmp(arg, "BIG")) {
		if (gs_debug_desc.big_cpu_start < 0 || gs_debug_desc.nr_big_cpu < 0) {
			pr_err("no big cpu info\n");
			return;
		}
		start = gs_debug_desc.big_cpu_start;
		end = start + gs_debug_desc.nr_big_cpu - 1;
	}

	if (start >= 0) {
		preempt_disable();
		curr_cpu = raw_smp_processor_id();
		for (cpu = start; cpu <= end; cpu++) {
			if (cpu == curr_cpu)
				continue;
			smp_call_function_single(cpu, simulate_HARD_LOCKUP_handler, 0, 0);
		}
		if (curr_cpu >= start && curr_cpu <= end) {
			local_irq_disable();
			asm("b .");
		}
		preempt_enable();
		return;
	}

	if (kstrtoint(arg, 10, &cpu)) {
		pr_err("input is invalid\n");
		return;
	}

	if (cpu < 0 || cpu >= gs_debug_desc.nr_cpu) {
		pr_err("input is invalid\n");
		return;
	}

	smp_call_function_single(cpu, simulate_HARD_LOCKUP_handler, 0, 0);
}

static void simulate_WRITE_PROTECT(char *arg)
{
	pr_crit("called!\n");

	if (!gs_debug_desc.cpm_sw_crst_ctrl) {
		pr_err("failed to remap cpm_sw_crst_ctrl, quit\n");
		return;
	}

	__raw_writel(1, gs_debug_desc.cpm_sw_crst_ctrl);
	while (true) {
		wfi();
	}
}

static void simulate_EMERG_RESET(char *arg)
{
	pr_crit("called!\n");

	google_cdd_emergency_reboot(NULL);
	mdelay(DELAY_TIME);
	/* should not reach here */
}

static char *buffer[NR_CPUS];
static void simulate_CPU_CONTEXT_handler(void *info)
{
	u64 cpu = raw_smp_processor_id();
	u64 i = 0;
	u64 addr = virt_to_phys((void *)(buffer[cpu]));
	local_irq_disable();

	memset(buffer[cpu], 0x5A, PAGE_SIZE * 2);
	google_cdd_set_debug_test_buffer_addr(addr, cpu);

	i = (0xcafeface0000 | cpu) << 16;
	/* populate registers with known values and go to infinite loop afterwards */
	asm volatile("mov x0, %0\n\t"
			"mov x1, #1\n\t"
			"mov x2, #0x200000000\n\t"
			"mov x3, #0x300000003\n\t"
			"add x4, x0, #4\n\t"
			"add x5, x0, #5\n\t"
			"add x6, x0, #6\n\t"
			"add x7, x0, #7\n\t"
			"add x8, x0, #8\n\t"
			"add x9, x0, #9\n\t"
			"add x10, x0, #10\n\t"
			"add x11, x0, #11\n\t"
			"add x12, x0, #12\n\t"
			"add x13, x0, #13\n\t"
			"add x14, x0, #14\n\t"
			"add x15, x0, #15\n\t"
			"add x16, x0, #16\n\t"
			"add x17, x0, #17\n\t"
			"add x18, x0, #18\n\t"
			"add x19, x0, #19\n\t"
			"add x20, x0, #20\n\t"
			"add x21, x0, #21\n\t"
			"add x22, x0, #22\n\t"
			"add x23, x0, #23\n\t"
			"add x24, x0, #24\n\t"
			"add x25, x0, #25\n\t"
			"add x26, x0, #26\n\t"
			"add x27, x0, #27\n\t"
			"add x28, x0, #28\n\t"
			"add x29, x0, #29\n\t"
			"add x30, x0, #30\n\t"
			"mov x0, #0\n\t"
			"b .\n\t"
			: : "r" (i));

	/* should not reach here */
}

static void simulate_CPU_CONTEXT_TEST(char *arg)
{
	int cpu;

	pr_crit("called!\n");

	for_each_possible_cpu(cpu) {
		google_cdd_set_debug_test_buffer_addr(0, cpu);
		buffer[cpu] = kmalloc(PAGE_SIZE * 2, GFP_KERNEL);
		memset(buffer[cpu], 0x3B, PAGE_SIZE * 2);
	}

	smp_call_function(simulate_CPU_CONTEXT_handler, NULL, 0);
	for (cpu = 0; cpu < gs_debug_desc.nr_cpu; cpu++) {
		if (cpu == raw_smp_processor_id())
			continue;

		while (!google_cdd_get_debug_test_buffer_addr(cpu))
			;
		pr_crit("CPU %d STOPPING\n", cpu);
	}

	google_cdd_emergency_reboot_timeout(arg, 0, 1);

	simulate_CPU_CONTEXT_handler(NULL);
}

static void simulate_CPU_CONTEXT(char *arg)
{
	simulate_CPU_CONTEXT_TEST("cpu context test");
}

static void simulate_EL3_ASSERT(char *arg)
{
	pr_crit("called!\n");

	google_smc(SIP_SVD_GS_DEBUG_CMD, CMD_ASSERT, 0, 0);
}

static void simulate_EL3_PANIC(char *arg)
{
	pr_crit("called!\n");

	google_smc(SIP_SVD_GS_DEBUG_CMD, CMD_PANIC, 0, 0);
}

static void simulate_ECC_INJECTION(void *info)
{
	unsigned long lev = *((unsigned long *)info);
	u64 count = 0x1000;
	u64 ctrl = 0x80000002;
	struct arm_smccc_res res;

	pr_info("CPU%d: Level%ld: ECC error injection!\n", raw_smp_processor_id(), lev);

	arm_smccc_smc(SIP_SVD_GS_DEBUG_CMD, CMD_ECC, lev, count, ctrl, 0, 0, 0, &res);
}

static void simulate_ECC(char *arg)
{
	int cpu, temp;
	unsigned long lev;

	pr_crit("called!\n");

	if (kstrtoint(arg, 16, &temp)) {
		pr_err("check input parameter\n");
		return;
	}
	cpu = (temp >> 4) & 0xf;
	lev = temp & 0xf;

	if (cpu == raw_smp_processor_id())
		simulate_ECC_INJECTION((void *)&lev);
	else
		smp_call_function_single(cpu, simulate_ECC_INJECTION, &lev, 1);
}

static int gs_debug_test_parsing_dt(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	void __iomem *cpm_grm_virt_mapping;
	u32 offset;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cpm_grm_base");
	if (!res) {
		dev_err(dev, "failed to get memory resources for cpm_grm_base\n");
		return -EINVAL;
	}

	cpm_grm_virt_mapping = devm_ioremap_resource(dev, res);
	ret = IS_ERR(cpm_grm_virt_mapping);
	if (ret) {
		dev_err(dev, "failed to cpm_grm_virtual mapping %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(np, "cpm_sw_crst_ctrl_offset", &offset);
	if (ret) {
		dev_err(dev, "no data(cpm_sw_crst_ctrl_offset)\n");
		return ret;
	}

	if (offset + sizeof(u32) > resource_size(res)) {
		dev_err(dev, "unexpected cpm_sw_crst_ctrl_offset %x or resource %pr properties\n",
			offset, res);
		return -EINVAL;
	}

	gs_debug_desc.cpm_sw_crst_ctrl = cpm_grm_virt_mapping + offset;

	ret = of_property_read_u32(np, "nr_cpu", &gs_debug_desc.nr_cpu);
	if (ret) {
		dev_err(dev, "no data(nr_cpu)\n");
		return ret;
	}

	ret = of_property_read_u32(np, "little_cpu_start", &gs_debug_desc.little_cpu_start);
	if (ret) {
		dev_err(dev, "no data(little_cpu_start)\n");
		gs_debug_desc.little_cpu_start = -1;
	}

	ret = of_property_read_u32(np, "nr_little_cpu", &gs_debug_desc.nr_little_cpu);
	if (ret) {
		dev_err(dev, "no data(nr_little_cpu)\n");
		gs_debug_desc.nr_little_cpu = -1;
	}

	ret = of_property_read_u32(np, "mid_cpu_start", &gs_debug_desc.mid_cpu_start);
	if (ret) {
		dev_err(dev, "no data(mid_cpu_start)\n");
		gs_debug_desc.mid_cpu_start = -1;
	}

	ret = of_property_read_u32(np, "nr_mid_cpu", &gs_debug_desc.nr_mid_cpu);
	if (ret) {
		dev_err(dev, "no data(nr_mid_cpu)\n");
		gs_debug_desc.nr_mid_cpu = -1;
	}

	ret = of_property_read_u32(np, "big_cpu_start", &gs_debug_desc.big_cpu_start);
	if (ret) {
		dev_err(dev, "no data(big_cpu_start)\n");
		gs_debug_desc.big_cpu_start = -1;
	}

	ret = of_property_read_u32(np, "nr_big_cpu", &gs_debug_desc.nr_big_cpu);
	if (ret) {
		dev_err(dev, "no data(nr_big_cpu)\n");
		gs_debug_desc.nr_big_cpu = -1;
	}

	return ret;
}

static struct debug_trigger gs_debug_test_trigger = {
	.hard_lockup = simulate_HARD_LOCKUP,
	.cold_reset = simulate_WRITE_PROTECT,
	.watchdog_emergency_reset = simulate_EMERG_RESET,
	.cpucontext = simulate_CPU_CONTEXT,
	.el3_assert = simulate_EL3_ASSERT,
	.el3_panic = simulate_EL3_PANIC,
	.ecc = simulate_ECC,
};

static int gs_debug_test_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	/* Parse data from DT and register to the general test trigger */
	ret = gs_debug_test_parsing_dt(pdev);
	if (ret) {
		dev_err(dev, "cannot parse test data from dt, ret=[0x%x]\n", ret);
		return ret;
	}

	debug_trigger_register(&gs_debug_test_trigger, "gs");

	gs_debug_desc.enabled = 1;

	dev_info(dev, "%s is successfully\n", __func__);

	return ret;
}

static const struct of_device_id gs_debug_test_matches[] = {
	{.compatible = "google,gs-debug-test"},
	{},
};
MODULE_DEVICE_TABLE(of, gs_debug_test_matches);

static struct platform_driver gs_debug_test_driver = {
	.probe		= gs_debug_test_probe,
	.driver		= {
		.name	= "gs-debug-test",
		.of_match_table	= of_match_ptr(gs_debug_test_matches),
	},
};
module_platform_driver(gs_debug_test_driver);

MODULE_AUTHOR("Jone Chou <jonechou@google.com>");
MODULE_DESCRIPTION("GS Debug Feature Test Driver");
MODULE_LICENSE("GPL v2");
