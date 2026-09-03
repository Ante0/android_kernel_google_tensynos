/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP telemetry: logging and tracing.
 *
 * Copyright (C) 2022-2026 Google LLC
 */

#ifndef __GCIP_TELEMETRY_H__
#define __GCIP_TELEMETRY_H__

#include <linux/device.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <gcip/gcip-event.h>
#include <gcip/gcip-memory.h>

#define GCIP_TELEMETRY_NAME_LOG "telemetry_log"
#define GCIP_TELEMETRY_NAME_TRACE "telemetry_trace"
#define GCIP_TELEMETRY_NAME_HWTRACE "telemetry_hwtrace"
#define GCIP_TELEMETRY_NAME_OPAQUE "telemetry_opaque"

/* Log level codes used by gcip firmware. */
#define GCIP_FW_LOG_LEVEL_VERBOSE (2)
#define GCIP_FW_LOG_LEVEL_DEBUG (1)
#define GCIP_FW_LOG_LEVEL_INFO (0)
#define GCIP_FW_LOG_LEVEL_WARN (-1)
#define GCIP_FW_LOG_LEVEL_ERROR (-2)
#define GCIP_FW_LOG_LEVEL_FATAL (-3)

/* When log data arrives, recheck for more log data after this delay. */
#define GCIP_TELEMETRY_TYPE_LOG_RECHECK_DELAY 200 /* ms */

/**
 * enum gcip_telemetry_state - Telemetry state codes
 * @GCIP_TELEMETRY_DISABLED: Telemetry is disabled.
 * @GCIP_TELEMETRY_ENABLED: Telemetry is enabled.
 * @GCIP_TELEMETRY_INVALID: Telemetry state is invalid (e.g. after exit).
 */
enum gcip_telemetry_state {
	GCIP_TELEMETRY_DISABLED = 0,
	GCIP_TELEMETRY_ENABLED = 1,
	GCIP_TELEMETRY_INVALID = -1,
};

/* To specify the target of operation. */
/**
 * enum gcip_telemetry_type - Telemetry buffer/device type
 * @GCIP_TELEMETRY_TYPE_LOG: Log telemetry buffer.
 * @GCIP_TELEMETRY_TYPE_TRACE: Trace telemetry buffer.
 * @GCIP_TELEMETRY_TYPE_HWTRACE: Hardware trace telemetry buffer.
 * @GCIP_TELEMETRY_TYPE_OPAQUE: Opaque telemetry buffer.
 * @GCIP_TELEMETRY_TYPE_COUNT: Number of telemetry types (must be the last item).
 */
enum gcip_telemetry_type {
	GCIP_TELEMETRY_TYPE_LOG,
	GCIP_TELEMETRY_TYPE_TRACE,
	GCIP_TELEMETRY_TYPE_HWTRACE,
	GCIP_TELEMETRY_TYPE_OPAQUE,
	GCIP_TELEMETRY_TYPE_COUNT,
};

/**
 * struct gcip_telemetry_header - Shared memory buffer header for telemetry
 * @head: Producer/consumer head pointer offset in the buffer.
 * @size: Total size of the telemetry buffer.
 * @reserved0: Reserved padding words to separate head and tail into different cache lines.
 * @tail: Consumer/producer tail pointer offset in the buffer.
 * @entries_dropped: Number of log/trace entries dropped due to buffer full.
 * @reserved1: Reserved padding words to pad the header size to 128 bytes.
 */
struct gcip_telemetry_header {
	u32 head;
	u32 size;
	u32 reserved0[14];
	u32 tail;
	u32 entries_dropped;
	u32 reserved1[14];
};

/**
 * struct gcip_log_entry_header - Header for an individual log entry in buffer
 * @code: Log level/code indicating severity (e.g. verbose, debug, info, etc.).
 * @length: Length of the log entry payload.
 * @timestamp: Timestamp when the log entry was generated or written by firmware.
 * @crc16: CRC16 checksum for data integrity of the log entry.
 */
struct gcip_log_entry_header {
	s16 code;
	u16 length;
	u64 timestamp;
	u16 crc16;
} __packed;

/**
 * struct gcip_telemetry - Telemetry management structure
 * @dev: Device used for logging and memory allocation.
 * @type: Type of the telemetry object (log, trace, hwtrace, opaque).
 * @memory: Shared memory buffer metadata.
 * @state_lock: Mutex protecting @state transitions.
 * @state: State of the telemetry instance.
 * @header: Pointer to the telemetry buffer header in shared memory.
 * @event_mgr: The event manager to signal userspace.
 * @event_id: The event ID to signal for telemetry events.
 * @name: Name of the telemetry buffer instance for debugging purposes.
 * @work: Work structure for processing of incoming telemetry data.
 * @fallback_fn: Fallback function called if no eventfd is registered or for default handling.
 * @mmap_lock: Mutex protecting @mmapped_count.
 * @mmapped_count: Number of VMAs currently mapped to this telemetry buffer.
 */
struct gcip_telemetry {
	struct device *dev;
	enum gcip_telemetry_type type;
	struct gcip_memory memory;
	struct mutex state_lock;
	enum gcip_telemetry_state state;
	struct gcip_telemetry_header *header;
	struct gcip_event_mgr *event_mgr;
	size_t event_id;
	const char *name;
	struct work_struct work;
	void (*fallback_fn)(const struct gcip_telemetry *tel);
	struct mutex mmap_lock;
	long mmapped_count;
};

struct gcip_kci;

/**
 * struct gcip_telemetry_kci_args - Arguments passed to KCI send callback.
 * @kci: Pointer to KCI object used for sending command/data.
 * @addr: DMA address of the telemetry memory buffer to be sent.
 * @size: Size of the telemetry memory buffer.
 */
struct gcip_telemetry_kci_args {
	struct gcip_kci *kci;
	u64 addr;
	u32 size;
};

/**
 * gcip_telemetry_kci() - Sends telemetry KCI through send kci callback.
 * @tel: The object holds the info of the telemetry buffer.
 * @send_kci: The callback function to send the KCI, which receives gcip_telemetry_kci_args and
 *            returns:
 *            0 - Success
 *            > 0 - Firmware error
 *            < 0 - Driver error
 * @kci: The pointer to the gcip_kci object to interact with gcip_kci APIs.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_telemetry_kci(struct gcip_telemetry *tel,
		       int (*send_kci)(const struct gcip_telemetry_kci_args *),
		       struct gcip_kci *kci);

/**
 * gcip_telemetry_irq_handler() - The interrupt handler to schedule the worker when irq arrives.
 * @tel: The object holds the info of the telemetry buffer.
 */
void gcip_telemetry_irq_handler(struct gcip_telemetry *tel);

/**
 * gcip_telemetry_mmap() - Mmaps the telemetry buffer.
 * @tel: The object holds the info of the telemetry buffer.
 * @vma: The struct holds the data to communicate with mm APIs.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_telemetry_mmap(struct gcip_telemetry *tel, struct vm_area_struct *vma);

/**
 * gcip_telemetry_init() - Initializes struct gcip_telemetry.
 * @tel: The telemetry object to be initialized.
 * @type: The type of telemetry.
 * @dev: The device this telemetry object bound to.
 * @event_mgr: The event manager to associate.
 * @event_id: The event ID to signal for telemetry events.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_telemetry_init(struct gcip_telemetry *tel, enum gcip_telemetry_type type,
			struct device *dev, struct gcip_event_mgr *event_mgr, size_t event_id);

/**
 * gcip_telemetry_exit() - Exits and sets the telemetry state to GCIP_TELEMETRY_INVALID.
 * @tel: The telemetry object to be exited.
 */
void gcip_telemetry_exit(struct gcip_telemetry *tel);

#endif /* __GCIP_TELEMETRY_H__ */
