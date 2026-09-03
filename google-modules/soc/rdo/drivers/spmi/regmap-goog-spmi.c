// SPDX-License-Identifier: GPL-2.0-only
/*
 * Register map access API - SPMI support (with additional support for
 * peripherals that use non-compliant 16 bit addressing).
 *
 * Based on upstream driver regmap-spmi.c
 * Copyright 2011 Wolfson Microelectronics plc
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 * Copyright (C) 2024, Google LLC
 * Author: Jim Wylder <jwylder@google.com>
 */

#include <linux/regmap.h>
#include <linux/regmap-goog-spmi.h>
#include <linux/spmi.h>
#include <linux/module.h>
#include <linux/init.h>

struct spmi_bus_context {
	struct spmi_device	*sdev;
	size_t			reg_bytes;
	size_t			val_bytes;
};

static int regmap_goog_spmi_read(void *context,
				 const void *reg, size_t reg_size,
				 void *val, size_t val_size)
{
	struct spmi_bus_context *sb_context = context;
	struct spmi_device *sdev = sb_context->sdev;
	int err;
	size_t len;
	u16 addr;

	if ((reg_size != sb_context->reg_bytes) || (val_size < sb_context->val_bytes))
		return -EINVAL;

	if (reg_size == 2)
		addr = *(u16 *)reg;
	else if (reg_size == 1)
		addr = *(u8 *)reg;
	else
		return -EINVAL;

	/* REG_READ: 5 bits of address and 1 byte of data */
	if (addr <= 0x1F && val_size == 1)
		return spmi_register_read(sdev, addr, val);

	/* EXT_REG_READ: 8 bits of address and up to 1 to 16 bytes of data */
	while (addr <= 0xFF && val_size) {
		len = min_t(size_t, val_size, 16);

		err = spmi_ext_register_read(sdev, addr, val, len);
		if (err)
			return err;

		addr += len / sb_context->val_bytes;
		val += len;
		val_size -= len;
	}

	/* handle remaining addresses > 0xFF */

	/* EXT_REG_READ_LONG: 16 bits of address and up to 1 to 8 bytes of data */
	while (val_size) {
		len = min_t(size_t, val_size, 8);

		err = spmi_ext_register_readl(sdev, addr, val, len);
		if (err)
			return err;

		addr += len / sb_context->val_bytes;
		val += len;
		val_size -= len;
	}

	return 0;
}

static int regmap_goog_spmi_gather_write(void *context,
					 const void *reg, size_t reg_size,
					 const void *val, size_t val_size)
{
	struct spmi_bus_context *sb_context = context;
	struct spmi_device *sdev = sb_context->sdev;
	int err;
	size_t len;
	u16 addr;

	if ((reg_size != sb_context->reg_bytes) || (val_size < sb_context->val_bytes))
		return -EINVAL;

	if (reg_size == 2)
		addr = *(u16 *)reg;
	else if (reg_size == 1)
		addr = *(u8 *)reg;
	else
		return -EINVAL;

	/* REG_WRITE: 5 bits of address and 1 byte of data */
	if (addr <= 0x1F && val_size == 1)
		return spmi_register_write(sdev, addr, *(u8 *)val);

	/* EXT_REG_WRITE: 8 bits of address and up to 1 to 16 bytes of data */
	while (addr <= 0xFF && val_size) {
		len = min_t(size_t, val_size, 16);

		err = spmi_ext_register_write(sdev, addr, val, len);
		if (err)
			return err;

		addr += len / sb_context->val_bytes;
		val += len;
		val_size -= len;
	}

	/* handle remaining addresses > 0xFF */

	/* EXT_REG_WRITE_LONG: 16 bits of address and up to 1 to 8 bytes of data */
	while (val_size) {
		len = min_t(size_t, val_size, 8);

		err = spmi_ext_register_writel(sdev, addr, val, len);
		if (err)
			return err;

		addr += len / sb_context->val_bytes;
		val += len;
		val_size -= len;
	}

	return 0;
}

static int regmap_goog_spmi_write(void *context, const void *data, size_t count)
{
	struct spmi_bus_context *sb_context = context;
	size_t reg_size;
	const void *reg;
	const void *val;
	size_t val_size;

	if (count < sb_context->reg_bytes)
		return -EINVAL;

	reg = data;
	reg_size = sb_context->reg_bytes;
	val = data + reg_size;
	val_size = count - reg_size;

	return regmap_goog_spmi_gather_write(sb_context, reg,  reg_size, val, val_size);
}

static const struct regmap_bus regmap_goog_spmi = {
	.read				= regmap_goog_spmi_read,
	.write				= regmap_goog_spmi_write,
	.gather_write			= regmap_goog_spmi_gather_write,
	.reg_format_endian_default	= REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default	= REGMAP_ENDIAN_NATIVE,
};

struct regmap *__regmap_init_goog_spmi(struct spmi_device *sdev,
				       const struct regmap_config *config,
				       struct lock_class_key *lock_key,
				       const char *lock_name)
{
	struct spmi_bus_context *context;
	struct regmap *regmap;

	if ((config->reg_bits != 8 && config->reg_bits != 16) ||
	    (config->val_bits != 8 && config->val_bits != 16))
		return ERR_PTR(-EINVAL);

	context = devm_kzalloc(&sdev->dev, sizeof(*context), GFP_KERNEL);
	if (!context)
		return ERR_PTR(-ENOMEM);
	context->sdev = sdev;
	context->reg_bytes = config->reg_bits / BITS_PER_BYTE;
	context->val_bytes = config->val_bits / BITS_PER_BYTE;

	regmap = __regmap_init(&sdev->dev, &regmap_goog_spmi, context, config,
			     lock_key, lock_name);
	if (IS_ERR(regmap))
		devm_kfree(&sdev->dev, context);

	return regmap;
}
EXPORT_SYMBOL_GPL(__regmap_init_goog_spmi);

struct regmap *__devm_regmap_init_goog_spmi(struct spmi_device *sdev,
					    const struct regmap_config *config,
					    struct lock_class_key *lock_key,
					    const char *lock_name)
{
	struct spmi_bus_context *context;
	struct regmap *regmap;

	if ((config->reg_bits != 8 && config->reg_bits != 16) ||
	    (config->val_bits != 8 && config->val_bits != 16))
		return ERR_PTR(-EINVAL);

	context = devm_kzalloc(&sdev->dev, sizeof(*context), GFP_KERNEL);
	if (!context)
		return ERR_PTR(-ENOMEM);
	context->sdev = sdev;
	context->reg_bytes = config->reg_bits / BITS_PER_BYTE;
	context->val_bytes = config->val_bits / BITS_PER_BYTE;

	regmap =  __devm_regmap_init(&sdev->dev, &regmap_goog_spmi, context, config,
				  lock_key, lock_name);

	if (IS_ERR(regmap))
		devm_kfree(&sdev->dev, context);

	return regmap;
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_goog_spmi);

MODULE_DESCRIPTION("regmap SPMI Interface");
MODULE_AUTHOR("Jim Wylder <jwylder@google.com>");
MODULE_LICENSE("GPL");
