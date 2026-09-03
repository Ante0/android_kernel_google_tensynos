/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#ifndef _CORESIGHT_TMC_ETS_H
#define _CORESIGHT_TMC_ETS_H

#define TMC_CONFIG_TYPE_ETS 3

extern const struct coresight_ops tmc_ets_cs_ops;
void tmc_ets_disable_hw(struct tmc_drvdata *drvdata);
#endif
