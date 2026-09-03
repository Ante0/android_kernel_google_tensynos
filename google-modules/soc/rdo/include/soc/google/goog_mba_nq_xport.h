/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022-2023, Google LLC
 */

#ifndef __GOOG_MBA_NQ_XPORT_H
#define __GOOG_MBA_NQ_XPORT_H

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/types.h>

/*
 * Header of message defines the service ID and a field for additional data
 *
 *  31 30       24 23        16 15                  0
 *  +--+----------+------------+--------------------+
 *  |R0|Service ID|   flags    | Defined by service |
 *  +--+----------+------------+--------------------+
 *
 *  A header has three fields.
 *
 *  Bit 31 is reserved - must be 0
 *  Bit 30-24 is the service ID - value must be between 0x0 and 0x70 (0x71-0x7F are reserved)
 *  Bits 23-16 are flags
 *  Bits 15..0 are defined by the service.
 */

#define GOOG_MBA_NQ_XPORT_DATA_MASK		GENMASK(15, 0)
#define GOOG_MBA_NQ_XPORT_ONEWAY_MASK		GENMASK(21, 21)
#define GOOG_MBA_NQ_XPORT_RESPONSE_MASK	GENMASK(22, 22)
#define GOOG_MBA_NQ_XPORT_ERROR_MASK		GENMASK(23, 23)
#define GOOG_MBA_NQ_XPORT_SERVICE_MASK		GENMASK(30, 24)

static inline u32 goog_mba_nq_xport_get_service_id(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_NQ_XPORT_SERVICE_MASK, *msg);
}

static inline u32 goog_mba_nq_xport_get_error(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_NQ_XPORT_ERROR_MASK, *msg);
}

static inline u32 goog_mba_nq_xport_get_response(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_NQ_XPORT_RESPONSE_MASK, *msg);
}

static inline u32 goog_mba_nq_xport_get_oneway(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_NQ_XPORT_ONEWAY_MASK, *msg);
}

static inline u32 goog_mba_nq_xport_get_data(u32 *msg)
{
	return FIELD_GET(GOOG_MBA_NQ_XPORT_DATA_MASK, *msg);
}

static inline void goog_mba_nq_xport_set_service_id(u32 *msg, u32 service_id)
{
	*msg &= ~GOOG_MBA_NQ_XPORT_SERVICE_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_NQ_XPORT_SERVICE_MASK, service_id);
}

static inline void goog_mba_nq_xport_set_error(u32 *msg, bool is_error)
{
	u32 val = is_error ? 1 : 0;

	*msg &= ~GOOG_MBA_NQ_XPORT_ERROR_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_NQ_XPORT_ERROR_MASK, val);
}

static inline void goog_mba_nq_xport_set_response(u32 *msg, bool is_response)
{
	u32 val = is_response ? 1 : 0;

	*msg &= ~GOOG_MBA_NQ_XPORT_RESPONSE_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_NQ_XPORT_RESPONSE_MASK, val);
}

static inline void goog_mba_nq_xport_set_oneway(u32 *msg, bool is_oneway)
{
	u32 val = is_oneway ? 1 : 0;

	*msg &= ~GOOG_MBA_NQ_XPORT_ONEWAY_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_NQ_XPORT_ONEWAY_MASK, val);
}

static inline void goog_mba_nq_xport_set_data(u32 *msg, u32 data)
{
	*msg &= ~GOOG_MBA_NQ_XPORT_DATA_MASK;
	*msg |= FIELD_PREP(GOOG_MBA_NQ_XPORT_DATA_MASK, data);
}

static inline u32 goog_mba_nq_xport_create_hdr(u32 service_id, u32 data)
{
	return FIELD_PREP(GOOG_MBA_NQ_XPORT_DATA_MASK, data) |
	       FIELD_PREP(GOOG_MBA_NQ_XPORT_SERVICE_MASK, service_id);
}

static inline u32 goog_mba_nq_xport_create_oneway_hdr(u32 service_id, u32 data)
{
	return FIELD_PREP(GOOG_MBA_NQ_XPORT_DATA_MASK, data) |
	       FIELD_PREP(GOOG_MBA_NQ_XPORT_ONEWAY_MASK, 1) |
	       FIELD_PREP(GOOG_MBA_NQ_XPORT_SERVICE_MASK, service_id);
}

#endif /* __GOOG_MBA_NQ_XPORT_H */
