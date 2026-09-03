// SPDX-License-Identifier: GPL-2.0-only
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <soc/google/goog-mba-aggr.h>
#include <soc/google/goog-mba-ebu-iface.h>
#include <soc/google/goog_ebu_service_ids.h>
#include <soc/google/goog_mba_nq_xport.h>

#include "goog-mba-ebu-iface.h"

static inline bool goog_mba_ebu_buf_is_response(void *payload)
{
	return goog_mba_nq_xport_get_response(payload);
}

static inline bool goog_mba_ebu_buf_is_request(void *payload)
{
	return !goog_mba_nq_xport_get_response(payload);
}

static inline bool goog_mba_ebu_buf_is_error(void *payload)
{
	return goog_mba_nq_xport_get_error(payload);
}

static inline bool is_valid_service_id(int service_id)
{
	return (service_id > 0) && (service_id < EBU_NUM_MBA_SERVICES);
}

/*
 * Assumption: Validity of the arguments have already been checked, and the
 * power domain for the underlying MBA is active.
 */
static int goog_mba_ebu_iface_send_message(struct ebu_iface *ebu_iface,
					   struct ebu_iface_message *message,
					   unsigned long tout_ms)
{
	struct device *dev = ebu_iface->dev;
	int service_id = message->dst_service_id;
	struct ebu_iface_payload *req = message->request;
	struct ebu_iface_payload *resp = message->response;
	int ret = 0;
	struct goog_mba_aggr_request client_req = {
		.req_buf        = req,
		.resp_buf       = resp,
		.oneway         = goog_mba_nq_xport_get_oneway(&req->header),
		.async		= false,
		.async_resp_cb  = NULL,
		.tout_ms        = tout_ms,
		.prv_data       = NULL,
	};

	ret = goog_mba_aggr_send_message(ebu_iface->normal_service, &client_req);
	if (ret < 0)
		return ret;

	if (goog_mba_ebu_buf_is_error(&resp->header) ||
	    !goog_mba_ebu_buf_is_response(&resp->header))
		return -EBADMSG;

	if (goog_mba_nq_xport_get_service_id(&resp->header) != service_id) {
		dev_err(dev, "Error: service id mismatch\n");
		return -EBADMSG;
	}

	return ret;
}

int ebu_send_request(struct ebu_iface *ebu_iface, struct ebu_iface_message *message)
{
	u32 *header = NULL;
	int ret = 0;

	if (!ebu_iface || !message || !message->request || !message->response ||
	    !is_valid_service_id(message->dst_service_id))
		return -EINVAL;

	header = &message->request->header;
	goog_mba_nq_xport_set_service_id(header, message->dst_service_id);
	goog_mba_nq_xport_set_response(header, false);
	goog_mba_nq_xport_set_error(header, false);
	goog_mba_nq_xport_set_oneway(header, false);

	pm_runtime_get_sync(ebu_iface->dev);
	ret = goog_mba_ebu_iface_send_message(ebu_iface, message, 0);
	__pm_runtime_put_autosuspend(ebu_iface->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(ebu_send_request);

int ebu_ping(struct ebu_iface *ebu_iface)
{
	struct device *dev = ebu_iface->dev;
	int ret = 0;
	const u32 ping_value = 0x1234 & GOOG_MBA_NQ_XPORT_DATA_MASK;
	struct ebu_iface_payload request = {0,};
	struct ebu_iface_payload response = {0,};
	struct ebu_iface_message message = {
		.dst_service_id = EBU_MBA_SERVICE_ID_PING,
		.request = &request,
		.response = &response,
	};

	/*
	 * It is not recommended to embed data into the header or directly
	 * touch the header. However, the ping service on the remote is
	 * implemented that way.
	 */
	goog_mba_nq_xport_set_data(&request.header, ping_value);

	ret = ebu_send_request(ebu_iface, &message);
	if (ret < 0) {
		dev_err(dev, "Failed to send a request: %d\n", ret);
		return ret;
	}

	if (ping_value + 1 != goog_mba_nq_xport_get_data(&response.header))
		return -EIO;

	return ret;
}
EXPORT_SYMBOL_GPL(ebu_ping);

struct ebu_iface *ebu_iface_get(struct device *dev)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct ebu_iface *ebu_iface;

	if (!dev->of_node) {
		dev_err(dev, "device does not have a device node entry\n");
		return ERR_PTR(-ENODEV);
	}

	np = of_parse_phandle(dev->of_node, "ebu-iface", 0);
	if (!np) {
		dev_err(dev, "failed to parse 'ebu-iface' phandle property\n");
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	ebu_iface = platform_get_drvdata(pdev);
	if (!ebu_iface) {
		platform_device_put(pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	return ebu_iface;
}
EXPORT_SYMBOL_GPL(ebu_iface_get);

void ebu_iface_put(struct ebu_iface *ebu_iface)
{
	put_device(ebu_iface->dev);
}
EXPORT_SYMBOL_GPL(ebu_iface_put);

static int goog_mba_ebu_iface_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct goog_mba_aggr_service *gservice;
	struct ebu_iface *ebu_iface;
	int i, ret;

	ebu_iface = devm_kzalloc(dev, sizeof(*ebu_iface), GFP_KERNEL);
	if (!ebu_iface)
		return -ENOMEM;

	ebu_iface->dev = dev;
	for (i = 0; i < GOOG_MBA_EBU_PRIOS_MAX; i++) {
		gservice = goog_mba_request_gservice(dev, i,
						     NULL,
						     ebu_iface);

		if (IS_ERR(gservice)) {
			ret = PTR_ERR(gservice);
			if (ret == -EPROBE_DEFER)
				dev_dbg(dev, "EBU aggregator is not ready. Probe later. (ret: %d)",
					ret);
			else
				dev_err(dev, "Failed to request service(ret:%ld)\n",
					PTR_ERR(gservice));
			return ret;
		}

		ebu_iface->normal_service = gservice;
	}

	platform_set_drvdata(pdev, ebu_iface);

	return 0;
}

static const struct of_device_id goog_mba_ebu_iface_of_match_table[] = {
	{ .compatible = "google,mba-ebu-iface" },
	{},
};
MODULE_DEVICE_TABLE(of, goog_mba_ebu_iface_of_match_table);

static struct platform_driver goog_mba_ebu_iface_driver = {
	.probe = goog_mba_ebu_iface_probe,
	.driver = {
		.name = "goog_mba_ebu_iface",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goog_mba_ebu_iface_of_match_table),
	},
};

module_platform_driver(goog_mba_ebu_iface_driver);

MODULE_DESCRIPTION("Google EBU MBA Interface");
MODULE_AUTHOR("Google Inc");
MODULE_LICENSE("GPL");
