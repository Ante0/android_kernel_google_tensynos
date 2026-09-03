// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include "sst/google_sst.h"

#define GOOGLE_SST_RATING 399

#define SST_GLOBAL_COUNTER_LOW		0x0C
#define SST_GLOBAL_COUNTER_HIGH_LATCH	0x10

#define SST_TIMER_BASE(n)		(0x1000 * ((n) + 1))
#define SST_TIMER_VALUE(n)		SST_TIMER_BASE(n)
#define SST_TIMER_CONTROL(n)		(SST_TIMER_BASE(n) + 0x04)
#define SST_TIMER_ENABLE		BIT(0)
#define SST_TIMER_MODE_ONESHOT		BIT(1)
#define SST_COMPARATOR_RESET		BIT(3)
#define SST_TIMER_INT_ENABLE		BIT(4)
#define SST_TIMER_COMPARATOR(n)		(SST_TIMER_BASE(n) + 0x08)

#define SST_CLK_EVT_MIN_DELTA		1
#define SST_CLK_EVT_MAX_DELTA		(0xffffffff - 1)

/*
 * For testing purpose
 */
static bool gtc_test_enabled;
module_param_named(enable_gtc_test, gtc_test_enabled, bool, 0);
static bool sst_timer_test_enabled;
module_param_named(enable_sst_timer_test, sst_timer_test_enabled, bool, 0);
static unsigned long sst_timer_test_threshold = 384000;
module_param(sst_timer_test_threshold, ulong, 0);

/*
 * We need to read twice and compare the delta to deal with a hardware issue.
 */
static u64 google_read_gtc_twice(void __iomem *base_addr)
{
	void __iomem *low_addr = base_addr + SST_GLOBAL_COUNTER_LOW;
	void __iomem *high_addr = base_addr + SST_GLOBAL_COUNTER_HIGH_LATCH;
	u32 lower;
	u32 lower2;
	u32 upper_latch;
	u32 upper_latch2;
	u64 result;
	u64 result2;
	u64 delta;
	int try_count = 100;

	while (try_count) {
		lower = readl(low_addr);
		smp_rmb(); /* address read should be in sequence */
		upper_latch = readl(high_addr);
		smp_rmb(); /* address read should be in sequence */
		lower2 = readl(low_addr);
		smp_rmb(); /* address read should be in sequence */
		upper_latch2 = readl(high_addr);
		result = ((u64)upper_latch << 32) | lower;
		result2 = ((u64)upper_latch2 << 32) | lower2;
		delta = result2 - result;
		if (delta < 2500) /* TODO figure out this value in future chips */
			break;
		try_count -= 1;
	}
	WARN_ON(try_count == 0);

	return result2;
}

/*
 * Read the Global Counter value from the SST registers.
 * We need to read twice and compare the delta to deal with the hardware issues in RDO and LGA.
 * In RDO, due to clock sync issue (b/264844533), the CSRs will be potentially non-deterministic.
 * In LGA, due to clock registers sharing between CAP and APC (b/367912552), the CSRs will be
 * potentially non-deterministic.
 */
static u64 google_read_gtc_from_sst(struct google_sst *sst)
{
	return google_read_gtc_twice(sst->base_addr);
}

static u64 google_sst_clks_src_read(struct clocksource *clk_src)
{
	struct google_sst *sst = container_of(clk_src, struct google_sst, clk_src);

	return sst->ops->read_gtc_time(sst);
}

static int google_sst_clocksource_init_and_register(struct google_sst *sst)
{
	struct clk *clk;
	unsigned long clk_rate;

	clk = devm_clk_get(sst->dev, "gtc");

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk_rate = clk_get_rate(clk);
	if (clk_rate == 0) {
		dev_err(sst->dev, "Get wrong clock rate (0)\n");
		return -EINVAL;
	}
	if (clk_rate > U32_MAX) {
		dev_warn(sst->dev, "Clock rate (%lu) too large for clock event framework!\n",
			 clk_rate);
		dev_warn(sst->dev, "Set the clock rate to U32_MAX, it may cause serious system problem, plz check\n");
		clk_rate = U32_MAX;
	}

	sst->freq = clk_rate;

	sst->clk_src.name = dev_name(sst->dev);
	sst->clk_src.flags = CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP;
	sst->clk_src.rating = GOOGLE_SST_RATING;
	sst->clk_src.read = google_sst_clks_src_read;
	sst->clk_src.mask = CLOCKSOURCE_MASK(56);
	sst->clk_src.owner = THIS_MODULE;

	return clocksource_register_hz(&sst->clk_src, sst->freq);
}

static int sst_timer_event_reset(struct clock_event_device *clk_evt)
{
	struct google_sst_timer *timer = container_of(clk_evt, struct google_sst_timer, clk_evt);
	struct google_sst *sst = timer->google_sst;
	size_t hw_idx = timer->hw_idx;

	dev_dbg(sst->dev, "%s reset (due to initialize or suspend)\n",
		clk_evt->name);

	writel(SST_TIMER_MODE_ONESHOT | SST_COMPARATOR_RESET | SST_TIMER_INT_ENABLE,
	       sst->base_addr + SST_TIMER_CONTROL(hw_idx));
	return 0;
}

static int sst_timer_set_next_event(unsigned long delta,
				    struct clock_event_device *clk_evt)
{
	struct google_sst_timer *timer = container_of(clk_evt, struct google_sst_timer, clk_evt);
	struct google_sst *sst = timer->google_sst;
	size_t hw_idx = timer->hw_idx;
	u32 value = 0;
	u64 time_now;

	if (delta > SST_CLK_EVT_MAX_DELTA || delta < SST_CLK_EVT_MIN_DELTA) {
		dev_err(sst->dev, "Delta (0x%lx) is out of range! Ignore!\n", delta);
		dev_err(sst->dev, "Max delta 0x%x, min delta 0x%x, plz check.\n",
			SST_CLK_EVT_MAX_DELTA, SST_CLK_EVT_MIN_DELTA);
		return -EINVAL;
	}

	value = delta;

	writel(SST_TIMER_MODE_ONESHOT | SST_COMPARATOR_RESET | SST_TIMER_INT_ENABLE,
	       sst->base_addr + SST_TIMER_CONTROL(hw_idx));
	writel(value, sst->base_addr + SST_TIMER_COMPARATOR(hw_idx));
	writel(0, sst->base_addr + SST_TIMER_VALUE(hw_idx));
	writel(SST_TIMER_ENABLE | SST_TIMER_MODE_ONESHOT | SST_TIMER_INT_ENABLE,
	       sst->base_addr + SST_TIMER_CONTROL(hw_idx));
	mb(); /* add memory barrier after writel */

	if (timer->timer_test_enabled) {
		time_now = sst->ops->read_gtc_time(sst);
		timer->next_event_delta = value;
		timer->next_event_request_time = time_now;
	}

	return 0;
}

static int sst_debugfs_test_enable_read(void *data, u64 *val)
{
	struct google_sst_timer *sst_timer = data;

	*val = sst_timer->timer_test_enabled;
	return 0;
}

static int sst_debugfs_test_enable_write(void *data, u64 val)
{
	struct google_sst_timer *sst_timer = data;
	struct google_sst *google_sst = sst_timer->google_sst;

	if (sst_timer->timer_test_enabled)
		dev_info(google_sst->dev, "Test count: %u\n", sst_timer->test_cnt);

	sst_timer->test_cnt = 0;
	sst_timer->next_event_delta = 0;
	sst_timer->next_event_request_time = 0;
	sst_timer->timer_test_enabled = !!val;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(sst_debugfs_test_enable_fops,
			 sst_debugfs_test_enable_read,
			 sst_debugfs_test_enable_write, "%llu\n");

static int gtc_debugfs_test_enable_read(void *data, u64 *val)
{
	struct google_sst *sst = data;

	*val = sst->timesource_test_enabled;
	return 0;
}

static int gtc_debugfs_test_enable_write(void *data, u64 val)
{
	struct google_sst *sst = data;
	u64 now_arm_timestamp_ns;
	u64 now_gtc_tick;
	u64 arm_timestamp_ns_diff;
	u64 gtc_tick_diff;
	u64 gtc_ns_diff;
	u64 ns_diff_delta;

	if (!val) {
		dev_info(sst->dev, "Clean last timestamp!\n");
		sst->last_arm_timestamp_ns = 0;
		sst->last_gtc_tick = 0;
		return 0;
	}

	now_arm_timestamp_ns = ktime_get();
	now_gtc_tick = sst->ops->read_gtc_time(sst);
	if (!sst->last_arm_timestamp_ns || !sst->last_gtc_tick) {
		dev_info(sst->dev, "Initialize time stamp!\n");
		goto update_test_gtc_timestamp;
	} else {
		arm_timestamp_ns_diff = now_arm_timestamp_ns - sst->last_arm_timestamp_ns;
		gtc_tick_diff = now_gtc_tick - sst->last_gtc_tick;

		/*
		 * SST time is (SST tick / SST tick rate)
		 * The kernel time should be 1000M ns per second
		 *   => SST time should be roughly equal to kernel ns
		 * And we accept 25% error rate for random kernel delay.
		 */
		gtc_ns_diff = gtc_tick_diff * ((u64)NSEC_PER_SEC / sst->freq);
		ns_diff_delta = arm_timestamp_ns_diff >> 2;
		if (likely(gtc_ns_diff > (arm_timestamp_ns_diff - ns_diff_delta)) &&
		    likely(gtc_ns_diff < (arm_timestamp_ns_diff + ns_diff_delta))) {
			dev_info(sst->dev, "Test pass!\n");
		} else {
			dev_info(sst->dev, "Test failed!\n");
			dev_info(sst->dev, "last: gtc tick %llu, kernel ns %llu\n",
				 sst->last_gtc_tick, sst->last_arm_timestamp_ns);
			dev_info(sst->dev, "now: gtc tick %llu, kernel ns %llu\n",
				 now_gtc_tick, now_arm_timestamp_ns);
			dev_info(sst->dev, "diff: gtc tick %llu, kernel ns %llu\n",
				 now_gtc_tick - sst->last_gtc_tick, arm_timestamp_ns_diff);
		}
	}
update_test_gtc_timestamp:
	sst->last_arm_timestamp_ns = now_arm_timestamp_ns;
	sst->last_gtc_tick = now_gtc_tick;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(gtc_debugfs_test_enable_fops,
			 gtc_debugfs_test_enable_read,
			 gtc_debugfs_test_enable_write, "%llu\n");

/*
 * Print the GTC and the AP clock boottime split by a space
 * "{GTC} {AP's clock boottime}"
 */
static ssize_t gtc_and_clock_boottime_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct google_sst *sst = dev_get_drvdata(dev);

	return (ssize_t)sysfs_emit(buf, "%llu %llu\n",
				   sst->ops->read_gtc_time(sst), /* GTC time */
				   ktime_get_boottime()); /* clock boottime */
}
static DEVICE_ATTR_RO(gtc_and_clock_boottime);

static int google_sst_clockevent_init(struct platform_device *pdev,
				      struct google_sst *sst, size_t idx)
{
	struct device *dev = &pdev->dev;
	struct google_sst_timer *sst_timer = &sst->sst_timer_arr[idx];
	struct clock_event_device *clk_evt = &sst_timer->clk_evt;
	struct clk *clk;
	char clk_name[10];
	unsigned long clk_rate;
	int irq_hdl;
	u32 min_delta;
	u32 max_delta;

	sst_timer->google_sst = sst;
	clk_evt->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_DYNIRQ;
	clk_evt->set_next_event = sst_timer_set_next_event;
	clk_evt->set_state_oneshot  = sst_timer_event_reset;
	clk_evt->set_state_shutdown  = sst_timer_event_reset;
	sst_timer->hw_idx = idx + 1; /* the 0th index should not be used for clockevent */

	irq_hdl = platform_get_irq(pdev, idx);
	if (irq_hdl < 0) {
		dev_err(dev, "Get irq (%zu) failed. ret %d.\n", idx, irq_hdl);
		return irq_hdl;
	}

	sst_timer->irq_hdl = irq_hdl;
	clk_evt->irq = irq_hdl;
	clk_evt->rating = GOOGLE_SST_RATING;
	clk_evt->bound_on = -1;
	clk_evt->cpumask = cpu_possible_mask;
	clk_evt->owner = THIS_MODULE;
	clk_evt->name = devm_kasprintf(dev, GFP_KERNEL, "%s.clk_evt.%zu",
				       dev_name(dev), idx);
	if (!clk_evt->name)
		dev_warn(dev,
			 "Failed to create name for clock event %zu. OOM?\n",
			 idx);

	snprintf(clk_name, sizeof(clk_name), "sst_%zu", sst_timer->hw_idx);
	clk = devm_clk_get(sst->dev, clk_name);

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	clk_rate = clk_get_rate(clk);
	if (clk_rate == 0) {
		dev_err(sst->dev, "Get wrong clock rate (0)\n");
		return -EINVAL;
	}

	min_delta = SST_CLK_EVT_MIN_DELTA;
	max_delta = SST_CLK_EVT_MAX_DELTA;

	clockevents_config_and_register(clk_evt, clk_rate, min_delta, max_delta);
	if (sst_timer_test_enabled) {
		sst_timer->debugfs_test = debugfs_create_dir(clk_evt->name,
							     sst->debugfs_root);
		debugfs_create_file("debug", 0664, sst_timer->debugfs_test,
				    sst_timer, &sst_debugfs_test_enable_fops);
	}
	return 0;
}

static irqreturn_t sst_timer_isr(int irq, void *data)
{
	struct google_sst_timer *sst_timer = data;
	struct google_sst *sst = sst_timer->google_sst;
	size_t hw_idx = sst_timer->hw_idx;
	struct device *dev = sst->dev;
	u64 gtc_time_now = sst->ops->read_gtc_time(sst);
	u32 sst_request_delta = sst_timer->next_event_delta;
	u64 next_event_request_time = sst_timer->next_event_request_time;
	u64 gtc_time_diff;
	u64 request_delta_tolerance = sst_request_delta >> 1;
	u64 request_delta_max = sst_request_delta + request_delta_tolerance;
	u64 request_delta_min = sst_request_delta - request_delta_tolerance;

	if (sst_timer->timer_test_enabled && sst_request_delta && next_event_request_time) {
		/*
		 * The gtc clock rate is the same as sst clock rate,
		 * If the tick is in between 0.5 * (ideal tick count) and 1.5 * (ideal tick count),
		 * we treat it as pass.
		 */
		gtc_time_diff = (gtc_time_now - next_event_request_time);
		if (sst_request_delta > sst_timer_test_threshold &&
		    (gtc_time_diff > request_delta_max || gtc_time_diff < request_delta_min)) {
			dev_err(dev, "SST Test out of bound!\n");
			dev_err(dev, "gtc_time_diff %llu\n", gtc_time_diff);
			dev_err(dev, "sst_request_delta %u\n", sst_request_delta);
			dev_err(dev, "request_delta_max %llu\n", request_delta_max);
			dev_err(dev, "request_delta_min %llu\n", request_delta_min);
		}
		sst_request_delta = 0;
		next_event_request_time = 0;
		sst_timer->test_cnt++;
	}

	writel(SST_TIMER_MODE_ONESHOT | SST_COMPARATOR_RESET | SST_TIMER_INT_ENABLE,
	       sst->base_addr + SST_TIMER_CONTROL(hw_idx));

	if (sst_timer->clk_evt.event_handler)
		sst_timer->clk_evt.event_handler(&sst_timer->clk_evt);

	return IRQ_HANDLED;
}

static int google_sst_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct google_sst *google_sst;
	u32 timer_cnt;
	size_t i;
	int ret;

	google_sst = devm_kzalloc(dev, sizeof(*google_sst), GFP_KERNEL);
	if (!google_sst)
		return -ENOMEM;

	google_sst->ops = of_device_get_match_data(dev);

	google_sst->dev = dev;

	google_sst->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(google_sst->base_addr))
		return PTR_ERR(google_sst->base_addr);

	ret = google_sst_clocksource_init_and_register(google_sst);
	if (ret < 0) {
		dev_err(dev, "Register clock source failed. ret %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(node, "timer-cnt", &timer_cnt);
	if (ret < 0) {
		dev_warn(dev, "Read timer-cnt error! ret %d\n", ret);
		dev_warn(dev, "Assume we don't have any timer\n");
		timer_cnt = 0;
	}

	google_sst->sst_timer_arr_size = timer_cnt;
	google_sst->sst_timer_arr = devm_kcalloc(dev, timer_cnt,
						 sizeof(*google_sst->sst_timer_arr),
						 GFP_KERNEL);
	if (!google_sst->sst_timer_arr) {
		ret = -ENOMEM;
		goto remove_clocksource;
	}
	pr_err("enable_gtc_test %d\n", gtc_test_enabled);
	pr_err("sst_timer_test_enabled %d\n", sst_timer_test_enabled);
	pr_err("sst_timer_test_threshold %lu\n", sst_timer_test_threshold);

	if (gtc_test_enabled || sst_timer_test_enabled)
		google_sst->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	if (gtc_test_enabled)
		debugfs_create_file("gtc_debug", 0664, google_sst->debugfs_root,
				    google_sst, &gtc_debugfs_test_enable_fops);
	for (i = 0; i < timer_cnt; ++i) {
		ret = google_sst_clockevent_init(pdev, google_sst, i);
		if (ret < 0) {
			dev_err(dev, "init clock event (%zu) failed. ret %d.\n",
				i, ret);
			goto remove_clocksource;
		}
	}

	for (i = 0; i < timer_cnt; ++i) {
		ret = devm_request_irq(dev, google_sst->sst_timer_arr[i].irq_hdl, sst_timer_isr,
				       IRQF_TIMER, dev_name(dev),
				       &google_sst->sst_timer_arr[i]);
		if (ret) {
			dev_err(dev, "Request irq (%d) failed. ret %d.\n",
				google_sst->sst_timer_arr[i].irq_hdl, ret);
			goto remove_clocksource;
		}
	}

	device_create_file(dev, &dev_attr_gtc_and_clock_boottime);

	platform_set_drvdata(pdev, google_sst);

	return 0;

remove_clocksource:
	clocksource_unregister(&google_sst->clk_src);

	return ret;
}

static void google_sst_remove(struct platform_device *pdev)
{
	struct google_sst *sst = platform_get_drvdata(pdev);

	clocksource_unregister(&sst->clk_src);
	device_remove_file(sst->dev, &dev_attr_gtc_and_clock_boottime);
}

static const struct google_sst_ops sst_ops = {
	.read_gtc_time = google_read_gtc_from_sst,
};

static const struct of_device_id google_sst_of_match_table[] = {
	{ .compatible = "google,sst-rdo", .data = &sst_ops},
	{ .compatible = "google,sst", .data = &sst_ops},
	{}
};
MODULE_DEVICE_TABLE(of, google_sst_of_match_table);

static struct platform_driver google_sst_driver = {
	.probe = google_sst_probe,
	.remove = google_sst_remove,
	.driver = {
		.name = "google-sst",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_sst_of_match_table),
	},
};
module_platform_driver(google_sst_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google SST driver");
MODULE_LICENSE("GPL");
