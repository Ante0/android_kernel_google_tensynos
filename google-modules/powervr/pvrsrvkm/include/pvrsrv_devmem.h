/*************************************************************************/ /*!
@File
@Title          Device Memory Management core
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Client side part of device memory management. Defines the
                exposed Services API to core memory management functions.
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

#ifndef PVRSRV_DEVMEM_H
#define PVRSRV_DEVMEM_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "img_types.h"
#include "img_defs.h"
#include "powervr/mem_types.h"
#include "devicemem_typedefs.h"
#include "pdumpdefs.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "services_km.h" /* for PVRSRV_DEV_CONNECTION */

/*!
* @Defgroup DevMemAPIs Device memory interface
* @Brief The document groups/lists the interfaces provided by the Services for using the device memory
* @{
*/

/*
  @Brief Device memory contexts, heaps and memory descriptors are passed
  through to underlying memory APIs directly, but are to be regarded
  as an opaque handle externally.
*/
#ifndef PVRSRV_DEV_MEM_TYPEDEFS
#define PVRSRV_DEV_MEM_TYPEDEFS
typedef struct PVRSRV_DEVMEMCTX_TAG *PVRSRV_DEVMEMCTX;       /*!< Device-Mem Client-Side Interface: Typedef for Context Ptr */
typedef DEVMEM_HEAP *PVRSRV_HEAP;               /*!< Device-Mem Client-Side Interface: Typedef for Heap Ptr */
typedef DEVMEM_MEMDESC *PVRSRV_MEMDESC;         /*!< Device-Mem Client-Side Interface: Typedef for Memory Descriptor Ptr */
#endif
typedef DEVMEM_EXPORTCOOKIE PVRSRV_DEVMEM_EXPORTCOOKIE;     /*!< Device-Mem Client-Side Interface: Typedef for Export Cookie */
typedef IMG_HANDLE PVRSRV_REMOTE_DEVMEMCTX;                 /*!< Type to use with context export import */
typedef struct PVRSRV_EXPORT_DEVMEMCTX_TAG *PVRSRV_EXPORT_DEVMEMCTX;

/* To use with PVRSRVSubAllocDeviceMem() as the default factor if no
 * over-allocation is desired. */
#define PVRSRV_DEVMEM_PRE_ALLOC_MULTIPLIER_NONE     DEVMEM_NO_PRE_ALLOCATE_MULTIPLIER

/* N.B.  Flags are now defined in pvrsrv_memallocflags.h as they need
         to be omnipresent. */

/*! @} End of Defgroup DevMemAPIs */

/*
 *
 *  API functions
 *
 */

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVCreateDeviceMemContext
@Description    Creates a device memory context.  There is a one-to-one
                correspondence between this context data structure and the
                top level MMU page table (known as the Page Catalogue, in the
                case of a 3-tier MMU). It is intended that a process with its
                own virtual space on the CPU will also have its own virtual
                space on the GPU. Thus there is loosely a one-to-one
                correspondence between process and device memory context, but
                this is not enforced at this API.

                Every process must create the device memory context before any
                memory allocations are made, and is responsible for freeing
                all such allocations before destroying the context

                This is a wrapper function above the "bare-metal" device
                memory context creation function which would create just a
                context and no heaps. This function will also create the
                heaps, according to the heap config that the device specific
                initialization code has nominated for use by this API.

                The number of heaps thus created is returned to the caller,
                such that the caller can allocate an array and the call in to
                fetch details of each heap, or look up the heap with the
                "Find Heap" API described below.

                In order to derive the details of the MMU configuration for
                the device, and for retrieving the "bridge handle" for
                communication internally in services, it is necessary to pass
                in a PVRSRV_DEV_CONNECTION.
@Input          psDevConnection Device that the memory should be allocated
                                from
@Output         phCtxOut        On success, the returned DevMem Context. The
                                caller is responsible for providing storage
                                for this.
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVCreateDeviceMemContext(PVRSRV_DEV_CONNECTION *psDevConnection,
                             PVRSRV_DEVMEMCTX *phCtxOut);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVReleaseDeviceMemContext
@Description    Release cannot fail.  Well.  It shouldn't, assuming the caller
                has obeyed the protocol, i.e. has freed all his allocations
                beforehand.
@Input          hCtx            Handle to a DevMem Context
@Return         None
*/ /**************************************************************************/
IMG_EXPORT void
PVRSRVReleaseDeviceMemContext(PVRSRV_DEVMEMCTX hCtx);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVFindHeapByName
@Description    Returns the heap handle for the named heap which is assumed to
                exist in this context. PVRSRV_HEAP *phHeapOut,

                N.B.  No need for acquire/release semantics here, as when
                using this wrapper layer, the heaps are automatically
                instantiated at context creation time and destroyed when the
                context is destroyed.

                The caller is required to know the heap names already as these
                will vary from device to device and from purpose to purpose.
@Input          hCtx            Handle to a DevMem Context
@Input          pszHeapName     Name of the heap to look for
@Output         phHeapOut       a handle to the heap, for use in future calls
                                to OpenAllocation / AllocDeviceMemory / Map
                                DeviceClassMemory, etc. (The PVRSRV_HEAP type
                                to be regarded by caller as an opaque, but
                                strongly typed, handle)
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVFindHeapByName(PVRSRV_DEVMEMCTX hCtx,
                     const IMG_CHAR *pszHeapName,
                     PVRSRV_HEAP *phHeapOut);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVDevmemGetHeapBaseDevVAddr
@Description    returns the device virtual address of the base of the heap.
@Input          hHeap           Handle to a Heap
@Output         pDevVAddr       On success, the device virtual address of the
                                base of the heap.
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVDevmemGetHeapBaseDevVAddr(PVRSRV_HEAP hHeap,
                                IMG_DEV_VIRTADDR *pDevVAddr);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVDevmemGetHeapSize
@Description    returns the size of the heap.
@Input          hHeap           Handle to a Heap
@Return         The size of the heap.
*/ /**************************************************************************/
IMG_EXPORT DEVMEM_SIZE_T
PVRSRVDevmemGetHeapSize(PVRSRV_HEAP hHeap);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVSubAllocDeviceMem
@Description    Allocate memory from the specified heap, acquiring physical
                memory from OS. The allocated memory is not mapped to either
                CPU or GPU.

                Size must be a positive integer multiple of alignment, or, to
                put it another way, the uiLog2Align LSBs should all be zero,
                but at least one other bit should not be.

                Caller to take charge of the PVRSRV_MEMDESC (the memory
                descriptor) which is to be regarded as an opaque handle.

@Input          uiPreAllocMultiplier  Size factor for internal pre-allocation of
                                      memory to make subsequent calls with the
                                      same flags faster. Independently if a value
                                      is set, the function will try to allocate
                                      from any pre-allocated memory first and -if
                                      successful- not pre-allocate anything more.
                                      That means the factor can always be set and
                                      the correct thing will be done internally.
@Input          hHeap                 Handle to the heap from which memory will be
                                      allocated
@Input          uiSize                Amount of memory to be allocated.
@Input          uiLog2Align           LOG2 of the required alignment
@Input          uiMemAllocFlags       Allocation Flags
@Input          pszText               Allocation descriptive name, this will
                                      be truncated to the number of characters
                                      specified in the PVR_ANNOTATION_MAX_LEN.
@Output         phMemDescOut          On success, the resulting memory descriptor
@Return         PVRSRV_OK on success. Otherwise, a PVRSRV_ error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVSubAllocDeviceMem(IMG_UINT8 uiPreAllocMultiplier,
                        PVRSRV_HEAP hHeap,
                        IMG_DEVMEM_SIZE_T uiSize,
                        IMG_DEVMEM_LOG2ALIGN_T uiLog2Align,
                        PVRSRV_MEMALLOCFLAGS_T uiMemAllocFlags,
                        const IMG_CHAR *pszText,
                        PVRSRV_MEMDESC *phMemDescOut);

#define PVRSRVAllocDeviceMem(...) \
    PVRSRVSubAllocDeviceMem(PVRSRV_DEVMEM_PRE_ALLOC_MULTIPLIER_NONE, __VA_ARGS__)

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVGetDefaultPhysicalHeap
@Description    For the specified device, get the physical heap used for
                allocations when the PVRSRV_PHYS_HEAP_DEFAULT
                physical heap hint is set in memalloc flags.
@Input          psConnection                Connection handle
@Output         peHeap                      Default Physheap setting
@Return         PVRSRV_OK on success. Otherwise, a PVRSRV_
                error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVGetDefaultPhysicalHeap(PVRSRV_DEV_CONNECTION *psConnection,
                             PVRSRV_PHYS_HEAP *peHeap);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVPhysHeapGetMemInfo
@Description    Get the size and free memory of the specified physical heaps.
                Note: the free memory reported is only a sample, and may be
                less by the time the caller carries out an allocation due to
                system dynamics, hence callers should allow for this.
@Input          psConnection        Connection handle
@Input          ui32PhysHeapCount   Physical heap count
@Input          paePhysHeapID       Array of PhysHeapID's the statistics
                                    are queried for.
@Output         paPhysHeapMemStats  Physical heap memory statistics buffer
                                    pre-allocated for ui32PhysHeapCount heaps.
@Return         eError   PVRSRV_OK on success. Otherwise, a PVRSRV_
                         error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVPhysHeapGetMemInfo(PVRSRV_DEV_CONNECTION *psConnection,
                         IMG_UINT32 ui32PhysHeapCount,
                         PVRSRV_PHYS_HEAP *paePhysHeapID,
                         PHYS_HEAP_MEM_STATS *paPhysHeapMemStats);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVDevmemGetMaxPhysBufferSizes
@Description    Get the maximum size of the virtual and physical buffers
                possible to allocate by the driver.
@Input          psConnection              Connection handle
@Return         Maximum physical allocation that can be requested from the
                Server.
*/ /**************************************************************************/
IMG_EXPORT IMG_UINT64
PVRSRVDevmemGetMaxPhysBufferSizes(PVRSRV_DEV_CONNECTION *psConnection);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVFreeDeviceMem
@Description    Free that allocated by PVRSRVSubAllocDeviceMem (Memory
                descriptor will be destroyed)
@Input          hMemDesc            Handle to the descriptor of the memory
                                    to be freed
@Return         None
*/ /**************************************************************************/
IMG_EXPORT void
PVRSRVFreeDeviceMem(PVRSRV_MEMDESC hMemDesc);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVAcquireCPUMapping
@Description    Causes the allocation referenced by this memory descriptor to
                be mapped into CPU virtual memory, if it wasn't already, and
                the CPU virtual address returned in the caller-provided
                location.

                The caller must call PVRSRVReleaseCPUMapping to advise when he
                has finished with the mapping.

@Input          hMemDesc            Handle to the memory descriptor for which
                                    a CPU mapping is required
@Output         ppvCpuVirtAddrOut   On success, the caller's ptr is set to the
                                    new CPU mapping
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVAcquireCPUMapping(PVRSRV_MEMDESC hMemDesc,
                        void **ppvCpuVirtAddrOut);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVReleaseCPUMapping
@Description    Relinquishes the cpu mapping acquired with
                PVRSRVAcquireCPUMapping()
@Input          hMemDesc            Handle of the memory descriptor
@Return         None
*/ /**************************************************************************/
IMG_EXPORT void
PVRSRVReleaseCPUMapping(PVRSRV_MEMDESC hMemDesc);


/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVMapToDevice
@Description    Map allocation into the device MMU. This function must only be
                called once, any further calls will return
                PVRSRV_ERROR_DEVICEMEM_ALREADY_MAPPED

                The caller must call PVRSRVReleaseDeviceMapping when they are
                finished with the mapping.

@Input          hMemDesc            Handle of the memory descriptor
@Input          hHeap               Device heap to map the allocation into
@Output         psDevVirtAddrOut    Device virtual address
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVMapToDevice(PVRSRV_MEMDESC hMemDesc,
                  PVRSRV_HEAP hHeap,
                  IMG_DEV_VIRTADDR *psDevVirtAddrOut);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVMapToDeviceAddress
@Description    Same as PVRSRVMapToDevice but caller chooses the address to
                map into.

                The caller is able to overwrite existing mappings so never use
                this function on a heap where PVRSRVMapToDevice() has been
                used before or will be used in the future.

                In general the caller has to know which regions of the heap
                have been mapped already and should avoid overlapping mappings.

@Input          psMemDesc           Pointer of the memory descriptor
@Input          psHeap              Device heap to map the allocation into
@Input          sDevVirtAddr        Device virtual address to map to
@Return         PVRSRV_ERROR        PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVMapToDeviceAddress(DEVMEM_MEMDESC *psMemDesc,
                         DEVMEM_HEAP *psHeap,
                         IMG_DEV_VIRTADDR sDevVirtAddr);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVMapToDeviceAsCPUReflection
@Description    Maps a DEVMEM_MEMDESC to an existing CPU address in the
                device's SVM Heap address space. The DEVMEM_MEMDESC must have
                been created from a DMA buffer import.

                The DEVMEM_MEMDESC must not have been created with
                PVRSRV_MEMALLOCFLAG_SVM_ALLOC set.

@Input          hMemDesc            Pointer of the memory descriptor
@Input          psSVMHeap           The SVM heap.
@Input          pvCPUVirtAddr       The CPU address that will be used in the
                                    device mapping, this address must be within
                                    the bounds of the device memory heap supplied.
@Return         PVRSRV_ERROR        PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVMapToDeviceAsCPUReflection(DEVMEM_MEMDESC *psMemDesc,
                                 PVRSRV_HEAP psSVMHeap,
                                 IMG_CPU_VIRTADDR pvCPUVirtAddr);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVGetDeviceVirtualAddress
@Description    Obtain the device VA for a previously mapped allocation. Must
                only be called after PVRSRVMapToDevice(Address) has been called.
                If the allocation wasn't mapped into the device then PVR_ASSERT
                is called and 0 is return.

@Input          psMemDesc           Handle to the memory descriptor for which a
                                    device mapping is required
@Return         IMG_DEV_VIRTADDR    A valid device address on success,
                                    0 otherwise.
*/ /**************************************************************************/
IMG_EXPORT IMG_DEV_VIRTADDR
PVRSRVGetDeviceVirtualAddress(DEVMEM_MEMDESC *psMemDesc);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVAcquireDeviceMapping
@Description    Acquire a reference on the device mapping the allocation.
                If the allocation wasn't mapped into the device then
                and the device virtual address returned in the
                PVRSRV_ERROR_DEVICEMEM_NO_MAPPING will be returned as
                PVRSRVMapToDevice must be called first.

                The caller must call PVRSRVReleaseDeviceMapping when they
                are finished with the mapping.

@Input          hMemDesc            Handle to the memory descriptor for which a
                                    device mapping is required
@Output         psDevVirtAddrOut    On success, the caller's ptr is set to the
                                    new device mapping
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVAcquireDeviceMapping(PVRSRV_MEMDESC hMemDesc,
                           IMG_DEV_VIRTADDR *psDevVirtAddrOut);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVReleaseDeviceMapping
@Description    Relinquishes the device mapping acquired with
                PVRSRVAcquireDeviceMapping, PVRSRVMapToDevice or
                PVRSRVMapToDeviceAddress
@Input          hMemDesc            Handle of the memory descriptor
@Return         None
*/ /**************************************************************************/
IMG_EXPORT void
PVRSRVReleaseDeviceMapping(PVRSRV_MEMDESC hMemDesc);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs

@Function       PVRSRVDevmemLocalImport

@Description    Import a PMR that was created with this connection.
                The general usage of this function is as follows:
                1) Create a devmem allocation on server side.
                2) Pass back the PMR of that allocation to client side by
                   creating a handle of type PMR_LOCAL_EXPORT_HANDLE.
                3) Pass the PMR_LOCAL_EXPORT_HANDLE to
                   PVRSRVMakeLocalImportHandle()to create a new handle type
                   (DEVMEM_MEM_IMPORT) that can be used with this function.

@Input          psDevConnection         Device that the memory should be allocated
                                        from

@Input          hExtHandle              Handle of the memory

@Input          uiFlags                 Import flags

@Output         phMemDescPtr            Created MemDesc

@Output         puiSizePtr              Size of the created MemDesc

@Input          pszAnnotation           Allocation descriptive name, this will
                                        be truncated to the number of characters
                                        specified in the PVR_ANNOTATION_MAX_LEN.

@Return         PVRSRV_OK is successful
*/
/*****************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVDevmemLocalImport(const PVRSRV_DEV_CONNECTION *psDevConnection,
                                     IMG_HANDLE hExtHandle,
                                     PVRSRV_MEMALLOCFLAGS_T uiFlags,
                                     PVRSRV_MEMDESC *phMemDescPtr,
                                     IMG_DEVMEM_SIZE_T *puiSizePtr,
                                     const IMG_CHAR *pszAnnotation);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVDevmemGetImportUID

@Description    Get the UID of the import that backs this MemDesc

@Input          hMemDesc                MemDesc
@Input          pui64UID                UID of import

@Return         PVRSRV_ERROR            PVRSRV_OK on success. Otherwise,
                                        an error code
*/
/*****************************************************************************/
IMG_EXPORT PVRSRV_ERROR PVRSRVDevmemGetImportUID(PVRSRV_MEMDESC hMemDesc,
                                      IMG_UINT64 *pui64UID);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVAllocExportableDevMem
@Description    Allocate memory without mapping into device memory context.
                This memory is exported and ready to be mapped into the device
                memory context of other processes, or to CPU only with
                PVRSRVMapMemoryToCPUOnly(). The caller agrees to later call
                PVRSRVFreeUnmappedExportedMemory(). The caller must give the
                page size of the heap into which this memory may be
                subsequently mapped, or the largest of such page sizes if it
                may be mapped into multiple places. This information is to be
                communicated in the Log2Align field.

                Size must be a positive integer multiple of the page size
@Input          psDevConnection     Device that the memory should be allocated
                                    from
@Input          uiSize              the amount of memory to be allocated
@Input          uiLog2Align         Log2 of the alignment required
@Input          uiLog2HeapPageSize  The page size to allocate. Must be a
                                    multiple of the heap that this is going
                                    to be mapped into.
@Input          uiFlags             Allocation flags
@Input          pszText             Text to describe the allocation, this will
                                    be truncated to the number of characters
                                    specified in the PVR_ANNOTATION_MAX_LEN.
@Output         hMemDesc
@Return         PVRSRV_OK on success. Otherwise, a PVRSRV_ error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVAllocExportableDevMem(const PVRSRV_DEV_CONNECTION *psDevConnection,
                            IMG_DEVMEM_SIZE_T uiSize,
                            IMG_DEVMEM_LOG2ALIGN_T uiLog2Align,
                            IMG_UINT32 uiLog2HeapPageSize,
                            PVRSRV_MEMALLOCFLAGS_T uiFlags,
                            const IMG_CHAR *pszText,
                            PVRSRV_MEMDESC *hMemDesc);

/*************************************************************************/ /*!
@Function       PVRSRVChangeSparseGrowMem
@Description    This function alters the underlying memory layout of the given
                allocation by allocating pages as requested
                This function also re-writes the GPU & CPU Maps accordingly

@Input          psMemDesc           The memory layout that needs to be modified
@Input          ui32PageCount       New page allocation count
@Input          pai32Indices        New page allocation indices (page granularity)
@Return         PVRSRV_OK on success.
                PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE when indices are invalid.
                PVRSRV_ERROR_OUT_OF_MEM when system memory has been exhausted.
                PVRSRV_ERROR_PMR_INVALID_MAP_INDEX_ARRAY when an index has been
                    duplicated.
                PVRSRV_ERROR_DEVICEMEM_NO_MAPPING when a value in pai32FreeIndices
                    has already been freed.
                PVRSRV_ERROR_DEVICEMEM_ALREADY_MAPPED when a value in pai32AllocIndices
                    has already been allocated.
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVChangeSparseGrowMem(PVRSRV_MEMDESC psMemDesc,
                          IMG_UINT32 ui32PageCount,
                          IMG_UINT32 *pai32Indices);

/*************************************************************************/ /*!
@Function       PVRSRVChangeSparseShrinkMem
@Description    This function alters the underlying memory layout of the given
                allocation by removing pages as requested
                This function also re-writes the GPU & CPU Maps accordingly

@Input          psMemDesc           The memory layout that needs to be modified
@Input          ui32PageCount       New page free count
@Input          pai32Indices        New page free indices (page granularity)
@Return         PVRSRV_OK on success.
                PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE when indices are invalid.
                PVRSRV_ERROR_PMR_INVALID_MAP_INDEX_ARRAY when an index has been
                    duplicated.
                PVRSRV_ERROR_DEVICEMEM_NO_MAPPING when a value in pai32FreeIndices
                    has already been freed.
                PVRSRV_ERROR_DEVICEMEM_ALREADY_MAPPED when a value in pai32AllocIndices
                    has already been allocated.
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVChangeSparseShrinkMem(PVRSRV_MEMDESC psMemDesc,
                            IMG_UINT32 ui32PageCount,
                            IMG_UINT32 *pai32Indices);

/*************************************************************************/ /*!
@Function       PVRSRVAllocSparseDevMem
@Description    Allocate sparse memory without mapping into device memory
                context. Sparse memory is used where you have an allocation
                that has a logical size (i.e. the amount of VM space it will
                need when mapping it into a device) that is larger than the
                amount of physical memory that allocation will use. An example
                of this is a NPOT texture where the twiddling algorithm
                requires you to round the width and height to next POT and so
                you know there will be pages that are never accessed.

                This memory can be exported and mapped into the device
                memory context of other processes, or to CPU address space.

                Size must be a positive integer multiple of the page size, see
                PVRSRVGetHeapLog2PageSize().

                Mapping Table array has ui32NumPhysChunks elements. Each
                element holds the page index of the VM space where the physical
                memory page will be mapped. All elements in this array are
                valid.

@Input          psDevMemCtx         Handle to DevMem context
@Input          uiSize              The logical size of allocation
@Input          ui32NumPhysChunks   The number of physical chunks required
@Input          ui32NumVirtChunks   The number of virtual chunks required
@Input          pui32MappingTable	VM space page index table
@Input          uiLog2Align         Log2 of the required alignment
@Input          uiLog2HeapPageSize  Log2 page size of the target heap
@Input          uiFlags             Allocation flags
@Input          pszText             Text to describe the allocation, this will
                                    be truncated to the number of characters
                                    specified in PVR_ANNOTATION_MAX_LEN.
@Output         hMemDesc
@Return         PVRSRV_OK on success. Otherwise, a PVRSRV_ error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVAllocSparseDevMem(const PVRSRV_DEVMEMCTX psDevMemCtx,
                         IMG_DEVMEM_SIZE_T uiSize,
                         IMG_UINT32 ui32NumPhysChunks,
                         IMG_UINT32 ui32NumVirtChunks,
                         IMG_UINT32 *pui32MappingTable,
                         IMG_DEVMEM_LOG2ALIGN_T uiLog2Align,
                         IMG_UINT32 uiLog2HeapPageSize,
                         PVRSRV_MEMALLOCFLAGS_T uiFlags,
                         const IMG_CHAR *pszText,
                         PVRSRV_MEMDESC *hMemDesc);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVGetMemAllocFlags
@Description    Queries the allocation flags

@Input          hMemDesc          Memdesc of Heap to be queried
@Output         puiFlags          Flags will be returned in this

@Return         PVRSRV_OK on success. Otherwise, a PVRSRV error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVGetMemAllocFlags(PVRSRV_MEMDESC hMemDesc, PVRSRV_MEMALLOCFLAGS_T *puiFlags);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVGetOSPageSize
@Description    Just call AFTER setting up the connection to the kernel module
                otherwise it will run into an assert.
                Gives the page size that is utilised by the OS.

@Return         The page size
*/ /**************************************************************************/

IMG_EXPORT IMG_UINT32 PVRSRVGetOSPageSize(void);


/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVGetOSPageShift
@Description    Just call AFTER setting up the connection to the kernel module
                otherwise it will run into an assert.
                Gives the log2 of the page size that is utilised by the OS.

@Return         The page size
*/ /**************************************************************************/

IMG_EXPORT IMG_UINT32 PVRSRVGetOSPageShift(void);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVGetHeapLog2PageSize
@Description    Queries the page size of a passed heap.

@Input          hHeap             Heap that is queried
@Output         puiLog2PageSize   Log2 page size will be returned in this

@Return         PVRSRV_OK on success. Otherwise, a PVRSRV error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVGetHeapLog2PageSize(PVRSRV_HEAP hHeap, IMG_UINT32* puiLog2PageSize);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVGetHeapReservedSize
@Description    Queries the reserved size of a passed heap.

@Input          hHeap             Heap that is queried
@Output         puiSize           Size will be returned in this

@Return         PVRSRV_OK on success. Otherwise, a PVRSRV error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVGetHeapReservedSize(PVRSRV_HEAP hHeap, IMG_DEVMEM_SIZE_T* puiSize);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function PVRSRVMakeLocalImportHandle
@Description    This is a "special case" function for making a local import
                handle. The server handle is a handle to a PMR of bridge type
                PMR_LOCAL_EXPORT_HANDLE. The returned local import handle will
                be of the bridge type DEVMEM_MEM_IMPORT that can be used with
                PVRSRVDevmemLocalImport().
@Input          psConnection        Services connection
@Input          hServerHandle       Server export handle
@Output         hLocalImportHandle  Returned client import handle
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVMakeLocalImportHandle(const PVRSRV_DEV_CONNECTION *psConnection,
                            IMG_HANDLE hServerHandle,
                            IMG_HANDLE *hLocalImportHandle);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function PVRSRVUnmakeLocalImportHandle
@Description    Destroy the hLocalImportHandle created with
                PVRSRVMakeLocalImportHandle().
@Input          psConnection        Services connection
@Output         hLocalImportHandle  Local import handle
@Return         PVRSRV_ERROR:       PVRSRV_OK on success. Otherwise, a PVRSRV_
                                    error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVUnmakeLocalImportHandle(const PVRSRV_DEV_CONNECTION *psConnection,
                              IMG_HANDLE hLocalImportHandle);

#if defined(SUPPORT_INSECURE_EXPORT)
/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVExportDevMem
@Description    Given a memory allocation allocated with Devmem_Allocate(),
                create a "cookie" that can be passed intact by the caller's
                own choice of secure IPC to another process and used as the
                argument to "map" to map this memory into a heap in the
                target processes.
                N.B.  This can also be used to map into multiple heaps in one
                process, though that's not the intention.

                Note, the caller must later call Unexport before freeing the
                memory.
@Input          hMemDesc        handle to the descriptor of the memory to be
                                exported
@Output         phExportCookie  On success, a handle to the exported cookie
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR PVRSRVExportDevMem(PVRSRV_MEMDESC hMemDesc,
                                PVRSRV_DEVMEM_EXPORTCOOKIE *phExportCookie);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVUnexportDevMem
@Description    Undo the export caused by "PVRSRVExport" - note - it doesn't
                actually tear down any mapping made by processes that received
                the export cookie.  It will simply make the cookie null and
                void and prevent further mappings.
@Input          hMemDesc        handle to the descriptor of the memory which
                                will no longer be exported
@Output         phExportCookie  On success, the export cookie provided will be
                                set to null
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR PVRSRVUnexportDevMem(PVRSRV_MEMDESC hMemDesc,
                                  PVRSRV_DEVMEM_EXPORTCOOKIE *phExportCookie);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVImportDevMem
@Description    Import memory that was previously exported with PVRSRVExport()
                into the current process.

                Note: This call only makes the memory accessible to this
                process, it doesn't map it into the device or CPU.

@Input          psConnection    Connection to services
@Input          phExportCookie  Ptr to the handle of the export-cookie
                                identifying
@Input          uiFlags         Device memory flags
@Output         phMemDescOut    On Success, a handle to a new memory
                                descriptor representing the memory as mapped
                                into the local process address space.
@Return         PVRSRV_ERROR:   PVRSRV_OK on success. Otherwise, a PVRSRV_
                                error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR PVRSRVImportDevMem(const PVRSRV_DEV_CONNECTION *psConnection,
                                PVRSRV_DEVMEM_EXPORTCOOKIE *phExportCookie,
                                PVRSRV_MEMALLOCFLAGS_T uiFlags,
                                PVRSRV_MEMDESC *phMemDescOut);
#endif /* SUPPORT_INSECURE_EXPORT */

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVIsDeviceMemAddrValid
@Description    Checks if given device virtual memory address is valid
                from the GPU's point of view.

                This method is intended to be called by a process that
                imported another process' memory context, hence the expected
                PVRSRV_REMOTE_DEVMEMCTX parameter.

                See PVRSRVAcquireRemoteDevMemContext for details about
                importing memory contexts.

@Input          hContext handle to memory context
@Input          sDevVAddr device 40bit virtual memory address
@Return         PVRSRV_OK if address is valid or
                PVRSRV_ERROR_INVALID_GPU_ADDR when address is invalid
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR PVRSRVIsDeviceMemAddrValid(PVRSRV_REMOTE_DEVMEMCTX hContext,
                                        IMG_DEV_VIRTADDR sDevVAddr);

/*************************************************************************/ /*!
@InGroup        DevMemAPIs
@Function       PVRSRVDevmemGetSize
@Description    Returns the allocated size for this device-memory.

@Input          hMemDesc handle to memory allocation
@Output         puiSize return value for size
@Return         PVRSRV_OK on success or
                PVRSRV_ERROR_INVALID_PARAMS
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVDevmemGetSize(PVRSRV_MEMDESC hMemDesc, IMG_DEVMEM_SIZE_T* puiSize);

/*************************************************************************/ /*!
@Function       PVRSRVDevmemGetAnnotation
@Description    Returns the annotation for this device-memory

@Input          hMemDesc handle to memory allocation
@Output         pszAnnotation return value for annotation
@Return         PVRSRV_OK on success or
                PVRSRV_ERROR_INVALID_PARAMS
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVDevmemGetAnnotation(PVRSRV_MEMDESC hMemDesc, IMG_CHAR **pszAnnotation);

/*************************************************************************/ /*!
@Function       PVRSRVExportDevMemContext
@Description    Makes the given memory context available to other processes
                that can get a handle to it via
                PVRSRVAcquireRemoteDevmemContext.
                This handle can be used for e.g. the breakpoint functions.

                The context will be only available to other processes that are
                able to pass in a memory descriptor that is shared between
                this and the importing process. We use the memory descriptor
                to identify the correct context and verify that the caller is
                allowed to request the context.

                The whole mechanism is intended to be used with the debugger
                that for example can load USC breakpoint handlers into the
                shared allocation and then use the acquired remote context
                (that is exported here) to set/clear breakpoints in USC code.

@Input          hLocalDevmemCtx    Context to export
@Input          hSharedAllocation  A memory descriptor that points to a shared
                                   allocation between the two processes.
                                   Must be in the given context.
@Output         phExportCtx        A handle to the exported context that is
                                   needed for the destruction with
                                   PVRSRVUnexportDevMemContext().
@Return         PVRSRV_ERROR:      PVRSRV_OK on success. Otherwise, a PVRSRV_
                                   error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVExportDevMemContext(PVRSRV_DEVMEMCTX hLocalDevmemCtx,
                          PVRSRV_MEMDESC hSharedAllocation,
                          PVRSRV_EXPORT_DEVMEMCTX *phExportCtx);

/*************************************************************************/ /*!
@Function       PVRSRVUnexportDevMemContext
@Description    Removes the context from the list of shareable contexts that
                can be imported via PVRSRVReleaseRemoteDevmemContext.

@Input          hExportCtx     An export context retrieved from
                               PVRSRVExportDevMemContext.
*/ /**************************************************************************/
IMG_EXPORT void
PVRSRVUnexportDevMemContext(PVRSRV_EXPORT_DEVMEMCTX hExportCtx);

/*************************************************************************/ /*!
@Function       PVRSRVAcquireRemoteDevMemContext
@Description    Retrieves an exported context that has been made available
                with PVRSRVExportDevMemContext in the remote process.

                hSharedMemDesc must be a memory descriptor pointing to the
                same physical resource as the one passed to
                PVRSRVExportDevMemContext in the remote process. The memory
                descriptor has to be retrieved from the remote process via
                a secure buffer export/import mechanism like DMABuf.

@Input          hDevmemCtx         Memory context of the calling process.
@Input          hSharedAllocation  Memory descriptor for exporting the context
@Output         phRemoteCtx        Handle to the remote context.
@Return         PVRSRV_ERROR:      PVRSRV_OK on success. Otherwise, a PVRSRV_
                                   error code
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVAcquireRemoteDevMemContext(PVRSRV_DEVMEMCTX hDevmemCtx,
                                 PVRSRV_MEMDESC hSharedAllocation,
                                 PVRSRV_REMOTE_DEVMEMCTX *phRemoteCtx);

/*************************************************************************/ /*!
@Function       PVRSRVReleaseRemoteDevMemContext
@Description    Releases the remote context and destroys it if this is the
                last reference.

@Input          hRemoteCtx      Handle to the remote context that will be
                                removed.
*/ /**************************************************************************/
IMG_EXPORT void
PVRSRVReleaseRemoteDevMemContext(PVRSRV_REMOTE_DEVMEMCTX hRemoteCtx);

/*************************************************************************/ /*!
@Function       PVRSRVRegisterDevmemPageFaultNotify
@Description    Registers to be notified when a page fault occurs on a
                specific device memory context.
@Input          psDevmemCtx     The context to be notified about.
@Return         PVRSRV_ERROR.
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVRegisterDevmemPageFaultNotify(PVRSRV_DEVMEMCTX psDevmemCtx);

/*************************************************************************/ /*!
@Function       PVRSRVUnregisterDevmemPageFaultNotify
@Description    Unegisters to be notified when a page fault occurs on a
                specific device memory context.
@Input          psDevmemCtx     The context to be unregistered from.
@Return         PVRSRV_ERROR.
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR
PVRSRVUnregisterDevmemPageFaultNotify(PVRSRV_DEVMEMCTX psDevmemCtx);

/*************************************************************************/ /*!
@Function       PVRSRVGetRemoteDeviceMemFaultAddress
@Description    Returns the device virtual address of a page fault
                on a given remote memory context.
                Only one address is stored at a time until consumed.

                This method is intended to be called by a process that
                imported another process' memory context, hence the
                expected PVRSRV_REMOTE_DEVMEMCTX parameter.

                See PVRSRVAcquireRemoteDevMemContext for details about
                importing memory contexts.

@Input          hContext handle to memory context.
@Output         psFaultAddress device 40bit virtual memory address.
@Return         PVRSRV_OK if an address is returned,
                PVRSRV_ERROR_RESOURCE_UNAVAILABLE otherwise.
*/ /**************************************************************************/
IMG_EXPORT PVRSRV_ERROR PVRSRVGetRemoteDeviceMemFaultAddress(PVRSRV_REMOTE_DEVMEMCTX hContext,
                                                             IMG_DEV_VIRTADDR *psFaultAddress);

#if defined(__cplusplus)
}
#endif
#endif /* PVRSRV_DEVMEM_H */
