/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Sam Chao <samchao@google.com>
 */
#ifndef __NOA_MODEM_RING_MANAGER_INSTANCE_H__
#define __NOA_MODEM_RING_MANAGER_INSTANCE_H__
#ifdef linux
#include "ring_manager_instance.h"
#else /* linux */
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */

struct NoaRingManagerInfoNetwork *NoaRingManagerInfoModemInstance(void);

#endif /* __NOA_MODEM_RING_MANAGER_INSTANCE_H__ */
