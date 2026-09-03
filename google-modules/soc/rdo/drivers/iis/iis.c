// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Google LLC.
 *
 * IP Initiator Sandboxer driver.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/trusty/trusty_ipc.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#define DRIVER_NAME "iis"

#define IIS_MGR_CMD_RESP 0x80000000

#define IIS_MGR_CMD_INIT 0x00000000
#define IIS_MGR_CMD_RESTORE 0x00000001
#define IIS_MGR_CMD_SUSPEND 0x00000002

#define IIS_MGR_PORT "com.google.pixel.iis"

#define TRUSTY_TA_CONNECTION_TIMEOUT 5000U
#define TRUSTY_TA_MESSAGE_TIMEOUT 1000U

/* minimal structure to send some additional data may follow */
struct iis_msg {
	__u32 cmd;
	__u32 dev_id;
};

struct iis_msg_rsp {
	__u32 cmd;
	__u32 dev_id;
	__s32 err;
};

#define IIS_RESP_SIZE sizeof(struct iis_msg_rsp)

struct iis_drvdata {
	struct device *dev;
	u32 ta_iis_id;
	struct tipc_chan *chan;
	bool connected;
	struct completion connect_comp;
	struct completion msg_comp;
	struct iis_msg_rsp rsp;
	struct mutex lock;
};

static int iis_connect_trusty(struct iis_drvdata *iis, const char *port);
static void iis_disconnect_trusty(struct iis_drvdata *iis);

static void iis_handle_event(void *cb_arg, int event)
{
	struct iis_drvdata *iis = cb_arg;

	if (event == TIPC_CHANNEL_CONNECTED) {
		iis->connected = true;
		complete(&iis->connect_comp);
	} else if (event == TIPC_CHANNEL_DISCONNECTED ||
			event == TIPC_CHANNEL_SHUTDOWN) {
		iis->connected = false;
		complete(&iis->connect_comp);
	}
}

static struct tipc_msg_buf *iis_handle_msg(void *cb_arg,
						struct tipc_msg_buf *mb)
{
	struct iis_drvdata *iis = cb_arg;
	void *data;
	int len;

	len = mb_avail_data(mb);
	if (len >= IIS_RESP_SIZE) {
		data = mb_get_data(mb, IIS_RESP_SIZE);
		if (data != NULL)
			memcpy(&iis->rsp, data, IIS_RESP_SIZE);
	}

	complete(&iis->msg_comp);

	return mb;
}

static const struct tipc_chan_ops iis_tipc_ops = {
	.handle_event = iis_handle_event,
	.handle_msg = iis_handle_msg,
};

/**
 * iis_send_msg - Send a message to the Trusty TA
 * @iis: IIS driver data
 * @data: Pointer to message data
 * @len: Length of message data
 * @err: Pointer to store TA-side error code
 *
 * Return: 0 on success (IPC transaction completed), negative error code on failure.
 */
static int iis_send_msg(struct iis_drvdata *iis, const void *data, size_t len,
			s32 *err)
{
	const struct iis_msg *msg = data;
	struct tipc_msg_buf *txbuf;
	void *msg_data;
	int ret;
	int cmd;

	if (len < sizeof(struct iis_msg))
		return -EINVAL;

	if (iis_connect_trusty(iis, IIS_MGR_PORT) < 0)
		return -ENOTCONN;

	txbuf = tipc_chan_get_txbuf_timeout(
		iis->chan, msecs_to_jiffies(TRUSTY_TA_MESSAGE_TIMEOUT));
	if (IS_ERR(txbuf)) {
		ret = PTR_ERR(txbuf);
		return ret;
	}

	msg_data = mb_put_data(txbuf, len);
	if (!msg_data) {
		tipc_chan_put_txbuf(iis->chan, txbuf);
		return -ENOMEM;
	}
	memcpy(msg_data, data, len);

	memset(&iis->rsp, 0, sizeof(iis->rsp));
	reinit_completion(&iis->msg_comp);
	ret = tipc_chan_queue_msg(iis->chan, txbuf);
	if (ret < 0) {
		tipc_chan_put_txbuf(iis->chan, txbuf);
		return ret;
	}

	ret = wait_for_completion_timeout(
		&iis->msg_comp, msecs_to_jiffies(TRUSTY_TA_MESSAGE_TIMEOUT));
	if (ret <= 0) {
		ret = (!ret) ? -ETIMEDOUT : ret;
		dev_err(iis->dev, "timeout on completion\n");
		iis_disconnect_trusty(iis);
		return ret;
	}

	cmd = msg->cmd;
	if (iis->rsp.cmd != (cmd | IIS_MGR_CMD_RESP)) {
		dev_err(iis->dev, "unexpected rsp cmd (%x vs %x)\n", iis->rsp.cmd,
			cmd | IIS_MGR_CMD_RESP);
		return -EIO;
	}

	if (err)
		*err = iis->rsp.err;

	return 0;
}

/**
 * iis_connect_trusty - Connect to the Trusty TA port
 * @iis: IIS driver data
 * @port: Trusty port name
 *
 * Return: 0 on success, negative error code on failure.
 */
static int iis_connect_trusty(struct iis_drvdata *iis, const char *port)
{
	int ret;

	if (iis->chan && iis->connected)
		return 0;

	if (iis->chan && !iis->connected) {
		tipc_chan_shutdown(iis->chan);
		tipc_chan_destroy(iis->chan);
		iis->chan = NULL;
	}

	if (!iis->chan) {
		iis->chan = tipc_create_channel(NULL, &iis_tipc_ops, iis);
		if (IS_ERR(iis->chan)) {
			dev_err(iis->dev, "failed to create tipc channel\n");
			ret = PTR_ERR(iis->chan);
			iis->chan = NULL;
			return ret;
		}
	}

	reinit_completion(&iis->connect_comp);
	ret = tipc_chan_connect(iis->chan, port);
	if (ret < 0) {
		dev_err(iis->dev, "failed to connect to %s\n", port);
		tipc_chan_destroy(iis->chan);
		iis->chan = NULL;
		return ret;
	}

	ret = wait_for_completion_timeout(
		&iis->connect_comp,
		msecs_to_jiffies(TRUSTY_TA_CONNECTION_TIMEOUT));
	if (ret == 0) {
		dev_err(iis->dev, "timeout on completion\n");
		tipc_chan_shutdown(iis->chan);
		tipc_chan_destroy(iis->chan);
		iis->chan = NULL;
		return -ETIMEDOUT;
	}

	if (!iis->connected) {
		dev_err(iis->dev, "failed to connect to %s\n", port);
		tipc_chan_shutdown(iis->chan);
		tipc_chan_destroy(iis->chan);
		iis->chan = NULL;
		return -ECONNREFUSED;
	}

	return 0;
}

static void iis_disconnect_trusty(struct iis_drvdata *iis)
{
	if (!iis->chan)
		return;

	tipc_chan_shutdown(iis->chan);
	tipc_chan_destroy(iis->chan);
	iis->chan = NULL;
	iis->connected = false;
}

static int iis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iis_drvdata *iis;
	struct iis_msg msg;
	s32 ta_err = 0;
	int ret;

	iis = devm_kzalloc(dev, sizeof(*iis), GFP_KERNEL);
	if (!iis)
		return -ENOMEM;

	iis->dev = dev;
	platform_set_drvdata(pdev, iis);

	ret = of_property_read_u32(dev->of_node, "google,ta-iis-id", &iis->ta_iis_id);
	if (ret) {
		dev_err(dev, "failed to read google,ta-iis-id\n");
		return ret;
	}

	init_completion(&iis->connect_comp);
	init_completion(&iis->msg_comp);
	mutex_init(&iis->lock);

	msg.cmd = IIS_MGR_CMD_INIT;
	msg.dev_id = iis->ta_iis_id;

	mutex_lock(&iis->lock);
	ret = iis_send_msg(iis, &msg, sizeof(msg), &ta_err);
	if (ret || ta_err) {
		dev_err(dev,
			"failed to send init message to TA (ret %d, ta_err %d)\n",
			ret, ta_err);
		ret = ret ? ret : -EIO;
		goto err_disconnect;
	}
	mutex_unlock(&iis->lock);

	/* Now that TA is initialized, enable PM runtime and mark as active. */
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	/* Hold usage count during probe without triggering resume callbacks. */
	pm_runtime_get_noresume(dev);

	/* Probe finished. Allow suspend. */
	pm_runtime_put_sync(dev);

	return 0;

err_disconnect:
	iis_disconnect_trusty(iis);
	mutex_unlock(&iis->lock);
	mutex_destroy(&iis->lock);
	return ret;
}

static int __iis_suspend(struct device *dev);

static void iis_remove(struct platform_device *pdev)
{
	struct iis_drvdata *iis = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	pm_runtime_get_sync(dev);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);

	/* Notify TA of turndown by sending suspend prior to removal */
	__iis_suspend(dev);

	mutex_lock(&iis->lock);
	iis_disconnect_trusty(iis);
	mutex_unlock(&iis->lock);
	mutex_destroy(&iis->lock);
}

#if defined(CONFIG_PM_SLEEP) || defined(CONFIG_PM)
static int __iis_resume(struct device *dev)
{
	struct iis_drvdata *iis = dev_get_drvdata(dev);
	u32 ta_iis_id = iis->ta_iis_id;
	s32 ta_err = 0;
	int ret;
	const struct iis_msg msg = {
		.cmd = IIS_MGR_CMD_RESTORE,
		.dev_id = ta_iis_id,
	};

	mutex_lock(&iis->lock);
	ret = iis_send_msg(iis, &msg, sizeof(msg), &ta_err);
	if (ret || ta_err) {
		dev_err(dev,
			"failed to send resume message to TA (ret %d, ta_err %d)\n",
			ret, ta_err);
		mutex_unlock(&iis->lock);
		return ret ? ret : -EIO;
	}
	mutex_unlock(&iis->lock);

	return 0;
}

static int __iis_suspend(struct device *dev)
{
	struct iis_drvdata *iis = dev_get_drvdata(dev);
	u32 ta_iis_id = iis->ta_iis_id;
	s32 ta_err = 0;
	int ret;
	const struct iis_msg msg = {
		.cmd = IIS_MGR_CMD_SUSPEND,
		.dev_id = ta_iis_id,
	};

	mutex_lock(&iis->lock);
	ret = iis_send_msg(iis, &msg, sizeof(msg), &ta_err);
	if (ret || ta_err) {
		dev_err(dev,
			"failed to send suspend message to TA (ret %d, ta_err %d)\n",
			ret, ta_err);
		mutex_unlock(&iis->lock);
		return ret ? ret : -EIO;
	}
	mutex_unlock(&iis->lock);

	return 0;
}
#endif

#ifdef CONFIG_PM
static int iis_runtime_suspend(struct device *dev)
{
	struct iis_drvdata *iis = dev_get_drvdata(dev);

	dev_dbg(iis->dev, "runtime suspending\n");
	return __iis_suspend(iis->dev);
}

static int iis_runtime_resume(struct device *dev)
{
	struct iis_drvdata *iis = dev_get_drvdata(dev);

	dev_dbg(iis->dev, "runtime resuming\n");
	return __iis_resume(iis->dev);
}
#endif

static const struct dev_pm_ops iis_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(iis_runtime_suspend, iis_runtime_resume, NULL)
};
#define IIS_PM_OPS (&iis_pm_ops)

static const struct of_device_id iis_of_match[] = {
	{
		.compatible = "google,iis",
	},
	{},
};
MODULE_DEVICE_TABLE(of, iis_of_match);

static struct platform_driver iis_driver = {
	.probe = iis_probe,
	.remove = iis_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = iis_of_match,
		.pm = pm_ptr(IIS_PM_OPS),
	},
};

module_platform_driver(iis_driver);

MODULE_AUTHOR("Chris Hockuba <khockuba@google.com>");
MODULE_DESCRIPTION("IIS Driver");
MODULE_LICENSE("GPL");
