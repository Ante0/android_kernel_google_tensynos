#include "sys_if_io.h"

#include <linux/io.h>
#include "wlan_cast.h"

void __iomem *SysIfIoRemap(uint64_t phys_addr, uint64_t size)
{
	return ioremap(WLAN_STATIC_CAST(unsigned long, phys_addr),
		       WLAN_STATIC_CAST(unsigned long, size));
}

void SysIfIoUnmap(void __iomem *addr)
{
	iounmap(addr);
}

void SysIfIoWritew(uint16_t data, void __iomem *addr)
{
	writew(data, addr);
}

uint16_t SysIfIoReadw(const volatile void __iomem *addr)
{
	return readw(addr);
}

void SysIfIoWritel(uint32_t data, void __iomem *addr)
{
	writel(data, addr);
}

uint32_t SysIfIoReadl(const volatile void __iomem *addr)
{
	return readl(addr);
}
