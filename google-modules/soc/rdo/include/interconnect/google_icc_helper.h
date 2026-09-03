/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_ICC_HELPER_H
#define _GOOGLE_ICC_HELPER_H

#include <linux/interconnect.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <interconnect/interconnect-google.h>

struct google_icc_path;

#if IS_ENABLED(CONFIG_GOOGLE_IRM)

/*
 * get google_icc_path handle
 *   @dev: device pointer for the consumer device
 *   @name: interconnect path name
 *
 * This function calls devm_of_icc_get() to get icc_path handle, and initialize other internal
 * structures of google_icc_path
 *
 * Return: google_icc_path pointer on success or ERR_PTR() on error. NULL is returned
 * when underlying devm_of_icc_get() returns NULL, which means upstream ICC API is disabled
 * or the "interconnects" DT property is missing
 */
struct google_icc_path *google_devm_of_icc_get(struct device *dev, const char *name);

/*
 * set read/write latency/LTV with VC information
 *   latency: nanosecond (ns)
 *   ltv: microsecond (us)
 */
int google_icc_set_read_latency_gmc(struct google_icc_path *p, u32 latency, u32 ltv, u8 vc);
int google_icc_set_read_latency_gslc(struct google_icc_path *p, u32 latency, u32 ltv, u8 vc);
int google_icc_set_write_latency_gmc(struct google_icc_path *p, u32 latency, u32 ltv, u8 vc);
int google_icc_set_write_latency_gslc(struct google_icc_path *p, u32 latency, u32 ltv, u8 vc);

/*
 * set read/write avg_bw/peak_bw/rt_bw with VC information
 *   avg_bw: MBps
 *   peak_bw: MBps
 *   rt_bw: MBps
 */
int google_icc_set_read_bw_gmc(struct google_icc_path *path, u32 avg_bw,
			       u32 peak_bw, u32 rt_bw, u8 vc);
int google_icc_set_read_bw_gslc(struct google_icc_path *path, u32 avg_bw,
				u32 peak_bw, u32 rt_bw, u8 vc);
int google_icc_set_write_bw_gmc(struct google_icc_path *path, u32 avg_bw,
				u32 peak_bw, u32 rt_bw, u8 vc);
int google_icc_set_write_bw_gslc(struct google_icc_path *path, u32 avg_bw,
				 u32 peak_bw, u32 rt_bw, u8 vc);

int google_icc_update_constraint_async(struct google_icc_path *p);
int google_icc_update_constraint(struct google_icc_path *p);

#else

static struct google_icc_path * __maybe_unused google_devm_of_icc_get(struct device *dev,
								      const char *name)
{
	return NULL;
}

static int __maybe_unused google_icc_set_read_latency_gmc(struct google_icc_path *p, u32 latency,
							  u32 ltv, u8 vc)
{
	return 0;
}

static int __maybe_unused google_icc_set_read_latency_gslc(struct google_icc_path *p, u32 latency,
							   u32 ltv, u8 vc)
{
	return 0;
}

static int __maybe_unused google_icc_set_write_latency_gmc(struct google_icc_path *p, u32 latency,
							   u32 ltv, u8 vc)
{
	return 0;
}

static int __maybe_unused google_icc_set_write_latency_gslc(struct google_icc_path *p, u32 latency,
							    u32 ltv, u8 vc)
{
	return 0;
}

static int __maybe_unused google_icc_set_read_bw_gmc(struct google_icc_path *path, u32 avg_bw,
						     u32 peak_bw, u32 rt_bw, u8 vc)
{
	return 0;
}

static int __maybe_unused google_icc_set_read_bw_gslc(struct google_icc_path *path, u32 avg_bw,
						      u32 peak_bw, u32 rt_bw, u8 vc)
{
	return 0;
}

static int __maybe_unused google_icc_set_write_bw_gmc(struct google_icc_path *path, u32 avg_bw,
						      u32 peak_bw, u32 rt_bw, u8 vc)
{
	return 0;
}

static int __maybe_unused google_icc_set_write_bw_gslc(struct google_icc_path *path, u32 avg_bw,
						       u32 peak_bw, u32 rt_bw, u8 vc)
{
	return 0;
}

static int __maybe_unused google_icc_update_constraint_async(struct google_icc_path *p)
{
	return 0;
}

static int __maybe_unused google_icc_update_constraint(struct google_icc_path *p)
{
	return 0;
}

#endif /* CONFIG_GOOGLE_IRM */

#endif /* _GOOGLE_ICC_HELPER_H */
