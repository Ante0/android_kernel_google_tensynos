/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS I2C Device Driver
 *
 * Copyright (c) 2018 Google, LLC
 */

#ifndef LWIS_DEVICE_I2C_H_
#define LWIS_DEVICE_I2C_H_

#include <linux/i2c.h>
#include <linux/pinctrl/consumer.h>

#include "lwis_device.h"
#include "lwis_bus_manager.h"
#include "lwis_device_i3c_proxy.h"

#define MAX_I2C_LOCK_NUM 8

struct lwis_otp_setting {
	u32 reg_addr;
	u32 value;
};

struct lwis_otp_config {
	struct lwis_otp_setting *settings;
	int setting_count;
	uint32_t settle_time_us;
};

enum lwis_i3c_match_type {
	LWIS_I3C_MATCH_NONE = 0,
	LWIS_I3C_MATCH_PID,
	LWIS_I3C_MATCH_DCR,
	LWIS_I3C_MATCH_BCR
};

union i3c_match_union {
	u64 pid;
	u8 dcr;
	u8 bcr;
};

/*
 *  struct lwis_i2c_device
 *  "Derived" lwis_device struct, with added i2c/i3c related elements.
 */
struct lwis_i2c_device {
	struct lwis_device base_dev;
	int address;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct pinctrl *state_pinctrl;
	bool pinctrl_default_state_only;
	/* Group id for I2C lock */
	u32 i2c_lock_group_id;
	/* Mutex shared by the same group id's I2C devices */
	struct mutex *group_i2c_lock;
	/* Device priority for bus manager processing order */
	int device_priority;
	/* I3C device */
	struct i3c_device *i3c;
	enum lwis_i3c_match_type match_type;
	union i3c_match_union match;
	bool i3c_enabled;
	bool ibi_enabled;
	struct lwis_otp_config i3c_otp_config;
	struct lwis_otp_config i2c_otp_config;
	struct lwis_i3c_ibi_config ibi_config;
	bool is_i2c_otp;
};

int lwis_i2c_device_init(void);
int lwis_i2c_device_deinit(void);
int lwis_i2c_device_setup(struct lwis_i2c_device *i2c_dev);
int lwis_otp_set_config(struct lwis_i2c_device *device, struct lwis_otp_config *otp_config);

#if IS_ENABLED(CONFIG_INPUT_STMVL53L1)
/*
 * Module stmvl53l1 shares one i2c bus with some lwis i2c devices. And use the
 * two APIs in stmvl53l1 driver to well handle the enabling and disabling.
 */
extern bool is_shared_i2c_with_stmvl53l1(struct pinctrl *pinctrl);
extern int shared_i2c_set_state(struct device *dev, struct pinctrl *pinctrl, const char *state_str);
#endif

#endif /* LWIS_DEVICE_I2C_H_ */
