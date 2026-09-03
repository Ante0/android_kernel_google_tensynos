// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include "modem_metrics.h"

#include "metrics_collection.h"
#include "mtk_dev.h"
#include "mtk_fsm.h"
#include "radio-utils.h"

static bool is_device_ready = false;

void modem_metrics_fsm_state_handler(struct radio_google *goog, struct mtk_fsm_param *param)
{
	switch (param->to) {
	case FSM_STATE_ON:
		is_device_ready = false;
		break;

	case FSM_STATE_POSTDUMP:
		mcf_notify_modem_boot_end(MODEM_BOOT_TYPE_MASK_DUMP);
		break;

	case FSM_STATE_READY:
		if (is_device_ready)
			break;

		is_device_ready = true;
		mcf_notify_modem_boot_end(MODEM_BOOT_TYPE_MASK_NORMAL |
					  MODEM_BOOT_TYPE_MASK_WARM_RESET);
		break;

	default:
		break;
	}
}

int modem_metrics_init(struct radio_google *goog)
{
	(void)goog;
	return 0;
}

void modem_metrics_exit(struct radio_google *goog)
{
	(void)goog;
}
