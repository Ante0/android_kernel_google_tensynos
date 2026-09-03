#ifndef __NCP_MODEM_DATA_H__
#define __NCP_MODEM_DATA_H__

#include "common/ring.h"
#include "modem/ncp/mediatek/modem_hif_alcedo.h"
#include "noa_md_shmem_layout.h"
#include "sys_common.h"

#ifndef linux
// Sync from mtk_dpmaif_ring.h
enum dpmaif_drb_type {
	PD_DRB,
	MSG_DRB,
};

/* pit type */
enum dpmaif_pit_type {
	PD_PIT,
	MSG_PIT,
};

/* buffer type */
enum dpmaif_bat_type {
	NORMAL_BAT,
	FRAG_BAT,
};

/* rxq attribute*/
enum dpmaif_queue_attr {
	DPMAIFQ_ATTR_NONE = 0,
	DPMAIFQ_ATTR_LOW_LATENCY = BIT(0),
	DPMAIFQ_ATTR_PIT_CACHED = BIT(1),
};

// Sync from mtk_dpmaif_drv.h
enum mtk_drv_err {
	DATA_ERR_STOP_MAX = 10,
	DATA_HW_REG_TIMEOUT,
	DATA_HW_REG_CHK_FAIL,
	DATA_FLOW_CHK_ERR,
	DATA_DMA_MAP_ERR,
	DATA_DL_ONCE_MORE,
	DATA_PIT_SEQ_CHK_FAIL,
	DATA_LOW_MEM_TYPE_MAX,
	DATA_LOW_MEM_DRB,
	DATA_LOW_MEM_BAT,
	DATA_LOW_MEM_PIT,
	DATA_LOW_MEM_SKB,
	DATA_HW_UNK_PKT,
};

enum {
	DPMAIF_CLEAR_INTR,
	DPMAIF_UNMASK_INTR,
};

enum dpmaif_drv_bat_id {
	DPMAIF_BAT0 = 0,
	DPMAIF_BAT1,
};

enum dpmaif_drv_ring_type {
	DPMAIF_PIT,
	DPMAIF_BAT,
	DPMAIF_FRAG,
	DPMAIF_DRB,
	NOA_FREE_POOL,
};

#define DPMAIF_DRB_LASTONE	0x00
#define DPMAIF_DRB_MORE		0x01

/* pit c_bit */
#define DPMAIF_PIT_LASTONE	0x00
#define DPMAIF_PIT_MORE		0x01

/* pit Pro_bit */
#define DPMAIF_PIT_TCP 0x01
#define DPMAIF_PIT_UDP 0x02

/* pit IP_bit */
#define DPMAIF_PIT_IPV4 0x00
#define DPMAIF_PIT_IPV6 0x01

/* default mtu size*/
#define DPMAIF_DFLT_MTU 3000
#define MIN_BAT_BURST_CNT	64

#define BAT0_COUNT 32768
#define BAT1_COUNT 1024
#define FRAG_BAT0_COUNT 8192
#define FRAG_BAT1_COUNT 1024
#define BAT0_ID 0
#define BAT1_ID 1

#define NOA_MD_RX_NOA_NORMAL_BAT0_BASE 0
#define NOA_MD_RX_NOA_FRAG_BAT0_BASE 32768
#define NOA_MD_RX_NOA_NORMAL_BAT1_BASE 40960
#define NOA_MD_RX_NOA_FRAG_BAT1_BASE 41984
#define NOA_MD_RX_NOA_MAX_BAT_BASE 43008

#define NOA_PIT_CNT_UPDATE_THRESHOLD 1024
// This is based on the dpmaif_rxq_cfg setting of mtk_dpmaif_drv_m9xx.
#define NOA_PIT_SEQ_MAX 251

#define le32_to_cpu(x) (x)
#define typecheck(type,x) \
({	type __dummy; \
	__typeof__(x) __dummy2; \
	(void)(&__dummy == &__dummy2); \
	1; \
})
#define time_after_eq(a,b)	\
	(typecheck(unsigned long long, a) && \
	 typecheck(unsigned long long, b) && \
	 ((long long)((a) - (b)) >= 0))

#define LSB(val) ((val) & -(val))
#define GENMASK(h, l) (((BIT(h) << 1) - 1) ^ (BIT(l) - 1))
#define FIELD_GET(mask, data) (((data) & (mask)) / LSB(mask))

#define PIT_PD_DATA_LEN		GENMASK(31, 16) /* Indicates the data length of current packet. */
#define PIT_PD_BUF_ID		GENMASK(15, 3) /* The low order of buffer index */
#define PIT_PD_BUF_TYPE		BIT(2) /* 0b: normal BAT entry; 1b: fragment BAT entry */
#define PIT_PD_CONT		BIT(1) /* 0b: last entry; 1b: more entry */
#define PIT_PD_PKT_TYPE		BIT(0) /* 0b: normal PIT entry; 1b: message PIT entry */
#define PIT_PD_HD_OFFSET	GENMASK(23, 19)
#define PIT_PD_IG		BIT(16)
#define PIT_PD_H_BID		GENMASK(10, 8) /* The high order of buffer index */
#define PIT_PD_SEQ		GENMASK(7, 0) /* PIT sequence */
#define PIT_MSG_DP		BIT(31) /* Indicates software to drop this packet if set. */
#define PIT_MSG_CHNL_ID		GENMASK(23, 16) /* channel index */
#define PIT_MSG_ERR		BIT(4)
#define PIT_MSG_CHECKSUM	GENMASK(3, 2)
#define PIT_MSG_HASH		GENMASK(31, 24) /* Hash value calculated by Hardware using packet */
#define PIT_MSG_PRO		GENMASK(17, 16)
#define PIT_MSG_IP		BIT(23)

#define DPMAIF_POLL_STEP 1
#define DPMAIF_POLL_PIT_CNT_MAX 2
#define DPMAIF_PIT_SEQ_CHECK_FAIL_CNT 2500

#define NOA_MD_FW_TX_POOL_TKID_OFFSET ((u16)(0x8000U))
#define NOA_MD_MAX_TX_PKT_SIZE 2048

// TODO: b/443210525 - Create schedule task to read desc from NOA DRB rings
#define NOA_MD_FW_READ_DESC_MAX 1024

#define NOA_FW_RING_SIZE 512
#define NOA_REL_BAT_WEIGHT 128

inline constexpr uint32_t kMaxUlQueueSize = 5;
inline constexpr uint32_t kMaxDlQueueSize = 3;
inline constexpr uint32_t kMaxBatInfoSize = 2;
inline constexpr uint32_t kMaxBatSize = 2;
inline constexpr uint32_t kMaxFragBatSize = 2;
inline constexpr uint32_t kMaxBatTypeSize = 4;
inline constexpr uint32_t kBatTypeCount[kMaxBatTypeSize] = { BAT0_COUNT, BAT1_COUNT, FRAG_BAT0_COUNT, FRAG_BAT1_COUNT };
inline constexpr uint32_t kRxqAttribute[kMaxDlQueueSize] = { DPMAIFQ_ATTR_PIT_CACHED,
							     DPMAIFQ_ATTR_PIT_CACHED,
							     DPMAIFQ_ATTR_PIT_CACHED | DPMAIFQ_ATTR_LOW_LATENCY };

struct noa_md_fw_ring {
	struct noa_ring_wrapper ring;
	spinlock_t lock;
};

// Define for modem drb rings
struct noa_tx_queue {
	uint64_t drb_base;
	uint64_t drb_dpa_base;
	int32_t drb_cnt;
	int32_t drb_wr_idx;
	int32_t drb_rd_idx;
	int32_t drb_rel_rd_idx;
	int32_t burst_submit_cnt;
	int32_t db_delay_ms;
	int32_t id;
	struct noa_md_tx_tkid_queue_fifo tkid_queue;
	atomic_t to_submit_cnt;
	unsigned int exit_tcp_ss_counter;
	unsigned int send_drb_cnt;
	atomic_t drb_stats;
	bool drb_poll_enable;
	bool drb_poll_mode;
	struct delayed_work tx_done_work;
	struct delayed_work doorbell_work;
};

/* RX: buffer address table */
struct dpmaif_bat {
	u32 buf_addr_low;
	u32 buf_addr_high;
};

struct noa_rx_info {
	u32 pit_pd_seq;
	u32 msg_pit;
	u32 pit_msg_chnl_id;
	u32 pit_msg_checksum;
	u32 pit_msg_err;
	u32 pit_msg_dp;
	u32 pit_msg_hash;
	u32 pit_msg_pro;
	u32 pit_msg_ip;
	u32 normal_bat;
	u32 pit_pd_cur_bid;
	u32 pit_pd_data_len;
	u32 pit_pd_hd_offset;
	u32 pit_continue;
	u64 pit_pd_dma_addr;
};

struct dpmaif_pd_pit {
	u32 pd_header;
	u32 addr_low;
	u32 addr_high;
	u32 pd_footer;
};

struct dpmaif_msg_pit {
	u32 dword1;
	u32 dword2;
	u32 dword3;
	u32 dword4;
};

struct noa_rx_record {
	bool is_msg_pit_recv;
	bool is_previous_msg_pit;
};

struct noa_rx_queue {
	unsigned char id;
	bool started;
	uint64_t pit_base;
	uint64_t pit_dpa_base;
	unsigned int pit_cnt;
	unsigned short pit_wr_idx;
	unsigned short pit_rd_idx;
	unsigned short pit_rel_rd_idx;
	unsigned char pit_seq_expect;
	bool pit_poll_enable;
	atomic_t pit_rel_cnt;
	atomic_t pit_stats;
	bool pit_cnt_err_intr_set;
	unsigned int pit_burst_rel_cnt;
	unsigned int pit_seq_fail_cnt;
	struct noa_rx_record rx_record;
	unsigned int intr_coalesce_frame;
	// Record the latest BID polled by this DLQ pit ring.
	unsigned int pit_bid;
	unsigned char bat_ring_id;
	unsigned int pit_seq_max;
	struct noa_rx_info *rx_info;
	unsigned int attr;
	struct tasklet_struct ncp_md_rx_done_task;
};

struct noa_rx_tkid_free_pool {
	unsigned short rx_tkid;
	struct dpmaif_bat bat;
	u64 noa_data_addr;
};

struct noa_rx_tkid_info {
	unsigned short *rx_tkid;
	spinlock_t rx_tkid_lock;
	struct noa_rx_tkid_free_pool *free_pool;
	unsigned short rx_tkid_free_fore;
	unsigned short rx_tkid_free_rear;
};

struct noa_rx_data_addr {
	u64 noa_va;
};

struct noa_bat_ring {
	enum dpmaif_bat_type type;
	unsigned char id;
	uint64_t bat_base;
	uint64_t bat_dpa_base;
	unsigned int bat_cnt;
	unsigned short bat_wr_idx;
	unsigned short bat_rd_idx;
	/* current max relaod bat cnt */
	unsigned short max_reload_cnt;
	atomic_t to_reload_cnt;
	/* reloaded bat cnt, not doorbelled */
	atomic_t reload_cnt;
	atomic_t bat_stats;
	unsigned int doorbell_th;
	unsigned int buf_size;
	unsigned long *mask_tbl;
	bool bat_cnt_err_intr_set;
	bool dynamic_reload;
	struct noa_rx_tkid_info rx_tkid_info;
	struct noa_rx_data_addr *noa_data_addr;
};

struct noa_bat_info {
	unsigned int max_mtu;
	bool frag_bat_enabled;
	struct noa_bat_ring normal_bat_ring;
	struct noa_bat_ring frag_bat_ring;
};

struct modem_fw_ring_ops {
	int (*begin_processing)(struct noa_ring_wrapper *ring);
	int (*complete_processing)(struct noa_ring_wrapper *ring);
	int (*read)(struct noa_ring_wrapper *ring, void *data, size_t len);
	int (*write)(struct noa_ring_wrapper *ring, void *data, size_t len);
	int (*tail_inc)(struct noa_ring_wrapper *ring);
	bool (*is_empty)(struct noa_ring_wrapper *ring);
	int (*init)(struct noa_md_fw *md_fw);
	void (*exit)(struct noa_md_fw *md_fw, int ring_type);
	int (*activate)(struct noa_ring_wrapper *ring, int ring_type);
	int (*deactivate)(struct noa_ring_wrapper *ring, int ring_type);
	const char *(*get_name)(struct noa_ring_wrapper *ring);
};

struct noa_md_fw_tx {
	struct noa_md_fw_ring tx_ring;
	uint32_t txq_cnt;
	struct noa_tx_queue txqs[kMaxUlQueueSize];
	const struct modem_fw_ring_ops *ring_ops;
	struct tasklet_struct md_tx_task;
	struct tasklet_struct apc2ncp_task;
	std::atomic<uint32_t> isr_apc2ncp_bitmask;
	struct mutex read_desc_lock;
	std::atomic<bool> stop_read_desc_flag;
};

struct noa_md_fw_rx {
	struct noa_md_fw_ring rx_ring[kNoaModemRingRxDataEnd];
	uint32_t rxq_cnt;
	struct noa_rx_queue dpmaif_rxqs[kMaxDlQueueSize];
	const struct modem_fw_ring_ops *ring_ops;
	uint32_t bat_ring_num;
	struct noa_bat_info bat_infos[kMaxBatInfoSize];
	struct tasklet_struct rx_reload_task;
	struct tasklet_struct rx_batcnt_len_err_task;
	struct tasklet_struct rx_tkid_free_poll_task;
	std::atomic<uint32_t> isr_refill_bitmask;
	char packet_buffers[kNoaModemRingRxDataEnd]
			   [NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE * NOA_FW_RING_SIZE];
	uint32_t switch_cmd;
};

struct noa_tx_buffer_pool {
	void *va_base;
	dma_addr_t pa_base;
	noa_ring_producer ring;
};

struct noa_shared_memory_info {
	uint64_t addr;
	uint32_t size;
};

struct noa_md_fw {
	struct noa_md_fw_tx *tx;
	struct noa_md_fw_rx *rx;
	struct noa_tx_buffer_pool tx_buffer_pool;
	struct timer_list ring_rel_ctrl_timer;
	struct noa_shared_memory_info shared_mem_info;
	uint32_t wwan_ifindex_table[MAX_WWAN_IFINDEX_TABLE_SIZE];
	noa::driver::modem::ncp::ModemHifAlcedo *hif;
};

struct noa_tx_queue_pb {
	uint64_t drb_base;
	uint64_t drb_dpa_base;
	int32_t drb_cnt;
	int32_t db_delay_ms;
	int32_t burst_submit_cnt;
	int32_t drb_wr_idx;
	int32_t drb_rd_idx;
	int32_t drb_rel_rd_idx;
};

struct noa_rx_queue_pb {
	uint64_t pit_base;
	uint64_t pit_dpa_base;
	unsigned int pit_cnt;
	unsigned char bat_ring_id;
	unsigned int pit_seq_max;
	int32_t pit_wr_idx;
	int32_t pit_rd_idx;
	int32_t pit_rel_rd_idx;
};

struct noa_bat_ring_pb {
	uint64_t bat_base;
	uint64_t bat_dpa_base;
	unsigned int buf_size;
	unsigned int bat_cnt;
	unsigned short max_reload_cnt;
	int32_t bat_wr_idx;
	int32_t bat_rd_idx;
	uint64_t mask_table_dpa_base;
	uint64_t tkid_table_dpa_base;
	uint64_t buffer_table_dpa_base;
};

struct noa_bat_info_pb {
	bool frag_bat_enabled;
	struct noa_bat_ring_pb normal_bat_ring;
	struct noa_bat_ring_pb frag_bat_ring;
};

struct noa_tx_buffer_pool_pb {
	void *va_base;
	dma_addr_t pa_base;
};

struct noa_md_protobuf {
	struct noa_tx_queue_pb txqs[kMaxUlQueueSize];
	struct noa_rx_queue_pb rxqs[kMaxDlQueueSize];
	struct noa_bat_info_pb bat_infos[kMaxBatInfoSize];
	struct noa_tx_buffer_pool_pb tx_buffer_pool;
	struct noa_shared_memory_info shared_mem_info;
	noa::driver::modem::ncp::ModemHifAlcedo *hif;
};
#endif

// Sync from mtk_pkt_format.h
// @brief Mtk message drb(Descriptor Ring Buffer).
struct MtkMessageDescriptorRingBuffer {
	uint16_t descriptor_type : 2;
	uint16_t continue_bit : 1;
	uint16_t noa_tcp_in_slow_start : 1;
	uint16_t reserved_1 : 12; // 13U->12U
	uint16_t packet_length;
	uint16_t count_l_psn;
	uint8_t channel_id : 8;
	uint8_t network_type : 3;
	uint8_t reserved_2 : 1;
	uint8_t ipv4 : 1;
	uint8_t l4_checksum : 1;
	uint8_t reserved_3 : 2;
	uint32_t reserved_4;
	uint32_t reserved_5;
};

// @brief Mtk payload drb(Descriptor Ring Buffer).
struct MtkPayloadDescriptorRingBuffer {
	uint16_t descriptor_type : 2;
	uint16_t continue_bit : 1;
	uint16_t reserved_1 : 13;
	uint16_t data_length;
	uint32_t address_low;
	uint32_t address_high;
	uint32_t reserved_2;
};

// @brief Mtk message pit(Packet Information Table) used in ncp.
struct MtkMessagePacketInfoTable {
	uint8_t packet_type : 1;
	uint8_t continue_bit : 1;
	uint8_t chechsum_result : 2;
	uint8_t error_bit : 1;
	uint8_t source_queue_id : 3;
	uint8_t hpc_index : 4;
	uint8_t reserved_1 : 4;
	uint8_t channel_id;
	uint8_t network_type : 3;
	uint8_t destination_queue_id : 3;
	uint8_t reserved_2 : 1;
	uint8_t drop_bit : 1;
	uint32_t count_l_psn : 18;
	uint8_t flow : 5;
	uint8_t reserved_3 : 1;
	uint8_t reserved_4 : 3;
	uint8_t hp_id : 5;
	uint16_t reserved_5;
	uint8_t protocol : 2;
	uint8_t reserved_6 : 6;
	uint8_t hash;
	uint8_t pit_sequence;
	uint16_t reserved_7 : 12;
	uint8_t mr : 2;
	uint8_t reserved_8 : 1;
	uint8_t ip : 1;
	uint8_t uplink_queue_done : 6;
	uint8_t downlink_queue_done : 2;
};

// @brief Mtk normal payload pit(Packet Information Table) used in ncp.
struct MtkPayloadPacketInfoTable {
	uint8_t packet_type : 1;
	uint8_t continue_bit : 1;
	uint8_t buffer_type : 1;
	uint16_t buffer_id : 13;
	uint16_t data_length;
	uint32_t address_low;
	uint32_t address_high;
	uint8_t pit_sequence;
	uint8_t h_buffer_id : 3;
	uint8_t reserved_1;
	uint8_t header_offset : 5;
	uint8_t uplink_queue_done : 6;
	uint8_t downlink_queue_done : 2;
};

// @brief Mtk bat(Buffer Address Table) used in ncp.
struct MtkBufferAddressTable {
	uint32_t address_low;
	uint32_t address_high;
};

struct noa_pd_pit {
	uint32_t pd_header;
	uint32_t addr_low;
	uint32_t addr_high;
	uint32_t pd_footer;
};

struct noa_msg_pit {
	uint32_t dword1;
	uint32_t dword2;
	uint32_t dword3;
	uint32_t dword4;
};

#endif /* __NCP_MODEM_DATA_H__ */
