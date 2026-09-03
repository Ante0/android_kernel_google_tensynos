/* SPDX-License-Identifier: GPL-2.0 only */
/*
 * Copyright (C) 2024 Google LLC
 */
#ifndef _GOOGLE_CPM_MIPM_SERVICE_H
#define _GOOGLE_CPM_MIPM_SERVICE_H

// TODO(b/322127453): Replace the file with service_ids.h in mba protocol path.

/*
 * MIPM_SET_TUNABLE:
 *   Word 0: Header
 *   Word 1: MIPM_SET_TUNABLE
 *   Word 2: bit [ 31 - 24 |       23 - 16       |          15 - 8         |    7 - 0   ]
 *               [ Unused  | PF Index (optional) | Device Index (optional) | Tunable ID ]
 *   Word 3: Tunable value (for MIPM_SET_TUNABLE)
 *
 *   The AP sends this message to the CPM to update the value of the specified
 *     tunable MIPM variable
 *
 *   Tunable ID: Required, see list of MIPM_TUNABLE_x values below
 *   Device Index: Some tunables require an additional parameter to specify
 *     which device to apply the tunable to. I.e. MIPM_TUNABLE_FAB_MIN_CLAMP
 *     requires an index to specify the appropriate fabric
 *   Fabric index order is defined in dev/mipm/<soc>/mipm_types.h: mipm_fabric_id_t
 *   IRM index order is defined in dev/irm/irm.h: irm_client_id_t
 *   DVFSMon index order is defined in dev/qos/qos.h: dvfsmon_id_t
 *
 *   PF Index: Some tunables require an additional parameter to specify
 *     which pf level to apply the tunable to. I.e. UTIL_MAX_RANGE
 *     needs both a device index and a pf level
 *
 * Response:
 *   Word 0: Header
 *   Word 1: MIPM_SET_TUNABLE
 *   Word 2: Error Code
 *   Word 3: Unused
 *
 *   The response packet returns an error code in Word 2:
 *     0: No error
 *     8: ERR_INVALID ARGS - tunable ID, device index, or pf index is invalid
 *
 */
#define MIPM_SET_TUNABLE 1

/*
 * MIPM_GET_TUNABLE:
 *   Word 0: Header
 *   Word 1: MIPM_GET_TUNABLE
 *   Word 2: bit [ 31 - 24 |       23 - 16       |          15 - 8         |    7 - 0   ]
 *               [ Unused  | PF Index (optional) | Device Index (optional) | Tunable ID ]
 *   Word 3: Unused
 *
 *   The AP sends this message to the CPM to read the value of the specified
 *     tunable MIPM variable
 *
 *   Tunable ID: Required, see list of MIPM_TUNABLE_x values below
 *   Device Index: Some tunables require an additional parameter to specify
 *     which device to apply the tunable to. I.e. MIPM_TUNABLE_FAB_MIN_CLAMP
 *     requires an index to specify the appropriate fabric
 *   Fabric index order is defined in dev/mipm/<soc>/mipm_types.h: mipm_fabric_id_t
 *   IRM index order is defined in dev/irm/irm.h: irm_client_id_t
 *   DVFSMon index order is defined in dev/qos/qos.h: dvfsmon_id_t
 *
 *   PF Index: Some tunables require an additional parameter to specify
 *     which pf level to apply the tunable to. I.e. UTIL_MAX_RANGE
 *     needs both a device index and a pf level
 *
 *
 * Response:
 *   Word 0: Header
 *   Word 1: MIPM_GET_TUNABLE
 *   Word 2: Error Code
 *   Word 3: Value
 *
 *   The response packet returns an error code in Word 2:
 *     0: No error
 *     8: ERR_INVALID ARGS - tunable ID, device index, or pf index is invalid
 *
 *   Word 3 is the value of the tunable, if no error
 *
 *
 */
#define MIPM_GET_TUNABLE 2

#define MIPM_TUNABLE_ID_SHIFT (0)
#define MIPM_TUNABLE_ID_MASK (0xFF)
#define MIPM_TUNABLE_DEV_IDX_SHIFT (8)
#define MIPM_TUNABLE_DEV_IDX_MASK (0xFF)
#define MIPM_TUNABLE_PF_IDX_SHIFT (16)
#define MIPM_TUNABLE_PF_IDX_MASK (0xFF)

// Tunable IDs:
#define MIPM_TUNABLE_DVFSMON_SAMPLE_WINDOW_SIZE_USEC 0
#define MIPM_TUNABLE_DVFSMON_BLACKOUT_TIME_US 1
#define MIPM_TUNABLE_FAB_UTIL_FEEDFORWARD 2
#define MIPM_TUNABLE_UTIL_MAX_RANGE 3
#define MIPM_TUNABLE_UTIL_MIN_RANGE 4
#define MIPM_TUNABLE_LOAD_MAX_RANGE 5
#define MIPM_TUNABLE_LOAD_MIN_RANGE 6
#define MIPM_TUNABLE_LAT_MAX_RANGE 7
#define MIPM_TUNABLE_LAT_MIN_RANGE 8
#define MIPM_TUNABLE_SLC_LAT_MAX_RANGE 9
#define MIPM_TUNABLE_SLC_LAT_MIN_RANGE 10
#define MIPM_TUNABLE_GMC_LAT_MAX_RANGE 11
#define MIPM_TUNABLE_GMC_LAT_MIN_RANGE 12
#define MIPM_TUNABLE_HIT_MISS_MAX_RATIO 13
#define MIPM_TUNABLE_HIT_MISS_MIN_RATIO 14
#define MIPM_TUNABLE_GMC_UTIL_FEEDFORWARD_PEAK 15
#define MIPM_TUNABLE_GMC_UTIL_FEEDFORWARD_NONPEAK 16
#define MIPM_TUNABLE_DDR_AVAIL_MARGIN 17
#define MIPM_TUNABLE_HRT_MARGIN 18
#define MIPM_TUNABLE_MIPM_SERVICE_ENABLED 19
#define MIPM_TUNABLE_MIPM_FEEDBACK_ENABLED 20
#define MIPM_TUNABLE_IRM_CLIENT_ENABLED 21
#define MIPM_TUNABLE_DVFSMON_ENABLED 22
#define MIPM_TUNABLE_FAB_MIN_CLAMP 23
#define MIPM_TUNABLE_FAB_MAX_CLAMP 24
#define MIPM_TUNABLE_SLC_MIN_CLAMP 25
#define MIPM_TUNABLE_SLC_MAX_CLAMP 26
#define MIPM_TUNABLE_GMC_MIN_CLAMP 27
#define MIPM_TUNABLE_GMC_MAX_CLAMP 28

#endif // _GOOGLE_CPM_MIPM_SERVICE_H
