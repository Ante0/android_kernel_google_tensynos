#ifndef MODULES_WLAN_RING_WLAN_RING_H
#define MODULES_WLAN_RING_WLAN_RING_H

#include <common/wlan/noa_wlan.h>
#include "sys_if/io/sys_if_io.h"
#include "sys_if/common.h"
#include "sys_if/types/types.h"
#include "sys_if/memory/sys_if_memory.h"
#include "wlan_cast.h"

/// @brief Structure representing a WLAN ring.
typedef struct noa_hw_ring WlanRing;

/// @brief Structure containing initialization parameters for a WLAN ring.
typedef struct noa_hw_ring WlanRingInitParams;

/// @brief Flags for WLAN ring.
typedef enum WlanRingFlag {
	kWlanRingFlagActive = (1U << 0),
} WlanRingFlag;

/// @brief @brief Calculates the number of elements available for writing in
/// the ring.
///
/// @param[in] r The read index of the ring.
/// @param[in] w The write index of the ring.
/// @param[in] len The length of the ring.
/// @return The number of elements available for writing.
static inline uint16_t CalculateWriteCount(uint16_t r, uint16_t w, uint16_t len)
{
	return (w < r) ? (r - w - 1) : (len - w - 1 + r);
}

/// @brief Calculates the number of elements available for reading from the
/// ring.
///
/// @param[in] r The read index of the ring.
/// @param[in] w The write index of the ring.
/// @param[in] len The length of the ring.
/// @return The number of elements available for reading.
static inline uint16_t CalculateReadCount(uint16_t r, uint16_t w, uint16_t len)
{
	return (w < r) ? (len - r + w) : (w - r);
}

/// @brief Check the address within the coherent region or not.
///
/// @param[addr] The address intend to check.
/// @return true: coherent region, false: noncoherent region.
static inline bool IsCoherentCheck(PhyAddr addr)
{
	return (addr < NONCACHE_IOVA_START || addr > NONCACHE_IOVA_END) ? true : false;
}

/// @brief Gets the number of elements available for writing in the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
/// @return The number of elements available for writing.
static inline uint16_t WlanRingGetWriteCount(WlanRing *const ring)
{
	uint16_t cnt = 0;

	if (ring->regs.read == 0 || ring->regs.write == 0) {
		return 0;
	}

	if (IsCoherentCheck(WLAN_STATIC_CAST(PhyAddr, ring->regs.read))) {
		SysIfInvalidDCache(WLAN_STATIC_CAST(PhyAddr, ring->regs.read), sizeof(uint16_t));
	}
	if (IsCoherentCheck(WLAN_STATIC_CAST(PhyAddr, ring->regs.write))) {
		SysIfInvalidDCache(WLAN_STATIC_CAST(PhyAddr, ring->regs.write), sizeof(uint16_t));
	}
	ring->read = SysIfIoReadw(WLAN_REINTERPRET_CAST(void *, ring->regs.read)) / ring->stride;
	ring->write = SysIfIoReadw(WLAN_REINTERPRET_CAST(void *, ring->regs.write)) / ring->stride;
	cnt = CalculateWriteCount(ring->read, ring->write, ring->ndesc);
	return cnt > ring->ndesc ? 0 : cnt;
}

/// @brief Gets the number of elements available for reading from the WLAN
/// ring.
///
/// @param[in] ring A pointer to the WLAN ring.
/// @return The number of elements available for reading.
static inline uint16_t WlanRingGetReadCount(WlanRing *const ring)
{
	uint16_t cnt = 0;

	if (ring->regs.read == 0 || ring->regs.write == 0) {
		return 0;
	}

	if (IsCoherentCheck(WLAN_STATIC_CAST(PhyAddr, ring->regs.read))) {
		SysIfInvalidDCache(WLAN_STATIC_CAST(PhyAddr, ring->regs.read), sizeof(uint16_t));
	}
	if (IsCoherentCheck(WLAN_STATIC_CAST(PhyAddr, ring->regs.write))) {
		SysIfInvalidDCache(WLAN_STATIC_CAST(PhyAddr, ring->regs.write), sizeof(uint16_t));
	}
	ring->read = SysIfIoReadw(WLAN_REINTERPRET_CAST(void *, ring->regs.read)) / ring->stride;
	ring->write = SysIfIoReadw(WLAN_REINTERPRET_CAST(void *, ring->regs.write)) / ring->stride;
	cnt = CalculateReadCount(ring->read, ring->write, ring->ndesc);
	return cnt > ring->ndesc ? 0 : cnt;
}

/// @brief Updates the software write index of the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
static inline void WlanRingUpdateSwWrite(WlanRing *const ring)
{
	ring->write = (ring->write + 1) % ring->ndesc;
}

/// @brief Updates the software read index of the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
static inline void WlanRingUpdateSwRead(WlanRing *const ring)
{
	ring->read = (ring->read + 1) % ring->ndesc;
}

/// @brief Gets the base address of the write buffer in the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
/// @return The base address of the write buffer.
static inline uint8_t *WlanRingGetWriteBase(const WlanRing *const ring)
{
	return WLAN_REINTERPRET_CAST(uint8_t *, WLAN_REINTERPRET_CAST(uintptr_t, ring->desc) +
							ring->write * ring->desc_sz);
}

/// @brief Gets the base address of the read buffer in the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
/// @return The base address of the read buffer.
static inline uint8_t *WlanRingGetReadBase(const WlanRing *const ring)
{
	return WLAN_REINTERPRET_CAST(uint8_t *, WLAN_REINTERPRET_CAST(uintptr_t, ring->desc) +
							ring->read * ring->desc_sz);
}

/// @brief Updates the hardware write index of the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
static inline void WlanRingUpdateHwWrite(const WlanRing *const ring)
{
	if (ring->regs.write) {
		if (IsCoherentCheck(WLAN_STATIC_CAST(PhyAddr, ring->regs.write))) {
			SysIfInvalidDCache(WLAN_STATIC_CAST(
				PhyAddr, ring->regs.write), sizeof(uint16_t));
		}

		//SysIfIoWritew(ring->write * ring->stride,
		//	      WLAN_REINTERPRET_CAST(void *, ring->regs.write));
		SysIfIoWritel(ring->write * ring->stride,
			      WLAN_REINTERPRET_CAST(void *, ring->regs.write));
		if (IsCoherentCheck(WLAN_STATIC_CAST(PhyAddr, ring->regs.write))) {
			SysIfFlushDCache(WLAN_STATIC_CAST(
				PhyAddr, ring->regs.write), sizeof(uint16_t));
		}
	}
}

/// @brief Updates the hardware read index of the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
static inline void WlanRingUpdateHwRead(const WlanRing *const ring)
{
	if (ring->regs.read) {
		if (IsCoherentCheck(WLAN_STATIC_CAST(PhyAddr, ring->regs.read))) {
			SysIfInvalidDCache(WLAN_STATIC_CAST(
				PhyAddr, ring->regs.read), sizeof(uint16_t));
		}
		//SysIfIoWritew(ring->read * ring->stride,
		//	      WLAN_REINTERPRET_CAST(void *, ring->regs.read));
		SysIfIoWritel(ring->read * ring->stride,
			      WLAN_REINTERPRET_CAST(void *, ring->regs.read));
		if (IsCoherentCheck(WLAN_STATIC_CAST(PhyAddr, ring->regs.read))) {
			SysIfFlushDCache(WLAN_STATIC_CAST(
				PhyAddr, ring->regs.read), sizeof(uint16_t));
		}
	}
}

/// @brief Activates a WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring to activate.
static inline void WlanRingActivate(WlanRing *const ring)
{
	ring->flags |= kWlanRingFlagActive;
}

/// @brief Deactivates a WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring to deactivate.
static inline void WlanRingDeactivate(WlanRing *const ring)
{
	ring->flags &= ~kWlanRingFlagActive;
}

/// @brief Checks if a WLAN ring is active.
///
/// @param[in] ring A pointer to the WLAN ring to check.
/// @return true if the ring buffer is active, false otherwise.
static inline bool WlanRingIsActive(const WlanRing *const ring)
{
	return (ring->flags & kWlanRingFlagActive) > 0;
}

/// @brief Initializes the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
/// @param[in] params A pointer to the initialization parameters.
/// @return 0 on success, an error code otherwise.
extern int32_t WlanRingInit(WlanRing *const ring, const WlanRingInitParams *const params);

/// @brief Deinitializes the WLAN ring.
///
/// @param[in] ring A pointer to the WLAN ring.
extern void WlanRingDeinit(WlanRing *const ring);

#endif /* MODULES_WLAN_RING_WLAN_RING_H */
