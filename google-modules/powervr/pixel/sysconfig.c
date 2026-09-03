// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_device.h>
#include <linux/dev_printk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>


#include <pvrsrvkm/services/system/include/interrupt_support.h>
#include <pvrsrvkm/services/system/include/syscommon.h>
#include <pvrsrvkm/services/system/include/sysconfig_cmn.h>
#include <pvrsrvkm/services/server/devices/volcanic/rgxdevice.h>
#include <pvrsrvkm/services/server/devices/hal/rgx/rgxstartstop.h>
#include <pvrsrvkm/services/server/devices/hal/rgx/rgxfwimageutils.h>
#include <pvrsrvkm/services/include/rgx_memallocflags.h>

#include <pvr_drv.h>
#include <pvrversion.h>

#include "sysconfig.h"
#include "pm_domain.h"
#include "dvfs.h"
#include "dvfs_governor.h"
#include "physmem.h"
#include "mba.h"
#include "fence_manager.h"
#include "ioctl.h"
#include "genpd.h"
#include "sscd.h"
#include "uid_time_in_state.h"
#include "gpu_uevent.h"
#include "scheduling.h"
#include <trace/hooks/systrace.h>

#if defined(SUPPORT_TRUSTED_DEVICE)
#include "gpu_secure.h"
#define SECURE_FW_MEM_SIZE   (0x200000) /* 2 MB */
#endif

#if defined(PVR_ANDROID_HAS_DMA_HEAP_FIND)
	/*! Name of DMA heap to allocate secure memory from. Used with dma_heap_find. */
#define PVR_SECURE_DMA_HEAP_NAME "vframe-secure"
#endif

struct pixel_gpu_device *device_to_pixel(struct device *dev)
{
	/*
	 * Unfortunately this is more convoluted than it should be.
	 *
	 * PVRSRV_DEVICE_NODE should embed struct drm_device, as the
	 * documentation for drm_device::dev_private suggests. Then the
	 * PVRSRV_DEVICE_NODE address could be calculated from the
	 * struct device address without any pointer dereferences. We'd still
	 * need to chase through the PVRSRV_DEVICE_CONFIG to get the
	 * pixel_gpu_device pointer.
	 */
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct pvr_drm_private *pvr_drm_priv = drm_dev->dev_private;
	struct _PVRSRV_DEVICE_NODE_ *pvr_dev = pvr_drm_priv->dev_node;
	struct _PVRSRV_DEVICE_CONFIG_ *pvr_dev_cfg = pvr_dev->psDevConfig;
	struct pixel_gpu_device *pixel_dev = pvr_dev_cfg->hSysData;

	return pixel_dev;
}

/*
 * Emulation platforms often behave as if the GPU is running very slowly
 * compared to the CPU.
 * E.g. a GPU with a minimum frequency of 100MHz might look like it's running
 * at 1MHz or even slower. A factor of 1000 ought to cover the realistic ranges
 * though.
 * Initialised 1 so that default is as if this were a silicon platform. A
 * device tree entry will instruct the system layer to change this to a
 * sensible value for pre-silicon.
 */
static unsigned int sysconfig_time_multiplier = 1;
static bool time_multiplier_set;

void set_time_multiplier(unsigned int multiplier)
{
	// Multiplier should be set once
	WARN_ON(time_multiplier_set);

	sysconfig_time_multiplier = multiplier;
	time_multiplier_set = true;
}

unsigned int get_time_multiplier(void)
{
	return sysconfig_time_multiplier;
}

enum GPU_HEAPS {
	DEFAULT_HEAP = 0,
#if defined(CONFIG_POWERVR_PIXEL_SLC)
	SLC_HEAP,
#endif
#if defined(SUPPORT_TRUSTED_DEVICE)
	GPU_SECURE_HEAP,
	FW_CODE_HEAP,
	FW_PRIV_DATA_HEAP,
	FW_PRIV_RODATA_HEAP,
	FW_SHARED_HEAP,
	FW_PT_HEAP,
#endif
};

#define PHYS_HEAP_IPA_CONFIG(PBHA_POLICY)                                                          \
       ((IPA_CONFIG){                                                                             \
               .ui8IPAPolicyDefault = (PBHA_POLICY),                                              \
               .ui8IPAPolicyMask = PBHA_MASK,                                                     \
               .ui8IPAPolicyShift = PBHA_BIT_POS,                                                 \
       })

/* This can't be const, as the physical address range of the trusted firmware
 * carveout will be populated from device-tree.
 */
static PHYS_HEAP_CONFIG phys_heaps[] = {
	[DEFAULT_HEAP] = {
		.eType                             = PHYS_HEAP_TYPE_UMA,
		.ui32UsageFlags                    = PHYS_HEAP_USAGE_GPU_LOCAL,
		.sIPAConfig                        =
			PHYS_HEAP_IPA_CONFIG(PBHA_DEFAULT),
		.uConfig.sUMA.pszPDumpMemspaceName = "SYSMEM",
		.uConfig.sUMA.psMemFuncs           = &g_sUmaHeapFns,
		.uConfig.sUMA.pszHeapName          = "uma_gpu_local",
		.uConfig.sUMA.sCardBase            = {},
		.uConfig.sUMA.hPrivData            = NULL,
	},
#if defined(CONFIG_POWERVR_PIXEL_SLC)
	[SLC_HEAP] = {
		.eType                             = PHYS_HEAP_TYPE_UMA,
		.ui32UsageFlags                    = PHYS_HEAP_USAGE_PIXEL_SLC,
		.sIPAConfig                        =
			PHYS_HEAP_IPA_CONFIG(PBHA_WRITE_ALLOC),
		.uConfig.sUMA.pszPDumpMemspaceName = "SLC",
		.uConfig.sUMA.psMemFuncs           = &g_sUmaHeapFns,
		.uConfig.sUMA.pszHeapName          = "uma_gpu_slc",
		.uConfig.sUMA.sCardBase            = {},
		.uConfig.sUMA.hPrivData            = NULL,
	},
#endif
#if defined(SUPPORT_TRUSTED_DEVICE)
	[GPU_SECURE_HEAP] = {
		.eType                             = PHYS_HEAP_TYPE_UMA,
		.ui32UsageFlags                    = PHYS_HEAP_USAGE_GPU_SECURE,
		.sIPAConfig                        =
			PHYS_HEAP_IPA_CONFIG(PBHA_DEFAULT),
		.uConfig.sUMA.pszPDumpMemspaceName = "GPU_SECURE",
		.uConfig.sUMA.psMemFuncs           = &g_sUmaHeapFns,
		.uConfig.sUMA.pszHeapName          = "uma_gpu_secure",
		.uConfig.sUMA.sCardBase            = {},
		.uConfig.sUMA.hPrivData            = NULL,
	},
	[FW_CODE_HEAP] = {
		.eType                             = PHYS_HEAP_TYPE_LMA,
		.ui32UsageFlags                    = PHYS_HEAP_USAGE_FW_CODE,
		.sIPAConfig                        =
			PHYS_HEAP_IPA_CONFIG(PBHA_DEFAULT),
		.uConfig.sLMA.pszPDumpMemspaceName = "FIRMWARE Code",
		.uConfig.sLMA.psMemFuncs           = &g_sUmaHeapFns,
		.uConfig.sLMA.pszHeapName          = "lma_fw_code",
		.uConfig.sLMA.sStartAddr           = {},
		.uConfig.sLMA.sCardBase            = {},
		.uConfig.sLMA.uiSize               = RGX_FW_CODE_SECTION_SIZE,
		.uConfig.sLMA.hPrivData            = NULL,
	},
	[FW_PRIV_DATA_HEAP] = {
		.eType                             = PHYS_HEAP_TYPE_LMA,
		.ui32UsageFlags                    = PHYS_HEAP_USAGE_FW_PRIV_DATA,
		.sIPAConfig                        =
			PHYS_HEAP_IPA_CONFIG(PBHA_DEFAULT),
		.uConfig.sLMA.pszPDumpMemspaceName = "FIRMWARE Private Data",
		.uConfig.sLMA.psMemFuncs           = &g_sUmaHeapFns,
		.uConfig.sLMA.pszHeapName          = "lma_priv_data",
		.uConfig.sLMA.sStartAddr           = {},
		.uConfig.sLMA.sCardBase            = {},
		.uConfig.sLMA.uiSize               = RGX_FW_DATA_SECTION_SIZE,
		.uConfig.sLMA.hPrivData            = NULL,
	},
	[FW_PRIV_RODATA_HEAP] = {
		.eType                             = PHYS_HEAP_TYPE_LMA,
		.ui32UsageFlags                    =
			PHYS_HEAP_USAGE_FW_PRIV_RODATA,
		.sIPAConfig                        =
			PHYS_HEAP_IPA_CONFIG(PBHA_DEFAULT),
		.uConfig.sLMA.pszPDumpMemspaceName = "FIRMWARE Private RO Data",
		.uConfig.sLMA.psMemFuncs           = &g_sUmaHeapFns,
		.uConfig.sLMA.pszHeapName          = "lma_priv_ro_data",
		.uConfig.sLMA.sStartAddr           = {},
		.uConfig.sLMA.sCardBase            = {},
		.uConfig.sLMA.uiSize               = RGX_FW_RODATA_SECTION_SIZE,
		.uConfig.sLMA.hPrivData            = NULL,
	},
	[FW_SHARED_HEAP] = {
		.eType                             = PHYS_HEAP_TYPE_LMA,
		.ui32UsageFlags                    =
			PHYS_HEAP_USAGE_FW_SHARED,
		.sIPAConfig                        =
			PHYS_HEAP_IPA_CONFIG(PBHA_DEFAULT),
		.uConfig.sLMA.pszPDumpMemspaceName = "FIRMWARE Shared",
		.uConfig.sLMA.psMemFuncs           = &g_sUmaHeapFns,
		.uConfig.sLMA.pszHeapName          = "lma_fw_shared",
		.uConfig.sLMA.sStartAddr           = {},
		.uConfig.sLMA.sCardBase            = {},
		.uConfig.sLMA.uiSize               = RGX_FIRMWARE_RAW_HEAP_SIZE - SECURE_FW_MEM_SIZE,
		.uConfig.sLMA.hPrivData            = NULL,
	},
	[FW_PT_HEAP] = {
		.eType                             = PHYS_HEAP_TYPE_LMA,
		.ui32UsageFlags                    =
			PHYS_HEAP_USAGE_FW_PREMAP_PT,
		.sIPAConfig                        =
			PHYS_HEAP_IPA_CONFIG(PBHA_DEFAULT),
		.uConfig.sLMA.pszPDumpMemspaceName = "FIRMWARE PT",
		.uConfig.sLMA.psMemFuncs           = &g_sUmaHeapFns,
		.uConfig.sLMA.pszHeapName          = "lma_fw_pt",
		.uConfig.sLMA.sStartAddr           = {},
		.uConfig.sLMA.sCardBase            = {},
		.uConfig.sLMA.uiSize               =
			RGX_FIRMWARE_MAX_PAGETABLE_SIZE,
		.uConfig.sLMA.hPrivData            = NULL,
	},
#endif
};

#if defined(SUPPORT_TRUSTED_DEVICE)
static void rgx_heaps_configure(phys_addr_t carveout_base)
{
	/*
	 * Populate the physheap configuration with LMA at fixed offsets, so that the KMD can
	 * allocate firmware sections within it.
	 * -------------------------------------------------------------------------------------
	 * |  FW_PRIVATE_HEAP  |                 FW_SHARED_HEAP               | FW_PT_HEAP     |
	 * |                   |          HOST         |         GUEST1       |                |
	 * -------------------------------------------------------------------------------------
	 * |<---- 2 MB ------->|<----------30MB-------><---------32MB-------->|<-----1 MB----->|
	 * -------------------------------------------------------------------------------------
	 */

	static_assert((RGX_FW_CODE_SECTION_SIZE + RGX_FW_DATA_SECTION_SIZE + RGX_FW_RODATA_SECTION_SIZE) == SECURE_FW_MEM_SIZE, "FW PRIVATE HEAPs must be exactly 2MB");

	size_t offset = 0;

	/* FW_CODE_HEAP */
	phys_heaps[FW_CODE_HEAP].uConfig.sLMA.sStartAddr.uiAddr = carveout_base;
	phys_heaps[FW_CODE_HEAP].uConfig.sLMA.sCardBase.uiAddr  = carveout_base;

	for (int heap = FW_PRIV_DATA_HEAP; heap < FW_PT_HEAP; heap++) {
		offset += phys_heaps[heap - 1].uConfig.sLMA.uiSize;
		phys_heaps[heap].uConfig.sLMA.sStartAddr.uiAddr =
			carveout_base + offset;
		phys_heaps[heap].uConfig.sLMA.sCardBase.uiAddr =
			phys_heaps[heap].uConfig.sLMA.sStartAddr.uiAddr;
	}

	/* FW_PT_HEAP */
	phys_heaps[FW_PT_HEAP].uConfig.sLMA.sStartAddr.uiAddr = carveout_base +
		RGX_FIRMWARE_RAW_HEAP_SIZE * RGX_NUM_DRIVERS_SUPPORTED;
	phys_heaps[FW_PT_HEAP].uConfig.sLMA.sCardBase.uiAddr =
		phys_heaps[FW_PT_HEAP].uConfig.sLMA.sStartAddr.uiAddr;
}
#endif

const char *rgx_context_reset_reason_str(RGX_CONTEXT_RESET_REASON reason)
{
	switch (reason) {
		case RGX_CONTEXT_RESET_REASON_NONE: return "NONE";
		case RGX_CONTEXT_RESET_REASON_GUILTY_LOCKUP: return "GUILTY_LOCKUP";
		case RGX_CONTEXT_RESET_REASON_INNOCENT_LOCKUP: return "INNOCENT_LOCKUP";
		case RGX_CONTEXT_RESET_REASON_GUILTY_OVERRUNING: return "GUILTY_OVERRUNING";
		case RGX_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING: return "INNOCENT_OVERRUNING";
		case RGX_CONTEXT_RESET_REASON_HARD_CONTEXT_SWITCH: return "HARD_CONTEXT_SWITCH";
		case RGX_CONTEXT_RESET_REASON_WGP_CHECKSUM: return "WGP_CHECKSUM";
		case RGX_CONTEXT_RESET_REASON_TRP_CHECKSUM: return "TRP_CHECKSUM";
		case RGX_CONTEXT_RESET_REASON_GPU_ECC_OK: return "GPU_ECC_OK";
		case RGX_CONTEXT_RESET_REASON_GPU_ECC_HWR: return "GPU_ECC_HWR";
		case RGX_CONTEXT_RESET_REASON_FW_ECC_OK: return "FW_ECC_OK";
		case RGX_CONTEXT_RESET_REASON_FW_ECC_ERR: return "FW_ECC_ERR";
		case RGX_CONTEXT_RESET_REASON_FW_WATCHDOG: return "FW_WATCHDOG";
		case RGX_CONTEXT_RESET_REASON_FW_PAGEFAULT: return "FW_PAGEFAULT";
		case RGX_CONTEXT_RESET_REASON_FW_EXEC_ERR: return "FW_EXEC_ERR";
		case RGX_CONTEXT_RESET_REASON_HOST_WDG_FW_ERR: return "HOST_WDG_FW_ERR";
		case RGX_CONTEXT_GEOM_OOM_DISABLED: return "GEOM_OOM_DISABLED";
		case RGX_CONTEXT_PVRIC_SIGNATURE_MISMATCH: return "PVRIC_SIGNATURE_MISMATCH";
		case RGX_CONTEXT_RESET_REASON_FW_PTE_PARITY_ERR:
			return "RGX_CONTEXT_RESET_REASON_FW_PTE_PARITY_ERR";
		case RGX_CONTEXT_RESET_REASON_FW_PARITY_ERR:
			return "RGX_CONTEXT_RESET_REASON_FW_PARITY_ERR";
		case RGX_CONTEXT_RESET_REASON_GPU_PARITY_HWR:
			return "RGX_CONTEXT_RESET_REASON_GPU_PARITY_HWR";
		case RGX_CONTEXT_RESET_REASON_GPU_LATENT_HWR:
			return "RGX_CONTEXT_RESET_REASON_GPU_LATENT_HWR";
		case RGX_CONTEXT_RESET_REASON_DCLS_ERR: return "RGX_CONTEXT_RESET_REASON_DCLS_ERR";
		case RGX_CONTEXT_RESET_REASON_ICS_HWR: return "RGX_CONTEXT_RESET_REASON_ICS_HWR";
		case RGX_CONTEXT_RESET_REASON_GPU_PAGE_FAULT_HWR: return "RGX_CONTEXT_RESET_REASON_GPU_PAGE_FAULT_HWR";
		case RGX_CONTEXT_RESET_REASON_GPU_LOCKUP_HWR: return "RGX_CONTEXT_RESET_REASON_GPU_LOCKUP_HWR";
		case RGX_CONTEXT_RESET_REASON_CPU_PAGE_FAULT: return "RGX_CONTEXT_RESET_REASON_CPU_PAGE_FAULT";
	}
	return "UNKNOWN";
}

const char *rgxfwif_dm_str(RGXFWIF_DM dm)
{
	switch (dm)
	{
		case RGXFWIF_DM_GP: return "GP";
		case RGXFWIF_DM_TDM: return "TDM";
		case RGXFWIF_DM_GEOM: return "GEOM";
		case RGXFWIF_DM_3D: return "3D";
		case RGXFWIF_DM_CDM: return "CDM";
		case RGXFWIF_DM_RAY: return "RAY";
		case RGXFWIF_DM_GEOM2: return "GEOM2";
		case RGXFWIF_DM_GEOM3: return "GEOM3";
		case RGXFWIF_DM_GEOM4: return "GEOM4";
	}
	return "UNKNOWN";
}

static IMG_UINT64 RGXGetFaultAddress(IMG_UINT64 ui64MMUStatus)
{
	return ((ui64MMUStatus & ~RGX_CR_MMU_FAULT_STATUS1_ADDRESS_CLRMSK)
			 >> RGX_CR_MMU_FAULT_STATUS1_ADDRESS_SHIFT) << 4; /* align shift */
}

static void gpu_error_notify(IMG_HANDLE sysdata, PVRSRV_ROBUSTNESS_NOTIFY_DATA *error)
{
	struct pixel_gpu_device * const pixel_dev = (struct pixel_gpu_device *)sysdata;
	struct device * const dev = pixel_dev->dev;
	char buf[128] = { 0 };
	int pid = error->pid ? error->pid : current->tgid;

	switch (error->eResetReason) {
	case RGX_CONTEXT_RESET_REASON_GUILTY_OVERRUNING:
	case RGX_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING:
	case RGX_CONTEXT_RESET_REASON_HARD_CONTEXT_SWITCH:
	case RGX_CONTEXT_RESET_REASON_WGP_CHECKSUM:
	case RGX_CONTEXT_RESET_REASON_TRP_CHECKSUM:
	case RGX_CONTEXT_RESET_REASON_GPU_ECC_OK:
	case RGX_CONTEXT_RESET_REASON_GPU_ECC_HWR:
	case RGX_CONTEXT_RESET_REASON_FW_ECC_OK:
	case RGX_CONTEXT_RESET_REASON_FW_ECC_ERR:
	case RGX_CONTEXT_RESET_REASON_FW_WATCHDOG:
	case RGX_CONTEXT_RESET_REASON_FW_PAGEFAULT:
	case RGX_CONTEXT_RESET_REASON_FW_EXEC_ERR:
	case RGX_CONTEXT_RESET_REASON_HOST_WDG_FW_ERR:
	case RGX_CONTEXT_GEOM_OOM_DISABLED:
	case RGX_CONTEXT_PVRIC_SIGNATURE_MISMATCH:
	case RGX_CONTEXT_RESET_REASON_FW_PTE_PARITY_ERR:
	case RGX_CONTEXT_RESET_REASON_FW_PARITY_ERR:
	case RGX_CONTEXT_RESET_REASON_GPU_PARITY_HWR:
	case RGX_CONTEXT_RESET_REASON_GPU_LATENT_HWR:
	case RGX_CONTEXT_RESET_REASON_DCLS_ERR:
	case RGX_CONTEXT_RESET_REASON_ICS_HWR:
		gpu_sscd_dump(pixel_dev, error);
		break;
	case RGX_CONTEXT_RESET_REASON_NONE:
	case RGX_CONTEXT_RESET_REASON_GUILTY_LOCKUP:
	case RGX_CONTEXT_RESET_REASON_INNOCENT_LOCKUP:
	default:
		break;
	}

	dev_warn(dev, "GPU reset reason=%s pid=%d",
		 rgx_context_reset_reason_str(error->eResetReason), error->pid);

	switch (error->eResetReason)
	{
	case RGX_CONTEXT_RESET_REASON_WGP_CHECKSUM:
	case RGX_CONTEXT_RESET_REASON_TRP_CHECKSUM:
		dev_warn(dev, "  ext_job_ref=%u dm=%s",
			 error->uErrData.sChecksumErrData.ui32ExtJobRef,
			 rgxfwif_dm_str(error->uErrData.sChecksumErrData.eDM));
		scnprintf(buf, sizeof(buf), "GPU Reset: %s (%s)",
			rgx_context_reset_reason_str(error->eResetReason),
			rgxfwif_dm_str(error->uErrData.sChecksumErrData.eDM));
		break;

	case RGX_CONTEXT_RESET_REASON_FW_PAGEFAULT:
		dev_warn(dev, "  fw_fault_addr=" IMG_DEV_VIRTADDR_FMTSPEC,
			 error->uErrData.sFwPFErrData.sFWFaultAddr.uiAddr);
		gpu_uevent_kmd_error_send(pixel_dev, GPU_UEVENT_INFO_FW_PAGEFAULT);
		scnprintf(buf, sizeof(buf), "GPU Reset: %s Addr 0x%llx",
			rgx_context_reset_reason_str(error->eResetReason),
			(unsigned long long)error->uErrData.sFwPFErrData.sFWFaultAddr.uiAddr);
		break;

	case RGX_CONTEXT_RESET_REASON_HOST_WDG_FW_ERR:
		dev_warn(dev, "  fw_status=%u fw_reason=%u",
			 error->uErrData.sHostWdgData.ui32Status,
			 error->uErrData.sHostWdgData.ui32Reason);
		gpu_uevent_kmd_error_send(pixel_dev, GPU_UEVENT_INFO_HOST_WDG_FW_ERROR);
		scnprintf(buf, sizeof(buf), "GPU Reset: %s Status %u, Reason %u",
			rgx_context_reset_reason_str(error->eResetReason),
			error->uErrData.sHostWdgData.ui32Status,
			error->uErrData.sHostWdgData.ui32Reason);
		break;

	case RGX_CONTEXT_RESET_REASON_GUILTY_LOCKUP:
	{
		PVRSRV_ROBUSTNESS_ERR_DATA_GUILTY_LOCKUP *lockup_data =
			&error->uErrData.sGuiltyLockupData;
		struct gpu_uevent evt = {
			.type = GPU_UEVENT_TYPE_KMD_ERROR,
			.info = GPU_UEVENT_INFO_GUILTY_LOCKUP,
			.uid = 0
		};

		OSGetUID(error->pid, &evt.uid);

		if (lockup_data->ui32Flags &
		    RGXFWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_PF) {
			dev_warn(dev, "  DataMaster=%s fault_address=0x%"
				IMG_UINT64_FMTSPECx
				" pc_address=0x%"IMG_UINT64_FMTSPECx,
				rgxfwif_dm_str(lockup_data->eDM),
				RGXGetFaultAddress(lockup_data->aui64MMUStatus[0]),
				lockup_data->ui64PCAddress);
		} else
			dev_warn(dev, "  DataMaster=%s",
				 rgxfwif_dm_str(lockup_data->eDM));

		gpu_uevent_send(pixel_dev, &evt);
	}
	__fallthrough;
	case RGX_CONTEXT_RESET_REASON_GUILTY_OVERRUNING:
	case RGX_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING:
	{
		scnprintf(buf, sizeof(buf), "GPU Reset: %s (%s)",
			rgx_context_reset_reason_str(error->eResetReason),
			rgxfwif_dm_str(error->uErrData.sGuiltyLockupData.eDM));
		break;
	}

	default:
		// no extra error info
		scnprintf(buf, sizeof(buf), "GPU Reset: %s",
			rgx_context_reset_reason_str(error->eResetReason));
		break;
	}

	__ATRACE_BEGIN_PID(pid, buf);
	__ATRACE_END_PID(pid, "");
}

#if defined(SUPPORT_FOREIGN_FENCE_TOKEN)
static void release_fence_data(IMG_HANDLE sys_data, IMG_UINT16 fence_data)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)sys_data;
	union pixel_rgxfwif_iif_handle const handle = {.data = fence_data};

	fence_manager_iif_retire(pixel_dev, &handle);
}
#endif

#if defined(SUPPORT_FOREIGN_FENCE_TOKEN) || defined(SUPPORT_FASTPATH_FENCE)
static IMG_BOOL extract_fence_data(IMG_HANDLE sys_data, IMG_HANDLE hEnvFenceObjPtr,
		SYNC_CHECKPOINT_FF_TOKEN *puiFFToken)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)sys_data;
	struct dma_fence *fence = hEnvFenceObjPtr;

	union pixel_rgxfwif_iif_handle handle = fence_manager_extract_iif(pixel_dev, fence);

	*puiFFToken = handle.data;

	return handle.valid;
}
#endif

#if !defined(SUPPORT_TRUSTED_DEVICE)
static int map_physmem_fw(struct pixel_gpu_device *pixel_dev)
{
	PVRSRV_DEVICE_CONFIG *dev_config = pixel_dev->dev_config;
	PVRSRV_DEVICE_NODE *dev_node = dev_config->psDevNode;
	PVRSRV_RGXDEV_INFO *info = dev_node->pvDevice;
	DEVMEM_HEAP *heap = info->psFirmwareMainHeap;
	struct pixel_fw_info *footer = &pixel_dev->fw_footer;
	struct pixel_devmap_phys_range phys_ranges[PIXEL_MAX_PA_RANGE_COUNT];
	struct pixel_devmap_phys_ranges_info map_info;
	int i, ret = 0;

	/* PA ranges are aligned to 16k from ABI version 15 */
	uint64_t pa_range_align = (footer->fwabi_version < PIXEL_PA_RANGE_16K_PAGE_ABI_VERSION) ?
		PAGE_SIZE : PIXEL_PA_RANGE_PAGE_SIZE;

	for (i = 0; i < footer->pa_range_count; ++i) {
		uint64_t pa_offset = footer->pa_ranges[i].base_pa & (pa_range_align - 1);
		phys_ranges[i] = (struct pixel_devmap_phys_range){
			.pa = footer->pa_ranges[i].base_pa & ~(pa_range_align - 1),
			.size = PVR_ALIGN(footer->pa_ranges[i].extent + pa_offset, pa_range_align),
		};
	}
	map_info = (struct pixel_devmap_phys_ranges_info){
		.ranges = phys_ranges,
		.flags = PVRSRV_MEMALLOCFLAG_GPU_READABLE | PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
			 PVRSRV_MEMALLOCFLAG_GPU_UNCACHED |
			 PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
			 PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN),
		.heap = heap,
		.n_ranges = footer->pa_range_count,
	};

	ret = devmap_phys_ranges(dev_node, &pixel_dev->gpu_phys_mem_import, &map_info);
	if (ret)
		goto exit;

	/* Provide the FW with the base address */
	info->psRGXFWIfSysInit->sCustomerRegBase =
		pixel_dev->gpu_phys_mem_import->sDeviceImport.sDevVAddr;
	PVR_DPF((PVR_DBG_CUSTOMER, "FW reg block va: %llx",
		pixel_dev->gpu_phys_mem_import->sDeviceImport.sDevVAddr.uiAddr));
exit:
	return ret;
}
#endif

PVRSRV_ERROR SysDevLateInit(PVRSRV_DEVICE_CONFIG *devcfg)
{
#if !defined(SUPPORT_TRUSTED_DEVICE)
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)devcfg->hSysData;

	return map_physmem_fw(pixel_dev);
#else
	return PVRSRV_OK;
#endif
}


static int init_slc(struct pixel_gpu_device *pixel_dev)
{
	return slc_init_data(&pixel_dev->slc_data, pixel_dev->dev);
}

static void deinit_slc(struct pixel_gpu_device *pixel_dev)
{
	slc_term_data(&pixel_dev->slc_data);
}

#if defined(SUPPORT_TRUSTED_DEVICE)
static int init_gpu_secure(struct pixel_gpu_device *pixel_dev)
{
	/* Connect to the gpu_secure Trusty app early */
	int ret = gpu_secure_init(pixel_dev);
	if (PVRSRV_OK != ret) {
		return ret;
	}

	rgx_heaps_configure(pixel_dev->gpu_secure->carveout_base);

	return PVRSRV_OK;
}
#endif

/**
 * struct subsystem_init_table_entry - Subsystem init/deinit methods.
 */
struct subsystem_init_table_entry {
	/** @init: Initialise method. */
	int (*init)(struct pixel_gpu_device*);

	/** @deinit: deinit method. */
	void (*deinit)(struct pixel_gpu_device*);

	/** @err_msg: Error message to be printed when init method fails. */
	const char *err_msg;
};

static const struct subsystem_init_table_entry subsys_init[] = {
	{init_pixel_of, deinit_pixel_of, "Failed to initialize open firmware"},
	{init_genpd, deinit_genpd, "Failed to initialize power domains"},
	{pixel_gpu_debug_init, pixel_gpu_debug_deinit, "Failed to initialize debugfs"},
	{init_dvfs_gov, deinit_dvfs_gov, "Failed to initialize dvfs governor"},
	{init_pixel_dvfs, deinit_pixel_dvfs, "Failed to initialize dvfs"},
	{init_slc, deinit_slc, "Failed to initialize slc"},
#if defined(SUPPORT_TRUSTED_DEVICE)
	{init_gpu_secure, gpu_secure_term, "Failed to initialize secure channel"},
#endif
	{mba_init, mba_term, "Failed to initialize MBA"},
	{fence_manager_init, fence_manager_term, "Failed to initialize fence manager"},
	{pixel_ioctl_init, pixel_ioctl_term, "Failed to initialize ioctls"},
	{gpu_sscd_init, gpu_sscd_deinit, "Failed to initialize SSCD"},
	{init_pixel_uid_tis, deinit_pixel_uid_tis, "Failed to initialize per-UID time-in-state"},
	{gpu_uevent_init, gpu_uevent_term, "Failed to initialize GPU uevent"},
	{init_scheduling, deinit_scheduling, "Failed to initialize GPU scheduling adjustments"},
};

static void subsystem_deinit(struct pixel_gpu_device *pixel_dev, int i)
{
	while (i-- > 0) {
		if (subsys_init[i].deinit)
			subsys_init[i].deinit(pixel_dev);
	}
}

static int subsystem_init(struct pixel_gpu_device *pixel_dev)
{
	int ret = 0, i;

	for (i = 0; i < ARRAY_SIZE(subsys_init); i++) {
		if (!subsys_init[i].init)
			continue;

		ret = subsys_init[i].init(pixel_dev);
		if (ret)
			break;
	}

	if (ret) {
		dev_err(pixel_dev->dev, "%s: %d\n", subsys_init[i].err_msg, ret);
		subsystem_deinit(pixel_dev, i);
	}

	return ret;
}

static bool validate_footer_field(struct pixel_gpu_device *pixel_dev, const char *name,
				  u32 expected, u32 actual)
{
	bool const ret = expected == actual;
	if (!ret) {
		dev_err(pixel_dev->dev, "FW %s mismatch, KM needs 0x%08X, FW provided 0x%08X", name,
			expected, actual);
	}
	return ret;
}

static bool validate_firmware_footer(struct pixel_gpu_device *pixel_dev,
				     struct pixel_fw_info const *footer)
{
	bool valid_fw = true;

	valid_fw &= validate_footer_field(pixel_dev, "magic", PIXEL_FOOTER_MAGIC, footer->magic);
	valid_fw &= validate_footer_field(pixel_dev, "footer version", PIXEL_FOOTER_VERSION,
					  footer->footer_version);
	valid_fw &= validate_footer_field(pixel_dev, "max pa range count", PIXEL_MAX_PA_RANGE_COUNT,
					  max(footer->pa_range_count, PIXEL_MAX_PA_RANGE_COUNT));

	return valid_fw;
}

/**
 * prepare_firmware_image - Extract a firmware image from the binary blob
 *
 * @hSysData:   Platform specific data
 * @psFWParams: Firmware binary blob with parameters
 *
 * The untouched binary blob can contain multiple length-prefixed FW images.
 * This function must examine each individual FW image, and try to find a
 * matching ABI version.
 */
static PVRSRV_ERROR prepare_firmware_image(IMG_HANDLE hSysData, PVRSRV_FW_PARAMS *psFWParams)
{
	struct pixel_gpu_device *pixel_dev = (struct pixel_gpu_device *)hSysData;
	PVRSRV_DEVICE_NODE *psDeviceNode = pixel_dev->dev_config->psDevNode;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	u32 const *entry_base = psFWParams->pvFirmware;
	void const *const end = psFWParams->pvFirmware + psFWParams->ui32FirmwareSize;

	do {
		void const *const fw_base = &entry_base[1];
		ptrdiff_t const remaining = end - fw_base;
		u32 size;
		struct pixel_fw_info const *footer;

		/* Check that it is safe to read the length prefix */
		if (remaining < 0) {
			dev_err(pixel_dev->dev,
				"No matching FW within catalogue (requested ABI: %d)",
				FW_ABI_VERSION);
			return PVRSRV_ERROR_FW_IMAGE_MISMATCH;
		}
		size = entry_base[0];

		if (remaining < size) {
			dev_err(pixel_dev->dev, "FW catalogue is too small for entry (bad size)");
			return PVRSRV_ERROR_FW_IMAGE_MISMATCH;
		}

		if (size < PIXEL_FOOTER_AREA_OFFSET_FROM_END) {
			dev_err(pixel_dev->dev, "FW image is too small (no footer)");
			return PVRSRV_ERROR_FW_IMAGE_MISMATCH;
		}
		if (!IS_ALIGNED(size, sizeof(u32))) {
			dev_err(pixel_dev->dev, "FW footer is not word-aligned");
			return PVRSRV_ERROR_FW_IMAGE_MISMATCH;
		}

		footer = fw_base + size - PIXEL_FOOTER_AREA_OFFSET_FROM_END;

		if (!validate_firmware_footer(pixel_dev, footer))
			return PVRSRV_ERROR_FW_IMAGE_MISMATCH;

		/* Exit if we've found a compatible FW image */
		if (footer->fwabi_version == FW_ABI_VERSION) {
			/* Crop to the compatible FW entry */
			psFWParams->pvFirmware = fw_base;
			psFWParams->ui32FirmwareSize = size;
			dev_info(pixel_dev->dev, "FW image ABI version: 0x%08X",
				 footer->fwabi_version);

			/* Make a copy of the firmware Build ID string here. */
			size = sizeof(footer->fw_build_id);
			if (psDevInfo->pszFWBuildID == NULL) {
				psDevInfo->pszFWBuildID = OSAllocZMem(size);
			}
			PVR_LOG_RETURN_IF_NOMEM(psDevInfo->pszFWBuildID, "OSAllocZMem");
			OSStringSafeCopy(psDevInfo->pszFWBuildID, footer->fw_build_id, size);

#if defined(SUPPORT_TRUSTED_DEVICE)
			return gpu_secure_prepare_firmware_image(hSysData, psFWParams);
#else
			/* Make a copy of the footer for mapping during late init */
			memcpy(&pixel_dev->fw_footer, footer, sizeof(*footer));
			return PVRSRV_OK;
#endif /* defined(SUPPORT_TRUSTED_DEVICE) */
		}

		entry_base = fw_base + size;
	} while (1);
}

/**
 * SysDevInit() - SOC-specific device initialization
 * @osdev:	pointer to the GPU's struct device
 * @devcfg:	returned device configuration info
 *
 * Return:	PVRSRV_OK on success, a failure code otherwise
 */
PVRSRV_ERROR SysDevInit(void *osdev, PVRSRV_DEVICE_CONFIG **devcfg)
{
	struct device * const dev = (struct device *)osdev;

	struct pixel_gpu_device *pixel_dev = NULL;
	PVRSRV_DEVICE_CONFIG *cfg = NULL;
	RGX_DATA *rgx_data = NULL;
	struct resource *reg_res;
	int irq;
	int ret;
	u32 init_frequency;

	PVR_DPF((PVR_DBG_CUSTOMER, "> %s", __func__));

	// Inform the kernel that the GPU can access the full 40-bit IPA space, so it doesn't need
	// to allocate "bounce buffers" for reading/writing data outside the GPU's accessible
	// region.
	dma_set_mask(dev, DMA_BIT_MASK(40));

	reg_res = platform_get_resource_byname(to_platform_device(dev), IORESOURCE_MEM, "rgx");
	if (!reg_res) {
		ret = PVRSRV_ERROR_INIT_FAILURE;
		goto init_fail;
	}
	PVR_DPF((PVR_DBG_CUSTOMER, "regs: %pa .. %pa", &reg_res->start, &reg_res->end));

	irq = platform_get_irq(to_platform_device(dev), 0);
	if (irq < 0) {
		dev_err(dev, "platform_get_irq: %d", irq);
		ret = PVRSRV_ERROR_INIT_FAILURE;
		goto init_fail;
	}
	PVR_DPF((PVR_DBG_CUSTOMER, "irq: %d", irq));

	pixel_dev = kzalloc(sizeof(struct pixel_gpu_device), GFP_KERNEL);
	if (!pixel_dev) {
		ret = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto init_fail;
	}
	pixel_dev->dev = dev;

	cfg = kzalloc(sizeof(PVRSRV_DEVICE_CONFIG) +
		      sizeof(RGX_DATA) +
		      sizeof(RGX_TIMING_INFORMATION),
		      GFP_KERNEL);
	if (!cfg) {
		ret = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto init_fail;
	}
	rgx_data = (RGX_DATA *)((char *)cfg + sizeof(PVRSRV_DEVICE_CONFIG));
	cfg->hDevData = rgx_data;
	rgx_data->psRGXTimingInfo = (RGX_TIMING_INFORMATION *)((char *)rgx_data + sizeof(RGX_DATA));

	cfg->pvOSDevice			= osdev;
	cfg->pszName			= PIXEL_GPU_GENERATION;
	cfg->sRegsCpuPBase.uiAddr	= reg_res->start;
	cfg->ui32RegsSize		= resource_size(reg_res);
	cfg->ui32IRQ			= irq;
	cfg->eSystemCoherencyMode       = PVRSRV_DEVICE_SNOOP_CPU_ONLY;
	cfg->hSysData			= pixel_dev;
	cfg->bHasNonMappableLocalMemory = false;
	cfg->bHasFBCDCVersion31		= true;
	cfg->eDefaultHeap		= PVRSRV_PHYS_HEAP_GPU_LOCAL;
	cfg->pasPhysHeaps		= (PHYS_HEAP_CONFIG *)&phys_heaps[0];
	cfg->ui32PhysHeapCount		= ARRAY_SIZE(phys_heaps);
	cfg->pfnPrePowerState 		= NULL;
	cfg->pfnPostPowerState 		= NULL;
	cfg->pfnGpuDomainPower		= NULL;
	cfg->bDevicePA0IsValid		= false;
	cfg->pfnSysDevErrorNotify	= gpu_error_notify;
	cfg->pfnPrepareFWImage 		= prepare_firmware_image;

#if defined(SUPPORT_TRUSTED_DEVICE)
	cfg->pfnTDSendFWImage    = gpu_secure_send_firmware_image;
	cfg->pfnTDSetPowerParams = gpu_secure_set_power_params;
	cfg->pfnTDRGXStart       = gpu_secure_start;
	cfg->pfnTDRGXStop        = gpu_secure_stop;
	cfg->pfnTDRGXDebugDump   = gpu_secure_fault;
#endif
#if defined(PVR_ANDROID_HAS_DMA_HEAP_FIND)
	/*! Name of DMA heap to allocate secure memory from. Used with dma_heap_find. */
	cfg->pszSecureDMAHeapName = PVR_SECURE_DMA_HEAP_NAME;
#endif

#if defined(SUPPORT_FOREIGN_FENCE_TOKEN) || defined(SUPPORT_FASTPATH_FENCE)
	cfg->pfnSysDevExtractFFToken = extract_fence_data;
#endif
#if defined(SUPPORT_FOREIGN_FENCE_TOKEN)
	cfg->pfnSysDevReleaseFF = release_fence_data;
#endif
	cfg->pfnRecordWorkPeriod = work_period_callback;

	if (of_property_read_u32_index(dev->of_node, "init-frequency-hz", 0, &init_frequency)) {
		dev_err(dev, "failed to read init-frequency-hz value from device tree");
		ret = PVRSRV_ERROR_INIT_FAILURE;
		goto init_fail;
	}
	rgx_data->psRGXTimingInfo->ui32CoreClockSpeed = init_frequency;
	rgx_data->psRGXTimingInfo->ui32SOCClockSpeed = GTC_FREQUENCY_HZ;
	rgx_data->psRGXTimingInfo->ui32USCTimerFreq = GTC_FREQUENCY_HZ;

	pixel_dev->dev_config = cfg;

	time_multiplier_set = false;

	ret = subsystem_init(pixel_dev);
	if (ret)
		goto init_fail;

	*devcfg = cfg;
	PVR_DPF((PVR_DBG_CUSTOMER, "< %s", __func__));
	return PVRSRV_OK;

init_fail:
	kfree(cfg);
	kfree(pixel_dev);
	return ret;
}

/**
 * SysDevDeInit() - SOC-specific device teardown
 * @devcfg:	device configuration to be deallocated
 */
void SysDevDeInit(PVRSRV_DEVICE_CONFIG *devcfg)
{
	struct pixel_gpu_device *pixel_dev = devcfg->hSysData;

	subsystem_deinit(pixel_dev, ARRAY_SIZE(subsys_init));

	kfree(devcfg);
	kfree(pixel_dev);
}

/**
 * SysDebugInfo() - dump SOC-specific device debug info
 * @devcfg:	device configuration to dump debug info for
 * @dbg_printf:	the printf-style function to send debug info to
 * @dbg_file:	optional file to pass to @dbg_printf
 *
 * Return:	PVRSRV_OK on success, a failure code otherwise
 */
PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *devcfg, DUMPDEBUG_PRINTF_FUNC *dbg_printf,
			  void *dbg_file)
{
	PVR_DPF((PVR_DBG_CUSTOMER, "%s", __func__));
	return PVRSRV_OK;
}

/**
 * SysInstallDeviceLISR() - register an interrupt service routine
 * @sysdata:        SOC-specific device data
 * @irq:            the IRQ number to register an ISR for
 * @name:	    name of the module registering the ISR
 * @isr:            interrupt service routine to register
 * @isr_data:       opaque data to pass to the ISR
 * @out_isr_handle: returned handle to the registered isr, for unregistering
 *
 * Return:	PVRSRV_OK on success, a failure code otherwise
 */
PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE sysdata, IMG_UINT32 irq, const IMG_CHAR *name,
				  PFN_LISR isr, void *isr_data,
				  IMG_HANDLE *out_isr_handle)
{
	PVR_UNREFERENCED_PARAMETER(sysdata);

	return OSInstallSystemLISR(out_isr_handle, irq, name, isr, isr_data,
				   SYS_IRQ_FLAG_TRIGGER_DEFAULT);
}

/**
 * SysUninstallDeviceLISR() - unregister an interrupt service routine
 * @isr_handle:	handle of the isr to be unregistered
 *
 * Return:	PVRSRV_OK on success, a failure code otherwise
 */
PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE isr_handle)
{
	return OSUninstallSystemLISR(isr_handle);
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Pixel PowerVR GPU Driver");
MODULE_INFO(fw_abi_ver, __stringify(FW_ABI_VERSION));
MODULE_INFO(ddk_version, PVRVERSION_BRANCHNAME);
