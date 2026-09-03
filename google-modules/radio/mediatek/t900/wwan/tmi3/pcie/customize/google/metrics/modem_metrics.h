/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef __MODEM_METRICS_H__
#define __MODEM_METRICS_H__

#include "../common/radio-google.h"
#include "mtk_fsm.h"

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
int modem_metrics_init(struct radio_google *goog);
void modem_metrics_exit(struct radio_google *goog);
void modem_metrics_fsm_state_handler(struct radio_google *goog, struct mtk_fsm_param *param);
#else
static inline int modem_metrics_init(struct radio_google *goog)
{
	return 0;
}

static inline void modem_metrics_exit(struct radio_google *goog)
{
}

static inline void modem_metrics_fsm_state_handler(struct radio_google *goog,
						   struct mtk_fsm_param *param)
{
}
#endif // CONFIG_METRICS_COLLECTION_FRAMEWORK

#endif // __MODEM_METRICS_H__
