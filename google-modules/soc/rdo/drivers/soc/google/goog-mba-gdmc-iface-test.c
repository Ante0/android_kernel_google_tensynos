// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_mba_nq_xport.h>

#define GDMC_MSG_SIZE 4
#define SHIFT_TO_HIGH_NIBBLE 12
#define MAX_BUF_SIZE 60
#define PING_TEST_USAGE_STRING_MAX_LEN 400

struct gdmc_iface_test_info {
	struct device *dev;
	struct gdmc_iface *iface;
	struct dentry *debugfs_root;
	struct dentry *ping_test_dir;
	/*
	 * test result of most recent test
	 * 0: PASS
	 * 1: FAIL
	 */
	bool test_status;
};

struct gdmc_iface_test_info *gdmc_iface_test_gdata;

#define DEBUG_ATTRIBUTE(name, fn_read, fn_write) \
static const struct file_operations name = {    \
	.open   = simple_open,                  \
	.read   = fn_read,                      \
	.write  = fn_write,                     \
}

struct gdmc_ping_async_resp_ctx {
	struct completion *done;
	u32 ping_resp_buf[GDMC_MSG_SIZE];
};

struct gdmc_iface_test_thread_data {
	int thread_id;
	struct completion complete;
	int pass;
};

struct gdmc_iface_test_ping_config {
	bool use_async_comm;
	bool use_crit_chan;
	bool use_oneway_msg;
	u32 nr_iters;
	u32 nr_threads;
};

static struct gdmc_iface_test_ping_config ping_test_config = {
	.use_async_comm = true,
	.use_crit_chan = false,
	.use_oneway_msg = false,
	.nr_iters = 1000,
	.nr_threads = 1,
};

static inline void
gdmc_iface_test_init_thread_data(struct gdmc_iface_test_thread_data *thread_data,
				 int thread_id)
{
	thread_data->thread_id = thread_id;
	init_completion(&thread_data->complete);
}

static void gdmc_iface_test_ping_async_resp_cb(void *resp_msg, void *priv_data)
{
	struct gdmc_ping_async_resp_ctx *resp_ctx = priv_data;
	u32 *payload = resp_msg;
	int i;

	for (i = 0; i < GDMC_MSG_SIZE; i++)
		resp_ctx->ping_resp_buf[i] = goog_mba_nq_xport_get_data(payload + i);

	complete(resp_ctx->done);
}

/* TODO: b/372280559 - Cleanup the gdmc_iface_test_handle_* functions to remove dup code */
static int gdmc_iface_test_handle_async_req(u32 *payload, u32 *resp_data)
{
	struct gdmc_ping_async_resp_ctx resp_ctx;
	int ret;
	DECLARE_COMPLETION_ONSTACK(complete);

	resp_ctx.done = &complete;

	ret = gdmc_send_message_async(gdmc_iface_test_gdata->iface, payload,
				      gdmc_iface_test_ping_async_resp_cb, &resp_ctx);
	if (ret < 0) {
		dev_err(gdmc_iface_test_gdata->dev, "gdmc_send_message_async failed(ret:%d)\n",
			ret);
		return ret;
	}

	if (!ping_test_config.use_oneway_msg) {
		wait_for_completion(&complete);
		memcpy(resp_data, resp_ctx.ping_resp_buf, sizeof(*resp_data) * GDMC_MSG_SIZE);
	}

	return ret;
}

static int gdmc_iface_test_handle_sync_req(u32 *payload, u32 *resp_data)
{
	int ret, i;

	ret = gdmc_send_message(gdmc_iface_test_gdata->iface, payload);
	if (ret < 0) {
		dev_err(gdmc_iface_test_gdata->dev, "gdmc_send_message failed(ret:%d)\n", ret);
		return ret;
	}

	if (!ping_test_config.use_oneway_msg) {
		resp_data[0] = goog_mba_nq_xport_get_data(payload);

		for (i = 1; i < GDMC_MSG_SIZE; i++)
			resp_data[i] = payload[i];
	}

	return ret;
}

static int gdmc_iface_test_handle_critical_async_req(u32 *payload, u32 *resp_data)
{
	struct gdmc_ping_async_resp_ctx resp_ctx;
	int ret;
	DECLARE_COMPLETION_ONSTACK(complete);

	resp_ctx.done = &complete;

	ret = gdmc_send_message_critical_async(gdmc_iface_test_gdata->iface, payload,
					       gdmc_iface_test_ping_async_resp_cb, &resp_ctx);
	if (ret < 0) {
		dev_err(gdmc_iface_test_gdata->dev, "%s: failed(ret:%d)\n", __func__, ret);
		return ret;
	}

	if (!ping_test_config.use_oneway_msg) {
		wait_for_completion(&complete);
		memcpy(resp_data, resp_ctx.ping_resp_buf, sizeof(*resp_data) * GDMC_MSG_SIZE);
	}

	return ret;
}

static int gdmc_iface_test_handle_critical_sync_req(u32 *payload, u32 *resp_data)
{
	int ret, i;

	ret = gdmc_send_message_critical(gdmc_iface_test_gdata->iface, payload);
	if (ret < 0) {
		dev_err(gdmc_iface_test_gdata->dev, "%s: failed(ret:%d)\n", __func__, ret);
		return ret;
	}

	if (!ping_test_config.use_oneway_msg) {
		resp_data[0] = goog_mba_nq_xport_get_data(payload);

		for (i = 1; i < GDMC_MSG_SIZE; i++)
			resp_data[i] = payload[i];
	}

	return ret;
}

static void gdmc_iface_test_prepare_ping_data(u32 *payload, u32 ping_value, bool oneway)
{
	int i;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_PING);
	goog_mba_nq_xport_set_oneway(payload, oneway);
	goog_mba_nq_xport_set_data(payload, ping_value);

	for (i = 1; i < GDMC_MSG_SIZE; i++)
		payload[i] = ping_value;
}

static int gdmc_iface_test_validate_ping_resp(u32 ping_value, u32 *resp)
{
	int i;
	struct device *dev = gdmc_iface_test_gdata->dev;

	for (i = 0; i < GDMC_MSG_SIZE; i++) {
		if (ping_value + 1 != resp[i]) {
			dev_err(dev, "Test failed! Expected [0]: 0x%x [1]: 0x%x [2]:0x%x [3]: 0x%x",
				ping_value + 1, ping_value + 1, ping_value + 1, ping_value + 1);
			dev_err(dev, " Received: [0]: 0x%x [1]: 0x%x [2]:0x%x [3]: 0x%x\n",
				resp[0], resp[1], resp[2], resp[3]);
			return -EINVAL;
		}
	}

	return 0;
}

static int gdmc_iface_test_ping_core(int ping_count, int thread_id)
{
	struct device *dev = gdmc_iface_test_gdata->dev;
	u32 data[GDMC_MSG_SIZE] = {0};
	int ret;
	u32 gdmc_resp_data[GDMC_MSG_SIZE] = {0};
	u32 ping_count_curr = ping_count & GOOG_MBA_NQ_XPORT_DATA_MASK;

	gdmc_iface_test_prepare_ping_data(data, ping_count_curr, ping_test_config.use_oneway_msg);

	dev_dbg(dev, "%s: Sending ping: Payload [0]: 0x%x [1]: 0x%x [2]:0x%x [3]: 0x%x\n",
		__func__, data[0], data[1], data[2], data[3]);

	if (ping_test_config.use_async_comm) {
		if (ping_test_config.use_crit_chan)
			ret = gdmc_iface_test_handle_critical_async_req(data, gdmc_resp_data);
		else
			ret = gdmc_iface_test_handle_async_req(data, gdmc_resp_data);
	} else {
		if (ping_test_config.use_crit_chan)
			ret = gdmc_iface_test_handle_critical_sync_req(data, gdmc_resp_data);
		else
			ret = gdmc_iface_test_handle_sync_req(data, gdmc_resp_data);
	}

	if (ret < 0) {
		dev_err(dev, "thread_id %d: gdmc send message failed\n", thread_id);
		return ret;
	}

	if (!ping_test_config.use_oneway_msg)
		ret = gdmc_iface_test_validate_ping_resp(ping_count_curr, gdmc_resp_data);

	return ret;
}

static int gdmc_iface_test_gdmc_ping_thread(void *data)
{
	int i;
	struct gdmc_iface_test_thread_data *thread_data = data;
	struct device *dev = gdmc_iface_test_gdata->dev;

	dev_info(dev, "th#%d: start ping test for iter(%u)\n",
		 thread_data->thread_id, ping_test_config.nr_iters);

	for (i = 0; i < ping_test_config.nr_iters; i++) {
		/*
		 * The ping value (first argument for the below function) is a combination of
		 * the thread_id and iteration number. Since the data mask in the non-queue mode
		 * is <15:0>, using the high nibble to represent the thread_id.
		 */
		if (gdmc_iface_test_ping_core((thread_data->thread_id << SHIFT_TO_HIGH_NIBBLE) + i,
					      thread_data->thread_id) < 0)
			break;
	}

	thread_data->pass = (i == ping_test_config.nr_iters);

	complete(&thread_data->complete);

	return 0;
}

static int gdmc_iface_test_invoke_ping(struct device *dev)
{
	int i, nr_pass = 0;
	int nr_threads = ping_test_config.nr_threads;
	struct gdmc_iface_test_thread_data *thread_data;

	thread_data =
		devm_kcalloc(dev, nr_threads, sizeof(*thread_data), GFP_KERNEL);
	if (!thread_data)
		return -ENOMEM;

	for (i = 0; i < nr_threads; i++)
		gdmc_iface_test_init_thread_data(&thread_data[i], i);

	for (i = 0; i < nr_threads; i++)
		kthread_run(gdmc_iface_test_gdmc_ping_thread, &thread_data[i],
			    "gdmc_test_ping_th#%d", i);

	for (i = 0; i < nr_threads; i++) {
		wait_for_completion(&thread_data[i].complete);
		dev_info(dev, "thread#%d %s\n", i, thread_data[i].pass ? "Pass" : "Fail");
		if (thread_data[i].pass > 0)
			nr_pass++;
	}

	devm_kfree(dev, thread_data);

	return (nr_pass == ping_test_config.nr_threads);
}

static ssize_t debugfs_read_test_status(struct file *filp,
					char __user *user_buf, size_t count,
					loff_t *ppos)
{
	char buf[MAX_BUF_SIZE];

	int len = snprintf(buf, sizeof(count), "%s\n",
		  gdmc_iface_test_gdata->test_status ? "Pass" : "Fail");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t debugfs_write_ping_test(struct file *filp,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct gdmc_iface_test_info *info = (struct gdmc_iface_test_info *)filp->private_data;

	/* Reset the test status before invoking the test */
	gdmc_iface_test_gdata->test_status = false;

	gdmc_iface_test_gdata->test_status = gdmc_iface_test_invoke_ping(info->dev);

	return count;
}
DEBUG_ATTRIBUTE(test_fops_ping_test, debugfs_read_test_status, debugfs_write_ping_test);

static ssize_t process_user_input(const char __user *user_buf, size_t count, char *buf,
				  size_t buf_size)
{
	size_t len = min(count, buf_size - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	/* Trim newline character if present */
	if (len > 0 && buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
		len--;
	}

	return len;
}

static ssize_t debugfs_read_msg_type(struct file *filp, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char buf[MAX_BUF_SIZE]; // Buffer for msg_type string

	int len = snprintf(buf, sizeof(count), "%s\n",
			   ping_test_config.use_oneway_msg ? "oneway" : "request");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t debugfs_write_msg_type(struct file *file,
				      const char __user *user_buf, size_t count,
				      loff_t *ppos)
{
	char buf[MAX_BUF_SIZE];
	ssize_t len = process_user_input(user_buf, count, buf, sizeof(buf));

	if (len < 0)
		return len;

	// Convert string to enum (case-insensitive comparison)
	if (strncasecmp(buf, "oneway", len) == 0) {
		ping_test_config.use_oneway_msg = true;
	} else if (strncasecmp(buf, "request", len) == 0) {
		ping_test_config.use_oneway_msg = false;
	} else {
		dev_err(gdmc_iface_test_gdata->dev,
			"%s: Invalid msg_type: %s. Set msg_type: oneway or request.\n",
			__func__, buf);
		return -EINVAL;
	}

	return count;
}
DEBUG_ATTRIBUTE(test_fops_msg_type, debugfs_read_msg_type, debugfs_write_msg_type);

static ssize_t debugfs_read_comm_type(struct file *filp, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	char buf[MAX_BUF_SIZE]; // Buffer for comm_type string

	int len = snprintf(buf, sizeof(count), "%s\n",
			   ping_test_config.use_async_comm ? "async" : "sync");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t debugfs_write_comm_type(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	char buf[MAX_BUF_SIZE];
	ssize_t len = process_user_input(user_buf, count, buf, sizeof(buf));

	if (len < 0)
		return len;

	// Convert string to enum (case-insensitive comparison)
	if (strncasecmp(buf, "sync", len) == 0) {
		ping_test_config.use_async_comm = false;
	} else if (strncasecmp(buf, "async", len) == 0) {
		ping_test_config.use_async_comm = true;
	} else {
		dev_err(gdmc_iface_test_gdata->dev,
			"%s: Invalid comm_type: %s. Set comm_type: sync or async.\n",
			__func__, buf);
		return -EINVAL;
	}

	return count;
}
DEBUG_ATTRIBUTE(test_fops_comm_type, debugfs_read_comm_type, debugfs_write_comm_type);

static ssize_t debugfs_read_msg_prio(struct file *filp, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char buf[MAX_BUF_SIZE]; // Buffer for msg priority string

	int len = snprintf(buf, sizeof(count), "%s\n",
			   ping_test_config.use_crit_chan ? "critical" : "normal");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t debugfs_write_msg_prio(struct file *file,
				      const char __user *user_buf, size_t count,
				      loff_t *ppos)
{
	char buf[MAX_BUF_SIZE];
	ssize_t len = process_user_input(user_buf, count, buf, sizeof(buf));

	if (len < 0)
		return len;

	// Convert string to enum (case-insensitive comparison)
	if (strncasecmp(buf, "normal", len) == 0) {
		ping_test_config.use_crit_chan = false;
	} else if (strncasecmp(buf, "critical", len) == 0) {
		ping_test_config.use_crit_chan = true;
	} else {
		dev_err(gdmc_iface_test_gdata->dev,
			"%s: Invalid msg_prio: %s. Set msg_prio: normal or critical.\n",
			__func__, buf);
		return -EINVAL;
	}

	return count;
}
DEBUG_ATTRIBUTE(test_fops_msg_prio, debugfs_read_msg_prio, debugfs_write_msg_prio);

static ssize_t gdmc_iface_test_debugfs_read_usage(struct file *file,
						  char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	static const char *usage =
		"Usage:\necho 1 > <test_path>/trigger_test\n"
		"Set\n<msg_type>: oneway or request\n<channel_priority>: normal or critical\n"
		"<communication_type>: sync or async\n<nr_threads>: (Range: 1 to 50)\n"
		"<nr_iterations>: (Range: 1 to 2000)\n";

	int len = strnlen(usage, PING_TEST_USAGE_STRING_MAX_LEN);

	return simple_read_from_buffer(user_buf, count, ppos, usage, len);
}

static const struct file_operations gdmc_iface_test_usage_fops = {
	.open = simple_open,
	.read = gdmc_iface_test_debugfs_read_usage,
};

static int create_debugfs_file(struct gdmc_iface_test_info *info, const char *name,
			       struct dentry *parent, const struct file_operations *fops)
{
	struct dentry *dentry = debugfs_create_file(name, 0660, parent, info, fops);

	if (IS_ERR(dentry)) {
		dev_err(info->dev, "Failed to create %s file. Error: %ld\n", name, PTR_ERR(dentry));
		return PTR_ERR(dentry);
	}

	return 0;
}

static int gdmc_iface_test_debugfs_init(struct gdmc_iface_test_info *info)
{
	int ret = 0;

	if (!debugfs_initialized())
		return -ENODEV;

	info->debugfs_root = debugfs_create_dir(dev_name(info->dev), NULL);
	if (IS_ERR(info->debugfs_root)) {
		dev_err(info->dev, "Failed to create debugfs dir\n");
		ret = PTR_ERR(info->debugfs_root);
		goto err;
	}

	info->ping_test_dir = debugfs_create_dir("ping_test", info->debugfs_root);
	if (IS_ERR(info->ping_test_dir)) {
		ret = PTR_ERR(info->ping_test_dir);
		dev_err(info->dev, "Failed to create ping_test sub dir\n");
		goto err;
	}

	ret = create_debugfs_file(info, "test_usage", info->ping_test_dir,
				  &gdmc_iface_test_usage_fops);
	if (ret)
		goto err;

	ret = create_debugfs_file(info, "trigger_test", info->ping_test_dir, &test_fops_ping_test);
	if (ret)
		goto err;

	ret = create_debugfs_file(info, "msg_type", info->ping_test_dir, &test_fops_msg_type);
	if (ret)
		goto err;

	ret = create_debugfs_file(info, "comm_type", info->ping_test_dir, &test_fops_comm_type);
	if (ret)
		goto err;

	ret = create_debugfs_file(info, "msg_prio", info->ping_test_dir, &test_fops_msg_prio);
	if (ret)
		goto err;

	debugfs_create_u32("nr_threads", 0660, info->ping_test_dir, &ping_test_config.nr_threads);

	debugfs_create_u32("nr_iterations", 0660, info->ping_test_dir, &ping_test_config.nr_iters);

err:
	return ret;
}

static int gdmc_iface_test_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gdmc_iface_test_info *test_info;
	int ret;

	dev_dbg(dev, "Probing %s\n", __func__);
	test_info = devm_kzalloc(dev, sizeof(*test_info), GFP_KERNEL);
	if (!test_info)
		return -ENOMEM;
	platform_set_drvdata(pdev, test_info);

	gdmc_iface_test_gdata = test_info;
	test_info->dev = dev;
	test_info->iface = gdmc_iface_get(dev);
	if (IS_ERR(test_info->iface))
		return PTR_ERR(test_info->iface);

	test_info->test_status = false;
	ret = gdmc_iface_test_debugfs_init(test_info);
	if (ret)
		goto put_gdmc_iface;

	dev_dbg(dev, "Probing done for %s\n", __func__);

	return 0;

put_gdmc_iface:
	gdmc_iface_put(test_info->iface);
	return ret;
}

static void gdmc_iface_test_remove(struct platform_device *pdev)
{
	struct gdmc_iface_test_info *test_info = platform_get_drvdata(pdev);

	gdmc_iface_put(test_info->iface);
}

static const struct of_device_id gdmc_test_of_match_table[] = {
	{ .compatible = "google,mba-gdmc-iface-test" },
	{},
};
MODULE_DEVICE_TABLE(of, gdmc_test_of_match_table);

static struct platform_driver gdmc_iface_test_driver = {
	.probe = gdmc_iface_test_probe,
	.remove = gdmc_iface_test_remove,
	.driver = {
		.name = "google-mba-gdmc-iface-test",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(gdmc_test_of_match_table),
	},
};
module_platform_driver(gdmc_iface_test_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GDMC Interface Test");
MODULE_LICENSE("GPL");
