/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC
 */

#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_SSR_SSR_SERVICE_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_SSR_SSR_SERVICE_H

/* SERVICE_ID: CPM_COMMON_SSR_SERVICE
 *
 * CMD: SSR_SERVICE_CMD_PREPARE, SSR_SERVICE_CMD_TRIGGER,
 *        SSR_SERVICE_CMD_CLEANUP
 *   Queue Mode:
 *     Word 0: Queue mode header
 *     Word 1: Resource Identifier
 *      bit [ 31 - 0                       ]
 *          | enum ssr_service_resource_id |
 *     Word 2: Command
 *      bit [ 31 - 0               ]
 *          | enum ssr_service_cmd |
 *     Word 3: Arg dependent on resource ID and command
 *      bit [ 31 - 0 ]
 *          | arg    |
 *
 *   Response:
 *     Word 0: Queue mode header
 *     Word 1: Resource Identifier
 *      bit [ 31 - 0               ]
 *          | enum ssr_service_resource_id |
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
 *      bit [ 31 - 0                    ]
 *          | ssr_service_resource_id_t |
 *     Word 2: Arg dependent on resource ID and command
 *      bit [ 31 - 0 ]
 *          | arg    |
 *
 *   Response:
 *     Word 0: Header
 *      bit [ 31 - 16             | 15 - 0 ]
 *          | polling mode header | cmd    |
 *     Word 1: Resource ID
 *      bit [ 31 - 0                    ]
 *          | ssr_service_resource_id_t |
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
 *
 * SERVICE_ID: CPM_COMMON_SSR_SERVICE
 */

enum ssr_service_resource_id {
	SSR_SERVICE_RESOURCE_ID_AOSS,
	SSR_SERVICE_RESOURCE_ID_NUM,
};

enum ssr_service_cmd {
	SSR_SERVICE_CMD_PREPARE,
	SSR_SERVICE_CMD_TRIGGER,
	SSR_SERVICE_CMD_CLEANUP,
};

enum ssr_service_result {
	SSR_SERVICE_RESULT_SUCCESS = 0,
	// Indicates that the process is underway, but not finished
	SSR_SERVICE_RESULT_STARTED,
	SSR_SERVICE_RESULT_FAIL_TIMEOUT,
	SSR_SERVICE_RESULT_FAIL_RESOURCE_NOT_READY,
	SSR_SERVICE_RESULT_FAIL_INVALID_CMD,
};

#define SSR_SVC_MSG_SIZE (4)

#endif /* _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_SSR_SSR_SERVICE_H */
