/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 *
 * This file defines necessary nanopb types based on Linux.
 */
#ifndef NANOPB_LINUX
#define NANOPB_LINUX
#include <linux/string.h>
#include <linux/types.h>

#define CHAR_BIT (8)

typedef u8 uint_fast8_t;
typedef u8 uint_least8_t;
typedef s8 int_least8_t;
typedef u16 uint_least16_t;
typedef s16 int_least16_t;
#endif
