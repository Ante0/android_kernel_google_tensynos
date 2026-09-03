// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Google LLC
 *
 */

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <trace/panel_trace.h>

#include "gs_panel_internal.h"

static irqreturn_t gs_panel_err_fg_irq_handler(int irq, void *dev_data)
{
	struct gs_panel *ctx = dev_data;
	unsigned long flags = 0;
	int val;

	if (!ctx)
		return IRQ_HANDLED;

	val = gpiod_get_raw_value(ctx->gpio.gpiod[DISP_ERR_FG_GPIO]);
	if (val >= 0) {
		PANEL_ATRACE_INT_PID("ERR_FG", val, ctx->trace_pid);
		spin_lock_irqsave(&ctx->spinlock_err_fg, flags);
		ctx->err_fg_state = val;
		spin_unlock_irqrestore(&ctx->spinlock_err_fg, flags);
	}

	return IRQ_HANDLED;
}

static irqreturn_t gs_panel_pmic_irq_handler(int irq, void *dev_data)
{
	struct gs_panel *ctx = dev_data;

	if (!ctx)
		return IRQ_HANDLED;

	/* SAFETY: Ignore hardware signals if the panel is intentionally off */
	if (!gs_is_panel_active(ctx))
		return IRQ_HANDLED;

	dev_dbg(ctx->dev, "PMIC irq triggered\n");
	set_bit(GS_PMIC_ERR_IRQ_TRIGGERED, ctx->pmic_errors);
	ctx->pmic_errors_cnt[GS_PMIC_ERR_IRQ_TRIGGERED]++;

	return IRQ_HANDLED;
}

static irqreturn_t gs_panel_te2_irq_handler(int irq, void *dev_data)
{
	struct gs_panel *ctx = dev_data;

	if (!ctx)
		return IRQ_HANDLED;

	/* Add a toggle to have a better visual effect of TE2 pulse in trace. */
	PANEL_ATRACE_INT_PID("TE2_rising", 1, ctx->trace_pid);
	PANEL_ATRACE_INT_PID("TE2_rising", 0, ctx->trace_pid);

	return IRQ_HANDLED;
}

void gs_panel_enable_te2_irq(struct gs_panel *ctx, bool enable)
{
	if (ctx->te2.irq < 0)
		return;

	dev_info(ctx->dev, "te2 irq: en %d, ref %d\n", enable,
		 atomic_read(&ctx->te2.irq_ref));

	if (enable) {
		if (atomic_inc_return(&ctx->te2.irq_ref) == 1)
			enable_irq(ctx->te2.irq);
	} else {
		int ret = atomic_dec_if_positive(&ctx->te2.irq_ref);

		if (ret == 0)
			disable_irq_nosync(ctx->te2.irq);
		else if (ret < 0)
			dev_warn(ctx->dev, "unexpected te2 irq_ref (%d)\n", ret);
	}
}

static void gs_panel_init_err_fg_irq(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	int irq;

	irq = gpiod_to_irq(ctx->gpio.gpiod[DISP_ERR_FG_GPIO]);
	if (irq < 0) {
		ctx->err_fg_irq = -1;
		dev_dbg(dev, "no err_fg interrupt-gpios specified\n");
	} else if (!devm_request_irq(dev, irq, gs_panel_err_fg_irq_handler,
				   IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "err_fg", ctx)) {
		ctx->err_fg_irq = irq;
		dev_info(dev, "err_fg irq requested successfully\n");
	} else {
		ctx->err_fg_irq = -1;
		dev_warn(dev, "failed to request irq for err_fg\n");
	}
}

static void gs_panel_init_pmic_irq(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	int irq;

	irq = gpiod_to_irq(ctx->gpio.gpiod[DISP_PMIC_IRQ_GPIO]);
	if (irq < 0) {
		ctx->pmic_irq.irq = -1;
		dev_dbg(dev, "no pmic interrupt-gpios specified\n");
	} else {
		irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY);
		/* Use threaded IRQ to allow I2C/SPI access in the handler */
		if (!devm_request_threaded_irq(dev, irq, NULL, gs_panel_pmic_irq_handler,
					      /* Active-Low trigger required by PMIC spec */
					      IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					      "gs_panel_pmic_irq", ctx)) {
			ctx->pmic_irq.irq = irq;
			dev_info(dev, "pmic irq requested successfully\n");
		} else {
			ctx->pmic_irq.irq = -1;
			dev_warn(dev, "failed to request pmic irq\n");
		}
	}
}

static void gs_panel_init_te2_irq(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	int irq;

	irq = gpiod_to_irq(ctx->gpio.gpiod[DISP_TOUT_GPIO]);
	if (irq < 0) {
		ctx->te2.irq = -1;
		dev_dbg(dev, "no te2 interrupt-gpios specified\n");
	} else {
		irq_set_status_flags(irq, IRQ_DISABLE_UNLAZY);
		if (!devm_request_irq(dev, irq, gs_panel_te2_irq_handler,
				      IRQF_TRIGGER_RISING | IRQF_NO_AUTOEN,
				      "gs_panel_te2_irq", ctx)) {
			ctx->te2.irq = irq;
			dev_info(dev, "te2 irq requested successfully\n");
		} else {
			ctx->te2.irq = -1;
			dev_warn(dev, "failed to request te2 irq\n");
		}
	}
}

void gs_panel_init_gpio_irq_handlers(struct gs_panel *ctx)
{
	gs_panel_init_err_fg_irq(ctx);
	gs_panel_init_pmic_irq(ctx);
	gs_panel_init_te2_irq(ctx);
}
