// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google MailBox Array (MBA) CPM Interface Test Client
 *
 * Copyright (c) 2024 Google LLC
 */
#include <linux/dcache.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>

#include <soc/google/goog_mba_cpm_iface.h>
#include <soc/google/google_gtc.h>

#define CPM_IFACE_TEST_CLIENT_SRC_ID	0xAB
#define CPM_COMMON_PING_SERVICE		0
#define NO_TIMEOUT	0
#define PING_TEST_MAX_NUM_THREADS 100
#define PING_TEST_MAX_NUM_ITER 5000
#define PING_TEST_MAX_TIMEOUT 1000
#define PING_TEST_USAGE_STRING_MAX_LEN 400
#define EBUSY_DELAY_TIME_IN_MS 100

struct cpm_iface_test_client {
	struct device *dev;
	struct cpm_iface_client *cpm_client;
	struct dentry *debugfs_root;
};

struct cpm_iface_test_client *client_data;

struct cpm_iface_test_ping_config {
	bool msg_type;
	u32 nr_threads;
	u32 nr_iters;
	u32 tout_ms;
};

static struct cpm_iface_test_ping_config ping_test_config = {
	.msg_type = REQUEST_MSG,
	.nr_threads = 1,
	.nr_iters = 100,
	.tout_ms = NO_TIMEOUT,
};

struct cpm_iface_test_timestamp_config {
	bool is_oneway_msg;
	u32 nr_iters;
	u32 tout_ms;
} timestamp_test_config = {false, 1, NO_TIMEOUT};

struct cpm_iface_test_thread_data {
	int thread_id;
	struct completion complete;
	int pass;
	struct cpm_iface_test_ping_config *config;
	int ebusy_count;
};

union ping_value {
	struct ping_encode {
		u16 tid;
		u16 data;
	} args;
	u32 value;
};

static void
cpm_iface_test_init_thread_data(struct cpm_iface_test_thread_data *thread_data,
				int thread_id,
				struct cpm_iface_test_ping_config *config)
{
	thread_data->thread_id = thread_id;
	thread_data->pass = 0;
	thread_data->config = config;
	thread_data->ebusy_count = 0;
	init_completion(&thread_data->complete);
}

static int cpm_iface_test_ping(u32 ping_value, enum cpm_iface_msg_type msg_type,
			       unsigned long tout_ms, struct cpm_iface_event_timestamps *timestamps)
{
	struct device *dev = client_data->dev;
	int ret;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req cpm_iface_req = {
		.msg_type = msg_type,
		.req_msg = &req_msg,
		.resp_msg = &resp_msg,
		.tout_ms = tout_ms,
		.dst_id = CPM_COMMON_PING_SERVICE,
		.timestamps = timestamps,
	};

	req_msg.payload[0] = ping_value;
	req_msg.payload[1] = ping_value;
	req_msg.payload[2] = ping_value;

	ret = cpm_send_message(client_data->cpm_client, &cpm_iface_req);
	if ((ret == 0) && (msg_type == REQUEST_MSG)) {
		if (resp_msg.payload[0] != (req_msg.payload[0] + 1)) {
			dev_dbg(dev, "Wrong response for payload[0] expected 0x%x received 0x%x\n)",
				req_msg.payload[0] + 1, resp_msg.payload[0]);
			ret = -EBADMSG;
		}
		if (resp_msg.payload[1] != (req_msg.payload[1] + 1)) {
			dev_dbg(dev, "Wrong response for payload[1] expected 0x%x received 0x%x\n)",
				req_msg.payload[1] + 1, resp_msg.payload[1]);
			ret = -EBADMSG;
		}
		if (resp_msg.payload[2] != (req_msg.payload[2] + 1)) {
			dev_dbg(dev, "Wrong response for payload[2] expected 0x%x received 0x%x\n)",
				req_msg.payload[2] + 1, resp_msg.payload[2]);
			ret = -EBADMSG;
		}
	}

	return ret;
}

static int cpm_iface_test_ping_thread(void *data)
{
	struct device *dev = client_data->dev;
	struct cpm_iface_test_thread_data *thread_data = data;
	struct cpm_iface_test_ping_config *config = thread_data->config;
	int thread_id = thread_data->thread_id;
	union ping_value ping_val;
	int iter = 0;
	int ret;

	dev_info(dev, "[t%d]: ping test start (nr_iters:%u)\n", thread_id, config->nr_iters);
	while (iter < config->nr_iters) {
		ping_val.args.tid = thread_data->thread_id;
		ping_val.args.data = iter;

		dev_dbg(dev, "[t%d]: send ping start (iter:%d, val:[%#x] [%#x] [%#x])", thread_id,
			 iter, ping_val.value, ping_val.value, ping_val.value);

		ret = cpm_iface_test_ping(ping_val.value, config->msg_type, config->tout_ms, NULL);
		if (ret == -ETIME) {
			dev_warn(dev, "[t%d]: send ping -ETIME (iter:%d, tout_ms=%u)",
				 thread_id, iter, config->tout_ms);
			if (config->tout_ms >= PING_TEST_MAX_TIMEOUT) {
				/*
				 * if the timeout is set to beyond the max timeout,
				 * there is some core issue leading to -ETIME, so report the
				 * test case as FAIL
				 */
				thread_data->pass = 0;
				break;
			}

			continue;
		} else if (ret == -EBUSY) {
			/*
			 * In this case, the client should resend the data to the mailbox
			 * reset the iter count to previous count so that we can resend
			 * the same iteration count
			 */
			thread_data->ebusy_count++;

			/* sleep before retrying to send the data again */
			msleep(EBUSY_DELAY_TIME_IN_MS);

			continue;
		} else if (ret < 0) {
			dev_err(dev, "[t%d]: send ping fail (iter:%d, ret=%d)",
				thread_id, iter, ret);
			break;
		}

		dev_dbg(dev, "[t%d]: send ping done (iter:%d)", thread_id, iter);
		iter++;
	}

	thread_data->pass = (iter == config->nr_iters);
	if (thread_data->ebusy_count)
		dev_warn(dev, "[t%d]: -EBUSY count: %d", thread_id, thread_data->ebusy_count);

	complete(&thread_data->complete);
	return 0;
}

static bool
cpm_iface_test_invoke_ping(struct cpm_iface_test_ping_config *config)
{
	struct device *dev = client_data->dev;
	struct cpm_iface_test_thread_data *thread_data;
	int i, nr_pass = 0;

	dev_info(client_data->dev,
		 "ping_test parameters: msg_type %u, nr_threads: %u, nr_iters: %u, tout_ms: %u\n",
		 config->msg_type, config->nr_threads, config->nr_iters, config->tout_ms);

	thread_data = devm_kcalloc(dev, config->nr_threads, sizeof(*thread_data), GFP_KERNEL);
	if (!thread_data)
		return -ENOMEM;

	for (i = 0; i < config->nr_threads; i++) {
		cpm_iface_test_init_thread_data(&thread_data[i], i, config);
		kthread_run(cpm_iface_test_ping_thread, &thread_data[i], "cpm_ping_th#%d", i);
	}

	for (i = 0; i < config->nr_threads; i++) {
		wait_for_completion(&thread_data[i].complete);
		dev_info(dev, "%s: thread#%d %s\n", __func__, i,
			 thread_data[i].pass ? "Pass" : "Fail");
		if (thread_data[i].pass > 0)
			nr_pass++;
	}
	devm_kfree(dev, thread_data);

	return (nr_pass == config->nr_threads);
}

static void cpm_iface_test_client_callback(u32 context, void *msg, void *priv_data)
{
	struct cpm_iface_test_client *client_data;
	struct device *dev;

	if (!priv_data) {
		pr_err("%s: priv_data is NULL (context: %#34x)\n", __func__, context);
		return;
	}

	client_data = priv_data;
	dev = client_data->dev;

	dev_info(dev, "CPM-initiated transaction triggered. (context: %#34x)\n", context);
}

static int cpm_iface_test_usage(struct seq_file *s, void *data)
{
	static const char *usage =
		"Usage:\necho <msg_type> > <test_path>/msg_type (0 (ONEWAY), 1 REQUEST))\n"
		"echo <nr_threads> > <test_path>/nr_threads (Range: 1 to 100)\n"
		"echo <nr_iterations> > <test_path>/nr_iterations (Range: 1 to 1000)\n"
		"echo <tout_ms> > <test_path>/tout_ms (Range: 1 to 100)\n"
		"cat <test_path>/trigger_test";

	seq_printf(s, "%s\n", usage);

	return 0;
}

static int cpm_iface_test_ping_test(struct seq_file *s, void *data)
{
	struct device *dev = client_data->dev;
	bool pass;

	if (ping_test_config.nr_iters > PING_TEST_MAX_NUM_ITER) {
		dev_err(dev, "Number of iterations out of bound. Range: 0 to %d received: %u\n",
			PING_TEST_MAX_NUM_ITER, ping_test_config.nr_iters);
		return -EINVAL;
	}

	if (ping_test_config.nr_threads > PING_TEST_MAX_NUM_THREADS) {
		dev_err(dev, "Number of threads out of bound. Range: 0 to %d received: %u\n",
			PING_TEST_MAX_NUM_THREADS, ping_test_config.nr_threads);
		return -EINVAL;
	}

	pass = cpm_iface_test_invoke_ping(&ping_test_config);
	seq_printf(s, "[%s]\n", pass ? "Pass" : "Fail");

	return 0;
}

static int cpm_iface_test_create_ping_test(struct device *dev, struct dentry *debugfs_root)
{
	struct dentry *ping_test_dir;

	ping_test_dir = debugfs_create_dir("ping_test", debugfs_root);
	if (IS_ERR(ping_test_dir)) {
		dev_err(dev, "Failed to create ping_test sub dir\n");
		return PTR_ERR(ping_test_dir);
	}

	debugfs_create_devm_seqfile(dev, "test_usage", ping_test_dir, cpm_iface_test_usage);
	debugfs_create_devm_seqfile(dev, "trigger_test", ping_test_dir, cpm_iface_test_ping_test);

	debugfs_create_bool("msg_type", 0660, ping_test_dir, &ping_test_config.msg_type);
	debugfs_create_u32("nr_threads", 0660, ping_test_dir, &ping_test_config.nr_threads);
	debugfs_create_u32("nr_iterations", 0660, ping_test_dir, &ping_test_config.nr_iters);
	debugfs_create_u32("tout_ms", 0660, ping_test_dir, &ping_test_config.tout_ms);

	return 0;
}

static int cpm_iface_timestamp_test_usage(struct seq_file *s, void *data)
{
	static const char *usage =
		"Usage:\n"
		"  echo <val> > is_oneway_msg  # 1=ONEWAY, 0=REQUEST\n"
		"  echo <val> > nr_iterations  # Range: 1 to 1000\n"
		"  echo <val> > tout_ms        # Timeout in ms\n"
		"  cat trigger_test            # Run the test";

	seq_printf(s, "%s\n", usage);

	return 0;
}

static int cpm_iface_test_timestamp(struct seq_file *s, void *data)
{
	struct device *dev = client_data->dev;
	struct cpm_iface_event_timestamps timestamps;
	ktime_t start_time, end_time, event_time, prev_event_time;
	int ret, idx, iter;
	enum cpm_iface_msg_type msg_type = REQUEST_MSG;

	if (timestamp_test_config.is_oneway_msg)
		msg_type = ONEWAY_MSG;

	for (iter = 0; iter < timestamp_test_config.nr_iters; iter++) {
		prev_event_time = 0;

		start_time = goog_gtc_get_time_ns();
		ret = cpm_iface_test_ping(iter, msg_type, timestamp_test_config.tout_ms,
					  &timestamps);
		if (ret != 0) {
			seq_printf(s, "[Fail]: ping test fail to complete (ret: %d)\n", ret);
			return 0;
		}
		end_time = goog_gtc_get_time_ns();

		for (idx = 0; idx < MAX_CPM_IFACE_EVENTS; idx++) {
			event_time = timestamps.events[idx];
			dev_dbg(dev, "[%d] %llu\n", idx, event_time);

			if (event_time < start_time || event_time > end_time) {
				seq_puts(s, "[Fail]: out of range timestamp\n");
				return 0;
			}
			if (prev_event_time && prev_event_time > event_time) {
				seq_puts(s, "[Fail]: timestamps aren't monotonic\n");
				return 0;
			}
			prev_event_time = event_time;
		}
	}
	seq_puts(s, "[Pass]\n");

	return 0;
}

static int cpm_iface_test_create_timestamp_test(struct device *dev, struct dentry *debugfs_root)
{
	struct dentry *testcase_dir;
	const char *testcase_name = "timestamp_test";

	testcase_dir = debugfs_create_dir(testcase_name, debugfs_root);
	if (IS_ERR(testcase_dir)) {
		dev_err(dev, "Failed to create testcase dir of %s\n", testcase_name);
		return PTR_ERR(testcase_dir);
	}

	debugfs_create_devm_seqfile(dev, "test_usage", testcase_dir,
				    cpm_iface_timestamp_test_usage);
	debugfs_create_devm_seqfile(dev, "trigger_test", testcase_dir, cpm_iface_test_timestamp);

	debugfs_create_bool("is_oneway_msg", 0660, testcase_dir,
			    &timestamp_test_config.is_oneway_msg);
	debugfs_create_u32("nr_iterations", 0660, testcase_dir, &timestamp_test_config.nr_iters);
	debugfs_create_u32("tout_ms", 0660, testcase_dir, &timestamp_test_config.tout_ms);

	return 0;
}

static int cpm_iface_test_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	client_data = devm_kzalloc(dev, sizeof(*client_data), GFP_KERNEL);
	if (!client_data)
		return -ENOMEM;

	client_data->dev = dev;
	client_data->cpm_client = cpm_iface_request_client(dev,
							   CPM_IFACE_TEST_CLIENT_SRC_ID,
							   cpm_iface_test_client_callback,
							   client_data);
	if (IS_ERR(client_data->cpm_client)) {
		ret = PTR_ERR(client_data->cpm_client);

		if (ret == -EPROBE_DEFER)
			return ret;
		dev_err(dev, "Failed to request client (err:%ld)\n",
			PTR_ERR(client_data->cpm_client));
		return PTR_ERR(client_data->cpm_client);
	}

	client_data->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(client_data->debugfs_root)) {
		dev_err(dev, "Failed to create debugfs dir\n");
		ret = PTR_ERR(client_data->debugfs_root);
		goto err;
	}

	ret = cpm_iface_test_create_ping_test(dev, client_data->debugfs_root);
	if (ret < 0)
		goto err;

	ret = cpm_iface_test_create_timestamp_test(dev, client_data->debugfs_root);
	if (ret < 0)
		goto err;

	platform_set_drvdata(pdev, client_data);

	dev_dbg(dev, "%s init done\n", __func__);

	return 0;

err:
	cpm_iface_free_client(client_data->cpm_client);

	return ret;
}

static const struct of_device_id cpm_iface_test_of_match_table[] = {
	{ .compatible = "google,mba-cpm-iface-test" },
	{},
};
MODULE_DEVICE_TABLE(of, cpm_iface_test_of_match_table);

struct platform_driver cpm_iface_test = {
	.probe = cpm_iface_test_probe,
	.driver = {
		.name = "goog-mba-cpm-iface-test",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cpm_iface_test_of_match_table),
	},
};

module_platform_driver(cpm_iface_test);

MODULE_DESCRIPTION("Google MBA CPM interface test client");
MODULE_AUTHOR("Lucas Wei <lucaswei@google.com>");
MODULE_LICENSE("GPL");
