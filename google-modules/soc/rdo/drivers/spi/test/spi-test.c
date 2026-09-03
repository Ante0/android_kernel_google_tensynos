// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include <linux/uaccess.h>

#define DEBUGFS_DIR_NAME "spi_test"
#define DEBUGFS_FILE_NAME_USER "send_user"
#define DEBUGFS_FILE_NAME_RANDOM "send_random"

struct spi_test_data {
	struct dentry *debugfs_dir;
	struct dentry *debugfs_file_user;
	struct dentry *debugfs_file_random;
	struct spi_device *spi_device;
};

static ssize_t spi_debugfs_write_random(struct file *file, const char __user *buf, size_t count,
					loff_t *ppos)
{
	struct spi_test_data *data = file->private_data;
	char *kbuf_data;
	int ret;
	int random_count;

	ret = kstrtoint_from_user(buf, count, 0, &random_count);
	if (ret)
		return ret;

	kbuf_data = kmalloc(random_count, GFP_KERNEL);
	if (!kbuf_data)
		return -ENOMEM;

	get_random_bytes(kbuf_data, random_count);

	ret = spi_write(data->spi_device, kbuf_data, random_count);
	kfree(kbuf_data);

	return ret < 0 ? ret : count;
}

static const struct file_operations fops_random = {
	.write = spi_debugfs_write_random,
	.owner = THIS_MODULE,
};

static ssize_t spi_debugfs_write(struct file *file, const char __user *buf, size_t count,
				 loff_t *ppos)
{
	struct spi_test_data *data = file->private_data;
	char *kbuf;
	int ret;

	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, buf, count)) {
		kfree(kbuf);
		return -EFAULT;
	}

	ret = spi_write(data->spi_device, kbuf, count);
	kfree(kbuf);

	return ret < 0 ? ret : count;
}

static const struct file_operations fops = {
	.write = spi_debugfs_write,
	.owner = THIS_MODULE,
};

static int spi_test_probe(struct spi_device *spi)
{
	struct spi_test_data *data;

	data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->spi_device = spi;
	spi_set_drvdata(spi, data);

	data->debugfs_dir = debugfs_create_dir(DEBUGFS_DIR_NAME, NULL);
	if (IS_ERR(data->debugfs_dir))
		return PTR_ERR(data->debugfs_dir);

	data->debugfs_file_user = debugfs_create_file(DEBUGFS_FILE_NAME_USER, 0200,
						      data->debugfs_dir, data, &fops);
	if (IS_ERR(data->debugfs_file_user)) {
		debugfs_remove_recursive(data->debugfs_dir);
		return PTR_ERR(data->debugfs_file_user);
	}

	data->debugfs_file_random = debugfs_create_file(DEBUGFS_FILE_NAME_RANDOM, 0200,
							data->debugfs_dir, data, &fops_random);
	if (IS_ERR(data->debugfs_file_random)) {
		debugfs_remove_recursive(data->debugfs_dir);
		return PTR_ERR(data->debugfs_file_random);
	}

	return 0;
}

static void spi_test_remove(struct spi_device *spi)
{
	struct spi_test_data *data = spi_get_drvdata(spi);

	debugfs_remove_recursive(data->debugfs_dir);
}

static const struct of_device_id spi_test_dt_ids[] = {
	{ .compatible = "google,spi-test" },
	{ }
};
MODULE_DEVICE_TABLE(of, spi_test_dt_ids);

static struct spi_driver spi_test_driver = {
	.driver = {
		.name = "spi_test",
		.of_match_table = spi_test_dt_ids,
	},
	.probe = spi_test_probe,
	.remove = spi_test_remove,
};

module_spi_driver(spi_test_driver);

MODULE_AUTHOR("Ivan Zaitsev <zaitsev@google.com>");
MODULE_DESCRIPTION("SPI Test Driver");
MODULE_LICENSE("GPL");
