// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "pixelmd: " fmt

#include "pixelmd_device.h"
#include "pixelmd_cmd_kswapd.h"
#include "pixelmd_cmd_lmkd_kill.h"

#include <linux/module.h>

static struct pixelmd_device pixelmd_device;

static int __init pixelmd_init(void)
{
	int ret;

	ret = pixelmd_kswapd_register_hooks();
	if (ret) {
		pr_err("failed to register hooks: %d\n", ret);
		return ret;
	}

	ret = pixelmd_lmkd_kill_register_hooks();
	if (ret) {
		pr_err("failed to register lmkd_kill hooks: %d\n", ret);
		goto unregister_kswapd;
	}

	ret = pixelmd_device_init(&pixelmd_device);
	if (ret < 0) {
		pr_err("failed to initialize device: %d\n", ret);
		goto unregister_lmkd_kill;
	}

	pr_info("loaded\n");
	return 0;

unregister_lmkd_kill:
	pixelmd_lmkd_kill_unregister_hooks();
unregister_kswapd:
	pixelmd_kswapd_unregister_hooks();
	return ret;
}

static void __exit pixelmd_exit(void)
{
	pixelmd_kswapd_unregister_hooks();
	pixelmd_lmkd_kill_unregister_hooks();
	pixelmd_device_destroy(&pixelmd_device);

	pr_info("unloaded\n");
}

module_init(pixelmd_init);
module_exit(pixelmd_exit);

MODULE_LICENSE("GPL");
