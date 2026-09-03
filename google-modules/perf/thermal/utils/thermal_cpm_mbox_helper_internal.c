// SPDX-License-Identifier: GPL-2.0-only
/*
 * thermal_cpm_mbox_impl Implement for thermal_cpm_mbox functionality.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#include <linux/devm-helpers.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <dt-bindings/soc/google/google-thermal-def.h>

#include "thermal_cpm_mbox_helper_internal.h"

static struct tz_cdev_map lga_tz_cdev_table[] = {
	[0] = {
		.sensor_id = TZ_BIG,
		.hw_tzid = HW_THERMAL_ZONE_0,
		.cdev_id = HW_CDEV_BIG,
		.tz_name = "BIG"
	},
	[1] = {
		.sensor_id = TZ_BIG_MID,
		.hw_tzid = HW_THERMAL_ZONE_1,
		.cdev_id = HW_CDEV_BIG_MID,
		.tz_name = "BIG_MID"
	},
	[2] = {
		.sensor_id = TZ_MID,
		.hw_tzid = HW_THERMAL_ZONE_2,
		.cdev_id = HW_CDEV_MID,
		.tz_name = "MID"
	},
	[3] = {
		.sensor_id = TZ_LIT,
		.hw_tzid = HW_THERMAL_ZONE_3,
		.cdev_id = HW_CDEV_LIT,
		.tz_name = "LITTLE"
	},
	[4] = {
		.sensor_id = TZ_GPU,
		.hw_tzid = HW_THERMAL_ZONE_4,
		.cdev_id = HW_CDEV_GPU,
		.tz_name = "GPU"
	},
	[5] = {
		.sensor_id = TZ_TPU,
		.hw_tzid = HW_THERMAL_ZONE_5,
		.cdev_id = HW_CDEV_TPU,
		.tz_name = "TPU"
	},
	[6] = {
		.sensor_id = TZ_AUR,
		.hw_tzid = HW_THERMAL_ZONE_6,
		.cdev_id = HW_CDEV_AUR,
		.tz_name = "AUR"
	},
	[7] = {
		.sensor_id = TZ_ISP,
		.hw_tzid = HW_THERMAL_ZONE_7,
		.cdev_id = HW_CDEV_ISP,
		.tz_name = "ISP"
	},
	[8] = {
		.sensor_id = TZ_MEM,
		.hw_tzid = HW_THERMAL_ZONE_8,
		.cdev_id = HW_CDEV_MEM,
		.tz_name = "MEM"
	},
	[9] = {
		.sensor_id = TZ_AOC,
		.hw_tzid = HW_THERMAL_ZONE_9,
		.cdev_id = HW_CDEV_AOC,
		.tz_name = "AOC"
	},
};

static const struct thermal_cpm_mbox_platform_data lga_data = {
	.tz_max = ARRAY_SIZE(lga_tz_cdev_table),
	.tz_cdev_table = lga_tz_cdev_table,
};

static struct tz_cdev_map mbu_tz_cdev_table[] = {
	[0] = {
		.sensor_id = TZ_BIG,
		.hw_tzid = HW_THERMAL_ZONE_0,
		.cdev_id = HW_CDEV_BIG,
		.tz_name = "BIG"
	},
	[1] = {
		.sensor_id = TZ_MID,
		.hw_tzid = HW_THERMAL_ZONE_1,
		.cdev_id = HW_CDEV_MID,
		.tz_name = "MID"
	},
	[2] = {
		.sensor_id = TZ_MIDLL,
		.hw_tzid = HW_THERMAL_ZONE_2,
		.cdev_id = HW_CDEV_MIDLL,
		.tz_name = "MIDLL"
	},
	[3] = {
		.sensor_id = TZ_CME,
		.hw_tzid = HW_THERMAL_ZONE_3,
		.cdev_id = HW_CDEV_CME,
		.tz_name = "CME"
	},
	[4] = {
		.sensor_id = TZ_DSU,
		.hw_tzid = HW_THERMAL_ZONE_4,
		.cdev_id = HW_CDEV_CME,
		.tz_name = "DSU"
	},
	[5] = {
		.sensor_id = TZ_GPU,
		.hw_tzid = HW_THERMAL_ZONE_5,
		.cdev_id = HW_CDEV_GPU,
		.tz_name = "GPU"
	},
	[6] = {
		.sensor_id = TZ_TPU,
		.hw_tzid = HW_THERMAL_ZONE_6,
		.cdev_id = HW_CDEV_TPU,
		.tz_name = "TPU"
	},
	[7] = {
		.sensor_id = TZ_AUR,
		.hw_tzid = HW_THERMAL_ZONE_7,
		.cdev_id = HW_CDEV_AUR,
		.tz_name = "AUR"
	},
	[8] = {
		.sensor_id = TZ_ISPFE,
		.hw_tzid = HW_THERMAL_ZONE_8,
		.cdev_id = HW_CDEV_ISPFE,
		.tz_name = "ISPFE"
	},
	[9] = {
		.sensor_id = TZ_ISPBE,
		.hw_tzid = HW_THERMAL_ZONE_9,
		.cdev_id = HW_CDEV_ISPBE,
		.tz_name = "ISPBE"
	},
	[10] = {
		.sensor_id = TZ_C3P,
		.hw_tzid = HW_THERMAL_ZONE_10,
		.cdev_id = HW_CDEV_C3P,
		.tz_name = "C3P"
	},
	[11] = {
		.sensor_id = TZ_MEM,
		.hw_tzid = HW_THERMAL_ZONE_11,
		.cdev_id = HW_CDEV_MEM,
		.tz_name = "MEMSS"
	},
	[12] = {
		.sensor_id = TZ_AOSS,
		.hw_tzid = HW_THERMAL_ZONE_12,
		.cdev_id = HW_CDEV_AOSS,
		.tz_name = "AOSS"
	},
};

static const struct thermal_cpm_mbox_platform_data mbu_data = {
	.tz_max = ARRAY_SIZE(mbu_tz_cdev_table),
	.tz_cdev_table = mbu_tz_cdev_table,
};

static const struct of_device_id thermal_cpm_mbox_platform_table[] = {
	{
		.compatible = "google,lga",
		.data = &lga_data,
	},
	{
		.compatible = "google,malibu",
		.data = &mbu_data,
	},
	{},
};

int cpm_mbox_parse_soc_data(struct thermal_cpm_mbox_driver_data *drv_data)
{
	struct device_node *np;
	const struct of_device_id *match;

	np = of_find_node_by_path("/");
	if (!np)
		return -ENODEV;

	match = of_match_node(thermal_cpm_mbox_platform_table, np);
	of_node_put(np);

	if (!match)
		return -EINVAL;

	drv_data->soc_data = match->data;

	if (!drv_data->soc_data)
		return -EINVAL;

	return 0;
}

#define LOOKUP_TZ_CDEV_TABLE_AND_RETURN(drv_data, key, value, target) \
	do{ \
		for (int i = 0; i < drv_data->soc_data->tz_max; ++i) { \
			if (drv_data->soc_data->tz_cdev_table[i].key == value) { \
				*target = drv_data->soc_data->tz_cdev_table[i].target; \
				return 0; \
			} \
		} \
		return -EINVAL; \
	} while(0)

int sensor_id_to_hw_tzid(struct thermal_cpm_mbox_driver_data *drv_data, int sensor_id,
			   enum hw_thermal_zone_id *hw_tzid)
{
	if (!drv_data || !hw_tzid || sensor_id >= TZ_MAX || sensor_id < 0)
		return -EINVAL;

	LOOKUP_TZ_CDEV_TABLE_AND_RETURN(drv_data, sensor_id, sensor_id, hw_tzid);
}

int hw_tzid_to_rx_cb_type(struct thermal_cpm_mbox_driver_data *drv_data,
			  enum hw_thermal_zone_id hw_tzid, enum hw_dev_type *cdev_id)
{
	if (!drv_data || !cdev_id || hw_tzid >= HW_THERMAL_ZONE_MAX || hw_tzid < 0)
		return -EINVAL;

	LOOKUP_TZ_CDEV_TABLE_AND_RETURN(drv_data, hw_tzid, hw_tzid, cdev_id);
}

int hw_cdev_id_to_hw_tzid(struct thermal_cpm_mbox_driver_data *drv_data,
			  enum hw_dev_type cdev_id, enum hw_thermal_zone_id *hw_tzid)
{
	if (!drv_data || !hw_tzid || cdev_id > HW_CDEV_MAX || cdev_id < 0)
		return -EINVAL;

	LOOKUP_TZ_CDEV_TABLE_AND_RETURN(drv_data, cdev_id, cdev_id, hw_tzid);
}

int cpm_mbox_send_message(struct thermal_cpm_mbox_driver_data *drv_data,
			  struct cpm_iface_req *cpm_req)
{
	return cpm_send_message(drv_data->client, cpm_req);
}

static void cpm_mbox_rx_worker(struct work_struct *work)
{
	struct thermal_cpm_mbox_rx_work *rx = container_of(work, struct thermal_cpm_mbox_rx_work,
							   work);
	u32 payload[GOOG_MBA_PAYLOAD_SIZE] = {0};
	struct thermal_cpm_mbox_request *req;
	unsigned long flags;

	spin_lock_irqsave(&rx->rx_lock, flags);
	req = (struct thermal_cpm_mbox_request *)rx->data;
	switch (req->type) {
	case THERMAL_NTC_REQUEST:
		payload[0] = req->tzid;
		payload[1] = req->req_rsvd0;
		break;
	case THERMAL_STATE_NOTIFICATION:
	case THERMAL_REQUEST_THROTTLE:
	default:
		memcpy(payload, rx->data, sizeof(payload));
		break;
	}
	spin_unlock_irqrestore(&rx->rx_lock, flags);

	blocking_notifier_call_chain(&rx->notifier, 0, payload);
}

/* This func will be execute in irq context */
void cpm_mbox_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct cpm_iface_payload *cpm_msg = msg;
	struct thermal_cpm_mbox_response *resp = (struct thermal_cpm_mbox_response *)
						cpm_msg->payload;
	struct thermal_cpm_mbox_driver_data *drv_data = priv_data;
	struct thermal_cpm_mbox_rx_work *rx;
	enum hw_dev_type hw_rx_cb_type;
	int ret;
	unsigned long flags;

	switch (resp->type) {
	case THERMAL_REQUEST_THROTTLE:
	case THERMAL_STATE_NOTIFICATION:
		ret = hw_tzid_to_rx_cb_type(drv_data, resp->tzid, &hw_rx_cb_type);
		if (ret) {
			dev_err(drv_data->dev, "Invalid tzid: %u type: %u from CPM: ret=%d\n",
				resp->tzid, resp->type, ret);
			return;
		}
		break;
	case THERMAL_NTC_REQUEST:
		hw_rx_cb_type = HW_RX_CB_NTC;
		dev_dbg(drv_data->dev, "Received NTC IRQ.\n");
		break;
	default:
		dev_err(drv_data->dev, "Invalid type:%d.\n", resp->type);
		return;
	}

	rx = &drv_data->rx_work[hw_rx_cb_type];

	spin_lock_irqsave(&rx->rx_lock, flags);
	memcpy(rx->data, cpm_msg->payload, sizeof(rx->data));
	spin_unlock_irqrestore(&rx->rx_lock, flags);
	queue_work(system_highpri_wq, &rx->work);
}

int cpm_mbox_init(struct thermal_cpm_mbox_driver_data *drv_data)
{
	int ret;

	drv_data->client = cpm_iface_request_client(drv_data->dev,
						    CPM_SERVICE_ID_THERMAL,
						    cpm_mbox_rx_callback,
						    drv_data);
	if (IS_ERR(drv_data->client)) {
		ret = PTR_ERR(drv_data->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(drv_data->dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(drv_data->dev, "Failed to request cpm client err: %d\n",
				ret);
		return ret;
	}

	return 0;
}

void thermal_cpm_mbox_init_notifier(struct thermal_cpm_mbox_driver_data *drv_data)
{
	int i;

	for (i = 0; i < HW_DEV_MAX; ++i) {
		BLOCKING_INIT_NOTIFIER_HEAD(&drv_data->rx_work[i].notifier);
		spin_lock_init(&drv_data->rx_work[i].rx_lock);
		devm_work_autocancel(drv_data->dev, &drv_data->rx_work[i].work,
				     cpm_mbox_rx_worker);
	}
}

void thermal_cpm_mbox_free(struct thermal_cpm_mbox_driver_data *drv_data)
{
	if (!IS_ERR_OR_NULL(drv_data) && !IS_ERR_OR_NULL(drv_data->client))
		cpm_iface_free_client(drv_data->client);
}

int cpm_mbox_parse_device_tree(struct thermal_cpm_mbox_driver_data *drv_data)
{
	struct device *dev = drv_data->dev;
	struct device_node *np = dev->of_node;

	if (of_property_read_u32(np, "mba-dest-channel", &drv_data->remote_ch)) {
		dev_err(dev, "Failed to read mba-dest-channel.\n");
		thermal_cpm_mbox_free(drv_data);
		return -EINVAL;
	}

	return 0;
}

const char *get_tz_name(struct thermal_cpm_mbox_driver_data *drv_data,
			enum hw_thermal_zone_id hw_tzid)
{
	if (hw_tzid >= HW_THERMAL_ZONE_MAX || hw_tzid < 0)
		return NULL;

	for (int i = 0; i < drv_data->soc_data->tz_max; ++i)
		if (drv_data->soc_data->tz_cdev_table[i].hw_tzid == hw_tzid)
			return drv_data->soc_data->tz_cdev_table[i].tz_name;

	return NULL;
}
