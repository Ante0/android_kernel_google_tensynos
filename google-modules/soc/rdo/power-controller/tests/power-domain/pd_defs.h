/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PD_DEFS_H
#define PD_DEFS_H

/**
 * g_pd_blocklist - Platform-specific array of blocklisted power domain names.
 *
 * Array is null-terminated (last element is 0/NULL). Domains listed here are
 * excluded from test traversals and verification transitions.
 */
extern const char * const g_pd_blocklist[];

/**
 * g_pd_state_strings - Platform-specific string representations for power states.
 *
 * Indexed by enum pd_state (PD_STATE_OFF, PD_STATE_ON, PD_STATE_ERROR).
 */
extern const char * const g_pd_state_strings[];

#endif /* PD_DEFS_H */
