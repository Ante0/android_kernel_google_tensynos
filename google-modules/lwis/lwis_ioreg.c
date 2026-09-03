// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Register I/O Interface
 *
 * Copyright (c) 2018 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-ioreg: " fmt

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/slab.h>

#include "lwis_device.h"
#include "lwis_ioreg.h"
#include "lwis_util.h"

static int find_block_idx_by_name(struct lwis_ioreg_list *list, char *name)
{
	for (int i = 0; i < list->count; ++i) {
		if (!strcmp(list->block[i].name, name))
			return i;
	}
	return -ENOENT;
}

static struct lwis_ioreg *get_block_by_idx(struct lwis_ioreg_device *ioreg_dev, int index)
{
	struct lwis_ioreg *block;
	struct lwis_ioreg_list *list;

	list = &ioreg_dev->reg_list;
	if (index < 0 || index >= list->count)
		return ERR_PTR(-EINVAL);

	block = &list->block[index];
	if (!block->base)
		return ERR_PTR(-EINVAL);

	return block;
}

/*
 * If defined, the offset for each block is subtracted from the register
 * address.
 */
static uint64_t get_total_offset(uint64_t entry_offset, struct lwis_ioreg *block)
{
	return entry_offset - block->offset;
}

static int validate_offset(struct lwis_ioreg_device *ioreg_dev, int index, struct lwis_ioreg *block,
			   uint64_t offset, size_t size_in_bytes, unsigned int alignment)
{
	if (offset % alignment) {
		dev_err(ioreg_dev->base_dev.dev, "Accessing invalid address! Alignment error\n");
		return -EFAULT;
	}

	if ((offset > U64_MAX - size_in_bytes) || (offset + size_in_bytes > block->size)) {
		dev_err(ioreg_dev->base_dev.dev, "Accessing invalid address! Block size is %d.\n",
			block->size);
		dev_err(ioreg_dev->base_dev.dev,
			"Offset %llu, size_in_bytes %zu, will be out of bound.\n", offset,
			size_in_bytes);
		return -EFAULT;
	}

	if (ioreg_dev->reg_valid_range_list.count == 0)
		return 0;

	for (int i = 0; i < ioreg_dev->reg_valid_range_list.count; ++i) {
		uint64_t range_low_bound = ioreg_dev->reg_valid_range_list.ranges[i].start_addr;
		uint64_t range_high_bound = ioreg_dev->reg_valid_range_list.ranges[i].start_addr +
					    ioreg_dev->reg_valid_range_list.ranges[i].size;

		if (ioreg_dev->reg_valid_range_list.ranges[i].block_id != index)
			continue;
		if (offset >= range_low_bound && offset <= range_high_bound &&
		    offset + size_in_bytes <= range_high_bound) {
			return 0;
		}
	}
	dev_err(ioreg_dev->base_dev.dev,
		"Index %d, offset %#llx, size_in_bytes %zu, will be out of valid range.\n", index,
		offset, size_in_bytes);
	return -EFAULT;
}

static int validate_access_size(int access_size, int native_value_bitwidth)
{
	if (access_size != native_value_bitwidth) {
		if (access_size != 8 && access_size != 16 && access_size != 32 &&
		    access_size != 64) {
			pr_err("Access size must be 8, 16, 32 or 64 - Actual %d\n", access_size);
			return -EINVAL;
		}
		if (access_size > native_value_bitwidth) {
			pr_err("Access size (%d) > bitwidth (%d) is not supported yet\n",
			       access_size, native_value_bitwidth);
			return -EINVAL;
		}
	}
	return 0;
}

int lwis_ioreg_list_alloc(struct lwis_ioreg_device *ioreg_dev, int num_blocks)
{
	struct lwis_ioreg_list *list;

	if (!ioreg_dev) {
		pr_err("LWIS IOREG device is NULL\n");
		return -ENODEV;
	}

	/* No need to allocate if num_blocks is invalid */
	if (num_blocks <= 0)
		return -EINVAL;

	list = &ioreg_dev->reg_list;
	list->block = kmalloc_array(num_blocks, sizeof(struct lwis_ioreg), GFP_KERNEL);
	if (!list->block)
		return -ENOMEM;

	list->count = num_blocks;

	return 0;
}

void lwis_ioreg_list_free(struct lwis_ioreg_device *ioreg_dev)
{
	struct lwis_ioreg_list *list;

	if (!ioreg_dev) {
		pr_err("LWIS IOREG device is NULL\n");
		return;
	}

	list = &ioreg_dev->reg_list;
	kfree(list->block);
	list->block = NULL;
	list->count = 0;
}

int lwis_ioreg_get(struct lwis_ioreg_device *ioreg_dev, int index, const char *name)
{
	struct resource *res;
	struct lwis_ioreg *block;
	struct lwis_ioreg_list *list;
	struct platform_device *plat_dev;

	if (!ioreg_dev) {
		pr_err("LWIS IOREG device is NULL\n");
		return -ENODEV;
	}

	plat_dev = ioreg_dev->base_dev.plat_dev;
	list = &ioreg_dev->reg_list;
	if (index < 0 || index >= list->count)
		return -EINVAL;

	res = platform_get_resource(plat_dev, IORESOURCE_MEM, index);
	if (!res) {
		dev_err(ioreg_dev->base_dev.dev, "platform_get_resource error\n");
		return -EINVAL;
	}

	block = &list->block[index];
	block->name = name;
	block->start = res->start;
	block->size = resource_size(res);
	block->base = devm_ioremap(ioreg_dev->base_dev.k_dev, res->start, resource_size(res));
	if (!block->base) {
		dev_err(ioreg_dev->base_dev.dev, "Cannot map I/O register space\n");
		return -EINVAL;
	}

	return 0;
}

int lwis_ioreg_put_by_idx(struct lwis_ioreg_device *ioreg_dev, int index)
{
	struct lwis_ioreg_list *list;
	struct device *dev;

	if (!ioreg_dev) {
		pr_err("LWIS IOREG device is NULL\n");
		return -ENODEV;
	};

	dev = ioreg_dev->base_dev.k_dev;
	list = &ioreg_dev->reg_list;
	if (index < 0 || index >= list->count)
		return -EINVAL;

	if (!list->block[index].base)
		return -EINVAL;

	devm_iounmap(dev, list->block[index].base);

	return 0;
}

int lwis_ioreg_put_by_name(struct lwis_ioreg_device *ioreg_dev, char *name)
{
	int bidx;
	struct lwis_ioreg_list *list;
	struct device *dev;

	if (!ioreg_dev) {
		pr_err("LWIS IOREG device is NULL\n");
		return -ENODEV;
	};

	dev = ioreg_dev->base_dev.k_dev;
	list = &ioreg_dev->reg_list;
	bidx = find_block_idx_by_name(list, name);
	if (bidx < 0)
		return bidx;

	if (list->block[bidx].base == NULL)
		return -EINVAL;

	devm_iounmap(dev, list->block[bidx].base);
	return 0;
}

static int ioreg_read_batch_internal(void __iomem *base, uint64_t offset, int value_bits,
				     size_t size_in_bytes, uint8_t *buf)
{
	int i;
	uint8_t *addr = (uint8_t *)base + offset;

	if (size_in_bytes & (value_bits / 8) - 1) {
		pr_err("Read buf size (%zu) not divisible by %d (bitwidth = %d)\n", size_in_bytes,
		       value_bits / 8, value_bits);
		return -EINVAL;
	}

	switch (value_bits) {
	case 8:
		for (i = 0; i < size_in_bytes; ++i)
			*(buf + i) = readb_relaxed((void __iomem *)(addr + i));
		break;
	case 16:
		for (i = 0; i < size_in_bytes; i += 2)
			*(uint16_t *)(buf + i) = readw_relaxed((void __iomem *)(addr + i));
		break;
	case 32:
		for (i = 0; i < size_in_bytes; i += 4)
			*(uint32_t *)(buf + i) = readl_relaxed((void __iomem *)(addr + i));
		break;
	case 64:
		for (i = 0; i < size_in_bytes; i += 8)
			*(uint64_t *)(buf + i) = readq_relaxed((void __iomem *)(addr + i));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ioreg_write_batch_internal(void __iomem *base, uint64_t offset, int value_bits,
				      size_t size_in_bytes, uint8_t *buf, bool is_offset_fixed)
{
	int i;
	uint8_t *addr = (uint8_t *)base + offset;

	if (size_in_bytes & (value_bits / 8) - 1) {
		pr_err("Write buf size (%zu) not divisible by %d (bitwidth = %d)\n", size_in_bytes,
		       value_bits / 8, value_bits);
		return -EINVAL;
	}

	switch (value_bits) {
	case 8:
		for (i = 0; i < size_in_bytes; ++i)
			writeb_relaxed(*(buf + i), is_offset_fixed ? (void __iomem *)(addr) :
								     (void __iomem *)(addr + i));
		break;
	case 16:
		for (i = 0; i < size_in_bytes; i += 2)
			writew_relaxed(*(uint16_t *)(buf + i), is_offset_fixed ?
								       (void __iomem *)(addr) :
								       (void __iomem *)(addr + i));
		break;
	case 32:
		for (i = 0; i < size_in_bytes; i += 4)
			writel_relaxed(*(uint32_t *)(buf + i), is_offset_fixed ?
								       (void __iomem *)(addr) :
								       (void __iomem *)(addr + i));
		break;
	case 64:
		for (i = 0; i < size_in_bytes; i += 8)
			writeq_relaxed(*(uint64_t *)(buf + i), is_offset_fixed ?
								       (void __iomem *)(addr) :
								       (void __iomem *)(addr + i));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ioreg_read_internal(void __iomem *base, uint64_t offset, int value_bits, uint64_t *value)
{
	void __iomem *addr = (void __iomem *)((uint8_t *)base + offset);

	switch (value_bits) {
	case 8:
		*value = readb_relaxed(addr);
		break;
	case 16:
		*value = readw_relaxed(addr);
		break;
	case 32:
		*value = readl_relaxed(addr);
		break;
	case 64:
		*value = readq_relaxed(addr);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ioreg_write_internal(void __iomem *base, uint64_t offset, int value_bits, uint64_t value)
{
	void __iomem *addr = (void __iomem *)((uint8_t *)base + offset);

	switch (value_bits) {
	case 8:
		writeb_relaxed((uint8_t)value, addr);
		break;
	case 16:
		writew_relaxed((uint16_t)value, addr);
		break;
	case 32:
		writel_relaxed((uint32_t)value, addr);
		break;
	case 64:
		writeq_relaxed(value, addr);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int lwis_ioreg_io_entry_rw(struct lwis_ioreg_device *ioreg_dev, struct lwis_io_entry *entry)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&ioreg_dev->base_dev.lock, flags);
	ret = lwis_ioreg_io_entry_rw_locked(ioreg_dev, entry);
	spin_unlock_irqrestore(&ioreg_dev->base_dev.lock, flags);

	return ret;
}

int lwis_ioreg_io_entry_rw_locked_with_size(struct lwis_ioreg_device *ioreg_dev,
					    struct lwis_io_entry *entry, int access_size)
{
	int ret = 0;
	int index;
	struct lwis_ioreg *block;
	uint64_t reg_value = 0;
	uint64_t total_offset;
	int effective_read_size;
	int effective_write_size;

	if (!ioreg_dev) {
		pr_err("LWIS IOREG device is NULL\n");
		return -ENODEV;
	};

	if (!entry) {
		dev_err(ioreg_dev->base_dev.dev, "IO entry is NULL.\n");
		return -EINVAL;
	}

	effective_read_size =
		access_size > 0 ? access_size : ioreg_dev->base_dev.native_read_value_bitwidth;
	effective_write_size =
		access_size > 0 ? access_size : ioreg_dev->base_dev.native_write_value_bitwidth;

	if (entry->type == LWIS_IO_ENTRY_READ) {
		index = entry->rw.bid;
		block = get_block_by_idx(ioreg_dev, index);
		if (IS_ERR_OR_NULL(block))
			return PTR_ERR(block);

		total_offset = get_total_offset(entry->rw.offset, block);

		ret = lwis_ioreg_read(ioreg_dev, block, index, total_offset, &entry->rw.val,
				      effective_read_size);
		if (ret) {
			dev_err(ioreg_dev->base_dev.dev,
				"ioreg read failed at: Bid: %d, Offset: 0x%llx\n", entry->rw.bid,
				total_offset);
		}
	} else if (entry->type == LWIS_IO_ENTRY_READ_BATCH) {
		index = entry->rw_batch.bid;
		block = get_block_by_idx(ioreg_dev, index);
		if (IS_ERR_OR_NULL(block))
			return PTR_ERR(block);

		total_offset = get_total_offset(entry->rw_batch.offset, block);

		ret = validate_offset(ioreg_dev, index, block, total_offset,
				      entry->rw_batch.size_in_bytes,
				      ioreg_dev->base_dev.native_addr_bitwidth / 8);
		if (ret) {
			dev_err(ioreg_dev->base_dev.dev,
				"ioreg validate_offset failed at: Offset: 0x%llx\n", total_offset);
			return ret;
		}

		ret = ioreg_read_batch_internal(block->base, total_offset,
						ioreg_dev->base_dev.native_read_value_bitwidth,
						entry->rw_batch.size_in_bytes, entry->rw_batch.buf);
		if (ret) {
			dev_err(ioreg_dev->base_dev.dev, "Invalid ioreg batch read at:\n");
			dev_err(ioreg_dev->base_dev.dev, "Offset: 0x%llx, Base: %pK\n",
				total_offset, block->base);
		}
	} else if (entry->type == LWIS_IO_ENTRY_WRITE) {
		index = entry->rw.bid;
		block = get_block_by_idx(ioreg_dev, index);
		if (IS_ERR_OR_NULL(block))
			return PTR_ERR(block);

		total_offset = get_total_offset(entry->rw.offset, block);

		ret = lwis_ioreg_write(ioreg_dev, block, index, total_offset, entry->rw.val,
				       effective_write_size);
		if (ret)
			dev_err(ioreg_dev->base_dev.dev,
				"ioreg write failed at: Bid: %d, Offset: 0x%llx\n", entry->rw.bid,
				total_offset);
	} else if (entry->type == LWIS_IO_ENTRY_WRITE_BATCH) {
		if (ioreg_dev->base_dev.is_read_only) {
			dev_err(ioreg_dev->base_dev.dev, "Device is read only\n");
			return -EPERM;
		}

		index = entry->rw_batch.bid;
		block = get_block_by_idx(ioreg_dev, index);
		if (IS_ERR_OR_NULL(block))
			return PTR_ERR(block);

		total_offset = get_total_offset(entry->rw_batch.offset, block);

		ret = validate_offset(ioreg_dev, index, block, total_offset,
				      entry->rw_batch.size_in_bytes,
				      ioreg_dev->base_dev.native_addr_bitwidth / 8);
		if (ret) {
			dev_err(ioreg_dev->base_dev.dev,
				"ioreg validate_offset failed at: Offset: 0x%llx\n", total_offset);
			return ret;
		}
		ret = ioreg_write_batch_internal(block->base, total_offset,
						 ioreg_dev->base_dev.native_write_value_bitwidth,
						 entry->rw_batch.size_in_bytes, entry->rw_batch.buf,
						 entry->rw_batch.is_offset_fixed);
		if (ret) {
			dev_err(ioreg_dev->base_dev.dev, "Invalid ioreg batch write at:\n");
			dev_err(ioreg_dev->base_dev.dev, "Offset: 0x%08llx, Base: %pK\n",
				total_offset, block->base);
		}
	} else if (entry->type == LWIS_IO_ENTRY_MODIFY) {
		index = entry->mod.bid;
		block = get_block_by_idx(ioreg_dev, index);
		if (IS_ERR_OR_NULL(block))
			return PTR_ERR(block);

		total_offset = get_total_offset(entry->mod.offset, block);

		ret = lwis_ioreg_read(ioreg_dev, block, index, total_offset, &reg_value,
				      effective_read_size);
		if (ret) {
			dev_err(ioreg_dev->base_dev.dev,
				"ioreg modify read failed at: Bid: %d, Offset: 0x%llx\n", index,
				total_offset);
			return ret;
		}
		reg_value &= ~entry->mod.val_mask;
		reg_value |= entry->mod.val_mask & entry->mod.val;
		ret = lwis_ioreg_write(ioreg_dev, block, index, total_offset, reg_value,
				       effective_write_size);
		if (ret) {
			dev_err(ioreg_dev->base_dev.dev, "ioreg modify write failed at:");
			dev_err(ioreg_dev->base_dev.dev, "Bid: %d, Offset: 0x%llx, Value: 0x%llx",
				index, total_offset, reg_value);
		}
	} else {
		dev_err(ioreg_dev->base_dev.dev, "Invalid IO entry type: %d\n", entry->type);
		return -EINVAL;
	}

	return ret;
}

int lwis_ioreg_io_entry_rw_locked(struct lwis_ioreg_device *ioreg_dev, struct lwis_io_entry *entry)
{
	return lwis_ioreg_io_entry_rw_locked_with_size(ioreg_dev, entry, 0);
}

int lwis_ioreg_read(struct lwis_ioreg_device *ioreg_dev, struct lwis_ioreg *block, int index,
		    uint64_t offset, uint64_t *value, int access_size)
{
	int ret;
	uint64_t internal_offset = offset;
	unsigned int native_value_bitwidth;
	uint64_t offset_mask;

	if (!ioreg_dev) {
		pr_err("LWIS IOREG device is NULL\n");
		return -ENODEV;
	};

	native_value_bitwidth = ioreg_dev->base_dev.native_read_value_bitwidth;
	ret = validate_access_size(access_size, native_value_bitwidth);
	if (ret) {
		dev_err(ioreg_dev->base_dev.dev, "Invalid access size\n");
		return ret;
	}
	if (access_size != native_value_bitwidth) {
		offset_mask = native_value_bitwidth / BITS_PER_BYTE - 1;
		internal_offset = offset & ~offset_mask;
	}

	/* Access_size is bitwidth and validate_offset expects size of bytes */
	ret = validate_offset(ioreg_dev, index, block, internal_offset, access_size / 8,
			      ioreg_dev->base_dev.native_addr_bitwidth / 8);
	if (ret)
		return ret;

	ret = ioreg_read_internal(block->base, internal_offset, native_value_bitwidth, value);

	if (access_size != native_value_bitwidth) {
		*value >>= (offset - internal_offset) * BITS_PER_BYTE;
		if (access_size < BITS_PER_TYPE(uint64_t))
			*value &= ((1ULL << access_size) - 1);
	}

	return ret;
}

int lwis_ioreg_write(struct lwis_ioreg_device *ioreg_dev, struct lwis_ioreg *block, int index,
		     uint64_t offset, uint64_t value, int access_size)
{
	int ret;
	uint64_t internal_offset = offset;
	unsigned int native_value_bitwidth;
	uint64_t read_value = 0;
	uint64_t offset_mask = 0;
	uint64_t value_mask = 0;

	if (!ioreg_dev) {
		pr_err("LWIS IOREG device is NULL\n");
		return -ENODEV;
	};

	if (ioreg_dev->base_dev.is_read_only) {
		dev_err(ioreg_dev->base_dev.dev, "Device is read only\n");
		return -EPERM;
	}

	native_value_bitwidth = ioreg_dev->base_dev.native_write_value_bitwidth;
	ret = validate_access_size(access_size, native_value_bitwidth);
	if (ret) {
		dev_err(ioreg_dev->base_dev.dev, "Invalid access size\n");
		return ret;
	}

	if (access_size < 64 && access_size != native_value_bitwidth) {
		offset_mask = native_value_bitwidth / BITS_PER_BYTE - 1;
		internal_offset = offset & ~offset_mask;
		value_mask = ((1ULL << access_size) - 1);
		value_mask <<= (offset - internal_offset) * BITS_PER_BYTE;
		/* We need to read-modify-write in this case */
		ioreg_read_internal(block->base, internal_offset, native_value_bitwidth,
				    &read_value);
		value <<= (offset - internal_offset) * 8;
		value = (value & value_mask) | (read_value & ~value_mask);
	}

	/* Access_size is bitwidth and validate_offset expects size of bytes */
	ret = validate_offset(ioreg_dev, index, block, internal_offset, access_size / 8,
			      ioreg_dev->base_dev.native_addr_bitwidth / 8);
	if (ret)
		return ret;

	return ioreg_write_internal(block->base, internal_offset, native_value_bitwidth, value);
}

int lwis_ioreg_set_io_barrier(struct lwis_ioreg_device *ioreg_dev, bool use_read_barrier,
			      bool use_write_barrier)
{
	if (use_read_barrier)
		dma_rmb();

	if (use_write_barrier)
		dma_wmb();

	return 0;
}
