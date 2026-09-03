/* SPDX-License-Identifier: GPL-2.0 */
/*
 * WCN Subsystem Power Control Driver - External API Header
 *
 * This header defines the public interface for the WCN power control
 * module. It allows external subsystems to interact with the power
 * manager, such as toggling the Find My Device (FMD) persistence state.
 */

#ifndef __WCN_PWRCTL_H__
#define __WCN_PWRCTL_H__

#include <linux/types.h>

void wcn_set_fmd_state(bool enable);

#endif /* __WCN_PWRCTL_H__ */
