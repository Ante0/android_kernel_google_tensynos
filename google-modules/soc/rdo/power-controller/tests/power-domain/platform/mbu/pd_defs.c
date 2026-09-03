// SPDX-License-Identifier: GPL-2.0-only

#include "../../pd_control.h"
#include "../../pd_defs.h"

const char * const g_pd_blocklist[] = {
	"gpu_core_logic_pd", /* This pd is owned by GPU team and cannot be tested. */
	NULL
};

const char * const g_pd_state_strings[] = {
	[PD_STATE_OFF] = "0",
	[PD_STATE_ON] = "1",
	NULL
};
