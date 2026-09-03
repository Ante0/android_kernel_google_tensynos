/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Google wdt header file.
 *
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_WDT_H
#define _GOOGLE_WDT_H

#include <linux/watchdog.h>
#include <dt-bindings/soc/google/google-wdt-def.h>

struct watchdog_device *google_wdt_wdd_get(struct device *dev);

typedef void (*wdt_pet_timestamp_callback_t)(u64 mono_raw_ns, u64 gtc_ns);

void google_wdt_register_pet_timestamp_callback(wdt_pet_timestamp_callback_t callback);
void google_wdt_unregister_pet_timestamp_callback(void);

#endif // _GOOGLE_WDT_H
