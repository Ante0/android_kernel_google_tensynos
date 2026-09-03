/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC.
 */

#ifndef __AP_AWAKE_H__
#define __AP_AWAKE_H__

#include "radio-google.h"

int ap_awake_gpio_init(struct radio_google *goog);
int ap_awake_gpio_set(bool state);

#endif /* __AP_AWAKE_H__ */
