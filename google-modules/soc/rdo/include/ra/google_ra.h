/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GOOGLE_RA_H__
#define __GOOGLE_RA_H__

#include <linux/device.h>
#include <linux/regmap.h>

struct google_ra_dbg {
	struct dentry *base_dir;
	u32 sid;
	u32 rpid;
	u32 wpid;
};

struct google_ra_pid_cache {
	bool usage;
	u32 reg;
};

struct google_ra {
	struct device *dev;
	void __iomem *base;
	const char *full_name;
	struct regmap *sid_pid_tbl;
	u32 sid_width;
	u32 pid_width;
	struct google_ra_pid_cache *pid_cache;
	struct google_ra_dbg *dbg;
};

/**
 * get_google_ra_by_index() - look up google_ra device with the index
 * @consumer: client driver with device node having the RA phandle
 * @ra_index: the index of a RA phandle
 *
 * Adds the device link between the consumer and google_ra device found, increases the reference
 * count for the device.
 * The device link is to define the dependency of runtime pm operations of the consumer and the
 * supplier(RA) devices. So the consumer driver needs to call this function before setting the
 * runtime PM APIs(e.g., pm_runtime_get, pm_runtime_set_active).
 *
 * Return: a pointer to the google_ra driver or an ERR_PTR() encoded error code on failure.
 */
struct google_ra *get_google_ra_by_index(struct device *consumer, int ra_index);

/**
 * get_google_ra_by_name() - look up google_ra with the name
 * @consumer: client driver with device node having the RA phandle
 * @ra_name: the name of the RA
 *
 * Adds the device link between the consumer and google_ra device found, increases the reference
 * count for the device.
 * The device link is to define the dependency of runtime pm operations of the consumer and the
 * supplier(RA) devices. So the consumer driver needs to call this function before setting the
 * runtime PM APIs(e.g., pm_runtime_get, pm_runtime_set_active).
 *
 * Return: a pointer to the google_ra driver or an ERR_PTR() encoded error code on failure.
 */
struct google_ra *get_google_ra_by_name(struct device *consumer, const char *ra_name);

/**
 * google_ra_sid_set_pid() - configure the SID to PID mapping to RA
 * @ra: google_ra device to add the mapping to
 * @sid: SID of the client device
 * @rpid: ArPID value to set
 * @wpid: AwPID value to set
 *
 * Return: 0 on success, negative error code on failure.
*/
int google_ra_sid_set_pid(struct google_ra *ra, u32 sid, u32 rpid, u32 wpid);

/**
 * google_ra_sid_get_pid() - find the assigned PID value of the given SID
 * @ra: google_ra driver device to find the mapping from
 * @sid: SID used to find the PIDs
 * @rpid: place to hold the ArPID (output)
 * @wpid: place to hold the AwPID (output)
 *
 * Return: 0 on success, negative error code on failure.
*/
int google_ra_sid_get_pid(struct google_ra *ra, u32 sid, u32 *rpid, u32 *wpid);

#ifdef CONFIG_DEBUG_FS
int google_ra_create_debugfs(struct google_ra *ra_dev);
void google_ra_remove_debugfs(struct google_ra *ra_dev);
#else /* CONFIG_DEBUG_FS */
static inline int google_ra_create_debugfs(struct google_ra *ra_dev)
{
	return 0;
}

static inline void google_ra_remove_debugfs(struct google_ra *ra_dev)
{
}
#endif /* CONFIG_DEBUG_FS */

#endif /* __GOOGLE_RA_H__ */
