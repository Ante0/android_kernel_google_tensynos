#pragma once

#include <pvrsrvkm/services/shared/include/devicemem_utils.h>
#include <pvrsrvkm/services/server/include/physmem_extmem.h>
#include <pvrsrvkm/services/server/include/pmr.h>

struct pixel_devmap_phys_range {
	u64 pa;
	u64 size;
};

struct pixel_devmap_phys_ranges_info {
	struct pixel_devmap_phys_range *ranges;
	PVRSRV_MEMALLOCFLAGS_T flags;
	DEVMEM_HEAP *heap;
	u32 n_ranges;
};

PVRSRV_ERROR devmap_phys_ranges(PVRSRV_DEVICE_NODE *dev_node, DEVMEM_IMPORT **import,
				struct pixel_devmap_phys_ranges_info *info);
