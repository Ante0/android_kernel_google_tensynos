#pragma once

#include <cstdint>

#include "wdev_if/wdev_if.h"

int32_t WdevChipFakeBrcmInit(WdevIf *const wdev_if);
void WdevChipFakeBrcmDeinit(WdevIf *const wdev_if);
void WdevChipFakeBrcmAcknowledgeInterrupt(WdevIf *const wdev_if, int32_t irq_id);
void WdevChipFakeBrcmRingTxPostDoorbell(WdevIf *const wdev_if, void *priv);
