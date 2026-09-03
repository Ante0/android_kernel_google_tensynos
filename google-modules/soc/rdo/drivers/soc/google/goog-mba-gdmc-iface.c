// SPDX-License-Identifier: GPL-2.0-only
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/trace.h>
#include <linux/workqueue.h>

#include <soc/google/goog-mba-aggr.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_gdmc_regdump_service.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog_mba_nq_xport.h>

#include "goog-mba-gdmc-iface.h"

#define CREATE_TRACE_POINTS
#include "goog-mba-gdmc-iface-trace.h"

static inline bool goog_mba_gdmc_buf_is_response(void *payload)
{
	return goog_mba_nq_xport_get_response(payload);
}

static inline bool goog_mba_gdmc_buf_is_request(void *payload)
{
	return !goog_mba_nq_xport_get_response(payload);
}

static inline bool goog_mba_gdmc_buf_is_error(void *payload)
{
	return goog_mba_nq_xport_get_error(payload);
}

static struct goog_mba_gdmc_service_handler *gdmc_get_service_handler(struct gdmc_iface *gdmc_iface,
								      int service_id)
{
	int index;
	struct goog_mba_gdmc_service_handler *client_service_hdl;

	if (!goog_mba_gdmc_is_valid_service_id(service_id))
		return NULL;

	index = service_id - APC_CRITICAL_GDMC_MAILBOX_SERVICE_LAST;
	client_service_hdl = &gdmc_iface->host_service_hdls[index];

	return client_service_hdl;
}

int gdmc_register_host_cb(struct gdmc_iface *gdmc_iface, int service_id,
			  gdmc_host_cb_t host_cb, void *priv_data)
{
	struct goog_mba_gdmc_service_handler *client_service_hdl;


	client_service_hdl = gdmc_get_service_handler(gdmc_iface, service_id);
	if (!client_service_hdl)
		return -EINVAL;
	else if (client_service_hdl->host_cb)
		return -EBUSY;

	client_service_hdl->host_cb = host_cb;
	client_service_hdl->priv_data = priv_data;

	trace_gdmc_register_host_cb(service_id);

	return 0;
}
EXPORT_SYMBOL_GPL(gdmc_register_host_cb);

void gdmc_unregister_host_cb(struct gdmc_iface *gdmc_iface, int service_id)
{
	struct goog_mba_gdmc_service_handler *client_service_hdl;

	if (!goog_mba_gdmc_is_valid_service_id(service_id))
		return;

	client_service_hdl = gdmc_get_service_handler(gdmc_iface, service_id);

	client_service_hdl->host_cb = NULL;
	client_service_hdl->priv_data = NULL;
}
EXPORT_SYMBOL_GPL(gdmc_unregister_host_cb);

static int goog_mba_gdmc_iface_send_message(struct gdmc_iface *gdmc_iface, bool crit, u32 *req_buf,
					    u32 *resp_buf, gdmc_async_resp_cb_t async_resp_cb,
					    unsigned long tout_ms, void *prv_data)
{
	struct device *dev = gdmc_iface->dev;
	struct goog_mba_aggr_service *gservice;
	u32 service_id;
	int ret;
	struct goog_mba_aggr_request client_req = {
		.req_buf        = req_buf,
		.resp_buf       = resp_buf,
		/* TODO: type casting is not elegant, handle it better */
		.async_resp_cb  = (goog_mbox_aggr_resp_cb_t)async_resp_cb,
		.tout_ms        = tout_ms,
		.prv_data       = prv_data,
		.oneway         = goog_mba_nq_xport_get_oneway(req_buf),
	};

	if (async_resp_cb)
		client_req.async = true;

	if (goog_mba_gdmc_buf_is_error(req_buf) || !goog_mba_gdmc_buf_is_request(req_buf))
		return -EINVAL;

	trace_goog_mba_gdmc_iface_send_message(crit, &client_req);

	service_id = goog_mba_nq_xport_get_service_id(req_buf);
	gservice = crit ? gdmc_iface->crit_service : gdmc_iface->normal_service;
	ret = goog_mba_aggr_send_message(gservice, &client_req);
	if (ret < 0 || client_req.async)
		return ret;

	/* if client specify oneway request, @req_buf won't be update and don't need to check */
	if (!client_req.oneway) {
		if (goog_mba_gdmc_buf_is_error(resp_buf))
			return -EBADMSG;
		if (!goog_mba_nq_xport_get_response(resp_buf))
			return -EBADRQC;
		if (goog_mba_nq_xport_get_service_id(resp_buf) != service_id) {
			dev_err(dev, "Error: service id mismatch\n");
			return -ENOMSG;
		}
	}

	return 0;
}

int gdmc_send_message(struct gdmc_iface *gdmc_iface, void *msg)
{
	return goog_mba_gdmc_iface_send_message(gdmc_iface, false, msg, msg, NULL, 0, NULL);
}
EXPORT_SYMBOL_GPL(gdmc_send_message);

int gdmc_send_message_async(struct gdmc_iface *gdmc_iface,
			    void *msg,
			    gdmc_async_resp_cb_t resp_cb,
			    void *priv_data)
{
	return goog_mba_gdmc_iface_send_message(gdmc_iface, false, msg, msg,
						resp_cb, 0, priv_data);
}
EXPORT_SYMBOL_GPL(gdmc_send_message_async);

int gdmc_send_message_critical(struct gdmc_iface *gdmc_iface, void *msg)
{
	return goog_mba_gdmc_iface_send_message(gdmc_iface, true, msg, msg, NULL, 0, NULL);
}
EXPORT_SYMBOL_GPL(gdmc_send_message_critical);

int gdmc_send_message_critical_async(struct gdmc_iface *gdmc_iface,
				     void *msg,
				     gdmc_async_resp_cb_t resp_cb,
				     void *priv_data)
{
	return goog_mba_gdmc_iface_send_message(gdmc_iface, true, msg, msg,
						resp_cb, 0, priv_data);
}
EXPORT_SYMBOL_GPL(gdmc_send_message_critical_async);

static int gdmc_dhub_uart_mux_get(struct gdmc_dhub_iface *dhub_iface, u32 *uart_num)
{
	struct gdmc_iface *gdmc_iface = container_of(dhub_iface, struct gdmc_iface, dhub_iface);
	struct device *dev = gdmc_iface->dev;
	u32 payload[GDMC_MBA_DHUB_PAYLOAD_SIZE] = { 0 };
	int ret;
	u32 err;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_DHUB);
	goog_mba_nq_xport_set_data(payload, GDMC_MBA_DHUB_CMD_UART_MUX_GET);

	ret = gdmc_send_message(gdmc_iface, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	err = goog_mba_nq_xport_get_error(payload);
	if (err) {
		dev_err(dev, "Response msg with error %u\n", err);
		return -EINVAL;
	}

	*uart_num = payload[1];

	return 0;
}

static int gdmc_dhub_uart_mux_set(struct gdmc_dhub_iface *dhub_iface, u32 uart_num)
{
	struct gdmc_iface *gdmc_iface = container_of(dhub_iface, struct gdmc_iface, dhub_iface);
	struct device *dev = gdmc_iface->dev;
	u32 payload[GDMC_MBA_DHUB_PAYLOAD_SIZE] = { 0 };
	int ret;
	u32 err;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_DHUB);
	goog_mba_nq_xport_set_data(payload, GDMC_MBA_DHUB_CMD_UART_MUX_SET);

	payload[1] = uart_num;

	ret = gdmc_send_message(gdmc_iface, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	err = goog_mba_nq_xport_get_error(payload);
	if (err) {
		dev_err(dev, "Response msg with error %u\n", err);
		return -EINVAL;
	}

	return 0;
}

static int gdmc_dhub_uart_baudrate_get(struct gdmc_dhub_iface *dhub_iface,
				 u32 uart_num, u32 *baudrate)
{
	struct gdmc_iface *gdmc_iface = container_of(dhub_iface, struct gdmc_iface, dhub_iface);
	struct device *dev = gdmc_iface->dev;
	u32 payload[GDMC_MBA_DHUB_PAYLOAD_SIZE] = { 0 };
	int ret;
	u32 err;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_DHUB);
	goog_mba_nq_xport_set_data(payload, GDMC_MBA_DHUB_CMD_UART_BAUDRATE_GET);

	payload[1] = uart_num;

	ret = gdmc_send_message(gdmc_iface, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	err = goog_mba_nq_xport_get_error(payload);
	if (err) {
		dev_err(dev, "Response msg with error %u\n", err);
		return -EINVAL;
	}

	*baudrate = payload[2];

	return 0;
}

static int gdmc_dhub_uart_baudrate_set(struct gdmc_dhub_iface *dhub_iface,
				 u32 uart_num, u32 baudrate)
{
	struct gdmc_iface *gdmc_iface = container_of(dhub_iface, struct gdmc_iface, dhub_iface);
	struct device *dev = gdmc_iface->dev;
	u32 payload[GDMC_MBA_DHUB_PAYLOAD_SIZE] = { 0 };
	int ret;
	u32 err;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_DHUB);
	goog_mba_nq_xport_set_data(payload, GDMC_MBA_DHUB_CMD_UART_BAUDRATE_SET);

	payload[1] = uart_num;
	payload[2] = baudrate;

	ret = gdmc_send_message(gdmc_iface, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	err = goog_mba_nq_xport_get_error(payload);
	if (err) {
		dev_err(dev, "Response msg with error %u\n", err);
		return -EINVAL;
	}

	return 0;
}

static int gdmc_dhub_virt_en_get(struct gdmc_dhub_iface *dhub_iface, u32 *mask)
{
	struct gdmc_iface *gdmc_iface = container_of(dhub_iface, struct gdmc_iface, dhub_iface);
	struct device *dev = gdmc_iface->dev;
	u32 payload[GDMC_MBA_DHUB_PAYLOAD_SIZE] = { 0 };
	int ret;
	u32 err;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_DHUB);
	goog_mba_nq_xport_set_data(payload, GDMC_MBA_DHUB_CMD_VIRTUALIZED_UARTS_GET);

	ret = gdmc_send_message(gdmc_iface, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	err = goog_mba_nq_xport_get_error(payload);
	if (err) {
		dev_err(dev, "Response msg with error %u\n", err);
		return -EINVAL;
	}

	*mask = payload[1];

	return 0;
}

static int gdmc_dhub_virt_en_set(struct gdmc_dhub_iface *dhub_iface, u32 mask)
{
	struct gdmc_iface *gdmc_iface = container_of(dhub_iface, struct gdmc_iface, dhub_iface);
	struct device *dev = gdmc_iface->dev;
	u32 payload[GDMC_MBA_DHUB_PAYLOAD_SIZE] = { 0 };
	int ret;
	u32 err;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_DHUB);
	goog_mba_nq_xport_set_data(payload, GDMC_MBA_DHUB_CMD_VIRTUALIZED_UARTS_SET);

	payload[1] = mask;

	ret = gdmc_send_message(gdmc_iface, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	err = goog_mba_nq_xport_get_error(payload);
	if (err) {
		dev_err(dev, "Response msg with error %u\n", err);
		return -EINVAL;
	}

	return 0;
}

int gdmc_ping(struct gdmc_iface *gdmc_iface)
{
	struct device *dev = gdmc_iface->dev;
	u32 data[4];
	u32 *payload = data;
	int ret;
	const u32 ping_value = 0x1234 & GOOG_MBA_NQ_XPORT_DATA_MASK;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_PING);
	goog_mba_nq_xport_set_oneway(payload, false);
	goog_mba_nq_xport_set_data(payload, ping_value);

	ret = gdmc_send_message(gdmc_iface, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	if (ping_value + 1 == goog_mba_nq_xport_get_data(payload))
		return 0;
	else
		return -1;
}
EXPORT_SYMBOL_GPL(gdmc_ping);

static void gdmc_ehld_timer_cb(void *resp_msg, void *priv_data)
{
	struct device *dev = priv_data;
	uint32_t *buf = resp_msg;
	int cmd, error;

	cmd = goog_mba_nq_xport_get_data(buf);
	error = goog_mba_nq_xport_get_error(buf);

	if (error) {
		dev_err(dev,
			"Error using service id %d, Failed to %s periodic timer in GDMC(error:%d)\n",
			GDMC_MBA_SERVICE_ID_EHLD,
			(cmd == GDMC_MBA_EHLD_CMD_TIMER_ENABLE) ? "Enable" : "Disable",
			error);
	} else {
		dev_dbg(dev,
			"GDMC periodic timer %s\n",
			(cmd == GDMC_MBA_EHLD_CMD_TIMER_ENABLE) ? "Enabled" : "Disabled");
	}
}

int gdmc_ehld_config(struct gdmc_iface *gdmc_iface, int cmd, void *msg, struct device *dev)
{
	u32 payload[4] = { 0 };
	int ret;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_EHLD);
	goog_mba_nq_xport_set_oneway(payload, true);
	goog_mba_nq_xport_set_data(payload, cmd);

	if (msg) {
		u32 *msg_data = msg;
		payload[1] = msg_data[0];
		payload[2] = msg_data[1];
	}

	ret = gdmc_send_message_async(gdmc_iface, payload, gdmc_ehld_timer_cb, dev);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gdmc_ehld_config);

int gdmc_reboot_with_reason(struct gdmc_iface *gdmc_iface, int cmd, void *msg, struct device *dev)
{
	u32 payload[GDMC_MBA_DHUB_PAYLOAD_SIZE] = { 0 };
	int ret;

	goog_mba_nq_xport_set_service_id(payload, GDMC_MBA_SERVICE_ID_REBOOT);
	goog_mba_nq_xport_set_oneway(payload, true);
	goog_mba_nq_xport_set_data(payload, cmd);

	if (msg) {
		payload[1] = *(u32 *)msg;
	}

	ret = gdmc_send_message(gdmc_iface, payload);
	if (ret < 0) {
		dev_err(dev, "Failed to gdmc_send_message(ret:%d)\n", ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gdmc_reboot_with_reason);

static void goog_mba_gdmc_host_tx_cb_handler(void *resp_buf, void *prv_data)
{
	struct gdmc_iface *gdmc_iface = prv_data;
	struct device *dev = gdmc_iface->dev;
	struct goog_mba_gdmc_service_handler *service_hdl;
	u32 service_id;

	if (goog_mba_gdmc_buf_is_error(resp_buf) || !goog_mba_gdmc_buf_is_request(resp_buf))
		return;

	service_id = goog_mba_nq_xport_get_service_id(resp_buf);

	service_hdl = gdmc_get_service_handler(gdmc_iface, service_id);
	if (service_hdl && service_hdl->host_cb) {
		service_hdl->host_cb(resp_buf, service_hdl->priv_data);
	} else {
		dev_warn(dev, "No handler for GDMC host tx with service_id=%u", service_id);
	}

	trace_goog_mba_gdmc_host_tx_cb_handler(resp_buf, !!(service_hdl));
}

static void goog_mba_gdmc_aoc_reset_handler(struct work_struct *work)
{
	struct gdmc_aoc_notify *gdmc_notify =
			container_of(work, struct gdmc_aoc_notify, work);
	struct gdmc_iface *gdmc_iface = gdmc_notify->gdmc_iface;
	struct gdmc_mba_cmd_get_regdump_msg *gdmc_regdump_msg;
	void *shared_buf = NULL;
	unsigned int shared_buf_size = 0;
	u32 payload[4];

	gdmc_regdump_msg = (struct gdmc_mba_cmd_get_regdump_msg *)payload;
	gdmc_regdump_msg->header = goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_GET_REGDUMP,
								GDMC_MBA_REGDUMP_ID_AOC);
	gdmc_regdump_msg->pa_low = (u32)gdmc_notify->shared_buf_phys_addr;
	gdmc_regdump_msg->pa_high = (u32)(gdmc_notify->shared_buf_phys_addr >> 32);
	gdmc_regdump_msg->size = gdmc_notify->shared_buf_size;

	if (gdmc_send_message(gdmc_iface, payload) == 0) {
		shared_buf = gdmc_notify->shared_buf;
		shared_buf_size = gdmc_notify->shared_buf_size;
	}
	gdmc_notify->aoc_reset_fn(shared_buf, shared_buf_size, gdmc_notify->client_prv_data);
}

static void goog_mba_gdmc_aoc_reset_cb(void *msg, void *priv_data)
{
	struct gdmc_aoc_notify *gdmc_notify = priv_data;

	schedule_work(&gdmc_notify->work);
}

void gdmc_unregister_aoc_reset_notifier(struct gdmc_iface *gdmc_iface)
{
	struct device *dev = gdmc_iface->dev;
	struct goog_mba_gdmc_service_handler *service_handler;
	struct gdmc_aoc_notify *gdmc_notify;
	const int service_id = APC_CRITICAL_GDMC_AOC_WATCHDOG_SERVICE;

	service_handler = gdmc_get_service_handler(gdmc_iface, service_id);
	if (!service_handler || !service_handler->host_cb)
		return;

	gdmc_notify = service_handler->priv_data;
	gdmc_unregister_host_cb(gdmc_iface, service_id);
	dmam_free_coherent(dev, gdmc_notify->shared_buf_size, gdmc_notify->shared_buf,
			   gdmc_notify->shared_buf_phys_addr);
	devm_kfree(dev, gdmc_notify);
}
EXPORT_SYMBOL_GPL(gdmc_unregister_aoc_reset_notifier);

int gdmc_register_aoc_reset_notifier(struct gdmc_iface *gdmc_iface,
				     gdmc_aoc_reset_cb_t aoc_reset_cb,
				     void *prv_data)
{
	struct device *dev = gdmc_iface->dev;
	struct gdmc_aoc_notify *gdmc_notify;
	struct gdmc_mba_aarch32_register_dump *regdump_data;
	dma_addr_t dma_phys_addr;
	int ret;

	gdmc_notify = devm_kzalloc(dev, sizeof(*gdmc_notify), GFP_KERNEL);
	if (!gdmc_notify)
		return -ENOMEM;

	regdump_data = dmam_alloc_coherent(dev, sizeof(*regdump_data), &dma_phys_addr, GFP_KERNEL);
	if (!regdump_data || !dma_phys_addr) {
		ret = -ENOMEM;
		goto exit;
	}

	gdmc_notify->gdmc_iface = gdmc_iface;
	gdmc_notify->aoc_reset_fn = aoc_reset_cb;
	gdmc_notify->client_prv_data = prv_data;
	INIT_WORK(&gdmc_notify->work, goog_mba_gdmc_aoc_reset_handler);
	gdmc_notify->shared_buf_phys_addr = dma_phys_addr;
	gdmc_notify->shared_buf = regdump_data;
	gdmc_notify->shared_buf_size = sizeof(*regdump_data);

	ret = gdmc_register_host_cb(gdmc_iface, APC_CRITICAL_GDMC_AOC_WATCHDOG_SERVICE,
				    goog_mba_gdmc_aoc_reset_cb, gdmc_notify);
	if (ret < 0)
		goto fail_registration;

	return 0;

fail_registration:
	dmam_free_coherent(dev, sizeof(*regdump_data), regdump_data, dma_phys_addr);
exit:
	devm_kfree(dev, gdmc_notify);

	return ret;
}
EXPORT_SYMBOL_GPL(gdmc_register_aoc_reset_notifier);

static void goog_mba_gdmc_dpa_reset_handler(struct work_struct *work)
{
	struct gdmc_dpa_notify *gdmc_notify =
			container_of(work, struct gdmc_dpa_notify, work);
	struct gdmc_iface *gdmc_iface = gdmc_notify->gdmc_iface;
	struct gdmc_mba_cmd_get_regdump_msg *gdmc_regdump_msg;
	void *shared_buf = NULL;
	unsigned int shared_buf_size = 0;
	u32 payload[4];

	gdmc_regdump_msg = (struct gdmc_mba_cmd_get_regdump_msg *)payload;
	gdmc_regdump_msg->header = goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_GET_REGDUMP,
								GDMC_MBA_REGDUMP_ID_DPA);
	gdmc_regdump_msg->pa_low = (u32)gdmc_notify->shared_buf_phys_addr;
	gdmc_regdump_msg->pa_high = (u32)(gdmc_notify->shared_buf_phys_addr >> 32);
	gdmc_regdump_msg->size = gdmc_notify->shared_buf_size;

	if (gdmc_send_message(gdmc_iface, payload) == 0) {
		shared_buf = gdmc_notify->shared_buf;
		shared_buf_size = gdmc_notify->shared_buf_size;
	}
	gdmc_notify->dpa_reset_fn(shared_buf, shared_buf_size, gdmc_notify->client_prv_data);
}

static void goog_mba_gdmc_dpa_reset_cb(void *msg, void *priv_data)
{
	struct gdmc_dpa_notify *gdmc_notify = priv_data;

	schedule_work(&gdmc_notify->work);
}

void gdmc_unregister_dpa_reset_notifier(struct gdmc_iface *gdmc_iface)
{
	struct device *dev = gdmc_iface->dev;
	struct goog_mba_gdmc_service_handler *service_handler;
	struct gdmc_dpa_notify *gdmc_notify;
	const int service_id = APC_CRITICAL_GDMC_DPA_WATCHDOG_SERVICE;

	service_handler = gdmc_get_service_handler(gdmc_iface, service_id);
	if (!service_handler || !service_handler->host_cb)
		return;

	gdmc_notify = service_handler->priv_data;
	gdmc_unregister_host_cb(gdmc_iface, service_id);
	dmam_free_coherent(dev, sizeof(*gdmc_notify->shared_buf), gdmc_notify->shared_buf,
			   gdmc_notify->shared_buf_phys_addr);
	devm_kfree(dev, gdmc_notify);
}
EXPORT_SYMBOL_GPL(gdmc_unregister_dpa_reset_notifier);

int gdmc_register_dpa_reset_notifier(struct gdmc_iface *gdmc_iface,
				     gdmc_dpa_reset_cb_t dpa_reset_cb,
				     void *prv_data)
{
	struct device *dev = gdmc_iface->dev;
	struct gdmc_dpa_notify *gdmc_notify;
	struct gdmc_mba_dpa_register_dump *regdump_data;
	dma_addr_t dma_phys_addr;
	int ret;

	gdmc_notify = devm_kzalloc(dev, sizeof(*gdmc_notify), GFP_KERNEL);
	if (!gdmc_notify)
		return -ENOMEM;

	regdump_data = dmam_alloc_coherent(dev, sizeof(*regdump_data), &dma_phys_addr, GFP_KERNEL);
	if (!regdump_data || !dma_phys_addr) {
		ret = -ENOMEM;
		goto exit;
	}

	gdmc_notify->gdmc_iface = gdmc_iface;
	gdmc_notify->dpa_reset_fn = dpa_reset_cb;
	gdmc_notify->client_prv_data = prv_data;
	INIT_WORK(&gdmc_notify->work, goog_mba_gdmc_dpa_reset_handler);
	gdmc_notify->shared_buf_phys_addr = dma_phys_addr;
	gdmc_notify->shared_buf = regdump_data;
	gdmc_notify->shared_buf_size = sizeof(*regdump_data);

	ret = gdmc_register_host_cb(gdmc_iface, APC_CRITICAL_GDMC_DPA_WATCHDOG_SERVICE,
				    goog_mba_gdmc_dpa_reset_cb, gdmc_notify);
	if (ret < 0)
		goto fail_registration;

	return 0;

fail_registration:
	dmam_free_coherent(dev, sizeof(*regdump_data), regdump_data, dma_phys_addr);
exit:
	devm_kfree(dev, gdmc_notify);

	return ret;
}
EXPORT_SYMBOL_GPL(gdmc_register_dpa_reset_notifier);

struct gdmc_iface *gdmc_iface_get(struct device *dev)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct gdmc_iface *gdmc_iface;

	if (!dev->of_node) {
		dev_dbg(dev, "device does not have a device node entry\n");
		return ERR_PTR(-ENODEV);
	}

	np = of_parse_phandle(dev->of_node, "gdmc-iface", 0);
	if (!np) {
		dev_dbg(dev, "failed to parse 'gdmc-iface' phandle property\n");
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	gdmc_iface = platform_get_drvdata(pdev);
	if (!gdmc_iface) {
		platform_device_put(pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	return gdmc_iface;
}
EXPORT_SYMBOL_GPL(gdmc_iface_get);

void gdmc_iface_put(struct gdmc_iface *gdmc_iface)
{
	put_device(gdmc_iface->dev);
}
EXPORT_SYMBOL_GPL(gdmc_iface_put);

struct gdmc_dhub_iface *dhub_iface_get(struct device *dev)
{
	struct gdmc_iface *gdmc_iface = gdmc_iface_get(dev);

	if (IS_ERR(gdmc_iface))
		return ERR_CAST(gdmc_iface);

	return &gdmc_iface->dhub_iface;
}
EXPORT_SYMBOL_GPL(dhub_iface_get);

void dhub_iface_put(struct gdmc_dhub_iface *dhub_iface)
{
	struct gdmc_iface *gdmc_iface = container_of(dhub_iface, struct gdmc_iface, dhub_iface);

	gdmc_iface_put(gdmc_iface);
}
EXPORT_SYMBOL_GPL(dhub_iface_put);

const char *gdmc_iface_default_events[] = {
	"gdmc_register_host_cb",
	"gdmc_unregister_host_cb",
	"goog_mba_gdmc_host_tx_cb_handler",
	"goog_mba_gdmc_iface_send_message",
	"goog_mba_ctrl_process_nq_rx",
	"goog_mba_ctrl_process_nq_txdone",
	"goog_mba_ctrl_send_data_nq",
	"goog_mba_aggr_client_rx_cb",
	"goog_mba_aggr_client_tx_done",
	"goog_mba_aggr_host_rx_done",
	"goog_mba_aggr_host_tx_cb",
	"goog_mba_aggr_queue_req",
	"goog_mba_aggr_send_message",
	"goog_mba_aggr_submit",
};

static void goog_mba_gdmc_create_ftrace_instance(struct gdmc_iface *gdmc_iface)
{
	struct device *dev = gdmc_iface->dev;
	struct trace_array *tr;
	int i;
	int ret;

	tr = trace_array_get_by_name("goog_nq_mailbox",
				     "goog_mba_ctrl,goog_mba_aggr,goog_mba_gdmc_iface");
	if (!tr) {
		dev_err(dev, "Failed to create trace array\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(gdmc_iface_default_events); i++) {
		ret = trace_array_set_clr_event(tr, NULL, gdmc_iface_default_events[i], true);
		if (ret < 0)
			dev_warn(dev, "Event:%s not present\n", gdmc_iface_default_events[i]);
	}

	trace_array_put(tr);
}

static int goog_mba_gdmc_iface_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct goog_mba_aggr_service *gservice;
	struct gdmc_iface *gdmc_iface;
	int i, ret;

	gdmc_iface = devm_kzalloc(dev, sizeof(*gdmc_iface), GFP_KERNEL);
	if (!gdmc_iface)
		return -ENOMEM;

	gdmc_iface->dev = dev;
	for (i = 0; i < GOOG_MBA_GDMC_PRIOS_MAX; i++) {
		gservice = goog_mba_request_gservice(dev, i,
						     goog_mba_gdmc_host_tx_cb_handler,
						     gdmc_iface);

		if (IS_ERR(gservice)) {
			ret = PTR_ERR(gservice);
			if (ret == -EPROBE_DEFER)
				dev_dbg(dev, "GDMC aggregator is not ready. Probe later. (ret: %d)",
					ret);
			else
				dev_err(dev, "Failed to request service(ret:%ld)\n",
					PTR_ERR(gservice));
			return ret;
		}

		if (i == GOOG_MBA_GDMC_NORMAL_PRIO)
			gdmc_iface->normal_service = gservice;
		else if (i == GOOG_MBA_GDMC_CRITICAL_PRIO)
			gdmc_iface->crit_service = gservice;
	}

	ret = of_reserved_mem_device_init(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to init device reserved memory\n");
		return ret;
	}

	goog_mba_gdmc_create_ftrace_instance(gdmc_iface);

	gdmc_iface->dhub_iface.dhub_mux_get = gdmc_dhub_uart_mux_get;
	gdmc_iface->dhub_iface.dhub_mux_set = gdmc_dhub_uart_mux_set;
	gdmc_iface->dhub_iface.dhub_baudrate_get = gdmc_dhub_uart_baudrate_get;
	gdmc_iface->dhub_iface.dhub_baudrate_set = gdmc_dhub_uart_baudrate_set;
	gdmc_iface->dhub_iface.dhub_virt_en_get = gdmc_dhub_virt_en_get;
	gdmc_iface->dhub_iface.dhub_virt_en_set = gdmc_dhub_virt_en_set;

	platform_set_drvdata(pdev, gdmc_iface);

	return 0;
}

static const struct of_device_id goog_mba_gdmc_iface_of_match_table[] = {
	{ .compatible = "google,mba-gdmc-iface" },
	{},
};
MODULE_DEVICE_TABLE(of, goog_mba_gdmc_iface_of_match_table);

struct platform_driver goog_mba_gdmc_iface_driver = {
	.probe = goog_mba_gdmc_iface_probe,
	.driver = {
		.name = "goog_mba_gdmc_iface",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goog_mba_gdmc_iface_of_match_table),
	},
};

module_platform_driver(goog_mba_gdmc_iface_driver);

MODULE_DESCRIPTION("Google GDMC MBA Interface");
MODULE_AUTHOR("Lucas Wei <lucaswei@google.com>");
MODULE_LICENSE("GPL");
