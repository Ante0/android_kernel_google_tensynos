/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Code Construct
 *
 * Author: Jeremy Kerr <jk@codeconstruct.com.au>
 */

#ifndef _DRIVERS_I3C_MASTER_DW_I3C_CORE_H
#define _DRIVERS_I3C_MASTER_DW_I3C_CORE_H

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/i3c/master.h>
#include <linux/reset.h>
#include <linux/types.h>

#define JEDEC_CCC_DEVCTRL 0x62
#define JEDEC_CCC_DEVCTRL_PAYLOAD {0xe6, 0x00, 0x80, 0x00, 0x00, 0x00}

#define DW_I3C_MAX_DEVS 32
#define DEVICE_CTRL			0x0
#define DEV_CTRL_ENABLE			BIT(31)
#define DEV_CTRL_RESUME			BIT(30)
#define DEV_CTRL_DMA_ENABLE		BIT(28)
#define DEV_CTRL_HOT_JOIN_NACK		BIT(8)
#define DEV_CTRL_I2C_SLAVE_PRESENT	BIT(7)

#define DEVICE_ADDR			0x4
#define DEV_ADDR_DYNAMIC_ADDR_VALID	BIT(31)
#define DEV_ADDR_DYNAMIC(x)		(((x) << 16) & GENMASK(22, 16))

#define HW_CAPABILITY			0x8
#define COMMAND_QUEUE_PORT		0xc
#define COMMAND_PORT_PEC		BIT(31)
#define COMMAND_PORT_TOC		BIT(30)
#define COMMAND_PORT_READ_TRANSFER	BIT(28)
#define COMMAND_PORT_SDAP		BIT(27)
#define COMMAND_PORT_ROC		BIT(26)
#define COMMAND_PORT_SPEED(x)		(((x) << 21) & GENMASK(23, 21))
#define COMMAND_PORT_DEV_INDEX(x)	(((x) << 16) & GENMASK(20, 16))
#define COMMAND_PORT_CP			BIT(15)
#define COMMAND_PORT_CMD(x)		(((x) << 7) & GENMASK(14, 7))
#define COMMAND_PORT_TID(x)		(((x) << 3) & GENMASK(6, 3))
#define COMMAND_PORT_TID_MASK(x)	((x) & GENMASK(3, 0))
#define COMMON_COMMAND_CODE_TID		0xF

#define COMMAND_PORT_ARG_DATA_LEN(x)	(((x) << 16) & GENMASK(31, 16))
#define COMMAND_PORT_ARG_DATA_LEN_MAX	65536
#define COMMAND_PORT_TRANSFER_ARG	0x01

#define COMMAND_PORT_SDA_DATA_BYTE_3(x)	(((x) << 24) & GENMASK(31, 24))
#define COMMAND_PORT_SDA_DATA_BYTE_2(x)	(((x) << 16) & GENMASK(23, 16))
#define COMMAND_PORT_SDA_DATA_BYTE_1(x)	(((x) << 8) & GENMASK(15, 8))
#define COMMAND_PORT_SDA_BYTE_STRB_3	BIT(5)
#define COMMAND_PORT_SDA_BYTE_STRB_2	BIT(4)
#define COMMAND_PORT_SDA_BYTE_STRB_1	BIT(3)
#define COMMAND_PORT_SHORT_DATA_ARG	0x02

#define COMMAND_PORT_DEV_COUNT(x)	(((x) << 21) & GENMASK(25, 21))
#define COMMAND_PORT_CMD_ATTR_MASK(x)	(((x)) & GENMASK(2, 0))
#define COMMAND_PORT_ADDR_ASSGN_CMD	0x03

#define RESPONSE_QUEUE_PORT		0x10
#define RESPONSE_PORT_ERR_STATUS(x)	(((x) & GENMASK(31, 28)) >> 28)
#define RESPONSE_NO_ERROR		0
#define RESPONSE_ERROR_CRC		1
#define RESPONSE_ERROR_PARITY		2
#define RESPONSE_ERROR_FRAME		3
#define RESPONSE_ERROR_IBA_NACK		4
#define RESPONSE_ERROR_ADDRESS_NACK	5
#define RESPONSE_ERROR_OVER_UNDER_FLOW	6
#define RESPONSE_ERROR_TRANSF_ABORT	8
#define RESPONSE_ERROR_I2C_W_NACK_ERR	9
#define RESPONSE_PORT_TID(x)		(((x) & GENMASK(27, 24)) >> 24)
#define RESPONSE_PORT_DATA_LEN(x)	((x) & GENMASK(15, 0))

#define RX_TX_DATA_PORT			0x14
#define IBI_QUEUE_STATUS		0x18
#define IBI_QUEUE_STATUS_IBI_ID(x)	(((x) & GENMASK(15, 8)) >> 8)
#define IBI_QUEUE_STATUS_DATA_LEN(x)	((x) & GENMASK(7, 0))
#define IBI_QUEUE_IBI_ADDR(x)		(IBI_QUEUE_STATUS_IBI_ID(x) >> 1)
#define IBI_QUEUE_IBI_RNW(x)		(IBI_QUEUE_STATUS_IBI_ID(x) & BIT(0))
#define IBI_TYPE_MR(x)                                                         \
	((IBI_QUEUE_IBI_ADDR(x) != I3C_HOT_JOIN_ADDR) && !IBI_QUEUE_IBI_RNW(x))
#define IBI_TYPE_HJ(x)                                                         \
	((IBI_QUEUE_IBI_ADDR(x) == I3C_HOT_JOIN_ADDR) && !IBI_QUEUE_IBI_RNW(x))
#define IBI_TYPE_SIRQ(x)                                                        \
	((IBI_QUEUE_IBI_ADDR(x) != I3C_HOT_JOIN_ADDR) && IBI_QUEUE_IBI_RNW(x))

#define QUEUE_THLD_CTRL			0x1c
#define QUEUE_THLD_CTRL_IBI_STAT_MASK	GENMASK(31, 24)
#define QUEUE_THLD_CTRL_IBI_STAT(x)	(((x) - 1) << 24)
#define QUEUE_THLD_CTRL_IBI_DATA_MASK	GENMASK(23, 16)
#define QUEUE_THLD_CTRL_IBI_DATA(x)	(((x) & GENMASK(8, 0)) << 16)
#define QUEUE_THLD_CTRL_RESP_BUF_MASK	GENMASK(15, 8)
#define QUEUE_THLD_CTRL_RESP_BUF(x)	((((x) - 1) & GENMASK(8, 0)) << 8)
#define QUEUE_THLD_CTRL_CMD_EMPTY_BUF_MASK GENMASK(7, 0)
#define QUEUE_THLD_CTRL_CMD_EMPTY_BUF(x) (((x) - 1) & GENMASK(7, 0))

#define DATA_BUFFER_THLD_CTRL		0x20
#define DATA_BUFFER_THLD_CTRL_TX_BUF(x) ((x) & GENMASK(2, 0))
#define DATA_BUFFER_THLD_CTRL_TX_BUF_MASK GENMASK(2, 0)
#define DATA_BUFFER_THLD_CTRL_RX_BUF(x) (((x) << 8) & GENMASK(10, 8))
#define DATA_BUFFER_THLD_CTRL_RX_BUF_MASK GENMASK(10, 8)
#define DATA_BUFFER_THLD_CTRL_TX_START_THLD(x) (((x) << 18) & GENMASK(18, 16))
#define DATA_BUFFER_THLD_CTRL_TX_START_THLD_MASK GENMASK(18, 16)
#define DATA_BUFFER_THLD_CTRL_RX_START_THLD(x) (((x) << 26) & GENMASK(26, 24))
#define DATA_BUFFER_THLD_CTRL_RX_START_THLD_MASK GENMASK(26, 24)

#define TX_START_THLD_1			0
#define TX_START_THLD_4			1
#define TX_START_THLD_8			2
#define TX_START_THLD_16		3
#define TX_START_THLD_32		4
#define TX_START_THLD_64		5
#define TX_START_THLD_GOOG_DEFAULT	TX_START_THLD_4	/* After reset, default value is 1 */

#define RX_START_THLD_1			0
#define RX_START_THLD_4			1
#define RX_START_THLD_8			2
#define RX_START_THLD_16		3
#define RX_START_THLD_32		4
#define RX_START_THLD_64		5
#define RX_START_THLD_GOOG_DEFAULT	RX_START_THLD_4 /* After reset, default value is 1 */

#define INTR_DATA_BUF_THLD_1		0x0
#define INTR_DATA_BUF_THLD_4		0x1
#define INTR_DATA_BUF_THLD_8		0x2
#define INTR_DATA_BUF_THLD_16		0x3

#define IBI_QUEUE_CTRL			0x24
#define IBI_MR_REQ_REJECT		0x2C
#define IBI_SIR_REQ_REJECT		0x30
#define IBI_REQ_REJECT_ALL		GENMASK(31, 0)

#define RESET_CTRL			0x34
#define RESET_CTRL_IBI_QUEUE		BIT(5)
#define RESET_CTRL_RX_FIFO		BIT(4)
#define RESET_CTRL_TX_FIFO		BIT(3)
#define RESET_CTRL_RESP_QUEUE		BIT(2)
#define RESET_CTRL_CMD_QUEUE		BIT(1)
#define RESET_CTRL_SOFT			BIT(0)

#define SLV_EVENT_CTRL			0x38
#define INTR_STATUS			0x3c
#define INTR_STATUS_EN			0x40
#define INTR_SIGNAL_EN			0x44
#define INTR_FORCE			0x48
#define INTR_BUSOWNER_UPDATE_STAT	BIT(13)
#define INTR_IBI_UPDATED_STAT		BIT(12)
#define INTR_READ_REQ_RECV_STAT		BIT(11)
#define INTR_DEFSLV_STAT		BIT(10)
#define INTR_TRANSFER_ERR_STAT		BIT(9)
#define INTR_DYN_ADDR_ASSGN_STAT	BIT(8)
#define INTR_CCC_UPDATED_STAT		BIT(6)
#define INTR_TRANSFER_ABORT_STAT	BIT(5)
#define INTR_RESP_READY_STAT		BIT(4)
#define INTR_CMD_QUEUE_READY_STAT	BIT(3)
#define INTR_IBI_THLD_STAT		BIT(2)
#define INTR_RX_THLD_STAT		BIT(1)
#define INTR_TX_THLD_STAT		BIT(0)
#define INTR_ALL			(INTR_BUSOWNER_UPDATE_STAT |	\
					INTR_IBI_UPDATED_STAT |		\
					INTR_READ_REQ_RECV_STAT |	\
					INTR_DEFSLV_STAT |		\
					INTR_TRANSFER_ERR_STAT |	\
					INTR_DYN_ADDR_ASSGN_STAT |	\
					INTR_CCC_UPDATED_STAT |		\
					INTR_TRANSFER_ABORT_STAT |	\
					INTR_RESP_READY_STAT |		\
					INTR_CMD_QUEUE_READY_STAT |	\
					INTR_IBI_THLD_STAT |		\
					INTR_TX_THLD_STAT |		\
					INTR_RX_THLD_STAT)

#define INTR_XFER_MASK			(INTR_TRANSFER_ERR_STAT |	\
					INTR_RESP_READY_STAT |		\
					INTR_CMD_QUEUE_READY_STAT |	\
					INTR_TX_THLD_STAT |		\
					INTR_RX_THLD_STAT)

#define QUEUE_STATUS_LEVEL		0x4c
#define QUEUE_STATUS_IBI_STATUS_CNT(x)	(((x) & GENMASK(28, 24)) >> 24)
#define QUEUE_STATUS_IBI_BUF_BLR(x)	(((x) & GENMASK(23, 16)) >> 16)
#define QUEUE_STATUS_LEVEL_RESP(x)	(((x) & GENMASK(15, 8)) >> 8)
#define QUEUE_STATUS_LEVEL_CMD(x)	((x) & GENMASK(7, 0))

#define DATA_BUFFER_STATUS_LEVEL	0x50
#define DATA_BUFFER_STATUS_LEVEL_TX(x)	((x) & GENMASK(7, 0))
#define DATA_BUFFER_STATUS_LEVEL_RX(x)	(((x) & GENMASK(23, 16)) >> 16)

#define PRESENT_STATE			0x54
#define CCC_DEVICE_STATUS		0x58
#define DEVICE_ADDR_TABLE_POINTER	0x5c
#define DEVICE_ADDR_TABLE_DEPTH(x)	(((x) & GENMASK(31, 16)) >> 16)
#define DEVICE_ADDR_TABLE_ADDR(x)	((x) & GENMASK(7, 0))

#define DEV_CHAR_TABLE_POINTER		0x60
#define VENDOR_SPECIFIC_REG_POINTER	0x6c
#define SLV_PID_VALUE			0x74
#define SLV_CHAR_CTRL			0x78
#define SLV_MAX_LEN			0x7c
#define MAX_READ_TURNAROUND		0x80
#define MAX_DATA_SPEED			0x84
#define SLV_DEBUG_STATUS		0x88
#define SLV_INTR_REQ			0x8c
#define DEVICE_CTRL_EXTENDED		0xb0
#define SCL_I3C_PP_TIMING		0xb8
#define SCL_I3C_TIMING_HCNT(x)		(((x) << 16) & GENMASK(23, 16))
#define SCL_I3C_TIMING_LCNT(x)		((x) & GENMASK(7, 0))
#define SCL_I3C_TIMING_CNT_MIN		5

#define SCL_I3C_OD_TIMING		0xb4
#define SCL_I2C_OD_TIMING_HCNT(x)	(((x) << 16) & GENMASK(31, 16))
#define SCL_I2C_OD_TIMING_LCNT(x)	((x) & GENMASK(15, 0))

#define SCL_I2C_FM_TIMING		0xbc
#define SCL_I2C_FM_TIMING_HCNT(x)	(((x) << 16) & GENMASK(31, 16))
#define SCL_I2C_FM_TIMING_LCNT(x)	((x) & GENMASK(15, 0))

#define SCL_I2C_FMP_TIMING		0xc0
#define SCL_I2C_FMP_TIMING_HCNT(x)	(((x) << 16) & GENMASK(23, 16))
#define SCL_I2C_FMP_TIMING_LCNT(x)	((x) & GENMASK(15, 0))

#define SCL_EXT_LCNT_TIMING		0xc8
#define SCL_EXT_LCNT_4(x)		(((x) << 24) & GENMASK(31, 24))
#define SCL_EXT_LCNT_3(x)		(((x) << 16) & GENMASK(23, 16))
#define SCL_EXT_LCNT_2(x)		(((x) << 8) & GENMASK(15, 8))
#define SCL_EXT_LCNT_1(x)		((x) & GENMASK(7, 0))

#define SCL_EXT_TERMN_LCNT_TIMING	0xcc
#define STOP_HLD_CNT_MASK		GENMASK(31, 28)

#define SDA_HOLD_SWITCH_DLY_TIMING	0xd0
#define SDA_OD_PP_SWITCH_DLY(x)		((x) & GENMASK(2, 0))
#define SDA_PP_OD_SWITCH_DLY(x)		(((x) << 8) & GENMASK(10, 8))
#define SDA_TX_HOLD(x)			(((x) << 16) & GENMASK(18, 16))

#define BUS_FREE_TIMING			0xd4
#define BUS_I3C_MST_FREE(x)		((x) & GENMASK(15, 0))

#define BUS_IDLE_TIMING			0xd8

#define I3C_VER_ID			0xe0
#define I3C_VER_101			0x3130312a

#define I3C_VER_TYPE			0xe4
#define EXTENDED_CAPABILITY		0xe8
#define SLAVE_CONFIG			0xec

#define DEV_ADDR_TABLE_IBI_PEC_EN	BIT(11)
#define DEV_ADDR_TABLE_IBI_MDB		BIT(12)
#define DEV_ADDR_TABLE_SIR_REJECT	BIT(13)
#define DEV_ADDR_TABLE_LEGACY_I2C_DEV	BIT(31)
#define DEV_ADDR_TABLE_DYNAMIC_ADDR(x)	(((x) << 16) & GENMASK(23, 16))
#define DEV_ADDR_TABLE_STATIC_ADDR(x)	((x) & GENMASK(6, 0))
#define DEV_ADDR_TABLE_LOC(start, idx)	((start) + ((idx) << 2))

#define DEV_CHARS_TABLE_LOC(start, id_dev, id_reg) ((start) + ((id_dev) << 4) + ((id_reg) << 2))

#define I3C_BUS_SDR1_SCL_RATE		8000000
#define I3C_BUS_SDR2_SCL_RATE		6000000
#define I3C_BUS_SDR3_SCL_RATE		4000000
#define I3C_BUS_SDR4_SCL_RATE		2000000
#define I3C_BUS_I2C_FM_TLOW_MIN_NS	1300
#define I3C_BUS_I2C_FMP_TLOW_MIN_NS	500
#define I3C_BUS_I2C_FMP_THIGH_MIN_NS	260
#define I3C_BUS_THIGH_MAX_NS		41

#define DATA_BUF_ENTRY_SIZE		4

#define I2C_FAST_MODE 0
#define I2C_FAST_MODE_PLUS 1
#define I2C_LVR_MODE BIT(4)
#define I3C_HDR_DDR_MODE 6

#define HDR_CAP_HDR_DDR_FIELD BIT(0)

/* Threshold chosen for transactions expected to complete within 200 microseconds. */
/* SDR rate = 12.5MHz. 100 bytes took ~72us + start header ~10us. */
#define I3C_HYBRID_BUSY_WAIT_BYTES_THLD 100
/* I2C rate = 1MHz. 10 bytes took ~100us + start header ~10us. */
#define I2C_HYBRID_BUSY_WAIT_BYTES_THLD 10
#define I3C_THREADED_THRESHOLD_DEFAULT 512
/* Polling for 300us first, the transfer is expected to finished in 200us. */
#define BUSY_WAIT_XFER_TIMEOUT_NS 300000
#define ADDRESS_BYTES 1

#define XFER_TIMEOUT (msecs_to_jiffies(1000))
#define RPM_AUTOSUSPEND_TIMEOUT_MS 1000
#define I3C_SUSPEND_LOCK_TIMEOUT_MS 10

#define SCL_I3C_TIMING_LCNT_VAL(reg)		((reg) & GENMASK(7, 0))
#define SCL_I3C_TIMING_HCNT_VAL(reg)		(((reg) >> 16) & GENMASK(7, 0))
#define SCL_I2C_FM_TIMING_LCNT_VAL(reg)		((reg) & GENMASK(15, 0))
#define SCL_I2C_FM_TIMING_HCNT_VAL(reg)		(((reg) >> 16) & GENMASK(15, 0))
#define SCL_I2C_FMP_TIMING_LCNT_VAL(reg)	((reg) & GENMASK(15, 0))
#define SCL_I2C_FMP_TIMING_HCNT_VAL(reg)	(((reg) >> 16) & GENMASK(7, 0))
#define SDA_TX_HOLD_VAL(reg)			(((reg) >> 16) & GENMASK(2, 0))
#define SDA_PP_OD_SWITCH_DLY_VAL(reg)		(((reg) >> 8) & GENMASK(2, 0))
#define SDA_OD_PP_SWITCH_DLY_VAL(reg)		((reg) & GENMASK(2, 0))

#define TLOW_OD_MARGIN_NS 5
#define DCT_PTR_I 5
#define DMA_MAXBURST 1
#define DMA_DEFAULT_ALIGN 16
#define AOSS_SSR_ONLINE_TIMEOUT_MS 20000

struct dw_i3c_master_caps {
	u8 cmdfifodepth;
	u8 datafifodepth;
};

struct dw_i3c_dat_entry {
	u8 addr;
	bool is_i2c;
	struct i3c_dev_desc *ibi_dev;
};

enum dw_i3c_xfer_complete_mode {
	INTERRUPT_MODE = 0,
	HYBRID_BUSY_WAIT_MODE,
};

struct dw_i3c_master {
	struct i3c_master_controller base;
	struct device *dev;
	u16 maxdevs;
	u16 datstartaddr;
	u32 free_pos;
	struct {
		struct list_head list;
		struct dw_i3c_xfer *cur;
		spinlock_t lock;
	} xferqueue;
	struct dw_i3c_master_caps caps;
	void __iomem *regs;
	resource_size_t regs_phys;
	struct reset_control *core_rst;
	struct clk *core_clk;
	struct clk *apb_clk;
	struct regulator *io_vreg_dev;
	char version[5];
	char type[5];
	bool ibi_capable;
	bool ibi_sir_req_rej;
	bool is_i2c_slot_shared;
	int shared_i2c_pos;
	enum dw_i3c_xfer_complete_mode operation_mode;
	bool hdr_ddr_capable;
	bool pec_enable;
	u32 i3c_hybrid_threshold;
	u32 i2c_hybrid_threshold;
	u32 i3c_threaded_threshold;
	u32 fifo_sz;

	/*
	 * Per-device hardware data, used to manage the device address table
	 * (DAT)
	 *
	 * Locking: the devs array may be referenced in IRQ context while
	 * processing an IBI. However, IBIs (for a specific device, which
	 * implies a specific DAT entry) can only happen while interrupts are
	 * requested for that device, which is serialised against other
	 * insertions/removals from the array by the global i3c infrastructure.
	 * So, devs_lock protects against concurrent updates to devs->ibi_dev
	 * between request_ibi/free_ibi and the IBI irq event.
	 */
	struct dw_i3c_dat_entry devs[DW_I3C_MAX_DEVS];
	spinlock_t devs_lock;

	/* platform-specific data */
	const struct dw_i3c_platform_ops *platform_ops;

	u32 dev_addr;
	u32 dev_ctrl;
	u32 i3c_pp_timing;
	u32 i3c_od_timing;
	u32 i3c_od_i2c_timing;
	u32 ext_lcnt_timing;
	u32 bus_free_timing;
	u32 i2c_fm_timing;
	u32 i2c_fmp_timing;

	struct {
		int xfers_count;
		bool suspended;
		spinlock_t lock;
		wait_queue_head_t wait_queue;
	} suspend;

	struct i2c_timings i2c_timings;

	struct dentry *debugfs;
	struct notifier_block aoss_ssr_nb;
	struct delayed_work aoss_ssr_work;

	struct dma_chan *chan_tx;
	struct dma_chan *chan_rx;
	bool can_dma;
	u8 dma_align;

	bool aoss_ssr_started;

	/* ENTDAA broadcast with lower speed for i2c/i3c compatible devices. */
	bool daa_with_i2c_speed;
};

struct dw_i3c_platform_ops {
	/*
	 * Called on early bus init: the i3c has been set up, but before any
	 * transactions have taken place. Platform implementations may use to
	 * perform actual device enabling with the i3c core ready.
	 */
	int (*init)(struct dw_i3c_master *i3c);

	/*
	 * Initialise a DAT entry to enable/disable IBIs. Allows the platform
	 * to perform any device workarounds on the DAT entry before
	 * inserting into the hardware table.
	 *
	 * Called with the DAT lock held; must not sleep.
	 */
	void (*set_dat_ibi)(struct dw_i3c_master *i3c,
			    struct i3c_dev_desc *dev, bool enable, u32 *reg);
};

struct dw_i3c_cmd {
	u32 cmd_lo;
	u32 cmd_hi;
};

enum dw_i3c_xfer_type {
	XFER_TYPE_I2C,
	XFER_TYPE_I3C,
	XFER_TYPE_CCC,
	XFER_TYPE_DAA,
};

struct dw_i3c_i2c_dev_data {
	u8 index;
	u8 write_ds;
	u8 read_ds;
	u8 i2c_addr;
	struct i3c_generic_ibi_pool *ibi_pool;
};

union xfer_msgs {
	const struct i3c_priv_xfer *i3c_msgs;
	const struct i2c_msg *i2c_msgs;
	const struct i3c_ccc_cmd *ccc_msg;
};

struct dw_i3c_xfer {
	struct list_head node;
	struct completion comp;
	struct completion dma_comp;
	int ret;
	int end_with_iba_nack;
	unsigned int ncmds;
	unsigned int cmd_idx;
	unsigned int resp_idx;
	unsigned int tx_cmd_idx;
	u32 res_tx_entries;
	u32 res_rx_entries;
	u16 tx_buf_cursor;
	unsigned int rx_cmd_idx;
	u16 rx_buf_cursor;
	enum dw_i3c_xfer_type type;
	struct dw_i3c_i2c_dev_data dev_data;
	union xfer_msgs msgs;
	struct dw_i3c_cmd ccc_cmd;
	dma_addr_t dma_addr;
	u32 ccc_resp;
	bool using_dma;
	bool dma_done;
	bool in_bottom_half;
};

extern int dw_i3c_common_probe(struct dw_i3c_master *master,
			       struct platform_device *pdev);

#endif /* _DRIVERS_I3C_MASTER_DW_I3C_CORE_H */
