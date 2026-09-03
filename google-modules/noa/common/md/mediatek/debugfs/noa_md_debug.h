#ifndef __NOA_MD_DEBUG_H__
#define __NOA_MD_DEBUG_H__

#include "../noa_md.h"
#include "noa_md_debug_shmem.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * noa_md_debug_init() - Init NOA MD debugfs features.
 * @p_md_dev: Pointer to the NOA modem device structure.
 *
 * Creates the root debugfs directory for NOA MD and initializes
 * sub-modules like VPN TX debugfs.
 *
 * Return:
 * * %0: On success.
 * * Negative error code: On failure.
 */
int noa_md_debug_init(struct noa_md_dev *p_md_dev);

/**
 * noa_md_debug_exit() - Exit and clean up NOA MD debugfs features.
 *
 * Removes all debugfs entries created by noa_md_debug_init().
 */
void noa_md_debug_exit(void);
#else  // CONFIG_DEBUG_FS
// Define empty inline functions if CONFIG_DEBUG_FS is not enabled
int noa_md_debug_init(struct noa_md_dev *p_md_dev) { return 0 };
void noa_md_debug_exit(void) { };
#endif  // CONFIG_DEBUG_FS
#endif  // __NOA_MD_DEBUG_H__
