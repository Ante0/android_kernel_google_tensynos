/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Platform data for the PE26100 driver.
 */

#ifndef _PE26100_CHG_DRIVER_H_
#define _PE26100_CHG_DRIVER_H_

#include "pe26100_regs.h"

enum pe26100_chg_mode {
	PE26100_CHG_MODE_INVALID,
	PE26100_CHG_MODE_BUCK,
	PE26100_CHG_MODE_CP,
};

enum pe26100_chg_reg_profile_id {
	PE26100_CHG_REG_PROFILE_ML3,
	PE26100_CHG_REG_PROFILE_ML4,
	PE26100_CHG_REG_PROFILE_CP_2_1,
	PE26100_CHG_REG_PROFILE_CP_3_1,
	PE26100_CHG_REG_PROFILE_SETUP_CHARGER,
	PE26100_CHG_REG_PROFILE_RESET_PRECHARGING,
	/* DO NOT add after this */
	PE26100_CHG_REG_PROFILE_MAX_VALUE,
};

struct pe26100_chg_default_reg {
	uint8_t reg;
	uint8_t val;
};

struct pe26100_chg_reg_profiles {
	struct pe26100_chg_default_reg *profile;
	ssize_t len;
};

#define PE26100_CHG_INIT_DEFAULT_REG(r, v)	\
	.reg = r,				\
	.val = v,				\

struct pe26100_chg_data {
	struct device *dev;
	struct regmap *regmap;

	uint8_t chip_rev;

	struct mutex mode_lock;
	enum pe26100_chg_mode cur_mode;

	bool chg_enabled;
	bool chg_hard_disabled;

	struct pe26100_chg_reg_profiles registered_configs[PE26100_CHG_REG_PROFILE_MAX_VALUE];

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry	*de;
	unsigned int	debug_address;
#endif

	bool is_preprod;
};

int pe26100_chg_reg_read(struct device *dev, uint8_t reg, uint8_t *val);
int pe26100_chg_reg_readn(struct device *dev, uint8_t reg, uint8_t *val, int count);
int pe26100_chg_reg_write(struct device *dev, uint8_t reg, uint8_t val);
int pe26100_chg_reg_writen(struct device *dev, uint8_t reg, uint8_t *val, int count); /* NOTYPO */
int pe26100_chg_reg_update(struct device *dev, uint8_t reg, uint8_t msk, uint8_t val);

int pe26100_chg_set_online(struct device *dev, bool online, enum pe26100_chg_mode mode);
bool pe26100_chg_is_online(struct device *dev, enum pe26100_chg_mode mode);

uint8_t pe26100_get_chip_rev(struct device *dev);

int pe26100_chg_read_vbatt(struct device *dev, int *vbatt);
int pe26100_chg_current_now(struct device *dev, int *iic, enum pe26100_chg_mode mode);
int pe26100_chg_read_vout(struct device *dev, int *vout);
int pe26100_chg_read_vin(struct device *dev, int *vin);
int pe26100_chg_read_temp(struct device *dev, int *temp);

int pe26100_chg_set_charge_enabled(struct device *dev, bool enabled, enum pe26100_chg_mode mode);
int pe26100_chg_get_charge_enabled(struct device *dev, int *enabled, enum pe26100_chg_mode mode);
int pe26100_chg_set_charge_disabled(struct device *dev, bool enabled);

int pe26100_dump_all_regs(struct device *dev);
int pe26100_chg_apply_reg_fixups(struct device *dev,
				 enum pe26100_chg_reg_profile_id id,
				 const struct pe26100_chg_default_reg *fixup_regs,
				 size_t fixup_regs_size);
int pe26100_chg_apply_default_reg_config(struct device *dev,
					 enum pe26100_chg_reg_profile_id id);
enum pe26100_chg_reg_profile_id pe26100_chg_get_cur_reg_profile_id(struct device *dev);
int pe26100_chg_register_reg_profile(struct device *dev,
				     enum pe26100_chg_reg_profile_id id,
				     struct pe26100_chg_default_reg cnfg[],
				     ssize_t len);
uint8_t pe26100_chg_find_reg_config_val(struct device *dev,
					enum pe26100_chg_reg_profile_id id,
					uint8_t reg);
bool pe26100_is_es8_compat(struct device *dev);
bool pe26100_is_es10(struct device *dev);
#endif /* PE26100_CHG_DRIVER */
