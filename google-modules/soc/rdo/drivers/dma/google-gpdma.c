// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google SOC General Purpose DMA Driver
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>

#define DMA_REQ_MUX_VAL(source)		(((source) << 1) | 0x1)
#define DMA_REQ_MUX_CHANNEL_OFFSET	0x4
#define DMA_AXUSER_VC_VAL(vc)		(((vc) & 0x7) << 12)
#define DMA_AXUSER_REG_OFFSET		0x8
#define DMA_REQ_MUX_INPUT_MAX		49

/* TX and RX channel */
#define MAX_CHANNEL_TYPES 2

#if IS_ENABLED(CONFIG_DMADEVICES_DEBUG)
#define DEBUG
#endif

#if IS_ENABLED(CONFIG_DMADEVICES_VDEBUG)
#define VERBOSE_DEBUG
#endif

/**
 * @enum: google_gpdma_route_state - SW state of DMA route.
 */
enum google_gpdma_route_state {
	GPDMA_ROUTE_FREE = 0, /* Route not in use */
	GPDMA_ROUTE_IN_USE, /* Route currently in use */
	GPDMA_ROUTE_TO_FREE, /* Route not in use but to be disabled in HW */
};

/**
 * @struct: google_gpdma_chan_route - Defines each route for dma-router.
 * @peripheral_source: Input source index of incoming peripheral dma request.
 * @dma_req_channel: Request Channel of DMA controller
 * @state: Bookmark route state.
 */
struct google_gpdma_chan_route {
	int peripheral_source;
	int dma_req_channel;
	enum google_gpdma_route_state state;
};

/**
 * struct google_gpdma_data - Defines gpdma data members
 * @router_base: Mapped base address of request router.
 * @axuser_base: Mapped base address of AxUser registers.
 * @route_lock: Lock to protect request route allocation.
 * @router: dma-router member.
 * @routes: Pointer to array of all routes
 * @num_routes: Number routes supported by GPDMA
 * @num_channels: Number of channels of DMA Controller
 * @qos_vc: Virtual Channel as per QoS requirement.
 */
struct google_gpdma_data {
	void __iomem *router_base;
	void __iomem *axuser_base;

	/* Lock for atomic route allocation */
	spinlock_t route_lock;

	struct dma_router router;
	struct google_gpdma_chan_route *routes;
	u32 num_routes;
	u32 num_channels;
	u32 qos_vc;
};

static void google_gpdma_program_vc(struct google_gpdma_data *gpdma)
{
	int i;
	/* Iterate for both Aw and Ar User*/
	for (i = 0; i < (gpdma->num_channels * 2); i++)
		writel(DMA_AXUSER_VC_VAL(gpdma->qos_vc),
		       gpdma->axuser_base + DMA_AXUSER_REG_OFFSET * i);
}

static void google_gpdma_req_ack_program(struct google_gpdma_data *gpdma,
					 struct google_gpdma_chan_route *route)
{
	writel(DMA_REQ_MUX_VAL(route->peripheral_source),
	       gpdma->router_base + DMA_REQ_MUX_CHANNEL_OFFSET * route->dma_req_channel);
}

static void google_gpdma_req_ack_disable(struct google_gpdma_data *gpdma,
					 struct google_gpdma_chan_route *route)
{
	writel(0, gpdma->router_base + DMA_REQ_MUX_CHANNEL_OFFSET * route->dma_req_channel);
}

static bool gpdma_runtime_get_if_active(struct device *dev)
{
#ifdef CONFIG_PM
	return pm_runtime_get_if_in_use(dev) > 0;
#else
	return true;
#endif
}

static void google_gpdma_req_ack_free(struct device *dev, void *data)
{
	struct google_gpdma_data *gpdma = dev_get_drvdata(dev);
	struct google_gpdma_chan_route *route = data;
	unsigned long flags;
	bool pm_put = false;

	spin_lock_irqsave(&gpdma->route_lock, flags);

	/*
	 * If SW releases the dma channel when the router is not powered ON
	 * then mark the route to be cleared later on resume.
	 */
	if (gpdma_runtime_get_if_active(dev)) {
		google_gpdma_req_ack_disable(gpdma, route);
		route->state = GPDMA_ROUTE_FREE;
		pm_put = true;
	} else {
		route->state = GPDMA_ROUTE_TO_FREE;
	}
	spin_unlock_irqrestore(&gpdma->route_lock, flags);

	if (pm_put)
		pm_runtime_put(dev);
}

static void *google_gpdma_req_ack_route_allocate(struct of_phandle_args *dma_spec,
						 struct of_dma *ofdma)
{
	struct platform_device *pdev = of_find_device_by_node(ofdma->of_node);
	struct google_gpdma_data *gpdma = platform_get_drvdata(pdev);
	struct google_gpdma_chan_route *route = NULL;
	unsigned long flags;
	int i;

	if (dma_spec->args_count != 2) {
		dev_err(&pdev->dev, "Invalid number of args\n");
		return ERR_PTR(-EINVAL);
	}

	if (dma_spec->args[0] >= MAX_CHANNEL_TYPES) {
		dev_err(&pdev->dev, "Invalid Channel type in args\n");
		return ERR_PTR(-EINVAL);
	}

	if (dma_spec->args[1] >= DMA_REQ_MUX_INPUT_MAX) {
		dev_err(&pdev->dev, "Invalid Peripheral source input in args\n");
		return ERR_PTR(-EINVAL);
	}

	spin_lock_irqsave(&gpdma->route_lock, flags);
	/*
	 * Find free channel.
	 * Tx and Rx requests are connected in pairs where all even channels
	 * are used for DMA_MEM_TO_DEV(TX) and odd channels are used for DMA_DEV_TO_MEM(RX).
	 */
	for (i = dma_spec->args[0]; i < gpdma->num_routes; i += MAX_CHANNEL_TYPES) {
		if (gpdma->routes[i].state != GPDMA_ROUTE_IN_USE) {
			route = &gpdma->routes[i];
			route->peripheral_source = dma_spec->args[1];
			route->dma_req_channel = i;
			route->state = GPDMA_ROUTE_IN_USE;
			break;
		}
	}
	spin_unlock_irqrestore(&gpdma->route_lock, flags);

	if (unlikely(!route))
		return ERR_PTR(-EBUSY);

	/* Update dma spec for dma controller child */
	dma_spec->np = of_get_next_child(ofdma->of_node, NULL);
	dma_spec->args_count = 1;
	dma_spec->args[0] = route->dma_req_channel;

	/*
	 * Program HW if PM active, dma user could request for a channel even when the router is
	 * suspended. There is no need to resume and program the route if it will not be used now.
	 * The same programming will be done while resuming before the actual DMA operation.
	 */
	if (gpdma_runtime_get_if_active(&pdev->dev)) {
		google_gpdma_req_ack_program(gpdma, route);
		pm_runtime_put(&pdev->dev);
	}
	dev_info(&pdev->dev, "Allocated source %d to request %d\n",
		 route->peripheral_source, route->dma_req_channel);
	return route;
}

static int google_gpdma_probe(struct platform_device *pdev)
{
	struct device_node *dma_np, *np = pdev->dev.of_node;
	struct google_gpdma_data *gpdma;
	int ret;

	gpdma = devm_kzalloc(&pdev->dev, sizeof(*gpdma), GFP_KERNEL);
	if (!gpdma)
		return -ENOMEM;

	ret = of_property_read_u32(np, "dma-requests", &gpdma->num_routes);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Can't get dma-requests property\n");

	ret = of_property_read_u32(np, "dma-channels", &gpdma->num_channels);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Can't get dma-channels property\n");

	ret = of_property_read_u32(np, "google,qos-vc", &gpdma->qos_vc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Can't get qos-vc property\n");

	if (of_get_child_count(np) != 1)
		return dev_err_probe(&pdev->dev, -ENODEV, "Supports only 1 Child DMA controller\n");

	dma_np = of_get_compatible_child(np, "arm,pl330");
	if (IS_ERR_OR_NULL(dma_np))
		return dev_err_probe(&pdev->dev, -ENODEV, "Supports only pl330 DMA controller\n");
	of_node_put(dma_np);

	gpdma->router_base = devm_platform_ioremap_resource_byname(pdev, "req_ack_csr");
	if (IS_ERR(gpdma->router_base))
		return dev_err_probe(&pdev->dev, PTR_ERR(gpdma->router_base),
							" Unable to remap req_ack_csr\n");

	gpdma->axuser_base = devm_platform_ioremap_resource_byname(pdev, "axuser_csr");
	if (IS_ERR(gpdma->axuser_base))
		return dev_err_probe(&pdev->dev, PTR_ERR(gpdma->axuser_base),
						" Unable to remap axuser_csr\n");

	gpdma->routes = devm_kzalloc(&pdev->dev,
				     sizeof(*gpdma->routes) * gpdma->num_routes, GFP_KERNEL);
	if (!gpdma->routes)
		return -ENOMEM;

	gpdma->router.dev = &pdev->dev;
	gpdma->router.route_free = google_gpdma_req_ack_free;

	ret = of_dma_router_register(np, google_gpdma_req_ack_route_allocate, &gpdma->router);
	if (ret)
		dev_err_probe(&pdev->dev, ret, "Failed to register DMA router\n");

	platform_set_drvdata(pdev, gpdma);
	spin_lock_init(&gpdma->route_lock);

	google_gpdma_program_vc(gpdma);
	pm_runtime_set_active(&pdev->dev);
	devm_pm_runtime_enable(&pdev->dev);

	return devm_of_platform_populate(&pdev->dev);
}

#ifdef CONFIG_PM
static int google_gpdma_runtime_resume(struct device *dev)
{
	struct google_gpdma_data *gpdma = dev_get_drvdata(dev);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&gpdma->route_lock, flags);

	/* Reprogram Active routes and clear unused routes*/
	for (i = 0; i < gpdma->num_routes; i++) {
		if (gpdma->routes[i].state == GPDMA_ROUTE_IN_USE) {
			google_gpdma_req_ack_program(gpdma, &gpdma->routes[i]);
		} else if (gpdma->routes[i].state == GPDMA_ROUTE_TO_FREE) {
			google_gpdma_req_ack_disable(gpdma, &gpdma->routes[i]);
			gpdma->routes[i].state = GPDMA_ROUTE_FREE;
		}
	}
	spin_unlock_irqrestore(&gpdma->route_lock, flags);
	google_gpdma_program_vc(gpdma);
	return 0;
}
#endif

static const struct dev_pm_ops google_gpdma_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(NULL, google_gpdma_runtime_resume, NULL)
};

static const struct of_device_id google_gpdma_match[] = {
	{ .compatible = "google,gpdma" },
	{},
};
MODULE_DEVICE_TABLE(of, google_gpdma_match);

static struct platform_driver google_gpdma = {
	.probe = google_gpdma_probe,
	.driver = {
		.name = "google-gpdma",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_gpdma_match),
		.pm = &google_gpdma_pm_ops,
	},
};

module_platform_driver(google_gpdma);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("General Purpose DMA");
MODULE_LICENSE("GPL");
