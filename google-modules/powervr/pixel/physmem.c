#include <linux/uaccess.h>
#include <linux/mm.h>

#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "pvr_debug.h"
#include "pmr.h"
#include "pmr_impl.h"
#include "pdump_physmem.h"
#include "physmem.h"
#include "devicemem_server_utils.h"


struct private_pmr_data {
	struct device *platform_dev;
	struct pixel_devmap_phys_range *pa_ranges;
	u64 n_pages;
	u32 n_ranges;
};

static PVRSRV_ERROR get_dev_pa(PMR_IMPL_PRIVDATA priv_data, u32 log2_page_size,
					 u32 npages, IMG_DEVMEM_OFFSET_T *offset,
#if defined(SUPPORT_STATIC_IPA)
					 u64 ipa_policy, u64 ipa_clear_mask,
#endif
					 bool *valid, IMG_DEV_PHYADDR *dev_pa)
{
	struct private_pmr_data *data = priv_data;
	u32 i;

#if defined(SUPPORT_STATIC_IPA)
	/* Physical memory is not dram-backed. The ipa policy is not required. */
	(void)ipa_policy, (void)ipa_clear_mask;
#endif

	PVR_DPF((PVR_DBG_CUSTOMER, "%s: mapping %u pages", __func__, npages));

	PVR_LOG_RETURN_IF_FALSE(npages != 0, "unexpected page count", PVRSRV_ERROR_OUT_OF_RANGE);
	PVR_LOG_RETURN_IF_FALSE(PAGE_SHIFT == log2_page_size, "unexpected page size",
				PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY);

	for (i = 0; i < npages; ++i) {
		u64 page_idx, page_offset;
		u32 range_idx;

		if (!valid[i])
			continue;

		/* We must find the abs page index, and offset within the page */
		page_idx = offset[i] >> PAGE_SHIFT;
		page_offset = offset[i] - (page_idx << PAGE_SHIFT);

		PVR_LOG_RETURN_IF_FALSE(page_idx < data->n_pages, "page index out of range",
					PVRSRV_ERROR_OUT_OF_RANGE);
		/* Deduce which PA range to base the mapping on */
		for (range_idx = 0; range_idx < data->n_ranges; ++range_idx) {
			u32 r_pages = ALIGN(data->pa_ranges[range_idx].size, PAGE_SIZE) / PAGE_SIZE;

			if (page_idx < r_pages)
				break;

			page_idx -= r_pages;
		}

		dev_pa[i].uiAddr =
			data->pa_ranges[range_idx].pa + (page_idx << PAGE_SHIFT) + page_offset;

		PVR_DPF((PVR_DBG_CUSTOMER,
			"%s: pa: 0x%llx, raw_offset: 0x%llx, page_offset: 0x%llx", __func__,
			dev_pa[i].uiAddr, offset[i], page_offset));
	}

	return PVRSRV_OK;
}

static PMR_IMPL_FUNCTAB pixel_wrap_register_page_ftable = {
    .pfnLockPhysAddresses = NULL,
    .pfnUnlockPhysAddresses = NULL,
    .pfnDevPhysAddr = get_dev_pa,
    .pfnAcquireKernelMappingData = NULL,
    .pfnReleaseKernelMappingData = NULL,
    .pfnReadBytes = NULL,
    .pfnWriteBytes = NULL,
    .pfnChangeSparseMem = NULL,
    .pfnFinalize = NULL,
};

static PVRSRV_ERROR wrap_phys_range(PVRSRV_DEVICE_NODE *dev_node,
				    struct pixel_devmap_phys_ranges_info *info, u64 size, PMR **pmr)
{
	u32 mapping_table = 0;
	struct private_pmr_data *priv_data;
	PVRSRV_ERROR err = PVRSRV_OK;
	/* Size is already a multiple of PAGE_SIZE */
	u64 const npages = size / PAGE_SIZE;
	PVRSRV_MEMALLOCFLAGS_T flags = info->flags;

	flags |= PVRSRV_MEMALLOCFLAG_SPARSE_NO_SCRATCH_BACKING;

	/* Alloc required struct mem */
	priv_data = kzalloc(sizeof(*priv_data), GFP_KERNEL);
	if (!priv_data)
		err = PVRSRV_ERROR_OUT_OF_MEMORY;
	PVR_LOG_GOTO_IF_ERROR(err, "alloc_failed", alloc_failed);

	*priv_data = (struct private_pmr_data) {
		.platform_dev = dev_node->psDevConfig->pvOSDevice,
		.pa_ranges = info->ranges,
		.n_ranges = info->n_ranges,
		.n_pages = npages,
	};

	/* Create a suitable PMR */
	err = PMRCreatePMR(dev_node->apsPhysHeap[PVRSRV_PHYS_HEAP_CPU_LOCAL], size, 1, 1,
			   &mapping_table, PAGE_SHIFT, (flags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK),
			   "WrappedRegPage", &pixel_wrap_register_page_ftable, priv_data,
			   PMR_TYPE_EXTMEM, pmr, PDUMP_NONE);
	PVR_LOG_GOTO_IF_ERROR(err, "create_pmr_failed", create_pmr_failed);

	/* Mark the PMR such that no layout changes can happen.
	 * The memory is allocated in the CPU domain and hence no changes can be made through any
	 * of our API.
	 */
	PMR_SetLayoutFixed(*pmr, true);

	return err;

create_pmr_failed:
	kfree(priv_data);

alloc_failed:
	return err;
}

PVRSRV_ERROR devmap_phys_ranges(PVRSRV_DEVICE_NODE *dev_node, DEVMEM_IMPORT **import,
				struct pixel_devmap_phys_ranges_info *info)
{
	PMR *pmr;
	PVRSRV_ERROR err;
	u64 size = 0;
	int i;

	/* Size must be page multiple */
	for (i = 0; i < info->n_ranges; ++i) {
		size += ALIGN(info->ranges[i].size, PAGE_SIZE);
	}

	/* Obtain a PMR that wraps the physical range */
	err = wrap_phys_range(dev_node, info, size, &pmr);
	PVR_LOG_GOTO_IF_ERROR(err, "wrap_failed", wrap_failed);

	/* Allocate required structs for mapping */
	err = DevmemImportStructAlloc(dev_node, import);
	PVR_LOG_GOTO_IF_ERROR(err, "alloc_failed", alloc_failed);

	/* Configure an import that references our new PMR */
	DevmemImportStructInit(
		*import, size, DevmemGetHeapLog2PageSize(info->heap), info->flags, pmr, 0);

	/* Map the sswrp PMR */
	err = DevmemImportStructDevMap(info->heap, true, *import, 0);
	PVR_LOG_GOTO_IF_ERROR(err, "map_failed", map_failed);

	return PVRSRV_OK;

map_failed:
	DevmemImportStructRelease(*import);
alloc_failed:
	PMRUnrefPMR(pmr);
wrap_failed:
	return err;
}
