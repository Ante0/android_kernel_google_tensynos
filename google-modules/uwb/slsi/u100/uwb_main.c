// SPDX-License-Identifier: GPL-2.0
/**
 * @copyright Copyright (c) 2023 Samsung Electronics Co., Ltd
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "include/uwb.h"
#include "include/uwb_coredump.h"
#include "include/uwb_debugfs.h"
#include "include/uwb_fw_common.h"
#include "include/uwb_gpio.h"
#include "include/uwb_spi.h"
#include "include/uwb_sysnodes.h"

#define UWB_DD_VERSION "0.7.13"
#define ENUM_TO_STR(R) #R
#define MIN_TL_SIZE 2
#define TEST_FW_SUFFIX "_test"
#define DOT_CHAR '.'
#define ALV_PLL_WORK 0x00
#define ALV_NOT_WORK 0x01
#define PLL_NOT_WORK 0x02

static char *fwname;
module_param(fwname, charp, 0664);
MODULE_PARM_DESC(fwname, "Use fwname as firmware binary to flash U100");

static bool disableCrcChecksum;
module_param(disableCrcChecksum, bool, 0664);
MODULE_PARM_DESC(disableCrcChecksum, "Disable CRC checksum verification while flashing fw");

static bool ignore_atr_error;
module_param(ignore_atr_error, bool, 0664);
MODULE_PARM_DESC(ignore_atr_error, "Ignore ATR error when loading u100 driver");

static bool disable_coredump_reset;
module_param(disable_coredump_reset, bool, 0664);
MODULE_PARM_DESC(disable_coredump_reset, "Disable reset when fw crashes");

static const char *get_firmware_type_str(struct firmware_info *fw_info)
{
	switch (fw_info->fw_type) {
	case FW_TYPE_EDS:
		return "EDS_FW";
	case FW_TYPE_FACTORY:
		return "Factory_FW_";
	case FW_TYPE_USER:
		return "User_FW_";
	case FW_TYPE_DEV:
		return "Dev_FW_";
	default:
		return "Unknown_FW_";
	}
}

static void display_fw_version(struct u100_ctx *u100_ctx)
{
	if ((u100_ctx->u100_state == U100_EDS_STATE || u100_ctx->u100_state == U100_FW_STATE)
		&& !atomic_read(&u100_ctx->flashing))
		UWB_INFO("U100 fw version[%s%.*s], key_type[%d], host_type[%#x], dev_id[%*phN]\n",
			get_firmware_type_str(&u100_ctx->fw_info), FW_VERSION_LEN,
			u100_ctx->fw_info.version, u100_ctx->fw_info.key_type,
			u100_ctx->fw_info.host_type, DEV_ID_LEN, u100_ctx->fw_info.devid);
}

static int parse_firmware_info(struct u100_ctx *u100_ctx, uint8_t *data, int skb_len)
{
	int ret = 0;
	uint8_t tlv_num;
	uint8_t type;
	uint8_t len;

	memcpy(u100_ctx->fw_info.version, UNKNOWN_STR, sizeof(UNKNOWN_STR));
	memset(u100_ctx->fw_info.nv_hash, 0, NV_HASH_LEN);
	u100_ctx->fw_info.host_type = HOST_TYPE_LEGACY;
	u100_ctx->fw_info.key_type = KEY_TYPE_UNKNOWN;

	tlv_num = *data;
	data++;
	skb_len--;
	while (tlv_num > 0 && skb_len > MIN_TL_SIZE) {
		type = *data;
		data++;
		skb_len--;

		len = *data;
		data++;
		skb_len--;
		if (len > skb_len)
			break;

		/* TODO: returns an error when mandatory TLVs are missing or broken. */
		if (type == TLV_TYPE_FW_VERSION) {
			if (len == FW_VERSION_LEN) {
				memcpy(u100_ctx->fw_info.version, data, len);
			} else {
				UWB_ERR("Invalid length %d for ATR FW_VERSION", len);
				ret = -EPROTO;
			}
		} else if (type == TLV_TYPE_NV_HASH) {
			if (len == NV_HASH_LEN) {
				memcpy(u100_ctx->fw_info.nv_hash, data, len);
			} else {
				UWB_ERR("Invalid length %d for ATR NV_HASH", len);
				ret = -EPROTO;
			}
		} else if (type == TLV_TYPE_KEY_TYPE) {
			if (len == KEY_TYPE_LEN) {
				u100_ctx->fw_info.key_type = *data;
			} else {
				UWB_ERR("Invalid length %d for ATR KEY_TYPE", len);
				ret = -EPROTO;
			}
		} else if (type == TLV_TYPE_DEV_ID) {
			if (len == DEV_ID_LEN) {
				memcpy(u100_ctx->fw_info.devid, data, len);
			} else {
				UWB_ERR("Invalid length %d for ATR DEV_ID", len);
				ret = -EPROTO;
			}
		} else if (type == TLV_TYPE_HW_STATUS) {
			if (len == HW_STATUS_LEN) {
				if ((*data & ALV_NOT_WORK) == ALV_NOT_WORK)
					UWB_ERR("Hardware status ALV clock is not working");
				if ((*data & PLL_NOT_WORK) == PLL_NOT_WORK)
					UWB_ERR("Hardware status PLL clock is not working");
				u100_ctx->fw_info.hw_status = *data;
			} else {
				UWB_ERR("Invalid length %d for ATR HW_STATUS", len);
				ret = -EPROTO;
			}
		} else if (type == TLV_TYPE_HOST_TYPE) {
			if (len == HOST_TYPE_LEN) {
				u100_ctx->fw_info.host_type = *data >> HOST_TYPE_BIT_OFFSET;
			} else {
				UWB_ERR("Invalid length %d for ATR HOST_TYPE", len);
				ret = -EPROTO;
			}
		} else {
			UWB_DEBUG("Unknown TLV type[0x%02x] in ATR", type);
		}

		data += len;
		skb_len -= len;
		tlv_num--;
	}

	if (!ret)
		display_fw_version(u100_ctx);

	return ret;
}

static bool is_atr(struct u100_ctx *u100_ctx, struct sk_buff *skb)
{
	struct uci_msg_hdr *hdr = (struct uci_msg_hdr *)skb->data;
	u32 bid = 0;

	if (skb->len < MINIMAL_ATR_SIZE)
		return false;

	if (hdr->mt != UCI_MT_NTF || hdr->gid != GID_VENDOR_CONFIG ||
		hdr->oid != OID_VENDOR_DEV_CTRL)
		return false;

	bid = *((u32 *)(skb->data + UCI_HEAD_SIZE));
	if (bid != UCI_ATR_BID)
		return false;

	return true;
}

static void dump_skb(struct sk_buff *skb)
{
	print_hex_dump(KERN_WARNING, LOG_TAG "Recv Data: ", DUMP_PREFIX_NONE, 16, 1,
		       skb->data, min(skb->len, SKB_MAX_PRINT_SIZE), false);
}

static int process_atr(struct u100_ctx *u100_ctx, struct sk_buff *skb)
{
	unsigned char *data = skb->data;

	mutex_lock(&u100_ctx->atr_lock);

	/* Process ATR only when waiting_atr is marked. */
	if (u100_ctx->waiting_atr) {
		if (!is_atr(u100_ctx, skb)) {
			UWB_WARN("Unexpected non-ATR received.\n");
			dump_skb(skb);
			mutex_unlock(&u100_ctx->atr_lock);
			return -EIO;
		}
	} else {
		if (is_atr(u100_ctx, skb)) {
			UWB_WARN("Unexpected ATR received.\n");
			dump_skb(skb);
			mutex_unlock(&u100_ctx->atr_lock);
			return -EIO;
		}
		/* Not in sync-power-up operation. */
		mutex_unlock(&u100_ctx->atr_lock);
		return 0;
	}

	if (skb->len < MINIMAL_ATR_SIZE) {
		UWB_WARN("Illegal ATR message received (too short).\n");
		goto err_process_atr;
	}

	memset(&u100_ctx->fw_info, 0, sizeof(struct firmware_info));
	u100_ctx->fw_info.fw_type = FW_TYPE_UNKNOWN;
	u100_ctx->u100_state = data[U100_STATE_INDEX_IN_ATR];

	if (u100_ctx->u100_state == U100_FW_STATE) {
		if (skb->len <= FW_TYPE_INDEX_IN_ATR) {
			UWB_WARN("Illegal ATR message received (failed to parse FW type).\n");
			goto err_process_atr;
		}

		u100_ctx->fw_info.fw_type = data[FW_TYPE_INDEX_IN_ATR];
		if (u100_ctx->fw_info.fw_type == FW_TYPE_EDS)
			u100_ctx->u100_state = U100_EDS_STATE;
		else if (parse_firmware_info(u100_ctx, &data[TLV_SIZE_INDEX_IN_ATR],
					     skb->len - TLV_SIZE_INDEX_IN_ATR))
			goto err_process_atr;

		u100_ctx->is_download_mode = false;
	}

	UWB_INFO("ATR received, U100 state: [%#x].\n", u100_ctx->u100_state);

	if (u100_ctx->u100_state == u100_ctx->u100_state_wanted) {
		u100_ctx->waiting_atr = false;
		complete_all(&u100_ctx->atr_done_cmpl);
	}
	mutex_unlock(&u100_ctx->atr_lock);

	return 0;

err_process_atr:
	u100_ctx->waiting_atr = false;
	u100_ctx->u100_state = U100_UNKNOWN_STATE;
	complete_all(&u100_ctx->atr_done_cmpl);
	mutex_unlock(&u100_ctx->atr_lock);

	dump_skb(skb);

	return -EIO;
}

static void io_dev_recv_uci(struct u100_ctx *u100_ctx, struct sk_buff *skb)
{
	struct sk_buff_head *rxq = &u100_ctx->sk_rx_q;
	struct sk_buff *rx_queue_head;

	if (process_atr(u100_ctx, skb))
		UWB_ERR("Failed to process ATR.\n");

	if (is_coredump(u100_ctx, skb)) {
		int ret = u100_process_coredump(u100_ctx, skb);

		if (ret)
			UWB_ERR("Uwb coredump %d", ret);
	}

	if (unlikely(rxq->qlen >= MAX_RXQ_DEPTH)) {
		UWB_WARN("Dropped because rx queue overflow: total %d\n",
				MAX_RXQ_DEPTH);
		while (rxq->qlen >= MAX_RXQ_DEPTH) {
			rx_queue_head = skb_dequeue(rxq);

			if (unlikely(!rx_queue_head))
				UWB_WARN("The rx queue head is null");
			else {
				UWB_WARN("Drop rx queue head data: %d bytes",
						rx_queue_head->len);
				dev_kfree_skb_any(rx_queue_head);
			}
		}
	}
	skb_queue_tail(rxq, skb);

	wake_up(&u100_ctx->wq);
}

static void unregister_device(struct u100_ctx *u100_ctx)
{
	misc_deregister(&u100_ctx->uci_dev);
	UWB_DEBUG("Deregistered %s\n", u100_ctx->uci_dev.name);
}

static void register_device(struct u100_ctx *u100_ctx)
{
	struct miscdevice *uci_misc;
	int ret;

	uci_misc = &u100_ctx->uci_dev;
	mutex_lock(&u100_ctx->atr_lock);
	UWB_DEBUG("Register Device");
	uci_misc->minor = MISC_DYNAMIC_MINOR;
	ret = misc_register(uci_misc);
	if (ret) {
		UWB_ERR("Failed to register uci device\n");
		mutex_unlock(&u100_ctx->atr_lock);
		return;
	}
	UWB_INFO("Registered %s\n", uci_misc->name);
	mutex_unlock(&u100_ctx->atr_lock);
}

static int uci_open(struct inode *inode, struct file *file)
{
	int ref_cnt;
	struct miscdevice *uci_dev = file->private_data;
	struct u100_ctx *u100_ctx =
		container_of(uci_dev, struct u100_ctx, uci_dev);
	ref_cnt = atomic_inc_return(&u100_ctx->opened);
	return 0;
}

static int uci_release(struct inode *inode, struct file *fp)
{
	int ref_cnt;
	struct miscdevice *uci_dev = fp->private_data;
	struct u100_ctx *u100_ctx =
		container_of(uci_dev, struct u100_ctx, uci_dev);
	ref_cnt = atomic_dec_return(&u100_ctx->opened);
	return 0;
}

static ssize_t uci_read(struct file *fp, char __user *buf, size_t count,
			loff_t *off)
{
	struct miscdevice *uci_dev = fp->private_data;
	struct u100_ctx *u100_ctx =
		container_of(uci_dev, struct u100_ctx, uci_dev);

	struct sk_buff_head *rxq = &u100_ctx->sk_rx_q;
	struct sk_buff *skb;
	int copied = 0;

	if (count == 0)
		return -EINVAL;

	if (atomic_read(&u100_ctx->flashing)) {
		UWB_WARN("Read device failed, device is busy.\n");
		return -EBUSY;
	}
	if (u100_ctx->is_download_mode) {
		UWB_WARN("Write device failed, u100 is in download mode.\n");
		return -EPERM;
	}
	if (fp->f_flags & O_NONBLOCK) {
		if (skb_queue_empty(rxq))
			return 0;
	} else {
		if (wait_event_interruptible(u100_ctx->wq, !skb_queue_empty(rxq)))
			return -ERESTARTSYS;
	}

	skb = skb_dequeue(rxq);
	if (unlikely(!skb))
		return 0;

	copied = skb->len > count ? count : skb->len;

	if (copy_to_user(buf, skb->data, copied)) {
		skb_queue_head(rxq, skb);
		return -EFAULT;
	}

	if (skb->len > count) {
		skb_pull(skb, count);
		skb_queue_head(rxq, skb);
	} else
		dev_kfree_skb_any(skb);
	UWB_DEBUG("Read UCI packet %d bytes", copied);
	return copied;
}

static ssize_t uci_write(struct file *fp, const char __user *data, size_t count,
			 loff_t *fpos)
{
	struct miscdevice *uci_dev = fp->private_data;
	struct u100_ctx *u100_ctx =
		container_of(uci_dev, struct u100_ctx, uci_dev);
	char *buff = NULL;
	int ret;
	int len = count;

	mutex_lock(&u100_ctx->ioctl_mutex);
	if (atomic_read(&u100_ctx->flashing)) {
		UWB_WARN("Write device failed, device is busy.\n");
		ret = -EBUSY;
		goto exit;
	}
	if (!atomic_read(&u100_ctx->u100_powered_on)) {
		UWB_WARN("Write device failed, due to device has powered off.\n");
		ret = -EIO;
		goto exit;
	}
	if (u100_ctx->is_download_mode) {
		UWB_WARN("Write device failed, u100 is in download mode.\n");
		ret = -EPERM;
		goto exit;
	}
	/* Store IPC message */
	buff = kzalloc(count, GFP_KERNEL);
	if (!buff) {
		ret = -ENOMEM;
		goto exit;
	}
	if (copy_from_user(buff, data, count)) {
		ret = -EFAULT;
		goto exit;
	}

	ret = link_send_package(u100_ctx, buff, count);

	if (ret < 0)
		goto exit;

	kfree(buff);
	mutex_unlock(&u100_ctx->ioctl_mutex);
	UWB_DEBUG("Write UCI packet %d bytes", len);
	return count;

exit:
	kfree(buff);
	mutex_unlock(&u100_ctx->ioctl_mutex);
	return ret;
}

static __poll_t uci_poll(struct file *fp, struct poll_table_struct *wait)
{
	struct miscdevice *uci_dev = fp->private_data;
	struct u100_ctx *u100_ctx =
		container_of(uci_dev, struct u100_ctx, uci_dev);

	if (skb_queue_empty(&u100_ctx->sk_rx_q))
		poll_wait(fp, &u100_ctx->wq, wait);

	if (!atomic_read(&u100_ctx->flashing)
		&& !u100_ctx->is_download_mode
		&& !skb_queue_empty(&u100_ctx->sk_rx_q))
		return POLLIN | POLLRDNORM;

	return POLLOUT | POLLWRNORM;
}

static bool set_fw_name(struct u100_ctx *u100_ctx, char *name)
{
	char *ret;
	int len;
	struct uwb_firmware_ctx *uwb_fw_ctx = &u100_ctx->uwb_fw_ctx;
	char *fw_name = uwb_fw_ctx->uwb_fw[DEFAULT_BIN_IDX].fw_name;
	char *test_fw_name = uwb_fw_ctx->uwb_fw[DEFAULT_BIN_TEST_IDX].fw_name;

	test_fw_name[0] = 0;
	len = strscpy(fw_name, name, MAX_FW_NAME_LEN + 1);
	if (len == -E2BIG) {
		UWB_ERR("fwname too long");
		return false;
	}

	len += strlen(TEST_FW_SUFFIX);
	if (len > MAX_FW_NAME_LEN) {
		UWB_WARN("fwname_test too long (%d)", len);
		return true;
	}

	ret = strrchr(fw_name, DOT_CHAR);
	if (!ret)
		scnprintf(test_fw_name, MAX_FW_NAME_LEN + 1, "%s" TEST_FW_SUFFIX, fw_name);
	else {
		len = ret - fw_name;
		scnprintf(test_fw_name, MAX_FW_NAME_LEN + 1, "%.*s" TEST_FW_SUFFIX "%s",
			len, fw_name, ret);
	}

	return true;
}

static long uci_ioctl(struct file *fp, unsigned int cmd, unsigned long args)
{
	struct miscdevice *uci_dev = fp->private_data;
	struct u100_ctx *u100_ctx =
		container_of(uci_dev, struct u100_ctx, uci_dev);
	int ret = 0;
	struct flash_param param;

	if (_IOC_TYPE(cmd) != UWB_IOC_TYPE)
		return -ENOEXEC;
	mutex_lock(&u100_ctx->ioctl_mutex);
	if (atomic_read(&u100_ctx->flashing)) {
		UWB_WARN("Ioctl failed, device is busy.\n");
		ret = -EBUSY;
		goto exit;
	}
	switch (cmd) {
	case UWB_IOCTL_RESET:
		UWB_DEBUG("UWB_IOCTL_RESET\n");
		disable_irq(u100_ctx->spi->irq);
		uwbs_reset(u100_ctx);
		enable_irq(u100_ctx->spi->irq);
		break;
	case UWB_IOCTL_FW_FLASH:
		UWB_DEBUG("UWB_IOCTL_FW_FLASH\n");
		if (copy_from_user(&param, (void __user *)args, sizeof(struct flash_param))) {
			UWB_ERR("Flash firmware failed, can't copy file from user space\n");
			ret = -EFAULT;
		} else {
			if (!set_fw_name(u100_ctx, param.fw_name))
				return -EPERM;
			u100_ctx->uwb_fw_ctx.is_auto = false;
			u100_ctx->uwb_fw_ctx.config = 0;
			u100_ctx->uwb_fw_ctx.disable_crc_check = param.disable_crc_check;
			UWB_DEBUG("UWB_IOCTL_FW_FLASH [%s], disable_crc_check[%d]\n",
				u100_ctx->uwb_fw_ctx.uwb_fw[DEFAULT_BIN_IDX].fw_name,
				u100_ctx->uwb_fw_ctx.disable_crc_check);
			ret = fw_download(u100_ctx);
		}
		break;

	case UWB_IOCTL_POWER_ON:
		UWB_DEBUG("UWB_IOCTL_POWER_ON\n");
		/* Check u100 status */
		ret = uwbs_sync_reset_and_coredump(u100_ctx, "IOCTL_POWER_ON");
		break;

	case UWB_IOCTL_POWER_OFF:
		UWB_DEBUG("UWB_IOCTL_POWER_OFF\n");
		disable_irq(u100_ctx->spi->irq);
		uwbs_power_off(u100_ctx);
		enable_irq(u100_ctx->spi->irq);
		break;

	case UWB_IOCTL_FW_INFO:
		UWB_DEBUG("UWB_IOCTL_FW_INFO");
		if (copy_to_user((void __user *)args, &u100_ctx->fw_info,
					sizeof(struct firmware_info))) {
			UWB_ERR("Get firmware version failed, can't copy to user space\n");
			ret = -EFAULT;
		}
		break;

	case UWB_IOCTL_DISABLE_COREDUMP_RESET:
		UWB_DEBUG("UWB_IOCTL_DISABLE_COREDUMP_RESET\n");
		if (copy_from_user(&u100_ctx->coredump->disable_reset, (void __user *)args,
					sizeof(int))) {
			UWB_ERR("UWB_IOCTL_DISABLE_COREDUMP_RESET failed");
			ret = -EFAULT;
		}
		break;

	/**
	 * An IOCTL interface to reset u100-power-switch to be called later from
	 * upper-layer when it is needed.
	 */
	case UWB_IOCTL_RESET_VBAT:
		/**
		 * INFO log to track if there is any vbat-reset called from externally.
		 */
		UWB_INFO("UWB_IOCTL_RESET_VBAT");
		if (IS_ERR_OR_NULL(u100_ctx->gpio_u100_power)) {
			UWB_ERR("Power switch error %d",
					PTR_ERR_OR_ZERO(u100_ctx->gpio_u100_power));
			ret = -EIO;
		} else {
			uwbs_ldsw_reset(u100_ctx);
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}
exit:
	mutex_unlock(&u100_ctx->ioctl_mutex);
	return ret;
}

static void start_download_mode(struct u100_ctx *u100_ctx)
{
	if (!uwbs_start_download(u100_ctx)) {
		u100_ctx->is_download_mode = true;
		UWB_INFO("Enter U100 download mode success.\n");
	} else {
		UWB_ERR("Enter U100 download mode failed.\n");
	}
}

static bool enter_bl0_mode(struct u100_ctx *u100_ctx)
{
	bool ret;

	if (!enter_uwb_bl0_mode_with_retry(u100_ctx, RETRY_MAX_CNT)) {
		u100_ctx->is_download_mode = true;
		UWB_INFO("Enter U100 BL0 mode success.");
		ret = true;
	} else {
		UWB_ERR("Enter U100 BL0 mode failed.");
		ret = false;
	}
	return ret;
}

static bool enter_bl1_mode(struct u100_ctx *u100_ctx)
{
	bool ret;

	if (!enter_uwb_bl1_mode_with_retry(u100_ctx, RETRY_MAX_CNT)) {
		u100_ctx->is_download_mode = true;
		UWB_INFO("Enter U100 BL1 mode success.");
		ret = true;
	} else {
		UWB_ERR("Enter U100 BL1 mode failed.");
		ret = false;
	}
	return ret;
}

static const struct file_operations uci_fops = {
	.owner = THIS_MODULE,
	.open = uci_open,
	.release = uci_release,
	.unlocked_ioctl = uci_ioctl,
	.read = uci_read,
	.write = uci_write,
	.poll = uci_poll,
};

static void dev_pm_clear_wake_irq_void(void *dev)
{
	dev_pm_clear_wake_irq(dev);
}

static void device_uninit_wakeup_void(void *dev)
{
	device_init_wakeup(dev, false);
}

static int uwb_spi_probe(struct spi_device *spi)
{
	struct u100_ctx *u100_ctx = NULL;
	int ret = 0;
	struct miscdevice *uci_misc;
	int8_t initial_retries = -1;

	UWB_INFO("Version %s", UWB_DD_VERSION);

	if (spi_setup(spi)) {
		UWB_ERR("Fail to setup spi\n");
		return -EINVAL;
	}

	u100_ctx = devm_kzalloc(&spi->dev, sizeof(*u100_ctx), GFP_KERNEL);
	if (!u100_ctx)
		return -ENOMEM;

	u100_ctx->spi = spi;
	mutex_init(&u100_ctx->lock);
	mutex_init(&u100_ctx->atr_lock);
	spi_set_drvdata(spi, u100_ctx);

	skb_queue_head_init(&u100_ctx->sk_rx_q);
	init_completion(&u100_ctx->tx_done_cmpl);
	init_completion(&u100_ctx->atr_done_cmpl);
	init_completion(&u100_ctx->process_done_cmpl);
	init_waitqueue_head(&u100_ctx->wq);
	mutex_init(&u100_ctx->ioctl_mutex);
	mutex_init(&u100_ctx->tx_mutex);

	ret = device_init_wakeup(&spi->dev, true);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(&spi->dev, device_uninit_wakeup_void, &spi->dev);
	if (ret)
		return ret;

	ret = dev_pm_set_wake_irq(&spi->dev, u100_ctx->spi->irq);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(&spi->dev, dev_pm_clear_wake_irq_void, &spi->dev);
	if (ret)
		return ret;

	uci_misc = &u100_ctx->uci_dev;
	uci_misc->minor = MISC_DYNAMIC_MINOR;
	uci_misc->name = "uci";
	uci_misc->fops = &uci_fops;
	uci_misc->parent = &spi->dev;

	u100_ctx->recv_package = io_dev_recv_uci;
	uwb_set_spi_type(SPI_TYPE_UCI);

	u100_ctx->coredump = devm_kzalloc(&spi->dev, sizeof(struct uwb_coredump),
			GFP_KERNEL);
	if (!u100_ctx->coredump) {
		ret = -ENOMEM;
		goto err_setup;
	}
	u100_ctx->coredump->disable_reset = disable_coredump_reset;
	ret = u100_register_coredump(u100_ctx);
	if (ret) {
		devm_kfree(&spi->dev, u100_ctx->coredump);
		u100_ctx->coredump = NULL;
		UWB_WARN("SSCD registration failed: %d", ret);
	}

	ret = init_controller_layer(u100_ctx);
	if (ret < 0)
		goto err_setup;
	mdelay(GPIO_DELAY_MS);
	u100_ctx->power_stats = devm_kzalloc(&spi->dev,
				sizeof(struct u100_power_stats),
				GFP_KERNEL);
	if (!u100_ctx->power_stats) {
		ret = -ENOMEM;
		goto err_setup;
	}
	ret = u100_power_stats_init(u100_ctx);
	if (ret) {
		u100_power_stats_deinit(u100_ctx);
		UWB_ERR("Failed to initialize the power stats monitor\n");
	}

	ret = uwb_sysfs_init(u100_ctx);
	if (ret)
		UWB_ERR("Failed to initialize the sysnodes\n");

	uwb_debugfs_init(u100_ctx);

	atomic_set(&u100_ctx->num_spi_slow_txs, 0);
	u100_ctx->is_download_mode = false;
	u100_ctx->uwb_fw_ctx.config = 0;
	u100_ctx->uwb_fw_ctx.is_auto = false;
	u100_ctx->uwb_fw_ctx.disable_crc_check = disableCrcChecksum;
	u100_ctx->uwb_fw_ctx.bl1_retries = initial_retries;
	if (fwname) {
		if (!strcmp(fwname, ENUM_TO_STR(DOWNLOAD))) {
			start_download_mode(u100_ctx);
		} else if (!strcmp(fwname, ENUM_TO_STR(BL0))) {
			enter_bl0_mode(u100_ctx);
		} else if (!strcmp(fwname, ENUM_TO_STR(BL1))) {
			enter_bl1_mode(u100_ctx);
		} else {
			UWB_DEBUG("fwname %s", fwname);
			if (set_fw_name(u100_ctx, fwname)) {
				ret = fw_download(u100_ctx);
				if (ret < 0)
					UWB_WARN("init_fw_download failed\n");
			}
		}
	}

	register_device(u100_ctx);
	UWB_DEBUG("UWB probe end");
	return 0;

err_setup:
	if (u100_ctx->coredump && u100_ctx->coredump->sscd)
		u100_unregister_coredump(u100_ctx);

	mutex_destroy(&u100_ctx->lock);
	mutex_destroy(&u100_ctx->atr_lock);
	mutex_destroy(&u100_ctx->ioctl_mutex);
	mutex_destroy(&u100_ctx->tx_mutex);
	unregister_device(u100_ctx);
	UWB_ERR("UWB probe error %d", ret);
	return ret;
}

static void uwb_spi_remove(struct spi_device *spi)
{
	struct u100_ctx *u100_ctx = spi_get_drvdata(spi);

	if (u100_ctx) {
		set_gpio_value(&u100_ctx->gpio_u100_en, 0);
		if (!IS_ERR_OR_NULL(u100_ctx->gpio_u100_power))
			gpiod_direction_output(u100_ctx->gpio_u100_power, 0);
		skb_queue_purge(&u100_ctx->sk_rx_q);
		unregister_device(u100_ctx);
		mutex_destroy(&u100_ctx->lock);
		mutex_destroy(&u100_ctx->atr_lock);
		mutex_destroy(&u100_ctx->ioctl_mutex);
		mutex_destroy(&u100_ctx->tx_mutex);

		u100_unregister_coredump(u100_ctx);

		uwb_sysfs_exit(u100_ctx);
		uwb_debugfs_deinit(u100_ctx);
		u100_power_stats_deinit(u100_ctx);
	}
	UWB_DEBUG("Removed U100 spi module\n");
}

static const struct of_device_id uwb_spi_dt_match[] = {
	{ .compatible = "samsung,u100" },
	{},
};
MODULE_DEVICE_TABLE(of, uwb_spi_dt_match);

static int u100_suspend(struct device *dev)
{
	struct u100_ctx *u100_ctx = dev_get_drvdata(dev);

	/*
	 * Make sure to mask the interrupt at suspend time. If we don't
	 * do this then interrupts can still come in post-suspend and
	 * cause us to kick off a transfer. If that happens after the SPI
	 * controller has been suspended then the transfer will fail and
	 * everyone will be confused.
	 */
	disable_irq(u100_ctx->spi->irq);

	return 0;
}

static int u100_resume(struct device *dev)
{
	struct u100_ctx *u100_ctx = dev_get_drvdata(dev);

	enable_irq(u100_ctx->spi->irq);
	u100_power_stats_on_switch(u100_ctx);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(u100_pm_ops, u100_suspend, u100_resume);

static struct spi_driver uwb_spi_driver = {
	.probe = uwb_spi_probe,
	.remove = uwb_spi_remove,
	.driver = {
		.name = "u100",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(uwb_spi_dt_match),
		.pm = &u100_pm_ops,
		.suppress_bind_attrs = true,
	},
};

module_spi_driver(uwb_spi_driver);

MODULE_VERSION(UWB_DD_VERSION);
MODULE_DESCRIPTION("UWB U100 SPI driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Samsung S.LSI");
