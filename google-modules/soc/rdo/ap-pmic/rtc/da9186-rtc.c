// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2022 Google LLC
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/time64.h>
#include <linux/types.h>

#include <ap-pmic/da9186.h>
#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>

#define MB_RTC_ID_NOT_ALARM (0)

/*
 * Seconds since 1970-01-01 to 2020-01-01 (Epoch time when RTC count = 0).
 * Morro datasheet specify the 32 bit RTC counter is representing the
 * seconds elapsed since 2020-01-01 00:00:00 UTC, not the common epoch
 * time (1970) that linux uses. This is the minimum time for RTC.
 */
#define RTC_TIMESTAMP_BEGIN_2020 1577836800ULL

enum pmic_rtc_alarm_ids {
	/*
	 * The linux RTC interface only support single alarm per RTC instance.
	 * Only use Alarm ID 0 in this driver.
	 */
	RTC_ALARM_0 = 0,
	RTC_ALARM_1,
	/* 2 and 3 are fix timer alarms. */
	RTC_ALARM_2,
	RTC_ALARM_3,
	RTC_ALARM_MAX,
};

struct da9186_rtc {
	struct device *dev;
	struct rtc_device *rtc;
	struct pmic_mfd_mbox mbox;
	/* DTS configurable parameters. */
	u32 mb_dest_channel;
};

// TODO(b/277538183): Blocked by CPM side, need to forward the alarm to AP.
/* Called when received alarm interrupt from CPM. */
//static int da9186_rtc_interrupt(struct device *dev, int irq_num)
//{
//	struct da9186_rtc *priv = dev_get_drvdata(dev);
//	rtc_update_irq(priv->rtc, 1, RTC_IRQF | RTC_AF);
//	return 0;
//}

/* Return 0 if NONCE is valid, else invalid. */
static int da9186_rtc_get_is_nonce_valid(struct device *dev)
{
	struct da9186_rtc *priv = dev_get_drvdata(dev);
	struct mailbox_data req_data = { 0 };
	struct mailbox_data resp_data;
	int ret;

	ret = pmic_mfd_mbox_send_req_blocking_read(priv->dev, &priv->mbox,
						   priv->mb_dest_channel,
						   MB_PMIC_TARGET_RTC,
						   MB_RTC_CMD_GET_NONCE_VALID,
						   MB_RTC_ID_NOT_ALARM,
						   req_data, &resp_data);
	if (unlikely(ret))
		return ret;

	/* Response is 1 when nonce is valid */
	return (resp_data.data[0] == 1) ? 0 : -EINVAL;
}

static int da9186_rtc_set_nonce_to_valid(struct device *dev)
{
	struct da9186_rtc *priv = dev_get_drvdata(dev);
	struct mailbox_data req_data = { 0 };
	int ret;

	ret = pmic_mfd_mbox_send_req_blocking(priv->dev, &priv->mbox,
					      priv->mb_dest_channel,
					      MB_PMIC_TARGET_RTC,
					      MB_RTC_CMD_SET_NONCE_VALID,
					      MB_RTC_ID_NOT_ALARM, req_data);
	if (unlikely(ret))
		return ret;
	return ret;
}

/* Read the current time from the RTC hardware */
static int da9186_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct da9186_rtc *priv = dev_get_drvdata(dev);
	struct mailbox_data req_data = { 0 };
	struct mailbox_data resp_data;
	u64 time;
	int ret;

	ret = pmic_mfd_mbox_send_req_blocking_read(priv->dev, &priv->mbox,
						   priv->mb_dest_channel,
						   MB_PMIC_TARGET_RTC,
						   MB_RTC_CMD_GET_RTC_TIME,
						   MB_RTC_ID_NOT_ALARM,
						   req_data, &resp_data);
	if (unlikely(ret))
		return ret;

	time = resp_data.data[0] + RTC_TIMESTAMP_BEGIN_2020;
	rtc_time64_to_tm(time, tm);

	return 0;
}

/* Set the current time in the RTC hardware */
static int da9186_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct da9186_rtc *priv = dev_get_drvdata(dev);
	time64_t time = rtc_tm_to_time64(tm);
	struct mailbox_data req_data = { 0 };

	if (time < RTC_TIMESTAMP_BEGIN_2020) {
		dev_err(dev, "Time before 2020-01-01 is impossible for RTC.\n");
		return -EINVAL;
	}

	req_data.data[0] = time - RTC_TIMESTAMP_BEGIN_2020;

	return pmic_mfd_mbox_send_req_blocking(priv->dev, &priv->mbox,
					       priv->mb_dest_channel,
					       MB_PMIC_TARGET_RTC,
					       MB_RTC_CMD_SET_RTC_TIME,
					       MB_RTC_ID_NOT_ALARM, req_data);
}

static int da9186_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct da9186_rtc *priv = dev_get_drvdata(dev);
	struct mailbox_data req_data = { 0 };
	struct mailbox_data resp_data;
	u64 alarm_time;
	int ret;

	ret = pmic_mfd_mbox_send_req_blocking_read(priv->dev, &priv->mbox,
						   priv->mb_dest_channel,
						   MB_PMIC_TARGET_RTC,
						   MB_RTC_CMD_GET_ALARM,
						   RTC_ALARM_0, req_data,
						   &resp_data);
	if (unlikely(ret))
		return ret;

	alarm->enabled = resp_data.data[0] ? 1 : 0;
	alarm_time = resp_data.data[1] + RTC_TIMESTAMP_BEGIN_2020;
	rtc_time64_to_tm(alarm_time, &alarm->time);

	return 0;
}

static int da9186_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct da9186_rtc *priv = dev_get_drvdata(dev);
	u64 alarm_time = rtc_tm_to_time64(&alarm->time);
	struct mailbox_data req_data = { 0 };

	if (alarm_time < RTC_TIMESTAMP_BEGIN_2020) {
		dev_err(dev, "Time before 2020-01-01 is impossible for RTC.\n");
		return -EINVAL;
	}

	req_data.data[0] = alarm->enabled ? 1 : 0;
	req_data.data[1] = (u32)(alarm_time - RTC_TIMESTAMP_BEGIN_2020);

	return pmic_mfd_mbox_send_req_blocking(priv->dev, &priv->mbox,
					       priv->mb_dest_channel,
					       MB_PMIC_TARGET_RTC,
					       MB_RTC_CMD_SET_ALARM,
					       RTC_ALARM_0, req_data);
}

static int da9186_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct da9186_rtc *priv = dev_get_drvdata(dev);
	struct mailbox_data req_data = { 0 };

	req_data.data[0] = enabled ? 1 : 0;
	return pmic_mfd_mbox_send_req_blocking(priv->dev, &priv->mbox,
					       priv->mb_dest_channel,
					       MB_PMIC_TARGET_RTC,
					       MB_RTC_CMD_SET_ALARM_ENABLE_DISABLE,
					       RTC_ALARM_0, req_data);
}

static const struct rtc_class_ops da9186_rtc_ops = {
	.read_time = da9186_rtc_read_time,
	.set_time = da9186_rtc_set_time,
	.read_alarm = da9186_rtc_read_alarm,
	.set_alarm = da9186_rtc_set_alarm,
	.alarm_irq_enable = da9186_rtc_alarm_irq_enable,
};

static int da9186_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da9186_rtc *priv;
	struct rtc_time tm;
	int ret;

	/* Design to obtain constraints from DT, not platform_data pointer */
	if (dev_get_platdata(dev)) {
		dev_err(dev,
			"platform data is not supported, use device tree\n");
		return -ENODEV;
	}
	if (!dev->of_node) {
		dev_err(dev, "Failed to find DT node: %s\n", pdev->name);
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->dev = dev;

	ret = of_property_read_u32(dev->of_node, "mba-dest-channel",
				   &priv->mb_dest_channel);
	if (ret < 0) {
		dev_err(dev, "Failed to read mba-dest-channel.\n");
		return ret;
	}

	if (of_property_read_bool(dev->of_node, "wakeup-source")) {
		ret = device_init_wakeup(dev, true);
		if (ret)
			dev_err(dev, "Init wakeup failed, err: %d\n", ret);
	}

	dev_info(dev, "Init mailbox client\n");
	ret = pmic_mfd_mbox_request(dev, &priv->mbox);
	if (ret < 0)
		return ret;

	priv->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(priv->rtc)) {
		dev_err(dev, "RTC device creation failed\n");
		goto free_mbox;
	}

	priv->rtc->ops = &da9186_rtc_ops;
	priv->rtc->range_min = RTC_TIMESTAMP_BEGIN_2020;
	priv->rtc->range_max = U32_MAX;

	/* Read the original RTC count from BBAT domain. */
	ret = da9186_rtc_read_time(dev, &tm);
	if (ret) {
		dev_err(dev, "Failed to read RTC time, err: %d\n", ret);
		goto free_mbox;
	}

	if (da9186_rtc_get_is_nonce_valid(dev) != 0) {
		/* If nonce is not valid, default RTC time to 2020 (zero) */
		rtc_time64_to_tm(RTC_TIMESTAMP_BEGIN_2020, &tm);
	}
	/*
	 * Re-write time back to RTC to update counts in both Main and BBAT power
	 * domain. This guarantees they will be in-sync and start ticking new time.
	 */
	ret = da9186_rtc_set_time(dev, &tm);
	if (ret < 0) {
		dev_err(dev, "Failed to init RTC time, err: %d\n", ret);
		goto free_mbox;
	}

	/* In the end, write to write-once nonce to lock it. */
	ret = da9186_rtc_set_nonce_to_valid(dev);
	if (ret < 0) {
		dev_err(dev, "Failed set nonce valid, err: %d\n", ret);
		goto free_mbox;
	}

	ret = devm_rtc_register_device(priv->rtc);
	if (ret < 0) {
		dev_err(dev, "Failed to register RTC device, err: %d\n", ret);
		goto free_mbox;
	}

	return ret;

free_mbox:
	pmic_mfd_mbox_release(&priv->mbox);

	return ret;
}

static void da9186_rtc_remove(struct platform_device *pdev)
{
	struct da9186_rtc *priv = platform_get_drvdata(pdev);

	if (IS_ERR_OR_NULL(priv))
		return;

	pmic_mfd_mbox_release(&priv->mbox);
}

static const struct platform_device_id da9186_rtc_id[] = {
	{ "da9186-rtc", 0 },
	{},
};
MODULE_DEVICE_TABLE(platform, da9186_rtc_id);

static struct platform_driver da9186_rtc_driver = {
	.probe = da9186_rtc_probe,
	.remove = da9186_rtc_remove,
	.driver = {
		.name = "da9186-rtc",
		.owner = THIS_MODULE,
	},
	.id_table = da9186_rtc_id,
};
module_platform_driver(da9186_rtc_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("DA9186 PMIC RTC Driver");
MODULE_LICENSE("GPL");
