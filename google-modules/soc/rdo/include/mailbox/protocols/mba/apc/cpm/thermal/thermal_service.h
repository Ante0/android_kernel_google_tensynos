/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Google LLC
 */
#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_APC_CPM_THERMAL_THERMAL_SERVICE_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_APC_CPM_THERMAL_THERMAL_SERVICE_H

// Contains all definitions needed to use the Thermal mailbox service

/* SERVICE_ID: APC_CPM_THERMAL_SERVICE
 * CPM -> AP throttling message
 * Word 0: Queue mode hearder
 * Word 1: Thermal zone id
 * Word 2: Throttling level
 * Word 3: Reserved
 *
 * CMD: THERMAL_SERVICE_COMMAND_EMUL
 * SUB-CMD: THERMAL_SERVICE_EMUL_COMMAND_SET
 * Queue Mode:
 * Word 0: Queue mode header
 * Word 1: cmd: THERMAL_SERVICE_COMMAND_EMUL
 *         subcmd: THERMAL_SERVICE_EMUL_COMMAND_SET
 *         sswrp id: which sswrp to modify
 *  bit [ 31 - 24 | 23 - 16 | 15 - 0   ]
 *      | cmd     | subcmd  | sswrp ID |
 * Word 2: probe id: which sensor on the sswrp
 *  bit [ 31 - 0   ]
 *      | probe id |
 * Word 3: temp: what temperature to set the temperature
 *  bit [ 31 - 0 ]
 *      | temp   |
 *
 * Response:
 * Word 0: Queue mode header
 * Word 1: Error code (NO_ERROR for success, else an error code)
 *  bit [ 31 - 0     ]
 *      | error code |
 * Word 2: sswrp id: what sswrp was requested
 *         probe id: what probe was requested
 *  bit [ 31 - 16  | 15 - 0   ]
 *      | sswrp id | probe id |
 * SUB-CMD: THERMAL_SERVICE_EMUL_COMMAND_SET
 *
 * SUB-CMD: THERMAL_SERVICE_EMUL_COMMAND_GET
 * Queue Mode:
 * Word 0: Queue mode header
 * Word 1: cmd: THERMAL_SERVICE_COMMAND_EMUL
 *         subcmd: THERMAL_SERVICE_EMUL_COMMAND_GET
 *         sswrp id: which sswrp to modify
 *  bit [ 31 - 24 | 23 - 16 | 15 - 0   ]
 *      | cmd     | subcmd  | sswrp ID |
 * Word 2: probe id: which sensor on the sswrp
 *  bit [ 31 - 0   ]
 *      | probe id |
 *
 * Response:
 * Word 0: Queue mode header
 * Word 1: Error code (NO_ERROR for success, else an error code)
 *  bit [ 31 - 0     ]
 *      | error code |
 * Word 2: sswrp id: what sswrp was requested
 *         probe id: what probe was requested
 *  bit [ 31 - 16  | 15 - 0   ]
 *      | sswrp id | probe id |
 * Word 3: temp: temperature of the requested probe
 *  bit [ 31 - 0   ]
 *      | temp |
 * SUB-CMD: THERMAL_SERVICE_EMUL_COMMAND_GET
 *
 * SUB-CMD: THERMAL_SERVICE_EMUL_COMMAND_SWITCH
 * Queue Mode:
 * Word 0: Queue mode header
 * Word 1: cmd: THERMAL_SERVICE_COMMAND_EMUL
 *         subcmd: THERMAL_SERVICE_EMUL_COMMAND_SWITCH
 *         sswrp id: which sswrp to modify
 *  bit [ 31 - 24 | 23 - 16 | 15 - 0   ]
 *      | cmd     | subcmd  | sswrp ID |
 * Word 2: enable: 1 if emulation is enabled, 0 if not
 *  bit [ 31 - 0 ]
 *      | enable |
 *
 * Response:
 * Word 0: Queue mode header
 * Word 1: Error code (NO_ERROR for success, else an error code)
 *  bit [ 31 - 0     ]
 *      | error code |
 * Word 2: enable: 1 if emulation is enabled, 0 if not
 *  bit [ 31 - 0 ]
 *      | enable |
 * SUB-CMD: THERMAL_SERVICE_EMUL_COMMAND_SWITCH
 * CMD: THERMAL_SERVICE_COMMAND_EMUL
 * SERVICE_ID: APC_CPM_THERMAL_SERVICE
 */

// Thermal zone id
enum thermal_zone_id {
	THERMAL_ZONE_CPU_SMALL,
	THERMAL_ZONE_CPU_MID,
	THERMAL_ZONE_CPU_BIG_MID,
	THERMAL_ZONE_CPU_BIG_BIG,
	THERMAL_ZONE_GPU,
	THERMAL_ZONE_TPU,
	THERMAL_ZONE_DSP,
	THERMAL_ZONE_ISP,
	THERMAL_ZONE_MEMSS,
	THERMAL_ZONE_AOC,
	THERMAL_ZONE_NUM,
};

#endif
