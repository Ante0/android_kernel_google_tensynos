// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/reset.h>
#include <linux/slab.h>

/* Register Definitions */
#define INT_CSTAT 0x00
#define TMR_REV 0x90
#define PWM_CTRL(id) ((0x10 * ((id) + 1)) + 0x00)
#define PWM_CNTB(id) ((0x10 * ((id) + 1)) + 0x04)
#define PWM_CMPB(id) ((0x10 * ((id) + 1)) + 0x08)
#define PWM_CNTO(id) ((0x10 * ((id) + 1)) + 0x0C)

/* PWM_CTRL Description */
#define TMX_CLK_SRC BIT(0)
#define TMX_START BIT(1)
#define TMX_UPDATE BIT(2)
#define TMX_OUT_INV BIT(3)
#define TMX_AUTO BIT(4)
#define TMX_INT_EN BIT(5)
#define TMX_INT_MO BIT(6)
#define TMX_DMA_EN BIT(7)
#define TMX_PWM_EN BIT(8)
#define TMX_DZ GENMASK(31, 24)

#define FARADAY_PWM_TIMER_MAX 0xffffffff

/* TODO(b/253967866): confirm timeout values */
#define PWM_ONE_SHOT_TIMEOUT 200
#define PWM_ONE_SHOT_DELAY_MIN_US 50
#define PWM_ONE_SHOT_DELAY_MAX_US 100
#define RPM_AUTOSUSPEND_DELAY_MS 500

enum tmx_clk_src {
	P_CLK,
	EXT_CLK,
};

enum tmx_update {
	NO_OP,
	MANUAL_UPDATE,
};

enum tmx_out_inv {
	INVERTER_OFF,
	INVERTER_ON,
};

enum tmx_auto {
	ONE_SHOT,
	INTERVAL_MODE,
};

enum tmx_int_en {
	INTERRUPT_DISABLE,
	INTERRUPT_ENABLE,
};

enum tmx_int_mo {
	INT_LEVEL_TRIG,
	INT_PULSE_TRIG,
};

enum tmx_dma_en {
	DMA_DISABLE,
	DMA_ENABLE,
};

enum tmx_pwm_en {
	PWM_DISABLE,
	PWM_ENABLE,
};

/*
 * struct pwm_faraday_cfg - Describes configurable parameters
 *
 * @clk_src: FTPWMTMR010 can use the internal system clock (PCLK) or
 *           external clock (ext_clkX) for individual
 * @update: Updates counter and compare buffers. Write ‘1’ to this
 *          register will trigger a manual update.
 * @out_inv: Output inverter
 * @auto_reload: This feature is used to copy TMX_CTNB into the counter
 *               When the counter reaches 0 and the auto reload feature is enabled.
 *               When the counter reaches 0 and auto reload feature is disabled,
 *               the counter will stop operating. This is called "one-shot operation".
 * @int_en: Interrupt mode enable
 * @int_mo: Interrupt mode
 * @dma_en: DMA request mode used to control the timing of data movement b/w two devices
 * @pwm_en: PWM function enable
 * @dz: Length of dead zone used to control the power devices to prevent
 *      two switching devices from turning on simultaneously
 */
struct pwm_faraday_cfg {
	enum tmx_clk_src clk_src;
	enum tmx_update update;
	enum tmx_out_inv out_inv;
	enum tmx_auto auto_reload;
	enum tmx_int_en int_en;
	enum tmx_int_mo int_mo;
	enum tmx_dma_en dma_en;
	enum tmx_pwm_en pwm_en;
	u8 dz;
};

/*
 * struct pwm_faraday_chip_data - Describes PWM Chip
 *
 * @clk_rate: current clock rate (in Hz) for clock source
 * @cfg: Configurable parameters
 */
struct pwm_faraday_chip_data {
	u64 clk_rate;
	struct pwm_faraday_cfg cfg;
};

/*
 * struct pwm_faraday_variant - Describes PWM Chip Variant
 *
 * @num_pwm: Number of PWM channels
 * @revision: IP revision
 */
struct pwm_faraday_variant {
	u8 num_pwm;
	u32 revision;
};

/*
 * struct pwm_faraday_host - Describes PWM Host Bus Adapter
 *
 * @chip: PWM Controller Instance
 * @regs: Base register
 * @irq: PWM channel interrupt numbers
 * @ext_clk: Handle for external clock
 * @pclk: Handle for system clock
 * @variant: Chip variant
 */
struct pwm_faraday_host {
	struct pwm_chip chip;
	void __iomem *regs;
	int *irq;
	struct clk *ext_clk;
	struct clk *pclk;
	struct reset_control *pwm_cfg_rst;
	struct reset_control *pwm_rst;
	struct pwm_faraday_variant variant;
};

static inline struct pwm_faraday_host *
pwm_faraday_chip_to_host(struct pwm_chip *pwm_faraday_chip)
{
	return container_of(pwm_faraday_chip, struct pwm_faraday_host, chip);
}

static inline u32 pwm_faraday_readl(struct pwm_faraday_host *host, u32 offset)
{
	return readl(host->regs + offset);
}

static inline void pwm_faraday_writel(struct pwm_faraday_host *host, u32 offset,
				      u32 val)
{
	writel(val, host->regs + offset);
}

static void pwm_faraday_dump_regs(struct pwm_faraday_host *host,
				  struct pwm_device *pwm)
{
	unsigned int id = pwm->pwm;

	dev_err(host->chip.dev, "╔═════════════════════════╗\n");
	dev_err(host->chip.dev, "║    PWM REGISTER DUMP    ║\n");
	dev_err(host->chip.dev, "╚═════════════════════════╝\n");
	dev_err(host->chip.dev, "INT_CSTAT:\t 0x%x\n",
		pwm_faraday_readl(host, INT_CSTAT));
	dev_err(host->chip.dev, "TMR_REV:\t 0x%x\n",
		pwm_faraday_readl(host, TMR_REV));
	dev_err(host->chip.dev, "TM%d_CTRL:\t 0x%x\n", id,
		pwm_faraday_readl(host, PWM_CTRL(id)));
	dev_err(host->chip.dev, "TM%d_CNTB:\t 0x%x\n", id,
		pwm_faraday_readl(host, PWM_CNTB(id)));
	dev_err(host->chip.dev, "TM%d_CMPB:\t 0x%x\n", id,
		pwm_faraday_readl(host, PWM_CMPB(id)));
	dev_err(host->chip.dev, "TM%d_CNTO:\t 0x%x\n", id,
		pwm_faraday_readl(host, PWM_CNTO(id)));
}

static u32 pwm_faraday_get_rev(struct pwm_faraday_host *host)
{
	return pwm_faraday_readl(host, TMR_REV);
}

static void pwm_faraday_start(struct pwm_faraday_host *host,
			      struct pwm_device *pwm)
{
	u32 data, offset;

	offset = PWM_CTRL(pwm->pwm);

	data = pwm_faraday_readl(host, offset);
	data |= FIELD_PREP(TMX_START, 1);
	pwm_faraday_writel(host, offset, data);
}

static void pwm_faraday_stop(struct pwm_faraday_host *host,
			     struct pwm_device *pwm)
{
	u32 data, offset;

	offset = PWM_CTRL(pwm->pwm);

	data = pwm_faraday_readl(host, offset);
	data &= ~FIELD_PREP(TMX_START, 1);
	pwm_faraday_writel(host, offset, data);
}

static void pwm_faraday_clear_intr_ctrl(struct pwm_faraday_host *host,
					struct pwm_device *pwm)
{
	unsigned int id = pwm->pwm;

	/* clear interrupt status */
	pwm_faraday_writel(host, INT_CSTAT, (1 << id));
	/* clear control register */
	pwm_faraday_writel(host, PWM_CTRL(id), 0x0);
}

static int pwm_faraday_setup(struct pwm_faraday_host *host,
			     struct pwm_device *pwm, u32 cntb, u32 cmpb)
{
	u32 data;
	unsigned int id;

	id = pwm->pwm;
	pwm_faraday_writel(host, PWM_CNTB(id), cntb);
	pwm_faraday_writel(host, PWM_CMPB(id), cmpb);

	/*
	 * FTPWMTMR010 Block Data Sheet Table 4-5
	 * Writing 1 to TMX_UPDATE triggers a manual update.
	 * This bit will be auto-cleared after the operation.
	 */
	data = pwm_faraday_readl(host, PWM_CTRL(id));
	data |= FIELD_PREP(TMX_UPDATE, 1);
	pwm_faraday_writel(host, PWM_CTRL(id), data);

	data = pwm_faraday_readl(host, PWM_CTRL(id));
	if ((data & TMX_UPDATE) != 0) {
		/* Return error in case TMX_UPDATE bit is not cleared */
		dev_err(host->chip.dev,
			"Manual update failed: TMX_UPDATE not auto-cleared\n");
		return -EIO;
	}

	data = pwm_faraday_readl(host, PWM_CNTO(id));
	if (data != cntb) {
		dev_err(host->chip.dev,
			"Manual update failed: TMX_CNTB not reflected in TMX_CNTO\n");
		return -EIO;
	}

	return 0;
}

static struct pwm_faraday_cfg *pwm_faraday_get_cfg(struct pwm_device *pwm)
{
	struct pwm_faraday_chip_data *chip_data;

	chip_data = pwm_get_chip_data(pwm);
	if (!chip_data)
		return NULL;

	return &chip_data->cfg;
}

static int pwm_faraday_init(struct pwm_faraday_host *host,
			    struct pwm_device *pwm)
{
	u32 data, offset;
	struct pwm_faraday_cfg *cfg;

	if (!host || !pwm)
		return -EINVAL;

	pwm_faraday_clear_intr_ctrl(host, pwm);

	cfg = pwm_faraday_get_cfg(pwm);
	if (!cfg)
		return -ENODEV;

	offset = PWM_CTRL(pwm->pwm);

	data = pwm_faraday_readl(host, offset);
	data |= FIELD_PREP(TMX_CLK_SRC, cfg->clk_src);
	data |= FIELD_PREP(TMX_UPDATE, cfg->update);
	data |= FIELD_PREP(TMX_OUT_INV, cfg->out_inv);
	data |= FIELD_PREP(TMX_AUTO, cfg->auto_reload);
	data |= FIELD_PREP(TMX_INT_EN, cfg->int_en);
	data |= FIELD_PREP(TMX_INT_MO, cfg->int_mo);
	data |= FIELD_PREP(TMX_DMA_EN, cfg->dma_en);
	data |= FIELD_PREP(TMX_PWM_EN, cfg->pwm_en);
	data |= FIELD_PREP(TMX_DZ, cfg->dz);
	pwm_faraday_writel(host, offset, data);

	return 0;
}

static int pwm_faraday_generate_pulse(struct pwm_faraday_host *host,
				      struct pwm_device *pwm, u64 period_ns,
				      u64 duty_cycle_ns)
{
	struct pwm_faraday_chip_data *chip_data;
	u64 max_period_ns, min_period_ns;
	struct pwm_faraday_cfg *cfg;
	u32 cnto, cntb, cmpb;
	int result, timeout;
	unsigned int id;

	chip_data = pwm_get_chip_data(pwm);
	min_period_ns = DIV_ROUND_UP(NSEC_PER_SEC, chip_data->clk_rate);
	max_period_ns = mul_u64_u64_div_u64(NSEC_PER_SEC, FARADAY_PWM_TIMER_MAX,
					    chip_data->clk_rate);

	if (!chip_data) {
		dev_err(host->chip.dev, "PWM chip data cannot be null\n");
		return -ENODEV;
	}
	if (period_ns > max_period_ns) {
		dev_err(host->chip.dev,
			"Requested period leads to PWM channel counter buffer overflow\n");
		return -EINVAL;
	}
	if (period_ns < min_period_ns) {
		dev_err(host->chip.dev,
			"Requested period cannot be less than clock period\n");
		return -EINVAL;
	}
	if (duty_cycle_ns > period_ns) {
		dev_err(host->chip.dev,
			"Duty cycle cannot be more than 100%%\n");
		return -EINVAL;
	}

	/*
	 * FTPWMTMR010 Block Data Sheet Section 4.3.3, 4.3.4
	 * count and compare buffers are written with
	 * one less than the desired value
	 */
	cntb = DIV64_U64_ROUND_CLOSEST(period_ns, min_period_ns) - 1;
	cmpb = DIV64_U64_ROUND_CLOSEST(duty_cycle_ns, min_period_ns) - 1;

	dev_dbg(host->chip.dev,
		"Effective values:\nClock Period\t: %lldns\nDuty Cycle\t: %lldns\n",
		(u64)((cntb + 1) * min_period_ns),
		(u64)((cmpb + 1) * min_period_ns));

	result = pwm_faraday_setup(host, pwm, cntb, cmpb);
	if (result != 0)
		return result;

	pwm_faraday_start(host, pwm);

	/*
	 * In case of one shot mode, poll till TMX_CNTO reaches zero
	 * as it won't auto reload.
	 * In case of interval mode, client needs to explicitly stop the timer
	 * or disable auto reload.
	 */
	cfg = pwm_faraday_get_cfg(pwm);
	if (!cfg)
		return -ENODEV;

	id = pwm->pwm;
	if (cfg->auto_reload == ONE_SHOT) {
		cnto = pwm_faraday_readl(host, PWM_CNTO(id));
		timeout = PWM_ONE_SHOT_TIMEOUT;
		do {
			cnto = pwm_faraday_readl(host, PWM_CNTO(id));
			usleep_range(PWM_ONE_SHOT_DELAY_MIN_US,
				     PWM_ONE_SHOT_DELAY_MAX_US);

		} while (cnto > 0 && --timeout > 0);

		pwm_faraday_stop(host, pwm);
	}

	return 0;
}

static int pwm_faraday_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_faraday_host *host = pwm_faraday_chip_to_host(chip);
	struct pwm_faraday_chip_data *data;
	int ret = 0, ch;

	for (ch = 0; ch < chip->npwm; ++ch) {
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		if (host->ext_clk)
			data->clk_rate = clk_get_rate(host->ext_clk);
		else
			data->clk_rate = clk_get_rate(host->pclk);
		ret = pwm_set_chip_data(&chip->pwms[ch], data);
		if (ret)
			return ret;
	}

	return ret;
}

static int pwm_faraday_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			     const struct pwm_state *state)
{
	struct pwm_faraday_host *host = pwm_faraday_chip_to_host(chip);
	struct pwm_state initial_state;
	struct device *dev = chip->dev;
	struct pwm_faraday_cfg *cfg;
	int ret;

	ret = 0;
	initial_state = pwm->state;
	cfg = pwm_faraday_get_cfg(pwm);

	if (!state->enabled) {
		if (pwm->state.enabled) {
			pwm->state.enabled = false;
			pwm_faraday_stop(host, pwm);
		}
		goto exit;
	}

	pwm->state.enabled = true;
	pwm->state.polarity = state->polarity;

	/* use external clock if provided */
	cfg->clk_src = host->ext_clk ? EXT_CLK : P_CLK;
	cfg->update = NO_OP;
	cfg->out_inv = (enum tmx_out_inv)pwm->state.polarity;
	cfg->auto_reload = INTERVAL_MODE;
	cfg->int_en = INTERRUPT_DISABLE;
	cfg->int_mo = INT_LEVEL_TRIG;
	cfg->dma_en = DMA_DISABLE;
	cfg->pwm_en = PWM_ENABLE;

	ret = pwm_faraday_init(host, pwm);
	if (ret) {
		dev_err(dev, "Failed to setup PWM channel (%d)\n", ret);
		goto rollback;
	}

	ret = pwm_faraday_generate_pulse(host, pwm, state->period,
					 state->duty_cycle);
	if (ret) {
		dev_err(dev, "Failed to generate signal (%d)\n", ret);
		pwm_faraday_dump_regs(host, pwm);
		goto rollback;
	}
	pwm->state.period = state->period;
	pwm->state.duty_cycle = state->duty_cycle;

rollback:
	pwm->state = initial_state;
exit:
	return ret;
}

static int pwm_faraday_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				 struct pwm_state *state)
{
	struct pwm_faraday_chip_data *chip_data;
	struct pwm_faraday_host *host;
	struct pwm_faraday_cfg *cfg;
	u64 min_period_ns;
	u32 data, id;

	host = pwm_faraday_chip_to_host(chip);
	chip_data = pwm_get_chip_data(pwm);
	cfg = pwm_faraday_get_cfg(pwm);

	id = pwm->pwm;
	min_period_ns = DIV_ROUND_UP(NSEC_PER_SEC, chip_data->clk_rate);

	data = pwm_faraday_readl(host, PWM_CTRL(id));
	cfg->out_inv = FIELD_GET(TMX_OUT_INV, data);
	state->polarity = (enum pwm_polarity)cfg->out_inv;
	state->enabled = FIELD_GET(TMX_START, data);
	data = pwm_faraday_readl(host, PWM_CNTB(id));
	state->period = ((u64)data + 1) * min_period_ns;
	data = pwm_faraday_readl(host, PWM_CMPB(id));
	state->duty_cycle = ((u64)data + 1) * min_period_ns;

	return 0;
}

/* TODO(b/247455060): use spinlocks for shared resources */
static const struct pwm_ops pwm_faraday_ops = {
	.request = pwm_faraday_request,
	.apply = pwm_faraday_apply,
	.get_state = pwm_faraday_get_state,
	.owner = THIS_MODULE,
};

static const struct pwm_faraday_variant r10301_4_variant = {
	.num_pwm = 4,
	.revision = 0x10301,
};

static const struct of_device_id pwm_faraday_of_match[] = {
	{ .compatible = "faraday,ftpwmtmr010", .data = &r10301_4_variant },
	{},
};
MODULE_DEVICE_TABLE(of, pwm_faraday_of_match);

static int pwm_faraday_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct pwm_faraday_host *host;
	struct pwm_chip *chip;
	u32 rev, chip_rev;
	int ret, ch;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	chip = &host->chip;
	chip->dev = dev;
	chip->ops = &pwm_faraday_ops;

	match = of_match_node(pwm_faraday_of_match, dev->of_node);
	memcpy(&host->variant, match->data, sizeof(host->variant));
	chip->npwm = host->variant.num_pwm;
	chip_rev = host->variant.revision;

	host->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->regs))
		return PTR_ERR(host->regs);
	host->irq = devm_kzalloc(dev, chip->npwm * sizeof(int), GFP_KERNEL);
	if (!host->irq)
		return -ENOMEM;

	for (ch = 0; ch < chip->npwm; ++ch) {
		host->irq[ch] = platform_get_irq(pdev, ch);
		if (host->irq[ch] < 0)
			return host->irq[ch];
		disable_irq(host->irq[ch]);
	}

	host->pwm_cfg_rst = devm_reset_control_get(dev, "pwm_cfg");
	if (IS_ERR(host->pwm_cfg_rst))
		return dev_err_probe(dev, PTR_ERR(host->pwm_cfg_rst),
				     "Unable to find pwm_cfg_rst\n");
	ret = reset_control_deassert(host->pwm_cfg_rst);
	if (ret) {
		dev_err(dev, "Failed to deassert config reset for pwm: %d\n",
			ret);
		return ret;
	}

	host->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(host->pclk))
		return dev_err_probe(dev, PTR_ERR(host->pclk),
				     "Unable to find pclk\n");

	ret = clk_prepare_enable(host->pclk);
	if (ret) {
		dev_err(dev, "Failed to enable pclk for pwm: %d\n", ret);
		return ret;
	}

	host->pwm_rst = devm_reset_control_get(dev, "pwm");
	if (IS_ERR(host->pwm_rst))
		return dev_err_probe(dev, PTR_ERR(host->pwm_rst),
				     "Unable to find pwm_rst\n");
	ret = reset_control_deassert(host->pwm_rst);
	if (ret) {
		dev_err(dev, "Failed to deassert reset for pwm: %d\n",
			ret);
		return ret;
	}

	host->ext_clk = devm_clk_get_optional(dev, "ext_clk");
	if (IS_ERR(host->ext_clk))
		return dev_err_probe(dev, PTR_ERR(host->ext_clk),
				     "Unable to find ext_clk\n");
	ret = clk_prepare_enable(host->ext_clk);
	if (ret) {
		dev_err(dev, "Failed to enable external clock for pwm: %d\n",
			ret);
		return ret;
	}

	rev = pwm_faraday_get_rev(host);
	if (rev != chip_rev) {
		dev_err(host->chip.dev,
			"Expected Revision ID 0x%x but got 0x%x", chip_rev,
			rev);
		ret = -ENXIO;
		goto disable_clk;
	}

	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err(dev, "Cannot register PWM: %d\n", ret);
		goto disable_clk;
	}

	platform_set_drvdata(pdev, host);
	dev_dbg(dev, "Faraday PWM chip registered %d PWMs\n", chip->npwm);

	WARN_ON(pm_runtime_enabled(&pdev->dev));
	pm_runtime_set_autosuspend_delay(&pdev->dev, RPM_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

disable_clk:
	clk_disable_unprepare(host->pclk);
	clk_disable_unprepare(host->ext_clk);

	return ret;
}

static void pwm_faraday_remove(struct platform_device *pdev)
{
	struct pwm_faraday_host *host = platform_get_drvdata(pdev);
	bool is_enabled = false;
	struct pwm_device *pwm;
	int ch;

	for (ch = 0; ch < host->chip.npwm; ++ch) {
		pwm = &host->chip.pwms[ch];
		if (pwm->state.enabled) {
			is_enabled = true;
			break;
		}
	}
	if (is_enabled) {
		clk_disable_unprepare(host->pclk);
		clk_disable_unprepare(host->ext_clk);
	}
	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);

	pwmchip_remove(&host->chip);
}

static int __maybe_unused pwm_faraday_runtime_suspend(struct device *dev)
{
	struct pwm_faraday_host *host = platform_get_drvdata(to_platform_device(dev));

	reset_control_assert(host->pwm_rst);
	reset_control_assert(host->pwm_cfg_rst);
	clk_disable_unprepare(host->ext_clk);
	clk_disable_unprepare(host->pclk);
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused pwm_faraday_runtime_resume(struct device *dev)
{
	struct pwm_faraday_host *host = platform_get_drvdata(to_platform_device(dev));
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret != 0)
		return ret;

	ret = clk_prepare_enable(host->pclk);
	if (ret != 0)
		return ret;

	ret = clk_prepare_enable(host->ext_clk);
	if (ret != 0)
		return ret;

	ret = reset_control_deassert(host->pwm_cfg_rst);
	if (ret != 0)
		return ret;

	return reset_control_deassert(host->pwm_rst);
}

static const struct dev_pm_ops pwm_faraday_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(pwm_faraday_runtime_suspend, pwm_faraday_runtime_resume, NULL)
};

static struct platform_driver pwm_faraday_driver = {
	.probe = pwm_faraday_probe,
	.remove = pwm_faraday_remove,
	.driver = {
		.name = "pwm-faraday",
		.of_match_table = pwm_faraday_of_match,
		.pm = &pwm_faraday_pm_ops,
	},
};
module_platform_driver(pwm_faraday_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Faraday PWM Host Controller Driver");
MODULE_LICENSE("GPL");
