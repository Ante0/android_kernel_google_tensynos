// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/of_platform.h>

#include "irq-gia-lib.h"

#define GIA_TEST_TIMEOUT_MS 1000
#define MAX_INPUT_SIZE 20
#define DEFAULT_OUTPUT_SIZE 100
#define GIA_TEST_THREADED 0x1
#define GIA_TEST_ONESHOT 0x2

static struct dentry *gia_test_root_dir;

struct gia_test_intr_line_data {
	int irq_val;
	const char *irq_name;
	struct completion irq_received;
	struct dentry *debugfs_file;
	u64 num_triggers;
};

struct gia_test_data {
	struct dentry *device_debugfs_dir;
	struct gia_test_intr_line_data *line_data;
};

struct gia_test_irq_trigger_data {
	struct platform_device *pdev;
	int irq;
};

static struct platform_device *gia_test_get_gia_parent_pdev(struct platform_device *test_pdev);

static struct gia_test_intr_line_data *gia_test_get_line_data(struct platform_device *pdev, int irq)
{
	struct gia_test_data *data = platform_get_drvdata(pdev);
	int irq_count = platform_irq_count(pdev);

	/* could be improved with binary search (since we can sort) if perf hit noticed */

	for (int i = 0; i < irq_count; ++i) {
		if (data->line_data[i].irq_val == irq)
			return &data->line_data[i];
	}

	dev_err(&pdev->dev, "could not find line for irq %d\n", irq);

	return NULL;
}

static irqreturn_t gia_test_isr(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;

	/* TODO move this result storage to a function */
	gia_test_get_line_data(pdev, irq)->num_triggers += 1;
	gia_set_clear_trigger_reg(gia_test_get_gia_parent_pdev(pdev), irq_get_irq_data(irq)->hwirq,
				  false);

	complete(&(gia_test_get_line_data(pdev, irq)->irq_received));
	dev_dbg(&pdev->dev, "gia_test device interrupt %d received\n", irq);

	return IRQ_HANDLED;
}

static irqreturn_t gia_test_isr_nocomplete(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;

	gia_set_clear_trigger_reg(gia_test_get_gia_parent_pdev(pdev), irq_get_irq_data(irq)->hwirq,
				  false);
	dev_dbg(&pdev->dev, "gia_test device fast handler %d. Wait for thread function\n", irq);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t gia_test_threaded_isr(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;

	/* TODO move this result storage to a function */
	gia_test_get_line_data(pdev, irq)->num_triggers += 1;

	complete(&(gia_test_get_line_data(pdev, irq)->irq_received));
	dev_dbg(&pdev->dev, "gia_test device interrupt %d received in threaded handler\n", irq);

	return IRQ_HANDLED;
}

static struct platform_device *gia_test_get_gia_parent_pdev(struct platform_device *test_pdev)
{
	struct platform_device *gia_pdev = NULL;
	struct device_node *gia_dev_node =
		of_parse_phandle(test_pdev->dev.of_node, "interrupt-parent", 0);

	if (!gia_dev_node) {
		dev_err(&test_pdev->dev, "could not find gia interrupt-parent node\n");
		return NULL;
	}

	gia_pdev = of_find_device_by_node(gia_dev_node);

	return gia_pdev;
}

static ssize_t gia_test_print_result(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char *buf;
	int len, ret = 0;
	struct gia_test_irq_trigger_data *trigger_data;
	u64 num_triggers;

	trigger_data = file->private_data;
	if (!trigger_data) {
		pr_err("Couldn't find trigger data for file\n");
		return -EINVAL;
	}

	num_triggers = gia_test_get_line_data(trigger_data->pdev, trigger_data->irq)->num_triggers;

	buf = kmalloc(DEFAULT_OUTPUT_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len = snprintf(buf, DEFAULT_OUTPUT_SIZE, "%lld interrupts handled on %s hwirq line %lu\n",
		       num_triggers, trigger_data->pdev->name,
		       irq_get_irq_data(trigger_data->irq)->hwirq);

	if (*ppos == 0) {
		if (copy_to_user(user_buf, buf, len) != 0) {
			ret = -EFAULT;
		} else {
			*ppos = len;
			ret = len;
		}
	}

	kfree(buf);
	return ret;
}

static int gia_test_trigger_interrupt(struct gia_test_irq_trigger_data *data, u32 flags)
{
	struct gia_test_irq_trigger_data *trigger_data = data;
	int hwirq = irq_get_irq_data(trigger_data->irq)->hwirq;
	struct device *test_dev = &trigger_data->pdev->dev;
	struct completion *irq_received;
	int ret, request_irq_rc;
	bool got_pm_runtime = true;

	irq_received = &gia_test_get_line_data(trigger_data->pdev, trigger_data->irq)->irq_received;

	/* automatically manage power as this command is intended for ease of use */
	if (pm_runtime_get_sync(&trigger_data->pdev->dev))
		got_pm_runtime = false;

	/* try registering the interrupt. If we don't get it, a real device has claimed it */
	if (flags & GIA_TEST_THREADED && flags & GIA_TEST_ONESHOT)
		/* TODO: this is ugly find a better way to handle flags */
		request_irq_rc = devm_request_threaded_irq(test_dev, trigger_data->irq,
								 gia_test_isr_nocomplete,
								 gia_test_threaded_isr,
								 IRQF_SHARED | IRQF_ONESHOT,
								 "gia_test", trigger_data->pdev);
	else if (flags & GIA_TEST_THREADED)
		request_irq_rc = devm_request_threaded_irq(test_dev, trigger_data->irq,
								 gia_test_isr_nocomplete,
								 gia_test_threaded_isr,
								 IRQF_SHARED, "gia_test",
								 trigger_data->pdev);
	else
		request_irq_rc = devm_request_irq(test_dev, trigger_data->irq, gia_test_isr,
							IRQF_SHARED, "gia_test",
							trigger_data->pdev);

	if (request_irq_rc) {
		ret = -EACCES;
		dev_err(test_dev,
			 "Could not claim interrupt for test. Bailing out. hwirq: %d\n",
			 hwirq);
		goto cleanup;
	}

	/* TODO: add flag to let user choose to fire/not fire on unclaimed interrupts */
	reinit_completion(irq_received);

	ret = gia_set_clear_trigger_reg(gia_test_get_gia_parent_pdev(trigger_data->pdev),
					hwirq, true);
	if (ret) {
		dev_err(test_dev, "error %d triggering interrupt\n", ret);
		goto free_irq;
	}

	/* return from wait_for_completion is jiffies left until timeout. 0 indicates timeout */
	if (!wait_for_completion_timeout(irq_received,
					 msecs_to_jiffies(GIA_TEST_TIMEOUT_MS))) {
		dev_err(test_dev, "timed out waiting for completion\n");
		ret = -ETIMEDOUT;
	}

free_irq:
	devm_free_irq(test_dev, trigger_data->irq, trigger_data->pdev);
cleanup:
	if (got_pm_runtime)
		pm_runtime_put_sync(&trigger_data->pdev->dev);

	return ret;
}

static int gia_test_set_clear_itr(struct gia_test_irq_trigger_data *data, bool set)
{
	return gia_set_clear_trigger_reg(gia_test_get_gia_parent_pdev(data->pdev),
				 irq_get_irq_data(data->irq)->hwirq, set);
}

static int gia_test_request_irq(struct gia_test_irq_trigger_data *data, bool threaded,
				unsigned long flags)
{
	if (threaded)
		return devm_request_threaded_irq(&data->pdev->dev, data->irq,
					gia_test_isr_nocomplete, gia_test_threaded_isr,
					IRQF_SHARED | flags, "gia_test", data->pdev);
	else
		return devm_request_irq(&data->pdev->dev, data->irq, gia_test_isr,
					IRQF_SHARED | flags, "gia_test", data->pdev);
}

static void gia_test_free_irq(struct gia_test_irq_trigger_data *data)
{
	devm_free_irq(&data->pdev->dev, data->irq, data->pdev);
}

static void gia_test_reset(struct gia_test_irq_trigger_data *data)
{
	gia_test_get_line_data(data->pdev, data->irq)->num_triggers = 0;
}

static ssize_t gia_test_parse_input(struct file *file, const char __user *user_buf, size_t count,
				    loff_t *ppos)
{
	struct gia_test_irq_trigger_data *trigger_data;
	char *buf;
	int input_size = (count > MAX_INPUT_SIZE) ? count : MAX_INPUT_SIZE;
	int ret = 0;

	trigger_data = file->private_data;
	if (!trigger_data) {
		pr_err("Couldn't find trigger data for file\n");
		return -EINVAL;
	}

	buf = kmalloc(input_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count)) {
		kfree(buf);
		return -EFAULT;
	}

	/*
	 *  Warning: if using similar command names, make sure that the longer one is first
	 *  i.e. "trigger_threaded" will match "trigger" if it's compared first
	 *  TODO: Change this by using better string parsing
	 */
	if (strncmp(buf, "trigger_threaded", 16) == 0) {
		ret = gia_test_trigger_interrupt(trigger_data, GIA_TEST_THREADED);
	} else if (strncmp(buf, "trigger_oneshot", 15) == 0) {
		ret = gia_test_trigger_interrupt(trigger_data, GIA_TEST_ONESHOT|GIA_TEST_THREADED);
	} else if (strncmp(buf, "trigger", 7) == 0) {
		ret = gia_test_trigger_interrupt(trigger_data, 0);
	} else if (strncmp(buf, "set_itr", 7) == 0) {
		ret = gia_test_set_clear_itr(trigger_data, true);
	} else if (strncmp(buf, "clear_itr", 9) == 0) {
		ret = gia_test_set_clear_itr(trigger_data, false);
	} else if (strncmp(buf, "request_irq_threaded", 20) == 0) {
		ret = gia_test_request_irq(trigger_data, true, 0);
	} else if (strncmp(buf, "request_irq_oneshot", 19) == 0) {
		ret = gia_test_request_irq(trigger_data, true, IRQF_ONESHOT);
	} else if (strncmp(buf, "request_irq", 11) == 0) {
		ret = gia_test_request_irq(trigger_data, false, 0);
	} else if (strncmp(buf, "free_irq", 8) == 0) {
		gia_test_free_irq(trigger_data);
	} else if (strncmp(buf, "reset", 5) == 0) {
		gia_test_reset(trigger_data);
	} else if (strncmp(buf, "mask", 4) == 0) {
		disable_irq(trigger_data->irq);
	} else if (strncmp(buf, "unmask", 6) == 0) {
		enable_irq(trigger_data->irq);
	} else if (strncmp(buf, "enable_wake", 11) == 0) {
		ret = enable_irq_wake(trigger_data->irq);
	} else if (strncmp(buf, "disable_wake", 12) == 0) {
		ret = disable_irq_wake(trigger_data->irq);
	} else if (strncmp(buf, "power_on", 8) == 0) {
		ret = pm_runtime_get_sync(&trigger_data->pdev->dev);
	} else if (strncmp(buf, "power_off", 9) == 0) {
		pm_runtime_put_sync(&trigger_data->pdev->dev);
	} else {
		dev_err(&trigger_data->pdev->dev, "Unknown command\n");
		ret = -EINVAL;
	}

	if (ret)
		dev_err(&trigger_data->pdev->dev, "command %s failed %d\n", buf, ret);
	else
		ret = count;

	kfree(buf);
	return ret;
}

static int gia_test_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations gia_test_fops = {
	.open = gia_test_debugfs_open,
	.write = gia_test_parse_input,
	.read = gia_test_print_result,
};

static int gia_test_create_debugfs(struct platform_device *pdev)
{
	struct gia_test_data *data = platform_get_drvdata(pdev);
	int irq_count = platform_irq_count(pdev);

	/* make device subdir */
	data->device_debugfs_dir = debugfs_create_dir(dev_name(&pdev->dev), gia_test_root_dir);
	if (!data->device_debugfs_dir) {
		dev_err(&pdev->dev, "failed to create debugfs dir\n");
		return -ENOENT;
	}

	for (int i = 0; i < irq_count; ++i) {
		/* make per-interrupt file based on name in device tree */
		struct gia_test_irq_trigger_data *trigger_data =
			devm_kzalloc(&pdev->dev, sizeof(*trigger_data), GFP_KERNEL);
		if (!trigger_data)
			return -ENOMEM;

		trigger_data->irq = data->line_data[i].irq_val;
		trigger_data->pdev = pdev;

		/*
		 * Using file permissions 0660 for read/write access to user & group.
		 * This file is the primary interface for this driver. Callers may be automated
		 * so they must be able to write the file.
		 * TODO: check once automation is in place if this permission is sufficient
		 */
		data->line_data[i].debugfs_file = debugfs_create_file(data->line_data[i].irq_name,
								      0660,
								      data->device_debugfs_dir,
								      trigger_data,
								      &gia_test_fops);
		if (!data->line_data[i].debugfs_file) {
			dev_err(&pdev->dev, "failed to create debugfs file %s\n",
				data->line_data[i].irq_name);
			return -ENOENT;
		}
	}

	return 0;
}

static void gia_test_remove_debugfs(struct platform_device *pdev)
{
	struct gia_test_data *data = platform_get_drvdata(pdev);

	debugfs_remove_recursive(data->device_debugfs_dir);
	data->device_debugfs_dir = NULL;
}

static int gia_test_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gia_test_data *data;
	int irq_count, string_count, ret;
	const char **intr_names;

	irq_count = platform_irq_count(pdev);
	if (irq_count < 0) {
		dev_err(&pdev->dev, "platform_irq_count() failed (ret = %d). Possible cause: Disabled or unprobed interrupt parent\n",
			irq_count);
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	/* array allocation */
	data->line_data = devm_kzalloc(dev, sizeof(*data->line_data) * irq_count, GFP_KERNEL);
	if (!data->line_data)
		return -ENOMEM;

	/* parse strings */
	intr_names = devm_kzalloc(dev, sizeof(*intr_names) * irq_count, GFP_KERNEL);
	if (!intr_names)
		return -ENOMEM;

	string_count = of_property_read_string_array(pdev->dev.of_node, "interrupt-names",
						     intr_names, irq_count);

	if (string_count != irq_count) {
		dev_err(dev, "Mismatched number of IRQs and IRQ names in device tree\n");
		return -EINVAL;
	}

	/* fill structures for each IRQ line */
	for (int i = 0; i < irq_count; ++i) {
		data->line_data[i].irq_val = platform_get_irq(pdev, i);
		data->line_data[i].irq_name = intr_names[i];
		init_completion(&data->line_data[i].irq_received);
		/* init to non-success value */
		data->line_data[i].num_triggers = 0;
	}

	/* register with debugfs. we have one test node per GIA node */
	ret = gia_test_create_debugfs(pdev);
	if (ret) {
		dev_err(dev, "Error %d creating debugfs files\n", ret);
		return ret;
	}

	if (dev->pm_domain) {
		pm_runtime_set_active(dev);
		devm_pm_runtime_enable(dev);
	}

	return 0;
}

static void gia_test_remove(struct platform_device *pdev)
{
	gia_test_remove_debugfs(pdev);

	pm_runtime_set_suspended(&pdev->dev);
}

static int gia_test_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "gia_test device suspend\n");
	return 0;
}

static int gia_test_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "gia_test device resume\n");
	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(gia_test_dev_pm_ops,
				 gia_test_runtime_suspend,
				 gia_test_runtime_resume,
				 NULL);

static const struct of_device_id gia_test_of_match[] = {
	{ .compatible = "google,gia-test" },
	{ },
};
MODULE_DEVICE_TABLE(of, gia_test_of_match);

static struct platform_driver gia_test_driver = {
	.probe = gia_test_probe,
	.remove = gia_test_remove,
	.driver = {
		.name = "irq_gia_google_test",
		.owner = THIS_MODULE,
		.of_match_table = gia_test_of_match,
		.pm = &gia_test_dev_pm_ops,
	},
};

static int gia_test_init(void)
{
	gia_test_root_dir = debugfs_create_dir("gia_test", NULL);

	if (!gia_test_root_dir) {
		pr_err("Couldn't create debugfs dir gia_test\n");
		return -ENOMEM;
	}

	return platform_driver_register(&gia_test_driver);
}

static void gia_test_exit(void)
{
	debugfs_remove_recursive(gia_test_root_dir);
	gia_test_root_dir = NULL;
	platform_driver_unregister(&gia_test_driver);
}

module_init(gia_test_init);
module_exit(gia_test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google GIA test driver");
