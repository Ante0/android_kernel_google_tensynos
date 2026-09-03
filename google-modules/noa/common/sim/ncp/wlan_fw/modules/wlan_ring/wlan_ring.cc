#include "wlan_ring.h"

#include "sys_if/io/sys_if_io.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "wlan_cast.h"

int32_t WlanRingInit(WlanRing *const ring, const WlanRingInitParams *const params)
{
	// Given the identical structure of WlanRingInitParams and WlanRing, we can
	// directly copy the `params` to the `ring`.
	memcpy(ring, params, sizeof(WlanRing));

	return 0;
}

void WlanRingDeinit(WlanRing *const ring)
{
	memset(ring, 0, sizeof(WlanRing));
}
