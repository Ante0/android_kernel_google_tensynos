// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifdef linux
#include "ring_manager.h"
#include "ring_shared_info.h"

#include <linux/errno.h>

#include <common/ring.h>
#include <common/inttypes.h>
#include <common/noa_ring_id.h>
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
#include <soc/google/google_dpa_ring_service_proxy.h>
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#else /* linux */
#include "ring_mgmt/ring_manager.h"
#include "ring_mgmt/ring_shared_info.h"

#include <cerrno>
#include <cinttypes>

#include "common/ring.h"
#include "common/noa_ring_id.h"
#endif /* linux */

// NOA ring shared info area
//-----------------------------------------
// Base   | defined in ring_manager.h
//-----------------------------------------
// OffSet | Area
//-----------------------------------------
// 0x0    | Common Info Area (32 bytes)
//-----------------------------------------
// 0x100  | Producer Area (32 bytes)
//-----------------------------------------
// 0x200  | Consumer Area (32 bytes)
//-----------------------------------------
// 0x300  | Ring Info Area (384 bytes)
//        | 0x300 ~ 0x900 for input ring
//        | 0x900 ~ 0xf00 for output ring
//-----------------------------------------
// 0xf00  | End of Ring Info Area

static struct ring_shared_info *shared_info = NULL;
static const struct NoaRingSharedInfoRoot *g_shared_info_root = NULL;
static struct noa_nep_stat nep_stat;
struct device *ring_device;

static int8_t get_ring_index(uint32_t port_id, RingType type)
{
	uint32_t ring_index = type == INPUT ? 0 : NOA_PORT_MAX;
	switch (port_id) {
	case NOA_PORT_WLAN_SW:
		return ring_index;
	case NOA_PORT_MODEM_SW:
		return ring_index + 1;
	case NOA_PORT_WLAN_FW:
		return ring_index + 2;
	case NOA_PORT_MODEM_FW:
		return ring_index + 3;
	case NOA_PORT_NETENGINE:
		return ring_index + 4;
	}
	return -ENODEV;
}

static void init_ring_for_port(uint32_t port_id, RingType type)
{
	struct noa_ring r;
	int8_t ring_index = get_ring_index(port_id, type);
	if (ring_index < 0 || !shared_info) {
		return;
	}
	r.base = 0;
	r.write = 0;
	r.read = 0;
	r.ctrl = 0;
	r.len = 0;
	r.dpa_base = 0;
	shared_info->ring_infos[ring_index] = r;
}

void noa_ring_shared_info_initialization(void)
{
	if (!shared_info) {
		return;
	}
	memset(shared_info->ring_infos, 0, shared_info->size);
}

int noa_ring_shared_info_register(struct ring_shared_info *info)
{
	if (!info) {
		return -EINVAL;
	}
	shared_info = info;
	return 0;
}

void rings_initialization(struct device *dev)
{
	int port_id, ring_type;
	for (port_id = 0; port_id < NOA_PORT_MAX; port_id++) {
		for (ring_type = 0; ring_type < RING_TYPE_MAX; ring_type++) {
			init_ring_for_port(port_id, (RingType)ring_type);
		}
	}
	ring_device = dev;
}

struct noa_ring *get_ring_info(uint8_t port_id, RingType ring_type)
{
	int8_t ring_index = get_ring_index(port_id, ring_type);
	if (ring_index < 0)
		return NULL;
	return &shared_info->ring_infos[ring_index];
}
#ifdef linux
EXPORT_SYMBOL_GPL(get_ring_info);
#endif

int noa_ring_manager_regs_get(struct noa_ring_regs *regs, uint8_t port, RingType type)
{
	struct noa_ring *info = get_ring_info(port, type);
	if (!info)
		return -EINVAL;
	regs->base = (unsigned long)&info->base;
	regs->max_item = (unsigned long)&info->ctrl;
	regs->len = (unsigned long)&info->len;
	regs->read = (unsigned long)&info->read;
	regs->write = (unsigned long)&info->write;
	regs->dpa_base = (unsigned long)&info->dpa_base;
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ring_manager_regs_get);
#endif /* linux */

int NoaRingSharedInfoRootRegister(const struct NoaRingSharedInfoRoot *root)
{
	if (!root) {
		return -EINVAL;
	}
	g_shared_info_root = root;

	return 0;
}

/* Get ring shared info through the new flattened id system */
static struct noa_ring *NoaRingSharedInfoGetFlattened(uint8_t ring_id)
{
	if (!g_shared_info_root) {
		pr_err("%s(): Failed to get ring shared info: g_shared_info_root is NULL\n",
		       __func__);
		return NULL;
	}

	return &g_shared_info_root->base->entries[ring_id];
}

/* Get shared info API for the old 4D id system */
struct noa_ring *NoaRingSharedInfoGet(uint8_t interface, uint8_t flow, uint8_t category,
				      uint8_t direction)
{
	if (!g_shared_info_root) {
		return NULL;
	}

	/* For debugging purpose */
	if (g_shared_info_root->get_ring) {
		return g_shared_info_root->get_ring(g_shared_info_root, interface, flow, category,
						    direction);
	}
	uint8_t ring_id = NoaRingIdMapping(interface, flow, category, direction);
        if (ring_id <= 0) {
                pr_err("%s(): NoaRingIdMapping returned 0 or negative\n", __func__);
                return NULL;
        }

	return NoaRingSharedInfoGetFlattened(ring_id);
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingSharedInfoGet);
#endif /* linux */

/**
 * @brief A helper function to update the given noa_ring
 *        pointer with the info in the given noa_ring_regs
 *
 *        Consider merge this function into NoaDpaRingSharedRegsGet
 *        after NoaRingSharedRegsGet is deprecated
 *
 * @param[in] info - a registered ring shared info
 * @param[out] regs - an address to the ring register
 *
 * @return 0 on success, negative error code on failure
 */
static int32_t NoaRingSharedRegsUpdate(struct noa_ring *info, struct noa_ring_regs *regs)
{
	if (!regs || !info) {
		pr_err("%s(): Invalid noa_ring_regs or noa_ring\n", __func__);
		return -EINVAL;
	}
	regs->base = (unsigned long)&info->base;
	regs->max_item = (unsigned long)&info->ctrl;
	regs->len = (unsigned long)&info->len;
	regs->read = (unsigned long)&info->read;
	regs->write = (unsigned long)&info->write;
	regs->dpa_base = (unsigned long)&info->dpa_base;
	return 0;
}
#ifdef linux
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
/**
 * @brief Temporary Helper function for NoaDpaRingSharedRegsGet
 *        Intended for new ID system
 */
static int32_t NoaDpaRingSharedRegsGetFlattened(struct google_dpa *dpa, struct noa_ring_regs *regs,
						uint8_t ring_id)
{
	struct noa_ring *info = (struct noa_ring *)google_dpa_ring_shared_info_get(dpa, ring_id);

	return NoaRingSharedRegsUpdate(info, regs);
}

/**
 * @brief Get ring shared register information from AP
 *        thru kernel driver
 *        Temporary API for the old 4D interface
 *
 * @param[in] dpa - a user must provide google_dpa to fetch desired data
 * @param[out] regs - an address to the ring shared register
 *
 * @return 0 on success, negative error code on failure
 */
int32_t NoaDpaRingSharedRegsGet(struct google_dpa *dpa, struct noa_ring_regs *regs,
				uint8_t interface, uint8_t flow, uint8_t category,
				uint8_t direction)
{
	uint8_t ring_id = NoaRingIdMapping(interface, flow, category, direction);
	return NoaDpaRingSharedRegsGetFlattened(dpa, regs, ring_id);
}
EXPORT_SYMBOL_GPL(NoaDpaRingSharedRegsGet);
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
#endif /*linux */

int32_t NoaRingSharedRegsGet(struct noa_ring_regs *regs, uint8_t interface, uint8_t flow,
			     uint8_t category, uint8_t direction)
{
	struct noa_ring *info = NoaRingSharedInfoGet(interface, flow, category, direction);

	return NoaRingSharedRegsUpdate(info, regs);
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingSharedRegsGet);
#endif /* linux */

void NoaRingSharedInfoInit(void)
{
	struct noa_ring *entries;

	if (!g_shared_info_root || !g_shared_info_root->base) {
		return;
	}
	entries = g_shared_info_root->base->entries;

	for (size_t i = 0; i < NOA_NEP_RING_MAX; ++i) {
		struct noa_ring *ring = &entries[i];
		memset(ring, 0, sizeof(*ring));
	}
}

struct device *get_ring_device(void)
{
	return ring_device;
}
#ifdef linux
EXPORT_SYMBOL_GPL(get_ring_device);
#endif /* linux */

struct noa_nep_stat *get_nep_stat(void)
{
	return &nep_stat;
}
#ifdef linux
EXPORT_SYMBOL_GPL(get_nep_stat);
#endif /* linux */

#define NOA_RING_NAMES \
        NOA_RING_NAME_MAPPING(kNoaUnknownRingInToNep, "UnknownRingInToNep") \
        NOA_RING_NAME_MAPPING(kNoaWlanH2DTxDataInToNep, "WlanH2DTxDataInToNep") \
        NOA_RING_NAME_MAPPING(kNoaWlanH2DRefillRxInToNep, "WlanH2DRefillRxInToNep") \
        NOA_RING_NAME_MAPPING(kNoaWlanD2HRxDataInToNep, "WlanD2HRxDataInToNep") \
        NOA_RING_NAME_MAPPING(kNoaWlanD2HNepBufferPoolInToNep, "WlanD2HNepBufferPoolInToNep") \
        NOA_RING_NAME_MAPPING(kNoaModemH2DTxDataInToNep, "ModemH2DTxDataInToNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HRxDataInToNep, "ModemD2HRxDataInToNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HRxq0InToNep, "ModemD2HRxq0InToNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HRxq1InToNep, "ModemD2HRxq1InToNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HRxq2InToNep, "ModemD2HRxq2InToNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HNepBufferPoolInToNep, "ModemD2HNepBufferPoolInToNep") \
        NOA_RING_NAME_MAPPING(kNoaNetengineH2DTunnelInToNep, "NetengineH2DTunnelInToNep") \
        NOA_RING_NAME_MAPPING(kNoaNetengineBufferPoolInToNep, "NetengineBufferPoolInToNep") \
        NOA_RING_NAME_MAPPING(kNoaNetworkStackInToNep, "NetworkStackInToNep") \
        NOA_RING_NAME_MAPPING(kNoaWlanH2DTxDataOutOfNep, "WlanH2DTxDataOutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaWlanH2DRefillRxOutOfNep, "WlanH2DRefillRxOutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaWlanD2HRxDataOutOfNep, "WlanD2HRxDataOutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaModemH2DTxDataOutOfNep, "ModemH2DTxDataOutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HRxDataOutOfNep, "ModemD2HRxDataOutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HRxq0OutOfNep, "ModemD2HRxq0OutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HRxq1OutOfNep, "ModemD2HRxq1OutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaModemD2HRxq2OutOfNep, "ModemD2HRxq2OutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaNetengineH2DTunnelOutOfNep, "NetengineH2DTunnelOutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaNetworkStackOutOfNep, "NetworkStackOutOfNep") \
        NOA_RING_NAME_MAPPING(kNoaWlanH2DTxDataApNcpDirect, "WlanH2DTxDataApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaWlanH2DVendorRxBufferReplenishApNcpDirect, "WlanH2DVendorRxBufferReplenishApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaWlanH2DNoaTxBufferReplenishApNcpDirect, "WlanH2DNoaTxBufferReplenishApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaWlanH2DFeedbackApNcpDirect, "WlanH2DFeedbackApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaWlanD2HRxDataApNcpDirect, "WlanD2HRxDataApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaWlanD2HTxCplApNcpDirect, "WlanD2HTxCplApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaWlanD2HRxFallbackApNcpDirect, "WlanD2HRxFallbackApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpTxDrb0ApNcpDirect, "ModemApcNcpTxDrb0ApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpTxDrb1ApNcpDirect, "ModemApcNcpTxDrb1ApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpTxDrb2ApNcpDirect, "ModemApcNcpTxDrb2ApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpTxDrb3ApNcpDirect, "ModemApcNcpTxDrb3ApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpTxDrb4ApNcpDirect, "ModemApcNcpTxDrb4ApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpRxRefillNormalBat0ApNcpDirect, "ModemApcNcpRxRefillNormalBat0ApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpRxRefillFragBat0ApNcpDirect, "ModemApcNcpRxRefillFragBat0ApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpRxRefillNormalBat1ApNcpDirect, "ModemApcNcpRxRefillNormalBat1ApNcpDirect") \
        NOA_RING_NAME_MAPPING(kNoaModemApcNcpRxRefillFragBat1ApNcpDirect, "ModemApcNcpRxRefillFragBat1ApNcpDirect") \

static const char* NoaRingNameGet(int8_t ring_id) {
        if (ring_id >= kNoaWlanD2HTxCplInToNep && ring_id < kNoaWlanD2HTxCplInToNepEnd) {
                static char dynamic_name[64];
                snprintf(dynamic_name, sizeof(dynamic_name), "WlanD2HTxCplInToNep_%d", ring_id - kNoaWlanD2HTxCplInToNep);
                return dynamic_name;
        }

        if (ring_id >= kNoaWlanD2HTxCplOutOfNep && ring_id < kNoaWlanD2HTxCplOutOfNepEnd) {
                static char dynamic_name[64];
                snprintf(dynamic_name, sizeof(dynamic_name), "WlanD2HTxCplOutOfNep_%d", ring_id - kNoaWlanD2HTxCplOutOfNep);
                return dynamic_name;
        }

        switch (ring_id) {
                #define NOA_RING_NAME_MAPPING(id, name) case id: return #name;
                NOA_RING_NAMES
                #undef NOA_RING_NAME_MAPPING

                default: return "UnKnown";
        }
}

int32_t NoaRingSharedInfoDumpAll(char *buf, int32_t len)
{
	struct NoaRingSharedInfoRootBase *root =
		g_shared_info_root ? g_shared_info_root->base : NULL;
	uint8_t ring_id;
	int32_t size = 0;
	int32_t offset = 0;

	if (!root) {
		pr_err("Shared info root not registered\n");
		return -EINVAL;
	}

	size = snprintf(buf + offset, len, "Dumping all NOA shared rings:\n");
	if (size < 0) {
		return size;
	}
	offset += size;
	len -= size;

	struct noa_ring *ring;
	const char *ring_name = "Unknown";
	for (ring_id = 0; ring_id < NOA_NEP_RING_MAX; ++ring_id) {
		if (ring_id == kNoaUnknownRing) {
			continue;
		}

		ring = NoaRingSharedInfoGetFlattened(ring_id);
		ring_name = NoaRingNameGet(ring_id);

		// each ring_id (index) is matched with exactly one ring
		if (!ring) {
			size = snprintf(buf + offset, len, "Ring %s UNAVAILABLE\n", ring_name);
		} else {
			size = snprintf(buf + offset, len,
					"Ring %s base 0x%" PRIxPTR ", dpa_base 0x%" PRIxPTR
					", item_len %" PRIu32 ", size %" PRIu32 ", read %" PRIu32
					", write %" PRIu32 "\n",
					ring_name, (uintptr_t)ring->base, (uintptr_t)ring->dpa_base,
					ring->len, ring->ctrl, ring->read, ring->write);
		}
		if (size < 0) {
			continue;
		}
		offset += size;
		len -= size;
	}
	return offset;
}
#ifdef linux
EXPORT_SYMBOL_GPL(NoaRingSharedInfoDumpAll);
#endif /* linux */
