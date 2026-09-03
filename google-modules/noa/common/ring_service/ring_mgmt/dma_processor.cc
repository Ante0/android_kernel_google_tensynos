// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifdef linux
#include "dma_processor.h"

#include <common/noatrace.h>
#include <common/core.h>

#include "ring_manager.h"
#include "ring_descriptor.h"
#include "ring_controller.h"
#include "dma_simulator.h"
#include "nep.h"
#include "dma_processor.h"
#else /* linux */
#include "ring_mgmt/dma_processor.h"

#include <cstdint>

#include "dma/dma.h"
#include "pw_status/status.h"
#include "ring_controller.h"

namespace DmaDrv = ::noa::driver::dma;
#endif /* linux */

#ifdef linux
static void dma_complete(void *context)
#else /* linux */
static void dma_complete(pw::Status status, int32_t transaction_id, void *context)
#endif /* linux */
{
	struct dma_processor *dma = (struct dma_processor *)context;
	if (!dma || !dma->processing_desc) {
		pr_err("Invalid dma context when DMA completed\n");
		return;
	}
	handle_desc_complete(dma->processing_desc);
#ifdef linux
	trace_ring_dma_transfer_completed(dma->channel, dma->processing_desc->src,
					  dma->processing_desc->dst);
#endif
}

#ifdef linux
static const u32 mask = 255U;
#else /* linux */
constexpr uint32_t g_burst_len = 16;
constexpr uint32_t g_burst_size = 16;
constexpr uint32_t mask = (g_burst_len * g_burst_size) - 1U;
#endif /* linux */
static inline uint32_t round_up_dma_unit_size(uint32_t size)
{
	return (((size) + (mask)) & ~(mask));
}

void configure_dma(int channel, void *driver)
{
#ifdef linux
#else /* linux */
	DmaDrv::Dma *dma = reinterpret_cast<DmaDrv::Dma *>(driver);
	DmaDrv::DmaChannelConfiguration config = {
		.burst_size = {g_burst_size},
		.burst_length = {g_burst_len},
		.num_transfer_units = 1,
	};

	dma->ConfigureChannel(channel, config);
#endif /* linux */
}

int perform_dma_copy(unsigned long dst_addr, unsigned long src_addr, u32 size,
		     struct dma_processor *dma_data)
{
#ifdef linux

	struct dma_request req = {
		.destination_address = dst_addr,
		.source_address = src_addr,
		.size = round_up_dma_unit_size(size),
		.channel = dma_data->channel,
		.callback = dma_complete,
		.context = dma_data,
	};
	return queue_dma_request(req);
#else /* linux */
	noa::driver::dma::DmaTransferUnit unit = {
		.destination_address = dst_addr,
		.source_address = src_addr,
		.size = round_up_dma_unit_size(size),
	};
	DmaDrv::DmaTransferRequest request = {
		.dma330_transfer_unit = { unit },
		.num_transfer_units = 1,
		.channel = dma_data->channel,
		.callback = dma_complete,
		.context = dma_data,
	};
	reinterpret_cast<DmaDrv::Dma *>(dma_data->driver)->InitiateTransfer(request);
	return 0;
#endif /* linux */
}
