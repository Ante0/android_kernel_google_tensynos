/* common/md/mediatek/debug/t900/noa_md_debug_t900_cmd.h */

#ifndef __NOA_MD_DEBUG_T900_CMD_H__
#define __NOA_MD_DEBUG_T900_CMD_H__

#include <linux/debugfs.h>
#include "noa_md.h"

#if IS_ENABLED(CONFIG_DEBUG_FS) && \
	IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
/**
 * @brief Initializes the T900 feature command debugfs interface.
 *
 * This function creates a debugfs file "t900_cmd" that allows calling
 * noa_md_wpr_t900_feature_cmd from userspace.
 *
 * @param noa_root The parent dentry for the "noa_md" debugfs directory.
 * @param p_md_dev Pointer to the main NOA modem device struct.
 * @return 0 on success, negative error code on failure.
 */
int noa_md_debug_t900_cmd_init(
	struct dentry *noa_root, struct noa_md_dev *p_md_dev);

/**
 * @brief Exits and cleans up the T900 feature command debugfs interface.
 *
 * This function is empty because cleanup is handled by the recursive removal
 * of the parent debugfs directory.
 */
void noa_md_debug_t900_cmd_exit(void);

#else /* CONFIG_DEBUG_FS */

static inline int noa_md_debug_t900_cmd_init(
	struct dentry *noa_root, struct noa_md_dev *p_md_dev)
{
	return 0;
}

static inline void noa_md_debug_t900_cmd_exit(void)
{
}

#endif /* CONFIG_DEBUG_FS */

#endif /* __NOA_MD_DEBUG_T900_CMD_H__ */