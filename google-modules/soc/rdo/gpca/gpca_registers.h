/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register offsets for the GPCA(General Purpose Crypto Accelerator)
 *
 * Copyright (C) 2022-2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_REGISTERS_H
#define _GOOGLE_GPCA_REGISTERS_H

#include <linux/bits.h>
#include <linux/types.h>

/* GPCA instance's min and max keyslots */
#define GPCA_MIN_KEYSLOT 0
#define GPCA_MAX_KEYSLOT 35

#define GPCA_OWNER_DOMAIN GPCA_OWNER_DOMAIN_ANDROID_VM

/* Domain GPCA FIFO offsets */
#define DOMAIN_GPCA_CRYPTO_FIFO_OFFSET 0xFD400
#define DOMAIN_GPCA_KEY_FIFO_OFFSET 0xFD800

/* Key slot assignments for the domain */
/* Key slot 8 is reserved for Android KWK */
#define DOMAIN_KWK_KEYSLOT 8
/* Key slot 9 is reserved for TZ Sharing KWK */
#define TZ_SHARE_KWK_KEYSLOT 9
/* Key slot 10 is reserved for GSA Sharing KWK */
#define GSA_SHARE_KWK_KEYSLOT 10
/* Key slots 11-15 can be used dynamically by Android */
#define DOMAIN_MIN_KEYSLOT 11
#define DOMAIN_MAX_KEYSLOT 15
/* Android can store large keys in slot 33 */
#define DOMAIN_LARGE_KEYSLOT 33

/* Per domain there are 8 operation slots */
#define DOMAIN_MIN_OPSLOT 0
#define DOMAIN_MAX_OPSLOT 7

/* GPCA Operation context size in bytes */
#define GPCA_OP_CTX_SIZE_BYTES 128

/* GPCA Domain Register offsets */
enum {
	REG_REQ_FIFO_DATA = 0x4,
	REG_REQ_FIFO_STATUS = 0x8,
	REG_RSP_FIFO_DATA = 0xC,
	REG_RSP_FIFO_STATUS = 0x10,
};

/* REQ_FIFO Status */
enum {
	REQ_FIFO_READY = BIT(0),
	REQ_FIFO_EMPTY = BIT(1),
	REQ_FIFO_FULL = BIT(2),
	REQ_FIFO_OVERFLOW = BIT(3),
	REQ_FIFO_CNTR = GENMASK(8, 4),
};

/* RSP_FIFO Status */
enum {
	RSP_FIFO_READY = BIT(0),
	RSP_FIFO_EMPTY = BIT(1),
	RSP_FIFO_FULL = BIT(2),
	RSP_FIFO_UNDERFLOW = BIT(3),
	RSP_FIFO_CNTR = GENMASK(8, 4),
};

/* Domain ISR offsets */
#define DOMAIN_GPCA_CM_ISR_OFFSET 0xFD414
#define DOMAIN_GPCA_KM_ISR_OFFSET 0xFD814

/* Domain ISR_OVF offsets */
#define DOMAIN_GPCA_CM_ISR_OVF_OFFSET 0xFD418
#define DOMAIN_GPCA_KM_ISR_OVF_OFFSET 0xFD818

/* Domain IER offsets */
#define DOMAIN_GPCA_CM_IER_OFFSET 0xFD41C
#define DOMAIN_GPCA_KM_IER_OFFSET 0xFD81C

/* Domain IMR offsets */
#define DOMAIN_GPCA_CM_IMR_OFFSET 0xFD420
#define DOMAIN_GPCA_KM_IMR_OFFSET 0xFD820

/* TODO (b/307237570) Enable ALARM interrupt after it is fixed */
#define GPCA_ALL_IRQS_MASK GENMASK(7, 0)
#define GPCA_ALL_MASKABLE_IRQS_MASK GENMASK(7, 0)

#define GPCA_FIFO_SIZE 16

enum {
	IRQ_REQ_FIFO_RDY = BIT(0),
	IRQ_REQ_FIFO_EMPTY = BIT(1),
	IRQ_REQ_FIFO_FULL = BIT(2),
	IRQ_REQ_FIFO_OVERFLOW = BIT(3),
	IRQ_RSP_FIFO_RDY = BIT(4),
	IRQ_RSP_FIFO_EMPTY = BIT(5),
	IRQ_RSP_FIFO_FULL = BIT(6),
	IRQ_RSP_FIFO_UNDERFLOW = BIT(7),
	IRQ_ALARM = BIT(8),
};

#endif /* _GOOGLE_GPCA_REGISTERS_H */
