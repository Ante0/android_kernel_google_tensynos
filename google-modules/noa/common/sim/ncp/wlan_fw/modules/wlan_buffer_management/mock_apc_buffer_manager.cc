// This file is specifically for testing and isn't part of the
// production code, so it might not adhere to all the usual coding
// conventions.
#include "mock_apc_buffer_manager.h"

#include "wlan_buffer_manager.h"
#include "buffer_management_table.h"
#include "wlan_cast.h"
#include "wlan_log/wlan_log.h"
#include "sys_if/memory/sys_if_memory.h"

static struct {
	BufferManagerInfo bm_info;
	MockApcBmRefillCallback refill_callback;
	void *refill_callback_context;
} g_mock_bm_info;

static void MockBufferManagementTableDeinit(BufferManagementTableInfo *tbl_info)
{
	SysIfFree(WLAN_REINTERPRET_CAST(void *, tbl_info->refill_bitmap_addr));
	SysIfFree(WLAN_REINTERPRET_CAST(void *, tbl_info->bmes_addr));
	SysIfFree(tbl_info);
}

static int32_t CreateMockBufferManagementTable(BufferManagementTableInfo *tbl_info,
					       uint32_t entry_num, uint32_t pkt_offset)
{
	// Allocate memory for pending bitmap and buffer management entries
	uint32_t *refill_bitmap = WLAN_REINTERPRET_CAST(
		uint32_t *, SysIfAllocateDram(sizeof(uint32_t) * BITS_TO_UINT32S(entry_num)));
	BufferManagementEntry *bmes =
		WLAN_REINTERPRET_CAST(BufferManagementEntry *,
				      SysIfAllocateDram(sizeof(BufferManagementEntry) * entry_num));

	if (!refill_bitmap || !bmes) {
		if (refill_bitmap) {
			SysIfFree(refill_bitmap);
		}

		if (bmes) {
			SysIfFree(bmes);
		}

		return -ENOMEM;
	}

	memset(refill_bitmap, 0, BITS_TO_UINT32S(entry_num) * sizeof(uint32_t));
	memset(bmes, 0, sizeof(BufferManagementEntry) * entry_num);

	tbl_info->refill_bitmap_addr = WLAN_REINTERPRET_CAST(uint64_t, refill_bitmap);
	tbl_info->refill_bitmap_size = BITS_TO_UINT32S(entry_num) * sizeof(uint32_t);
	tbl_info->bmes_addr = WLAN_REINTERPRET_CAST(uint64_t, bmes);
	tbl_info->bmes_size = sizeof(BufferManagementEntry) * entry_num;
	tbl_info->entry_num = entry_num;
	tbl_info->pktid_offset = pkt_offset;

	return 0;
}

BufferManagerInfo *MockApcBmInit(MockApcBmInitParams *params)
{
	BufferManagerInfo *bm_info = &g_mock_bm_info.bm_info;

	memset(bm_info, 0, sizeof(BufferManagerInfo));

	g_mock_bm_info.refill_callback = params->refill_callback;
	g_mock_bm_info.refill_callback_context = params->refill_callback_context;

	if (params->apc_tx_buf_size) {
		BufferManagementTableInfo *table_info =
			WLAN_REINTERPRET_CAST(BufferManagementTableInfo *,
					      SysIfAllocateDram(sizeof(BufferManagementTableInfo)));
		if (!table_info) {
			goto APC_TX_TABLE_INFO_ALLOC_FAILED;
		}

		memset(table_info, 0, sizeof(BufferManagementTableInfo));

		bm_info->apc_tx_buffer_table_info_addr =
			WLAN_REINTERPRET_CAST(uint64_t, table_info);

		if (CreateMockBufferManagementTable(table_info, params->apc_tx_buf_size,
						    params->apc_tx_buf_pkt_offset) != 0) {
			goto APC_TX_TABLE_CREATE_FAILED;
		}
	}

	if (params->apc_rx_buf_size) {
		BufferManagementTableInfo *table_info =
			WLAN_REINTERPRET_CAST(BufferManagementTableInfo *,
					      SysIfAllocateDram(sizeof(BufferManagementTableInfo)));
		if (!table_info) {
			goto APC_RX_TABLE_INFO_ALLOC_FAILED;
		}

		memset(table_info, 0, sizeof(BufferManagementTableInfo));

		bm_info->apc_rx_buffer_table_info_addr =
			WLAN_REINTERPRET_CAST(uint64_t, table_info);

		if (CreateMockBufferManagementTable(table_info, params->apc_rx_buf_size,
						    params->apc_rx_buf_pkt_offset) != 0) {
			goto APC_RX_TABLE_CREATE_FAILED;
		}
	}

	if (params->noa_tx_buf_size) {
		BufferManagementTableInfo *table_info =
			WLAN_REINTERPRET_CAST(BufferManagementTableInfo *,
					      SysIfAllocateDram(sizeof(BufferManagementTableInfo)));
		if (!table_info) {
			goto NOA_TX_TABLE_INFO_ALLOC_FAILED;
		}

		memset(table_info, 0, sizeof(BufferManagementTableInfo));

		bm_info->noa_tx_buffer_table_info_addr =
			WLAN_REINTERPRET_CAST(uint64_t, table_info);

		if (CreateMockBufferManagementTable(table_info, params->noa_tx_buf_size,
						    params->noa_tx_buf_pkt_offset) != 0) {
			goto NOA_TX_TABLE_CREATE_FAILED;
		}
	}

	if (params->noa_rx_buf_size) {
		BufferManagementTableInfo *table_info =
			WLAN_REINTERPRET_CAST(BufferManagementTableInfo *,
					      SysIfAllocateDram(sizeof(BufferManagementTableInfo)));
		if (!table_info) {
			goto NOA_RX_TABLE_INFO_ALLOC_FAILED;
		}

		memset(table_info, 0, sizeof(BufferManagementTableInfo));

		bm_info->noa_rx_buffer_table_info_addr =
			WLAN_REINTERPRET_CAST(uint64_t, table_info);

		if (CreateMockBufferManagementTable(table_info, params->noa_rx_buf_size,
						    params->noa_rx_buf_pkt_offset) != 0) {
			goto NOA_RX_TABLE_CREATE_FAILED;
		}
	}

	return bm_info;

NOA_RX_TABLE_CREATE_FAILED:
	SysIfFree(WLAN_REINTERPRET_CAST(void *, bm_info->noa_rx_buffer_table_info_addr));
NOA_RX_TABLE_INFO_ALLOC_FAILED:
	MockBufferManagementTableDeinit(WLAN_REINTERPRET_CAST(
		BufferManagementTableInfo *, bm_info->noa_tx_buffer_table_info_addr));
NOA_TX_TABLE_CREATE_FAILED:
	SysIfFree(WLAN_REINTERPRET_CAST(void *, bm_info->noa_tx_buffer_table_info_addr));
NOA_TX_TABLE_INFO_ALLOC_FAILED:
	MockBufferManagementTableDeinit(WLAN_REINTERPRET_CAST(
		BufferManagementTableInfo *, bm_info->apc_rx_buffer_table_info_addr));
APC_RX_TABLE_CREATE_FAILED:
	SysIfFree(WLAN_REINTERPRET_CAST(void *, bm_info->apc_rx_buffer_table_info_addr));
APC_RX_TABLE_INFO_ALLOC_FAILED:
	MockBufferManagementTableDeinit(WLAN_REINTERPRET_CAST(
		BufferManagementTableInfo *, bm_info->apc_tx_buffer_table_info_addr));
APC_TX_TABLE_CREATE_FAILED:
	SysIfFree(WLAN_REINTERPRET_CAST(void *, bm_info->apc_tx_buffer_table_info_addr));
APC_TX_TABLE_INFO_ALLOC_FAILED:
	return NULL;
}

void MockApcBmDeinit(void)
{
	BufferManagerInfo *bm_info = &g_mock_bm_info.bm_info;

	if (bm_info->apc_tx_buffer_table_info_addr) {
		MockBufferManagementTableDeinit(WLAN_REINTERPRET_CAST(
			BufferManagementTableInfo *, bm_info->apc_tx_buffer_table_info_addr));
	}

	if (bm_info->apc_rx_buffer_table_info_addr) {
		MockBufferManagementTableDeinit(WLAN_REINTERPRET_CAST(
			BufferManagementTableInfo *, bm_info->apc_rx_buffer_table_info_addr));
	}

	if (bm_info->noa_tx_buffer_table_info_addr) {
		MockBufferManagementTableDeinit(WLAN_REINTERPRET_CAST(
			BufferManagementTableInfo *, bm_info->noa_tx_buffer_table_info_addr));
	}

	if (bm_info->noa_rx_buffer_table_info_addr) {
		MockBufferManagementTableDeinit(WLAN_REINTERPRET_CAST(
			BufferManagementTableInfo *, bm_info->noa_rx_buffer_table_info_addr));
	}
}

int32_t MockApcBmRefill(BufferTableType type, uint32_t len_buffer_info_list,
			const BufferInfo *buffer_info_list, bool to_dev)
{
	BufferManagementTableInfo *table_info;
	BufferManagementEntry *bme;
	const BufferInfo *buffer_info;
	uint32_t i;
	uint32_t entry_idx;

	if (!buffer_info_list) {
		return -EINVAL;
	}

	if (type == kBufferTableTypeApcTx) {
		table_info =
			WLAN_REINTERPRET_CAST(BufferManagementTableInfo *,
					      g_mock_bm_info.bm_info.apc_tx_buffer_table_info_addr);
	} else if (type == kBufferTableTypeApcRx) {
		table_info =
			WLAN_REINTERPRET_CAST(BufferManagementTableInfo *,
					      g_mock_bm_info.bm_info.apc_rx_buffer_table_info_addr);
	} else if (type == kBufferTableTypeNoaTx) {
		table_info =
			WLAN_REINTERPRET_CAST(BufferManagementTableInfo *,
					      g_mock_bm_info.bm_info.noa_tx_buffer_table_info_addr);
	} else if (type == kBufferTableTypeNoaRx) {
		table_info =
			WLAN_REINTERPRET_CAST(BufferManagementTableInfo *,
					      g_mock_bm_info.bm_info.noa_rx_buffer_table_info_addr);
	} else {
		return -EINVAL;
	}

	for (i = 0; i < len_buffer_info_list; i++) {
		buffer_info = &buffer_info_list[i];
		entry_idx = buffer_info->pktid - table_info->pktid_offset;

		if (entry_idx >= table_info->entry_num) {
			WLAN_LOG_ERROR(Bm, "%s(): invalid pktid: %" PRIu32, __func__,
				       buffer_info->pktid);
			continue;
		}

		bme = &(WLAN_REINTERPRET_CAST(BufferManagementEntry *,
					      table_info->bmes_addr)[entry_idx]);
		bme->buffer_addr_cpu = buffer_info->buffer_addr_cpu;
		bme->buffer_addr_phy = buffer_info->buffer_addr_phy;
		bme->buffer_size = buffer_info->buffer_size;
		bme->pktid = buffer_info->pktid;
		bme->ownership = kBufferOwnershipWlanFw;
		SET_BIT(table_info->refill_bitmap_addr, entry_idx);
	}

	table_info->wlan_sw_sync_request = 1;

	if (g_mock_bm_info.refill_callback && to_dev) {
		g_mock_bm_info.refill_callback(g_mock_bm_info.refill_callback_context);
	}

	return 0;
}
