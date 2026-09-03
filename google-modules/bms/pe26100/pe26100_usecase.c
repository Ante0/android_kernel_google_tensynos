// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for PE26100 charger Usecase driver
 * Support FW Integration version: ES7.03
 * PE26100 FW Version: 3.6
 */

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif /* CONFIG_DEBUG_FS */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include "gbms_irq.h"
#include "google_bms.h"
#include "google_bms_usecase.h"
#include "pe26100_buck_charger.h"
#include "pe26100_driver.h"
#include "pe26100_regs.h"
#include "pe26100_usecase.h"

#include "max77779_usecase_v2.h"

#define PE26100_FINISH_USECASE_DEFAULT_WAIT_MS 10000

#define PE26100_USECASE_DEFAULT_RETRY_COUNT 3

struct pe26100_chg_default_reg es7_ml3_reg[] = {
	/* FSW 1.2MHz */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FREQUENCY, 0x82) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_REG, 0xC7) },		/* 4.55V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_REG, 0xB3) },		/* 4.35V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_EST, 0x01) },		/* 0 Ohm */
	/* 3.5% power loss */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EFF_LOSS, 0x46) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT, 0x7D) },		/* 125C */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC, 0x03) },		/* 72mA */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV, 0x38) },		/* 4.48V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV, 0x9C) },		/* 12.48V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV, 0x9C) },		/* 12.48V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV, 0x9C) },		/* 12.48V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV, 0x9B) },		/* 3.1V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV, 0xE4) },		/* 4.56V */
	/* 3.1V same as vbatt_uv */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV, 0x9B) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV, 0xF0) },		/* 4.8V */
	/* 100C (25C below OT) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT_WARN, 0x64) },
	/* 120mA (40mA above UC) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC_WARN, 0x05) },	/* 120 ma */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV_WARN, 0x3C) },	/* 4.8V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV_WARN, 0x96) },	/* 12V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV_WARN, 0x96) },	/* 12V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV_WARN, 0x96) },	/* 12V */
	/* 3.2V (0.1V above UV) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV_WARN, 0xA0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV_WARN, 0xE1) },	/* 4.5V */
	/* 4.5V same as vbatt_uvw */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV_WARN, 0xA0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV_WARN, 0xE6) },	/* 4.6V */

	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FLT_MASK1, 0x40) },	/* mask uc_iin */

	/* 31 consecutive fails before fault */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_DEGLITCH, 0x9F) },
	/* reset WD timer every i2c transaction */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WATCHDOG, 0x08) },
	/* max operation temp */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_LIM, 0x64) },
};

struct pe26100_chg_default_reg es8_ml3_reg[] = {
	/* FSW 1.2MHz */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FREQUENCY, 0x82) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_REG, 0xC7) },		/* 4.55V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_REG, 0xB3) },		/* 4.35V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_EST, 0x01) },		/* 0 Ohm */
	/* 3.5% power loss */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EFF_LOSS, 0x46) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT, 0x7D) },		/* 125C */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC, 0x03) },		/* 72mA */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV, 0x38) },		/* 4.48V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV, 0x95) },		/* 11.92V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV, 0x95) },		/* 11.92V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV, 0x95) },		/* 11.92V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV, 0x9B) },		/* 3.1V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV, 0xE4) },		/* 4.56V */
	/* 3.1V same as vbatt_uv */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV, 0x9B) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV, 0xF0) },		/* 4.8V */
	/* 100C (25C below OT) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT_WARN, 0x64) },
	/* 120mA (40mA above UC) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC_WARN, 0x05) },	/* 120 ma */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV_WARN, 0x3C) },	/* 4.8V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV_WARN, 0x8F) },	/* 11.44V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV_WARN, 0x8F) },	/* 11.44V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV_WARN, 0x8F) },	/* 11.44V */
	/* 3.2V (0.1V above UV) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV_WARN, 0xA0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV_WARN, 0xE1) },	/* 4.5V */
	/* 4.5V same as vbatt_uvw */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV_WARN, 0xA0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV_WARN, 0xE6) },	/* 4.6V */

	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FLT_MASK1, 0x40) },	/* mask uc_iin */

	/* mask everything but iin_uc */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WARN_MASK1, 0xF7) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WARN_MASK2, 0xFF) }, /* mask all */

	/* 31 consecutive fails before fault */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_DEGLITCH, 0x8F) },
	/* reset WD timer every i2c transaction */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WATCHDOG, 0x08) },
	/* max operation temp */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_LIM, 0x64) },

	/* ES8 only */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_BUCK_CTRL, 0x13) },

	/* ES8 untrimmed chips - NOTE order matters */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_BACK_REG_UNLOCK_1, 0xA5) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_BACK_REG_UNLOCK_2, 0x96) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_SECRET_1, 0x21) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_SECRET_3, 0x30) },
};

struct pe26100_chg_default_reg es7_ml4_reg[] = {
	/* Input ramp 40mA/128ms; Dither off; FSW 1.2MHz */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FREQUENCY, 0x12) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_OC, 0x58) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_REG, 0xC7) },		/* 4.55V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_REG, 0xB3) },		/* 4.35V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_EST, 0x01) },		/* 0 ohm */
	/* 3.5% power loss */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EFF_LOSS, 0x46) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT, 0x7D) },		/* 125C */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC, 0x03) },		/* 72mA */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV, 0x38) },		/* 4.48V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV, 0xF3) },		/* 19.44V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV, 0xF3) },		/* 19.44V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV, 0xF3) },		/* 19.44V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV, 0x9B) },		/* 3.1V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV, 0xE4) },		/* 4.56V */
	/* 3.1V same as vbatt_uv */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV, 0x9B) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV, 0xF0) },		/* 4.8V */
	/* 100C (25C below OT) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT_WARN, 0x64) },
	/* 120mA (40mA above UC) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC_WARN, 0x05) },	/* 120ma */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV_WARN, 0x3C) },	/* 4.8V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV_WARN, 0xED) },	/* 18.96V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV_WARN, 0xED) },	/* 18,96V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV_WARN, 0xED) },	/* 18.96V */
	/* 3.2V (0.1V above UV) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV_WARN, 0xA0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV_WARN, 0xE1) },	/* 4.5V */
	/* 4.5V same as vbatt_uvw */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV_WARN, 0xA0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV_WARN, 0xE6) },	/* 4.6V */

	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FLT_MASK1, 0x40) },	/* mask uc_iin */

	/* 31 consecutive fails before fault */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_DEGLITCH, 0x9F) },
	/* reset WD timer every i2c transaction */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WATCHDOG, 0x08) },
	/* max operation temp */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_LIM, 0x64) },
};

struct pe26100_chg_default_reg es8_ml4_reg[] = {
	/* FSW 1.2MHz */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FREQUENCY, 0x2) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_OC, 0x58) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_REG, 0xC7) },		/* 4.55V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_REG, 0xB3) },		/* 4.35V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IOUT_EST, 0x01) },		/* 0 ohm */
	/* 3.5% power loss */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EFF_LOSS, 0x78) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT, 0x7D) },		/* 125C */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC, 0x03) },		/* 72mA */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV, 0x70) },		/* 8.96V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV, 0xF3) },		/* 19.44V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV, 0xF3) },		/* 19.44V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV, 0xF3) },		/* 19.44V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV, 0x9B) },		/* 3.1V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV, 0xE4) },		/* 4.56V */
	/* 3.1V same as vbatt_uv */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV, 0x9B) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV, 0xF0) },		/* 4.8V */
	/* 100C (25C below OT) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_OT_WARN, 0x64) },
	/* 120mA (40mA above UC) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_IIN_UC_WARN, 0x05) },	/* 120ma */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_UV_WARN, 0x3C) },	/* 4.8V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VIN_OV_WARN, 0xED) },	/* 18.96V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT1_OV_WARN, 0xED) },	/* 18,96V */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EXT2_OV_WARN, 0xED) },	/* 18.96V */
	/* 3.2V (0.1V above UV) */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_UV_WARN, 0xA0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VBATT_OV_WARN, 0xE1) },	/* 4.5V */
	/* 4.5V same as vbatt_uvw */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_UV_WARN, 0xA0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_VOUT_OV_WARN, 0xE6) },	/* 4.6V */

	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FLT_MASK1, 0x40) },	/* mask uc_iin */

	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WARN_MASK1, 0xFF) }, /* mask all */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WARN_MASK2, 0x0F) }, /* mask all */

	/* 31 consecutive fails before fault */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_DEGLITCH, 0x8F) },
	/* reset WD timer every i2c transaction */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_WATCHDOG, 0x08) },
	/* max operation temp */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_TEMP_LIM, 0x64) },

	/* ES8 only */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_BUCK_CTRL, 0x13) },

	/* ES8 untrimmed chips - NOTE order matters */
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_BACK_REG_UNLOCK_1, 0xA5) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_BACK_REG_UNLOCK_2, 0x96) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_FEED_FORWARD, 0x29) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_CAP_BALANCE, 0x0C) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_SECRET_1, 0x21) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_SECRET_2, 0x0E) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_SECRET_3, 0x30) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_4, 0x31) },
};

static int pe26100_uc_get_charger_mode_from_uc(struct bms_usecase_entry *entry,
					       struct pe26100_uc_data *uc_data)
{
	uint8_t mode = 0;

	switch (entry->usecase) {
	case GSU_MODE_USB_CHG_HYBRID:
		mode = _pe26100_chg_mode_extg_en_set(0, 1) |
		       _pe26100_chg_mode_pt_en_set(0, 1);
		break;
	case GSU_MODE_WLC_RX_HYBRID:
	case GSU_MODE_USB_OTG_WLC_RX_HYBRID:
		mode = _pe26100_chg_mode_extg_en_set(0, 1) |
		       _pe26100_chg_mode_extgx_set(0, 1) |
		       _pe26100_chg_mode_pt_en_set(0, 1);
		break;
	default:
		break;
	}

	return mode;
}

static int pe26100_usecase_soft_reset(struct pe26100_uc_data *uc_data)
{
	int ret;

	ret = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_IC_ENABLE, 0);
	if (ret) {
		dev_err(uc_data->dev, "Error disabling IC ret:%d\n", ret);
		return ret;
	}

	usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	ret = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_IC_ENABLE, 1);
	if (ret) {
		dev_err(uc_data->dev, "Error enabling IC ret:%d\n", ret);
		return ret;
	}

	usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	return 0;
}

/* wired buck charging only */
static int pe26100_usecase_to_ml3(struct pe26100_uc_data *uc_data)
{
	int ret;

	ret = pe26100_usecase_soft_reset(uc_data);
	if (ret)
		goto done;

	ret = pe26100_chg_apply_default_reg_config(uc_data->core, PE26100_CHG_REG_PROFILE_ML3);
	if (ret) {
		dev_err(uc_data->dev, "Error applying default_config:%d\n", ret);
		goto done;
	}

	ret = pe26100_buck_apply_charger_current_max_ua();
	if (ret) {
		dev_err(uc_data->dev, "Error applying cc_max:%d\n", ret);
		goto done;
	}

	ret = pe26100_buck_apply_regulation_voltage();
	if (ret) {
		dev_err(uc_data->dev, "Error applying fv_uv:%d\n", ret);
		goto done;
	}

	ret = pe26100_buck_apply_ilim_max_ua(PE26100_BUCK_MODE_WIRED);
	if (ret) {
		dev_err(uc_data->dev, "Error applying ilim:%d\n", ret);
		goto done;
	}

done:
	if (ret) {
		const int ret1 = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_IC_ENABLE, 0);

		if (ret1)
			dev_err(uc_data->dev, "Error disabling IC ret:%d\n", ret1);
		usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	} else {
		pe26100_buck_set_online(true, PE26100_BUCK_MODE_WIRED);
	}

	return ret;
}

/* wireless buck charging only */
static int pe26100_usecase_to_ml4(struct pe26100_uc_data *uc_data)
{
	int ret;

	ret = pe26100_usecase_soft_reset(uc_data);
	if (ret)
		goto done;

	ret = pe26100_chg_apply_default_reg_config(uc_data->core, PE26100_CHG_REG_PROFILE_ML4);
	if (ret) {
		dev_err(uc_data->dev, "Error applying default_config:%d\n", ret);
		goto done;
	}

	ret = pe26100_buck_apply_charger_current_max_ua();
	if (ret) {
		dev_err(uc_data->dev, "Error applying cc_max:%d\n", ret);
		goto done;
	}

	ret = pe26100_buck_apply_regulation_voltage();
	if (ret) {
		dev_err(uc_data->dev, "Error applying fv_uv:%d\n", ret);
		goto done;
	}

	ret = pe26100_buck_apply_ilim_max_ua(PE26100_BUCK_MODE_WIRELESS);
	if (ret) {
		dev_err(uc_data->dev, "Error applying ilim:%d\n", ret);
		goto done;
	}

done:
	if (ret) {
		const int ret1 = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_IC_ENABLE, 0);

		if (ret1)
			dev_err(uc_data->dev, "Error disabling IC ret:%d\n", ret1);
		usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	} else {
		pe26100_buck_set_online(true, PE26100_BUCK_MODE_WIRELESS);
	}

	return ret;
}

static void pe26100_usecase_finish_usecase_work(struct work_struct *work)
{
	struct pe26100_uc_data *uc_data = container_of(work, struct pe26100_uc_data,
						       finish_usecase_work.work);
	int ret;

	guard(mutex)(&uc_data->uc_lock);

	/* re-enable all faults */
	ret = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_FLT_MASK1, 0x0);
	if (ret) {
		dev_err_ratelimited(uc_data->dev, "Error enabling faults ret:%d\n", ret);
		goto retry;
	}

	dev_info_ratelimited(uc_data->dev, "Re-enabled faults\n");

	pe26100_buck_set_init_complete(true);

	if (pe26100_is_es8_compat(uc_data->core) &&
	    bms_usecase_is_uc_wireless(uc_data->cur_usecase)) {
		ret = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_BUCK_CTRL, 0x1F);
		if (ret) {
			dev_err_ratelimited(uc_data->dev, "Error writing buck_ctrl ret:%d\n", ret);
			goto retry;
		}

		ret = pe26100_buck_apply_ilim_max_ua(PE26100_BUCK_MODE_WIRELESS);
		if (ret) {
			dev_err_ratelimited(uc_data->dev, "Error applying ilim:%d\n", ret);
			pe26100_buck_set_init_complete(false);
			goto retry;
		}
	}

	if (uc_data->cur_usecase == GSU_MODE_USB_CHG_HYBRID)
		pe26100_buck_charger_schedule_aicl_work(true);
	pe26100_buck_enable_irq(true);

	return;

retry:
	schedule_delayed_work(&uc_data->finish_usecase_work, msecs_to_jiffies(100));

}

/* Requires &uc_data->uc_lock to be held */
static int pe26100_usecase_uc_offline(struct pe26100_uc_data *uc_data)
{
	int ret;

	ret = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_IC_ENABLE, 0);
	if (ret) {
		dev_err(uc_data->dev, "Error disabling IC ret:%d\n", ret);
		return ret;
	}
	usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);

	/* finish_usecase_work must run with uc_data->uc_lock unlocked */
	mutex_unlock(&uc_data->uc_lock);
	cancel_delayed_work_sync(&uc_data->finish_usecase_work);
	mutex_lock(&uc_data->uc_lock);

	pe26100_buck_set_init_complete(false);
	pe26100_buck_charger_schedule_aicl_work(false);
	pe26100_buck_enable_irq(false);

	return 0;
}

static int pe26100_usecase_set_mode(struct pe26100_uc_data *uc_data,
				    struct bms_usecase_entry *entry)
{
	const int from_uc = bms_usecase_get_usecase();
	const struct bms_usecase_foreach_cb_data *cb_data = entry->cb_data;
	const uint8_t new_mode = pe26100_uc_get_charger_mode_from_uc(entry, uc_data);
	const bool is_wireless = bms_usecase_is_uc_wireless(entry->usecase);
	const struct pe26100_chg_default_reg mode_reg[] = {
		{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_MODE, is_wireless ? 0x90 : 0x80) },
	};
	ktime_t start_time, end_time;
	uint8_t reg;
	int ret;
	int vin = 0;

	ret = pe26100_chg_reg_read(uc_data->core, PE26100_CHG_MODE, &reg);
	if (ret < 0)
		dev_err(uc_data->dev, "Error reading mode reg ret:%d\n", ret);

	ret = pe26100_chg_apply_reg_fixups(uc_data->core, PE26100_CHG_REG_PROFILE_SETUP_CHARGER,
					   mode_reg, sizeof(mode_reg));
	if (ret < 0) {
		dev_err(uc_data->dev, "Error applying mode reg change:%d\n", ret);
		return ret;
	}

	ret = pe26100_chg_apply_default_reg_config(uc_data->core,
						   PE26100_CHG_REG_PROFILE_SETUP_CHARGER);
	if (ret) {
		dev_err(uc_data->dev, "Error setting up charger:%d\n", ret);
		return ret;
	}

	start_time = ktime_get();
	ret = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_MODE, new_mode);
	if (ret < 0)
		dev_err(uc_data->dev, "use_case=%d->%d MODE=0x%x failed ret:%d\n",
			from_uc, entry->usecase, new_mode, ret);

	usleep_range(2 * USEC_PER_MSEC, 3 * USEC_PER_MSEC);

	ret = pe26100_chg_read_vin(uc_data->core, &vin);
	if (ret < 0)
		dev_warn(uc_data->dev, "Error reading vin %d\n", ret);

	switch (entry->usecase) {
	case GSU_MODE_USB_CHG_HYBRID:
		if (vin <= 6000000)  { /* < 6V */
			if (pe26100_is_es10(uc_data->core))
				usleep_range(10 * USEC_PER_MSEC, 15 * USEC_PER_MSEC);
			else
				msleep(25);
		} else {
			if (pe26100_is_es10(uc_data->core))
				usleep_range(15 * USEC_PER_MSEC, 20 * USEC_PER_MSEC);
			else
				msleep(35);
		}
		break;
	case GSU_MODE_WLC_RX_HYBRID:
	case GSU_MODE_USB_OTG_WLC_RX_HYBRID:
		if (vin >= 13000000) {/* 13V */
			if (pe26100_is_es10(uc_data->core))
				msleep(25);
			else
				msleep(50);
		} else {
			if (pe26100_is_es10(uc_data->core))
				usleep_range(20 * USEC_PER_MSEC, 25 * USEC_PER_MSEC);
			else
				msleep(40);
		}
		break;
	default:
		break;
	}

	ret = pe26100_chg_apply_default_reg_config(uc_data->core,
						   PE26100_CHG_REG_PROFILE_RESET_PRECHARGING);
	if (ret) {
		dev_err(uc_data->dev, "Error resetting precharging:%d\n", ret);
		return ret;
	}

	end_time = ktime_get();

	dev_info(uc_data->dev, "%s:%s use_case=%s(%d)->%s(%d) CHARGER_MODE=0x%x->0x%x setup_time_ms:%lld vin:%d\n",
		__func__, cb_data ? cb_data->reason ? cb_data->reason : "<>" : "RESET",
		bms_usecase_to_str(from_uc), from_uc,
		bms_usecase_to_str(entry->usecase), entry->usecase,
		ret ? ret : reg, new_mode,
		ktime_to_ms(ktime_sub(end_time, start_time)),
		vin);

	return 0;
}

/*
 * switch to a use case, handle the transitions
 * Requires &uc_data->uc_lock to be held
 */
static int pe26100_usecase_set_usecase(void *d, struct bms_usecase_entry *entry)
{
	struct pe26100_uc_data *uc_data = d;
	const int from_uc = bms_usecase_get_usecase();
	const int use_case = entry->usecase;
	bool set_mode_reg = true;
	int ret;

	if (((from_uc == use_case) || !bms_usecase_is_chg_changed(uc_data->cur_usecase, use_case))
	     && (entry->state != BMS_USECASE_FORCE_USECASE))
		return 0;

	switch (use_case) {
	case GSU_MODE_USB_CHG_HYBRID:
		ret = pe26100_chg_set_online(uc_data->core, 1, PE26100_CHG_MODE_BUCK);
		if (ret)
			return ret;

		ret = pe26100_usecase_to_ml3(uc_data);
		if (ret) {
			pe26100_buck_vote_dc_avail(GBMS_ALL_SEC_CHG_DISABLED ,0);
			return ret;
		}
		break;
	case GSU_MODE_WLC_RX_HYBRID:
	case GSU_MODE_USB_OTG_WLC_RX_HYBRID:
		ret = pe26100_chg_set_online(uc_data->core, 1, PE26100_CHG_MODE_BUCK);
		if (ret)
			return ret;
		ret = pe26100_usecase_to_ml4(uc_data);
		if (ret) {
			pe26100_buck_vote_dc_avail(GBMS_ACTIVE_CHG_DISABLE, 0);
			return ret;
		}
		break;
	default:
		set_mode_reg = false;
		break;
	}

	if (set_mode_reg) {
		ret = pe26100_usecase_set_mode(uc_data, entry);
		schedule_delayed_work(&uc_data->finish_usecase_work,
				      msecs_to_jiffies(PE26100_FINISH_USECASE_DEFAULT_WAIT_MS));
	} else if (from_uc == GSU_MODE_USB_CHG_HYBRID || from_uc == GSU_MODE_WLC_RX_HYBRID ||
		   from_uc == GSU_MODE_USB_OTG_WLC_RX_HYBRID) {
		/* Disable PE26100 */
		dev_dbg(uc_data->dev, "Disabling IC ret:%d\n", ret);
		cancel_delayed_work(&uc_data->reset_work);

		ret = pe26100_usecase_uc_offline(uc_data);
		if (ret)
			dev_warn(uc_data->dev, "Failed to offline\n");

		ret = pe26100_buck_set_online(false, from_uc == GSU_MODE_USB_CHG_HYBRID ?
					      PE26100_BUCK_MODE_WIRED :
					      PE26100_BUCK_MODE_WIRELESS);
		if (ret)
			return ret;

		ret = pe26100_chg_set_online(uc_data->core, 0, PE26100_CHG_MODE_BUCK);
		if (ret)
			return ret;
		uc_data->retry_count = PE26100_USECASE_DEFAULT_RETRY_COUNT;
	}

	if (!ret)
		uc_data->cur_usecase = use_case;

	return ret;
}

static void pe26100_usecase_uc_reset(struct pe26100_uc_data *uc_data)
{
	struct bms_usecase_entry entry;
	int ret;

	dev_info(uc_data->dev, "Resetting chip... retries:%d\n", uc_data->retry_count);

	mutex_lock(&uc_data->uc_lock);

	entry.usecase = uc_data->cur_usecase;
	entry.state = BMS_USECASE_FORCE_USECASE;
	/*
	 * don't call pe26100_usecase_set_usecase with a standby usecase because we don't want
	 * to offline the charger
	 */
	ret = pe26100_usecase_uc_offline(uc_data);
	if (ret)
		goto unlock;

	pe26100_usecase_set_usecase((void *)uc_data, &entry);

unlock:
	mutex_unlock(&uc_data->uc_lock);
}

static int pe26100_usecase_from_uc_completion_cb(void *d, struct bms_usecase_entry *entry)
{
	struct pe26100_uc_data *uc_data = d;
	const struct bms_usecase_foreach_cb_data *cb_data = entry->cb_data;
	int ret;

	/* transitioning to a pe26100 usecase */
	if (cb_data->chg_sel == MAX77779_UC_V2_CHG_SEL_HYBRID)
		return 0;

	mutex_lock(&uc_data->uc_lock);
	ret = pe26100_usecase_set_usecase(d, entry);
	mutex_unlock(&uc_data->uc_lock);

	return ret;
}

static int pe26100_usecase_to_uc_completion_cb(void *d, struct bms_usecase_entry *entry)
{
	struct pe26100_uc_data *uc_data = d;
	const struct bms_usecase_foreach_cb_data *cb_data = entry->cb_data;
	int ret;

	/*
	 * transitioning to a non-pe26100 usecase
	 * handled in pe26100_usecase_from_uc_completion_cb
	 */
	if (cb_data->chg_sel != MAX77779_UC_V2_CHG_SEL_HYBRID)
		return 0;

	mutex_lock(&uc_data->uc_lock);
	ret = pe26100_usecase_set_usecase(d, entry);
	mutex_unlock(&uc_data->uc_lock);

	return ret;
}

static void pe26100_usecase_init_work(struct work_struct *work)
{
	struct pe26100_uc_data *uc_data = container_of(work, struct pe26100_uc_data,
						       init_work.work);
	int ret;

	ret = bms_usecase_register_completion_cb(uc_data,
						 pe26100_usecase_from_uc_completion_cb,
						 pe26100_usecase_to_uc_completion_cb);
	if (ret == 0) {
		dev_dbg(uc_data->dev, "Init complete\n");
	} else if (ret == -EAGAIN) {
		schedule_delayed_work(&uc_data->init_work, msecs_to_jiffies(100));
	} else {
		dev_err(uc_data->dev, "Error registering PE26100 usecase CB (%d)\n", ret);
	}
}

/*
 * make reset a delayed work item because disable_irq blocks until irq is complete so you
 * can't call it in an interrupt context
 */
static void pe26100_usecase_reset_work(struct work_struct *work)
{
	struct pe26100_uc_data *uc_data = container_of(work, struct pe26100_uc_data,
						       reset_work.work);

	pe26100_usecase_uc_reset(uc_data);

	if (uc_data->retry_count > 0)
		uc_data->retry_count--;
}

static irqreturn_t pe26100_usecase_irq_handler(int irq, void *ptr)
{
	struct pe26100_uc_data *uc_data = ptr;
	gbms_irq_t val;
	int ret;
	int chg_avail = 1;

	if (!pe26100_buck_is_online(PE26100_BUCK_MODE_WIRELESS))
		return IRQ_NONE;

	ret = gbms_irq_read(irq, &val);
	if (ret) {
		dev_err(uc_data->dev, "Error reading IRQ:%d\n", ret);
		return IRQ_NONE;
	}

	if (((val & GBMS_IRQ_VIN_UV_MASK) || (val & GBMS_IRQ_IIN_UC_MASK) ||
	    (val & GBMS_IRQ_IC_OCP_MASK)) && uc_data->retry_count)
		mod_delayed_work(system_wq, &uc_data->reset_work, msecs_to_jiffies(1500));
	else if ((val & GBMS_IRQ_VIN_OV_MASK) || (val & GBMS_IRQ_IIN_OC_MASK))
		chg_avail = GBMS_ACTIVE_CHG_DISABLE;
	else
		chg_avail = GBMS_ALL_SEC_CHG_DISABLED;

	if (chg_avail <= GBMS_ALL_SEC_CHG_DISABLED) {
		pe26100_buck_vote_dc_avail(chg_avail, 0);
		/* disable chip after voting to prevent irq spam */
		ret = pe26100_chg_reg_write(uc_data->core, PE26100_CHG_IC_ENABLE, 0);
		if (ret)
			dev_err(uc_data->dev, "Failed to disable chip (%d)\n", ret);
	}

	return IRQ_HANDLED;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int pe26100_usecase_hw_reset(void *d, u64 val)
{
	struct pe26100_uc_data *uc_data = d;

	pe26100_usecase_uc_reset(uc_data);

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(debug_ops_pe26100_uc_hw_reset, NULL, pe26100_usecase_hw_reset, "%llu\n");

static int pe26100_usecase_init_debugfs(struct pe26100_uc_data *uc_data)
{
	uc_data->de = debugfs_create_dir("pe26100_usecase", NULL);
	if (IS_ERR_OR_NULL(uc_data->de)) {
		dev_err(uc_data->dev, "Couldn't create debug dir\n");
		return -ENOENT;
	}

	debugfs_create_file("reset", 0400, uc_data->de, uc_data, &debug_ops_pe26100_uc_hw_reset);

	return 0;
}
#endif /* DEBUGFS */

static int pe26100_usecase_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct pe26100_uc_data *uc_data;
	int irq_in;
	struct pe26100_chg_default_reg *cnfg;
	ssize_t cnfg_len;
	const bool is_es8 = pe26100_is_es8_compat(dev->parent);

	irq_in = platform_get_irq(pdev, 0);
	if (irq_in == -EPROBE_DEFER)
		return irq_in;
	else if (irq_in < 0)
		dev_warn(dev, "Failed to get IRQ ret:%d\n", irq_in);

	uc_data = devm_kzalloc(dev, sizeof(*uc_data), GFP_KERNEL);
	if (!uc_data) {
		dev_err(dev, "Error allocating uc_data!!!\n");
		return -ENOMEM;
	}

	uc_data->dev = dev;
	uc_data->dev->init_name = "pe26100_usecase";
	uc_data->core = dev->parent;
	uc_data->retry_count = PE26100_USECASE_DEFAULT_RETRY_COUNT;

	INIT_DELAYED_WORK(&uc_data->init_work, pe26100_usecase_init_work);
	INIT_DELAYED_WORK(&uc_data->finish_usecase_work, pe26100_usecase_finish_usecase_work);
	INIT_DELAYED_WORK(&uc_data->reset_work, pe26100_usecase_reset_work);

	mutex_init(&uc_data->uc_lock);

	cnfg = is_es8 ? es8_ml3_reg : es7_ml3_reg;
	cnfg_len = is_es8 ? sizeof(es8_ml3_reg) : sizeof(es7_ml3_reg);
	ret = pe26100_chg_register_reg_profile(uc_data->core,
					       PE26100_CHG_REG_PROFILE_ML3,
					       cnfg,
					       (cnfg_len /
						sizeof(struct pe26100_chg_default_reg)));
	if (ret)
		return ret;

	cnfg = is_es8 ? es8_ml4_reg : es7_ml4_reg;
	cnfg_len = is_es8 ? sizeof(es8_ml4_reg) : sizeof(es7_ml4_reg);
	ret = pe26100_chg_register_reg_profile(uc_data->core,
					       PE26100_CHG_REG_PROFILE_ML4,
					       cnfg,
					       (cnfg_len /
						sizeof(struct pe26100_chg_default_reg)));
	if (ret)
		return ret;

	if (irq_in >= 0) {
		ret = devm_request_threaded_irq(dev, irq_in, NULL,
						pe26100_usecase_irq_handler,
						IRQF_SHARED | IRQF_ONESHOT,
						"pe26100_usecase_irq_handler",
						uc_data);
		if (ret < 0)
			dev_warn(dev, "Error setting up irq ret:%d\n", ret);
	}

#if IS_ENABLED(CONFIG_DEBUG_FS)
	ret = pe26100_usecase_init_debugfs(uc_data);
	if (ret) {
		dev_err(dev, "Error initing debugfs %d\n", ret);
		return ret;
	}
#endif

	schedule_delayed_work(&uc_data->init_work, msecs_to_jiffies(100));

	return 0;
}

static void pe26100_usecase_remove(struct platform_device *pdev)
{

}

static const struct platform_device_id pe26100_usecase_id[] = {
	{ "pe26100-usecase", 0},
	{},
};
MODULE_DEVICE_TABLE(platform, pe26100_usecase_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id pe26100_usecase_match_table[] = {
	{ .compatible = "pe26100-usecase",},
	{ },
};
#endif

static struct platform_driver pe26100_usecase_driver = {
	.probe = pe26100_usecase_probe,
	.remove = pe26100_usecase_remove,
	.id_table = pe26100_usecase_id,
	.driver = {
		.name = "pe26100-usecase",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = pe26100_usecase_match_table,
#endif
	},
};

module_platform_driver(pe26100_usecase_driver);

MODULE_DESCRIPTION("PE26100 Usecase driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
