/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024 Google LLC */
#ifndef _DA9188_MFD_H
#define _DA9188_MFD_H

#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/types.h>

#include <soc/google/goog_mba_cpm_iface.h>

/* Mailbox data struct. */
struct mailbox_data {
	u32 data[2];
};

struct pmic_mfd_mbox {
	struct cpm_iface_client *client;
};

struct cpm_msg {
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
};

/* MFD shared pmic mailbox API functions */
int da9188_mfd_mbox_request(struct device *dev,
			    struct pmic_mfd_mbox *mbox);
void da9188_mfd_mbox_release(struct pmic_mfd_mbox *mbox);

int da9188_mfd_mbox_send_req_blocking_read(struct device *dev,
					   struct pmic_mfd_mbox *mbox,
					   u8 mbox_dst, u8 target, u8 cmd,
					   u16 id_or_addr,
					   struct mailbox_data req_data,
					   struct mailbox_data *resp_data);

int da9188_mfd_mbox_send_req_blocking(struct device *dev,
				      struct pmic_mfd_mbox *mbox,
				      u8 mbox_dst, u8 target, u8 cmd,
				      u16 id_or_addr,
				      struct mailbox_data req_data);

#endif /* _DA9188_MFD_H */
