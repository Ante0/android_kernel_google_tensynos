/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _G2D_RECOVERY_H_
#define _G2D_RECOVERY_H_

#include <linux/workqueue.h>

#include "g2d_sc.h"

struct g2d_sc;

struct g2d_recovery {
	struct work_struct recovery_work;
	atomic_t in_recovery;
};

void g2d_reset_trigger(struct g2d_sc *sc);
void g2d_reset_register(struct g2d_sc *sc);

#endif /* _G2D_RECOVERY_H_ */
