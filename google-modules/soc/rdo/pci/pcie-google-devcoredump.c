// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022-2025 Google LLC
 */

#include <linux/devcoredump.h>
#include <linux/timekeeping.h>
#include <linux/time.h>

#include "pcie-designware-host-customized.h"
#include "pcie-google.h"

enum pcie_dump_region_id {
	PCIE_DUMP_REGION_TIMESTAMP = 0,
	PCIE_DUMP_REGION_SII,
	PCIE_DUMP_REGION_TOP,
	PCIE_DUMP_REGION_AER,
	PCIE_DUMP_REGION_MSI,
	PCIE_DUMP_REGION_MAX,
};

struct pcie_coredump_info {
	size_t region_size[PCIE_DUMP_REGION_MAX];
	void *buff_head;
	size_t buff_size;
	void *start;
};

#define DUMP_REGION_LINE_FORMAT " [+0x%04x]: %08x %08x %08x %08x\n"
/*
 * Calculate the length of a formatted line for a full 16-byte chunk.
 * Example: " [+0xffff]: 0xffffffff 0xffffffff 0xffffffff 0xffffffff\n"
 * This is 11 (prefix) + 4 * (1 + 10) (4 hex values) + 1 (newline) = 56 characters.
 * DUMP_REGION_LINE_MAX_LEN is defined as 64 to provide a safe margin
 * for the buffer.
 */
#define DUMP_REGION_LINE_MAX_LEN 64
/*
 * DUMP_REGION_REQUIRED_SIZE(sz): Calculates the total buffer size needed
 * to pretty-print a memory region of 'sz' bytes, assuming each formatted
 * line holds a maximum of 16 bytes of data.
 */
#define DUMP_REGION_REQUIRED_SIZE(sz) (DIV_ROUND_UP(sz, 16) * DUMP_REGION_LINE_MAX_LEN)
#define PCI_AER_CAP_SIZE 0x48
#define PCI_MSI_CTRL_OFFSET PCIE_MSI_ADDR_LO
#define PCI_MSI_CTRL_SIZE 0x68

static void google_pcie_dump_line(struct pcie_coredump_info *cd_info, const char *fmt, ...)
{
	va_list args;
	void *buffer_end = cd_info->buff_head + cd_info->buff_size;
	ptrdiff_t remaining = buffer_end - cd_info->start;
	int len;

	if (remaining <= DUMP_REGION_LINE_MAX_LEN)
		return;

	va_start(args, fmt);
	len = vscnprintf(cd_info->start, DUMP_REGION_LINE_MAX_LEN, fmt, args);
	va_end(args);

	if (WARN_ON_ONCE(len > DUMP_REGION_LINE_MAX_LEN))
		len = DUMP_REGION_LINE_MAX_LEN;

	cd_info->start += len;
}

static void google_pcie_dump_csr_region(struct pcie_coredump_info *cd_info,
					const char *name, void __iomem *base, size_t size)
{
	size_t size_align;
	int i;

	if (!base)
		return;

	/* Dump header */
	google_pcie_dump_line(cd_info, "Dump %s regs (size: %zu)...\n", name, size);

	/* Dump data */
	size_align = size & ~0xf;
	for (i = 0; i < size_align; i += 0x10) {
		google_pcie_dump_line(cd_info, DUMP_REGION_LINE_FORMAT, i,
				      readl(base + i), readl(base + i + 4),
				      readl(base + i + 8), readl(base + i + 12));
	}

	if (i < size) {
		int len = 0;
		char tmp[DUMP_REGION_LINE_MAX_LEN];

		len += scnprintf(tmp, DUMP_REGION_LINE_MAX_LEN, " [+0x%04x]:", i);
		for (; i < size; i += 0x4)
			len += scnprintf(tmp + len, DUMP_REGION_LINE_MAX_LEN - len,
					 " %08x", readl(base + i));

		len += scnprintf(tmp + len, DUMP_REGION_LINE_MAX_LEN - len, "\n");
		google_pcie_dump_line(cd_info, tmp);
	}
}

static void google_pcie_dump_dbi_region(struct pcie_coredump_info *cd_info,
					struct google_pcie *gpcie, const char *name,
					u32 base, size_t size)
{
	struct dw_pcie *pci = gpcie->pci;
	size_t size_align;
	int i;

	/* Dump header */
	google_pcie_dump_line(cd_info, "Dump DBI %s regs (size: %zu)...\n", name, size);

	/* Dump data */
	size_align = size & ~0xf;
	for (i = 0; i < size_align; i += 0x10) {
		google_pcie_dump_line(cd_info, DUMP_REGION_LINE_FORMAT, i,
				      dw_pcie_readl_dbi(pci, base + i),
				      dw_pcie_readl_dbi(pci, base + i + 4),
				      dw_pcie_readl_dbi(pci, base + i + 8),
				      dw_pcie_readl_dbi(pci, base + i + 12));
	}

	if (i < size) {
		int len = 0;
		char tmp[DUMP_REGION_LINE_MAX_LEN];

		len += scnprintf(tmp, DUMP_REGION_LINE_MAX_LEN, " [+0x%04x]:", i);
		for (; i < size; i += 0x4)
			len += scnprintf(tmp + len, DUMP_REGION_LINE_MAX_LEN - len, " %08x",
					 dw_pcie_readl_dbi(pci, base + i));

		len += scnprintf(tmp + len, DUMP_REGION_LINE_MAX_LEN - len, "\n");
		google_pcie_dump_line(cd_info, tmp);
	}
}

static void google_pcie_dump_timestamp(struct pcie_coredump_info *cd_info,
				       struct google_pcie *gpcie)
{
	time64_t ts = ktime_get_real_seconds();

	/* Dump header */
	google_pcie_dump_line(cd_info, "Timestamp: %ptTs\n\n", &ts);

	dev_info(gpcie->dev, "Generating coredump at %ptTs\n", &ts);
}

static void google_pcie_init_reg_size(struct pcie_coredump_info *cd_info,
				      struct google_pcie *gpcie)
{
	cd_info->region_size[PCIE_DUMP_REGION_TIMESTAMP] = 0;
	cd_info->region_size[PCIE_DUMP_REGION_SII] = gpcie->sii_size;
	cd_info->region_size[PCIE_DUMP_REGION_TOP] = gpcie->top_size;
	cd_info->region_size[PCIE_DUMP_REGION_AER] = PCI_AER_CAP_SIZE;
	cd_info->region_size[PCIE_DUMP_REGION_MSI] = PCI_MSI_CTRL_SIZE;
}

void google_pcie_create_devcoredump(struct google_pcie *gpcie)
{
	struct pcie_coredump_info cd_info;
	int i;

	if (atomic_cmpxchg(&gpcie->coredump_in_progress, 0, 1) != 0) {
		dev_info(gpcie->dev, "PCIe: Coredump already in progress, skipping...\n");
		return;
	}

	google_pcie_init_reg_size(&cd_info, gpcie);

	/* calculate required buff size */
	for (i = 0; i < PCIE_DUMP_REGION_MAX; i++) {
		/* for header */
		cd_info.buff_size += DUMP_REGION_LINE_MAX_LEN;
		/* for data */
		cd_info.buff_size += DUMP_REGION_REQUIRED_SIZE(cd_info.region_size[i]);
	}

	cd_info.buff_head = vzalloc(cd_info.buff_size);
	if (!cd_info.buff_head) {
		atomic_set(&gpcie->coredump_in_progress, 0);
		return;
	}
	cd_info.start = cd_info.buff_head;

	google_pcie_dump_timestamp(&cd_info, gpcie);

	scoped_guard(spinlock_irqsave, &gpcie->power_on_lock) {
		if (!gpcie->powered_on) {
			dev_info(gpcie->dev, "PCIe is down\n");
			vfree(cd_info.buff_head);
			atomic_set(&gpcie->coredump_in_progress, 0);
			return;
		}

		google_pcie_dump_csr_region(&cd_info, "sii", gpcie->sii_base, gpcie->sii_size);
		google_pcie_dump_csr_region(&cd_info, "top", gpcie->top_base, gpcie->top_size);
	}

	if (gpcie->aer)
		google_pcie_dump_dbi_region(&cd_info, gpcie, "AER", gpcie->aer, PCI_AER_CAP_SIZE);

	google_pcie_dump_dbi_region(&cd_info, gpcie, "msi", PCI_MSI_CTRL_OFFSET, PCI_MSI_CTRL_SIZE);

	dev_coredumpv(gpcie->dev, cd_info.buff_head, cd_info.start - cd_info.buff_head, GFP_KERNEL);

	atomic_set(&gpcie->coredump_in_progress, 0);
}

void google_pcie_init_devcoredump(struct google_pcie *gpcie)
{
	atomic_set(&gpcie->coredump_in_progress, 0);
}
