// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "h2omg.h"

#define REG_ID    0x95
#define ID_SLG_V2 0x0aef
#define ID_SLG_V3 0x0a9f

static ssize_t control_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	unsigned int vreg;
	int err;

	err = h2omg_control_get(info, &vreg);
	if (err)
		return err;

	return sysfs_emit(buf, "%#02x\n", vreg);
}

static ssize_t control_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	unsigned int vreg;
	int err;

	err = kstrtouint(buf, 0, &vreg);
	if (err)
		return err;

	if (vreg > 0xff)
		return -EINVAL;

	err = h2omg_control_set(info, vreg);
	if (err)
		return err;
	info->control_set = vreg;

	return count;
}
static DEVICE_ATTR_RW(control);

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	unsigned int vreg;
	int err;

	mutex_lock(&info->lock);
	err = h2omg_status_get(info, &vreg);
	mutex_unlock(&info->lock);
	if (err)
		return err;

	return sysfs_emit(buf, "%#02x\n", vreg);
}
static DEVICE_ATTR_RO(state);

static struct attribute *h2omg_registers_attributes[] = {
	&dev_attr_control.attr,
	&dev_attr_state.attr,
	NULL
};

static struct attribute_group h2omg_registers_attr_group = {
	.name = "registers",
	.attrs = h2omg_registers_attributes,
};

static ssize_t sensor_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf,
				  enum h2omg_sensor_id id)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	bool enable;
	int err;

	err = h2omg_sensor_enable_get(info, id, &enable);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", enable);
}

static ssize_t sensor_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count,
				   enum h2omg_sensor_id id)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	unsigned int enable;
	int err;

	err = kstrtouint(buf, 0, &enable);
	if (err)
		return err;

	err = info->ops->sensor_enable_set(info, id, !!enable);
	if (err)
		return err;

	return count;
}

static ssize_t sensor_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf,
				enum h2omg_sensor_id id)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	enum h2omg_detect_mode mode;
	int err;

	mutex_lock(&info->lock);
	err = h2omg_sensor_mode_get(info, id, &mode);
	mutex_unlock(&info->lock);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", mode);
}

static ssize_t sensor_mode_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count,
				 enum h2omg_sensor_id id)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	enum h2omg_detect_mode mode;
	int err;

	err = kstrtouint(buf, 0, &mode);
	if (err)
		return err;

	if (mode > DETECT_DRY)
		return -EINVAL;

	mutex_lock(&info->lock);
	err = h2omg_sensor_mode_set(info, id, mode);
	mutex_unlock(&info->lock);
	if (err)
		return err;

	return count;
}

static ssize_t sensor_boot_value_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf, enum h2omg_sensor_id id)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	enum h2omg_sensor_state sensor_state;

	mutex_lock(&info->lock);
	sensor_state = info->boot_state.sensors[id];
	mutex_unlock(&info->lock);

	return sysfs_emit(buf, "%u\n", sensor_state);
}

static ssize_t sensor_latched_value_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf, enum h2omg_sensor_id id)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	enum h2omg_sensor_state sensor_state;

	mutex_lock(&info->lock);
	sensor_state = info->latched_state.sensors[id];
	mutex_unlock(&info->lock);

	return sysfs_emit(buf, "%u\n", sensor_state);
}

static ssize_t sensor_acmp_show(struct device *dev,
				struct device_attribute *attr,
				char *buf, enum h2omg_sensor_id id)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	unsigned int acmp;
	int err;

	disable_irq(info->irq);
	mutex_lock(&info->lock);
	err = info->ops->sensor_acmp_get(info, id, &acmp);
	mutex_unlock(&info->lock);
	enable_irq(info->irq);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", acmp);
}

#define H2OMG_SENSOR_ATTR(_name, _id) \
	/* sensorX/boot_value */ \
	static ssize_t _name##_boot_value_show(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
	{ \
		return sensor_boot_value_show(dev, attr, buf, _id); \
	} \
	\
	static struct device_attribute dev_attr_##_name##_boot_value =  \
		__ATTR(boot_value, 0444, _name##_boot_value_show, NULL); \
	\
	/* sensorX/latched_value */ \
	static ssize_t _name##_value_show(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
	{ \
		return sensor_latched_value_show(dev, attr, buf, _id); \
	} \
	\
	static struct device_attribute dev_attr_##_name##_value =  \
		__ATTR(latched_value, 0444, _name##_value_show, NULL); \
	\
	/* sensorX/enable */ \
	static ssize_t _name##_enable_show(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
	{ \
		return sensor_enable_show(dev, attr, buf, _id); \
	} \
	\
	static ssize_t _name##_enable_store(struct device *dev, \
			     struct device_attribute *attr, \
			     const char *buf, size_t count) \
	{ \
		return sensor_enable_store(dev, attr, buf, count, _id); \
	} \
	\
	static struct device_attribute dev_attr_##_name##_enable = \
		__ATTR(enable, 0644, _name##_enable_show, \
		       _name##_enable_store); \
	\
	/* sensorX/mode */ \
	static ssize_t _name##_mode_show(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
	{ \
		return sensor_mode_show(dev, attr, buf, _id); \
	} \
	\
	static ssize_t _name##_mode_store(struct device *dev, \
			     struct device_attribute *attr, \
			     const char *buf, size_t count) \
	{ \
		return sensor_mode_store(dev, attr, buf, count, _id); \
	} \
	\
	static struct device_attribute dev_attr_##_name##_mode = \
		__ATTR(mode, 0644, _name##_mode_show, \
		       _name##_mode_store); \
	\
	/* sensorX/realtime_value */ \
	static ssize_t _name##_acmp_show(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
	{ \
		return sensor_acmp_show(dev, attr, buf, _id); \
	} \
	\
	static struct device_attribute dev_attr_##_name##_acmp =  \
		__ATTR(realtime_value, 0444, _name##_acmp_show, NULL); \
	\
	/* Create Group */ \
	static struct attribute *_name##_attributes[] = { \
		&dev_attr_##_name##_boot_value.attr, \
		&dev_attr_##_name##_value.attr, \
		&dev_attr_##_name##_enable.attr, \
		&dev_attr_##_name##_mode.attr, \
		&dev_attr_##_name##_acmp.attr, \
		NULL \
	}; \
	static struct attribute_group h2omg_##_name##_attr_group = { \
		.name = #_name, \
		.attrs = _name##_attributes,\
	}

H2OMG_SENSOR_ATTR(sensor0, SENSOR0);
H2OMG_SENSOR_ATTR(sensor1, SENSOR1);
H2OMG_SENSOR_ATTR(sensor2, SENSOR2);
H2OMG_SENSOR_ATTR(reference, SENSOR_REFERENCE);

struct device_attribute dev_attr_detect_enable =
	__ATTR(enable, 0644, reference_enable_show, reference_enable_store);

struct device_attribute dev_attr_detect_mode =
	__ATTR(mode, 0644, reference_mode_show, reference_mode_store);

static struct attribute *h2omg_detect_attributes[] = {
	&dev_attr_detect_mode.attr,
	&dev_attr_detect_enable.attr,
	NULL
};

static struct attribute_group h2omg_detect_attr_group = {
	.name = "detect",
	.attrs = h2omg_detect_attributes,
};

static ssize_t fuse_status_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	enum h2omg_fuse_state state;

	mutex_lock(&info->lock);
	h2omg_fuse_state_get(info, &state);
	mutex_unlock(&info->lock);
	return sysfs_emit(buf, "%u\n", state);
}

static struct device_attribute dev_attr_fuse_status =
	__ATTR(status, 0444, fuse_status_show, NULL);

static ssize_t fuse_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	bool enable;
	int err;

	err = h2omg_fuse_enable_get(info, &enable);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", enable);
}

static ssize_t fuse_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct h2omg_info *info = dev_get_drvdata(dev);
	unsigned int enable;
	int err;

	err = kstrtouint(buf, 0, &enable);
	if (err)
		return err;

	err = h2omg_fuse_enable_set(info, !!enable);
	if (err)
		return err;

	return count;
}

static struct device_attribute dev_attr_fuse_enable =
	__ATTR(enable, 0644, fuse_enable_show, fuse_enable_store);

static struct attribute *h2omg_fuse_attributes[] = {
	&dev_attr_fuse_enable.attr,
	&dev_attr_fuse_status.attr,
	NULL
};

static struct attribute_group h2omg_fuse_attr_group = {
	.name = "fuse",
	.attrs = h2omg_fuse_attributes,
};

static int h2omg_read_id(struct i2c_client *i2c)
{
	struct i2c_msg xfer[2];
	u8 reg = REG_ID;
	u16 id;
	int ret;

	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;

	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 2;
	xfer[1].buf = (u8 *)&id;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret == 2)
		return id;
	return -EIO;
}

static int h2omg_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct h2omg_info *info;
	struct gpio_desc *gpiod_irq;
	int (*revision_init)(struct i2c_client *client);
	int revision_id;
	int err;

	revision_id = h2omg_read_id(client);
	if (revision_id < 0)
		return -ENODEV;

	if (revision_id == ID_SLG_V2)
		revision_init = h2omg_slg_v2_init;
	else if (revision_id == ID_SLG_V3)
		revision_init = h2omg_slg_v3_init;
	else
		revision_init = h2omg_atl_v1_init;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = dev;
	i2c_set_clientdata(client, info);

	info->vref_div = of_property_read_bool(dev->of_node, "vref_div");

	err = revision_init(client);
	if (err) {
		dev_err(dev, "Unable to initialize chip (%d)\n", err);
		return err;
	}

	h2omg_timer_init(info);

	if (info->dual_trigger && info->trigger_delay_ms >= 10)
		dev_info(dev, "Fuse blows when both zones are wet or one zone is wet for > %u ms\n",
			 info->trigger_delay_ms);
	else if (info->dual_trigger)
		dev_info(dev, "trigger fuse when both zones are wet\n");
	else
		dev_info(dev, "trigger fuse when one zones is wet\n");

	gpiod_irq = devm_gpiod_get_optional(&client->dev,
					    "h2omg_irq", GPIOD_ASIS);
	if (IS_ERR(gpiod_irq)) {
		err = PTR_ERR(gpiod_irq);
		dev_err(dev, "GPIO not found (%d)\n", err);
		return err;
	}
	info->irq = gpiod_to_irq(gpiod_irq);
	if (info->irq < 0) {
		err = info->irq;
		dev_err(dev, "IRQ %d not found\n", info->irq);
		return err;
	}
	err = devm_request_threaded_irq(info->dev, info->irq, NULL,
					info->ops->irq_handler,
					(IRQF_TRIGGER_FALLING | IRQF_ONESHOT),
					"h2omg_irq",
					info);
	if (err) {
		dev_err(dev, "Failed to create IRQ handler (%d)\n", err);
		return err;
	}

	err = sysfs_create_group(&dev->kobj, &h2omg_detect_attr_group);
	if (err)
		dev_warn(dev, "Failed to create sysfs detect group (%d)\n", err);
	err = sysfs_create_group(&dev->kobj, &h2omg_registers_attr_group);
	if (err)
		dev_warn(dev, "Failed to create sysfs registers group (%d)\n", err);
	err = sysfs_create_group(&dev->kobj, &h2omg_sensor0_attr_group);
	if (err)
		dev_warn(dev, "Failed to create sysfs sensor0 group (%d)\n", err);
	err = sysfs_create_group(&dev->kobj, &h2omg_sensor1_attr_group);
	if (err)
		dev_warn(dev, "Failed to create sysfs sensor1 group (%d)\n", err);
	if (h2omg_sensor_count_get(info) >= 3) {
		err = sysfs_create_group(&dev->kobj, &h2omg_sensor2_attr_group);
		if (err)
			dev_warn(dev, "Failed to create sysfs sensor2 group (%d)\n", err);
	}
	err = sysfs_create_group(&dev->kobj, &h2omg_reference_attr_group);
	if (err)
		dev_warn(dev, "Failed to create sysfs reference group (%d)\n", err);
	err = sysfs_create_group(&dev->kobj, &h2omg_fuse_attr_group);
	if (err)
		dev_warn(dev, "Failed to create sysfs fuse group (%d)\n", err);

	return err;
}

static void h2omg_remove(struct i2c_client *client)
{
	struct h2omg_info *info = i2c_get_clientdata(client);

	if (info->ops && info->ops->cleanup)
		info->ops->cleanup(info);

	h2omg_timer_cleanup(info);
	sysfs_remove_group(&info->dev->kobj, &h2omg_detect_attr_group);
	sysfs_remove_group(&info->dev->kobj, &h2omg_registers_attr_group);
	sysfs_remove_group(&info->dev->kobj, &h2omg_sensor0_attr_group);
	sysfs_remove_group(&info->dev->kobj, &h2omg_sensor1_attr_group);
	sysfs_remove_group(&info->dev->kobj, &h2omg_sensor2_attr_group);
	sysfs_remove_group(&info->dev->kobj, &h2omg_reference_attr_group);
	sysfs_remove_group(&info->dev->kobj, &h2omg_fuse_attr_group);
	mutex_destroy(&info->lock);
}

static const struct i2c_device_id h2omg_id[] = {
	{"h2omg", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, h2omg_id);

static const struct of_device_id h2omg_of_match_table[] = {
	{ .compatible = "google,h2omg" },
	{},
};
MODULE_DEVICE_TABLE(of, h2omg_of_match_table);

static struct i2c_driver h2omg_driver = {
	.driver = {
		.name = "h2omg",
		.owner = THIS_MODULE,
		.of_match_table = h2omg_of_match_table,
	},
	.id_table = h2omg_id,
	.probe = h2omg_probe,
	.remove = h2omg_remove,
};

module_i2c_driver(h2omg_driver);
MODULE_DESCRIPTION("Google H2OMG Driver");
MODULE_AUTHOR("Jim Wylder <jwylder@google.com>");
MODULE_LICENSE("GPL");
