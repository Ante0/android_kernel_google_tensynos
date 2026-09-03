/* SPDX-License-Identifier: GPL-2.0 */

#if defined(SUPPORT_TRUSTED_DEVICE)

/* Linux */
#include <linux/arm-smccc.h>
#include <linux/dev_printk.h>
#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/log2.h>
#include <linux/of.h>
#include <linux/atomic.h>

/* Trusty */
#include <linux/trusty/trusty.h>
#include <linux/trusty/trusty_ipc.h>

/* PowerVR */
#include <pvrsrvkm/services/shared/include/devicemem_utils.h>
#include <pvrsrvkm/services/server/include/pmr.h>
#include <pvrsrvkm/services/server/devices/hal/rgx/rgxfwimageutils.h>
#include <pvrsrvkm/services/server/devices/rgxfwutils.h>
#include <pvrsrvkm/services/server/devices/volcanic/rgxdevice.h>

#include <trace/hooks/systrace.h>

/* Pixel */
#include "gpu_secure.h"
#include "gpu_secure_protocol.h"
#include "sysconfig.h"


/* IPC timeout */
#define GPU_SECURE_TIMEOUT_MS 30000

/* Whether to do START/STOP via TF-A (1) or Trusty (0) */
#define GPU_SECURE_TF_A 1


/**
 * Handle a connection event on the TIPC channel.
 *
 * @data   The pixel_gpu_device.
 * @event  The TIPC event.
 */
static void gpu_secure_on_event(void *data, int event)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)data;
	struct pixel_gpu_secure *self = pixel_dev->gpu_secure;

	/* Map the event to a connection result */
	switch (event) {
	case TIPC_CHANNEL_CONNECTED:
		PVR_DPF((PVR_DBG_CUSTOMER, "IPC channel connected"));
		self->result = PVRSRV_OK;
		break;

	case TIPC_CHANNEL_DISCONNECTED:
		dev_err(pixel_dev->dev, "IPC channel disconnected");
		self->result = PVRSRV_ERROR_SRV_CONNECT_FAILED;
		break;

	default:
		dev_warn(pixel_dev->dev, "Unrecognized IPC event %d", event);
		break;
	}

	/* Wake the originator thread */
	complete(&self->done);
}


/**
 * Handle a message received from the TIPC channel.
 *
 * @data   The pixel_gpu_device.
 * @rxbuf  The received message, ownership passed to us.
 *
 * May return any tipc_msg_buf to recycle, or NULL.
 *
 * This function gets executed as a consequence
 * of a successful gpu_secure_call() protected by the ipc_lock
 * and hence doesn't need to acquire the lock itself.
 */
static struct tipc_msg_buf *gpu_secure_on_msg(void *data,
		struct tipc_msg_buf *rxbuf)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)data;
	struct pixel_gpu_secure *self = pixel_dev->gpu_secure;
	gpu_secure_rsp_base_t rsp;

	if (mb_avail_data(rxbuf) != sizeof(rsp)) {
		dev_err(pixel_dev->dev, "Response size %zu != %zu, IPC will time out",
				mb_avail_data(rxbuf), sizeof(rsp));
		goto done;
	}

	/* Copy it out, as we can't guarantee alignment */
	memcpy(&rsp, mb_get_data(rxbuf, sizeof(rsp)), sizeof(rsp));

	PVR_DPF((PVR_DBG_CUSTOMER, "IPC command %d completed with result %d",
			rsp.command, rsp.result));

	uint32_t active_req_command = self->command;

	/* match response to the active request only */
	if (active_req_command == rsp.command) {
		self->result = rsp.result;
		/* Wake the originator thread */
		complete(&self->done);
	} else {
		dev_err(pixel_dev->dev, "Received response %d != active command %d",
				rsp.command, active_req_command);
	}
done:
	/* Return the rxbuf for immediate recycle */
	return rxbuf;
}

/**
 * TIPC channel ops.
 */
static const struct tipc_chan_ops gpu_secure_ops = {
	.handle_event = gpu_secure_on_event,
	.handle_msg   = gpu_secure_on_msg,
};


PVRSRV_ERROR gpu_secure_init(struct pixel_gpu_device *pixel_dev)
{
	struct pixel_gpu_secure *self;
	struct resource carveout;
	unsigned long remaining;
	PVRSRV_ERROR err;
	int rc;

	/* Allocate and zero our private data */
	self = kzalloc(sizeof(struct pixel_gpu_secure), GFP_KERNEL);
	if (!self) {
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	pixel_dev->gpu_secure = self;

	mutex_init(&self->ipc_lock);

	rc = fill_carveout_resource(&carveout);
	if (rc < 0) {
		dev_err(pixel_dev->dev, "Failed to populate carveout (%d)", rc);
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto done;
	}

	/* Save the carveout details */
	self->carveout_base = carveout.start;
	self->carveout_size = resource_size(&carveout);

	/* b/381059015: Use 33 MiB carveout instead of 65.0 MiB -- transitional */
	self->carveout_size = MIN(self->carveout_size, 0x2100000U);

	dev_info(pixel_dev->dev, "Firmware carveout at 0x%llx-0x%llx",
			self->carveout_base, self->carveout_base + self->carveout_size);

	/* We'll need this to wait for connection and responses */
	init_completion(&self->done);
	self->result = PVRSRV_ERROR_SRV_CONNECT_FAILED;

	dev_info(pixel_dev->dev, "Connecting to gpu_secure trusty app...");

	/* Create a TIPC channel */
	self->chan = tipc_create_channel(NULL, &gpu_secure_ops, pixel_dev);

	if (IS_ERR_OR_NULL(self->chan)) {
		dev_err(pixel_dev->dev, "tipc_create_channel() failed (%ld)",
				PTR_ERR(self->chan));
		self->chan = NULL;
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto done;
	}

	/* Connect the TIPC channel to the gpu_secure app's port */
	rc = tipc_chan_connect(self->chan, GPU_SECURE_PORT_NAME);

	if (rc < 0) {
		dev_err(pixel_dev->dev, "tipc_chan_connect(\"%s\") failed (%d)",
				GPU_SECURE_PORT_NAME, rc);
		err = PVRSRV_ERROR_INIT_FAILURE;
		goto done;
	}

	/* Wait for the connection to complete */
	remaining = wait_for_completion_timeout(&self->done,
			msecs_to_jiffies(GPU_SECURE_TIMEOUT_MS));

	if (remaining > 0) {
		/* The result of the connection attempt is our error code */
		err = self->result;
	} else {
		dev_err(pixel_dev->dev, "Timed out connecting to trusty app");
		err = PVRSRV_ERROR_TIMEOUT;
	}
done:
	/* Clean up again if we did not initialize successfully */
	if (PVRSRV_OK == err) {
		dev_info(pixel_dev->dev, "Connected to trusty app");
	} else {
		gpu_secure_term(pixel_dev);
	}

	return err;
}


void gpu_secure_term(struct pixel_gpu_device *pixel_dev)
{
	/* If we initialized at all */
	if (pixel_dev->gpu_secure) {

		mutex_destroy(&pixel_dev->gpu_secure->ipc_lock);

		dev_info(pixel_dev->dev, "Disconnecting from trusty app...");

		/* If a channel exists, shut it down and destroy it */
		if (pixel_dev->gpu_secure->chan) {
			tipc_chan_shutdown(pixel_dev->gpu_secure->chan);
			tipc_chan_destroy(pixel_dev->gpu_secure->chan);
		}

		/* Free our private data */
		kfree(pixel_dev->gpu_secure);
		pixel_dev->gpu_secure = NULL;
	}
}

#define GPU_SECURE_CONV(x) #x
static const char * const gpu_secure_cmd_strs[] = GPU_SECURE_CMDS;
#undef GPU_SECURE_CONV

/**
 * Call the gpu_secure trusty app, synchronously waiting for the response.
 *
 * @pixel_dev    The pixel_gpu_device.
 *
 * @req          The request.
 * @req_size     Size of the request.
 */
static PVRSRV_ERROR gpu_secure_call(struct pixel_gpu_device *pixel_dev,
		const gpu_secure_req_base_t *req, size_t req_size)
{
	struct pixel_gpu_secure *self = pixel_dev->gpu_secure;
	unsigned long remaining;
	PVRSRV_ERROR err;
	int rc;

	ATRACE_BEGIN(gpu_secure_cmd_strs[req->command]);

	/* Get a TX buffer (allocating or recycling) */
	struct tipc_msg_buf *txbuf = tipc_chan_get_txbuf_timeout(self->chan,
			GPU_SECURE_TIMEOUT_MS);

	mutex_lock(&self->ipc_lock);

	self->command = req->command;

	if (IS_ERR_OR_NULL(txbuf)) {
		dev_err(pixel_dev->dev, "tipc_chan_get_txbuf_timeout() failed (%ld)",
				PTR_ERR(txbuf));
		txbuf = NULL;
		err = PVRSRV_ERROR_TIMEOUT;
		goto done;
	}

	/* Ensure it is large enough for our request */
	if (mb_avail_space(txbuf) < req_size) {
		dev_err(pixel_dev->dev,
				"tipc_chan_get_txbuf_timeout() returned buffer size %zu<%zu",
				mb_avail_space(txbuf), req_size);
		err = PVRSRV_ERROR_INIT_FAILURE;
		goto done;
	}


	/* Copy in the request */
	memcpy(mb_put_data(txbuf, req_size), req, req_size);

	/* Enqueue the TX buffer for transmission */
	rc = tipc_chan_queue_msg(self->chan, txbuf);

	if (rc < 0) {
		dev_err(pixel_dev->dev, "tipc_chan_queue_msg() failed (%d)", rc);
		err = PVRSRV_ERROR_INIT_FAILURE;
		goto done;
	}

	/* The IPC channel now owns the TX buffer, so don't put() it later */
	txbuf = NULL;

	/* Wait for the response */
	remaining = wait_for_completion_timeout(&self->done,
			msecs_to_jiffies(GPU_SECURE_TIMEOUT_MS));

	if (0 == remaining) {
		dev_err(pixel_dev->dev, "Timed out waiting for response from trusty app");
		err = PVRSRV_ERROR_TIMEOUT;
		goto done;
	}

	err = self->result;
done:
	/* Clear the done flag now! */
	reinit_completion(&self->done);

	mutex_unlock(&self->ipc_lock);

	/* If we still have a TX buffer, recycle it */
	if (txbuf) {
		tipc_chan_put_txbuf(self->chan, txbuf);
	}

	ATRACE_END();

	return err;
}


PVRSRV_ERROR gpu_secure_set_power_params(IMG_HANDLE hSysData,
		PVRSRV_TD_POWER_PARAMS *psTDPowerParams)
{
	return PVRSRV_OK;
}


PVRSRV_ERROR gpu_secure_start(IMG_HANDLE hSysData)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;
	PVRSRV_ERROR err = PVRSRV_OK;

	ATRACE_BEGIN(__func__);

#if GPU_SECURE_TF_A
	struct arm_smccc_1_2_regs args = {
		.a0 = SMC_OEM_GPU_CONTROL,
		.a1 = OEM_GPU_CONTROL_START,
	};
	struct arm_smccc_1_2_regs res = {
		.a0 = 0xFFFFFFFF,
	};

	PVR_DPF((PVR_DBG_CUSTOMER, "Sending START to TF-A..."));
	arm_smccc_1_2_smc(&args, &res);

	if (unlikely(res.a0)) {
		dev_err(pixel_dev->dev, "OEM_GPU_CONTROL_START failed (%u)\n", (u32)res.a0);
		err = PVRSRV_ERROR_INIT_FAILURE;
	}
#else
	gpu_secure_req_base_t req = { .command = GPU_SECURE_REQ_START };
	PVR_DPF((PVR_DBG_CUSTOMER, "Sending IPC: START to trusty app..."));
	err = gpu_secure_call(pixel_dev, &req, sizeof(req));
#endif

	ATRACE_END();

	return err;
}


PVRSRV_ERROR gpu_secure_stop(IMG_HANDLE hSysData)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;
	PVRSRV_ERROR err = PVRSRV_OK;

	ATRACE_BEGIN(__func__);

#if GPU_SECURE_TF_A
	struct arm_smccc_1_2_regs args = {
		.a0 = SMC_OEM_GPU_CONTROL,
		.a1 = OEM_GPU_CONTROL_STOP,
	};
	struct arm_smccc_1_2_regs res = {
		.a0 = 0xFFFFFFFF,
	};

	PVR_DPF((PVR_DBG_CUSTOMER, "Sending STOP to TF-A..."));
	arm_smccc_1_2_smc(&args, &res);

	if (unlikely(res.a0)) {
		dev_err(pixel_dev->dev, "OEM_GPU_CONTROL_STOP failed (%u)\n", (u32)res.a0);
		err = PVRSRV_ERROR_INIT_FAILURE;
	}
#else
	gpu_secure_req_base_t req = { .command = GPU_SECURE_REQ_STOP };
	PVR_DPF((PVR_DBG_CUSTOMER, "Sending IPC: STOP to trusty app..."));
	err = gpu_secure_call(pixel_dev, &req, sizeof(req));
#endif

	ATRACE_END();

	return err;
}

void gpu_secure_fault(IMG_HANDLE hSysData)
{
	static atomic_t seqno = ATOMIC_INIT(0);
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;

	gpu_secure_req_fault_t const req = {
		{ 0 }, /* zero init other members */
		.base = {
			0, /* zero init other members */
			.command = GPU_SECURE_FAULT,
		},
		.session_counter = (uint32_t)atomic_inc_return(&seqno),
	};

	dev_info(pixel_dev->dev, "Dumping additional fault data for session %u in TEE\n", req.session_counter);
	PVR_DPF((PVR_DBG_CUSTOMER, "Sending IPC: FAULT to trusty app..."));

	(void)gpu_secure_call(pixel_dev, &req.base, sizeof(req));
}

#define GPU_FW_IMAGE_HEADER_MAGIC 0x46555047 /* "GPUF" */

// Old pre-RDO format that we still use for signing local builds.
#define WHI_IMAGE_HEADER_SIZE         0x1000
#define WHI_IMAGE_HEADER_MAGIC_OFFSET 0x0400

#define LGA_IMAGE_HEADER_VERSION      1
#define LGA_IMAGE_HEADER_SIZE         0x1000
#define LGA_IMAGE_HEADER_MAGIC_OFFSET 0x0864

#define MBU_IMAGE_HEADER_VERSION      2
#define MBU_IMAGE_HEADER_SIZE         0x5000
#define MBU_IMAGE_HEADER_MAGIC_OFFSET 0x463C

static uint32_t get_image_header_size(struct pixel_gpu_device *pixel_dev, const void *fw,
	uint32_t fw_size)
{
	const uint8_t *img_hdr = (const uint8_t *)fw;
	uint32_t magic = 0;
	uint32_t version = 0;

	if (fw_size < WHI_IMAGE_HEADER_SIZE) {
		dev_info(pixel_dev->dev, "Firmware image signature: unexpected image size");
		return 0;
	}

	memcpy(&magic, img_hdr + WHI_IMAGE_HEADER_MAGIC_OFFSET, sizeof(magic));
	if (magic == GPU_FW_IMAGE_HEADER_MAGIC) {
		dev_info(pixel_dev->dev, "Firmware image signature found with Whitechapel format");
		return WHI_IMAGE_HEADER_SIZE;
	}

	memcpy(&version, img_hdr, sizeof(version));
	switch (version) {

	case LGA_IMAGE_HEADER_VERSION:
		if (fw_size >= LGA_IMAGE_HEADER_SIZE) {
			memcpy(&magic, img_hdr + LGA_IMAGE_HEADER_MAGIC_OFFSET, sizeof(magic));
			if (magic == GPU_FW_IMAGE_HEADER_MAGIC) {
				dev_info(pixel_dev->dev, "Firmware image signature found with LGA format");
				return LGA_IMAGE_HEADER_SIZE;
			}
		}
		dev_info(pixel_dev->dev, "Firmware image signature: unexpected magic %d", magic);
		return 0;

	case MBU_IMAGE_HEADER_VERSION:
		if (fw_size >= MBU_IMAGE_HEADER_SIZE) {
			memcpy(&magic, img_hdr + MBU_IMAGE_HEADER_MAGIC_OFFSET, sizeof(magic));
			if (magic == GPU_FW_IMAGE_HEADER_MAGIC) {
				dev_info(pixel_dev->dev, "Firmware image signature found");
				return MBU_IMAGE_HEADER_SIZE;
			}
			dev_info(pixel_dev->dev, "Firmware image signature: unexpected magic %d",
				magic);
		}
		dev_info(pixel_dev->dev, "Firmware image signature: unexpected image size");
		return 0;

	default:
		dev_info(pixel_dev->dev, "Firmware image signature format not recognized (version=%d)",
			version);
		return 0;
	}
}

PVRSRV_ERROR gpu_secure_prepare_firmware_image(IMG_HANDLE hSysData,
		PVRSRV_FW_PARAMS *psFWParams)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;

	uint32_t size = get_image_header_size(pixel_dev, psFWParams->pvFirmware,
		psFWParams->ui32FirmwareSize);

	/* Check if the image is large enough to contain a header */
	if (size == 0) {
		dev_info(pixel_dev->dev, "Firmware image is NOT SIGNED");
		return PVRSRV_OK;
	}

	/* The header comes first */
	psFWParams->pvSignature = psFWParams->pvFirmware;
	psFWParams->ui32SignatureSize = size;

	/* The firmware proper is after the header */
	psFWParams->pvFirmware = (const uint8_t *)psFWParams->pvFirmware + size;
	psFWParams->ui32FirmwareSize -= size;

	return PVRSRV_OK;
}


/**
 * Prepare a firmware section allocation for transfer to trusty.
 *
 * @pixel_dev  The pixel_gpu_device.
 *
 * @devmem     Input: The memory descriptor for the existing allocation.
 * @fw_va      Input: The 32-bit GPU FW VA (COREMEM only, may be NULL).
 *
 * @section    Output: The section struct to fill in, part of the IPC request.
 */
static PVRSRV_ERROR gpu_secure_prepare_firmware_section(
		struct pixel_gpu_device *pixel_dev,
		DEVMEM_MEMDESC *devmem,
		RGXFWIF_DEV_VIRTADDR *fw_va,
		gpu_secure_memory_t *section)
{
	struct pixel_gpu_secure *self = pixel_dev->gpu_secure;
	IMG_DEVMEM_OFFSET_T offset;
	IMG_DEVMEM_SIZE_T size;
	IMG_CPU_PHYADDR paddr;
	IMG_BOOL valid;
	PMR *pmr = NULL;
	PVRSRV_ERROR err;

	/* Query the section size */
	err = DevmemGetSize(devmem, &size);
	if (PVRSRV_OK != err) {
		dev_err(pixel_dev->dev, "DevmemGetSize() failed (%d)", err);
		goto done;
	} else if (size > GPU_SECURE_MAX_IMAGE_SIZE) {
		dev_err(pixel_dev->dev, "Section size too large %llu>%u", size,
				GPU_SECURE_MAX_IMAGE_SIZE);
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto done;
	}

	/* Get the physical memory resource (PMR) that backs the allocation */
	DevmemGetPMRData(devmem, (IMG_HANDLE *)&pmr, &offset);
	if (!pmr) {
		dev_err(pixel_dev->dev, "DevmemGetPMRData() failed");
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto done;
	}

	/* Lock the PMR's physical page layout */
	err = PMRLockPhysAddresses(pmr);
	if (PVRSRV_OK != err) {
		dev_err(pixel_dev->dev, "PMRLockPhysAddresses() failed (%d)", err);
		goto done;
	}

	/* Get physical address of the PMR's first page */
	err = PMR_CpuPhysAddr(pmr, const_ilog2(PAGE_SIZE), 1, 0, &paddr, &valid, CPU_USE);
	if ((PVRSRV_OK != err) || (!valid)) {
		dev_err(pixel_dev->dev, "PMR_CpuPhysAddr() failed (%d)", err);
		goto done;
	}

	/* Unlock again, to satisfy the contract -- addresses won't change */
	err = PMRUnlockPhysAddresses(pmr);
	if (PVRSRV_OK != err) {
		dev_err(pixel_dev->dev, "PMRUnlockPhysAddresses() failed (%d)", err);
		goto done;
	}

	/* Advance to the offset of the devmem allocation within the PMR */
	paddr.uiAddr += offset;

	/* Validate that the allocation is entirely within the carveout */
	if ((paddr.uiAddr < self->carveout_base) ||
			(paddr.uiAddr >= (self->carveout_base + self->carveout_size)) ||
			((self->carveout_base + self->carveout_size - paddr.uiAddr)
			< size)) {
		dev_err(pixel_dev->dev,
				"GPU section allocated PA 0x%llx-0x%llx, carveout is 0x%llx-0x%llx",
				paddr.uiAddr, paddr.uiAddr + size,
				self->carveout_base, self->carveout_base + self->carveout_size);
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto done;
	}

	/* Fill in the section description */
	section->pa     = paddr.uiAddr;
	section->fw_va  = fw_va ? fw_va->ui32Addr : 0;
	section->size   = (uint32_t)size;

	PVR_DPF((PVR_DBG_CUSTOMER,
			"GPU FW section of %u bytes (PA: 0x%llx FW VA: 0x%x)",
			section->size, section->pa, section->fw_va));

done:
	return err;
}


/**
 * Prepare a firmware blob (the image itself, or the auth header).
 *
 * @pixel_dev  The pixel_gpu_device.
 *
 * @image      Input: Pointer to the firmware image blob.
 * @size       Input: Size of the image.
 *
 * @handle     Output: The section description to fill in.
 */
static PVRSRV_ERROR gpu_secure_prepare_firmware_blob(
		struct pixel_gpu_device *pixel_dev,
		const void *image,
		size_t size,
		gpu_secure_memory_t *section)
{
	PVRSRV_DEVICE_CONFIG *dev_config = pixel_dev->dev_config;
	PVRSRV_DEVICE_NODE *dev_node = dev_config->psDevNode;
	DEVMEM_MEMDESC *devmem = NULL;
	size_t aligned_size;
	PVRSRV_ERROR err;
	void *mapped;

	/* Validate size */
	if ((!size) || (size > GPU_SECURE_MAX_IMAGE_SIZE)) {
		dev_err(pixel_dev->dev, "Firmware image size too large %zu>%u", size,
				GPU_SECURE_MAX_IMAGE_SIZE);
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto done;
	}

	/* Round the size up to a page boundary, as these allocations will be
	 * processed by GSA and must not share pages with any other allocation.
	 */
	aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	/* Make a temporary allocation for the firmwage image blob -- this API
	 * requires GPU-readable, but that's OK.
	 */
	err = DevmemFwAllocate(dev_node->pvDevice, aligned_size,
			RGX_FWCODEDATA_ALLOCFLAGS |
			PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_CODE) |
			PVRSRV_MEMALLOCFLAG_CPU_READABLE |
			PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE,
			"FwImage", &devmem);
	if (PVRSRV_OK != err) {
		dev_err(pixel_dev->dev, "DevmemFwAllocate failed (%d)", err);
		goto done;
	}

	/* Map it for the CPU */
	err = DevmemAcquireCpuVirtAddr(devmem, &mapped);
	if (PVRSRV_OK != err) {
		dev_err(pixel_dev->dev, "DevmemAcquireCpuVirtAddr failed (%d)", err);
		goto done;
	}

	/* Populate and unmap it */
	memset(mapped, 0, aligned_size);
	memcpy(mapped, image, size);
	DevmemReleaseCpuVirtAddr(devmem);

	/* We can now treat the image as any other section allocation */
	err = gpu_secure_prepare_firmware_section(pixel_dev, devmem, NULL,
			section);

	if (PVRSRV_OK == err) {
		/* The allocation size was rounded up for alignment, contract it back
		 * to its real size for the image loader's benefit */
		section->size = (uint32_t)size;

		/* Verify that its base address was indeed page-aligned */
		if (section->pa & (PAGE_SIZE - 1)) {
			dev_err(pixel_dev->dev, "FwImage allocation not page-aligned");
			err = PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}

done:
	return err;
}


PVRSRV_ERROR gpu_secure_send_firmware_image(IMG_HANDLE hSysData,
		PVRSRV_FW_PARAMS *psFWParams)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;
	PVRSRV_RGXDEV_INFO *psDevInfo = pixel_dev->dev_config->psDevNode->pvDevice;
	gpu_secure_req_firmware_t req;
	PVRSRV_ERROR err;

	/* Start filling in the IPC request */
	memset(&req, 0, sizeof(req));
	req.base.command = GPU_SECURE_REQ_SEND_FIRMWARE_IMAGE;
	req.base.version = GPU_SECURE_REQ_FIRMWARE_VERSION_25_2;
	req.use_tf_a = GPU_SECURE_TF_A;
	req.host_page_shift = PAGE_SHIFT;

	/* Prepare the firmware image first, at the start of the carveout */
	err = gpu_secure_prepare_firmware_blob(pixel_dev, psFWParams->pvFirmware,
			psFWParams->ui32FirmwareSize, &req.image_body);
	if (PVRSRV_OK != err)
		goto done;

	/* Then the authentication header, if used */
	if (0 != psFWParams->ui32SignatureSize) {
		err = gpu_secure_prepare_firmware_blob(pixel_dev,
				psFWParams->pvSignature, psFWParams->ui32SignatureSize,
				&req.image_header);
		if (PVRSRV_OK != err)
			goto done;
	}

	/* Prepare FW section: CODE */
	if (psDevInfo->psRGXFWCodeMemDesc) {
		err = gpu_secure_prepare_firmware_section(pixel_dev,
				psDevInfo->psRGXFWCodeMemDesc,
				NULL,
				&req.section[GPU_SECURE_SECTION_CODE]);
		if (PVRSRV_OK != err)
			goto done;
	}

	/* Prepare FW section: DATA */
	if (psDevInfo->psRGXFWDataMemDesc) {
		err = gpu_secure_prepare_firmware_section(pixel_dev,
				psDevInfo->psRGXFWDataMemDesc,
				NULL,
				&req.section[GPU_SECURE_SECTION_DATA]);
		if (PVRSRV_OK != err)
			goto done;
	}

	/* Prepare FW section: RODATA */
	if (psDevInfo->psRGXFWRoDataMemDesc) {
		err = gpu_secure_prepare_firmware_section(pixel_dev,
				psDevInfo->psRGXFWRoDataMemDesc,
				NULL,
				&req.section[GPU_SECURE_SECTION_RODATA]);
		if (PVRSRV_OK != err)
			goto done;
	}

	/* Prepare FW section: RODATA */
	if (psDevInfo->psRGXFWRoDataMemDesc) {
		err = gpu_secure_prepare_firmware_section(pixel_dev,
				psDevInfo->psRGXFWRoDataMemDesc,
				NULL,
				&req.section[GPU_SECURE_SECTION_RODATA]);
		if (PVRSRV_OK != err)
			goto done;
	}

	//FW VA Addresses are now derived from device VA. CL6896907 and CL6897137
	//Hack for 26.1 RISCV FW:
	#define RGXRISCVFW_REGION_SIZE_LOG2				IMG_UINT32_C(28)
	#define RGXRISCVFW_GET_REGION_BASE(r)			((r) << RGXRISCVFW_REGION_SIZE_LOG2)
	#define RGXRISCVFW_SHARED_CACHED_DATA0_REGION	(0xCUL)
	#define RGXRISCVFW_SHARED_CACHED_DATA0_BASE		(RGXRISCVFW_GET_REGION_BASE(RGXRISCVFW_SHARED_CACHED_DATA0_REGION))
	#define RGXFW_FW_HEAP_BASE_CACHED				(RGXRISCVFW_SHARED_CACHED_DATA0_BASE)

	IMG_UINT32 ui32FwCodeHeapBase = RGXFW_FW_HEAP_BASE_CACHED;
	IMG_UINT32 ui32FwDataHeapBase = RGXFW_FW_HEAP_BASE_CACHED;
#if defined(RGX_FEATURE_META_MAX_VALUE_IDX)
	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META)) {
		ui32FwCodeHeapBase = RGXFW_SEGMMU_DATA_BASE_ADDRESS;
		ui32FwDataHeapBase = RGXFW_SEGMMU_DATA_BASE_ADDRESS;
	}
#endif

	RGXFWIF_DEV_VIRTADDR sCorememCodeAddr;
	sCorememCodeAddr.ui32Addr = (IMG_UINT32)(ui32FwCodeHeapBase +
											((uintptr_t)(psDevInfo->sFWCorememCodeDevVAddrBase.uiAddr ^ RGX_FIRMWARE_RAW_HEAP_BASE)));

	RGXFWIF_DEV_VIRTADDR sCorememDataAddr;
	sCorememDataAddr.ui32Addr = (IMG_UINT32)(ui32FwDataHeapBase +
											((uintptr_t)(psDevInfo->sFWCorememDataStoreDevVAddrBase.uiAddr ^ RGX_FIRMWARE_RAW_HEAP_BASE)));

	/* Prepare FW section: COREMEM_CODE */
	if (psDevInfo->psRGXFWCorememCodeMemDesc) {
		err = gpu_secure_prepare_firmware_section(pixel_dev,
				psDevInfo->psRGXFWCorememCodeMemDesc,
				&sCorememCodeAddr,
				&req.section[GPU_SECURE_SECTION_COREMEM_CODE]);
		if (PVRSRV_OK != err)
			goto done;
	}

	/* Prepare FW section: COREMEM_DATA */
	if (psDevInfo->psRGXFWIfCorememDataStoreMemDesc) {
		err = gpu_secure_prepare_firmware_section(pixel_dev,
				psDevInfo->psRGXFWIfCorememDataStoreMemDesc,
				&sCorememDataAddr,
				&req.section[GPU_SECURE_SECTION_COREMEM_DATA]);
		if (PVRSRV_OK != err)
			goto done;
	}

	/* Make the call */
	dev_info(pixel_dev->dev, "Sending IPC SEND_FIRMWARE_IMAGE to trusty app...");
	err = gpu_secure_call(pixel_dev, &req.base, sizeof(req));

	/* This only happens once, so log it in detail */
	if (PVRSRV_OK == err)
		dev_info(pixel_dev->dev, "SEND_FIRMWARE_IMAGE succeeded");
	else
		dev_err(pixel_dev->dev,
				"SEND_FIRMWARE_IMAGE failed (%d - %s), see /dev/trusty-log0 for more",
				err, PVRSRVGetErrorString(err));

done:
	return err;
}

#endif
