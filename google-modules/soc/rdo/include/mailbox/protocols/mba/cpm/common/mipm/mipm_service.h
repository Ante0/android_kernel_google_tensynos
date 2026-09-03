/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Google LLC
 */
#ifndef _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_MIPM_MIPM_SERVICE_H
#define _MAILBOX_GOOGLE_PROTOCOLS_MBA_CPM_COMMON_MIPM_MIPM_SERVICE_H

/* SERVICE_ID: CPM_COMMON_MIPM_SERVICE
 *
 * Service used for interacting with MIPM
 * Queue mode:
 *
 * MBA_TRANSPORT_TYPE_MSG:
 *   Word 0: Queue mode header
 *   Word 1: MIPM_IRM_ACK_COMMAND
 *   Word 2: bit [ 31 - 25 |    24 - 0     ]
 *               [ unused  | irm client id ]
 *   Word 3: unused
 *
 *   The MIPM sends this message to the AP to signal that it has finished handling
 *     IRM votes for the relevant IRM clients
 *
 *   Word 2, bits 24-0 signal which clients have been handled. I.e., if bits 5 and 6
 *     are set, votes for IRM_APC_USB and IRM_APC_ISP_SET0 have been handled
 *     (see go/rdo-irm-client-mapping for the list of IRM clients)
 *
 *
 * SERVICE_ID: CPM_AP_COMMON_MIPM_SERVICE
 */

#define MIPM_IRM_ACK_COMMAND 0

#endif
