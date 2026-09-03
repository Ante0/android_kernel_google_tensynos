/*************************************************************************/ /*!
@File
@Title          Host memory management implementation for Linux
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/version.h>

#include "img_defs.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "process_stats.h"
#include "osfunc.h"


/*
 * When memory statistics are disabled, memory records are used instead.
 * In order for these to work, the PID of the process that requested the
 * allocation needs to be stored at the end of the kmalloc'd memory, making
 * sure 4 extra bytes are allocated to fit the PID.
 *
 * There is no need for this extra allocation when memory statistics are
 * enabled, since all allocations are tracked in DebugFS mem_area files.
 */
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && !defined(PVRSRV_ENABLE_MEMORY_STATS)
/* kmalloc guarantees a minimal alignment which is ARCH_KMALLOC_MINALIGN. This
 * alignment is architecture specific and can be quite big, e.g. on Aarch64
 * it can be 64 bytes. This is too much for keeping a single PID field and could
 * lead to a lot of wasted memory. This is a reason why we're defaulting to 8
 * bytes alignment which should be enough for any architecture.
 */
#define ALLOCMEM_PID_SIZE_PADDING PVR_ALIGN(sizeof(IMG_UINT32), 8)
#else
#define ALLOCMEM_PID_SIZE_PADDING 0UL
#endif

/* How many times kmalloc can fail before the allocation threshold is reduced */
static const IMG_UINT32 g_ui32kmallocFailLimit = 10;
/* How many kmalloc failures happened since the last allocation threshold change */
static IMG_UINT32 g_ui32kmallocFailCount = 0;
/* Current kmalloc threshold value in bytes */
static IMG_UINT32 g_ui32kmallocThreshold = PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD;
/* Spinlock used so that the global variables above may not be modified by more than 1 thread at a time */
static DEFINE_SPINLOCK(kmalloc_lock);


static inline void OSTryDecreaseKmallocThreshold(void)
{
	unsigned long flags;
	spin_lock_irqsave(&kmalloc_lock, flags);

	g_ui32kmallocFailCount++;

	if (g_ui32kmallocFailCount >= g_ui32kmallocFailLimit)
	{
		g_ui32kmallocFailCount = 0;
		if (g_ui32kmallocThreshold > PAGE_SIZE)
		{
			g_ui32kmallocThreshold >>= 1;
			printk(KERN_INFO "Threshold is now set to %d\n", g_ui32kmallocThreshold);
		}
	}

	spin_unlock_irqrestore(&kmalloc_lock, flags);
}

static inline void OSResetKmallocFailCount(void)
{
	unsigned long flags;
	spin_lock_irqsave(&kmalloc_lock, flags);

	g_ui32kmallocFailCount = 0;

	spin_unlock_irqrestore(&kmalloc_lock, flags);
}

static inline void _pvr_vfree(const void* pvAddr)
{
#if defined(DEBUG)
	/* Size harder to come by for vmalloc and since vmalloc allocates
	 * a whole number of pages, poison the minimum size known to have
	 * been allocated.
	 */
	OSCachedMemSet((void*)pvAddr, PVRSRV_POISON_ON_ALLOC_VALUE,
	               PAGE_SIZE);
#endif
	vfree(pvAddr);
}

static inline void _pvr_kfree(const void* pvAddr)
{
#if defined(DEBUG)
	/* Poison whole memory block */
	OSCachedMemSet((void*)pvAddr, PVRSRV_POISON_ON_ALLOC_VALUE,
	               ksize(pvAddr));
#endif
	kfree(pvAddr);
}

static inline void *_pvr_alloc_stats_add(void *pvAddr, size_t uiSize DEBUG_MEMSTATS_PARAMS)
{
#if !defined(PVRSRV_ENABLE_PROCESS_STATS) || !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
	PVR_UNREFERENCED_PARAMETER(pvAddr);
#else
	if (!is_vmalloc_addr(pvAddr))
	{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC_BYTES,
									  pvAddr,
									  sCpuPAddr,
									  ksize(pvAddr),
									  OSGetCurrentClientProcessIDKM()
									  DEBUG_MEMSTATS_ARGS);
#else
		/* because clang has some features that allow detection out-of-bounds
		 * access we need to put the metadata in the beginning of the allocation */
		*(IMG_UINT32 *) pvAddr = OSGetCurrentClientProcessIDKM();
		PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_KMALLOC_BYTES, ksize(pvAddr),
		                            *(IMG_UINT32 *) pvAddr);

		/* because metadata is kept in the beginning of the allocation we need
		 * to return address offset by the ALLOCMEM_PID_SIZE_PADDING */
		pvAddr = (IMG_UINT8 *) pvAddr + ALLOCMEM_PID_SIZE_PADDING;
#endif /* defined(PVRSRV_ENABLE_MEMORY_STATS) */
	}
	else
	{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC_BYTES,
									  pvAddr,
									  sCpuPAddr,
									  PVR_ALIGN(uiSize, PAGE_SIZE),
									  OSGetCurrentClientProcessIDKM()
									  DEBUG_MEMSTATS_ARGS);
#else
		PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE_VMALLOC_BYTES,
		                                    PVR_ALIGN(uiSize, PAGE_SIZE),
		                                    (IMG_UINT64)(uintptr_t) pvAddr,
		                                    OSGetCurrentClientProcessIDKM());
#endif /* defined(PVRSRV_ENABLE_MEMORY_STATS) */
	}
#endif /* !defined(PVRSRV_ENABLE_PROCESS_STATS) || !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) */

	return pvAddr;
}

static inline void *_pvr_alloc_stats_remove(void *pvAddr)
{
#if !defined(PVRSRV_ENABLE_PROCESS_STATS) || !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
	PVR_UNREFERENCED_PARAMETER(pvAddr);
#else
	if (!is_vmalloc_addr(pvAddr))
	{
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		/* because metadata is kept in the beginning of the allocation we need
		 * shift address offset by the ALLOCMEM_PID_SIZE_PADDING to the original
		 * value */
		pvAddr = (IMG_UINT8 *) pvAddr - ALLOCMEM_PID_SIZE_PADDING;

		/* first 4 bytes of the allocation are the process' PID */
		PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_KMALLOC_BYTES, ksize(pvAddr), *(IMG_UINT32 *) pvAddr);
#else
		PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC_BYTES,
		                                (IMG_UINT64)(uintptr_t) pvAddr,
		                                OSGetCurrentClientProcessIDKM());
#endif
	}
	else
	{
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		PVRSRVStatsDecrMemAllocStatAndUntrack(PVRSRV_MEM_ALLOC_TYPE_VMALLOC_BYTES,
		                                      (IMG_UINT64)(uintptr_t) pvAddr);
#else
		PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC_BYTES,
		                                (IMG_UINT64)(uintptr_t) pvAddr,
		                                OSGetCurrentClientProcessIDKM());
#endif
	}
#endif /* !defined(PVRSRV_ENABLE_PROCESS_STATS) || !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) */

	return pvAddr;
}

void *(OSAllocMem_noprof)(size_t uiSize DEBUG_MEMSTATS_PARAMS)
{
	void *pvRet = NULL;
	size_t uiAllocSize = uiSize + ALLOCMEM_PID_SIZE_PADDING;

	if (uiSize == 0)
	{
		return IMG_ZERO_SIZE_PTR;
	}

	// Check for overflow.
	PVR_ASSERT(uiAllocSize >= uiSize);
	if (uiAllocSize < uiSize)
	{
		return NULL;
	}

	if (uiAllocSize <= g_ui32kmallocThreshold)
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		pvRet = kmalloc_noprof(uiAllocSize, GFP_KERNEL);
#else
		pvRet = kmalloc(uiAllocSize, GFP_KERNEL);
#endif
		if (pvRet == NULL)
		{
			OSTryDecreaseKmallocThreshold();
		}
		else
		{
			OSResetKmallocFailCount();
		}
	}

	if (pvRet == NULL)
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		pvRet = vmalloc_noprof(uiAllocSize);
#else
		pvRet = vmalloc(uiAllocSize);
#endif
	}

	if (pvRet != NULL)
	{
		pvRet = _pvr_alloc_stats_add(pvRet, uiSize DEBUG_MEMSTATS_ARGS);
	}

	return pvRet;
}

void *(OSAllocZMem_noprof)(size_t uiSize DEBUG_MEMSTATS_PARAMS)
{
	void *pvRet = NULL;
	size_t uiAllocSize = uiSize + ALLOCMEM_PID_SIZE_PADDING;

	if (uiSize == 0)
	{
		return IMG_ZERO_SIZE_PTR;
	}

	// Check for overflow.
	PVR_ASSERT(uiAllocSize >= uiSize);
	if (uiAllocSize < uiSize)
	{
		return NULL;
	}

	if (uiAllocSize <= g_ui32kmallocThreshold)
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		pvRet = kzalloc_noprof(uiAllocSize, GFP_KERNEL);
#else
		pvRet = kzalloc(uiAllocSize, GFP_KERNEL);
#endif
		if (pvRet == NULL)
		{
			OSTryDecreaseKmallocThreshold();
		}
		else
		{
			OSResetKmallocFailCount();
		}
	}

	if (pvRet == NULL)
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		pvRet = vzalloc_noprof(uiAllocSize);
#else
		pvRet = vzalloc(uiAllocSize);
#endif
	}

	if (pvRet != NULL)
	{
		pvRet = _pvr_alloc_stats_add(pvRet, uiSize DEBUG_MEMSTATS_ARGS);
	}

	return pvRet;
}

/*
 * The parentheses around OSFreeMem prevent the macro in allocmem.h from
 * applying, as it would break the function's definition.
 */
void (OSFreeMem)(void *pvMem)
{
	PVR_ASSERT(pvMem != IMG_ZERO_SIZE_PTR);
	if (pvMem == IMG_ZERO_SIZE_PTR)
	{
		return;
	}


	if (pvMem != NULL)
	{
		pvMem = _pvr_alloc_stats_remove(pvMem);

		if (!is_vmalloc_addr(pvMem))
		{
			_pvr_kfree(pvMem);
		}
		else
		{
			_pvr_vfree(pvMem);
		}
	}
}

void *OSAllocMemNoStats_noprof(size_t uiSize)
{
	void *pvRet = NULL;

	PVR_ASSERT(uiSize != 0);

	if (uiSize <= g_ui32kmallocThreshold)
	{
		/* Unlike OSAllocMem we don't add extra length to the allocation so,
		 * we'll let kzalloc handle the `uiSize = 0` case. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		pvRet = kmalloc_noprof(uiSize, GFP_KERNEL);
#else
		pvRet = kmalloc(uiSize, GFP_KERNEL);
#endif
		if (pvRet == NULL)
		{
			OSTryDecreaseKmallocThreshold();
		}
		else
		{
			OSResetKmallocFailCount();
		}
	}

	if (pvRet == NULL)
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		pvRet = vmalloc_noprof(uiSize);
#else
		pvRet = vmalloc(uiSize);
#endif
	}

	return pvRet;
}

void *OSAllocZMemNoStats_noprof(size_t uiSize)
{
	void *pvRet = NULL;

	PVR_ASSERT(uiSize != 0);

	if (uiSize <= g_ui32kmallocThreshold)
	{
		/* Unlike OSAllocZMem we don't add extra length to the allocation so,
		 * we'll let kzalloc handle the `uiSize = 0` case. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		pvRet = kzalloc_noprof(uiSize, GFP_KERNEL);
#else
		pvRet = kzalloc(uiSize, GFP_KERNEL);
#endif
		if (pvRet == NULL)
		{
			OSTryDecreaseKmallocThreshold();
		}
		else
		{
			OSResetKmallocFailCount();
		}
	}

	if (pvRet == NULL)
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
		pvRet = vzalloc_noprof(uiSize);
#else
		pvRet = vzalloc(uiSize);
#endif
	}

	return pvRet;
}

/*
 * The parentheses around OSFreeMemNoStats prevent the macro in allocmem.h from
 * applying, as it would break the function's definition.
 */
void (OSFreeMemNoStats)(void *pvMem)
{
	PVR_ASSERT(pvMem != IMG_ZERO_SIZE_PTR);
	if (pvMem == IMG_ZERO_SIZE_PTR)
	{
		return;
	}


	if (pvMem != NULL)
	{
		if (!is_vmalloc_addr(pvMem))
		{
			_pvr_kfree(pvMem);
		}
		else
		{
			_pvr_vfree(pvMem);
		}
	}
}
