#ifndef WDEV_IF_CHIP_BRCM_4390_WDEV_IF_CHIP_BRCM_4390_H
#define WDEV_IF_CHIP_BRCM_4390_WDEV_IF_CHIP_BRCM_4390_H

#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "wdev_if/wdev_if.h"

extern int32_t WdevChipBrcm4390Init(WdevIf *const wdev_if);
extern void WdevChipBrcm4390Deinit(WdevIf *const wdev_if);
extern void WdevChipBrcm4390AcknowledgeInterrupt(WdevIf *const wdev_if, int32_t irq_id);
extern void WdevChipBrcm4390RingTxPostDoorbell(WdevIf *const wdev_if, void *priv);
extern bool WdevChipBrcm4390CheckPcieCmplTimeOut(void);
extern bool WdevChipBrcm4390FwTrapCheck(uint64_t fw_trap_addr);

#endif /* WDEV_IF_CHIP_BRCM_4390_WDEV_IF_CHIP_BRCM_4390_H */
