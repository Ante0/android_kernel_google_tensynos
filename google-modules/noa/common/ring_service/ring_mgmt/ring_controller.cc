// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifdef linux
#include "ring_controller.h"
#include <common/noatrace.h>
#include <common/ring.h>
#include <common/inttypes.h>
#include "nep.h"
#include "ring_descriptor.h"
#include "ring_manager_instance.h"
#include "util/ring_util.h"
#else
#include <cinttypes>

#include <sys/errno.h>
#include "dma_processor.h"
#include "ring_mgmt/port.h"
#include "ring_mgmt/port_instance.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "ring_controller.h"
#include "ring_descriptor.h"
#include "ring_util.h"
#endif

static void noa_dma_stat_update(struct noa_desc *in)
{
	const uint8_t output_port = NoaRingOutputPathIdToPort(in->dst);
	const uint8_t netengine_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineRingData);
	get_nep_stat()->mode[in->mode]++;
	if (in->mode == NOAD_MODE_DATA) {
		if (in->src == netengine_path)
			get_nep_stat()->reason[in->reason]++;
		else
			get_nep_stat()->reason[FWD_REASON_FEEDTHROUGH]++;
	}
	if (output_port < NOA_PORT_MAX) {
		get_nep_stat()->dst[output_port]++;
	}
}

void handle_desc_complete(struct noa_desc *desc)
{
	struct noa_ring_wrapper *src = NoaRingManagerRingGetById(desc->src, kNoaRingNepInput);
	struct noa_ring_wrapper *dst = NoaRingManagerRingGetById(desc->dst, kNoaRingNepOutput);

	if (!src || !dst) {
		dev_err(get_ring_device(), "Invalid src %d or dst %d ports\n", desc->src,
			desc->dst);
		return;
	}
	// Update ring index & notify ring  based on desc
	desc->ddone = 1;
	noa_ring_complete_processing(src);
	noa_ring_complete_processing(dst);
	noa_dma_stat_update(desc);
}

/*
 * Forwared pkt to destion ring of Port
 *
 * return: 1: success
 *         0: resource busy, try again
 *        <0: error
 */
static int noa_output_task(noa_ring_producer *output_ring, void *data, size_t data_len,
			   bool is_notify_ring)
{
	int ret;

	ret = noa_ring_begin_processing(output_ring);
	if (ret <= 0)
		return ret;

	ret = noa_ring_write_variable_length(output_ring, data, data_len);
	if (ret < 0) {
		if (ret == -EAGAIN)
			ret = 0;
		else
			dev_err(get_ring_device(), "Failed to write data to dst ring %s, err: %d\n",
				output_ring->name, ret);
		goto out;
	}
	ret = 1;

out:
	if (is_notify_ring) {
		noa_ring_complete_processing(output_ring);
		/* update counter */
		noa_dma_stat_update((struct noa_desc *)data);
	} else if (ret <= 0) {
		noa_ring_complete_processing(output_ring);
	}
	return ret;
}

/*
 * return: 1: success
 *         0: resource busy, try again
 *        <0: error
 */
static int noa_send_feedback_event(struct noa_desc *desc)
{
	int ret;
	uint8_t feedback_ring_id = NoaFeedbckPathIdFromDataPath(desc->src);
	noa_ring_producer *output_ring =
		NoaRingManagerRingGetById(feedback_ring_id, kNoaRingNepOutput);
	struct noa_desc feedback = {
		.mode = NOAD_MODE_FEEDBACK,
		.dst = feedback_ring_id,
		.ddone = 1,
		.tkid = desc->tkid,
		.reason = desc->reason,
		.src = NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel,
					    kNoaNetengineRingData),
		.desc_type = NOA_DESC_BASIC,
		.cp = NOAD_NO_COPY,
	};

#ifdef linux
	trace_ring_port_feedback(NoaRingOutputPathIdToPort(feedback_ring_id));
#endif
	if (!output_ring) {
		dev_err(get_ring_device(),
			"Invalid dst ring %d to send feedback event, drop this desc\n",
			feedback_ring_id);
		ret = -EINVAL;
		goto out;
	}

	// Write feedback packet to ring & notify
	ret = noa_output_task(output_ring, &feedback, sizeof(feedback),
			      true /* update index & notify */);
out:
	return ret;
}

static void input_ring_task(noa_ring_consumer *input_ring, uint8_t src_ring_id, uint8_t isr_port_id)
{
	int i;
	int ret;

	if (!input_ring) {
		dev_err(get_ring_device(), "Invalid ring id %d when handling ring ISR",
			src_ring_id);
		goto out;
	}

	ret = noa_ring_begin_processing(input_ring);
	if (ret <= 0)
		goto out;

	/* We should limit the max handled desc per task */
	for (i = 0; i < MAX_HANDLED_COUNT; ++i) {
		struct noa_desc *in_desc;
		struct ring_manager_instance *dst_instance;
		unsigned long data_addr;
		ssize_t read_size;

		read_size = noa_ring_read_variable_length(input_ring, &data_addr, sizeof(data_addr),
							  true);
		if (!read_size) {
			// ring is empty.
			break;
		} else if (read_size < 0 || !data_addr) {
			dev_err(get_ring_device(),
				"Failed to read data from src ring %s, drop this item, err: %zd\n",
				input_ring->name, read_size);
			noa_ring_tail_inc(input_ring);
			continue;
		}

		in_desc = (struct noa_desc *)data_addr;
#ifdef linux
		trace_ring_descriptor_receive(in_desc);
#endif
		/* Handle feedback event:
		 *
		 * The packet src did not be changed afrer netengine processed the packet.
		 * So ring manager should update src for the packet after feedback packet is sent.
		 * Note: Feedback packet should be updated after NE is handled with "reason".
		 */
		if (in_desc->fk && isr_port_id == NOA_PORT_NETENGINE) {
			ret = noa_send_feedback_event(in_desc);
			if (!ret) {
				/* dst ring is busy, rollback the `tail` pos in input ring */
				noa_ring_rollback(input_ring,
						  DIV_ROUND_UP(read_size,
							       input_ring->basic.item_len));
				break;
			} else if (ret < 0) {
				dev_err(get_ring_device(),
					"Failed to send feedback packet err: %d, "
					"but try to send desc to dst.\n",
					ret);
			}
			// Update src for netengine packet
			in_desc->fk = 0; // always reset fk since it was done.
		}
		/* Update in->src to align src port */
		in_desc->src = src_ring_id;
		get_nep_stat()->src[isr_port_id]++;

		dst_instance = NoaRingManagerInfoInstanceGetById(in_desc->dst, kNoaRingNepOutput);
		if (!dst_instance) {
			dev_err(get_ring_device(),
				"Invalid dst ring id %" PRIx8
				" from src ring %s desc, drop this desc\n",
				in_desc->dst, input_ring->name);
			continue;
		}
		ret = noa_output_task(&dst_instance->ring, (void *)data_addr,
				      noa_desc_bytes(noa_desc_type_parse(in_desc)), !in_desc->cp);
		if (!ret) {
			/* dst ring is busy, rollback the `tail` pos in input ring */
			noa_ring_rollback(input_ring,
					  DIV_ROUND_UP(read_size, input_ring->basic.item_len));
			break;
		} else if (ret < 0) {
			dev_err(get_ring_device(),
				"Failed to send packet err: %d, drop this desc!\n", ret);
			continue;
		}

		if (in_desc->cp) {
			if (!dst_instance->dma.src || !dst_instance->dma.dst) {
				dev_err(get_ring_device(),
					"Invalid dst 0x%lx, src 0x%lx, size %d\n",
					dst_instance->dma.dst, dst_instance->dma.src,
					dst_instance->dma.size);
			}
			perform_dma_copy(dst_instance->dma.dst, dst_instance->dma.src,
					 dst_instance->dma.size, &dst_instance->dma);
			get_nep_stat()->cp[isr_port_id]++;
#ifdef linux
			trace_ring_dma_transfer_start(dst_instance->dma.channel, in_desc->src,
						      in_desc->dst);
#endif /* linux */
			goto out;
		}
	}

	noa_ring_complete_processing(input_ring);

out:
	if (!noa_ring_is_empty(input_ring)) {
#ifdef linux
		trace_ring_port_reschedule(isr_port_id);
#endif
		notify_ring_manager(isr_port_id);
	}
}

void NoaRingServiceHandleIsr(struct noa_port *isr_port)
{
	uint8_t interface;
	uint8_t flow;
	uint32_t bitmap;
	uint8_t i;

	if (!isr_port) {
		return;
	}

	interface = isr_port->interface_of_rings;
	flow = isr_port->flow_of_rings;
	bitmap = isr_port->rings_bitmap;

	for (i = 0; i <= NOA_RING_CATEGORY_MASK; ++i) {
		noa_ring_consumer *ring;
		if (!(bitmap & (1 << i))) {
			continue;
		}
		ring = NoaRingManagerRingGet(interface, flow, i, kNoaRingNepInput);
		if (!ring) {
			pr_err("Invalid ring with %u %u %u\n", interface, flow, i);
			continue;
		}
		input_ring_task(ring, NoaRingPathIdConvert(interface, flow, i), isr_port->idx);
	}
}
