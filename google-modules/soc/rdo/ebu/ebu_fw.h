/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC
 */
#ifndef _EBU_FW_H_
#define _EBU_FW_H_
#include "ebu_google.h"

int load_and_release_firmware(struct google_ebu *gebu, const char *firmware_name);
void stop_firmware(struct google_ebu *gebu);
#endif
