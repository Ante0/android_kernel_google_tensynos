/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_THERMAL_DEBUGFS_H
#define _GOOGLE_THERMAL_DEBUGFS_H

#include <linux/debugfs.h>

#include "google_thermal.h"

#define THERMAL_DEBUGFS_BUF_SIZE 128

#define DEFINE_THERMAL_PID_PARAM_DEBUGFS_ATTRIBUTE(__name, __param)			\
static int __name##_set(void *data, u64 val)						\
{											\
	struct thermal_data *thermal_data = data;					\
	struct google_cpm_thermal *cpm_thermal = thermal_data->cpm_thermal;		\
	thermal_data->gov_param.pid_param.__name = val;				\
	return cpm_thermal->ops->set_param(cpm_thermal, thermal_data->id,		\
				    __param,						\
				    thermal_data->gov_param.pid_param.__name);	\
}											\
static int __name##_get(void *data, u64 *val)						\
{											\
	struct thermal_data *thermal_data = data;					\
	*val = thermal_data->gov_param.pid_param.__name;				\
	return 0;									\
}											\
DEFINE_DEBUGFS_ATTRIBUTE(__name, __name##_get, __name##_set, "%lld\n")

#define DEFINE_THERMAL_GRADUAL_PARAM_DEBUGFS_ATTRIBUTE(__name, __param)			\
static int __name##_set(void *data, u64 val)						\
{											\
	struct thermal_data *thermal_data = data;					\
	struct google_cpm_thermal *cpm_thermal = thermal_data->cpm_thermal;		\
	thermal_data->gov_param.gradual_param.__name = val;				\
	return cpm_thermal->ops->set_param(cpm_thermal, thermal_data->id,		\
				    __param,						\
				    thermal_data->gov_param.gradual_param.__name);	\
}											\
static int __name##_get(void *data, u64 *val)						\
{											\
	struct thermal_data *thermal_data = data;					\
	*val = thermal_data->gov_param.gradual_param.__name;				\
	return 0;									\
}											\
DEFINE_DEBUGFS_ATTRIBUTE(__name, __name##_get, __name##_set, "%lld\n")

void thermal_init_debugfs(struct thermal_data *data);
void thermal_remove_debugfs(struct thermal_data *data);

#endif /* _GOOGLE_THERMAL_DEBUGFS_H */
