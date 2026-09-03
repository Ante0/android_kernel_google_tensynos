#include "sys_if_memory.h"

#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/device.h>
#include "noa_desc.h"
#include "wlan_log/wlan_log.h"

extern struct device *ncp_wlan_get_wlan_client_device(void);

void SysIfInvalidDCache(PhyAddr addr, size_t size)
{
}

void SysIfFlushDCache(PhyAddr addr, size_t size)
{
}

void SysIfDmaSyncForCpu(PhyAddr addr, size_t size)
{
	struct device *client_dev = ncp_wlan_get_wlan_client_device();

	if (client_dev) {
		dma_sync_single_for_cpu(client_dev, (dma_addr_t)addr, size, DMA_FROM_DEVICE);
	} else {
		WLAN_LOG_WARN(Cfg, "%s(): Invalid WLAN client device.", __func__);
	}
}

void SysIfDmaSyncForDevice(PhyAddr addr, size_t size)
{
	struct device *client_dev = ncp_wlan_get_wlan_client_device();

	if (client_dev) {
		dma_sync_single_for_device(client_dev, (dma_addr_t)addr, size, DMA_TO_DEVICE);
	} else {
		WLAN_LOG_WARN(Cfg, "%s(): Invalid WLAN client device.", __func__);
	}
}

void *SysIfAllocateDram(size_t size)
{
	return kzalloc(size, GFP_KERNEL);
}

void SysIfFree(void *ptr)
{
	kfree(ptr);
}
