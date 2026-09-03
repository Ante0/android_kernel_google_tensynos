/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM Network Packet Generator Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_NET_PKTGEN_H__
#define __LVM_NET_PKTGEN_H__

#include <net/platform.h>

/**
 * struct lvm_pktgen - LVM packet generator
 * @filepath: Path to the file containing captured packet data
 * @raw: Pointer to the raw packet data
 * @len: Length of the packet data
 */
struct lvm_pktgen {
	char			*filepath;
	unsigned char		*raw;
	int			len;
};

int lvm_pktgen_file_read(struct lvm_pktgen *pktgen);
ssize_t lvm_pktgen_raw_dump(struct lvm_pktgen *pktgen, char *buf);
int lvm_pktgen_init(struct lvm_net *net);
void lvm_pktgen_deinit(struct lvm_net *net);

#endif  /* __LVM_NET_PKTGEN_H__ */
