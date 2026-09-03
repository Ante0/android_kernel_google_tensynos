/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google Whitechapel AoC Core Driver
 *
 * Copyright (c) 2021 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#if IS_ENABLED(CONFIG_SOC_MBU)
  #include "aoc-interface-mbu.h"
#elif IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
  #include "aoc-interface-lga.h"
#elif IS_ENABLED(CONFIG_SOC_ZUMA)
  #include "aoc-interface-zuma.h"
#elif IS_ENABLED(CONFIG_SOC_GS201)
  #include "aoc-interface-gs201.h"
#elif IS_ENABLED(CONFIG_SOC_GS101)
  #include "aoc-interface-gs101.h"
#else
  #error "Unsupported platform!"
#endif

