// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
#include <soc/google/goog_cpm_service_ids.h>
#endif

#include "google_powerdashboard_iface.h"
#include "google_powerdashboard_helper.h"

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
static int google_powerdashboard_send_msg(struct google_powerdashboard *pd,
					  u32 pd_msg_type, u32 value0,
					  u32 value1)
{
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	int ret;

	cpm_req.msg_type = ONEWAY_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.tout_ms = powerdashboard_constants.mba_client_tx_timeout;
	cpm_req.dst_id = pd->remote_ch;

	req_msg.payload[0] = pd_msg_type;
	req_msg.payload[1] = value0;
	req_msg.payload[2] = value1;

	dev_dbg(pd->dev, " MSG:[0]:%#08x [1]:%#08x [2]:%#08x\n",
		req_msg.payload[0], req_msg.payload[1], req_msg.payload[2]);

	ret = cpm_send_message(pd->client, &cpm_req);
	if (ret < 0) {
		dev_err(pd->dev,
			"Failed to send request to CPM, payload=[%08X, %08X, %08X], ret=%d\n",
			req_msg.payload[0], req_msg.payload[1],
			req_msg.payload[2], ret);
		return ret;
	}

	return 0;
}
#else
static int google_powerdashboard_send_msg(struct google_powerdashboard *pd,
					  u32 pd_msg_type, u32 value0,
					  u32 value1)
{
	struct mba_data_package data_package;
	struct mba_transport_payload *payload;
	struct mba_queue_msg_hdr *hdr;
	int ret;

	payload = &data_package.payload;
	hdr = &payload->hdr;
	hdr->type = MBA_TRANSPORT_TYPE_MSG;
	hdr->dst = pd->remote_ch;

	payload->data[0] = pd_msg_type;
	payload->data[1] = value0;
	payload->data[2] = value1;

	dev_dbg(pd->dev, "MSG:[0]:%#08x [1]:%#08x [2]:%#08x\n",
		payload->data[0], payload->data[1], payload->data[2]);

	ret = mbox_send_message(pd->channel, &data_package);
	if (ret < 0) {
		dev_err(pd->dev, "Send message failed ret (%d)\n", ret);
		return ret;
	}

	return 0;
}
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
static int google_powerdashboard_send_req(struct google_powerdashboard *pd,
					  u32 pd_msg_type, u32 value0,
					  u32 value1,
					  struct cpm_iface_payload *resp_msg)
{
	struct cpm_iface_req cpm_req = { 0 };
	struct cpm_iface_payload req_msg;
	int ret;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = resp_msg;
	cpm_req.tout_ms = powerdashboard_constants.mba_client_tx_timeout;
	cpm_req.dst_id = pd->remote_ch;

	req_msg.payload[0] = pd_msg_type;
	req_msg.payload[1] = value0;
	req_msg.payload[2] = value1;

	dev_dbg(pd->dev, " REQ:[0]:%#08x [1]:%#08x [2]:%#08x\n",
		req_msg.payload[0], req_msg.payload[1], req_msg.payload[2]);

	ret = cpm_send_message(pd->client, &cpm_req);
	if (ret < 0) {
		dev_err(pd->dev, "Failed to send req message. ret (%d)\n", ret);
		return ret;
	}

	return 0;
}
#else
static int
google_powerdashboard_send_req(struct google_powerdashboard *pd,
			       u32 pd_msg_type, u32 value0, u32 value1,
			       struct mba_transport_payload *received_payload)
{
	struct mba_data_package data_package;
	struct mba_transport_payload *payload;
	struct mba_queue_msg_hdr *hdr;
	int ret;

	payload = &data_package.payload;
	hdr = &payload->hdr;
	hdr->type = MBA_TRANSPORT_TYPE_REQ;
	hdr->dst = pd->remote_ch;

	payload->data[0] = pd_msg_type;
	payload->data[1] = value0;
	payload->data[2] = value1;

	dev_dbg(pd->dev, "REQ:[0]:%#08x [1]:%#08x [2]:%#08x\n",
		payload->data[0], payload->data[1], payload->data[2]);

	ret = google_mba_send_msg_and_block(
		pd->dev, pd->channel, &data_package, received_payload,
		msecs_to_jiffies(powerdashboard_constants
					 .mba_client_tx_timeout));
	if (ret < 0) {
		dev_err(pd->dev, "Send request failed ret (%d)\n", ret);
		return ret;
	}

	return 0;
}
#endif

static ssize_t sampling_rate_show(struct device *dev,
				  struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd =
		(struct google_powerdashboard *)platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%u\n", pd->sampling_rate);
}

static ssize_t sampling_rate_store(struct device *dev,
				   struct device_attribute *devattr,
				   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	u32 sampling_rate;
	int ret;

	if (kstrtou32(buf, 10, &sampling_rate))
		return -EINVAL;

	if (sampling_rate < 10000 || sampling_rate > 10000000)
		return -EINVAL;

	mutex_lock(&pd->config_lock);
	pd->sampling_rate = sampling_rate;
	mutex_unlock(&pd->config_lock);

	ret = google_powerdashboard_send_msg(pd, POWER_DASH_SET_SAMPLING_RATE,
					     sampling_rate, 0);
	if (ret)
		return ret;

	return count;
}

static ssize_t platform_power_state_show(struct device *dev,
					 struct device_attribute *devattr,
					 char *buf)
{
	struct pd_platform_power_section *platform_power_section =
		powerdashboard_iface.sections.platform_power_section;
	const struct pd_attr_str *platform_power_state_names = powerdashboard_iface.attrs
							       .platform_power_state_names;
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);

	if (google_read_section(pd, PD_PLATFORM_POWER))
		return sysfs_emit(buf, "Read/Write Collision\n");

	if (platform_power_section->curr_power_state < platform_power_state_names->count)
		return sysfs_emit(buf, "current_power_state: %s\n",
				  platform_power_state_names->values[platform_power_section
								     ->curr_power_state]);
	else
		return sysfs_emit(buf, "current_power_state: %d\n",
				  (u32)platform_power_section->curr_power_state);
}

static ssize_t apc_power_ppu_show(struct device *dev,
				  struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	ssize_t len = 0;
	int i;

	if (google_read_section(pd, PD_APC_POWER))
		return sysfs_emit(buf, "Read/Write Collision\n");

	for (i = 0; i < powerdashboard_iface.attrs.apc_power_ppu_names->count;
	     ++i)
		len += sysfs_emit_at(buf, len, "%s: %u\n",
				     powerdashboard_iface.attrs
					     .apc_power_ppu_names->values[i],
				     powerdashboard_iface.sections
					     .apc_power_section->ppu[i]);

	return len;
}

static ssize_t cpm_m55_power_show(struct device *dev,
				  struct device_attribute *devattr, char *buf)
{
	struct pd_cpm_m55_power_section *cpm_section =
		powerdashboard_iface.sections.cpm_m55_power_section;
	const struct pd_attr_str *cpm_state_names = powerdashboard_iface.attrs
						    .cpm_m55_power_state_names;
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	ssize_t len = 0;
	int i;

	if (google_read_section(pd, PD_CPM_M55))
		return sysfs_emit(buf, "Read/Write Collision\n");

	if (cpm_section->current_state < cpm_state_names->count)
		len += sysfs_emit(buf, "current power_state: %s\n",
				  cpm_state_names->values[cpm_section->current_state]);
	else
		len += sysfs_emit(buf, "current power_state: %d\n",
				  (u32)cpm_section->current_state);

	for (i = 0; i < cpm_state_names->count; ++i) {
		len += sysfs_emit_at(buf, len, "power_state: %s\n",
				     powerdashboard_iface.attrs
					     .cpm_m55_power_state_names
					     ->values[i]);
		len += sysfs_emit_at(buf, len, "entry_count: %llu\n",
				     (u64)powerdashboard_iface.sections
					     .cpm_m55_power_section
					     ->power_state_res[i]
					     .entry_count);
		len += sysfs_emit_at(buf, len, "time_in_state: %llu (us)\n",
				     powerdashboard_iface.sections
					     .cpm_m55_power_section
					     ->power_state_res[i]
					     .time_in_state);
		len += sysfs_emit_at(buf, len, "last_entry_timestamp: %llu\n",
				     powerdashboard_iface.sections
					     .cpm_m55_power_section
					     ->power_state_res[i]
					     .last_entry_ts);
		len += sysfs_emit_at(buf, len, "last_exit_timestamp: %llu\n",
				     powerdashboard_iface.sections
					     .cpm_m55_power_section
					     ->power_state_res[i]
					     .last_exit_ts);
	}

	return len;
}

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
static ssize_t force_refresh_store(struct device *dev,
				   struct device_attribute *devattr,
				   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	struct cpm_iface_payload received_payload;
	int refresh;
	int ret;

	/* Converts string with base 10 to u32 */
	if (kstrtou32(buf, 10, &refresh) && refresh != 1)
		return -EINVAL;

	ret = google_powerdashboard_send_req(pd, POWER_DASH_FORCE_REFRESH, 0, 0,
					     &received_payload);
	if (ret)
		return ret;

	return count;
}
#else
static ssize_t force_refresh_store(struct device *dev,
				   struct device_attribute *devattr,
				   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);
	struct mba_transport_payload received_payload;
	int refresh;
	int ret;

	if (kstrtou32(buf, 10, &refresh) && refresh != 1)
		return -EINVAL;

	ret = google_powerdashboard_send_req(pd, POWER_DASH_FORCE_REFRESH, 0, 0,
					     &received_payload);
	if (ret)
		return ret;

	return count;
}
#endif

static DEVICE_ATTR_RW(sampling_rate);
static DEVICE_ATTR_RO(platform_power_state);
static DEVICE_ATTR_RO(apc_power_ppu);
static DEVICE_ATTR_RO(cpm_m55_power);
static DEVICE_ATTR_WO(force_refresh);

static struct attribute *google_powerdashboard_common_attrs[] = {
	&dev_attr_sampling_rate.attr,
	&dev_attr_platform_power_state.attr,
	&dev_attr_apc_power_ppu.attr,
	&dev_attr_cpm_m55_power.attr,
	&dev_attr_force_refresh.attr,
	NULL,
};

static struct attribute_group google_powerdashboard_common_group = {
	.attrs = google_powerdashboard_common_attrs,
};
static struct attribute_group google_powerdashboard_sswrp_group = {
	.attrs = google_powerdashboard_sswrp_attrs,
};
static struct attribute_group google_powerdashboard_power_state_group = {
	.attrs = google_powerdashboard_power_state_attrs,
};
static struct attribute_group google_powerdashboard_soc_specific_group = {
	.attrs = google_powerdashboard_soc_specific_attrs,
};
static const struct attribute_group *google_powerdashboard_groups[] = {
	&google_powerdashboard_common_group,
	&google_powerdashboard_sswrp_group,
	&google_powerdashboard_power_state_group,
	&google_powerdashboard_soc_specific_group,
	NULL
};

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
static int google_powerdashboard_setup(struct google_powerdashboard *pd)
{
	int i = 0, ret;
	struct cpm_iface_payload resp_msg;
	u32 section_id, section_base;

	/* Send config to CPM */
	ret = google_powerdashboard_send_msg(pd, POWER_DASH_SET_SAMPLING_RATE,
					     pd->sampling_rate, 0);
	if (ret)
		return ret;

	/* Get all section address */
	for (i = 0; i < PD_SECTION_NUM; ++i) {
		ret = google_powerdashboard_send_req(pd,
						     POWER_DASH_GET_SECT_ADDR,
						     i, 0, &resp_msg);
		if (ret)
			return ret;
		section_id = resp_msg.payload[1];
		section_base = resp_msg.payload[2];
		dev_info(pd->dev, "Get section_id: %d address: %u\n",
			 section_id, section_base);

		/* Section that is not defined shouldn't
		 * be used and should crash the system otherwise
		 */
		if (!section_base) {
			continue;
		}
		powerdashboard_iface.sections.section_bases[section_id] =
			devm_ioremap(pd->dev, section_base,
						 powerdashboard_iface.sections
							.section_sizes[section_id]);
		if (!powerdashboard_iface.sections.section_bases[section_id])
			return -EINVAL;
	}

	/* Tell CPM to start sampling power data*/
	ret = google_powerdashboard_send_msg(pd, POWER_DASH_START_SAMPLING, 0,
					     0);
	if (ret)
		return ret;

	return 0;
}
#else
static int google_powerdashboard_setup(struct google_powerdashboard *pd)
{
	int i = 0, ret;
	struct mba_transport_payload received_payload;
	u32 section_id, section_base;

	/* Send config to CPM */
	ret = google_powerdashboard_send_msg(pd, POWER_DASH_SET_SAMPLING_RATE,
					     pd->sampling_rate, 0);
	if (ret)
		return ret;

	/* Get all section address */
	for (i = 0; i < PD_SECTION_NUM; ++i) {
		ret = google_powerdashboard_send_req(pd,
						     POWER_DASH_GET_SECT_ADDR,
						     i, 0, &received_payload);
		if (ret)
			return ret;

		section_id = received_payload.data[1];
		section_base = received_payload.data[2];
		dev_info(pd->dev, "Get section_id: %d address: %d\n",
			 section_id, section_base);

		if (!section_base)
			return -EINVAL;

		powerdashboard_iface.sections.section_bases[section_id] =
			devm_ioremap(pd->dev, section_base,
				     powerdashboard_iface.sections
					     .section_sizes[section_id]);
		if (!powerdashboard_iface.sections.section_bases[section_id])
			return -EINVAL;
	}

	/* Tell CPM to start sampling power data*/
	ret = google_powerdashboard_send_msg(pd, POWER_DASH_START_SAMPLING, 0,
					     0);
	if (ret)
		return ret;

	return 0;
}
#endif

static int google_powerdashboard_parse_dt(struct device *dev,
					  struct google_powerdashboard *pd)
{
	struct device_node *np = dev->of_node;

	if (!np) {
		dev_err(dev, "Parse powerdashboard dts node failed\n");
		return -ENODEV;
	}

	if (of_property_read_u32(np, "mba-dest-channel", &pd->remote_ch)) {
		dev_err(dev, "Failed to read mba-dest-channel. Defaulting to CPM_AP_NS_POWER_DASH_MB_SERVICE = %d\n",
						CPM_AP_NS_POWER_DASH_MB_SERVICE);
		pd->remote_ch = CPM_AP_NS_POWER_DASH_MB_SERVICE;
	}

	if (of_property_read_u32(np, "sampling-rate-us", &pd->sampling_rate)) {
		dev_err(dev, "Read sampling rate fail\n");
		return -EINVAL;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
static int initialize_mailbox_client(struct google_powerdashboard *pd)
{
	int ret = 0;

	pd->client = cpm_iface_request_client(pd->dev,
					      CPM_AP_NS_POWER_DASH_MB_SERVICE,
					      NULL, NULL);

	if (IS_ERR(pd->client)) {
		ret = PTR_ERR(pd->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(pd->dev,
				"cpm interface not ready. Try again later\n");
		else
			dev_err(pd->dev,
				"Failed to request cpm mailbox client. Err: %d\n",
				ret);
		return ret;
	}

	return ret;
}
#else
static int initialize_mailbox_client(struct google_powerdashboard *pd)
{
	struct mbox_client *client;
	int ret = 0;

	client = &pd->client;
	client->dev = pd->dev;
	client->tx_block = true;
	client->tx_tout = 3000;

	pd->channel = mbox_request_channel(client, 0);
	if (IS_ERR(pd->channel)) {
		ret = PTR_ERR(pd->channel);
		dev_err(pd->dev, "Failed to request mailbox channel err %d.\n",
			ret);
		return ret;
	}

	return ret;
}
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MBA_CPM_INTERFACE)
static inline void
google_powerdashboard_mbox_free(struct google_powerdashboard *pd)
{
	cpm_iface_free_client(pd->client);
}
#else
static inline void
google_powerdashboard_mbox_free(struct google_powerdashboard *pd)
{
	mbox_free_channel(pd->channel);
}
#endif

static int google_powerdashboard_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_powerdashboard *pd;
	int ret;

	if (!plat_dvfs_helper_ready())
		return -EPROBE_DEFER;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->dev = dev;

	ret = google_powerdashboard_parse_dt(dev, pd);
	if (ret < 0)
		return ret;

	ret = initialize_mailbox_client(pd);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, pd);

	ret = google_powerdashboard_setup(pd);
	if (ret)
		goto free_mba;

	return 0;

free_mba:
	google_powerdashboard_mbox_free(pd);
	return ret;
}

static void google_powerdashboard_remove(struct platform_device *pdev)
{
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);

	google_powerdashboard_mbox_free(pd);
}

static const struct of_device_id google_powerdashboard_of_match_table[] = {
	{ .compatible = "google,power-dashboard" },
	{},
};
MODULE_DEVICE_TABLE(of, google_powerdashboard_of_match_table);

static struct platform_driver google_powerdashboard_driver = {
	.probe = google_powerdashboard_probe,
	.remove = google_powerdashboard_remove,
	.driver = {
		.name = "google-powerdashboard",
		.owner = THIS_MODULE,
		.dev_groups = google_powerdashboard_groups,
		.of_match_table = of_match_ptr(google_powerdashboard_of_match_table),
	},
};
module_platform_driver(google_powerdashboard_driver);

MODULE_AUTHOR("Konrad Korczynski");
MODULE_DESCRIPTION("Google power dashboard driver");
MODULE_LICENSE("GPL");
