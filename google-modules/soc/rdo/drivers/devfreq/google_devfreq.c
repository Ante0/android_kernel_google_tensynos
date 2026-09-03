// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/mailbox_client.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/units.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/google_vote_manager.h>
#include <trace/events/power.h>
#include <dt-bindings/lpcm/pf_state.h>
#include <dvfs-helper/google_dvfs_helper.h>
#include <mailbox/protocols/mba/cpm/common/service_ids.h>

// TODO(b/322127453): Needs to replace the file with the proper cpm protocol information.
#include "google_cpm_mipm_service.h"
#include <soc/google/goog_mba_cpm_iface.h>

#define MAILBOX_SEND_TIMEOUT_MS 10000
#define MAILBOX_RECEIVE_TIMEOUT_MS 10000
#define MAILBOX_RETRY_COUNT 5
#define MAILBOX_RETRY_USLEEP_MIN 50
#define MAILBOX_RETRY_USLEEP_MAX 100

#define MIPM_TUNABLE_PF_IDX_FIELD (MIPM_TUNABLE_PF_IDX_MASK << MIPM_TUNABLE_PF_IDX_SHIFT)
#define MIPM_DEV_INX_FIELD (MIPM_TUNABLE_DEV_IDX_MASK << MIPM_TUNABLE_DEV_IDX_SHIFT)
#define MIPM_TUNABLE_ID_FIELD (MIPM_TUNABLE_ID_MASK << MIPM_TUNABLE_ID_SHIFT)

#define LPCM_GET_PF_STATE 3
#define LPCM_LPCM_ID_FIELD GENMASK(15, 8)
#define LPCM_REQUEST_ID_FIELD GENMASK(7, 0)

#define MAILBOX_RESP_NO_ERROR 0
#define MIPM_SERVICE_ERR_INVALID_ARGS 8
#define LPCM_SERVICE_ERR_ALREADY_STARTED (-6)
#define LPCM_SERVICE_ERR_NOT_SUPPORTED (-24)
#define LPCM_SERVICE_ERR_NOT_ALLOWED (-17)
#define LPCM_SERVICE_ERR_INVALID_ARGS (-8)
#define LPCM_SERVICE_ERR_TIMED_OUT (-13)
#define LPCM_SERVICE_ERR_GENERIC (-1)

#define DEVFREQ_TRACE_STR_LEN 32

/*
 * This enum should be aligned with the mipm_fabric_id of
 * cpm/google/dev/mipm/rdo/mipm_types.h
 */
enum mipm_fabric_id {
	MIPM_FABMED = 0,
	MIPM_FABSYSS,
	MIPM_FABRT,
	MIPM_FABSTBY,
	MIPM_FABHBW,
	MIPM_FABMEM,
	MIPM_NUM_FABRICS,
};

struct cpm_mipm_tunable_resp_data {
	u32 unused;
	u32 error_code;
} __packed;

struct cpm_lpcm_get_rate_resp_data {
	u32 error_code;
	u8 lpcm_id;
	u8 pf_state;
} __packed;

struct tunable_ids {
	u32 min_pf_level;
	u32 max_pf_level;
};

struct google_devfreq_mbox {
	struct cpm_iface_client *client;
	struct tunable_ids tunable_ids;
	enum mipm_fabric_id fabric_id;
	u32 lpcm_id;
};

struct google_devfreq {
	struct device *dev;

	struct devfreq *devfreq;
	struct devfreq_dev_profile *profile;
	struct notifier_block min_freq_nb;
	struct notifier_block max_freq_nb;

	struct google_devfreq_mbox cpm_mbox;

	char min_clamp_trace_name[DEVFREQ_TRACE_STR_LEN];
	char max_clamp_trace_name[DEVFREQ_TRACE_STR_LEN];
};

static int
google_devfreq_send_pf_level_clamp_req(struct google_devfreq *df, u32 pf_level, u32 tunable_id,
				       enum mipm_fabric_id fabric_id)
{
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req cpm_req;
	struct cpm_mipm_tunable_resp_data *response =
		(struct cpm_mipm_tunable_resp_data *)resp_msg.payload;
	int err;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MAILBOX_RECEIVE_TIMEOUT_MS;
	cpm_req.dst_id = CPM_COMMON_MIPM_SERVICE;

	req_msg.payload[0] = MIPM_SET_TUNABLE;
	req_msg.payload[1] = FIELD_PREP(MIPM_DEV_INX_FIELD, fabric_id) |
			     FIELD_PREP(MIPM_TUNABLE_ID_FIELD, tunable_id);
	req_msg.payload[2] = pf_level;

	err = cpm_send_message(df->cpm_mbox.client, &cpm_req);
	if (err) {
		dev_err(df->dev, "failed to send cpm mba message, err: %d\n", err);
		return err;
	}

	if (response->error_code == MIPM_SERVICE_ERR_INVALID_ARGS) {
		dev_err(df->dev,
			"mba response err: tunable ID, device index, or pf index is invalid.\n");
		return -EINVAL;
	}
	return 0;
}

static int google_devfreq_update_min_freq_mba(struct notifier_block *nb, unsigned long event,
					      void *data)
{
	u32 pf_level;
	u32 flags = 0;
	s32 min_freq;
	unsigned long min_freq_hz;
	struct dev_pm_opp *opp;
	struct google_devfreq *df = container_of(nb, struct google_devfreq, min_freq_nb);

	min_freq = dev_pm_qos_read_value(df->dev, DEV_PM_QOS_MIN_FREQUENCY);
	if (min_freq < 0) {
		dev_err(df->dev, "failed to read minfreq, err: %d\n", min_freq);
		goto out;
	}

	min_freq_hz = (unsigned long)HZ_PER_KHZ * min_freq;
	flags &= ~DEVFREQ_FLAG_LEAST_UPPER_BOUND; /* Use GLB */
	opp = devfreq_recommended_opp(df->dev, &min_freq_hz, flags);
	if (IS_ERR(opp)) {
		dev_err(df->dev, "failed to find OPP. requested minfreq: %lu, err: %ld\n",
			min_freq_hz, PTR_ERR(opp));
		goto out;
	}
	pf_level = dev_pm_opp_get_level(opp);
	dev_pm_opp_put(opp);

	if (trace_clock_set_rate_enabled())
		trace_clock_set_rate(df->min_clamp_trace_name, min_freq_hz,
			raw_smp_processor_id());

	google_devfreq_send_pf_level_clamp_req(df, pf_level, df->cpm_mbox.tunable_ids.max_pf_level,
					       df->cpm_mbox.fabric_id);

out:
	return NOTIFY_OK;
}

static int google_devfreq_update_max_freq_mba(struct notifier_block *nb, unsigned long event,
					      void *data)
{
	u32 pf_level;
	u32 flags = 0;
	s32 max_freq;
	unsigned long max_freq_hz;
	struct dev_pm_opp *opp;
	struct google_devfreq *df = container_of(nb, struct google_devfreq, max_freq_nb);

	max_freq = dev_pm_qos_read_value(df->dev, DEV_PM_QOS_MAX_FREQUENCY);
	if (max_freq < 0) {
		dev_err(df->dev, "failed to read maxfreq, err: %d\n", max_freq);
		goto out;
	}

	max_freq_hz = (unsigned long)HZ_PER_KHZ * max_freq;
	flags |= DEVFREQ_FLAG_LEAST_UPPER_BOUND; /* Use LUB */
	opp = devfreq_recommended_opp(df->dev, &max_freq_hz, flags);
	if (IS_ERR(opp)) {
		dev_err(df->dev, "failed to find OPP. requested maxfreq: %lu, err: %ld\n",
			max_freq_hz, PTR_ERR(opp));
		goto out;
	}
	pf_level = dev_pm_opp_get_level(opp);
	dev_pm_opp_put(opp);

	if (trace_clock_set_rate_enabled())
		trace_clock_set_rate(df->max_clamp_trace_name, max_freq_hz,
			raw_smp_processor_id());

	google_devfreq_send_pf_level_clamp_req(df, pf_level, df->cpm_mbox.tunable_ids.min_pf_level,
					       df->cpm_mbox.fabric_id);

out:
	return NOTIFY_OK;
}

static inline int parse_lpcm_resp_err(struct device *dev, u32 error_code)
{
	int err = le32_to_cpu(error_code);

	switch (err) {
	case MAILBOX_RESP_NO_ERROR:
		return 0;
	case LPCM_SERVICE_ERR_ALREADY_STARTED:
		dev_err(dev, "An operation is already in progress on LPCM. Try again later\n");
		return -EAGAIN;
	case LPCM_SERVICE_ERR_NOT_SUPPORTED:
		dev_err(dev, "The received request is invalid\n");
		return -EINVAL;
	case LPCM_SERVICE_ERR_NOT_ALLOWED:
		dev_err(dev, "Attempt to modify LPCM state is not allowed\n");
		return -ESERVERFAULT;
	case LPCM_SERVICE_ERR_INVALID_ARGS:
		dev_err(dev, "Invalid request\n");
		return -EINVAL;
	case LPCM_SERVICE_ERR_TIMED_OUT:
		dev_err(dev, "Operation timed out\n");
		return -ETIMEDOUT;
	case LPCM_SERVICE_ERR_GENERIC:
		dev_err(dev, "Internal error occurred\n");
		return -ESERVERFAULT;
	default:
		dev_err(dev, "Unknown error response.\n");
		return -ESERVERFAULT;
	}
}

static int google_devfreq_lpcm_get_pf_level(struct google_devfreq *df, int *pf_level)
{
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req cpm_req;
	struct cpm_lpcm_get_rate_resp_data *response =
		(struct cpm_lpcm_get_rate_resp_data *)&resp_msg.payload;
	int err;
	int retry = 0;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MAILBOX_RECEIVE_TIMEOUT_MS;
	cpm_req.dst_id = CPM_COMMON_LPCM_SERVICE;

	req_msg.payload[0] = FIELD_PREP(LPCM_REQUEST_ID_FIELD, LPCM_GET_PF_STATE) |
			     FIELD_PREP(LPCM_LPCM_ID_FIELD, df->cpm_mbox.lpcm_id);

	while (retry < MAILBOX_RETRY_COUNT) {
		err = cpm_send_message(df->cpm_mbox.client, &cpm_req);
		if (err) {
			dev_err(df->dev, "failed to send cpm mba message, err: %d\n", err);
			return err;
		}

		err = parse_lpcm_resp_err(df->dev, response->error_code);
		if (err != -EAGAIN)
			break;

		retry++;
		usleep_range(MAILBOX_RETRY_USLEEP_MIN, MAILBOX_RETRY_USLEEP_MAX);
	}
	if (err)
		return err;

	*pf_level = response->pf_state;
	return 0;
}

static int google_devfreq_get_cur_freq_fabrics(struct device *dev, unsigned long *freq)
{
	struct google_devfreq *df = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	int pf_level;
	int err;

	err = google_devfreq_lpcm_get_pf_level(df, &pf_level);
	if (err)
		return err;

	opp = dev_pm_opp_find_level_exact(dev, pf_level);
	if (IS_ERR(opp)) {
		dev_err(dev, "failed to find OPP, pf-level %u, opp: %pe\n", pf_level, opp);
		return PTR_ERR(opp);
	}

	*freq = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);
	return 0;
}

static int google_devfreq_get_cur_freq(struct device *dev,
	unsigned long *freq)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct google_devfreq *df = platform_get_drvdata(pdev);
	int ret = 0;

	if (IS_ERR_OR_NULL(df->devfreq) || !df->devfreq->previous_freq)
		return google_devfreq_get_cur_freq_fabrics(dev, freq);

	*freq = df->devfreq->previous_freq;

	return ret;
}

static int google_devfreq_target_no_ops(struct device *dev, unsigned long *freq, u32 flags)
{
	/*
	 * The target callback doesn't conduct the clock setting operations. Instead,
	 * google_devfreq_update_*_freq_mba() sends the pf-state clamp request to the CPM.
	 * For the fabric clocks, devfreq driver is to route the min/max frequency clamp request to
	 * the CPM so that the CPM arbitrates the rate. The fabric devfreq driver directly hooks the
	 * min/max frequency update request event, rather than deciding the frequency rate in the
	 * target callback.
	 */

	return 0;
}

static inline void google_devfreq_mbox_free(struct google_devfreq *df)
{
	cpm_iface_free_client(df->cpm_mbox.client);
}

static void google_devfreq_remove_devfreq(struct google_devfreq *df)
{
	dev_pm_qos_remove_notifier(df->dev, &df->min_freq_nb, DEV_PM_QOS_MIN_FREQUENCY);
	dev_pm_qos_remove_notifier(df->dev, &df->max_freq_nb, DEV_PM_QOS_MAX_FREQUENCY);

	if (!IS_ERR_OR_NULL(df->devfreq)) {
		vote_manager_remove_devfreq(df->devfreq);
		google_unregister_devfreq(df->devfreq);
		devm_devfreq_remove_device(df->dev, df->devfreq);
	}
}

static void google_devfreq_remove(struct platform_device *pdev)
{
	struct google_devfreq *df = platform_get_drvdata(pdev);

	google_devfreq_remove_devfreq(df);
	google_devfreq_mbox_free(df);
}

static inline int google_devfreq_select_fabric_id(u32 lpcm_id, enum mipm_fabric_id *fabric_id)
{
	/* The fabric_id field is unused for GMC pf-state clamp. */
	switch (lpcm_id) {
	case LPCM_FABMED:
		*fabric_id = MIPM_FABMED;
		break;
	case LPCM_FABSYSS:
		*fabric_id = MIPM_FABSYSS;
		break;
	case LPCM_FABRT:
		*fabric_id = MIPM_FABRT;
		break;
	case LPCM_FABSTBY:
		*fabric_id = MIPM_FABSTBY;
		break;
	case LPCM_FABHBW:
		*fabric_id = MIPM_FABHBW;
		break;
	case LPCM_MEMSS:
		*fabric_id = MIPM_FABMEM;
		break;
	}
	return 0;
}

static inline int google_devfreq_select_tunable_id(u32 lpcm_id, struct tunable_ids *tunable_ids)
{
	switch (lpcm_id) {
	case LPCM_FABMED:
		fallthrough;
	case LPCM_FABSYSS:
		fallthrough;
	case LPCM_FABRT:
		fallthrough;
	case LPCM_FABSTBY:
		fallthrough;
	case LPCM_FABHBW:
		fallthrough;
	case LPCM_MEMSS:
		tunable_ids->min_pf_level = MIPM_TUNABLE_FAB_MIN_CLAMP;
		tunable_ids->max_pf_level = MIPM_TUNABLE_FAB_MAX_CLAMP;
		break;
	case LPCM_GMC0:
		fallthrough;
	case LPCM_GMC1:
		fallthrough;
	case LPCM_GMC2:
		fallthrough;
	case LPCM_GMC3:
		tunable_ids->min_pf_level = MIPM_TUNABLE_GMC_MIN_CLAMP;
		tunable_ids->max_pf_level = MIPM_TUNABLE_GMC_MAX_CLAMP;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int google_devfreq_init_mailbox_dts(struct google_devfreq *df)
{
	int err;

	err = of_property_read_u32(df->dev->of_node, "lpcm-id", &df->cpm_mbox.lpcm_id);
	if (err) {
		dev_err(df->dev, "%pOF is missing lpcm-id property\n", df->dev->of_node);
		return err;
	}

	err = google_devfreq_select_fabric_id(df->cpm_mbox.lpcm_id, &df->cpm_mbox.fabric_id);
	if (err) {
		dev_err(df->dev, "failed to select fabric_id. lpcm_id: %u", df->cpm_mbox.lpcm_id);
		return err;
	}

	err = google_devfreq_select_tunable_id(df->cpm_mbox.lpcm_id, &df->cpm_mbox.tunable_ids);
	if (err) {
		dev_err(df->dev, "failed to select tunable_id. lpcm_id: %u", df->cpm_mbox.lpcm_id);
		return err;
	}

	return 0;
}

static int google_devfreq_init_mailbox_client(struct google_devfreq *df)
{
	int err;
	struct google_devfreq_mbox *cpm_mbox = &df->cpm_mbox;

	cpm_mbox->client = cpm_iface_request_client(df->dev, 0, NULL, NULL);
	if (IS_ERR(cpm_mbox->client)) {
		err = PTR_ERR(cpm_mbox->client);
		if (err == -EPROBE_DEFER)
			dev_dbg(df->dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(df->dev, "Failed to request cpm client err: %d\n", err);
		return err;
	}

	err = google_devfreq_init_mailbox_dts(df);
	if (err) {
		google_devfreq_mbox_free(df);
		return err;
	}

	return 0;
}

static int google_devfreq_init_devfreq(struct google_devfreq *df)
{
	struct device *dev = df->dev;
	struct devfreq_dev_profile *profile;
	int err;

	profile = devm_kzalloc(dev, sizeof(*profile), GFP_KERNEL);
	if (!profile)
		return -ENOMEM;

	profile->polling_ms = 0;
	profile->target = google_devfreq_target_no_ops;
	profile->get_cur_freq = google_devfreq_get_cur_freq;

	df->profile = profile;

	err = dvfs_helper_add_opps_to_device(dev, dev->of_node);
	if (err) {
		dev_err(dev, "failed to add OPP table\n");
		goto out;
	}

	df->min_freq_nb.notifier_call = google_devfreq_update_min_freq_mba;
	err = dev_pm_qos_add_notifier(dev, &df->min_freq_nb, DEV_PM_QOS_MIN_FREQUENCY);
	if (err) {
		dev_err(dev, "failed to add min_freq notifier\n");
		goto out;
	}
	df->max_freq_nb.notifier_call = google_devfreq_update_max_freq_mba;
	err = dev_pm_qos_add_notifier(dev, &df->max_freq_nb, DEV_PM_QOS_MAX_FREQUENCY);
	if (err) {
		dev_err(dev, "failed to add max_freq notifier\n");
		goto remove_min_nb;
	}

	df->devfreq = devm_devfreq_add_device(dev, profile, DEVFREQ_GOV_POWERSAVE, NULL);
	if (IS_ERR(df->devfreq)) {
		dev_err(dev, "failed to add devfreq device\n");
		err = PTR_ERR(df->devfreq);
		goto remove_max_nb;
	}

	scnprintf(df->min_clamp_trace_name, DEVFREQ_TRACE_STR_LEN, "%s_min_clamp", dev_name(dev));
	scnprintf(df->max_clamp_trace_name, DEVFREQ_TRACE_STR_LEN, "%s_max_clamp", dev_name(dev));

	return 0;

remove_max_nb:
	dev_pm_qos_remove_notifier(dev, &df->max_freq_nb, DEV_PM_QOS_MAX_FREQUENCY);
remove_min_nb:
	dev_pm_qos_remove_notifier(dev, &df->min_freq_nb, DEV_PM_QOS_MIN_FREQUENCY);
out:
	return err;
}

static int google_devfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_devfreq *df;
	int err = 0;

	df = devm_kzalloc(dev, sizeof(*df), GFP_KERNEL);
	if (!df)
		return -ENOMEM;

	df->dev = dev;
	platform_set_drvdata(pdev, df);

	err = google_devfreq_init_mailbox_client(df);
	if (err)
		goto out;

	err = google_devfreq_init_devfreq(df);
	if (err)
		goto remove_mba_client;

	err = google_register_devfreq(df->devfreq);
	if (err)
		goto remove_mba_client;

	err = vote_manager_init_devfreq(df->devfreq);
	if (err)
		goto remove_mba_client;

	return 0;

remove_mba_client:
	google_devfreq_mbox_free(df);
out:
	return err;
}

static const struct of_device_id google_devfreq_of_match_table[] = {
	{
		.compatible = "google,devfreq-fabric-mem-mba",
	},
	{},
};
MODULE_DEVICE_TABLE(of, google_devfreq_of_match_table);

struct platform_driver google_devfreq_driver = {
	.driver = {
		.name = "google_devfreq",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_devfreq_of_match_table),
	},
	.probe  = google_devfreq_probe,
	.remove = google_devfreq_remove,
};

module_platform_driver(google_devfreq_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google devfreq driver");
MODULE_LICENSE("GPL");
