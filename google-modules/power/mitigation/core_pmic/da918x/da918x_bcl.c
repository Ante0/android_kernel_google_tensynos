// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 *
 */

#include "da9188_limits.h"
#include "da9189_limits.h"
#include <kunit/visibility.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/types.h>
#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>
#include <mailbox/protocols/mba/cpm/common/bcl/bcl_service.h>
#include <google_odpm.h>

#include "core_pmic_defs.h"
#include "da918x.h"

#define BYTES_IN_UINT64 (sizeof(uint64_t))
#define SYS_EVT_ONE_BYTE 1
#define INIT_BBAT_MAX_COUNT 5
#define INIT_MAIN_ODPM_MAX_COUNT 5

int google_bcl_configure_modem(struct bcl_device *bcl_dev)
{
	/* TODO fill out */
	return 0;
}

static int retrieve_pre_uvlo_lvl(u16 reg)
{
	return DA9188_PRE_UVLO_MIN + reg * DA9188_PRE_UVLO_STEP;
}

VISIBLE_IF_KUNIT u32 convert_pre_uvlo_lvl(int value)
{
	u8 rise_th, fall_th;

	if (value < DA9188_PRE_UVLO_MIN || value > DA9188_PRE_UVLO_MAX)
		return 0;
	fall_th = (value - DA9188_PRE_UVLO_MIN) / DA9188_PRE_UVLO_STEP;
	if (fall_th > 2)
		rise_th = fall_th - 2;
	else
		rise_th = 0;
	return (rise_th << 4) | fall_th;
}
EXPORT_SYMBOL_IF_KUNIT(convert_pre_uvlo_lvl);

VISIBLE_IF_KUNIT int convert_pre_ocp_lvl(int value, int idx, u32 *output)
{
	int pre_ocp_lvl = 0x0;

	switch (idx) {
	case PRE_OCP_CPU1:
	case PRE_OCP_CPU2:
	case PRE_OCP_GPU:
	case PRE_OCP_AUR:
	case PRE_OCP_MM:
	case PRE_OCP_INFRA:
		if ((value < PRE_OCP_MIN) || (value > DA9188_PRE_OCP_B2M_LIMIT))
			return -EINVAL;
		pre_ocp_lvl = (DA9188_PRE_OCP_B2M_LIMIT - value - 1) / PRE_OCP_STEP;
		break;
	case PRE_OCP_TPU:
		if ((value < PRE_OCP_TPU_MIN) || (value > DA9188_PRE_OCP_B7M_LIMIT))
			return -EINVAL;
		pre_ocp_lvl = (DA9188_PRE_OCP_B7M_LIMIT - value - 9) / PRE_OCP_TPU_STEP;
		break;
	case SOFT_PRE_OCP_CPU1:
	case SOFT_PRE_OCP_CPU2:
	case SOFT_PRE_OCP_GPU:
	case SOFT_PRE_OCP_AUR:
	case SOFT_PRE_OCP_MM:
	case SOFT_PRE_OCP_INFRA:
		if ((value < SOFT_PRE_OCP_MIN) || (value > SOFT_PRE_OCP_MAX))
			return -EINVAL;
		pre_ocp_lvl = (SOFT_PRE_OCP_MAX - value - 1) / PRE_OCP_STEP;
		break;
	case SOFT_PRE_OCP_TPU:
		if ((value < SOFT_PRE_OCP_TPU_MIN) || (value > SOFT_PRE_OCP_TPU_MAX))
			return -EINVAL;
		pre_ocp_lvl = (SOFT_PRE_OCP_TPU_MAX - value - 9) / PRE_OCP_TPU_STEP;
		break;
	default:
		return -EINVAL;
	};

	if (pre_ocp_lvl < 0)
		pre_ocp_lvl = 0;
	*output = (pre_ocp_lvl | pre_ocp_lvl << 4);
	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(convert_pre_ocp_lvl);

static int retrieve_pre_ocp_lvl(struct bcl_device *bcl_dev, int idx, u16 reg,
				enum PRE_OCP_RANGE range)
{
	int step;
	int limit;
	int offset = (range == SOFT_RANGE) ? SOFT_POCP_OFFSET : 0;

	switch (idx) {
	case PRE_OCP_CPU1:
	case SOFT_PRE_OCP_CPU1:
		limit = DA9188_PRE_OCP_B3M_LIMIT;
		step = PRE_OCP_STEP;
		break;
	case PRE_OCP_CPU2:
	case SOFT_PRE_OCP_CPU2:
		limit = DA9188_PRE_OCP_B2M_LIMIT;
		step = PRE_OCP_STEP;
		break;
	case PRE_OCP_GPU:
	case SOFT_PRE_OCP_GPU:
		limit = DA9189_PRE_OCP_B2S_LIMIT;
		step = PRE_OCP_STEP;
		break;
	case PRE_OCP_AUR:
	case SOFT_PRE_OCP_AUR:
		limit = DA9188_PRE_OCP_B5M_LIMIT;
		step = PRE_OCP_AUR_STEP;
		break;
	case PRE_OCP_MM:
	case SOFT_PRE_OCP_MM:
		limit = DA9189_PRE_OCP_B1S_LIMIT;
		step = PRE_OCP_MM_STEP;
		break;
	case PRE_OCP_INFRA:
	case SOFT_PRE_OCP_INFRA:
		limit = DA9189_PRE_OCP_B9S_LIMIT;
		step = PRE_OCP_INFRA_STEP;
		break;
	case PRE_OCP_INFRA_GPU:
	case SOFT_PRE_OCP_INFRA_GPU:
		limit = DA9189_PRE_OCP_B10S_LIMIT;
		step = PRE_OCP_INFRA_STEP;
		break;
	case PRE_OCP_TPU:
	case SOFT_PRE_OCP_TPU:
		limit = DA9188_PRE_OCP_B7M_LIMIT;
		step = PRE_OCP_TPU_STEP;
		break;
	default:
		return -EINVAL;
	};

	return limit - ((reg + 1 + offset) * step);
}

static u16 retrieve_pre_ocp_conf4_addr(int idx)
{
	switch (idx) {
	case SOFT_PRE_OCP_CPU1:
	case PRE_OCP_CPU1:
		return DA9188_B3M_CONF4;
	case SOFT_PRE_OCP_CPU2:
	case PRE_OCP_CPU2:
		return DA9188_B2M_CONF4;
	case SOFT_PRE_OCP_AUR:
	case PRE_OCP_AUR:
		return DA9188_B5M_CONF4;
	case SOFT_PRE_OCP_TPU:
	case PRE_OCP_TPU:
		return DA9188_B7M_CONF4;
	case SOFT_PRE_OCP_GPU:
	case PRE_OCP_GPU:
		return DA9189_B2S_CONF4;
	case SOFT_PRE_OCP_MM:
	case PRE_OCP_MM:
		return DA9189_B1S_CONF4;
	case SOFT_PRE_OCP_INFRA:
	case PRE_OCP_INFRA:
		return DA9189_B9S_CONF4;
	case SOFT_PRE_OCP_INFRA_GPU:
	case PRE_OCP_INFRA_GPU:
		return DA9189_B10S_CONF4;
	default:
		return 0;
	}
}

static enum PRE_OCP_TYPE retrieve_pre_ocp_type(int idx)
{
	switch (idx) {
	case SOFT_PRE_OCP_CPU1:
	case SOFT_PRE_OCP_CPU2:
	case SOFT_PRE_OCP_AUR:
	case SOFT_PRE_OCP_TPU:
	case SOFT_PRE_OCP_GPU:
	case SOFT_PRE_OCP_MM:
	case SOFT_PRE_OCP_INFRA:
	case SOFT_PRE_OCP_INFRA_GPU:
		return SOFT_PRE_OCP;
	case PRE_OCP_CPU1:
	case PRE_OCP_CPU2:
	case PRE_OCP_AUR:
	case PRE_OCP_TPU:
	case PRE_OCP_GPU:
	case PRE_OCP_MM:
	case PRE_OCP_INFRA:
	case PRE_OCP_INFRA_GPU:
		return NORMAL_PRE_OCP;
	default:
		return 0;
	}
}

static u16 retrieve_pre_ocp_addr(int idx, int *pmic)
{
	switch (idx) {
	case SOFT_PRE_OCP_CPU1:
		*pmic = CORE_MAIN_PMIC;
		return DA9188_PRE_OCP_B3M_CONF2;
	case SOFT_PRE_OCP_CPU2:
		*pmic = CORE_MAIN_PMIC;
		return DA9188_PRE_OCP_B2M_CONF2;
	case SOFT_PRE_OCP_AUR:
		*pmic = CORE_MAIN_PMIC;
		return DA9188_PRE_OCP_B5M_CONF2;
	case SOFT_PRE_OCP_TPU:
		*pmic = CORE_MAIN_PMIC;
		return DA9188_PRE_OCP_B7M_CONF2;
	case SOFT_PRE_OCP_GPU:
		*pmic = CORE_SUB_PMIC;
		return DA9189_PRE_OCP_B2S_CONF2;
	case SOFT_PRE_OCP_MM:
		*pmic = CORE_SUB_PMIC;
		return DA9189_PRE_OCP_B1S_CONF2;
	case SOFT_PRE_OCP_INFRA:
		*pmic = CORE_SUB_PMIC;
		return DA9189_PRE_OCP_B9S_CONF2;
	case SOFT_PRE_OCP_INFRA_GPU:
		*pmic = CORE_SUB_PMIC;
		return DA9189_PRE_OCP_B10S_CONF2;
	case PRE_OCP_CPU1:
		*pmic = CORE_MAIN_PMIC;
		return DA9188_PRE_OCP_B3M_CONF3;
	case PRE_OCP_CPU2:
		*pmic = CORE_MAIN_PMIC;
		return DA9188_PRE_OCP_B2M_CONF3;
	case PRE_OCP_AUR:
		*pmic = CORE_MAIN_PMIC;
		return DA9188_PRE_OCP_B5M_CONF3;
	case PRE_OCP_TPU:
		*pmic = CORE_MAIN_PMIC;
		return DA9188_PRE_OCP_B7M_CONF3;
	case PRE_OCP_GPU:
		*pmic = CORE_SUB_PMIC;
		return DA9189_PRE_OCP_B2S_CONF3;
	case PRE_OCP_MM:
		*pmic = CORE_SUB_PMIC;
		return DA9189_PRE_OCP_B1S_CONF3;
	case PRE_OCP_INFRA:
		*pmic = CORE_SUB_PMIC;
		return DA9189_PRE_OCP_B9S_CONF3;
	case PRE_OCP_INFRA_GPU:
		*pmic = CORE_SUB_PMIC;
		return DA9189_PRE_OCP_B10S_CONF3;
	default:
		return 0;
	}
}

static enum PRE_OCP_RANGE retrieve_pre_ocp_range(struct bcl_device *bcl_dev,
						 int idx, int pmic)
{
	int ret;
	struct mailbox_data req_data, resp_data;
	enum PRE_OCP_TYPE type = retrieve_pre_ocp_type(idx);
	u32 mask = (type == SOFT_PRE_OCP) ? SOFT_PRE_OCP_RANGE_MASK :
					    PRE_OCP_RANGE_MASK;
	u16 addr = retrieve_pre_ocp_conf4_addr(idx);

	req_data.data[0] = pmic;
	req_data.data[1] = 1; /* Only read 1 byte. */
	ret = da9188_mfd_mbox_send_req_blocking_read(
		bcl_dev->device, &bcl_dev->pmic_mbox, bcl_dev->remote_pmic_ch,
		MB_PMIC_TARGET_REGISTER, MB_REG_CMD_GET_PMIC_REG_BURST, addr,
		req_data, &resp_data);
	if (unlikely(ret))
		return ret;

	if (resp_data.data[0] & mask)
		return SOFT_RANGE;
	else
		return NORMAL_RANGE;
}

static int google_bcl_core_pmic_get_pre_ocp(struct bcl_device *bcl_dev, int idx)
{
	int ret;
	struct mailbox_data req_data, resp_data;
	u16 value;
	int pmic;
	u16 addr = retrieve_pre_ocp_addr(idx, &pmic);
	enum PRE_OCP_TYPE type = retrieve_pre_ocp_type(idx);
	enum PRE_OCP_RANGE range = retrieve_pre_ocp_range(bcl_dev, idx, pmic);

	req_data.data[0] = pmic;
	req_data.data[1] = 1; /* Only read 1 byte. */
	ret = da9188_mfd_mbox_send_req_blocking_read(bcl_dev->device,
						     &bcl_dev->pmic_mbox,
						     bcl_dev->remote_pmic_ch,
						     MB_PMIC_TARGET_REGISTER,
						     MB_REG_CMD_GET_PMIC_REG_BURST,
						     addr, req_data,
						     &resp_data);
	if (unlikely(ret))
		return ret;

	value = (u16)resp_data.data[0];

	if (type == NORMAL_PRE_OCP)
		value >>= 4;

	value &= PRE_OCP_THRESH_MASK;
	return retrieve_pre_ocp_lvl(bcl_dev, idx, value, range);
}

int core_pmic_get_scratch_pad(struct bcl_device *bcl_dev, u8 *value)
{
	uint32_t response[] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_SCRATCH, 0, 0, 0, response);
	*value = (u8)response[MB_BCL_IDX_RSP_DATA];
	return 0;
}

void core_pmic_set_scratch_pad(struct bcl_device *bcl_dev, u8 value)
{
	int ret;

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_SET_SCRATCH, 0, 0, value, NULL);
	if (ret < 0)
		dev_err(bcl_dev->device, "Cannot set scratchpad\n");
}

int get_scratch_value(struct bcl_device *bcl_dev, bool is_usb_plugged, u8 *value)
{
	*value = is_usb_plugged ? 1 : 0;
	return 0;
}

static int google_bcl_core_pmic_set_pre_ocp(struct bcl_device *bcl_dev, int value, int idx)
{
	int ret;
	struct mailbox_data req_data;
	int pmic;
	u16 addr = retrieve_pre_ocp_addr(idx, &pmic);

	req_data.data[0] = pmic;
	ret = convert_pre_ocp_lvl(value, idx, &req_data.data[1]);
	if (ret < 0) {
		dev_err(bcl_dev->device, "PRE_OCP Out of range.\n");
		return ret;
	}
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						addr, req_data);
	return ret;

}

static int google_bcl_core_pmic_set_pre_ocp_deb(struct bcl_device *bcl_dev, u8 value, int idx)
{
	struct mailbox_data req_data;
	int pmic;
	u16 addr;

	if (value == 0) {
		dev_err(bcl_dev->device, "Boot PRE_OCP Out of range.\n");
		return -EINVAL;
	}

	addr = retrieve_pre_ocp_addr(idx, &pmic) - PRE_OCP_DEB_OFFSET;

	req_data.data[0] = pmic;
	req_data.data[1] = value;

	return da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						 bcl_dev->remote_pmic_ch,
						 MB_PMIC_TARGET_REGISTER,
						 MB_REG_CMD_SET_PMIC_REG_SINGLE,
						 addr, req_data);
}

static int get_ocp_lvl(struct bcl_device *bcl_dev, u64 *val, u8 id)
{
	/* b/326658593 */
	*val = google_bcl_core_pmic_get_pre_ocp(bcl_dev, id);
	return 0;
}

static int set_ocp_lvl(struct bcl_device *bcl_dev, u64 val, u8 id)
{
	int ret = 0;

	if (!bcl_dev->zone[id])
		return -EIO;
	/* b/326658593 */
	ret = google_bcl_core_pmic_set_pre_ocp(bcl_dev, val, id);

	return ret;
}

void core_pmic_parse_dtree(struct bcl_device *bcl_dev)
{
	int ret, len, i, read;
	u32 val;
	struct device_node *child;
	struct device_node *p_np;
	struct device_node *np = bcl_dev->device->of_node;

	ret = of_property_read_u32(np, "rffe_channel", &val);
	bcl_dev->rffe_channel = ret ? 11 : val;
	ret = of_property_read_u32(np, "pre_uvlo_th", &val);
	bcl_dev->smpl_ctrl = ret ? DEFAULT_PRE_UVLO : val;
	ret = of_property_read_u32(np, "pre_uvlo_debounce", &val);
	bcl_dev->pre_uvlo_debounce = ret ? 0 : val;
	ret = of_property_read_u32(np, "odpm_cpu1_ch", &val);
	bcl_dev->sys_evt_odpm_param.cpu1_ch = ret ? DEFAULT_SYS_EVT_ODPM_CH : val;
	ret = of_property_read_u32(np, "odpm_cpu1_pmic", &val);
	bcl_dev->sys_evt_odpm_param.cpu1_pmic = ret ? DEFAULT_SYS_EVT_ODPM_PMIC : val;
	ret = of_property_read_u32(np, "odpm_cpu2_ch", &val);
	bcl_dev->sys_evt_odpm_param.cpu2_ch = ret ? DEFAULT_SYS_EVT_ODPM_CH : val;
	ret = of_property_read_u32(np, "odpm_cpu2_pmic", &val);
	bcl_dev->sys_evt_odpm_param.cpu2_pmic = ret ? DEFAULT_SYS_EVT_ODPM_PMIC : val;
	ret = of_property_read_u32(np, "odpm_gpu_ch", &val);
	bcl_dev->sys_evt_odpm_param.gpu_ch = ret ? DEFAULT_SYS_EVT_ODPM_CH : val;
	ret = of_property_read_u32(np, "odpm_gpu_pmic", &val);
	bcl_dev->sys_evt_odpm_param.gpu_pmic = ret ? DEFAULT_SYS_EVT_ODPM_PMIC : val;
	ret = of_property_read_u32(np, "odpm_tpu_ch", &val);
	bcl_dev->sys_evt_odpm_param.tpu_ch = ret ? DEFAULT_SYS_EVT_ODPM_CH : val;
	ret = of_property_read_u32(np, "odpm_tpu_pmic", &val);
	bcl_dev->sys_evt_odpm_param.tpu_pmic = ret ? DEFAULT_SYS_EVT_ODPM_PMIC : val;

	bcl_dev->qos_update_wq = create_singlethread_workqueue("bcl_qos_update");

	/* parse ODPM main mitigation module */
	p_np = of_get_child_by_name(np, "main_mitigation");
	if (p_np) {
		i = 0;
		for_each_child_of_node(p_np, child) {
			if (i < METER_CHANNEL_MAX) {
				of_property_read_u32(child, "module_id", &read);
				bcl_dev->main_mitigation_conf[i].module_id = read;

				of_property_read_u32(child, "threshold", &read);
				bcl_dev->main_mitigation_conf[i].threshold = read;
				i++;
			}
		}
	}

	/* parse ODPM sub mitigation module */
	p_np = of_get_child_by_name(np, "sub_mitigation");
	if (p_np) {
		i = 0;
		for_each_child_of_node(p_np, child) {
			if (i < METER_CHANNEL_MAX) {
				of_property_read_u32(child, "module_id", &read);
				bcl_dev->sub_mitigation_conf[i].module_id = read;

				of_property_read_u32(child, "threshold", &read);
				bcl_dev->sub_mitigation_conf[i].threshold = read;
				i++;
			}
		}
	}

	/* parse and init non-monitored modules */
	bcl_dev->non_monitored_mitigation_module_ids = 0;
	len = 0;
	if (of_get_property(np, "non_monitored_module_ids", &len) && len >= sizeof(u32)) {
		bcl_dev->non_monitored_module_ids = kmalloc(len, GFP_KERNEL);
		len /= sizeof(u32);
		if (bcl_dev->non_monitored_module_ids) {
			for (i = 0; i < len; i++) {
				ret = of_property_read_u32_index(np,
						 "non_monitored_module_ids",
						 i, &bcl_dev->non_monitored_module_ids[i]);
				if (ret) {
					dev_err(bcl_dev->device,
						"failed to read non_monitored_module_id_%d\n", i);
				}
				bcl_dev->non_monitored_mitigation_module_ids |=
					BIT(bcl_dev->non_monitored_module_ids[i]);
			}
		}
	}
}

uint32_t core_pmic_get_cpm_cached_sys_evt(struct bcl_device *bcl_dev)
{
	int ret;
	int i;

	for (i = 0; i < SYS_EVT_MAX_MAIN; i++) {
		uint32_t response[] = {0, 0};

		ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CACHED_SYS_EVT, 0,
					      CORE_MAIN_PMIC, i, response);

		if (ret < 0)
			return ret;

		if (response[MB_BCL_IDX_RSP_STATUS] > 0)
			return -EINVAL;

		bcl_dev->cpm_cached_sys_evt_main[i] =
			response[MB_BCL_IDX_RSP_DATA] & SYS_EVT_RD_MASK;
		if (bcl_dev->cpm_cached_sys_evt_main[i] != 0)
			dev_info(bcl_dev->device, "SYS_EVENT_%d_MAIN: %#x\n", i,
				 bcl_dev->cpm_cached_sys_evt_main[i]);
	}
	for (i = 0; i < SYS_EVT_MAX_SUB; i++) {
		uint32_t response[] = {0, 0};

		ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_CACHED_SYS_EVT, 0,
					      CORE_SUB_PMIC, i, response);

		if (ret < 0)
			return ret;

		if (response[MB_BCL_IDX_RSP_STATUS] > 0)
			return -EINVAL;

		bcl_dev->cpm_cached_sys_evt_sub[i] =
			response[MB_BCL_IDX_RSP_DATA] & SYS_EVT_RD_MASK;
		if (bcl_dev->cpm_cached_sys_evt_sub[i] != 0)
			dev_info(bcl_dev->device, "SYS_EVENT_%d_SUB: %#x\n", i,
				 bcl_dev->cpm_cached_sys_evt_sub[i]);
	}

	return 0;
}

static void google_bcl_setup_main_odpm(struct work_struct *work)
{
	int rail_idx, i;
	struct google_odpm *odpm;
	struct bcl_device *bcl_dev = container_of(work, struct bcl_device,
						  setup_main_odpm_work.work);

	bcl_dev->indio_dev = google_get_odpm_iio_dev();
	if (!bcl_dev->indio_dev) {
		dev_info(bcl_dev->device, "failed to get odpm iio\n");
		if (bcl_dev->init_main_odpm_count < INIT_MAIN_ODPM_MAX_COUNT) {
			bcl_dev->init_main_odpm_count++;
			schedule_delayed_work(&bcl_dev->setup_main_odpm_work,
					      msecs_to_jiffies(TIMEOUT_5S));
		}
		return;
	}
	bcl_dev->odpm = iio_priv(bcl_dev->indio_dev);
	odpm = bcl_dev->odpm;
	for (i = 0; i < ODPM_CHANNEL_NUM; ++i) {
		if (!bcl_dev->odpm->channels[i].enabled)
			continue;
		rail_idx = bcl_dev->odpm->channels[i].rail_idx;
		if (i >= METER_CHANNEL_MAX)
			bcl_dev->sub_rail_names[i - METER_CHANNEL_MAX] = bcl_dev->odpm->chip
				.rails[rail_idx].schematic_name;
		else
			bcl_dev->main_rail_names[i] = bcl_dev->odpm->chip.rails[rail_idx]
				.schematic_name;
	}

	dev_info(bcl_dev->device, "odpm iio retrieved\n");
}

static int read_sys_evt_cache(struct bcl_device *bcl_dev, int addr, int pmic, void *data,
			      size_t len)
{
	switch (pmic) {
	case SYS_EVT_MAIN:
		memcpy(data, &bcl_dev->sys_evt_main[addr], len);
		break;
	case SYS_EVT_SUB:
		memcpy(data, &bcl_dev->sys_evt_sub[addr], len);
		break;
	default:
		break;
	}

	return 0;
}

static int read_sys_evt(struct bcl_device *bcl_dev, int addr, int pmic, void *data, size_t len)
{
	int ret;
	int i;
	uint8_t *p = data;

	for (i = 0; i < len; i++) {
		uint32_t response[] = {0, 0};

		ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_SYS_EVT, SYS_EVT_RD_BYTES,
					      pmic, addr + i, response);

		if (ret < 0)
			return ret;

		if (response[MB_BCL_IDX_RSP_STATUS] > 0)
			return -EINVAL;

		*p++ = response[MB_BCL_IDX_RSP_DATA] & SYS_EVT_RD_MASK;
	}
	return 0;
}

static void google_bcl_setup_core_pmic(struct work_struct *work)
{
	int idx;
	int ret;
	int addr = 0;
	struct mailbox_data req_data;

	struct bcl_device *bcl_dev = container_of(work,
					    struct bcl_device, setup_core_pmic_work.work);

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_SYS_EVT,
				      SYS_EVT_BYTE_CNT, SYS_EVT_MAIN,
				      0, NULL);

	if (ret < 0) {
		dev_info(bcl_dev->device, "failed to read from SYS_EVT\n");
		if (bcl_dev->init_core_pmic_count < INIT_BBAT_MAX_COUNT) {
			bcl_dev->init_core_pmic_count++;
			schedule_delayed_work(&bcl_dev->setup_core_pmic_work,
					      msecs_to_jiffies(TIMEOUT_5S));
		}
		return;
	}

	for (idx = PRE_OCP_CPU1; idx <= SOFT_PRE_OCP_GPU; idx++) {
		ret = google_bcl_core_pmic_set_pre_ocp_deb(bcl_dev, PRE_OCP_DEB_VAL, idx);

		if (ret < 0)
			dev_err(bcl_dev->device, "failed to set pre ocp deb: %i", idx);
	}

	ret = read_sys_evt(bcl_dev, addr, SYS_EVT_MAIN, &bcl_dev->sys_evt_main[addr],
			   SYS_EVT_MAX_MAIN);
	if (ret < 0) {
		dev_err(bcl_dev->device, "failed to read sys_evt_main[%i]", addr);
		return;
	}

	ret = read_sys_evt(bcl_dev, addr, SYS_EVT_SUB, &bcl_dev->sys_evt_sub[addr],
			   SYS_EVT_MAX_SUB);
	if (ret < 0) {
		dev_err(bcl_dev->device, "failed to read sys_evt_sub[%i]", addr);
		return;
	}

	req_data.data[0] = CORE_MAIN_PMIC;
	req_data.data[1] = SYS_EVT_CLR;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_RTC_SYSTEM, req_data);
	if (unlikely(ret))
		dev_err(bcl_dev->device, "failed to clear main bbat");

	req_data.data[0] = CORE_MAIN_PMIC;
	req_data.data[1] = 0;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_RTC_SYSTEM, req_data);
	if (unlikely(ret))
		dev_err(bcl_dev->device, "failed to restore main bbat");

	req_data.data[0] = CORE_SUB_PMIC;
	req_data.data[1] = SYS_EVT_CLR;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9189_BBAT_SYSTEM, req_data);
	if (unlikely(ret))
		dev_err(bcl_dev->device, "failed to clear sub bbat");

	req_data.data[0] = CORE_SUB_PMIC;
	req_data.data[1] = 0;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9189_BBAT_SYSTEM, req_data);
	if (unlikely(ret))
		dev_err(bcl_dev->device, "failed to restore sub bbat");

	req_data.data[0] = CORE_MAIN_PMIC;
	req_data.data[1] = DA9188_PRE_UVLO_CNT_EN;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_PRE_UVLO_DBG_CNTR0, req_data);
	if (unlikely(ret))
		dev_err(bcl_dev->device, "failed to enable main pre-uvlo cnt");

	req_data.data[0] = CORE_SUB_PMIC;
	req_data.data[1] = DA9188_PRE_UVLO_CNT_EN;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_PRE_UVLO_DBG_CNTR0, req_data);
	if (unlikely(ret))
		dev_err(bcl_dev->device, "failed to enable main pre-uvlo cnt");
}

static void google_bcl_setup_core_pmic_nsamp(struct bcl_device *bcl_dev, int pmic_id)
{
	struct mailbox_data req_data;
	int ret;

	req_data.data[0] = pmic_id;
	req_data.data[1] = 0x0;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_TELEM_AVG_CTRL, req_data);
	if (unlikely(ret))
		dev_info(bcl_dev->device, "Writing 0x0 to AVG_NSAMP fail: %d\n", ret);

}

static void google_bcl_setup_core_pmic_pwrwarn(struct bcl_device *bcl_dev, int pmic_id)
{
	struct mailbox_data req_data;
	int ret;

	req_data.data[0] = pmic_id;
	req_data.data[1] = 0x0; /* Unmask all IRQ */
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_TELEM_VGPO_MASK0, req_data);
	if (unlikely(ret))
		dev_info(bcl_dev->device, "Writing 0x0 to VGPO_MASK0 fail: %d\n", ret);

	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_TELEM_VGPO_MASK1, req_data);
	if (unlikely(ret))
		dev_info(bcl_dev->device, "Writing 0x0 to VGPO_MASK1 fail: %d\n", ret);

	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_TELEM_VGPO_MASK2, req_data);
	if (unlikely(ret))
		dev_info(bcl_dev->device, "Writing 0x0 to VGPO_MASK2 fail: %d\n", ret);

	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_TELEM_VGPO_MASK3, req_data);
	if (unlikely(ret))
		dev_info(bcl_dev->device, "Writing 0x0 to VGPO_MASK3 fail: %d\n", ret);
}

static void google_bcl_setup_core_pmic_th(struct bcl_device *bcl_dev)
{
	struct mailbox_data req_data;
	int ret;

	if (bcl_dev->smpl_ctrl == 0) {
		dev_err(bcl_dev->device, "Boot PRE_UVLO Out of range.\n");
		return;
	}
	req_data.data[0] = CORE_MAIN_PMIC;
	req_data.data[1] = bcl_dev->smpl_ctrl;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_PRE_UVLO_TH, req_data);
	if (unlikely(ret))
		dev_info(bcl_dev->device, "Writing %#x to Main PRE_UVLO_TH fail: %d\n",
			bcl_dev->smpl_ctrl, ret);

	req_data.data[0] = CORE_SUB_PMIC;
	req_data.data[1] = bcl_dev->smpl_ctrl;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_PRE_UVLO_TH, req_data);
	if (unlikely(ret))
		dev_info(bcl_dev->device, "Writing %#x to Sub PRE_UVLO_TH fail: %d\n",
			bcl_dev->smpl_ctrl, ret);

	req_data.data[0] = CORE_MAIN_PMIC;
	req_data.data[1] = bcl_dev->pre_uvlo_debounce;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_PRE_UVLO_DEBOUNCE, req_data);
	if (unlikely(ret))
		dev_info(bcl_dev->device, "Writing %#x to Main PRE_UVLO_DEBOUNCE fail: %d\n",
			bcl_dev->pre_uvlo_debounce, ret);

}

static int core_pmic_configure_rcs(struct bcl_device *bcl_dev, int pmic, u16 addr, u8 bit_mask)
{
	u8 value;
	struct mailbox_data req_data, resp_data;
	int ret;

	req_data.data[0] = pmic;
	req_data.data[1] = 1;
	ret = da9188_mfd_mbox_send_req_blocking_read(bcl_dev->device, &bcl_dev->pmic_mbox,
						     bcl_dev->remote_pmic_ch,
						     MB_PMIC_TARGET_REGISTER,
						     MB_REG_CMD_GET_PMIC_REG_BURST,
						     addr, req_data, &resp_data);
	if (unlikely(ret)) {
		dev_err(bcl_dev->device, "rcs reading %#x fail\n", addr);
		return ret;
	}
	value = (u16)resp_data.data[0] | bit_mask;
	req_data.data[0] = pmic;
	req_data.data[1] = value;
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						addr, req_data);
	if (unlikely(ret))
		dev_err(bcl_dev->device, "rcs writing %#x fail\n", addr);
	return ret;
}

static void google_bcl_setup_core_pmic_rcs(struct bcl_device *bcl_dev)
{
	int ret;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_MAIN_PMIC, DA9188_B2M_VGPO_MASK,
				      VGPO_PRE_OCP | VGPO_SOFT_PRE_OCP);
	if (ret < 0)
		return;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_MAIN_PMIC, DA9188_B3M_VGPO_MASK,
				      VGPO_PRE_OCP | VGPO_SOFT_PRE_OCP);
	if (ret < 0)
		return;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_MAIN_PMIC, DA9188_B5M_VGPO_MASK,
				      VGPO_PRE_OCP | VGPO_SOFT_PRE_OCP);
	if (ret < 0)
		return;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_MAIN_PMIC, DA9188_B7M_VGPO_MASK,
				      VGPO_PRE_OCP | VGPO_SOFT_PRE_OCP);
	if (ret < 0)
		return;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_MAIN_PMIC, DA9188_EH_VGPO_MASK2,
				      VGPO_PRE_UVLO);
	if (ret < 0)
		return;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_SUB_PMIC, DA9189_B1S_VGPO_MASK,
				      VGPO_PRE_OCP | VGPO_SOFT_PRE_OCP);
	if (ret < 0)
		return;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_SUB_PMIC, DA9189_B2S_VGPO_MASK,
				      VGPO_PRE_OCP | VGPO_SOFT_PRE_OCP);
	if (ret < 0)
		return;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_SUB_PMIC, DA9189_B9S_VGPO_MASK,
				      VGPO_PRE_OCP | VGPO_SOFT_PRE_OCP);
	if (ret < 0)
		return;

	ret = core_pmic_configure_rcs(bcl_dev, CORE_SUB_PMIC, DA9189_B10S_VGPO_MASK,
				      VGPO_PRE_OCP | VGPO_SOFT_PRE_OCP);
	if (ret < 0)
		return;
}

int core_pmic_mbox_request(struct bcl_device *bcl_dev)
{
	return da9188_mfd_mbox_request(bcl_dev->device, &bcl_dev->pmic_mbox);
}

int core_pmic_main_setup(struct bcl_device *bcl_dev, struct platform_device *pdev)
{
	struct gpio_desc *pre_uvlo_pin;
	int ret, i, read;
	u32 pre_uvlo;
	u32 flag;
	struct device_node *np = bcl_dev->device->of_node;
	struct device_node *p_np;

	p_np = of_get_child_by_name(np, "main_limit");
	if (p_np) {
		i = 0;
		for_each_child_of_node_scoped(p_np, child) {
			of_property_read_u32(child, "setting", &read);
			if (i < METER_CHANNEL_MAX) {
				bcl_dev->main_setting[i] = read;
				meter_write(CORE_PMIC_MAIN, bcl_dev, i, read);
				bcl_dev->main_limit[i] =
						settings_to_current(bcl_dev, CORE_PMIC_MAIN, i,
								    read);
				i++;
			}
		}
		of_node_put(p_np);
	}

	p_np = of_get_child_by_name(np, "sub_limit");
	if (p_np) {
		i = 0;
		for_each_child_of_node_scoped(p_np, child) {
			of_property_read_u32(child, "setting", &read);
			if (i < METER_CHANNEL_MAX) {
				bcl_dev->sub_setting[i] = read;
				meter_write(CORE_PMIC_SUB, bcl_dev, i, read);
				bcl_dev->sub_limit[i] =
						settings_to_current(bcl_dev, CORE_PMIC_SUB, i,
								    read);
				i++;
			}
		}
		of_node_put(p_np);
	}

	pre_uvlo_pin = devm_gpiod_get(bcl_dev->device, "uvlo", GPIOD_ASIS);
	pre_uvlo = platform_get_irq_byname(pdev, "pre_uvlo");

	flag = IRQF_ONESHOT | IRQF_NO_AUTOEN | IRQF_TRIGGER_RISING;

	ret = google_bcl_register_zone(bcl_dev, PRE_UVLO, "pre_uvlo", pre_uvlo_pin,
				       pre_uvlo, CORE_MAIN_PMIC, IRQ_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: PRE_UVLO\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, PRE_OCP_CPU1, "pre_ocp_cpu1", 0, 0,
				       CORE_MAIN_PMIC, IRQ_NOT_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: PRE_OCP_CPU1\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, PRE_OCP_CPU2, "pre_ocp_cpu2", 0, 0,
				       CORE_MAIN_PMIC, IRQ_NOT_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: PRE_OCP_CPU2\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, SOFT_PRE_OCP_CPU1, "soft_pre_ocp_cpu1",
				       0, 0, CORE_MAIN_PMIC, IRQ_NOT_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_PRE_OCP_CPU1\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, SOFT_PRE_OCP_CPU2, "soft_pre_ocp_cpu2",
				       0, 0, CORE_MAIN_PMIC, IRQ_NOT_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_PRE_OCP_CPU2\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, PRE_OCP_TPU, "pre_ocp_tpu", 0, 0,
				       CORE_MAIN_PMIC, IRQ_NOT_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: PRE_OCP_TPU\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, SOFT_PRE_OCP_TPU, "soft_pre_ocp_tpu",
				       0, 0, CORE_MAIN_PMIC, IRQ_NOT_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_PRE_OCP_TPU\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, PRE_OCP_GPU, "pre_ocp_gpu", 0,
				       0, CORE_SUB_PMIC, IRQ_NOT_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: PRE_OCP_GPU\n");
		return -ENODEV;
	}
	ret = google_bcl_register_zone(bcl_dev, SOFT_PRE_OCP_GPU, "soft_pre_ocp_gpu",
				       0, 0, CORE_SUB_PMIC, IRQ_NOT_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_PRE_OCP_GPU\n");
		return -ENODEV;
	}

	google_bcl_setup_core_pmic_th(bcl_dev);
	google_bcl_setup_core_pmic_rcs(bcl_dev);
	google_bcl_setup_core_pmic_pwrwarn(bcl_dev, CORE_MAIN_PMIC);
	google_bcl_setup_core_pmic_pwrwarn(bcl_dev, CORE_SUB_PMIC);
	google_bcl_setup_core_pmic_nsamp(bcl_dev, CORE_MAIN_PMIC);
	google_bcl_setup_core_pmic_nsamp(bcl_dev, CORE_SUB_PMIC);
	core_pmic_set_scratch_pad(bcl_dev, 0);

	INIT_DELAYED_WORK(&bcl_dev->setup_core_pmic_work, google_bcl_setup_core_pmic);
	schedule_delayed_work(&bcl_dev->setup_core_pmic_work, msecs_to_jiffies(TIMEOUT_5MS));

	INIT_DELAYED_WORK(&bcl_dev->setup_main_odpm_work, google_bcl_setup_main_odpm);
	schedule_delayed_work(&bcl_dev->setup_main_odpm_work, msecs_to_jiffies(TIMEOUT_5MS));

	return 0;
}

int core_pmic_sub_setup(struct bcl_device *bcl_dev)
{
	return 0;
}

void core_pmic_main_meter_read_lpf_data(struct bcl_device *bcl_dev, struct brownout_stats *br_stat)
{
	struct google_odpm *odpm = bcl_dev->odpm;
	int ch, meter_ch_idx, ret;
	int rail_idx;
	u32 *val = br_stat->main_odpm_lpf.value;

	if (!bcl_dev->odpm->ready) {
		dev_err(bcl_dev->device, "odpm not initialized\n");
		return;
	}

	ret = inst_read(&bcl_dev->indio_dev->dev, ODPM_POWER);
	if (ret) {
		dev_info(bcl_dev->device, "failed to read odpm inst power\n");
		return;
	}

	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		if (ch >= METER_CHANNEL_MAX) {
			val = bcl_dev->br_stats->sub_odpm_lpf.value;
			meter_ch_idx = ch - METER_CHANNEL_MAX;
		} else
			meter_ch_idx = ch;

		rail_idx = odpm->channels[ch].rail_idx;
		if (odpm->chip.rails[rail_idx].rail_type == ODPM_RAIL_TYPE_LDO)
			val[meter_ch_idx] = (odpm->channels[ch].data_read *
					DATA_POWER_LDO_FACTOR) >> DATA_RIGHT_SHIFTER;
		else
			val[meter_ch_idx] = (odpm->channels[ch].data_read *
					DATA_POWER_FACTOR) >> DATA_RIGHT_SHIFTER;
	}

	ktime_get_real_ts64((struct timespec64 *)&br_stat->main_odpm_lpf.time);
	ktime_get_real_ts64((struct timespec64 *)&br_stat->sub_odpm_lpf.time);
}

int core_pmic_main_read_uvlo(struct bcl_device *bcl_dev, unsigned int *smpl_warn_lvl)
{
	int ret;
	struct mailbox_data req_data, resp_data;
	u16 value;

	req_data.data[0] = CORE_MAIN_PMIC;
	req_data.data[1] = 1; /* Only read 1 byte. */
	ret = da9188_mfd_mbox_send_req_blocking_read(bcl_dev->device,
						     &bcl_dev->pmic_mbox,
						     bcl_dev->remote_pmic_ch,
						     MB_PMIC_TARGET_REGISTER,
						     MB_REG_CMD_GET_PMIC_REG_BURST,
						     DA9188_PRE_UVLO_TH, req_data,
						     &resp_data);
	if (unlikely(ret))
		return ret;
	value = (u16)resp_data.data[0] & 0xF;
	*smpl_warn_lvl = retrieve_pre_uvlo_lvl(value);

	return 0;
}

u16 core_pmic_main_store_uvlo(struct bcl_device *bcl_dev, unsigned int val, size_t size)
{
	int ret;
	struct mailbox_data req_data;

	req_data.data[0] = CORE_MAIN_PMIC;
	req_data.data[1] = convert_pre_uvlo_lvl(val);
	if (req_data.data[1] == 0) {
		dev_err(bcl_dev->device, "PRE_UVLO Out of range.\n");
		return 0;
	}
	ret = da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						bcl_dev->remote_pmic_ch,
						MB_PMIC_TARGET_REGISTER,
						MB_REG_CMD_SET_PMIC_REG_SINGLE,
						DA9188_PRE_UVLO_TH, req_data);
	if (unlikely(ret))
		return ret;

	return size;
}

int core_pmic_main_get_ocp_lvl(struct bcl_device *bcl_dev, u64 *val, u8 id, u16 limit, u16 step)
{
	return get_ocp_lvl(bcl_dev, val, id);
}

int core_pmic_main_set_ocp_lvl(struct bcl_device *bcl_dev, u64 val, u8 id,
			       u16 llimit, u16 ulimit, u16 step)
{
	return set_ocp_lvl(bcl_dev, val, id);
}

static int _core_pmic_read_register(struct bcl_device *bcl_dev, u16 reg, u8 *value, bool is_meter,
				    int idx)
{
	int ret;
	struct mailbox_data req_data, resp_data;

	req_data.data[0] = idx;
	req_data.data[1] = 1;

	ret = da9188_mfd_mbox_send_req_blocking_read(bcl_dev->device,
						     &bcl_dev->pmic_mbox,
						     bcl_dev->remote_pmic_ch,
						     MB_PMIC_TARGET_REGISTER,
						     MB_REG_CMD_GET_PMIC_REG_BURST,
						     reg, req_data,
						     &resp_data);
	if (unlikely(ret))
		return ret;

	*value = (u8)resp_data.data[0];

	return ret;
}

int core_pmic_main_read_register(struct bcl_device *bcl_dev, u16 reg, u8 *value, bool is_meter)
{
	return _core_pmic_read_register(bcl_dev, reg, value, is_meter, CORE_MAIN_PMIC);
}

int core_pmic_sub_read_register(struct bcl_device *bcl_dev, u16 reg, u8 *value, bool is_meter)
{
	return _core_pmic_read_register(bcl_dev, reg, value, is_meter, CORE_SUB_PMIC);
}

static int _core_pmic_write_register(struct bcl_device *bcl_dev, u16 reg, u8 value, bool is_meter,
				     int idx)
{
	struct mailbox_data req_data;

	req_data.data[0] = idx;
	req_data.data[1] = value;

	return da9188_mfd_mbox_send_req_blocking(bcl_dev->device, &bcl_dev->pmic_mbox,
						 bcl_dev->remote_pmic_ch,
						 MB_PMIC_TARGET_REGISTER,
						 MB_REG_CMD_SET_PMIC_REG_SINGLE,
						 reg, req_data);
}

int core_pmic_main_write_register(struct bcl_device *bcl_dev, u16 reg, u8 value, bool is_meter)
{
	return _core_pmic_write_register(bcl_dev, reg, value, is_meter, CORE_MAIN_PMIC);
}

int core_pmic_sub_write_register(struct bcl_device *bcl_dev, u16 reg, u8 value, bool is_meter)
{
	return _core_pmic_write_register(bcl_dev, reg, value, is_meter, CORE_SUB_PMIC);
}


void compute_odpm_lpf(struct bcl_device *bcl_dev,
				struct timespec64 triggered_time,
				struct bcl_mitigation_conf *mitigation_conf,
				struct odpm_lpf *odpm_lpf,
				struct max_odpm_lpf *max_odpm_lpf)
{
	int ch, meter_ch_idx;
	u32 odpm_lpf_value, odpm_lpf_thres;

	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		/* google_odpm integrated main and sub in one reading
		 * on anacapa.
		 */
		if (ch >= METER_CHANNEL_MAX) {
			meter_ch_idx = ch - METER_CHANNEL_MAX;
			mitigation_conf = bcl_dev->main_mitigation_conf;
			odpm_lpf = &bcl_dev->br_stats->main_odpm_lpf;
			max_odpm_lpf = bcl_dev->max_odpm_stats->main_max_odpm_lpf;
		} else {
			meter_ch_idx = ch;
			mitigation_conf = bcl_dev->sub_mitigation_conf;
			odpm_lpf = &bcl_dev->br_stats->sub_odpm_lpf;
			max_odpm_lpf = bcl_dev->max_odpm_stats->sub_max_odpm_lpf;
		}
		odpm_lpf_value = odpm_lpf->value[meter_ch_idx];
		odpm_lpf_thres = mitigation_conf[meter_ch_idx].threshold;
		if (odpm_lpf_value >= odpm_lpf_thres) {
			/* Compute mitigation modules */
			atomic_or(BIT(mitigation_conf[meter_ch_idx].module_id),
					  &bcl_dev->mitigation_module_ids);

			if (odpm_lpf_value >= odpm_lpf_thres * 3)
				max_odpm_lpf[meter_ch_idx].count_lvl_2++;
			else if (odpm_lpf_value >= odpm_lpf_thres * 2)
				max_odpm_lpf[meter_ch_idx].count_lvl_1++;
			else
				max_odpm_lpf[meter_ch_idx].count_lvl_0++;
		}
		if (odpm_lpf_value >= max_odpm_lpf[meter_ch_idx].value) {
			max_odpm_lpf[meter_ch_idx].time = triggered_time;
			max_odpm_lpf[meter_ch_idx].value = odpm_lpf_value;
			max_odpm_lpf[meter_ch_idx].triggered_idx = bcl_dev->br_stats->triggered_idx;
		}

	}
}

int read_uvlo_dur(struct bcl_device *bcl_dev, uint64_t *data)
{
	uint64_t pre_uvlo_ts = 0;
	uint64_t shdn_ts = 0;
	int ret;

	*data = 0;

	ret = read_sys_evt_cache(bcl_dev, SYS_EVT_PRE_UVLO_START, SYS_EVT_MAIN, &pre_uvlo_ts,
				 SYS_EVT_UVLO_DUR_CNT);
	if (ret < 0)
		return ret;

	ret = read_sys_evt_cache(bcl_dev, SYS_EVT_SHDN_START, SYS_EVT_MAIN, &shdn_ts,
				 SYS_EVT_UVLO_DUR_CNT);
	if (ret < 0)
		return ret;

	if (pre_uvlo_ts > shdn_ts)
		return 0;

	*data = shdn_ts - pre_uvlo_ts;
	return 0;
}

int read_pre_uvlo_hit_cnt(struct bcl_device *bcl_dev, uint16_t *data, int pmic)
{
	uint16_t cnt = 0;
	int ret, rd_addr;

	switch (pmic) {
	case SYS_EVT_MAIN:
		rd_addr = SYS_EVT_PRE_UVLO_HIT_CNT_M;
		break;
	case SYS_EVT_SUB:
		rd_addr = SYS_EVT_PRE_UVLO_HIT_CNT_S;
		break;
	default:
		return -EINVAL;
	}

	ret = read_sys_evt_cache(bcl_dev, rd_addr, pmic, &cnt, SYS_EVT_PRE_UVLO_HIT_CNT_RD_CNT);
	if (ret < 0)
		return ret;

	*data = (uint16_t)cnt;
	return 0;
}

int read_pre_ocp_bckup(struct bcl_device *bcl_dev, int *pre_ocp_bckup, int rail)
{
	int sel_upper, rd_addr, pmic, ret;
	uint8_t data;

	switch (rail) {
	case CPU1A:
		sel_upper = SYS_EVT_PRE_OCP_CPU1_UPPER_BITS;
		rd_addr = SYS_EVT_PRE_OCP_CPU2_CPU1;
		pmic = SYS_EVT_MAIN;
		break;
	case CPU2:
		sel_upper = SYS_EVT_PRE_OCP_CPU2_UPPER_BITS;
		rd_addr = SYS_EVT_PRE_OCP_CPU2_CPU1;
		pmic = SYS_EVT_MAIN;
		break;
	case TPU:
		sel_upper = SYS_EVT_PRE_OCP_TPU_UPPER_BITS;
		rd_addr = SYS_EVT_PRE_OCP_TPU_CPU0;
		pmic = SYS_EVT_MAIN;
		break;
	case GPU:
		sel_upper = SYS_EVT_PRE_OCP_GPU_UPPER_BITS;
		rd_addr = SYS_EVT_PRE_OCP_GPU_MM;
		pmic = SYS_EVT_SUB;
		break;
	default:
		return -EINVAL;
	}

	ret = read_sys_evt_cache(bcl_dev, rd_addr, pmic, &data, SYS_EVT_ONE_BYTE);
	if (ret < 0)
		return ret;

	if (sel_upper)
		*pre_ocp_bckup = (data & 0xF0) >> 4;
	else
		*pre_ocp_bckup &= data & 0xF;

	return 0;
}

static int read_odpm_ch_type(struct bcl_device *bcl_dev, u16 *type, int pmic, int channel)
{
	int ret;
	u16 addr;
	struct mailbox_data req_data, resp_data;

	req_data.data[0] = pmic;
	req_data.data[1] = 1;

	addr = DA9188_TELEM_CH0_INT_CTRL + (channel * DA9188_TELEM_CH_INT_CTRL_ADDR_INCR);
	if (channel >= DA9188_TELEM_CH_INT_MAX)
		addr = DA9188_TELEM_CH0_EXT_CTRL + channel - DA9188_TELEM_CH_INT_MAX;

	ret = da9188_mfd_mbox_send_req_blocking_read(bcl_dev->device,
						     &bcl_dev->pmic_mbox,
						     bcl_dev->remote_pmic_ch,
						     MB_PMIC_TARGET_REGISTER,
						     MB_REG_CMD_GET_PMIC_REG_BURST,
						     addr, req_data,
						     &resp_data);
	if (unlikely(ret))
		return ret;

	*type = ((u16)resp_data.data[0] & DA9188_VIP_SEL_CH_INT_MASK)
		>> DA9188_VIP_SEL_CH_INT_OFFSET;

	return ret;
}

static int read_odpm_int_select_ch(struct bcl_device *bcl_dev, int pmic, int channel, u16 *int_sel)
{
	int ret;
	u16 addr;
	struct mailbox_data req_data, resp_data;

	req_data.data[0] = pmic;
	req_data.data[1] = 1;

	if (channel >= DA9188_TELEM_CH_INT_MAX) {
		*int_sel = DA9188_TELEM_BUCK_CHAN_MIN;
		return 0;
	}

	addr = DA9188_TELEM_CH0_INT_IN_SEL + (channel * DA9188_TELEM_CH_INT_CTRL_ADDR_INCR);

	ret = da9188_mfd_mbox_send_req_blocking_read(bcl_dev->device,
						     &bcl_dev->pmic_mbox,
						     bcl_dev->remote_pmic_ch,
						     MB_PMIC_TARGET_REGISTER,
						     MB_REG_CMD_GET_PMIC_REG_BURST,
						     addr, req_data,
						     &resp_data);
	if (unlikely(ret))
		return ret;

	*int_sel = (u16)resp_data.data[0] & DA9188_TELEM_CH_SEL_MASK;

	return 0;
}

int read_odpm_int_bckup(struct bcl_device *bcl_dev, int *odpm_int_bckup, u16 *type, int pmic,
			int channel)
{
	int ret, pmic_chan, sel_reg, fs_range;
	struct mailbox_data req_data;
	u16 int_sel;
	uint8_t rdback;

	switch (pmic) {
	case SYS_EVT_MAIN:
		pmic_chan = SYS_EVT_ODPM_MAIN;
		req_data.data[0] = CORE_MAIN_PMIC;
		break;
	case SYS_EVT_SUB:
		pmic_chan = SYS_EVT_ODPM_SUB;
		req_data.data[0] = CORE_SUB_PMIC;
		break;
	default:
		return -EINVAL;
	}

	ret = read_odpm_ch_type(bcl_dev, type, pmic, channel);
	if (unlikely(ret))
		return ret;

	ret = read_odpm_int_select_ch(bcl_dev, pmic, channel, &int_sel);
	if (unlikely(ret))
		return ret;

	if (pmic == SYS_EVT_MAIN) {
		switch (int_sel) {
		case DA9188_TELEM_BUCK_CHAN_MIN ... DA9188_TELEM_BUCK_CHAN_MAX:
			sel_reg = DA9188_TELEM_SEL_BUCK;
			break;
		case DA9188_TELEM_LDO_CHAN_MIN ... DA9188_TELEM_LDO_CHAN_MAX:
			sel_reg = DA9188_TELEM_SEL_LDO;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (int_sel) {
		case DA9189_TELEM_BUCK_CHAN_MIN ... DA9189_TELEM_BUCK_CHAN_MAX:
			sel_reg = DA9188_TELEM_SEL_BUCK;
			break;
		case DA9189_TELEM_LDO_CHAN_MIN ... DA9189_TELEM_LDO_CHAN_MAX:
			sel_reg = DA9188_TELEM_SEL_LDO;
			break;
		default:
			return -EINVAL;
		}
	}

	pmic_chan += channel;

	ret = read_sys_evt_cache(bcl_dev, pmic_chan, pmic, &rdback, SYS_EVT_ONE_BYTE);
	if (ret < 0)
		return ret;

	switch (*type) {
	case TELEM_VOLTAGE:
		fs_range = DA9188_TELEM_VOLT_FS_RANGE_MV;
		break;
	case TELEM_CURRENT:
		if (sel_reg == DA9188_TELEM_SEL_BUCK)
			fs_range = DA9188_TELEM_BUCK_CURR_FS_RANGE_MA;
		else
			fs_range = DA9188_TELEM_LDO_CURR_FS_RANGE_MA;
		break;
	case TELEM_POWER:
		if (sel_reg == DA9188_TELEM_SEL_BUCK)
			fs_range = DA9188_TELEM_BUCK_PWR_FS_RANGE_MW;
		else
			fs_range = DA9188_TELEM_LDO_PWR_FS_RANGE_MW;
		break;
	default:
		return -EINVAL;
	}
	*odpm_int_bckup = ((int)rdback  * fs_range) >> DA9188_TELEM_MSB_MASK_SHIFT;

	return 0;
}

void core_pmic_teardown(struct bcl_device *bcl_dev)
{
	if (bcl_dev->pmic_mbox.client)
		da9188_mfd_mbox_release(&bcl_dev->pmic_mbox);
}

static uint32_t _core_pmic_read_pwrwarn(struct bcl_device *bcl_dev, int pwrwarn_idx, int cmd)
{
	uint32_t response[] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, cmd, pwrwarn_idx, 0, 0, response);
	if (response[MB_BCL_IDX_RSP_STATUS] > 0)
		return -EINVAL;

	return response[MB_BCL_IDX_RSP_DATA];
}

uint32_t core_pmic_read_main_pwrwarn(struct bcl_device *bcl_dev, int pwrwarn_idx)
{
	return _core_pmic_read_pwrwarn(bcl_dev, pwrwarn_idx, MB_BCL_CMD_GET_MAIN_PWRWARN_COUNT);
}

uint32_t core_pmic_read_sub_pwrwarn(struct bcl_device *bcl_dev, int pwrwarn_idx)
{
	return _core_pmic_read_pwrwarn(bcl_dev, pwrwarn_idx, MB_BCL_CMD_GET_SUB_PWRWARN_COUNT);
}

uint32_t core_pmic_get_pre_evt_cnt(struct bcl_device *bcl_dev, int zone_idx)
{
	uint32_t response[] = {0, 0};

	google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_PRE_EVT_COUNT, 0, 0, zone_idx, response);

	if (response[MB_BCL_IDX_RSP_STATUS] > 0)
		return -EINVAL;

	return response[MB_BCL_IDX_RSP_DATA];
}

u64 settings_to_current(struct bcl_device *bcl_dev, int pmic, int idx, u32 setting)
{
	int ret, sel_reg;
	u32 resolution;
	u16 int_sel, type;
	u64 res;

	ret = read_odpm_ch_type(bcl_dev, &type, pmic, idx);
	if (unlikely(ret))
		return ret;

	ret = read_odpm_int_select_ch(bcl_dev, pmic, idx, &int_sel);
	if (unlikely(ret))
		return ret;

	if (pmic == CORE_PMIC_MAIN) {
		switch (int_sel) {
		case DA9188_TELEM_BUCK_CHAN_MIN ... DA9188_TELEM_BUCK_CHAN_MAX:
			sel_reg = DA9188_TELEM_SEL_BUCK;
			break;
		case DA9188_TELEM_LDO_CHAN_MIN ... DA9188_TELEM_LDO_CHAN_MAX:
			sel_reg = DA9188_TELEM_SEL_LDO;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (int_sel) {
		case DA9189_TELEM_BUCK_CHAN_MIN ... DA9189_TELEM_BUCK_CHAN_MAX:
			sel_reg = DA9188_TELEM_SEL_BUCK;
			break;
		case DA9189_TELEM_LDO_CHAN_MIN ... DA9189_TELEM_LDO_CHAN_MAX:
			sel_reg = DA9188_TELEM_SEL_LDO;
			break;
		default:
			return -EINVAL;
		}
	}

	switch (type) {
	case TELEM_VOLTAGE:
		resolution = DA9188_TELEM_VOLT_RES_UV;
		break;
	case TELEM_CURRENT:
		if (sel_reg == DA9188_TELEM_SEL_BUCK)
			resolution = DA9188_TELEM_BUCK_CURR_RES_UA;
		else
			resolution = DA9188_TELEM_LDO_CURR_RES_UA;
		break;
	case TELEM_POWER:
		if (sel_reg == DA9188_TELEM_SEL_BUCK)
			resolution = DA9188_TELEM_BUCK_PWR_RES_UW;
		else
			resolution = DA9188_TELEM_LDO_PWR_RES_UW;
		break;
	default:
		return -EINVAL;
	}

	res = (u64)setting * resolution;

	return res;
}

int core_pmic_sub_get_ocp_lvl(struct bcl_device *bcl_dev, u64 *val, u8 id, u16 limit, u16 step)
{
	return 0;
}
int core_pmic_sub_set_ocp_lvl(struct bcl_device *bcl_dev, u64 val, u8 id,
			       u16 llimit, u16 ulimit, u16 step)
{
	return 0;
}
