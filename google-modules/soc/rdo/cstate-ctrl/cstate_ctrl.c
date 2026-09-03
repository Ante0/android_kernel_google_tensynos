// SPDX-License-Identifier: GPL-2.0-only
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

struct cstate_ctrl_data {
	bool disabled;
};

static ssize_t cstate_disabled_read(struct file *filp, char __user *ubuf,
				    size_t size, loff_t *ppos)
{
	char kbuf[2] = { '0', '\n' };
	struct device *dev = filp->private_data;
	struct cstate_ctrl_data *cstate_ctrl_data = dev_get_drvdata(dev);

	if (*ppos > 0)
		return 0; /* EOF */

	if (cstate_ctrl_data->disabled)
		kbuf[0] = '1';

	if (copy_to_user(ubuf, &kbuf, 2))
		return -EFAULT;

	*ppos += 2;
	return 2;
}

static ssize_t cstate_disabled_write(struct file *filp, const char __user *ubuf,
				     size_t size, loff_t *ppos)
{
	struct device *dev = filp->private_data;
	struct cstate_ctrl_data *cstate_ctrl_data = dev_get_drvdata(dev);
	bool disable;

	if (kstrtobool_from_user(ubuf, size, &disable))
		return -EINVAL;

	if (disable && !cstate_ctrl_data->disabled) {
		cstate_ctrl_data->disabled = true;
		pm_runtime_get_sync(dev);
	} else if (!disable && cstate_ctrl_data->disabled) {
		cstate_ctrl_data->disabled = false;
		pm_runtime_put_sync(dev);
	}

	return size;
}

static const struct file_operations cstate_disabled_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = cstate_disabled_write,
	.read = cstate_disabled_read,
};

static int cpuidle_cstate_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev;
	struct device_node *pd_node;
	const char *debugfs_name;
	struct dentry *debugfs_parent;
	struct cstate_ctrl_data *data;

	pd_node = of_parse_phandle(pdev->dev.of_node, "power-domains", 0);
	if (!pd_node) {
		dev_err(&pdev->dev, "Missing power-domains property\n");
		return -ENODEV;
	}

	dev = genpd_dev_pm_attach_by_id(&pdev->dev, 0);
	if (!dev)
		return -ENODEV;

	/* Allocate memory for the device-specific data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	pm_runtime_irq_safe(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	if (of_property_read_bool(dev->of_node, "disable-on-boot")) {
		data->disabled = true;
		pm_runtime_get_sync(&pdev->dev);
	} else {
		data->disabled = false;
	}

	/* Store the device-specific data in the device structure */
	dev_set_drvdata(&pdev->dev, data);

	debugfs_parent = debugfs_lookup("cstate_ctrl", NULL);
	if (!debugfs_parent)
		debugfs_parent = debugfs_create_dir("cstate_ctrl", NULL);

	debugfs_name = of_get_property(dev->of_node, "debugfs-name", NULL);
	if (!debugfs_name)
		debugfs_name = pd_node->name;

	debugfs_create_file(debugfs_name, 0644, debugfs_parent, &pdev->dev,
			    &cstate_disabled_fops);

	return 0;
}

static const struct of_device_id cstate_ctrl_of_match_table[] = {
	{
		.compatible = "google,cstate-ctrl",
	},
	{},
};

static struct platform_driver cstate_ctrl_driver = {
	.probe = cpuidle_cstate_ctrl_probe,
	.driver = {
		.name = "cstate-ctrl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cstate_ctrl_of_match_table),
	},
};

static int __init cstate_ctrl_init_driver(void)
{
	return platform_driver_register(&cstate_ctrl_driver);
}
subsys_initcall_sync(cstate_ctrl_init_driver);

MODULE_AUTHOR("Henry Chow <henrychow@google.com>");
MODULE_DESCRIPTION("CPU cluster state control for testing");
MODULE_LICENSE("GPL");
