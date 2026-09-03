// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>

#include "apt_internal.h"
#include "apt_gt_comp.h"

static void gt_comp_disable(struct google_apt_gt_comp *gt_comp)
{
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	const int idx = gt_comp->idx;
	u32 config;

	config = google_apt_readl(apt, GOOGLE_APT_GT_COMPX_CONFIG(idx));
	config &= ~GOOGLE_APT_GT_COMPX_CONFIG_FIELD_ENABLE;
	google_apt_writel(apt, config, GOOGLE_APT_GT_COMPX_CONFIG(idx));
}

static void gt_comp_enable(struct google_apt_gt_comp *gt_comp)
{
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	const int idx = gt_comp->idx;
	u32 config;

	config = google_apt_readl(apt, GOOGLE_APT_GT_COMPX_CONFIG(idx));
	config |= GOOGLE_APT_GT_COMPX_CONFIG_FIELD_ENABLE;
	google_apt_writel(apt, config, GOOGLE_APT_GT_COMPX_CONFIG(idx));
}

static int gt_comp_debug_itr_trigger_write(void *data, u64 val)
{
	struct google_apt_gt_comp *gt_comp = data;
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	const int idx = gt_comp->idx;

	if (!val)
		return -EINVAL;
	google_apt_writel(apt, GOOGLE_APT_INTERRUPT_FIELD,
			  GOOGLE_APT_GT_COMPX_INTERRUPT_ITR(idx));
	google_apt_writel(apt, 0x0, GOOGLE_APT_GT_COMPX_INTERRUPT_ITR(idx));
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(gt_comp_debug_itr_trigger_fops, NULL,
			 gt_comp_debug_itr_trigger_write, "%llu\n");

static void gt_comp_init_debugfs(struct google_apt_gt_comp *gt_comp)
{
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	struct device *dev = apt->dev;

	gt_comp->debugfs = debugfs_create_dir(gt_comp->name, apt->debugfs);
	if (IS_ERR(gt_comp->debugfs)) {
		dev_warn(dev, "%s: failed to create debugfs", gt_comp->name);
		return;
	}

	debugfs_create_u32("irqcount", 0600, gt_comp->debugfs,
			   &gt_comp->debug_irqcount);
	debugfs_create_file_unsafe("itr_trigger", 0200, gt_comp->debugfs,
				   gt_comp, &gt_comp_debug_itr_trigger_fops);
}

static irqreturn_t gt_comp_isr(int irq, void *data)
{
	struct google_apt_gt_comp *gt_comp = data;
	const int idx = gt_comp->idx;
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	u32 isr;
	u32 isr_ovf;

	isr = google_apt_readl(apt, GOOGLE_APT_GT_COMPX_INTERRUPT_ISR(idx));
	google_apt_writel(apt, isr, GOOGLE_APT_GT_COMPX_INTERRUPT_ISR(idx));

	isr_ovf = google_apt_readl(apt,
				   GOOGLE_APT_GT_COMPX_INTERRUPT_ISR_OVF(idx));
	google_apt_writel(apt, isr_ovf,
			  GOOGLE_APT_GT_COMPX_INTERRUPT_ISR_OVF(idx));

	if (gt_comp->clkevt.event_handler)
		gt_comp->clkevt.event_handler(&gt_comp->clkevt);
	gt_comp->debug_irqcount += 1;
	return IRQ_HANDLED;
}

static int gt_comp_set_next_event(unsigned long delta,
				  struct clock_event_device *clkevt)
{
	struct google_apt_gt_comp *gt_comp =
		clkevt_to_google_apt_gt_comp(clkevt);
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	struct device *dev = apt->dev;
	const int idx = gt_comp->idx;
	u32 tim_lower;
	u32 tim_upper_latch;
	u64 tim;

	dev_dbg(dev, "%s: set next one shot event, irq count: %d.\n",
		clkevt->name, gt_comp->debug_irqcount);

	gt_comp_disable(gt_comp);

	tim_lower = google_apt_readl(apt, GOOGLE_APT_GT_TIM_LOWER);
	tim_upper_latch = google_apt_readl(apt, GOOGLE_APT_GT_TIM_UPPER_LATCH);
	tim = ((u64)tim_upper_latch << 32 | tim_lower) + delta;

	google_apt_writel(apt, tim >> 32, GOOGLE_APT_GT_COMPX_UPPER_LATCH(idx));
	google_apt_writel(apt, tim & 0xFFFFFFFFul,
			  GOOGLE_APT_GT_COMPX_LOWER(idx));

	gt_comp_enable(gt_comp);
	return 0;
}

static int gt_comp_set_state_periodic(struct clock_event_device *clkevt)
{
	struct google_apt_gt_comp *gt_comp =
		clkevt_to_google_apt_gt_comp(clkevt);
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	struct device *dev = apt->dev;
	const int idx = gt_comp->idx;
	u32 config;

	dev_dbg(dev, "%s: set state to periodic.\n", clkevt->name);
	config = google_apt_readl(apt, GOOGLE_APT_GT_COMPX_CONFIG(idx));
	config &= ~GOOGLE_APT_GT_COMPX_CONFIG_FIELD_MODE;
	config |= FIELD_PREP(GOOGLE_APT_GT_COMPX_CONFIG_FIELD_MODE,
			     GOOGLE_APT_GT_COMPX_CONFIG_MODE_AUTO_INCREMENT);
	google_apt_writel(apt, config, GOOGLE_APT_GT_COMPX_CONFIG(idx));
	return 0;
}

static int gt_comp_set_state_oneshot(struct clock_event_device *clkevt)
{
	struct google_apt_gt_comp *gt_comp =
		clkevt_to_google_apt_gt_comp(clkevt);
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	struct device *dev = apt->dev;
	const int idx = gt_comp->idx;
	u32 config;

	dev_dbg(dev, "%s: set state to oneshot.\n", clkevt->name);
	config = google_apt_readl(apt, GOOGLE_APT_GT_COMPX_CONFIG(idx));
	config &= ~GOOGLE_APT_GT_COMPX_CONFIG_FIELD_MODE;
	config |= FIELD_PREP(GOOGLE_APT_GT_COMPX_CONFIG_FIELD_MODE,
			     GOOGLE_APT_GT_COMPX_CONFIG_MODE_ONESHOT);
	google_apt_writel(apt, config, GOOGLE_APT_GT_COMPX_CONFIG(idx));
	return 0;
}

static int gt_comp_set_state_oneshot_stopped(struct clock_event_device *clkevt)
{
	struct google_apt_gt_comp *gt_comp =
		clkevt_to_google_apt_gt_comp(clkevt);
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	struct device *dev = apt->dev;

	dev_dbg(dev, "%s: set state to oneshot_stopped.\n", clkevt->name);
	gt_comp_disable(gt_comp);
	return 0;
}

static int gt_comp_set_state_shutdown(struct clock_event_device *clkevt)
{
	struct google_apt_gt_comp *gt_comp =
		clkevt_to_google_apt_gt_comp(clkevt);
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	struct device *dev = apt->dev;

	dev_dbg(dev, "%s: set state to shutdown.\n", clkevt->name);
	gt_comp_disable(gt_comp);
	return 0;
}

static void gt_comp_init_clockevents(struct google_apt_gt_comp *gt_comp)
{
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	struct device *dev = apt->dev;
	struct clock_event_device *clkevt = &gt_comp->clkevt;

	clkevt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;

	clkevt->set_next_event = gt_comp_set_next_event;
	clkevt->set_state_periodic = gt_comp_set_state_periodic;
	clkevt->set_state_oneshot = gt_comp_set_state_oneshot;
	clkevt->set_state_oneshot_stopped = gt_comp_set_state_oneshot_stopped;
	clkevt->set_state_shutdown = gt_comp_set_state_shutdown;

	// Nothing more is required to initialize for
	// clkevt->set_state_(periodic|oneshot) when timer is shutdown, so we
	// don't have clkevt->tick_resume.

	// clkevt->(suspend|resume) is for higher level flow, however
	// clkevt->set_set_shutdown should be invoked already before
	// clkevt->suspend, and clkevt->tick_resume should be invoked before
	// clkevt->resume.

	clkevt->name = devm_kasprintf(dev, GFP_KERNEL, "%s.%s", dev_name(dev),
				      gt_comp->name);
	clkevt->rating = GOOGLE_APT_GT_COMP_CLKEVT_RATING;
	clkevt->irq = gt_comp->irq;
	clkevt->bound_on = -1;
	clkevt->cpumask = cpu_possible_mask;
	clkevt->owner = THIS_MODULE;

	clockevents_config_and_register(clkevt,
					google_apt_get_prescaled_tclk_rate(apt),
					GOOGLE_APT_GT_COMP_MIN_DELTA,
					GOOGLE_APT_GT_COMP_MAX_DELTA);
}

int google_apt_gt_comp_init(struct google_apt_gt_comp *gt_comp)
{
	struct google_apt *apt = gt_comp_to_google_apt(gt_comp);
	struct device *dev = apt->dev;
	const int idx = gt_comp->idx;
	int err;

	gt_comp->name = devm_kasprintf(dev, GFP_KERNEL, "gt_comp%d", idx);
	gt_comp_init_debugfs(gt_comp);

	err = devm_request_irq(dev, gt_comp->irq, gt_comp_isr, IRQF_TIMER,
			       dev_name(dev), gt_comp);
	if (err) {
		dev_err(dev, "failed to request irq %d for %s", gt_comp->irq,
			gt_comp->clkevt.name);
		return err;
	}

	gt_comp_init_clockevents(gt_comp);

	google_apt_writel(apt, GOOGLE_APT_INTERRUPT_FIELD,
			  GOOGLE_APT_GT_COMPX_INTERRUPT_IMR(idx));
	return 0;
}

int google_apt_gt_comp_stat(const struct google_apt_gt_comp *gt_comp,
			    struct seq_file *file)
{
	seq_puts(file, "=== global timer comparator stat ===");
	seq_printf(file, "name: %s\n", gt_comp->name);
	seq_printf(file, "clockevents name: %s\n", gt_comp->clkevt.name);
	seq_printf(file, "irq id: %d\n", gt_comp->irq);
	seq_printf(file, "irq count: %d\n", gt_comp->debug_irqcount);
	return 0;
}
