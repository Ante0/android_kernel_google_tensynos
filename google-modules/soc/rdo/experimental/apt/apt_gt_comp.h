/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _GOOGLE_APT_GT_COMP_H
#define _GOOGLE_APT_GT_COMP_H

struct google_apt_gt_comp;

int google_apt_gt_comp_init(struct google_apt_gt_comp *gt_comp);
// We cannot remove clockevents.

int google_apt_gt_comp_stat(const struct google_apt_gt_comp *gt_comp,
			    struct seq_file *file);

#endif /* _GOOGLE_APT_GT_COMP_H */
