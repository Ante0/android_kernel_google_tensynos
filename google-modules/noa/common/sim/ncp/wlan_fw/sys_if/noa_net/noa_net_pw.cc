#include "noa_net.h"

#include <cstring>

#include "net/map_def.h"

void SysIfTetherFlowIdEntryInit(NoaNetTetherFlowIdEntry *entry)
{
	std::memset(entry, 0, sizeof(TetherFlowIdEntry));
}

void SysIfTetherFlowIdKeyInit(NoaNetTetherFlowIdKey *key)
{
	std::memset(key, 0, sizeof(TetherFlowIdKey));
}
