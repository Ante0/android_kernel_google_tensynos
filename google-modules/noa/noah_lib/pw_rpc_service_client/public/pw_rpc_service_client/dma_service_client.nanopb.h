#ifndef DMA_SERVICE_CLIENT_NANOPB_H
#define DMA_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc_c/pw_rpc_service_client.h"
#include "dma_service/dma_service.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DMA_SERVICE_ID (0x94f11746)
#define DMA_SERVICE_WRITE_MESSAGE_METHOD_ID (0xeb82d994)
#define DMA_SERVICE_WRITE_WORD_METHOD_ID (0x9900a240)
#define DMA_SERVICE_READ_WORD_METHOD_ID (0xd51a3ca8)
#define DMA_SERVICE_COPY_DATA_METHOD_ID (0x9773c107)
#define DMA_SERVICE_DMA_COPY_DATA_METHOD_ID (0xbaabf154)

/*
 * rpc WriteMessage(WriteMessageRequest) returns (WriteResponse)
 */
PW_RPC_UNARY_METHOD(DmaService, DMA_SERVICE_ID, WriteMessage,
                    DMA_SERVICE_WRITE_MESSAGE_METHOD_ID,
                    noa_service_dma_service_WriteMessageRequest,
                    noa_service_dma_service_WriteResponse);

PW_RPC_UNARY_METHOD(DmaService, DMA_SERVICE_ID, WriteWord,
                    DMA_SERVICE_WRITE_WORD_METHOD_ID,
                    noa_service_dma_service_WriteWordRequest,
                    noa_service_dma_service_WriteResponse);

PW_RPC_UNARY_METHOD(DmaService, DMA_SERVICE_ID, ReadWord,
                    DMA_SERVICE_READ_WORD_METHOD_ID,
                    noa_service_dma_service_ReadRequest,
                    noa_service_dma_service_ReadWordResponse);

PW_RPC_UNARY_METHOD(DmaService, DMA_SERVICE_ID, CopyData,
                    DMA_SERVICE_COPY_DATA_METHOD_ID,
                    noa_service_dma_service_CopyDataRequest,
                    noa_service_dma_service_CopyDataResponse);

PW_RPC_UNARY_METHOD(DmaService, DMA_SERVICE_ID, DmaCopyData,
                    DMA_SERVICE_DMA_COPY_DATA_METHOD_ID,
                    noa_service_dma_service_DmaCopyDataRequest,
                    noa_service_dma_service_DmaCopyDataResponse);
#ifdef __cplusplus
}
#endif
#endif /* DMA_SERVICE_CLIENT_NANOPB_H */
