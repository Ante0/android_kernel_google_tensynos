// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM Network Sysfs
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file provides the sysfs interface for the LVM common network layer.
 * It allows userspace to interact with the LVM framework and driver, such as
 * checking the current state, triggering state transitions, and initiating
 * data transfers.
 */

#include <core/print.h>
#include <net/sysfs.h>
#include <net/pktgen.h>

extern struct lvm_net *net;
extern const char * const lvm_state_str[];

/**
 * lvm_sysfs_state_show - Show the current state of framework and driver
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer to store the output string
 *
 * This function is called when the "state" sysfs attribute is read.
 * It retrieves the current state of both the LVM framework and the
 * registered driver.
 *
 * Return: Number of bytes written to the buffer, or negative error code.
 */
static ssize_t lvm_sysfs_state_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct lvm_platform_driver *driver = net->driver;

	if (net->state >= __LVM_STATE_MAX)
		return sprintf(buf, "Unknown state\n");

	return sprintf(buf, "LVM state: %s\nDriver state: %s\n",
		       lvm_state_str[net->state],
		       driver->ops->get_state(driver));
}

/**
 * lvm_sysfs_switch_store - Toggle the driver functionality
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer containing the input value
 * @count: Length of the input buffer
 *
 * This function is called when the "switch" sysfs attribute is written.
 * It enables/disables the driver functionality on the fly.
 *
 * Return: Number of bytes processed from the input buffer, or negative
 *         error code.
 */
static ssize_t lvm_sysfs_switch_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct lvm_platform_driver *driver = net->driver;
	int new_state;

	if (sscanf(buf, "%d", &new_state) != 1)
		return -EINVAL;

	if (net->state != LVM_STATE_FRAMEWORK_READY ||
	    driver->ops->switch_state(driver, new_state))
		return -EINVAL;

	return count;
}

/**
 * lvm_sysfs_init_store - Initialize the LVM platform driver
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer containing the input value
 * @count: Length of the input buffer
 *
 * This function is called when the "init" sysfs attribute is written.
 * It triggers the probing of the LVM platform driver and transits the
 * LVM state machine to "Driver Ready".
 *
 * Return: Number of bytes processed from the input buffer, or negative
 *         error code.
 */
static ssize_t lvm_sysfs_init_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct lvm_platform_driver *driver = net->driver;
	int val, ret;

	if (sscanf(buf, "%d", &val) != 1 || val != 1)
		return -EINVAL;

	if (net->state != LVM_STATE_FRAMEWORK_READY)
		return -EINVAL;

	ret = lvm_platform_probe(driver);
	if (ret) {
		LVM_ERR("LVM platform driver probing failed: %d\n", ret);
		return -EINVAL;
	}

	lvm_state_transit(LVM_STATE_DRIVER_READY);

	return count;
}

/**
 * lvm_sysfs_exit_store - Deinitialize the LVM platform driver
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer containing the input value
 * @count: Length of the input buffer
 *
 * This function is called when the "exit" sysfs attribute is written.
 * It triggers the removal of the LVM platform driver and transits the
 * LVM state machine back to "Framework Ready".
 *
 * Return: Number of bytes processed from the input buffer, or negative
 *         error code.
 */
static ssize_t lvm_sysfs_exit_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct lvm_platform_driver *driver = net->driver;
	int val, ret;

	if (sscanf(buf, "%d", &val) != 1 || val != 1)
		return -EINVAL;

	if (net->state != LVM_STATE_DRIVER_READY)
		return -EINVAL;

	ret = lvm_platform_remove(driver);
	if (ret) {
		LVM_ERR("LVM platform driver removal failed: %d\n", ret);
		return -EINVAL;
	}

	lvm_state_transit(LVM_STATE_FRAMEWORK_READY);

	return count;
}

/**
 * lvm_sysfs_start_store - Start the LVM network device
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer containing the input value
 * @count: Length of the input buffer
 *
 * This function is called when the "start" sysfs attribute is written.
 * It triggers the opening of the LVM network device and transits the
 * LVM state machine to "Interface Running".
 *
 * Return: Number of bytes processed from the input buffer, or negative
 *         error code.
 */
static ssize_t lvm_sysfs_start_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct lvm_platform_driver *driver = net->driver;
	int val, ret;

	if (sscanf(buf, "%d", &val) != 1 || val != 1)
		return -EINVAL;

	if (net->state != LVM_STATE_DRIVER_READY)
		return -EINVAL;

	ret = lvm_netdev_open(driver);
	if (ret)
		return -EINVAL;

	lvm_state_transit(LVM_STATE_INTERFACE_RUNNING);

	return count;
}

/**
 * lvm_sysfs_stop_store - Stop the LVM network device
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer containing the input value
 * @count: Length of the input buffer
 *
 * This function is called when the "stop" sysfs attribute is written.
 * It triggers the stopping of the LVM network device and transits the
 * LVM state machine back to "Driver Ready".
 *
 * Return: Number of bytes processed from the input buffer, or negative
 *         error code.
 */
static ssize_t lvm_sysfs_stop_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct lvm_platform_driver *driver = net->driver;
	int val, ret;

	if (sscanf(buf, "%d", &val) != 1 || val != 1)
		return -EINVAL;

	if (net->state != LVM_STATE_INTERFACE_RUNNING)
		return -EINVAL;

	ret = lvm_netdev_stop(driver);
	if (ret)
		return -EINVAL;

	lvm_state_transit(LVM_STATE_DRIVER_READY);

	return count;
}

/**
 * lvm_sysfs_tx_store - Trigger packet transmission
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer containing the input value
 * @count: Length of the input buffer
 *
 * This function is called when the "tx" sysfs attribute is written.
 * It initiates the transmission of a packet through the LVM network
 * device. The actual packet transmission logic is handled by the
 * lvm_netdev_start_xmit() function.
 *
 * Return: Number of bytes processed from the input buffer, or negative
 *         error code.
 */
static ssize_t lvm_sysfs_tx_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct lvm_platform_driver *driver = net->driver;
	int val;

	if (sscanf(buf, "%d", &val) != 1 || val != 1)
		return -EINVAL;

	if (net->state != LVM_STATE_INTERFACE_RUNNING)
		return -EINVAL;

	lvm_netdev_start_xmit(driver);

	return count;
}

/**
 * lvm_sysfs_reg_show - Show the registers of the platform driver
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer to store the output string
 *
 * This function is called when the "reg" sysfs attribute is read.
 * It dumps the WLAN hardware registers to the provided buffer.
 *
 * Return: Number of bytes written to the buffer, or negative error code.
 */
static ssize_t lvm_sysfs_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (net->state != LVM_STATE_INTERFACE_RUNNING)
		return -EINVAL;

	return net->driver->ops->dump_reg(net->driver, buf);
}

/**
 * lvm_sysfs_filepath_show - Show the file path of the packet generator
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer to store the output string
 *
 * This function is called when the "filepath" sysfs attribute is read.
 * It retrieves the file path stored in the lvm_pktgen structure and
 * prints it to the buffer. It also displays the file size and the raw
 * content of the file.
 *
 * Return: Number of bytes written to the buffer, or negative error code.
 */
static ssize_t lvm_sysfs_filepath_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct lvm_pktgen *pktgen = net->pktgen;
	int offset = 0;

	if (!pktgen || !pktgen->filepath)
		return sprintf(buf, "Unknown captured file path\n");

	offset += sprintf(buf + offset, "File path: %s\n", pktgen->filepath);
	offset += sprintf(buf + offset, "File size: %d bytes\n", pktgen->len);
	offset += lvm_pktgen_raw_dump(pktgen, buf + offset);

	return offset;
}

/**
 * lvm_sysfs_filepath_store - Store the file path for the packet generator
 * @dev: Device for which the attribute is being accessed
 * @attr: Device attribute being accessed
 * @buf: Buffer containing the input value
 * @count: Length of the input buffer
 *
 * This function is called when the "filepath" sysfs attribute is written.
 * It stores the file path provided as input to the lvm_pktgen structure.
 * It also reads the content of the file and stores it in the pktgen->raw
 * buffer.
 *
 * Return: Number of bytes processed from the input buffer, or negative
 *         error code.
 */
static ssize_t lvm_sysfs_filepath_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct lvm_pktgen *pktgen = net->pktgen;
	size_t len = strnlen(buf, count - 1);
	int ret = 0;

	if (!pktgen)
		return -EINVAL;

	if (pktgen->filepath) {
		kfree(pktgen->raw);
		kfree(pktgen->filepath);
	}

	pktgen->filepath = kmalloc(len + 1, GFP_KERNEL);
	if (!pktgen->filepath)
		return -ENOMEM;

	memcpy(pktgen->filepath, buf, len);
	pktgen->filepath[len] = '\0';

	ret = lvm_pktgen_file_read(pktgen);
	if (ret)
		return -EINVAL;

	return count;
}

DEVICE_ATTR(state, 0644, lvm_sysfs_state_show, NULL);
DEVICE_ATTR(switch, 0644, NULL, lvm_sysfs_switch_store);
DEVICE_ATTR(init, 0644, NULL, lvm_sysfs_init_store);
DEVICE_ATTR(exit, 0644, NULL, lvm_sysfs_exit_store);
DEVICE_ATTR(start, 0644, NULL, lvm_sysfs_start_store);
DEVICE_ATTR(stop, 0644, NULL, lvm_sysfs_stop_store);
DEVICE_ATTR(tx, 0644, NULL, lvm_sysfs_tx_store);
DEVICE_ATTR(reg, 0644, lvm_sysfs_reg_show, NULL);
DEVICE_ATTR(filepath, 0644, lvm_sysfs_filepath_show, lvm_sysfs_filepath_store);

static struct attribute *lvm_attrs[] = {
	&dev_attr_state.attr,	 &dev_attr_switch.attr,
	&dev_attr_init.attr,	 &dev_attr_exit.attr,
	&dev_attr_start.attr,	 &dev_attr_stop.attr,
	&dev_attr_tx.attr,	 &dev_attr_reg.attr,
	&dev_attr_filepath.attr, NULL,
};

static struct attribute_group lvm_attr_group = {
	.attrs = lvm_attrs,
};

/**
 * lvm_net_sysfs_init - Initialize LVM sysfs interface
 * @net: Pointer to the global LVM network structure
 *
 * This function initializes the sysfs interface for the LVM network
 * framework. It creates a sysfs group with various attributes that
 * expose the functionality of the LVM framework and driver to userspace.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_net_sysfs_init(struct lvm_net *net)
{
	int ret = 0;

	if (!net || !net->driver)
		return -EINVAL;

	ret = sysfs_create_group(net->driver->kobj, &lvm_attr_group);
	if (ret)
		return -EINVAL;

	return 0;
}

/**
 * lvm_net_sysfs_deinit - Deinitialize LVM sysfs interface
 * @net: Pointer to the global LVM network structure
 *
 * This function deinitializes the sysfs interface for the LVM network
 * framework. It removes the sysfs group created during initialization
 * stage.
 */
void lvm_net_sysfs_deinit(struct lvm_net *net)
{
	if (!net || !net->driver)
		return;

	sysfs_remove_group(net->driver->kobj, &lvm_attr_group);
}
