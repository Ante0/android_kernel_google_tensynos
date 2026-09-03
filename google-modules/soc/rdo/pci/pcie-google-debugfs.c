// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022-2024 Google LLC
 */

#include <linux/pci-pwrctrl.h>
#include <linux/pcie_google_if.h>
#include "pcie-google.h"

struct google_pcie_debug {
	char base_dirname[8];
	struct dentry *base_dir;
};

struct google_pcie_debug_pwrctrl {
	struct pci_pwrctrl pwrctrl;
	struct device dev;
};

static inline struct pci_pwrctrl *to_pwrctrl(struct device *dev)
{
	struct google_pcie_debug_pwrctrl *debug_pwrctrl;

	debug_pwrctrl = container_of(dev, struct google_pcie_debug_pwrctrl, dev);

	return &debug_pwrctrl->pwrctrl;
}

#define GOOGLE_PWRCTRL_DEBUGFS_NAME "debug-pwrctrl"

static void gpcie_debug_pwrctrl_release(struct device *dev)
{
	struct google_pcie_debug_pwrctrl *debug_pwrctrl;

	debug_pwrctrl = container_of(dev, struct google_pcie_debug_pwrctrl, dev);

	kfree(debug_pwrctrl);
}

static struct pci_pwrctrl *gpcie_get_debug_pwrctrl(struct google_pcie *gpcie)
{
	struct pci_host_bridge *bridge = gpcie->pci->pp.bridge;
	struct google_pcie_debug_pwrctrl *debug_pwrctrl;
	struct device *dev;
	int ret;

	dev = device_find_child_by_name(&bridge->dev, GOOGLE_PWRCTRL_DEBUGFS_NAME);
	if (dev) {
		put_device(dev);
		return to_pwrctrl(dev);
	}

	debug_pwrctrl = kzalloc(sizeof(*debug_pwrctrl), GFP_KERNEL);
	if (!debug_pwrctrl)
		return NULL;

	dev = &debug_pwrctrl->dev;

	dev_set_name(dev, GOOGLE_PWRCTRL_DEBUGFS_NAME);
	dev->parent = &bridge->dev;
	dev->release = gpcie_debug_pwrctrl_release;

	ret = device_register(dev);
	if (ret) {
		dev_err(gpcie->dev, "Failed to register pwrctrl device: %d\n", ret);
		put_device(dev);
		return NULL;
	}

	pci_pwrctrl_init(&debug_pwrctrl->pwrctrl, dev);

	return &debug_pwrctrl->pwrctrl;
}

static int gpcie_set_power(void *data, u64 val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;
	struct pci_pwrctrl *pwrctrl = gpcie_get_debug_pwrctrl(gpcie);
	int ret;

	if (val == 1) {
		dev_dbg(gpcie->dev, "Request for power ON from debugfs\n");

		ret = google_pcie_rc_poweron(gpcie->domain);
		if (ret)
			return ret;

		if (!gpcie->power_ready && pwrctrl)
			return pci_pwrctrl_device_set_ready(pwrctrl);

		return 0;
	} else if (val == 0) {
		dev_dbg(gpcie->dev, "Request for power OFF from debugfs\n");

		if (gpcie->power_ready && pwrctrl)
			pci_pwrctrl_device_unset_ready(pwrctrl);

		return google_pcie_rc_poweroff(gpcie->domain);
	}

	dev_err(gpcie->dev, "Invalid value for debugfs power entry");
	return -EINVAL;
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_power, NULL, gpcie_set_power, "%llu\n");

static int gpcie_get_max_link_speed(void *data, u64 *val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	*val = gpcie->max_link_speed;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_max_link_speed,
			 gpcie_get_max_link_speed, NULL, "%llu\n");

static int gpcie_get_max_link_width(void *data, u64 *val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	*val = gpcie->max_link_width;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_max_link_width,
			 gpcie_get_max_link_width, NULL, "%llu\n");

static int gpcie_get_current_link_speed(void *data, u64 *val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	*val = gpcie->current_link_speed;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_current_link_speed,
			 gpcie_get_current_link_speed, NULL, "%llu\n");

static int gpcie_get_current_link_width(void *data, u64 *val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	*val = gpcie->current_link_width;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_current_link_width,
			 gpcie_get_current_link_width, NULL, "%llu\n");

static int gpcie_get_tgt_link_speed(void *data, u64 *val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	*val = gpcie->target_link_speed;
	return 0;
}

static int gpcie_set_tgt_link_speed(void *data, u64 val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	return google_pcie_rc_change_link_speed(gpcie->domain, (unsigned int)val);
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_tgt_link_speed,
			 gpcie_get_tgt_link_speed, gpcie_set_tgt_link_speed, "%llu\n");

static int gpcie_get_tgt_link_width(void *data, u64 *val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	*val = gpcie->target_link_width;
	return 0;
}

static int gpcie_set_tgt_link_width(void *data, u64 val)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;

	return google_pcie_rc_change_link_width(gpcie->domain, (unsigned int)val);
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_tgt_link_width,
			 gpcie_get_tgt_link_width, gpcie_set_tgt_link_width, "%llu\n");

static int gpcie_force_cpl_timeout(void *data, u64 val)
{
	struct google_pcie *gpcie = data;
	struct pci_bus *bus;
	u32 cfg;
	int counter, ret = 0;

	dev_info(gpcie->dev, "forcing cpl timeout\n");

	mutex_lock(&gpcie->link_lock);
	if (!gpcie->is_link_up) {
		dev_err(gpcie->dev, "link is down, skip cpl timeout\n");
		ret = -ENODEV;
		goto exit_cpl;
	}

	bus = pci_find_bus(gpcie->domain, 1);
	if (!bus) {
		dev_err(gpcie->dev, "cpl timeout bus not found\n");
		ret = -ENODEV;
		goto exit_cpl;
	}

	google_pcie_assert_perst_n(gpcie, true);

	for (counter = 0; counter < 100; counter++) {
		pci_bus_read_config_dword(bus, 0, 0, &cfg);

		if (cfg == 0xffffffff)
			break;
		usleep_range(10, 12);
	}

	if (counter == 100) {
		dev_err(gpcie->dev, "force cpl timeout failed\n");
		ret = -ETIMEDOUT;
	}

exit_cpl:
	mutex_unlock(&gpcie->link_lock);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_force_cpl_timeout, NULL,
			 gpcie_force_cpl_timeout, "%llu\n");

static int gpcie_force_link_down(void *data, u64 val)
{
	struct google_pcie *gpcie = data;

	dev_info(gpcie->dev, "forcing link down\n");

	mutex_lock(&gpcie->link_lock);
	if (!gpcie->is_link_up) {
		dev_err(gpcie->dev, "link is down, skip force link down\n");
		mutex_unlock(&gpcie->link_lock);
		return -ENODEV;
	}

	google_pcie_hot_reset(gpcie);

	mutex_unlock(&gpcie->link_lock);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_force_link_down, NULL,
			 gpcie_force_link_down, "%llu\n");

static int gpcie_force_trigger_coredump(void *data, u64 val)
{
	struct google_pcie *gpcie = data;

	dev_info(gpcie->dev, "forcing trigger devcoredump\n");

	google_pcie_create_devcoredump(gpcie);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(gpcie_fops_force_trigger_coredump, NULL,
			 gpcie_force_trigger_coredump, "%llu\n");

int google_pcie_init_debugfs(struct google_pcie *gpcie)
{
	struct google_pcie_debug *dbg;
	int domain = gpcie->domain;

	dbg = devm_kzalloc(gpcie->dev, sizeof(*dbg), GFP_KERNEL);
	if (!dbg)
		return -ENOMEM;

	snprintf(dbg->base_dirname, sizeof(dbg->base_dirname),
		 "pcie%d", domain);
	dbg->base_dir = debugfs_create_dir(dbg->base_dirname, NULL);
	debugfs_create_file("power", 0220, dbg->base_dir,
			    gpcie, &gpcie_fops_power);

	debugfs_create_file("max_link_speed", 0440, dbg->base_dir,
			    gpcie, &gpcie_fops_max_link_speed);
	debugfs_create_file("max_link_width", 0440, dbg->base_dir,
			    gpcie, &gpcie_fops_max_link_width);
	debugfs_create_file("current_link_speed", 0440, dbg->base_dir,
			    gpcie, &gpcie_fops_current_link_speed);
	debugfs_create_file("current_link_width", 0440, dbg->base_dir,
			    gpcie, &gpcie_fops_current_link_width);
	debugfs_create_file("target_link_speed", 0660, dbg->base_dir,
			    gpcie, &gpcie_fops_tgt_link_speed);
	debugfs_create_file("target_link_width", 0660, dbg->base_dir,
			    gpcie, &gpcie_fops_tgt_link_width);
	debugfs_create_file("force_cpl_timeout", 0220, dbg->base_dir,
			    gpcie, &gpcie_fops_force_cpl_timeout);
	debugfs_create_file("force_link_down", 0220, dbg->base_dir,
			    gpcie, &gpcie_fops_force_link_down);
	debugfs_create_file("force_trigger_coredump", 0220, dbg->base_dir,
			    gpcie, &gpcie_fops_force_trigger_coredump);

	dev_dbg(gpcie->dev, "Created debugfs files\n");
	gpcie->debugfs = dbg;
	return 0;
}

void google_pcie_exit_debugfs(void *data)
{
	struct google_pcie *gpcie = (struct google_pcie *)data;
	struct google_pcie_debug *dbg = (struct google_pcie_debug *)gpcie->debugfs;

	debugfs_remove_recursive(dbg->base_dir);
	dev_dbg(gpcie->dev, "Removed debugfs files\n");
}
