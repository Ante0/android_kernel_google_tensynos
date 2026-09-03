/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#include <linux/types.h>

struct dma_request {
	unsigned long destination_address;
	unsigned long source_address;
	u32 size;
	u16 channel;
	void (*callback)(void *context);
	void *context;
};

#define MAX_DMA_CACHE_DESC_NUM (2U)

typedef struct {
	void (*callback)(void *context);
	void *context;
	uint8_t num;
	uint16_t desc_len;
	struct {
		uint16_t desc_num;
		uintptr_t src;
		uintptr_t dst;
	} unit[MAX_DMA_CACHE_DESC_NUM];
} dma_cache_desc_request;

#define DMA_CACHE_BUFFER_POOL_DESC_NUM (16U)
typedef struct {
	void (*callback)(void *context);
	void *context;
	uintptr_t src;
	uintptr_t dst;
} dma_cache_buffer_pool_request;

#define MAX_FETCH_HEADER_NUM (8U)
typedef struct {
	void (*callback)(void *context);
	void *context;
	uint8_t num;
	struct {
		uintptr_t src;
		uintptr_t dst;
	} unit[MAX_FETCH_HEADER_NUM];
} dma_fetch_header_request;

#define MAX_DMA_ROUTE_DESC_NUM (8U)
#define MAX_DMA_ROUTE_FORWARD_PACKET_NUM (2U)

typedef struct {
	void (*callback)(void *context);
	void *context;
	uint8_t num;
	uint16_t desc_len;
	struct {
		uintptr_t src;
		uintptr_t dst;
	} unit[MAX_DMA_ROUTE_DESC_NUM];
	uintptr_t src_ring_addr;
	uint32_t src_ring_tail;
	uintptr_t dst_ring_addr;
	uint32_t dst_ring_head;
} dma_route_data_request;

typedef struct {
	void (*callback)(void *context);
	void *context;
	uintptr_t src_ring_addr;
	uint32_t src_ring_tail;
} dma_drop_data_request;

typedef struct {
	void (*callback)(void *context);
	void *context;
	uint8_t num;
	uint16_t forward_desc_len;
	struct {
		uint16_t header_len;
		uint16_t payload_len;
		uintptr_t header_src;
		uintptr_t payload_src;
		uintptr_t pkt_dst;
		uintptr_t forward_desc;
		uintptr_t forward_dst;
		uintptr_t feedback_desc;
		uintptr_t feedback_dst;
	} forward_unit[MAX_DMA_ROUTE_DESC_NUM];
	uintptr_t src_ring_addr;
	uint32_t src_ring_tail;
	uintptr_t dst_ring_addr;
	uint32_t dst_ring_head;
	uintptr_t feedback_ring_addr;
	uint32_t feedback_ring_head;
	uintptr_t buffer_ring_addr;
	uint32_t buffer_ring_tail;
} dma_route_forward_pkt_request;

int start_dma_simulator(void);
void stop_dma_simulator(void);
int queue_dma_request(const struct dma_request request);
int queue_dma_cache_desc_request(const dma_cache_desc_request *request);
int queue_dma_cache_buffer_pool_request(const dma_cache_buffer_pool_request *request);
int queue_dma_fetch_header_request(const dma_fetch_header_request *request);
int queue_dma_route_data_request(const dma_route_data_request *request);
int queue_dma_drop_data_request(const dma_drop_data_request *request);
int queue_dma_route_forward_pkt_request(const dma_route_forward_pkt_request *request);
