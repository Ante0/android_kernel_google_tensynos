/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS PWRSEQ Interface
 *
 * Copyright (c) 2025 Google, LLC
 */

#ifndef LWIS_PWRSEQ_H_
#define LWIS_PWRSEQ_H_

#include <linux/device.h>
#include <linux/list.h>
#include <linux/pwrseq/consumer.h>

#include "lwis_commands.h"

struct lwis_pwrseq_info {
	struct pwrseq_desc *pwrseq;
	char name[LWIS_MAX_NAME_STRING_LEN];
	struct list_head node;
};

/*
 *  Allocate pwrseqs info and add to the list if the name is not found in
 *  exist list. It also register the pwrseq.
 */
int lwis_pwrseq_list_add_info(struct device *dev, struct list_head *list, const char *name);

/*
 *  Free the nodes in the list.
 */
void lwis_pwrseq_list_free(struct list_head *list);

/*
 *  Search the lwis device pwrseqs_list and return the lwis_pwrseq_info
 *  if the name is matched
 */
struct lwis_pwrseq_info *lwis_pwrseq_get_info(struct list_head *list, const char *name);

/*
 *  lwis_pwrseq_enable: Turn on/enable the pwrseq.
 *  Returns: 0 if success, -ve if error
 */
int lwis_pwrseq_enable(struct list_head *list, const char *name);

/*
 *  lwis_pwrseq_disable: Turn off/disable the pwrseq.
 *  Returns: 0 if success, -ve if error
 */
int lwis_pwrseq_disable(struct list_head *list, const char *name);

/*
 *  lwis_pwrseq_print: Debug function to print all the pwrseqs in the
 *  supplied list.
 */
void lwis_pwrseq_print(struct list_head *list);

#endif /* LWIS_PWRSEQ_H_ */
