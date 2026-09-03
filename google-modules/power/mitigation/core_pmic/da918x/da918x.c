// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 *
 */

#include "core_pmic_defs.h"
#include "da918x.h"
#include "da9188_limits.h"

int meter_write(int pmic, struct bcl_device *bcl_dev, int idx, u8 value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return core_pmic_sub_write_register(bcl_dev, DA9188_TELEM_CH0_THRESH + idx, value,
						    false);
	case CORE_PMIC_MAIN:
		return core_pmic_main_write_register(bcl_dev, DA9188_TELEM_CH0_THRESH + idx, value,
						     false);
	}
	return 0;
}

int meter_read(int pmic, struct bcl_device *bcl_dev, int idx, u8 *value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return core_pmic_sub_read_register(bcl_dev, DA9188_TELEM_CH0_THRESH + idx, value,
						   false);
	case CORE_PMIC_MAIN:
		return core_pmic_main_read_register(bcl_dev, DA9188_TELEM_CH0_THRESH + idx, value,
						    false);
	}
	return 0;
}

int pmic_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return core_pmic_sub_write_register(bcl_dev, reg, value, false);
	case CORE_PMIC_MAIN:
		return core_pmic_main_write_register(bcl_dev, reg, value, false);
	}
	return 0;
}

int pmic_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return core_pmic_sub_read_register(bcl_dev, reg, value, false);
	case CORE_PMIC_MAIN:
		return core_pmic_main_read_register(bcl_dev, reg, value, false);
	}
	return 0;
}

