/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * This header provides constants for CPM IRQ.
 *
 * Copyright 2025 Google LLC
 *
 */
#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_IRQ_CPM_H
#define _DT_BINDINGS_INTERRUPT_CONTROLLER_IRQ_CPM_H

#define CPM_IRQ_RTC_ALARM_NO			0
#define CPM_IRQ_AOC_MIN				7
#define CPM_IRQ_AOC_MAX				(CPM_IRQ_AOC_MIN + 149)
#define CPM_IRQ_AP_GDMC_WAKE			(CPM_IRQ_AOC_MAX + 1)
#define CPM_IRQ_COUNT				512

#endif  /* _DT_BINDINGS_INTERRUPT_CONTROLLER_IRQ_CPM_H */
