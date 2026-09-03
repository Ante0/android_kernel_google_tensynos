// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of NEP Pipeline Packet Table
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>

#include "packet_table.h"
#else /* linux */
#include "network_pipeline_framework/packet_table.h"

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "linux_port/log.h"
#include "linux_port/bitops.h"
#endif /* linux */

void NepPacketTableInit(struct NepPacketTable *table)
{
	if (!table) {
		return;
	}
	table->free_count = table->size;
	table->buffer_recycle = NULL;
	table->buffer_recycle_handler = NULL;
	bitmap_fill(table->free_bitmap, table->size);
	memset(table->arr, 0, table->size * sizeof(struct NepPacketContext));
}

int32_t NepPacketTableFreePacketAcquire(struct NepPacketTable *table, uint16_t *pkt_id)
{
	unsigned long index;
	uint16_t free_id;

	if (!table) {
		return -EINVAL;
	} else if (NepPacketTableIsEmpty(table)) {
		return -EAGAIN;
	}

	index = find_first_bit(table->free_bitmap, table->size);
	free_id = (uint16_t)index + 1;
	if (index == table->size) {
		return -EFAULT;
	}
	table->free_count--;
	clear_bit(index, table->free_bitmap);
	*pkt_id = (uint16_t)free_id;
	return 0;
}

void NepPacketTablePacketFree(struct NepPacketTable *table, uint16_t pkt_id)
{
	int32_t ret;
	uint16_t index = pkt_id - 1;

	if (!table) {
		return;
	}
	if (index >= table->size) {
		return;
	}

	if (table->buffer_recycle) {
		ret = table->buffer_recycle(table, pkt_id, table->buffer_recycle_handler);
		if (ret) {
			return;
		}
	}

	if (unlikely(test_bit(index, table->free_bitmap))) {
		pr_warn("This packet %" PRIu16 "has been double freed\n", pkt_id);
	} else {
		set_bit(index, table->free_bitmap);
		table->free_count++;
	}
}

void NepPacketTableReset(struct NepPacketTable *table)
{
	if (!table) {
		return;
	}
	// TODO(b/367865490) - Free the packet's buffer in pkt context
	NepPacketTableInit(table);
}

bool NoPacketSendingToDestination(struct NepPacketTable *table, uint8_t dst)
{
	int i;
	if (table->size == table->free_count) {
		return true;
	}
	for (i = 0; i < table->size; ++i) {
		struct NepPacketContext *packet = &(table->arr[i]);
		if (!NepHasBufferToRecycled(packet, kUseOriginalPacket)) {
			continue;
		}
		if (!test_bit(i, table->free_bitmap) && table->arr[i].dst == dst) {
			return false;
		}
	}
	return true;
}
