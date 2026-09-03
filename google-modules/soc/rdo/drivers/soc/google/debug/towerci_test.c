// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 */

#include <kunit/device.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <kunit/visibility.h>
#include <linux/interrupt.h>
#include "towerci.h"

struct fake_tower_registers {
	u32 err_gsr;
	u32 err_status;
	u64 err_addr;
	u32 err_misc;
	u32 err_en;

	/* Fields to track writes */
	u32 last_written_status;
	int status_write_count;
};

struct test_towerci_priv {
	struct towerci_dev test_towerci;
	struct fake_tower_registers hw_registers;
};

static u32 fake_towerci_reg_read(bool is_secure, phys_addr_t reg_s, void __iomem *reg_ns,
		 u32 offset)
{
	struct kunit *test = kunit_get_current_test();
	struct test_towerci_priv *priv = test->priv;

	switch (offset) {
	case MCN_ERRGSR_OFFSET:
		return priv->hw_registers.err_gsr;
	case MCN_ERR_STATUS_OFFSET:
		return priv->hw_registers.err_status;
	case MCN_ERR_ADDRL_OFFSET:
		return lower_32_bits(priv->hw_registers.err_addr);
	case MCN_ERR_ADDRH_OFFSET:
		return upper_32_bits(priv->hw_registers.err_addr);
	case MCN_ERR_MISC_OFFSET:
		return priv->hw_registers.err_misc;
	}

	kunit_info(test, "Unhandled read offset: %#x", offset);
	return 0;
}

static void fake_towerci_reg_write(bool is_secure, phys_addr_t reg_s, void __iomem *reg_ns,
		 u32 offset, u32 val)
{
	struct kunit *test = kunit_get_current_test();
	struct test_towerci_priv *priv = test->priv;

	switch (offset) {
	case MCN_ERR_STATUS_OFFSET:
		priv->hw_registers.last_written_status = val;
		priv->hw_registers.status_write_count++;
		/* Simulate clear on write */
		if (val == priv->hw_registers.err_status)
			priv->hw_registers.err_status = 0;
		break;
	case MCN_ERR_EN_OFFSET:
		priv->hw_registers.err_en = val;
		break;
	default:
		kunit_info(test, "Unhandled write offset: %#x, val: %#x", offset, val);
		break;
	}
}

static void towerci_test_irq_handler_base(struct kunit *test)
{
	struct test_towerci_priv *priv = test->priv;
	irqreturn_t ret;

	priv->hw_registers.err_gsr = BIT(0);
	priv->hw_registers.err_status = BIT(30) | BIT(29); // Valid, Uncorrected

	ret = tower_irq_handler(123, &priv->test_towerci);

	KUNIT_EXPECT_EQ(test, IRQ_HANDLED, ret);
	KUNIT_EXPECT_EQ(test, 1, priv->hw_registers.status_write_count);
	KUNIT_EXPECT_EQ(test, BIT(30) | BIT(29), priv->hw_registers.last_written_status);
}

static void towerci_test_irq_handler_no_valid_bit(struct kunit *test)
{
	struct test_towerci_priv *priv = test->priv;
	irqreturn_t ret;

	priv->hw_registers.err_gsr = BIT(0);
	priv->hw_registers.err_status = BIT(29); // Uncorrected, but not Valid

	ret = tower_irq_handler(123, &priv->test_towerci);

	KUNIT_EXPECT_EQ(test, IRQ_HANDLED, ret);
	KUNIT_EXPECT_EQ(test, 1, priv->hw_registers.status_write_count);
}

static void towerci_test_irq_handler_addr_valid(struct kunit *test)
{
	struct test_towerci_priv *priv = test->priv;
	irqreturn_t ret;

	priv->hw_registers.err_gsr = BIT(0);
	priv->hw_registers.err_status = BIT(31) | BIT(30); // Addr Valid, Valid
	priv->hw_registers.err_addr = 0x1234567890ABCDEFULL;

	ret = tower_irq_handler(123, &priv->test_towerci);
	KUNIT_EXPECT_EQ(test, IRQ_HANDLED, ret);
	KUNIT_EXPECT_EQ(test, 1, priv->hw_registers.status_write_count);
}

static void towerci_test_irq_handler_misc_valid_0(struct kunit *test)
{
	struct test_towerci_priv *priv = test->priv;
	irqreturn_t ret;

	priv->hw_registers.err_gsr = BIT(0); // Group 0
	priv->hw_registers.err_status = BIT(30) | BIT(26); // Valid, Misc Valid
	priv->hw_registers.err_misc = (1 << 17) | (0x5 << 11); // sys_opcode_select=1, sys_opcode=5

	ret = tower_irq_handler(123, &priv->test_towerci);
	KUNIT_EXPECT_EQ(test, IRQ_HANDLED, ret);
	KUNIT_EXPECT_EQ(test, 1, priv->hw_registers.status_write_count);
}

static void towerci_test_irq_handler_misc_valid_1(struct kunit *test)
{
	struct test_towerci_priv *priv = test->priv;
	irqreturn_t ret;

	priv->hw_registers.err_gsr = BIT(1); // Group 1
	priv->hw_registers.err_status = BIT(30) | BIT(26); // Valid, Misc Valid
	priv->hw_registers.err_misc = BIT(2) | BIT(0); // rd_wr_ewa=1, snoop_ewa=1

	ret = tower_irq_handler(123, &priv->test_towerci);
	KUNIT_EXPECT_EQ(test, IRQ_HANDLED, ret);
	KUNIT_EXPECT_EQ(test, 1, priv->hw_registers.status_write_count);
}

static void towerci_test_irq_handler_group_2(struct kunit *test)
{
	struct test_towerci_priv *priv = test->priv;
	irqreturn_t ret;

	priv->hw_registers.err_gsr = BIT(2); // Group 2
	priv->hw_registers.err_status = BIT(30); // Valid
	priv->hw_registers.err_misc = 0xAAAAAAAA; // Should not be read

	ret = tower_irq_handler(123, &priv->test_towerci);
	KUNIT_EXPECT_EQ(test, IRQ_HANDLED, ret);
	KUNIT_EXPECT_EQ(test, 1, priv->hw_registers.status_write_count);
}

static void towerci_test_irq_handler_spurious(struct kunit *test)
{
	struct test_towerci_priv *priv = test->priv;
	irqreturn_t ret;

	ret = tower_irq_handler(456, &priv->test_towerci); // Different IRQ number
	KUNIT_EXPECT_EQ(test, IRQ_NONE, ret);
	KUNIT_EXPECT_EQ(test, 0, priv->hw_registers.status_write_count);
}

static void towerci_test_string_getters(struct kunit *test)
{
	KUNIT_EXPECT_STREQ(test, "Uncorrected, uncontainable", get_uet_string(0));
	KUNIT_EXPECT_STREQ(test, "Unknown UET", get_uet_string(10));

	KUNIT_EXPECT_STREQ(test, "NOC", get_ierr_string(1));
	KUNIT_EXPECT_STREQ(test, "Unknown IERR", get_ierr_string(5));

	KUNIT_EXPECT_STREQ(test, "No error", get_serr_string(0));
	KUNIT_EXPECT_STREQ(test, "Illegal Address (access to unpopulated memory)",
				 get_serr_string(13));
	KUNIT_EXPECT_STREQ(test, "Unknown SERR", get_serr_string(30));

	KUNIT_EXPECT_STREQ(test, "ReadShared", get_sys_opcode_string(1, 0));
	KUNIT_EXPECT_STREQ(test, "WriteUniqueZero", get_sys_opcode_string(3, 1));
	KUNIT_EXPECT_STREQ(test, "Unknown SYS_INTF_OPCODE", get_sys_opcode_string(100, 0));
	KUNIT_EXPECT_STREQ(test, "Unknown SYS_INTF_OPCODE", get_sys_opcode_string(100, 1));
}

static int towerci_irq_test_init(struct kunit *test)
{
	struct test_towerci_priv *priv;
	struct device_driver *test_towerci_drv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);
	test->priv = priv;

	test_towerci_drv = kunit_driver_create(test, "tower_ci_test_device-drv");
	priv->test_towerci.dev = kunit_device_register_with_driver(test,
				 "tower_ci_test_device", test_towerci_drv);
	priv->test_towerci.tnode = kunit_kzalloc(test,
				 sizeof(*priv->test_towerci.tnode), GFP_KERNEL);
	priv->test_towerci.tnode->irq_count = 1;

	priv->test_towerci.tnode->irq = kunit_kzalloc(test,
				 sizeof(*priv->test_towerci.tnode->irq) * 1, GFP_KERNEL);
	priv->test_towerci.tnode->irq[0].irq_num = 123;
	priv->test_towerci.tnode->irq[0].is_secure = true;
	priv->test_towerci.tnode->irq[0].is_error = false;

	priv->test_towerci.res = kunit_kzalloc(test, sizeof(*priv->test_towerci.res), GFP_KERNEL);
	priv->test_towerci.res->start = 0x10000000; // Example base address

	/* Set function pointers to fake implementations */
	priv->test_towerci.reg_read = fake_towerci_reg_read;
	priv->test_towerci.reg_write = fake_towerci_reg_write;

	return 0;
}

static struct kunit_case towerci_irq_test_cases[] = {
	KUNIT_CASE(towerci_test_irq_handler_base),
	KUNIT_CASE(towerci_test_irq_handler_no_valid_bit),
	KUNIT_CASE(towerci_test_irq_handler_addr_valid),
	KUNIT_CASE(towerci_test_irq_handler_misc_valid_0),
	KUNIT_CASE(towerci_test_irq_handler_misc_valid_1),
	KUNIT_CASE(towerci_test_irq_handler_group_2),
	KUNIT_CASE(towerci_test_irq_handler_spurious),
	KUNIT_CASE(towerci_test_string_getters),
	{}
};

static struct kunit_suite towerci_irq_test_suite = {
	.name = "towerci-irq-handler",
	.init = towerci_irq_test_init,
	.test_cases = towerci_irq_test_cases,
};

kunit_test_suite(towerci_irq_test_suite);
MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_AUTHOR("Mayank Rungta <mrungta@google.com>");
MODULE_DESCRIPTION("Tower CI KUnit test");
MODULE_LICENSE("GPL");
