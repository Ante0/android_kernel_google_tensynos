// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <perf/core/apc_irm.h>

#include "apc_irm_debug.h"

#define TEST_SUBCLIENT_1 "kunit_sub_1"
#define TEST_SUBCLIENT_2 "kunit_sub_2"

#if IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)

#define ASYNC_CLIENT_NAME "gpca"
#define SYNC_CLIENT_NAME "ispSet0"

#define RD_BW_AVG_GMC_FILE_NAME "rdBwAvgGmc"
#define RD_BW_PEAK_GMC_FILE_NAME "rdBwPeakGmc"
#define RD_BW_RT_GMC_FILE_NAME "rdBwRtGmc"
#define WR_BW_AVG_GMC_FILE_NAME "wrBwAvgGmc"
#define WR_BW_PEAK_GMC_FILE_NAME "wrBwPeakGmc"
#define WR_BW_RT_GMC_FILE_NAME "wrBwRtGmc"
#define MIN_CLAMP_GMC_FILE_NAME "minClampGmc"

#endif

static char *async_client_name = ASYNC_CLIENT_NAME;
module_param(async_client_name, charp, 0444);
MODULE_PARM_DESC(async_client_name, "Name of the asynchronous IRM client to use for testing");

static char *sync_client_name = SYNC_CLIENT_NAME;
module_param(sync_client_name, charp, 0444);
MODULE_PARM_DESC(sync_client_name, "Name of the synchronous IRM client to use for testing");

/*
 * Note: These tests depend on the existence of valid IRM clients.
 * If the clients are not found (e.g. MBFS not ready or invalid name),
 * the tests will skip or fail.
 */

static void kunit_action_remove_irm_subclient(void *subclient)
{
	remove_irm_subclient((struct irm_subclient_t *)subclient);
}

static void apc_irm_test_basic_lifecycle(struct kunit *test)
{
	struct irm_client_t *client;
	struct irm_subclient_t *sc;
	int ret;

	client = get_irm_client(async_client_name);
	// It's possible the client doesn't exist.
	if (IS_ERR(client))
		kunit_skip(test, "Could not get irm client (check async_client_name param)");

	sc = kunit_kzalloc(test, sizeof(*sc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sc);

	ret = add_irm_subclient(client, sc, TEST_SUBCLIENT_1);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kunit_add_action(test, kunit_action_remove_irm_subclient, sc);

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, sc);
}

static void apc_irm_test_aggregation(struct kunit *test)
{
	struct irm_subclient_t *sc1, *sc2;
	struct irm_client_t *client;
	struct irm_vote_t vote_out;
	int ret;

	client = get_irm_client(async_client_name);
	if (IS_ERR(client))
		kunit_skip(test, "Could not get irm client");

	sc1 = kunit_kzalloc(test, sizeof(*sc1), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sc1);
	ret = add_irm_subclient(client, sc1, TEST_SUBCLIENT_1);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kunit_add_action(test, kunit_action_remove_irm_subclient, sc1);

	sc2 = kunit_kzalloc(test, sizeof(*sc2), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sc2);
	ret = add_irm_subclient(client, sc2, TEST_SUBCLIENT_2);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kunit_add_action(test, kunit_action_remove_irm_subclient, sc2);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, client);

	// Reset any previous state (if module reloaded or subclients persist)
	clear_irm_subclient_vote(sc1);
	stage_irm_subclient_vote(sc1);
	clear_irm_subclient_vote(sc2);
	stage_irm_subclient_vote(sc2);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	// Set votes
	set_irm_subclient_average_read_gmc_bandwidth(sc1, 100);
	set_irm_subclient_peak_read_gmc_bandwidth(sc1, 200);
	stage_irm_subclient_vote(sc1);

	set_irm_subclient_average_read_gmc_bandwidth(sc2, 50);
	set_irm_subclient_peak_read_gmc_bandwidth(sc2, 50);
	stage_irm_subclient_vote(sc2);

	// Publish
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	// Verify
	get_irm_client_published_vote(client, &vote_out);
	KUNIT_EXPECT_EQ(test, vote_out.avg_read_gmc_bw, 150); // 100 + 50
	KUNIT_EXPECT_EQ(test, vote_out.peak_read_gmc_bw, 250); // 200 + 50

	// Test updating one
	set_irm_subclient_average_read_gmc_bandwidth(sc1, 0);
	stage_irm_subclient_vote(sc1);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	get_irm_client_published_vote(client, &vote_out);
	KUNIT_EXPECT_EQ(test, vote_out.avg_read_gmc_bw, 50); // 0 + 50

	// Clear all
	clear_irm_subclient_vote(sc1);
	clear_irm_subclient_vote(sc2);
	stage_irm_subclient_vote(sc1);
	stage_irm_subclient_vote(sc2);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	get_irm_client_published_vote(client, &vote_out);
	KUNIT_EXPECT_EQ(test, vote_out.avg_read_gmc_bw, 0);
	KUNIT_EXPECT_EQ(test, vote_out.peak_read_gmc_bw, 0);

	// Test PF levels (min aggregation)
	// Init to default (max value 0xF)
	set_irm_subclient_pf_req_gmc(sc1, 0xF);
	set_irm_subclient_pf_req_gmc(sc2, 0xF);
	stage_irm_subclient_vote(sc1);
	stage_irm_subclient_vote(sc2);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	get_irm_client_published_vote(client, &vote_out);
	KUNIT_EXPECT_EQ(test, vote_out.pf_gmc, 0xF);

	// Set sc1 to 5, sc2 to 10. Min should be 5.
	set_irm_subclient_pf_req_gmc(sc1, 5);
	set_irm_subclient_pf_req_gmc(sc2, 10);
	stage_irm_subclient_vote(sc1);
	stage_irm_subclient_vote(sc2);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	get_irm_client_published_vote(client, &vote_out);
	KUNIT_EXPECT_EQ(test, vote_out.pf_gmc, 5);

	// Cleanup PF levels
	clear_irm_subclient_vote(sc1);
	clear_irm_subclient_vote(sc2);
	stage_irm_subclient_vote(sc1);
	stage_irm_subclient_vote(sc2);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);
}

static void apc_irm_test_register_writes(struct kunit *test)
{
	struct irm_subclient_t *sc;
	struct irm_client_t *client;
	u32 val;
	u16 expected_min_clamp;
	int ret;

	client = get_irm_client(async_client_name);
	if (IS_ERR(client))
		kunit_skip(test, "Could not get irm client");

	sc = kunit_kzalloc(test, sizeof(*sc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sc);
	ret = add_irm_subclient(client, sc, TEST_SUBCLIENT_1);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kunit_add_action(test, kunit_action_remove_irm_subclient, sc);

	// Setup a specific vote
	set_irm_subclient_average_read_gmc_bandwidth(sc, 100);
	set_irm_subclient_peak_read_gmc_bandwidth(sc, 200);
	set_irm_subclient_rt_read_gmc_bandwidth(sc, 300);
	set_irm_subclient_average_write_gmc_bandwidth(sc, 400);
	set_irm_subclient_peak_write_gmc_bandwidth(sc, 500);
	set_irm_subclient_rt_write_gmc_bandwidth(sc, 600);

	set_irm_subclient_pf_req_gmc(sc, 5);
	set_irm_subclient_pf_req_memss(sc, 16);
	set_irm_subclient_pf_req_intermediate_ancestor_fab(sc, 7);
	set_irm_subclient_pf_req_intermediate_descendant_fab(sc, 8);

	stage_irm_subclient_vote(sc);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	// Verify registers
	val = get_irm_register_value_from_mbfs(client, RD_BW_AVG_GMC_FILE_NAME);
	KUNIT_EXPECT_EQ(test, val, 100);

	val = get_irm_register_value_from_mbfs(client, RD_BW_PEAK_GMC_FILE_NAME);
	KUNIT_EXPECT_EQ(test, val, 200);

	val = get_irm_register_value_from_mbfs(client, RD_BW_RT_GMC_FILE_NAME);
	KUNIT_EXPECT_EQ(test, val, 300);

	val = get_irm_register_value_from_mbfs(client, WR_BW_AVG_GMC_FILE_NAME);
	KUNIT_EXPECT_EQ(test, val, 400);

	val = get_irm_register_value_from_mbfs(client, WR_BW_PEAK_GMC_FILE_NAME);
	KUNIT_EXPECT_EQ(test, val, 500);

	val = get_irm_register_value_from_mbfs(client, WR_BW_RT_GMC_FILE_NAME);
	KUNIT_EXPECT_EQ(test, val, 600);

	// Calculate expected min clamp register value
	expected_min_clamp = 0;
	expected_min_clamp |= min_t(u16, 5, CPM_IRM_FREQ_CLAMP_GMC_MASK)
			      << CPM_IRM_FREQ_CLAMP_GMC_SHIFT;
	expected_min_clamp |= min_t(u16, 16, CPM_IRM_FREQ_CLAMP_MEMSS_MASK)
			      << CPM_IRM_FREQ_CLAMP_MEMSS_SHIFT;
	expected_min_clamp |= min_t(u16, 7, CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_MASK)
			      << CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_SHIFT;
	expected_min_clamp |= min_t(u16, 8, CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_MASK)
			      << CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_SHIFT;
#if (IS_ENABLED(CONFIG_SOC_MBU) || IS_ENABLED(CONFIG_SOC_LGA))
	// Expect valid bit to be set since values are not default (0xF)
	expected_min_clamp |= CPM_IRM_FREQ_CLAMP_VALID_BIT;
#endif

	val = get_irm_register_value_from_mbfs(client, MIN_CLAMP_GMC_FILE_NAME);
	KUNIT_EXPECT_EQ(test, val, expected_min_clamp);

	// Cleanup
	clear_irm_subclient_vote(sc);

	stage_irm_subclient_vote(sc);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);
}

/*
 * Test that stage_irm_subclient_vote can be called from atomic context.
 * This verifies that the API uses spinlocks instead of mutexes for
 * internal state protection.
 *
 * Also verifies that publish_irm_vote can be called from atomic context
 * for asynchronous clients.
 */
static void apc_irm_test_atomic_context(struct kunit *test)
{
	struct irm_subclient_t *sc;
	struct irm_client_t *client;
	spinlock_t test_lock;
	unsigned long flags;
	int ret;

	spin_lock_init(&test_lock);

	client = get_irm_client(async_client_name);
	if (IS_ERR(client))
		kunit_skip(test, "Could not get async client");

	sc = kunit_kzalloc(test, sizeof(*sc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sc);

	ret = add_irm_subclient(client, sc, TEST_SUBCLIENT_1);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kunit_add_action(test, kunit_action_remove_irm_subclient, sc);

	if (is_irm_client_synchronous(client))
		KUNIT_FAIL(test, "Client %s is expected to be asynchronous", async_client_name);

	/* Enter atomic context */
	spin_lock_irqsave(&test_lock, flags);

	/* These should not sleep or warn */
	set_irm_subclient_average_read_gmc_bandwidth(sc, 123);
	stage_irm_subclient_vote(sc);

	/* For async clients, publish must also be atomic-safe */
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	spin_unlock_irqrestore(&test_lock, flags);

	/* Wait for 120s to allow manual verification */
	/* ssleep(120); */

	/* Clean up */
	clear_irm_subclient_vote(sc);
	stage_irm_subclient_vote(sc);
	publish_irm_vote(client);
}

/*
 * Test that a synchronous client can publish a vote and receive an ACK from FW.
 * This test will timeout (and panic, as per driver design) if the ACK is not received.
 */
static void apc_irm_test_sync_client_ack(struct kunit *test)
{
	struct irm_subclient_t *sc;
	struct irm_client_t *client;
	int ret;

	client = get_irm_client(sync_client_name);
	if (IS_ERR(client))
		kunit_skip(test, "Could not get sync client (check sync_client_name param)");

	if (!is_irm_client_synchronous(client))
		KUNIT_FAIL(test, "Client %s is expected to be synchronous", sync_client_name);

	sc = kunit_kzalloc(test, sizeof(*sc), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, sc);

	ret = add_irm_subclient(client, sc, TEST_SUBCLIENT_1);
	KUNIT_ASSERT_EQ(test, ret, 0);
	kunit_add_action(test, kunit_action_remove_irm_subclient, sc);

	// Set a vote
	set_irm_subclient_average_read_gmc_bandwidth(sc, 100);
	stage_irm_subclient_vote(sc);

	// This function waits for ACK and panics on timeout
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);

	// Clean up
	clear_irm_subclient_vote(sc);
	stage_irm_subclient_vote(sc);
	KUNIT_EXPECT_EQ(test, publish_irm_vote(client), 0);
}

static struct kunit_case apc_irm_test_cases[] = {
	KUNIT_CASE(apc_irm_test_basic_lifecycle), KUNIT_CASE(apc_irm_test_aggregation),
	KUNIT_CASE(apc_irm_test_register_writes), KUNIT_CASE(apc_irm_test_atomic_context),
	KUNIT_CASE(apc_irm_test_sync_client_ack), {}
};

static struct kunit_suite apc_irm_test_suite = {
	.name = "apc_irm_test",
	.test_cases = apc_irm_test_cases,
};

kunit_test_suite(apc_irm_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google APC IRM KUnit Test");
