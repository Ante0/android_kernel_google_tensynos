// SPDX-License-Identifier: GPL-2.0 only
/*
 * google_bcl_core_helper.c Google bcl core driver library functions
 *
 * Copyright (c) 2025 Google LLC.
 *
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/cpu_pm.h>
#include "bcl.h"
#include "core_pmic/core_pmic_defs.h"
#include "ifpmic/ifpmic_defs.h"
#include "ifpmic/max77759/max77759_irq.h"
#include "ifpmic/max77779/max77779_irq.h"
#include "soc/soc_defs.h"
#include "bcl_trace.h"

static void ocpsmpl_read_stats(struct bcl_device *bcl_dev,
			       struct ocpsmpl_stats *dst,
			       struct power_supply *psy)
{
	union power_supply_propval ret;
	int err = 0;

	if (!psy)
		return;
	dst->_time = ktime_to_ms(ktime_get());
	err = power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &ret);
	if (err < 0) {
		dst->capacity = -1;
	} else {
		dst->capacity = ret.intval;
		bcl_dev->batt_psy_initialized = true;
	}
	err = power_supply_get_property(psy, POWER_SUPPLY_PROP_VOLTAGE_NOW,
					&ret);
	if (err < 0) {
		dst->voltage = -1;
	} else {
		dst->voltage = ret.intval;
		bcl_dev->batt_psy_initialized = true;
	}
}

static int google_bcl_wait_for_response_locked(struct bcl_zone *zone,
					       int timeout_ms)
{
	struct bcl_device *bcl_dev = zone->parent;

	if (bcl_dev->ifpmic == MAX77759)
		return 0;
	reinit_completion(&zone->deassert);
	return wait_for_completion_timeout(&zone->deassert,
					   msecs_to_jiffies(timeout_ms));
}

static irqreturn_t latched_irq_handler(int irq, void *data)
{
	struct bcl_zone *zone = data;
	struct bcl_device *bcl_dev;
	u8 idx;

	/* zone->disabled if irq_config has not occurred yet,
	 * small chance of IRQ trigger between irq registry and
	 * irq disable
	 */
	if (!zone || !zone->parent || zone->disabled)
		return IRQ_HANDLED;

	idx = zone->idx;
	bcl_dev = zone->parent;

	/* Ensure sw mitigation enabled is read correctly.
	 * The smp_load_acquire() ensures that all subsequent memory reads
	 * in this function are ordered after the read of sw_mitigation_enabled.
	 */
	if (!smp_load_acquire(&bcl_dev->sw_mitigation_enabled))
		return IRQ_HANDLED;

	queue_work(bcl_dev->qos_update_wq, &zone->irq_triggered_work);
	return IRQ_HANDLED;
}

static bool google_warn_check(struct bcl_zone *zone)
{
	struct bcl_device *bcl_dev;
	int gpio_level;

	bcl_dev = zone->parent;
	if (!IS_ERR_OR_NULL(zone->bcl_pin)) {
		gpio_level = gpiod_get_raw_value(zone->bcl_pin);
		return (gpio_level == zone->polarity);
	}
	return ifpmic_retrieve_batoilo_asserted(bcl_dev->intf_pmic_dev,
						bcl_dev->ifpmic);
}

static void google_bcl_release_throttling(struct bcl_zone *zone)
{
	struct bcl_device *bcl_dev;

	bcl_dev = zone->parent;
	if (zone->bcl_qos)
		google_bcl_qos_update(zone, QOS_NONE);
	else if (zone->idx == BATOILO2 && bcl_dev->zone[BATOILO])
		google_bcl_qos_update(bcl_dev->zone[BATOILO], QOS_NONE);
	complete(&zone->deassert);
	trace_bcl_zone_stats(zone, 0);
	if (zone->irq_type == IF_PMIC) {
		update_irq_end_times(bcl_dev, zone->idx);
		if (is_if_pmic_irq(zone->idx) && bcl_dev->ifpmic == MAX77779)
			evt_cnt_rd_and_clr(bcl_dev, zone->idx, false);
	}
	if (zone->idx == BATOILO) {
		google_bcl_cancel_batfet_timer(bcl_dev);
		google_bcl_cancel_bat_throttle_timer(bcl_dev);
	}
}

static void google_warn_work(struct work_struct *work)
{
	struct bcl_zone *zone =
		container_of(work, struct bcl_zone, warn_work.work);
	struct bcl_device *bcl_dev;

	bcl_dev = zone->parent;
	if (!google_warn_check(zone)) {
		google_bcl_upstream_state(zone, DISABLED);
		google_bcl_release_throttling(zone);
	} else {
		/* ODPM Read to kick off LIGHT module throttling */
		mod_delayed_work(bcl_dev->qos_update_wq, &zone->warn_work,
				 msecs_to_jiffies(TIMEOUT_5MS));
	}
}

int google_pwr_loop_trigger_mitigation(struct bcl_device *bcl_dev)
{
	/* TODO: b/356694140 - implement power reduction */
	core_pmic_main_meter_read_lpf_data(bcl_dev, &bcl_dev->vimon_odpm_stats);
	return 0;
}

static void google_irq_triggered_work(struct work_struct *work)
{
	struct bcl_zone *zone =
		container_of(work, struct bcl_zone, irq_triggered_work);
	struct bcl_device *bcl_dev;
	int irq_val = 0;
	int gpio_val;
	int idx;

	idx = zone->idx;
	bcl_dev = zone->parent;

	google_bcl_upstream_state(zone, START);

	if (zone->irq_type == IF_PMIC)
		trace_bcl_ifpmic_serviced(zone->idx, zone->devname);

	if (!IS_ERR_OR_NULL(zone->bcl_pin) || (zone->irq_type == IF_PMIC && zone->idx != BATOILO)) {

		if (zone->irq_type == IF_PMIC) {
			bcl_dev->ifpmic_ops->get_irq(bcl_dev->ifpmic_irq_dev,
						     &irq_val);
			if (!irq_val)
				return;
			bcl_dev->ifpmic_ops->clr_irq(bcl_dev->ifpmic_irq_dev,
						     idx);
		}

		if (bcl_dev->ifpmic == MAX77759 && is_if_pmic_irq(idx)) {
			idx = irq_val;
			zone = bcl_dev->zone[idx];
		}

		if (zone->irq_type == IF_PMIC) {
			gpio_val = bcl_dev->ifpmic_ops->get_raw_sts(bcl_dev->ifpmic_irq_dev,
					      zone->idx);
		} else {
			gpio_val = gpiod_get_raw_value(zone->bcl_pin);
		}

		if (gpio_val == zone->polarity) {
			if (is_if_pmic_irq(idx)) {
				atomic_inc(&zone->last_triggered
						    .triggered_cnt[START]);
				zone->last_triggered.triggered_time[START] =
					ktime_to_ms(ktime_get());
			}
		} else {
			google_bcl_release_throttling(zone);
			return;
		}
	}
	if (zone->bcl_qos) {
		google_bcl_qos_update(zone, QOS_LIGHT);
		if (zone->irq_type == IF_PMIC)
			trace_bcl_ifpmic_mitigation(zone->idx, zone->devname, QOS_LIGHT);
		mod_delayed_work(bcl_dev->qos_update_wq, &zone->warn_work,
				 msecs_to_jiffies(TIMEOUT_5MS));
	}

	google_bcl_start_data_logging(bcl_dev, idx);

	/* LIGHT phase */
	google_bcl_upstream_state(zone, LIGHT);

	if (bcl_dev->batt_psy_initialized) {
		if (idx == BATOILO2 || idx == UVLO2) {
			if (irq_val != 0) {
				atomic_inc(&bcl_dev->zone[irq_val]->bcl_cnt);
				ocpsmpl_read_stats(
					bcl_dev,
					&bcl_dev->zone[irq_val]->bcl_stats,
					bcl_dev->batt_psy);
			}
		} else {
			atomic_inc(&zone->bcl_cnt);
			ocpsmpl_read_stats(bcl_dev, &zone->bcl_stats,
					   bcl_dev->batt_psy);
		}
	}

	idx = zone->idx;
	bcl_dev = zone->parent;
	trace_bcl_zone_stats(zone, 1);

	if (zone->irq_type == IF_PMIC) {
		update_irq_start_times(bcl_dev, idx);
		if (idx == BATOILO) {
			google_bcl_set_batfet_timer(bcl_dev);
			google_bcl_set_bat_throttle_timer(bcl_dev);
		}
	}

	if (google_bcl_wait_for_response_locked(zone, TIMEOUT_5MS) > 0)
		return;
	google_bcl_upstream_state(zone, MEDIUM);

	/* MEDIUM phase: b/300504518 */
	if (google_bcl_wait_for_response_locked(zone, TIMEOUT_5MS) > 0)
		return;
	google_bcl_upstream_state(zone, HEAVY);
	/* We most likely have to shutdown after this */

	/* Reset Mitigation module if we are still alive */
	atomic_set(&bcl_dev->mitigation_module_ids, 0);

	/* HEAVY phase */
	/* IRQ deasserted */
}

int google_bcl_mitigation_trigger(int idx, void *data)
{
	struct bcl_device *bcl_dev = data;
	struct bcl_zone *zone;

	zone = bcl_dev->zone[idx];
	if (!zone)
		return -EINVAL;

	if (zone->disabled)
		return IRQ_HANDLED;

	/* Ensure sw mitigation enabled is read correctly */
	if (!smp_load_acquire(&bcl_dev->sw_mitigation_enabled))
		return -EINVAL;

	if (zone->irq_type == IF_PMIC)
		trace_bcl_ifpmic_irq(zone->idx, zone->devname);

	atomic_inc(&zone->last_triggered.triggered_cnt[START]);
	zone->last_triggered.triggered_time[START] = ktime_to_ms(ktime_get());
	queue_work(bcl_dev->qos_update_wq, &zone->irq_triggered_work);

	return 0;
}
EXPORT_SYMBOL_GPL(google_bcl_mitigation_trigger);

int google_bcl_init_vdroop_gpio(struct bcl_device *bcl_dev)
{
	return google_init_vd_soc(bcl_dev);
}
EXPORT_SYMBOL_GPL(google_bcl_init_vdroop_gpio);

int google_bcl_register_ifpmic(struct bcl_ifpmic_ops *ops,
			       struct device *ifpmic_irq_dev)
{
	struct bcl_device *bcl_dev = google_retrieve_bcl_handle();

	if (!bcl_dev)
		return -EPROBE_DEFER;

	bcl_dev->ifpmic_ops = ops;
	bcl_dev->ifpmic_irq_dev = ifpmic_irq_dev;

	return 0;
}
EXPORT_SYMBOL_GPL(google_bcl_register_ifpmic);

void google_bcl_unregister_ifpmic(void)
{
	struct bcl_device *bcl_dev = google_retrieve_bcl_handle();

	if (!bcl_dev)
		return;

	bcl_dev->ifpmic_ops = NULL;
	bcl_dev->ifpmic_irq_dev = NULL;
}
EXPORT_SYMBOL_GPL(google_bcl_unregister_ifpmic);

/**
 * @brief Parses the device tree to check if a specific IRQ is enabled.
 *
 * @param [in] bcl_dev Pointer to the BCL device structure containing the device node.
 * @param [in] idx     The index (enum) of the specific IRQ type to query.
 *
 * @return true if the IRQ exists, false otherwise.
 */
static bool is_irq_enabled(struct bcl_device *bcl_dev, u8 idx)
{
	struct device_node *np = bcl_dev->device->of_node;
	struct device_node *child __free(device_node) =
		of_get_child_by_name(np, "irq_config");

	if (!child)
		return false;

	switch (idx) {
	case UVLO1:
		return of_property_read_bool(child, "irq,uvlo1");
	case UVLO2:
		return of_property_read_bool(child, "irq,uvlo2");
	case BATOILO:
		return of_property_read_bool(child, "irq,batoilo");
	case BATOILO2:
		return of_property_read_bool(child, "irq,batoilo2");
	case PRE_UVLO: /* SMPL for P24 and below */
		return of_property_read_bool(child, "irq,pre_uvlo") ||
		       of_property_read_bool(child, "irq,smpl_warn");
	case PRE_OCP_CPU1:
		return of_property_read_bool(child, "irq,ocp_cpu1");
	case PRE_OCP_CPU2:
		return of_property_read_bool(child, "irq,ocp_cpu2");
	case PRE_OCP_TPU:
		return of_property_read_bool(child, "irq,ocp_tpu");
	case PRE_OCP_GPU:
		return of_property_read_bool(child, "irq,ocp_gpu");
	case SOFT_PRE_OCP_CPU1:
		return of_property_read_bool(child, "irq,soft_ocp_cpu1");
	case SOFT_PRE_OCP_CPU2:
		return of_property_read_bool(child, "irq,soft_ocp_cpu2");
	case SOFT_PRE_OCP_TPU:
		return of_property_read_bool(child, "irq,soft_ocp_tpu");
	case SOFT_PRE_OCP_GPU:
		return of_property_read_bool(child, "irq,soft_ocp_gpu");
	default:
		return false;
	}
}

int google_bcl_register_zone(struct bcl_device *bcl_dev, int idx,
			     const char *devname, struct gpio_desc *pin,
			     int irq, int type, int irq_config, int polarity,
			     u32 flag)
{
	int ret = 0;
	struct bcl_zone *zone;

	if (!is_if_pmic_irq(idx) &&
	    ((irq_config == IRQ_EXIST) && (IS_ERR(pin) || irq < 0))) {
		dev_err(bcl_dev->device,
			"Failed to register zone %s, pin error, pin:%ld, irq:%d\n",
			devname, PTR_ERR(pin), irq);
		return -EPROBE_DEFER;
	}
	zone = devm_kzalloc(bcl_dev->device, sizeof(struct bcl_zone),
			    GFP_KERNEL);

	if (!zone)
		return -ENOMEM;

	init_completion(&zone->deassert);
	zone->idx = idx;
	zone->bcl_pin = pin;
	zone->bcl_irq = irq;
	zone->has_irq = irq_config;
	zone->parent = bcl_dev;
	zone->irq_type = type;
	zone->devname = devname;
	zone->disabled = true;
	zone->device = bcl_dev->device;
	zone->polarity = polarity;
	atomic_set(&zone->bcl_cnt, 0);
	atomic_set(&zone->last_triggered.triggered_cnt[START], 0);
	atomic_set(&zone->last_triggered.triggered_cnt[LIGHT], 0);
	atomic_set(&zone->last_triggered.triggered_cnt[MEDIUM], 0);
	atomic_set(&zone->last_triggered.triggered_cnt[HEAVY], 0);

	INIT_WORK(&zone->irq_triggered_work, google_irq_triggered_work);
	INIT_DELAYED_WORK(&zone->warn_work, google_warn_work);

	if ((irq_config == IRQ_EXIST) && IS_ERR_OR_NULL(zone->bcl_pin) &&
	    !zone->irq_reg) {
		if (!is_if_pmic_irq(idx)) {
			zone->bcl_irq = bcl_dev->pmic_irq;
		}
		zone->irq_reg = true;
	}
	if ((irq_config == IRQ_EXIST) && !IS_ERR_OR_NULL(zone->bcl_pin) &&
	    !zone->irq_reg) {
		if (!is_if_pmic_irq(zone->idx)) {
			ret = devm_request_threaded_irq(bcl_dev->device,
							zone->bcl_irq, NULL,
							latched_irq_handler,
							flag, devname, zone);

			if (ret < 0) {
				dev_err(zone->device,
					"Failed to request IRQ: %d: %d\n",
					zone->bcl_irq, ret);
				devm_kfree(bcl_dev->device, zone);
				return ret;
			}
		}
		zone->irq_reg = true;
	}

	/* Zone setup successful; enable flag and track zone */
	zone->disabled = !is_irq_enabled(bcl_dev, idx);
	bcl_dev->zone[idx] = zone;

	return ret;
}

void google_bcl_remove_thermal(struct bcl_device *bcl_dev)
{
	int i;
	struct bcl_zone *zone;

	if (bcl_dev->batt_psy_initialized)
		power_supply_unreg_notifier(&bcl_dev->psy_nb);
	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		if (!bcl_dev->zone[i])
			continue;
		zone = bcl_dev->zone[i];
		if (zone->irq_reg) {
			if ((bcl_dev->ifpmic == MAX77779) && (i == BATOILO))
				devm_free_irq(bcl_dev->device,
					      bcl_dev->pmic_irq, bcl_dev);
			else if (!is_if_pmic_irq(zone->idx))
				devm_free_irq(bcl_dev->device, zone->bcl_irq,
					      zone);
		}
		zone->irq_reg = false;
		if (zone->irq_triggered_work.func != NULL)
			cancel_work_sync(&zone->irq_triggered_work);
		if (zone->warn_work.work.func != NULL)
			cancel_delayed_work_sync(&zone->warn_work);
		devm_kfree(bcl_dev->device, zone);
		bcl_dev->zone[i] = NULL;
	}
	if (bcl_dev->main_pwr_irq_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->main_pwr_irq_work);
	if (bcl_dev->sub_pwr_irq_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->sub_pwr_irq_work);
	if (bcl_dev->setup_core_pmic_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->setup_core_pmic_work);
	if (bcl_dev->setup_main_odpm_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->setup_main_odpm_work);
	if (bcl_dev->setup_sub_odpm_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->setup_sub_odpm_work);
	google_bcl_remove_qos(bcl_dev);
	google_bcl_remove_data_logging(bcl_dev);
	if (bcl_dev->qos_update_wq) {
		flush_workqueue(bcl_dev->qos_update_wq);
		destroy_workqueue(bcl_dev->qos_update_wq);
	}
	if (bcl_dev->non_monitored_module_ids != NULL)
		kfree(bcl_dev->non_monitored_module_ids);
	cpu_pm_unregister_notifier(&bcl_dev->cpu_nb);
	google_bcl_remove_votable(bcl_dev);
	mutex_destroy(&bcl_dev->cpu_ratio_lock);
	mutex_destroy(&bcl_dev->sysreg_lock);
	google_bcl_teardown_mailbox(bcl_dev);
	core_pmic_teardown(bcl_dev);
	ifpmic_teardown(bcl_dev);
}
