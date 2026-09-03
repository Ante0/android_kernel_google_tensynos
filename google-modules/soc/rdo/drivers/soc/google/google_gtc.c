// SPDX-License-Identifier: GPL-2.0-only
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>

#include <asm/arch_timer.h>
#include <clocksource/arm_arch_timer.h>

#include <soc/google/google_gtc.h>

static struct dentry *gtc_debugfs_dir;
static u64 gtc_debugfs_user_ticks;
static u64 gtc_freq_hz __ro_after_init;

// TODO: b/380782170 Refactor to remove the macro part
#if !(IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_RDO))
static u64 div_u64_and_lshift(u64 dividend, u64 divisor, u64 lshift)
{
	u64 quotient, remainder;

	quotient = dividend / divisor;
	remainder = dividend - (quotient * divisor);

	return (quotient << lshift) + ((remainder << lshift) / divisor);
}
#endif

u64 goog_gtc_get_counter(void)
{
// TODO: b/380782170 Refactor to remove the macro part
#if IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_RDO)
	return arch_timer_read_counter();
#else
	/* 26.0416259765625 = 213333 / 8192 = 21333 / (2^13)
	 * GTC tick = Arch Timer tick / 26.0416259765625
	 *          = (Arch Timer tick / 213333) * (2^13)
	 *          = (Arch Timer tick / 213333) << 13
	 */
	return div_u64_and_lshift(arch_timer_read_counter(), 213333, 13);
#endif
}
EXPORT_SYMBOL_GPL(goog_gtc_get_counter);

/**
 * goog_gtc_ticks_to_units - Converts GTC ticks to a generic time unit.
 * @gtc_tick:   The number of GTC ticks.
 * @multiplier: The number of units per second
 *              (e.g., NSEC_PER_SEC or USEC_PER_SEC).
 *
 * This is an internal helper function that performs the core conversion logic
 * to prevent u64 overflow by calculating the seconds and the sub-second
 * parts separately.
 *
 * Return: The corresponding value in the desired unit.
 */
static inline u64 goog_gtc_ticks_to_units(u64 gtc_tick, u64 multiplier)
{
	u64 sec, sub_tick, sub_sec;

	/*
	 * The simple calculation is:
	 *   result = tick / freq * multiplier
	 * The calculation is split to prevent overflow.
	 * In math:
	 *   seconds part     = (tick - (tick % freq)) / freq
	 *   sub-seconds part = ((tick % freq) / freq) * multiplier
	 * In C code, after considering the rounding of integer division:
	 *   seconds part      = tick / freq
	 *   sub-second part   = ((tick % freq) * multiplier) / freq
	 * The final result is (sec * multiplier) + sub_sec.
	 */
	sec = gtc_tick / gtc_freq_hz;
	sub_tick = gtc_tick - (sec * gtc_freq_hz); /* gtc_tick % gtc_freq_hz */
	sub_sec = (sub_tick * multiplier) / gtc_freq_hz;

	return (sec * multiplier) + sub_sec;
}

u64 goog_gtc_ticks_to_ns(u64 gtc_tick)
{
	return goog_gtc_ticks_to_units(gtc_tick, NSEC_PER_SEC);
}
EXPORT_SYMBOL_GPL(goog_gtc_ticks_to_ns);

u64 goog_gtc_ticks_to_us(u64 gtc_tick)
{
	return goog_gtc_ticks_to_units(gtc_tick, USEC_PER_SEC);
}
EXPORT_SYMBOL_GPL(goog_gtc_ticks_to_us);

u64 goog_gtc_get_time_ns(void)
{
	return goog_gtc_ticks_to_ns(goog_gtc_get_counter());
}
EXPORT_SYMBOL_GPL(goog_gtc_get_time_ns);

u64 goog_gtc_get_time_us(void)
{
	return goog_gtc_ticks_to_us(goog_gtc_get_counter());
}
EXPORT_SYMBOL_GPL(goog_gtc_get_time_us);

u64 goog_gtc_get_freq_hz(void)
{
	return gtc_freq_hz;
}
EXPORT_SYMBOL_GPL(goog_gtc_get_freq_hz);

/* Read-Only files for current GTC timestamp */

static int gtc_ticks_show(struct seq_file *seq, void *data)
{
	seq_printf(seq, "%llu\n", goog_gtc_get_counter());

	return 0;
}

static int gtc_ns_show(struct seq_file *seq, void *data)
{
	seq_printf(seq, "%llu\n", goog_gtc_get_time_ns());

	return 0;
}

static int gtc_us_show(struct seq_file *seq, void *data)
{
	seq_printf(seq, "%llu\n", goog_gtc_get_time_us());

	return 0;
}

static int gtc_all_show(struct seq_file *seq, void *data)
{
	u64 ticks = goog_gtc_get_counter();

	seq_printf(seq, "ticks: %llu\n", ticks);
	seq_printf(seq, "ns:    %llu\n", goog_gtc_ticks_to_ns(ticks));
	seq_printf(seq, "us:    %llu\n", goog_gtc_ticks_to_us(ticks));

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(gtc_ticks);
DEFINE_SHOW_ATTRIBUTE(gtc_ns);
DEFINE_SHOW_ATTRIBUTE(gtc_us);
DEFINE_SHOW_ATTRIBUTE(gtc_all);

/* Files for converting user-provided ticks */

static int user_ticks_to_ns_show(struct seq_file *seq, void *data)
{
	seq_printf(seq, "%llu\n", goog_gtc_ticks_to_ns(gtc_debugfs_user_ticks));

	return 0;
}

static int user_ticks_to_us_show(struct seq_file *seq, void *data)
{
	seq_printf(seq, "%llu\n", goog_gtc_ticks_to_us(gtc_debugfs_user_ticks));

	return 0;
}

static int user_ticks_to_all_show(struct seq_file *seq, void *data)
{
	seq_printf(seq, "ticks: %llu\n", gtc_debugfs_user_ticks);
	seq_printf(seq, "ns:    %llu\n", goog_gtc_ticks_to_ns(gtc_debugfs_user_ticks));
	seq_printf(seq, "us:    %llu\n", goog_gtc_ticks_to_us(gtc_debugfs_user_ticks));

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(user_ticks_to_ns);
DEFINE_SHOW_ATTRIBUTE(user_ticks_to_us);
DEFINE_SHOW_ATTRIBUTE(user_ticks_to_all);

static int __init gtc_init(void)
{
	int ret = 0;

// TODO: b/380782170 Refactor to remove the macro part
#if IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_RDO)
	/*
	 * Since the ARM arch timer has the same frequency as GTC,
	 * the API using the ARM arch timer frequency as well.
	 */
	gtc_freq_hz = arch_timer_get_cntfrq();
#else
	gtc_freq_hz = 38400000ULL;
#endif

	gtc_debugfs_dir = debugfs_create_dir("gtc", NULL);
	if (IS_ERR(gtc_debugfs_dir)) {
		ret = PTR_ERR(gtc_debugfs_dir);
		pr_err("%s: Fail to create gtc debugfs\n", __func__);
		goto err;
	}

	debugfs_create_u64("gtc_frequency_hz", 0440, gtc_debugfs_dir, &gtc_freq_hz);

	debugfs_create_file("current_ticks", 0440, gtc_debugfs_dir, NULL, &gtc_ticks_fops);
	debugfs_create_file("current_ns", 0440, gtc_debugfs_dir, NULL, &gtc_ns_fops);
	debugfs_create_file("current_us", 0440, gtc_debugfs_dir, NULL, &gtc_us_fops);
	debugfs_create_file("current_all", 0440, gtc_debugfs_dir, NULL, &gtc_all_fops);

	debugfs_create_u64("user_ticks", 0660, gtc_debugfs_dir, &gtc_debugfs_user_ticks);

	debugfs_create_file("user_ns", 0440, gtc_debugfs_dir, NULL, &user_ticks_to_ns_fops);
	debugfs_create_file("user_us", 0440, gtc_debugfs_dir, NULL, &user_ticks_to_us_fops);
	debugfs_create_file("user_all", 0440, gtc_debugfs_dir, NULL, &user_ticks_to_all_fops);

	pr_debug("GTC debugfs interface created.\n");

	return 0;
err:
	return ret;
}

static void __exit gtc_exit(void)
{
	debugfs_remove_recursive(gtc_debugfs_dir);
}

module_init(gtc_init);
module_exit(gtc_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google global time counter driver");
MODULE_LICENSE("GPL");
