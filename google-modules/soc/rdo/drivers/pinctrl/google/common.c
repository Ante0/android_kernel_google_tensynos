// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Google LLC
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <core.h>

#include "common.h"
#include "pinctrl-utils.h"
#include "pinctrl-debugfs.h"

#define CREATE_TRACE_POINTS
#include "pinctrl-trace.h"

/* TODO(b/281621411): Figure out correct value for delay. */
#define RPM_AUTOSUSPEND_DELAY_MS	200
#define AOC_SSR_TIMEOUT_US		(1 * 60 * 1000 * 1000)
#define AOC_SSR_POLLING_PERIOD_US	200

static struct google_pinctrl *aoc_ssr_gctl;

static void google_gpio_irq_mask_update(struct google_pinctrl *gctl,
					const struct google_pingroup *g,
					struct generic_ixr_t imr_reg);

/**
 * google_pinctrl_check_set - Read back and verify a GPIO set operation.
 * @gctl:	The google_pinctrl instance.
 * @g:		The pingroup being modified.
 * @value:	The logical value (0 or 1) that was written.
 *
 * This function is called after a txdata dout modification.
 * If the 'set_check_enabled' debugfs flag is true, it reads the
 * pin's value from the RXDATA register and prints a message if
 * the read-back value does not match the intended value.
 */
static inline void google_pinctrl_check_set(struct google_pinctrl *gctl,
						const struct google_pingroup *g,
						bool value)
{
	struct rxdata_t rx_reg;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return;

	if (gctl->set_check_enabled && google_pinctrl_pin_valid(gctl, g->num)) {
		__iowmb();
		rx_reg.val = google_readl(get_reg2offset(RXDATA_ID), gctl, g);
		if (value != rx_reg.layout.pad_val)
			dev_err(gctl->dev, "gpio-set-check mismatch: pin %d, set %d, read %d",
				g->num, value, rx_reg.layout.pad_val);
	}
}

int google_pinctrl_get_csr_pd(struct google_pinctrl *gctl, int pin)
{
	int ret = 0;

	trace_google_pinctrl_get_csr_pd(gctl);

	google_pinctrl_debugfs_inc_cnt(gctl, RPM_GET_CNT, pin);

	if (IS_ERR_OR_NULL(gctl->csr_access_pd))
		return ret;

	/* We want full power right away */
	ret = pm_runtime_resume_and_get(gctl->dev);
	if (unlikely(ret)) {
		dev_err(gctl->dev,
			"Error %d while getting csr_access_pd\n", ret);
	}

	return ret;
}

void google_pinctrl_put_csr_pd(struct google_pinctrl *gctl, int pin)
{
	trace_google_pinctrl_put_csr_pd(gctl);

	google_pinctrl_debugfs_inc_cnt(gctl, RPM_PUT_CNT, pin);

	if (IS_ERR_OR_NULL(gctl->csr_access_pd))
		return;

	/* As per the driver design, we know that a vote down
	 * on csr_access_pd would make its count 0. So, we don't
	 * want to put into idle queue and then autosuspend. If
	 * fact, directly put into autosuspend.
	 */
	pm_runtime_put_autosuspend(gctl->dev);
}

static void google_pinctrl_detach_power_domain(struct google_pinctrl *gctl)
{
	if (!IS_ERR_OR_NULL(gctl->csr_access_pd)) {
		pm_runtime_disable(gctl->csr_access_pd);
		dev_pm_domain_detach(gctl->csr_access_pd, true);
	}

	if (!IS_ERR_OR_NULL(gctl->pad_function_pd)) {
		pm_runtime_put_sync(gctl->pad_function_pd);
		pm_runtime_disable(gctl->pad_function_pd);
		dev_pm_domain_detach(gctl->pad_function_pd, true);
	}

	trace_google_pinctrl_detach_power_domain(gctl);
}

/*
 * Attach all power domains defined in DT. These power domains have specific
 * purpose. A vote on "pad_function_pd" will ensure minimum power for GPIO
 * functionality to work (like interrupts). And a vote on "csr_access_pd"
 * will ensure that we can touch registers and configure the GPIO controller
 */
static int google_pinctrl_attach_power_domain(struct google_pinctrl *gctl)
{
	/* Discard previous settings */
	dev_pm_domain_detach(gctl->dev, false);

	/*
	 * If 'pad_function_pd' is defined, then the HW expects this state of
	 * power to be maintained unless system is suspended. SSWRPs which
	 * have interrupts over GPIO usecase, then to detect interrupt event,
	 * some minimum power (and clock) is required. This might not be
	 * a requirement for every SSWRP.
	 */

	gctl->pad_function_pd = dev_pm_domain_attach_by_name(gctl->dev,
							     "pad_function_pd");

	dev_dbg(gctl->dev, "%s: pad_function_pd: %p\n", __func__,
		gctl->pad_function_pd);

	if (IS_ERR(gctl->pad_function_pd))
		goto fail;

	if (gctl->pad_function_pd)
		pm_runtime_get_sync(gctl->pad_function_pd);

	/*
	 * If 'csr_access_pd' is defined, then HW expects this state of
	 * power to be maintained whenever driver wants to access GPIO
	 * registers. However, this power requirement is high and
	 * opportunistically, this can be saved by using autosuspend feature
	 * from pm_runtime.
	 *
	 * A thing to note is that, csr_access_pd is a super-set and turns on
	 * pad_function_pd as well.
	 */
	gctl->csr_access_pd = dev_pm_domain_attach_by_name(gctl->dev, "csr_access_pd");

	if (IS_ERR(gctl->csr_access_pd))
		goto fail;

	if (gctl->csr_access_pd)
		device_link_add(gctl->dev, gctl->csr_access_pd,
				DL_FLAG_PM_RUNTIME |
				DL_FLAG_STATELESS);

	return 0;
fail:
	google_pinctrl_detach_power_domain(gctl);
	return -EINVAL;
}

static int google_pinctrl_configure_runtime_pm(struct google_pinctrl *gctl)
{
	int ret;

	pm_runtime_set_autosuspend_delay(gctl->dev, RPM_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(gctl->dev);

	ret = pm_runtime_set_active(gctl->dev);
	if (ret) {
		dev_err(gctl->dev, "failed to set device rpm active: %d\n", ret);
		return ret;
	}

	/*
	 * Hold a reference that will be dropped at the end of probe
	 * unless the device is not `runtime-pm-capable`. NOTE that
	 * we call this function instead of pm_runtime_get_sync()
	 * because we _know_ we're already active (see the call above)
	 * and in general calling `pm_runtime_get_sync()` isn't so
	 * legit before calling `pm_runtime_enable()`.
	 */
	pm_runtime_get_noresume(gctl->dev);

	ret = devm_pm_runtime_enable(gctl->dev);
	if (ret) {
		dev_err(gctl->dev, "failed to enable device rpm: %d\n", ret);
		pm_runtime_put_noidle(gctl->dev);
		return ret;
	}

	gctl->rpm_capable = of_property_read_bool(gctl->dev->of_node, "runtime-pm-capable");

	return 0;
}

#ifdef CONFIG_PM

/*
 * Common function to mask/unmask GPIO interrupts based on the function pointer
 */
static void google_gpio_wake_up_irq_mask_update(struct google_pinctrl *gctl,
						struct generic_ixr_t imr_reg)
{
	const struct google_pingroup *g;

	if (!gctl->wakeup_capable_eint)
		return;

	for (int i = 0; i < gctl->num_irqs; i++) {
		/* Skip pads who are not used as IRQ source and who have wake up capability */
		if (gctl->irqinfo[i].is_pad_as_irq && gctl->irqinfo[i].wakeup_capable == 0 &&
		    !gctl->irqinfo[i].shared_via_irq_routing) {
			g = &gctl->info->groups[gctl->irqinfo[i].pad_number];
			google_gpio_irq_mask_update(gctl, g, imr_reg);
		}
	}
}

/**
 * google_pinctrl_suspend
 * @dev: the pinctrl device to suspend.
 *
 * Returns zero on success else returns a negative error code.
 */
static int google_pinctrl_suspend(struct device *dev)
{
	struct google_pinctrl *gctl = dev_get_drvdata(dev);
	struct generic_ixr_t imr_reg = {0};

	trace_google_pinctrl_suspend(gctl);

	google_pinctrl_debugfs_suspend_dump_regs(gctl);
	gctl->suspended = true;

	/*
	 * From the wake up capable SSWRP, mask all GPIO which
	 * are not requested to be wake up capable by their clients
	 */
	imr_reg.layout.irq = 0;
	google_gpio_wake_up_irq_mask_update(gctl, imr_reg);

	if (!IS_ERR_OR_NULL(gctl->pad_function_pd))
		pm_runtime_put_sync(gctl->pad_function_pd);

	return pm_runtime_force_suspend(dev);
}

/**
 * google_pinctrl_resume
 * @dev: the pinctrl device to runtime resume.
 *
 * Returns zero on success else returns a negative error code.
 */
static int google_pinctrl_resume(struct device *dev)
{
	struct google_pinctrl *gctl = dev_get_drvdata(dev);
	struct generic_ixr_t imr_reg = {0};
	int ret;

	trace_google_pinctrl_resume(gctl);
	google_pinctrl_debugfs_inc_cnt(gctl, SYS_RESM_CNT, -1);

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	if (!IS_ERR_OR_NULL(gctl->pad_function_pd))
		pm_runtime_get_sync(gctl->pad_function_pd);

	/*
	 * From wakeup capable SSWRP, undo the effect of masking some
	 * interrupts.
	 */
	imr_reg.layout.irq = 1;
	google_gpio_wake_up_irq_mask_update(gctl, imr_reg);

	google_pinctrl_debugfs_resume_dump_regs(gctl);
	gctl->suspended = false;

	return 0;
}

static void google_pinctrl_disable_irqs(struct google_pinctrl *gctl)
{
	for (int i = 0; i < gctl->num_irqs; i++)
		if (gctl->info->should_toggle_irq &&
		    gctl->info->should_toggle_irq(&gctl->irqinfo[i]))
			disable_irq(gctl->irqinfo[i].virq);
}

static void google_pinctrl_enable_irqs(struct google_pinctrl *gctl)
{
	for (int i = 0; i < gctl->num_irqs; i++)
		if (gctl->info->should_toggle_irq &&
		    gctl->info->should_toggle_irq(&gctl->irqinfo[i]))
			enable_irq(gctl->irqinfo[i].virq);
}

/**
 * google_pinctrl_runtime_suspend
 * @dev: the pinctrl device to runtime suspend.
 *
 * Returns zero on success else returns a negative error code.
 */
static int google_pinctrl_runtime_suspend(struct device *dev)
{
	struct google_pinctrl *gctl = dev_get_drvdata(dev);

	trace_google_pinctrl_runtime_suspend(gctl);

	google_pinctrl_debugfs_inc_cnt(gctl, RPM_SUSP_CNT, -1);
	gctl->rpm_last_suspend_time = ktime_get();

	google_pinctrl_disable_irqs(gctl);
	clk_bulk_disable_unprepare(gctl->nr_clks, gctl->clks);

	return 0;
}

/**
 * google_pinctrl_runtime_resume
 * @dev: the pinctrl device to runtime resume.
 *
 * Returns zero on success else returns a negative error code.
 */
static int google_pinctrl_runtime_resume(struct device *dev)
{
	struct google_pinctrl *gctl = dev_get_drvdata(dev);
	ktime_t now, delta;
	int res;

	trace_google_pinctrl_runtime_resume(gctl);

	google_pinctrl_debugfs_inc_cnt(gctl, RPM_RESM_CNT, -1);

	/* Calculate time delta since last suspend and save it */
	now = ktime_get();
	delta = ktime_sub(now, gctl->rpm_last_suspend_time);
	gctl->rpm_suspend_resume_times[gctl->rpm_suspend_resume_idx] = delta;
	gctl->rpm_suspend_resume_idx = (gctl->rpm_suspend_resume_idx + 1) % RPM_STATS_SIZE;
	if (ktime_after(delta, gctl->rpm_max_suspend_time))
		gctl->rpm_max_suspend_time = delta;

	res = clk_bulk_prepare_enable(gctl->nr_clks, gctl->clks);
	google_pinctrl_enable_irqs(gctl);
	return res;
}

const struct dev_pm_ops google_pinctrl_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(google_pinctrl_suspend, google_pinctrl_resume)
	SET_RUNTIME_PM_OPS(google_pinctrl_runtime_suspend, google_pinctrl_runtime_resume, NULL)
};
#endif

static int google_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int group_selector,
				    unsigned long *configs,
				    unsigned int num_configs);

static inline bool google_pinctrl_shared_pad_quirks(unsigned int reg_offset,
						    struct google_pinctrl *gctl,
						    const struct google_pingroup *g)
{
	/*
	 * If the pad is shared, and the register belongs to both the
	 * controller, then update the secondary register. So, PARAM and
	 * DMUXSEL will always be touched at primary CSR block and rest at
	 * secondary CSR.
	 *
	 * Alternatively, we can touch CSR based on what DMUXSEL at primary
	 * controller is referring. But while registering interrupts, we
	 * want to touch only secondary controller, irrerspective of DMUXSEL.
	 * It will be buggy to expect clients to change DMUXSEL before
	 * requesting these IRQ from their probe. So, it makes sense to touch
	 * only secondary controller registers if they are there. This approach
	 * also impose few limitations but it's okay to live with them right
	 * now.
	 */
	if (gctl->secondary_csr_base && gctl->secondary_pad_index[g->num] > 0 &&
	    reg_offset > get_reg2offset(DMUX_ID))
		return true;
	else
		return false;
}

static inline void __iomem *active_controller_reg(unsigned int reg_offset,
						  struct google_pinctrl *gctl,
						  const struct google_pingroup *g)
{
	size_t pin_stride = gctl->info->common->pin_stride;

	if (google_pinctrl_shared_pad_quirks(reg_offset, gctl, g))
		return gctl->secondary_csr_base +
		       pin_stride * (gctl->secondary_pad_index[g->num] - 1) +
		       reg_offset;

	return gctl->csr_base + pin_stride * g->num + reg_offset;
}

u32 google_readl(unsigned int reg_offset, struct google_pinctrl *gctl,
		 const struct google_pingroup *g)
{
	return readl(active_controller_reg(reg_offset, gctl, g));
}

void google_writel(unsigned int reg_offset, u32 val, struct google_pinctrl *gctl,
		   const struct google_pingroup *g)
{
	writel(val, active_controller_reg(reg_offset, gctl, g));
}

/**
 * google_pinctrl_trylock_cond_aoc - Acquire lock with conditional AOC SSR check.
 *
 * Wrapper to acquire the pinctrl lock.
 */
static int google_pinctrl_trylock_cond_aoc(struct google_pinctrl *gctl, unsigned long *flags,
					   unsigned int reg_offset, const struct google_pingroup *g)
{
	bool check_aoc = !google_pinctrl_shared_pad_quirks(reg_offset, gctl, g);

	return google_pinctrl_trylock(gctl, flags, check_aoc);
}


/**
 * Wrapper function to acquire pinctrl block lock. It also check aoc_ssr_active flag to identify
 * if AOC SSR process is active. If it's active, AOC SSWRP power domain is disabled and access to
 * AOC CSRs is not available. aoc_ssr_active is being changed in the thread context following AOC
 * driver request. It's ok to wait here for it gets cleared but only outside of atomic context
 * since changing the flag requires scheduling.
 */
int google_pinctrl_trylock(struct google_pinctrl *gctl, unsigned long *flags, bool check_aoc_ssr)
{
	int res = 0;
	unsigned long timeout = jiffies + usecs_to_jiffies(AOC_SSR_TIMEOUT_US);

	for (;;) {
		raw_spin_lock_irqsave(&gctl->lock, *flags);

		if (!check_aoc_ssr || !gctl->aoc_ssr_active)
			break;

		if (time_after(jiffies, timeout))
			panic("pinctrl kernel panic: timed out waiting for AOC SSR to complete");

		if (in_interrupt()) {
			dev_err(gctl->dev,
				"AOC SSR is active, AOC pincrl registers are not available");
			res = -EAGAIN;
			raw_spin_unlock_irqrestore(&gctl->lock, *flags);
			break;
		}

		raw_spin_unlock_irqrestore(&gctl->lock, *flags);
		usleep_range(AOC_SSR_POLLING_PERIOD_US,
			     AOC_SSR_POLLING_PERIOD_US + AOC_SSR_POLLING_PERIOD_US / 10);
	}

	return res;
}

void google_pinctrl_unlock(struct google_pinctrl *gctl, unsigned long *flags)
{
	raw_spin_unlock_irqrestore(&gctl->lock, *flags);
}

static int google_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int group_selector)
{
	const struct google_pingroup *g;
	struct google_pinctrl *gctl = gpiochip_get_data(chip);
	unsigned long flags;
	struct param_t p_reg;
	struct txdata_t tx_reg;
	unsigned long param_format_type;
	int ret;

	trace_google_gpio_direction_input(gctl, group_selector, 0);

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;
	g = &gctl->info->groups[group_selector];
	param_format_type = (unsigned long) gctl->info->pins[group_selector].drv_data;

	google_pinctrl_debugfs_inc_fops_cnt(gctl, DIR_I_CNT, group_selector);

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return ret;

	ret = google_pinctrl_trylock(gctl, &flags, true);
	if (!ret) {
		/* Ensure IE bit enabled in input mode */
		p_reg.val = google_readl(get_reg2offset(PARAM_ID), gctl, g);
		SET_PARAM_FIELD(p_reg, param_format_type, ie, 0x1);
		google_writel(get_reg2offset(PARAM_ID), p_reg.val, gctl, g);

		tx_reg.val = google_readl(get_reg2offset(TXDATA_ID), gctl, g);
		tx_reg.layout.oe = 0;
		google_writel(get_reg2offset(TXDATA_ID), tx_reg.val, gctl, g);

		google_pinctrl_unlock(gctl, &flags);
	}

	google_pinctrl_put_csr_pd(gctl, group_selector);

	return ret;
}

static int google_gpio_direction_output(struct gpio_chip *chip,
					unsigned int group_selector, int value)
{
	const struct google_pingroup *g;
	struct google_pinctrl *gctl = gpiochip_get_data(chip);
	unsigned long flags;
	struct txdata_t tx_reg;
	int ret;

	trace_google_gpio_direction_output(gctl, group_selector, value);

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;
	g = &gctl->info->groups[group_selector];

	google_pinctrl_debugfs_inc_fops_cnt(gctl, DIR_O_CNT, group_selector);

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return ret;

	ret = google_pinctrl_trylock_cond_aoc(gctl, &flags,
					      get_reg2offset(TXDATA_ID), g);
	if (!ret) {
		tx_reg.val = google_readl(get_reg2offset(TXDATA_ID), gctl, g);
		tx_reg.layout.oe = 1;
		tx_reg.layout.dout = (value ? 1 : 0);
		google_writel(get_reg2offset(TXDATA_ID), tx_reg.val, gctl, g);

		google_pinctrl_check_set(gctl, g, value);

		google_pinctrl_unlock(gctl, &flags);
	}

	google_pinctrl_put_csr_pd(gctl, group_selector);

	return ret;
}

static int google_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int group_selector)
{
	struct google_pinctrl *gctl = gpiochip_get_data(chip);
	const struct google_pingroup *g;
	struct txdata_t tx_reg;
	unsigned long flags;
	int ret;

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return ret;

	g = &gctl->info->groups[group_selector];

	google_pinctrl_debugfs_inc_fops_cnt(gctl, DIR_GET_CNT, group_selector);

	ret = google_pinctrl_trylock_cond_aoc(gctl, &flags,
					      get_reg2offset(TXDATA_ID), g);
	if (!ret) {
		tx_reg.val = google_readl(get_reg2offset(TXDATA_ID), gctl, g);
		google_pinctrl_unlock(gctl, &flags);
	} else
		tx_reg.val = 0;

	trace_google_gpio_get_direction(gctl, group_selector, tx_reg.layout.oe);

	google_pinctrl_put_csr_pd(gctl, group_selector);

	return (tx_reg.layout.oe) ? GPIO_LINE_DIRECTION_OUT :
				     GPIO_LINE_DIRECTION_IN;
}

static int google_gpio_get(struct gpio_chip *chip, unsigned int group_selector)
{
	const struct google_pingroup *g;
	struct google_pinctrl *gctl = gpiochip_get_data(chip);
	unsigned long flags;
	u32 val = 0;
	int ret;

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;

	trace_google_gpio_get(gctl, group_selector, 0);

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return ret;

	g = &gctl->info->groups[group_selector];

	google_pinctrl_debugfs_inc_fops_cnt(gctl, GET_CNT, group_selector);

	ret = google_pinctrl_trylock_cond_aoc(gctl, &flags, get_reg2offset(RXDATA_ID), g);
	if (!ret) {
		val = google_readl(get_reg2offset(RXDATA_ID), gctl, g);
		google_pinctrl_unlock(gctl, &flags);
	}

	google_pinctrl_put_csr_pd(gctl, group_selector);

	return (val & BIT(GPIO_PAD_BIT));
}

static void google_gpio_set(struct gpio_chip *chip, unsigned int group_selector,
			    int value)
{
	const struct google_pingroup *g;
	struct google_pinctrl *gctl = gpiochip_get_data(chip);
	unsigned long flags;
	struct txdata_t tx_reg;
	int ret;

	trace_google_gpio_set(gctl, group_selector, value);

	if (group_selector >= gctl->info->num_groups) {
		dev_err(gctl->dev, "Invalid group selector\n");
		return;
	}
	g = &gctl->info->groups[group_selector];

	google_pinctrl_debugfs_inc_fops_cnt(gctl, SET_CNT, group_selector);

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return;

	ret = google_pinctrl_trylock_cond_aoc(gctl, &flags, get_reg2offset(TXDATA_ID), g);
	if (!ret) {
		tx_reg.val = google_readl(get_reg2offset(TXDATA_ID), gctl, g);
		tx_reg.layout.dout = (value ? 1 : 0);
		google_writel(get_reg2offset(TXDATA_ID), tx_reg.val, gctl, g);

		google_pinctrl_check_set(gctl, g, value);

		google_pinctrl_unlock(gctl, &flags);
	}

	google_pinctrl_put_csr_pd(gctl, group_selector);
}

static int google_gpio_set_config(struct gpio_chip *chip,
				  unsigned int group_selector,
				  unsigned long config)
{
	unsigned long configs[] = { config };
	struct google_pinctrl *gctl = gpiochip_get_data(chip);

	google_pinctrl_debugfs_inc_fops_cnt(gctl, CFG_CNT, group_selector);

	return google_pinconf_group_set(gctl->pctl, group_selector, configs, ARRAY_SIZE(configs));
}

static const struct gpio_chip google_gpio_chip = {
	.direction_input = google_gpio_direction_input,
	.direction_output = google_gpio_direction_output,
	.get_direction = google_gpio_get_direction,
	.get = google_gpio_get,
	.set = google_gpio_set,
	.set_config = google_gpio_set_config,
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
};

static int google_pinmux_request(struct pinctrl_dev *pctldev,
				 unsigned int offset)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *chip = &gctl->chip;

	return gpiochip_line_is_valid(chip, offset) ? 0 : -EINVAL;
}

static int google_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);

	return gctl->info->num_funcs;
}

static const char *google_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned int func_selector)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);
	const struct google_pin_function *func;

	if (func_selector >= gctl->info->num_funcs) {
		dev_err(gctl->dev, "Invalid function selector\n");
		return NULL;
	}
	func = &gctl->info->funcs[func_selector];

	return func->name;
}

static int google_get_function_groups(struct pinctrl_dev *pctldev,
				      unsigned int func_selector,
				      const char *const **groups,
				      unsigned *const num_groups)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);
	const struct google_pin_function *func;

	if (func_selector >= gctl->info->num_funcs)
		return -EINVAL;

	func = &gctl->info->funcs[func_selector];
	*groups = func->groups;
	*num_groups = func->ngroups;

	return 0;
}

static int google_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_number,
				 unsigned int group_selector)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);
	const struct google_pingroup *g;
	u32 new_mux_val;
	unsigned long flags;
	int i, ret;

	trace_google_pinmux_set_mux(gctl, group_selector, func_number);

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;
	g = &gctl->info->groups[group_selector];

	if (g->nfuncs > MAX_NR_FUNCS)
		return -EINVAL;

	for (i = 0; i < g->nfuncs; i++) {
		if (g->funcs[i] == func_number)
			break;
	}

	if (WARN_ON(i == g->nfuncs))
		return -EINVAL;

	new_mux_val = BIT(i);

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return ret;

	ret = google_pinctrl_trylock(gctl, &flags, true);
	if (!ret) {
		google_writel(get_reg2offset(DMUX_ID), new_mux_val, gctl, g);

		google_pinctrl_unlock(gctl, &flags);
	}

	google_pinctrl_put_csr_pd(gctl, group_selector);

	return ret;
}

static int google_pinmux_request_gpio(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int group_selector)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);
	const struct google_pingroup *g;
	unsigned int func_num;

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;
	g = &gctl->info->groups[group_selector];

	if (!g->nfuncs)
		return -EPERM;

	if (google_pinctrl_shared_pad_quirks(GPIO_REGS_OFFSET, gctl, g))
		func_num = GPIO_FUNC_BIT_POS_FOR_SHARED_PAD;
	else
		func_num = g->funcs[gctl->info->gpio_func];

	return google_pinmux_set_mux(pctldev, func_num, group_selector);
}

static const struct pinmux_ops google_pinmux_ops = {
	.request = google_pinmux_request,
	.get_functions_count = google_get_functions_count,
	.get_function_name = google_get_function_name,
	.get_function_groups = google_get_function_groups,
	.gpio_request_enable = google_pinmux_request_gpio,
	.set_mux = google_pinmux_set_mux,
};

static void google_pinctrl_pin_dbg_show(struct pinctrl_dev *pctl,
					struct seq_file *s, unsigned int offset)
{
	seq_printf(s, " %s", dev_name(pctl->dev));
}

static int google_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);

	return gctl->info->num_groups;
}

static const char *google_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						 unsigned int group_selector)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);

	if (group_selector >= gctl->info->num_groups) {
		dev_err(gctl->dev, "Invalid group selector\n");
		return NULL;
	}
	return gctl->info->groups[group_selector].name;
}

static int google_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					 unsigned int group_selector,
					 const unsigned int **pins,
					 unsigned int *num_pins)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;

	*pins = (unsigned int *)gctl->info->groups[group_selector].pins;
	*num_pins = gctl->info->groups[group_selector].npins;
	return 0;
}

static const struct pinctrl_ops google_pinctrl_ops = {
	.get_groups_count = google_pinctrl_get_groups_count,
	.get_group_name = google_pinctrl_get_group_name,
	.get_group_pins = google_pinctrl_get_group_pins,
	.pin_dbg_show = google_pinctrl_pin_dbg_show,

	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinctrl_utils_free_map,
};

static int google_pinconf_group_get(struct pinctrl_dev *pctldev,
				    unsigned int group_selector,
				    unsigned long *config)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	const struct google_pingroup *g;
	u32 arg = 0;
	unsigned long param_format_type;
	struct param_t p_reg;
	struct txdata_t tx_reg;
	int ret;

	trace_google_pinconf_group_get(gctl, param, group_selector);

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return ret;

	g = &gctl->info->groups[group_selector];
	param_format_type = (unsigned long) gctl->info->pins[group_selector].drv_data;

	p_reg.val = google_readl(get_reg2offset(PARAM_ID), gctl, g);
	tx_reg.val = google_readl(get_reg2offset(TXDATA_ID), gctl, g);

	google_pinctrl_put_csr_pd(gctl, group_selector);

	/* Convert register value to pinconf value */
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		arg = (tx_reg.layout.pupd == PUPD_OPEN);
		if (!arg)
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = (tx_reg.layout.pupd == PUPD_PD);
		if (!arg)
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = (tx_reg.layout.pupd == PUPD_PU);
		if (!arg)
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		arg = tx_reg.layout.oe;
		/* Pin is not output */
		if (!arg)
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		GET_PARAM_FIELD(p_reg, param_format_type, drv, arg);
		break;
	case PIN_CONFIG_SLEW_RATE:
		GET_PARAM_FIELD(p_reg, param_format_type, slew, arg);
		if (!arg)
			return -EINVAL;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		GET_PARAM_FIELD(p_reg, param_format_type, ie, arg);
		if (!arg)
			return -EINVAL;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		GET_PARAM_FIELD(p_reg, param_format_type, rxmode, arg);
		if (!arg)
			return -EINVAL;
		break;
	case PIN_CONFIG_OUTPUT:
		arg = tx_reg.layout.oe;
		/* Pin is not output */
		if (!arg)
			return -EINVAL;
		arg = tx_reg.layout.dout;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		arg = tx_reg.layout.oe;
		if (!arg)
			return -EINVAL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int google_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int group_selector,
				    unsigned long *configs,
				    unsigned int num_configs)
{
	struct google_pinctrl *gctl = pinctrl_dev_get_drvdata(pctldev);
	const struct google_pingroup *g;
	unsigned long flags;
	u32 param, arg;
	unsigned long param_format_type;
	struct param_t p_reg, p_reg_orig;
	struct txdata_t tx_reg, tx_reg_orig;
	int dout_val = -1;
	int ret;
	int i;

	trace_google_pinconf_group_set_enter(gctl);

	if (group_selector >= gctl->info->num_groups)
		return -EINVAL;

	g = &gctl->info->groups[group_selector];
	param_format_type = (unsigned long) gctl->info->pins[group_selector].drv_data;

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return ret;

	ret = google_pinctrl_trylock(gctl, &flags, true);
	if (ret) {
		google_pinctrl_put_csr_pd(gctl, group_selector);
		return ret;
	}

	p_reg_orig.val = google_readl(get_reg2offset(PARAM_ID), gctl, g);
	tx_reg_orig.val = google_readl(get_reg2offset(TXDATA_ID), gctl, g);

	p_reg.val = p_reg_orig.val;
	tx_reg.val = tx_reg_orig.val;
	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		/* Convert pinconf values to register values */
		switch (param) {

		/* Disable any bias on the pin
		 * DT Property: bias-disable
		 */
		case PIN_CONFIG_BIAS_DISABLE:
			tx_reg.layout.pupd = PUPD_OPEN;
			break;

		/* The pin will be pulled down
		 * DT Property: bias-pull-down
		 */
		case PIN_CONFIG_BIAS_PULL_DOWN:
			/* Do PUPD_OPEN before PUPD_PD */
			tx_reg_orig.layout.pupd = PUPD_OPEN;
			google_writel(get_reg2offset(TXDATA_ID), tx_reg_orig.val, gctl, g);
			udelay(PUPD_OPEN_SETTLING_INTERVAL_IN_US);
			tx_reg.layout.pupd = PUPD_PD;
			break;

		/* The pin will be pulled up
		 * DT Property: bias-pull-up
		 */
		case PIN_CONFIG_BIAS_PULL_UP:
			/* Do PUPD_OPEN before PUPD_PU */
			tx_reg_orig.layout.pupd = PUPD_OPEN;
			google_writel(get_reg2offset(TXDATA_ID), tx_reg_orig.val, gctl, g);
			udelay(PUPD_OPEN_SETTLING_INTERVAL_IN_US);
			tx_reg.layout.pupd = PUPD_PU;
			break;

		/* The pin will be in push-pull mode
		 * DT Property: drive-push-pull
		 */
		case PIN_CONFIG_DRIVE_PUSH_PULL:
			tx_reg.layout.oe = 1;
			break;

		/* The pin will adjust driver strength as passed in arg.
		 * DT Property: drive-strength = <x> | x: follows different ranges per
		 * param_format_type. Generally, [0:7] or [0:15]
		 */
		case PIN_CONFIG_DRIVE_STRENGTH:
			/* Check for invalid values */
			if (arg > max_drv_per_pin_type(param_format_type)) {
				ret = -EOPNOTSUPP;
				dev_dbg(gctl->dev,
					"Drive strength %u is more than pin can accommodate\n",
					arg);
				goto handle_err;
			}

			SET_PARAM_FIELD(p_reg, param_format_type, drv, arg);
			break;

		/* Enable/Disable slew mode of the pin
		 * DT propert: slew-rate
		 */
		case PIN_CONFIG_SLEW_RATE:
			SET_PARAM_FIELD(p_reg, param_format_type, slew, arg);
			break;

		/* Enable or disable input mode of a pin.
		 * Enable the input - DT Property: input-enable
		 * Disable the input - DT Property: input-disable
		 */
		case PIN_CONFIG_INPUT_ENABLE:
			SET_PARAM_FIELD(p_reg, param_format_type, ie, arg);
			break;

		/* Enable or disable schmitt trigger mode of a pin.
		 * Enable the schmitt trigger - DT Property: input-schmitt-enable
		 * Disable the schmitt trigger - DT Property: input-schmitt-disable
		 */
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			SET_PARAM_FIELD(p_reg, param_format_type, rxmode, (arg ? 1 : 0));
			break;

		/* Set a pin in output mode along with an output value.
		 * Set output value as 0 - DT Property: output-low
		 * Set output values as 1 - DT Property: output-high
		 */
		case PIN_CONFIG_OUTPUT:
			tx_reg.layout.oe = 1;
			tx_reg.layout.dout = arg;
			dout_val = arg;
			break;

		/* Enable or disable output mode of a pin.
		 * Enable the output - DT Property: output-enable
		 * Disable the output - DT Property: output-disable
		 */
		case PIN_CONFIG_OUTPUT_ENABLE:
			tx_reg.layout.oe = arg;
			break;

		/* gpiolib while 'export'ing a pin, tries to set a config into a pin
		 * with this pilot flag, which does nothing to the register but act
		 * as an ack that .set_config() is implemented.
		 */
		case PIN_CONFIG_PERSIST_STATE:
			break;

		default:
			ret = -EOPNOTSUPP;
			goto handle_err;
		}

	}

	trace_google_pinconf_group_set(gctl, p_reg.val, tx_reg.val, group_selector);

	if (p_reg_orig.val != p_reg.val)
		google_writel(get_reg2offset(PARAM_ID), p_reg.val, gctl, g);

	if (tx_reg_orig.val != tx_reg.val)
		google_writel(get_reg2offset(TXDATA_ID), tx_reg.val, gctl, g);

	if (dout_val != -1)
		google_pinctrl_check_set(gctl, g, dout_val);

handle_err:
	google_pinctrl_unlock(gctl, &flags);

	google_pinctrl_put_csr_pd(gctl, group_selector);

	if (ret == -EOPNOTSUPP)
		dev_err(gctl->dev, "Unsupported config parameter: %x\n", param);

	return ret;
}

static const struct pinconf_ops google_pinconf_ops = {
	.is_generic = true,
	.pin_config_group_get = google_pinconf_group_get,
	.pin_config_group_set = google_pinconf_group_set,
};

static int google_gpio_find_irq_index(struct google_pinctrl *gctl, int hwirq)
{
	int irq_index;

	for (irq_index = 0; irq_index < gctl->num_irqs; irq_index++)
		if (gctl->irqinfo[irq_index].pad_number == hwirq)
			break;

	if (irq_index == gctl->num_irqs) {
		dev_err(gctl->dev, "cannot find irqinfo for hwirq %d\n", hwirq);
		return -EINVAL;
	}

	return irq_index;
}

static void google_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	unsigned long flags;
	const struct google_pingroup *g;
	unsigned int group_selector;
	struct generic_ixr_t ier_reg = {0};
	int irq_index = google_gpio_find_irq_index(gctl, d->hwirq);
	int ret;

	group_selector = d->hwirq;
	g = &gctl->info->groups[group_selector];

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return;

	trace_google_gpio_irq_disable(gctl, d->irq, group_selector);

	if (irq_index < 0 || !gctl->irqinfo[irq_index].shared_via_irq_routing) {
		ret = google_pinctrl_trylock_cond_aoc(gctl, &flags, get_reg2offset(IER_ID), g);
		if (!ret) {
			ier_reg.layout.irq = 0;
			google_writel(get_reg2offset(IER_ID), ier_reg.val, gctl, g);
			google_writel(get_reg2offset(IMR_ID), ier_reg.val, gctl, g);

			google_pinctrl_unlock(gctl, &flags);
		}
	} else if (gctl->irqinfo[irq_index].enabled_by_client) {
		/*
		 * For GIA-shared pins, the GPIO IMR might be re-enabled once ownership
		 * is transferred to a peer subsystem (e.g., AoSS/AOC). To prevent a
		 * potential IRQ storm on the AP side while the peer is active, we must
		 * explicitly disable the GIA parent.
		 *
		 * NOTE: Due to GIA hardware limitations, masking the parent also
		 * suppresses wakeup signals to the CPM. We prioritize system stability
		 * (avoiding IRQ storms) here, assuming wakeup responsibility has been
		 * transferred to the peer subsystem along with IRQ ownership.
		 */
		disable_irq_nosync(gctl->irqinfo[irq_index].virq);
		gctl->irqinfo[irq_index].enabled_by_client = false;
	}

	google_pinctrl_put_csr_pd(gctl, group_selector);
}

static void google_gpio_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	unsigned long flags;
	const struct google_pingroup *g;
	unsigned int group_selector;
	struct generic_ixr_t ier_reg = {0};
	int irq_index = google_gpio_find_irq_index(gctl, d->hwirq);
	int ret;

	group_selector = d->hwirq;
	g = &gctl->info->groups[group_selector];

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return;

	trace_google_gpio_irq_enable(gctl, d->irq, group_selector);

	ret = google_pinctrl_trylock_cond_aoc(gctl, &flags, get_reg2offset(IER_ID), g);
	if (!ret) {
		ier_reg.layout.irq = 1;
		google_writel(get_reg2offset(IER_ID), ier_reg.val, gctl, g);
		google_writel(get_reg2offset(IMR_ID), ier_reg.val, gctl, g);

		google_pinctrl_unlock(gctl, &flags);
	}

	if (irq_index >= 0 && gctl->irqinfo[irq_index].shared_via_irq_routing &&
	    !gctl->irqinfo[irq_index].enabled_by_client) {
		enable_irq(gctl->irqinfo[irq_index].virq);
		gctl->irqinfo[irq_index].enabled_by_client = true;
	}

	google_pinctrl_put_csr_pd(gctl, group_selector);
}

static void google_gpio_irq_mask_update(struct google_pinctrl *gctl,
					const struct google_pingroup *g,
					struct generic_ixr_t imr_reg)
{
	unsigned long flags;
	int ret;

	ret = google_pinctrl_get_csr_pd(gctl, g->num);
	if (ret < 0)
		return;

	ret = google_pinctrl_trylock_cond_aoc(gctl, &flags, get_reg2offset(IMR_ID), g);
	if (!ret) {
		struct rxdata_t rxdata_reg;
		bool is_level_trigger;

		rxdata_reg.val = google_readl(get_reg2offset(RXDATA_ID), gctl, g);
		is_level_trigger =
			(rxdata_reg.layout.trigger_type == INT_TRIGGER_LEVEL_HIGH) ||
			(rxdata_reg.layout.trigger_type == INT_TRIGGER_LEVEL_LOW);

		if (imr_reg.layout.irq == 1 && is_level_trigger) {
			struct generic_ixr_t isr_reg;
			struct generic_ixr_t isr_ovf_reg;

			isr_reg.val = 1;
			isr_ovf_reg.val = 1;

			/* write-to-clear for ISR register */
			google_writel(get_reg2offset(ISR_ID), isr_reg.val, gctl, g);
			google_writel(get_reg2offset(ISROVF_ID), isr_ovf_reg.val, gctl, g);
		}

		trace_google_gpio_irq_mask_update(gctl, g->num, imr_reg.val);

		google_writel(get_reg2offset(IMR_ID), imr_reg.val, gctl, g);
		google_pinctrl_unlock(gctl, &flags);
	}

	google_pinctrl_put_csr_pd(gctl, g->num);
}

static void google_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	const struct google_pingroup *g;
	unsigned int group_selector;
	struct generic_ixr_t imr_reg = {0};

	group_selector = d->hwirq;
	g = &gctl->info->groups[group_selector];

	trace_google_gpio_irq_mask(gctl, d->irq, group_selector);

	imr_reg.layout.irq = 0;
	google_gpio_irq_mask_update(gctl, g, imr_reg);
}

static void google_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	const struct google_pingroup *g;
	unsigned int group_selector;
	struct generic_ixr_t imr_reg = {0};

	group_selector = d->hwirq;
	g = &gctl->info->groups[group_selector];

	trace_google_gpio_irq_unmask(gctl, d->irq, group_selector);

	imr_reg.layout.irq = 1;
	google_gpio_irq_mask_update(gctl, g, imr_reg);
}

static void google_gpio_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);

	trace_google_gpio_irq_bus_lock(gctl, d->irq);
	google_pinctrl_get_csr_pd(gctl, d->hwirq);
}

static void google_gpio_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);

	trace_google_gpio_irq_bus_unlock(gctl, d->irq);
	google_pinctrl_put_csr_pd(gctl, d->hwirq);
}

static void google_gpio_irq_ack(struct irq_data *d)
{}

static int google_gpio_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	unsigned long flags;
	const struct google_pingroup *g;
	unsigned int group_selector;
	struct rxdata_t rxdata_reg;
	u32 trigger_type;
	int ret;

	group_selector = d->hwirq;
	g = &gctl->info->groups[group_selector];

	trace_google_gpio_irq_set_type(gctl, d->irq, group_selector, flow_type);

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		trigger_type = INT_TRIGGER_RISING_EDGE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		trigger_type = INT_TRIGGER_FALLING_EDGE;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		trigger_type = INT_TRIGGER_BOTH_EDGE;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		trigger_type = INT_TRIGGER_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		trigger_type = INT_TRIGGER_LEVEL_LOW;
		break;
	default:
		trigger_type = INT_TRIGGER_NONE;
		dev_err(gctl->dev,
			"Unknown interrupt trigger event, so setting none.\n");
	}

	if (flow_type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	for (int i = 0; i < gctl->num_irqs; i++) {
		if (gctl->irqinfo[i].pad_number == group_selector) {
			gctl->irqinfo[i].is_pad_as_irq = true;
			dev_dbg(gctl->dev, "Claimed pad %d as irq %d\n", group_selector, d->irq);
		}
	}

	ret = google_pinctrl_get_csr_pd(gctl, group_selector);
	if (ret < 0)
		return ret;

	ret = google_pinctrl_trylock_cond_aoc(gctl, &flags, get_reg2offset(IMR_ID), g);
	if (!ret) {
		/* Update interrupt trigger type */
		rxdata_reg.val = google_readl(get_reg2offset(RXDATA_ID), gctl, g);
		rxdata_reg.layout.trigger_type = trigger_type;
		google_writel(get_reg2offset(RXDATA_ID), rxdata_reg.val, gctl, g);

		google_pinctrl_unlock(gctl, &flags);
	}

	google_pinctrl_put_csr_pd(gctl, group_selector);
	return ret;
}

static int google_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	unsigned int group_selector;

	group_selector = d->hwirq;

	if (!gctl->wakeup_capable_eint) {
		dev_err(gctl->dev,
			"SSWRP not eint capable but its pad %d requested to wakeup irq %d\n",
			group_selector, d->irq);
		return -EINVAL;
	}

	for (int i = 0; i < gctl->num_irqs; i++) {
		if (gctl->irqinfo[i].pad_number == group_selector) {
			gctl->irqinfo[i].wakeup_capable = on;
			dev_dbg(gctl->dev,
				"Marked pad %d (irq %d) as wakeup %d\n",
				group_selector, d->irq, on);
			return 0;
		}
	}

	dev_err(gctl->dev,
		"The pad requested for wake up capable is not an irq\n");
	return -EINVAL;
}

static void google_gpio_irq_affinity_notifier_callback(struct irq_affinity_notify *notify,
						       const cpumask_t *mask)
{
	unsigned int virq_child;
	struct irq_data *d;
	struct irq_data *d_child;
	struct google_pinctrl_irqinfo *gp_irqinfo = container_of(notify,
								 struct google_pinctrl_irqinfo,
								 notifier);
	struct google_pinctrl *gctl = gp_irqinfo->gctl;
	struct irq_domain *irq_domain = gctl->chip.irq.domain;
	unsigned long flags;

	if (!gp_irqinfo->is_pad_as_irq)
		return;

	virq_child = irq_find_mapping(irq_domain, gp_irqinfo->pad_number);
	if (!virq_child)
		return;

	d_child = irq_get_irq_data(virq_child);
	if (!d_child)
		return;

	d = irq_get_irq_data(gp_irqinfo->virq);
	if (!d)
		return;

	raw_spin_lock_irqsave(&gctl->lock, flags);
	/*
	 * For pads sending their IRQ directly to the GIC, won't receive a notification as GIC
	 * driver doesn't call desc's notifier. So, in those case, do update the child simply.
	 * Notification is primarily for GPIOs behind GIA who effectively set their sibling pads'
	 * affinity also.
	 */
	irq_data_update_effective_affinity(d_child, irq_data_get_effective_affinity_mask(d));
	raw_spin_unlock_irqrestore(&gctl->lock, flags);

	trace_google_gpio_irq_affinity_notifier_callback(gctl,
				*cpumask_bits(irq_data_get_effective_affinity_mask(d_child)),
				gp_irqinfo->pad_number,
				virq_child);
}

static int google_gpio_irq_set_affinity(struct irq_data *d,
					const struct cpumask *cpu_mask,
					bool force)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	unsigned int group_selector;
	struct irq_chip *parent_chip;
	struct irq_data *parent_data;
	unsigned int virq = 0;
	unsigned long flags;
	int ret;

	group_selector = d->hwirq;

	for (int i = 0; i < gctl->num_irqs; i++) {
		if (gctl->irqinfo[i].pad_number == group_selector)
			virq = gctl->irqinfo[i].virq;
	}

	if (!virq) {
		dev_err(gctl->dev, "%s(): PAD not found for the given IRQ\n", __func__);
		return -EINVAL;
	}

	parent_chip = irq_get_chip(virq);
	parent_data = irq_get_irq_data(virq);

	if (!parent_chip || !parent_chip->irq_set_affinity) {
		dev_err(gctl->dev, "parent_chip %pK or its irq_set_affinity is null for pad %d\n",
			parent_chip, group_selector);
		return -EINVAL;
	}

	ret = parent_chip->irq_set_affinity(parent_data, cpu_mask, force);
	if (ret < IRQ_SET_MASK_OK)
		return ret;

	raw_spin_lock_irqsave(&gctl->lock, flags);
	irq_data_update_effective_affinity(d, irq_data_get_effective_affinity_mask(parent_data));
	raw_spin_unlock_irqrestore(&gctl->lock, flags);

	trace_google_gpio_irq_set_affinity(gctl,
		*cpumask_bits(irq_data_get_effective_affinity_mask(d)), group_selector, virq);

	return IRQ_SET_MASK_OK_DONE;
}

static inline void google_gpio_handle_isr(struct google_pinctrl *gctl,
					  const struct google_pingroup *g,
					  struct irq_data *d,
					  int group_selector,
					  struct generic_ixr_t isr_reg,
					  struct generic_ixr_t isr_ovf_reg,
					  bool edge_trigger)
{
	/* write-to-clear for ISR register */
	google_writel(get_reg2offset(ISR_ID), isr_reg.val, gctl, g);

	if (isr_ovf_reg.val) {
		google_writel(get_reg2offset(ISROVF_ID), isr_ovf_reg.val, gctl, g);
		if (edge_trigger)
			dev_warn_ratelimited(gctl->dev,
				"irq overflow occurred for pad %d (irq %u, hwirq %lu)\n",
				group_selector, d->irq, d->hwirq);
	}
}

static void google_gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_data *d = irq_desc_get_irq_data(desc);
	struct google_pinctrl_irqinfo *irq_data = irq_desc_get_handler_data(desc);
	struct google_pinctrl *gctl;
	const struct google_pingroup *g;
	int group_selector;
	struct generic_ixr_t isr_reg;
	struct generic_ixr_t isr_ovf_reg;
	struct rxdata_t rxdata_reg;
	bool is_level_trigger;

	if (!irq_data) {
		pr_err("pinctrl-google: irq_data is NULL\n");
		return;
	}

	gctl = irq_data->gctl;
	group_selector = irq_data->pad_number;
	if (group_selector < 0) {
		dev_err(gctl->dev, "Invalid pad number %d for irq %d\n",
			group_selector, d->irq);
		return;
	}

	trace_google_gpio_irq_handler(gctl, d->irq, d->hwirq, group_selector,
				      gctl->suspended, pm_runtime_suspended(gctl->dev));

	g = &gctl->info->groups[group_selector];

	if (gctl->suspended) {
		dev_err(gctl->dev, "IRQ while suspended, pad %d, irq %u, hwirq %lu\n",
			group_selector, d->irq, d->hwirq);
		dev_err(gctl->dev, "ISR=%#10x, ISR_OVF=%#10x, IER=%#10x, IMR=%#10x\n",
			google_readl(get_reg2offset(ISR_ID), gctl, g),
			google_readl(get_reg2offset(ISROVF_ID), gctl, g),
			google_readl(get_reg2offset(IER_ID), gctl, g),
			google_readl(get_reg2offset(IMR_ID), gctl, g));
	}

	/* Note: Just like all other places, we ideally require turning ON
	 * power domains here also before accessing registers. But turning
	 * ON power domain from atomic context is unsolved problem for now.
	 * Moreover, the SSWRP which can go low power mode, they all have
	 * GIAs sitting in front of them. And GIA's interrupt handler would
	 * be invoked first. Power domains for GIA and GPIO are common. So,
	 * we can safely assume that if we have reached till here, means
	 * required power is already turned ON by GIA or we are in always
	 * ON domain.
	 */
	chained_irq_enter(chip, desc);

	rxdata_reg.val = google_readl(get_reg2offset(RXDATA_ID), gctl, g);
	is_level_trigger =
		(rxdata_reg.layout.trigger_type == INT_TRIGGER_LEVEL_HIGH) ||
		(rxdata_reg.layout.trigger_type == INT_TRIGGER_LEVEL_LOW);

	isr_reg.val = google_readl(get_reg2offset(ISR_ID), gctl, g);
	isr_ovf_reg.val = google_readl(get_reg2offset(ISROVF_ID), gctl, g);
	if (!isr_reg.val && !isr_ovf_reg.val) {
		dev_err_once(gctl->dev, "%s(): FIXME: Handler invoked but ISR not set, pad_number %d\n",
			__func__, group_selector);
	}

	trace_generic_handle_domain_irq(gctl, gctl->chip.irq.domain, group_selector);

	if (is_level_trigger) {
		/*
		 * For level trigger interrupt, clear the source first
		 */
		generic_handle_domain_irq(gctl->chip.irq.domain, group_selector);
		google_gpio_handle_isr(gctl, g, d, group_selector, isr_reg, isr_ovf_reg, false);
	} else {
		/*
		 * For edge trigger interrupt, clear the ISR/ISR_OVF first.
		 * Handle the source later. Because if source gets clear
		 * first, till the time we clear the ISR/ISR_OVF, we may
		 * miss the interrupt.
		 */
		google_gpio_handle_isr(gctl, g, d, group_selector, isr_reg, isr_ovf_reg, true);
		generic_handle_domain_irq(gctl->chip.irq.domain, group_selector);
	}

	chained_irq_exit(chip, desc);
}

static unsigned int google_gpio_irq_startup(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	int irq_index = google_gpio_find_irq_index(gctl, d->hwirq);

	trace_google_gpio_irq_startup(gctl, d->irq, d->hwirq);

	if (irq_index < 0)
		return irq_index;

	irq_set_chained_handler_and_data(gctl->irqinfo[irq_index].virq,
						google_gpio_irq_handler,
						&gctl->irqinfo[irq_index]);
	gctl->irqinfo[irq_index].requested_by_client = true;
	gctl->irqinfo[irq_index].enabled_by_client = true;
	google_gpio_irq_enable(d);

	return 0;
}

static void google_gpio_irq_shutdown(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct google_pinctrl *gctl = gpiochip_get_data(gc);
	int irq_index = google_gpio_find_irq_index(gctl, d->hwirq);

	trace_google_gpio_irq_startup(gctl, d->irq, d->hwirq);

	if (irq_index < 0)
		return;

	google_gpio_irq_disable(d);
	gctl->irqinfo[irq_index].requested_by_client = false;
	irq_set_chained_handler_and_data(gctl->irqinfo[irq_index].virq, NULL, NULL);
}

static int google_gpio_init(struct google_pinctrl *gctl)
{
	struct gpio_chip *chip;
	struct gpio_irq_chip *girq;
	int ret;

	chip = &gctl->chip;
	chip->base = -1;
	chip->ngpio = gctl->info->num_gpios;
	chip->label = gctl->info->label;
	chip->parent = gctl->dev;
	chip->owner = THIS_MODULE;

	girq = &chip->irq;
	girq->chip = devm_kzalloc(gctl->dev, sizeof(struct irq_chip), GFP_KERNEL);
	if (IS_ERR_OR_NULL(girq->chip)) {
		dev_err(gctl->dev, "Failed to allocate for girq->chip\n");
		return -ENOMEM;
	}

	girq->chip->name = dev_name(gctl->dev);
	girq->chip->irq_enable = google_gpio_irq_enable;
	girq->chip->irq_disable = google_gpio_irq_disable;
	girq->chip->irq_mask = google_gpio_irq_mask;
	girq->chip->irq_unmask = google_gpio_irq_unmask;
	girq->chip->irq_bus_lock = google_gpio_irq_bus_lock;
	girq->chip->irq_bus_sync_unlock = google_gpio_irq_bus_sync_unlock;
	girq->chip->irq_set_type = google_gpio_irq_set_type;
	girq->chip->irq_set_wake = google_gpio_irq_set_wake;
	girq->chip->irq_set_affinity = google_gpio_irq_set_affinity;
	girq->chip->irq_ack = google_gpio_irq_ack;
	girq->chip->irq_startup = google_gpio_irq_startup;
	girq->chip->irq_shutdown = google_gpio_irq_shutdown;
	girq->chip->flags = IRQCHIP_IMMUTABLE | IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND;

	/*
	 * With .irq_set_type() callback, this will get updated based
	 * on client requested type. But call out cases where trigger
	 * type is not set
	 */
	girq->handler = handle_bad_irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->fwnode = dev_fwnode(gctl->dev);

	ret = devm_gpiochip_add_data(gctl->dev, &gctl->chip, gctl);
	if (ret) {
		dev_err(gctl->dev, "Failed to register gpiochip\n");
		return ret;
	}

	return 0;
}

static int google_gpio_shared_pad_parsing(struct platform_device *pdev,
					  struct google_pinctrl *gctl,
					  struct google_pinctrl_soc_sswrp_info *sswrp,
					  struct resource *res)
{
	int total_shared_pads;
	u32 *shared_pads_array;
	int ret;

	dev_dbg(&pdev->dev,
		"Some pads will be shared by primary and secondary GPIO controllers\n");
	gctl->secondary_csr_base = devm_ioremap_resource(&pdev->dev,
							 res);
	if (IS_ERR_OR_NULL(gctl->secondary_csr_base)) {
		dev_err(&pdev->dev,
			"secondary_csr_base could not be ioremapped, status %p\n",
			gctl->secondary_csr_base);
		return PTR_ERR(gctl->secondary_csr_base);
	}

	gctl->secondary_pad_index = devm_kzalloc(&pdev->dev,
						 sizeof(int) * sswrp->num_pins,
						 GFP_KERNEL);
	if (IS_ERR_OR_NULL(gctl->secondary_pad_index)) {
		dev_err(&pdev->dev,
			"Failed to allocate array for secondary_pad_index\n");
		return PTR_ERR(gctl->secondary_pad_index);
	}

	total_shared_pads = of_property_count_u32_elems(pdev->dev.of_node,
							"shared-pads");
	if (total_shared_pads < 0) {
		dev_err(&pdev->dev,
			"Failed to read shared-pads property: %d\n",
			total_shared_pads);
		return total_shared_pads;
	}

	shared_pads_array = devm_kzalloc(&pdev->dev,
					 sizeof(u32) * total_shared_pads,
					 GFP_KERNEL);
	if (IS_ERR_OR_NULL(shared_pads_array)) {
		dev_err(&pdev->dev,
			"Failed to allocate shared_pads_array %lx\n",
			(unsigned long)shared_pads_array);
		return PTR_ERR(shared_pads_array);
	}

	ret = of_property_read_variable_u32_array(pdev->dev.of_node,
						  "shared-pads",
						  shared_pads_array,
						  0,
						  total_shared_pads);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to read shared-pads array: %d\n", ret);
		return ret;
	}

	/*
	 * gctl->secondary_pad_index array holds info about all the shared
	 * PAD's index into secondary controller reg. Later this will be
	 * used for quick check whether the PAD has secondary regs or not.
	 * And if it has, what's the new index. 0 means PAD is not shared.
	 */
	for (int i = 0; i < total_shared_pads; i++)
		gctl->secondary_pad_index[shared_pads_array[i]] = i + 1;

	return 0;
}

static int google_gpio_interrupt_parsing(struct platform_device *pdev,
					 struct google_pinctrl *gctl)
{
	struct of_phandle_args oirq;
	unsigned int hwirq;
	int *irqs_to_pad;
	u32 *irqs_gia_routed_shared;
	int ret;

	gctl->num_irqs = platform_irq_count(pdev);
	if (gctl->num_irqs < 0) {
		dev_err(&pdev->dev,
			"IRQ count request failed %d. Could be DT node disabled\n",
			gctl->num_irqs);
		return gctl->num_irqs;
	}

	if (!gctl->num_irqs) {
		dev_info(&pdev->dev,
			 "This SSWRP has no interrupts going to CPU\n");
		return 0;
	}

	gctl->irqinfo =
		devm_kzalloc(gctl->dev,
			     sizeof(struct google_pinctrl_irqinfo) * (gctl->num_irqs),
			     GFP_KERNEL);
	if (IS_ERR_OR_NULL(gctl->irqinfo)) {
		dev_err(&pdev->dev,
			"Failed to allocate array for google_pinctrl_irqinfo\n");
		return -ENOMEM;
	}

	irqs_to_pad = devm_kzalloc(gctl->dev,
				   sizeof(int) * (gctl->num_irqs), GFP_KERNEL);
	if (IS_ERR_OR_NULL(irqs_to_pad)) {
		dev_err(&pdev->dev,
			"Failed to allocate array for irqs_to_pad mapping\n");
		return -ENOMEM;
	}

	/*
	 * DT should have exactly 'gctl->num_irqs' entries for this property.
	 * Otherwise, DT entries are wrong and no way to proceed further.
	 */
	ret = of_property_read_variable_u32_array(pdev->dev.of_node,
						  "irq-pad-mapping",
						  irqs_to_pad,
						  gctl->num_irqs,
						  gctl->num_irqs);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to read irq-pad-mapping property: %d\n", ret);
		return ret;
	}

	irqs_gia_routed_shared = devm_kzalloc(gctl->dev,
					      sizeof(u32) * (gctl->num_irqs), GFP_KERNEL);
	if (IS_ERR_OR_NULL(irqs_gia_routed_shared))
		return -ENOMEM;

	/*
	 * DT should have exactly 'gctl->num_irqs' entries for this property.
	 * Otherwise, DT entries are wrong and no way to proceed further.
	 */
	ret = of_property_read_variable_u32_array(pdev->dev.of_node,
						  "gia-routed-shared-irq",
						  irqs_gia_routed_shared,
						  gctl->num_irqs,
						  gctl->num_irqs);
	if (ret < 0)
		dev_warn(&pdev->dev, "Failed to read gia-routed-shared-irq property: %d\n", ret);

	if (of_property_read_bool(pdev->dev.of_node, "sswrp-wakeup-capable"))
		gctl->wakeup_capable_eint = 1;

	for (int i = 0; i < gctl->num_irqs; i++) {
		if (of_irq_parse_one(pdev->dev.of_node, i, &oirq)) {
			dev_err(&pdev->dev,
				"Failed to parse %d-th irq entry\n", i);
			return -EINVAL;
		}

		if (oirq.args_count == 4 || oirq.args_count == 3) {
			/* GIC demands this way */
			hwirq = oirq.args[1];
		} else if (oirq.args_count == 2) {
			/* GIA collection demands this way */
			hwirq = oirq.args[1];
		} else if (oirq.args_count == 1) {
			/* GIA single instance demands this way */
			hwirq = oirq.args[0];
		} else {
			dev_err(&pdev->dev,
				"%dth entry of interrupts-extended is not proper\n", i);
			return -EINVAL;
		}

		gctl->irqinfo[i].virq = irq_create_of_mapping(&oirq);
		if (gctl->irqinfo[i].virq < 0) {
			dev_err(&pdev->dev,
				"Failed to get %d-th irq mapping with err %d\n",
				i, gctl->irqinfo[i].virq);
			return gctl->irqinfo[i].virq;
		}

		gctl->irqinfo[i].pad_number = irqs_to_pad[i];
		gctl->irqinfo[i].shared_via_irq_routing = irqs_gia_routed_shared[i];
		gctl->irqinfo[i].gctl = gctl;
		dev_dbg(&pdev->dev, "pad_number:%d hwirq:%d virq %d\n",
			gctl->irqinfo[i].pad_number, hwirq, gctl->irqinfo[i].virq);

		gctl->irqinfo[i].notifier.irq = gctl->irqinfo[i].virq;
		gctl->irqinfo[i].notifier.notify = google_gpio_irq_affinity_notifier_callback;
		ret = irq_set_affinity_notifier(gctl->irqinfo[i].virq, &gctl->irqinfo[i].notifier);
		if (ret)
			dev_err(gctl->dev, "affinity: irq %d could not set notifier, error %d\n",
				gctl->irqinfo[i].virq, ret);

		irq_set_status_flags(gctl->irqinfo[i].virq, IRQ_DISABLE_UNLAZY);
	}

	devm_kfree(gctl->dev, irqs_gia_routed_shared);
	devm_kfree(gctl->dev, irqs_to_pad);
	return 0;
}

/*
 * Mask out all GPIO interrupts
 */
static void google_gpio_irq_mask_all(struct google_pinctrl *gctl)
{
	struct generic_ixr_t imr_reg = { 0 };
	const struct google_pingroup *g;
	int pin_nr;

	for (int i = 0; i < gctl->num_irqs; i++) {
		pin_nr = gctl->irqinfo[i].pad_number;

		if (!google_pinctrl_pin_valid(gctl, pin_nr))
			continue;

		g = &gctl->info->groups[gctl->irqinfo[i].pad_number];
		google_gpio_irq_mask_update(gctl, g, imr_reg);
	}
}

int google_pinctrl_probe(struct platform_device *pdev,
			 const struct of_device_id *google_soc_pinctrl_of_match)
{
	struct google_pinctrl *gctl;
	struct resource *res;
	struct google_pinctrl_soc_sswrp_info *sswrp;
	const struct of_device_id *match;
	int ret;

	google_pinctrl_trace_init(pdev);

	gctl = devm_kzalloc(&pdev->dev, sizeof(*gctl), GFP_KERNEL);
	if (!gctl)
		return -ENOMEM;

	platform_set_drvdata(pdev, gctl);

	match = of_match_device(google_soc_pinctrl_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	sswrp = (struct google_pinctrl_soc_sswrp_info *)(match->data);
	if (!sswrp)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, MAIN_CONTROLLER_REGS);
	gctl->csr_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gctl->csr_base))
		return PTR_ERR(gctl->csr_base);

	/*
	 * If secondary controller registers are defined in DT, that means
	 * shared pads are present in this SSWRP. So, populate it's related
	 * data structures.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM,
				    SECONDARY_CONTROLLER_REGS);
	if (res) {
		ret = google_gpio_shared_pad_parsing(pdev, gctl, sswrp, res);
		if (ret)
			return ret;
	}

	gctl->info = sswrp;
	gctl->dev = &pdev->dev;
	gctl->chip = google_gpio_chip;

	raw_spin_lock_init(&gctl->lock);

	gctl->desc.name = dev_name(&pdev->dev);
	gctl->desc.pins = sswrp->pins;
	gctl->desc.npins = sswrp->num_pins;
	gctl->desc.pctlops = &google_pinctrl_ops;
	gctl->desc.confops = &google_pinconf_ops;
	gctl->desc.pmxops = &google_pinmux_ops;
	gctl->desc.owner = THIS_MODULE;
	atomic_inc(&gctl->rpm_get_count);
	ret = google_pinctrl_attach_power_domain(gctl);
	if (ret < 0)
		return ret;

	ret = google_pinctrl_configure_runtime_pm(gctl);
	if (ret)
		goto detach_pd;

	ret = devm_clk_bulk_get_all(&pdev->dev, &gctl->clks);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get clocks %d\n", ret);
		goto detach_pd;
	}
	gctl->nr_clks = ret;

	ret = clk_bulk_prepare_enable(gctl->nr_clks, gctl->clks);
	if (ret) {
		dev_err(&pdev->dev, "Failed clock(s) %d enable %d\n", gctl->nr_clks, ret);
		goto detach_pd;
	}

	ret = google_gpio_interrupt_parsing(pdev, gctl);
	if (ret)
		goto detach_pd;

	gctl->pctl = devm_pinctrl_register(&pdev->dev, &gctl->desc, gctl);
	if (IS_ERR(gctl->pctl)) {
		dev_err(&pdev->dev, "Failed to register pinctrl\n");
		goto detach_pd;
	}

	ret = google_gpio_init(gctl);
	if (ret)
		goto detach_pd;

	if (of_property_read_bool(pdev->dev.of_node, "aoc-ssr-capable"))
		aoc_ssr_gctl = gctl;

	ret = google_pinctrl_init_debugfs(gctl, pdev, sswrp->num_groups);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to init pinctrl debugfs\n");

	/*
	 * Mask pinctrl irqs on probe to make sure no interrupt
	 * fires until client registers handlers.
	 */
	google_gpio_irq_mask_all(gctl);

	if (gctl->rpm_capable)
		pm_runtime_put(&pdev->dev);

	return 0;

detach_pd:
	google_pinctrl_detach_power_domain(gctl);
	return ret;
}

void google_pinctrl_remove(struct platform_device *pdev)
{
	struct google_pinctrl *gctl = platform_get_drvdata(pdev);

	google_pinctrl_remove_recursive_debugfs(gctl);

	clk_bulk_disable_unprepare(gctl->nr_clks, gctl->clks);
	google_pinctrl_detach_power_domain(gctl);
}

/*
 * This is exported function to allow AOC to indicate when it starts and ends subsystem restart
 * process(SSR). When SSR happens, power to the whole AOC block will be removed. Pinctrl driver
 * waits until there is no other users for the block and sets the flag indicating that AOC SSR
 * is active. Other places of the driver which require access to AOC registers will wait until
 * flag is cleared.
 */
void google_pinctrl_aoc_ssr_request(bool start)
{
	struct google_pinctrl *gctl = aoc_ssr_gctl;
	unsigned long flags;
	int ret;

	if (!gctl)
		return;

	dev_warn(gctl->dev, "%s: enter with start = %d\n", __func__, start);

	if (start) {
		/* Acquire spinlock to make sure other threads don't use pinctrl block. */
		raw_spin_lock_irqsave(&gctl->lock, flags);
		gctl->aoc_ssr_active = true;
		raw_spin_unlock_irqrestore(&gctl->lock, flags);

		/*
		 * Remove vote for SSWRP power domain. After this point AOC is free to use
		 * SSWRP powerd domain as it wants to perform SSR.
		 */
		ret = pm_runtime_put_sync(gctl->pad_function_pd);
	} else {
		ret = pm_runtime_get_sync(gctl->pad_function_pd);

		raw_spin_lock_irqsave(&gctl->lock, flags);
		gctl->aoc_ssr_active = false;
		raw_spin_unlock_irqrestore(&gctl->lock, flags);
	}
	dev_warn(gctl->dev, "%s: exit res %d, sswrp_aoc_pd usage_count %d\n",
		__func__, ret, atomic_read(&gctl->pad_function_pd->power.usage_count));
}
EXPORT_SYMBOL_GPL(google_pinctrl_aoc_ssr_request);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google pin controller driver");
MODULE_LICENSE("GPL");
