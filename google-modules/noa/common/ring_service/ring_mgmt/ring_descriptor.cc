// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ring operation modeule.
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifdef linux
#include "ring_descriptor.h"
#include "linux/dev_printk.h"
#include "util/ring_util.h"
#include "ring_service/ring_buffer_pool.h"
#include "ring_manager_instance.h"
#include "common/noa_share/types.h"
#include "nep.h"
#include "common/noatrace.h"
#include "common/inttypes.h"
#else
#include <cerrno>
#include <cinttypes>

#include "ring_descriptor.h"
#include "ring_buffer_pool.h"
#include "ring_util.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "linux_port/noa_share/types.h"
#endif

static int noa_desc_cp_handle(const struct noa_desc *in, struct noa_desc *out)
{
	const uint8_t output_port_id = NoaRingOutputPathIdToPort(in->dst);
	struct ring_manager_instance *instance =
		NoaRingManagerInfoInstanceGetById(in->dst, kNoaRingNepOutput);
	noa_buffer_pool_desc buffer_desc;
	int32_t ret = noa_ring_service_buffer_get(output_port_id, &buffer_desc);
	if (ret) {
		if (ret != -EAGAIN) {
			dev_err(get_ring_device(),
				"Failed to get buffer from dst ring %" PRIx8 ", err %" PRIu32 "\n",
				in->dst, ret);
		}
		return ret;
	}
	if (!instance) {
		return -EINVAL;
	}
	/* packet copy into net engine should keep tkid for feedback event. */
	if (in->cp == NOAD_COPY_DATA_AND_RENEW_TKID) {
		out->tkid = buffer_desc.tkid;
	} else {
		out->tkid = in->tkid;
	}
	out->dv = buffer_desc.dv;
	out->dp_low = buffer_desc.dp_low;
	out->dp_high = buffer_desc.dp_high;
	out->ver = 0;
	out->mode = NOAD_MODE_DATA;
	instance->dma.processing_desc = NOA_CONST_CAST(struct noa_desc *, in);
#ifdef linux
	instance->dma.src = in->dv;
	instance->dma.dst = out->dv;
#else
	instance->dma.src = static_cast<uint32_t>(in->dv);
	instance->dma.dst = static_cast<uint32_t>(out->dv);
#endif /* linux */
	instance->dma.size = in->dl;
	return 0;
}

static int ring_tx_handle(const struct noa_desc *in, struct noa_desc *out)
{
	/* handle DMA control fields in descriptor. */
	if (in->cp) {
		/* configure DMA to copy data buffer to destination */
		/* destination ring should provide the RX buffer */
		int ret = noa_desc_cp_handle(in, out);
		if (ret) {
			return ret;
		}
		return 0;
	}
	/* Finally, set ddone bit as 1 when complete to write descriptor */
	out->ddone = 1;
	return 0;
}

ssize_t noa_ring_manager_desc_transfer(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	int ret;
	const struct noa_desc *in = (const struct noa_desc *)data;
	struct noa_desc *out = (struct noa_desc *)buf;
	(void)buf_len;

	memcpy(buf, data, data_len);
	ret = ring_tx_handle(in, out);
	if (ret) {
		return ret;
	}
#ifdef linux
	trace_ring_descriptor_transmit(out);
#endif
	return data_len;
}

ssize_t noa_ring_manager_payload_parser(const void *data)
{
	int type;

	if (noa_desc_is_noop((const struct noa_desc *)data))
		return 0;

	type = noa_desc_type_parse((const struct noa_desc *)data);
	if (type >= NOA_DESC_TYPE_MAX) {
		dev_err(get_ring_device(), "Ring Service handle an invalid descriptor type %d\n",
			type);
		return -EINVAL;
	}

	return noa_desc_bytes(type);
}

int noa_ring_manager_noop_payload(void *buf, size_t buf_len)
{
	struct noa_desc *desc = (struct noa_desc *)buf;
	if (buf_len < sizeof(struct noa_desc))
		return -EINVAL;
	noa_desc_mark_noop(desc);
	return 0;
}
