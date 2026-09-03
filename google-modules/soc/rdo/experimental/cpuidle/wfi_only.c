// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpuidle.h>
#include <linux/module.h>

#ifdef CONFIG_ARM64
static int state_wfi_enter(struct cpuidle_device *dev,
			   struct cpuidle_driver *drv, int idx)
{
	asm volatile("dsb sy" : : : "memory");
	asm volatile("wfi" : : : "memory");
	return 0;
}

static struct cpuidle_driver wfi_only_driver = {
	.name = "wfi_only",
	.owner = THIS_MODULE,
	.states[0] = {
		.enter = state_wfi_enter,
		.flags = CPUIDLE_FLAG_TIMER_STOP,
		.exit_latency = 1,
		.target_residency = 1,
		.power_usage = UINT_MAX,
		.name = "WFI",
		.desc = "ARM WFI",
	},
	.state_count = 1,
};

static int wfi_only_init(void)
{
	return cpuidle_register(&wfi_only_driver, NULL);
}
module_init(wfi_only_init);

static void wfi_only_exit(void)
{
	cpuidle_unregister(&wfi_only_driver);
}
module_exit(wfi_only_exit);
#endif

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("CPU idle driver for testing");
MODULE_LICENSE("GPL");
