// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * This file is a fork of remoteproc_elf_loader.c. Modified for DPA driver.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/align.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/remoteproc.h>

#include <soc/google/google_elf_helpers.h>

#include "google_dpa_dma.h"
#include "google_dpa_elf_loader.h"
#include "google_dpa_internal.h"
#include "google_dpa_io.h"

enum {
	RSC_GOOGLE_DPA_ADDR_MAPPING = RSC_VENDOR_START,
	RSC_GOOGLE_DPA_SHARED_INFO_CONFIG,
};

struct google_dpa_addr_mapping_rsc {
	u64 da;
	u64 pa;
	u32 len;
	char name[32];
} __packed;

struct google_dpa_shared_info_config_rsc {
	u32 ver;
	u32 da;
} __packed;

static void google_dpa_cleanup_addr_mapping(struct google_dpa *dpa,
					    struct google_dpa_addr_mapping *addr_mapping)
{
	if (addr_mapping->dma_addr) {
		google_dpa_dma_free_coherent_unlink_range(dpa, addr_mapping->len,
							  addr_mapping->ioremapped_addr,
							  addr_mapping->dma_addr,
							  addr_mapping->mcu_view_addr);
	} else {
		iounmap(addr_mapping->ioremapped_addr);
	}
	kfree(addr_mapping->name);
};

static int handle_addr_mapping_rsc(struct google_dpa *dpa, struct google_dpa_mcu *mcu, u32 rsc_type,
				   void *rsc, int offset, int avail)
{
	struct google_dpa_addr_mapping_rsc *addr_mapping_rsc;
	struct google_dpa_addr_mapping *addr_mapping;
	struct device *dev = dpa->dev;
	int ret;
	size_t name_len;

	if (avail < sizeof(*addr_mapping_rsc)) {
		dev_err(dev, "addr mapping resource is truncated\n");
		return -EINVAL;
	}

	addr_mapping_rsc = rsc;

	addr_mapping = kzalloc(sizeof(*addr_mapping), GFP_KERNEL);
	if (!addr_mapping)
		return -ENOMEM;

	addr_mapping->mcu_view_addr = addr_mapping_rsc->da;
	addr_mapping->soc_view_addr = addr_mapping_rsc->pa;
	addr_mapping->dma_addr = 0;
	addr_mapping->len = addr_mapping_rsc->len;
	addr_mapping->ioremapped_addr = NULL;

	name_len = strnlen(addr_mapping_rsc->name, sizeof(addr_mapping_rsc->name));
	addr_mapping->name = kstrndup(addr_mapping_rsc->name, name_len, GFP_KERNEL);
	if (!addr_mapping->name) {
		ret = -ENOMEM;
		goto free_mapping;
	}
	if (addr_mapping->soc_view_addr) {
		addr_mapping->ioremapped_addr =
			ioremap(addr_mapping->soc_view_addr, addr_mapping->len);
	} else {
		/*
		 * When addr_mapping->soc_view_addr is zero, it indicates dynamic allocation
		 * of DRAM regions for DPA FW code and data. This functionality should be
		 * restricted to the development stage, as DPA FW code/data regions require
		 * TrustZone protection for production environments.
		 */
		if (dpa->use_secure_boot) {
			dev_err(dev,
				"Dynamic allocation of DRAM region for DPA FW code/data region is not supported.\n");
			ret = -EINVAL;
			goto free_name;
		}
		addr_mapping->ioremapped_addr =
			google_dpa_dma_alloc_coherent_link_range(dpa, addr_mapping->len,
								 &addr_mapping->dma_addr,
								 GFP_KERNEL,
								 addr_mapping->mcu_view_addr,
								 IOMMU_WRITE | IOMMU_READ);
		if (!addr_mapping->ioremapped_addr) {
			dev_err(dev, "Failed to allocate memory for %s", addr_mapping->name);
			ret = -ENOMEM;
			goto free_name;
		}
		dev_dbg(dev, "IOMMU Mapping (RSC): %s 0x%llx (0x%zx)\n", addr_mapping->name,
			addr_mapping->mcu_view_addr, addr_mapping->len);
	}

	if (!addr_mapping->ioremapped_addr) {
		dev_err(dev, "Failed to process region of %s.", addr_mapping->name);
		ret = -EINVAL;
		goto free_name;
	}

	list_add(&addr_mapping->node, &mcu->addr_mappings);

	return RSC_HANDLED;

free_name:
	kfree(addr_mapping->name);
free_mapping:
	kfree(addr_mapping);
	return ret;
}

static int handle_shared_info_config(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				     u32 rsc_type, void *rsc, int offset, int avail)
{
	struct google_dpa_shared_info_config_rsc *shared_info_config =
		(struct google_dpa_shared_info_config_rsc *)rsc;
	struct device *dev = dpa->dev;

	if (avail < sizeof(struct google_dpa_shared_info_config_rsc)) {
		dev_err(dev, "shared_info resource is truncated\n");
		return -EINVAL;
	}

	dev_dbg(dev, "shared_info version: %d\n", shared_info_config->ver);

	if (shared_info_config->ver == 1) {
		dpa->shared_info_device_address = shared_info_config->da;
		dev_dbg(dev, "shared_info device address: 0x%x\n", dpa->shared_info_device_address);
	} else {
		dev_warn(dev, "unsupported shared_info version: %d, ignored\n",
			 shared_info_config->ver);
	}

	return 0;
}

static int google_dpa_handle_vendor_rsc(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
					u32 rsc_type, void *rsc, int offset, int avail)
{
	switch (rsc_type) {
	case RSC_GOOGLE_DPA_ADDR_MAPPING:
		return handle_addr_mapping_rsc(dpa, mcu, rsc_type, rsc, offset, avail);
	case RSC_GOOGLE_DPA_SHARED_INFO_CONFIG:
		return handle_shared_info_config(dpa, mcu, rsc_type, rsc, offset, avail);
	default:
		return RSC_IGNORED;
	};
}

/* handle firmware resource entries before booting the mcu */
int google_dpa_handle_resources(struct google_dpa *dpa, struct google_dpa_mcu *mcu)
{
	struct device *dev = dpa->dev;
	int ret = 0, i;

	if (!mcu->rsc_table)
		return 0;

	for (i = 0; i < mcu->rsc_table->num; i++) {
		int offset = mcu->rsc_table->offset[i];
		struct fw_rsc_hdr *hdr = (void *)mcu->rsc_table + offset;
		int avail = mcu->rsc_table_size - offset - sizeof(*hdr);
		void *rsc = (void *)hdr + sizeof(*hdr);

		if (i + 1 < mcu->rsc_table->num)
			avail = mcu->rsc_table->offset[i + 1] - offset - sizeof(*hdr);

		/* make sure table isn't truncated */
		if (avail < 0) {
			dev_err(dev, "rsc table is truncated\n");
			return -EINVAL;
		}

		dev_dbg(dev, "rsc: type %d\n", hdr->type);

		if (hdr->type >= RSC_VENDOR_START && hdr->type <= RSC_VENDOR_END) {
			ret = google_dpa_handle_vendor_rsc(dpa, mcu, hdr->type, rsc,
							   offset + sizeof(*hdr), avail);
			if (ret == RSC_IGNORED)
				dev_warn(dev, "unsupported vendor resource %d\n", hdr->type);
			else if (ret < 0)
				return ret;
		} else {
			dev_warn(dev, "unsupported resource %d\n", hdr->type);
		}
	}

	return ret;
}

void google_dpa_release_resources(struct google_dpa *dpa, struct google_dpa_mcu *mcu)
{
	struct google_dpa_addr_mapping *addr_mapping, *tmp;

	list_for_each_entry_safe(addr_mapping, tmp, &mcu->addr_mappings, node) {
		list_del(&addr_mapping->node);
		google_dpa_cleanup_addr_mapping(dpa, addr_mapping);
		kfree(addr_mapping);
	}
}

void google_dpa_release_resource_table(struct google_dpa_mcu *mcu)
{
	kfree(mcu->rsc_table);
	mcu->rsc_table = NULL;
	mcu->rsc_table_size = 0;
}

static inline bool google_dpa_u64_fit_in_size_t(u64 val)
{
	if (sizeof(size_t) == sizeof(u64))
		return true;

	return (val <= (size_t)-1);
}

/**
 * google_dpa_elf_sanity_check() - Sanity Check for ELF32/ELF64 firmware image
 * @dpa: the DPA handle
 * @firmware_name: the name of firmware
 * @fw: the ELF firmware image
 *
 * Make sure this fw image is valid (ie a correct ELF32/ELF64 file).
 *
 * Return: 0 on success and -EINVAL upon any failure
 */
int google_dpa_elf_sanity_check(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				const struct firmware *fw)
{
	struct device *dev = dpa->dev;
	/*
	 * ELF files are beginning with the same structure. Thus, to simplify
	 * header parsing, we can use the elf32_hdr one for both elf64 and
	 * elf32.
	 */
	struct elf32_hdr *ehdr;
	u32 elf_shdr_get_size;
	u64 phoff, shoff;
	char class;
	u16 phnum;

	if (!fw) {
		dev_err(dev, "failed to load %s\n", mcu->firmware_name);
		return -EINVAL;
	}

	if (fw->size < sizeof(struct elf32_hdr)) {
		dev_err(dev, "Image is too small\n");
		return -EINVAL;
	}

	ehdr = (struct elf32_hdr *)fw->data;

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
		dev_err(dev, "Image is corrupted (bad magic)\n");
		return -EINVAL;
	}

	class = ehdr->e_ident[EI_CLASS];
	if (class != ELFCLASS32 && class != ELFCLASS64) {
		dev_err(dev, "Unsupported class: %d\n", class);
		return -EINVAL;
	}

	if (class == ELFCLASS64 && fw->size < sizeof(struct elf64_hdr)) {
		dev_err(dev, "elf64 header is too small\n");
		return -EINVAL;
	}

	/* We assume the firmware has the same endianness as the host */
#ifdef __LITTLE_ENDIAN
	if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
#else /* BIG ENDIAN */
	if (ehdr->e_ident[EI_DATA] != ELFDATA2MSB)
#endif
	{
		dev_err(dev, "Unsupported firmware endianness\n");
		return -EINVAL;
	}

	phoff = elf_hdr_get_e_phoff(class, fw->data);
	shoff = elf_hdr_get_e_shoff(class, fw->data);
	phnum = elf_hdr_get_e_phnum(class, fw->data);
	elf_shdr_get_size = elf_size_of_shdr(class);

	if (fw->size < shoff + elf_shdr_get_size) {
		dev_err(dev, "Image is too small\n");
		return -EINVAL;
	}

	if (phnum == 0) {
		dev_err(dev, "No loadable segments\n");
		return -EINVAL;
	}

	if (phoff > fw->size) {
		dev_err(dev, "Firmware size is too small\n");
		return -EINVAL;
	}

	dev_dbg(dev, "Firmware is an elf%d file\n", class == ELFCLASS32 ? 32 : 64);

	return 0;
}

/**
 * google_dpa_elf_load_segments() - load firmware segments to memory
 * @dpa: the google_dpa handle
 * @mcu: the google_dpa_mcu handle to boot
 * @fw: the ELF firmware image
 *
 * This function loads the ELF segments to SRAM, where the mcu expects them.
 *
 * Return: 0 on success and an appropriate error code otherwise
 */
int google_dpa_elf_load_segments(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				 const struct firmware *fw)
{
	struct device *dev = dpa->dev;
	const void *ehdr, *phdr;
	int i, ret = 0;
	u16 phnum;
	const u8 *elf_data = fw->data;
	u8 class = fw_elf_get_class(fw);
	u32 elf_phdr_get_size = elf_size_of_phdr(class);

	ehdr = elf_data;
	phnum = elf_hdr_get_e_phnum(class, ehdr);
	phdr = elf_data + elf_hdr_get_e_phoff(class, ehdr);

	/* go through the available ELF segments */
	for (i = 0; i < phnum; i++, phdr += elf_phdr_get_size) {
		u64 da = elf_phdr_get_p_paddr(class, phdr);
		u64 memsz = elf_phdr_get_p_memsz(class, phdr);
		u64 filesz = elf_phdr_get_p_filesz(class, phdr);
		u64 offset = elf_phdr_get_p_offset(class, phdr);
		u32 type = elf_phdr_get_p_type(class, phdr);
		bool is_iomem = false;
		void *ptr;

		if (type != PT_LOAD || !memsz)
			continue;

		dev_dbg(dev, "phdr: type %d da 0x%llx memsz 0x%llx filesz 0x%llx\n", type, da,
			memsz, filesz);

		if (filesz > memsz) {
			dev_err(dev, "bad phdr filesz 0x%llx memsz 0x%llx\n", filesz, memsz);
			ret = -EINVAL;
			break;
		}

		if (offset + filesz > fw->size) {
			dev_err(dev, "truncated fw: need 0x%llx avail 0x%zx\n", offset + filesz,
				fw->size);
			ret = -EINVAL;
			break;
		}

		if (!google_dpa_u64_fit_in_size_t(memsz)) {
			dev_err(dev, "size (%llx) does not fit in size_t type\n", memsz);
			ret = -EOVERFLOW;
			break;
		}

		/* grab the kernel address for this device address */
		ptr = google_dpa_da_to_va_internal(dpa, mcu, da, memsz, &is_iomem);
		if (!ptr) {
			dev_err(dev, "bad phdr da 0x%llx mem 0x%llx\n", da, memsz);
			ret = -EINVAL;
			break;
		}

		/* put the segment where the remote processor expects it */
		if (filesz) {
			if (is_iomem)
				google_dpa_memcpy_toio((void __iomem *)ptr, elf_data + offset,
						       filesz);
			else
				memcpy(ptr, elf_data + offset, filesz);
		}

		/*
		 * Zero out remaining memory for this segment.
		 *
		 * This isn't strictly required since dma_alloc_coherent already
		 * did this for us. albeit harmless, we may consider removing
		 * this.
		 */
		if (memsz > filesz) {
			if (is_iomem)
				google_dpa_memset_io((void __iomem *)(ptr + filesz), 0,
						     memsz - filesz);
			else
				memset(ptr + filesz, 0, memsz - filesz);
		}
	}

	return ret;
}

static const void *find_table(struct device *dev, const struct firmware *fw)
{
	const void *shdr, *name_table_shdr;
	int i;
	const char *name_table;
	struct resource_table *table = NULL;
	const u8 *elf_data = (void *)fw->data;
	u8 class = fw_elf_get_class(fw);
	size_t fw_size = fw->size;
	const void *ehdr = elf_data;
	u16 shnum = elf_hdr_get_e_shnum(class, ehdr);
	u32 elf_shdr_get_size = elf_size_of_shdr(class);
	u16 shstrndx = elf_hdr_get_e_shstrndx(class, ehdr);

	/* look for the resource table and handle it */
	/* First, get the section header according to the elf class */
	shdr = elf_data + elf_hdr_get_e_shoff(class, ehdr);
	/* Compute name table section header entry in shdr array */
	name_table_shdr = shdr + (shstrndx * elf_shdr_get_size);
	/* Finally, compute the name table section address in elf */
	name_table = elf_data + elf_shdr_get_sh_offset(class, name_table_shdr);

	for (i = 0; i < shnum; i++, shdr += elf_shdr_get_size) {
		u64 size = elf_shdr_get_sh_size(class, shdr);
		u64 offset = elf_shdr_get_sh_offset(class, shdr);
		u32 name = elf_shdr_get_sh_name(class, shdr);

		if (strcmp(name_table + name, ".resource_table"))
			continue;

		table = (struct resource_table *)(elf_data + offset);

		/* make sure we have the entire table */
		if (offset + size > fw_size || offset + size < size) {
			dev_err(dev, "resource table truncated\n");
			return NULL;
		}

		/* make sure table has at least the header */
		if (sizeof(struct resource_table) > size) {
			dev_err(dev, "header-less resource table\n");
			return NULL;
		}

		/* we don't support any version beyond the first */
		if (table->ver != 1) {
			dev_err(dev, "unsupported fw ver: %d\n", table->ver);
			return NULL;
		}

		/* make sure reserved bytes are zeroes */
		if (table->reserved[0] || table->reserved[1]) {
			dev_err(dev, "non zero reserved bytes\n");
			return NULL;
		}

		/* make sure the offsets array isn't truncated */
		if (struct_size(table, offset, table->num) > size) {
			dev_err(dev, "resource table incomplete\n");
			return NULL;
		}

		return shdr;
	}

	return NULL;
}

/**
 * google_dpa_elf_load_rsc_table() - load the resource table
 * @dpa: the google_dpa handle
 * @mcu: the google_dpa_mcu handle to load resource table
 * @fw: the ELF firmware image
 *
 * This function finds the resource table inside the mcu's firmware image,
 * load it into the mcu->rsc_table.
 *
 * Return: 0 on success, negative errno on failure.
 */
int google_dpa_elf_load_rsc_table(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
				  const struct firmware *fw)
{
	const void *shdr;
	struct device *dev = dpa->dev;
	struct resource_table *table = NULL;
	const u8 *elf_data = fw->data;
	size_t tablesz;
	u8 class = fw_elf_get_class(fw);
	u64 sh_offset;

	shdr = find_table(dev, fw);
	if (!shdr)
		return -EINVAL;

	sh_offset = elf_shdr_get_sh_offset(class, shdr);
	table = (struct resource_table *)(elf_data + sh_offset);
	tablesz = elf_shdr_get_sh_size(class, shdr);

	/*
	 * Create a copy of the resource table. When a virtio device starts
	 * and calls vring_new_virtqueue() the address of the allocated vring
	 * will be stored in the cached_table. Before the device is started,
	 * cached_table will be copied into device memory.
	 */
	mcu->rsc_table = kmemdup(table, tablesz, GFP_KERNEL);
	if (!mcu->rsc_table)
		return -ENOMEM;
	mcu->rsc_table_size = tablesz;

	return 0;
}
