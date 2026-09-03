// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2023 Google LLC */
#include <linux/device.h>
#include <linux/devm-helpers.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "google_thermal.h"

//TODO(b/346574616): Re-organize all the API's to not require cpm_thermal and other cleanup.
static struct google_cpm_thermal *cpm_thermal;

static int cpm_thermal_register_thermal_zone(struct google_cpm_thermal *cpm_thermal,
					     struct thermal_zone_device *tz)
{
	struct thermal_zone_node *node;

	node = devm_kzalloc(cpm_thermal->dev, sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->tz = tz;

	list_add_tail(&node->list, &cpm_thermal->thermal_zone_list);

	return 0;
}

static int cpm_thermal_unregister_thermal_zone(struct google_cpm_thermal *cpm_thermal,
					       struct thermal_zone_device *tz)
{
	struct thermal_zone_node *node;
	struct list_head *it, *n;
	struct thermal_data *removed = thermal_zone_device_priv(tz);
	int removed_id = removed->id;

	mutex_lock(&cpm_thermal->thermal_zone_list_lock);
	list_for_each_safe(it, n, &cpm_thermal->thermal_zone_list) {
		node = list_entry(it, struct thermal_zone_node, list);
		struct thermal_data *data = thermal_zone_device_priv(node->tz);

		if (data->id == removed_id) {
			list_del(&node->list);
			devm_kfree(cpm_thermal->dev, node);
			mutex_unlock(&cpm_thermal->thermal_zone_list_lock);
			return 0;
		}
	}
	mutex_unlock(&cpm_thermal->thermal_zone_list_lock);

	return -EINVAL;
}

static struct thermal_zone_device *get_thermal_zone_by_id(struct google_cpm_thermal *cpm_thermal,
							  int id)
{
	struct thermal_zone_node *node;

	list_for_each_entry(node, &cpm_thermal->thermal_zone_list, list) {
		struct thermal_data *data = thermal_zone_device_priv(node->tz);

		if (data->id == id)
			return node->tz;
	}

	return NULL;
}

static void cpm_thermal_throttle_worker(struct work_struct *work)
{
	struct thermal_throttle_work *throttle_work = container_of(work,
								   struct thermal_throttle_work,
								   work);
	struct google_cpm_thermal *cpm_thermal  = container_of(throttle_work,
							       struct google_cpm_thermal,
							       throttle_work);
	struct thermal_zone_device *tz;
	struct thermal_data *data;
	struct device *dev = cpm_thermal->dev;
	union thermal_message message;

	spin_lock_irq(&cpm_thermal->throttle_lock);
	message = throttle_work->thermal_msg;
	spin_unlock_irq(&cpm_thermal->throttle_lock);

	tz = get_thermal_zone_by_id(cpm_thermal, message.req.tzid);
	if (!tz) {
		dev_warn(dev, "No thermal zone id %d\n", message.req.tzid);
		return;
	}

	dev_dbg(dev, "Route to thermal_zone: %d\n", thermal_zone_device_id(tz));

	data = thermal_zone_device_priv(tz);

	data->ops->google_thermal_throttling(data, message.data[1]);
}

static void cpm_thermal_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct google_cpm_thermal *cpm_thermal = priv_data;
	struct cpm_iface_payload *cpm_msg = msg;

	spin_lock(&cpm_thermal->throttle_lock);
	memcpy(&cpm_thermal->throttle_work.thermal_msg, cpm_msg->payload,
	       sizeof(cpm_thermal->throttle_work.thermal_msg));
	spin_unlock(&cpm_thermal->throttle_lock);

	if (cpm_thermal->throttle_work.thermal_msg.req.type == THERMAL_RX_THROTTLE) {
		/* Thermal mitigation needs to happen in high priority to contain the Tj. */
		queue_work(system_highpri_wq, &cpm_thermal->throttle_work.work);
	}
}

/* send request will need to wait for CPM response */
static int cpm_thermal_send_request(struct google_cpm_thermal *cpm_thermal,
				    union thermal_message *message)
{
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req cpm_req;
	int ret;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MBA_CLIENT_TX_TIMEOUT;
	cpm_req.dst_id = cpm_thermal->remote_ch;

	memcpy(req_msg.payload, message->data, sizeof(*message));

	dev_dbg(cpm_thermal->dev, "msg: [0]:0x%08x [1]:0x%08x [2]:0x%08x\n", req_msg.payload[0],
		req_msg.payload[1], req_msg.payload[2]);

	/* Send the message */
	ret = cpm_send_message(cpm_thermal->client, &cpm_req);
	if (ret < 0) {
		dev_err(cpm_thermal->dev, "Send cpm message failed (%d)\n", ret);
		return ret;
	}

	memcpy(message->data, resp_msg.payload, sizeof(*message));

	return 0;
}

int cpm_thermal_send_mbox_request(union thermal_message *message)
{
	if (!cpm_thermal)
		return -ENODATA;

	return cpm_thermal_send_request(cpm_thermal, message);
}
EXPORT_SYMBOL_GPL(cpm_thermal_send_mbox_request);

/* send msg won't need to wait for CPM response */
static int cpm_thermal_send_msg(struct google_cpm_thermal *cpm_thermal,
				union thermal_message *message)
{
	struct cpm_iface_payload req_msg;
	struct cpm_iface_req cpm_req;
	int ret;

	cpm_req.msg_type = ONEWAY_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.tout_ms = MBA_CLIENT_TX_TIMEOUT;
	cpm_req.dst_id = cpm_thermal->remote_ch;

	memcpy(req_msg.payload, message->data, sizeof(*message));

	dev_dbg(cpm_thermal->dev, "msg: [0]:0x%08x [1]:0x%08x [2]:0x%08x\n", req_msg.payload[0],
		req_msg.payload[1], req_msg.payload[2]);

	/* Send the message */
	ret = cpm_send_message(cpm_thermal->client, &cpm_req);
	if (ret < 0) {
		dev_err(cpm_thermal->dev, "Send cpm message failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int cpm_thermal_init(struct google_cpm_thermal *cpm_thermal)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_INIT;

	return cpm_thermal_send_msg(cpm_thermal, &message);
}

static int cpm_thermal_get_temp(struct google_cpm_thermal *cpm_thermal, u8 tz)
{
	union thermal_message message;
	int ret;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_TEMP;
	message.req.tzid = tz;

	ret = cpm_thermal_send_request(cpm_thermal, &message);

	return ret ? ret : message.resp.temp;
}

static int cpm_thermal_set_trip_temp(struct google_cpm_thermal *cpm_thermal, u8 tz,
				     s8 *temperature)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_TRIP_TEMP;
	message.req.tzid = tz;
	message.req.req_rsvd0 = temperature[0];
	message.req.req_rsvd1 = temperature[1];
	message.req.req_rsvd2 = temperature[2];
	message.req.req_rsvd3 = temperature[3];
	message.req.req_rsvd4 = temperature[4];
	message.req.req_rsvd5 = temperature[5];
	message.req.req_rsvd6 = temperature[6];
	message.req.req_rsvd7 = temperature[7];

	return cpm_thermal_send_request(cpm_thermal, &message);
}

static int cpm_thermal_set_trip_hyst(struct google_cpm_thermal *cpm_thermal, u8 tz,
				     u8 *hysteresis)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_TRIP_HYST;
	message.req.tzid = tz;
	message.req.req_rsvd0 = hysteresis[0];
	message.req.req_rsvd1 = hysteresis[1];
	message.req.req_rsvd2 = hysteresis[2];
	message.req.req_rsvd3 = hysteresis[3];
	message.req.req_rsvd4 = hysteresis[4];
	message.req.req_rsvd5 = hysteresis[5];
	message.req.req_rsvd6 = hysteresis[6];
	message.req.req_rsvd7 = hysteresis[7];

	return cpm_thermal_send_request(cpm_thermal, &message);
}

static int cpm_thermal_set_trip_type(struct google_cpm_thermal *cpm_thermal, u8 tz,
				     enum thermal_trip_type *type)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_TRIP_TYPE;
	message.req.tzid = tz;
	message.req.req_rsvd0 = type[0];
	message.req.req_rsvd1 = type[1];
	message.req.req_rsvd2 = type[2];
	message.req.req_rsvd3 = type[3];
	message.req.req_rsvd4 = type[4];
	message.req.req_rsvd5 = type[5];
	message.req.req_rsvd6 = type[6];
	message.req.req_rsvd7 = type[7];

	return cpm_thermal_send_request(cpm_thermal, &message);
}

/* TODO(b/327984482): Need to be discussed if this is needed or not. */
static int cpm_thermal_set_interrupt_enable(struct google_cpm_thermal *cpm_thermal, u8 tz,
					    u16 inten)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_TRIP_TYPE;
	message.req.tzid = tz;
	message.req.req_rsvd0 = inten;

	return cpm_thermal_send_request(cpm_thermal, &message);
}

/* TODO(b/327984482): Need to be discussed if this is needed or not. */
static int cpm_thermal_tmu_control(struct google_cpm_thermal *cpm_thermal, u8 tz, bool control)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_TMU_CONTROL;
	message.req.tzid = tz;
	message.req.req_rsvd0 = control;

	return cpm_thermal_send_request(cpm_thermal, &message);
}

static int cpm_thermal_set_param(struct google_cpm_thermal *cpm_thermal, u8 tz, u8 type, int val)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_PARAM;
	message.req.tzid = tz;
	message.req.rsvd = type;
	message.data[1] = val;

	return cpm_thermal_send_request(cpm_thermal, &message);
}

static int cpm_thermal_set_gov_select(struct google_cpm_thermal *cpm_thermal, u8 tz, u32 gov_select)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_GOV_SELECT;
	message.req.tzid = tz;
	message.data[1] = gov_select;

	return cpm_thermal_send_request(cpm_thermal, &message);
}

static int cpm_thermal_get_sm_addr(struct google_cpm_thermal *cpm_thermal, u8 tz,
				   enum thermal_section section, u8 *version, int *addr,
				   u32 *size)
{
	union thermal_message message;
	int ret;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_SM;
	message.req.tzid = tz;
	message.req.rsvd = section;

	ret = cpm_thermal_send_request(cpm_thermal, &message);

	*version = message.resp.ret;
	*addr = message.data[1];
	*size = message.data[2];

	return ret;
}

static int cpm_thermal_set_powertable(struct google_cpm_thermal *cpm_thermal, u8 tz, u8 idx,
				      int val)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_POWERTABLE;
	message.req.tzid = tz;
	message.req.rsvd = idx;
	message.data[1] = val;

	return cpm_thermal_send_request(cpm_thermal, &message);
}

static int cpm_thermal_get_powertable(struct google_cpm_thermal *cpm_thermal, u8 tz, u8 idx,
				      int *val)
{
	union thermal_message message;
	int ret;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_POWERTABLE;
	message.req.tzid = tz;
	message.req.rsvd = idx;

	ret = cpm_thermal_send_request(cpm_thermal, &message);

	*val = message.data[1];

	return ret;
}

static int cpm_thermal_set_polling_delay(struct google_cpm_thermal *cpm_thermal, u8 tz, u16 delay)
{
	union thermal_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_POLLING_DELAY;
	message.req.tzid = tz;
	message.data[1] = delay;

	return cpm_thermal_send_request(cpm_thermal, &message);
}

static struct cpm_thermal_ops ops = {
	.register_thermal_zone = cpm_thermal_register_thermal_zone,
	.unregister_thermal_zone = cpm_thermal_unregister_thermal_zone,
	.init = cpm_thermal_init,
	.get_temp = cpm_thermal_get_temp,
	.set_trip_temp = cpm_thermal_set_trip_temp,
	.set_trip_hyst = cpm_thermal_set_trip_hyst,
	.set_trip_type = cpm_thermal_set_trip_type,
	.set_interrupt_enable = cpm_thermal_set_interrupt_enable,
	.tmu_control = cpm_thermal_tmu_control,
	.set_param = cpm_thermal_set_param,
	.set_gov_select = cpm_thermal_set_gov_select,
	.get_sm_addr = cpm_thermal_get_sm_addr,
	.set_powertable = cpm_thermal_set_powertable,
	.get_powertable = cpm_thermal_get_powertable,
	.set_polling_delay = cpm_thermal_set_polling_delay,
};

static int google_cpm_thermal_init_mbox(struct google_cpm_thermal *cpm_thermal)
{
	int ret;

	cpm_thermal->client = cpm_iface_request_client(cpm_thermal->dev, CPM_SERVICE_ID_THERMAL,
						       cpm_thermal_rx_callback, cpm_thermal);
	if (IS_ERR(cpm_thermal->client)) {
		ret = PTR_ERR(cpm_thermal->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(cpm_thermal->dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(cpm_thermal->dev, "Failed to request cpm client err: %d\n", ret);

		return ret;
	}

	return 0;
}

static int cpm_thermal_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	cpm_thermal = devm_kzalloc(dev, sizeof(*cpm_thermal), GFP_KERNEL);
	cpm_thermal->dev = dev;

	mutex_init(&cpm_thermal->thermal_zone_list_lock);
	INIT_LIST_HEAD(&cpm_thermal->thermal_zone_list);
	spin_lock_init(&cpm_thermal->throttle_lock);
	devm_work_autocancel(dev, &cpm_thermal->throttle_work.work, cpm_thermal_throttle_worker);

	ret = google_cpm_thermal_init_mbox(cpm_thermal);
	if (ret)
		return ret;

	if (of_property_read_u32(np, "mba-dest-channel", &cpm_thermal->remote_ch)) {
		dev_err(dev, "Failed to read mba-dest-channel.\n");
		return -EINVAL;
	}

	cpm_thermal->ops = &ops;
	platform_set_drvdata(pdev, cpm_thermal);
	return 0;
}

static inline void google_cpm_thermal_mbox_free(struct google_cpm_thermal *cpm_thermal)
{
	if (!IS_ERR_OR_NULL(cpm_thermal->client))
		cpm_iface_free_client(cpm_thermal->client);
}

static void cpm_thermal_platform_remove(struct platform_device *pdev)
{
	struct google_cpm_thermal *cpm_thermal = (struct google_cpm_thermal *)
						       platform_get_drvdata(pdev);

	google_cpm_thermal_mbox_free(cpm_thermal);
}

static const struct of_device_id google_thermal_client_of_match_table[] = {
	{ .compatible = "google,cpm-thermal" },
	{},
};
MODULE_DEVICE_TABLE(of, google_thermal_client_of_match_table);

static struct platform_driver google_thermal_client_driver = {
	.probe = cpm_thermal_platform_probe,
	.remove = cpm_thermal_platform_remove,
	.driver = {
		.name = "google-cpm-thermal",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_thermal_client_of_match_table),
	},
};
module_platform_driver(google_thermal_client_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google thermal client driver");
MODULE_LICENSE("GPL");
