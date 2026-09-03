// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_ntc_helper.c Helper for NTC thermistor functionality.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include "thermal_ntc_helper.h"
#include "thermal_ntc_mock.h"

#define THERMAL_NTC_THRESHOLD_BIT_SHIFT 4
#define THERMAL_NTC_TEMPERATURE_TO_THRESHOLD(_temp) \
	(thermal_ntc_map_data(_temp, ADC_MAP_TEMPERATURE, adc_thresh_data, \
			      ARRAY_SIZE(adc_thresh_data)) >> THERMAL_NTC_THRESHOLD_BIT_SHIFT)

#define THERMAL_NTC_STATS_GROUP "spmic"
#define THERMAL_NTC_LOG_BUF_IDX(idx) \
		((TEMPERATURE_LOG_BUF_LEN + (idx)) % TEMPERATURE_LOG_BUF_LEN)
#define THERMAL_NTC_IRQ_GUARD_BAND_MILLIC 500

static const int ntc_stats_thresholds[MAX_SUPPORTED_THRESHOLDS] = {0, 39000, 43000, 45000,
								    46500, 52000, 55000, 70000 };

static bool read_ntc_avg_data, enable_ntc_avg_read_feature, filter_spurious_reading;
static int dt_hot_trip = THERMAL_TEMP_INVALID;
DEFINE_MUTEX(thermal_ntc_irq_lock);

/*
 * thermal_ntc_map_data - Map the register value to temperature reading.
 *
 * This function does a binary search in the register value to temperature map list
 * and finds the range between which the register value falls. Then does a slope
 * calculation and estimates the temperature based on the slope.
 *
 * @input: register or temperature value.
 * @inp_type: search value type.
 * @data: The 2D integer register to temperature map to use.
 * @data_len: number of register, temperature pair in the 'data' array.
 *
 * Return: temperature in milli-Celsius or the register value.
 */
int thermal_ntc_map_data(int input, enum adc_map_index inp_type,
			 int data[][ADC_MAP_INDEX_MAX], size_t data_len)
{
	int low = 0;
	int high = data_len - 1;
	int mid = 0;
	enum adc_map_index search_type = ((inp_type == ADC_MAP_REGISTER_VALUE) ?
					  ADC_MAP_TEMPERATURE : ADC_MAP_REGISTER_VALUE);

	if (inp_type >= ADC_MAP_INDEX_MAX || high < 0)
		return -EINVAL;
	if (inp_type == ADC_MAP_REGISTER_VALUE) {
		if (data[low][inp_type] <= input)
			return data[low][search_type];
		if (data[high][inp_type] >= input)
			return data[high][search_type];
	} else {
		if (data[low][inp_type] >= input)
			return data[low][search_type];
		if (data[high][inp_type] <= input)
			return data[high][search_type];
	}

	/* Binary search, value will be between index low and low - 1 */
	while (low <= high) {
		mid = (low + high) / 2;
		if (data[mid][inp_type] == input)
			return data[mid][search_type];

		if (inp_type == ADC_MAP_REGISTER_VALUE) {
			if (data[mid][inp_type] < input)
				high = mid - 1;
			else
				low = mid + 1;
		} else {
			if (data[mid][inp_type] < input)
				low = mid + 1;
			else
				high = mid - 1;
		}
	}

	return data[low][search_type] +
	       mult_frac(data[low - 1][search_type] - data[low][search_type],
			 input - data[low][inp_type],
			 data[low - 1][inp_type] - data[low][inp_type]);
}

/*
 * thermal_ntc_configure_sensor - setup individual sensor related configurations
 *
 * @sens: sensor data
 *
 * Return: 0 on success.
 */
int thermal_ntc_configure_sensor(struct google_sensor_data *sens)
{
	int ret = 0;

	// Register for temperature residency stats.
	// Failure to register stats should not result in probe error.
	tr_handle tr_stats_handle = register_ntc_temp_residency_stats(
		thermal_zone_device_type(sens->tzd), THERMAL_NTC_STATS_GROUP);
	if (tr_stats_handle < 0) {
		dev_err(sens->dev,
			"Failed to register temperature residency stats for sensor:%s. ret: %d\n",
			thermal_zone_device_type(sens->tzd), tr_stats_handle);
		goto ntc_configure_err;
	}

	ret = ntc_temp_residency_stats_set_thresholds(tr_stats_handle, ntc_stats_thresholds,
						      ARRAY_SIZE(ntc_stats_thresholds));
	if (ret < 0) {
		dev_err(sens->dev, "Failed to set ntc_stats_thresholds for sensor:%s. ret = %d\n",
			thermal_zone_device_type(sens->tzd), ret);
		goto ntc_configure_err;
	}

	sens->sens_data.ntc_data.tr_stats_handle = tr_stats_handle;
	return 0;

ntc_configure_err:
	sens->sens_data.ntc_data.tr_stats_handle = -EINVAL;
	return 0;
}

/*
 * thermal_ntc_cleanup_sensor - clear individual sensor related configurations
 *
 * @sens: sensor data
 *
 * Return: 0 on success.
 *	- Otherwise errors returned by unregister_stats
 */
int thermal_ntc_cleanup_sensor(struct google_sensor_data *sens)
{
	tr_handle tr_stats_handle = sens->sens_data.ntc_data.tr_stats_handle;
	int ret = 0;

	if (tr_stats_handle >= 0) {
		ret = unregister_ntc_temp_residency_stats(tr_stats_handle);
		if (ret < 0) {
			dev_warn(
				sens->dev,
				"Failed to unregister stats for sensor:%s. ret: %d\n",
				thermal_zone_device_type(sens->tzd), ret);
		}
	}

	return ret;
}

void __thermal_ntc_temp_log_update(struct google_sensor_data *gsens, int temp, int reg)
{
	struct ntc_sensor_data *sens = &gsens->sens_data.ntc_data;
	int iter = THERMAL_NTC_LOG_BUF_IDX(sens->log_ct - 1);

	if (sens->temp_log[iter].temperature == temp) {
		sens->temp_log[iter].read_ct++;
		return;
	}

	iter = sens->log_ct;
	sens->temp_log[iter].time = ktime_get_real();
	sens->temp_log[iter].temperature = temp;
	sens->temp_log[iter].reg = reg;
	sens->temp_log[iter].read_ct = 1;
	sens->log_ct = (sens->log_ct + 1) % TEMPERATURE_LOG_BUF_LEN;
}

int __thermal_ntc_get_filtered_temp(struct google_sensor_data *gsens)
{
	struct ntc_sensor_data *sens = &gsens->sens_data.ntc_data;
	int prev_temp_idx = 0, latest_temp_idx = 0;

	/* Get the previous valid reading. */
	prev_temp_idx = THERMAL_NTC_LOG_BUF_IDX(sens->log_ct - 2);
	/* Get the error reading index to get the first occurrence. */
	latest_temp_idx = THERMAL_NTC_LOG_BUF_IDX(sens->log_ct - 1);

	if (sens->temp_log[latest_temp_idx].read_ct == 1 ||
		ktime_ms_delta(ktime_get_real(), sens->temp_log[latest_temp_idx].time) <
		THERMAL_NTC_ERR_READING_IGNORE_TIME_MSEC) {
		dev_dbg(gsens->dev, "Filtering spurious reading for sensor %s.\n",
			thermal_zone_device_type(gsens->tzd));
		return sens->temp_log[prev_temp_idx].temperature;
	}
	dev_err(gsens->dev, "%s spurious reading persisted %d msec. Reading actual value.",
		thermal_zone_device_type(gsens->tzd),
		THERMAL_NTC_ERR_READING_IGNORE_TIME_MSEC);
	return sens->temp_log[latest_temp_idx].temperature;
}

void __thermal_ntc_dump_thermal_history(struct google_sensor_data *gsens)
{
	struct ntc_sensor_data *sens = &gsens->sens_data.ntc_data;
	int i = 0, ct = 0;

	dev_info(gsens->dev, "Dump thermal reading of %s", thermal_zone_device_type(gsens->tzd));
	for (i = 0; i < TEMPERATURE_LOG_BUF_LEN; i++) {
		ct = THERMAL_NTC_LOG_BUF_IDX(sens->log_ct + i);
		dev_info(gsens->dev, "%lld: Raw:%d, temp:%d, count:%d",
				sens->temp_log[ct].time, sens->temp_log[ct].reg,
				sens->temp_log[ct].temperature,
				sens->temp_log[ct].read_ct);
	}
}

/*
 * thermal_ntc_get_temp - Get NTC thermistor temperature.
 *
 * @tzd: Thermal zone device.
 * @temp: pointer where the temperature will be populated.
 *
 * Return: 0 on success.
 *	- 0 in case of stats update failures, ignoring error ret values.
 *	- Other errors returned by msg_ntc_channel_read_temp().
 */
int thermal_ntc_get_temp(struct thermal_zone_device *tzd, int *temp)
{
	struct google_sensor_data *sens = thermal_zone_device_priv(tzd);
	int ret = 0, reg_data = 0;
	bool read_ntc_avg_data_local = (read_ntc_avg_data &&
					!sens->sens_data.ntc_data.irq_handle_phase);

	if (sens->sens_data.ntc_data.irq_handle_phase &&
	    (sens->sens_data.ntc_data.irq_temp != THERMAL_TEMP_INVALID)) {
		*temp = sens->sens_data.ntc_data.irq_temp;
		dev_dbg(sens->dev,
			"Thermistor channel:%d name:%s temperature:%d.\n",
			sens->sensor_id, thermal_zone_device_type(sens->tzd),
			*temp);
		return 0;
	}

	if (read_ntc_avg_data_local)
		ret = read_avg_ntc_temp(sens, &reg_data);
	else
		ret = read_ntc_temp(sens, &reg_data);
	if (ret) {
		dev_err(sens->dev,
			"Thermistor channel:%d temperature read error:%d.\n",
			sens->sensor_id, ret);
		return ret;
	}

	*temp = thermal_ntc_map_data(reg_data, ADC_MAP_REGISTER_VALUE,
				     adc_temperature_data, ARRAY_SIZE(adc_temperature_data));
	dev_dbg(sens->dev,
		"Thermistor channel:%d name:%s reg:0x%x temperature:%d.\n",
		sens->sensor_id, thermal_zone_device_type(sens->tzd),
		reg_data, *temp);

	// Update temperature residency stats
	if (sens->sens_data.ntc_data.tr_stats_handle >= 0) {
		ret = ntc_temp_residency_stats_update(sens->sens_data.ntc_data.tr_stats_handle,
						      *temp);
		if (ret < 0) {
			dev_warn(sens->dev, "Failed to update stats for sensor:%s\n ret: %d\n",
				 thermal_zone_device_type(sens->tzd), ret);
		}
		if (filter_spurious_reading && !read_ntc_avg_data_local) {
			__thermal_ntc_temp_log_update(sens, *temp, reg_data);
			if (unlikely(ret == EXTREME_HIGH_TEMP)) {
				__thermal_ntc_dump_thermal_history(sens);
				*temp = __thermal_ntc_get_filtered_temp(sens);
			}
		}
	}

	return 0;
}

/*
 * thermal_ntc_set_trips - Set NTC thermistor trips.
 *
 * @tzd: Thermal zone device for which the trips need change.
 * @low_trip: The low trip value.
 * @high_trip: The high trip value.
 *
 * Return: 0 on success.
 */
int thermal_ntc_set_trips(struct thermal_zone_device *tzd, int low_trip,
			  int high_trip)
{
	struct google_sensor_data *sens = thermal_zone_device_priv(tzd);
	int ret = 0, trip_val = high_trip, trip_hyst = low_trip;
	int trip_reg_val, trip_hyst_reg_val;

	if (sens->sens_data.ntc_data.low_trip == low_trip &&
	    sens->sens_data.ntc_data.high_trip == high_trip)
		return ret;

	/* Clear and Mask the irq when we change the threshold. */
	ret = ntc_clear_and_mask_warn_irq(sens->sensor_id, false);
	if (ret) {
		dev_err(sens->dev, "sensor:%d IRQ clear error. %d\n",
			sens->sensor_id, ret);
		return ret;
	}

	if (low_trip <= -INT_MAX && high_trip == INT_MAX)
		return ret;

	if (high_trip > THERMAL_NTC_MAX_TRIP)
		trip_val = THERMAL_NTC_MAX_TRIP;
	if (low_trip < THERMAL_NTC_MIN_TRIP)
		trip_hyst = THERMAL_NTC_MIN_TRIP;

	trip_reg_val = THERMAL_NTC_TEMPERATURE_TO_THRESHOLD(trip_val);
	trip_hyst_reg_val = THERMAL_NTC_TEMPERATURE_TO_THRESHOLD(trip_hyst);

	dev_dbg(sens->dev,
		"Thermistor channel:%d set_trips low:%d high:%d. trip:%d(0x%x) hyst:%d(0x%x)\n",
		sens->sensor_id, low_trip, high_trip, trip_val, trip_reg_val, trip_hyst,
		trip_hyst_reg_val);
	ret = ntc_set_trips(sens->sensor_id, trip_reg_val, trip_hyst_reg_val);
	if (ret) {
		dev_err(sens->dev,
			"Thermistor channel:%d set_trips low:%d high:%d error: %d.\n",
			sens->sensor_id, low_trip, high_trip, ret);
		return ret;
	}
	sens->sens_data.ntc_data.low_trip = low_trip;
	sens->sens_data.ntc_data.high_trip = high_trip;

	ret = ntc_clear_and_mask_warn_irq(sens->sensor_id, true);
	if (ret) {
		dev_err(sens->dev, "sensor:%d clear & unmask IRQ error.:%d\n",
			sens->sensor_id, ret);
		return ret;
	}

	return ret;
}

/*
 * thermal_ntc_set_fault_trip - Set the Hardware fault threshold.
 *
 * @tzd: The thermal zone for which the fault threhsold needs to be configured.
 * @trip: The trip value in milli-C;
 */
int thermal_ntc_set_fault_trip(struct thermal_zone_device *tzd, int trip)
{
	struct google_sensor_data *sens = thermal_zone_device_priv(tzd);
	int ret = 0, fault_trip_reg_val;

	if (sens->sens_data.ntc_data.fault_trip == trip)
		return ret;

	fault_trip_reg_val = THERMAL_NTC_TEMPERATURE_TO_THRESHOLD(trip);
	dev_dbg(sens->dev, "Sensor:%d fault temp %d. reg:0x%x\n",
		sens->sensor_id, trip, fault_trip_reg_val);
	ret = ntc_set_fault_trip(sens->sensor_id, fault_trip_reg_val);
	if (ret) {
		dev_err(sens->dev,
			"Thermistor channel:%d set critical trip:%d error:%d.\n",
			sens->sensor_id, trip, ret);
		return ret;
	}
	sens->sens_data.ntc_data.fault_trip = trip;
	if (dt_hot_trip == THERMAL_TEMP_INVALID)
		dt_hot_trip = trip;

	ret = ntc_clear_and_mask_fault_irq(sens->sensor_id, true);
	if (ret) {
		dev_err(sens->dev, "sensor:%d unmask Fault IRQ error.:%d\n",
			sens->sensor_id, ret);
		return ret;
	}
	return ret;
}

/*
 * thermal_ntc_process_irq - Thermal sensor IRQ handler.
 *
 * This interrupt handler is called when the sensor reaches the given threshold.
 * @nb: notifier block pointer.
 * @val: event type.
 * @data: IRQ data if any.
 *
 * Return: 0 on success
 *	-ENODEV, if the sensor list is not available.
 *	other errors returned by ntc mailbox APIs.
 */
int thermal_ntc_process_irq(struct notifier_block *nb, unsigned long val, void *data)
{
	int ret = 0, temp = 0;
	u32 ch_id, ot_ut_event, *payload = (u32 *)data;
	struct google_sensor_data *sens;
	bool enable_ntc_irq = true;

	mutex_lock(&thermal_ntc_irq_lock);
	if (!thermal_ntc_sens_list) {
		pr_err("NTC sensor list empty.\n");
		ret = -ENODEV;
		goto ntc_irq_exit;
	}
	if (!payload) {
		pr_err("NTC payload empty.\n");
		ret = -ENODEV;
		goto ntc_irq_exit;
	}
	ch_id = payload[0];
	ot_ut_event = payload[1];
	list_for_each_entry(sens, thermal_ntc_sens_list, node) {
		if (sens->sensor_id != ch_id)
			continue;
		// Check for spurious IRQ and notify only for valid violation.
		mutex_lock(&sens->tzd->lock);
		sens->sens_data.ntc_data.irq_handle_phase = true;
		thermal_ntc_get_temp(sens->tzd, &temp);
		dev_info(sens->dev,
			 "Thermistor ch:%d IRQ received. Temp:%d. Status:0x%x low:%d high:%d\n",
			 sens->sensor_id, temp,
			 ot_ut_event,
			 sens->sens_data.ntc_data.low_trip,
			 sens->sens_data.ntc_data.high_trip);
		if (sens->tzd->emul_temperature) {
			enable_ntc_irq = false;
			mutex_unlock(&sens->tzd->lock);
			thermal_zone_device_update(sens->tzd, THERMAL_TRIP_VIOLATED);
			mutex_lock(&sens->tzd->lock);
			goto exit_irq_handling;
		}
		if ((ot_ut_event & THERMAL_NTC_OT_EVENT) &&
		    (temp >= (sens->sens_data.ntc_data.high_trip
			      - THERMAL_NTC_IRQ_GUARD_BAND_MILLIC))) {
			enable_ntc_irq = false;
			sens->sens_data.ntc_data.irq_temp =
					max(temp, sens->sens_data.ntc_data.high_trip);
			dev_info(sens->dev, "Thermistor channel:%d OT trip violation.\n",
				 sens->sensor_id);
			mutex_unlock(&sens->tzd->lock);
			thermal_zone_device_update(sens->tzd, THERMAL_TRIP_VIOLATED);
			mutex_lock(&sens->tzd->lock);
			sens->sens_data.ntc_data.irq_temp = THERMAL_TEMP_INVALID;
			goto exit_irq_handling;
		}
		if ((ot_ut_event & THERMAL_NTC_UT_EVENT) &&
		    (temp <= (sens->sens_data.ntc_data.low_trip
			      + THERMAL_NTC_IRQ_GUARD_BAND_MILLIC))) {
			enable_ntc_irq = false;
			sens->sens_data.ntc_data.irq_temp =
					min(temp, sens->sens_data.ntc_data.low_trip);
			dev_info(sens->dev, "Thermistor channel:%d UT trip violation.\n",
				 sens->sensor_id);
			mutex_unlock(&sens->tzd->lock);
			thermal_zone_device_update(sens->tzd, THERMAL_TRIP_VIOLATED);
			mutex_lock(&sens->tzd->lock);
			sens->sens_data.ntc_data.irq_temp = THERMAL_TEMP_INVALID;
			goto exit_irq_handling;
		}
exit_irq_handling:
		sens->sens_data.ntc_data.irq_handle_phase = false;

		if (enable_ntc_irq) {
			ret = ntc_clear_and_mask_warn_irq(sens->sensor_id, true);
			if (ret) {
				dev_err(sens->dev,
					"Thermistor channel:%d trip enable error. %d\n",
					ch_id, ret);
				ret = 0;
				// Pass through
			}
		}
		mutex_unlock(&sens->tzd->lock);
		break;
	}

ntc_irq_exit:
	mutex_unlock(&thermal_ntc_irq_lock);
	return ret;
}

/*
 * thermal_ntc_post_configure - Post configuration to be executed after the sensors
 * are registered.
 *
 * @sensor_list: list pointer for the ntc sensors.
 *
 * Return: 0 on success.
 *	Other errors returned by ntc mailbox APIs.
 */
int thermal_ntc_post_configure(struct list_head *sensor_list)
{
	struct google_sensor_data *sens;
	int ret;

	thermal_ntc_sens_list = sensor_list;

	mutex_lock(&thermal_ntc_irq_lock);
	list_for_each_entry(sens, thermal_ntc_sens_list, node) {
		sens->sens_data.ntc_data.irq_handle_phase = false;
		sens->sens_data.ntc_data.irq_temp = THERMAL_TEMP_INVALID;
		/* Re-enable the IRQ to see if we have missed any IRQ before we
		 * registered the mbox callback. CPM would have disabled the IRQ
		 * by default when an IRQ is received and expects kernel to re-enable.
		 */
		ret = ntc_clear_and_mask_warn_irq(sens->sensor_id, true);
		if (ret) {
			dev_err(sens->dev,
				"Thermistor channel:%d trip enable error. %d\n",
				sens->sensor_id, ret);
		}
	}
	mutex_unlock(&thermal_ntc_irq_lock);

	ret = ntc_cpm_mbox_register_notification(HW_RX_CB_NTC, &thermal_ntc_irq_notifier);
	if (ret)
		pr_err("ntc_thermal: Error registering for CPM callback. err:%d\n", ret);

	return ret;
}

static void thermal_ntc_init_work(struct work_struct *work)
{
	read_ntc_avg_data = true;
}
static DECLARE_DEFERRABLE_WORK(ntc_dwork, thermal_ntc_init_work);
/*
 * thermal_ntc_configure - Initialize the hardware.
 *
 * This function does a series of register writes to initialize the hardware.
 * 1. clears all the IRQ events.
 * 2. Mask all the Warning and h/w Fault IRQ.
 * @pdev: The platform device.
 * @sensor_ct: Maximum number of potential sensors this h/w can support.
 *
 * Return: 0 on success.
 *	error if the register writes fail.
 */
int thermal_ntc_configure(struct platform_device *pdev, int sensor_ct)
{
	int ret = 0, i = 0;
	struct device *dev;
	struct device_node *node;

	if (!pdev || sensor_ct <= 0)
		return -EINVAL;

	dev = &pdev->dev;
	node = pdev->dev.of_node;

	ret = ntc_channel_enable();
	if (ret) {
		dev_err(dev, "NTC thermistor channel enable error:%d.\n", ret);
		return ret;
	}

	ret = ntc_clr_data();
	if (ret) {
		dev_err(dev, "Data register clear error.:%d\n", ret);
		return ret;
	}
	read_ntc_avg_data = false;
	enable_ntc_avg_read_feature = ntc_of_property_read_bool(node, "enable-hw-lpf");
	if (enable_ntc_avg_read_feature)
		ntc_schedule_work(&ntc_dwork);
	filter_spurious_reading = ntc_of_property_read_bool(node, "filter-spurious-samples");

	for (; i < sensor_ct; i++) {
		ret = ntc_clear_and_mask_warn_irq(i, false);
		if (ret) {
			dev_err(dev, "sensor:%d IRQ clear error. %d\n", i, ret);
			return ret;
		}
		ret = ntc_clear_and_mask_fault_irq(i, false);
		if (ret) {
			dev_err(dev, "sensor:%d unmask Fault IRQ error.:%d\n",
				i, ret);
			return ret;
		}
	}

	return ret;
}

/*
 * __thermal_ntc_flush_work - Flush the delayed work.
 */
void __thermal_ntc_flush_work(void)
{
	flush_delayed_work(&ntc_dwork);
}

/*
 * thermal_ntc_cleanup - Do any cleanup before the driver exits.
 *
 * @pdev: the platform device.
 */
void thermal_ntc_cleanup(struct platform_device *pdev)
{
	cancel_delayed_work_sync(&ntc_dwork);
}

int __thermal_ntc_set_trip_temp(struct thermal_zone_device *tzd, const struct thermal_trip *trip,
				int temp)
{
	struct google_sensor_data *sens = thermal_zone_device_priv(tzd);
	int ret = 0;

	if (trip->type != THERMAL_TRIP_HOT)
		return ret;
	if (dt_hot_trip == THERMAL_TEMP_INVALID)
		return ret;

	if (temp > dt_hot_trip) {
		dev_warn(sens->dev, "Thermistor channel:%d capping the hot trip to:%d",
			 sens->sensor_id, dt_hot_trip);
		temp = dt_hot_trip;
	}
	dev_dbg(sens->dev, "Thermistor channel:%d hot trip:%d", sens->sensor_id, temp);

	return thermal_ntc_set_fault_trip(tzd, temp);
}

/* thermal_ntc_set_trip_temp - A function to set hot trip.
 *
 * This function will set the hot trip type only. It will ignore all other trip
 * type because set_trips callback will take care of it. This function will do
 * a validity check to see if the hot trip is greater than the one set by devicetree
 * and caps it. This will prevent any rougue user from disabling the hot trip.
 *
 * @tzd: thermal zone device.
 * @trip: trip pointer.
 * @temp temperature.
 *
 * Return: 0 always.
 */
int thermal_ntc_set_trip_temp(struct thermal_zone_device *tzd, const struct thermal_trip *trip,
			      int temp)
{
	/* Enable changing the hot trip only for userdebug build. */
#if IS_ENABLED(CONFIG_DEBUG_FS)
	return __thermal_ntc_set_trip_temp(tzd, trip, temp);
#else
	return 0;
#endif
}
