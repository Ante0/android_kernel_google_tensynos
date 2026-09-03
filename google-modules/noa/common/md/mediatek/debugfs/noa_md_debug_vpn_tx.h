/* common/md/mediatek/debug/noa_md_debug_vpn_tx.h */

#ifndef __NOA_MD_DEBUG_TX_VPN_H__
#define __NOA_MD_DEBUG_TX_VPN_H__

#include <linux/debugfs.h>
#include "../noa_md.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * noa_md_debug_vpn_tx_init() - Initialize debugfs entries for VPN TX testing.
 * @noa_root: The parent dentry for the "noa_md" debugfs directory.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 *
 * Creates the "vpn_tx" debugfs directory and populates it with files
 * for functional and stress testing of the VPN TX path.
 *
 * Return:
 * * %0: On success.
 * * Negative error code: On failure.
 */
int noa_md_debug_vpn_tx_init(
	struct dentry *noa_root, struct noa_md_dev *p_md_dev);

/**
 * noa_md_debug_vpn_tx_exit() - Exit function for VPN TX debugfs.
 *
 * This function is empty because cleanup is handled by the caller,
 * which recursively removes the parent debugfs directory.
 */
void noa_md_debug_vpn_tx_exit(void);
#else
static inline int noa_md_debug_vpn_tx_init(
	struct dentry *noa_root, struct noa_md_dev *p_md_dev)
{
	return 0;
}

static inline void noa_md_debug_vpn_tx_exit(void)
{
}
#endif /* CONFIG_DEBUG_FS */

#endif /* __NOA_MD_DEBUG_TX_VPN_H__ */