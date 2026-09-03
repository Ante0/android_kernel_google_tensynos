// SPDX-License-Identifier: GPL-2.0
/*
 * lga_bcl_soc.c Google bcl driver - Utility
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include "da918x/da9188_limits.h"
#include "da918x/da9189_limits.h"
#include <mailbox/protocols/mba/cpm/common/bcl/bcl_service.h>
#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>
#include "../soc_defs.h"
#include "core_pmic/core_pmic_defs.h"

#define MAILBOX_SEND_TIMEOUT_MS	3000
#define OCP_BATFET_TIMER_MAXIMUM_MS	1000
#define OCP_BATFET_TIMER_MINIMUM_MS	0

static int google_bcl_mbox_request(struct bcl_device *bcl_dev, int idx)
{
	int ret = 0;

	bcl_dev->client = cpm_iface_request_client(bcl_dev->device, idx, NULL, NULL);

	if (IS_ERR(bcl_dev->client)) {
		ret = PTR_ERR(bcl_dev->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(bcl_dev->device,
				"cpm interface not ready. Try again later\n");
		else
			dev_err(bcl_dev->device,
				"Failed to request cpm mailbox client. Err: %d\n", ret);
		return ret;
	}
	return 0;
}

int google_bcl_cpm_send_cmd(struct bcl_device *bcl_dev, uint8_t command, uint8_t zone,
				uint8_t config, uint16_t value, uint32_t *response)
{
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	int ret;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MAILBOX_SEND_TIMEOUT_MS;
	cpm_req.dst_id = bcl_dev->remote_ch;
	req_msg.payload[0] = FIELD_PREP(MB_QUEUE_BCL_CMD_MASK, command) |
			FIELD_PREP(MB_QUEUE_BCL_CMD_ZONE_MASK, zone) |
			FIELD_PREP(MB_QUEUE_BCL_CMD_CONFIG_MASK, config) |
			FIELD_PREP(MB_QUEUE_BCL_CMD_VALUE_MASK, value);
	ret = cpm_send_message(bcl_dev->client, &cpm_req);
	if (ret < 0) {
		dev_err(bcl_dev->device, "Send message failed ret (%d)\n", ret);
		return -EINVAL;
	}
	if (response) {
		response[MB_BCL_IDX_RSP_STATUS] = resp_msg.payload[0];
		response[MB_BCL_IDX_RSP_DATA] = resp_msg.payload[1];
	}

	return 0;

}

void google_bcl_teardown_mailbox(struct bcl_device *bcl_dev)
{
	if (!IS_ERR_OR_NULL(bcl_dev->client))
		cpm_iface_free_client(bcl_dev->client);
}

static void google_bcl_setup_clock_div_ratio(struct bcl_device *bcl_dev)
{
	int ret, i;
	uint32_t response[2] = {0, 0};

	for (i = 0; i < SUBSYSTEM_SOURCE_MAX; i++) {
		if (i > AUR)
			break;
		ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, i,
					      MB_ZONE_CONFIG_DIV_4_DIV_RATIO,
					      (uint16_t)bcl_dev->core_conf[i].con_heavy, response);
		if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
			dev_err(bcl_dev->device, "Cannot set heavy clock ratio %d\n", i);
			break;
		}
		ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, i,
					      MB_ZONE_CONFIG_DIV_2_DIV_RATIO,
					      (uint16_t)bcl_dev->core_conf[i].con_light, response);
		if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
			dev_err(bcl_dev->device, "Cannot set light clock ratio %d\n", i);
			break;
		}
		ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, i,
					      MB_ZONE_CONFIG_MITIGATION_RESPONSE_EN,
					      (uint16_t)bcl_dev->core_conf[i].clkdivstep,
					      response);
		if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
			dev_err(bcl_dev->device, "Cannot set mitigation response en %d\n", i);
			break;
		}
		ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, i,
					      MB_ZONE_CONFIG_MITIGATION_RESPONSE_TYPE,
					      (uint16_t)bcl_dev->core_conf[i].mitigation_type,
					      response);
		if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
			dev_err(bcl_dev->device, "Cannot set mitigation response type %d\n", i);
			break;
		}
	}
}

static ssize_t google_bcl_mitigation_to_cpm(struct bcl_device *bcl_dev,
					    size_t size, bool sw_enabled,
					    bool hw_enabled)
{
	int i, ret;
	uint16_t value;
	uint32_t response[2] = {0, 0};

	for (i = 0; i <= SUBSYSTEM_SOURCE_MAX; i++) {
		if (i >= AUR)
			break;
		mutex_lock(&bcl_dev->sysreg_lock);
		value = (uint16_t)bcl_dev->core_conf[i].clkdivstep;
		if (!bcl_dev->hw_mitigation_enabled) {
			/* pre_uvlo(0x1), vdroop1(0x2), vdroop2(0x4) */
			/* pre_ocp(0x8), soft_pre_ocp(0x10)          */
			value &= ~(0x1 | 0x2 | 0x4 | 0x8 | 0x10);
		}
		mutex_unlock(&bcl_dev->sysreg_lock);

		ret = google_bcl_cpm_send_cmd(
			bcl_dev, MB_BCL_CMD_SET_CONFIG, i,
			MB_ZONE_CONFIG_MITIGATION_RESPONSE_EN, value, response);
		if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
			dev_err(bcl_dev->device,
				"Cannot set mitigation response en %d\n", i);
			return ret;
		}
	}
	return size;
}

void google_bcl_set_bat_throttle_timer(struct bcl_device *bcl_dev)
{
	int ret;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
				      MB_BCL_CONFIG_OCP_BAT_THROTTLE_TIMER_SET, 1, NULL);
	if (ret < 0)
		dev_err(bcl_dev->device, "Cannot cancel OCP THROTTLE timer\n");
}

void google_bcl_set_batfet_timer(struct bcl_device *bcl_dev)
{
	int ret;
	uint32_t response[2] = {0, 0};

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
			MB_BCL_CONFIG_OCP_BATFET_TIMER_SET, 1, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS])
		dev_err(bcl_dev->device, "Cannot cancel OCP BATFET timer\n");

}

void google_bcl_cancel_bat_throttle_timer(struct bcl_device *bcl_dev)
{
	int ret;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
				      MB_BCL_CONFIG_OCP_BAT_THROTTLE_TIMER_CANCEL, 1, NULL);
	if (ret < 0)
		dev_err(bcl_dev->device, "Cannot cancel OCP BAT THROTTLE timer\n");

}

void google_bcl_cancel_batfet_timer(struct bcl_device *bcl_dev)
{
	int ret;
	uint32_t response[2] = {0, 0};

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
			MB_BCL_CONFIG_OCP_BATFET_TIMER_CANCEL, 1, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS])
		dev_err(bcl_dev->device, "Cannot cancel OCP BATFET timer\n");

}

static void google_bcl_setup_batfet_timer(struct bcl_device *bcl_dev)
{
	int ret;
	uint32_t response[2] = {0, 0};

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
			MB_BCL_CONFIG_OCP_BATFET_TIMEOUT, bcl_dev->ocp_batfet_timeout, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS])
		dev_err(bcl_dev->device, "Cannot set OCP BATFET timeout\n");

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
			MB_BCL_CONFIG_OCP_BATFET_TIMEOUT_ENABLE,
			bcl_dev->ocp_batfet_timeout_enable, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS])
		dev_err(bcl_dev->device, "Cannot set OCP BATFET timeout enable\n");

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
				      MB_BCL_CONFIG_OCP_BAT_THROTTLE_TIMEOUT,
				      bcl_dev->ocp_bat_throttle_timeout, NULL);
	if (ret < 0)
		dev_err(bcl_dev->device, "Cannot set OCP BAT THROTTLE timeout\n");

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
				      MB_BCL_CONFIG_OCP_BAT_THROTTLE_TIMEOUT_ENABLE,
				      bcl_dev->ocp_bat_throttle_timeout_enable, NULL);
	if (ret < 0)
		dev_err(bcl_dev->device, "Cannot set OCP BAT THROTTLE timeout enable\n");
}

static void google_bcl_setup_core_pmic_mb(struct work_struct *work)
{
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	struct bcl_device *bcl_dev = container_of(work, struct bcl_device,
						  mailbox_init_work.work);

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_INIT, 0, 0, 0, NULL);
	google_bcl_setup_clock_div_ratio(bcl_dev);
	google_bcl_setup_batfet_timer(bcl_dev);
	schedule_delayed_work(&bcl_dev->init_qos_work, msecs_to_jiffies(30 * TIMEOUT_1000MS));
	if (bcl_dev->mailbox_init_work.work.func != NULL)
		cancel_delayed_work(&bcl_dev->mailbox_init_work);
#endif
}

void google_bcl_setup_irq_mb(struct bcl_device *bcl_dev)
{
	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_INIT, 0, 0, 0, NULL);
}

int google_bcl_setup_mailbox(struct bcl_device *bcl_dev)
{
	int ret;
	struct device_node *np = bcl_dev->device->of_node;

	ret = of_property_read_u32(np, "mba-pmic-dest-channel", &bcl_dev->remote_pmic_ch);
	if (ret < 0) {
		dev_err(bcl_dev->device, "Failed to read mba-pmic-dest-channel\n");
		return ret;
	}
	ret = of_property_read_u32(np, "mba-dest-service-id", &bcl_dev->remote_ch);
	if (ret < 0) {
		dev_err(bcl_dev->device, "Failed to read mba-dest-service-id\n");
		return ret;
	}

	ret = google_bcl_mbox_request(bcl_dev, bcl_dev->remote_ch);
	if (ret < 0) {
		dev_err(bcl_dev->device, "Failed to request remote channel\n");
		return ret;
	}

	ret = core_pmic_mbox_request(bcl_dev);
	if (ret < 0) {
		dev_err(bcl_dev->device, "Failed to request remote pmic channel\n");
		return ret;
	}

	schedule_delayed_work(&bcl_dev->mailbox_init_work, msecs_to_jiffies(TIMEOUT_1MS));
	/* Reinitialize CPM IRQ */
	return 0;
}

static uint32_t get_prop_u32(struct device_node *np, const char *prop)
{
	uint32_t val;

	return of_property_read_u32(np, prop, &val) ? 0 : val;
}

void google_bcl_parse_clk_div_dtree(struct bcl_device *bcl_dev)
{
	struct device_node *np = bcl_dev->device->of_node;

	WARN_ON_ONCE(bcl_dev->mailbox_init_work.work.func);
	INIT_DELAYED_WORK(&bcl_dev->mailbox_init_work, google_bcl_setup_core_pmic_mb);
	WARN_ON_ONCE(bcl_dev->init_qos_work.work.func);
	INIT_DELAYED_WORK(&bcl_dev->init_qos_work, google_bcl_setup_qos_work);
	bcl_dev->ocp_batfet_timeout_enable = of_property_read_bool(np, "ocp_batfet_timeout_en");
	bcl_dev->ocp_batfet_timeout = get_prop_u32(np, "ocp_batfet_timeout");
	bcl_dev->core_conf[AUR].con_heavy = get_prop_u32(np, "aur_con_heavy");
	bcl_dev->core_conf[AUR].con_light = get_prop_u32(np, "aur_con_light");
	bcl_dev->core_conf[TPU].con_heavy = get_prop_u32(np, "tpu_con_heavy");
	bcl_dev->core_conf[TPU].con_light = get_prop_u32(np, "tpu_con_light");
	bcl_dev->core_conf[GPU].con_heavy = get_prop_u32(np, "gpu_con_heavy");
	bcl_dev->core_conf[GPU].con_light = get_prop_u32(np, "gpu_con_light");
	bcl_dev->core_conf[CPU1A].con_heavy = get_prop_u32(np, "cpu1a_con_heavy");
	bcl_dev->core_conf[CPU1A].con_light = get_prop_u32(np, "cpu1a_con_light");
	bcl_dev->core_conf[CPU1B].con_heavy = get_prop_u32(np, "cpu1b_con_heavy");
	bcl_dev->core_conf[CPU1B].con_light = get_prop_u32(np, "cpu1b_con_light");
	bcl_dev->core_conf[CPU2].con_heavy = get_prop_u32(np, "cpu2_con_heavy");
	bcl_dev->core_conf[CPU2].con_light = get_prop_u32(np, "cpu2_con_light");
	bcl_dev->core_conf[CPU0].con_heavy = get_prop_u32(np, "cpu0_con_heavy");
	bcl_dev->core_conf[CPU0].con_light = get_prop_u32(np, "cpu0_con_light");

	bcl_dev->core_conf[AUR].clkdivstep = get_prop_u32(np, "aur_mitigation_res_en");
	bcl_dev->core_conf[GPU].clkdivstep = get_prop_u32(np, "gpu_mitigation_res_en");
	bcl_dev->core_conf[TPU].clkdivstep = get_prop_u32(np, "tpu_mitigation_res_en");
	bcl_dev->core_conf[CPU1A].clkdivstep = get_prop_u32(np, "cpu1a_mitigation_res_en");
	bcl_dev->core_conf[CPU1B].clkdivstep = get_prop_u32(np, "cpu1b_mitigation_res_en");
	bcl_dev->core_conf[CPU2].clkdivstep = get_prop_u32(np, "cpu2_mitigation_res_en");
	bcl_dev->core_conf[CPU0].clkdivstep = get_prop_u32(np, "cpu0_mitigation_res_en");

	bcl_dev->core_conf[AUR].mitigation_type = get_prop_u32(np, "aur_mitigation_type");
	bcl_dev->core_conf[GPU].mitigation_type = get_prop_u32(np, "gpu_mitigation_type");
	bcl_dev->core_conf[TPU].mitigation_type = get_prop_u32(np, "tpu_mitigation_type");
	bcl_dev->core_conf[CPU1A].mitigation_type = get_prop_u32(np, "cpu1a_mitigation_type");
	bcl_dev->core_conf[CPU1B].mitigation_type = get_prop_u32(np, "cpu1b_mitigation_type");
	bcl_dev->core_conf[CPU2].mitigation_type = get_prop_u32(np, "cpu2_mitigation_type");
	bcl_dev->core_conf[CPU0].mitigation_type = get_prop_u32(np, "cpu0_mitigation_type");

	bcl_dev->cpu_cluster[QOS_CPU0] = get_prop_u32(np, "cpu0_cluster");
	bcl_dev->cpu_cluster[QOS_CPU1A] = get_prop_u32(np, "cpu1a_cluster");
	bcl_dev->cpu_cluster[QOS_CPU1B] = get_prop_u32(np, "cpu1b_cluster");
	bcl_dev->cpu_cluster[QOS_CPU2] = get_prop_u32(np, "cpu2_cluster");
}

ssize_t safe_emit_bcl_cnt(char *buf, struct bcl_zone *zone)
{
	uint32_t response[2] = {0, 0};
	struct bcl_device *bcl_dev;

	if (!zone || WARN_ON_ONCE(!zone->parent))
		return sysfs_emit(buf, "0\n");
	bcl_dev = zone->parent;
	google_bcl_cpm_send_cmd(zone->parent, MB_BCL_CMD_GET_COUNT, 0, 0, zone->idx, response);
	return sysfs_emit(buf, "%d\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t safe_emit_pre_evt_cnt(char *buf, struct bcl_zone *zone)
{
	struct bcl_device *bcl_dev;
	uint32_t ret;

	if (!zone || WARN_ON_ONCE(!zone->parent))
		return sysfs_emit(buf, "0\n");
	bcl_dev = zone->parent;
	ret = core_pmic_get_pre_evt_cnt(bcl_dev, zone->idx);
	if (ret < 0)
		return sysfs_emit(buf, "0\n");
	return sysfs_emit(buf, "%d\n", ret);
}

unsigned int google_get_db(struct bcl_device *data, enum MPMM_SOURCE index)
{
	return -EINVAL;
}

int google_set_db(struct bcl_device *data, unsigned int value, enum MPMM_SOURCE index)
{
	return -EINVAL;
}

ssize_t get_clk_ratio(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx, char *buf, int sub_idx)
{
	uint32_t response[2] = {0, 0};
	uint8_t mode;

	mode = idx == heavy ? MB_ZONE_CONFIG_DIV_4_DIV_RATIO : MB_ZONE_CONFIG_DIV_2_DIV_RATIO;

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, sub_idx,
				mode, 0, response);
	return sysfs_emit(buf, "%#x\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t get_clk_stats(struct bcl_device *bcl_dev, int idx, char *buf)
{
	uint32_t response[2] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, idx,
				MB_ZONE_CONFIG_DIV_2_DIV_STATUS, 0, response);
	return sysfs_emit(buf, "%#x\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t set_clk_div(struct bcl_device *bcl_dev, int idx, const char *buf, size_t size)
{
	uint32_t value;
	int ret;
	uint32_t response[2] = {0, 0};

	ret = kstrtou32(buf, 16, &value);
	if (ret)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, idx,
				      MB_ZONE_CONFIG_DIV_2_CTRL_MODE, (uint16_t)value, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
		dev_err(bcl_dev->device, "Cannot set clock div %d\n", idx);
		return -EINVAL;
	}
	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, idx,
				      MB_ZONE_CONFIG_DIV_4_CTRL_MODE, (uint16_t)value, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
		dev_err(bcl_dev->device, "Cannot set clock div %d\n", idx);
		return -EINVAL;
	}
	return size;
}

ssize_t get_clk_div(struct bcl_device *bcl_dev, int idx, char *buf)
{
	uint32_t response[2] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, idx,
				MB_ZONE_CONFIG_DIV_2_CTRL_MODE, 0, response);
	return sysfs_emit(buf, "%#x\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t set_clk_ratio(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx,
		      const char *buf, size_t size, int sub_idx)
{
	uint32_t value;
	int ret;
	uint8_t mode;
	uint32_t response[2] = {0, 0};

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;

	mode = idx == heavy ? MB_ZONE_CONFIG_DIV_4_DIV_RATIO : MB_ZONE_CONFIG_DIV_2_DIV_RATIO;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, sub_idx,
				      mode, (uint16_t)value, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
		dev_err(bcl_dev->device, "Cannot set clock ratio %d\n", idx);
		return -EINVAL;
	}
	return size;
}

ssize_t set_hw_mitigation(struct bcl_device *bcl_dev, const char *buf,
			  size_t size)
{
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	/* Ensure hw mitigation enabled is read correctly */
	if (smp_load_acquire(&bcl_dev->hw_mitigation_enabled) == value)
		return size;

	/* Ensure hw mitigation is set correctly */
	smp_store_release(&bcl_dev->hw_mitigation_enabled, value);

	return google_bcl_mitigation_to_cpm(bcl_dev, size,
					    bcl_dev->sw_mitigation_enabled,
					    bcl_dev->hw_mitigation_enabled);
}

ssize_t set_sw_mitigation(struct bcl_device *bcl_dev, const char *buf,
			  size_t size)
{
	bool value;
	int ret, i;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	/* Ensure hw mitigation enabled is read correctly */
	if (smp_load_acquire(&bcl_dev->sw_mitigation_enabled) == value)
		return size;

	/* Ensure hw mitigation is set correctly */
	smp_store_release(&bcl_dev->sw_mitigation_enabled, value);

	/* Control IRQ from ifpmic */
	if (value) {
		for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
			if (bcl_dev->zone[i] && i != BATOILO)
				enable_irq(bcl_dev->zone[i]->bcl_irq);
		}
	} else {
		for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
			if (bcl_dev->zone[i] && i != BATOILO)
				disable_irq(bcl_dev->zone[i]->bcl_irq);
		}
	}

	return google_bcl_mitigation_to_cpm(bcl_dev, size,
					    bcl_dev->sw_mitigation_enabled,
					    bcl_dev->hw_mitigation_enabled);
}

ssize_t set_mitigation_res_en(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx)
{
	uint32_t value;
	int ret;
	uint32_t response[2] = {0, 0};

	ret = kstrtou32(buf, 16, &value);
	if (ret)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, idx,
				      MB_ZONE_CONFIG_MITIGATION_RESPONSE_EN, (uint16_t)value,
				      response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
		dev_err(bcl_dev->device, "Cannot set mitigation response en %d\n", idx);
		return -EINVAL;
	}
	return size;
}

ssize_t get_mitigation_res_en(struct bcl_device *bcl_dev, int idx, char *buf)
{
	uint32_t response[2] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, idx,
				MB_ZONE_CONFIG_MITIGATION_RESPONSE_EN, 0, response);
	return sysfs_emit(buf, "%#x\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t set_mitigation_res_type(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx)
{
	uint32_t value;
	int ret;
	uint32_t response[2] = {0, 0};

	ret = kstrtou32(buf, 16, &value);
	if (ret)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, idx,
				      MB_ZONE_CONFIG_MITIGATION_RESPONSE_TYPE, (uint16_t)value,
				      response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
		dev_err(bcl_dev->device, "Cannot set mitigation response type %d\n", idx);
		return -EINVAL;
	}
	return size;
}

ssize_t get_mitigation_res_type(struct bcl_device *bcl_dev, int idx, char *buf)
{
	uint32_t response[2] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, idx,
				MB_ZONE_CONFIG_MITIGATION_RESPONSE_TYPE, 0, response);
	return sysfs_emit(buf, "%#x\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t set_mitigation_res_hyst(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx)
{
	uint32_t value;
	int ret;
	uint32_t response[2] = {0, 0};

	ret = kstrtou32(buf, 16, &value);
	if (ret)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, idx,
				      MB_ZONE_CONFIG_MITIGATION_RESPONSE_HYST, (uint16_t)value,
				      response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS]) {
		dev_err(bcl_dev->device, "Cannot set mitigation response hyst %d\n", idx);
		return -EINVAL;
	}
	return size;
}

ssize_t get_mitigation_res_hyst(struct bcl_device *bcl_dev, int idx, char *buf)
{
	uint32_t response[2] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, idx,
				MB_ZONE_CONFIG_MITIGATION_RESPONSE_HYST, 0, response);
	return sysfs_emit(buf, "%#x\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t get_ocp_bat_throttle_timeout_enable(struct bcl_device *bcl_dev, char *buf)
{
	uint32_t response[] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, 0,
				MB_BCL_CONFIG_OCP_BAT_THROTTLE_TIMEOUT_ENABLE, 0, response);
	return sysfs_emit(buf, "%d\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t set_ocp_bat_throttle_timeout_enable(struct bcl_device *bcl_dev,
					    const char *buf, size_t size)
{
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return -EINVAL;
	if (bcl_dev->ocp_bat_throttle_timeout_enable == value)
		return size;

	bcl_dev->ocp_bat_throttle_timeout_enable = value;
	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
				      MB_BCL_CONFIG_OCP_BAT_THROTTLE_TIMEOUT_ENABLE,
				      (bool)value, NULL);
	if (ret < 0)
		dev_err(bcl_dev->device, "Cannot set OCP BAT THROTTLE timeout enable\n");
	return size;
}

ssize_t get_ocp_batfet_timeout_enable(struct bcl_device *bcl_dev, char *buf)
{
	uint32_t response[2] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, 0,
			MB_BCL_CONFIG_OCP_BATFET_TIMEOUT_ENABLE, 0, response);
	return sysfs_emit(buf, "%d\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t set_ocp_batfet_timeout_enable(struct bcl_device *bcl_dev, const char *buf, size_t size)
{
	bool value;
	int ret;
	uint32_t response[2] = {0, 0};

	ret = kstrtobool(buf, &value);
	if (ret)
		return -EINVAL;
	if (bcl_dev->ocp_batfet_timeout_enable == value)
		return size;

	bcl_dev->ocp_batfet_timeout_enable = value;
	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
			MB_BCL_CONFIG_OCP_BATFET_TIMEOUT_ENABLE, (bool)value, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS])
		dev_err(bcl_dev->device, "Cannot set OCP BATFET timeout enable\n");
	return size;
}

ssize_t get_ocp_bat_throttle_timeout(struct bcl_device *bcl_dev, char *buf)
{
	uint32_t response[] = {0, 0};

	/* Mailbox payload only supports sending value of size 16bits */
	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, 0,
				MB_BCL_CONFIG_OCP_BAT_THROTTLE_TIMEOUT, 0, response);
	return sysfs_emit(buf, "%ums\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t get_ocp_batfet_timeout(struct bcl_device *bcl_dev, char *buf)
{
	uint32_t response[2] = {0, 0};

	/* Mailbox payload only supports sending value of size 16bits */
	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CONFIG, 0,
			MB_BCL_CONFIG_OCP_BATFET_TIMEOUT, 0, response);
	return sysfs_emit(buf, "%ums\n", response[MB_BCL_IDX_RSP_DATA]);
}

ssize_t set_ocp_bat_throttle_timeout(struct bcl_device *bcl_dev, const char *buf, size_t size)
{
	uint32_t value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return -EINVAL;
	if (value > OCP_BATFET_TIMER_MAXIMUM_MS || value < OCP_BATFET_TIMER_MINIMUM_MS)
		return -EINVAL;
	if (bcl_dev->ocp_bat_throttle_timeout == value)
		return size;

	bcl_dev->ocp_bat_throttle_timeout = value;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
				      MB_BCL_CONFIG_OCP_BAT_THROTTLE_TIMEOUT,
				      bcl_dev->ocp_bat_throttle_timeout, NULL);
	if (ret < 0)
		dev_err(bcl_dev->device, "Cannot set OCP BAT Throttle timeout\n");
	return size;
}

ssize_t set_ocp_batfet_timeout(struct bcl_device *bcl_dev, const char *buf, size_t size)
{
	uint32_t value;
	int ret;
	uint32_t response[2] = {0, 0};

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return -EINVAL;
	if (value > OCP_BATFET_TIMER_MAXIMUM_MS || value < OCP_BATFET_TIMER_MINIMUM_MS)
		return -EINVAL;
	if (bcl_dev->ocp_batfet_timeout == value)
		return size;

	bcl_dev->ocp_batfet_timeout = value;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
			MB_BCL_CONFIG_OCP_BATFET_TIMEOUT, bcl_dev->ocp_batfet_timeout, response);
	if (ret < 0 || response[MB_BCL_IDX_RSP_STATUS])
		dev_err(bcl_dev->device, "Cannot set OCP BATFET timeout\n");
	return size;
}

int google_init_vd_soc(struct bcl_device *bcl_dev)
{
	int ret;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_CONFIG, 0,
				      MB_BCL_CONFIG_VDROOP_DMUX_SET, 1, NULL);
	if (ret < 0)
		dev_err(bcl_dev->device, "Cannot set VDROOP DMUX");

	return ret;
}
