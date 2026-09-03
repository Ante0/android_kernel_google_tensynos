/* SPDX-License-Identifier: GPL-2.0 */

#ifndef PIXELMD_CMD_LMKD_KILL_
#define PIXELMD_CMD_LMKD_KILL_

/* Handles PIXELMD_CMD_LMKD_KILL. */
long pixelmd_cmd_lmkd_kill(void __user *param);

int pixelmd_lmkd_kill_register_hooks(void);
void pixelmd_lmkd_kill_unregister_hooks(void);

#endif /* PIXELMD_CMD_LMKD_KILL_ */
