// SPDX-License-Identifier: GPL-2.0-only
/*
 * DVFS Helper
 * Copyright (C) 2023 Google LLC.
 */

#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/string.h>

#include "dvfs_helper.h"
#include <dvfs-helper/google_dvfs_helper.h>
#include <soc/google/goog_mba_cpm_iface.h>
#include <soc/google/goog_cpm_service_ids.h>

#define MAILBOX_SEND_TIMEOUT_MS (3000)
#define MAILBOX_RECEIVE_TIMEOUT_MS (3000)

#define MAINCMD_DVFS_GET (3) /* For pwrblk service */
#define PWRBLK_SERVICE_ID (0xE)

#define MAIN_CMD_MASK GENMASK(7, 0)
#define DVFS_REQ_ID_MASK GENMASK(15, 8)
#define PWRBLK_ID_MASK GENMASK(23, 16)
#define OPP_INDEX_MASK GENMASK(31, 24)

/* These max values are just for safety and should never be reached in practice */
#define MAX_DOMAINS (48)
#define MAX_OPP_LEVELS (32)

#define DVFS_INFO_REV_ID (1) /* TODO(b/303146305): Figure out the rev ID */

enum pwrblk_dvfs_req_id {
	PWRBLK_DVFS_NUM_DOMAINS_GET_REQ = 0,
	PWRBLK_DVFS_DOMAIN_INFO_GET_REQ,
	PWRBLK_DVFS_OPP_GET_REQ,
};

struct pwrblk_dvfs_req {
	u32 rsvd0 : 8;
	u32 req_id : 8; /* DVFS Request ID */
	u32 pwrblk_id : 8; /* PWRBLK ID as defined in pwrblk.h */
	u32 index : 8; /* Index to keep track of V/F pairs */
	u64 rsvd1;
};

struct pwrblk_dvfs_num_domains_resp {
	u32 num_domains : 8;
	u32 rsvd0 : 24;
	u64 rsvd1;
};

struct pwrblk_dvfs_domain_info_resp {
	u32 num_levels : 8;
	u32 rsvd : 24;
	char domain_name[8];
};

struct pwrblk_dvfs_opp_resp {
	u32 rsvd0;
	u32 freq1 : 14;
	u32 supported1 : 1;
	u32 rsvd1 : 1;
	u32 voltage1 : 14;
	u32 sw_override1 : 1;
	u32 rsvd2 : 1;
	u32 freq2 : 14;
	u32 supported2 : 1;
	u32 rsvd3 : 1;
	u32 voltage2 : 14;
	u32 sw_override2 : 1;
	u32 rsvd4 : 1;
};

struct cpm_mbox {
	struct device *dev;
	struct cpm_iface_client *client;
};

struct dvfs_helper {
	struct device *dev;
	struct cpm_mbox mbox;
	struct dvfs_info dvfs_info;
};

static const struct dvfs_info *dvfs_info_g;

bool dvfs_helper_ready(void)
{
	return dvfs_info_g != NULL;
}
EXPORT_SYMBOL_GPL(dvfs_helper_ready);

struct dvfs_domain_info *dvfs_helper_get_domain_from_id(u16 domain_idx)
{
	if (domain_idx > dvfs_info_g->num_domains - 1)
		return NULL;

	return &dvfs_info_g->domains[domain_idx];
}
EXPORT_SYMBOL_GPL(dvfs_helper_get_domain_from_id);

struct dvfs_domain_info *dvfs_helper_get_domain(const char *name)
{
	if (!name)
		return NULL;

	for (int i = 0; i < dvfs_info_g->num_domains; ++i) {
		if (strncmp(name, dvfs_info_g->domains[i].name, MAX_DVFS_NAME_LEN) == 0)
			return &dvfs_info_g->domains[i];
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(dvfs_helper_get_domain);

const char *dvfs_helper_domain_id_to_name(u16 domain_idx)
{
	if (domain_idx > dvfs_info_g->num_domains - 1)
		return NULL;

	return dvfs_info_g->domains[domain_idx].name;
}
EXPORT_SYMBOL_GPL(dvfs_helper_domain_id_to_name);

/* Find the lowest OPP that is >= Hz */
s64 dvfs_helper_round_rate(struct dvfs_domain_info const * const di, const u64 Hz)
{
	struct dvfs_opp_info *opp;

	if (!di)
		return -EINVAL;

	for (int i = di->num_levels - 1; i >= 0; i--) {
		opp = &di->table[i];
		if (opp->supported && FREQ_MHZ_TO_HZ(opp->freq) >= Hz)
			return FREQ_MHZ_TO_HZ(opp->freq);
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(dvfs_helper_round_rate);

s64 dvfs_helper_lvl_to_freq_exact(struct dvfs_domain_info const * const di, const unsigned int lvl)
{
	struct dvfs_opp_info *opp;

	if (!di)
		return -EINVAL;

	if (lvl > di->num_levels - 1)
		return -ENOENT;

	opp = &di->table[lvl];

	if (opp->supported)
		return FREQ_MHZ_TO_HZ(opp->freq);

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(dvfs_helper_lvl_to_freq_exact);

int dvfs_helper_freq_to_lvl_exact(struct dvfs_domain_info const * const di, const u64 Hz)
{
	struct dvfs_opp_info *opp;

	if (!di)
		return -EINVAL;

	for (int i = 0; i < di->num_levels; ++i) {
		opp = &di->table[i];
		if (opp->supported && FREQ_MHZ_TO_HZ(opp->freq) == Hz)
			return opp->level;
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(dvfs_helper_freq_to_lvl_exact);

s64 dvfs_helper_lvl_to_freq_ceil(struct dvfs_domain_info const * const di, const unsigned int lvl)
{
	struct dvfs_opp_info *opp;

	if (!di)
		return -EINVAL;

	/* Tables are in descending order */
	for (int i = di->num_levels - 1; i >= 0; --i) {
		opp = &di->table[i];
		if (opp->supported && opp->level <= lvl)
			return FREQ_MHZ_TO_HZ(opp->freq);
	}

	/* Return max supported freq */
	opp = &di->table[di->max_supported_level];
	return FREQ_MHZ_TO_HZ(opp->freq);
}
EXPORT_SYMBOL_GPL(dvfs_helper_lvl_to_freq_ceil);

int dvfs_helper_freq_to_lvl_ceil(struct dvfs_domain_info const * const di, const u64 Hz)
{
	struct dvfs_opp_info *opp;

	if (!di)
		return -EINVAL;

	/* Tables are in descending order */
	for (int i = di->num_levels - 1; i >= 0; --i) {
		opp = &di->table[i];
		if (opp->supported && FREQ_MHZ_TO_HZ(opp->freq) >= Hz)
			return opp->level;
	}

	/* Return max supported level */
	return di->max_supported_level;
}
EXPORT_SYMBOL_GPL(dvfs_helper_freq_to_lvl_ceil);

s64 dvfs_helper_lvl_to_freq_floor(struct dvfs_domain_info const * const di, const unsigned int lvl)
{
	struct dvfs_opp_info *opp;

	if (!di)
		return -EINVAL;

	/* Tables are in descending order */
	for (int i = 0; i < di->num_levels; ++i) {
		opp = &di->table[i];
		if (opp->supported && opp->level >= lvl)
			return FREQ_MHZ_TO_HZ(opp->freq);
	}

	/* Return min supported freq */
	opp = &di->table[di->min_supported_level];
	return FREQ_MHZ_TO_HZ(opp->freq);
}
EXPORT_SYMBOL_GPL(dvfs_helper_lvl_to_freq_floor);

int dvfs_helper_freq_to_lvl_floor(struct dvfs_domain_info const * const di, const u64 Hz)
{
	struct dvfs_opp_info *opp;

	if (!di)
		return -EINVAL;

	/* Tables are in descending order */
	for (int i = 0; i < di->num_levels; ++i) {
		opp = &di->table[i];
		if (opp->supported && FREQ_MHZ_TO_HZ(opp->freq) <= Hz)
			return opp->level;
	}

	/* Return min supported level */
	return di->min_supported_level;
}
EXPORT_SYMBOL_GPL(dvfs_helper_freq_to_lvl_floor);

s64 dvfs_helper_get_domain_opp_frequency_mhz(u16 domain_idx, const unsigned int lvl)
{
	if (domain_idx > dvfs_info_g->num_domains - 1)
		return -EINVAL;

	s64 freq_hz = dvfs_helper_lvl_to_freq_exact(
		&dvfs_info_g->domains[domain_idx], lvl);
	if (freq_hz < 0)
		return freq_hz;

	return FREQ_HZ_TO_MHZ(freq_hz);
}
EXPORT_SYMBOL_GPL(dvfs_helper_get_domain_opp_frequency_mhz);

int dvfs_helper_get_domain_info(struct device * const dev,
				struct device_node * const node,
				struct dvfs_domain_info **domain_info)
{
	const char *dvfs_name = NULL;
	int ret;

	if (!dev || !node)
		return -ENODEV;

	if (!domain_info)
		return -EINVAL;

	if (!dvfs_info_g)
		return -EPROBE_DEFER;

	ret = of_property_read_string(node, "dvfs-helper-domain-name", &dvfs_name);

	if (ret) {
		dev_err(dev, "Could not find domain name in dts");
		return ret;
	}

	dev_dbg(dev, "domain_name: %s", dvfs_name);
	*domain_info = dvfs_helper_get_domain(dvfs_name);

	if (!*domain_info) {
		dev_err(dev, "Error getting domain_info");
		return -ENOENT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dvfs_helper_get_domain_info);

/* This function requires the domain_info to be valid */
static int dvfs_helper_add_opps_to_device_by_domain(struct device * const dev,
				struct dvfs_domain_info *domain_info)
{
	struct dvfs_opp_info *opp_info;
	struct dev_pm_opp *opp;
	struct dev_pm_opp_data opp_data;
	u64 freq_hz;
	u32 u_volt;
	int ret;

	dev_dbg(dev, "num_levels: %u", domain_info->num_levels);
	for (int i = 0; i < domain_info->num_levels; ++i) {
		opp_info = &domain_info->table[i];
		if (!(opp_info->supported || opp_info->sw_override)) {
			dev_info(dev, "Skipping OPP: Freq = %uMHz, Volt = %umV",
				 opp_info->freq, opp_info->voltage);
			continue;
		}

		/* Convert freq to Hz and voltage to uV */
		freq_hz = FREQ_MHZ_TO_HZ(opp_info->freq);
		u_volt = VOLT_MV_TO_UV(opp_info->voltage);

		dev_dbg(dev, "Adding OPP: Freq = %uMHz, Volt = %umV",
			 opp_info->freq, opp_info->voltage);
		opp_data.freq = freq_hz;
		opp_data.u_volt = u_volt;
		opp_data.level = i;

		ret = dev_pm_opp_add_dynamic(dev, &opp_data);

		if (ret) {
			dev_err(dev, "Failed to add OPP: Freq = %lluHz, Volt = %uuV",
				freq_hz, u_volt);
			dev_pm_opp_remove_all_dynamic(dev);
			return ret;
		}

		/*
		 * If the OPP is 'not supported' but can be 'sw overridden', the OPP should be
		 * disabled. It can be enabled later if needed, for example, for testings purposes.
		 */
		if (!opp_info->supported && opp_info->sw_override) {
			opp = dev_pm_opp_find_freq_exact(dev, freq_hz, true);
			dev_pm_opp_put(opp);

			if (IS_ERR(opp)) {
				dev_err(dev, "OPP error: %ld", PTR_ERR(opp));
				return PTR_ERR(opp);
			}

			ret = dev_pm_opp_disable(dev, freq_hz);
			if (ret) {
				dev_err(dev, "OPP disable error: %d", ret);
				return ret;
			}

			dev_info(dev, "Disabled OPP %lluHz %uuV", freq_hz, u_volt);
		}
	}
	return 0;
}

int dvfs_helper_add_opps_to_device_by_name(struct device * const dev, const char *name)
{
	struct dvfs_domain_info *domain_info;

	if (!dvfs_info_g)
		return -EPROBE_DEFER;

	if (!dev)
		return -ENODEV;

	domain_info = dvfs_helper_get_domain(name);
	if (!domain_info) {
		dev_err(dev, "Error getting domain_info");
		return -ENOENT;
	}

	return dvfs_helper_add_opps_to_device_by_domain(dev, domain_info);
}
EXPORT_SYMBOL_GPL(dvfs_helper_add_opps_to_device_by_name);

int dvfs_helper_add_opps_to_device(struct device * const dev, struct device_node * const node)
{
	struct dvfs_domain_info *domain_info;
	int ret;

	ret = dvfs_helper_get_domain_info(dev, node, &domain_info);
	if (ret)
		return ret;

	return dvfs_helper_add_opps_to_device_by_domain(dev, domain_info);
}
EXPORT_SYMBOL_GPL(dvfs_helper_add_opps_to_device);

static int dvfs_helper_cpm_mailbox_init(struct device * const dev,
					struct cpm_mbox * const mbox)
{
	int ret = 0;

	mbox->client = cpm_iface_request_client(dev, 0, NULL, NULL);
	if (IS_ERR(mbox->client)) {
		ret = PTR_ERR(mbox->client);
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request cpm mailbox client. Err: %d\n", ret);
		return ret;
	}

	return ret;
}

static void dvfs_helper_cpm_mailbox_exit(struct dvfs_helper * const dvfs_helper)
{
	cpm_iface_free_client(dvfs_helper->mbox.client);
}

static int dvfs_helper_send_mbox_msg(struct dvfs_helper * const dvfs_helper,
				     struct cpm_iface_payload *req_msg,
				     struct cpm_iface_payload * const resp_msg)
{
	struct device *dev = dvfs_helper->dev;
	struct cpm_iface_req cpm_req = {0};
	int ret = 0;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = req_msg;
	cpm_req.resp_msg = resp_msg;
	cpm_req.tout_ms = MAILBOX_SEND_TIMEOUT_MS;
	cpm_req.dst_id = CPM_COMMON_PWRBLK_SERVICE;

	dev_dbg(dvfs_helper->dev,
		"Send mbox msg: [%08X]\n", req_msg->payload[0]);

	ret = cpm_send_message(dvfs_helper->mbox.client, &cpm_req);
	if (ret < 0) {
		dev_err(dev, "Failed to send request to CPM, payload=%08X, ret=%d\n",
						req_msg->payload[0], ret);
		return ret;
	}

	dev_dbg(dvfs_helper->dev, "Resp: [%08X][%08X][%08X]\n", resp_msg->payload[0],
					resp_msg->payload[1], resp_msg->payload[2]);

	return ret;
}

static int dvfs_helper_dvfs_num_domains_req(struct dvfs_helper * const dvfs_helper,
					    struct cpm_iface_payload * const resp_msg)
{
	struct cpm_iface_payload req_msg = { 0 };
	int num_domains = 0;

	req_msg.payload[0] = FIELD_PREP(MAIN_CMD_MASK, MAINCMD_DVFS_GET) |
			  FIELD_PREP(DVFS_REQ_ID_MASK, PWRBLK_DVFS_NUM_DOMAINS_GET_REQ);

	num_domains = dvfs_helper_send_mbox_msg(dvfs_helper, &req_msg, resp_msg);

	return num_domains;
}

static int dvfs_helper_dvfs_domain_info_req(struct dvfs_helper * const dvfs_helper,
					    const u8 pwrblk_id,
					    struct cpm_iface_payload * const resp_msg)
{
	struct cpm_iface_payload req_msg = { 0 };

	req_msg.payload[0] = FIELD_PREP(MAIN_CMD_MASK, MAINCMD_DVFS_GET) |
			  FIELD_PREP(DVFS_REQ_ID_MASK, PWRBLK_DVFS_DOMAIN_INFO_GET_REQ) |
			  FIELD_PREP(PWRBLK_ID_MASK, pwrblk_id);

	return dvfs_helper_send_mbox_msg(dvfs_helper, &req_msg, resp_msg);
}

static int dvfs_helper_dvfs_opp_req(struct dvfs_helper * const dvfs_helper,
				    const u8 pwrblk_id,
				    const u8 index,
				    struct cpm_iface_payload * const resp_msg)
{
	struct cpm_iface_payload req_msg = { 0 };

	req_msg.payload[0] = FIELD_PREP(MAIN_CMD_MASK, MAINCMD_DVFS_GET) |
			     FIELD_PREP(DVFS_REQ_ID_MASK, PWRBLK_DVFS_OPP_GET_REQ) |
			     FIELD_PREP(PWRBLK_ID_MASK, pwrblk_id) |
			     FIELD_PREP(OPP_INDEX_MASK, index);

	return dvfs_helper_send_mbox_msg(dvfs_helper, &req_msg, resp_msg);
}

static int dvfs_helper_read_dvfs_domain_levels(struct dvfs_helper * const dvfs_helper,
					       u16 domain_idx)
{
	struct dvfs_domain_info * const domains = dvfs_helper->dvfs_info.domains;
	struct pwrblk_dvfs_opp_resp *resp;
	struct cpm_iface_payload received_payload;

	size_t table_size = domains[domain_idx].num_levels * sizeof(struct dvfs_opp_info);
	u16 j;
	int ret = 0;

	domains[domain_idx].table = devm_kzalloc(dvfs_helper->dev, table_size, GFP_KERNEL);

	if (!domains[domain_idx].table)
		return -ENOMEM;

	for (j = 0; j < domains[domain_idx].num_levels; j += 2) {
		ret = dvfs_helper_dvfs_opp_req(dvfs_helper, domains[domain_idx].pwrblk_id, j,
					       &received_payload);
		if (ret < 0)
			return ret;

		resp = (struct pwrblk_dvfs_opp_resp *)received_payload.payload;

		domains[domain_idx].table[j].freq = resp->freq1;
		domains[domain_idx].table[j].supported = resp->supported1;
		domains[domain_idx].table[j].level = j;
		domains[domain_idx].table[j].voltage = resp->voltage1;
		domains[domain_idx].table[j].sw_override = resp->sw_override1;

		if ((j + 1) < domains[domain_idx].num_levels) {
			domains[domain_idx].table[j + 1].freq = resp->freq2;
			domains[domain_idx].table[j + 1].supported = resp->supported2;
			domains[domain_idx].table[j + 1].level = j + 1;
			domains[domain_idx].table[j + 1].voltage = resp->voltage2;
			domains[domain_idx].table[j + 1].sw_override = resp->sw_override2;
		}
	}

	return 0;
}

static int dvfs_helper_read_dvfs_info(struct dvfs_helper * const dvfs_helper)
{
	struct dvfs_info * const dvfs_info = &dvfs_helper->dvfs_info;
	struct pwrblk_dvfs_domain_info_resp *resp;
	struct cpm_iface_payload received_payload;

	u8 i;
	int ret;

	for (i = 0; i < dvfs_info->num_domains; ++i) {
		ret = dvfs_helper_dvfs_domain_info_req(dvfs_helper, i, &received_payload);
		if (ret < 0)
			return ret;

		resp = (struct pwrblk_dvfs_domain_info_resp *)received_payload.payload;

		dvfs_info->domains[i].pwrblk_id = i;

		strscpy(dvfs_info->domains[i].name, resp->domain_name,
			sizeof(dvfs_info->domains[i].name));
		dev_dbg(dvfs_helper->dev, "[%u]dvfs_name: %s", i, dvfs_info->domains[i].name);

		if (resp->num_levels > MAX_OPP_LEVELS)
			resp->num_levels = MAX_OPP_LEVELS;

		dvfs_info->domains[i].num_levels = resp->num_levels;

		ret = dvfs_helper_read_dvfs_domain_levels(dvfs_helper, i);

		if (ret < 0)
			return ret;
	}

	return ret;
}

static int dvfs_helper_read_dvfs_tables(struct dvfs_helper * const dvfs_helper)
{
	struct pwrblk_dvfs_num_domains_resp *resp;
	struct cpm_iface_payload received_payload;
	int ret = 0;

	ret = dvfs_helper_dvfs_num_domains_req(dvfs_helper, &received_payload);
	if (ret < 0)
		return ret;

	resp = (struct pwrblk_dvfs_num_domains_resp *)received_payload.payload;

	if (resp->num_domains > MAX_DOMAINS)
		resp->num_domains = MAX_DOMAINS;

	dvfs_helper->dvfs_info.rev_id = DVFS_INFO_REV_ID;
	dvfs_helper->dvfs_info.num_domains = resp->num_domains;
	dvfs_helper->dvfs_info.domains = devm_kzalloc(dvfs_helper->dev,
						      dvfs_helper->dvfs_info.num_domains *
						      sizeof(struct dvfs_domain_info), GFP_KERNEL);

	if (!dvfs_helper->dvfs_info.domains)
		return -ENOMEM;

	ret = dvfs_helper_read_dvfs_info(dvfs_helper);

	return ret;
}

static int dvfs_helper_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dvfs_helper *dvfs_helper;
	int ret = 0;

	dvfs_helper = devm_kzalloc(dev, sizeof(*dvfs_helper), GFP_KERNEL);
	if (!dvfs_helper)
		return -ENOMEM;

	dvfs_helper->dev = dev;

	ret = dvfs_helper_cpm_mailbox_init(dev, &dvfs_helper->mbox);

	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, dvfs_helper);

	/* Read all the DVFS tables from CPM */
	ret = dvfs_helper_read_dvfs_tables(dvfs_helper);
	dvfs_helper_cpm_mailbox_exit(dvfs_helper);

	if (!ret)
		dvfs_info_g = &dvfs_helper->dvfs_info;

	return ret;
};

static const struct of_device_id dvfs_helper_of_match_table[] = {
	{ .compatible = "google,dvfs-helper" },
	{},
};
MODULE_DEVICE_TABLE(of, dvfs_helper_of_match_table);

static struct platform_driver dvfs_helper_driver = {
	.probe = dvfs_helper_probe,
	.driver = {
		.name = "dvfs_helper",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dvfs_helper_of_match_table),
	},
};

module_platform_driver(dvfs_helper_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google DVFS Helper");
MODULE_LICENSE("GPL");
