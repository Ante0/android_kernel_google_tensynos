/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PIXELMD_CMD_KSWAPD_
#define PIXELMD_CMD_KSWAPD_

/* Register / unregister kswapd-related hooks. Must be called on module init. */
int pixelmd_kswapd_register_hooks(void);
void pixelmd_kswapd_unregister_hooks(void);

/** Handles PIXELMD_CMD_NUDGE_KSWAPD. */
long pixelmd_cmd_nudge_kswapd(void);

#endif /* PIXELMD_CMD_KSWAPD_ */
