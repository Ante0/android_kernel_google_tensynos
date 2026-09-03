#ifndef SYS_IF_NOA_NET_NOA_NET_H
#define SYS_IF_NOA_NET_NOA_NET_H

#if defined(__KERNEL__)
#include "sys_if/types/types.h"

#include <linux/if_ether.h>

#include "nep_helpers.h"
#include "netengine.h"
#else
#include "net/map_def.h"
#endif

#if defined(__KERNEL__)
// Linting is disabled for compatibility code.
// NOLINTBEGIN(readability-identifier-naming)
#define kNepPutFlowIdMap NEP_CMD_NETENGINE_FLOWID_UPDATE
#define kNepRemoveFlowIdMap NEP_CMD_NETENGINE_FLOWID_UPDATE

typedef struct NoaNetTetherFlowIdEntry {
	struct {
		char dstMac[ETH_ALEN];
		uint8_t priority;
	} key;
	uint8_t enable;
	uint32_t value; // flowid
} NoaNetTetherFlowIdEntry;

typedef struct NoaNetTetherFlowIdKey {
	char dstMac[ETH_ALEN];
	uint8_t priority;
	uint8_t enable;
	uint32_t flowid;
} NoaNetTetherFlowIdKey;
// NOLINTEND(readability-identifier-naming)
#else
typedef TetherFlowIdEntry NoaNetTetherFlowIdEntry;
typedef TetherFlowIdKey NoaNetTetherFlowIdKey;
#endif

/// @brief Initializes a TetherFlowIdEntry structure.
///
/// This function initializes a TetherFlowIdEntry structure to its default
/// values.
///
/// @param[out] entry A pointer to the TetherFlowIdEntry structure to be
/// initialized.
extern void SysIfTetherFlowIdEntryInit(NoaNetTetherFlowIdEntry *entry);

/// @brief Initializes a TetherFlowIdKey structure.
///
/// This function initializes a TetherFlowIdKey structure to its default
/// values.
///
/// @param[out] key A pointer to the TetherFlowIdKey structure to be
/// initialized.
extern void SysIfTetherFlowIdKeyInit(NoaNetTetherFlowIdKey *key);

#endif /* SYS_IF_NOA_NET_NOA_NET_H */
