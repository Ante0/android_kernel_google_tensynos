// SPDX-License-Identifier: GPL-2.0 only
/*
 * max77759_bcl_irq_tests.c Google BCL MAX77759 tests
 *
 * Copyright (c) 2025 Google LLC.
 *
 */

#include <linux/err.h>
#include <kunit/device.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <kunit/static_stub.h>

#include <bcl.h>
#include <max77759.h>
#include <ifpmic/max77759/max77759_bcl_irq.h>

struct max77759_test_priv {
	u8 mock_data;
	u8 addr;
	struct max77759_bcl_irq_data data;
	struct device dev;
};

static bool is_config_addr(uint8_t addr)
{
	return addr == MAX77759_CHG_CNFG_14 || addr == MAX77759_CHG_CNFG_15 ||
	       addr == MAX77759_CHG_CNFG_16;
}

static int get_uvlo_reg_data(int val)
{
	return (val - VD_LOWER_LIMIT) / VD_STEP;
}

/*
 * mock_max77759_external_pmic_reg_read - Mock function for reading a PMIC register.
 * @dev: The device structure.
 * @reg: The register address to read.
 * @val: Pointer to store the read value.
 *
 * This function intercepts calls to read a PMIC register and returns a
 * value controlled by the test case, simulating hardware state.
 *
 * Return: 0 on success.
 */
static int mock_max77759_external_reg_read(struct device *dev, u8 reg, u8 *val)
{
	struct kunit *test = kunit_get_current_test();
	struct max77759_test_priv *priv = test->priv;

	*val = priv->mock_data;
	priv->addr = reg;

	return 0;
}

/*
 * mock_max77759_external_pmic_reg_write - Mock function for writing to a PMIC register.
 * @dev: The device structure.
 * @reg: The register address to write.
 * @val: The value to write.
 *
 * This function intercepts calls to write to a PMIC register, allowing the
 * test to verify that interrupt clear operations are performed correctly.
 *
 * Return: 0 on success.
 */
static int mock_max77759_external_reg_write(struct device *dev, u8 reg, u8 val)
{
	struct kunit *test = kunit_get_current_test();
	struct max77759_test_priv *priv = test->priv;

	priv->addr = reg;

	if (is_config_addr(reg)) {
		priv->mock_data = val;
		return 0;
	}

	priv->mock_data &= ~val;
	return 0;
}

/*
 * test_max77759_no_irq_pending - Test case for no pending interrupts.
 * @test: The kunit test context.
 *
 * Verifies that max77759_get_irq() returns IRQ_NONE when no interrupt
 * bits are set in the mock register.
 */
static void test_max77759_no_irq_pending(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	/* Test case 1: no interrupts are pending */
	priv->mock_data = 0;
	ret = max77759_get_irq(data->dev, &idx);
	KUNIT_EXPECT_EQ(test, ret, IRQ_NONE);
}

/*
 * test_max77759_uvlo1_pending - Test case for a single pending interrupt.
 * @test: The kunit test context.
 *
 * Verifies that max77759_get_irq() correctly identifies and returns the
 * index of the UVLO1 interrupt when its corresponding bit is set.
 */
static void test_max77759_uvlo1_pending(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	/* Test case 2: UVLO1 is pending */
	priv->mock_data = MAX77759_CHG_INT2_SYS_UVLO1_I;
	ret = max77759_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, UVLO1);
}

/*
 * test_max77759_uvlo1_oilo1_pending - Test case for multiple pending interrupts.
 * @test: The kunit test context.
 *
 * Verifies the interrupt prioritization logic. When both UVLO2 and BATOILO
 * interrupts are pending, the test ensures that the higher priority
 * interrupt (UVLO2) is reported.
 */
static void test_max77759_uvlo1_oilo1_pending(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	/* Test case 3: UVLO2 and BATOILO1 are pending. UVLO2 should win */
	priv->mock_data = MAX77759_CHG_INT2_SYS_UVLO2_I |
			  MAX77759_CHG_INT2_BAT_OILO_I;
	ret = max77759_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, UVLO2);
}

/*
 * test_max77759_clr_irq - Test case for clearing an interrupt.
 * @test: The kunit test context.
 *
 * Verifies that after an interrupt is identified, it can be successfully
 * cleared by max77759_clr_irq(), which should result in the corresponding
 * bit being cleared in the mock register.
 */
static void test_max77759_clr_irq(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	priv->mock_data = MAX77759_CHG_INT2_SYS_UVLO2_I;
	ret = max77759_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, UVLO2);
	ret = max77759_clr_irq(data->dev, idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0);
}

/*
 * test_max77759_irq_priority - Test case for interrupt priority.
 * @test: The kunit test context.
 *
 * Verifies the interrupt prioritization logic. When multiple interrupts
 * are pending, the test ensures that the higher priority interrupt is
 * reported.
 */
static void test_max77759_irq_priority(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	/* BATOILO1 and UVLO1 are pending. BATOILO1 should win */
	priv->mock_data = MAX77759_CHG_INT2_BAT_OILO_I |
			  MAX77759_CHG_INT2_SYS_UVLO1_I;
	ret = max77759_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, BATOILO1);

	/* UVLO2 and UVLO1 are pending. UVLO2 should win */
	priv->mock_data = MAX77759_CHG_INT2_SYS_UVLO2_I |
			  MAX77759_CHG_INT2_SYS_UVLO1_I;
	ret = max77759_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, UVLO2);
}

static void oilo_test_sequence(struct kunit *test, int regval, int threshold)
{
	int val, ret;
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;

	ret = max77759_set_oilo(data->dev, threshold);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_oilo(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, threshold);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, regval);
}

static void oilo_test_err(struct kunit *test, int threshold)
{
	int ret;
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;

	ret = max77759_set_oilo(data->dev, threshold);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void test_max77759_bcl_test_oilo(struct kunit *test)
{
	oilo_test_sequence(test, 0x0, 0);
	oilo_test_sequence(test, 0x1, 4000);
	oilo_test_sequence(test, 0x2, 4200);
	oilo_test_sequence(test, 0x3, 4400);
	oilo_test_sequence(test, 0x4, 4600);
	oilo_test_sequence(test, 0x5, 4800);
	oilo_test_sequence(test, 0x6, 5000);
	oilo_test_sequence(test, 0x7, 5200);
	oilo_test_sequence(test, 0x8, 5400);
	oilo_test_sequence(test, 0x9, 5600);
	oilo_test_sequence(test, 0xA, 5800);
	oilo_test_sequence(test, 0xB, 6000);
	oilo_test_sequence(test, 0xC, 6200);
	oilo_test_sequence(test, 0xD, 6400);
	oilo_test_sequence(test, 0xE, 6600);
	oilo_test_sequence(test, 0xF, 6800);

	oilo_test_err(test, 1);
	oilo_test_err(test, 3800);
	oilo_test_err(test, 3999);
	oilo_test_err(test, 6801);
	oilo_test_err(test, 7000);
}

/*
 * test_max77759_clr_irq_uvlo1 - Test case for clearing UVLO1 interrupt.
 * @test: The kunit test context.
 *
 * Verifies that after UVLO1 interrupt is identified, it can be successfully
 * cleared by max77779_clr_irq(), which should result in the corresponding
 * bit being cleared in the mock register.
 */
static void test_max77759_clr_irq_uvlo1(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	priv->mock_data = MAX77759_CHG_INT2_SYS_UVLO1_I;
	ret = max77759_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, UVLO1);
	ret = max77759_clr_irq(data->dev, idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0);
}

/*
 * test_max77759_bcl_test_uvlo_addr - Test uvlo address
 * @test: The knuit test context.
 *
 * The test checks if address is mapping correctly or not.
 */
static void test_max77759_bcl_test_uvlo_addr(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int ret, valid_lvl = 3200;

	ret = max77759_set_uvlo1(data->dev, valid_lvl);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->addr, MAX77759_CHG_CNFG_15);

	ret = max77759_set_uvlo2(data->dev, valid_lvl);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->addr, MAX77759_CHG_CNFG_16);

	ret = max77759_get_uvlo1(data->dev, &valid_lvl);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->addr, MAX77759_CHG_CNFG_15);

	ret = max77759_get_uvlo2(data->dev, &valid_lvl);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->addr, MAX77759_CHG_CNFG_16);
}

/*
 * test_max77759_bcl_test_uvlo - Test case for uvlo setters and getters
 * @test: The kunit test context.
 *
 * The test checks for the proper behavior of setting uvlo1/2 within bounds,
 * out of bounds, and the getter behavior for each of those cases. The test
 * also checks the return values of the functions.
 */
static void test_max77759_bcl_test_uvlo(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int val, ret, i;
	int invalid_input = VD_LOWER_LIMIT - 10;

	/* uvlo lower than VD_LOWER_LIMIT is invalid, VD_LOWER_LIMIT is default */
	ret = max77759_set_uvlo1(data->dev, invalid_input);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = max77759_get_uvlo1(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, VD_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x0);

	ret = max77759_set_uvlo2(data->dev, invalid_input);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = max77759_get_uvlo2(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, VD_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0);

	/* uvlo larger than VD_UPPER_LIMIT is invalid, VD_LOWER_LIMIT is default */
	invalid_input = VD_UPPER_LIMIT + 10;
	ret = max77759_set_uvlo1(data->dev, invalid_input);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = max77759_get_uvlo1(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, VD_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x0);

	ret = max77759_set_uvlo2(data->dev, invalid_input);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = max77759_get_uvlo2(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, VD_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0);

	/* test valid with fixed steps */
	for (i = VD_LOWER_LIMIT; i <= VD_UPPER_LIMIT; i = i + VD_STEP) {
		ret = max77759_set_uvlo1(data->dev, i);
		KUNIT_EXPECT_EQ(test, ret, 0);

		ret = max77759_get_uvlo1(data->dev, &val);
		KUNIT_EXPECT_EQ(test, val, i);
		KUNIT_EXPECT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, priv->mock_data, get_uvlo_reg_data(i));

		ret = max77759_set_uvlo2(data->dev, i);
		KUNIT_EXPECT_EQ(test, ret, 0);

		ret = max77759_get_uvlo2(data->dev, &val);
		KUNIT_EXPECT_EQ(test, val, i);
		KUNIT_EXPECT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, priv->mock_data, get_uvlo_reg_data(i));
	}

	/* Test with valid voltage which is not a multiple of VD_STEP */
	ret = max77759_set_uvlo1(data->dev, 3210);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_uvlo1(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, 3200);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, get_uvlo_reg_data(3200));

	ret = max77759_set_uvlo2(data->dev, 3210);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_uvlo2(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, 3200);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, get_uvlo_reg_data(3200));
}

/*
 * test_max77759_get_irq_read_fail - Test case for a hardware read failure.
 * @test: The kunit test context.
 *
 * Verifies that max77759_get_irq() returns IRQ_NONE when the hardware
 * read fails.
 */
static void test_max77759_get_irq_read_fail(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	priv->mock_data = -1;
	ret = max77759_get_irq(data->dev, &idx);
	KUNIT_EXPECT_EQ(test, ret, IRQ_NONE);
}

/*
 * test_max77759_bcl_test_uvlo_hyst - Test case for uvlo hysteresis setters and getters.
 * @test: The kunit test context.
 *
 * The test checks for the proper behavior of setting uvlo1/2 hysteresis within bounds,
 * out of bounds, and the getter behavior for each of those cases. The test
 * also checks the return values of the functions.
 */
static void test_max77759_bcl_test_uvlo_hyst(struct kunit *test)
{
	struct max77759_test_priv *priv = test->priv;
	struct max77759_bcl_irq_data *data = &priv->data;
	int val, ret;
	int valid_input = 150;

	/* Test setting a valid hysteresis value (e.g., 150mV) */
	ret = max77759_set_uvlo1_hyst(data->dev, valid_input);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, valid_input);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x20);

	/* Test setting the lower boundary limit (HYST_LOWER_LIMIT) */
	ret = max77759_set_uvlo1_hyst(data->dev, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x0);

	/* Test setting an invalid value below the lower limit (e.g., 0mV).
	 * The set operation should fail, and the register value should remain
	 * at the previous successful setting (HYST_LOWER_LIMIT).
	 */
	ret = max77759_set_uvlo1_hyst(data->dev, 0);
	KUNIT_EXPECT_LT(test, ret, 0);

	ret = max77759_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x0);

	/* Test setting the upper boundary limit (HYST_UPPER_LIMIT) */
	ret = max77759_set_uvlo1_hyst(data->dev, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x30);

	/* Test setting an invalid value above the upper limit (e.g., 250mV).
	 * The set operation should fail, and the register value should remain
	 * at the previous successful setting (HYST_UPPER_LIMIT).
	 */
	ret = max77759_set_uvlo1_hyst(data->dev, HYST_UPPER_LIMIT + 50);
	KUNIT_EXPECT_LT(test, ret, 0);

	ret = max77759_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x30);

	/* Test setting a valid hysteresis value (e.g., 150mV) */
	ret = max77759_set_uvlo2_hyst(data->dev, valid_input);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, valid_input);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x20);

	/* Test setting the lower boundary limit (HYST_LOWER_LIMIT) */
	ret = max77759_set_uvlo2_hyst(data->dev, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x0);

	/* Test setting an invalid value below the lower limit (e.g., 0mV).
	 * The set operation should fail, and the register value should remain
	 * at the previous successful setting (HYST_LOWER_LIMIT).
	 */
	ret = max77759_set_uvlo2_hyst(data->dev, 0);
	KUNIT_EXPECT_LT(test, ret, 0);

	ret = max77759_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x0);

	/* Test setting the upper boundary limit (HYST_UPPER_LIMIT) */
	ret = max77759_set_uvlo2_hyst(data->dev, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77759_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x30);

	/* Test setting an invalid value above the upper limit (e.g., 250mV).
	 * The set operation should fail, and the register value should remain
	 * at the previous successful setting (HYST_UPPER_LIMIT).
	 */
	ret = max77759_set_uvlo2_hyst(data->dev, HYST_UPPER_LIMIT + 50);
	KUNIT_EXPECT_LT(test, ret, 0);

	ret = max77759_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_data, 0x30);
}

/*
 * max77759_bcl_test_init - Initialization function for the test suite.
 * @test: The kunit test context.
 *
 * Allocates and initializes the private data structure for the test,
 * sets up mock devices, and activates static stubs for PMIC register
 * read/write operations.
 *
 * Return: 0 on success, or an error code on failure.
 */
static int max77759_bcl_test_init(struct kunit *test)
{
	struct max77759_test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	dev_set_drvdata(&priv->dev, &priv->data);

	priv->data.dev = &priv->dev;
	priv->data.pmic_dev = &priv->dev;

	test->priv = priv;

	kunit_activate_static_stub(test, max77759_external_reg_read_helper,
				   mock_max77759_external_reg_read);
	kunit_activate_static_stub(test, max77759_external_reg_write_helper,
				   mock_max77759_external_reg_write);

	return 0;
}

/*
 * max77759_bcl_test_exit - Exit function for the test suite.
 * @test: The kunit test context.
 *
 * Deactivates the static stubs used for mocking PMIC register access,
 * cleaning up the test environment.
 */
static void max77759_bcl_test_exit(struct kunit *test)
{
	kunit_deactivate_static_stub(test, max77759_external_reg_read_helper);
	kunit_deactivate_static_stub(test, max77759_external_reg_write_helper);
}

static struct kunit_case max77759_bcl_irq_tests[] = {
	KUNIT_CASE(test_max77759_no_irq_pending),
	KUNIT_CASE(test_max77759_uvlo1_pending),
	KUNIT_CASE(test_max77759_uvlo1_oilo1_pending),
	KUNIT_CASE(test_max77759_clr_irq),
	KUNIT_CASE(test_max77759_irq_priority),
	KUNIT_CASE(test_max77759_clr_irq_uvlo1),
	KUNIT_CASE(test_max77759_get_irq_read_fail),
	KUNIT_CASE(test_max77759_bcl_test_uvlo_addr),
	KUNIT_CASE(test_max77759_bcl_test_uvlo),
	KUNIT_CASE(test_max77759_bcl_test_oilo),
	KUNIT_CASE(test_max77759_bcl_test_uvlo_hyst),
	{},
};

static struct kunit_suite max77759_bcl_irq_test_suite = {
	.name = "max77759_bcl_irq_test",
	.test_cases = max77759_bcl_irq_tests,
	.init = max77759_bcl_test_init,
	.exit = max77759_bcl_test_exit,
};

kunit_test_suite(max77759_bcl_irq_test_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Allen Jiang <alljiang@google.com>");
MODULE_AUTHOR("Hiroshi Akiyama <hiroshiakiyama@google.com>");
MODULE_AUTHOR("Jasmine Cha <chajasmine@google.com>");
MODULE_AUTHOR("Sam Ou <samou@google.com>");
MODULE_AUTHOR("Maggie Cheng <maggiecheng@google.com>");
MODULE_DESCRIPTION("Google LLC MAX77759 BCL IRQ Tests");
