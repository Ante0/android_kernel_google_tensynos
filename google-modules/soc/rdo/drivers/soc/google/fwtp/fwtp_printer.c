// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint printer services.
 *
 * This header is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT licenses.
 */

/* Self include. */
#ifdef __KERNEL__
#include "fwtp_printer.h"
#else
#include "lib/fwtp/fwtp_printer.h"
#endif

/* System includes. */
#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/string.h>
#define PRIu64 "llu"
#else
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static inline uint64_t div_u64(uint64_t dividend, uint32_t divisor)
{
	return dividend / divisor;
}
#endif

/* Google includes. */
#ifdef __KERNEL__
#include "fwtp_entry.h"
#include "fwtp_platform.h"
#else
#include "lib/fwtp/fwtp_entry.h"
#include "lib/fwtp/fwtp_platform.h"
#endif

/*******************************************************************************
 *
 * Internal services.
 */

/*
 * Adds a data item with the data specified by data and data_size to the data
 * item list specified by data_items.
 *
 *   data_items             Data item list to which to add data item.
 *   data                   Data item data.
 *   data_size              Size of data item data.
 */
static void fwtp_add_data_item(struct fwtp_data_item_list *data_items,
			       void *data, int data_size)
{
	struct fwtp_data_item *data_item;
	int data_item_size;
	int rem_data_buffer_size;

	/* Do nothing if there's no data buffer. */
	if (!data_items->data_buffer)
		return;

	/* Get the data item size and the remaining data buffer size. */
	data_item_size = sizeof(struct fwtp_data_item) + data_size;
	rem_data_buffer_size =
		data_items->data_buffer_size -
		(data_items->p_next_data_item - data_items->data_buffer);

	/* Do nothing if there's no space for the data item. */
	if (rem_data_buffer_size < data_item_size)
		return;

	/* Add the data item. */
	data_item = (struct fwtp_data_item *)(data_items->p_next_data_item);
	data_item->data_size = data_size;
	memcpy(data_item->data, data, data_size);
	data_items->p_next_data_item += data_item_size;
	data_items->total_data_size += data_item_size;
	data_items->item_count++;
}

/*
 * Flushes the output buffer in the printer context specified by printer_ctx.
 *
 *   printer_ctx            Printer context.
 */
static void fwtp_flush_output_buffer(struct fwtp_printer_ctx *printer_ctx)
{
	if (printer_ctx->output_buffer_end > 0) {
		printer_ctx->output_buffer[printer_ctx->output_buffer_end] =
			'\0';
		printer_ctx->append_output(printer_ctx,
					   printer_ctx->output_buffer);
		printer_ctx->output_buffer_end = 0;
	}
}

/*
 * Appends the string specified by str to some output. The printer context is
 * specified by printer_ctx.
 *
 * If printer_ctx has an output buffer, buffers the output before invoking
 * append_output.
 *
 *   printer_ctx            Printer context.
 *   str                    String to append to output.
 */
static void fwtp_buffered_append_output(struct fwtp_printer_ctx *printer_ctx,
					const char *str)
{
	int str_len;
	int i;

	/*
	 * Append output directly if there's no buffer or buffer is not large enough
	 * for one character and one null character.
	 */
	if (printer_ctx->output_buffer_size < 2) {
		printer_ctx->append_output(printer_ctx, str);
		return;
	}

	/* Buffer the output. */
	str_len = strlen(str);
	for (i = 0; i < str_len; ++i) {
		/* If the buffer is full, flush it. */
		if (printer_ctx->output_buffer_end >=
		    (printer_ctx->output_buffer_size - 1)) {
			fwtp_flush_output_buffer(printer_ctx);
		}

		/* Buffer another character. */
		printer_ctx->output_buffer[printer_ctx->output_buffer_end++] =
			str[i];
	}
}

/*
 * Copies tracepoint data to the buffer specified by dst using the tracepoint
 * data reader specified by data_reader. The number of bytes to copy is
 * specified by size.
 *
 *   data_reader            Tracepoint data reader.
 *   dst                    Buffer to which to copy tracepoint data.
 *   size                   Number of bytes to copy.
 */
static void
fwtp_tracepoint_data_copy(struct fwtp_tracepoint_data_reader *data_reader,
			  void *dst, int size)
{
	uint8_t *dst8 = dst;

	/* Copy the tracepoint data. */
	for (int i = 0; i < size; ++i) {
		/*
		 * Low-latency entries are composed of 16 byte (2 * uint64_t) chunks. The
		 * type field is contained in a byte in the first 8 byte word of each chunk.
		 * Skip this byte when copying data.
		 */
		if (data_reader->is_ll &&
		    ((data_reader->next_data % (2 * sizeof(uint64_t))) ==
		     (FWTP_LL_BASE_ENTRY0_TYPE_SHIFT / 8))) {
			++data_reader->next_data;
		}

		/*
		 * If there's no more tracepoint data to read, mark the underflow condition
		 * and return.
		 */
		if (data_reader->next_data >= data_reader->tracepoint_size) {
			data_reader->underflow = true;
			return;
		}

		/* Copy the next byte of tracepoint data. */
		*dst8++ = data_reader->tracepoint[data_reader->next_data++];
	}
}

/*
 * Decodes the tracepoint with data string specified by tracepoint_string.
 * Returns the size of data required for the string in p_required_data_size if
 * it's non-NULL.
 *
 * If printer_ctx is non-NULL, prints the string using the tracepoint data
 * reader specified by data_reader and the printer context specified by
 * printer_ctx.
 *
 *   printer_ctx            Printer context.
 *   tracepoint_string      Tracepoint name string.
 *   data_reader            Tracepoint data reader.
 *   p_required_data_size   Returned size of data required by string.
 */
/* TODO: b/525380920 - Refactor logic supporting conditional printing. */
static void fwtp_decode_and_print_tracepoint_with_data_string(
	struct fwtp_printer_ctx *printer_ctx, const char *tracepoint_string,
	struct fwtp_tracepoint_data_reader *data_reader,
	unsigned int *p_required_data_size)
{
	const char *p_tp_str = tracepoint_string;
	const char *p_rem_tp_str = p_tp_str;
	char tp_char;
	char string_buffer[32];
	unsigned int required_data_size = 0;

	/* Print the tracepoint string with printf formatting. */
	while (1) {
		/* Get the next tracepoint character. */
		tp_char = *p_tp_str++;
		if (tp_char == '\0')
			break;

		/*
		 * If the tracepoint character is '%', process the printf conversion
		 * specification. Otherwise, print the tracepoint character.
		 */
		if (tp_char == '%') {
			bool is_64bit = false;

			/* Get the next tracepoint character. */
			tp_char = *p_tp_str++;
			if (tp_char == '\0')
				break;

			/*
			 * Decode the conversion specification and print the result. If the
			 * conversion specification is not recognized, stop printing.
			 */
			if (tp_char == 'l') {
				is_64bit = true;
				tp_char = *p_tp_str++;
			}

			if (tp_char == '\0')
				break;

			if (tp_char == 'l')
				tp_char = *p_tp_str++;

			if (tp_char == '\0')
				break;

			if ((tp_char == 'd') || (tp_char == 'u') ||
			    (tp_char == 'x')) {
				const char conversion_spec[5] = { '%', 'l', 'l',
								  tp_char,
								  '\0' };
				union {
					int32_t int32_val;
					uint32_t uint32_val;
					int64_t int64_val;
					uint64_t uint64_val;
				} val;

				/* Update the required data size. */
				required_data_size += is_64bit ?
							      sizeof(uint64_t) :
							      sizeof(uint32_t);

				/* Continue decoding if not printing. */
				if (!printer_ctx)
					continue;

				/* Get the data value. */
				fwtp_tracepoint_data_copy(
					data_reader, &val,
					is_64bit ? sizeof(uint64_t) :
						   sizeof(uint32_t));
				if (data_reader->underflow)
					break;

				/* Print the formatted data value. */
				if (tp_char == 'd') {
					int64_t upcasted_val =
						is_64bit ? val.int64_val :
							   val.int32_val;
					snprintf(string_buffer,
						 sizeof(string_buffer),
						 conversion_spec, upcasted_val);
				} else {
					uint64_t upcasted_val =
						is_64bit ? val.uint64_val :
							   val.uint32_val;
					snprintf(string_buffer,
						 sizeof(string_buffer),
						 conversion_spec, upcasted_val);
				}
				fwtp_buffered_append_output(printer_ctx,
							    string_buffer);

				/* Add the data item. */
				fwtp_add_data_item(&(printer_ctx->data_items),
						   &val,
						   is_64bit ? sizeof(uint64_t) :
							      sizeof(uint32_t));
			} else if (tp_char == 's') {
				uint32_t val;

				/* Update the required data size. */
				required_data_size += sizeof(val);

				/* Continue decoding if not printing. */
				if (!printer_ctx)
					continue;

				/* Get the data value. */
				fwtp_tracepoint_data_copy(data_reader, &val,
							  sizeof(val));
				if (data_reader->underflow)
					break;

				/* Print the data value. */
				fwtp_buffered_append_output(
					printer_ctx, printer_ctx->get_string(
							     printer_ctx, val));

				/* Add the data item. */
				fwtp_add_data_item(&(printer_ctx->data_items),
						   &val, sizeof(val));
			} else {
				break;
			}
		} else {
			/* Continue decoding if not printing. */
			if (!printer_ctx)
				continue;

			/* Print the tracepoint character. */
			string_buffer[0] = tp_char;
			string_buffer[1] = '\0';
			fwtp_buffered_append_output(printer_ctx, string_buffer);
		}

		/* Advance the remaining tracepoint string. */
		p_rem_tp_str = p_tp_str;
	}

	/* Print remaining tracepoint string. */
	if (printer_ctx && *p_rem_tp_str != '\0')
		fwtp_buffered_append_output(printer_ctx, p_rem_tp_str);

	/* Return the required data size. */
	if (p_required_data_size)
		*p_required_data_size = required_data_size;
}

/*
 * Prints and post-processes the tracepoint entry specified by type,
 * entry_timestamp, and name_id using the printer context specified by
 * printer_ctx. Any associated tracepoint data is specified by data_reader and
 * data_items. If p_skip_count is non-zero, skip printing the tracepoint and
 * decrement p_skip_count; the tracepoint will still be post-processed.
 *
 *   printer_ctx            Printer context.
 *   type                   Tracepoint type.
 *   entry_timestamp        Entry timestamp.
 *   name_id                Tracepoint name string ID.
 *   data_reader            Tracepoint data reader.
 *   data_items             Tracepoint data items for post-processing.
 *   p_skip_count           Count of the number of remaining tracepoints to skip
 *                          printing.
 */
static void
fwtp_print_tracepoint(struct fwtp_printer_ctx *printer_ctx, unsigned int type,
		      uint64_t entry_timestamp, uint64_t name_id,
		      struct fwtp_tracepoint_data_reader *data_reader,
		      struct fwtp_data_item_list *data_items, int *p_skip_count)
{
	char string_buffer[32];
	const char *name_str = printer_ctx->get_string(printer_ctx, name_id);

	/* Print the tracepoint. */
	if (*p_skip_count > 0) {
		--(*p_skip_count);
	} else {
		/* Print the tracepoint name. */
		if (printer_ctx->name) {
			fwtp_buffered_append_output(printer_ctx,
						    printer_ctx->name);
			fwtp_buffered_append_output(printer_ctx, ": ");
		}

		/* Print the entry timestamp. */
		if (printer_ctx->timestamp_hz > 0) {
			uint64_t timestamp_sec = div_u64(
				entry_timestamp, printer_ctx->timestamp_hz);
			uint64_t timestamp_usec =
				div_u64(entry_timestamp * 1000000,
					printer_ctx->timestamp_hz) %
				1000000;
			snprintf(string_buffer, sizeof(string_buffer),
				 "%" PRIu64 ".%06" PRIu64 ": ", timestamp_sec,
				 timestamp_usec);
		} else {
			snprintf(string_buffer, sizeof(string_buffer),
				 "%" PRIu64 ": ", entry_timestamp);
		}
		fwtp_buffered_append_output(printer_ctx, string_buffer);

		/* Print any tracepoint prefix. */
		switch (type) {
		case FWTP_LL_ENTRY_TYPE_TRACE_BEGIN:
			fwtp_buffered_append_output(printer_ctx, "BEGIN: ");
			break;
		case FWTP_LL_ENTRY_TYPE_TRACE_END:
			fwtp_buffered_append_output(printer_ctx, "END: ");
			break;
		default:
			break;
		}

		/* Print the entry string. */
		if (data_reader) {
			fwtp_decode_and_print_tracepoint_with_data_string(
				printer_ctx, name_str, data_reader, NULL);
		} else {
			fwtp_buffered_append_output(printer_ctx, name_str);
		}

		/* Print any tracepoint postfix. */
		switch (type) {
		case FWTP_LL_ENTRY_TYPE_TRACE_COUNTER:
			/* Print the counter value. */
			fwtp_decode_and_print_tracepoint_with_data_string(
				printer_ctx, ": %d", data_reader, NULL);
			break;
		default:
			break;
		}

		/* Print newline if needed and flush the tracepoint. */
		if (!printer_ctx->dont_append_new_line)
			fwtp_buffered_append_output(printer_ctx, "\n");
		fwtp_flush_output_buffer(printer_ctx);
	}

	/*
	 * Post-process the tracepoint, even if it's not printed.
	 * Post-processing may use data from multiple tracepoints and generate
	 * output for a tracepoint that is printed (e.g., for SEM).
	 */
	if (printer_ctx->post_process) {
		data_items->p_next_data_item = data_items->data_buffer;
		printer_ctx->post_process(printer_ctx, type, entry_timestamp,
					  name_id, name_str, data_items);
	}
}

/*
 * Returns the size in bytes of the low-latency tracepoint with data entry
 * specified by entry_words. The maximum size is specified by max_entry_size.
 *
 *   entry_words            Low-latency tracepoint with data entry words.
 *   max_entry_size         Maximum size of tracepoint entry.
 */
static int fwtp_get_ll_entry_with_data_size(const uint64_t *entry_words,
					    unsigned int max_entry_size)
{
	int entry_size;

	/* Low-latency tracepoint entries with data start with two entry words. */
	if (max_entry_size < (2 * sizeof(uint64_t)))
		return 0;

	/* Add any additional data entry words. */
	entry_size = 2 * sizeof(uint64_t);
	for (uint32_t i = 2; i * sizeof(uint64_t) < max_entry_size; i += 2) {
		uint64_t forward_type =
			(entry_words[i] >> FWTP_LL_BASE_ENTRY0_TYPE_SHIFT) &
			FWTP_LL_BASE_ENTRY0_TYPE_MASK;
		if (forward_type == FWTP_LL_ENTRY_TYPE_DATA)
			entry_size += 2 * sizeof(uint64_t);
		else
			break;
	}

	return entry_size;
}

/*
 * Returns the number of printable entries in the tracepoint ring specified by
 * ring starting from the ring head offset specified by head_offset.
 *
 *   ring                   Ring from which to get printable entry count.
 *   head_offset            Ring offset from which to start counting.
 */
static int fwtp_get_printable_entry_count_in_ring(struct tracepoint_ring *ring,
						  uint32_t head_offset)
{
	uint32_t unread_size;
	uint32_t printable_entry_count;

	/* Count the number of printable entries. */
	printable_entry_count = 0;
	unread_size = TRACEPOINT_RING_UNREAD_SIZE(ring, head_offset);
	while (unread_size >= sizeof(uint64_t)) {
		const uint64_t *p_entry_words;
		uint32_t entry_size;
		uint32_t buf_end_size;
		uint32_t max_entry_size;
		uint64_t entry_word;
		uint64_t type;

		/*
		 * The entry size cannot be larger than the remaining size to be read and
		 * the size of the data at the end of the ring buffer.
		 */
		buf_end_size = ring->size -
			       TRACEPOINT_RING_MOD_BUF_SIZE(ring, head_offset);
		if (buf_end_size > unread_size)
			max_entry_size = unread_size;
		else
			max_entry_size = buf_end_size;

		/* Get the entry first word. */
		entry_size = sizeof(uint64_t);
		p_entry_words = (uint64_t *)(ring->buffer +
					     TRACEPOINT_RING_MOD_BUF_SIZE(
						     ring, head_offset));
		entry_word = p_entry_words[0];
		type = (entry_word >> FWTP_BASE_ENTRY_TYPE_SHIFT) &
		       FWTP_BASE_ENTRY_TYPE_MASK;

		/* Count printable entries. */
		switch (type) {
		case FWTP_ENTRY_TYPE_BASIC_TRACE:
		case FWTP_ENTRY_TYPE_TRACE_WITH_DATA32:
			++printable_entry_count;
			break;
		case FWTP_ENTRY_TYPE_TRACE_WITH_DATA:
			/* Get the first entry data word. */
			entry_size = 2 * sizeof(uint64_t);
			if (entry_size > max_entry_size)
				break;
			entry_word = p_entry_words[1];

			/* Update and validate the entry size. */
			entry_size =
				sizeof(uint64_t) +
				FWTP_ENTRY_WITH_DATA_WORD_COUNT(entry_word) *
					sizeof(uint64_t);
			if (entry_size > max_entry_size)
				break;

			/* Add another printable entry. */
			++printable_entry_count;

			break;
		case FWTP_LL_ENTRY_TYPE_TRACE_WITH_DATA:
		case FWTP_LL_ENTRY_TYPE_TRACE_BEGIN:
		case FWTP_LL_ENTRY_TYPE_TRACE_END:
		case FWTP_LL_ENTRY_TYPE_TRACE_COUNTER:
			/* Get the total tracepoint with data entry size. */
			entry_size = fwtp_get_ll_entry_with_data_size(
				p_entry_words, max_entry_size);
			if (entry_size == 0)
				break;

			/* Add another printable entry. */
			++printable_entry_count;

			break;
		default:
			break;
		}

		/*
		 * If the entry is not a full entry fitting within the maximum size, stop
		 * counting.
		 */
		if ((entry_size == 0) || (entry_size > max_entry_size))
			break;

		/* Advance the head offset past the counted entry. */
		unread_size -= entry_size;
		head_offset = TRACEPOINT_RING_OFFSET_ADD(ring, head_offset,
							 entry_size);
	}

	return printable_entry_count;
}

/*******************************************************************************
 *
 * External services.
 */

/*
 * Prints the tracepoint entries in the ring specified by ring. The printer
 * context is specified by printer_ctx. Updates the head_offset in the printer
 * context to point to just after the last printed tracepoint entry.
 *
 * If recent_entry_count is greater than 0, prints only the most recent
 * recent_entry_count entries.
 *
 * The latest absolute timestamp is specified by printer_ctx.absolute_timestamp.
 * If an absolute timestamp entry is contained in the ring,
 * printer_ctx.absolute_timestamp is updated with the last absolute timestamp
 * entry.
 *
 * The function specified by printer_ctx.get_string is used to get a string
 * corresponding to a string ID.
 *
 * The function specified by printer_ctx.append_output is used to append printed
 * tracepoints to some output.
 *
 *   printer_ctx            Printer context.
 *   ring                   Ring from which to print tracepoints.
 *   recent_entry_count     If > 0, print only the most recent entries.
 */
void fwtp_print_ring_entries(struct fwtp_printer_ctx *printer_ctx,
			     struct tracepoint_ring *ring,
			     int recent_entry_count)
{
	uint64_t absolute_timestamp = printer_ctx->absolute_timestamp;
	struct fwtp_data_item_list *data_items = &(printer_ctx->data_items);
	uint64_t entry_timestamp;
	uint64_t name_id;
	uint32_t buf_size;
	uint32_t unread_size;
	uint32_t head_offset;
	uint32_t tail_offset;
	int skip_count;

	/* Get the ring info. */
	buf_size = ring->size;
	tail_offset = ring->tail_offset;

	/*
	 * Use the timestamp frequency from the ring if not set in the printer
	 * context.
	 */
	if (!printer_ctx->timestamp_hz)
		printer_ctx->timestamp_hz = ring->timestamp_hz;

	/*
	 * Get the head offset from which to start printing. On overflow, advance the
	 * head offset to the oldest tracepoint.
	 */
	head_offset = printer_ctx->head_offset;
	unread_size = TRACEPOINT_RING_UNREAD_SIZE(ring, head_offset);
	if (unread_size > buf_size) {
		head_offset = TRACEPOINT_RING_OFFSET_ADD(ring, tail_offset,
							 -buf_size);
		unread_size = buf_size;
	}

	/*
	 * If printing only recent entries, determine the number of printable entries
	 * to skip.
	 */
	skip_count = 0;
	if (recent_entry_count > 0) {
		int printable_entry_count =
			fwtp_get_printable_entry_count_in_ring(ring,
							       head_offset);

		if (printable_entry_count > recent_entry_count)
			skip_count = printable_entry_count - recent_entry_count;
	}

	/* Print the entries. */
	while (unread_size >= sizeof(uint64_t)) {
		struct fwtp_tracepoint_data_reader data_reader = {};
		const uint64_t *p_entry_words;
		uint32_t entry_size;
		uint32_t buf_end_size;
		uint32_t max_entry_size;
		uint64_t entry_word;
		uint64_t type;
		unsigned int data_size;
		unsigned int required_data_size;

		/*
		 * The entry size cannot be larger than the remaining size to be read and
		 * the size of the data at the end of the ring buffer.
		 */
		buf_end_size = buf_size -
			       TRACEPOINT_RING_MOD_BUF_SIZE(ring, head_offset);
		if (buf_end_size > unread_size)
			max_entry_size = unread_size;
		else
			max_entry_size = buf_end_size;

		/* Get the first word of the entry. */
		entry_size = sizeof(uint64_t);
		p_entry_words = (uint64_t *)(ring->buffer +
					     TRACEPOINT_RING_MOD_BUF_SIZE(
						     ring, head_offset));
		entry_word = p_entry_words[0];
		type = (entry_word >> FWTP_BASE_ENTRY_TYPE_SHIFT) &
		       FWTP_BASE_ENTRY_TYPE_MASK;

		/* Reset the decoded data items for post-processing. */
		data_items->p_next_data_item = data_items->data_buffer;
		data_items->total_data_size = 0;
		data_items->item_count = 0;

		/* Decode the entry info. */
		switch (type) {
		case FWTP_ENTRY_TYPE_BASIC_TRACE:
		case FWTP_ENTRY_TYPE_TRACE_WITH_DATA:
			/* Get the entry name and timestamp. */
			name_id = (entry_word >> FWTP_BASIC_ENTRY_NAME_SHIFT) &
				  FWTP_BASIC_ENTRY_NAME_MASK;
			entry_timestamp =
				absolute_timestamp +
				((entry_word >>
				  FWTP_BASIC_ENTRY_TIMESTAMP_DELTA_SHIFT) &
				 FWTP_BASIC_ENTRY_TIMESTAMP_DELTA_MASK);
			break;
		case FWTP_ENTRY_TYPE_TRACE_WITH_DATA32:
			name_id = (entry_word >> FWTP_BASIC_ENTRY_NAME_SHIFT) &
				  FWTP_BASIC_ENTRY_NAME_MASK;
			entry_timestamp = absolute_timestamp;
			break;
		case FWTP_ENTRY_TYPE_ABSOLUTE_TIMESTAMP:
			/* Update the absolute timestamp. */
			absolute_timestamp =
				(entry_word >>
				 FWTP_ABSOLUTE_TIMESTAMP_ENTRY_SHIFT) &
				FWTP_ABSOLUTE_TIMESTAMP_ENTRY_MASK;
			break;
		case FWTP_LL_ENTRY_TYPE_TRACE_WITH_DATA:
		case FWTP_LL_ENTRY_TYPE_TRACE_BEGIN:
		case FWTP_LL_ENTRY_TYPE_TRACE_END:
		case FWTP_LL_ENTRY_TYPE_TRACE_COUNTER:
			/* Get the entry name and timestamp. */
			entry_size = 2 * sizeof(uint64_t);
			if (entry_size > max_entry_size)
				break;
			entry_timestamp =
				(entry_word >>
				 FWTP_LL_BASE_ENTRY0_TIMESTAMP_SHIFT) &
				FWTP_LL_BASE_ENTRY0_TIMESTAMP_MASK;
			name_id = (p_entry_words[1] >>
				   FWTP_LL_BASE_ENTRY1_NAME_SHIFT) &
				  FWTP_LL_BASE_ENTRY1_NAME_MASK;
			break;
		default:
			break;
		}

		/* If the entry size was bigger than the maximum size, stop printing. */
		if (entry_size > max_entry_size)
			break;

		/* Print and post-process the entry. */
		switch (type) {
		case FWTP_ENTRY_TYPE_BASIC_TRACE:
			fwtp_print_tracepoint(printer_ctx, type,
					      entry_timestamp, name_id, NULL,
					      data_items, &skip_count);
			break;

		case FWTP_ENTRY_TYPE_TRACE_WITH_DATA32:
			data_reader.tracepoint = (uint8_t *)&(p_entry_words[0]);
			data_reader.next_data =
				FWTP_ENTRY_WITH_32BIT_DATA_DATA_SHIFT / 8;
			data_reader.tracepoint_size =
				data_reader.next_data + sizeof(uint32_t);
			fwtp_print_tracepoint(printer_ctx, type,
					      entry_timestamp, name_id,
					      &data_reader, data_items,
					      &skip_count);
			break;

		case FWTP_ENTRY_TYPE_TRACE_WITH_DATA:
			/* Get the first entry data word. */
			entry_size = 2 * sizeof(uint64_t);
			if (entry_size > max_entry_size)
				break;
			entry_word = p_entry_words[1];
			data_size = FWTP_ENTRY_WITH_DATA_SIZE(entry_word);

			/*
			 * The start of the data is after the 16-bit size field in the second
			 * entry word.
			 */
			data_reader.tracepoint = (uint8_t *)&(p_entry_words[0]);
			data_reader.next_data =
				sizeof(p_entry_words[0]) + sizeof(uint16_t);
			data_reader.tracepoint_size =
				data_reader.next_data + data_size;

			/* Get the remaining data words. */
			entry_size =
				sizeof(uint64_t) +
				FWTP_ENTRY_WITH_DATA_WORD_COUNT(entry_word) *
					sizeof(uint64_t);
			if (entry_size > max_entry_size)
				break;

			/* Print and post-process the tracepoint. */
			fwtp_print_tracepoint(printer_ctx, type,
					      entry_timestamp, name_id,
					      &data_reader, data_items,
					      &skip_count);

			break;

		case FWTP_LL_ENTRY_TYPE_TRACE_WITH_DATA:
		case FWTP_LL_ENTRY_TYPE_TRACE_BEGIN:
		case FWTP_LL_ENTRY_TYPE_TRACE_END:
		case FWTP_LL_ENTRY_TYPE_TRACE_COUNTER:
			/* Get the size of data required to print the tracepoint. */
			fwtp_decode_and_print_tracepoint_with_data_string(
				NULL,
				printer_ctx->get_string(printer_ctx, name_id),
				NULL, &required_data_size);

			/*
			 * Counter tracepoints require an additional 4 bytes of data.
			 *
			 * TODO: b/525380920 - Move this to be with the decoding logic in
			 *                     fwtp_print_tracepoint.
			 */
			if (type == FWTP_LL_ENTRY_TYPE_TRACE_COUNTER)
				required_data_size += 4;

			/*
			 * Get the entry size required for the required data size. The first two
			 * entry words provide four bytes of data. Two additional entry words
			 * are required for each addition 15 bytes of required data.
			 */
			entry_size = 2 * sizeof(uint64_t);
			if (required_data_size > 4) {
				entry_size +=
					((required_data_size - 4 + 14) / 15) *
					2 * sizeof(uint64_t);
			}
			if (entry_size > max_entry_size)
				break;

			/* Get the actual total tracepoint with data entry size. */
			entry_size = fwtp_get_ll_entry_with_data_size(
				p_entry_words, max_entry_size);

			/*
			 * Set up the tracepoint data reader. The start of the data is the data
			 * field in the second entry word.
			 */
			data_reader.tracepoint = (uint8_t *)&(p_entry_words[0]);
			data_reader.next_data =
				sizeof(p_entry_words[0]) +
				(FWTP_LL_ENTRY_WITH_DATA_DATA_SHIFT / 8);
			data_reader.tracepoint_size = entry_size;
			data_reader.is_ll = true;

			/* Print and post-process the tracepoint. */
			fwtp_print_tracepoint(printer_ctx, type,
					      entry_timestamp, name_id,
					      &data_reader, data_items,
					      &skip_count);

			break;

		default:
			break;
		}

		/* If the entry size was bigger than the maximum size, stop printing. */
		if (entry_size > max_entry_size)
			break;

		/* Advance the head offset past the printed entry. */
		unread_size -= entry_size;
		head_offset = TRACEPOINT_RING_OFFSET_ADD(ring, head_offset,
							 entry_size);
	}

	/* Update the printer context. */
	printer_ctx->head_offset = head_offset;
	printer_ctx->absolute_timestamp = absolute_timestamp;
}

/*
 * Prints the tracepoint entries in the ring specified by ring. Prints in the
 * same manner as fwtp_print_ring_entries but uses an intermediate decode
 * buffer. The decode buffer and its size are specified by decode_buffer and
 * decode_buffer_size. The decode buffer must be 64-bit aligned and it must be
 * at least as large as the largest tracepoint
 * (FWTP_MAX_ENTRY_WORDS * sizeof(uint64_t)).
 *
 * If recent_entry_count is greater than 0, prints only the most recent
 * recent_entry_count entries.
 *
 * Tracepoints are copied from the ring buffer to the decode buffer, and this
 * buffer is used to decode the tracepoints.
 *
 * This function is useful in cases where the tracepoint ring buffer is not in
 * local memory. For example, the AP can read and print tracepoints directly
 * from CPM SRAM. However, decoding tracepoints from CPM SRAM may be slow and
 * AP and CPM memory may have different alignment restrictions. Copying
 * tracepoints to an intermediate print buffer can alleviate these issues.
 *
 *   printer_ctx            Printer context.
 *   ring                   Ring from which to print tracepoints.
 *   recent_entry_count     If > 0, print only the most recent entries.
 *   decode_buffer          Intermediate decode buffer.
 *   decode_buffer_size     Size of intermediate decode buffer.
 */
void fwtp_print_ring_entries_with_decode_buffer(
	struct fwtp_printer_ctx *printer_ctx, struct tracepoint_ring *ring,
	int recent_entry_count, void *decode_buffer,
	unsigned int decode_buffer_size)
{
	uint32_t unread_size;
	uint32_t head_offset;
	uint32_t tail_offset;
	uint32_t buf_size;

	/* Get some ring info. */
	buf_size = ring->size;
	tail_offset = ring->tail_offset;

	/*
	 * Use the timestamp frequency from the ring if not set in the printer
	 * context.
	 */
	if (!printer_ctx->timestamp_hz)
		printer_ctx->timestamp_hz = ring->timestamp_hz;

	/*
	 * Determine the size of unread tracepoints and update the head offset on
	 * overrun.
	 */
	head_offset = printer_ctx->head_offset;
	unread_size = TRACEPOINT_RING_UNREAD_SIZE(ring, head_offset);
	if (unread_size > buf_size) {
		head_offset = TRACEPOINT_RING_OFFSET_ADD(ring, tail_offset,
							 -buf_size);
		unread_size = buf_size;
	}

	/*
	 * Print the entries using the decode buffer. Read a block of entries into the
	 * decode buffer and print them from that buffer.
	 */
	while (unread_size > 0) {
		uint32_t block_size;
		uint32_t head_buf_offset;
		uint32_t tail_buf_offset;
		uint32_t head_linear_unread_size;
		uint32_t bytes_decoded;

		/* Determine the decode block size. */
		block_size = unread_size;
		if (block_size > decode_buffer_size)
			block_size = decode_buffer_size;

		/* Get the head and tail ring buffer offsets. */
		head_buf_offset =
			TRACEPOINT_RING_MOD_BUF_SIZE(ring, head_offset);
		tail_buf_offset =
			TRACEPOINT_RING_MOD_BUF_SIZE(ring, tail_offset);

		/*
		 * Determine the size of linear unread tracepoints starting at the head. If
		 * the unread tracepoints wrap around the end of the ring buffer, the head
		 * linear unread size will extend from the head buf offset to the end of the
		 * buffer. Otherwise, the linear unread size includes all unread
		 * tracepoints.
		 */
		if (tail_buf_offset <= head_buf_offset)
			head_linear_unread_size = buf_size - head_buf_offset;
		else
			head_linear_unread_size = unread_size;

		/* Limit the decode block size to the head linear unread size. */
		if (block_size > head_linear_unread_size)
			block_size = head_linear_unread_size;

		/* Copy the tracepoints into the decode buffer. */
		memcpy(decode_buffer, &(ring->buffer[head_buf_offset]),
		       block_size);

		/*
		 * Print the buffered tracepoints and update the unread size and head
		 * offset. Printing will reset the head offset in the printer context.
		 * This is OK in the loop, but the head offset will need to be properly
		 * updated in the printer context before returning.
		 */
		fwtp_print_entries(printer_ctx, (const uint64_t *)decode_buffer,
				   block_size / sizeof(uint64_t));
		bytes_decoded = printer_ctx->head_offset;
		unread_size -= bytes_decoded;
		head_offset = TRACEPOINT_RING_OFFSET_ADD(ring, head_offset,
							 bytes_decoded);
	}

	/* Update the printer context. */
	printer_ctx->head_offset = head_offset;
}
EXPORT_SYMBOL_GPL(fwtp_print_ring_entries_with_decode_buffer);

/*
 * Prints the tracepoint entries in the entry word buffer specified by
 * entry_words. The number of entry words in the buffer is specified by
 * num_words. The printer context is specified by printer_ctx.
 *
 * The latest absolute timestamp is specified by printer_ctx.absolute_timestamp.
 * If an absolute timestamp entry is contained in entry_words,
 * printer_ctx.absolute_timestamp is updated with the last absolute timestamp
 * entry.
 *
 * printer_ctx.head_offset will be set to the number of bytes of printed
 * tracepoints.
 *
 * The function specified by printer_ctx.get_string is used to get a string
 * corresponding to a string ID.
 *
 * The function specified by printer_ctx.append_output is used to append printed
 * tracepoints to some output.
 *
 *   printer_ctx            Printer context.
 *   entry_words            Tracepoint entry buffer.
 *   num_words              Number of words to print from buffer.
 */
void fwtp_print_entries(struct fwtp_printer_ctx *printer_ctx,
			const uint64_t *entry_words, int num_words)
{
	struct tracepoint_ring ring = { 0 };

	/* Do nothing if nothing to print. */
	if (num_words <= 0)
		return;

	/* Set up a ring record for the entries. */
	ring.buffer = (uint8_t *)entry_words;
	ring.size = num_words * sizeof(uint64_t);
	ring.tail_offset = ring.size;

	/* Print all the entries using the ring record. */
	printer_ctx->head_offset = 0;
	fwtp_print_ring_entries(printer_ctx, &ring, 0);
}

/*
 * Gets the next data item from the list of data items specified by data_items.
 * Returns the size of the data item. Each time fwtp_get_next_data_item returns
 * a data item, it advances the next data item in the data item list.
 *
 * If the data item size is less than or equal to max_data_item_size, the data
 * item is returned in p_data_item; otherwise, p_data_item is not modified, and
 * the next data item in the list is not advanced (i.e., calling
 * fwtp_get_next_data_item again will attempt to return the same data item). If
 * the returned data size is greater than max_data_item_size, then p_data_item
 * was not modified.
 *
 * Returns 0 if there are no more data items to read.
 *
 *   data_items             List of data items.
 *   p_data_item            Returned data item.
 *   max_data_item_size     Maximum size of data item to return.
 */
int fwtp_get_next_data_item(struct fwtp_data_item_list *data_items,
			    void *p_data_item, int max_data_item_size)
{
	struct fwtp_data_item *data_item;
	unsigned int data_item_size;
	unsigned int remaining_data_size;

	/*
	 * Get the total size of data remaining to be read. Return 0 if the end of the
	 * data has been reached.
	 */
	if ((unsigned int)(data_items->p_next_data_item -
			   data_items->data_buffer) >=
	    data_items->total_data_size) {
		return 0;
	}
	remaining_data_size =
		data_items->total_data_size -
		(data_items->p_next_data_item - data_items->data_buffer);

	/*
	 * Get the next data item. Return 0 if there's not enough data left for the
	 * entire data item.
	 */
	data_item = (struct fwtp_data_item *)data_items->p_next_data_item;
	data_item_size = sizeof(struct fwtp_data_item) + data_item->data_size;
	if (data_item_size > remaining_data_size)
		return 0;

	/* Return the data item and advance to the next data item in the list. */
	if (data_item->data_size <= max_data_item_size) {
		memcpy(p_data_item, data_item->data, data_item->data_size);
		data_items->p_next_data_item += data_item_size;
	}

	return data_item->data_size;
}
EXPORT_SYMBOL_GPL(fwtp_get_next_data_item);

/*
 * Looks up the string with the ID specified by string_id in the string table
 * specified by string_table and string_table_size. The offset specified by
 * string_table_offset is subtracted from the string ID before looking up in the
 * table.
 *
 * Returns the corresponding string if the ID is in the table; otherwise,
 * returns "OUT OF BOUNDS".
 *
 *   string_table           String table in which to lookup ID.
 *   string_table_size      Size of the string table.
 *   string_table_offset    Offset to the start of the string table.
 *   string_id              String ID to lookup.
 */
const char *fwtp_lookup_string(const char *string_table,
			       uint32_t string_table_size,
			       uint32_t string_table_offset, uint32_t string_id)
{
	static const char *const out_of_bounds_string = "OUT OF BOUNDS";
	uint32_t string_offset;

	/*
	 * If the string ID is less than the string table offset, it's out of bounds.
	 */
	if (string_id < string_table_offset)
		return out_of_bounds_string;

	/*
	 * If the starting string offset is past the end of the table, it's out of
	 * bounds.
	 */
	string_offset = string_id - string_table_offset;
	if (string_offset >= string_table_size)
		return out_of_bounds_string;

	/*
	 * If the string does not extend past the end of the table, it's in bounds.
	 * Otherwise, it's out of bounds.
	 */
	for (uint32_t i = string_offset; i < string_table_size; ++i) {
		if (string_table[i] == '\0')
			return string_table + string_offset;
	}

	return out_of_bounds_string;
}
