// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 */

#include <kunit/test.h>
#include <kunit/visibility.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include "google_gdmc_dhub.h"

struct fake_gdmc_dhub_iface {
	struct gdmc_dhub_iface dhub_iface;
	u32 fake_mux_id;
	u32 fake_virt_en_mask;
	u32 fake_baudrates[32];
};

struct test_gdmc_dhub_priv {
	/* Test Device */
	struct gdmc_dhub test_dhub;
	/* Fake DHUB mailbox interface */
	struct fake_gdmc_dhub_iface fake_dhub_iface;
};

static inline struct fake_gdmc_dhub_iface *to_fake_dhub_iface(struct gdmc_dhub_iface *dhub_iface)
{
	return container_of(dhub_iface, struct fake_gdmc_dhub_iface, dhub_iface);
}


static int fake_dhub_mux_get(struct gdmc_dhub_iface *dhub_iface, u32 *uart_id)
{
	struct fake_gdmc_dhub_iface *fake_dhub_iface = to_fake_dhub_iface(dhub_iface);

	*uart_id = fake_dhub_iface->fake_mux_id;
	return 0;
}

static int fake_dhub_mux_set(struct gdmc_dhub_iface *dhub_iface, u32 uart_id)
{
	struct fake_gdmc_dhub_iface *fake_dhub_iface = to_fake_dhub_iface(dhub_iface);

	fake_dhub_iface->fake_mux_id = uart_id;
	return 0;
}

static int fake_dhub_baudrate_get(struct gdmc_dhub_iface *dhub_iface, u32 uart_num, u32 *baudrate)
{
	struct fake_gdmc_dhub_iface *fake_dhub_iface = to_fake_dhub_iface(dhub_iface);

	if (uart_num == GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED)
		*baudrate = fake_dhub_iface->fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_MAX];
	else
		*baudrate = fake_dhub_iface->fake_baudrates[uart_num];
	return 0;
}

static int fake_dhub_baudrate_set(struct gdmc_dhub_iface *dhub_iface, u32 uart_num, u32 baudrate)
{
	struct fake_gdmc_dhub_iface *fake_dhub_iface = to_fake_dhub_iface(dhub_iface);
	u64 total_baudrate_sum = 0;
	int i;

	if (uart_num == GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED) {
		for (i = 0; i < GDMC_MBA_MBU_DHUB_UART_ID_MAX; i++)
			total_baudrate_sum += fake_dhub_iface->fake_baudrates[i];

		if (total_baudrate_sum > (baudrate / 2))
			return -EINVAL;

		fake_dhub_iface->fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_MAX] = baudrate;
	} else {
		for (i = 0; i < GDMC_MBA_MBU_DHUB_UART_ID_MAX; i++) {
			if (i == uart_num)
				total_baudrate_sum += baudrate;
			else
				total_baudrate_sum += fake_dhub_iface->fake_baudrates[i];
		}

		if (total_baudrate_sum >
			 (fake_dhub_iface->fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_MAX] / 2))
			return -EINVAL;

		fake_dhub_iface->fake_baudrates[uart_num] = baudrate;
	}

	return 0;
}

static int fake_dhub_virt_en_get(struct gdmc_dhub_iface *dhub_iface, u32 *mask)
{
	struct fake_gdmc_dhub_iface *fake_dhub_iface = to_fake_dhub_iface(dhub_iface);

	*mask = fake_dhub_iface->fake_virt_en_mask;
	return 0;
}

static int fake_dhub_virt_en_set(struct gdmc_dhub_iface *dhub_iface, u32 mask)
{
	struct fake_gdmc_dhub_iface *fake_dhub_iface = to_fake_dhub_iface(dhub_iface);

	fake_dhub_iface->fake_virt_en_mask = mask;
	return 0;
}

static void test_uart_mux_get_gen1(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv = test->priv;
	const char *name;

	/* Get UART MUX when selection is APC UART ID */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_LGA_DHUB_UART_ID_APC;
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_FALSE(test, IS_ERR(name));
	KUNIT_EXPECT_STREQ(test, name, "apc");

	/* Get UART MUX when selection is no uart id */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_DHUB_UART_MUX_ID_NONE;
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_FALSE(test, IS_ERR(name));
	KUNIT_EXPECT_STREQ(test, name, "none");

	/* Get UART MUX when selection is DHUB */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED;
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_FALSE(test, IS_ERR(name));
	KUNIT_EXPECT_STREQ(test, name, "virt");

	/* Get UART MUX when selection is last UART ID which is GDMC */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_LGA_DHUB_UART_ID_GDMC;
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_FALSE(test, IS_ERR(name));
	KUNIT_EXPECT_STREQ(test, name, "gdmc");

	/* Get UART MUX when selection uart ID is out of bounds */
	priv->fake_dhub_iface.fake_mux_id = 9999; /* Invalid ID */
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_TRUE(test, IS_ERR(name));
	KUNIT_EXPECT_EQ(test, PTR_ERR(name), -EINVAL);

	/* Get UART MUX when selection uart ID is plausible but invalid */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_LGA_DHUB_UART_ID_MAX; /* Invalid ID */
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_TRUE(test, IS_ERR(name));
	KUNIT_EXPECT_EQ(test, PTR_ERR(name), -EINVAL);
}

static void test_uart_mux_get_gen2(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv = test->priv;
	const char *name;

	/* Get UART MUX when selection is APC UART ID */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_MBU_DHUB_UART_ID_APC;
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_FALSE(test, IS_ERR(name));
	KUNIT_EXPECT_STREQ(test, name, "apc");

	/* Get UART MUX when selection is no uart id */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_DHUB_UART_MUX_ID_NONE;
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_FALSE(test, IS_ERR(name));
	KUNIT_EXPECT_STREQ(test, name, "none");

	/* Get UART MUX when selection is DHUB */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED;
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_FALSE(test, IS_ERR(name));
	KUNIT_EXPECT_STREQ(test, name, "virt");

	/* Get UART MUX when selection is last UART ID which is PCIE ADPA1 */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_MBU_DHUB_UART_ID_PCIE_DPA1;
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_FALSE(test, IS_ERR(name));
	KUNIT_EXPECT_STREQ(test, name, "pcie_dpa1");

	/* Get UART MUX when selection uart ID is out of bounds */
	priv->fake_dhub_iface.fake_mux_id = 9999; /* Invalid ID */
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_TRUE(test, IS_ERR(name));
	KUNIT_EXPECT_EQ(test, PTR_ERR(name), -EINVAL);

	/* Get UART MUX when selection uart ID is plausible but invalid */
	priv->fake_dhub_iface.fake_mux_id = GDMC_MBA_MBU_DHUB_UART_ID_MAX; /* Invalid ID */
	name = mux_get(&priv->test_dhub);
	KUNIT_ASSERT_TRUE(test, IS_ERR(name));
	KUNIT_EXPECT_EQ(test, PTR_ERR(name), -EINVAL);
}

static void test_uart_mux_set_gen1(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv = test->priv;
	int ret;

	/* Set Uart Mux selection to APC UART. */
	ret = mux_set(&priv->test_dhub, "apc");
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_mux_id, GDMC_MBA_LGA_DHUB_UART_ID_APC);

	/* Set Uart Mux selection to None. */
	ret = mux_set(&priv->test_dhub, "none");
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_mux_id, GDMC_MBA_DHUB_UART_MUX_ID_NONE);

	/* Set Uart Mux selection to DHUB. */
	ret = mux_set(&priv->test_dhub, "virt");
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_mux_id,
			 GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED);

	/* Set Uart Mux selection to PCIE ADPA1 UART which is not in uart gen1 list */
	ret = mux_set(&priv->test_dhub, "pcie_dpa1");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Set Uart Mux selection to Valid APC UART with extra space */
	ret = mux_set(&priv->test_dhub, "apc ");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Set Uart Mux selection to misspelled APC UART */
	ret = mux_set(&priv->test_dhub, "apc_uart");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void test_uart_mux_set_gen2(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv = test->priv;
	int ret;

	/* Set Uart Mux selection to APC UART. */
	ret = mux_set(&priv->test_dhub, "apc");
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_mux_id, GDMC_MBA_MBU_DHUB_UART_ID_APC);

	/* Set Uart Mux selection to None. */
	ret = mux_set(&priv->test_dhub, "none");
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_mux_id, GDMC_MBA_DHUB_UART_MUX_ID_NONE);

	/* Set Uart Mux selection to DHUB. */
	ret = mux_set(&priv->test_dhub, "virt");
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_mux_id,
			 GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED);

	/* Set Uart Mux selection to ISPFE UART which is not in uart gen2 list */
	ret = mux_set(&priv->test_dhub, "ispfe");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Set Uart Mux selection to Valid APC UART with extra space */
	ret = mux_set(&priv->test_dhub, "apc ");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Set Uart Mux selection to misspelled APC UART */
	ret = mux_set(&priv->test_dhub, "apc_uart");
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void test_virt_en_get_gen2(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv = test->priv;
	int ret;
	bool enable;
	u32 apc_bit = BIT(GDMC_MBA_MBU_DHUB_UART_ID_APC);
	u32 gdmc_bit = BIT(GDMC_MBA_MBU_DHUB_UART_ID_GDMC);

	priv->fake_dhub_iface.fake_virt_en_mask = apc_bit;

	/* Get virtualization for APC UART. */
	ret = virt_en_get(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_APC, &enable);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, enable);

	/* Get virtualization for GDMC UART. */
	ret = virt_en_get(&priv->test_dhub, gdmc_bit, &enable);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, enable);
}

static void test_virt_en_set_gen2(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv = test->priv;
	int ret;
	u32 apc_bit = BIT(GDMC_MBA_MBU_DHUB_UART_ID_APC);
	u32 gdmc_bit = BIT(GDMC_MBA_MBU_DHUB_UART_ID_GDMC);

	priv->fake_dhub_iface.fake_virt_en_mask = 0;

	/* Enable virtualization for APC UART. */
	ret = virt_en_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_APC, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_virt_en_mask,
			 apc_bit & (~GDMC_MBA_DHUB_VIRT_MASK_EN));

	/* Enable virtualization for GDMC UART. */
	ret = virt_en_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_GDMC, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_virt_en_mask,
			 (apc_bit | gdmc_bit) & (~GDMC_MBA_DHUB_VIRT_MASK_EN));

	/* Disable virtualization for APC UART. */
	ret = virt_en_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_APC, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_virt_en_mask,
			 (gdmc_bit) & (~GDMC_MBA_DHUB_VIRT_MASK_EN));

	/* Disable virtualization for GDMC UART. */
	ret = virt_en_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_GDMC, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->fake_dhub_iface.fake_virt_en_mask, 0);
}

static void test_baudrate_get_gen2(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv = test->priv;
	u32 baudrate = 0;
	int ret;

	priv->fake_dhub_iface.fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_APC] = 115200;
	ret = baudrate_get(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_APC, &baudrate);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, baudrate, 115200);

	priv->fake_dhub_iface.fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_MAX] = 6000000;
	ret = baudrate_get(&priv->test_dhub, GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED, &baudrate);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, baudrate, 6000000);

}

static void test_baudrate_set_gen2(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv = test->priv;
	int ret;

	ret = baudrate_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_APC, 115200);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = baudrate_set(&priv->test_dhub, GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED, 6000000);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test,
		 priv->fake_dhub_iface.fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_MAX], 6000000);

	ret = baudrate_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_APC, 115200);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test,
		 priv->fake_dhub_iface.fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_APC], 115200);

	ret = baudrate_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_GDMC, 115200);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test,
		 priv->fake_dhub_iface.fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_GDMC], 115200);

	ret = baudrate_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_APC, 3000000);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = baudrate_set(&priv->test_dhub, GDMC_MBA_MBU_DHUB_UART_ID_APC, 1500000);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test,
		 priv->fake_dhub_iface.fake_baudrates[GDMC_MBA_MBU_DHUB_UART_ID_APC], 1500000);

	ret = baudrate_set(&priv->test_dhub, GDMC_MBA_DHUB_UART_MUX_ID_VIRTUALIZED, 3000000);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

}

static int gdmc_dhub_gen1_test_init(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	test->priv = priv;

	/*
	 * Add fake kunit test functions to dhub ops
	 * These will be used in sysfs handlers instead of real functions
	 */
	priv->fake_dhub_iface.dhub_iface.dhub_mux_get = fake_dhub_mux_get;
	priv->fake_dhub_iface.dhub_iface.dhub_mux_set = fake_dhub_mux_set;
	priv->fake_dhub_iface.dhub_iface.dhub_baudrate_get = fake_dhub_baudrate_get;
	priv->fake_dhub_iface.dhub_iface.dhub_baudrate_set = fake_dhub_baudrate_set;
	priv->fake_dhub_iface.dhub_iface.dhub_virt_en_get = fake_dhub_virt_en_get;
	priv->fake_dhub_iface.dhub_iface.dhub_virt_en_set = fake_dhub_virt_en_set;
	memset(priv->fake_dhub_iface.fake_baudrates, 0,
		 sizeof(priv->fake_dhub_iface.fake_baudrates));
	gdmc_dhub_init(&priv->test_dhub, &priv->fake_dhub_iface.dhub_iface, &dhub_uart_list_gen1);

	return 0;
}

static int gdmc_dhub_gen2_test_init(struct kunit *test)
{
	struct test_gdmc_dhub_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	test->priv = priv;

	/*
	 * Add fake kunit test functions to dhub ops
	 * These will be used in sysfs handlers instead of real functions
	 */
	priv->fake_dhub_iface.dhub_iface.dhub_mux_get = fake_dhub_mux_get;
	priv->fake_dhub_iface.dhub_iface.dhub_mux_set = fake_dhub_mux_set;
	priv->fake_dhub_iface.dhub_iface.dhub_baudrate_get = fake_dhub_baudrate_get;
	priv->fake_dhub_iface.dhub_iface.dhub_baudrate_set = fake_dhub_baudrate_set;
	priv->fake_dhub_iface.dhub_iface.dhub_virt_en_get = fake_dhub_virt_en_get;
	priv->fake_dhub_iface.dhub_iface.dhub_virt_en_set = fake_dhub_virt_en_set;
	memset(priv->fake_dhub_iface.fake_baudrates, 0,
		 sizeof(priv->fake_dhub_iface.fake_baudrates));
	gdmc_dhub_init(&priv->test_dhub, &priv->fake_dhub_iface.dhub_iface, &dhub_uart_list_gen2);

	return 0;
}

static struct kunit_case google_gdmc_dhub_gen1_test_cases[] = {
	KUNIT_CASE(test_uart_mux_get_gen1),
	KUNIT_CASE(test_uart_mux_set_gen1),
	{}
};

static struct kunit_case google_gdmc_dhub_gen2_test_cases[] = {
	KUNIT_CASE(test_uart_mux_get_gen2),
	KUNIT_CASE(test_uart_mux_set_gen2),
	KUNIT_CASE(test_virt_en_get_gen2),
	KUNIT_CASE(test_virt_en_set_gen2),
	KUNIT_CASE(test_baudrate_set_gen2),
	KUNIT_CASE(test_baudrate_get_gen2),
	{}
};

static struct kunit_suite google_gdmc_dhub_gen1_test_suite = {
	.name = "google-gdmc-dhub-gen1-test",
	.init = gdmc_dhub_gen1_test_init,
	.test_cases = google_gdmc_dhub_gen1_test_cases,
};

static struct kunit_suite google_gdmc_dhub_gen2_test_suite = {
	.name = "google-gdmc-dhub-gen2-test",
	.init = gdmc_dhub_gen2_test_init,
	.test_cases = google_gdmc_dhub_gen2_test_cases,
};

kunit_test_suite(google_gdmc_dhub_gen1_test_suite);
kunit_test_suite(google_gdmc_dhub_gen2_test_suite);
MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_AUTHOR("Mayank Rungta <mrungta@google.com>");
MODULE_DESCRIPTION("Google DHUB KUnit test");
MODULE_LICENSE("GPL");
