#include "sys_if_memory.h"

#include <cstddef>

#include "arch/memory.h"
#include "buffer_mgmt/buffer_manager.h"
#include "noa_desc.h"

void SysIfInvalidDCache(PhyAddr addr, size_t size)
{
	InvalidateDCache(reinterpret_cast<void *>(addr), size);
}

void SysIfFlushDCache(PhyAddr addr, size_t size)
{
	FlushDCache(reinterpret_cast<void *>(addr), size);
}

void SysIfDmaSyncForCpu(PhyAddr addr, size_t size)
{
	InvalidateDCache(reinterpret_cast<void *>(addr), size);
}

void SysIfDmaSyncForDevice(PhyAddr addr, size_t size)
{
	FlushDCache(reinterpret_cast<void *>(addr), size);
}

void *SysIfAllocateDram(size_t size)
{
#ifdef HOST_SIMULATOR
	return malloc(size);
#else
	return nmalloc(size, GFB_DRAM_COHERENCE);
#endif // HOST_SIMULATOR
}

void SysIfFree(void *ptr)
{
#ifdef HOST_SIMULATOR
	return free(ptr);
#else
	return nfree(ptr);
#endif // HOST_SIMULATOR
}
