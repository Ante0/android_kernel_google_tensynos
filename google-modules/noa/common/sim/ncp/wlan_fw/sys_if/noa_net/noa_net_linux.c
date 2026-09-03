#include "noa_net.h"

#include <linux/string.h>

void SysIfTetherFlowIdEntryInit(NoaNetTetherFlowIdEntry *entry)
{
	memset(entry, 0, sizeof(NoaNetTetherFlowIdEntry));
	entry->enable = 1;
}

void SysIfTetherFlowIdKeyInit(NoaNetTetherFlowIdKey *key)
{
	memset(key, 0, sizeof(NoaNetTetherFlowIdKey));
	key->enable = 0;
	key->flowid = 0;
}
