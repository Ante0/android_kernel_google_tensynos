// SPDX-License-Identifier: GPL-2.0

#include "pixelmd_api.h"
#include "pixelmd_client.h"
#include <kunit/test.h>

static int clients_with_enabled_source(enum pixelmd_source source)
{
	return atomic_read(&pixelmd_clients.source_enable_counts[source]);
}

static void test_create_destroy(struct kunit *test)
{
	struct pixelmd_client *client;

	client = pixelmd_client_create();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, client);
	KUNIT_ASSERT_FALSE(test, list_empty(&pixelmd_clients.list));

	pixelmd_client_destroy(client);
	KUNIT_ASSERT_TRUE(test, list_empty(&pixelmd_clients.list));
}

static void test_enable_disable_source(struct kunit *test)
{
	struct pixelmd_client *client;
	const enum pixelmd_source source = PIXELMD_SOURCE_TEST_GENERATOR;

	client = pixelmd_client_create();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, client);

	// Enable source
	pixelmd_client_enable_source(client, source, true);
	KUNIT_ASSERT_TRUE(test, test_bit(source, client->enabled_sources));
	KUNIT_ASSERT_EQ(test, 1, clients_with_enabled_source(source));

	// Enable again (should be idempotent)
	pixelmd_client_enable_source(client, source, true);
	KUNIT_ASSERT_TRUE(test, test_bit(source, client->enabled_sources));
	KUNIT_ASSERT_EQ(test, 1, clients_with_enabled_source(source));

	// Disable source
	pixelmd_client_enable_source(client, source, false);
	KUNIT_ASSERT_FALSE(test, test_bit(source, client->enabled_sources));
	KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(source));

	// Disable again (should be idempotent)
	pixelmd_client_enable_source(client, source, false);
	KUNIT_ASSERT_FALSE(test, test_bit(source, client->enabled_sources));
	KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(source));

	pixelmd_client_destroy(client);
}

static void test_enable_disable_multiple_clients(struct kunit *test)
{
	struct pixelmd_client *client1, *client2;
	const enum pixelmd_source source = PIXELMD_SOURCE_TEST_GENERATOR;

	client1 = pixelmd_client_create();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, client1);

	client2 = pixelmd_client_create();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, client2);

	// Enable on client1
	pixelmd_client_enable_source(client1, source, true);
	KUNIT_ASSERT_EQ(test, 1, clients_with_enabled_source(source));

	// Enable on client2
	pixelmd_client_enable_source(client2, source, true);
	KUNIT_ASSERT_EQ(test, 2, clients_with_enabled_source(source));

	// Disable on client1
	pixelmd_client_enable_source(client1, source, false);
	KUNIT_ASSERT_EQ(test, 1, clients_with_enabled_source(source));

	// Disable on client2
	pixelmd_client_enable_source(client2, source, false);
	KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(source));

	pixelmd_client_destroy(client1);
	pixelmd_client_destroy(client2);
}

static void test_enable_disable_multiple_sources(struct kunit *test)
{
	struct pixelmd_client *client;
	const enum pixelmd_source source1 = PIXELMD_SOURCE_TEST_GENERATOR;
	const enum pixelmd_source source2 = PIXELMD_SOURCE_KSWAPD;

	client = pixelmd_client_create();
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, client);

	// Enable source1
	pixelmd_client_enable_source(client, source1, true);
	KUNIT_ASSERT_TRUE(test, test_bit(source1, client->enabled_sources));
	KUNIT_ASSERT_EQ(test, 1, clients_with_enabled_source(source1));
	KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(source2));

	// Enable source2
	pixelmd_client_enable_source(client, source2, true);
	KUNIT_ASSERT_TRUE(test, test_bit(source2, client->enabled_sources));
	KUNIT_ASSERT_EQ(test, 1, clients_with_enabled_source(source1));
	KUNIT_ASSERT_EQ(test, 1, clients_with_enabled_source(source2));

	// Disable source1
	pixelmd_client_enable_source(client, source1, false);
	KUNIT_ASSERT_FALSE(test, test_bit(source1, client->enabled_sources));
	KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(source1));
	KUNIT_ASSERT_EQ(test, 1, clients_with_enabled_source(source2));

	// Disable source2
	pixelmd_client_enable_source(client, source2, false);
	KUNIT_ASSERT_FALSE(test, test_bit(source2, client->enabled_sources));
	KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(source1));
	KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(source2));

	pixelmd_client_destroy(client);
}

static struct kunit_case pixelmd_client_test_cases[] = {
	KUNIT_CASE(test_create_destroy),
	KUNIT_CASE(test_enable_disable_source),
	KUNIT_CASE(test_enable_disable_multiple_clients),
	KUNIT_CASE(test_enable_disable_multiple_sources),
	{},
};

static int pixelmd_client_test_init(struct kunit *test)
{
	int i;

	// Make sure we start with a clean slate.
	KUNIT_ASSERT_TRUE(test, list_empty(&pixelmd_clients.list));
	for (i = 0; i < PIXELMD_NUM_SOURCES; i++)
		KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(i));

	return 0;
}

static void pixelmd_client_test_exit(struct kunit *test)
{
	int i;

	// Make sure we don't leave any clients behind.
	KUNIT_ASSERT_TRUE(test, list_empty(&pixelmd_clients.list));
	for (i = 0; i < PIXELMD_NUM_SOURCES; i++)
		KUNIT_ASSERT_EQ(test, 0, clients_with_enabled_source(i));
}

static struct kunit_suite pixelmd_client_test_suite = {
	.name = "pixelmd_client_test",
	.init = pixelmd_client_test_init,
	.exit = pixelmd_client_test_exit,
	.test_cases = pixelmd_client_test_cases,
};

kunit_test_suite(pixelmd_client_test_suite);

MODULE_LICENSE("GPL");
