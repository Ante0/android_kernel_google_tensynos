// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/export.h>
#include <linux/kernel.h>

#include "mtk_debug.h"

#define TAG "REG_DUMP"

#define __mtk_log(level)                                                        \
void __mtk_##level(struct device *dev, const char *fmt, ...) \
{                                                                                                \
	struct va_format vaf = {                                                  \
		.fmt = fmt,                                                               \
	};                                                                                      \
	va_list args;                                                                    \
	va_start(args, fmt);                                                      \
	vaf.va = &args;                                                             \
	dev_##level(dev, "%pV", &vaf);                                   \
	trace_mtk_##level(dev, &vaf);                                    \
	va_end(args);                                                               \
}

static unsigned int mtk_log_mask = MTK_DBG_NONE;

__mtk_log(info)
EXPORT_SYMBOL(__mtk_info);
__mtk_log(warn)
EXPORT_SYMBOL(__mtk_warn);
__mtk_log(err)
EXPORT_SYMBOL(__mtk_err);

void __mtk_dbg(struct mtk_md_dev *mdev, enum mtk_debug_mask dbg_mask,
	       enum mtk_memlog_region_id rg_id, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	if (mtk_log_mask & dbg_mask)
		dev_dbg(mdev->dev, "%pV", &vaf);

	mtk_memlog_write(mdev, rg_id, "%pV", &vaf);
	trace_mtk_debug(mdev->dev, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__mtk_dbg);

void __mtk_hex_dump(struct mtk_md_dev *mdev, enum mtk_debug_mask dbg_mask,
		    enum mtk_memlog_region_id rg_id, const char *msg,
		    const void *mem, size_t len)
{
	char buf[MTK_LOG_BUFF_SIZE];
	size_t buf_len;
	const void *ptr;

	if (msg)
		__mtk_dbg(mdev, dbg_mask, rg_id, "%s\n", msg);

	for (ptr = mem; (ptr - mem) < len; ptr += MTK_BYTES_PER_LINE) {
		memset(buf, 0, MTK_LOG_BUFF_SIZE);
		buf_len = scnprintf(buf, sizeof(buf),
				    "%08x:", (unsigned int)(ptr - mem));
		hex_dump_to_buffer(ptr, len - (ptr - mem), MTK_BYTES_PER_LINE, 1,
				   buf + buf_len,
				   sizeof(buf) - buf_len, false);
		__mtk_dbg(mdev, dbg_mask, rg_id, "%s\n", buf);
	}

	trace_mtk_debug_dump(mdev->dev, msg, buf, len);
}
EXPORT_SYMBOL(__mtk_hex_dump);

void __mtk_dbg_ratelimited(struct mtk_md_dev *mdev, enum mtk_debug_mask dbg_mask,
			   enum mtk_memlog_region_id rg_id, const char *fmt, ...)
{
	static DEFINE_RATELIMIT_STATE(_rs,
				      DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	if (!__ratelimit(&_rs))
		return;

	va_start(args, fmt);
	vaf.va = &args;

	if (mtk_log_mask & dbg_mask)
		dev_dbg(mdev->dev, "%pV", &vaf);

	mtk_memlog_write(mdev, rg_id, "%pV", &vaf);
	trace_mtk_debug(mdev->dev, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__mtk_dbg_ratelimited);

void __mtk_info_ratelimited(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	dev_info_ratelimited(dev, "%pV", &vaf);
	trace_mtk_info(dev, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__mtk_info_ratelimited);

void __mtk_warn_ratelimited(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	dev_warn_ratelimited(dev, "%pV", &vaf);
	trace_mtk_warn(dev, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__mtk_warn_ratelimited);

void __mtk_err_ratelimited(struct device *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;

	dev_err_ratelimited(dev, "%pV", &vaf);
	trace_mtk_err(dev, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(__mtk_err_ratelimited);

module_param(mtk_log_mask, uint, 0644);
MODULE_PARM_DESC(mtk_log_mask, "The value is used to control mtk log mask.");

