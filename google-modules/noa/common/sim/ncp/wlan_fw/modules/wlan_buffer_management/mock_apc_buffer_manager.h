// This file is specifically for testing and isn't part of the
// production code, so it might not adhere to all the usual coding
// conventions.
#ifndef MODULES_WLAN_BUFFER_MANAGEMENT_MOCK_APC_BUFFER_MANAGER_H
#define MODULES_WLAN_BUFFER_MANAGEMENT_MOCK_APC_BUFFER_MANAGER_H

#include "wlan_buffer_manager.h"
#include "sys_if/types/types.h"
#include "sys_if/common.h"

typedef void (*MockApcBmRefillCallback)(void *);

typedef struct MockApcBmInitParams {
	uint32_t apc_tx_buf_size;
	uint32_t apc_rx_buf_size;
	uint32_t noa_tx_buf_size;
	uint32_t noa_rx_buf_size;
	uint32_t apc_tx_buf_pkt_offset;
	uint32_t apc_rx_buf_pkt_offset;
	uint32_t noa_tx_buf_pkt_offset;
	uint32_t noa_rx_buf_pkt_offset;
	MockApcBmRefillCallback refill_callback;
	void *refill_callback_context;
} MockApcBmInitParams;

extern BufferManagerInfo *MockApcBmInit(MockApcBmInitParams *params);
extern void MockApcBmDeinit(void);
extern int32_t MockApcBmRefill(BufferTableType type, uint32_t len_buffer_info_list,
			       const BufferInfo *buffer_info_list, bool to_dev);

#endif /* MODULES_WLAN_BUFFER_MANAGEMENT_MOCK_APC_BUFFER_MANAGER_H */
