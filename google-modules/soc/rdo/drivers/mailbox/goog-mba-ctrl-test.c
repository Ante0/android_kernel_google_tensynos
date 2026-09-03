// SPDX-License-Identifier: GPL-2.0-only

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <soc/google/gdmc_service_ids.h>

enum goog_mba_mbox_channel {
	TX_CH_0,
	RX_CH_1,
	NR_CHANNELS,
};

#define MAX_TX_PAYLOAD 4
#define MAX_RX_PAYLOAD 4

struct goog_mba_ctrl_test {
	struct device *dev;
	struct dentry *debugfs_root;
	struct mbox_client clients[NR_CHANNELS];
	struct mbox_chan *chans[NR_CHANNELS];
	u32 payload_tx[MAX_TX_PAYLOAD];
	u32 payload_rx[MAX_RX_PAYLOAD];
	struct task_struct *mba_ctrl_test_thread;
	struct completion comp;
};

#define RESPONSE_TIMEOUT_MS 100
#define RESPONSE_TIMEOUT_JIFFIES msecs_to_jiffies(RESPONSE_TIMEOUT_MS)

static int goog_mba_ctrl_test_gdmc_ping_test(struct goog_mba_ctrl_test *mba_ctrl_test)
{
	const int ping_request_value = 0x1234;
	struct device *dev = mba_ctrl_test->dev;
	struct mbox_chan *mbox_chan;
	u32 *payload;
	u32 value;
	int ret;

	mbox_chan = mba_ctrl_test->chans[TX_CH_0];
	payload = mba_ctrl_test->payload_tx;
	memset(payload, 0, sizeof(*payload) * MAX_TX_PAYLOAD);

	dev_info(dev, "Start to run %s.\n", __func__);

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_PING);
	goog_mba_nq_xport_set_oneway(payload, false);
	goog_mba_nq_xport_set_data(payload, ping_request_value);

	ret = mbox_send_message(mbox_chan, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to send msg, ret %d\n", ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&mba_ctrl_test->comp, RESPONSE_TIMEOUT_JIFFIES);
	if (ret == 0) {
		dev_err(dev, "Wait for response timeout.\n");
		return ret;
	}

	value = goog_mba_nq_xport_get_data(mba_ctrl_test->payload_rx);
	if (value != (ping_request_value + 1)) {
		dev_err(dev, "Testing failed. Value is 0x%x. expected 0x%x\n",
			value, ping_request_value + 1);
		return ret;
	}

	dev_info(dev, "%s passed.\n", __func__);

	return 0;
}

static int goog_mba_ctrl_test_main(void *data)
{
	struct goog_mba_ctrl_test *mba_ctrl_test = data;

	goog_mba_ctrl_test_gdmc_ping_test(mba_ctrl_test);

	return 0;
}

static void goog_mba_ctrl_test_tx_done(struct mbox_client *client, void *mssg, int r)
{
	struct device *dev = client->dev;

	dev_dbg(dev, "tx done\n");
}

static void goog_mba_ctrl_test_rx_callback(struct mbox_client *client, void *mssg)
{
	struct goog_mba_ctrl_test *mba_ctrl_test;
	struct device *dev = client->dev;
	int index;

	mba_ctrl_test = dev_get_drvdata(dev);
	index = client - mba_ctrl_test->clients;
	dev_dbg(dev, "Client#%d received callback.\n", index);

	memcpy(mba_ctrl_test->payload_rx, mssg, sizeof(MAX_RX_PAYLOAD));
	complete(&mba_ctrl_test->comp);
}

static void goog_mba_ctrl_test_remove_chan(struct goog_mba_ctrl_test *mba_ctrl_test, int index)
{
	struct mbox_chan *chan;

	chan = mba_ctrl_test->chans[index];
	if (chan)
		mbox_free_channel(chan);
}

static void goog_mba_ctrl_test_remove_chans(struct goog_mba_ctrl_test *mba_ctrl_test)
{
	int i;

	for (i = 0; i < NR_CHANNELS; i++)
		goog_mba_ctrl_test_remove_chan(mba_ctrl_test, i);
}

static int goog_mba_ctrl_test_request_chan(struct goog_mba_ctrl_test *mba_ctrl_test, int index)
{
	struct mbox_client *client;
	struct mbox_chan *chan;

	client = &mba_ctrl_test->clients[index];

	client->dev = mba_ctrl_test->dev;
	client->tx_block = false;
	client->rx_callback = goog_mba_ctrl_test_rx_callback;
	client->tx_done = goog_mba_ctrl_test_tx_done;

	chan = mbox_request_channel(client, index);

	if (IS_ERR(chan)) {
		dev_err(client->dev,
			"Failed to request mailbox channel. (index: %d, err %ld)\n",
			index, PTR_ERR(chan));

		mba_ctrl_test->chans[index] = NULL;

		return PTR_ERR(chan);
	}

	mba_ctrl_test->chans[index] = chan;

	return 0;
}

static int goog_mba_ctrl_test_request_chans(struct goog_mba_ctrl_test *mba_ctrl_test)
{
	int i;
	int ret = 0;

	for (i = 0; i < NR_CHANNELS; i++) {
		ret = goog_mba_ctrl_test_request_chan(mba_ctrl_test, i);
		if (ret) {
			/* remove all previously requested channels */
			goog_mba_ctrl_test_remove_chans(mba_ctrl_test);

			return ret;
		}
	}

	return ret;
}

static int goog_mba_ctrl_test_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct goog_mba_ctrl_test *mba_ctrl_test;
	struct task_struct *ts;
	int ret;

	mba_ctrl_test = devm_kzalloc(dev, sizeof(*mba_ctrl_test), GFP_KERNEL);
	if (!mba_ctrl_test)
		return -ENOMEM;
	platform_set_drvdata(pdev, mba_ctrl_test);
	mba_ctrl_test->dev = dev;
	init_completion(&mba_ctrl_test->comp);

	ret = goog_mba_ctrl_test_request_chans(mba_ctrl_test);
	if (ret)
		return ret;

	mba_ctrl_test->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	ts = kthread_run(goog_mba_ctrl_test_main, mba_ctrl_test, "goog_mba_ctrl_test_thread");
	if (IS_ERR(ts)) {
		dev_err(dev, "Failed to run test thread. (ret=%ld)\n", PTR_ERR(ts));

		debugfs_remove_recursive(mba_ctrl_test->debugfs_root);
		/* remove all previously requested channels */
		goog_mba_ctrl_test_remove_chans(mba_ctrl_test);

		return PTR_ERR(ts);
	}
	mba_ctrl_test->mba_ctrl_test_thread = ts;

	return 0;
}

static void goog_mba_ctrl_test_remove(struct platform_device *pdev)
{
	struct goog_mba_ctrl_test *mba_ctrl_test = platform_get_drvdata(pdev);

	goog_mba_ctrl_test_remove_chans(mba_ctrl_test);

	debugfs_remove_recursive(mba_ctrl_test->debugfs_root);
}

static const struct of_device_id
	goog_mba_ctrl_test_of_match_table[] = {
		{ .compatible = "google,mba-ctrl-test" },
		{},
	};
MODULE_DEVICE_TABLE(of, goog_mba_ctrl_test_of_match_table);

struct platform_driver goog_mba_ctrl_test_driver = {
	.probe = goog_mba_ctrl_test_probe,
	.remove = goog_mba_ctrl_test_remove,
	.driver = {
		.name = "goog-mba-ctrl-test",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goog_mba_ctrl_test_of_match_table),
	},
};

module_platform_driver(goog_mba_ctrl_test_driver);

MODULE_DESCRIPTION("Google MailBox Array (MBA) Driver Testing");
MODULE_AUTHOR("Lucas Wei <lucaswei@google.com>");
MODULE_LICENSE("GPL");
