#include "wdev_if_chip_fake_brcm.h"

#include <cstdint>

#include "wdev_if/wdev_if.h"
#include "hwio/hwio.h"
#include "sys_if/io/sys_if_io.h"

constexpr uint32_t kTopOutputRingIsrRegOffset = 0x30;
constexpr uint32_t kTopInputRingDoorbellRegOffset = 0x40;

int32_t WdevChipFakeBrcmInit(WdevIf *const wdev_if)
{
	WdevIrqInfo *irq_info = &wdev_if->irq_info;

	irq_info->num_irq = 1;
	irq_info->info[0].irq_num = INT_FAKE_WIFI_DEV_OUTPUT;
	irq_info->info[0].rx_data_ring_polling_mask = 0x1;
	irq_info->info[0].tx_cpl_ring_polling_mask = 0x1;

	return 0;
};

void WdevChipFakeBrcmDeinit(WdevIf *const /* wdev_if */) {};

void WdevChipFakeBrcmAcknowledgeInterrupt(WdevIf *const /* wdev_if */, int32_t /* irq_id */)
{
	uint32_t ints = 0;

	ints = SysIfIoReadl(
		reinterpret_cast<void *>(FAKE_WIFI_DEV_BASE + kTopOutputRingIsrRegOffset));
	SysIfIoWritel(ints,
		      reinterpret_cast<void *>(FAKE_WIFI_DEV_BASE + kTopOutputRingIsrRegOffset));
};

void WdevChipFakeBrcmRingTxPostDoorbell(WdevIf *const /* wdev_if */, void * /* priv */)
{
	SysIfIoWritel(0xFFFFFFFF, reinterpret_cast<void *>(FAKE_WIFI_DEV_BASE +
							   kTopInputRingDoorbellRegOffset));
};
