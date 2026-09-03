#include <cstring>

#include "gtest/gtest.h"
#include "modem_cmd_service.pb.h"
#include "mock_transport.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/modem_cmd_service_client.nanopb.h"

namespace {

constexpr uint32_t kRpcChannelIdTest = 9;
constexpr uint32_t kCpBase = 1;
constexpr int32_t kUlInfoRgnOffset = 2;
constexpr int32_t kUlDescRgnOffset = 3;
constexpr int32_t kUlBuffRgnOffset = 4;
constexpr int32_t kUlBuffRgnSize = 5;
constexpr int32_t kUlNumQueue = 6;
constexpr int32_t kUlDefaultMaxPacketSize = 7;
constexpr int32_t kModemFwOutputRingRgnOffset = 8;
constexpr int32_t kCpPaddingSize = 9;
constexpr int32_t kModemFwInputRingRegionOffset = 10;
constexpr int32_t kDlBuffRegionOffset = 11;
constexpr int32_t kDlBuffRegionSize = 12;
constexpr int32_t kDlDefaultMaxPacketSize = 13;
constexpr int32_t kDlInfoRegionOffset = 14;
constexpr int32_t kDlDescRegionOffset = 15;
constexpr int32_t kDlNumQueue = 16;
constexpr int32_t kSkbPaddingSize = 17;
constexpr uint64_t kTxq0DrbBase = 18;
constexpr uint64_t kTxq1DrbBase = 19;
constexpr uint64_t kTxq2DrbBase = 20;
constexpr uint64_t kTxq3DrbBase = 21;
constexpr uint64_t kTxq4DrbBase = 22;
constexpr int32_t kTxq0DrbCount = 23;
constexpr int32_t kTxq1DrbCount = 24;
constexpr int32_t kTxq2DrbCount = 25;
constexpr int32_t kTxq3DrbCount = 26;
constexpr int32_t kTxq4DrbCount = 27;
constexpr int32_t kTxq0DoorbellDelayMilliseconds = 28;
constexpr int32_t kTxq1DoorbellDelayMilliseconds = 29;
constexpr int32_t kTxq2DoorbellDelayMilliseconds = 30;
constexpr int32_t kTxq3DoorbellDelayMilliseconds = 31;
constexpr int32_t kTxq4DoorbellDelayMilliseconds = 32;
constexpr int32_t kTxq0BurstSubmitCount = 33;
constexpr int32_t kTxq1BurstSubmitCount = 34;
constexpr int32_t kTxq2BurstSubmitCount = 35;
constexpr int32_t kTxq3BurstSubmitCount = 36;
constexpr int32_t kTxq4BurstSubmitCount = 37;
constexpr uint64_t kTxq0NoaDrbBase = 38;
constexpr uint64_t kTxq1NoaDrbBase = 39;
constexpr uint64_t kTxq2NoaDrbBase = 40;
constexpr uint64_t kTxq3NoaDrbBase = 41;
constexpr uint64_t kTxq4NoaDrbBase = 42;
constexpr uint64_t kRxq0PitBase = 43;
constexpr uint64_t kRxq1PitBase = 44;
constexpr uint64_t kRxq2PitBase = 45;
constexpr int32_t kRxq0PitCount = 46;
constexpr int32_t kRxq1PitCount = 47;
constexpr int32_t kRxq2PitCount = 48;
constexpr int32_t kRxq0PitSeqMax = 49;
constexpr int32_t kRxq1PitSeqMax = 50;
constexpr int32_t kRxq2PitSeqMax = 51;
constexpr int32_t kRxq0BatRingId = 52;
constexpr int32_t kRxq1BatRingId = 53;
constexpr int32_t kRxq2BatRingId = 54;
constexpr int32_t kBatBufSize = 55;
constexpr uint64_t kBat0Base = 56;
constexpr uint64_t kBat1Base = 57;
constexpr int32_t kBat0Count = 58;
constexpr int32_t kBat1Count = 59;
constexpr int32_t kBat0ReloadCount = 60;
constexpr int32_t kBat1ReloadCount = 61;
constexpr int32_t kFragBatBufSize = 62;
constexpr uint64_t kFragBat0Base = 63;
constexpr uint64_t kFragBat1Base = 64;
constexpr int32_t kFragBat0Count = 65;
constexpr int32_t kFragBat1Count = 66;
constexpr int32_t kFragBat0ReloadCount = 67;
constexpr int32_t kFragBat1ReloadCount = 68;
constexpr uint64_t kTxq0DrbDpaBase = 69;
constexpr uint64_t kTxq1DrbDpaBase = 70;
constexpr uint64_t kTxq2DrbDpaBase = 71;
constexpr uint64_t kTxq3DrbDpaBase = 72;
constexpr uint64_t kTxq4DrbDpaBase = 73;
constexpr uint64_t kRxq0PitDpaBase = 74;
constexpr uint64_t kRxq1PitDpaBase = 75;
constexpr uint64_t kRxq2PitDpaBase = 76;
constexpr uint64_t kBat0DpaBase = 77;
constexpr uint64_t kBat1DpaBase = 78;
constexpr uint64_t kFragBat0DpaBase = 79;
constexpr uint64_t kFragBat1DpaBase = 80;
constexpr uint64_t kBatMaskTable0DpaBase = 81;
constexpr uint64_t kBatMaskTable1DpaBase = 82;
constexpr uint64_t kFragBatMaskTable0DpaBase = 83;
constexpr uint64_t kFragBatMaskTable1DpaBase = 84;
constexpr uint64_t kBatTkidTable0DpaBase = 85;
constexpr uint64_t kBatTkidTable1DpaBase = 86;
constexpr uint64_t kFragBatTkidTable0DpaBase = 87;
constexpr uint64_t kFragBatTkidTable1DpaBase = 88;
constexpr uint64_t kBatBufferTable0DpaBase = 89;
constexpr uint64_t kBatBufferTable1DpaBase = 90;
constexpr uint64_t kFragBatBufferTable0DpaBase = 91;
constexpr uint64_t kFragBatBufferTable1DpaBase = 92;
constexpr int32_t kDrbWrIdx0 = 93;
constexpr int32_t kDrbWrIdx1 = 94;
constexpr int32_t kDrbWrIdx2 = 95;
constexpr int32_t kDrbWrIdx3 = 96;
constexpr int32_t kDrbWrIdx4 = 97;
constexpr int32_t kDrbRdIdx0 = 98;
constexpr int32_t kDrbRdIdx1 = 99;
constexpr int32_t kDrbRdIdx2 = 100;
constexpr int32_t kDrbRdIdx3 = 101;
constexpr int32_t kDrbRdIdx4 = 102;
constexpr int32_t kDrbRelIdx0 = 103;
constexpr int32_t kDrbRelIdx1 = 104;
constexpr int32_t kDrbRelIdx2 = 105;
constexpr int32_t kDrbRelIdx3 = 106;
constexpr int32_t kDrbRelIdx4 = 107;
constexpr int32_t kPitWrIdx0 = 108;
constexpr int32_t kPitWrIdx1 = 109;
constexpr int32_t kPitWrIdx2 = 110;
constexpr int32_t kPitRdIdx0 = 111;
constexpr int32_t kPitRdIdx1 = 112;
constexpr int32_t kPitRdIdx2 = 113;
constexpr int32_t kPitRelIdx0 = 114;
constexpr int32_t kPitRelIdx1 = 115;
constexpr int32_t kPitRelIdx2 = 116;
constexpr int32_t kBatWrIdx0 = 117;
constexpr int32_t kBatWrIdx1 = 118;
constexpr int32_t kBatRdIdx0 = 119;
constexpr int32_t kBatRdIdx1 = 120;
constexpr int32_t kFragBatWrIdx0 = 121;
constexpr int32_t kFragBatWrIdx1 = 122;
constexpr int32_t kFragBatRdIdx0 = 123;
constexpr int32_t kFragBatRdIdx1 = 124;
constexpr uint64_t kTxBufferPoolBase = 125;
constexpr uint64_t kTxBufferPoolDpaBase = 126;
constexpr uint64_t kSharedMemoryAddr  = 127;
constexpr uint32_t kSharedMemorySize  = 128;


struct TestTransport test_transport_info;

TEST(ModemCmdServiceClientTestNanopb, InvokeInitCommandSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);
  noa_service_modem_cmd_service_InitRequest request;
  request.cp_base = kCpBase;
  request.ul_info_rgn_offset = kUlInfoRgnOffset;
  request.ul_desc_rgn_offset = kUlDescRgnOffset;
  request.ul_buff_rgn_offset = kUlBuffRgnOffset;
  request.ul_buff_rgn_size = kUlBuffRgnSize;
  request.ul_num_queue = kUlNumQueue;
  request.ul_default_max_packet_size = kUlDefaultMaxPacketSize;
  request.modem_fw_output_ring_rgn_offset = kModemFwOutputRingRgnOffset;
  request.cp_padding_size = kCpPaddingSize;
  request.modem_fw_input_ring_region_offset = kModemFwInputRingRegionOffset;
  request.dl_buff_region_offset = kDlBuffRegionOffset;
  request.dl_buff_region_size = kDlBuffRegionSize;
  request.dl_default_max_packet_size = kDlDefaultMaxPacketSize;
  request.dl_info_region_offset = kDlInfoRegionOffset;
  request.dl_desc_region_offset = kDlDescRegionOffset;
  request.dl_num_queue = kDlNumQueue;
  request.skb_padding_size = kSkbPaddingSize;
  request.txq_drb_base[0] = kTxq0DrbBase;
  request.txq_drb_base[1] = kTxq1DrbBase;
  request.txq_drb_base[2] = kTxq2DrbBase;
  request.txq_drb_base[3] = kTxq3DrbBase;
  request.txq_drb_base[4] = kTxq4DrbBase;
  request.txq_drb_count[0] = kTxq0DrbCount;
  request.txq_drb_count[1] = kTxq1DrbCount;
  request.txq_drb_count[2] = kTxq2DrbCount;
  request.txq_drb_count[3] = kTxq3DrbCount;
  request.txq_drb_count[4] = kTxq4DrbCount;
  request.txq_doorbell_delay_millisecond[0] = kTxq0DoorbellDelayMilliseconds;
  request.txq_doorbell_delay_millisecond[1] = kTxq1DoorbellDelayMilliseconds;
  request.txq_doorbell_delay_millisecond[2] = kTxq2DoorbellDelayMilliseconds;
  request.txq_doorbell_delay_millisecond[3] = kTxq3DoorbellDelayMilliseconds;
  request.txq_doorbell_delay_millisecond[4] = kTxq4DoorbellDelayMilliseconds;
  request.txq_burst_submit_count[0] = kTxq0BurstSubmitCount;
  request.txq_burst_submit_count[1] = kTxq1BurstSubmitCount;
  request.txq_burst_submit_count[2] = kTxq2BurstSubmitCount;
  request.txq_burst_submit_count[3] = kTxq3BurstSubmitCount;
  request.txq_burst_submit_count[4] = kTxq4BurstSubmitCount;
  request.txq_noa_drb_base[0] = kTxq0NoaDrbBase;
  request.txq_noa_drb_base[1] = kTxq1NoaDrbBase;
  request.txq_noa_drb_base[2] = kTxq2NoaDrbBase;
  request.txq_noa_drb_base[3] = kTxq3NoaDrbBase;
  request.txq_noa_drb_base[4] = kTxq4NoaDrbBase;
  request.rxq_pit_base[0] = kRxq0PitBase;
  request.rxq_pit_base[1] = kRxq1PitBase;
  request.rxq_pit_base[2] = kRxq2PitBase;
  request.rxq_pit_count[0] = kRxq0PitCount;
  request.rxq_pit_count[1] = kRxq1PitCount;
  request.rxq_pit_count[2] = kRxq2PitCount;
  request.rxq_pit_seq_max[0] = kRxq0PitSeqMax;
  request.rxq_pit_seq_max[1] = kRxq1PitSeqMax;
  request.rxq_pit_seq_max[2] = kRxq2PitSeqMax;
  request.rxq_bat_ring_id[0] = kRxq0BatRingId;
  request.rxq_bat_ring_id[1] = kRxq1BatRingId;
  request.rxq_bat_ring_id[2] = kRxq2BatRingId;
  request.bat_buf_size = kBatBufSize;
  request.bat_base[0] = kBat0Base;
  request.bat_base[1] = kBat1Base;
  request.bat_count[0] = kBat0Count;
  request.bat_count[1] = kBat1Count;
  request.bat_reload_count[0] = kBat0ReloadCount;
  request.bat_reload_count[1] = kBat1ReloadCount;
  request.frag_bat_buf_size = kFragBatBufSize;
  request.frag_bat_base[0] = kFragBat0Base;
  request.frag_bat_base[1] = kFragBat1Base;
  request.frag_bat_count[0] = kFragBat0Count;
  request.frag_bat_count[1] = kFragBat1Count;
  request.frag_bat_reload_count[0] = kFragBat0ReloadCount;
  request.frag_bat_reload_count[1] = kFragBat1ReloadCount;
  request.txq_drb_dpa_base[0] = kTxq0DrbDpaBase;
  request.txq_drb_dpa_base[1] = kTxq1DrbDpaBase;
  request.txq_drb_dpa_base[2] = kTxq2DrbDpaBase;
  request.txq_drb_dpa_base[3] = kTxq3DrbDpaBase;
  request.txq_drb_dpa_base[4] = kTxq4DrbDpaBase;
  request.rxq_pit_dpa_base[0] = kRxq0PitDpaBase;
  request.rxq_pit_dpa_base[1] = kRxq1PitDpaBase;
  request.rxq_pit_dpa_base[2] = kRxq2PitDpaBase;
  request.bat_dpa_base[0] = kBat0DpaBase;
  request.bat_dpa_base[1] = kBat1DpaBase;
  request.frag_bat_dpa_base[0] = kFragBat0DpaBase;
  request.frag_bat_dpa_base[1] = kFragBat1DpaBase;
  request.bat_mask_table_dpa_base[0] = kBatMaskTable0DpaBase;
  request.bat_mask_table_dpa_base[1] = kBatMaskTable1DpaBase;
  request.frag_bat_mask_table_dpa_base[0] = kFragBatMaskTable0DpaBase;
  request.frag_bat_mask_table_dpa_base[1] = kFragBatMaskTable1DpaBase;
  request.bat_tkid_table_dpa_base[0] = kBatTkidTable0DpaBase;
  request.bat_tkid_table_dpa_base[1] = kBatTkidTable1DpaBase;
  request.frag_bat_tkid_table_dpa_base[0] = kFragBatTkidTable0DpaBase;
  request.frag_bat_tkid_table_dpa_base[1] = kFragBatTkidTable1DpaBase;
  request.bat_buffer_table_dpa_base[0] = kBatBufferTable0DpaBase;
  request.bat_buffer_table_dpa_base[1] = kBatBufferTable1DpaBase;
  request.frag_bat_buffer_table_dpa_base[0] = kFragBatBufferTable0DpaBase;
  request.frag_bat_buffer_table_dpa_base[1] = kFragBatBufferTable1DpaBase;
  request.drb_wr_idx[0] = kDrbWrIdx0;
  request.drb_wr_idx[1] = kDrbWrIdx1;
  request.drb_wr_idx[2] = kDrbWrIdx2;
  request.drb_wr_idx[3] = kDrbWrIdx3;
  request.drb_wr_idx[4] = kDrbWrIdx4;
  request.drb_rd_idx[0] = kDrbRdIdx0;
  request.drb_rd_idx[1] = kDrbRdIdx1;
  request.drb_rd_idx[2] = kDrbRdIdx2;
  request.drb_rd_idx[3] = kDrbRdIdx3;
  request.drb_rd_idx[4] = kDrbRdIdx4;
  request.drb_rel_rd_idx[0] = kDrbRelIdx0;
  request.drb_rel_rd_idx[1] = kDrbRelIdx1;
  request.drb_rel_rd_idx[2] = kDrbRelIdx2;
  request.drb_rel_rd_idx[3] = kDrbRelIdx3;
  request.drb_rel_rd_idx[4] = kDrbRelIdx4;
  request.pit_wr_idx[0] = kPitWrIdx0;
  request.pit_wr_idx[1] = kPitWrIdx1;
  request.pit_wr_idx[2] = kPitWrIdx2;
  request.pit_rd_idx[0] = kPitRdIdx0;
  request.pit_rd_idx[1] = kPitRdIdx1;
  request.pit_rd_idx[2] = kPitRdIdx2;
  request.pit_rel_rd_idx[0] = kPitRelIdx0;
  request.pit_rel_rd_idx[1] = kPitRelIdx1;
  request.pit_rel_rd_idx[2] = kPitRelIdx2;
  request.bat_wr_idx[0] = kBatWrIdx0;
  request.bat_wr_idx[1] = kBatWrIdx1;
  request.bat_rd_idx[0] = kBatRdIdx0;
  request.bat_rd_idx[1] = kBatRdIdx1;
  request.frag_bat_wr_idx[0] = kFragBatWrIdx0;
  request.frag_bat_wr_idx[1] = kFragBatWrIdx1;
  request.frag_bat_rd_idx[0] = kFragBatRdIdx0;
  request.frag_bat_rd_idx[1] = kFragBatRdIdx1;
  request.tx_buffer_pool_base = kTxBufferPoolBase;
  request.tx_buffer_pool_dpa_base = kTxBufferPoolDpaBase;
  request.shared_memory_addr = kSharedMemoryAddr;
  request.shared_memory_size = kSharedMemorySize;

  EXPECT_EQ(ModemCmdServiceInitCommand(&client, &request, nullptr, nullptr,
                                       nullptr, nullptr),
            kPwStatusOk);
  ASSERT_GT(test_transport_info.tx_bytes, 0U);

  uint8_t payload_buf[1024];
  pw_rpc_internal_RpcPacket packet;
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, MODEM_CMD_SERVICE_ID);
  EXPECT_EQ(packet.method_id, MODEM_CMD_SERVICE_INIT_COMMAND_METHOD_ID);

  noa_service_modem_cmd_service_InitRequest written_request;
  EXPECT_EQ(PwRpcClientDeserializeResponse(
                payload_buf, payload_len,
                noa_service_modem_cmd_service_InitRequest_fields,
                &written_request),
            kPwStatusOk);
  EXPECT_EQ(written_request.cp_base, kCpBase);
  EXPECT_EQ(written_request.ul_info_rgn_offset, kUlInfoRgnOffset);
  EXPECT_EQ(written_request.ul_desc_rgn_offset, kUlDescRgnOffset);
  EXPECT_EQ(written_request.ul_buff_rgn_offset, kUlBuffRgnOffset);
  EXPECT_EQ(written_request.ul_buff_rgn_size, kUlBuffRgnSize);
  EXPECT_EQ(written_request.ul_num_queue, kUlNumQueue);
  EXPECT_EQ(written_request.ul_default_max_packet_size,
            kUlDefaultMaxPacketSize);
  EXPECT_EQ(written_request.modem_fw_output_ring_rgn_offset,
            kModemFwOutputRingRgnOffset);
  EXPECT_EQ(written_request.cp_padding_size, kCpPaddingSize);
  EXPECT_EQ(written_request.modem_fw_input_ring_region_offset,
            kModemFwInputRingRegionOffset);
  EXPECT_EQ(written_request.dl_info_region_offset, kDlInfoRegionOffset);
  EXPECT_EQ(written_request.dl_desc_region_offset, kDlDescRegionOffset);
  EXPECT_EQ(written_request.dl_buff_region_offset, kDlBuffRegionOffset);
  EXPECT_EQ(written_request.dl_buff_region_size, kDlBuffRegionSize);
  EXPECT_EQ(written_request.dl_num_queue, kDlNumQueue);
  EXPECT_EQ(written_request.dl_default_max_packet_size,
            kDlDefaultMaxPacketSize);
  EXPECT_EQ(written_request.skb_padding_size, kSkbPaddingSize);

  EXPECT_EQ(written_request.txq_drb_base[0], kTxq0DrbBase);
  EXPECT_EQ(written_request.txq_drb_base[1], kTxq1DrbBase);
  EXPECT_EQ(written_request.txq_drb_base[2], kTxq2DrbBase);
  EXPECT_EQ(written_request.txq_drb_base[3], kTxq3DrbBase);
  EXPECT_EQ(written_request.txq_drb_base[4], kTxq4DrbBase);
  EXPECT_EQ(written_request.txq_drb_count[0], kTxq0DrbCount);
  EXPECT_EQ(written_request.txq_drb_count[1], kTxq1DrbCount);
  EXPECT_EQ(written_request.txq_drb_count[2], kTxq2DrbCount);
  EXPECT_EQ(written_request.txq_drb_count[3], kTxq3DrbCount);
  EXPECT_EQ(written_request.txq_drb_count[4], kTxq4DrbCount);
  EXPECT_EQ(written_request.txq_doorbell_delay_millisecond[0],
            kTxq0DoorbellDelayMilliseconds);
  EXPECT_EQ(written_request.txq_doorbell_delay_millisecond[1],
            kTxq1DoorbellDelayMilliseconds);
  EXPECT_EQ(written_request.txq_doorbell_delay_millisecond[2],
            kTxq2DoorbellDelayMilliseconds);
  EXPECT_EQ(written_request.txq_doorbell_delay_millisecond[3],
            kTxq3DoorbellDelayMilliseconds);
  EXPECT_EQ(written_request.txq_doorbell_delay_millisecond[4],
            kTxq4DoorbellDelayMilliseconds);
  EXPECT_EQ(written_request.txq_burst_submit_count[0], kTxq0BurstSubmitCount);
  EXPECT_EQ(written_request.txq_burst_submit_count[1], kTxq1BurstSubmitCount);
  EXPECT_EQ(written_request.txq_burst_submit_count[2], kTxq2BurstSubmitCount);
  EXPECT_EQ(written_request.txq_burst_submit_count[3], kTxq3BurstSubmitCount);
  EXPECT_EQ(written_request.txq_burst_submit_count[4], kTxq4BurstSubmitCount);
  EXPECT_EQ(written_request.txq_noa_drb_base[0], kTxq0NoaDrbBase);
  EXPECT_EQ(written_request.txq_noa_drb_base[1], kTxq1NoaDrbBase);
  EXPECT_EQ(written_request.txq_noa_drb_base[2], kTxq2NoaDrbBase);
  EXPECT_EQ(written_request.txq_noa_drb_base[3], kTxq3NoaDrbBase);
  EXPECT_EQ(written_request.txq_noa_drb_base[4], kTxq4NoaDrbBase);
  EXPECT_EQ(written_request.rxq_pit_base[0], kRxq0PitBase);
  EXPECT_EQ(written_request.rxq_pit_base[1], kRxq1PitBase);
  EXPECT_EQ(written_request.rxq_pit_base[2], kRxq2PitBase);
  EXPECT_EQ(written_request.rxq_pit_count[0], kRxq0PitCount);
  EXPECT_EQ(written_request.rxq_pit_count[1], kRxq1PitCount);
  EXPECT_EQ(written_request.rxq_pit_count[2], kRxq2PitCount);
  EXPECT_EQ(written_request.rxq_pit_seq_max[0], kRxq0PitSeqMax);
  EXPECT_EQ(written_request.rxq_pit_seq_max[1], kRxq1PitSeqMax);
  EXPECT_EQ(written_request.rxq_pit_seq_max[2], kRxq2PitSeqMax);
  EXPECT_EQ(written_request.rxq_bat_ring_id[0], kRxq0BatRingId);
  EXPECT_EQ(written_request.rxq_bat_ring_id[1], kRxq1BatRingId);
  EXPECT_EQ(written_request.rxq_bat_ring_id[2], kRxq2BatRingId);
  EXPECT_EQ(written_request.bat_buf_size, kBatBufSize);
  EXPECT_EQ(written_request.bat_base[0], kBat0Base);
  EXPECT_EQ(written_request.bat_base[1], kBat1Base);
  EXPECT_EQ(written_request.bat_count[0], kBat0Count);
  EXPECT_EQ(written_request.bat_count[1], kBat1Count);
  EXPECT_EQ(written_request.bat_reload_count[0], kBat0ReloadCount);
  EXPECT_EQ(written_request.bat_reload_count[1], kBat1ReloadCount);
  EXPECT_EQ(written_request.frag_bat_buf_size, kFragBatBufSize);
  EXPECT_EQ(written_request.frag_bat_base[0], kFragBat0Base);
  EXPECT_EQ(written_request.frag_bat_base[1], kFragBat1Base);
  EXPECT_EQ(written_request.frag_bat_count[0], kFragBat0Count);
  EXPECT_EQ(written_request.frag_bat_count[1], kFragBat1Count);
  EXPECT_EQ(written_request.frag_bat_reload_count[0], kFragBat0ReloadCount);
  EXPECT_EQ(written_request.frag_bat_reload_count[1], kFragBat1ReloadCount);
  EXPECT_EQ(written_request.txq_drb_dpa_base[0], kTxq0DrbDpaBase);
  EXPECT_EQ(written_request.txq_drb_dpa_base[1], kTxq1DrbDpaBase);
  EXPECT_EQ(written_request.txq_drb_dpa_base[2], kTxq2DrbDpaBase);
  EXPECT_EQ(written_request.txq_drb_dpa_base[3], kTxq3DrbDpaBase);
  EXPECT_EQ(written_request.txq_drb_dpa_base[4], kTxq4DrbDpaBase);
  EXPECT_EQ(written_request.rxq_pit_dpa_base[0], kRxq0PitDpaBase);
  EXPECT_EQ(written_request.rxq_pit_dpa_base[1], kRxq1PitDpaBase);
  EXPECT_EQ(written_request.rxq_pit_dpa_base[2], kRxq2PitDpaBase);
  EXPECT_EQ(written_request.bat_dpa_base[0], kBat0DpaBase);
  EXPECT_EQ(written_request.bat_dpa_base[1], kBat1DpaBase);
  EXPECT_EQ(written_request.frag_bat_dpa_base[0], kFragBat0DpaBase);
  EXPECT_EQ(written_request.frag_bat_dpa_base[1], kFragBat1DpaBase);
  EXPECT_EQ(written_request.bat_mask_table_dpa_base[0], kBatMaskTable0DpaBase);
  EXPECT_EQ(written_request.bat_mask_table_dpa_base[1], kBatMaskTable1DpaBase);
  EXPECT_EQ(written_request.frag_bat_mask_table_dpa_base[0], kFragBatMaskTable0DpaBase);
  EXPECT_EQ(written_request.frag_bat_mask_table_dpa_base[1], kFragBatMaskTable1DpaBase);
  EXPECT_EQ(written_request.bat_tkid_table_dpa_base[0], kBatTkidTable0DpaBase);
  EXPECT_EQ(written_request.bat_tkid_table_dpa_base[1], kBatTkidTable1DpaBase);
  EXPECT_EQ(written_request.frag_bat_tkid_table_dpa_base[0], kFragBatTkidTable0DpaBase);
  EXPECT_EQ(written_request.frag_bat_tkid_table_dpa_base[1], kFragBatTkidTable1DpaBase);
  EXPECT_EQ(written_request.bat_buffer_table_dpa_base[0], kBatBufferTable0DpaBase);
  EXPECT_EQ(written_request.bat_buffer_table_dpa_base[1], kBatBufferTable1DpaBase);
  EXPECT_EQ(written_request.frag_bat_buffer_table_dpa_base[0], kFragBatBufferTable0DpaBase);
  EXPECT_EQ(written_request.frag_bat_buffer_table_dpa_base[1], kFragBatBufferTable1DpaBase);
  EXPECT_EQ(written_request.drb_wr_idx[0], kDrbWrIdx0);
  EXPECT_EQ(written_request.drb_wr_idx[1], kDrbWrIdx1);
  EXPECT_EQ(written_request.drb_wr_idx[2], kDrbWrIdx2);
  EXPECT_EQ(written_request.drb_wr_idx[3], kDrbWrIdx3);
  EXPECT_EQ(written_request.drb_wr_idx[4], kDrbWrIdx4);
  EXPECT_EQ(written_request.drb_rd_idx[0], kDrbRdIdx0);
  EXPECT_EQ(written_request.drb_rd_idx[1], kDrbRdIdx1);
  EXPECT_EQ(written_request.drb_rd_idx[2], kDrbRdIdx2);
  EXPECT_EQ(written_request.drb_rd_idx[3], kDrbRdIdx3);
  EXPECT_EQ(written_request.drb_rd_idx[4], kDrbRdIdx4);
  EXPECT_EQ(written_request.drb_rel_rd_idx[0], kDrbRelIdx0);
  EXPECT_EQ(written_request.drb_rel_rd_idx[1], kDrbRelIdx1);
  EXPECT_EQ(written_request.drb_rel_rd_idx[2], kDrbRelIdx2);
  EXPECT_EQ(written_request.drb_rel_rd_idx[3], kDrbRelIdx3);
  EXPECT_EQ(written_request.drb_rel_rd_idx[4], kDrbRelIdx4);
  EXPECT_EQ(written_request.pit_wr_idx[0], kPitWrIdx0);
  EXPECT_EQ(written_request.pit_wr_idx[1], kPitWrIdx1);
  EXPECT_EQ(written_request.pit_wr_idx[2], kPitWrIdx2);
  EXPECT_EQ(written_request.pit_rd_idx[0], kPitRdIdx0);
  EXPECT_EQ(written_request.pit_rd_idx[1], kPitRdIdx1);
  EXPECT_EQ(written_request.pit_rd_idx[2], kPitRdIdx2);
  EXPECT_EQ(written_request.pit_rel_rd_idx[0], kPitRelIdx0);
  EXPECT_EQ(written_request.pit_rel_rd_idx[1], kPitRelIdx1);
  EXPECT_EQ(written_request.pit_rel_rd_idx[2], kPitRelIdx2);
  EXPECT_EQ(written_request.bat_wr_idx[0], kBatWrIdx0);
  EXPECT_EQ(written_request.bat_wr_idx[1], kBatWrIdx1);
  EXPECT_EQ(written_request.bat_rd_idx[0], kBatRdIdx0);
  EXPECT_EQ(written_request.bat_rd_idx[1], kBatRdIdx1);
  EXPECT_EQ(written_request.frag_bat_wr_idx[0], kFragBatWrIdx0);
  EXPECT_EQ(written_request.frag_bat_wr_idx[1], kFragBatWrIdx1);
  EXPECT_EQ(written_request.frag_bat_rd_idx[0], kFragBatRdIdx0);
  EXPECT_EQ(written_request.frag_bat_rd_idx[1], kFragBatRdIdx1);
  EXPECT_EQ(written_request.tx_buffer_pool_base, kTxBufferPoolBase);
  EXPECT_EQ(written_request.tx_buffer_pool_dpa_base, kTxBufferPoolDpaBase);
  EXPECT_EQ(written_request.shared_memory_addr, kSharedMemoryAddr);
  EXPECT_EQ(written_request.shared_memory_size, kSharedMemorySize);

  PwRpcClientDeinit(&client);
}

TEST(ModemCmdServiceClientTestNanopb, DeserializeInitCommandResponseSuccess) {
  constexpr noa_service_modem_cmd_service_CmdResult kResult =
      noa_service_modem_cmd_service_CmdResult_SUCCESS;
  uint8_t buf[32] = {0};

  noa_service_modem_cmd_service_Response response;
  memset(&response, 0, sizeof(noa_service_modem_cmd_service_Response));
  response.result = kResult;

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream,
                        noa_service_modem_cmd_service_Response_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_modem_cmd_service_Response));
  EXPECT_EQ(ModemCmdServiceInitCommandDeserializeResponse(
                buf, stream.bytes_written, &response),
            kPwStatusOk);
  EXPECT_EQ(response.result, kResult);
}

constexpr uint32_t kTestId = 45;
constexpr char kTestCmd[] = {"TestCommand"};
constexpr noa_service_modem_cmd_service_CmdResult kTestResult =
    noa_service_modem_cmd_service_CmdResult_FAILURE;
constexpr char kTestMsg[] = {"Failure"};

TEST(ModemCmdServiceClientTestNanopb, InvokeCommandSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);
  noa_service_modem_cmd_service_Request request;
  request.id = kTestId;
  memcpy(request.msg.bytes, reinterpret_cast<const uint8_t*>(kTestCmd),
         sizeof(kTestCmd));
  request.msg.size = sizeof(kTestCmd);

  EXPECT_EQ(ModemCmdServiceCommand(&client, &request, nullptr, nullptr, nullptr,
                                  nullptr),
            kPwStatusOk);
  ASSERT_GT(test_transport_info.tx_bytes, 0U);

  uint8_t payload_buf[32];
  pw_rpc_internal_RpcPacket packet;
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, MODEM_CMD_SERVICE_ID);
  EXPECT_EQ(packet.method_id, MODEM_CMD_SERVICE_COMMAND_METHOD_ID);

  noa_service_modem_cmd_service_Request written_request;
  EXPECT_EQ(PwRpcClientDeserializeResponse(
                payload_buf, payload_len,
                noa_service_modem_cmd_service_Request_fields, &written_request),
            kPwStatusOk);
  EXPECT_EQ(written_request.id, kTestId);
  EXPECT_EQ(written_request.msg.size, sizeof(kTestCmd));
  EXPECT_STREQ(reinterpret_cast<const char*>(written_request.msg.bytes),
               kTestCmd);

  PwRpcClientDeinit(&client);
}

TEST(ModemCmdServiceClientTestNanopb, DeserializeCommandResponseSuccess) {
  uint8_t buf[32] = {0};

  noa_service_modem_cmd_service_Response response;
  memset(&response, 0, sizeof(noa_service_modem_cmd_service_Response));
  response.result = kTestResult;
  response.msg.size = sizeof(kTestMsg);
  memcpy(response.msg.bytes, kTestMsg, sizeof(kTestMsg));

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_modem_cmd_service_Response_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_modem_cmd_service_Response));
  EXPECT_EQ(ModemCmdServiceCommandDeserializeResponse(buf, stream.bytes_written,
                                                     &response),
            kPwStatusOk);
  EXPECT_EQ(response.result, kTestResult);
  EXPECT_EQ(response.msg.size, sizeof(kTestMsg));
  EXPECT_STREQ(reinterpret_cast<const char*>(response.msg.bytes), kTestMsg);
}

TEST(ModemCmdServiceClientTestNanopb, InvokeEventSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);
  uint32_t call_id = 0;
  EXPECT_EQ(ModemCmdServiceEvent(&client, nullptr, nullptr, nullptr, nullptr,
                                &call_id),
            kPwStatusOk);
  ASSERT_GT(test_transport_info.tx_bytes, 0U);

  uint8_t payload_buf[32];
  pw_rpc_internal_RpcPacket packet;
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, MODEM_CMD_SERVICE_ID);
  EXPECT_EQ(packet.method_id, MODEM_CMD_SERVICE_EVENT_METHOD_ID);

  noa_service_modem_cmd_service_Response request;
  request.result = kTestResult;
  memcpy(request.msg.bytes, reinterpret_cast<const uint8_t*>(kTestMsg),
         sizeof(kTestMsg));
  request.msg.size = sizeof(kTestMsg);
  EXPECT_EQ(ModemCmdServiceEventNext(&client, call_id, &request), kPwStatusOk);
  EXPECT_EQ(ModemCmdServiceEventNext(&client, call_id, &request), kPwStatusOk);
  EXPECT_EQ(ModemCmdServiceEventComplete(&client, call_id), kPwStatusOk);

  PwRpcClientDeinit(&client);
}

TEST(ModemCmdServiceClientTestNanopb, DeserializeEventResponseSuccess) {
  uint8_t buf[32] = {0};

  noa_service_modem_cmd_service_Request response;
  memset(&response, 0, sizeof(noa_service_modem_cmd_service_Request));
  response.id = kTestId;
  response.msg.size = sizeof(kTestMsg);
  memcpy(response.msg.bytes, kTestMsg, sizeof(kTestMsg));

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_modem_cmd_service_Request_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_modem_cmd_service_Request));
  EXPECT_EQ(ModemCmdServiceEventDeserializeResponse(buf, stream.bytes_written,
                                                   &response),
            kPwStatusOk);
  EXPECT_EQ(response.id, kTestId);
  EXPECT_EQ(response.msg.size, sizeof(kTestMsg));
  EXPECT_STREQ(reinterpret_cast<const char*>(response.msg.bytes), kTestMsg);
}
}  // namespace
