// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ganymede chip specific GXP MicroController Unit management.
 *
 * Copyright (C) 2024 Google LLC
 */

#include "gxp-config.h"
#include "gxp-internal.h"
#include "gxp-lpm.h"
#include "gxp-mcu.h"
#include "gxp-mcu-firmware.h"
#include "gxp-mcu-platform.h"

bool gxp_mcu_need_lpm_init(struct gxp_mcu_firmware *mcu_fw)
{
	if (mcu_fw->is_secure)
		return false;

	return true;
}

int gxp_mcu_reset(struct gxp_dev *gxp, bool release_reset)
{
	/* MCU reset is not supported from kernel driver. */
	dev_info(gxp->dev, "NS reset unspported");
	return -EOPNOTSUPP;
}

bool gxp_mcu_recovery_boot_shutdown(struct gxp_dev *gxp, bool force)
{
	struct gxp_mcu_firmware *mcu_fw = gxp_mcu_firmware_of(gxp);
	int ret;

	lockdep_assert_held(&mcu_fw->lock);

	if (!gxp_lpm_wait_state_eq(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), LPM_PG_STATE))
		goto recovery;

	ret = gxp_mcu_firmware_shutdown(mcu_fw);
	if (ret >= 0)
		return true;

	/* Likely could be due to communication issue with GSA. */
	dev_warn(gxp->dev, "MCU shutdown by GSA failed (ret=%d)", ret);
	return false;
recovery:
	dev_info(gxp->dev, "Initiating MCU recovery via GSA");
	/* GSA is expected to reset MCU in recovery mode if MCU is not in PG.
	 *
	 * 1. GSA checks if MCU is in PG, and if so, powers down SSWRAP and returns success.
	 * 2. If not, GSA toggles reset and returns failure
	 *    a. KD waits for MCU PG, and once done, calls GSA shutdown again (step #1)
	 *    b. If not (recovery process failed), prints warning and returns failure
	 */
	gxp_mcu_set_boot_mode(mcu_fw, GXP_MCU_BOOT_MODE_RECOVERY);

	ret = gxp_mcu_firmware_shutdown(mcu_fw);
	if (ret >= 0)
		return true;

	if (gxp_lpm_wait_state_eq(gxp, CORE_TO_PSM(GXP_REG_MCU_ID), LPM_PG_STATE)) {
		/*
		 * if after recovery MCU falls in PG, expect clean GSA
		 * shutdown.
		 */
		ret = gxp_mcu_firmware_shutdown(mcu_fw);
		if (ret >= 0)
			return true;

		/* Likely could be due to communication issue with GSA. */
		dev_warn(gxp->dev, "MCU recovery shutdown by GSA failed (ret=%d)", ret);
		return false;
	}
	dev_warn(gxp->dev, "MCU PSM transition to PG via recovery mode failed, current state: %u",
			gxp_lpm_get_state(gxp, CORE_TO_PSM(GXP_REG_MCU_ID)));
	return false;
}
