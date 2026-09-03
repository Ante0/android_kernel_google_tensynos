/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC.
 */
#ifndef _SOC_QOS_EXT_H_
#define _SOC_QOS_EXT_H_

#include <linux/types.h>
#include "../common/radio-google.h"

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)

int soc_qos_init(struct radio_google *goog);

void soc_qos_exit(struct radio_google *goog);

/* Report Normal BAT throughput for current cycle */
void soc_qos_report_normal_bat_tput(struct radio_google *goog, unsigned int tput);

/* Report Frag BAT throughput for current cycle */
void soc_qos_report_frag_bat_tput(struct radio_google *goog, unsigned int tput);

/* Report TX throughput for current cycle */
void soc_qos_report_tx_tput(struct radio_google *goog, unsigned int tput);

/*
 * Aggregate reported tputs, perform hysteresis logic, and update DDR vote.
 * This should be called once per timer tick (outside the loop).
 *
 * @shift: The traffic stats shift value (2^shift ms per update)
 */
void soc_qos_update_ddr_vote(struct radio_google *goog, unsigned int shift);

#else

static inline int soc_qos_init(struct radio_google *goog)
{
	return 0;
}

static inline void soc_qos_exit(struct radio_google *goog)
{
}

static inline void soc_qos_report_normal_bat_tput(struct radio_google *goog, unsigned int tput)
{
}

static inline void soc_qos_report_frag_bat_tput(struct radio_google *goog, unsigned int tput)
{
}

static inline void soc_qos_report_tx_tput(struct radio_google *goog, unsigned int tput)
{
}

static inline void soc_qos_update_ddr_vote(struct radio_google *goog, unsigned int shift)
{
}

#endif /* CONFIG_GOOGLE_MODEM_SOC_BW_QOS */

#endif /* _SOC_QOS_EXT_H_ */
