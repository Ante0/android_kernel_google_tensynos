/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023-2024, Google LLC
 */

#ifndef __GOOG_MBA_Q_XPORT_H
#define __GOOG_MBA_Q_XPORT_H

/*
 * Header of queue mode message defines fields for type, src_id and dst_id.
 *
 *  31 30     29  24      20    16     8     0
 *  +---+------+---+-------+-----+-----+-----+
 *  | V | Type | R | Token | Seq | Src | Dst |
 *  +---+------+---+-------+-----+-----+-----+
 *
 *  Bit 31 is valid bit:	Must be 1.
 *  Bits 30-29 are type:	The message type can be one of the following values:
 *				0x0 One-way message
 *				0x1 Requests (expect response)
 *				0x2 Response (to a previous request)
 *  Bits 28-24 are token:	Reserved.
 *  Bits 23-20 are token:	Reserved for transport layer module.
 *  Bits 19-16 are seq:		Reserved for transport layer module.
 *  Bits 15-8 are source id.
 *  Bits 7-0 are destination id.
 */

#define GOOG_MBA_Q_XPORT_DST_ID_MASK	GENMASK(7, 0)
#define GOOG_MBA_Q_XPORT_SRC_ID_MASK	GENMASK(15, 8)
#define GOOG_MBA_Q_XPORT_SEQ_MASK	GENMASK(19, 16)
#define GOOG_MBA_Q_XPORT_TOKEN_MASK	GENMASK(23, 20)
#define GOOG_MBA_Q_XPORT_TYPE_MASK	GENMASK(30, 29)
#define GOOG_MBA_Q_XPORT_VALID_MASK	GENMASK(31, 31)

#define GOOG_MBA_Q_XPORT_MAX_SRC_ID_VAL 0xff
#define GOOG_MBA_Q_XPORT_MIN_SEQ_VAL	1
#define GOOG_MBA_Q_XPORT_MAX_SEQ_VAL	13
#define GOOG_MBA_Q_XPORT_MAX_TOKEN_VAL	16

#define GOOG_MBA_Q_XPORT_INIT_PROTO_VAL 0x100

#define GOOG_MBA_Q_XPORT_TYPE_ONEWAY		0x0
#define GOOG_MBA_Q_XPORT_TYPE_REQUEST		0x1
#define GOOG_MBA_Q_XPORT_TYPE_RESPONSE		0x2

static inline u32 goog_mba_q_xport_get_dst_id(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_Q_XPORT_DST_ID_MASK, *msg);
}

static inline u32 goog_mba_q_xport_get_src_id(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_Q_XPORT_SRC_ID_MASK, *msg);
}

static inline u32 goog_mba_q_xport_get_seq(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_Q_XPORT_SEQ_MASK, *msg);
}

static inline u32 goog_mba_q_xport_get_token(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_Q_XPORT_TOKEN_MASK, *msg);
}

static inline u32 goog_mba_q_xport_get_type(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_Q_XPORT_TYPE_MASK, *msg);
}

static inline u32 goog_mba_q_xport_get_valid(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_Q_XPORT_VALID_MASK, *msg);
}

static inline void goog_mba_q_xport_set_dst_id(u32 *msg, u32 dst_id)
{
	*msg &= ~GOOG_MBA_Q_XPORT_DST_ID_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_Q_XPORT_DST_ID_MASK, dst_id);
}

static inline void goog_mba_q_xport_set_src_id(u32 *msg, u32 src_id)
{
	*msg &= ~GOOG_MBA_Q_XPORT_SRC_ID_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_Q_XPORT_SRC_ID_MASK, src_id);
}

static inline void goog_mba_q_xport_set_seq(u32 *msg, u32 seq)
{
	*msg &= ~GOOG_MBA_Q_XPORT_SEQ_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_Q_XPORT_SEQ_MASK, seq);
}

static inline void goog_mba_q_xport_set_token(u32 *msg, u32 token)
{
	*msg &= ~GOOG_MBA_Q_XPORT_TOKEN_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_Q_XPORT_TOKEN_MASK, token);
}

static inline void goog_mba_q_xport_set_type(u32 *msg, u32 type)
{
	*msg &= ~GOOG_MBA_Q_XPORT_TYPE_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_Q_XPORT_TYPE_MASK, type);
}

static inline void goog_mba_q_xport_set_valid(u32 *msg, bool is_valid)
{
	u32 val = is_valid ? 1 : 0;
	*msg &= ~GOOG_MBA_Q_XPORT_VALID_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_Q_XPORT_VALID_MASK, val);
}

static inline u32 goog_mba_q_xport_create_hdr(u32 src_id, u32 dst_id, u32 seq, u32 token, u32 type)
{
	return FIELD_PREP(GOOG_MBA_Q_XPORT_SRC_ID_MASK, src_id) |
	       FIELD_PREP(GOOG_MBA_Q_XPORT_DST_ID_MASK, dst_id) |
	       FIELD_PREP(GOOG_MBA_Q_XPORT_SEQ_MASK, seq) |
	       FIELD_PREP(GOOG_MBA_Q_XPORT_TOKEN_MASK, token) |
	       FIELD_PREP(GOOG_MBA_Q_XPORT_TYPE_MASK, type) |
	       FIELD_PREP(GOOG_MBA_Q_XPORT_VALID_MASK, 1);
}

#endif /* __GOOG_MBA_Q_XPORT_H */
