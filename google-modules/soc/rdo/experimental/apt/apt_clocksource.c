// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clocksource.h>

#include "apt_internal.h"

static u64 google_apt_clocksource_read(struct clocksource *cs)
{
	struct google_apt *apt = clocksource_to_google_apt(cs);
	u32 lower;
	u32 upper_latch;

	lower = google_apt_readl(apt, GOOGLE_APT_GT_TIM_LOWER);
	upper_latch = google_apt_readl(apt, GOOGLE_APT_GT_TIM_UPPER_LATCH);
	return (u64)upper_latch << 32 | lower;
}

static void google_apt_clocksource_suspend(struct clocksource *cs)
{
	struct google_apt *apt = clocksource_to_google_apt(cs);
	u32 lower;
	u32 upper;

	google_apt_writel(apt, GOOGLE_APT_GT_TIM_CONFIG_VALUE_DISABLE,
			  GOOGLE_APT_GT_TIM_CONFIG);

	lower = google_apt_readl(apt, GOOGLE_APT_GT_TIM_LOWER);
	google_apt_writel(apt, lower, GOOGLE_APT_GT_TIM_LOAD_LOWER);

	upper = google_apt_readl(apt, GOOGLE_APT_GT_TIM_UPPER);
	google_apt_writel(apt, upper, GOOGLE_APT_GT_TIM_LOAD_UPPER);
}

static void google_apt_clocksource_resume(struct clocksource *cs)
{
	struct google_apt *apt = clocksource_to_google_apt(cs);

	google_apt_writel(apt, GOOGLE_APT_GT_TIM_CONFIG_VALUE_ENABLE,
			  GOOGLE_APT_GT_TIM_CONFIG);
}

int google_apt_clocksource_init(struct google_apt *apt)
{
	apt->clocksource.name = dev_name(apt->dev);
	apt->clocksource.rating = GOOGLE_APT_CLOCKSOURCE_RATING;
	apt->clocksource.read = google_apt_clocksource_read;
	apt->clocksource.mask = CLOCKSOURCE_MASK(64);
	apt->clocksource.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	apt->clocksource.suspend = google_apt_clocksource_suspend;
	apt->clocksource.resume = google_apt_clocksource_resume;
	apt->clocksource.owner = THIS_MODULE;

	return clocksource_register_hz(&apt->clocksource,
				       google_apt_get_prescaled_tclk_rate(apt));
}

void google_apt_clocksource_exit(struct google_apt *apt)
{
	clocksource_unregister(&apt->clocksource);
}
