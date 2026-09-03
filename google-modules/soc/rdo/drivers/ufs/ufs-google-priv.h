/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */
#ifndef _UFS_GOOGLE_PRIV_H_
#define _UFS_GOOGLE_PRIV_H_

/* UFSHCD error handling flags */
enum {
	UFSHCD_EH_IN_PROGRESS = (1 << 0),
};

#define ufshcd_eh_in_progress(h) \
	((h)->eh_flags & UFSHCD_EH_IN_PROGRESS)

#define UFS_GOOG_REGISTER_VH_HANDLER(name)                                    \
	do {                                                                  \
		ret = register_trace_android_vh_##name(                       \
			ufs_google_vh_##name##_handler, NULL);                \
		if (ret) {                                                    \
			pr_err("ufs: failed to register tracepoint %s: %d\n", \
			       #name, ret);                                   \
			return ret;                                           \
		}                                                             \
	} while (0)

#endif /* _UFS_GOOGLE_PRIV_H_ */
