/* SPDX-License-Identifier: GPL-2.0 */

#pragma once


#include <linux/completion.h>
#include <pvrsrvkm/services/server/include/pvrsrv_device.h>
#include <pvrsrvkm/services/include/physheap_config.h>

struct pixel_gpu_device;
struct tipc_chan;


/**
 * Private state for connection to gpu_secure trusty app.
 */
struct pixel_gpu_secure {
	/* The firmware carveout physical address */
	phys_addr_t carveout_base;

	/* The firmware carveout size in bytes */
	size_t carveout_size;

	/* A completion when connection completes or a response is received */
	struct completion done;

	/* A connected TIPC channel */
	struct tipc_chan *chan;

	/* request command */
	uint32_t command;

	/* The result indicated in the current response */
	PVRSRV_ERROR result;

	/**
	 * Protect gpu_secure_call()
	 */
	struct mutex ipc_lock;
};


/**
 * Connect to the gpu_secure trusty app.
 */
PVRSRV_ERROR gpu_secure_init(struct pixel_gpu_device *pixel_dev);

/**
 * Disconnect from the gpu_secure trusty app.
 */
void gpu_secure_term(struct pixel_gpu_device *pixel_dev);

/**
 * Prepare a GPU firmware image immediately after it is loaded, and before
 * it is parsed by the upstream driver.  This will separate the authentication
 * header from the firmware image proper.
 */
PVRSRV_ERROR gpu_secure_prepare_firmware_image(IMG_HANDLE hSysData,
	PVRSRV_FW_PARAMS *psFWParams);

/**
 * Send the GPU firmware image to the gpu_secure trusty app.
 */
PVRSRV_ERROR gpu_secure_send_firmware_image(IMG_HANDLE hSysData,
	PVRSRV_FW_PARAMS *psFWParams);

/**
 * Send power parameters to the gpu_secure trusty app.
 */
PVRSRV_ERROR gpu_secure_set_power_params(IMG_HANDLE hSysData,
	PVRSRV_TD_POWER_PARAMS *psTDPowerParams);

/**
 * Have the gpu_secure trusty app call RGXStart().
 */
PVRSRV_ERROR gpu_secure_start(IMG_HANDLE hSysData);

/**
 * Have the gpu_secure trusty app call RGXStop().
 */
PVRSRV_ERROR gpu_secure_stop(IMG_HANDLE hSysData);

/**
 * Have the gpu_secure trusty app Dump TEE specific register debug info
 */
void gpu_secure_fault(IMG_HANDLE hSysData);
