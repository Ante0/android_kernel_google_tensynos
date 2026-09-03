// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, Google LLC
 *
 * MAX777x9 SPMI bus driver
 */
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pm_wakeup.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/usb/pd.h>
#include <linux/usb/tcpm.h>

#include "tcpci_max77759.h"
#include "tcpci_max77779_vendor_reg.h"

#define SPMI_MAX_EXT_RD_WR_SIZE                 16U
#define MAX_PD_MSG_SIZE_PER_READ                (SPMI_MAX_EXT_RD_WR_SIZE - \
						 TCPC_RECEIVE_BUFFER_METADATA_SIZE)

#define TX_BUF_REG_COUNT_SIZE                   1U
#define MAX_PD_MSG_SIZE_PER_WRITE               (SPMI_MAX_EXT_RD_WR_SIZE - TX_BUF_REG_COUNT_SIZE)

#define MAX777x9_BUCK_BOOST_I2C_SID		0x3
#define MAX77779_PMIC_SID			0xF

/* To protect against direct access to spmi driver and access via regmap */
struct mutex g_spmi_regmap_lock;

static void spmi_access_lock(void *arg)
{
	mutex_lock(&g_spmi_regmap_lock);
}

static void spmi_access_unlock(void *arg)
{
	mutex_unlock(&g_spmi_regmap_lock);
}

static const struct regmap_range max77779_tcpci_read_range[] = {
	regmap_reg_range(TCPC_VENDOR_ID        /* 0x0 */,
			 TCPC_PD_INT_REV_H     /* 0xb */),
	regmap_reg_range(TCPC_ALERT            /* 0x10 */,
			 TCPC_ALERT_EXTENDED   /* 0x21 */),
	regmap_reg_range(TCPC_COMMAND          /* 0x23 */,
			 TCPC_CNFG_EXT1        /* 0x2a */),
	regmap_reg_range(TCPC_MSG_HDR_INFO     /* 0x2e */,
			 TCPC_RX_BYTE_CNT      /* 0x30 */),
	regmap_reg_range(TCPC_TRANSMIT         /* 0x50 */,
			 TCPC_TRANSMIT         /* 0x50 */),
	regmap_reg_range(TCPC_VBUS_VOLTAGE     /* 0x70 */,
			 TCPC_VBUS_VOLTAGE_ALARM_LO_CFG_H /* 0x79 */),
	regmap_reg_range(TCPC_VENDOR_ALERT     /* 0x80 */,
			 MAX77779_MAX_ADDR     /* 0x97 */),
};

const struct regmap_access_table max77779_tcpci_read_table = {
	.yes_ranges = max77779_tcpci_read_range,
	.n_yes_ranges = ARRAY_SIZE(max77779_tcpci_read_range),
};

static const struct regmap_range max77779_tcpci_write_range[] = {
	regmap_reg_range(TCPC_ALERT             /* 0x10 */,
			 TCPC_POWER_CTRL        /* 0x1c */),
	regmap_reg_range(TCPC_FAULT_STATUS      /* 0x1f */,
			 TCPC_FAULT_STATUS      /* 0x1f */),
	regmap_reg_range(TCPC_ALERT_EXTENDED    /* 0x21 */,
			 TCPC_ALERT_EXTENDED    /* 0x21 */),
	regmap_reg_range(TCPC_COMMAND           /* 0x23 */,
			 TCPC_COMMAND           /* 0x23 */),
	regmap_reg_range(TCPC_CNFG_EXT1         /* 0x2a */,
			 TCPC_CNFG_EXT1         /* 0x2a */),
	regmap_reg_range(TCPC_MSG_HDR_INFO      /* 0x2e */,
			 TCPC_RX_DETECT         /* 0x2f */),
	regmap_reg_range(TCPC_TRANSMIT          /* 0x50 */,
			 TCPC_TX_BYTE_CNT       /* 0x51 */),
	regmap_reg_range(TCPC_VBUS_SINK_DISCONNECT_THRESH     /* 0x72 */,
			 TCPC_VBUS_VOLTAGE_ALARM_LO_CFG_H     /* 0x79 */),
	regmap_reg_range(TCPC_VENDOR_ALERT       /* 0x80 */,
			 TCPC_VENDOR_ALERT_MASK2 /* 0x83 */),
	regmap_reg_range(VENDOR_WDG_CTRL         /* 0x8a */,
			 MAX77779_MAX_ADDR       /* 0x97 */),
};

const struct regmap_access_table max77779_tcpci_write_table = {
	.yes_ranges = max77779_tcpci_write_range,
	.n_yes_ranges = ARRAY_SIZE(max77779_tcpci_write_range),
};

static const struct regmap_config max77779_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = MAX77779_MAX_ADDR,
	.rd_table = &max77779_tcpci_read_table,
	.wr_table = &max77779_tcpci_write_table,
	.lock = spmi_access_lock,
	.unlock = spmi_access_unlock,
};

/*
 * spmi_rmw8: SPMI Read Modify Write for 8 bit data
 *
 * @sdev: Handle to SPMI device
 * @addr: Register address of the SPMI device
 * @mask: Mask for value to be written
 * @val: Value to be written into register "addr"
 *
 * Returns 0 on success else return error value
 */
static int spmi_ext_register_rmw8(struct spmi_device *sdev, u8 addr, u8 mask, u8 val)
{
	int ret;
	u8 buf;

	ret = spmi_ext_register_read(sdev, addr, &buf, 1);
	if (ret)
		return ret;

	if ((mask & buf) == (mask & val))
		return 0;

	buf = (buf & ~mask) | (mask & val);
	return spmi_ext_register_write(sdev, addr, &buf, 1);
}

static int spmi_device_rmw(struct spmi_device *sdev, u8 sid, u8 addr, u8 mask, u8 val)
{
	int ret;
	u8 cached_usid = sdev->usid;

	mutex_lock(&g_spmi_regmap_lock);
	sdev->usid = sid;
	ret = spmi_ext_register_rmw8(sdev, addr, mask, val);
	sdev->usid = cached_usid;
	mutex_unlock(&g_spmi_regmap_lock);
	return ret;
}

static int max777x9_spmi_direct_otg_en(struct max77759_plat *chip, bool en)
{
	struct gpio_desc *gpiod = NULL;

	if (!chip->gpio.gpiodev) {
		logbuffer_log(chip->log, "Unable to locate max77759_tcpc_gpio chip");
		return -ENODEV;
	}

	gpiod = gpio_device_get_desc(chip->gpio.gpiodev, 0);
	gpiod_set_value_cansleep(gpiod, en ? 1 : 0);
	return 0;
}

static int configure_alert(struct max77759_plat *chip)
{
	struct spmi_device *sdev = chip->client;
	struct device_node *dn;

	dn = dev_of_node(chip->dev);
	if (!dn)
		return -EINVAL;

	if (!of_property_read_bool(dn, "goog,pmic-agpio-alert-config"))
		return 0;

	return spmi_device_rmw(sdev, MAX77779_PMIC_SID, AGPIO_CNFG5,
			       AGPIO_CNFG5_MODE_MASK, AGPIO_CNFG5_MODE_FN_PP);
}

static int max777x9_init_alert(struct max77759_plat *chip)
{
	struct gpio_desc *irq_gpio;
	int irq, ret;

	ret = configure_alert(chip);
	if (ret) {
		dev_err(chip->dev, "Unable to configure AGPIO as alert, ret=%d", ret);
		return ret;
	}

	irq_gpio = devm_gpiod_get_optional(chip->dev, "usbpd,usbpd_int", GPIOD_ASIS);
	irq = gpiod_to_irq(irq_gpio);
	if (!irq)
		return -ENODEV;

	return max77759_register_irq(irq, (IRQF_TRIGGER_LOW | IRQF_ONESHOT), chip);
}

/* Note: 'rx_buf' has space allocated for metadata (count and frametype) + pd_msg_size. */
static int max777x9_spmi_rx(struct google_shim_tcpci *tcpci, u8 *rx_buf, size_t pd_msg_size)
{
	struct max77759_plat *chip = tdata_to_max77759(tcpci->data);
	int rx_buf_head = 0, read_size, ret;
	int pending_pd_msg_size = (int)pd_msg_size;

	/*
	 * Use cache to store last 2 bytes as subsequent reads re-write it with
	 * metadata. Restore these cached values after read (except first) as
	 * the metadata for is discarded for subsequent reads.
	 */
	u8 cache[TCPC_RECEIVE_BUFFER_METADATA_SIZE] = {0,};
	u8 frame_type, cnt, frame_type_idx, cnt_idx;

	while (pending_pd_msg_size > 0) {
		if (rx_buf_head)
			memcpy(cache, rx_buf + rx_buf_head, TCPC_RECEIVE_BUFFER_METADATA_SIZE);

		read_size = min_t(int, pending_pd_msg_size, MAX_PD_MSG_SIZE_PER_READ);
		if (unlikely(read_size + rx_buf_head > pd_msg_size)) {
			logbuffer_log(chip->log,
				      "Insufficient buffer size, required size: %d, available size: %zu",
				      read_size, pd_msg_size - rx_buf_head);
			return -ENOMEM;
		}

		ret = regmap_raw_read(chip->data.regmap, TCPC_RX_BYTE_CNT, rx_buf + rx_buf_head,
				      read_size + TCPC_RECEIVE_BUFFER_METADATA_SIZE);
		if (ret) {
			logbuffer_log(chip->log,
				      "Regmap read failed with error %d", ret);
			return ret;
		}

		cnt_idx = rx_buf_head + TCPC_RECEIVE_BUFFER_COUNT_OFFSET;
		frame_type_idx = rx_buf_head + TCPC_RECEIVE_BUFFER_FRAME_TYPE_OFFSET;

		if (rx_buf_head) {
			if (unlikely(cnt < rx_buf[cnt_idx] ||
				     frame_type !=
				     rx_buf[frame_type_idx])) {
				logbuffer_log(chip->log,
					      "Mismatched RX buffer metadata. Previously, cnt=%u, frame_type=%u. Now, cnt=%u, frame_type=%u.",
					      cnt, frame_type, rx_buf[cnt_idx],
					      rx_buf[frame_type_idx]);
				return -EIO;
			}
			memcpy(rx_buf + rx_buf_head, cache, TCPC_RECEIVE_BUFFER_METADATA_SIZE);
		} else {
			/*
			 * Save metadata obtained from first SPMI read. These values will be used to
			 * compare with metadata from subsequent reads for correctness.
			 */
			cnt = rx_buf[cnt_idx];
			frame_type = rx_buf[frame_type_idx];
		}

		pending_pd_msg_size -= read_size;
		rx_buf_head += read_size;
	}
	return 0;
}

static int max777x9_spmi_tx(struct google_shim_tcpci *tcpci, enum tcpm_transmit_type type,
			    const struct pd_message *msg, unsigned int negotiated_rev)
{
	u8 tx_buf[TCPC_TRANSMIT_BUFFER_MAX_LEN] = {0,};
	struct max77759_plat *chip = tdata_to_max77759(tcpci->data);
	unsigned int reg;
	u16 header = msg ? le16_to_cpu(msg->header) : 0;

	/*
	 * Reserve TX_BUF_REG_COUNT_SIZE space at the beginning of tx_buf to store
	 * count.
	 */
	int pending_pd_msg_size = 0, tx_buf_head = TX_BUF_REG_COUNT_SIZE;

	int msg_payload_size, send_size;
	int ret;

	msg_payload_size = msg ? pd_header_cnt(header) * 4 : 0;

	if (!tcpci->data->TX_BUF_BYTE_x_hidden) {
		logbuffer_logk(chip->log, LOGLEVEL_ERR, "TX_BUF_BYTE_x_hidden should be set");
		return -EINVAL;
	}

	if (msg) {
		memcpy(&tx_buf[pending_pd_msg_size + TX_BUF_REG_COUNT_SIZE], &msg->header,
		       sizeof(msg->header));
		pending_pd_msg_size += sizeof(msg->header);
		memcpy(&tx_buf[pending_pd_msg_size + TX_BUF_REG_COUNT_SIZE], msg->payload,
		       msg_payload_size);
		pending_pd_msg_size += msg_payload_size;
	}

	while (pending_pd_msg_size > 0) {
		send_size = min_t(size_t, pending_pd_msg_size, MAX_PD_MSG_SIZE_PER_WRITE);
		tx_buf[tx_buf_head - TX_BUF_REG_COUNT_SIZE] = send_size;
		ret = regmap_raw_write(tcpci->regmap, TCPC_TX_BYTE_CNT,
				       tx_buf + tx_buf_head - TX_BUF_REG_COUNT_SIZE,
				       send_size + TX_BUF_REG_COUNT_SIZE);
		if (ret < 0) {
			logbuffer_logk(chip->log, LOGLEVEL_ERR,
				       "Regmap write failed with error %d", ret);
			return ret;
		}
		tx_buf_head += send_size;
		pending_pd_msg_size -= send_size;
	}

	/* nRetryCount is 3 in PD2.0 spec where 2 in PD3.0 spec */
	reg = ((negotiated_rev > PD_REV20 ? PD_RETRY_COUNT_3_0_OR_HIGHER : PD_RETRY_COUNT_DEFAULT)
	       << TCPC_TRANSMIT_RETRY_SHIFT) | (type << TCPC_TRANSMIT_TYPE_SHIFT);
	ret = regmap_write(tcpci->regmap, TCPC_TRANSMIT, reg);

	return ret;
}

static int max777x9_spmi_probe(struct spmi_device *sdev)
{
	int ret;
	struct regmap *regmap;
	struct max777x9_desc desc = {};

	mutex_init(&g_spmi_regmap_lock);
	sdev->dev.init_name = "spmi-max77759tcpc";
	regmap = devm_regmap_init_spmi_ext(sdev, &max77779_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&sdev->dev, "Regmap init failed");
		return PTR_ERR(regmap);
	}

	desc.dev = &sdev->dev;
	desc.rmap = regmap;
	desc.client = (void *)sdev;
	desc.tx = max777x9_spmi_tx;
	desc.rx = max777x9_spmi_rx;

	ret = max77759_register(&desc, max777x9_spmi_direct_otg_en);
	if (ret)
		return ret;

	if (!desc.plat) {
		dev_err(&sdev->dev, "Expected non-NULL desc.plat value");
		return -EINVAL;
	}

	ret = max777x9_init_alert(desc.plat);
	if (ret < 0) {
		dev_err(&sdev->dev, "init alert failed");
		max77759_unregister(desc.plat);
		return ret;
	}

	device_init_wakeup(&sdev->dev, true);
	return ret;
}

static void max777x9_spmi_remove(struct spmi_device *sdev)
{
	struct max77759_plat *chip = spmi_device_get_drvdata(sdev);

	max77759_unregister(chip);
}

static void max777x9_spmi_shutdown(struct spmi_device *sdev)
{
	struct max77759_plat *chip = spmi_device_get_drvdata(sdev);

	max77759_shutdown(chip);
}

#ifdef CONFIG_OF
static const struct of_device_id max777x9_of_match[] = {
	{ .compatible = "max77759tcpc-spmi", },
	{},
};
MODULE_DEVICE_TABLE(of, max777x9_of_match);
#endif

static struct spmi_driver max777x9_spmi_driver = {
	.driver = {
		.name = "max77759tcpc-spmi",
		.of_match_table = of_match_ptr(max777x9_of_match),
	},
	.probe = max777x9_spmi_probe,
	.remove = max777x9_spmi_remove,
	.shutdown = max777x9_spmi_shutdown,
};

module_spmi_driver(max777x9_spmi_driver);

MODULE_DESCRIPTION("MAX777x9 TCPCI SPMI Driver");
MODULE_LICENSE("GPL");
