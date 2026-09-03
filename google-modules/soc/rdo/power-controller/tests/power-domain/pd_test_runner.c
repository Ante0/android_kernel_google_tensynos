// SPDX-License-Identifier: GPL-2.0-only

#include "pd_control.h"
#include "pd_defs.h"

#include <stdio.h>
#include <string.h>

static struct power_domain *g_power_on_sequence[MAX_DOMAINS];
static int g_power_on_count;
static struct power_domain *g_power_off_sequence[MAX_DOMAINS];
static int g_power_off_count;

static void get_power_on_sequence(struct power_domain *curr)
{
	if (!curr)
		return;

	for (int i = 0; i < g_power_on_count; i++) {
		if (g_power_on_sequence[i] == curr)
			return;
	}

	if (g_power_on_count < MAX_DOMAINS)
		g_power_on_sequence[g_power_on_count++] = curr;

	for (int i = 0; i < curr->child_count; i++)
		get_power_on_sequence(curr->children[i]);
}

static void get_power_off_sequence(struct power_domain *curr)
{
	if (!curr)
		return;

	for (int i = 0; i < curr->child_count; i++)
		get_power_off_sequence(curr->children[i]);

	if (curr->is_always_on)
		return;

	for (int i = 0; i < g_power_off_count; i++) {
		if (g_power_off_sequence[i] == curr)
			return;
	}
	if (g_power_off_count < MAX_DOMAINS)
		g_power_off_sequence[g_power_off_count++] = curr;
}

static int verify_domain(struct power_domain *domain, enum pd_state expected_state)
{
	const char *expected_str;
	enum pd_state cur_state;
	enum pd_state actual_mbfs;

	if (!domain || expected_state < 0) {
		fprintf(stderr, "ERROR: Invalid domain pointer or state passed to %s\n", __func__);
		return 0;
	}

	expected_str = g_pd_state_strings[expected_state];
	if (!expected_str) {
		fprintf(stderr, "ERROR: No state string mapping found for state enum %d\n",
				expected_state);
		return 0;
	}

	printf("  [test case] Transitioning %s to state %s\n", domain->name, expected_str);

	if (!pd_set_debugfs_state(domain->name, expected_state)) {
		fprintf(stderr, "ERROR: set_domain_state failed for %s -> %s\n",
				domain->name, expected_str);
		return 0;
	}

	cur_state = pd_get_debugfs_state(domain->name);
	if (cur_state != expected_state) {
		const char *actual_str = (cur_state >= 0) ?
					 g_pd_state_strings[cur_state] : "read_error";
		fprintf(stderr, "ERROR: Debugfs state for %s reached '%s', expected '%s'\n",
				domain->name, actual_str, expected_str);
		return 0;
	}

	actual_mbfs = pd_get_mbfs_state(domain->mbfs_name);
	if (actual_mbfs != expected_state) {
		const char *actual_str = (actual_mbfs >= 0) ?
					 g_pd_state_strings[actual_mbfs] : "read_error";
		fprintf(stderr, "ERROR: MBFS state for %s reached '%s', expected '%s'\n",
				domain->name, actual_str, expected_str);
		return 0;
	}
	return 1;
}

static int verify_subsystem(const char *root_name)
{
	struct power_domain *root;
	struct power_domain *phase1_fail = NULL;
	struct power_domain *phase2_fail = NULL;
	struct power_domain *phase3_fail = NULL;

	if (!root_name) {
		fprintf(stderr, "ERROR: root_name passed to %s is NULL\n", __func__);
		return 0;
	}

	root = pd_find_domain(root_name);
	if (!root) {
		fprintf(stderr, "ERROR: Domain not found in /d/power_controller: %s\n", root_name);
		return 0;
	}

	g_power_on_count = 0;
	get_power_on_sequence(root);

	g_power_off_count = 0;
	get_power_off_sequence(root);

	if (g_power_on_count == 0) {
		fprintf(stderr, "ERROR: No testable ON domains resolved for root: %s\n", root_name);
		return 0;
	}

	printf("=== Phase 1: Powering ON subtree ===\n");
	for (int i = 0; i < g_power_on_count; i++) {
		if (!verify_domain(g_power_on_sequence[i], PD_STATE_ON)) {
			phase1_fail = g_power_on_sequence[i];
			printf("  [phase 1 abort] Breaking ON cycle early on failure: %s\n",
					g_power_on_sequence[i]->name);
			break;
		}
	}

	printf("=== Phase 2: Powering OFF subtree ===\n");
	for (int i = 0; i < g_power_off_count; i++) {
		if (!verify_domain(g_power_off_sequence[i], PD_STATE_OFF)) {
			phase2_fail = g_power_off_sequence[i];
			printf("  [phase 2 abort] Breaking OFF cycle early on failure: %s\n",
					g_power_off_sequence[i]->name);
			break;
		}
	}

	printf("=== Phase 3: Restoring ON subtree ===\n");
	for (int i = 0; i < g_power_on_count; i++) {
		if (!verify_domain(g_power_on_sequence[i], PD_STATE_ON)) {
			phase3_fail = g_power_on_sequence[i];
			printf("  [phase 3 abort] Breaking restore cycle early on failure: %s\n",
					g_power_on_sequence[i]->name);
			break;
		}
	}

	if (phase1_fail || phase2_fail || phase3_fail) {
		fprintf(stderr, "Subsystem verification failed across phases:\n");
		if (phase1_fail)
			fprintf(stderr, " [Phase 1 (ON): %s]\n", phase1_fail->name);
		if (phase2_fail)
			fprintf(stderr, " [Phase 2 (OFF): %s]\n", phase2_fail->name);
		if (phase3_fail)
			fprintf(stderr, " [Phase 3 (ON): %s]\n", phase3_fail->name);
		return 0;
	}
	return 1;
}

static int is_valid_sswrp_domain(const char *name)
{
	size_t len;

	if (!name)
		return 0;

	len = strlen(name);
	if (!strncmp(name, "sswrp", 5))
		return 1;
	if (len >= 3 && !strcmp(name + len - 3, "_pd"))
		return 1;

	return 0;
}

int main(int argc, char *argv[])
{
	const char *root_name;

	if (argc < 2) {
		fprintf(stderr, "Usage: pd_test_runner <domain_name>\n");
		return 1;
	}

	if (!is_valid_sswrp_domain(argv[1])) {
		fprintf(stderr, "ERROR: Invalid power domain target: %s\n", argv[1]);
		return 1;
	}
	root_name = argv[1];

	if (!pd_control_init(root_name))
		return 1;

	if (!verify_subsystem(root_name))
		return 1;

	return 0;
}
