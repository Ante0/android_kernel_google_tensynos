/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS Regulator Interface
 *
 * Copyright (c) 2018 Google, LLC
 */

#ifndef LWIS_REGULATOR_H_
#define LWIS_REGULATOR_H_

#include <linux/device.h>
#include <linux/list.h>
#include <linux/regulator/consumer.h>
#include "lwis_commands.h"

struct lwis_regulator_info {
	struct regulator *reg;
	char name[LWIS_MAX_NAME_STRING_LEN];
	struct list_head node;
};

/*
 *  Allocate regulators info and add to the list if the name is not found in
 *  exist list. It also register the regulator.
 */
int lwis_regulator_list_add_info(struct device *dev, struct list_head *list, const char *name);

/**
 * @brief Adds regulator information to a list based on a name that includes a mode prefix.
 *
 * This function parses an input name string that is expected to be prefixed
 * with a regulator mode (e.g., "fast-", "normal-", "idle-", "standby-").
 * It strips this prefix to extract the actual regulator name and then calls
 * lwis_regulator_list_add_info() to add the regulator to the provided list.
 *
 * This is typically used when the regulator name in the device tree or
 * configuration also specifies an initial mode to be set.
 *
 * @param dev Pointer to the device structure associated with this regulator.
 *            This is used by devm_regulator_get().
 * @param list Pointer to the head of the list (struct list_head) where the
 *             lwis_regulator_info structure will be added.
 * @param name The name of the regulator, prefixed with its intended mode
 *             (e.g., "fast-vdd_core", "normal-vdd_io").
 *             The supported prefixes are:
 *             - "fast-"
 *             - "normal-"
 *             - "idle-"
 *             - "standby-"
 *
 * @return 0 on success.
 * @return -EINVAL if the mode prefix in 'name' is invalid or not recognized.
 * @return -ENOMEM if memory allocation for lwis_regulator_info fails.
 * @return Other negative error codes from devm_regulator_get() if the
 *         underlying regulator cannot be found or acquired.
 */
int lwis_regulator_list_add_info_by_set_mode(struct device *dev, struct list_head *list,
					     const char *name);

/*
 *  Free the nodes in the list.
 */
void lwis_regulator_list_free(struct list_head *list);

/*
 *  Search the lwis device regulators_list and return the lwis_regulator_info
 *  if the name is matched
 */
struct lwis_regulator_info *lwis_regulator_get_info(struct list_head *list, const char *name);

/*
 *  lwis_regulator_put: Unregister the regulator.
 *  Returns: 0 if success, -ve if error
 */
int lwis_regulator_put(struct list_head *list, char *name);

/*
 *  lwis_regulator_put_all: Unregister the all the regulators in the list
 *  Returns: 0 if success, -ve if error
 */
int lwis_regulator_put_all(struct list_head *list);

/*
 *  lwis_regulator_enable: Turn on/enable the regulator.
 *  Returns: 0 if success, -ve if error
 */
int lwis_regulator_enable(struct list_head *list, char *name);

/*
 *  lwis_regulator_disable: Turn off/disable the regulator.
 *  Returns: 0 if success, -ve if error
 */
int lwis_regulator_disable(struct list_head *list, char *name);

/*
 *  lwis_regulator_set_mode: Set the regulator operation mode.
 *  Returns: 0 if success, -ve if error
 */
int lwis_regulator_set_mode(struct list_head *list, char *name);

/*
 *  lwis_regulator_print: Debug function to print all the regulators in the
 *  supplied list.
 */
void lwis_regulator_print(struct list_head *list);

#endif /* LWIS_REGULATOR_H_ */
