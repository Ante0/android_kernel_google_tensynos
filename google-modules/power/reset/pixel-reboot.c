// SPDX-License-Identifier: GPL-2.0
/*
 *  pixel-reboot.c - Google Pixel SoC reset code
 *
 * Copyright 2023 Google LLC
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#if IS_ENABLED(CONFIG_GS_ACPM)
#include <soc/google/acpm_ipc_ctrl.h>
#endif
#include <soc/google/exynos-pmu-if.h>

#include <trace/hooks/reboot.h>

#include <gbms_storage.h>

#define BMS_RSBM_VALID			BIT(31)

#define PIXEL_REBOOT_VER_01_00		(0x0100)
#define PIXEL_REBOOT_VER_02_00		(0x0200)
#define PIXEL_REBOOT_VER_03_00		(0x0300)
#define PIXEL_REBOOT_VER_03_01		(0x0301)

struct gpio_desc *power_gpio;
// TODO: b/517433810 - Encapsulate global variables within a struct.
static u32 pixel_shutdown_offset, pixel_shutdown_mask, pixel_shutdown_value;

struct pixel_reboot_data {
	u16 version;
	u32 cold_reboot_offset;
	u32 cold_reboot_mask;
	u32 cold_reboot_value;
	u32 warm_reboot_offset;
	u32 warm_reboot_value;
	u32 reboot_cmd_offset;
	bool thermal_warm_reboot;
	bool bms_persist_reboot_mode;
};

struct pixel_reboot_context {
	const struct pixel_reboot_data *reboot_data;
	struct notifier_block reboot_handler;
};

enum pixel_reboot_mode {
	REBOOT_MODE_NORMAL		= 0x00,
	REBOOT_MODE_CHARGE		= 0x0A,
	REBOOT_MODE_SHUTDOWN		= 0x10,

	REBOOT_MODE_DMVERITY_CORRUPTED	= 0x50,
	REBOOT_MODE_SHUTDOWN_THERMAL	= 0x51,
	REBOOT_MODE_AB_UPDATE		= 0x52,

	REBOOT_MODE_THERMAL_HW_PROTECTION_SHUTDOWN		= 0x60,
	REBOOT_MODE_REG_HW_FAIL_HW_PROTECTION_SHUTDOWN1		= 0x61,
	REBOOT_MODE_REG_HW_FAIL_HW_PROTECTION_SHUTDOWN2		= 0x62,
	REBOOT_MODE_REG_FAIL_HW_PROTECTION_SHUTDOWN1		= 0x63,
	REBOOT_MODE_REG_FAIL_HW_PROTECTION_SHUTDOWN2		= 0x64,
	REBOOT_MODE_REG_UNDER_VOLTAGE_HW_PROTECTION_SHUTDOWN	= 0x65,
	REBOOT_MODE_REG_OVER_CURRENT_HW_PROTECTION_SHUTDOWN	= 0x66,
	REBOOT_MODE_REG_EVENT_FAIL_HW_PROTECTION_SHUTDOWN	= 0x67,
	REBOOT_MODE_UNKNOWN_HW_PROTECTION_SHUTDOWN	= 0x6F,

	REBOOT_MODE_RESCUE		= 0xF9,
	REBOOT_MODE_FASTBOOT		= 0xFA,
	REBOOT_MODE_BOOTLOADER		= 0xFC,
	REBOOT_MODE_FACTORY		= 0xFD,
	REBOOT_MODE_RECOVERY		= 0xFF,
	/* add a new reboot mode here */

	REBOOT_MODE_UNKNOWN		= U32_MAX,
};

static void pixel_power_off(void)
{
	u32 poweroff_try = 0;

	if (IS_ERR_OR_NULL(power_gpio)) {
		pr_err("Couldn't find power key node\n");
		return;
	}

	while (1) {
		/* wait for power button release */
		if (gpiod_get_raw_value(power_gpio)) {
#if IS_ENABLED(CONFIG_GS_ACPM)
			exynos_acpm_reboot();
#endif
			pr_emerg("Set PS_HOLD Low.\n");
			exynos_pmu_update(pixel_shutdown_offset,
					  pixel_shutdown_mask,
					  pixel_shutdown_value);
			++poweroff_try;
			pr_emerg("Should not reach here! (poweroff_try:%d)\n", poweroff_try);
		} else {
			/*
			 * if power button is not released,
			 * wait and check TA again
			 */
			pr_info("PWR Key is not released.\n");
		}
		// TODO: b/517437119 - Investigate and remove 1000ms infinite busy-wait loop.
		mdelay(1000);
	}
}

static int pixel_reboot_mode_set_gbms(enum pixel_reboot_mode mode)
{
	int ret = 0;

	u32 rsbm = mode | BMS_RSBM_VALID;
	ret = gbms_storage_write(GBMS_TAG_RSBM, &rsbm, sizeof(rsbm));
	if (ret < 0)
		pr_err("failed to write gbms storage: %d(%d)\n", GBMS_TAG_RSBM, ret);
	return ret;
}

static int pixel_reboot_mode_set(struct pixel_reboot_context *ctx,
				 enum pixel_reboot_mode mode)
{
	if (ctx->reboot_data && ctx->reboot_data->version < PIXEL_REBOOT_VER_03_00)
		exynos_pmu_write(ctx->reboot_data->reboot_cmd_offset, mode);

	return pixel_reboot_mode_set_gbms(mode);
}

static int pixel_reboot_mode_get_gbms(enum pixel_reboot_mode *mode)
{
	int ret = 0;
	u32 rsbm = 0;

	ret = gbms_storage_read(GBMS_TAG_RSBM, &rsbm, sizeof(rsbm));
	if (ret < 0) {
		pr_err("failed to read gbms storage: %d(%d)\n", GBMS_TAG_RSBM, ret);
		return ret;
	}

	if (rsbm & BMS_RSBM_VALID) {
		rsbm &= ~BMS_RSBM_VALID;
		*mode = (enum pixel_reboot_mode)rsbm;
	}

	return 0;
}

static int pixel_reboot_parse_restart(struct pixel_reboot_context *ctx, const char *cmd)
{
	enum pixel_reboot_mode mode = REBOOT_MODE_UNKNOWN;

	if (!cmd)
		return NOTIFY_DONE;

	pr_info("Reboot command: '%s'\n", cmd);

	if (!strcmp(cmd, "charge")) {
		mode = REBOOT_MODE_CHARGE;
	} else if (!strcmp(cmd, "bootloader")) {
		mode = REBOOT_MODE_BOOTLOADER;
	} else if (!strcmp(cmd, "fastboot")) {
		mode = REBOOT_MODE_FASTBOOT;
	} else if (!strcmp(cmd, "recovery")) {
		mode = REBOOT_MODE_RECOVERY;
	} else if (!strcmp(cmd, "dm-verity device corrupted")) {
		mode = REBOOT_MODE_DMVERITY_CORRUPTED;
	} else if (!strcmp(cmd, "rescue")) {
		mode = REBOOT_MODE_RESCUE;
	} else if (!strncmp(cmd, "shutdown-thermal", 16) ||
		   !strncmp(cmd, "shutdown,thermal", 16)) {
		if (ctx->reboot_data && ctx->reboot_data->thermal_warm_reboot)
			reboot_mode = REBOOT_WARM;
		mode = REBOOT_MODE_SHUTDOWN_THERMAL;
	} else if (!strcmp(cmd, "reboot-ab-update")) {
		mode = REBOOT_MODE_AB_UPDATE;
	} else if (!strcmp(cmd, "from_fastboot") ||
		   !strcmp(cmd, "shell") ||
		   !strcmp(cmd, "userrequested") ||
		   !strcmp(cmd, "userrequested,fastboot") ||
		   !strcmp(cmd, "userrequested,recovery") ||
		   !strcmp(cmd, "userrequested,recovery,ui")) {
		mode = REBOOT_MODE_NORMAL;
	}

	if (mode == REBOOT_MODE_UNKNOWN) {
		pr_err("Unknown reboot command: '%s'\n", cmd);
		return NOTIFY_DONE;
	}

	if (ctx->reboot_data && ctx->reboot_data->bms_persist_reboot_mode &&
	    pixel_reboot_mode_set(ctx, mode) < 0)
		return NOTIFY_DONE;

	return NOTIFY_OK;
}

static int pixel_reboot_parse_poweroff(struct pixel_reboot_context *ctx)
{
	enum pixel_reboot_mode mode = REBOOT_MODE_UNKNOWN;

	pr_info("Poweroff. Skipping command fetch.\n");

	/*
	 * Skip the following steps if it's not post-RDO devices with gBMS support.
	 * This logic corresponds to PIXEL_REBOOT version 3.0.
	 */
	if (!ctx->reboot_data || ctx->reboot_data->version != PIXEL_REBOOT_VER_03_00)
		return NOTIFY_DONE;

	if (!pixel_reboot_mode_get_gbms(&mode) && mode == REBOOT_MODE_NORMAL) {
		/*
		 * This is not a specific shutdown.
		 * Set reboot mode to general shutdown.
		 */
		mode = REBOOT_MODE_SHUTDOWN;
	}

	if (mode == REBOOT_MODE_UNKNOWN) {
		pr_err("Unknown reboot mode.\n");
		return NOTIFY_DONE;
	}

	if (ctx->reboot_data->bms_persist_reboot_mode && pixel_reboot_mode_set(ctx, mode) < 0)
		return NOTIFY_DONE;

	return NOTIFY_OK;
}

static int pixel_reboot_handler(struct notifier_block *nb, unsigned long action, void *cmd)
{
	struct pixel_reboot_context *ctx =
		container_of(nb, struct pixel_reboot_context, reboot_handler);
	pr_info("Reboot action: 0x%lx\n", action);
	switch (action) {
	case SYS_RESTART:
		return pixel_reboot_parse_restart(ctx, cmd);
	case SYS_POWER_OFF:
		return pixel_reboot_parse_poweroff(ctx);
	default:
		break;
	}
	return NOTIFY_DONE;
}

static int pixel_restart_handler(struct sys_off_data *data)
{
	struct pixel_reboot_context *ctx = data->cb_data;

#if IS_ENABLED(CONFIG_GS_ACPM)
	exynos_acpm_reboot();
#endif

	if (reboot_mode == REBOOT_WARM || reboot_mode == REBOOT_SOFT) {
		exynos_pmu_write(ctx->reboot_data->warm_reboot_offset,
				 ctx->reboot_data->warm_reboot_value);
	} else {
		pr_emerg("Set PS_HOLD Low.\n");
		mdelay(2);
		exynos_pmu_update(ctx->reboot_data->cold_reboot_offset,
				  ctx->reboot_data->cold_reboot_mask,
				  ctx->reboot_data->cold_reboot_value);
	}

	while (1)
		wfi();

	return NOTIFY_DONE;
}

static void pixel_reboot_hw_protection_shutdown(void *ignore, const char *reason)
{
	int len = strnlen(reason, 64);

	pr_emerg("detect hw_protection_shutdown: len(%d), reason(%s).\n", len, reason);
	if (!strncmp(reason, "Temperature too high", len))
		pixel_reboot_mode_set_gbms(REBOOT_MODE_THERMAL_HW_PROTECTION_SHUTDOWN);
	else if (!strncmp(reason, "Regulator HW failure? - no IC recovery", len))
		pixel_reboot_mode_set_gbms(REBOOT_MODE_REG_HW_FAIL_HW_PROTECTION_SHUTDOWN1);
	else if (!strncmp(reason, "Regulator HW failure. IC recovery failed", len))
		pixel_reboot_mode_set_gbms(REBOOT_MODE_REG_HW_FAIL_HW_PROTECTION_SHUTDOWN2);
	else if (!strncmp(reason, "Regulator failure. Retry count exceeded", len))
		pixel_reboot_mode_set_gbms(REBOOT_MODE_REG_FAIL_HW_PROTECTION_SHUTDOWN1);
	else if (!strncmp(reason, "Regulator failure. Recovery failed", len))
		pixel_reboot_mode_set_gbms(REBOOT_MODE_REG_FAIL_HW_PROTECTION_SHUTDOWN2);
	else if (!strncmp(reason, "System critical regulator: voltage drop detected", len))
		pixel_reboot_mode_set_gbms(REBOOT_MODE_REG_UNDER_VOLTAGE_HW_PROTECTION_SHUTDOWN);
	else if (!strncmp(reason, "System critical regulator: over-current detected", len))
		pixel_reboot_mode_set_gbms(REBOOT_MODE_REG_OVER_CURRENT_HW_PROTECTION_SHUTDOWN);
	else if (!strncmp(reason, "System critical regulator: unknown error", len))
		pixel_reboot_mode_set_gbms(REBOOT_MODE_REG_EVENT_FAIL_HW_PROTECTION_SHUTDOWN);
	else
		pixel_reboot_mode_set_gbms(REBOOT_MODE_UNKNOWN_HW_PROTECTION_SHUTDOWN);
}

static int pixel_reboot_probe(struct platform_device *pdev)
{
	struct pixel_reboot_context *ctx;
	struct device *dev = &pdev->dev;
	struct device_node *np, *pp;
	unsigned int keycode = 0;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reboot_data = of_device_get_match_data(dev);

	ctx->reboot_handler.notifier_call = pixel_reboot_handler;
	ctx->reboot_handler.priority = INT_MAX;
	ret = devm_register_reboot_notifier(dev, &ctx->reboot_handler);
	if (ret) {
		dev_err(dev, "failed to register reboot notifier, ret %d\n", ret);
		return ret;
	}

	if (register_trace_android_rvh_hw_protection_shutdown(pixel_reboot_hw_protection_shutdown,
	    NULL))
		dev_err(dev, "failed to register pixel reboot hw protection shutdown VH\n");

	if (!ctx->reboot_data || ctx->reboot_data->version >= PIXEL_REBOOT_VER_02_00)
		goto exit;

	/*
	 * We must set priority 130 to be higher than psci_sys_reset(129) and lower than
	 * other handlers. Thus we can make sure pixel_restart_handler is run in the end
	 * of system restart.
	 */
	ret = devm_register_sys_off_handler(dev, SYS_OFF_MODE_RESTART, 130, pixel_restart_handler,
					    ctx);
	if (ret) {
		dev_err(dev, "failed to register restart notifier, ret %d\n", ret);
		return ret;
	}

	// TODO: b/517435912 - Refactor gpio_keys node probing to use phandle.
	np = of_find_node_by_path("/gpio_keys");
	if (!np)
		return -EINVAL;
	for_each_child_of_node(np, pp) {
		if (!of_find_property(pp, "gpios", NULL))
			continue;
		of_property_read_u32(pp, "linux,code", &keycode);

		if (keycode == KEY_POWER) {
			power_gpio = devm_fwnode_gpiod_get_index(dev, of_fwnode_handle(pp),
								 NULL, 0, GPIOD_IN |
								 GPIOD_FLAGS_BIT_NONEXCLUSIVE,
								 NULL);
			if (IS_ERR(power_gpio)) {
				dev_err(dev, "failed to get KEY_POWER gpio (%ld)\n",
					 PTR_ERR(power_gpio));
				return PTR_ERR(power_gpio);
			}
			break;
		}
	}
	of_node_put(np);

	pixel_shutdown_offset = ctx->reboot_data->cold_reboot_offset;
	pixel_shutdown_mask = ctx->reboot_data->cold_reboot_mask;
	pixel_shutdown_value = ctx->reboot_data->cold_reboot_value;
	ret = register_platform_power_off(pixel_power_off);
	if (ret) {
		dev_err(dev, "failed to register platform power off, ret %d\n", ret);
		return ret;
	}

exit:
	dev_info(dev, "pixel reboot probe successfully (ver 0x%04x)\n",
		ctx->reboot_data ? ctx->reboot_data->version : 0);

	return ret;
}

static const struct pixel_reboot_data pixel_reboot_data_v1 = {
	.version = PIXEL_REBOOT_VER_01_00,
	.cold_reboot_offset = 0x3e9c,
	.cold_reboot_mask = 0x100,
	.cold_reboot_value = 0x0,
	.warm_reboot_offset = 0x3a00,
	.warm_reboot_value = 0x2,
	.reboot_cmd_offset = 0x810,
	.thermal_warm_reboot = true,
	.bms_persist_reboot_mode = true,
};

static const struct pixel_reboot_data pixel_reboot_data_v2 = {
	.version = PIXEL_REBOOT_VER_02_00,
	.reboot_cmd_offset = 0x810,
	.thermal_warm_reboot = true,
	.bms_persist_reboot_mode = true,
};

static const struct pixel_reboot_data pixel_reboot_data_v3 = {
	.version = PIXEL_REBOOT_VER_03_00,
	.thermal_warm_reboot = true,
	.bms_persist_reboot_mode = true,
};

static const struct pixel_reboot_data pixel_reboot_data_v3p1 = {
	.version = PIXEL_REBOOT_VER_03_01,
	.thermal_warm_reboot = true,
	.bms_persist_reboot_mode = false,
};

static const struct of_device_id pixel_reboot_of_match[] = {
	{ .compatible = "google,pixel-reboot-v1", .data = &pixel_reboot_data_v1 },
	{ .compatible = "google,pixel-reboot-v2", .data = &pixel_reboot_data_v2 },
	{ .compatible = "google,pixel-reboot-v3", .data = &pixel_reboot_data_v3 },
	{ .compatible = "google,pixel-reboot-v3p1", .data = &pixel_reboot_data_v3p1 },
	{ .compatible = "google,pixel-reboot" },
	{}
};

static struct platform_driver pixel_reboot_driver = {
	.probe = pixel_reboot_probe,
	.driver = {
		.name = "pixel-reboot",
		.of_match_table = pixel_reboot_of_match,
	},
};
module_platform_driver(pixel_reboot_driver);

MODULE_DESCRIPTION("Pixel Reboot driver");
MODULE_AUTHOR("Jone Chou<jonechou@google.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pixel-reboot");
