#include "sys_if_io.h"

#if defined(HOST_SIMULATOR)
#include <unordered_map>
#else
#include "common/macro.h"
#endif

#if defined(HOST_SIMULATOR)
static std::unordered_map<uintptr_t, uint32_t> g_io_table;
#endif

void __iomem *SysIfIoRemap(uint64_t phys_addr, [[maybe_unused]] uint64_t size)
{
	return reinterpret_cast<void *>(phys_addr);
}

void SysIfIoUnmap([[maybe_unused]] void __iomem *addr)
{
	// Not supported.
}

void SysIfIoWritew(uint16_t data, void __iomem *addr)
{
#if defined(HOST_SIMULATOR)
	g_io_table[reinterpret_cast<uintptr_t>(addr)] = data;
#else
	WR_REG16(addr, data);
#endif
}

uint16_t SysIfIoReadw(const volatile void __iomem *addr)
{
#if defined(HOST_SIMULATOR)
	return static_cast<uint16_t>(g_io_table[reinterpret_cast<uintptr_t>(addr)]);
#else
	return RD_REG16(addr);
#endif
}

void SysIfIoWritel(uint32_t data, void __iomem *addr)
{
#if defined(HOST_SIMULATOR)
	g_io_table[reinterpret_cast<uintptr_t>(addr)] = data;
#else
	WR_REG(addr, data);
#endif
}

uint32_t SysIfIoReadl(const volatile void __iomem *addr)
{
#if defined(HOST_SIMULATOR)
	return g_io_table[reinterpret_cast<uintptr_t>(addr)];
#else
	return RD_REG(addr);
#endif
}
