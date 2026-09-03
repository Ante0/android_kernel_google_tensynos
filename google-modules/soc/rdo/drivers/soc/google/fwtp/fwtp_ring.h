/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint ring services header.
 *
 * This header is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT licenses.
 */

#ifndef FWTP_RING_H_
#define FWTP_RING_H_

#ifdef __KERNEL__
#include <linux/build_bug.h>
#include <linux/stddef.h>
#define STATIC_ASSERT static_assert
#else
#include <lib/utils/compiler.h>
#include <lib/utils/size.h>
#include <stdbool.h>
#include <stdint.h>
#endif

__BEGIN_CDECLS

////////////////////////////////////////////////////////////////////////////////
//
// Types and definitions.
//

// Ring version enumeration.
enum tracepoint_ring_version {
	TRACEPOINT_RING_VERSION_INVALID = 0,
	TRACEPOINT_RING_VERSION_LEGACY,
	TRACEPOINT_RING_VERSION_FWTP
};

//
// Tracepoint entry ring buffer.
//
// New entries are added at the byte offset specified by tail_offset. The next
// entry to be read (the oldest entry) is located at the byte offset specified
// by head_offset.
//
// When entries are added or read, tail_offset and head_offset are incremented
// by the entry size. They do not need to wrap back to 0 when they exceed the
// ring size. If they don't wrap, then overflows can be detected when
// tail_offset - head_offset is greater than the ring buffer size. For this
// reason, the offset modulo the ring buffer size must be used to access entries
// in the ring buffer.
//
// Another advantage of not wrapping the offsets is that it allows for multiple
// head offsets to be used in the future. When tracepoints are added, only the
// tail offset is needed; the head offset is only needed when the tracepoints
// are read. The head offset could be moved out of struct tracepoint_ring and
// into other data structures. This could enable having one head offset for
// copying tracepoints to a larger DRAM buffer and one head offset for sending
// tracepoints to the AP.
//
// Since the head and tail offsets have a maximum size, they must eventually
// wrap. The value at which they wrap is the ring size. In order to preserve
// correct behavior, the ring size must be a multiple of the ring buffer size.
// If the ring buffer size is a power of 2, then the ring size may be equal to
// the maximum offset size plus one (e.g., UINT32_MAX + 1). In this case,
// modulus operations are implicitly performed by unsigned 32-bit operations.
//
// If the ring buffer size is not a power of 2, explicit modulus operations must
// be performed. The ring size is chosen to be N * the ring buffer size where N
// is some integer and N * the ring buffer size is less than the maximum ring
// offset size.
//
// The ring is shared between different software modules, so it must be packed.
// Declaring it packed could incur a performance penalty on the compiled code,
// so a static assertion is used to ensure it's packed. Pointers can be
// different sizes on different systems, so the buffer field is placed last and
// is assumed to require 64-bit alignment.
//
struct tracepoint_ring {
	uint32_t magic;
	uint16_t version;
	uint8_t low_latency;
	uint8_t reserved;
	uint32_t timestamp_hz;
	uint32_t size;
	uint32_t head_offset;
	uint32_t tail_offset;
	uint8_t *buffer;
};
STATIC_ASSERT((sizeof_field(struct tracepoint_ring, magic) +
	       sizeof_field(struct tracepoint_ring, version) +
	       sizeof_field(struct tracepoint_ring, low_latency) +
	       sizeof_field(struct tracepoint_ring, reserved) +
	       sizeof_field(struct tracepoint_ring, timestamp_hz) +
	       sizeof_field(struct tracepoint_ring, size) +
	       sizeof_field(struct tracepoint_ring, head_offset) +
	       sizeof_field(struct tracepoint_ring, tail_offset) +
	       sizeof_field(struct tracepoint_ring, buffer)) ==
	      sizeof(struct tracepoint_ring));

// Ring management macros.

#define TRACEPOINT_MAGIC 0x54524350
#define TRACEPOINT_RING_SIZE_MULTIPLE 128

/*
 * Division operations must be avoided on M0 CPUs like CAP. For these CPUs, the
 * ring buffer size must be a power of 2.
 */
#if defined(ARMCM0P_MPU)
#define TRACEPOINT_RING_BUF_SIZE_IS_POWER_OF_2
#endif

#ifdef TRACEPOINT_RING_BUF_SIZE_IS_POWER_OF_2
#define TRACEPOINT_RING_MOD_BUF_SIZE(ring, offset) \
	((offset) & ((ring)->size - 1))
#define TRACEPOINT_RING_SIZE(ring) \
	((1ULL << 32) + (0 * (/*unused*/ ring)->size))
#define TRACEPOINT_RING_MOD_RING_SIZE(ring, offset) \
	((offset) + (0 * (/*unused*/ ring)->size))
#else
#define TRACEPOINT_RING_MOD_BUF_SIZE(ring, offset) ((offset) % (ring)->size)
#define TRACEPOINT_RING_SIZE(ring) \
	(TRACEPOINT_RING_SIZE_MULTIPLE * (ring)->size)
#define TRACEPOINT_RING_MOD_RING_SIZE(ring, offset) \
	((offset) % TRACEPOINT_RING_SIZE(ring))
#endif

#define TRACEPOINT_RING_OFFSET_ADD(ring, offset, add_value) \
	TRACEPOINT_RING_MOD_RING_SIZE(ring, (offset) + (add_value))

#define TRACEPOINT_RING_HEAD_BUF_OFFSET(ring) \
	TRACEPOINT_RING_MOD_BUF_SIZE(ring, (ring)->head_offset)
#define TRACEPOINT_RING_TAIL_BUF_OFFSET(ring) \
	TRACEPOINT_RING_MOD_BUF_SIZE(ring, (ring)->tail_offset)

#define TRACEPOINT_RING_ADVANCE_HEAD(ring, count) \
	((ring)->head_offset =                    \
		 TRACEPOINT_RING_OFFSET_ADD(ring, (ring)->head_offset, count))
#define TRACEPOINT_RING_ADVANCE_TAIL(ring, count) \
	((ring)->tail_offset =                    \
		 TRACEPOINT_RING_OFFSET_ADD(ring, (ring)->tail_offset, count))

#define TRACEPOINT_RING_UNREAD_SIZE(ring, head_offset) \
	TRACEPOINT_RING_MOD_RING_SIZE(ring, (ring)->tail_offset - (head_offset))

// Forward declarations.
struct fwtp_ring;
struct fwtp_ring_fwd;

// Maximum number of FWTP rings.
#define FWTP_MAX_RING_COUNT 6

//
// Callback function that handles notifications when the tail offset of the ring
// specified by ring has advanced to a threshold. Context for the callback is
// specified by cb_ctx.
//
//   ring                   Ring whose tail offset has advanced to a threshold.
//   cb_ctx                 Context for use by the callback.
//
typedef void(fwtp_ring_notify_cb)(struct fwtp_ring *ring, void *cb_ctx);

//
// Function that sends ring notifications for the ring specified by ring.
//
//   ring                   Ring for which to send notifications.
//
typedef void(fwtp_ring_send_notifications_func)(struct fwtp_ring *ring);

//
// Structure representing an FWTP ring.
//
struct fwtp_ring {
	// Ring number.
	unsigned int ring_num;
	// Legacy tracepoint ring.
	struct tracepoint_ring *ring;
	// Ring notification callback.
	fwtp_ring_notify_cb *notify_cb;
	// Ring notification callback context.
	void *notify_cb_ctx;
	// Invoke notification callback after this number of bytes are written to
	// ring.
	unsigned int notify_byte_count;
	// Next ring tail offset at which to invoke notification callback.
	uint32_t next_notify_tail_offset;
};

//
// Function that operates on the FWTP ring forwarding record specified by
// ring_fwd.
//
//   ring_fwd               FWTP ring forwarding record.
//
typedef void(fwtp_ring_fwd_func)(struct fwtp_ring_fwd *ring_fwd);

//
// Structure used for forwarding tracepoints from a source ring to a destination
// ring.
//
// When update_byte_count bytes are written to the source ring, the tracepoints
// in the source ring are forwarded to the destination ring using
// fwtp_ring_fwd_update; however, if update_func is non-NULL, it's invoked
// instead. update_func may be used to defer forwarding (e.g., in a low priority
// thread). Eventually, fwtp_ring_fwd_update should be called to forward the
// tracepoints.
//
// If min_update_invocation_ts_interval is non-zero, it rate limits how often
// update_func is invoked. The first time update_byte_count bytes are written,
// update_func is invoked. update_func won't be invoked again until either
// fwtp_ring_fwd_update is called or at least min_update_invocation_ts_interval
// time has elapsed. The time unit of min_update_invocation_ts_interval is the
// same as is used by PLATFORM_tp_get_timestamp.
//
struct fwtp_ring_fwd {
	// Source ring from which to forward tracepoints.
	struct fwtp_ring *src_ring;
	// Destination ring to which to forward tracepoints.
	struct fwtp_ring *dst_ring;
	// Update destination ring after this many bytes have been added to the source
	// ring.
	unsigned int update_byte_count;
	// If non-NULL, function to use to update the destination ring with new
	// tracepoints from the source ring.
	fwtp_ring_fwd_func *update_func;
	// Context for update function.
	void *update_func_ctx;
	// If non-NULL, this function is invoked whenever the source ring overruns
	// before the destination ring is updated.
	fwtp_ring_fwd_func *overrun_func;
	// Context for overrun function.
	void *overrun_func_ctx;
	// Number of bytes to skip on update when a source ring overrun is detected.
	unsigned int skip_count_on_overrun;
	// Minimum interval to invoke update function if tracepoints haven't been
	// forwarded.
	uint64_t min_update_invocation_ts_interval;
	// Minimum timestamp of next invocation of update function. Set to zero by
	// fwtp_ring_fwd_update.
	uint64_t next_update_invocation_ts;
	// If true, tracepoint forwarding is running.
	bool is_running;
	// Flags blocking tracepoint forwarding. If any bit is set, forwarding is
	// blocked.
	uint32_t block_flags;
};

////////////////////////////////////////////////////////////////////////////////
//
// Prototypes.
//

// FWTP ring service prototypes.
void fwtp_ring_init(void);

void fwtp_ring_enable_notifications(void);

void fwtp_ring_disable_notifications(void);

bool fwtp_ring_get_notifications_enabled(void);

void fwtp_ring_copy(struct tracepoint_ring *src_ring,
		    struct tracepoint_ring *dst_ring,
		    unsigned int skip_count_on_overrun);

void fwtp_ring_set_notify_cb(struct fwtp_ring *fwtp_ring,
			     unsigned int byte_count,
			     fwtp_ring_notify_cb *notify_cb, void *cb_ctx);

void fwtp_ring_set_notify_cb_by_ring_num(unsigned int ring_num,
					 unsigned int byte_count,
					 fwtp_ring_notify_cb *notify_cb,
					 void *cb_ctx);

void fwtp_ring_invoke_notify_cb_if_needed(unsigned int ring_num);

void fwtp_ring_send_notifications(struct fwtp_ring *ring);

void fwtp_ring_set_send_notifications(fwtp_ring_send_notifications_func *func);

struct fwtp_ring *
fwtp_get_ring_from_tracepoint_ring(struct tracepoint_ring *tracepoint_ring);

struct fwtp_ring *fwtp_ring_get_ring_by_ring_num(unsigned int ring_num);

// FWTP ring forwarding service prototypes.
void fwtp_ring_fwd_start(struct fwtp_ring_fwd *ring_fwd);

void fwtp_ring_fwd_stop(struct fwtp_ring_fwd *ring_fwd);

void fwtp_ring_fwd_set_block_flag(struct fwtp_ring_fwd *ring_fwd,
				  unsigned int flag_num, bool block);

void fwtp_ring_fwd_update(struct fwtp_ring_fwd *ring_fwd);

__END_CDECLS

#endif // FWTP_RING_H_
