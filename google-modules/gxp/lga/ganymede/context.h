/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Ganymede context related macros.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __GANYMEDE_CONTEXT_H__
#define __GANYMEDE_CONTEXT_H__

//TODO(b/249920540): check SIDs
/* The stream IDs used for each core. */
#define INST_SID_FOR_CORE(_x_) ((_x_) << 4)
#define DATA_SID_FOR_CORE(_x_) (((_x_) << 4) | (1 << 3))
#define IDMA_SID_FOR_CORE(_x_) ((1 << 6) | ((_x_) << 4))

#endif /* __GANYMEDE_CONTEXT_H__ */
