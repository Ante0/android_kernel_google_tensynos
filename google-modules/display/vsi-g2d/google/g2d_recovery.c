// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <drm/drm_crtc.h>
#include <drm/drm_print.h>
#include <linux/iopoll.h>

#include "g2d_crtc.h"
#include "g2d_sc.h"
#include "g2d_sc_hw.h"
#include "g2d_trace.h"
#include "g2d_writeback.h"
#include "g2d_registers.h"

#define SW_RESET_INTERVAL_US 1000
#define SW_RESET_TIMEOUT_US 10000

static void g2d_reset_wb(struct g2d_sc *sc)
{
	int i;

	for (i = 0; i < NUM_PIPELINES; i++)
		g2d_signal_wb_error(sc->writeback[i]);
}

static void g2d_reset_handler(struct work_struct *work)
{
	struct g2d_recovery *recovery = container_of(work, struct g2d_recovery, recovery_work);
	struct g2d_sc *sc = container_of(recovery, struct g2d_sc, g2d_recovery);
	struct device *dev = sc->hw.dev;
	u8 g2d_rst_status = 0;
	int ret = 0;
	int i = 0;

	if (!sc->allow_reset) {
		dev_err(dev, "G2D reset has been disabled. Skipping recovery");
		return;
	}

	G2D_ATRACE_BEGIN(__func__);
	dev_err(dev, "triggering g2d reset");

	sc_write(&sc->hw, SCREG_SW_RESET_Address, 0x1);

	ret = readb_poll_timeout(&sc->hw.reset_status, g2d_rst_status, g2d_rst_status,
				 SW_RESET_INTERVAL_US, SW_RESET_TIMEOUT_US);

	if (ret) {
		dev_err(dev, "G2D reset timedout %d", ret);
		return;
	}

	g2d_reset_wb(sc);

	for (i = 0; i < NUM_PIPELINES; i++)
		sc->crtc[i]->funcs->disable(dev, &sc->crtc[i]->base);

	for (i = 0; i < NUM_PIPELINES; i++)
		sc->crtc[i]->funcs->enable(dev, &sc->crtc[i]->base);

	sc_hw_restore_state(&sc->hw);

	sc->requires_reset = false;
	G2D_ATRACE_END(__func__);
}

void g2d_reset_register(struct g2d_sc *sc)
{
	INIT_WORK(&sc->g2d_recovery.recovery_work, g2d_reset_handler);
	sc->allow_reset = true;
}

void g2d_reset_trigger(struct g2d_sc *sc)
{
	if (sc->requires_reset)
		queue_work(system_highpri_wq, &sc->g2d_recovery.recovery_work);
}
