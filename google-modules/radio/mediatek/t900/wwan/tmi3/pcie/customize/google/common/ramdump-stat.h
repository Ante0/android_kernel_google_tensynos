/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __RAMDUMP_STAT_H__
#define __RAMDUMP_STAT_H__

#include <linux/kernel.h>

#include "../common/radio-google.h"
#include "mtk_fsm.h"

enum google_cdd_modem_events {
	CDD_EVENT_PCIE_LINK,
	CDD_EVENT_VOICE_CALL,
	CDD_EVENT_MODEM_FSM_STATE,
	CDD_EVENT_MODEM_FSM_FLAG,
};

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_CDD)

void update_google_cdd_modem_stat(uint32_t event, uint32_t value);
void modem_cdd_fsm_state_handler(struct radio_google *goog, struct mtk_fsm_param *param);

#else

static inline void update_google_cdd_modem_stat(uint32_t event, uint32_t value)
{
}

static inline void modem_cdd_fsm_state_handler(struct radio_google *goog,
					       struct mtk_fsm_param *param)
{
}

#endif /* CONFIG_GOOGLE_MODEM_CDD */

#endif /* __RAMDUMP_STAT_H__ */
