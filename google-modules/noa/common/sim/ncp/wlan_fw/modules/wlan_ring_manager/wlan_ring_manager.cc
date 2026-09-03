#include "wlan_ring_manager.h"

#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "wlan_log/wlan_log.h"
#include "wlan_wdev_ring_manager.h"
#include "wlan_nep_ring_manager.h"
#include "wlan_apc_ring_manager.h"

#define INIT_RING_GROUP(MGR, RING_TYPE)                                                            \
	MGR->ring_groups[k##RING_TYPE##RingGroup].num_ring =                                       \
		k##RING_TYPE##RingEnd - k##RING_TYPE##RingStart;                                   \
	MGR->ring_groups[k##RING_TYPE##RingGroup].ring_pool =                                      \
		&MGR->ring_pool[k##RING_TYPE##RingStart];

int32_t WlanRingManagerInit(WlanRingManager *const manager)
{
	if (manager == NULL) {
		return -EINVAL;
	}

	memset(manager, 0, sizeof(WlanRingManager));

	INIT_RING_GROUP(manager, WdevTxPost);
	INIT_RING_GROUP(manager, WdevTxCmpl);
	INIT_RING_GROUP(manager, WdevRxPost);
	INIT_RING_GROUP(manager, WdevRxCmpl);
	INIT_RING_GROUP(manager, WdevCtrlPost);
	INIT_RING_GROUP(manager, WdevCtrlCmpl);
	INIT_RING_GROUP(manager, NepTxPost);
	INIT_RING_GROUP(manager, NepRxCmpl);
	INIT_RING_GROUP(manager, NepTxCmpl);
	INIT_RING_GROUP(manager, NepFeedback);
	INIT_RING_GROUP(manager, ApcDirectTxPost);
	INIT_RING_GROUP(manager, ApcDirectRxCmpl);
	INIT_RING_GROUP(manager, ApcTxCmpl);
	INIT_RING_GROUP(manager, ApcVendorRxBufRepln);
	INIT_RING_GROUP(manager, ApcNoaTxBufRepln);
	INIT_RING_GROUP(manager, ApcFeedback);
	INIT_RING_GROUP(manager, ApcFallbackRxCmpl);

	return 0;
}

void WlanRingManagerDeinit(WlanRingManager *const manager)
{
	memset(manager, 0, sizeof(WlanRingManager));
}

int32_t WlanRingManagerAddRing(WlanRingManager *const manager, RingGroupType type, uint32_t ring_id,
			       const WlanRingInitParams *const params)
{
	WlanRing *ring;

	if (manager == NULL) {
		return -ENODEV;
	}

	if (WlanRingManagerGetRing(manager, type, ring_id, &ring) != 0) {
		return -ENODEV;
	}

	return WlanRingInit(ring, params);
}

int32_t WlanRingManagerRemoveRing(WlanRingManager *const manager, RingGroupType type,
				  uint32_t ring_id)
{
	WlanRing *ring;

	if (WlanRingManagerGetRing(manager, type, ring_id, &ring) != 0) {
		return -ENODEV;
	}

	WlanRingDeinit(ring);

	return 0;
}

int32_t WlanRingManagerActivateRing(WlanRingManager *const manager, RingGroupType type,
				    uint32_t ring_id)
{
	WlanRing *ring;

	if (WlanRingManagerGetRing(manager, type, ring_id, &ring) != 0) {
		return -ENODEV;
	}

	WlanRingActivate(ring);

	return 0;
}

int32_t WlanRingManagerDeactivateRing(WlanRingManager *const manager, RingGroupType type,
				      uint32_t ring_id)
{
	WlanRing *ring;

	if (WlanRingManagerGetRing(manager, type, ring_id, &ring) != 0) {
		return -ENODEV;
	}

	WlanRingDeactivate(ring);

	return 0;
}
