/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _GOOGLE_APT_LT_H
#define _GOOGLE_APT_LT_H

#include <linux/seq_file.h>

struct google_apt_lt;

int google_apt_lt_init(struct google_apt_lt *lt);
// We cannot remove clockevents.

int google_apt_lt_stat(const struct google_apt_lt *lt, struct seq_file *file);

#endif /* _GOOGLE_APT_LT_H */
