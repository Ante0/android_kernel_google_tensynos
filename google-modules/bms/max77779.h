/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2023 Google, LLC
 *
 * SW Support for MAX77779 IF-PMIC
 */

#ifndef MAX77779_H_
#define MAX77779_H_

#include <linux/i2c.h>

//#define CONFIG_SCNPRINTF_DEBUG 0
#include "max77779_regs.h"

#define MAX77779_CHG_INT_COUNT 2

#define MAX77779_PMIC_REV_A0		0x01
#define MAX77779_PMIC_REV_A1		0x02

#define MAX77779_PMIC_ID_SEQ	0x79
#define MAX77779_PMIC_OF_NAME	"max77779,pmic"

/* FG's reg 0x40 and status value of 0x82 are not documented */
#define MAX77779_FG_BOOT_CHECK_REG 0x40
#define MAX77779_FG_BOOT_CHECK_SUCCESS 0x82

#define MAX77779_REASON_FIRMWARE        "FW_UPDATE"


struct device* max77779_get_dev(struct device *dev, const char *name);
int max77779_irq_of_parse_and_map(struct device_node *dev, int index);

/* write to a register */
int max77779_external_chg_reg_write(struct device *dev, u8 reg, u8 value);
/* read a register */
int max77779_external_chg_reg_read(struct device *dev, u8 reg, u8 *value);
/* update a register */
int max77779_external_chg_reg_update(struct device *dev, u8 reg, u8 mask, u8 value);
/* change the mode register */
int max77779_external_chg_mode_write(struct device *dev, uint8_t mode);
int max77779_external_chg_mode_read(struct device *dev, uint8_t *mode);
/* change the insel register */
int max77779_external_chg_insel_write(struct device *dev, u8 mask, u8 value);
/* read the insel register */
int max77779_external_chg_insel_read(struct device *dev, u8 *value);

int max77779_external_pmic_reg_read(struct device *dev, uint8_t reg, uint8_t *val);
int max77779_external_pmic_reg_write(struct device *dev, uint8_t reg, uint8_t val);
int max77779_external_pmic_reg_update(struct device *dev, uint8_t reg, uint8_t msk, uint8_t val);

int max77779_external_fg_reg_read(struct device *dev, uint16_t reg, uint16_t *val);
int max77779_external_fg_reg_write(struct device *dev, uint16_t reg, uint16_t val);

int max77779_external_vimon_reg_read(struct device *dev, uint16_t reg, void *val, int len);
int max77779_external_vimon_reg_write(struct device *dev, uint16_t reg, const void *val, int len);
int max77779_external_vimon_read_buffer(struct device *dev, uint16_t *buff, size_t *count,
					size_t buff_max);
int max77779_external_vimon_enable(struct device *dev, bool enable);

int max77779_fg_enable_firmware_update(struct device *dev, bool enable);
int max77779_fg_vdroop_snapshot(struct device *dev);

static inline int max77779_read_batt_conn(struct device *dev, int *temp)
{
	return -ENODEV;
}
static inline int max77779_read_usb_temp(struct device *dev, int *temp)
{
	return -ENODEV;
}
static inline int max77779_read_batt_id(struct device *dev, unsigned int *id)
{
	return -ENODEV;
}

#endif
