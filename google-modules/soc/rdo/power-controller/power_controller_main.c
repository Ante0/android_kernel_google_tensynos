// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver to power on/off google soc power domains
 * Copyright (C) 2023-2026 Google LLC.
 */

#include <linux/arm-smccc.h>
#include <linux/atomic.h>
#include <linux/container_of.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/dev_printk.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/iopoll.h>
#include <linux/workqueue.h>

#include <dt-bindings/power/genpd_lga.h>
#include <dt-bindings/power/genpd_mbu.h>
#include <mailbox/protocols/mba/cpm/common/lpb/lpb_service.h>
#include <mailbox/protocols/mba/cpm/common/syspm/syspm_interface_types.h>
#include <mailbox/protocols/mba/cpm/common/power_dash/pd_syspm_latency_data.h>
#include <perf/mbfs.h>

#include "latency/pd_latency_profile.h"
#include "power_controller.h"
#include "cpm_mappings.h"
#include "gpu_core_logic.h"

#define CREATE_TRACE_POINTS
#include "power_controller_trace.h"

#include <soc/google/goog_cpm_service_ids.h>

#define MAILBOX_TIMEOUT_EMULATION_MULTIPLIER 30
#define MBFS_MAX_NAME_LEN 17

#define LPB_REMOTE_CHANNEL 0x5
#define LPCM_CMD_SET_GEN_PD 1
#define LPCM_REMOTE_CHANNEL 0x8
#define POWER_ON_BITFIELD GENMASK(31, 24)
#define PD_ID_BITFIELD GENMASK(23, 16)
#define SSWRP_ID_BITFIELD GENMASK(15, 8)
#define REQ_ID_BITFIELD GENMASK(7, 0)

#define NO_ERROR 0
#define LPB_STATUS_ON 3
#define LPB_STATUS_OFF 0
#define LPB_EVENT_ON 0
#define LPB_EVENT_OFF 1
#define CPM_COMMON_PROT_NO_ERR 15

#define SIP_SVC_PD_CONTROL 0x82000701

#define ON_OFF_STR(x) ((x) ? "power_on" : "power_off")

static u32 mbx_send_timeout_ms = 3000;
static u32 mbx_receive_timeout_ms = 3000;
static bool dump_sswrp_on_suspend;

static ssize_t dump_sswrp_on_suspend_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", dump_sswrp_on_suspend);
}

static ssize_t dump_sswrp_on_suspend_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	if (kstrtobool(buf, &dump_sswrp_on_suspend))
		return -EINVAL;

	return count;
}

static struct device_attribute attr_dump_sswrp_on_suspend =
	__ATTR_RW(dump_sswrp_on_suspend);
/*
 * This data has to remain global in order to support external client APIs that
 * call directly to power controller without device context.
 *
 * It is based on the assumption, that this module is in fact a singleton, will
 * never be _remove()'d (device wouldn't be able to function otherwise) and
 * would require significant code changes if either of these assumptions changed
 */
static struct pc_privdata _pc_privdata;

static inline struct power_domain *to_power_domain(struct generic_pm_domain *d)
{
	return container_of(d, struct power_domain, genpd);
}

/*
 * This callback is triggered when Kernel receives the second message
 * (vote added) from CPM's SysPM service, or that a transition has finished from
 * LPB driver.
 */
static void power_request_completion_rx_callback(u32 context, void *msg,
						 void *priv_data)
{
	struct cpm_iface_payload *cpm_msg = msg;
	struct power_controller *pc = priv_data;
	struct device *dev = pc->dev;
	struct power_domain *pd = NULL;
	u32 resource_sid = cpm_msg->payload[0];
	union latency_payload_t fw_latency_payload = {
		.data = cpm_msg->payload[2]
	};
	int i;

	dev_dbg(dev, "rx callback msg %d %d %d\n", cpm_msg->payload[0],
		cpm_msg->payload[1], cpm_msg->payload[2]);

	for (i = 0; i < pc->privdata->pd_count; i++) {
		/* For MBA service */
		if (pc->privdata->pds[i].is_top_pd &&
		    pc->privdata->pds[i].cpm_lpb_sswrp_id == resource_sid) {
			pd = &pc->privdata->pds[i];
			break;
		}

		/* For MBFS service */
		if (!is_mbfs_handle_invalid(pc->privdata->pds[i].syspm_handle) &&
		    pc->privdata->pds[i].pm_resource_id == resource_sid) {
			pd = &pc->privdata->pds[i];
			break;
		}
	}
	if (!pd) {
		dev_err(dev,
			"cannot find corresponding power domain for ID: %d",
			resource_sid);
		return;
	}

	if (pc->privdata->pds) {
		pd_latency_profile_store(&pd->genpd, fw_latency_payload.latency,
					 PD_STATE_ON, FW);
	}

	dev_dbg(dev, "%s: power_on completed\n", pd->name);
	pd_latency_profile_stop(&pd->genpd, PD_STATE_ON, LATENCY_TYPE_BIT(EXEC),
				false);
	complete(&pd->cpm_resp_done);
}

static int initialize_mailbox_client(struct power_controller *power_controller)
{
	struct device *dev = power_controller->dev;
	int ret = 0;
	struct device_node *np = dev->of_node;

	if (of_property_present(np, "in_emulation")) {
		mbx_send_timeout_ms *= MAILBOX_TIMEOUT_EMULATION_MULTIPLIER;
		mbx_receive_timeout_ms *= MAILBOX_TIMEOUT_EMULATION_MULTIPLIER;
	}

	power_controller->client =
		cpm_iface_request_client(dev,
					 APC_COMMON_SERVICE_ID_POWER_CONTROLLER,
					 power_request_completion_rx_callback,
					 power_controller);

	if (IS_ERR(power_controller->client))
		ret = PTR_ERR(power_controller->client);

	return ret;
}

#if IS_ENABLED(CONFIG_GOOGLE_SYSPM)
static int init_mbfs(struct power_controller *power_controller,
		     const char *syspm_root_path)
{
	enum mbfs_error_code mbfs_ret;
	struct device *dev = power_controller->dev;

	if (!syspm_root_path) {
		dev_err(dev, "Missing syspm root path\n");
		return -EINVAL;
	}

	mbfs_ret =
		mbfs_get_handle(syspm_root_path, &power_controller->syspm_root);
	if (mbfs_ret) {
		dev_err(dev, "Failed to get mbfs handle for %s: %s\n",
			syspm_root_path, get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	return 0;
}

static int configure_mbfs_pd(struct power_domain *pd,
			     struct device_node *child_np)
{
	int ret;
	enum mbfs_error_code mbfs_ret;
	const char *mbfs_node_name;
	struct power_controller *pc = pd->power_controller;
	union val64 mbfs_payload;

	ret = of_property_read_u32(child_np, "pm-resource-id",
				   &pd->pm_resource_id);
	if (ret < 0) {
		dev_err(pc->dev, "%s: failed to read resource-id\n", pd->name);
		return -EINVAL;
	}

	ret = of_property_read_string(child_np, "mbfs-node-name",
				      &mbfs_node_name);
	if (ret < 0) {
		dev_err(pc->dev,
			"%s: failed to read mbfs-node-name\n", pd->name);
		return -EINVAL;
	}

	mbfs_ret = mbfs_get_child_by_name(pc->syspm_root, mbfs_node_name,
					  &pd->syspm_handle);
	if (mbfs_ret) {
		dev_err(pc->dev, "Failed to get %s MBFS handle: %s\n",
			mbfs_node_name, get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	mbfs_ret = mbfs_read_file(pd->syspm_handle, &mbfs_payload);
	if (mbfs_ret) {
		dev_err(pc->dev, "%s: MBFS error reading initial state: %s",
			pd->name, get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	switch (mbfs_payload.number) {
	case SYSPM_CLIENT_VOTE_STATE_NO_VOTE:
		atomic_set(&pd->state, PD_STATE_OFF);
		break;
	case SYSPM_CLIENT_VOTE_STATE_HAS_VOTE:
		atomic_set(&pd->state, PD_STATE_ON);
		break;
	default:
		dev_err(pc->dev, "%s: MBFS initial state error: %llu",
			pd->name, mbfs_payload.number);
		return -EINVAL;
	}

	init_completion(&pd->cpm_resp_done);

	return NO_ERROR;
}

static int send_power_request(struct power_domain *pd, enum power_state state)
{
	int ret;
	union val64 mbfs_payload;
	enum mbfs_error_code mbfs_ret;

	mbfs_payload.number = state;
	reinit_completion(&pd->cpm_resp_done);

	trace_send_mail_mbfs(pd, state);
	mbfs_ret = mbfs_write_file(pd->syspm_handle, mbfs_payload);
	pd_latency_profile_stop(&pd->genpd, state, LATENCY_TYPE_BIT(ACK),
				false);

	switch (mbfs_ret) {
	case MBFS_OK:
		/* Resource already active */
		dev_dbg(&pd->genpd.dev, "Resource already active.");

		break;
	case MBFS_TRY_AGAIN:
		/* Sequence toggled, wait for resource readiness */
		dev_dbg(&pd->genpd.dev, "Wait for resource readiness.");

		ret = wait_for_completion_timeout(
			&pd->cpm_resp_done,
			msecs_to_jiffies(mbx_receive_timeout_ms));
		if (ret == 0) {
			panic("%s: %s: wait for CPM response timeout\n",
			      pd->name, ON_OFF_STR(state));

			return -ETIMEDOUT;
		}

		break;
	default:
		/* MBFS write file error occurred */
		dev_err(&pd->genpd.dev, "MBFS error on %s: %s\n", pd->name,
			get_mbfs_error_string(mbfs_ret));

		return mbfs_error2linux(mbfs_ret);
	}

	trace_recv_result_mbfs(pd, state,
			       (mbfs_ret == MBFS_TRY_AGAIN) ? 0 : mbfs_ret);

	return 0;
}

static int get_pd_vote_status(struct generic_pm_domain *genpd)
{
	struct power_domain *pd = to_power_domain(genpd);
	struct device *dev = pd->power_controller->dev;
	union val64 mbfs_payload;
	enum mbfs_error_code mbfs_ret;

	if (is_mbfs_handle_invalid(pd->syspm_handle)) {
		dev_warn(dev,
			 "%s: Vote status get not supported for this domain.",
			 pd->name);
		return -EINVAL;
	}

	mbfs_ret = mbfs_read_file(pd->syspm_handle, &mbfs_payload);
	if (mbfs_ret) {
		dev_err(&pd->genpd.dev, "MBFS error reading initial state: %s",
			get_mbfs_error_string(mbfs_ret));
		return mbfs_error2linux(mbfs_ret);
	}

	return mbfs_payload.number;
}

#else /* CONFIG_GOOGLE_SYSPM */

static int init_mbfs(struct power_controller *power_controller,
		     const char *mbfs_root_path)
{
	return 0;
}

static int configure_mbfs_pd(struct power_domain *pd,
			     struct device_node *child_np)
{
	return 0;
}

static int send_power_request(struct power_domain *pd, enum power_state state)
{
	struct device *dev = pd->power_controller->dev;
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	int ret;

	dev_dbg(dev, "%s: %s: sswrp_id = %d\n", pd->name, ON_OFF_STR(state),
		pd->cpm_lpb_sswrp_id);

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = mbx_send_timeout_ms;
	cpm_req.dst_id = LPB_REMOTE_CHANNEL;

	req_msg.payload[0] = LPB_CMD_SSWRP_STATE_SET;
	req_msg.payload[1] = pd->cpm_lpb_sswrp_id;
	req_msg.payload[2] = state;

	reinit_completion(&pd->cpm_resp_done);

	trace_send_mail_lpb(pd, state);
	ret = cpm_send_message(pd->power_controller->client, &cpm_req);
	pd_latency_profile_stop(&pd->genpd, state, LATENCY_TYPE_BIT(ACK),
				false);

	if (ret < 0) {
		dev_err(dev, "%s: %s: send lpb message failed ret (%d)\n",
			pd->name, ON_OFF_STR(state), ret);

		return ret;
	}

	dev_dbg(dev, "%s: %s: lpb response msg %d %d %d\n", pd->name,
		ON_OFF_STR(state), resp_msg.payload[0], resp_msg.payload[1],
		resp_msg.payload[2]);

	if (resp_msg.payload[0] != CPM_COMMON_PROT_NO_ERR) {
		/* We failed completely no additional message is waiting */
		dev_err(dev, "%s: %s: CPM LPB service failed with err=%d\n",
			pd->name, ON_OFF_STR(state), resp_msg.payload[0]);

		return -EPROTO;
	}

	switch (resp_msg.payload[1]) {
	case LPB_SERVICE_CMD_SUCCESS:
		/* Command requires no asynchronous processing -- we are done */
		ret = NO_ERROR;
		break;
	case LPB_SERVICE_PWRUP_STARTED:
	case LPB_SERVICE_PWRDN_STARTED:
		/*
		 * We have received the first response for power on/off.  The
		 * final result will be received asynchronously.
		 */

		ret = wait_for_completion_timeout(
			&pd->cpm_resp_done,
			msecs_to_jiffies(mbx_receive_timeout_ms));
		if (ret == 0) {
			panic("%s: %s: wait for CPM response timeout\n",
			      pd->name, ON_OFF_STR(state));
			ret = -ETIMEDOUT;
			break;
		}
		ret = NO_ERROR;
		break;
	case LPB_SERVICE_CMD_FAIL:
	default:
		/* Command got through but the result was failure */
		dev_err(dev, "%s: %s: CPM LPB services returned fail, err=%d",
			pd->name, ON_OFF_STR(state), resp_msg.payload[1]);
		ret = -EPROTO;
		break;
	}

	trace_recv_result_lpb(pd, state, ret);

	return ret;
}

static int get_pd_vote_status(struct generic_pm_domain *genpd)
{
	struct power_domain *pd = to_power_domain(genpd);
	struct device *dev = pd->power_controller->dev;

	if (!pd->is_top_pd) {
		dev_warn(dev, "%s: Only SSWRP vote status get supported.",
			 pd->name);
		return -EINVAL;
	}

	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	int ret;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = mbx_send_timeout_ms;
	cpm_req.dst_id = LPB_REMOTE_CHANNEL;

	req_msg.payload[0] = LPB_CMD_SSWRP_STATE_GET;
	req_msg.payload[1] = pd->cpm_lpb_sswrp_id;

	ret = cpm_send_message(pd->power_controller->client, &cpm_req);
	if (ret < 0) {
		dev_err(dev, "%s: failed to get SSWRP state, MBA err: %d",
			pd->name, ret);
		return ret;
	}

	return resp_msg.payload[1] == LPB_STATUS_ON ? PD_STATE_ON :
						      PD_STATE_OFF;
}

#endif /* CONFIG_GOOGLE_SYSPM */

static int configure_mba_pd(struct power_domain *pd,
			    struct device_node *child_np,
			    const struct cpm_mappings *cpm_map)
{
	int ret;
	struct power_controller *pc = pd->power_controller;
	struct device *dev = pc->dev;

	if (cpm_map == NULL)
		return -EINVAL;

	ret = of_property_read_u32(child_np, "subdomain-id",
				   &pd->cpm_lpcm_subdomain_id);
	if (ret == -EINVAL) {
		/* Top power domains do not have subdomain-id property. */
		pd->is_top_pd = true;
	} else if (ret < 0) {
		dev_err(dev, "%s: failed to read subdomain-id\n", pd->name);
		return -EINVAL;
	}

	ret = of_property_read_u32(child_np, "sswrp-id", &pd->pm_resource_id);
	if (ret < 0) {
		dev_err(dev, "%s: failed to read sswrp-id\n", child_np->name);
		return -EINVAL;
	}

	if (pd->is_top_pd && cpm_map->sswrp_to_lpb_ids)
		pd->cpm_lpb_sswrp_id =
			cpm_map->sswrp_to_lpb_ids[pd->pm_resource_id];
	else if (cpm_map->sswrp_to_lpcm_ids)
		pd->cpm_lpcm_sswrp_id =
			cpm_map->sswrp_to_lpcm_ids[pd->pm_resource_id];
	else
		return -EINVAL;

	init_completion(&pd->cpm_resp_done);

	return NO_ERROR;
}

static int send_lpcm_mail(struct power_domain *pd, enum power_state state)
{
	struct device *dev = pd->power_controller->dev;
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	int ret;

	if (pd->cpm_lpcm_sswrp_id == NOT_SUPPORTED) {
		dev_err(dev,
			"%s: %s: ID %d is not supported by LPCM service\n",
			pd->name, ON_OFF_STR(state), pd->pm_resource_id);
		return -EPROTONOSUPPORT;
	}

	dev_dbg(dev, "%s: %s: lpcm_id = %d\n", pd->name, ON_OFF_STR(state),
		pd->cpm_lpcm_sswrp_id);

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = mbx_send_timeout_ms;
	cpm_req.dst_id = LPCM_REMOTE_CHANNEL;
	req_msg.payload[0] =
		FIELD_PREP(REQ_ID_BITFIELD, LPCM_CMD_SET_GEN_PD) |
		FIELD_PREP(SSWRP_ID_BITFIELD, pd->cpm_lpcm_sswrp_id) |
		FIELD_PREP(PD_ID_BITFIELD, pd->cpm_lpcm_subdomain_id) |
		FIELD_PREP(POWER_ON_BITFIELD, state);

	trace_send_mail_lpcm(pd, state);
	ret = cpm_send_message(pd->power_controller->client, &cpm_req);
	trace_recv_result_lpcm(pd, state, resp_msg.payload[0]);
	pd_latency_profile_stop(&pd->genpd, state, LATENCY_TYPE_BIT(ACK),
				false);

	if (ret < 0) {
		dev_err(dev, "%s: %s: send lpcm message failed ret (%d)\n",
			pd->name, ON_OFF_STR(state), ret);

		return ret;
	}

	dev_dbg(dev, "%s: %s: got resp from %u, data %u %u %u.\n", pd->name,
		ON_OFF_STR(state), cpm_req.dst_id, resp_msg.payload[0],
		resp_msg.payload[1], resp_msg.payload[2]);

	switch (resp_msg.payload[0]) {
	case NO_ERROR:
		dev_dbg(dev, "%s: %s: lpcm service success.\n", pd->name,
			ON_OFF_STR(state));
		ret = 0;
		pd_latency_profile_stop(&pd->genpd, state,
					LATENCY_TYPE_BIT(EXEC), false);
		break;
	default:
		dev_err(dev, "%s: %s: unknown error code %d.\n", pd->name,
			ON_OFF_STR(state), resp_msg.payload[0]);
		ret = -EPROTO;
		break;
	}

	return ret;
}

static bool is_gpu_logic_core_pd(struct power_domain *pd)
{
	return !strcmp(pd->name, "gpu_core_logic_pd");
}

static bool is_aoc_sswrp_pd(struct power_domain *pd)
{
	return !strcmp(pd->name, "sswrp_aoc_pd");
}

static inline unsigned long lpcm_smc(struct power_domain *pd,
				     enum power_state state)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SIP_SVC_PD_CONTROL, pd->pm_resource_id,
		      pd->cpm_lpcm_subdomain_id, state, 0, 0, 0, 0, &res);

	WARN_ON(res.a0);
	if ((unsigned long)res.a1) {
		dev_err(pd->power_controller->dev,
			"Failed SMC call sswrp:%d sub-domain:%d, res.a1:%lu\n",
			pd->pm_resource_id, pd->cpm_lpcm_subdomain_id, res.a1);
		return -EINVAL;
	}

	return 0;
}

static int set_power_state(struct generic_pm_domain *domain,
			   enum power_state state)
{
	int ret;
	struct power_domain *pd = to_power_domain(domain);

	if (state >= PD_STATE_COUNT || state < 0)
		return -EINVAL;

	if (pd->boot_stay_on) {
		dev_warn(pd->power_controller->dev,
			"%s: skipping %s (boot-stay-on), current state:%d\n",
			pd->name, ON_OFF_STR(state), atomic_read(&pd->state));
		return 0;
	}

	pd_latency_profile_start(domain, state,
				 LATENCY_TYPE_BIT(E2E) |
				 LATENCY_TYPE_BIT(EXEC) |
				 LATENCY_TYPE_BIT(ACK));

	/*
	 * With CONFIG_GOOGLE_SYSPM the code should default to
	 * send_power_request if handle is available.
	 *
	 * Otherwise the handle is always invalid, but for top domains the code
	 * should still send_power_request, as without CONFIG_GOOGLE_SYSPM it
	 * sends legacy, direct mailbox message to trigger top domain on in CPM.
	 */
	if (is_gpu_logic_core_pd(pd))
		ret = state == PD_STATE_ON ?
			      gpu_core_logic_on(pd->power_controller->dev,
						&pd->gpu_core_logic) :
			      gpu_core_logic_off(pd->power_controller->dev,
						 &pd->gpu_core_logic);
	else if (pd->use_smc)
		ret = lpcm_smc(pd, state);
	else if (!is_mbfs_handle_invalid(pd->syspm_handle) || pd->is_top_pd)
		ret = send_power_request(pd, state);
	else
		ret = send_lpcm_mail(pd, state);

	if (!ret)
		atomic_set(&pd->state, state);

	/*
	 * We cancel just in case it was never stopped (error scenario).
	 * If the stop was called before, then cancelling/stopping does nothing,
	 * so this line catches all the branches.
	 */
	pd_latency_profile_stop(domain, state,
				LATENCY_TYPE_BIT(E2E)  |
				LATENCY_TYPE_BIT(EXEC) |
				LATENCY_TYPE_BIT(ACK),
				ret);

	return ret;
}

static int power_on(struct generic_pm_domain *domain)
{
	return set_power_state(domain, PD_STATE_ON);
}

static int power_off(struct generic_pm_domain *domain)
{
	return set_power_state(domain, PD_STATE_OFF);
}

static int parse_pd_attributes(struct power_domain *pd,
			       struct device_node *child_np)
{
	int ret;
	struct device *dev = pd->power_controller->dev;

	/*
	 * The 'on-at-init' flag signifies that the power domain is already on
	 * during driver probe. Not required if MBFS is available, as the state
	 * is dynamically queried in such case.
	 */
	if (is_mbfs_handle_invalid(pd->syspm_handle) &&
	    of_property_read_bool(child_np, "on-at-init")) {
		atomic_set(&pd->state, PD_STATE_ON);
	}

	/*
	 * The 'use-smc' flag signifies that the power domain is controlled
	 * directly in kernel trust zone, instead of sending message requests
	 * to the hardware.
	 */
	pd->use_smc = of_property_read_bool(child_np, "use-smc");

	/*
	 * The 'force-on' flag signifies that the power domain has to be turned
	 * ON.
	 */
	if (atomic_read(&pd->state) == PD_STATE_OFF &&
	    of_property_read_bool(child_np, "force-on")) {
		ret = power_on(&pd->genpd);
		if (ret) {
			dev_err(dev, "%s: failed to turn ON: %d\n", pd->name,
				ret);
			return ret;
		}
		atomic_set(&pd->state, PD_STATE_ON);
	}

	/*
	 * The 'always-on' flag signifies that the power domain needs to stay ON
	 * always.
	 * The core GenPD driver expects such domains to be ON already.
	 */
	if (of_property_read_bool(child_np, "always-on")) {
		pd->genpd.flags |= GENPD_FLAG_ALWAYS_ON;
		if (atomic_read(&pd->state) == PD_STATE_OFF) {
			dev_err(dev, "%s: always-on PM domain is not on\n", pd->name);
			return -EINVAL;
		}
	}

	/*
	 * The 'rpm-always-on' flag signifies that the power domain needs to
	 * stay ON during runtime suspend.
	 * The core GenPD driver expects such domains to be ON already.
	 */
	if (of_property_read_bool(child_np, "rpm-always-on")) {
		pd->genpd.flags |= GENPD_FLAG_RPM_ALWAYS_ON;
		if (atomic_read(&pd->state) == PD_STATE_OFF) {
			dev_err(dev, "%s: rpm-always-on PM domain is not on\n",
				pd->name);
			return -EINVAL;
		}
	}

	if (of_property_read_bool(child_np, "irq-safe")) {
		pd->genpd.flags |= GENPD_FLAG_IRQ_SAFE;
		if (pd->genpd.flags & GENPD_FLAG_RPM_ALWAYS_ON) {
			dev_err(dev,
				"%s: rpm-always-on and irq-safe both set\n",
				pd->name);
			return -EINVAL;
		}
	}

	/*
	 * The `active-wakeup` flag signifies power domain can act as a source
	 * of wakeup events from kernel suspend, hence should be kept active
	 * during kernel suspend in case a device attached to it is wakeup
	 * capable.
	 */
	if (of_property_read_bool(child_np, "active-wakeup"))
		pd->genpd.flags |= GENPD_FLAG_ACTIVE_WAKEUP;

	if (of_property_read_bool(child_np, "google,boot-stay-on")) {
		if (atomic_read(&pd->state) == PD_STATE_OFF) {
			ret = power_on(&pd->genpd);
			if (ret) {
				dev_err(dev, "%s: failed to turn ON: %d\n", pd->name,
					ret);
				return ret;
			}
		}
		pd->boot_stay_on = true;
	}

	return NO_ERROR;
}

static struct generic_pm_domain *dev_to_genpd(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev->pm_domain))
		return ERR_PTR(-EINVAL);

	return pd_to_genpd(dev->pm_domain);
}

#define genpd_is_active_wakeup(genpd)	(genpd->flags & GENPD_FLAG_ACTIVE_WAKEUP)

/*
 * This definition is internal to the kernel's drivers/pmdomain/core.c, so we
 * can't reference it directly. It's important it matches the kernel's
 * implementation exactly.
 */
struct genpd_lock_ops {
	void (*lock)(struct generic_pm_domain *genpd);
	void (*lock_nested)(struct generic_pm_domain *genpd, int depth);
	int (*lock_interruptible)(struct generic_pm_domain *genpd);
	void (*unlock)(struct generic_pm_domain *genpd);
};

static void genpd_lock(struct generic_pm_domain *genpd)
{
	genpd->lock_ops->lock(genpd);
}

static void genpd_unlock(struct generic_pm_domain *genpd)
{
	genpd->lock_ops->unlock(genpd);
}

/* This is exported, but not in the GKI symbol list. Imitate it for now. */
static int __pm_generic_resume_noirq(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->resume_noirq ? pm->resume_noirq(dev) : 0;
}

/*
 * We have chosen to override genpd_resume_noirq()/genpd_finish_resume() for
 * 'no-auto-resume' domains to optimize system-resume behaviors. Specifically:
 *
 *  1) we try to imitate most of what it does; but
 *  2) we omit actually resuming (genpd_sync_power_on(), genpd_start_dev()) the
 *     domain. We expect it to resume at a later time via runtime-PM.
 *
 * This invites pitfalls, in case we don't do #1 correctly. (For instance, it's
 * important to manage suspended_count carefully.)
 *
 * This function attempts to match the structure of genpd_finish_resume(), so
 * that it's easier to review any discrepancies over time.
 */
static int noop_resume_noirq_overwrite(struct device *dev)
{
	struct generic_pm_domain *genpd = dev_to_genpd(dev);

	if (IS_ERR(genpd))
		return -EINVAL;

	dev_dbg(dev, "Skipping power_domain resume_noirq call for the device.");

	if (device_wakeup_path(dev) && genpd_is_active_wakeup(genpd))
		return __pm_generic_resume_noirq(dev);

	genpd_lock(genpd);
	genpd->suspended_count--;
	genpd_unlock(genpd);

	return __pm_generic_resume_noirq(dev);
}

static void unregister_power_domains(struct platform_device *pdev, int count)
{
	struct power_controller *power_controller = platform_get_drvdata(pdev);
	struct device_node *pwr_ctrl_np = pdev->dev.of_node;
	struct device_node *pwr_domain_np;
	struct power_domain *pd;
	int i = 0;

	dev_dbg(&pdev->dev, "Unregistering all power domains\n");
	for_each_available_child_of_node(pwr_ctrl_np, pwr_domain_np) {
		if (i >= count)
			break;
		pd = &power_controller->privdata->pds[i];
		of_genpd_del_provider(pwr_domain_np);
		pm_genpd_remove(&pd->genpd);
		++i;
	}
}

static struct power_domain *pc_get_pd_by_name(const char *name)
{
	int i;

	if (name == NULL)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < _pc_privdata.pd_count; i++) {
		if (!strcmp(_pc_privdata.pds[i].name, name))
			break;
	}
	if (i == _pc_privdata.pd_count)
		return ERR_PTR(-EINVAL);

	return &_pc_privdata.pds[i];
}

int get_pd_state_by_name(const char *name, enum power_state *state)
{
	struct power_domain *pd;

	if (!atomic_read_acquire(&_pc_privdata.initialized))
		return -EAGAIN;

	pd = pc_get_pd_by_name(name);
	if (IS_ERR(pd))
		return -EINVAL;

	*state = atomic_read(&pd->state);

	return 0;
}
EXPORT_SYMBOL_GPL(get_pd_state_by_name);

int register_pd_notifier_by_name(const char *name, struct notifier_block *nb)
{
	struct power_domain *pd;
	int ret;

	if (!atomic_read_acquire(&_pc_privdata.initialized))
		return -EAGAIN;

	pd = pc_get_pd_by_name(name);
	if (IS_ERR(pd))
		return -EINVAL;

	/*
	 * GenPD supports multiple lock types, but the exact implementation
	 * is not exposed to vendor modules. Limit the supported domains to the
	 * ones that use mutex, ie. they are not marked as CPU/IRQ-safe.
	 */
	if ((pd->genpd.flags & GENPD_FLAG_CPU_DOMAIN) ||
	    (pd->genpd.flags & GENPD_FLAG_IRQ_SAFE)) {
		dev_err(&pd->genpd.dev, "Can't register nb for IRQ-safe/CPU domains\n");
		return -EPERM;
	}

	mutex_lock(&pd->genpd.mlock);
	ret = raw_notifier_chain_register(&pd->genpd.power_notifiers, nb);
	mutex_unlock(&pd->genpd.mlock);

	return ret;
}
EXPORT_SYMBOL_GPL(register_pd_notifier_by_name);

#ifdef CONFIG_DEBUG_FS
/*
 * This flag can be dangerous because it allows power on/off a power domain
 * without changing reference count in genpd core. This can lead to an
 * inconsistent state where genpd core thinks the domain is ON while it is
 * actually OFF and vice versa. Only define this flag for testing purposes.
 */
static int debugfs_power_domain_ctrl_set(void *domain, u64 val)
{
	struct generic_pm_domain *genpd = domain;
	int ret = 0;

	dev_dbg(&genpd->dev, "%s\n", ON_OFF_STR(val));

	mutex_lock(&genpd->mlock);
	if (val)
		ret = power_on(genpd);
	else
		ret = power_off(genpd);
	mutex_unlock(&genpd->mlock);

	return ret;
}

static int debugfs_power_domain_ctrl_get(void *domain, u64 *val)
{
	struct generic_pm_domain *genpd = domain;
	int ret;

	ret = get_pd_vote_status(genpd);
	if (ret < 0)
		return ret;

	*val = ret;

	return NO_ERROR;
}

DEFINE_DEBUGFS_ATTRIBUTE(debugfs_power_domain_ctrl_fops,
			 debugfs_power_domain_ctrl_get,
			 debugfs_power_domain_ctrl_set, "%llu\n");

static void genpd_debugfs_add(struct generic_pm_domain *domain)
{
	struct power_domain *pd = to_power_domain(domain);
	struct dentry *d =
		debugfs_create_dir(domain->name,
				   pd->power_controller->debugfs_root);

	debugfs_create_file("state", 0220, d, domain,
			    &debugfs_power_domain_ctrl_fops);
}

static void genpd_debugfs_init(struct power_controller *power_controller)
{
	power_controller->debugfs_root =
		debugfs_create_dir(dev_name(power_controller->dev), NULL);

	for (int i = 0; i < power_controller->privdata->pd_count; ++i)
		genpd_debugfs_add(&power_controller->privdata->pds[i].genpd);
}

static void genpd_debugfs_remove(struct power_controller *power_controller)
{
	debugfs_remove_recursive(power_controller->debugfs_root);
}
#else
static void genpd_debugfs_init(struct power_controller *power_controller) {}
static void genpd_debugfs_remove(struct power_controller *power_controller) {}
#endif

static inline void power_controller_mbox_free(struct power_controller *pc)
{
	cpm_iface_free_client(pc->client);
}

struct power_controller_desc {
	enum gpu_dts_version version;
	const struct cpm_mappings *cpm_map;
	const char *syspm_root_path;
	bool enable_sswrp_debug;
};

static struct power_controller_desc lga_power_controller_desc = {
	.version = GPU_DTS_VERSION_2,
	.cpm_map = &lga_cpm_mappings,
	.enable_sswrp_debug = true,
};

static struct power_controller_desc mbu_power_controller_desc = {
	.version = GPU_DTS_VERSION_3,
	.cpm_map = &mbu_cpm_mappings,
	.syspm_root_path = "cpm/syspm/clients/vm1",
};

static int power_controller_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct power_controller *power_controller;
	struct device_node *np = dev->of_node;
	struct device_node *child_np;
	struct of_phandle_args child_args, parent_args;
	const struct power_controller_desc *desc;
	int ret, i = 0, pd_count;

	power_controller =
		devm_kzalloc(dev, sizeof(*power_controller), GFP_KERNEL);
	if (!power_controller)
		return -ENOMEM;

	power_controller->dev = dev;
	power_controller->privdata = &_pc_privdata;

	platform_set_drvdata(pdev, power_controller);

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	ret = initialize_mailbox_client(power_controller);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev,
				"mailbox is not ready. Probe later. (ret: %d)",
				ret);
		else
			dev_err(dev,
				"failed to request mailbox channel err %d\n",
				ret);
		return ret;
	}

	pd_count = of_get_child_count(np);
	power_controller->privdata->pds =
		devm_kcalloc(dev, pd_count,
			     sizeof(*power_controller->privdata->pds),
			     GFP_KERNEL);
	power_controller->privdata->pd_count = pd_count;

	ret = init_mbfs(power_controller, desc->syspm_root_path);
	if (ret)
		goto free_mailbox;

	for_each_available_child_of_node(np, child_np) {
		struct power_domain *pd = &power_controller->privdata->pds[i];

		pd->is_top_pd = false;
		atomic_set(&pd->state, PD_STATE_OFF);
		pd->name = child_np->name;
		pd->power_controller = power_controller;
		pd->syspm_handle = MBFS_INVALID_HANDLE;
		pd->genpd.name = child_np->name;
		pd->genpd.power_on = power_on;
		pd->genpd.power_off = power_off;

		if (is_gpu_logic_core_pd(&power_controller->privdata->pds[i])) {
			pd->gpu_core_logic.gpu_dts_version = desc->version;
			ret = gpu_core_logic_init(dev, child_np,
						  &pd->gpu_core_logic);
		} else if (of_property_present(child_np, "mbfs-node-name")) {
			ret = configure_mbfs_pd(pd, child_np);
		} else {
			ret = configure_mba_pd(pd, child_np, desc->cpm_map);
		}
		if (ret)
			goto cleanup_pds;

		/* May turn on the domain if `force-on` is set */
		ret = parse_pd_attributes(pd, child_np);
		if (ret) {
			dev_err(dev, "%s: failed to parse attributes\n",
				pd->name);
			goto cleanup_pds;
		}

		ret = pm_genpd_init(&pd->genpd, NULL,
				    atomic_read(&pd->state) == PD_STATE_OFF);
		if (ret) {
			dev_err(dev, "%s: failed to init power domain\n",
				pd->name);
			goto cleanup_pds;
		}
		dev_dbg(dev, "%s: init power domain, state: %d", pd->name,
			atomic_read(&pd->state));

		/*
		 * The `no-auto-resume` signifies power domain should not be
		 * resumed during kernel resume_noirq phase. It has to be called
		 * after `pm_genpd_init()` as it overwrites the default callback
		 * that's assigned during initialization.
		 */
		if (of_property_read_bool(child_np, "no-auto-resume")) {
			dev_dbg(dev, "%s: set noop for resume_noirq callback",
				pd->name);
			pd->genpd.domain.ops.resume_noirq =
				noop_resume_noirq_overwrite;
		}

		ret = of_genpd_add_provider_simple(child_np, &pd->genpd);
		if (ret) {
			dev_err(dev, "%s: error adding genpd provider\n",
				pd->name);
			pm_genpd_remove(&pd->genpd);
			goto cleanup_pds;
		}

		i++;
	}
	for_each_available_child_of_node(np, child_np) {
		if (of_parse_phandle_with_args(child_np, "power-domains",
					       "#power-domain-cells", 0,
					       &parent_args))
			continue;

		child_args.np = child_np;
		child_args.args_count = 0;
		ret = of_genpd_add_subdomain(&parent_args, &child_args);
		if (ret) {
			dev_err(dev, "%s: failed to add parent: %d",
				child_args.np->name, ret);
			of_node_put(parent_args.np);
			goto cleanup_pds;
		}
		of_node_put(parent_args.np);
	}

	genpd_debugfs_init(power_controller);
	ret = pd_latency_profile_init(pdev);
	if (ret)
		dev_err(dev, "Error initiating latency profiler: %d", ret);

	atomic_set_release(&power_controller->privdata->initialized, true);

	if (desc->enable_sswrp_debug) {
		ret = device_create_file(dev, &attr_dump_sswrp_on_suspend);
		if (ret)
			dev_err(dev,
			"Failed to create attr_dump_sswrp_on_suspend ret: %d",
			ret);
	}
	return 0;

cleanup_pds:
	of_node_put(child_np);
	unregister_power_domains(pdev, i);
free_mailbox:
	power_controller_mbox_free(power_controller);
	return ret;
};

static void power_controller_remove(struct platform_device *pdev)
{
	struct power_controller *power_controller = platform_get_drvdata(pdev);
	struct device_node *child_np, *np = pdev->dev.of_node;
	const struct power_controller_desc *desc;
	int i;

	pd_latency_profile_remove(pdev);
	genpd_debugfs_remove(power_controller);

	power_controller_mbox_free(power_controller);

	for_each_available_child_of_node(np, child_np) {
		of_genpd_del_provider(child_np);
	}
	for (i = power_controller->privdata->pd_count - 1; i >= 0; i--)
		pm_genpd_remove(&power_controller->privdata->pds[i].genpd);

	desc = of_device_get_match_data(&pdev->dev);
	if (desc && desc->enable_sswrp_debug)
		device_remove_file(&pdev->dev, &attr_dump_sswrp_on_suspend);
}

static const struct of_device_id power_controller_of_match_table[] = {
	[0] = { .compatible = "google,lga-power-controller",
		.data = &lga_power_controller_desc },
	[1] = { .compatible = "google,mbu-power-controller",
		.data = &mbu_power_controller_desc },
	[2] = {},
};
MODULE_DEVICE_TABLE(of, power_controller_of_match_table);

static int dump_sswrp_status(struct device *dev)
{
	struct power_controller *controller = dev_get_drvdata(dev);
	const struct power_controller_desc *desc;
	struct power_domain *pd;
	struct generic_pm_domain *genpd;
	struct pm_domain_data *pdd;
	enum power_state state;

	desc = of_device_get_match_data(dev);
	if (!desc || !desc->enable_sswrp_debug)
		return 0;

	if (!dump_sswrp_on_suspend)
		return 0;

	for (int i = 0; i < controller->privdata->pd_count; i++) {
		pd = &controller->privdata->pds[i];
		genpd = &pd->genpd;
		state = atomic_read(&pd->state);

		if (state == PD_STATE_OFF || (genpd->flags & GENPD_FLAG_ALWAYS_ON))
			continue;

		list_for_each_entry(pdd, &genpd->dev_list, list_node) {
			if (pm_runtime_active(pdd->dev)) {
				dev_err(dev, "pd: %s dev: %s active\n",
					pd->name, dev_name(pdd->dev));
			}
		}
	}
	return 0;
}

static int power_controller_suspend_prepare(struct device *dev)
{
	struct power_controller *controller = dev_get_drvdata(dev);

	for (int i = 0; i < controller->privdata->pd_count; i++) {
		struct power_domain *pd = &controller->privdata->pds[i];

		if (is_aoc_sswrp_pd(pd)) {
			pd->genpd.flags |= GENPD_FLAG_ALWAYS_ON;
			break;
		}
	}
	return 0;
}

static void power_controller_suspend_complete(struct device *dev)
{
	struct power_controller *controller = dev_get_drvdata(dev);

	for (int i = 0; i < controller->privdata->pd_count; i++) {
		struct power_domain *pd = &controller->privdata->pds[i];

		if (is_aoc_sswrp_pd(pd)) {
			pd->genpd.flags &= ~GENPD_FLAG_ALWAYS_ON;
			break;
		}
	}
}

static const struct dev_pm_ops simple_pm_ops = {
	.prepare = power_controller_suspend_prepare,
	.complete = power_controller_suspend_complete,
	.suspend = dump_sswrp_status,
};

static void power_controller_sync_state(struct device *dev)
{
	struct power_controller *pc = dev_get_drvdata(dev);
	int i;

	dev_dbg(dev, "Sync state reached, clearing boot_stay_on flags\n");

	for (i = 0; i < pc->privdata->pd_count; i++) {
		struct power_domain *pd = &pc->privdata->pds[i];

		if (pd->boot_stay_on) {
			dev_dbg(dev, "    Clearing %s, current state: %d\n",
				pd->name, atomic_read(&pd->state));
			pd->boot_stay_on = false;
		}
	}
}

static struct platform_driver power_controller_driver = {
	.probe = power_controller_probe,
	.remove = power_controller_remove,
	.driver = {
		.name = "power-controller",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(power_controller_of_match_table),
		.pm = &simple_pm_ops,
		.sync_state = power_controller_sync_state,
	},
};

module_platform_driver(power_controller_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("power controller driver");
MODULE_LICENSE("GPL");
