/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC.
 */

#ifndef FSM_LISTENER_H
#define FSM_LISTENER_H

#include "../common/radio-google.h"

int fsm_listener_init(struct radio_google *goog);

void fsm_listener_exit(struct radio_google *goog);

#endif /* FSM_LISTENER_H */
