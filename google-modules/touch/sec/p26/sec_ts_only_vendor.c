/* drivers/input/touchscreen/sec_ts_fw.c
 *
 * Copyright (C) 2015 Samsung Electronics Co., Ltd.
 * http://www.samsungsemi.com/
 *
 * Core file for Samsung TSC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/irq.h>
#include <linux/of_gpio.h>
#include <linux/time.h>
#include <linux/vmalloc.h>

#include <linux/uaccess.h>
/*#include <asm/gpio.h>*/

#include "sec_ts.h"

u8 lv1cmd;
u8 *read_lv1_buff;
static int lv1_readsize;
static int lv1_readremain;
static int lv1_readoffset;

static ssize_t sec_ts_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t sec_ts_regreadsize_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static inline ssize_t sec_ts_store_error(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t sec_ts_enter_recovery_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t sec_ts_regread_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t sec_ts_gesture_status_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static inline ssize_t sec_ts_show_error(struct device *dev,
		struct device_attribute *attr, char *buf);

static DEVICE_ATTR_WO(sec_ts_reg);
static DEVICE_ATTR_WO(sec_ts_regreadsize);
static DEVICE_ATTR_WO(sec_ts_enter_recovery);
static DEVICE_ATTR_RO(sec_ts_regread);
static DEVICE_ATTR_RO(sec_ts_gesture_status);

static struct attribute *cmd_attributes[] = {
	&dev_attr_sec_ts_reg.attr,
	&dev_attr_sec_ts_regreadsize.attr,
	&dev_attr_sec_ts_enter_recovery.attr,
	&dev_attr_sec_ts_regread.attr,
	&dev_attr_sec_ts_gesture_status.attr,
	NULL,
};

static struct attribute_group cmd_attr_group = {
	.attrs = cmd_attributes,
};

static ssize_t sec_ts_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct sec_ts_data *ts = dev_get_drvdata(dev);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGI("Power off state\n");
		return -EIO;
	}

	if (size > 0)
		ts->sec_ts_write(ts, (u8)buf[0], (u8 *)&buf[1], size - 1);

	LOGI("0x%x, 0x%x, size %d\n", buf[0], buf[1], (int)size);
	return size;
}

static ssize_t sec_ts_regread_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct sec_ts_data *ts = dev_get_drvdata(dev);
	int ret;

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("Power off state\n");
		return -EIO;
	}

	ts->sec_irq_enable(ts, false);
	mutex_lock(&ts->device_mutex);

	read_lv1_buff = kzalloc(lv1_readsize, GFP_KERNEL);
	if (!read_lv1_buff)
		goto malloc_err;

	ret = ts->sec_ts_read(ts, lv1cmd, read_lv1_buff, lv1_readsize);
	if (ret < 0) {
		LOGE("read %x command fail\n", lv1cmd);
		goto i2c_err;
	}

	LOGI("lv1_readsize = %d\n", lv1_readsize);
	memcpy(buf, read_lv1_buff + lv1_readoffset, lv1_readsize);

i2c_err:
	kfree(read_lv1_buff);
malloc_err:
	mutex_unlock(&ts->device_mutex);
	lv1_readremain = 0;
	ts->sec_irq_enable(ts, true);

	return lv1_readsize;
}

static ssize_t sec_ts_gesture_status_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct sec_ts_data *ts = dev_get_drvdata(dev);

	mutex_lock(&ts->device_mutex);
	memcpy(buf, ts->gesture_status, sizeof(ts->gesture_status));
	LOGI("GESTURE STATUS %x %x %x %x %x %x\n",
		    ts->gesture_status[0], ts->gesture_status[1],
		    ts->gesture_status[2], ts->gesture_status[3],
		    ts->gesture_status[4], ts->gesture_status[5]);
	mutex_unlock(&ts->device_mutex);

	return sizeof(ts->gesture_status);
}

static ssize_t sec_ts_regreadsize_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct sec_ts_data *ts = dev_get_drvdata(dev);

	mutex_lock(&ts->device_mutex);

	lv1cmd = buf[0];
	lv1_readsize = ((unsigned int)buf[4] << 24) |
			((unsigned int)buf[3] << 16) |
			((unsigned int) buf[2] << 8) |
			((unsigned int)buf[1] << 0);
	lv1_readoffset = 0;
	lv1_readremain = 0;

	mutex_unlock(&ts->device_mutex);

	return size;
}

static ssize_t sec_ts_enter_recovery_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t size)
{
	struct sec_ts_data *ts = dev_get_drvdata(dev);
	struct sec_ts_plat_data *pdata = ts->plat_data;
	int ret;
	unsigned long on;
#if defined(I3C_INTERFACE)
	struct i3c_master_controller *i3cmaster;
#endif

	ret = kstrtoul(buf, 10, &on);
	if (ret != 0) {
		LOGE("failed to read: %d\n", ret);
		return -EINVAL;
	}

	if (on == 1) {
		ts->sec_irq_enable(ts, false);
		gpio_free(pdata->irq_gpio);

		LOGI("gpio free\n");
		if (gpio_is_valid(pdata->irq_gpio)) {
			ret = gpio_request_one(pdata->irq_gpio,
					    GPIOF_OUT_INIT_LOW, "sec,tsp_int");
			LOGI("gpio request one\n");
			if (ret < 0)
				LOGE("Unable to request tsp_int [%d]: %d\n", pdata->irq_gpio, ret);
		} else {
			LOGE("Failed to get irq gpio\n");
			return -EINVAL;
		}

		// pdata->power(ts, false);
		// sec_ts_delay(100);
		// pdata->power(ts, true);
		sec_ts_hw_reset(ts, false);
		sec_ts_delay(500);

#if defined(I3C_INTERFACE)
		sec_ts_delay(70);

		LOGE("power off & on and do i3c daa\n");
		i3cmaster = i3c_dev_get_master(ts->client->desc);
		i3c_master_do_daa(i3cmaster);
#endif
	} else {
		gpio_free(pdata->irq_gpio);

		if (gpio_is_valid(pdata->irq_gpio)) {
			ret = gpio_request_one(pdata->irq_gpio, GPIOF_IN,
						"sec,tsp_int");
			if (ret) {
				LOGE("Unable to request tsp_int [%d]\n", pdata->irq_gpio);
				return -EINVAL;
			}
		} else {
			LOGE("Failed to get irq gpio\n");
			return -EINVAL;
		}

		// pdata->power(ts, false);
		// sec_ts_delay(500);
		// pdata->power(ts, true);
		sec_ts_hw_reset(ts, false);
		sec_ts_delay(500);

#if defined(I3C_INTERFACE)
		sec_ts_delay(70);

		LOGE("power off & on and do i3c daa\n");
		i3cmaster = i3c_dev_get_master(ts->client->desc);
		i3c_master_do_daa(i3cmaster);
#endif

		/* AFE Calibration */
		/*
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_PANEL_CAL, NULL, 0);
		if (ret < 0)
			LOGE("fail to write AFE_CAL\n");

		sec_ts_delay(1000);
		*/
		ts->sec_irq_enable(ts, true);
	}

	sec_ts_read_information(ts);

	return size;
}

static inline ssize_t sec_ts_show_error(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	LOGE("read only function, %s\n", attr->attr.name);
	return -EPERM;
}

static inline ssize_t sec_ts_store_error(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	LOGE("write only function, %s\n", attr->attr.name);
	return -EPERM;
}

int sec_ts_raw_device_init(struct sec_ts_data *ts)
{
	int ret;

#ifdef CONFIG_SEC_SYSFS
	ts->dev = sec_device_create(ts, "sec_ts");
#else
	ts->dev = device_create(sec_class, NULL, 0, ts, "sec_ts");
#endif
	ret = IS_ERR(ts->dev);
	if (ret) {
		LOGE("fail - device_create\n");
		return ret;
	}

	ret = sysfs_create_group(&ts->dev->kobj, &cmd_attr_group);
	if (ret < 0) {
		LOGE("fail - sysfs_create_group\n");
		goto err_sysfs;
	}

	return ret;
err_sysfs:
	LOGE("fail\n");
	return ret;
}

void sec_ts_raw_device_exit(struct sec_ts_data *ts)
{
	sysfs_remove_group(&ts->dev->kobj, &cmd_attr_group);
#ifdef CONFIG_SEC_SYSFS
	sec_device_destroy(ts->dev->devt);
#else
	device_destroy(sec_class, 0);
#endif
}

