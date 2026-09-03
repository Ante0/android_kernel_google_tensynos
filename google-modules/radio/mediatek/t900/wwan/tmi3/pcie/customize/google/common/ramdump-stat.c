// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/sched.h>
#include <linux/spinlock.h>
#include <soc/google/google-cdd.h>

#include "feature-control.h"
#include "radio-utils.h"
#include "ramdump-stat.h"

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_CDD)

static DEFINE_SPINLOCK(google_cdd_data_lock);
static uint32_t google_cdd_modem_data;

void update_google_cdd_modem_stat(uint32_t event, uint32_t value)
{
	unsigned long flags;
	uint32_t mask, corrected_value;

	if (!get_cdd_enable_status())
		return;

	/*
	 * Bit definitions for google_cdd_modem_data are described below.
	 * Refer to go/pixel-modem-tmi3-ramdump-codes for more details.
	 *
	 *     google_cdd_modem_data bitfield layout
	 * |----------|-----------------------------------|
	 * |  Bit(s)  |           Description             |
	 * |----------|-----------------------------------|
	 * |   3:0    | Modem State (enum mtk_fsm_state)  |
	 * |          | Invalid: 0x0, Off: 0x1            |
	 * |          | On: 0x2, Postdump: 0x3            |
	 * |          | Download: 0x4, Bootup: 0x5        |
	 * |          | Ready: 0x6, Exception: 0x7        |
	 * |          |                                   |
	 * |    4     | PCIe Link State                   |
	 * |          | Linkdown: 0x0, Linkup: 0x1        |
	 * |          |                                   |
	 * |    5     | Voice Call Status                 |
	 * |          | Voice call off: 0x0               |
	 * |          | Voice call on: 0x1                |
	 * |          |                                   |
	 * |   31:8   | FSM Flag (enum mtk_fsm_flag)      |
	 * |----------|-----------------------------------|
	 */
	spin_lock_irqsave(&google_cdd_data_lock, flags);
	switch (event) {
	case CDD_EVENT_MODEM_FSM_STATE:
		mask = GENMASK(3, 0);
		corrected_value = (value & 0xF);
		google_cdd_modem_data = (google_cdd_modem_data & ~mask) | corrected_value;
		break;
	case CDD_EVENT_PCIE_LINK:
		mask = GENMASK(4, 4);
		corrected_value = (value & 0x1) << 4;
		google_cdd_modem_data = (google_cdd_modem_data & ~mask) | corrected_value;
		break;
	case CDD_EVENT_VOICE_CALL:
		mask = GENMASK(5, 5);
		corrected_value = (value & 0x1) << 5;
		google_cdd_modem_data = (google_cdd_modem_data & ~mask) | corrected_value;
		break;
	case CDD_EVENT_MODEM_FSM_FLAG:
		mask = GENMASK(31, 8);
		corrected_value = (value & 0xFFFFFF) << 8;
		google_cdd_modem_data = (google_cdd_modem_data & ~mask) | corrected_value;
		break;
	default:
		LOG_ERR("Invalid event: %#x (value: %#x), called by %ps\n", event, value, CALLER);
		break;
	}
	spin_unlock_irqrestore(&google_cdd_data_lock, flags);

	google_cdd_set_system_dev_stat(CDD_SYSTEM_DEVICE_MODEM, google_cdd_modem_data);
}

void modem_cdd_fsm_state_handler(struct radio_google *goog, struct mtk_fsm_param *param)
{
	/* Ignores event notifications from mtk_fsm_event_notify. */
	if (param->to == FSM_STATE_INVALID)
		return;

	update_google_cdd_modem_stat(CDD_EVENT_MODEM_FSM_STATE, param->to);
	update_google_cdd_modem_stat(CDD_EVENT_MODEM_FSM_FLAG, param->full_fsm_flags);
}

#endif /* CONFIG_GOOGLE_MODEM_CDD */
