/* SPDX-License-Identifier: GPL-2.0 only */

#ifndef _DW_I3C_XFER_HOST_H
#define _DW_I3C_XFER_HOST_H

#include <linux/i3c/master.h>
#include <linux/types.h>
#include <linux/completion.h>

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
};

union dw_i3c_xfer_msgs {
	const struct i3c_priv_xfer *i3c_msgs;
	const struct i2c_msg *i2c_msgs;
	const struct i3c_ccc_cmd *ccc_msg;
};

struct dw_i3c_cmd {
	u32 cmd_lo;
	u32 cmd_hi;
};

struct dw_i3c_host_xfer {
	struct list_head node;
	struct completion comp;
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
	union dw_i3c_xfer_msgs msgs;
	struct dw_i3c_cmd ccc_cmd;
	u32 ccc_resp;
};

struct dw_i3c_xfer_host_ops_t {
	const char *(*get_name)(struct platform_device *pdev);

	int (*resume)(struct platform_device *pdev);

	int (*suspend)(struct platform_device *pdev);

	int (*priv_xfers)(struct i3c_dev_desc *dev,
			  struct i3c_priv_xfer *i3c_xfers, int i3c_nxfers,
			  struct platform_device *pdev);

	int (*i2c_xfers)(struct i2c_dev_desc *dev,
			 const struct i2c_msg *i2c_xfers, int i2c_nxfers,
			 struct platform_device *pdev);

	int (*execute_xfer)(struct dw_i3c_host_xfer *xfer,
			    struct platform_device *pdev);
};

extern const struct dw_i3c_xfer_host_ops_t *dw_i3c_xfer_host_get_ops(struct platform_device *pdev);
extern void __iomem *dw_i3c_xfer_host_get_regs(struct platform_device *pdev);

#endif /* _DW_I3C_XFER_HOST_H */
