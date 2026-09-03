/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PD_CONTROL_H
#define PD_CONTROL_H

#include <stddef.h>

#define MAX_DOMAINS 256
#define MAX_NAME 64
#define MAX_CHILDREN 32

struct power_domain {
	char name[MAX_NAME];
	char mbfs_name[MAX_NAME];
	int is_always_on;
	int child_count;
	struct power_domain *children[MAX_CHILDREN];
};

/*
 * Initializes the power domain tree for the given root domain.
 * Returns 0 on failure, 1 on success.
 */
int pd_control_init(const char *root_name);

/* Returns a pointer to the power domain with the given name, or NULL if not found. */
struct power_domain *pd_find_domain(const char *name);

/* Power domain operating states for verification transitions. */
enum pd_state {
	PD_STATE_ERROR = -1,
	PD_STATE_OFF = 0,
	PD_STATE_ON = 1,
};

/*
 * Returns the current state enum of the power domain with the given name.
 * Returns PD_STATE_ERROR if the node cannot be read or parsed.
 */
enum pd_state pd_get_debugfs_state(const char *domain_name);

/*
 * Returns the current state enum of the power domain with the given MBFS name.
 * Returns PD_STATE_ERROR if the node cannot be read or parsed.
 */
enum pd_state pd_get_mbfs_state(const char *mbfs_name);

/*
 * Sets the state of the power domain with the given name to the given state.
 * Returns 0 on failure, 1 on success.
 */
int pd_set_debugfs_state(const char *domain_name, enum pd_state expected_state);

#endif /* PD_CONTROL_H */
