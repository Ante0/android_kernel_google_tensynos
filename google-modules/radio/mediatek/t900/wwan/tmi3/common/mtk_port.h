/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_PORT_H__
#define __MTK_PORT_H__

#include <linux/bits.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/radix-tree.h>
#include <linux/skbuff.h>
#include <linux/termios.h>
#include <linux/types.h>

#include "mtk_ctrl_plane.h"
#include "mtk_dev.h"
#include "mtk_fsm.h"

#define MTK_PEER_ID_MASK			(0xF000)
#define MTK_PEER_ID_SHIFT			(12)
#define MTK_PEER_ID(ch)				(((ch) & MTK_PEER_ID_MASK) >> MTK_PEER_ID_SHIFT)
#define MTK_PEER_ID_SAP				(0x1)
#define MTK_PEER_ID_MD				(0x2)
#define MTK_CH_ID_MASK				(0x0FFF)
#define MTK_CH_ID(ch)				((ch) & MTK_CH_ID_MASK)
#define MTK_CH_NUM					(34)

#define MTK_PORT_NAME_HDR			"wwanD"

#define MTK_DSS_NAME				"MBIMDSS"
#define MTK_DSS_NAME_LEN			(7)
#define MTK_DSS_PORT(n)				(MTK_DSS_NAME#n)

#define MTK_GCCI_NAME				"GCCI"
#define MTK_GCCI_PORT(n)			(MTK_GCCI_NAME#n)

#define MTK_DFLT_MAX_DEV_CNT			(10)
#define MTK_DFLT_PORT_NAME_LEN			(20)
#define MTK_DFLT_FULL_NAME_LEN			(50)

/* Mapping MTK_PEER_ID and mtk_port_tbl index */
#define MTK_PORT_TBL_TYPE(ch)			(MTK_PEER_ID(ch) - 1)

/* ccci header length + reserved space that is used in exception flow */
#define MTK_CCCI_H_ELEN		(128)

#define MTK_HDR_FLD_AST		((u32)BIT(31))
#define MTK_HDR_FLD_SEQ		GENMASK(30, 16)
#define MTK_HDR_FLD_CHN		GENMASK(15, 0)

#define MTK_INFO_FLD_EN		((u16)BIT(15))
#define MTK_INFO_FLD_CHID	GENMASK(14, 0)

#define MTK_FLOWCTRL_TIMEOUT	(HZ * 5)

#define MTK_DFLT_TX_DUMP_CNT			2
#if IS_ENABLED(CONFIG_GOOGLE_B493109074_DEBUG)
#define MTK_DFLT_RX_DUMP_CNT			5
#define MTK_SKB_DUMP_SIZE_MAX		17
#else
#define MTK_DFLT_RX_DUMP_CNT			2
#define MTK_SKB_DUMP_SIZE_MAX		9
#endif
/* For checking if in MBIM mode */
extern unsigned short brom_dl_flag;

/* enum mtk_port_status - Descript port's some status.
 * @PORT_S_DFLT: default value when port initialize.
 * @PORT_S_ENABLE: port has been enabled.
 * @PORT_S_OPEN: port has been opened.
 * @PORT_S_RD: port read is allowed.
 * @PORT_S_WR: port write is allowed.
 * @PORT_S_FLUSH: driver is flushing.
 * @PORT_S_ON_STALE_LIST: port is on stale list.
 * @PORT_S_STOP: port is stopped for TX/RX
 */
enum mtk_port_status {
	PORT_S_DFLT = 0,
	PORT_S_ENABLE,
	PORT_S_OPEN,
	PORT_S_RD,
	PORT_S_WR,
	PORT_S_FLUSH,
	PORT_S_ON_STALE_LIST,
	PORT_S_STOP,
};

enum mtk_ccci_ch {
	/* to sAP */
	CCCI_SAP_CONTROL_RX			= 0x1000,
	CCCI_SAP_CONTROL_TX			= 0x1001,
	CCCI_SAP_LBIT_RX			= 0x1002,
	CCCI_SAP_LBIT_TX			= 0x1003,
	CCCI_SAP_GNSS_RX			= 0x1004,
	CCCI_SAP_GNSS_TX			= 0x1005,
	CCCI_SAP_META_RX			= 0x1006,
	CCCI_SAP_META_TX			= 0x1007,
	CCCI_SAP_LOG_RX				= 0x1008,
	CCCI_SAP_LOG_TX				= 0x1009,
	CCCI_SAP_ADB_RX				= 0x100a,
	CCCI_SAP_ADB_TX				= 0x100b,
	CCCI_SAP_MINIDUMP_NOTIFY_RX		= 0x1010,
	CCCI_SAP_MINIDUMP_NOTIFY_TX		= 0x1011,
	CCCI_SAP_GCCI0_RX			= 0x1012,
	CCCI_SAP_GCCI0_TX			= 0x1013,
	CCCI_SAP_GCCI1_RX			= 0x1014,
	CCCI_SAP_GCCI1_TX			= 0x1015,
	CCCI_SAP_GCCI2_RX			= 0x1016,
	CCCI_SAP_GCCI2_TX			= 0x1017,
	CCCI_SAP_TIMESYNC_RX			= 0x1018,
	CCCI_SAP_TIMESYNC_TX			= 0x1019,
	CCCI_SAP_AGPS_RX			= 0x101A,
	CCCI_SAP_AGPS_TX			= 0x101B,
	CCCI_SAP_RFS_RX				= 0x101C,
	CCCI_SAP_RFS_TX				= 0x101D,
	CCCI_SAP_LPM_RX				= 0x101E,
	CCCI_SAP_LPM_TX				= 0x101F,
	CCCI_SAP_ATA_RX				= 0x1020,
	CCCI_SAP_ATA_TX				= 0x1021,
	CCCI_SAP_GCCI3_RX			= 0x1022,
	CCCI_SAP_GCCI3_TX			= 0x1023,
	CCCI_SAP_GCCI4_RX			= 0x1024,
	CCCI_SAP_GCCI4_TX			= 0x1025,
	CCCI_SAP_GCCI5_RX			= 0x1026,
	CCCI_SAP_GCCI5_TX			= 0x1027,
	CCCI_SAP_AUD_CFG_RX			= 0x1028,
	CCCI_SAP_AUD_CFG_TX			= 0x1029,
	BROM_INDEX_RX				= 0x1040,
	BROM_INDEX_TX				= 0x1041,
	DUMP_INDEX_RX				= 0x1042,
	DUMP_INDEX_TX				= 0x1043,
	/* fastboot_download port */
	FBOOT_INDEX_RX				= 0x1044,
	FBOOT_INDEX_TX				= 0x1045,
	/* firmware_download port */
	FW_INDEX_RX				= 0x1046,
	FW_INDEX_TX				= 0x1047,

	/* to MD */
	CCCI_CONTROL_RX				= 0x2000,
	CCCI_CONTROL_TX				= 0x2001,
	CCCI_UART1_RX				= 0x2006,
	CCCI_UART1_RX_ACK			= 0x2007,
	CCCI_UART1_TX				= 0x2008,
	CCCI_UART1_TX_ACK			= 0x2009,
	CCCI_UART2_RX				= 0x200A,
	CCCI_UART2_RX_ACK			= 0x200B,
	CCCI_UART2_TX				= 0x200C,
	CCCI_UART2_TX_ACK			= 0x200D,
	CCCI_MD_RFS_RX				= 0x200E,
	CCCI_MD_RFS_TX				= 0x200F,
	CCCI_MD_LOG_RX				= 0x202A,
	CCCI_MD_LOG_TX				= 0x202B,
	CCCI_LB_IT_RX				= 0x203E,
	CCCI_LB_IT_TX				= 0x203F,
	CCCI_STATUS_RX				= 0x2043,
	CCCI_STATUS_TX				= 0x2044,
	CCCI_MIPC_RX				= 0x20CE,
	CCCI_MIPC_TX				= 0x20CF,
	CCCI_MBIM_RX				= 0x20D0,
	CCCI_MBIM_TX				= 0x20D1,
	CCCI_DSS0_RX				= 0x20D2,
	CCCI_DSS0_TX				= 0x20D3,
	CCCI_DSS1_RX				= 0x20D4,
	CCCI_DSS1_TX				= 0x20D5,
	CCCI_DSS2_RX				= 0x20D6,
	CCCI_DSS2_TX				= 0x20D7,
	CCCI_DSS3_RX				= 0x20D8,
	CCCI_DSS3_TX				= 0x20D9,
	CCCI_DSS4_RX				= 0x20DA,
	CCCI_DSS4_TX				= 0x20DB,
	CCCI_DSS5_RX				= 0x20DC,
	CCCI_DSS5_TX				= 0x20DD,
	CCCI_DSS6_RX				= 0x20DE,
	CCCI_DSS6_TX				= 0x20DF,
	CCCI_DSS7_RX				= 0x20E0,
	CCCI_DSS7_TX				= 0x20E1,
	CCCI_MD_LOG_NTFY_RX			= 0x20E2,
	CCCI_MD_LOG_NTFY_TX			= 0x20E3,
	CCCI_MIPC1_RX				= 0x20E4,
	CCCI_MIPC1_TX				= 0x20E5,
	CCCI_MIPC7_RX				= 0x20F0,
	CCCI_MIPC7_TX				= 0x20F1,
	CCCI_GNSS_CONTROL_RX			= 0x3000,
	CCCI_GNSS_CONTROL_TX			= 0x3001,
	CCCI_GC4_0_RX				= 0x3002,
	CCCI_GC4_0_TX				= 0x3003,
	CCCI_GC4_1_RX				= 0x3004,
	CCCI_GC4_1_TX				= 0x3005,
	CCCI_GC4_2_RX				= 0x3006,
	CCCI_GC4_2_TX				= 0x3007,
	CCCI_GC4_3_RX				= 0x3008,
	CCCI_GC4_3_TX				= 0x3009,
	CCCI_GC4_4_RX				= 0x300A,
	CCCI_GC4_4_TX				= 0x300B,
	CCCI_GC4_5_RX				= 0x300C,
	CCCI_GC4_5_TX				= 0x300D,
};

enum mtk_port_flag {
	PORT_F_RAW_DATA = BIT(0),
	PORT_F_BLOCKING = BIT(1),
	PORT_F_ALLOW_DROP = BIT(2),
	PORT_F_FLOWCTRL = BIT(3),
	PORT_F_FORCE_NO_SPILT_PACKET = BIT(4),
	PORT_F_DUMP = BIT(5),
	PORT_F_FORCE_SEND = BIT(6),
	PORT_F_MDEE_SPT = BIT(7),
	PORT_F_MDEE_DL_ONLY = BIT(8)
};

enum mtk_port_tbl {
	PORT_TBL_SAP,
	PORT_TBL_MD,
	PORT_TBL_GNSS,
	PORT_TBL_MAX
};

enum mtk_port_type {
	PORT_TYPE_CHAR,
	PORT_TYPE_INTERNAL,
	PORT_TYPE_PROPRIETARY,
	PORT_TYPE_WWAN,
	PORT_TYPE_RELAYFS,
	PORT_TYPE_MAX
};

enum mtk_port_flowctrl_msg {
	FLOWCTRL_MSG_REACHABLE,
	FLOWCTRL_MSG_UNREACHABLE,
};

enum mtk_skb_record_type {
	TX_DROP = 1,
	TX_FREE,
	TX_RECORD_MAX = 0x80,
	RX_DROP_WITH_HEADER,
	RX_DROP_NO_HEADER,
	RX_DROP_HEADER_ERR
};

struct mtk_cdev_port {
	struct cdev *cdev;
	struct ktermios termios;
	int mdmbits;
};

struct mtk_internal_port {
	void *arg;
	int (*recv_cb)(void *arg, struct sk_buff *skb);
};

struct mtk_proprietary_port {
	struct device *dev;
	int id;
};

struct mtk_wwan_port {
	/* w_lock Protect wwan_port when recv data and disable port at the same time */
	struct mutex w_lock;
	int w_type;
	void *w_port;
};

struct mtk_relayfs_port {
	struct dentry *ctrl_file;
	struct dentry *d_wwan;
	struct rchan *rc;
	atomic_t is_full;
	char ctrl_file_name[MTK_DFLT_FULL_NAME_LEN];
};

/* union mtk_port_priv - Contains private data for different type of ports.
 * @c_priv: private data for character device port.
 * @i_priv: private data for internal other user.
 * @p_priv: private data of proprietary_port for user of other kernel driver.
 * @w_priv: private data for wwan port.
 * @rf_priv: private data for relayfs port
 */
union mtk_port_priv {
	struct mtk_cdev_port c_priv;
	struct mtk_internal_port i_priv;
	struct mtk_proprietary_port p_priv;
	struct mtk_wwan_port w_priv;
	struct mtk_relayfs_port rf_priv;
};

/* struct mtk_port_cfg - Contains port's basic configuration.
 * @tx_ch: TX channel id (peer id (bit 12~15)+ channel id(bit 0 ~11)).
 * @rx_ch: RX channel id.
 * @type: port type.
 * @name: port name.
 * @flags: port flags.
 * @tx_dump_cnt: port TX skb dump count.
 * @rx_dump_cnt: port RX skb dump count.
 */
struct mtk_port_cfg {
	enum mtk_ccci_ch tx_ch;
	enum mtk_ccci_ch rx_ch;
	enum mtk_port_type type;
	char name[MTK_DFLT_PORT_NAME_LEN];
	unsigned short flags;
	unsigned int tx_dump_cnt;
	unsigned int rx_dump_cnt;
};

/* struct mtk_port - Represents a port of the control plane.
 * @info: port's basic configuration.
 * @kref: reference count.
 * @enable: enable msg from modem.
 * @flowctrl_acked: receive flowctrl reachable msg ack.
 * @need_dump: once port was enabled, this flag will set for dump.
 * @status: port's current state, like open, enable etc.
 * @minor: device minor id offset.
 * @tx_seq: TX sequence id for mtk_ccci_header.
 * @rx_seq: RX sequence id for mtk_ccci_header.
 * @tx_err_cnt: TX error packet count.
 * @rx_err_cnt: RX error packet count.
 * @tx_mtu: TX max trans unit (64k at most).
 * @rx_mtu: RX max trans unit (64k at most).
 * @rx_skb_list: RX skb buffer.
 * @tx_skb_record_list tx skb record list for dump.
 * @rx_skb_record_list rx skb record list for dump.
 * @rx_data_len: data length in RX skb buffer.
 * @rx_buf_size: max size of RX skb buffer.
 * @dump_rx_budget: buget for rx dump.
 * @trb_wq: wait queue for trb submit.
 * @rx_wq: wait queue for reading.
 * @write_lock: mutex lock used in user write function.
 * @read_buf_lock: mutex lock used in user read function.
 * @stale_entry: list head entry for stale list.
 * @dev_str: string to identify the device which the port belongs.
 * @port_mngr: point to mtk_port_mngr.
 * @priv: private data for different type.
 * @fc_lock: protect reachable msg and unreachable msg send flow.
 */
struct mtk_port {
	struct mtk_port_cfg info;
	struct kref kref;
	bool enable;
	bool flowctrl_acked;
	bool need_dump;
	unsigned long status;
	unsigned int minor;
	unsigned short tx_seq;
	unsigned short rx_seq;
	unsigned long long tx_err_cnt;
	unsigned long long rx_err_cnt;
	u32 tx_mtu;
	u32 rx_mtu;
	u32 tx_frag_size;
	u32 rx_frag_size;
	u32 max_rx_list_cnt;
	struct sk_buff_head rx_skb_list;
	struct sk_buff_head tx_skb_record_list;
	struct sk_buff_head rx_skb_record_list;
	unsigned int rx_data_len;
	unsigned int rx_buf_size;
	unsigned int dump_rx_budget;
	wait_queue_head_t trb_wq;
	wait_queue_head_t rx_wq;
	/* Use write_lock to lock user's write and disable thread */
	struct mutex write_lock;
	/* Used to lock user's read thread */
	struct mutex read_buf_lock;
	struct list_head stale_entry;
	char dev_str[MTK_DEV_STR_LEN];
	struct mtk_port_mngr *port_mngr;
	union mtk_port_priv priv;
	/* Used to lock reachable msg and unreachable msg send flow */
	struct mutex fc_lock;
	u8 log_rg_offset;
};

/* struct mtk_port_mngr - Include all the port information of a device.
 * @ctrl_blk: pointer to mtk_ctrl_blk structure.
 * @port_tbl: the table which manages sAP ports and md ports.
 * @port_attr_kobj: pointer to attribute kobject structure.
 * @port_cnt: number of ports.
 * @dev_id: index to identify the device.
 * @port_mngr_lock: protecting the use of port_mngr in user callback functions
 */
struct mtk_port_mngr {
	struct mtk_ctrl_blk *ctrl_blk;
	struct radix_tree_root port_tbl[PORT_TBL_MAX];
	struct kobject *port_attr_kobj;
	unsigned int port_cnt;
	struct mtk_port *flowctrl_port[PORT_TBL_MAX];
	struct mtk_port *miscctrl_port[PORT_TBL_MAX];
	int lb_mode[PORT_TBL_MAX];
	wait_queue_head_t flowctrl_wq;
	struct dentry *dentry;
	int dev_id;
};

struct mtk_stale_list {
	struct list_head entry;
	struct list_head ports;
	char dev_str[MTK_DEV_STR_LEN];
	int dev_id;
	rwlock_t port_mngr_lock;
};

struct mtk_port_flowctrl_blk {
	__le16 rx_ch;
	__le16 msg;
} __packed;

struct mtk_port_info {
	__le16 channel;
	__le16 reserved;
} __packed;

struct mtk_port_enum_msg {
	__le32 head_pattern;
	__le16 port_cnt;
	__le16 version;
	__le32 tail_pattern;
	u8 data[];
} __packed;

enum mtk_port_cfg_type {
	PORT_CFG_RPC = 1,
	PORT_CFG_FS,
	PORT_CFG_SYSMSG,
	PORT_CFG_CH_INFO,
	PORT_CFG_HIF_INFO,
};

enum mtk_port_cfg_node_type {
	PORT_NORMAL = 0,
	PORT_NEED_CONFIG,
};

enum mtk_port_cfg_msg_type {
	PORT_CFG_MSG_REQUEST = 1,
	PORT_CFG_MSG_RESPONSE,
};

struct mtk_port_cfg_ch_info {
	__le16 dl_ch_id;
	__le16 ul_ch_id;
	u8 dl_hw_queue_id;
	u8 ul_hw_queue_id;
	u8 reserve[2];
	u8 node_type;
	u8 peer_id;
	u8 reserved;
	u8 port_name_len;
	char port_name[MTK_DFLT_PORT_NAME_LEN];
} __packed;

struct mtk_port_cfg_hif_info {
	u8 dl_hw_queue_id;
	u8 ul_hw_queue_id;
	u8 peer_id;
	u8 reserved;
	__le32 dl_hw_queue_mtu;
	__le32 ul_hw_queue_mtu;
} __packed;

/* struct mtk_port_cfg_header - Message from device to configure unified port
 * @port_config_len: data length.
 * @cfg_type: 1:RPC / 2:FS/ 3:SYSMSG/ 4:Channel/ 5:Hif
 * @msg_type: 1:request/ 2:response.
 * @is_enable: 0:disable/ 1:enable.
 * @reserve: reserve bytes.
 * @data: the data is channel config information @ref mtk_port_cfg_ch_info or
 * hif config information @ref mtk_port_cfg_hif_info, following the cfg_type value.
 */
struct mtk_port_cfg_header {
	__le16 port_config_len;
	u8 cfg_type;
	u8 msg_type;
	u8 is_enable;
	u8 reserve[3];
	u8 data[];
} __packed;

struct mtk_ccci_header {
	__le32 packet_header;
	__le32 packet_len;
	__le32 status;
	__le32 ex_msg;
};

struct mtk_port_layer_cfg {
	struct mtk_port_cfg *port_cfg;
	int (*get_port_cnt)(void);
};

extern const struct port_ops *ports_ops[PORT_TYPE_MAX];

void mtk_port_release(struct kref *port_kref);
void mtk_port_pkt_record(struct mtk_port *port, struct sk_buff *skb,
			 enum mtk_skb_record_type type);
void mtk_ports_dump(struct mtk_md_dev *mdev);
void mtk_port_trb_free(struct kref *port_kref);
struct mtk_port *mtk_port_search_by_name(struct mtk_port_mngr *port_mngr, char *name);
void mtk_port_stale_list_grp_cleanup(void);
int mtk_port_add_header(struct sk_buff *skb);
struct mtk_ccci_header *mtk_port_strip_header(struct sk_buff *skb);
int mtk_port_status_check(struct mtk_port *port);
int mtk_port_send_data(struct mtk_port *port, void *data);
int mtk_port_status_update(struct mtk_md_dev *mdev, void *data, int len);
int mtk_port_tbl_update(struct mtk_md_dev *mdev, void *data, unsigned int len);
int mtk_port_ch_enable(struct mtk_port *port);
int mtk_port_ch_disable(struct mtk_port *port);
void mtk_port_mngr_fsm_state_handler(struct mtk_fsm_param *fsm_param, void *arg);
void mtk_port_mngr_fsm_state_handler_late(struct mtk_fsm_param *fsm_param, void *arg);
int mtk_port_mngr_vq_status_check(struct sk_buff *skb);
int mtk_port_mngr_init(struct mtk_ctrl_blk *ctrl_blk, struct mtk_port_cfg *port_cfg, int port_cnt);
void mtk_port_mngr_exit(struct mtk_ctrl_blk *ctrl_blk);
void mtk_port_trb_init(struct mtk_port *port, struct trb *trb, enum mtk_trb_cmd_type cmd,
		       int (*trb_complete)(struct sk_buff *skb));
int mtk_port_send_flowctrl_msg(struct mtk_port *port, int msg_id);
rwlock_t *mtk_port_get_port_mngr_lock(const char *dev_str);
int mtk_port_rx_pre_check(struct sk_buff *skb, void *priv);

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
struct mtk_port *mtk_port_search_by_id(struct mtk_port_mngr *port_mngr, int rx_ch);
#endif
#endif /* __MTK_PORT_H__ */
