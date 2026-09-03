/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024, Google LLC
 */

#ifndef __REGMAP_GOOG_SPMI_H
#define __REGMAP_GOOG_SPMI_H

#include <linux/types.h>
#include <linux/regmap.h>

struct regmap *__regmap_init_goog_spmi(struct spmi_device *dev,
				       const struct regmap_config *config,
				       struct lock_class_key *lock_key,
				       const char *lock_name);

struct regmap *__devm_regmap_init_goog_spmi(struct spmi_device *sdev,
					    const struct regmap_config *config,
					    struct lock_class_key *lock_key,
					    const char *lock_name);

#define regmap_init_goog_spmi(dev, config) \
	__regmap_lockdep_wrapper(__regmap_init_goog_spmi, #config, dev, config)

#define devm_regmap_init_goog_spmi(dev, config) \
	__regmap_lockdep_wrapper(__devm_regmap_init_goog_spmi, #config, dev, config)

#endif
