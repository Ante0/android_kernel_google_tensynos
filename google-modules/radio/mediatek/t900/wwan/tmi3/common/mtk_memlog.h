/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2023, MediaTek Inc.
 */

#ifndef __MTK_MEMLOG_H__
#define __MTK_MEMLOG_H__

#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

#include "mtk_dev.h"

#define MTK_DFLT_MEMLOG_NAME_LEN		(30)
#define MTK_BUFFER_BASE_SIZE			(1 * 1024 * 1024)
#define MTK_BUDDY_MAX_SIZE			(4 * 1024 * 1024)
#define MTK_BUFFER_MIN_SIZE			MTK_BUFFER_BASE_SIZE
#define MTK_BUFFER_DIVIDE_MAX			(MTK_BUDDY_MAX_SIZE / MTK_BUFFER_BASE_SIZE)
#define MTK_MEMLOG_LINE_MAX_LENGTH		(1024)
#define MTK_DFLT_MEMLOG_ATTR_NAME_LEN		(20)
#define MTK_REGION_HEADER_LEN			(64)
#define MTK_DFLT_BUILD_INFO_HEADER_LEN		(128)
#define MTK_DFLT_TIME_INFO_HEADER_LEN		(64)
#define MTK_DFLT_HEADER_LEN \
	((MTK_DFLT_BUILD_INFO_HEADER_LEN + MTK_DFLT_TIME_INFO_HEADER_LEN) * MTK_BUFFER_DIVIDE_MAX)
#define MTK_DFLT_MAX_MEMLOG_DEV_CNT		(10)
#define MTK_MAX_MEMLOG_BUF_CNT			(8)

/* enum mtk_memlog_region_id - enumerate the ID of region.
 * @MTK_MEMLOG_RG_COMMON: common region for MTK_DBG log.
 * @MTK_MEMLOG_RG_PM_H_FREQ: pm high frequency log.
 * @MTK_MEMLOG_RG_CTRL_DUMP: dump ctrl information.
 * @MTK_MEMLOG_RG_CTRL_MISC: ctrl misc info.
 * @MTK_MEMLOG_RG_CTRL_H_FREQ: ctrl high frequency log.
 * @MTK_MEMLOG_RG_DATA_TX: all TX data traffic log.
 * @MTK_MEMLOG_RG_DATA_TX_H_FREQ: all TX data high frequency traffic log.
 * @MTK_MEMLOG_RG_DATA_RX: rx data traffic log.
 * @MTK_MEMLOG_RG_DATA_MISC: bat and doorbell region, low frequency of use.
 * @MTK_MEMLOG_RG_DATA_DUMP: dump data information.
 * @MTK_MEMLOG_RG_DATA_IRQ: DPMAIF irq tophalf log.
 * @MTK_MEMLOG_RG_HIF_DUMP: HIF register dump information.
 */
enum mtk_memlog_region_id {
	MTK_MEMLOG_RG_COMMON = 0,
	MTK_MEMLOG_RG_PM_H_FREQ,
	MTK_MEMLOG_RG_CTRL_DUMP = 5,
	MTK_MEMLOG_RG_CTRL_MISC,
	MTK_MEMLOG_RG_CTRL_H_FREQ,
	MTK_MEMLOG_RG_DATA_TX = 10,
	MTK_MEMLOG_RG_DATA_TX_H_FREQ = 15,
	MTK_MEMLOG_RG_DATA_RX = 16,
	MTK_MEMLOG_RG_DATA_RX_H_FREQ = 19,
	MTK_MEMLOG_RG_DATA_MISC = 22,
	MTK_MEMLOG_RG_DATA_DUMP = 23,
	MTK_MEMLOG_RG_DATA_IRQ = 25,
	MTK_MEMLOG_RG_HIF_DUMP = 35,
	MTK_MEMLOG_RG_STATS = 44,
	MTK_MEMLOG_RG_MAX = 45
};

#ifdef CONFIG_MTK_MEMLOG_EVENT_SUPPORT
#define MEMLOG_EVENT_MAGIC		0x5A5AA5A5
#define MEMLOG_EVENT_ENABLE_FLAG	BIT(4)

enum memlog_event_version {
	MEMLOG_EVENT_VERSION = 4,
};

enum memlog_event_type {
	CTRL_ADD_HEADER,
	CTRL_RX_DISPATCH,
	CTRL_PORT_READ,
	/* Add new value before this line */
	EXTERN_EVT_TYPE = 1024,
};

struct memlog_add_info {
	u8 cpu_id;
	u16 pid_no;
	u64 time_stamp;
} __packed;

struct memlog_event_msg {
	u32 magic_num;
	u16 event_type:12;
	u16 event_version:4;
	u16 reserve;
	struct memlog_add_info add_info;
} __packed;

void *mtk_memlog_req_address(struct mtk_md_dev *mdev, enum mtk_memlog_region_id region_id,
			     u32 event_size);
void mtk_memlog_add_info(struct memlog_event_msg *event_msg);
void mtk_memlog_req_done(struct mtk_md_dev *mdev, enum mtk_memlog_region_id region_id);

static inline void mtk_memlog_event_msg_init(struct memlog_event_msg *event_msg,
					     u16 event_type)
{
	event_msg->magic_num = MEMLOG_EVENT_MAGIC;
	event_msg->event_type = event_type;
	event_msg->event_version = MEMLOG_EVENT_VERSION;
	event_msg->reserve = 0;
}

struct event_ctrl_add_header {
	struct memlog_event_msg event_msg;
	__le32 pkt_len;
	__le32 header_status;
} __packed;

#define MTK_DBG_CTRL_ADD_HEADER(mdev, __pkt_len, __header_status, __rg_offset) \
do {\
	struct event_ctrl_add_header *event_ctrl_add_header; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 rg_offset = __rg_offset; \
	event_ctrl_add_header = mtk_memlog_req_address(__mdev, \
						       MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset, \
						       sizeof(struct event_ctrl_add_header)); \
	if (!event_ctrl_add_header) \
		break; \
	event_ctrl_add_header->pkt_len = __pkt_len; \
	event_ctrl_add_header->header_status = __header_status; \
	mtk_memlog_event_msg_init(&event_ctrl_add_header->event_msg, CTRL_ADD_HEADER); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset); \
} while (0)

struct event_ctrl_rx_dispatch {
	struct memlog_event_msg event_msg;
	__le32 pkt_len;
	__le32 header_status;
} __packed;

#define MTK_DBG_CTRL_RX_DISPATCH(mdev, __pkt_len, __header_status, __rg_offset) \
do {\
	struct event_ctrl_rx_dispatch *event_ctrl_rx_dispatch; \
	struct mtk_md_dev *__mdev = mdev;\
	u8 rg_offset = __rg_offset; \
	event_ctrl_rx_dispatch = mtk_memlog_req_address(__mdev, \
							MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset, \
							sizeof(struct event_ctrl_rx_dispatch)); \
	if (!event_ctrl_rx_dispatch) \
		break; \
	event_ctrl_rx_dispatch->pkt_len = __pkt_len; \
	event_ctrl_rx_dispatch->header_status = __header_status; \
	mtk_memlog_event_msg_init(&event_ctrl_rx_dispatch->event_msg, CTRL_RX_DISPATCH); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset); \
} while (0)

struct event_ctrl_port_read {
	struct memlog_event_msg event_msg;
	int channel_id;
	unsigned int total_read;
	int merge_cnt;
	unsigned int left_len;
} __packed;

#define MTK_DBG_CTRL_PORT_READ(mdev, __channel_id, __total_read, \
				__merge_cnt, __left_len, __rg_offset) \
do { \
	struct event_ctrl_port_read *event_ctrl_port_read; \
	struct mtk_md_dev *__mdev = mdev;\
	u8 rg_offset = __rg_offset; \
	event_ctrl_port_read = mtk_memlog_req_address(__mdev, \
						      MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset, \
						      sizeof(struct event_ctrl_port_read)); \
	if (!event_ctrl_port_read) \
		break; \
	event_ctrl_port_read->channel_id = __channel_id; \
	event_ctrl_port_read->total_read = __total_read; \
	event_ctrl_port_read->merge_cnt = __merge_cnt; \
	event_ctrl_port_read->left_len = __left_len; \
	mtk_memlog_event_msg_init(&event_ctrl_port_read->event_msg, CTRL_PORT_READ); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset); \
} while (0)

#else
#define MEMLOG_EVENT_ENABLE_FLAG	(0x0)

#define MTK_DBG_CTRL_ADD_HEADER(mdev, len, status, rg_offset) \
	MTK_DBG(mdev, MTK_DBG_CTRL_TX, MTK_MEMLOG_RG_CTRL_H_FREQ + (rg_offset), \
		"pkt_len:0x%x header_status:0x%x\n", len, status)

#define MTK_DBG_CTRL_RX_DISPATCH(mdev, len, status, rg_offset) \
	MTK_DBG(mdev, MTK_DBG_CTRL_RX, MTK_MEMLOG_RG_CTRL_H_FREQ + (rg_offset), \
		"pkt_len:0x%x header_status:0x%x\n", len, status)

#define MTK_DBG_CTRL_PORT_READ(mdev, channel, size, cnt, left, rg_offset) \
	MTK_DBG(mdev, MTK_DBG_CTRL_RX, MTK_MEMLOG_RG_CTRL_H_FREQ + (rg_offset), \
		"channel_id:0x%x total_read:0x%x merge_cnt:0x%x left_len:0x%x\n", \
		channel, size, cnt, left)

#endif

enum mtk_memlog_flag {
	MTK_MEMLOG_F_ADDINFO = BIT(0),
	MTK_MEMLOG_F_EXCLUSIVE = BIT(1),
	MTK_MEMLOG_F_RING = BIT(2),
	MTK_MEMLOG_F_ONESHOT = BIT(3),
	MTK_MEMLOG_F_EVENT = MEMLOG_EVENT_ENABLE_FLAG,
};

struct mtk_memlog_region {
	u32 base_offset;
	u32 pos;
	u32 len;
	unsigned char buf_idx;
	unsigned int flag;
	char *tmp_log;
	/* protects the buffer write operation */
	spinlock_t lock;
};

struct mtk_memlog_region_cfg {
	char name[MTK_DFLT_MEMLOG_NAME_LEN];
	u32 region_size;
	enum mtk_memlog_flag flag;
};

struct mtk_memlog_cfg {
	struct mtk_memlog_region_cfg *region_cfg;
	unsigned int region_cnt;
	int *region_id_tbl;
	int default_resize;
};

struct mtk_memlog {
	struct proc_dir_entry *proc_entry;
	struct mtk_md_dev *mdev;
	struct dentry *dentry;
	int dev_id;
	unsigned char buf_divide;
	unsigned char total_buf_cnt;
	bool is_kalloc_buf;
	u32 single_buf_size;
	u32 len;
	unsigned int region_cnt;
	char *buffer[MTK_MAX_MEMLOG_BUF_CNT];
	int *region_id_tbl;
	struct mtk_memlog_region region[];
};

void mtk_memlog_write(struct mtk_md_dev *mdev, enum mtk_memlog_region_id region_id,
		      const char *fmt, ...);
int mtk_memlog_init(struct mtk_md_dev *mdev, char *build_time_str);
void mtk_memlog_exit(struct mtk_md_dev *mdev);

#endif

