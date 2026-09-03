/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _THERMAL_TMU_DEBUGFS_HELPER_H
#define _THERMAL_TMU_DEBUGFS_HELPER_H

#include <linux/debugfs.h>

#include "thermal_tmu_helper.h"

#define TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE (256)

#if IS_ENABLED(CONFIG_DEBUG_FS)
int thermal_tmu_debugfs_gradual_irq_gain_set(void *data, u64 val);
int thermal_tmu_debugfs_gradual_irq_gain_get(void *data, u64 *val);
int thermal_tmu_debugfs_gradual_timer_gain_set(void *data, u64 val);
int thermal_tmu_debugfs_gradual_timer_gain_get(void *data, u64 *val);

int thermal_tmu_debugfs_pi_k_po_set(void *data, u64 val);
int thermal_tmu_debugfs_pi_k_po_get(void *data, u64 *val);
int thermal_tmu_debugfs_pi_k_pu_set(void *data, u64 val);
int thermal_tmu_debugfs_pi_k_pu_get(void *data, u64 *val);
int thermal_tmu_debugfs_pi_k_i_set(void *data, u64 val);
int thermal_tmu_debugfs_pi_k_i_get(void *data, u64 *val);

int thermal_tmu_debugfs_early_throttle_k_p_set(void *data, u64 val);
int thermal_tmu_debugfs_early_throttle_k_p_get(void *data, u64 *val);

int thermal_tmu_debugfs_gradual_select_set(void *data, u64 val);
int thermal_tmu_debugfs_gradual_select_get(void *data, u64 *val);
int thermal_tmu_debugfs_pi_select_set(void *data, u64 val);
int thermal_tmu_debugfs_pi_select_get(void *data, u64 *val);
int thermal_tmu_debugfs_temp_lut_select_set(void *data, u64 val);
int thermal_tmu_debugfs_temp_lut_select_get(void *data, u64 *val);
int thermal_tmu_debugfs_hardlimit_via_pid_select_set(void *data, u64 val);
int thermal_tmu_debugfs_hardlimit_via_pid_select_get(void *data, u64 *val);
int thermal_tmu_debugfs_early_throttle_select_set(void *data, u64 val);
int thermal_tmu_debugfs_early_throttle_select_get(void *data, u64 *val);

int thermal_tmu_debugfs_polling_delay_ms_set(void *data, u64 val);
int thermal_tmu_debugfs_polling_delay_ms_get(void *data, u64 *val);

int thermal_tmu_debugfs_init(struct device *dev);
int thermal_tmu_debugfs_sensor_init(struct google_sensor_data *data);
void thermal_tmu_debugfs_cleanup(void);

#if IS_ENABLED(CONFIG_KUNIT)
ssize_t __thermal_tmu_debugfs_temp_state_table_set(
		struct google_sensor_data *sensor, char *write_buf, size_t count);
int __thermal_tmu_debugfs_temp_state_table_get(
		struct google_sensor_data *sensor, char *read_buf, size_t read_buf_size);
#endif /* CONFIG_KUNIT */
#else
static inline int thermal_tmu_debugfs_gradual_irq_gain_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_gradual_irq_gain_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_gradual_timer_gain_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_gradual_timer_gain_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_pi_k_po_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_pi_k_po_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_pi_k_pu_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_pi_k_pu_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_pi_k_i_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_pi_k_i_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_early_throttle_k_p_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_early_throttle_k_p_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_gradual_select_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_gradual_select_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_pi_select_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_pi_select_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_temp_lut_select_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_temp_lut_select_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_hardlimit_via_pid_select_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_hardlimit_via_pid_select_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_early_throttle_select_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_early_throttle_select_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_polling_delay_ms_set(void *data, u64 val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_polling_delay_ms_get(void *data, u64 *val)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_init(void)
{
	return -EOPNOTSUPP;
}
static inline int thermal_tmu_debugfs_sensor_init(struct google_sensor_data *data)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_DEBUG_FS */
#endif /* _THERMAL_TMU_DEBUGFS_HELPER_H */
