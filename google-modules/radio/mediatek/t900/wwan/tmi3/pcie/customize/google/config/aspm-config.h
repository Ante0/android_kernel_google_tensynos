/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __ASPM_CONFIG_H__
#define __ASPM_CONFIG_H__

#include <linux/pci.h>

/*
 * =========================================================================
 * Google-Specific PCIe ASPM (Active State Power Management) Config
 * =========================================================================
 * This header provides a configurable bitmask for PCIe ASPM link states.
 * It is intended to be included only when CONFIG_ARCH_GOOGLE is defined.
 *
 * b/386315862: Disables L1.2 to workaround CLDMA stuck issue.
 */

/*
 * -------------------------------------------------------------------------
 * ASPM Link State Options
 * -------------------------------------------------------------------------
 * Set to 1 to enable the state, 0 to disable.
 */
#define GOOGLE_PCIE_ASPM_ENABLE_L0S 1
#define GOOGLE_PCIE_ASPM_ENABLE_L1 1
#define GOOGLE_PCIE_ASPM_ENABLE_L1_1 1
#define GOOGLE_PCIE_ASPM_ENABLE_L1_2 1
#define GOOGLE_PCIE_ASPM_ENABLE_L1_1_PCIPM 1
#define GOOGLE_PCIE_ASPM_ENABLE_L1_2_PCIPM 1
#define GOOGLE_PCIE_ASPM_ENABLE_CLKPM 1

/*
 * =========================================================================
 * Configuration Assembly (Do not modify below)
 * =========================================================================
 */

#if (GOOGLE_PCIE_ASPM_ENABLE_L0S == 1) && (GOOGLE_PCIE_ASPM_ENABLE_L1 == 1) &&                    \
	(GOOGLE_PCIE_ASPM_ENABLE_L1_1 == 1) && (GOOGLE_PCIE_ASPM_ENABLE_L1_2 == 1) &&             \
	(GOOGLE_PCIE_ASPM_ENABLE_L1_1_PCIPM == 1) && (GOOGLE_PCIE_ASPM_ENABLE_L1_2_PCIPM == 1) && \
	(GOOGLE_PCIE_ASPM_ENABLE_CLKPM == 1)

/*
 * If all states are enabled, use the default value 0xFFFF.
 * This can avoid the redundant `pci_enable_link_state` call in vendor code.
 */
#define GOOGLE_PCIE_ASPM_LINK_STATE 0xFFFF

#else

/* Otherwise, construct the bitmask from the individual flags. */
#define GOOGLE_PCIE_ASPM_LINK_STATE                                              \
	((GOOGLE_PCIE_ASPM_ENABLE_L0S ? PCIE_LINK_STATE_L0S : 0) |               \
	 (GOOGLE_PCIE_ASPM_ENABLE_L1 ? PCIE_LINK_STATE_L1 : 0) |                 \
	 (GOOGLE_PCIE_ASPM_ENABLE_L1_1 ? PCIE_LINK_STATE_L1_1 : 0) |             \
	 (GOOGLE_PCIE_ASPM_ENABLE_L1_2 ? PCIE_LINK_STATE_L1_2 : 0) |             \
	 (GOOGLE_PCIE_ASPM_ENABLE_L1_1_PCIPM ? PCIE_LINK_STATE_L1_1_PCIPM : 0) | \
	 (GOOGLE_PCIE_ASPM_ENABLE_L1_2_PCIPM ? PCIE_LINK_STATE_L1_2_PCIPM : 0) | \
	 (GOOGLE_PCIE_ASPM_ENABLE_CLKPM ? PCIE_LINK_STATE_CLKPM : 0))

#endif /* Check for all states enabled */

#endif /* __ASPM_CONFIG_H__ */
