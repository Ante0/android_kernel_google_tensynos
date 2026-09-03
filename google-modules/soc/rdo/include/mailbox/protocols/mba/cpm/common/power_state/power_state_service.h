/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Google LLC
 */

#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_POWER_STATE_POWER_STATE_SERVICE_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_POWER_STATE_POWER_STATE_SERVICE_H

/* SERVICE_ID: CPM_COMMON_POWER_STATE_SERVICE
 *
 * CMD: SOC_PWR_CMD_STATE_REQ
 * Word 0: Queue mode header
 * Word 1: SOC_PWR_CMD_STATE_REQ
 *  bit [ 31 - 0 ]
 *      | cmd    |
 * Word 2: Power state
 *  bit [ 31 - 0 ]
 *      | state  |
 * Word 3: Res

 * Polling Mode:
 * CMD: SOC_PWR_CMD_STATE_REQ
 * Word 0: Header and data (1 to set on, 0 to set off)
 *  bit [ 31 - 16             | 15-0                  ]
 *      | polling mode header | SOC_PWR_CMD_STATE_REQ |
 * Word 1: type soc_pwr_state_service_state_t
 *  bit [ 31 - 0 ]
 *      | state  |
 * Word 2: Res
 * CMD: SOC_PWR_CMD_STATE_REQ
 *
 * CMD: SOC_PWR_CMD_TRIGGER_WAKE
 * Word 0: Header and data (1 to set on, 0 to set off)
 *  bit [ 31 - 16             | 15-0                     ]
 *      | polling mode header | SOC_PWR_CMD_TRIGGER_WAKE |
 * Word 1: Res
 * CMD: SOC_PWR_CMD_TRIGGER_WAKE

 * SERVICE_ID: CPM_COMMON_POWER_STATE_SERVICE
 */

enum soc_pwr_state_service_state {
	SOC_PWR_STATE_SERVICE_MISSION,
	SOC_PWR_STATE_SERVICE_DORMANT,
	SOC_PWR_STATE_SERVICE_AMBIENT,
	SOC_PWR_STATE_SERVICE_VOLTE_WIFI,
	SOC_PWR_STATE_SERVICE_TPU_ON,
	SOC_PWR_STATE_SERVICE_MC_ON,
	SOC_PWR_STATE_SERVICE_SLC_ON,
	SOC_PWR_NUM_SERVICE_STATES,
};

#define POWER_STATE_SVC_MSG_SIZE (4)
#define SOC_PWR_CMD_STATE_REQ (1)
#define SOC_PWR_CMD_TRIGGER_WAKE (2)

#endif
