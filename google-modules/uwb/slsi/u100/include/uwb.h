/* SPDX-License-Identifier: GPL-2.0 */

/**
 * @copyright Copyright (c) 2023 Samsung Electronics Co., Ltd
 *
 */

#ifndef __UWB_H__
#define __UWB_H__
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/gpio/consumer.h>
#include <linux/irqreturn.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "uwb_fw.h"
#include "uwb_power_stats.h"
#include "uwb_sysnodes.h"

#define MAX_NAME_LEN 64
#define MAX_RXQ_DEPTH 1024

#define UWB_IOC_TYPE 'U'

#define UWB_IOCTL_RESET _IO(UWB_IOC_TYPE, 1)
#define UWB_IOCTL_FW_FLASH _IOW(UWB_IOC_TYPE, 2, struct flash_param)
#define UWB_IOCTL_POWER_ON _IO(UWB_IOC_TYPE, 3)
#define UWB_IOCTL_POWER_OFF _IO(UWB_IOC_TYPE, 4)
#define UWB_IOCTL_FW_INFO _IOR(UWB_IOC_TYPE, 5, struct firmware_info)
#define UWB_IOCTL_DISABLE_COREDUMP_RESET _IOW(UWB_IOC_TYPE, 6, int)
#define UWB_IOCTL_RESET_VBAT _IO(UWB_IOC_TYPE, 7)

#define UCI_HEAD_SIZE ((int)sizeof(struct uci_msg_hdr))
#define UCI_MT_CMD (0b001)
#define UCI_MT_RESP (0b010)
#define UCI_MT_NTF (0b011)
#define UCI_ATR_BID (0x10)
#define GID_VENDOR_CONFIG (0b1100)
#define OID_VENDOR_DEV_CTRL (0b111111)
#define VENDOR_BID_SIZE 4
#define U100_STATE_INDEX_IN_ATR 8
#define FW_TYPE_INDEX_IN_ATR 10
#define TLV_SIZE_INDEX_IN_ATR 11
#define UCI_DATA_LEN_OFFSET 3
#define MINIMAL_ATR_SIZE 9
#define U100_BL0_STATE 0x00
#define U100_BL1_STATE 0x01
#define U100_FW_STATE 0x02
#define U100_EDS_STATE 0x03
#define U100_UNKNOWN_STATE 0xFF
#define NONE_DATA_00 0x00
#define NONE_DATA_FF 0xFF
#define PROBE_ATTR_TIMEOUT msecs_to_jiffies(1000)
#define UNKNOWN_STR "Unknown"
#define SKB_MAX_PRINT_SIZE 64U
#define HOST_TYPE_LEGACY 0xF

enum {
	DOWNLOAD,
	BL0,
	BL1,
};

struct uwb_irq {
	unsigned int num;
	unsigned long flags;
	bool registered;
};

struct uci_msg_hdr {
	u8 gid:4;
	u8 pbf:1;
	u8 mt:3;
	union {
		struct {
			u8 oid:6;
			u8 rfu1:2;
		} __packed;
		u8 data_rfu1:8;
	};
	union {
		struct {
			u8 rfu2:8;
			u8 len:8;
		} __packed;
		u16 data_len:16;
	};
} __packed;

struct u100_ctx {
	atomic_t opened;
	struct spi_device *spi;
	struct miscdevice uci_dev;
	struct uwb_irq u100_irq;
	struct mutex lock;
	atomic_t num_spi_slow_txs;

	struct mutex ioctl_mutex;
	struct mutex tx_mutex;

	struct gpio_desc *gpio_u100_en;
	struct gpio_desc *gpio_u100_power;
	struct gpio_desc *gpio_u100_reset;
	struct gpio_desc *gpio_u100_sync;

	wait_queue_head_t wq;
	struct sk_buff_head sk_rx_q;
	struct sk_buff *sk_tx;
	int tx_status;
	struct completion tx_done_cmpl;
	struct completion process_done_cmpl;

	struct uwb_firmware_ctx uwb_fw_ctx;
	atomic_t flashing;
	atomic_t flashing_fw;

	struct mutex atr_lock;
	struct completion atr_done_cmpl;
	bool waiting_atr;
	int u100_state;
	int u100_state_wanted;

	atomic_t u100_powered_on;
	atomic_t u100_enter_download;
	struct firmware_info fw_info;
	unsigned long flags;
	bool is_download_mode;
	bool is_bhalf_entered;
	struct uwb_sysnode uwb_node;
	struct dentry *debugfs;

	void (*recv_package)(struct u100_ctx *u100_ctx, struct sk_buff *skb);

	struct uwb_coredump *coredump;
	struct u100_power_stats *power_stats;
};

int init_controller_layer(struct u100_ctx *u100_ctx);

irqreturn_t uwb2ap_irq_handler(int irq, void *data);

int link_send_package(struct u100_ctx *u100_ctx, char *buff, unsigned int size);

irqreturn_t rx_tsk_work(int irq, void *data);
void handle_fw_ap_send(struct u100_ctx *u100_ctx);

#define LOG_TAG "UWB_Kernel: "
#define CALLER  (__builtin_return_address(0))

#define UWB_ERR(fmt, ...) \
	pr_err(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define UWB_DEBUG(fmt, ...) \
	pr_debug(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define UWB_INFO(fmt, ...) \
	pr_info(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define UWB_WARN(fmt, ...) \
	pr_warn(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
/* system is unusable */
#define UWB_EMERG(fmt, ...) \
	pr_emerg(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
/* action must be taken immediately */
#define UWB_ALERT(fmt, ...) \
	pr_alert(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
/* critical conditions */
#define UWB_CRIT(fmt, ...) \
	pr_crit(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
/* normal but significant condition */
#define UWB_NOTICE(fmt, ...) \
	pr_notice(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)

int fw_download(struct u100_ctx *u100_ctx);

/* Load Switch On */
void pin_ldsw_high(struct u100_ctx *u100_ctx);

/* Load Switch Off */
void pin_ldsw_low(struct u100_ctx *u100_ctx);

void uwbs_init(struct u100_ctx *u100_ctx);

void uwbs_power_on(struct u100_ctx *u100_ctx);

void uwbs_power_off(struct u100_ctx *u100_ctx);

void uwbs_reset(struct u100_ctx *u100_ctx);

/**
 * UWBS reset VBAT by means of load-switch pin.
 * Calling IS_ERR_OR_NULL(u100_ctx->gpio_u100_power) to check if it is available since
 * it is not defined in older version device tree.
 */
void uwbs_ldsw_reset(struct u100_ctx *u100_ctx);

/**
 * uwbs_sync_reset() - Rest UWBS and get ATR.
 * @u100_ctx: U100 context.
 *
 * GPIO reset, waiting for the ATR from UWBS and switch to level-triggered interrupts.
 * The ATR should indicate UWBS's state, UCI transmission can be handled in FW state but not
 * in BL0, BL1 or any error states.
 * Free the IRQ if U100 is in an error state.
 *
 * Return: 0 on success (UWBS boots successfully and AP gets ATR with FW state), else is failed.
 */
int uwbs_sync_reset(struct u100_ctx *u100_ctx);

/**
 * uwbs_sync_reset_and_coredump() - Reset UWBS and get ATR and report coredump when it fails.
 * @u100_ctx: U100 context.
 * @msg: coredump message.
 *
 * Return: 0 on success.
 */
int uwbs_sync_reset_and_coredump(struct u100_ctx *u100_ctx, const char *msg);

int uwbs_start_download(struct u100_ctx *u100_ctx);

bool uwbs_sync_start_download(struct u100_ctx *u100_ctx);

static inline unsigned int get_irq_count(struct u100_ctx *u100_ctx)
{
	return kstat_irqs_usr(u100_ctx->u100_irq.num);
}

#endif /* __UWB_H__ */
