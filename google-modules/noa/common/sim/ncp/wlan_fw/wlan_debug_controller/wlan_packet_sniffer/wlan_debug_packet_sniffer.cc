#include "wlan_debug_packet_sniffer.h"

#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "sys_if/log/sys_if_log.h"
#include "common/compiler.h"
#include "sys_if/memory/sys_if_memory.h"
#include "sys_if/mailbox/sys_if_mailbox.h"
#include "ext_svc/ext_svc.h"
#include "wlan_debug_controller/wlan_debug_common.h"
#include "wlan_service_rpc_protocol.h"
#include "wlan_log/wlan_log.h"
#include "wlan_cast.h"

// Temporarily used until the DRAM carveout is defined.
static const char *wlan_packet_sniffer_ring_names[kWlanPacketSnifferRingTypeNum] = {
	"NOA_HW_TX_RING", "NOA_HW_RX_RING", "NEP_TX_RING", "NEP_RX_RING"
};

typedef struct WlanPacketSnifferCtrl {
	bool enable;
	bool enable_desc;
	uint16_t capture_size;
	uint16_t curr_entry_idx;
	enum Location location;
	enum PacketRunoutAction behavior;
	unsigned char *write_pointer;
	unsigned char *entry_header_table;
	uint64_t wlan_packet_sniffer_dram_buffer;
	uint32_t wlan_packet_sniffer_dram_size;
	uint32_t packet_sequence;
	ExternalServices *ext_svc;
	uint8_t notified;
	spinlock_t lock;
} WlanPacketSnifferCtrl;

SEC_FAST_DATA static WlanPacketSnifferCtrl control;

void WlanPacketSnifferInit(ExternalServices *ext_svc, uint64_t dram_addr, uint32_t dram_size)
{
	control.enable = false;
	control.enable_desc = false;
	control.curr_entry_idx = 0;
	control.capture_size = DEFAULT_PACKET_CAPTURE_SIZE;
	control.location = kUart;
	control.behavior = kOverwrite;
	control.entry_header_table = WLAN_REINTERPRET_CAST(unsigned char *, dram_addr);
	control.write_pointer = control.entry_header_table + PACKET_SNIFFER_BUFFER_OFFSET;
	control.wlan_packet_sniffer_dram_buffer = dram_addr;
	control.wlan_packet_sniffer_dram_size = dram_size;
	control.packet_sequence = 1;
	control.ext_svc = ext_svc;
	spin_lock_init(&control.lock);
}

void WlanPacketSnifferSetRunoutAction(enum PacketRunoutAction action)
{
	if (kUndefinedPacketRunoutAction < action && action < kPacketRunoutActionNum) {
		control.behavior = action;
	}
}

void WlanPackerSnifferSetLocation(enum Location location)
{
	if (kUndefinedLocation < location && location < kLocationNum) {
		control.location = location;
	}
}

void WlanPacketSnifferSetCaptureSize(uint16_t capture_size)
{
	if (capture_size < MAX_PACKET_SIZE) {
		control.capture_size = capture_size;
	} else {
		control.capture_size = MAX_PACKET_SIZE;
	}
}

void WlanPacketSnifferSwitch(bool enable)
{
	control.enable = enable;
}

void WlanPacketSnifferSwitchDescDump(bool enable_desc)
{
	control.enable_desc = enable_desc;
}

static bool WlanPacketSnifferCheckOverflowType(uint16_t cap_size)
{
	uint64_t wlan_packet_sniffer_dram_buffer = control.wlan_packet_sniffer_dram_buffer;

	if (control.curr_entry_idx + 1 >= MAX_PACKET_ENTRY_NUM ||
	    control.write_pointer + cap_size >
		    WLAN_REINTERPRET_CAST(unsigned char *, wlan_packet_sniffer_dram_buffer) +
			    control.wlan_packet_sniffer_dram_size) {
		return true;
	}

	return false;
}

static void WlanPacketSnifferDumpToDram(uint64_t pkt_addr, uint16_t cap_size,
					enum WlanPacketSnifferRingType ring_type)
{
	int32_t packet_offset;
	uint8_t ring_type_fixed_len = WLAN_STATIC_CAST(uint8_t, ring_type);
	uint64_t wlan_packet_sniffer_dram_buffer = control.wlan_packet_sniffer_dram_buffer;
	SnifferEntryHeader *sniffer_header = WLAN_REINTERPRET_CAST(
		SnifferEntryHeader *,
		control.entry_header_table + control.curr_entry_idx * sizeof(SnifferEntryHeader));

	sniffer_header->valid = false;
	sniffer_header->ring_type = ring_type_fixed_len;
	sniffer_header->pkt_len = cap_size;
	sniffer_header->pkt_seq = control.packet_sequence;
	sniffer_header->pkt_offset = 0;

	spin_lock(&control.lock);
	SysIfLogTimestamp(TIMESTAMP_MAX_SIZE, sniffer_header->timestamp);

	// Check for buffer overflow
	if (WlanPacketSnifferCheckOverflowType(cap_size)) {
		if (control.behavior == kOverwrite) {
			// Reset to beginning
			control.curr_entry_idx = 0;
			control.write_pointer = WLAN_REINTERPRET_CAST(
				unsigned char *,
				wlan_packet_sniffer_dram_buffer + PACKET_SNIFFER_BUFFER_OFFSET);
		} else if (control.behavior == kNotifyDriver) {
			if (SysIfIsDriverMode()) {
				ExtSvcSendEventToApc(control.ext_svc,
						     kWlanEventTypePacketSnifferFull, NULL, 0);
			} else {
				SysIfNotifyMailbox(kNcpWifiMailboxTypeAp, kPacketSnifferFullEvent);
			}
			spin_unlock(&control.lock);
			return;
		} else {
			spin_unlock(&control.lock);
			return; // Discard if not overwriting
		}
	}

	packet_offset =
		control.write_pointer -
		WLAN_REINTERPRET_CAST(unsigned char *, wlan_packet_sniffer_dram_buffer +
							       PACKET_SNIFFER_BUFFER_OFFSET);
	sniffer_header->pkt_offset = WLAN_STATIC_CAST(uint32_t, packet_offset);

	memcpy(WLAN_STATIC_CAST(void *, control.write_pointer),
	       WLAN_REINTERPRET_CAST(const void *, pkt_addr), cap_size);
	sniffer_header->valid = true;
	control.write_pointer += cap_size;
	control.curr_entry_idx++;

	SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, sniffer_header),
			 sizeof(SnifferEntryHeader));
	SysIfFlushDCache(WLAN_REINTERPRET_CAST(PhyAddr, control.write_pointer - cap_size),
			 cap_size);
	spin_unlock(&control.lock);
}

void WlanPacketSniff(uint64_t pkt_addr, uint32_t pkt_len, void *desc, void (*dump_desc)(void *desc),
		     enum WlanPacketSnifferRingType ring_type)
{
	uint16_t cap_size = control.capture_size > pkt_len ? pkt_len : control.capture_size;

	if (control.enable_desc && dump_desc != NULL && desc != NULL) {
		dump_desc(desc);
	}

	if (control.enable) {
		if (control.location == kUart || control.location == kDramAndUart) {
			WlanPacketSnifferHexDump(pkt_addr, cap_size, ring_type);
		}

		if ((control.location == kDram || control.location == kDramAndUart) &&
		    control.write_pointer != NULL) {
			WlanPacketSnifferDumpToDram(pkt_addr, cap_size, ring_type);
		}
	}
	control.packet_sequence++;
}

const char *WlanPacketSnifferGetRingType(enum WlanPacketSnifferRingType ring_type)
{
	if (ring_type >= kWlanPacketSnifferRingTypeNum) {
		WLAN_LOG_ERROR(Stats, "Invalid ring type: %" PRId32 "\n", ring_type);
		return "Invalid Ring Type";
	}
	return wlan_packet_sniffer_ring_names[ring_type];
}

enum Location WlanPacketSnifferGetLocation(void)
{
	return control.location;
}

uint64_t WlanPacketSnifferGetDramAddr(void)
{
	return control.wlan_packet_sniffer_dram_buffer;
}

uint16_t WlanPacketSnifferGetCaptureSize(void)
{
	return control.capture_size;
}

uint32_t WlanPacketSnifferGetDramSize(void)
{
	return control.wlan_packet_sniffer_dram_size;
}

void WlanPacketSnifferHexDump(uint64_t pkt_addr, uint32_t pkt_len,
			      enum WlanPacketSnifferRingType ring_type)
{
	WLAN_LOG_INFO(Stats, "%s|Packet Size: %hu\n", WlanPacketSnifferGetRingType(ring_type),
		      pkt_len);
	SYS_IF_HEX_DUMP(WlanPacketSnifferGetRingType(ring_type), pkt_addr, pkt_len);
}

void WlanPacketSnifferReset(void)
{
	control.notified = 0;
	control.curr_entry_idx = 0;
	control.write_pointer =
		WLAN_REINTERPRET_CAST(unsigned char *, control.wlan_packet_sniffer_dram_buffer +
							       PACKET_SNIFFER_BUFFER_OFFSET);

	memset(control.entry_header_table, 0, MAX_PACKET_ENTRY_NUM * sizeof(SnifferEntryHeader));
}