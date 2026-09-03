// SPDX-License-Identifier: GPL-2.0-only

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "include/uwb.h"
#include "include/uwb_coredump.h"

#define NAME "uwb"

#define UCI_PAYLOAD_OFFSET 4

#define OID_VENDOR_DEV_CRASH (0b111110)

#define CRASH_DUMP_SIZE (4 * 1024)
#define SEG_COUNT 1

#define UWB_COREDUMP_TIMEOUT 3
#define BOOT_FAIL_BUF_SIZE 40

static void u100_release_coredump(struct device *dev)
{
}

int u100_register_coredump(struct u100_ctx *u100_ctx)
{
	int ret;
	struct spi_device *spi;
	struct sscd_desc *sscd;
	struct uwb_coredump *coredump = u100_ctx->coredump;

	spi = u100_ctx->spi;
	sscd = devm_kzalloc(&spi->dev, struct_size(sscd, segs, SEG_COUNT), GFP_KERNEL);
	if (!sscd)
		return -ENOMEM;

	sscd->segs[0].addr = devm_kzalloc(&spi->dev, CRASH_DUMP_SIZE, GFP_KERNEL);
	if (!sscd->segs[0].addr)
		return -ENOMEM;

	sscd->sscd_dev.name = NAME;
	sscd->sscd_dev.driver_override = SSCD_NAME;
	sscd->sscd_dev.id = -1;
	sscd->sscd_dev.dev.platform_data = &sscd->sscd_pdata;
	sscd->sscd_dev.dev.release = u100_release_coredump;

	sscd->seg_count = SEG_COUNT;
	sscd->name = NAME;

	ret = platform_device_register(&sscd->sscd_dev);
	if (ret) {
		devm_kfree(&spi->dev, sscd);
		return ret;
	}

	if (of_property_read_bool(u100_ctx->spi->dev.of_node, "ignore-boot-error")) {
		coredump->ignore_boot_error = true;
		UWB_INFO("don't report ssr boot error: %d\n", coredump->ignore_boot_error);
	}

	coredump->sscd = sscd;
	return 0;
}

void u100_unregister_coredump(struct u100_ctx *u100_ctx)
{
	platform_device_unregister(&u100_ctx->coredump->sscd->sscd_dev);
	devm_kfree(&u100_ctx->spi->dev, u100_ctx->coredump->sscd);
}

/*
 * There are two sets of messages per coredump.  Each message set consists of a
 * notification header and ends with pbf (Packet Boundary Flag) = 0.  There
 * could be multiple messages for each message set but each message set ends
 * with pbf = 0. So at minimum, there has to be two messages with pbf = 0.  The
 * max number of messages is not defined.
 *
 * This function will store two complete message sets and report the coredump
 * to sscd. Incomplete coredumps will be discarded. Coredumps are expected
 * to finish in 3 seconds.
 */
int u100_process_coredump(struct u100_ctx *u100_ctx, struct sk_buff *skb)
{
	struct uci_msg_hdr *hdr = (struct uci_msg_hdr *)skb->data;
	struct uwb_coredump *coredump = u100_ctx->coredump;
	struct sscd_desc *sscd = coredump->sscd;
	struct sscd_platform_data *sscd_pdata = &sscd->sscd_pdata;
	int index = sscd->segs[0].size;
	time64_t current_time = ktime_get_boottime_seconds();

	if (!sscd_pdata || !sscd_pdata->sscd_report)
		return -EINVAL;

	if ((current_time - coredump->time) > UWB_COREDUMP_TIMEOUT) {
		coredump->time = current_time;
		sscd->segs[0].size = 0;
		coredump->state = CRASH_NTF_0;
	}

	if (index + hdr->len <= CRASH_DUMP_SIZE) {
		memcpy(&sscd->segs[0].addr[index], &skb->data[UCI_PAYLOAD_OFFSET],
			hdr->len);
		sscd->segs[0].size += hdr->len;
	}

	if (hdr->pbf == 0) {
		if (coredump->state == CRASH_NTF_1) {
			sscd_pdata->sscd_report(&coredump->sscd->sscd_dev, sscd->segs,
				sscd->seg_count, 0, "u100 coredump");
			coredump->time = 0;
			if (!coredump->disable_reset) {
				UWB_DEBUG("coredump reset\n");
				uwbs_reset(u100_ctx);
			}
		}
		coredump->state++;
	}

	return 0;
}

bool is_coredump(struct u100_ctx *u100_ctx, struct sk_buff *skb)
{
	struct uci_msg_hdr *hdr = (struct uci_msg_hdr *)skb->data;

	return ((hdr->mt == UCI_MT_NTF) &&
		(hdr->gid == GID_VENDOR_CONFIG && hdr->oid == OID_VENDOR_DEV_CRASH));
}

void u100_report_coredump_on_poweron(struct u100_ctx *u100_ctx, const char *msg, int errcode)
{
	int err;

	if (u100_ctx->coredump && u100_ctx->coredump->sscd &&
		!u100_ctx->coredump->ignore_boot_error) {
		char buf[BOOT_FAIL_BUF_SIZE];
		struct uwb_coredump *coredump = u100_ctx->coredump;
		struct sscd_platform_data *sscd_pdata = &coredump->sscd->sscd_pdata;

		if (u100_ctx->gpio_u100_power && sscd_pdata && sscd_pdata->sscd_report) {
			UWB_WARN("failure: %s\n", msg);
			scnprintf(buf, BOOT_FAIL_BUF_SIZE, "u100 power on err: %s(%d)",
				  msg, errcode);
			err = sscd_pdata->sscd_report(&coredump->sscd->sscd_dev,
					coredump->sscd->segs, 0, 0, (const char *)buf);
			if (err)
				UWB_WARN("sscd report error %d", err);
		}
	}
}
