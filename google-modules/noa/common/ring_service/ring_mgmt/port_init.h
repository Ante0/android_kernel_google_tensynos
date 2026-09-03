/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for init noa ports
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __NOA_SIM_NEP_PORT_INIT_H__
#define __NOA_SIM_NEP_PORT_INIT_H__

void noa_doorbell_task(unsigned long data);
void noa_ports_init(void);
void noa_ports_enable_ring_service(void);
void noa_ports_free(void);
#endif /* __NOA_SIM_NEP_PORT_INIT_H__ */
