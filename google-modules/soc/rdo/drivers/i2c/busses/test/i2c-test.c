// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#define DEFAULT_INTERVAL_MS 1000
#define DEBUGFS_DIR_NAME "i2c_test"
#define DEBUGFS_FILE_NAME_REPEAT_INTERVAL "repeat_interval_ms"
#define DEBUGFS_FILE_NAME_REPEAT_ENEBLE "repeat_enable"

struct i2c_test_data {
	struct i2c_client *client;
	struct dentry *debugfs_dir;
	struct dentry *repeat_interval_file;
	struct dentry *repeat_enable_file;
	struct delayed_work i2c_work;
	unsigned int repeat_interval_ms;
	bool repeat_enabled;
};

static void i2c_test_work(struct work_struct *work)
{
	struct i2c_test_data *data = container_of(work, struct i2c_test_data, i2c_work.work);
	const u8 dummy_data = 0;
	int ret;

	if (!data->repeat_enabled)
		return;

	ret = i2c_master_send(data->client, &dummy_data, 1);
	if (ret < 0)
		dev_err(&data->client->dev, "i2c transaction failed: %d\n", ret);

	schedule_delayed_work(&data->i2c_work, msecs_to_jiffies(data->repeat_interval_ms));
}

static int repeat_interval_read(void *d, u64 *val)
{
	struct i2c_test_data *data = d;

	*val = data->repeat_interval_ms;
	return 0;
}

static int repeat_interval_write(void *d, u64 val)
{
	struct i2c_test_data *data = d;

	data->repeat_interval_ms = val;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(repeat_interval_fops, repeat_interval_read, repeat_interval_write,
			"%llu\n");

static int repeat_enable_write(void *d, u64 val)
{
	struct i2c_test_data *data = d;

	if (val && !data->repeat_enabled) {
		data->repeat_enabled = true;
		schedule_delayed_work(&data->i2c_work, msecs_to_jiffies(data->repeat_interval_ms));
	} else if (!val && data->repeat_enabled) {
		data->repeat_enabled = false;
		cancel_delayed_work_sync(&data->i2c_work);
	}
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(repeat_enable_fops, NULL, repeat_enable_write, "%llu\n");

static int i2c_test_probe(struct i2c_client *client)
{
	struct i2c_test_data *data;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(&client->dev, data);

	data->client = client;
	data->repeat_interval_ms = DEFAULT_INTERVAL_MS;
	INIT_DELAYED_WORK(&data->i2c_work, i2c_test_work);

	data->debugfs_dir = debugfs_create_dir(DEBUGFS_DIR_NAME, NULL);
	if (IS_ERR(data->debugfs_dir))
		return PTR_ERR(data->debugfs_dir);

	data->repeat_interval_file = debugfs_create_file(DEBUGFS_FILE_NAME_REPEAT_INTERVAL, 0600,
						  data->debugfs_dir, data,
						  &repeat_interval_fops);
	if (IS_ERR(data->repeat_interval_file)) {
		ret = PTR_ERR(data->repeat_interval_file);
		goto err;
	}

	data->repeat_enable_file = debugfs_create_file(DEBUGFS_FILE_NAME_REPEAT_ENEBLE, 0200,
						data->debugfs_dir, data,
						&repeat_enable_fops);
	if (IS_ERR(data->repeat_enable_file)) {
		ret = PTR_ERR(data->repeat_enable_file);
		goto err;
	}

	return 0;

err:
	debugfs_remove_recursive(data->debugfs_dir);
	return ret;
}

static void i2c_test_remove(struct i2c_client *client)
{
	struct i2c_test_data *data = dev_get_drvdata(&client->dev);

	cancel_delayed_work_sync(&data->i2c_work);
	debugfs_remove_recursive(data->debugfs_dir);
}

static const struct i2c_device_id i2c_test_id[] = {
	{ "i2c-test", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_test_id);

static const struct of_device_id i2c_test_of_match[] = {
	{ .compatible = "google,i2c-test", },
	{}
};
MODULE_DEVICE_TABLE(of, i2c_test_of_match);

static struct i2c_driver i2c_test_driver = {
	.probe = i2c_test_probe,
	.remove = i2c_test_remove,
	.id_table = i2c_test_id,
	.driver = {
		.name = "i2c-test",
		.owner	= THIS_MODULE,
		.of_match_table	= i2c_test_of_match,
	},
};

module_i2c_driver(i2c_test_driver);

MODULE_AUTHOR("Ivan Zaitsev <zaitsev@google.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C test driver");
