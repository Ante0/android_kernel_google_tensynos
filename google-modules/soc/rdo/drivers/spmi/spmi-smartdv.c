// SPDX-License-Identifier: GPL-2.0-only

/*
 * SmartDV IP Solutions SPMI controller driver
 * Copyright 2023 Google LLC
 *
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spmi.h>

/* SPMI controller register address offsets */
#define CONTROL			0x0
#define IRQ_ENABLE		0x4
#define IRQ_STATUS		0x8
#define RSP_ADDR_DATA		0xc
#define REQ_COMMAND		0x10
#define REQ_DATA0		0x14
#define REQ_DATA1		0x18
#define REQ_DATA2		0x1c
#define REQ_DATA3		0x20
#define REQ_RD_STATUS		0x24
#define RD_ERR_INDEX		0x28
#define TIMER			0x2c
#define TBT_RST_CONTROL		0x30
#define STATUS			0x34
#define CONTROL_SOC_TIMEOUT	0x38
#define FIFO_CONTROL		0x3c
#define FIFO_STATUS		0x40
#define MST_WDATA_FIFO		0xf0
#define CMD_WDATA_FIFO		0x200
#define RDATA_FIFO		0x300

/* SPMI controller CONTROL register bit masks */
#define SPMI_CONTROL_MASTER_EN			BIT(0)
#define SPMI_CONTROL_ARB_ENABLE_MASK		BIT(1)
#define SPMI_CONTROL_MID_MASK			GENMASK(3, 2)
#define SPMI_CONTROL_VERSION_MASK		GENMASK(5, 4)
#define SPMI_CONTROL_ARB_RETRY_LIMIT_MASK	GENMASK(13, 6)
#define SPMI_CONTROL_PRESCALER_MASK		GENMASK(23, 14)
#define SPMI_CONTROL_RD_LATENCY_MASK		GENMASK(29, 22)
#define SPMI_CONTROL_ARB_SSC_DELAY_MASK		GENMASK(31, 30)

/* REQ_DATA register width in bytes */
#define REQ_DATA_REG_SIZE	4

/* SPMI irq enable/status bit position */
#define SPMI_IRQ_REQ_CMD_DONE		BIT(0)
#define SPMI_IRQ_RSP_NEW_CMD		BIT(1)
#define SPMI_IRQ_ABORT_MST_CMD		BIT(2)
#define SPMI_IRQ_READ_DATA_PAR_ERR	BIT(3)
#define SPMI_IRQ_READ_NO_RESP_ERR	BIT(4)
#define SPMI_IRQ_CMD_NACK_ERR		BIT(5)
#define SPMI_IRQ_BUS_TIMEOUT		BIT(6)
#define SPMI_IRQ_NO_BOM_CMD_PAR_ERR	BIT(7)
#define SPMI_IRQ_NO_BOM_ADDR_LO_PAR_ERR	BIT(8)
#define SPMI_IRQ_NO_BOM_WR_DATA_PAR_ERR	BIT(9)
#define SPMI_IRQ_ARB_REQ_TIMER_TIMEOUT	BIT(10)
#define SPMI_IRQ_IS_BOM			BIT(11)
#define SPMI_IRQ_CONNECTED		BIT(12)
#define SPMI_IRQ_WR_FIFO_TT		BIT(13)
#define SPMI_IRQ_WR_FIFO_UR		BIT(14)
#define SPMI_IRQ_WR_FIFO_OR		BIT(15)
#define SPMI_IRQ_WR_FIFO_FULL		BIT(16)
#define SPMI_IRQ_WR_FIFO_EMPTY		BIT(17)
#define SPMI_IRQ_CMD_WDATA_FIFO_UR	BIT(18)
#define SPMI_IRQ_CMD_WDATA_FIFO_OR	BIT(19)
#define SPMI_IRQ_CMD_WDATA_FIFO_FULL	BIT(20)
#define SPMI_IRQ_CMD_WDATA_FIFO_EMPTY	BIT(21)
#define SPMI_IRQ_RDATA_FIFO_UR		BIT(22)
#define SPMI_IRQ_RDATA_FIFO_OR		BIT(23)
#define SPMI_IRQ_RDATA_FIFO_FULL	BIT(24)
#define SPMI_IRQ_RDATA_FIFO_EMPTY	BIT(25)
#define SPMI_IRQ_DMA_CMD_ERR		BIT(26)

#define SDVT_SPMI_PRINT_IRQSTATUS(sdrv, mask) \
	do { \
		if ((sdrv)->irq_status_reg & (mask)) \
			dev_dbg((sdrv)->dev, "%s-Bit(%u)\n", #mask, \
			(fls(mask) - 1)); \
	} while (0)

#define SPMI_IRQ_ENABLE_ALL_MASK	GENMASK(26, 0)

/* TODO: b/318674765 regarding the list of interrupts to be enabled */
#define SPMI_IRQ_ENABLE_MASK			\
	(					\
	SPMI_IRQ_REQ_CMD_DONE |			\
	SPMI_IRQ_RSP_NEW_CMD |			\
	SPMI_IRQ_ABORT_MST_CMD |		\
	SPMI_IRQ_READ_DATA_PAR_ERR |		\
	SPMI_IRQ_READ_NO_RESP_ERR |		\
	SPMI_IRQ_CMD_NACK_ERR |			\
	SPMI_IRQ_BUS_TIMEOUT |			\
	SPMI_IRQ_NO_BOM_CMD_PAR_ERR |		\
	SPMI_IRQ_NO_BOM_ADDR_LO_PAR_ERR |	\
	SPMI_IRQ_NO_BOM_WR_DATA_PAR_ERR |	\
	SPMI_IRQ_ARB_REQ_TIMER_TIMEOUT |	\
	SPMI_IRQ_IS_BOM |			\
	SPMI_IRQ_CONNECTED			\
	)

#define SPMI_ANY_ERROR_IRQ_FLAG			\
	(					\
	SPMI_IRQ_ABORT_MST_CMD |		\
	SPMI_IRQ_READ_DATA_PAR_ERR |		\
	SPMI_IRQ_READ_NO_RESP_ERR |		\
	SPMI_IRQ_CMD_NACK_ERR |			\
	SPMI_IRQ_BUS_TIMEOUT |			\
	SPMI_IRQ_NO_BOM_CMD_PAR_ERR |		\
	SPMI_IRQ_NO_BOM_ADDR_LO_PAR_ERR |	\
	SPMI_IRQ_NO_BOM_WR_DATA_PAR_ERR		\
	)

/* REQ_COMMAND register fields mask */
#define SPMI_REQ_COMMAND_NEW_CMD_MASK		BIT(0)
#define SPMI_REQ_COMMAND_PRIMARY_ARB_MASK	BIT(1)
#define SPMI_REQ_COMMAND_OPCODE_MASK		GENMASK(6, 2)
#define SPMI_REQ_COMMAND_ADDR_MASK		GENMASK(10, 7)
#define SPMI_REQ_COMMAND_BYTE_CNT_MASK		GENMASK(14, 11)
#define SPMI_REQ_COMMAND_REQ_ADDR_MASK		GENMASK(30, 15)

/* TODO: b/318678188 - Fine tune the default values */
#define DEFAULT_ARB_RETRY_LIMIT	32
#define DEFAULT_PRESCALAR	0
#define DEFAULT_READ_LATANCY	32
#define DEFAULT_ARB_SCL_DELAY	0
#define REQ_COMMAND_TIMEOUT_MS	100
#define REQ_COMMAND_TIMEOUT_US	(REQ_COMMAND_TIMEOUT_MS * USEC_PER_MSEC)
#define REQ_COMMAND_TIMEOUT	(msecs_to_jiffies(REQ_COMMAND_TIMEOUT_MS))
#define REQ_COMMAND_POLL_US	1

enum smartdv_spmi_xfer_type {
	SMARTDV_SPMI_XFER_CMD = 0,
	SMARTDV_SPMI_XFER_WRITE = 1,
	SMARTDV_SPMI_XFER_READ = 2,
};

/* SDVT SPMI command opcodes */
enum smartdv_spmi_hc_cmd_op_code {
	SDVT_SPMI_CMD_REG_WRITE = 0,
	SDVT_SPMI_CMD_REG_READ = 1,
	SDVT_SPMI_CMD_EXT_REG_WRITE = 2,
	SDVT_SPMI_CMD_EXT_REG_READ = 3,
	SDVT_SPMI_CMD_EXT_REG_WRITE_L = 4,
	SDVT_SPMI_CMD_EXT_REG_READ_L = 5,
	SDVT_SPMI_CMD_REG_WRITE_0 = 6,
	SDVT_SPMI_CMD_MASTER_READ = 7,
	SDVT_SPMI_CMD_MASTER_WRITE = 8,
	SDVT_SPMI_CMD_RESERVED = 9,
	SDVT_SPMI_CMD_RESET = 10,
	SDVT_SPMI_CMD_SLEEP = 11,
	SDVT_SPMI_CMD_SHUTDOWN = 12,
	SDVT_SPMI_CMD_WAKEUP = 13,
	SDVT_SPMI_CMD_AUTH = 14,
	SDVT_SPMI_CMD_DDB_MASTER_READ = 15,
	SDVT_SPMI_CMD_DDB_SLAVE_READ = 16,
	SDVT_SPMI_CMD_TBO = 17,
	SDVT_SPMI_CMD_BUS_CONNECT = 18,
	SDVT_SPMI_CMD_SEND_DUMMY_SCL = 19,
};

/* flags, for platform specific data */
#define FLAG_HW_ARBITRATION		BIT(0)
#define FLAG_DEVICE_GOOGLE		BIT(8)
#define FLAG_DEVICE_TYPE_MASK		GENMASK(15, 8)

/**
 * struct smartdv_spmi_pdata - This definition defines platform data
 * flags : Device flags
 */
struct smartdv_spmi_pdata {
	unsigned long			flags;
};

/**
 * struct smartdv_spmi - This definition defines spmi driver instance
 * @ctrl : Pointer to spmi_controller structure
 * @dev : Pointer to device structure
 * @base : Virtual address of the SPMI controller registers
 * @ext : Extended register space
 * @lock : Prevent I/O concurrent access
 * @hc_req : Completion to notify the transfer request cmd/read/write
 * @i_clk : Hw clock feeding the controller
 * @rst : Hw reset to controller
 * @clk_freq : Core clock frequency in Hz (i_clk)
 * @irq : Controller interrupt line
 * @irq_status_reg : Cached irq status register from last read
 * @ctrl_reg : Cached control register value from last write
 * @pdata : Platform specific data
 * @bus_lock : Pointer to bus lock function
 * @bus_unlock : Pointer to bus unlock function
 */
struct smartdv_spmi {
	struct spmi_controller		*ctrl;
	struct device			*dev;
	void __iomem			*base;
	void __iomem			*ext;
	spinlock_t			lock;
	struct mutex			mlock;
	struct completion		hc_req;
	struct clk			*i_clk;
	struct reset_control		*rst;
	unsigned int			clk_freq;
	unsigned int			irq;
	unsigned int			irq_status_reg;
	u32				ctrl_reg;
	const struct smartdv_spmi_pdata	*pdata;
	int (*bus_lock)(struct smartdv_spmi *sdrv, unsigned long *flags);
	void (*bus_unlock)(struct smartdv_spmi *sdrv, unsigned long flags);
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry			*debugfs_root;
	u8				dbg_sid;
	u8				dbg_len;
	u16				dbg_addr;
	u32				dbg_data[4];
#endif
};

static inline u32 smartdv_spmi_readl(struct smartdv_spmi *sdrv, u32 offset)
{
	return readl(sdrv->base + offset);
}

static inline void smartdv_spmi_writel(struct smartdv_spmi *sdrv, u32 offset, u32 val)
{
	writel(val, sdrv->base + offset);
}

/**
 * smartdv_spmi_irq_status_dump - Dump irq status information in readable format
 * @sdrv: pointer to the smartdv spmi controller data structure
 */
static void smartdv_spmi_irq_status_dump(struct smartdv_spmi *sdrv)
{
	dev_dbg(sdrv->dev, "-------------IRQ status dump-----------\n");
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_REQ_CMD_DONE);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_RSP_NEW_CMD);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_ABORT_MST_CMD);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_READ_DATA_PAR_ERR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_READ_NO_RESP_ERR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_CMD_NACK_ERR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_BUS_TIMEOUT);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_NO_BOM_CMD_PAR_ERR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_NO_BOM_ADDR_LO_PAR_ERR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_NO_BOM_WR_DATA_PAR_ERR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_ARB_REQ_TIMER_TIMEOUT);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_IS_BOM);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_CONNECTED);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_WR_FIFO_TT);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_WR_FIFO_UR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_WR_FIFO_OR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_WR_FIFO_FULL);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_WR_FIFO_EMPTY);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_CMD_WDATA_FIFO_UR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_CMD_WDATA_FIFO_OR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_CMD_WDATA_FIFO_FULL);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_CMD_WDATA_FIFO_EMPTY);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_RDATA_FIFO_UR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_RDATA_FIFO_OR);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_RDATA_FIFO_FULL);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_RDATA_FIFO_EMPTY);
	SDVT_SPMI_PRINT_IRQSTATUS(sdrv, SPMI_IRQ_DMA_CMD_ERR);
	dev_dbg(sdrv->dev, "---------------------------------------\n");
}

/**
 * smartdv_spmi_read_err_status_dump - Dump error status in reads
 * @sdrv: pointer to the smartdv spmi controller data structure
 */
static inline void smartdv_spmi_read_err_status_dump(struct smartdv_spmi *sdrv)
{
	dev_dbg(sdrv->dev, "No response frame & parity error: %#x\n",
		smartdv_spmi_readl(sdrv, REQ_RD_STATUS));
	dev_dbg(sdrv->dev, "Read data frame error: %#x\n", smartdv_spmi_readl(sdrv, RD_ERR_INDEX));
}

/**
 * smartdv_spmi_clear_req_data_reg - Clear the content of REQ_DATA0-3 registers by writing zero.
 * @sdrv: pointer to the smartdv spmi controller data structure
 */
static inline void smartdv_spmi_clear_req_data_reg(struct smartdv_spmi *sdrv)
{
	/* Can be improvised to clear based on byte count */
	smartdv_spmi_writel(sdrv, REQ_DATA0, 0x0);
	smartdv_spmi_writel(sdrv, REQ_DATA1, 0x0);
	smartdv_spmi_writel(sdrv, REQ_DATA2, 0x0);
	smartdv_spmi_writel(sdrv, REQ_DATA3, 0x0);
}

/**
 * smartdv_spmi_irq_handler - Controller interrupt handler
 * @irq: IRQ number
 * @data: pointer to the spmi controller data structure
 */
static irqreturn_t smartdv_spmi_irq_handler(int irq, void *data)
{
	struct smartdv_spmi *sdrv = data;
	u32 irq_status;

	irq_status = smartdv_spmi_readl(sdrv, IRQ_STATUS);

	if (!irq_status)
		return IRQ_NONE;

	/* disable all interrupts */
	smartdv_spmi_writel(sdrv, IRQ_ENABLE, 0x0);

	/* clear all interrupts */
	smartdv_spmi_writel(sdrv, IRQ_STATUS, irq_status);

	sdrv->irq_status_reg = irq_status;

	complete(&sdrv->hc_req);

	return IRQ_HANDLED;
}

/**
 * smartdv_spmi_wait_for_done - Wait for any status change in IRQ_STATUS register
 * @sdrv: pointer to the smartdv spmi controller data structure
 *
 * Returns 0 on success or -ETIMEDOUT on timeout
 */
static int smartdv_spmi_wait_for_done(struct smartdv_spmi *sdrv)
{
	u32 irq_status;
	int ret = 0;

	if (sdrv->irq) {
		if (!wait_for_completion_timeout(&sdrv->hc_req, REQ_COMMAND_TIMEOUT))
			ret = -ETIMEDOUT;
		return ret;
	}

	/* polling mode, wait for cmd done status */
	ret = readl_poll_timeout_atomic(sdrv->base + IRQ_STATUS, irq_status,
					(irq_status & SPMI_IRQ_REQ_CMD_DONE) != 0,
					REQ_COMMAND_POLL_US, REQ_COMMAND_TIMEOUT_US);
	sdrv->irq_status_reg = irq_status;
	/* clear all interrupts */
	smartdv_spmi_writel(sdrv, IRQ_STATUS, irq_status);

	return ret;
}

/**
 * smartdv_spmi_xfer - Initiate a transfer on the bus (cmd/read/write)
 * @sdrv: pointer to the smartdv spmi controller data structure
 * @opc: SmartDV SPMI controller command opcode
 * @sid: slave address on the bus
 * @saddr: address to read from the salve
 * @bc: no of bytes in the transfer
 */
static int smartdv_spmi_xfer(struct smartdv_spmi *sdrv,
			     enum smartdv_spmi_xfer_type xfer_type,
			     u8 opc, u8 sid, u16 saddr, size_t bc)
{
	u32 cmd = 0;
	int ret;

	/* program control register, master enable */
	smartdv_spmi_writel(sdrv, CONTROL, sdrv->ctrl_reg | SPMI_CONTROL_MASTER_EN);

	dev_dbg(sdrv->dev, "ctrl reg: %#x\n", smartdv_spmi_readl(sdrv, CONTROL));

	/* prepare req_command register */
	cmd |= SPMI_REQ_COMMAND_NEW_CMD_MASK;
	cmd |= SPMI_REQ_COMMAND_PRIMARY_ARB_MASK;
	cmd |= FIELD_PREP(SPMI_REQ_COMMAND_OPCODE_MASK, opc);
	cmd |= FIELD_PREP(SPMI_REQ_COMMAND_ADDR_MASK, sid);

	if (xfer_type != SMARTDV_SPMI_XFER_CMD) {
		cmd |= FIELD_PREP(SPMI_REQ_COMMAND_BYTE_CNT_MASK, bc - 1);
		cmd |= FIELD_PREP(SPMI_REQ_COMMAND_REQ_ADDR_MASK, saddr);
	}

	dev_dbg(sdrv->dev, "req cmd reg: %#x\n", cmd);

	if (sdrv->irq) {
		/* enable interrupts */
		smartdv_spmi_writel(sdrv, IRQ_ENABLE, SPMI_IRQ_ENABLE_MASK);
		reinit_completion(&sdrv->hc_req);
	}

	/* request new command */
	smartdv_spmi_writel(sdrv, REQ_COMMAND, cmd);

	ret = smartdv_spmi_wait_for_done(sdrv);
	if (ret) {
		dev_err(sdrv->dev, "request timeout.\n");
		goto out;
	}

	/* check interrupt status for any error(s) */
	if (sdrv->irq_status_reg & SPMI_ANY_ERROR_IRQ_FLAG) {
		ret = -EIO;
		dev_err(sdrv->dev,
			"sid %#x opc %#x addr %#x len %#zx irq_status_reg: %#x",
			sid, opc, saddr, bc, sdrv->irq_status_reg);
		smartdv_spmi_irq_status_dump(sdrv);

		if (xfer_type == SMARTDV_SPMI_XFER_READ)
			smartdv_spmi_read_err_status_dump(sdrv);

		goto out;
	}

	/* check interrupt status for command completion */
	if (sdrv->irq_status_reg & SPMI_IRQ_REQ_CMD_DONE) {
		ret = 0;
	} else {
		/* Not a possible case, considering as a catch block */
		ret = -EINVAL;
		dev_err(sdrv->dev, "no cmd-done interrupt, status reg %#x\n",
			sdrv->irq_status_reg);
	}

out:
	/* program control register, with master disable */
	smartdv_spmi_writel(sdrv, CONTROL, sdrv->ctrl_reg);

	return ret;
}

/**
 * smartdv_spmi_read_req_data - Copy the bc no of bytes from hardware to destination buffer
 * @sdrv: pointer to the smartdv spmi controller data structure
 * @buf: destination buffer for storing data read from the hardware
 * @bc: no of bytes to read
 */
static void smartdv_spmi_read_req_data(struct smartdv_spmi *sdrv, u8 *buf, size_t bc)
{
	u32 addr = REQ_DATA0;
	u32 reg;

	while (bc) {
		if (bc < REQ_DATA_REG_SIZE) {
			reg = smartdv_spmi_readl(sdrv, addr);
			memcpy(buf, &reg, bc);
			break;
		}
		reg = smartdv_spmi_readl(sdrv, addr);
		memcpy(buf, &reg, REQ_DATA_REG_SIZE);
		buf += REQ_DATA_REG_SIZE;
		bc -= REQ_DATA_REG_SIZE;
		addr += 4;
	}
}

static int smartdv_bus_lock(struct smartdv_spmi *sdrv, unsigned long *flags)
{
	unsigned long irqsave_flags = 0;

	if (sdrv->irq) {
		mutex_lock(&sdrv->mlock);
	} else {
		spin_lock_irqsave(&sdrv->lock, irqsave_flags);
		*flags = irqsave_flags;
	}

	return 0;
}

static void smartdv_bus_unlock(struct smartdv_spmi *sdrv, unsigned long flags)
{
	if (sdrv->irq)
		mutex_unlock(&sdrv->mlock);
	else
		spin_unlock_irqrestore(&sdrv->lock, flags);
}

#define GOOGLE_HW_LOCK_POLL_US		10
#define GOOGLE_HW_LOCK_TIMEOUT_MS	100
#define GOOGLE_HW_LOCK_TIMEOUT_US	(GOOGLE_HW_LOCK_TIMEOUT_MS * USEC_PER_MSEC)
#define GOOGLE_HW_LOCK_UNLOCK		0

/* Google specific bus lock/un-lock functions */
static int smartdv_google_bus_lock(struct smartdv_spmi *sdrv, unsigned long *flags)
{
	u32 lock;

	*flags = 0;
	return readl_poll_timeout_atomic(sdrv->ext, lock, lock == 0,
						GOOGLE_HW_LOCK_POLL_US, GOOGLE_HW_LOCK_TIMEOUT_US);
}

static void smartdv_google_bus_unlock(struct smartdv_spmi *sdrv, unsigned long flags)
{
	(void)flags;
	writel(GOOGLE_HW_LOCK_UNLOCK, sdrv->ext);
}

/**
 * smartdv_spmi_read_cmd - Core read_cmd callback
 * @ctrl: pointer to the spmi controller data structure
 * @opcode: command code from SPMI framework
 * @sid: slave address on the bus
 * @saddr: address to read from the salve
 * @__buf: destination buffer for storing data read from the hardware
 * @bc: no of bytes to read
 */
static int smartdv_spmi_read_cmd(struct spmi_controller *ctrl,
				 u8 opcode, u8 sid, u16 saddr, u8 *__buf, size_t bc)
{
	struct smartdv_spmi *sdrv = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	int ret;
	u8 opc;

	switch (opcode) {
	case SPMI_CMD_READ:
		opc = SDVT_SPMI_CMD_REG_READ;
		break;
	case SPMI_CMD_EXT_READ:
		opc = SDVT_SPMI_CMD_EXT_REG_READ;
		break;
	case SPMI_CMD_EXT_READL:
		opc = SDVT_SPMI_CMD_EXT_REG_READ_L;
		break;
	default:
		dev_err(&ctrl->dev, "invalid cmd %#x\n", opcode);
		return -EINVAL;
	}

	dev_dbg(sdrv->dev, "read %#x sid %#x addr %#x len %#zx\n", opc, sid, saddr, bc);

	if (sdrv->bus_lock(sdrv, &flags)) {
		dev_err(sdrv->dev, "bus-lock request timeout.\n");
		return -ETIMEDOUT;
	}


	/* clear req_data registers by writing zeros */
	smartdv_spmi_clear_req_data_reg(sdrv);

	/* clear read status register */
	smartdv_spmi_writel(sdrv, REQ_RD_STATUS, 0x0);

	/* initiate transfer on the bus */
	ret = smartdv_spmi_xfer(sdrv, SMARTDV_SPMI_XFER_READ, opc, sid, saddr, bc);

	if (!ret)
		/* read received data */
		smartdv_spmi_read_req_data(sdrv, __buf, bc);

	sdrv->bus_unlock(sdrv, flags);

	return ret;
}

/**
 * smartdv_spmi_write_req_data - Write the bc no of bytes from source buffer to hardware
 * @sdrv: pointer to the smartdv spmi controller data structure
 * @buf: source buffer providing data to be written to the hardware
 * @bc: no of bytes to write
 */
static void smartdv_spmi_write_req_data(struct smartdv_spmi *sdrv, const u8 *buf, size_t bc)
{
	u32 addr = REQ_DATA0;

	while (bc) {
		if (bc < REQ_DATA_REG_SIZE) {
			u32 data = 0;
			memcpy(&data, buf, bc);
			smartdv_spmi_writel(sdrv, addr, data);
			break;
		}
		smartdv_spmi_writel(sdrv, addr, *(u32 *)buf);
		buf += REQ_DATA_REG_SIZE;
		bc -= REQ_DATA_REG_SIZE;
		addr += 4;
	}
}

/**
 * smartdv_spmi_write_cmd - Core write_cmd callback
 * @ctrl: pointer to the spmi controller data structure
 * @opcode: command code from SPMI framework
 * @sid: slave address on the bus
 * @saddr: address to write in slave memory/register
 * @__buf: source buffer providing data to be written to the hardware
 * @bc: no of bytes to write
 */
static int smartdv_spmi_write_cmd(struct spmi_controller *ctrl,
				  u8 opcode, u8 sid, u16 saddr, const u8 *__buf, size_t bc)
{
	struct smartdv_spmi *sdrv = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	u8 opc;
	int ret;

	switch (opcode) {
	case SPMI_CMD_ZERO_WRITE:
		opc = SDVT_SPMI_CMD_REG_WRITE_0;
		break;
	case SPMI_CMD_WRITE:
		opc = SDVT_SPMI_CMD_REG_WRITE;
		break;
	case SPMI_CMD_EXT_WRITE:
		opc = SDVT_SPMI_CMD_EXT_REG_WRITE;
		break;
	case SPMI_CMD_EXT_WRITEL:
		opc = SDVT_SPMI_CMD_EXT_REG_WRITE_L;
		break;
	default:
		dev_err(&ctrl->dev, "invalid write cmd %#x\n", opcode);
		return -EINVAL;
	}

	dev_dbg(sdrv->dev, "write %#x sid %#x addr %#x len %#zx\n", opc, sid, saddr, bc);

	if (sdrv->bus_lock(sdrv, &flags)) {
		dev_err(sdrv->dev, "bus-lock request timeout.\n");
		return -ETIMEDOUT;
	}

	/* fill data */
	smartdv_spmi_write_req_data(sdrv, __buf, bc);

	/* initiate transfer on the bus */
	ret = smartdv_spmi_xfer(sdrv, SMARTDV_SPMI_XFER_WRITE, opc, sid, saddr, bc);

	sdrv->bus_unlock(sdrv, flags);

	return ret;
}

/**
 * smartdv_spmi_cmd - Core cmd callback
 * @ctrl: pointer to the spmi controller data structure
 * @opcode: command code from SPMI framework
 * @sid: slave address on the bus
 */
static int smartdv_spmi_cmd(struct spmi_controller *ctrl, u8 opcode, u8 sid)
{
	struct smartdv_spmi *sdrv = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	int ret;
	u8 opc;

	switch (opcode) {
	case SPMI_CMD_RESET:
		opc = SDVT_SPMI_CMD_RESET;
		break;
	case SPMI_CMD_SLEEP:
		opc = SDVT_SPMI_CMD_SLEEP;
		break;
	case SPMI_CMD_SHUTDOWN:
		opc = SDVT_SPMI_CMD_SHUTDOWN;
		break;
	case SPMI_CMD_WAKEUP:
		opc = SDVT_SPMI_CMD_WAKEUP;
		break;
	default:
		dev_err(&ctrl->dev, "invalid cmd %#x\n", opcode);
		return -EINVAL;
	}


	if (sdrv->bus_lock(sdrv, &flags)) {
		dev_err(sdrv->dev, "bus-lock request timeout.\n");
		return -ETIMEDOUT;
	}

	/* initiate transfer on the bus */
	ret = smartdv_spmi_xfer(sdrv, SMARTDV_SPMI_XFER_CMD, opc, sid, 0, 0);

	sdrv->bus_unlock(sdrv, flags);

	return ret;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)

#define SPMI_MAX_BYTES 16ul
#define SPMI_ADDR_LIMIT 0x1f
#define SPMI_ADDR_EXT_LIMIT 0xff
#define SPMI_ADDR_EXT_LONG_LIMIT 0xffff

#define SPMI_DEBUG_ATTRIBUTE(name, fn_read, fn_write) \
static const struct file_operations name = {    \
	.open   = simple_open,                  \
	.read   = fn_read,                      \
	.write  = fn_write,                     \
}

static int debugfs_read_cmd(struct smartdv_spmi *sdrv)
{
	u16 addr = sdrv->dbg_addr;
	u8 len = sdrv->dbg_len;
	u8 sid = sdrv->dbg_sid;
	u8 opc;
	u8 buf[SPMI_MAX_BYTES] = {0};
	int err;

	memset(sdrv->dbg_data, 0, sizeof(sdrv->dbg_data));

	if (addr <= SPMI_ADDR_LIMIT && len == 1) {
		opc = SPMI_CMD_READ;
	} else if (addr <= SPMI_ADDR_EXT_LIMIT && len <= 16) {
		opc = SPMI_CMD_EXT_READ;
	} else if (addr <= SPMI_ADDR_EXT_LONG_LIMIT && len <= 8) {
		opc = SPMI_CMD_EXT_READL;
	} else {
		return -EINVAL;
	}

	err = smartdv_spmi_read_cmd(sdrv->ctrl, opc, sid, addr, buf, len);
	if (err) {
		dev_err(sdrv->dev, "Unable to read (%d)\n", err);
		return err;
	}

	memcpy(sdrv->dbg_data, buf, SPMI_MAX_BYTES);

	return 0;
}

static int debugfs_write_cmd(struct smartdv_spmi *sdrv)
{
	u16 addr = sdrv->dbg_addr;
	u8 len = sdrv->dbg_len;
	u8 sid = sdrv->dbg_sid;
	u8 opc;
	u8 buf[SPMI_MAX_BYTES];

	if (addr == 0 && len == 1) {
		opc = SPMI_CMD_ZERO_WRITE;
	} else if (addr <= SPMI_ADDR_LIMIT && len == 1) {
		opc = SPMI_CMD_WRITE;
	} else if (addr <= SPMI_ADDR_EXT_LIMIT && len <= 16) {
		opc = SPMI_CMD_EXT_WRITE;
	} else if (addr <= SPMI_ADDR_EXT_LONG_LIMIT && len <= 8) {
		opc = SPMI_CMD_EXT_WRITEL;
	} else {
		return -EINVAL;
	}

	memcpy(buf, sdrv->dbg_data, SPMI_MAX_BYTES);

	return smartdv_spmi_write_cmd(sdrv->ctrl, opc, sid, addr, buf, len);
}

static ssize_t debugfs_cmd_write(struct file *filp, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct spmi_controller *ctrl = (struct spmi_controller *)filp->private_data;
	struct smartdv_spmi *sdrv = spmi_controller_get_drvdata(ctrl);
	char cmd;
	int err;

	err = copy_from_user(&cmd, user_buf, 1);
	if (err)
		return err;

	switch (toupper(cmd)) {
	case 'W':
		err = debugfs_write_cmd(sdrv);
		if (err)
			return err;
		break;
	case 'R':
		err = debugfs_read_cmd(sdrv);
		if (err)
			return err;
		break;
	default:
		return -EINVAL;
	}

	return count;
}
SPMI_DEBUG_ATTRIBUTE(cmd_fops, NULL, debugfs_cmd_write);

static int smartdv_spmi_debugfs_init(struct platform_device *pdev)
{
	char dir_name[NAME_MAX];
	struct smartdv_spmi *sdrv = platform_get_drvdata(pdev);

	if (!debugfs_initialized())
		return -ENODEV;

	snprintf(dir_name, sizeof(dir_name), "smartdv-%s", dev_name(sdrv->dev));

	sdrv->debugfs_root = debugfs_create_dir(dir_name, NULL);
	if (IS_ERR(sdrv->debugfs_root)) {
		dev_err(sdrv->dev, "Failed to create debugfs directory %s\n", dir_name);
		return -ENOMEM;
	}

	debugfs_create_x8("sid", 0660, sdrv->debugfs_root, &sdrv->dbg_sid);
	debugfs_create_u8("len", 0660, sdrv->debugfs_root, &sdrv->dbg_len);
	debugfs_create_x16("addr", 0660, sdrv->debugfs_root, &sdrv->dbg_addr);
	debugfs_create_x32("dat0", 0660, sdrv->debugfs_root, &sdrv->dbg_data[0]);
	debugfs_create_x32("dat1", 0660, sdrv->debugfs_root, &sdrv->dbg_data[1]);
	debugfs_create_x32("dat2", 0660, sdrv->debugfs_root, &sdrv->dbg_data[2]);
	debugfs_create_x32("dat3", 0660, sdrv->debugfs_root, &sdrv->dbg_data[3]);
	debugfs_create_file("cmd", 0660, sdrv->debugfs_root, sdrv->ctrl, &cmd_fops);

	return 0;
}

static inline void smartdv_spmi_debugfs_remove(struct smartdv_spmi *sdrv)
{
	debugfs_remove(sdrv->debugfs_root);
}

#else
static int smartdv_spmi_debugfs_init(struct platform_device *pdev)
{
	return 0;
}

static inline void smartdv_spmi_debugfs_remove(struct smartdv_spmi *sdrv)
{
}
#endif

/* devm action callbacks */
static void smartdv_spmi_clk_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static void smartdv_spmi_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

/**
 * smartdv_spmi_init_clk_reset - initializes clock and reset, if available
 * @sdrv: pointer to the smartdv spmi controller data structure
 */
static int smartdv_spmi_init_clk_reset(struct smartdv_spmi *sdrv)
{
	struct device *dev = sdrv->dev;
	int ret;

	/* read i_clk rate from the device tree node */
	(void)device_property_read_u32(dev, "clock-frequency", &sdrv->clk_freq);

	sdrv->i_clk = devm_clk_get_optional(dev, NULL);
	if (sdrv->i_clk)
		sdrv->clk_freq = clk_get_rate(sdrv->i_clk);

	/* no clock rate is defined, fail. */
	if (!sdrv->clk_freq)
		return dev_err_probe(dev, -EINVAL, "i_clk rate not defined\n");

	if (sdrv->i_clk) {
		ret = clk_prepare_enable(sdrv->i_clk);
		if (ret)
			return dev_err_probe(dev, ret, "could not enable i_clk\n");

		ret = devm_add_action_or_reset(dev, smartdv_spmi_clk_disable_unprepare,
					       sdrv->i_clk);
		if (ret)
			return dev_err_probe(dev, ret, "error register clk disable devm action\n");
	}

	/* check for optional reset */
	sdrv->rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (sdrv->rst) {
		if (IS_ERR(sdrv->rst))
			return dev_err_probe(dev, PTR_ERR(sdrv->rst), "error in get reset ctrl\n");

		ret = reset_control_deassert(sdrv->rst);
		if (ret)
			return dev_err_probe(dev, ret, "error in deassert reset %d\n", ret);

		ret = devm_add_action_or_reset(dev, smartdv_spmi_reset_control_assert, sdrv->rst);
		if (ret)
			return dev_err_probe(dev, ret, "error register rst assert devm action\n");
	}

	return 0;
}

/**
 * smartdv_spmi_process_ctrl_reg_dt_param - Function to parse device tree node for reading control
 *	register configuration parameter values.
 * @sdrv: pointer to the smartdv spmi controller data structure
 *
 * Populates to default values, if the parameters are not provided.
 */
static void smartdv_spmi_process_ctrl_reg_dt_param(struct smartdv_spmi *sdrv)
{
	struct device *dev = sdrv->dev;
	unsigned int ctrlreg = 0;
	u32 reg = 0;

	/* read control register parameters from the device tree node */
	if (device_property_read_u32(dev, "master_arb_en", &reg))
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_ARB_ENABLE_MASK, 0);
	else
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_ARB_ENABLE_MASK, reg);

	if (device_property_read_u32(dev, "master_id", &reg))
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_MID_MASK, 0);
	else
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_MID_MASK, reg);

	if (device_property_read_u32(dev, "spmi_ver", &reg))
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_VERSION_MASK, 0);
	else
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_VERSION_MASK, reg);

	if (device_property_read_u32(dev, "arb_retry_limit", &reg))
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_ARB_RETRY_LIMIT_MASK, DEFAULT_ARB_RETRY_LIMIT);
	else
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_ARB_RETRY_LIMIT_MASK, reg);

	if (device_property_read_u32(dev, "prescalar", &reg))
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_PRESCALER_MASK, DEFAULT_PRESCALAR);
	else
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_PRESCALER_MASK, reg);

	if (device_property_read_u32(dev, "read_latancy", &reg))
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_RD_LATENCY_MASK, DEFAULT_READ_LATANCY);
	else
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_RD_LATENCY_MASK, reg);

	if (device_property_read_u32(dev, "arb_scl_delay", &reg))
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_ARB_SSC_DELAY_MASK, DEFAULT_ARB_SCL_DELAY);
	else
		ctrlreg |= FIELD_PREP(SPMI_CONTROL_ARB_SSC_DELAY_MASK, reg);

	sdrv->ctrl_reg = ctrlreg;

	dev_dbg(dev, "\nmaster_arb_en	: %lu\nmaster_id	: %lu\nspmi_ver		: %lu\n"
		"arb_retry_limit	: %lu\nprescalar	: %lu\nread_latancy	: %lu\n"
		"arb_scl_delay	: %lu\n", FIELD_GET(SPMI_CONTROL_ARB_ENABLE_MASK, ctrlreg),
		FIELD_GET(SPMI_CONTROL_MID_MASK, ctrlreg),
		FIELD_GET(SPMI_CONTROL_VERSION_MASK, ctrlreg),
		FIELD_GET(SPMI_CONTROL_ARB_RETRY_LIMIT_MASK, ctrlreg),
		FIELD_GET(SPMI_CONTROL_PRESCALER_MASK, ctrlreg),
		FIELD_GET(SPMI_CONTROL_RD_LATENCY_MASK, ctrlreg),
		FIELD_GET(SPMI_CONTROL_ARB_SSC_DELAY_MASK, ctrlreg));
}

/**
 * smartdv_spmi_pr_info - print out the information
 * @sdrv: pointer to the smartdv spmi controller data structure
 */
static inline void smartdv_spmi_pr_info(struct smartdv_spmi *sdrv)
{
	u32 prescalar = (u32)FIELD_GET(SPMI_CONTROL_PRESCALER_MASK, sdrv->ctrl_reg);
	char msg[64];
	int len = 0;
	const int size = sizeof(msg);

	len += scnprintf(msg + len, size - len, "%s: clk %u KHz scl %u KHz",
			dev_name(sdrv->dev), sdrv->clk_freq / 1000,
			sdrv->clk_freq / ((prescalar != 0 ? prescalar * 2 : 1) * 1000));

	if (sdrv->irq)
		len += scnprintf(msg + len, size - len, ", irq: %d", sdrv->irq);
	else
		len += scnprintf(msg + len, size - len, ", polling");

	dev_info(sdrv->dev, "%s\n", msg);
}

/**
 * smartdv_spmi_pr_info - print out the information
 * @pdev: pointer to the platform device
 */
static void smartdv_spmi_of_configure(struct platform_device *pdev)
{
	struct smartdv_spmi *sdrv = platform_get_drvdata(pdev);

	sdrv->pdata = device_get_match_data(sdrv->dev);

	if (!sdrv->pdata)
		return;

	if ((sdrv->pdata->flags & FLAG_DEVICE_TYPE_MASK) == FLAG_DEVICE_GOOGLE) {
		if (!(sdrv->pdata->flags & FLAG_HW_ARBITRATION))
			return;

		sdrv->ext = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(sdrv->ext))
			return;
		sdrv->bus_lock = smartdv_google_bus_lock;
		sdrv->bus_unlock = smartdv_google_bus_unlock;
	}
}

static int smartdv_spmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct smartdv_spmi *sdrv;
	struct spmi_controller *ctrl;
	int ret;

	ctrl = spmi_controller_alloc(dev, sizeof(*sdrv));
	if (!ctrl)
		return -ENOMEM;

	sdrv = spmi_controller_get_drvdata(ctrl);
	sdrv->ctrl = ctrl;
	sdrv->dev = dev;

	sdrv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sdrv->base)) {
		ret = PTR_ERR(sdrv->base);
		goto out;
	}

	platform_set_drvdata(pdev, sdrv);

	/* parse control register parameters from device tree properties */
	smartdv_spmi_process_ctrl_reg_dt_param(sdrv);

	/* initialize clock and reset if available */
	ret = smartdv_spmi_init_clk_reset(sdrv);
	if (ret)
		goto out;

	/* no interrupt, continue to operate in polling mode*/
	sdrv->irq = platform_get_irq_optional(pdev, 0);
	if (sdrv->irq == -ENXIO) {
		sdrv->irq = 0;
	} else {
		ret = devm_request_irq(dev, sdrv->irq, smartdv_spmi_irq_handler,
				       0, pdev->name, sdrv);
		if (ret) {
			dev_err(dev, "unable to register IRQ handler\n");
			goto out;
		}

		init_completion(&sdrv->hc_req);
	}

	spin_lock_init(&sdrv->lock);
	mutex_init(&sdrv->mlock);

	sdrv->bus_lock = smartdv_bus_lock;
	sdrv->bus_unlock = smartdv_bus_unlock;

	dev_set_drvdata(&ctrl->dev, sdrv);

	/* Callbacks */
	ctrl->cmd = smartdv_spmi_cmd;
	ctrl->read_cmd = smartdv_spmi_read_cmd;
	ctrl->write_cmd = smartdv_spmi_write_cmd;

	smartdv_spmi_of_configure(pdev);

	ret = spmi_controller_add(ctrl);
	if (ret) {
		dev_err(dev, "spmi_controller_add failed with error %d!\n", ret);
		goto out;
	}

	smartdv_spmi_debugfs_init(pdev);

	smartdv_spmi_pr_info(sdrv);

	return 0;
out:
	spmi_controller_remove(ctrl);
	spmi_controller_put(ctrl);
	return ret;
}

static void smartdv_spmi_remove(struct platform_device *pdev)
{
	struct smartdv_spmi *sdrv = platform_get_drvdata(pdev);
	struct spmi_controller *ctrl = sdrv->ctrl;

	smartdv_spmi_debugfs_remove(sdrv);

	spmi_controller_remove(ctrl);

	spmi_controller_put(ctrl);
}


static const struct smartdv_spmi_pdata google_smartdv_spmi_pdata = {
	.flags = (FLAG_DEVICE_GOOGLE)
};

static const struct of_device_id smartdv_spmi_of_match[] = {
	{ .compatible = "smartdv,spmi",},
	{ .compatible = "google,smartdv-spmi", .data = &google_smartdv_spmi_pdata},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, smartdv_spmi_of_match);

static struct platform_driver smartdv_spmi_platform_driver = {
	.driver = {
		.name           = "smartdv-spmi",
		.of_match_table = smartdv_spmi_of_match,
	},
	.probe                  = smartdv_spmi_probe,
	.remove                 = smartdv_spmi_remove,
};

module_platform_driver(smartdv_spmi_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SmartDV IP Solutions SPMI master controller driver");
MODULE_ALIAS("platform:smartdv-spmi");

