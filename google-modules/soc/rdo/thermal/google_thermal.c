// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2023 Google LLC */
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/units.h>
#include <linux/io.h>

#include "google_thermal.h"
#include "google_thermal_cooling.h"
#include "google_thermal_debugfs.h"
#include "thermal_core.h"
#include "thermal_hwmon.h"

static int google_get_temp(struct thermal_zone_device *tz, int *temperature)
{
	struct thermal_data *data = thermal_zone_device_priv(tz);
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;
	int temp = cpm_thermal->ops->get_temp(cpm_thermal, data->id);

	if (temp < 0)
		*temperature = 0;
	else
		*temperature = temp * MCELSIUS;

	dev_dbg(data->dev, "temperautre: %d\n", *temperature);

	return 0;
}

static int google_set_trip_temp(struct thermal_zone_device *tz,
				const struct thermal_trip *trip, int temp)
{
	struct thermal_data *data = thermal_zone_device_priv(tz);
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;
	s8 temp_list[NR_TRIPS];
	u8 trip_id;

	for (trip_id = 0; trip_id < tz->num_trips; ++trip_id) {
		if (&tz->trips[trip_id].trip == trip)
			temp_list[trip_id] = (s8)(temp / MCELSIUS);
		else
			temp_list[trip_id] = (s8)(tz->trips[trip_id].trip.temperature / MCELSIUS);
	}

	return cpm_thermal->ops->set_trip_temp(cpm_thermal, data->id, temp_list);
}

static const struct thermal_zone_device_ops google_thermal_ops = {
	.get_temp = google_get_temp,
	.set_trip_temp = google_set_trip_temp,
};

static void google_thermal_callback(struct thermal_data *data, int level)
{
	dev_info(data->dev, "thermal_zone_id: %d, level: %d\n", data->id, level);
}

static void google_thermal_throttling(struct thermal_data *data, u32 freq)
{
	struct devfreq *devfreq = data->devfreq;
	struct device *dev = devfreq->dev.parent;
	struct dev_pm_opp *opp;
	unsigned long ap_freq = freq;

	opp = dev_pm_opp_find_freq_floor(dev, &ap_freq);
	if (IS_ERR(opp)) {
		dev_err(data->dev, "No opp, err: %ld\n", PTR_ERR(opp));
		return;
	}
	dev_pm_opp_put(opp);
	dev_dbg(data->dev, "CPM throttling freq: %u, AP throttling freq: %lu\n", freq,
		DIV_ROUND_UP(ap_freq, HZ_PER_KHZ));

	dev_pm_qos_update_request(&data->tj_gpu_max_freq,
				  DIV_ROUND_UP(ap_freq, HZ_PER_KHZ));
}

static struct thermal_data_ops thermal_data_ops = {
	.example = google_thermal_callback,
	.google_thermal_throttling = google_thermal_throttling,
};

static int google_thermal_set_governor(struct thermal_data *data)
{
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;

	return cpm_thermal->ops->set_gov_select(cpm_thermal, data->id, data->gov_param.gov_select);
}

static int google_thermal_set_pi_param(struct thermal_data *data)
{
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;
	int ret;

	ret = cpm_thermal->ops->set_param(cpm_thermal, data->id,
					  THERMAL_PARAM_K_PO,
					  data->gov_param.pid_param.k_po);
	if (ret) {
		dev_err(data->dev, "Set k_po error\n");
		goto set_pi_error;
	}

	ret = cpm_thermal->ops->set_param(cpm_thermal, data->id,
					  THERMAL_PARAM_K_PU,
					  data->gov_param.pid_param.k_pu);
	if (ret) {
		dev_err(data->dev, "Set k_pu error\n");
		goto set_pi_error;
	}

	ret = cpm_thermal->ops->set_param(cpm_thermal, data->id,
					  THERMAL_PARAM_K_I,
					  data->gov_param.pid_param.k_i);
	if (ret) {
		dev_err(data->dev, "Set k_i error\n");
		goto set_pi_error;
	}

	ret = cpm_thermal->ops->set_param(cpm_thermal, data->id,
					  THERMAL_PARAM_I_MAX,
					  data->gov_param.pid_param.i_max);
	if (ret) {
		dev_err(data->dev, "Set i_max error\n");
		goto set_pi_error;
	}

set_pi_error:
	return ret;
}

static int google_thermal_set_gradual_param(struct thermal_data *data)
{
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;
	int ret;

	ret = cpm_thermal->ops->set_param(cpm_thermal, data->id,
					  THERMAL_PARAM_IRQ_GAIN,
					  data->gov_param.gradual_param.irq_gradual_gain);
	if (ret) {
		dev_err(data->dev, "Set irq gradual gain error\n");
		goto set_gradual_error;
	}

	ret = cpm_thermal->ops->set_param(cpm_thermal, data->id,
					  THERMAL_PARAM_TIMER_GAIN,
					  data->gov_param.gradual_param.timer_gradual_gain);
	if (ret) {
		dev_err(data->dev, "Set timer gradual gain error\n");
		goto set_gradual_error;
	}

set_gradual_error:
	return ret;
}

/* Driver probing shouldn't failed, if we didn't get the shared memory addr */
static void google_thermal_get_sm(struct thermal_data *data)
{
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;
	int ret;
	int addr;
	u8 version;
	u32 size;

	ret = cpm_thermal->ops->get_sm_addr(cpm_thermal, data->id, THERMAL_CURR_STATE,
					    &version, &addr, &size);
	if (ret) {
		dev_warn(data->dev, "Get current state address failed\n");
		return;
	}

	if (size != sizeof(struct thermal_curr_state)) {
		dev_warn(data->dev,
			 "CPM thermal curr_state size mismatch, CPM size: %u, Kenrel: %zu\n",
			 size, sizeof(struct thermal_curr_state));
		return;
	}

	data->curr_state_addr = devm_ioremap(data->dev, addr, size);
	if (!data->curr_state_addr) {
		dev_warn(data->dev, "Thermal current state io_remap failed\n");
		return;
	}
}

static int google_thermal_initialize(struct thermal_zone_device *tz)
{
	struct thermal_data *data = thermal_zone_device_priv(tz);
	struct google_cpm_thermal *cpm_thermal = data->cpm_thermal;
	u8 hyst_list[NR_TRIPS];
	s8 temp_list[NR_TRIPS];
	u8 trip_id;
	u16 inten = 0;
	int ret;

	for (trip_id = 0; trip_id < tz->num_trips; ++trip_id) {
		temp_list[trip_id] = (s8)(tz->trips[trip_id].trip.temperature / MCELSIUS);
		hyst_list[trip_id] = (u8)(tz->trips[trip_id].trip.hysteresis / MCELSIUS);
		if (tz->trips[trip_id].trip.type == THERMAL_TRIP_PASSIVE)
			continue;
		inten |= 1 << trip_id;
	}

	/* TODO(b/319490816): Enable when CPM handled the power state correctly */
	ret = cpm_thermal->ops->set_trip_temp(cpm_thermal, data->id, temp_list);
	if (ret) {
		dev_err(data->dev, "Set trip temp error\n");
		goto error;
	}

	ret = cpm_thermal->ops->set_trip_hyst(cpm_thermal, data->id, hyst_list);
	if (ret) {
		dev_err(data->dev, "Set trip hyst error\n");
		goto error;
	}

	ret = cpm_thermal->ops->set_interrupt_enable(cpm_thermal, data->id, inten);
	if (ret) {
		dev_err(data->dev, "Set trip interrupt enable error\n");
		goto error;
	}

	if (test_bit(THERMAL_GOV_SEL_BIT_PI_LOOP, (unsigned long *)&data->gov_param.gov_select) &&
	    google_thermal_set_pi_param(data))
		goto error;

	if (test_bit(THERMAL_GOV_SEL_BIT_GRADUAL, (unsigned long *)&data->gov_param.gov_select) &&
	    google_thermal_set_gradual_param(data))
		goto error;

	ret = google_thermal_set_governor(data);
	if (ret) {
		dev_err(data->dev, "set governor select error\n");
		goto error;
	}

	google_thermal_get_sm(data);

	if (data->cpm_polling_delay_ms > 0) {
		ret = cpm_thermal->ops->set_polling_delay(cpm_thermal, data->id,
						  data->cpm_polling_delay_ms);
		if (ret) {
			dev_err(data->dev, "Set polling delay error\n");
			goto error;
		}
	} else {
		dev_warn(data->dev, "cpm_polling_delay is invalid\n");
	}

	return 0;

error:
	return ret;
}

static int google_thermal_parse_base(struct thermal_data *data)
{
	struct device *dev = data->dev, *supplier_dev;
	struct device_node *np = dev->of_node, *cpm_thermal_node;
	struct platform_device *supplier_pdev;
	struct device_link *link;

	if (of_property_read_s32(np, "id", &data->id)) {
		dev_err(dev, "parse thermal zone id failed\n");
		return -EINVAL;
	}

	if (of_property_read_u16(np, "cpm-polling-delay-ms", &data->cpm_polling_delay_ms))
		dev_warn(dev, "parse polling-delay failed\n");

	cpm_thermal_node = of_parse_phandle(np, "cpm_thermal", 0);
	if (!cpm_thermal_node) {
		dev_err(dev, "parse phandle cpm_thermal failed\n");
		return -EINVAL;
	}

	supplier_pdev = of_find_device_by_node(cpm_thermal_node);
	if (!supplier_pdev) {
		dev_err(dev, "get cpm_thermal platform device failed\n");
		return -EINVAL;
	}

	supplier_dev = &supplier_pdev->dev;

	link = device_link_add(dev, supplier_dev, DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!link) {
		dev_err(dev, "add cpm_thermal device_link fail\n");
		return -EINVAL;
	}

	if (supplier_dev->links.status != DL_DEV_DRIVER_BOUND) {
		dev_dbg(dev, "supplier: %s is not probed\n", dev_name(supplier_dev));
		return -EPROBE_DEFER;
	}

	data->cpm_thermal = platform_get_drvdata(supplier_pdev);
	if (!data->cpm_thermal) {
		dev_err(dev, "get cpm_thermal data failed\n");
		return -EINVAL;
	}

	return 0;
}

static int google_thermal_init_gov_param(struct thermal_data *data)
{
	/* set pid param to default value */
	data->gov_param.pid_param.k_po = DEFAULT_PID_K_PO;
	data->gov_param.pid_param.k_pu = DEFAULT_PID_K_PU;
	data->gov_param.pid_param.k_i = DEFAULT_PID_K_I;
	data->gov_param.pid_param.i_max = DEFAULT_PID_I_MAX;
	/* set gradual param to default value */
	data->gov_param.gradual_param.irq_gradual_gain = DEFAULT_GRADUAL_IRQ_GAIN;
	data->gov_param.gradual_param.timer_gradual_gain = DEFAULT_GRADUAL_TIMER_GAIN;
	/* set mpmm param to default value */
	data->gov_param.mpmm_param.throttle_level = DEFAULT_MPMM_THROTTLE_LEVEL;
	data->gov_param.mpmm_param.clr_throttle_level = DEFAULT_MPMM_CLR_THROTTLE_LEVEL;
	/* set hardlimit param to default value */
	data->gov_param.hardlimit_param.use_pid = DEFAULT_HARDLIMIT_USE_PID;
	/* set early throttle param to default value */
	data->gov_param.early_throttle_param.k_p = DEFAULT_EARLY_THROTTLE_K_P;

	return 0;
}

static void google_thermal_parse_pi_param(struct thermal_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;

	/* if k_po, k_pu, k_i, i_max is in dts, enable pi governor */
	if (!of_property_read_u8(np, "k_po", &data->gov_param.pid_param.k_po) &&
	    !of_property_read_u8(np, "k_pu", &data->gov_param.pid_param.k_pu) &&
	    !of_property_read_u8(np, "k_i", &data->gov_param.pid_param.k_i) &&
	    !of_property_read_u8(np, "i_max", &data->gov_param.pid_param.i_max)) {
		dev_dbg(dev, "pi governor enabled\n");
		set_bit(THERMAL_GOV_SEL_BIT_PI_LOOP, (unsigned long *)&data->gov_param.gov_select);
	}
}

static void google_thermal_parse_gradual_param(struct thermal_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;

	/* if irq_gradual_gain, timer_gradual_gain is in dts, enable gradual governor */
	if (!of_property_read_u8(np, "irq_gradual_gain",
				&data->gov_param.gradual_param.irq_gradual_gain) &&
	    !of_property_read_u8(np, "timer_gradual_gain",
				&data->gov_param.gradual_param.timer_gradual_gain)) {
		dev_dbg(dev, "gradual governor enabled\n");
		set_bit(THERMAL_GOV_SEL_BIT_GRADUAL, (unsigned long *)&data->gov_param.gov_select);
	}
}

static void google_thermal_parse_mpmm_param(struct thermal_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;

	if (!of_property_read_u16(np, "mpmm_throotle_level",
				 &data->gov_param.mpmm_param.throttle_level) &&
	    !of_property_read_u16(np, "mpmm_clr_throttle_level",
				 &data->gov_param.mpmm_param.clr_throttle_level)) {
		dev_dbg(dev, "mpmm governor enabled\n");
		set_bit(THERMAL_GOV_SEL_BIT_MPMM, (unsigned long *)&data->gov_param.gov_select);
	}
}

static void google_thermal_parse_hardlimit_param(struct thermal_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	bool hardlimit_enable;

	hardlimit_enable = of_property_read_bool(np, "hardlimit-enable");

	if (hardlimit_enable) {
		dev_dbg(dev, "hardlimit governor enabled\n");
		data->gov_param.hardlimit_param.use_pid = of_property_read_bool(np,
										"hardlimit-use-pid");
		set_bit(THERMAL_GOV_SEL_BIT_HARDLIMIT_VIA_PID,
			(unsigned long *)&data->gov_param.gov_select);
	}
}

static void google_thermal_parse_early_throttle_param(struct thermal_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;

	if (!of_property_read_u16(np, "early_throttle_k_p",
				 &data->gov_param.early_throttle_param.k_p)) {
		dev_dbg(dev, "early throttle governor enabled\n");
		set_bit(THERMAL_GOV_SEL_BIT_EARLY_THROTTLE,
			(unsigned long *)&data->gov_param.gov_select);
	}
}

static int google_thermal_parse_thermal_pressure(struct thermal_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	const char *buf;

	data->pressure = devm_kzalloc(dev, sizeof(*data->pressure), GFP_KERNEL);
	if (!data->pressure)
		return -ENOMEM;

	if (of_property_read_u32(np, "thermal_pressure_time_window",
				 &data->pressure->time_window)) {
		dev_warn(dev, "set thermal_pressure_time_window to 0\n");
		data->pressure->time_window = 0;
	}

	if (of_property_read_string(np, "mapped_cpus", &buf)) {
		cpumask_clear(&data->pressure->mapped_cpus);
	} else {
		cpulist_parse(buf, &data->pressure->mapped_cpus);
		cpumask_and(&data->pressure->mapped_cpus, &data->pressure->mapped_cpus,
			    cpu_possible_mask);
	}

	if (of_property_read_u32(np, "pressure_index", &data->pressure->pressure_index))
		data->pressure->pressure_index = -1;
	if (data->pressure->pressure_index >= NR_PRESSURE_TZ)
		data->pressure->pressure_index = -1;

	return 0;
}

static int google_thermal_parse_junction_offset(struct thermal_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;

	if (of_property_read_u32_array(np, "junction_offset", (u32 *)data->junction_offset,
				       NR_TRIPS))
		dev_warn(dev, "parse junction_offset failed, no junction_offset");
	return 0;
}

static int google_thermal_get_devfreq(struct thermal_data *data)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	int err;

	/* Only GPU have devfreq for now */
	if (!strncmp(np->name, "GPU", THERMAL_NAME_LENGTH)) {
		data->devfreq = devfreq_get_devfreq_by_phandle(dev, "devfreq", 0);
		if (IS_ERR(data->devfreq)) {
			dev_err(dev, "No devfreq for %s", np->name);
			return -EPROBE_DEFER;
		}
		err = dev_pm_qos_add_request(data->devfreq->dev.parent, &data->tj_gpu_max_freq,
					     DEV_PM_QOS_MAX_FREQUENCY,
					     PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
		if (err < 0) {
			dev_err(dev, "PM QOS add request failed, err: %d\n", err);
			return err;
		}
	}

	return 0;
}

static int google_thermal_parse_dt(struct thermal_data *data)
{
	int ret;

	ret = google_thermal_parse_base(data);
	if (ret)
		goto error;

	ret = google_thermal_init_gov_param(data);
	if (ret)
		goto error;

	/* probing should not fail, when parsing gov_param failed */
	google_thermal_parse_pi_param(data);
	google_thermal_parse_gradual_param(data);
	google_thermal_parse_mpmm_param(data);
	google_thermal_parse_hardlimit_param(data);
	google_thermal_parse_early_throttle_param(data);

	ret = google_thermal_parse_thermal_pressure(data);
	if (ret)
		goto error;

	ret = google_thermal_parse_junction_offset(data);
	if (ret)
		goto error;

	return 0;

error:
	return ret;
}

static void google_thermal_remove_pm_qos(struct thermal_data *data)
{
	if (!strncmp(data->tz->type, "GPU", THERMAL_NAME_LENGTH))
		dev_pm_qos_remove_request(&data->tj_gpu_max_freq);
}

static int google_thermal_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct thermal_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	ret = google_thermal_parse_dt(data);
	if (ret) {
		dev_err(dev, "parse thermal device tree failed\n");
		goto error;
	}

	ret = google_thermal_get_devfreq(data);
	if (ret) {
		dev_err(dev, "parse thermal devfreq failed\n");
		goto error;
	}

	platform_set_drvdata(pdev, data);

	data->tz = devm_thermal_of_zone_register(dev, 0, data, &google_thermal_ops);

	if (IS_ERR(data->tz)) {
		dev_err(dev, "Thermal zone register failed\n");
		ret = PTR_ERR(data->tz);
		goto free_pm_qos;
	}

	if (devm_thermal_add_hwmon_sysfs(dev, data->tz))
		dev_warn(dev, "Thermal add hwmon failed\n");

	data->ops = &thermal_data_ops;

	ret = data->cpm_thermal->ops->register_thermal_zone(data->cpm_thermal, data->tz);
	if (ret) {
		dev_err(dev, "failed register thermal zone in cpm_thermal\n");
		goto free_pm_qos;
	}

	thermal_zone_device_disable(data->tz);

	ret = google_thermal_initialize(data->tz);
	if (ret) {
		dev_err(dev, "thermal init failed\n");
		goto error;
	}

	ret = google_thermal_cooling_init(data);
	if (ret) {
		dev_err(dev, "failed to create tj_cooling\n");
		goto error;
	}

	thermal_init_debugfs(data);

	return 0;

free_pm_qos:
	google_thermal_remove_pm_qos(data);
error:
	return ret;
}

static void google_thermal_platform_remove(struct platform_device *pdev)
{
	struct thermal_data *data = platform_get_drvdata(pdev);
	int ret;

	ret = data->cpm_thermal->ops->unregister_thermal_zone(data->cpm_thermal, data->tz);
	if (ret) {
		dev_err(data->dev, "failed unregister thermal zone in cpm_thermal\n");
		return;
	}

	debugfs_remove_recursive(data->debugfs_root);

	google_thermal_remove_pm_qos(data);
}

static const struct of_device_id google_thermal_of_match_table[] = {
	{ .compatible = "google,thermal" },
	{},
};
MODULE_DEVICE_TABLE(of, google_thermal_of_match_table);

static struct platform_driver google_thermal_driver = {
	.probe = google_thermal_platform_probe,
	.remove = google_thermal_platform_remove,
	.driver = {
		.name = "google-thermal",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_thermal_of_match_table),
	},
};
module_platform_driver(google_thermal_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google thermal sensor driver");
MODULE_LICENSE("GPL");
