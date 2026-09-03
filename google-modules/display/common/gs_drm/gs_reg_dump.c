// SPDX-License-Identifier: MIT

#include <linux/minmax.h>
#include <linux/regmap.h>
#include <gs_drm/gs_reg_dump.h>
#include <trace/dpu_trace.h>

static inline bool
_gs_reg_dump_print_line_buf(char *line_buf, size_t line_buf_size, const volatile void __iomem *base,
			    u32 offset, u32 line_size,
			    const struct regmap_access_table *regmap_access_table)
{
	u32 line_vals[REG_DUMP_MAX_LINE_BYTE_LEN / sizeof(u32)] = { 0 };
	bool any_read = false;
	int i;

	/* read registers into temp buffer */
	for (i = 0; i < line_size / sizeof(u32); ++i) {
		const volatile u32 __iomem *addr = base + offset + i * sizeof(u32);

		if (!regmap_access_table ||
		    regmap_check_range_table(NULL, offset + i * sizeof(u32), regmap_access_table)) {
			line_vals[i] = readl(addr);
			any_read = true;
		} else {
			line_vals[i] = 0x0;
		}
	}

	if (!any_read)
		return false;

	/* convert temp buffer to hex */
	hex_dump_to_buffer(line_vals, line_size, REG_DUMP_MAX_LINE_BYTE_LEN, REG_DUMP_REG_BYTES,
			   line_buf, line_buf_size, false);
	return true;
}

int gs_reg_dump_with_skips(const char *desc, const volatile void __iomem *base, u32 offset,
			   u32 size, struct drm_printer *p,
			   const struct regmap_access_table *regmap_access_table)
{
	/*
	 * Each byte is printed as 3 characters (2 hex digits and a space),
	 * so we need 3 * REG_DUMP_MAX_LINE_BYTE_LEN + 1 bytes for the buffer.
	 */
	char line_buf[REG_DUMP_MAX_LINE_BYTE_LEN * 3 + 1];

	if (offset % REG_DUMP_OFFSET_ALIGNMENT)
		return -EINVAL;
	if (size % REG_DUMP_REG_BYTES)
		return -EINVAL;
	if (size == 0)
		return -EINVAL;

	DPU_ATRACE_BEGIN(__func__);
	if (p)
		drm_printf(p, "# %s\n", desc);
	trace_reg_dump_header(desc, offset, size);
	while (size > 0) {
		u32 line_size = min(size, REG_DUMP_MAX_LINE_BYTE_LEN);

		if (_gs_reg_dump_print_line_buf(line_buf, sizeof(line_buf), base, offset, line_size,
						regmap_access_table)) {
			if (p)
				drm_printf(p, "%08X: %s\n", offset, line_buf);
			trace_reg_dump_line(offset, line_buf);
		}

		size -= line_size;
		offset += line_size;
	}
	DPU_ATRACE_END(__func__);

	return 0;
}
EXPORT_SYMBOL_GPL(gs_reg_dump_with_skips);

int gs_reg_dump(const char *desc, const volatile void __iomem *base, u32 offset, u32 size,
		struct drm_printer *p)
{
	return gs_reg_dump_with_skips(desc, base, offset, size, p, NULL);
}
EXPORT_SYMBOL_GPL(gs_reg_dump);
