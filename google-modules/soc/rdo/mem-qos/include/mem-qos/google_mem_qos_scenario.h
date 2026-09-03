/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC
 */

#ifndef __MEM_QOS_GOOGLE_MEM_QOS_SCENARIO_H
#define __MEM_QOS_GOOGLE_MEM_QOS_SCENARIO_H

#include <dt-bindings/mem-qos/google,mem-qos.h>
#include <linux/types.h>

#if IS_ENABLED(CONFIG_GOOGLE_MEM_QOS)


/**
 * google_mem_qos_scenario_vote() - vote a scenario
 * @scenario: the scenario
 *
 * Vote a scenario to mem_qos framework, the mem_qos framework updates active scenario and
 * applies correlated QoS settings (qos_box, ...etc.).
 *
 * The mem_qos framework will pick the highest priority scenario that has at least one vote
 * as active scenario.
 *
 * Return: 0 on successful, -EINVAL for an invalid scenario parameter, -EFAULT when the
 * mem_qos framework is not able to update certain QoS settings.
 */
int google_mem_qos_scenario_vote(u32 scenario);

/**
 * google_mem_qos_scenario_unvote() - unvote a scenario
 * @scenario: the scenario
 *
 * Unvote a scenario to mem_qos framework, the mem_qos framework updates active scenario and
 * applies correlated QoS settings (qos_box, ...etc.).
 *
 * The mem_qos framework will pick the highest priority scenario that has at least one vote
 * as active scenario (the same as google_mem_qos_scenario_vote()).
 *
 * Return: 0 on successful, -EINVAL for an invalid scenario parameter, -EFAULT when the
 * mem_qos framework is not able to update certain QoS settings.
 */
int google_mem_qos_scenario_unvote(u32 scenario);

#else

static inline int google_mem_qos_scenario_vote(u32 scenario)
{
	return 0;
}

static inline int google_mem_qos_scenario_unvote(u32 scenario)
{
	return 0;
}

#endif

#endif /* __MEM_QOS_GOOGLE_MEM_QOS_SCENARIO_H */
