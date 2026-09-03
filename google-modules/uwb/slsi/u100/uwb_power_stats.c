// SPDX-License-Identifier: GPL-2.0-ONLY
/**
 * @copyright Copyright (c) 2025 Samsung Electronics Co., Ltd
 *
 */

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include "include/uwb.h"
#include "include/uwb_gpio.h"

static const char *const uwb_power_state_name[] = {
	"U100_SLEEP", "U100_ACTIVE", "U100_OFF"
};

static irqreturn_t uwb2ap_power_state_irq_handler(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}

void u100_power_stats_on_switch(struct u100_ctx *u100_ctx)
{
	struct u100_power_stats *power_stats = u100_ctx->power_stats;
	uint64_t current_time_ms;
	int is_active;
	enum u100_power_state current_state, new_state;

	if (!power_stats)
		return;

	if (IS_ERR_OR_NULL(power_stats->gpio_u100_power_stats)) {
		UWB_ERR("Failed to get power stats related resources");
		return;
	}
	mutex_lock(&power_stats->power_stats_mutex);
	current_time_ms = ktime_to_ms(ktime_get_boottime());
	is_active = get_gpio_value(&power_stats->gpio_u100_power_stats);
	if (!atomic_read(&u100_ctx->u100_powered_on))
		new_state = U100_OFF;
	else
		new_state = is_active ? U100_ACTIVE : U100_SLEEP;

	current_state = power_stats->current_power_state;
	if (new_state == current_state) {
		if (new_state != U100_OFF)
			UWB_DEBUG("Missing power state from %s: %llu",
				uwb_power_state_name[current_state],
				current_time_ms);
		mutex_unlock(&power_stats->power_stats_mutex);
		return;
	}

	if (power_stats->power_stats_data[current_state].last_entry != 0) {
		power_stats->power_stats_data[current_state].last_exit = current_time_ms;
		power_stats->power_stats_data[current_state].duration +=
			power_stats->power_stats_data[current_state].last_exit -
			power_stats->power_stats_data[current_state].last_entry;
	}
	power_stats->current_power_state = new_state;
	power_stats->power_stats_data[new_state].count++;
	power_stats->power_stats_data[new_state].last_entry = current_time_ms;
	mutex_unlock(&power_stats->power_stats_mutex);
}

static irqreturn_t uwb2ap_power_state_irq_handler_fn(int irq, void *data)
{
	struct u100_ctx *u100_ctx = data;

	u100_power_stats_on_switch(u100_ctx);
	return IRQ_HANDLED;
}

int u100_power_stats_init(struct u100_ctx *u100_ctx)
{
	int ret;
	struct miscdevice *uci_misc = &u100_ctx->uci_dev;
	unsigned long flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
	struct u100_power_stats *power_stats = u100_ctx->power_stats;

	mutex_init(&power_stats->power_stats_mutex);
	power_stats->gpio_u100_power_stats = devm_gpiod_get(&u100_ctx->spi->dev,
		"u100-powerstat", GPIOD_IN);
	if (IS_ERR_OR_NULL(power_stats->gpio_u100_power_stats)) {
		UWB_WARN("Init gpio_u100_power_stats failed %d",
			PTR_ERR_OR_ZERO(power_stats->gpio_u100_power_stats));
		return -ENODEV;
	}
	power_stats->irq_u100_power_stats =
		gpiod_to_irq(power_stats->gpio_u100_power_stats);

	ret = irq_set_irq_type(power_stats->irq_u100_power_stats,
			IRQ_TYPE_EDGE_BOTH);
	if (ret) {
		UWB_WARN("set_irq_type failed\n");
		return -EIO;
	}

	ret = devm_request_threaded_irq(uci_misc->parent,
		power_stats->irq_u100_power_stats,
		(irq_handler_t)uwb2ap_power_state_irq_handler,
		uwb2ap_power_state_irq_handler_fn, flags,
		"u100_power_state",
		u100_ctx);

	if (ret) {
		UWB_ERR("Power stats request IRQ:%d flags:%#08lX failed:%d\n",
			power_stats->irq_u100_power_stats, flags, ret);
		return -EIO;
	}

	UWB_DEBUG("Power stats (#%d) handler registered (flags:%#08lX)\n",
		power_stats->irq_u100_power_stats, flags);
	power_stats->current_power_state = U100_OFF;
	power_stats->power_stats_data[U100_OFF].count++;
	power_stats->power_stats_data[U100_OFF].last_entry = ktime_to_ms(ktime_get_boottime());
	return ret;
}

void u100_power_stats_deinit(struct u100_ctx *u100_ctx)
{
	if (!u100_ctx->power_stats)
		return;
	mutex_destroy(&u100_ctx->power_stats->power_stats_mutex);
	devm_kfree(&u100_ctx->spi->dev, u100_ctx->power_stats);
	u100_ctx->power_stats = NULL;
}
