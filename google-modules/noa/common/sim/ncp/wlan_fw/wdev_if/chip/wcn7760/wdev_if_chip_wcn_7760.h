#ifndef WDEV_IF_CHIP_WCN_7760_WDEV_IF_CHIP_WCN_7760_H
#define WDEV_IF_CHIP_WCN_7760_WDEV_IF_CHIP_WCN_7760_H

#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "wdev_if/wdev_if.h"

extern int32_t WdevChipWcn7760Init(WdevIf *const wdev_if);
extern void WdevChipWcn7760Deinit(WdevIf *const wdev_if);
extern void WdevChipWcn7760AcknowledgeInterrupt(WdevIf *const wdev_if, int32_t irq_id);
extern void WdevChipWcn7760RingTxPostDoorbell(WdevIf *const wdev_if, void *priv);
extern bool WdevChipWcn7760CheckPcieCmplTimeOut(void);

#endif /* WDEV_IF_CHIP_WCN_7760_WDEV_IF_CHIP_WCN_7760_H */
