// SPDX-License-Identifier: GPL-2.0
/**
 * @copyright Copyright (c) 2023 Samsung Electronics Co., Ltd
 *
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/string.h>

#include "include/uwb.h"
#include "include/uwb_gpio.h"
#include "include/uwb_spi.h"

#define MT_OFFSET 0
#define CONTROL_PACKET_LENGTH_OFFSET 3
#define DATA_PACKET_LENGTH_MSB_OFFSET 3
#define DATA_PACKET_LENGTH_LSB_OFFSET 2
#define CRASH_UCI_LEN 8
#define MIN_UCI_PAYLOAD_SIZE 0
#define MAX_UCI_PAYLOAD_SIZE 0xFFFF

static bool drop_uci_pkt_on_format_error = true;
module_param(drop_uci_pkt_on_format_error, bool, 0664);
MODULE_PARM_DESC(drop_uci_pkt_on_format_error,
		"Drop uci packet on formatted error");

static int spi_send_timeout_ms = 200;
module_param(spi_send_timeout_ms, int, 0664);
MODULE_PARM_DESC(spi_send_timeout_ms, "Timeout of SPI sending");

static int uci_retry_count = 6;
module_param(uci_retry_count, int, 0664);
MODULE_PARM_DESC(uci_retry_count, "The max retry count of UCI command");

static int get_uci_payload_len(unsigned char *buffer)
{
	struct uci_msg_hdr *msg_hdr = (struct uci_msg_hdr *)buffer;

	if (msg_hdr->mt == UCI_MT_CMD || msg_hdr->mt == UCI_MT_RESP
		|| msg_hdr->mt == UCI_MT_NTF)
		return msg_hdr->len;
	else
		return msg_hdr->data_len;
}

static bool valid_header(char *buffer)
{
	if ((*(buffer) == NONE_DATA_FF && *(buffer + 1) == NONE_DATA_FF &&
				*(buffer + 2) == NONE_DATA_FF && *(buffer + 3) == NONE_DATA_FF))
		return false;
	if ((*(buffer) == NONE_DATA_00 && *(buffer + 1) == NONE_DATA_00 &&
				*(buffer + 2) == NONE_DATA_00 && *(buffer + 3) == NONE_DATA_00))
		return false;
	return true;
}

/* Function to handle receive-only communication */
static void handle_receive_only(struct u100_ctx *u100_ctx)
{
	int ret;
	unsigned int uci_payload_len, uci_transfer_len;
	struct sk_buff *rx_skb;
	char *rx_head_buff = NULL;
	char *rx_data_buff = NULL;

	rx_head_buff = kzalloc(UCI_HEAD_SIZE, GFP_KERNEL);
	if (!rx_head_buff)
		goto exit;

	ret = uwb_spi_recv(u100_ctx, rx_head_buff, UCI_HEAD_SIZE);
	if (ret)
		goto exit;

	if (!valid_header(rx_head_buff))
		goto exit;

	uci_payload_len = get_uci_payload_len(rx_head_buff);

	/* UCI payload length can be zero according to spec */
	/* Should not be treated as error */
	if (likely(uci_payload_len > 0)) {
		uci_transfer_len = ALIGN(uci_payload_len, 4);
		rx_data_buff = kzalloc(uci_transfer_len, GFP_KERNEL);
		if (!rx_data_buff)
			goto exit;

		ret = uwb_spi_recv(u100_ctx, rx_data_buff, uci_transfer_len);
		if (ret)
			goto exit;
	}

	rx_skb = alloc_skb(uci_payload_len + UCI_HEAD_SIZE, GFP_KERNEL);
	if (!rx_skb) {
		UWB_ERR("Failed to allocate sk_buff (msg size:%d)\n",
			uci_payload_len + UCI_HEAD_SIZE);
		goto exit;
	}
	skb_put(rx_skb, uci_payload_len + UCI_HEAD_SIZE);
	memcpy(rx_skb->data, rx_head_buff, UCI_HEAD_SIZE);
	if (likely(uci_payload_len > 0))
		memcpy((char *)(rx_skb->data + UCI_HEAD_SIZE), rx_data_buff, uci_payload_len);
	u100_ctx->recv_package(u100_ctx, rx_skb);
	UWB_DEBUG("Received UCI package %d bytes\n", uci_payload_len + UCI_HEAD_SIZE);

exit:
	kfree(rx_head_buff);
	kfree(rx_data_buff);
}

static void wakeup_sync_low(struct u100_ctx *u100_ctx)
{
	if (!atomic_read(&u100_ctx->u100_enter_download))
		set_gpio_value(&u100_ctx->gpio_u100_sync, 0);
}

static void link_clear_tx(struct u100_ctx *u100_ctx)
{
	dev_kfree_skb_any(u100_ctx->sk_tx);
	u100_ctx->sk_tx = NULL;
}

/* Function to handle full-duplex communication */
static void handle_full_duplex(struct u100_ctx *u100_ctx)
{
	struct sk_buff *rx_skb;
	int ret = 0;
	char *rx_data_buff = NULL;
	char *rx_rest_data_buff = NULL;
	char *tx_data_buff = NULL;
	unsigned int send_len, recv_len, recv_rest_len, recv_rest_align_len;

	u100_ctx->tx_status = 0;
	if (unlikely(!u100_ctx->sk_tx)) {
		UWB_WARN("TX is empty\n");
		wakeup_sync_low(u100_ctx);
		handle_receive_only(u100_ctx);
		goto exit;
	}
	send_len = ALIGN(u100_ctx->sk_tx->len, 4);
	tx_data_buff = kzalloc(send_len, GFP_KERNEL);
	rx_data_buff = kzalloc(send_len, GFP_KERNEL);
	if (unlikely(!tx_data_buff) || unlikely(!rx_data_buff)) {
		ret = -ENOMEM;
		wakeup_sync_low(u100_ctx);
		handle_receive_only(u100_ctx);
		goto exit;
	}
	memcpy(tx_data_buff, u100_ctx->sk_tx->data, u100_ctx->sk_tx->len);
	ret = uwb_spi_send(u100_ctx, tx_data_buff, send_len, rx_data_buff);
	wakeup_sync_low(u100_ctx);

	if (ret < 0)
		goto exit;

	/* RX header is valid when HOST is sending and receiving at the same time. */
	/* RX header is invalid when HOST is only sending and it is normal case. */
	if (!valid_header(rx_data_buff))
		goto exit;

	recv_len = get_uci_payload_len(rx_data_buff);
	if (recv_len + UCI_HEAD_SIZE > send_len) {
		recv_rest_len = recv_len + UCI_HEAD_SIZE - send_len;
		recv_rest_align_len = ALIGN(recv_rest_len, 4);
		rx_rest_data_buff = kzalloc(recv_rest_align_len, GFP_KERNEL);
		if (!rx_rest_data_buff) {
			ret = -ENOMEM;
			goto exit;
		}
		ret = uwb_spi_recv(u100_ctx, rx_rest_data_buff, recv_rest_align_len);
		if (ret < 0)
			goto exit;
	}

	rx_skb = alloc_skb(recv_len + UCI_HEAD_SIZE, GFP_KERNEL);
	if (!rx_skb) {
		ret = -ENOMEM;
		UWB_ERR("Failed to allocate sk buff (msg size:%d)\n",
			recv_len + UCI_HEAD_SIZE);
		goto exit;
	}
	skb_put(rx_skb, recv_len + UCI_HEAD_SIZE);
	if (!rx_rest_data_buff)
		memcpy(rx_skb->data, rx_data_buff, recv_len + UCI_HEAD_SIZE);
	else {
		memcpy(rx_skb->data, rx_data_buff, send_len);
		memcpy(rx_skb->data + send_len, rx_rest_data_buff, recv_rest_len);
	}
	u100_ctx->recv_package(u100_ctx, rx_skb);

exit:
	/* If objp is NULL, no operation is performed. */
	kfree(rx_data_buff);
	kfree(rx_rest_data_buff);
	kfree(tx_data_buff);

	/* Clear TX at the end of threaded interrupt. */
	link_clear_tx(u100_ctx);
	/* Complete the tx sending, the caller should check the status. */
	u100_ctx->tx_status = ret;
	complete_all(&u100_ctx->tx_done_cmpl);
}

irqreturn_t rx_tsk_work(int irq, void *data)
{
	struct u100_ctx *u100_ctx = data;

	if (atomic_read(&u100_ctx->flashing_fw)) {
		handle_fw_ap_send(u100_ctx);
		return IRQ_HANDLED;
	}

	mutex_lock(&u100_ctx->tx_mutex);
	u100_ctx->is_bhalf_entered = true;
	if (!u100_ctx->sk_tx) {
		/* no data in tx, only receive */
		handle_receive_only(u100_ctx);
	} else {
		/* need full duplex */
		/* first get send data from tx buffer */
		handle_full_duplex(u100_ctx);
	}
	mutex_unlock(&u100_ctx->tx_mutex);
	return IRQ_HANDLED;
}

static int link_check_uci(struct u100_ctx *u100_ctx, char *buff, unsigned int size)
{
	int payload_len;

	if (size < UCI_HEAD_SIZE)
		return -EPERM;

	payload_len = get_uci_payload_len(buff);
	if (payload_len != size - UCI_HEAD_SIZE) {
		print_hex_dump(KERN_ERR, LOG_TAG "Host UCI: ",
			DUMP_PREFIX_NONE, 16, 1, buff, size, false);

		if (drop_uci_pkt_on_format_error) {
			UWB_ERR("Uci packet will be dropped due to format error, size[%u]",
					size);
			return -EBADMSG;
		}
		UWB_WARN("Uci packet format error, size[%u]", size);
	}

	return 0;
}

/**
 * Only one TX packet is allowed at any time and that concurrent transmissions are not supported.
 * This improves maintainability and prevents misuse.
 */
static int link_set_tx(struct u100_ctx *u100_ctx, char *buff, unsigned int size)
{
	mutex_lock(&u100_ctx->tx_mutex);
	if (u100_ctx->sk_tx) {
		UWB_ERR("UCI sending in progress\n");
		mutex_unlock(&u100_ctx->tx_mutex);
		return -EBUSY;
	}

	u100_ctx->sk_tx = alloc_skb(size, GFP_KERNEL);
	if (!u100_ctx->sk_tx) {
		UWB_ERR("Failed to allocate sk_buff (msg size:%d)\n", size);
		mutex_unlock(&u100_ctx->tx_mutex);
		return -ENOMEM;
	}

	skb_put(u100_ctx->sk_tx, size);
	memcpy(u100_ctx->sk_tx->data, buff, size);
	mutex_unlock(&u100_ctx->tx_mutex);

	return 0;
}

static int link_send_tx(struct u100_ctx *u100_ctx)
{
	int ret = 0;
	unsigned int irq_count_before, irq_count_after;

	mutex_lock(&u100_ctx->tx_mutex);
	if (get_gpio_value(&u100_ctx->gpio_u100_sync)
		&& (atomic_read(&u100_ctx->u100_enter_download) == 0)) {
		UWB_WARN("Sync pin was unexpectedly left high\n");
		set_gpio_value(&u100_ctx->gpio_u100_sync, 0);
		udelay(100);
	}

	/* UWBS may wakeup host and report notification message */
	/* The tx could be empty since TX and RX could happen at the same time */
	if (!u100_ctx->sk_tx)
		goto exit;

	u100_ctx->is_bhalf_entered = false;
	irq_count_before = get_irq_count(u100_ctx);
	set_gpio_value(&u100_ctx->gpio_u100_sync, 1);
	reinit_completion(&u100_ctx->tx_done_cmpl);
	mutex_unlock(&u100_ctx->tx_mutex);

	ret = wait_for_completion_timeout(&u100_ctx->tx_done_cmpl,
			msecs_to_jiffies(spi_send_timeout_ms));

	mutex_lock(&u100_ctx->tx_mutex);
	wakeup_sync_low(u100_ctx);
	if (ret == 0) {
		ret = -ETIMEDOUT;
		irq_count_after = get_irq_count(u100_ctx);
		UWB_WARN("TX timeout, irq_cnt %u-%u\n", irq_count_before, irq_count_after);
		if (!u100_ctx->is_bhalf_entered)
			UWB_WARN("Not enter threaded IRQ Bottom Half\n");
		else
			UWB_WARN("Entered threaded IRQ Bottom Half but SPI sending timeout\n");

		/* Clear TX when sending timeout. */
		link_clear_tx(u100_ctx);
		goto exit;
	}
	ret = u100_ctx->tx_status;

exit:
	mutex_unlock(&u100_ctx->tx_mutex);
	return ret;
}

static void on_send_timeout(struct u100_ctx *u100_ctx)
{
	UWB_WARN("Send UCI timeout %d ms * %d times\n", spi_send_timeout_ms, uci_retry_count + 1);
	UWB_WARN("Force reset UWBS\n");
	uwbs_reset(u100_ctx);
}

int link_send_package(struct u100_ctx *u100_ctx, char *buff, unsigned int size)
{
	int retry = 0;
	int ret;

	ret = link_check_uci(u100_ctx, buff, size);
	if (ret)
		return ret;

	do {
		ret = link_set_tx(u100_ctx, buff, size);
		if (ret)
			return ret;

		UWB_DEBUG("Set TX buffer and prepare to wakeup U100\n");
		ret = link_send_tx(u100_ctx);
		retry++;
	} while (-ETIMEDOUT == ret && retry <= uci_retry_count);

	if (-ETIMEDOUT == ret)
		on_send_timeout(u100_ctx);

	return ret;
}

