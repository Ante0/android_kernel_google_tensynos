// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "include/dptx.h"

#ifdef DPTX_DEBUG_DPCD_CMDS

#define BYTES_STR_LEN 128

char *__bytes_str(u8 *bytes, unsigned int count)
{
	static char str[BYTES_STR_LEN];
	unsigned int i;
	int written = 0;
	int total = 0;

	memset(str, 0, sizeof(str));

	for (i = 0; i < count; i++) {
		written = snprintf(&str[total], BYTES_STR_LEN - total,
				   "0x%02x", bytes[i]);
		if (written >= (BYTES_STR_LEN - total))
			break;

		total += written;

		if (i < (count - 1)) {
			written = snprintf(&str[total], BYTES_STR_LEN - total,
					   ", ");
			if (written >= (BYTES_STR_LEN - total))
				break;

			total += written;
		}
	}

	if (written == (BYTES_STR_LEN - total)) {
		str[BYTES_STR_LEN - 2] = '.';
		str[BYTES_STR_LEN - 3] = '.';
		str[BYTES_STR_LEN - 4] = '.';
		str[BYTES_STR_LEN - 5] = ' ';
	}

	return str;
}

#endif
