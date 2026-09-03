/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC
 */

#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_SSR_AOSS_SSR_SERVICE_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_SSR_AOSS_SSR_SERVICE_H

#include "ssr_service.h"

/* SERVICE_ID: CPM_COMMON_SSR_SERVICE
 *
 * CMD: SSR_SERVICE_CMD_PREPARE, SSR_SERVICE_CMD_TRIGGER,
 *        SSR_SERVICE_CMD_CLEANUP
 *   Queue Mode:
 *     Word 0: Queue mode header
 *     Word 1: Resource Identifier
 *      bit [ 31 - 0                       ]
 *          | SSR_SERVICE_RESOURCE_ID_AOSS |
 *     Word 2: Command
 *      bit [ 31 - 0               ]
 *          | enum ssr_service_cmd |
 *     Word 3: Arg dependent on command
 *      SSR_SERVICE_CMD_PREPARE: Stage of prep
 *        bit [ 31 - 0           ]
 *            | aoss_ssr_stage_t |
 *      SSR_SERVICE_CMD_TRIGGER: Type of SSR
 *        bit [ 31 - 0          ]
 *            | aoss_ssr_type_t |
 *      SSR_SERVICE_CMD_CLEANUP: Not used
 *        bit [ 31 - 0 ]
 *            | unused |
 *
 *   Response:
 *     Word 0: Queue mode header
 *     Word 1: Resource Identifier
 *      bit [ 31 - 0               ]
 *          | SSR_SERVICE_RESOURCE_ID_AOSS |
 *     Word 2: SSR_SERVICE_CMD_PREPARE
 *      bit [ 31 - 0 ]
 *          | cmd    |
 *     Word 3: Result
 *      bit [ 31 - 0                  ]
 *          | enum ssr_service_result |
 *
 *   Polling Mode:
 *     Word 0: Header and command
 *        cmd: SSR_SERVICE_CMD_PREPARE, SSR_SERVICE_CMD_TRIGGER,
 *               SSR_SERVICE_CMD_CLEANUP
 *      bit [ 31 - 16             | 15 - 0 ]
 *          | polling mode header | cmd    |
 *     Word 1: Resource ID
 *      bit [ 31 - 0                       ]
 *          | SSR_SERVICE_RESOURCE_ID_AOSS |
 *     Word 3: Arg dependent on command
 *      SSR_SERVICE_CMD_PREPARE: Stage of prep
 *        bit [ 31 - 0           ]
 *            | aoss_ssr_stage_t |
 *      SSR_SERVICE_CMD_TRIGGER: Type of SSR
 *        bit [ 31 - 0          ]
 *            | aoss_ssr_type_t |
 *      SSR_SERVICE_CMD_CLEANUP: Not used
 *        bit [ 31 - 0 ]
 *            | unused |
 *
 *   Response:
 *     Word 0: Header
 *      bit [ 31 - 16             | 15 - 0 ]
 *          | polling mode header | cmd    |
 *     Word 1: Resource ID
 *      bit [ 31 - 0                       ]
 *          | SSR_SERVICE_RESOURCE_ID_AOSS |
 *     Word 2: Arg dependent on resource ID and command
 *      bit [ 31 - 0 ]
 *          | arg    |
 *     Word 3: Result
 *      bit [ 31 - 0                  ]
 *          | enum ssr_service_result |
 *
 * CMD: SSR_SERVICE_CMD_PREPARE, SSR_SERVICE_CMD_TRIGGER,
 *        SSR_SERVICE_CMD_CLEANUP
 *
 */

/* Type of AOSS SSR to do, as signaled by the AOSS Kernel Driver */
enum aoss_ssr_type {
	AOSS_SSR_TYPE_PARTIAL,
	AOSS_SSR_TYPE_FULL,
};

enum aoss_ssr_stage {
	AOSS_SSR_STAGE_AP,
	AOSS_SSR_STAGE_GDMC,
};

#endif /* _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_SSR_AOSS_SSR_SERVICE_H */
