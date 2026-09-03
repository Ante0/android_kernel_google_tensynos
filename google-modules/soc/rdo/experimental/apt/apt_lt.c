// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>

#include "apt_internal.h"
#include "apt_lt.h"

static void lt_disable(struct google_apt_lt *lt)
{
	struct google_apt *apt = lt_to_google_apt(lt);
	const int idx = lt->idx;
	u32 config;

	config = google_apt_readl(apt, GOOGLE_APT_LTX_CONFIG(idx));
	config &= ~GOOGLE_APT_LTX_CONFIG_FIELD_ENABLE;
	google_apt_writel(apt, config, GOOGLE_APT_LTX_CONFIG(idx));
}

static void lt_enable(struct google_apt_lt *lt)
{
	struct google_apt *apt = lt_to_google_apt(lt);
	const int idx = lt->idx;
	u32 config;

	config = google_apt_readl(apt, GOOGLE_APT_LTX_CONFIG(idx));
	config |= GOOGLE_APT_LTX_CONFIG_FIELD_ENABLE;
	google_apt_writel(apt, config, GOOGLE_APT_LTX_CONFIG(idx));
}

static int lt_debug_itr_trigger_write(void *data, u64 val)
{
	struct google_apt_lt *lt = data;
	struct google_apt *apt = lt_to_google_apt(lt);
	const int idx = lt->idx;

	if (!val)
		return -EINVAL;
	google_apt_writel(apt, GOOGLE_APT_INTERRUPT_FIELD,
			  GOOGLE_APT_LTX_INTERRUPT_ITR(idx));
	google_apt_writel(apt, 0x0, GOOGLE_APT_LTX_INTERRUPT_ITR(idx));
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(lt_debug_itr_trigger_fops, NULL,
			 lt_debug_itr_trigger_write, "%llu\n");

static void lt_init_debugfs(struct google_apt_lt *lt)
{
	struct google_apt *apt = lt_to_google_apt(lt);
	struct device *dev = apt->dev;

	lt->debugfs = debugfs_create_dir(lt->name, apt->debugfs);
	if (IS_ERR(lt->debugfs)) {
		dev_warn(dev, "%s: failed to create debugfs", lt->name);
		return;
	}

	debugfs_create_u32("irqcount", 0600, lt->debugfs, &lt->debug_irqcount);
	debugfs_create_file_unsafe("itr_trigger", 0200, lt->debugfs, lt,
				   &lt_debug_itr_trigger_fops);
}

static irqreturn_t lt_isr(int irq, void *data)
{
	struct google_apt_lt *lt = data;
	struct google_apt *apt = lt_to_google_apt(lt);
	const int idx = lt->idx;
	u32 isr;
	u32 isr_ovf;

	isr = google_apt_readl(apt, GOOGLE_APT_LTX_INTERRUPT_ISR(idx));
	google_apt_writel(apt, isr, GOOGLE_APT_LTX_INTERRUPT_ISR(idx));

	isr_ovf = google_apt_readl(apt, GOOGLE_APT_LTX_INTERRUPT_ISR_OVF(idx));
	google_apt_writel(apt, isr_ovf, GOOGLE_APT_LTX_INTERRUPT_ISR_OVF(idx));

	if (lt->clkevt.event_handler)
		lt->clkevt.event_handler(&lt->clkevt);
	lt->debug_irqcount += 1;
	return IRQ_HANDLED;
}

static int lt_set_next_event(unsigned long delta,
			     struct clock_event_device *clkevt)
{
	struct google_apt_lt *lt = clkevt_to_google_apt_lt(clkevt);
	struct google_apt *apt = lt_to_google_apt(lt);
	struct device *dev = apt->dev;
	const int idx = lt->idx;

	dev_dbg(dev, "%s: set next one shot event, irq count: %d.\n",
		clkevt->name, lt->debug_irqcount);

	lt_disable(lt);
	google_apt_writel(apt, delta, GOOGLE_APT_LTX_CNT_LOAD(idx));
	lt_enable(lt);
	return 0;
}

static int lt_set_state_periodic(struct clock_event_device *clkevt)
{
	struct google_apt_lt *lt = clkevt_to_google_apt_lt(clkevt);
	struct google_apt *apt = lt_to_google_apt(lt);
	struct device *dev = apt->dev;
	const int idx = lt->idx;
	u32 config;

	dev_dbg(dev, "%s: set state to periodic.\n", clkevt->name);
	config = google_apt_readl(apt, GOOGLE_APT_LTX_CONFIG(idx));
	config &= ~GOOGLE_APT_LTX_CONFIG_FIELD_MODE;
	config |= FIELD_PREP(GOOGLE_APT_LTX_CONFIG_FIELD_MODE,
			     GOOGLE_APT_LTX_CONFIG_MODE_AUTORELOAD);
	google_apt_writel(apt, config, GOOGLE_APT_LTX_CONFIG(idx));
	return 0;
}

static int lt_set_state_oneshot(struct clock_event_device *clkevt)
{
	struct google_apt_lt *lt = clkevt_to_google_apt_lt(clkevt);
	struct google_apt *apt = lt_to_google_apt(lt);
	struct device *dev = apt->dev;
	const int idx = lt->idx;
	u32 config;

	dev_dbg(dev, "%s: set state to oneshot.\n", clkevt->name);
	config = google_apt_readl(apt, GOOGLE_APT_LTX_CONFIG(idx));
	config &= ~GOOGLE_APT_LTX_CONFIG_FIELD_MODE;
	config |= FIELD_PREP(GOOGLE_APT_LTX_CONFIG_FIELD_MODE,
			     GOOGLE_APT_LTX_CONFIG_MODE_ONESHOT);
	google_apt_writel(apt, config, GOOGLE_APT_LTX_CONFIG(idx));
	return 0;
}

static int lt_set_state_oneshot_stopped(struct clock_event_device *clkevt)
{
	struct google_apt_lt *lt = clkevt_to_google_apt_lt(clkevt);
	struct google_apt *apt = lt_to_google_apt(lt);
	struct device *dev = apt->dev;

	dev_dbg(dev, "%s: set state to oneshot_stopped.\n", clkevt->name);
	lt_disable(lt);
	return 0;
}

static int lt_set_state_shutdown(struct clock_event_device *clkevt)
{
	struct google_apt_lt *lt = clkevt_to_google_apt_lt(clkevt);
	struct google_apt *apt = lt_to_google_apt(lt);
	struct device *dev = apt->dev;

	dev_dbg(dev, "%s: set state to shutdown.\n", clkevt->name);
	lt_disable(lt);
	return 0;
}

static void lt_init_clockevents(struct google_apt_lt *lt)
{
	struct google_apt *apt = lt_to_google_apt(lt);
	struct device *dev = apt->dev;
	struct clock_event_device *clkevt = &lt->clkevt;

	clkevt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;

	clkevt->set_next_event = lt_set_next_event;
	clkevt->set_state_periodic = lt_set_state_periodic;
	clkevt->set_state_oneshot = lt_set_state_oneshot;
	clkevt->set_state_oneshot_stopped = lt_set_state_oneshot_stopped;
	clkevt->set_state_shutdown = lt_set_state_shutdown;

	// Nothing more is required to initialize for
	// clkevt->set_state_(periodic|oneshot) when timer is shutdown, so we
	// don't have clkevt->tick_resume.

	// clkevt->(suspend|resume) is for higher level flow, however
	// clkevt->set_set_shutdown should be invoked already before
	// clkevt->suspend, and clkevt->tick_resume should be invoked before
	// clkevt->resume.

	clkevt->name = devm_kasprintf(dev, GFP_KERNEL, "%s.%s", dev_name(dev),
				      lt->name);
	clkevt->rating = GOOGLE_APT_LT_CLKEVT_RATING;
	clkevt->irq = lt->irq;
	clkevt->bound_on = -1;
	clkevt->cpumask = cpu_possible_mask;
	clkevt->owner = THIS_MODULE;

	clockevents_config_and_register(clkevt,
					google_apt_get_prescaled_tclk_rate(apt),
					GOOGLE_APT_LT_MIN_DELTA,
					GOOGLE_APT_LT_MAX_DELTA);
}

int google_apt_lt_init(struct google_apt_lt *lt)
{
	struct google_apt *apt = lt_to_google_apt(lt);
	struct device *dev = apt->dev;
	const int idx = lt->idx;
	int err;

	lt->name = devm_kasprintf(dev, GFP_KERNEL, "lt%d", idx);
	lt_init_debugfs(lt);

	err = devm_request_irq(dev, lt->irq, lt_isr, IRQF_TIMER, dev_name(dev),
			       lt);
	if (err) {
		dev_err(dev, "failed to request irq %d for %s.\n", lt->irq,
			lt->clkevt.name);
		return err;
	}

	lt_init_clockevents(lt);

	google_apt_writel(apt, GOOGLE_APT_INTERRUPT_FIELD,
			  GOOGLE_APT_LTX_INTERRUPT_IMR(idx));
	return 0;
}

int google_apt_lt_stat(const struct google_apt_lt *lt, struct seq_file *file)
{
	seq_puts(file, "=== local timer stat ===");
	seq_printf(file, "name: %s\n", lt->name);
	seq_printf(file, "clockevents name: %s\n", lt->clkevt.name);
	seq_printf(file, "irq id: %d\n", lt->irq);
	seq_printf(file, "irq count: %d\n", lt->debug_irqcount);
	return 0;
}
