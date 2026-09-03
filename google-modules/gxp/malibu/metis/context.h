/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Metis context related macros.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __METIS_CONTEXT_H__
#define __METIS_CONTEXT_H__

/* TODO(b/328171394): The settings here might be incorrect, need to check and update for Metis. */

/* The stream IDs used for each core. */
#define INST_SID_FOR_CORE(_x_) ((_x_) << 4)
#define DATA_SID_FOR_CORE(_x_) (((_x_) << 4) | (1 << 3))
#define IDMA_SID_FOR_CORE(_x_) ((1 << 6) | ((_x_) << 4))

#endif /* __METIS_CONTEXT_H__ */
