// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2023-2024 Google LLC
#include <linux/configfs.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kmod.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/of_platform.h>

#include <linux/iio/iio.h>
#include <linux/iio/configfs.h>
#include <linux/iio/sysfs.h>

#include "google_odpm.h"

#define CPM_AP_NS_ODPM_MB_SERVICE	0x69

#define SHUNT_RES_REG_CONST		81920000
#define MAX_SHUNT_RES_UOHM		96000
#define MIN_SHUNT_RES_UOHM		1500

#define MBA_RES_DATA_NOT_READY (-3)

#define CALC_INC_POWER(data, fact) ((data * fact) >> DATA_RIGHT_SHIFTER)

static struct google_odpm *kern_if_odpm;
static struct odpm_rail_energy kern_odpm_re[ODPM_CHANNEL_NUM] = {0};

static void google_odpm_mba_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct google_odpm *odpm = priv_data;
	struct cpm_iface_payload *cpm_msg = msg;

	dev_dbg(odpm->dev, "rx callback msg 0x%x\n", cpm_msg->payload[0]);
	dev_dbg(odpm->dev, "rx callback hdr 0x%x\n", cpm_msg->header);
}

static int google_odpm_mba_resp_hdl(const u32 data[], struct google_odpm *odpm)
{
	u32 mba_msg_type = data[0];
	int ret = 0;
	u32 version = 0;

	switch (mba_msg_type) {
	case ODPM_MBA_MSG_TYPE_ADDR:
		odpm->shared_memory_address = data[2];
		odpm->shared_memory_base = devm_ioremap(odpm->dev,
							odpm->shared_memory_address,
							odpm->shared_memory_size);
		version = readl(odpm->shared_memory_base);
		if (odpm->protocol_version != version) {
			dev_warn(odpm->dev,
				 "AP odpm version: %u != CPM odpm version: %u ",
				 version, odpm->protocol_version);
			ret = -EPROTO;
		}
		break;
	case ODPM_MBA_MSG_TYPE_SAMPLING_MODE:
		odpm->config_cpl = true;
		break;
	case ODPM_MBA_MSG_TYPE_DATA:
		if (data[1] == 0) {
			if (odpm->chip.acc_transfer_started) {
				odpm->chip.acc_data_ready = true;
				odpm->chip.acc_transfer_started = false;
			}
		} else if (data[1] != MBA_RES_DATA_NOT_READY)
			dev_err(odpm->dev, "Incorrect MBA response for data read: %d\n",
				data[1]);
		break;
	default:
		/* If the response is correct, data[1] and data[2] should be 0 */
		if (data[1] != 0 || data[2] != 0)
			dev_err(odpm->dev, "Incorrect MBA response: %u\n", mba_msg_type);
		break;
	}

	return ret;
}

static int google_odpm_send(struct google_odpm *odpm, u32 type, u32 value0, u32 value1)
{
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req cpm_req;
	int ret;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MBA_REQUEST_TIMEOUT;
	cpm_req.dst_id = odpm->remote_ch;

	/* Setup the message payload */
	req_msg.payload[0] = type;
	req_msg.payload[1] = value0;
	req_msg.payload[2] = value1;

	/* Send the message */
	ret = cpm_send_message(odpm->client, &cpm_req);
	if (ret < 0) {
		dev_err(odpm->dev, "Send cpm message failed: type=%u ret=%d\n", type, ret);
		return ret;
	}

	return google_odpm_mba_resp_hdl(resp_msg.payload, odpm);
}

static void google_read_shared_memory(struct google_odpm *odpm, u32 mode_type, u32 data_type)
{
	int ch;

	switch (mode_type) {
	case ODPM_INST_MODE:
	case ODPM_ACC_MODE:
	case ODPM_AVG_MODE:
		for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
			odpm->channels[ch].data_read =
				readq(odpm->shared_memory_base +
				      SHARED_CHANNEL_OFFSET +
				      ODPM_CHANNEL_SIZE * ch);
		}
		break;
	case ODPM_DEBUG_MODE:
		for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
			odpm->channels[ch].debug_stats =
				readq(odpm->shared_memory_base +
				      SHARED_CHANNEL_OFFSET +
				      ODPM_CHANNEL_SIZE * ch);
		}
		break;
	default:
		dev_err(odpm->dev, "Unknown mode type (%d)\n", mode_type);
	}

	odpm->chip.sample_count_m_int =
		readl(odpm->shared_memory_base + SHARED_SAMPLE_M_INT_OFFSET);
	odpm->chip.sample_count_m_ext =
		readl(odpm->shared_memory_base + SHARED_SAMPLE_M_EXT_OFFSET);
	odpm->chip.sample_count_s_int =
		readl(odpm->shared_memory_base + SHARED_SAMPLE_S_INT_OFFSET);
	odpm->chip.sample_count_s_ext =
		readl(odpm->shared_memory_base + SHARED_SAMPLE_S_EXT_OFFSET);
}

/*
 * Verify and return sampling rate
 *
 * @return Sampling rate if >= 0, else error number
 */
static int google_odpm_sampling_rate_verify(struct google_odpm *odpm, const char *buf)
{
	int uhz;

	if (kstrtoint(buf, 10, &uhz)) {
		dev_err(odpm->dev, "sampling rate is not an integer\n");
		return -EINVAL;
	}

	return uhz;
}

static bool google_odpm_match_int_sampling_rate(struct google_odpm *odpm,
						u32 sampling_rate, int *index)
{
	int i;

	for (i = 0; i < odpm->chip.sampling_rate_int_count; ++i) {
		if (sampling_rate == odpm->chip.sampling_rate_int_uhz[i]) {
			*index = i;
			return true;
		}
	}

	return false;
}

static bool google_odpm_match_ext_sampling_rate(struct google_odpm *odpm,
						u32 sampling_rate, int *index)
{
	int i;

	for (i = 0; i < odpm->chip.sampling_rate_ext_count; ++i) {
		if (sampling_rate == odpm->chip.sampling_rate_ext_uhz[i]) {
			*index = i;
			return true;
		}
	}

	return false;
}

static bool google_odpm_telem_channel_valid(int telem_channel_idx)
{
	if ((telem_channel_idx >= ODPM_CHANNEL_M_MIN && telem_channel_idx <= ODPM_CHANNEL_M_MAX) ||
	    (telem_channel_idx >= ODPM_CHANNEL_S_MIN && telem_channel_idx <= ODPM_CHANNEL_S_MAX))
		return true;
	return false;
}

static enum odpm_pmic_type google_odpm_get_pmic_type(int telem_channel_idx)
{
	if (telem_channel_idx >= ODPM_CHANNEL_M_MIN && telem_channel_idx <= ODPM_CHANNEL_M_MAX)
		return PMIC_MAIN;
	if (telem_channel_idx >= ODPM_CHANNEL_S_MIN && telem_channel_idx <= ODPM_CHANNEL_S_MAX)
		return PMIC_SUB;
	return PMIC_INVALID;
}

static int google_odpm_get_channel_idx(int telem_channel_idx)
{
	if (telem_channel_idx >= ODPM_CHANNEL_S_MIN && telem_channel_idx <= ODPM_CHANNEL_S_MAX)
		return telem_channel_idx - ODPM_CHANNEL_OFFSET;
	return telem_channel_idx;
}

static int google_odpm_get_rail_idx(struct google_odpm *odpm, const char *rail_name)
{
	int rail_idx;

	for (rail_idx = 0; rail_idx < odpm->chip.num_rails; ++rail_idx) {
		if (!strcmp(odpm->chip.rails[rail_idx].regulator_name, rail_name))
			return rail_idx;
	}
	return rail_idx;
}

/*
 * Create mba odpm node and append to the tail of odpm->mba_send_list
 */
static int google_odpm_enqueue_odpm_node(struct google_odpm *odpm, u32 type, u32 value0, u32 value1)
{
	int ret = 0;
	struct mba_send_node *node;

	node = devm_kmalloc(odpm->dev, sizeof(*node), GFP_KERNEL);
	if (!node) {
		ret = -ENOMEM;
	} else {
		node->type = type;
		node->value0 = value0;
		node->value1 = value1;
		list_add_tail(&node->list, &odpm->mba_send_list);
	}

	return ret;
}

static ssize_t sample_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	ssize_t count = 0;

	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "sample_count_m_int: %u\n",
			   odpm->chip.sample_count_m_int);
	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "sample_count_m_ext: %u\n",
			   odpm->chip.sample_count_m_ext);
	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "sample_count_s_int: %u\n",
			   odpm->chip.sample_count_s_int);
	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "sample_count_s_ext: %u\n",
			   odpm->chip.sample_count_s_ext);

	return count;
}

static ssize_t available_sampling_rate_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int i;
	ssize_t count = 0;

	for (i = 0; i < odpm->chip.sampling_rate_int_count; ++i) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%u (uhz)\n",
				   odpm->chip.sampling_rate_int_uhz[i]);
	}

	return count;
}

static ssize_t available_ext_sampling_rate_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int i;
	ssize_t count = 0;

	for (i = 0; i < odpm->chip.sampling_rate_ext_count; ++i) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%u (uhz)\n",
				   odpm->chip.sampling_rate_ext_uhz[i]);
	}

	return count;
}

static ssize_t sampling_rate_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int i = odpm->chip.int_sampling_rate_index;
	u32 freq = odpm->chip.sampling_rate_int_uhz[i];

	return scnprintf(buf, PAGE_SIZE, "%d.%06d (hz)\n", freq / UHZ_PER_HZ,
			 freq % UHZ_PER_HZ);
}

static ssize_t sampling_rate_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);

	int new_sampling_rate_i;
	int new_sampling_rate;

	int ret = google_odpm_sampling_rate_verify(odpm, buf);

	if (ret < 0)
		return ret;

	new_sampling_rate = ret;
	if (!google_odpm_match_int_sampling_rate(odpm, new_sampling_rate,
						 &new_sampling_rate_i)) {
		dev_err(odpm->dev, "odpm: cannot match new sampling rate value: %d uHz\n",
			new_sampling_rate);
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;

	if (!odpm->config_cpl) {
		dev_err(odpm->dev, "odpm: initial configuration is in progress, please do the sysfs operation again.\n");
		goto sampling_rate_store_exit;
	}

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_SAMPLING_RATE_IDX,
			       new_sampling_rate_i, 0);
	if (ret)
		goto sampling_rate_store_exit;

	odpm->chip.int_sampling_rate_index = new_sampling_rate_i;

	dev_info(odpm->dev, "odpm: Applied new sampling rate in uhz: %d\n",
		 new_sampling_rate);

sampling_rate_store_exit:
	mutex_unlock(&odpm->lock);
	return ret == 0 ? count : ret;
}

static ssize_t ext_sampling_rate_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int i = odpm->chip.ext_sampling_rate_index;
	u32 freq = odpm->chip.sampling_rate_ext_uhz[i];

	return scnprintf(buf, PAGE_SIZE, "%d.%06d (hz)\n", freq / UHZ_PER_HZ,
			 freq % UHZ_PER_HZ);
}

static ssize_t ext_sampling_rate_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);

	int new_sampling_rate_i;
	int new_sampling_rate;

	int ret = google_odpm_sampling_rate_verify(odpm, buf);

	if (ret < 0)
		return ret;

	new_sampling_rate = ret;
	if (!google_odpm_match_ext_sampling_rate(odpm, new_sampling_rate,
						 &new_sampling_rate_i)) {
		dev_err(odpm->dev, "odpm: cannot match new sampling rate value: %d uHz\n",
			new_sampling_rate);
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;

	if (!odpm->config_cpl) {
		dev_err(odpm->dev, "odpm: initial configuration is in progress, please do the sysfs operation again.\n");
		goto ext_sampling_rate_store_exit;
	}

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_EXT_SAMPLING_RATE_IDX,
			       new_sampling_rate_i, 0);
	if (ret)
		goto ext_sampling_rate_store_exit;

	odpm->chip.ext_sampling_rate_index = new_sampling_rate_i;

	dev_info(odpm->dev, "odpm: %s: Applied new ext sampling rate in uhz: %d\n",
		 odpm->chip.name, new_sampling_rate);

ext_sampling_rate_store_exit:
	mutex_unlock(&odpm->lock);
	return ret == 0 ? count : ret;
}

static int energy_value_read(struct google_odpm *odpm)
{
	u64 acc_read_start_ms, acc_read_duration_ms, acc_read_interval_ms, current_ms;
	int ret = 0;

	if (!odpm->config_cpl) {
		dev_err(odpm->dev, "odpm: initial configuration is in progress, please do the sysfs operation again.\n");
		return -EAGAIN;
	}

	acc_read_start_ms = ktime_to_ms(ktime_get_boottime());
	if (odpm->chip.acc_timestamp_ms && odpm->rate_limit_ms) {
		acc_read_interval_ms = ktime_sub(acc_read_start_ms, odpm->chip.acc_timestamp_ms);
		if (ktime_compare(acc_read_interval_ms, odpm->rate_limit_ms) < 0)
			return -EAGAIN;
	}

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_PMIC_TRANS, ODPM_TRANS_ACC, 0);
	if (ret != 0)
		return ret;

	odpm->chip.acc_transfer_started = true;
	odpm->chip.acc_data_ready = false;

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_DATA, ODPM_ACC_MODE, 0);
	if (ret != 0)
		return ret;

	while (!odpm->chip.acc_data_ready) {
		//TODO(b/414547668): Expose polling cycle and timeout data
		current_ms = ktime_to_ms(ktime_get_boottime());
		acc_read_duration_ms = ktime_sub(current_ms, acc_read_start_ms);
		if (ktime_compare(acc_read_duration_ms, ACC_READ_TIMEOUT_MS) > 0) {
			dev_err(odpm->dev, "ACC read latency exceeds allowed time: %llums\n",
				acc_read_duration_ms);
			return -ETIMEDOUT;
		}

		ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_DATA, ODPM_ACC_MODE, 0);
		if (ret != 0)
			return ret;

		usleep_range(ACC_READ_POLLING_MIN_US, ACC_READ_POLLING_MAX_US);
	}

	google_read_shared_memory(odpm, ODPM_ACC_MODE, ODPM_POWER);
	odpm->chip.acc_timestamp_ms = ktime_to_ms(ktime_get_boottime());

	return ret;
}

static void energy_value_update_acc_power(int ch, struct google_odpm *odpm)
{
	int rail_idx;
	u64 newly_increased_power = 0;
	u32 sampling_rate;

	rail_idx = odpm->channels[ch].rail_idx;
	if (odpm->chip.rails[rail_idx].rail_type == ODPM_RAIL_TYPE_LDO)
		newly_increased_power =
	    CALC_INC_POWER(odpm->channels[ch].data_read, DATA_POWER_LDO_FACTOR);
	else
		newly_increased_power =
		CALC_INC_POWER(odpm->channels[ch].data_read, DATA_POWER_FACTOR);

	if (odpm->chip.rails[rail_idx].power_coefficient)
		newly_increased_power =
		CALC_INC_POWER(newly_increased_power, odpm->chip.rails[rail_idx].power_coefficient);

	sampling_rate =
		odpm->channels[ch].external ?
		odpm->chip.sampling_rate_ext_uhz[odpm->chip.ext_sampling_rate_index] :
		odpm->chip.sampling_rate_int_uhz[odpm->chip.int_sampling_rate_index];

	odpm->chip.rails[rail_idx].acc_power +=
		newly_increased_power * UHZ_PER_HZ / sampling_rate;

	odpm->channels[ch].data_read = 0;
}

static ssize_t energy_value_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	ssize_t count = 0;
	int ch, ret;
	int rail_idx, telem_channel_idx;
	u64 start_ms, duration_ms;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;

	ret = energy_value_read(odpm);
	if (ret)
		goto energy_value_show_exit;

	/*
	 * Output format:
	 * t=<Measurement timestamp, ms>
	 * CH<N>(T=<Duration, ms>)[<Schematic name>], <Accumulated Energy, Ws>
	 */
	count += scnprintf(buf + count, PAGE_SIZE - count, "t=%llu\n",
			   odpm->chip.acc_timestamp_ms);

	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		rail_idx = odpm->channels[ch].rail_idx;
		telem_channel_idx = odpm->channels[ch].telem_channel_idx;
		start_ms = odpm->chip.rails[rail_idx].measurement_start_ms;
		duration_ms = odpm->chip.acc_timestamp_ms - start_ms;

		energy_value_update_acc_power(ch, odpm);

		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "CH%d(T=%llu)[%s], %llu\n", telem_channel_idx,
				   duration_ms,
				   odpm->chip.rails[rail_idx].schematic_name,
				   odpm->chip.rails[rail_idx].acc_power);

	}

energy_value_show_exit:
	mutex_unlock(&odpm->lock);
	if (ret)
		return ret;

	return count;
}

const struct odpm_rail_energy *google_odpm_get_rail_energy(void)
{
	int ch, ret;
	int rail_idx;
	u64 start_ms, duration_ms;

	if (!kern_if_odpm)
		return NULL;

	ret = mutex_lock_interruptible(&kern_if_odpm->lock);
	if (ret)
		return NULL;

	ret = energy_value_read(kern_if_odpm);
	if (ret)
		goto google_odpm_get_rail_energy_exit;

	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		rail_idx = kern_if_odpm->channels[ch].rail_idx;
		start_ms = kern_if_odpm->chip.rails[rail_idx].measurement_start_ms;
		duration_ms = kern_if_odpm->chip.acc_timestamp_ms - start_ms;

		energy_value_update_acc_power(ch, kern_if_odpm);

		kern_odpm_re[ch].regulator_name = kern_if_odpm->chip.rails[rail_idx].regulator_name;
		kern_odpm_re[ch].schematic_name = kern_if_odpm->chip.rails[rail_idx].schematic_name;
		kern_odpm_re[ch].subsystem_name = kern_if_odpm->chip.rails[rail_idx].subsystem_name;
		kern_odpm_re[ch].pmic_type      = kern_if_odpm->chip.rails[rail_idx].pmic_type;
		kern_odpm_re[ch].rail_type      = kern_if_odpm->chip.rails[rail_idx].rail_type;
		kern_odpm_re[ch].acc_energy     = kern_if_odpm->chip.rails[rail_idx].acc_power;
		kern_odpm_re[ch].timestamp_ms   = duration_ms;
	}

google_odpm_get_rail_energy_exit:
	mutex_unlock(&kern_if_odpm->lock);
	if (ret)
		return NULL;

	return kern_odpm_re;
}
EXPORT_SYMBOL_GPL(google_odpm_get_rail_energy);

static ssize_t virt_nodes_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	ssize_t count = 0;
	int ch, vn, tci, ret = 0;
	int ch_idx, rail_idx;
	u64 start_ms, rail_duration_ms, duration_ms;

	if (odpm->support_virt_nodes == false)
		return ret;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;

	ret = energy_value_read(odpm);
	if (ret)
		goto virt_nodes_show_exit;

	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch)
		energy_value_update_acc_power(ch, odpm);

	count += scnprintf(buf + count, PAGE_SIZE - count, "t=%llu\n",
			   odpm->chip.acc_timestamp_ms);

	for (vn = 0; vn < ODPM_VNODE_NUM; ++vn) {
		odpm->virt_nodes[vn].total_acc_power = 0;
		duration_ms = 0;
		for (tci = 0; tci < odpm->virt_nodes[vn].telem_channel_idx_count; ++tci) {
			ch_idx = google_odpm_get_channel_idx(
				odpm->virt_nodes[vn].telem_channel_idx[tci]);
			rail_idx = odpm->channels[ch_idx].rail_idx;
			odpm->virt_nodes[vn].total_acc_power +=
				odpm->chip.rails[rail_idx].acc_power;
			start_ms = odpm->chip.rails[rail_idx].measurement_start_ms;
			rail_duration_ms = odpm->chip.acc_timestamp_ms - start_ms;
			duration_ms = max(duration_ms, rail_duration_ms);
		}

		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "VN%d(T=%llu)[%s], %llu\n",
				   vn + 1,
				   duration_ms,
				   odpm->virt_nodes[vn].node_name,
				   odpm->virt_nodes[vn].total_acc_power);
	}

virt_nodes_show_exit:
	mutex_unlock(&odpm->lock);
	if (ret)
		return ret;

	return count;
}

static ssize_t available_rails_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int rail_idx;
	ssize_t count = 0;

	/* No locking needed for rail specific information */
	for (rail_idx = 0; rail_idx < odpm->chip.num_rails; ++rail_idx) {
		struct odpm_rail_data *rail = &odpm->chip.rails[rail_idx];

		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "%s(%s):%s\n", rail->name,
				   rail->schematic_name, rail->subsystem_name);
	}

	return count;
}

static ssize_t enabled_rails_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	struct odpm_rail_data *rail;
	ssize_t count = 0;
	int ch, ret;
	int rail_idx;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;
	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		if (!odpm->channels[ch].enabled)
			continue;
		rail_idx = odpm->channels[ch].rail_idx;
		rail = &odpm->chip.rails[rail_idx];

		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "CH%d[%s]:%s\n", odpm->channels[ch].telem_channel_idx,
				   rail->schematic_name, rail->subsystem_name);
	}
	mutex_unlock(&odpm->lock);

	return count;
}

static ssize_t enabled_rails_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);

	char rail_name[ODPM_RAIL_NAME_STR_LEN_MAX + 1];
	int telem_channel_idx;
	int ch = -1;
	int rail_idx;
	int scan_result;
	int current_rail_idx, new_rail_idx;
	int ret;

	scan_result = sscanf(buf, "CH%d=%s", &telem_channel_idx, rail_name);

	if (scan_result != 2) {
		dev_err(odpm->dev, "odpm: The valid syntax should be \"CH%%d=%%s\".\n");
		return -EINVAL;
	}
	if (!google_odpm_telem_channel_valid(telem_channel_idx)) {
		dev_err(odpm->dev, "odpm: Channel must be within [%d, %d] or [%d, %d]\n",
			ODPM_CHANNEL_M_MIN, ODPM_CHANNEL_M_MAX,
			ODPM_CHANNEL_S_MIN, ODPM_CHANNEL_S_MAX);
		return -EINVAL;
	}

	if (odpm->channels[google_odpm_get_channel_idx(telem_channel_idx)].external) {
		dev_err(odpm->dev, "odpm: External channel could not be selected.\n");
		return -EINVAL;
	}

	rail_idx = google_odpm_get_rail_idx(odpm, rail_name);
	if (rail_idx == odpm->chip.num_rails) {
		dev_err(odpm->dev, "odpm: The rail name is invalid.\n");
		return -EINVAL;
	}

	if (google_odpm_get_pmic_type(telem_channel_idx) != odpm->chip.rails[rail_idx].pmic_type) {
		dev_err(odpm->dev, "odpm: Channel %u cannot monitor %s\n",
			telem_channel_idx, odpm->chip.rails[rail_idx].regulator_name);
		return -EINVAL;
	}

	if (odpm->chip.rails[rail_idx].rail_type == ODPM_RAIL_TYPE_SHUNT) {
		dev_err(odpm->dev, "odpm: Internal channel %u cannot monitor external rails %s\n",
			telem_channel_idx, rail_name);
		return -EINVAL;
	}

	ch = google_odpm_get_channel_idx(telem_channel_idx);

	new_rail_idx = rail_idx;
	current_rail_idx = odpm->channels[ch].rail_idx;

	if (new_rail_idx == current_rail_idx) {
		/*
		 * Do not apply rail selection if the same rail is being
		 * replaced.
		 */
		return count;
	}

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;

	if (!odpm->config_cpl) {
		dev_err(odpm->dev, "odpm: initial configuration is in progress, please do the sysfs operation again.\n");
		goto enabled_rails_store_exit;
	}
	/* Send a refresh and store values for old rails */
	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_DATA, ODPM_ACC_MODE, 0);
	if (ret)
		goto enabled_rails_store_exit;

	google_read_shared_memory(odpm, ODPM_ACC_MODE, ODPM_POWER);

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_CHANNEL_ENABLED,
			       telem_channel_idx, 1);
	if (ret)
		goto enabled_rails_store_exit;

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_CHANNEL_RAILS,
			       telem_channel_idx, rail_idx);
	if (ret)
		goto enabled_rails_store_exit;

	/* Capture measurement time for current rail */
	odpm->chip.rails[current_rail_idx].measurement_stop_ms =
		ktime_to_ms(ktime_get_boottime());

	/* Assign new rail to channel */
	odpm->channels[ch].rail_idx = new_rail_idx;

	/* Record measurement start time / reset stop time */
	odpm->chip.rails[new_rail_idx].measurement_start_ms =
		ktime_to_ms(ktime_get_boottime());
	odpm->chip.rails[new_rail_idx].measurement_stop_ms = 0;
	dev_info(odpm->dev, "Applying new rail success.\n");

enabled_rails_store_exit:
	mutex_unlock(&odpm->lock);
	return ret == 0 ? count : ret;
}

/*
 * Check when measurements have started in ms from boot (channel-specific)
 * Format: <rail>, <start ms>
 */
static ssize_t measurement_start_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int ch, ret;
	int rail_idx;
	ssize_t count = 0;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;
	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		rail_idx = odpm->channels[ch].rail_idx;

		if (!odpm->channels[ch].enabled)
			continue;
		count += scnprintf(buf + count,
				   PAGE_SIZE - count,
				   "CH%d[%s], %llu\n",
				   odpm->channels[ch].telem_channel_idx,
				   odpm->chip.rails[rail_idx].schematic_name,
				   odpm->chip.rails[rail_idx].measurement_start_ms);
	}
	mutex_unlock(&odpm->lock);

	return count;
}

/*
 * If rails have been swapped out (by changing monitored rails),
 * check cached data for those rails (rail-specific)
 * Format: <rail>, <start ms>, <stop ms>, <energy>
 */
static ssize_t measurement_stop_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	struct odpm_rail_data *rail;
	int rail_idx, ret;
	ssize_t count = 0;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;
	for (rail_idx = 0; rail_idx < odpm->chip.num_rails; ++rail_idx) {
		rail = &odpm->chip.rails[rail_idx];

		if (rail->measurement_stop_ms == 0)
			continue;
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "%s(%s), %llu, %llu, %llu\n",
				   rail->name, rail->schematic_name,
				   rail->measurement_start_ms,
				   rail->measurement_stop_ms,
				   rail->acc_power);
	}
	mutex_unlock(&odpm->lock);

	return count;
}

static ssize_t avg_read(struct device *dev, int data_type)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int ret;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;

	if (!odpm->config_cpl) {
		dev_err(odpm->dev, "odpm: initial configuration is in progress, please do the sysfs operation again.\n");
		goto avg_show_exit;
	}

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_PMIC_TRANS, ODPM_TRANS_AVG, 0);
	if (ret)
		goto avg_show_exit;

	// Wait for data transfer to complete
	msleep(AVG_READ_INTERVAL_MS);

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_DATA, ODPM_AVG_MODE, 0);
	if (ret)
		goto avg_show_exit;

	google_read_shared_memory(odpm, ODPM_AVG_MODE, data_type);

avg_show_exit:
	mutex_unlock(&odpm->lock);
	return ret;
}

static ssize_t avg_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int telem_channel_idx;
	int scan_result;
	int ret;
	u32 coefficient;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;
	scan_result = sscanf(buf, "CH%d=%u", &telem_channel_idx, &coefficient);
	if (scan_result != 2) {
		dev_err(odpm->dev, "The syntax (%s) was invalid\n", buf);
		ret = -EINVAL;
		goto avg_store_exit;
	}
	if (!google_odpm_telem_channel_valid(telem_channel_idx)) {
		dev_err(odpm->dev, "Channel must be within [%d, %d] or [%d, %d]\n",
			ODPM_CHANNEL_M_MIN, ODPM_CHANNEL_M_MAX,
			ODPM_CHANNEL_S_MIN, ODPM_CHANNEL_S_MAX);
		ret = -EINVAL;
		goto avg_store_exit;
	}

	if (!odpm->config_cpl) {
		dev_err(odpm->dev, "odpm: initial configuration is in progress, please do the sysfs operation again.\n");
		goto avg_store_exit;
	}

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_AVG_COEFF,
			       telem_channel_idx, coefficient);

avg_store_exit:
	mutex_unlock(&odpm->lock);
	return ret == 0 ? count : ret;
}

/*
 * Output format:
 * t=<Measurement timestamp, ms>
 * CH<N>[<Schematic name>], <AVG current, A>
 */
static ssize_t avg_current_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	ssize_t count = 0;
	int ch, ret;
	int rail_idx;
	u64 avg_data_ua;

	ret = avg_read(dev, ODPM_CURRENT);
	if (ret)
		return ret;
	count += scnprintf(buf + count, PAGE_SIZE - count, "t=%lld\n",
			   ktime_to_ms(ktime_get_boottime()));
	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		rail_idx = odpm->channels[ch].rail_idx;
		if (odpm->chip.rails[rail_idx].rail_type == ODPM_RAIL_TYPE_LDO)
			avg_data_ua = (odpm->channels[ch].data_read *
				       DATA_CURRENT_LDO_FACTOR) >> DATA_RIGHT_SHIFTER;
		else
			avg_data_ua = (odpm->channels[ch].data_read *
				       DATA_CURRENT_FACTOR) >> DATA_RIGHT_SHIFTER;
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "CH%d[%s], %lld.%06lld\n", odpm->channels[ch].telem_channel_idx,
				   odpm->chip.rails[rail_idx].schematic_name,
				   avg_data_ua / DATA_SCALAR, avg_data_ua % DATA_SCALAR);
	}
	return count;
}

static ssize_t avg_current_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	return avg_store(dev, attr, buf, count);
}

int inst_read(struct device *dev, int data_type)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	int ret;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;

	if (!odpm->config_cpl) {
		dev_err(odpm->dev, "odpm: initial configuration is in progress, please do the sysfs operation again.\n");
		goto inst_read_exit;
	}

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_DATA, ODPM_INST_MODE, 0);
	if (ret)
		goto inst_read_exit;

	google_read_shared_memory(odpm, ODPM_INST_MODE, 0);

inst_read_exit:
	mutex_unlock(&odpm->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(inst_read);

/*
 * Output format:
 * t=<Measurement timestamp, ms>
 * CH<N>[<Schematic name>], <INST power, W>
 */
static ssize_t inst_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	ssize_t count = 0;
	int ch, ret;
	int rail_idx;
	u64 inst_data_uw;

	ret = inst_read(dev, ODPM_POWER);
	if (ret)
		return ret;
	count += scnprintf(buf + count, PAGE_SIZE - count, "t=%lld\n",
			   ktime_to_ms(ktime_get_boottime()));
	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		rail_idx = odpm->channels[ch].rail_idx;
		if (odpm->chip.rails[rail_idx].rail_type == ODPM_RAIL_TYPE_LDO)
			inst_data_uw = (odpm->channels[ch].data_read *
					DATA_POWER_LDO_FACTOR) >> DATA_RIGHT_SHIFTER;
		else
			inst_data_uw = (odpm->channels[ch].data_read *
					DATA_POWER_FACTOR) >> DATA_RIGHT_SHIFTER;
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "CH%d[%s], %lld.%06lld\n", odpm->channels[ch].telem_channel_idx,
				   odpm->chip.rails[rail_idx].schematic_name,
				   inst_data_uw / DATA_SCALAR, inst_data_uw % DATA_SCALAR);
	}

	return count;
}

static ssize_t debug_stats_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct google_odpm *odpm = iio_priv(indio_dev);
	ssize_t count = 0;
	int ch, ret;
	int rail_idx;

	ret = mutex_lock_interruptible(&odpm->lock);
	if (ret)
		return ret;

	if (!odpm->config_cpl) {
		dev_err(odpm->dev, "odpm: initial configuration is in progress, please do the sysfs operation again.\n");
		goto debug_stats_show_exit;
	}

	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_DEBUG, 0, 0);
	if (ret)
		goto debug_stats_show_exit;

	google_read_shared_memory(odpm, ODPM_DEBUG_MODE, ODPM_POWER);

debug_stats_show_exit:
	mutex_unlock(&odpm->lock);
	if (ret)
		return ret;

	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		rail_idx = odpm->channels[ch].rail_idx;

		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "CH%d[%s], %lld\n", odpm->channels[ch].telem_channel_idx,
				   odpm->chip.rails[rail_idx].schematic_name,
				   odpm->channels[ch].debug_stats);
	}

	return count;
}

static ssize_t rate_limit_ms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct google_odpm *odpm = iio_priv(dev_to_iio_dev(dev));

	if (odpm->rate_limit_ms)
		ret = scnprintf(buf, PAGE_SIZE, "%u\n", odpm->rate_limit_ms);
	else
		ret = scnprintf(buf, PAGE_SIZE, "disabled\n");

	return ret;
}

static ssize_t rate_limit_ms_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int ret;
	struct google_odpm *odpm = iio_priv(dev_to_iio_dev(dev));

	ret = kstrtou32(buf, 10, &odpm->rate_limit_ms);
	if (ret)
		return ret;

	return count;
}

static IIO_DEVICE_ATTR_RO(available_sampling_rate, 0);
static IIO_DEVICE_ATTR_RO(available_ext_sampling_rate, 0);
static IIO_DEVICE_ATTR_RW(sampling_rate, 0);
static IIO_DEVICE_ATTR_RW(ext_sampling_rate, 0);
static IIO_DEVICE_ATTR_RO(energy_value, 0);
static IIO_DEVICE_ATTR_RO(available_rails, 0);
static IIO_DEVICE_ATTR_RW(enabled_rails, 0);
static IIO_DEVICE_ATTR_RO(measurement_start, 0);
static IIO_DEVICE_ATTR_RO(measurement_stop, 0);
static IIO_DEVICE_ATTR_RW(avg_current, 0);
static IIO_DEVICE_ATTR_RO(inst_power, 0);
static IIO_DEVICE_ATTR_RO(debug_stats, 0);
static IIO_DEVICE_ATTR_RO(sample_count, 0);
static IIO_DEVICE_ATTR_RW(rate_limit_ms, 0);
static IIO_DEVICE_ATTR_RO(virt_nodes, 0);

#define ODPM_DEV_ATTR(name) (&iio_dev_attr_##name.dev_attr.attr)

static struct attribute *odpm_custom_attributes[] = {
	ODPM_DEV_ATTR(available_sampling_rate),
	ODPM_DEV_ATTR(available_ext_sampling_rate),
	ODPM_DEV_ATTR(sampling_rate),
	ODPM_DEV_ATTR(ext_sampling_rate),
	ODPM_DEV_ATTR(energy_value),
	ODPM_DEV_ATTR(available_rails),
	ODPM_DEV_ATTR(enabled_rails),
	ODPM_DEV_ATTR(measurement_start),
	ODPM_DEV_ATTR(measurement_stop),
	ODPM_DEV_ATTR(avg_current),
	ODPM_DEV_ATTR(inst_power),
	ODPM_DEV_ATTR(debug_stats),
	ODPM_DEV_ATTR(sample_count),
	ODPM_DEV_ATTR(rate_limit_ms),
	ODPM_DEV_ATTR(virt_nodes),
	NULL
};

static const struct attribute_group odpm_group = {
	.attrs = odpm_custom_attributes,
};

static const struct iio_info odpm_iio_info = {
	.attrs = &odpm_group,
};

static int google_odpm_parse_dt_channels(struct device_node *channels_np, struct google_odpm *odpm)
{
	int rail_idx, ch = 0;
	int num_channels = of_get_child_count(channels_np);
	struct device_node *iter_np;
	const char *rail_name;
	int telem_channel_idx;
	int scan_result;

	/* Check channel count */
	if (num_channels != ODPM_CHANNEL_NUM) {
		dev_err(odpm->dev, "odpm: expected %d channels, got %d\n", ODPM_CHANNEL_NUM,
			num_channels);
		return -EINVAL;
	}

	/* Parse channels */
	for_each_child_of_node(channels_np, iter_np) {
		/*
		 * Explicitly set enabled to false until we find the
		 * associated rail
		 */
		odpm->channels[ch].enabled = false;
		odpm->channels[ch].external = false;

		scan_result = sscanf(iter_np->full_name, "channel@%d", &telem_channel_idx);

		if (scan_result == 1 && google_odpm_telem_channel_valid(telem_channel_idx)) {
			odpm->channels[ch].telem_channel_idx = telem_channel_idx;
		} else {
			dev_err(odpm->dev, "odpm: Invalid input on %s\n",
				iter_np->full_name);
			return -EINVAL;
		}

		odpm->channels[ch].pmic_type = google_odpm_get_pmic_type(telem_channel_idx);
		if (odpm->channels[ch].pmic_type == PMIC_INVALID) {
			dev_err(odpm->dev, "odpm: invalid channel ID on %s\n",
				iter_np->full_name);
			return -EINVAL;
		}

		odpm->channels[ch].external =
			of_property_read_bool(iter_np, "external");

		/* Read rail name */
		if (of_property_read_string(iter_np, "rail-name", &rail_name)) {
			dev_err(odpm->dev, "odpm: invalid rail-name value on %s\n",
				iter_np->full_name);
			return -EINVAL;
		}

		/* Match rail name */
		rail_idx = google_odpm_get_rail_idx(odpm, rail_name);
		if (rail_idx == odpm->chip.num_rails) {
			dev_err(odpm->dev, "odpm: Could not find rail-name %s\n",
				rail_name);
			return -EINVAL;
		}
		odpm->channels[ch].rail_idx = rail_idx;

		/* Check if the channel and rail are in same PMIC */
		if (odpm->chip.rails[rail_idx].pmic_type != odpm->channels[ch].pmic_type) {
			dev_err(odpm->dev, "odpm: The channel cannot monitor %s\n",
				rail_name);
			return -EINVAL;
		}

		/* Check if the channel is enabled or not */
		odpm->channels[ch].enabled =
			of_property_read_bool(iter_np, "channel_enabled");

		++ch;
	}

	return 0;
}

static u32 google_odpm_shunt_uohm_to_register(u32 uohm)
{
	if (uohm < MIN_SHUNT_RES_UOHM || uohm > MAX_SHUNT_RES_UOHM)
		uohm = 10000;
	return SHUNT_RES_REG_CONST / uohm;
}

static int google_odpm_parse_dt_rail(struct google_odpm *odpm, struct odpm_rail_data *rail_data,
				     struct device_node *rail_np, enum odpm_pmic_type pmic_type)
{
	u32 shunt_res_uohm;

	if (!rail_np->name) {
		dev_err(odpm->dev, "cannot read node name\n");
		return -EINVAL;
	}
	rail_data->name = rail_np->name;
	rail_data->pmic_type = pmic_type;

	/*
	 * If the read fails, the pointers are not assigned; thus the pointers
	 * are left NULL if the values don't exist in the DT
	 */
	if (of_property_read_string(rail_np, "regulator-name",
				    &rail_data->regulator_name)) {
		dev_err(odpm->dev, "cannot read regulator-name\n");
		return -EINVAL;
	}

	if (of_property_read_string(rail_np, "google,schematic-name",
				    &rail_data->schematic_name)) {
		dev_err(odpm->dev, "cannot read google,schematic-name\n");
		return -EINVAL;
	}

	if (of_property_read_string(rail_np, "google,subsystem-name",
				    &rail_data->subsystem_name)) {
		dev_err(odpm->dev, "cannot read google,subsystem-name\n");
		return -EINVAL;
	}

	if (!of_property_read_u32(rail_np, "google,power-coefficient",
				  &rail_data->power_coefficient)) {
		dev_info(odpm->dev, "power coefficient is loaded for %s: %u",
			 rail_data->schematic_name, rail_data->power_coefficient);
	}

	if (of_property_read_bool(rail_np, "google,external-rail")) {
		rail_data->rail_type = ODPM_RAIL_TYPE_SHUNT;

		if (of_property_read_u32(rail_np, "google,shunt-res-uohm", &shunt_res_uohm)) {
			dev_err(odpm->dev, "cannot read \"google,shunt-res-uohm\" for %s\n",
				rail_data->schematic_name);
			return -EINVAL;
		} else {
			rail_data->shunt_res_reg =
					google_odpm_shunt_uohm_to_register(shunt_res_uohm);
		}
	} else if (of_property_read_bool(rail_np, "google,buck-rail"))
		rail_data->rail_type = ODPM_RAIL_TYPE_BUCK;
	else
		rail_data->rail_type = ODPM_RAIL_TYPE_LDO;

	rail_data->measurement_stop_ms = 0;

	return 0;
}

static int google_odpm_parse_dt_rails(struct google_odpm *odpm, struct device_node *pmic_np,
				      struct device_node *odpm_np)
{
	struct device_node *iter_np, *regulators_np, *pmic_main_np, *pmic_sub_np;
	struct device_node *ext_rails_np, *pmic_main_ext_rails_np, *pmic_sub_ext_rails_np;
	struct odpm_rail_data *rail_data;
	int rail_idx = 0, num_rails = 0;

	regulators_np = of_find_node_by_name(pmic_np, "regulators");
	if (!regulators_np) {
		dev_err(odpm->dev, "odpm: cannot find regulators_np sub-node!\n");
		return -ENODEV;
	}

	pmic_main_np = of_find_node_by_name(regulators_np, "da9188");
	if (!pmic_main_np) {
		dev_err(odpm->dev, "odpm: cannot find pmic_main_np sub-node!\n");
		return -ENODEV;
	}

	pmic_sub_np = of_find_node_by_name(regulators_np, "da9189");
	if (!pmic_sub_np) {
		dev_err(odpm->dev, "odpm: cannot find pmic_sub_np sub-node!\n");
		return -ENODEV;
	}

	ext_rails_np = of_find_node_by_name(odpm_np, "ext_rails");
	if (!ext_rails_np) {
		dev_err(odpm->dev, "odpm: cannot find ext_rails_np sub-node!\n");
		return -ENODEV;
	}

	pmic_main_ext_rails_np = of_find_node_by_name(ext_rails_np, "pmic_main");
	if (!pmic_main_ext_rails_np) {
		dev_err(odpm->dev, "odpm: cannot find pmic_main_ext_rails_np sub-node!\n");
		return -ENODEV;
	}

	pmic_sub_ext_rails_np = of_find_node_by_name(ext_rails_np, "pmic_sub");
	if (!pmic_sub_ext_rails_np) {
		dev_err(odpm->dev, "odpm: cannot find pmic_sub_ext_rails_np sub-node!\n");
		return -ENODEV;
	}
	/* Count rails */
	num_rails += of_get_child_count(pmic_main_np);
	num_rails += of_get_child_count(pmic_sub_np);
	num_rails += of_get_child_count(pmic_main_ext_rails_np);
	num_rails += of_get_child_count(pmic_sub_ext_rails_np);
	if (num_rails <= 0) {
		dev_err(odpm->dev, "odpm: Could not find any rails\n");
		return -EINVAL;
	}
	odpm->chip.num_rails = num_rails;

	/* Allocate/Initialize rails */
	rail_data = devm_kcalloc(odpm->dev, num_rails, sizeof(*rail_data), GFP_KERNEL);
	if (!rail_data)
		return -ENOMEM;

	odpm->chip.rails = rail_data;

	/* Populate rail data */
	for_each_child_of_node(pmic_main_np, iter_np) {
		int ret = google_odpm_parse_dt_rail(odpm, &rail_data[rail_idx], iter_np, PMIC_MAIN);
		if (ret != 0)
			return ret;
		++rail_idx;
	}

	for_each_child_of_node(pmic_sub_np, iter_np) {
		int ret = google_odpm_parse_dt_rail(odpm, &rail_data[rail_idx], iter_np, PMIC_SUB);
		if (ret != 0)
			return ret;
		++rail_idx;
	}

	for_each_child_of_node(pmic_main_ext_rails_np, iter_np) {
		int ret = google_odpm_parse_dt_rail(odpm, &rail_data[rail_idx], iter_np, PMIC_MAIN);
		if (ret != 0)
			return ret;
		++rail_idx;
	}

	for_each_child_of_node(pmic_sub_ext_rails_np, iter_np) {
		int ret = google_odpm_parse_dt_rail(odpm, &rail_data[rail_idx], iter_np, PMIC_SUB);
		if (ret != 0)
			return ret;
		++rail_idx;
	}

	/* Confidence check rail count */
	if (rail_idx != num_rails) {
		dev_err(odpm->dev, "odpm: expected %d rails, got %d\n", num_rails, rail_idx);
		return -EINVAL;
	}

	return 0;
}

static int google_odpm_parse_dt_virt_nodes(struct device_node *odpm_np, struct google_odpm *odpm)
{
	struct device_node *virt_nodes_np, *iter_np;
	int idx = 0, ret = 0, tci;

	virt_nodes_np = of_find_node_by_name(odpm_np, "virt_nodes");
	if (!virt_nodes_np) {
		odpm->support_virt_nodes = false;
		dev_warn(odpm->dev, "odpm: cannot find virtual nodes DT node!\n");
		return ret;
	}

	odpm->support_virt_nodes = true;
	for_each_child_of_node(virt_nodes_np, iter_np) {
		if (idx >= ODPM_VNODE_NUM) {
			dev_err(odpm->dev, "odpm: invalid index in virtual node parsing\n");
			ret = -EINVAL;
			goto parse_dt_virt_nodes_error_exit;
		}

		odpm->virt_nodes[idx].total_acc_power = 0;

		if (of_property_read_string(iter_np, "node-name",
				    &odpm->virt_nodes[idx].node_name)) {
			dev_err(odpm->dev, "odpm: cannot read virtual node name\n");
			ret = -EINVAL;
			goto parse_dt_virt_nodes_error_exit;
		}

		if (of_property_read_u32(iter_np, "chan-idxs-count",
				 &odpm->virt_nodes[idx].telem_channel_idx_count)) {
			dev_err(odpm->dev, "odpm: cannot read chan-idxs-count\n");
			ret = -EINVAL;
			goto parse_dt_virt_nodes_error_exit;
		}

		odpm->virt_nodes[idx].telem_channel_idx =
			devm_kcalloc(odpm->dev, odpm->virt_nodes[idx].telem_channel_idx_count,
					sizeof(u32), GFP_KERNEL);

		if (!odpm->virt_nodes[idx].telem_channel_idx) {
			dev_err(odpm->dev, "odpm: failed to allocate telem_channel_idx\n");
			ret = -ENOMEM;
			goto parse_dt_virt_nodes_error_exit;
		}

		if (of_property_read_u32_array(iter_np, "chan-idxs",
				       odpm->virt_nodes[idx].telem_channel_idx,
				       odpm->virt_nodes[idx].telem_channel_idx_count)) {
			dev_err(odpm->dev, "odpm: cannot read virtual node channel idxs\n");
			ret = -EINVAL;
			goto parse_dt_virt_nodes_error_exit;
		}

		for (tci = 0; tci < odpm->virt_nodes[idx].telem_channel_idx_count; ++tci) {
			if (!google_odpm_telem_channel_valid(
				odpm->virt_nodes[idx].telem_channel_idx[tci])) {
				dev_err(odpm->dev,
					"odpm: VN%d: Channel must be within [%d, %d] or [%d, %d]\n",
					idx + 1,
					ODPM_CHANNEL_M_MIN, ODPM_CHANNEL_M_MAX,
					ODPM_CHANNEL_S_MIN, ODPM_CHANNEL_S_MAX);
				ret = -EINVAL;
				goto parse_dt_virt_nodes_error_exit;
			}
		}
		idx++;
	}
	goto parse_dt_virt_nodes_exit;

parse_dt_virt_nodes_error_exit:
	of_node_put(iter_np);
parse_dt_virt_nodes_exit:
	of_node_put(virt_nodes_np);

	return ret;
}

static int google_odpm_parse_dt(struct device *dev, struct google_odpm *odpm)
{
	struct device_node *odpm_np = dev->of_node;
	struct device_node *pmic_np, *channels_np;
	int ret;

	if (!odpm_np) {
		dev_err(odpm->dev, "odpm: cannot find odpm node!\n");
		return -ENODEV;
	}

	if (of_property_read_u32(odpm_np, "mba-dest-channel", &odpm->remote_ch)) {
		dev_err(odpm->dev, "Failed to read mba-dest-channel.\n");
		return -EINVAL;
	}

	if (of_property_read_u32(odpm_np, "sample-rate-count",
				 &odpm->chip.sampling_rate_int_count)) {
		dev_err(odpm->dev, "odpm: cannot read sample-rate-count\n");
		return -EINVAL;
	}
	odpm->chip.sampling_rate_int_uhz =
		devm_kcalloc(odpm->dev, odpm->chip.sampling_rate_int_count,
			     sizeof(u32), GFP_KERNEL);
	if (!odpm->chip.sampling_rate_int_uhz) {
		dev_err(odpm->dev, "odpm: cannot allocate internal sampling rate!\n");
		return -ENOMEM;
	}
	if (of_property_read_u32_array(odpm_np, "sample-rate-uhz",
				       odpm->chip.sampling_rate_int_uhz,
				       odpm->chip.sampling_rate_int_count)) {
		dev_err(odpm->dev, "odpm: cannot read sample-rate-uhz\n");
		return -EINVAL;
	}
	if (of_property_read_u32(odpm_np, "sample-rate-index",
				 &odpm->chip.int_sampling_rate_index)) {
		dev_err(odpm->dev, "odpm: cannot read sample-rate-index\n");
		return -EINVAL;
	}

	if (of_property_read_u32(odpm_np, "sample-rate-external-count",
				 &odpm->chip.sampling_rate_ext_count)) {
		dev_err(odpm->dev, "odpm: cannot read sample-rate-external-count\n");
		return -EINVAL;
	}
	odpm->chip.sampling_rate_ext_uhz =
		devm_kcalloc(odpm->dev, odpm->chip.sampling_rate_ext_count, sizeof(u32),
			     GFP_KERNEL);
	if (!odpm->chip.sampling_rate_ext_uhz) {
		dev_err(odpm->dev, "odpm: cannot allocate external sampling rate!\n");
		return -ENOMEM;
	}
	if (of_property_read_u32_array(odpm_np, "sample-rate-external-uhz",
				       odpm->chip.sampling_rate_ext_uhz,
				       odpm->chip.sampling_rate_ext_count)) {
		dev_err(odpm->dev, "odpm: cannot read sample-rate-external-uhz\n");
		return -EINVAL;
	}
	if (of_property_read_u32(odpm_np, "sample-rate-external-index",
				 &odpm->chip.ext_sampling_rate_index)) {
		dev_err(odpm->dev, "odpm: cannot read sample-rate-external-index\n");
		return -EINVAL;
	}

	if (of_property_read_u32(odpm_np, "shared-memory-size",
				 &odpm->shared_memory_size)) {
		dev_err(odpm->dev, "odpm: cannot read shared-memory-size\n");
		return -EINVAL;
	}

	if (of_property_read_u32(odpm_np, "protocol-version",
				 &odpm->protocol_version)) {
		dev_err(odpm->dev, "odpm: cannot read protocol-version\n");
		return -EINVAL;
	}

	pmic_np = of_parse_phandle(odpm_np, "pmic", 0);
	if (!pmic_np) {
		dev_err(odpm->dev, "odpm: cannot find pmic DT node!\n");
		return -ENODEV;
	}

	ret = google_odpm_parse_dt_rails(odpm, pmic_np, odpm_np);
	if (ret != 0)
		return ret;

	channels_np = of_find_node_by_name(odpm_np, "channels");
	if (!channels_np) {
		dev_err(odpm->dev, "odpm: cannot find channels DT node!\n");
		return -ENODEV;
	}

	ret = google_odpm_parse_dt_channels(channels_np, odpm);
	if (ret != 0)
		return ret;

	ret = google_odpm_parse_dt_virt_nodes(odpm_np, odpm);
	if (ret != 0)
		return ret;

	return 0;
}

static inline void google_odpm_mbox_free(struct google_odpm *odpm)
{
	if (!IS_ERR_OR_NULL(odpm->client))
		cpm_iface_free_client(odpm->client);
}

static void google_odpm_platform_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(&pdev->dev);
	struct google_odpm *odpm = iio_priv(indio_dev);

	if (odpm->wq) {
		cancel_work_sync(&odpm->mba_send_work);
		flush_workqueue(odpm->wq);
		destroy_workqueue(odpm->wq);
	}

	google_odpm_mbox_free(odpm);
}

static void wq_resp_handler(struct work_struct *work)
{
	struct google_odpm *odpm;
	struct list_head list;
	struct mba_send_node *node;
	unsigned long flags;
	int ch, rail_idx;
	int ret;

	odpm = container_of(work, struct google_odpm, mba_send_work);
	spin_lock_irqsave(&odpm->mba_send_list_lock, flags);
	list_replace_init(&odpm->mba_send_list, &list);
	spin_unlock_irqrestore(&odpm->mba_send_list_lock, flags);
	while (!list_empty(&list)) {
		node = list_first_entry(&list, typeof(*node), list);
		ret = google_odpm_send(odpm, node->type, node->value0, node->value1);

		if (ret < 0) {
			list_move_tail(&node->list, &list);
			continue;
		}
		if (node->type == ODPM_MBA_MSG_TYPE_CHANNEL_ENABLED &&
		    node->value1 == CHANNEL_ENABLED) {
			for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
				if (odpm->channels[ch].telem_channel_idx == node->value0)
					break;
			}

			/*
			 * To avoid the lack of ODPM data in westworld, the boot
			 * measurement time should be set to 0. This will result
			 * in a period where power measurements are not accounted
			 * for during boot.
			 */
			rail_idx = odpm->channels[ch].rail_idx;
			odpm->chip.rails[rail_idx].measurement_start_ms = 0;
		}
		list_del(&node->list);
		devm_kfree(odpm->dev, node);
	}
}

static int google_odpm_init_mbox(struct google_odpm *odpm)
{
	int ret;

	odpm->client = cpm_iface_request_client(odpm->dev, CPM_AP_NS_ODPM_MB_SERVICE,
						google_odpm_mba_rx_callback, odpm);
	if (IS_ERR(odpm->client)) {
		ret = PTR_ERR(odpm->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(odpm->dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(odpm->dev, "Failed to request cpm client err: %d\n", ret);
		return ret;
	}

	return 0;
}

struct iio_dev *google_get_odpm_iio_dev(void)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct iio_dev *indio_dev;

	np = of_find_compatible_node(NULL, NULL, "google,odpm");
	if (!np || !virt_addr_valid(np) || !of_device_is_available(np))
		return NULL;
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return NULL;
	indio_dev = platform_get_drvdata(pdev);
	if (IS_ERR_OR_NULL(indio_dev))
		return NULL;

	return indio_dev;
}
EXPORT_SYMBOL_GPL(google_get_odpm_iio_dev);

static int google_odpm_platform_probe(struct platform_device *pdev)
{
	struct google_odpm *odpm;
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	int ret, ch, rail_idx, ext;
	struct odpm_rail_data rail;

	/*
	 * Allocate the memory for our private structure
	 * related to the chip info structure
	 */
	indio_dev = devm_iio_device_alloc(dev, sizeof(*odpm));
	if (!indio_dev) {
		pr_err("odpm: Could not allocate device!\n");
		ret = -ENOMEM;
		goto probe_exit;
	}
	platform_set_drvdata(pdev, indio_dev);

	/*
	 * Point our chip info structure towards
	 * the address freshly allocated
	 */
	odpm = kern_if_odpm = iio_priv(indio_dev);
	odpm->dev = dev;
	ret = google_odpm_parse_dt(dev, odpm);
	if (ret != 0) {
		dev_err(dev, "DT parsing error!\n");
		goto probe_exit;
	}

	/* Setup IIO */
	indio_dev->info = &odpm_iio_info;
	indio_dev->name = pdev->name;
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	ret = devm_iio_device_register(dev, indio_dev);
	if (ret < 0) {
		dev_err(dev, "Register iio device failed. Ret (%d)\n", ret);
		goto probe_exit;
	}

	ret = google_odpm_init_mbox(odpm);
	if (ret)
		goto probe_exit;

	mutex_init(&odpm->lock);
	odpm->config_cpl = false;
	odpm->rate_limit_ms = TIME_ALLOWED_TO_UPDATE_MS;

	odpm->wq = alloc_ordered_workqueue("odpm_wq", 0);
	if (!odpm->wq) {
		ret = -ENOMEM;
		goto probe_exit;
	}
	spin_lock_init(&odpm->mba_send_list_lock);
	INIT_LIST_HEAD(&odpm->mba_send_list);
	INIT_WORK(&odpm->mba_send_work, wq_resp_handler);

	/* Request for the shared-memory address */
	ret = google_odpm_send(odpm, ODPM_MBA_MSG_TYPE_ADDR, 0, 0);
	if (ret)
		goto probe_exit;

	/* Send channel config */
	for (ch = 0; ch < ODPM_CHANNEL_NUM; ++ch) {
		/* Enable each channel */
		ret = google_odpm_enqueue_odpm_node(odpm,
						    ODPM_MBA_MSG_TYPE_CHANNEL_ENABLED,
						    odpm->channels[ch].telem_channel_idx,
						    odpm->channels[ch].enabled);
		if (ret)
			goto probe_exit;

		if (!odpm->channels[ch].external) {
			/* Set the monitoring rail of each channel */
			ret = google_odpm_enqueue_odpm_node(odpm,
							    ODPM_MBA_MSG_TYPE_CHANNEL_RAILS,
							    odpm->channels[ch].telem_channel_idx,
							    odpm->channels[ch].rail_idx);
			if (ret)
				goto probe_exit;
		}
	}

	/* Reset pmic telemetry to 0 */
	ret = google_odpm_enqueue_odpm_node(
			odpm, ODPM_MBA_MSG_TYPE_RESET_TELEM, GPADC_DATA_CLR_VAL, 0);
	if (ret)
		goto probe_exit;

	/* Set shunt resistor for each external rail */
	ext = 0;
	for (rail_idx = 0; rail_idx < odpm->chip.num_rails; rail_idx++) {
		rail = odpm->chip.rails[rail_idx];
		if (rail.rail_type != ODPM_RAIL_TYPE_SHUNT)
			continue;

		ret = google_odpm_enqueue_odpm_node(
				odpm,
				ODPM_MBA_MSG_TYPE_SHUNT_RES,
				ext++,
				rail.shunt_res_reg);
		if (ret)
			goto probe_exit;

		dev_info(dev, "sent CPM request to set shunt resistor 0x%X for %s\n",
			 rail.shunt_res_reg, rail.schematic_name);
	}


	/* Set default sampling rate from DTS */
	ret = google_odpm_enqueue_odpm_node(
			odpm,
			ODPM_MBA_MSG_TYPE_SAMPLING_RATE_IDX,
			odpm->chip.int_sampling_rate_index,
			0);
	ret = google_odpm_enqueue_odpm_node(
			odpm,
			ODPM_MBA_MSG_TYPE_EXT_SAMPLING_RATE_IDX,
			odpm->chip.ext_sampling_rate_index,
			0);

	/* Set instant power mode */
	ret = google_odpm_enqueue_odpm_node(
			odpm, ODPM_MBA_MSG_TYPE_MEAS_MODE, ODPM_POWER, 0);
	if (ret)
		goto probe_exit;

	/* Set accumulative power mode */
	ret = google_odpm_enqueue_odpm_node(
			odpm, ODPM_MBA_MSG_TYPE_ACC_MODE, ODPM_ACC_POW, 0);
	if (ret)
		goto probe_exit;

	/* Set average current mode */
	ret = google_odpm_enqueue_odpm_node(
			odpm, ODPM_MBA_MSG_TYPE_AVG_MODE, ODPM_AVG_CURR, 0);
	if (ret)
		goto probe_exit;

	/* Set continuous mode */
	ret = google_odpm_enqueue_odpm_node(
			odpm, ODPM_MBA_MSG_TYPE_SAMPLING_MODE, ODPM_SAMPLING_CONTINUOUS, 0);
	if (ret)
		goto probe_exit;

	/* Trigger acc transfer */
	ret = google_odpm_enqueue_odpm_node(
			odpm, ODPM_MBA_MSG_TYPE_PMIC_TRANS, ODPM_TRANS_ACC, 0);
	if (ret)
		goto probe_exit;

	/* Enable the telemetry measurement */
	ret = google_odpm_enqueue_odpm_node(
			odpm, ODPM_MBA_MSG_TYPE_MEAS_SWITCH, MEASUREMENT_ENABLED, 0);
	if (ret)
		goto probe_exit;

	queue_work(odpm->wq, &odpm->mba_send_work);

	odpm->ready = true;

	dev_info(dev, "odpm: %s: probe completed\n", pdev->name);
	return ret;

probe_exit:
	dev_warn(dev, "%s: odpm: probe failure. ret = %d\n", pdev->name, ret);
	google_odpm_platform_remove(pdev);
	return ret;
}

static const struct of_device_id google_odpm_of_match_table[] = {
	{ .compatible = "google,odpm" },
	{},
};
MODULE_DEVICE_TABLE(of, google_odpm_of_match_table);

static struct platform_driver google_odpm_platform_driver = {
	.probe = google_odpm_platform_probe,
	.remove = google_odpm_platform_remove,
	.driver = {
		.name = "google-odpm",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_odpm_of_match_table),
	},
};
module_platform_driver(google_odpm_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google IIO ODPM Driver");
MODULE_LICENSE("GPL");
