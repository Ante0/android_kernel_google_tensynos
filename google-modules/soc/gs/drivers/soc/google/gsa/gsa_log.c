// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Google LLC.
 */
#include <asm/page.h>
#include <kunit/visibility.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include "gsa_log.h"

#define GSA_LOG_MAGIC 'GSAL'

/*
 * struct gsa_log_hdr - GSA log header
 * @magic: should be set to %GSA_LOG_MAGIC
 * @size: log buffer size
 * @tail: offset of location in log buffer where next character should be
 *        written. Normally should contain 0 as a head/tail separator.
 *
 * In memory this structure prepends GSA log body.
 */
struct gsa_log_hdr {
	uint32_t magic;
	uint32_t size;
	uint32_t tail;
} __packed;

struct gsa_log_mem {
	struct gsa_log_hdr hdr;
	uint8_t body[];
} __packed;

struct gsa_log {
	struct device *dev;
	struct gsa_log_mem __iomem *base_main;
	struct gsa_log_mem __iomem *base_intermediate;
};

struct gsa_log *gsa_log_init(struct platform_device *pdev)
{
	struct device_node *np;
	struct gsa_log *log;
	struct reserved_mem *rmem;
	struct device *dev = &pdev->dev;
	uint32_t magic;
	size_t size;

	/* Check if we have log in the device tree; it is not required. */
	np = of_parse_phandle(dev->of_node, "log-region", 0);
	if (!np) {
		dev_info(dev, "log region not configured\n");
		return NULL;
	}

	rmem = of_reserved_mem_lookup(np);
	of_node_put(np);
	if (!rmem) {
		dev_err(dev, "configured log region invalid\n");
		return ERR_PTR(-ENODEV);
	}

	if (rmem->size % (GSA_LOG_SIZE * 2)) {
		dev_err(dev, "log size not multiple of expected size %d", (GSA_LOG_SIZE * 2));
		return ERR_PTR(-EINVAL);
	}

	log = devm_kzalloc(dev, sizeof(*log), GFP_KERNEL);
	if (!log)
		return ERR_PTR(-ENOMEM);

	log->dev = dev;

	/* map main log region */
	log->base_main = devm_ioremap(dev, rmem->base, rmem->size);
	if (!log->base_main)
		return ERR_PTR(-ENOMEM);

	/* map intermediate reset log region */
	log->base_intermediate = (void __iomem *)log->base_main + GSA_LOG_SIZE;

	/* validate the main log header, but only warn for now */
	magic = readl(&log->base_main->hdr.magic);
	size = readl(&log->base_main->hdr.size);

	if (magic != GSA_LOG_MAGIC)
		dev_warn(dev, "bad log header magic: %#x\n", magic);

	if (size != (GSA_LOG_SIZE - sizeof(struct gsa_log_hdr)))
		dev_warn(dev, "bad log header size: %zu\n", size);

	return log;
}

ssize_t gsa_log_read(struct gsa_log *log, bool intermediate, char *buf, size_t read_size)
{
	struct gsa_log_mem __iomem *gsa_log_mem_base;
	uint32_t magic = 0;
	size_t size = 0;
	size_t tail = 0;
	size_t next_read_size = 0;
	size_t copied = 0;

	if (IS_ERR(log))
		return PTR_ERR(log);
	else if (log == NULL)
		return -ENODEV;

	dev_dbg(log->dev, "%s(%p, %d, %p, %zu)\n", __func__, log, intermediate, buf, read_size);

	if (intermediate)
		gsa_log_mem_base = log->base_intermediate;
	else
		gsa_log_mem_base = log->base_main;

	magic = readl(&gsa_log_mem_base->hdr.magic);
	size = readl(&gsa_log_mem_base->hdr.size);
	tail = readl(&gsa_log_mem_base->hdr.tail);

	if (magic != GSA_LOG_MAGIC || size != (GSA_LOG_SIZE - sizeof(struct gsa_log_hdr)) ||
	    tail >= size) {
		dev_err(log->dev, "log hdr is corrupted\n");
		dev_dbg(log->dev, "Magic: %u\n", magic);
		dev_dbg(log->dev, "Size: %#zx\n", size);
		dev_dbg(log->dev, "Size Expected size: %#lx\n",
			(GSA_LOG_SIZE - sizeof(struct gsa_log_hdr)));
		dev_dbg(log->dev, "Tail: %#zx\n", tail);
		return -EIO;
	}

	/*
	 * 'tail' points to the end of the current log. There are two
	 * possibilities about the rest of the buffer after 'tail':
	 *  1. The buffer is empty; we ignore it.
	 *  2. The buffer has older log data; we need to copy it first.
	 *
	 * Check to see if the byte after tail is null to discriminate
	 * which case the log is in:
	 */
	next_read_size = size - tail - 1;
	copied = 0;
	if ((tail + 1) != size && readb(&gsa_log_mem_base->body[tail + 1])) {
		if (next_read_size > read_size - 1)
			next_read_size = read_size - 1;
		dev_dbg(log->dev, "Reading first hunk (%zu)\n", next_read_size);
		memcpy_fromio(buf, &gsa_log_mem_base->body[tail + 1], next_read_size);
		copied += next_read_size;
	}

	/* Copy the newer log data (bytes up to 'tail') */
	next_read_size = tail;
	if (next_read_size > read_size - copied - 1)
		next_read_size = read_size - copied - 1;
	dev_dbg(log->dev, "Reading second hunk (%zu)\n", next_read_size);
	memcpy_fromio(&buf[copied], gsa_log_mem_base->body, next_read_size);
	copied += next_read_size;
	buf[copied] = '\0';

	return copied + 1;
}
EXPORT_SYMBOL_IF_KUNIT(gsa_log_read);

MODULE_LICENSE("GPL v2");
