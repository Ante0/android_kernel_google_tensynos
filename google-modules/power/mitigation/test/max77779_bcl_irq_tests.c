// SPDX-License-Identifier: GPL-2.0 only
/*
 * max77779_bcl_irq_tests.c Google BCL MAX77779 tests
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
#include <max77779.h>
#include <ifpmic/max77779/max77779_bcl_irq.h>

#define VDROOP1_VAL 1
#define VDROOP2_VAL 2
#define VDROOP_INVALID_VAL 3
#define VIMON_BUF_SIZE_TEST 64

const uint16_t mock_vimon_raw_data[] = {
	0x1234, 0x5678, 0xABCD, 0xEF01,
	0x1234, 0x5678, 0xABCD, 0xEF01,
};

struct max77779_test_priv {
	u8 mock_vdroop_int_reg;
	u8 mock_chg_reg;
	struct max77779_bcl_irq_data data;
	struct device dev;
	struct bcl_device bcl_dev;
};

/*
 * mock_max77779_external_pmic_reg_read - Mock function for reading a PMIC register.
 * @dev: The device structure.
 * @reg: The register address to read.
 * @val: Pointer to store the read value.
 *
 * This function intercepts calls to read a PMIC register and returns a
 * value controlled by the test case, simulating hardware state.
 *
 * Return: 0 on success.
 */
static int mock_max77779_external_pmic_reg_read(struct device *dev, u8 reg, u8 *val)
{
	struct kunit *test = kunit_get_current_test();
	struct max77779_test_priv *priv = test->priv;

	*val = priv->mock_vdroop_int_reg;
	return 0;
}

/*
 * mock_max77779_external_pmic_reg_write - Mock function for writing to a PMIC register.
 * @dev: The device structure.
 * @reg: The register address to write.
 * @val: The value to write.
 *
 * This function intercepts calls to write to a PMIC register, allowing the
 * test to verify that interrupt clear operations are performed correctly.
 *
 * Return: 0 on success.
 */
static int mock_max77779_external_pmic_reg_write(struct device *dev, u8 reg, u8 val)
{
	struct kunit *test = kunit_get_current_test();
	struct max77779_test_priv *priv = test->priv;

	priv->mock_vdroop_int_reg &= ~val;
	return 0;
}

/*
 * mock_max77779_external_chg_reg_read - Mock function for reading a PMIC register.
 * @dev: The device structure.
 * @reg: The register address to read.
 * @val: Pointer to store the read value.
 *
 * This function intercepts calls to read a PMIC register and returns a
 * value controlled by the test case, simulating hardware state.
 *
 * Return: 0 on success.
 */
static int mock_max77779_external_chg_reg_read(struct device *dev, u8 reg, u8 *val)
{
	struct kunit *test = kunit_get_current_test();
	struct max77779_test_priv *priv = test->priv;

	*val = priv->mock_chg_reg;
	return 0;
}

/*
 * mock_max77779_external_chg_reg_write - Mock function for writing to a PMIC register.
 * @dev: The device structure.
 * @reg: The register address to write.
 * @val: The value to write.
 *
 * This function intercepts calls to write to a PMIC register, allowing the
 * test to verify that interrupt clear operations are performed correctly.
 *
 * Return: 0 on success.
 */
static int mock_max77779_external_chg_reg_write(struct device *dev, u8 reg, u8 val)
{
	struct kunit *test = kunit_get_current_test();
	struct max77779_test_priv *priv = test->priv;

	priv->mock_chg_reg = val;
	return 0;
}

/*
 * mock_max77779_external_vimon_read_buffer - Mock function for reading VIMON data.
 * @dev: The device structure.
 * @buff: The buffer to store the read data.
 * @count: Pointer to store the number of bytes read.
 * @buff_max: The maximum number of bytes to read.
 *
 * This function simulates reading VIMON data, allowing the test to control
 * the data returned and verify the behavior of the calling function.
 *
 * Return: 0 on success.
 */
static int mock_max77779_external_vimon_read_buffer(struct device *dev, uint16_t *buff,
				size_t *count, size_t buff_max)
{
	size_t data_size = sizeof(mock_vimon_raw_data);
	*count = min(buff_max, data_size);
	memcpy(buff, mock_vimon_raw_data, *count);
	return 0;
}

/*
 * max77779_get_dev - Mock function to get the MAX77779 device.
 * @dev: The device structure.
 * @name: The name of the device to get.
 *
 * This function returns the mock device for the test.
 *
 * Return: The mock device.
 */
struct device *max77779_get_dev(struct device *dev, const char *name)
{
	return dev;
}

/*
 * test_max77779_no_irq_pending - Test case for no pending interrupts.
 * @test: The kunit test context.
 *
 * Verifies that max77779_get_irq() returns IRQ_NONE when no interrupt
 * bits are set in the mock register.
 */
static void test_max77779_no_irq_pending(struct kunit *test)
{
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	/* Test case 1: no interrupts are pending */
	priv->mock_vdroop_int_reg = 0;
	ret = max77779_get_irq(data->dev, &idx);
	KUNIT_EXPECT_EQ(test, ret, IRQ_NONE);
}

/*
 * test_max77779_uvlo1_pending - Test case for a single pending interrupt.
 * @test: The kunit test context.
 *
 * Verifies that max77779_get_irq() correctly identifies and returns the
 * index of the UVLO1 interrupt when its corresponding bit is set.
 */
static void test_max77779_uvlo1_pending(struct kunit *test)
{
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	/* Test case 2: UVLO1 is pending */
	priv->mock_vdroop_int_reg = MAX77779_PMIC_VDROOP_INT_SYS_UVLO1_INT_MASK;
	ret = max77779_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, UVLO1);
}

/*
 * test_max77779_uvlo1_oilo1_pending - Test case for multiple pending interrupts.
 * @test: The kunit test context.
 *
 * Verifies the interrupt prioritization logic. When both UVLO2 and BATOILO1
 * interrupts are pending, the test ensures that the higher priority
 * interrupt (UVLO2) is reported.
 */
static void test_max77779_uvlo1_oilo1_pending(struct kunit *test)
{
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	/* Test case 3: UVLO2 and BATOILO1 are pending. UVLO2 should win */
	priv->mock_vdroop_int_reg = MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK |
					MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK;
	ret = max77779_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, UVLO2);
}

/*
 * test_max77779_clr_irq - Test case for clearing an interrupt.
 * @test: The kunit test context.
 *
 * Verifies that after an interrupt is identified, it can be successfully
 * cleared by max77779_clr_irq(), which should result in the corresponding
 * bit being cleared in the mock register.
 */
static void test_max77779_clr_irq(struct kunit *test)
{
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;
	int idx = 0;
	int ret;

	priv->mock_vdroop_int_reg = MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK;
	ret = max77779_get_irq(data->dev, &idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, idx, UVLO2);
	ret = max77779_clr_irq(data->dev, idx);
	KUNIT_EXPECT_GE(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_vdroop_int_reg, 0);
}

static void oilo_test_sequence(struct kunit *test, int regval, int threshold, bool is_oilo2)
{
	int val, ret;
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;

	ret = is_oilo2 ? max77779_set_oilo2(data->dev, threshold) :
			 max77779_set_oilo1(data->dev, threshold);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = is_oilo2 ? max77779_get_oilo2(data->dev, &val) :
			 max77779_get_oilo1(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, threshold);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, regval);
}

/*
 * test_max77779_bcl_test_oilo - Test case for oilo setters and getters.
 * @test: The kunit test context.
 *
 * The test checks for the proper behavior of setting oilo1/2 within bounds,
 * out of bounds, and the getter behavior for each of those cases. The test
 * also checks the return values of the functions.
 */
static void test_max77779_bcl_test_oilo(struct kunit *test)
{
	oilo_test_sequence(test, 0x1, 2000, false);
	oilo_test_sequence(test, 0x10, 5000, false);
	oilo_test_sequence(test, 0x1F, 8000, false);

	oilo_test_sequence(test, 0x1, 4000, true);
	oilo_test_sequence(test, 0x10, 7000, true);
	oilo_test_sequence(test, 0x1F, 10000, true);
}


static void uvlo_test_sequence(struct kunit *test, int regval, int threshold, bool is_uvlo2)
{
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;
	int val, ret;

	ret = is_uvlo2 ? max77779_set_uvlo2(data->dev, threshold) :
			 max77779_set_uvlo1(data->dev, threshold);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = is_uvlo2 ? max77779_get_uvlo2(data->dev, &val) :
			 max77779_get_uvlo1(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, threshold);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, regval);
}

/*
 * test_max77779_bcl_test_uvlo - Test case for uvlo setters and getters.
 * @test: The kunit test context.
 *
 * The test checks for the proper behavior of setting uvlo1/2 within bounds,
 * out of bounds, and the getter behavior for each of those cases. The test
 * also checks the return values of the functions.
 */
static void test_max77779_bcl_test_uvlo(struct kunit *test)
{
	uvlo_test_sequence(test, 0x0, 2600, false);
	uvlo_test_sequence(test, 0x8, 3000, false);
	uvlo_test_sequence(test, 0xf, 3350, false);

	uvlo_test_sequence(test, 0x0, 2600, true);
	uvlo_test_sequence(test, 0x8, 3000, true);
	uvlo_test_sequence(test, 0xf, 3350, true);
}

/*
 * test_max77779_bcl_test_vdroop_config - Test case for vdroop setters.
 * @test: The kunit test context.
 *
 * The test checks for the proper behavior of setting vdroop configs.
 */
static void test_max77779_bcl_test_vdroop_config(struct kunit *test)
{
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;
	int ret;

	ret = max77779_set_uvlo_vdroop(data->dev, UVLO1, VDROOP1_VAL, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x80);

	ret = max77779_set_uvlo_vdroop(data->dev, UVLO1, VDROOP1_VAL, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	ret = max77779_set_uvlo_vdroop(data->dev, UVLO1, VDROOP_INVALID_VAL, true);
	KUNIT_EXPECT_LT(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	ret = max77779_set_uvlo_vdroop(data->dev, UVLO1, VDROOP1_VAL, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x80);
	ret = max77779_set_uvlo_vdroop(data->dev, UVLO1, VDROOP2_VAL, true);
	KUNIT_EXPECT_LT(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x80);

	ret = max77779_set_uvlo_vdroop(data->dev, UVLO2, VDROOP2_VAL, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x80);

	ret = max77779_set_uvlo_vdroop(data->dev, UVLO2, VDROOP2_VAL, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	ret = max77779_set_uvlo_vdroop(data->dev, UVLO2, VDROOP_INVALID_VAL, true);
	KUNIT_EXPECT_LT(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	ret = max77779_set_uvlo_vdroop(data->dev, UVLO2, VDROOP2_VAL, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x80);
	ret = max77779_set_uvlo_vdroop(data->dev, UVLO2, VDROOP1_VAL, true);
	KUNIT_EXPECT_LT(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x80);

	priv->mock_chg_reg = 0;
	ret = max77779_set_oilo_vdroop(data->dev, BATOILO1, VDROOP1_VAL, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x40);
	ret = max77779_set_oilo_vdroop(data->dev, BATOILO1, VDROOP2_VAL, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0xC0);
	ret = max77779_set_oilo_vdroop(data->dev, BATOILO1, VDROOP1_VAL, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x80);
	ret = max77779_set_oilo_vdroop(data->dev, BATOILO1, VDROOP2_VAL, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);
	ret = max77779_set_uvlo_vdroop(data->dev, BATOILO1, VDROOP_INVALID_VAL, true);
	KUNIT_EXPECT_LT(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	ret = max77779_set_oilo_vdroop(data->dev, BATOILO2, VDROOP1_VAL, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x40);
	ret = max77779_set_oilo_vdroop(data->dev, BATOILO2, VDROOP2_VAL, true);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0xC0);
	ret = max77779_set_oilo_vdroop(data->dev, BATOILO2, VDROOP1_VAL, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x80);
	ret = max77779_set_oilo_vdroop(data->dev, BATOILO2, VDROOP2_VAL, false);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);
	ret = max77779_set_uvlo_vdroop(data->dev, BATOILO2, VDROOP_INVALID_VAL, true);
	KUNIT_EXPECT_LT(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);
}

/*
 * test_max77779_vimon_read - Test case for reading VIMON data.
 * @test: The kunit test context.
 *
 * Verifies that max77779_vimon_read() correctly reads VIMON data and
 * copies it to the BCL device's vimon interface.
 */
static void test_max77779_vimon_read(struct kunit *test)
{
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;
	int ret;

	ret = max77779_vimon_read(data->dev);
	KUNIT_EXPECT_EQ(test, ret, sizeof(mock_vimon_raw_data));
	KUNIT_EXPECT_MEMEQ(test, mock_vimon_raw_data, data->bcl_dev->vimon_intf.data,
		    sizeof(mock_vimon_raw_data));
}

/*
 * test_max77779_bcl_test_uvlo_hyst - Test case for uvlo hysteresis setters and getters.
 * @test: The kunit test context.
 *
 * The test checks for the proper behavior of setting uvlo1/2 hysteresis within bounds,
 * out of bounds, and the getter behavior for each of those cases. The test
 * also checks the return values of the functions.
 */
static void test_max77779_bcl_test_uvlo_hyst(struct kunit *test)
{
	struct max77779_test_priv *priv = test->priv;
	struct max77779_bcl_irq_data *data = &priv->data;
	int val, ret;
	int valid_input = 150;

	/* Test setting a valid hysteresis value (e.g., 150mV) */
	ret = max77779_set_uvlo1_hyst(data->dev, valid_input);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77779_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, valid_input);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x20);

	/* Test setting the lower boundary limit (HYST_LOWER_LIMIT) */
	ret = max77779_set_uvlo1_hyst(data->dev, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77779_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	/* Test setting an invalid value below the lower limit (e.g., 0mV).
	 * The set operation should fail, and the register value should remain
	 * at the previous successful setting (HYST_LOWER_LIMIT).
	 */
	ret = max77779_set_uvlo1_hyst(data->dev, 0);
	KUNIT_EXPECT_LT(test, ret, 0);

	ret = max77779_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	/* Test setting the upper boundary limit (HYST_UPPER_LIMIT) */
	ret = max77779_set_uvlo1_hyst(data->dev, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77779_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x30);

	/* Test setting an invalid value above the upper limit (e.g., 250mV).
	 * The set operation should fail, and the register value should remain
	 * at the previous successful setting (HYST_UPPER_LIMIT).
	 */
	ret = max77779_set_uvlo1_hyst(data->dev, HYST_UPPER_LIMIT + 50);
	KUNIT_EXPECT_LT(test, ret, 0);

	ret = max77779_get_uvlo1_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x30);

	/* Test setting a valid hysteresis value (e.g., 150mV) */
	ret = max77779_set_uvlo2_hyst(data->dev, valid_input);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77779_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, valid_input);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x20);

	/* Test setting the lower boundary limit (HYST_LOWER_LIMIT) */
	ret = max77779_set_uvlo2_hyst(data->dev, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77779_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	/* Test setting an invalid value below the lower limit (e.g., 0mV).
	 * The set operation should fail, and the register value should remain
	 * at the previous successful setting (HYST_LOWER_LIMIT).
	 */
	ret = max77779_set_uvlo2_hyst(data->dev, 0);
	KUNIT_EXPECT_LT(test, ret, 0);

	ret = max77779_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_LOWER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x0);

	/* Test setting the upper boundary limit (HYST_UPPER_LIMIT) */
	ret = max77779_set_uvlo2_hyst(data->dev, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);

	ret = max77779_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x30);

	/* Test setting an invalid value above the upper limit (e.g., 250mV).
	 * The set operation should fail, and the register value should remain
	 * at the previous successful setting (HYST_UPPER_LIMIT).
	 */
	ret = max77779_set_uvlo2_hyst(data->dev, HYST_UPPER_LIMIT + 50);
	KUNIT_EXPECT_LT(test, ret, 0);

	ret = max77779_get_uvlo2_hyst(data->dev, &val);
	KUNIT_EXPECT_EQ(test, val, HYST_UPPER_LIMIT);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, priv->mock_chg_reg, 0x30);
}

/*
 * max77779_bcl_test_init - Initialization function for the test suite.
 * @test: The kunit test context.
 *
 * Allocates and initializes the private data structure for the test,
 * sets up mock devices, and activates static stubs for PMIC register
 * read/write operations.
 *
 * Return: 0 on success, or an error code on failure.
 */
static int max77779_bcl_test_init(struct kunit *test)
{
	struct max77779_test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, priv);

	dev_set_drvdata(&priv->dev, &priv->data);

	priv->data.dev = &priv->dev;
	priv->data.pmic_dev = &priv->dev;
	priv->data.bcl_dev = &priv->bcl_dev;

	test->priv = priv;

	kunit_activate_static_stub(test, max77779_external_pmic_reg_read_helper,
				   mock_max77779_external_pmic_reg_read);
	kunit_activate_static_stub(test, max77779_external_pmic_reg_write_helper,
				   mock_max77779_external_pmic_reg_write);
	kunit_activate_static_stub(test, max77779_external_chg_reg_read_helper,
				   mock_max77779_external_chg_reg_read);
	kunit_activate_static_stub(test, max77779_external_chg_reg_write_helper,
				   mock_max77779_external_chg_reg_write);
	kunit_activate_static_stub(test, max77779_external_vimon_read_buffer_helper,
				   mock_max77779_external_vimon_read_buffer);

	return 0;
}

/*
 * max77779_bcl_test_exit - Exit function for the test suite.
 * @test: The kunit test context.
 *
 * Deactivates the static stubs used for mocking PMIC register access,
 * cleaning up the test environment.
 */
static void max77779_bcl_test_exit(struct kunit *test)
{
	kunit_deactivate_static_stub(test, max77779_external_pmic_reg_read_helper);
	kunit_deactivate_static_stub(test, max77779_external_pmic_reg_write_helper);
	kunit_deactivate_static_stub(test, max77779_external_chg_reg_read_helper);
	kunit_deactivate_static_stub(test, max77779_external_chg_reg_write_helper);
	kunit_deactivate_static_stub(test, max77779_external_vimon_read_buffer_helper);
}

static struct kunit_case max77779_bcl_irq_tests[] = {
	KUNIT_CASE(test_max77779_no_irq_pending),
	KUNIT_CASE(test_max77779_uvlo1_pending),
	KUNIT_CASE(test_max77779_uvlo1_oilo1_pending),
	KUNIT_CASE(test_max77779_clr_irq),
	KUNIT_CASE(test_max77779_bcl_test_oilo),
	KUNIT_CASE(test_max77779_bcl_test_uvlo),
	KUNIT_CASE(test_max77779_bcl_test_vdroop_config),
	KUNIT_CASE(test_max77779_vimon_read),
	KUNIT_CASE(test_max77779_bcl_test_uvlo_hyst),
	{},
};

static struct kunit_suite max77779_bcl_irq_test_suite = {
	.name = "max77779_bcl_irq_test",
	.test_cases = max77779_bcl_irq_tests,
	.init = max77779_bcl_test_init,
	.exit = max77779_bcl_test_exit,
};

kunit_test_suite(max77779_bcl_irq_test_suite);

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Allen Jiang <alljiang@google.com>");
MODULE_AUTHOR("Hiroshi Akiyama <hiroshiakiyama@google.com>");
MODULE_AUTHOR("Jasmine Cha <chajasmine@google.com>");
MODULE_AUTHOR("Sam Ou <samou@google.com>");
MODULE_AUTHOR("Maggie Cheng <maggiecheng@google.com>");
MODULE_DESCRIPTION("Google LLC MAX77779 BCL IRQ Tests");
