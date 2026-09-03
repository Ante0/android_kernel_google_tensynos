/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Router Advertisement Proxy header file
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include "ra_packet.h"
#else
#include "linux_port/types.h"
#include "net/ra_packet.h"
#endif

#ifndef RA_PROXY_H
#define RA_PROXY_H

// RA packets with a sufficiently large minimum lifetime are not supported
#define RA_PROXY_MAX_SUPPORT_LIFETIME_SECONDS		300

typedef struct rpc_send_ra {
	int ifindex;
	uint32_t packet_len;
	uint8_t packet_data[MAX_RA_PACKET_SIZE];
} __attribute__((packed, aligned(4))) rpc_send_ra_t;

void ra_proxy_init(void);
void ra_proxy_deinit(void);

// return 0: pass
// return -1: drop
int ra_proxy_accept_packet(const uint8_t* packet, uint32_t packet_len, int ifindex);

void ra_proxy_sta_connect(int ifindex);
void ra_proxy_sta_disconnect(int ifindex);

#endif //RA_PROXY_H
