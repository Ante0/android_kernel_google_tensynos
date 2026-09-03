// NOLINTBEGIN
#ifndef __WLAN_SVC_WLAN_RING_WLAN_HOST_SIM_RING_ADAPATEE_H__
#define __WLAN_SVC_WLAN_RING_WLAN_HOST_SIM_RING_ADAPATEE_H__

#include <errno.h>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "pw_sync/mutex.h"

#include "linux_port/log.h"

namespace noa::service::wlan_service
{

constexpr uint32_t kDescNum = 3;
constexpr uint32_t kDescSize = 128;

class WlanHostSimRing {
    public:
	~WlanHostSimRing() = default;

	WlanHostSimRing() : read_(0), write_(0), ndesc_(kDescNum), desc_sz_(kDescSize)
	{
	}

	int Init()
	{
		read_ = 0;
		write_ = 0;
		ndesc_ = kDescNum;
		desc_sz_ = kDescSize;

		return 0;
	}

	int Write(void *data, size_t len)
	{
		if (GetWriteCount()) {
			void *addr = reinterpret_cast<void *>(&buffer_[write_ * desc_sz_]);
			std::memcpy(addr, data, len);
			write_++;
			write_ %= ndesc_;
		} else {
			return -EAGAIN;
		}

		return 0;
	}

	int Read(void *data, size_t)
	{
		if (!data)
			return 0;

		if (GetReadCount()) {
			*(void **)data = reinterpret_cast<void *>(&buffer_[read_ * desc_sz_]);
			read_++;
			read_ %= ndesc_;
		} else {
			return 0;
		}

		return desc_sz_;
	}

	uint32_t GetWriteIdx()
	{
		return write_;
	}

	uint32_t GetWriteCount()
	{
		if (!ndesc_)
			return 0;

		uint32_t cnt =
			(write_ < read_) ? (read_ - write_ - 1) : (ndesc_ - write_ - 1 + read_);
		return cnt > ndesc_ ? 0 : cnt;
	}

	uint32_t GetReadCount()
	{
		if (!ndesc_)
			return 0;

		uint32_t cnt = (write_ < read_) ? (ndesc_ - read_ + write_) : (write_ - read_);
		return cnt > ndesc_ ? 0 : cnt;
	}

    private:
	char buffer_[kDescNum * kDescSize];
	uint32_t read_;
	uint32_t write_;
	uint32_t ndesc_;
	uint32_t desc_sz_;
};

} // namespace noa::service::wlan_service

extern const struct wlan_ring_adaptee_ops *wlan_host_sim_ring_adaptee_get_tx_ops();
extern const struct wlan_ring_adaptee_ops *wlan_host_sim_ring_adaptee_get_rx_ops();

#endif /* __WLAN_SVC_WLAN_RING_WLAN_HOST_SIM_RING_ADAPATEE_H__ */
// NOLINTEND
