// NOLINTBEGIN
#ifndef __WLAN_SERVICE_WLAN_RING_WLAN_RING_ADAPATER_H__
#define __WLAN_SERVICE_WLAN_RING_WLAN_RING_ADAPATER_H__

#include "../../wlan_service_system.h"
#include "wlan_ring_wrapper.h"

enum {
	ADAPTEE_TYPE_UNDEFINED = 0,
	ADAPTEE_TYPE_NEP_TX_RING,
	ADAPTEE_TYPE_NEP_RX_RING,
	ADAPTEE_TYPE_NOA_HW_TX_RING,
	ADAPTEE_TYPE_NOA_HW_RX_RING,
	ADAPTEE_TYPE_HOST_SIM_TX_RING,
	ADAPTEE_TYPE_HOST_SIM_RX_RING,
	ADAPTEE_TYPE_MAX,
};

struct wlan_ring_adaptee_ops {
	int (*begin_processing)(struct wlan_ring_wrapper *const adaptee);
	int (*complete_processing)(struct wlan_ring_wrapper *const adaptee);
	int (*read)(struct wlan_ring_wrapper *const adaptee, void *const data, size_t len);
	int (*write)(struct wlan_ring_wrapper *const daptee, void *const data, size_t len);
	int (*tail_inc)(struct wlan_ring_wrapper *const adaptee);
	int (*tail_rollback)(struct wlan_ring_wrapper *const adaptee);
	bool (*is_empty)(struct wlan_ring_wrapper *const adaptee);
	int (*init)(struct wlan_ring_wrapper *const adaptee, const void *const params);
	void (*exit)(struct wlan_ring_wrapper *const adaptee);
	int (*activate)(struct wlan_ring_wrapper *const adaptee, bool active);
	const char *(*get_name)(struct wlan_ring_wrapper *const adaptee);
	bool (*is_active)(struct wlan_ring_wrapper *const adaptee);
	uint32_t (*get_hw_idx)(struct wlan_ring_wrapper *const adaptee);
	uint32_t (*get_write_idx)(struct wlan_ring_wrapper *const adaptee);
	uint32_t (*get_sn)(struct wlan_ring_wrapper *const adaptee);
	void (*sn_inc)(struct wlan_ring_wrapper *const adaptee, uint32_t divisor);
};

struct wlan_ring_adapter {
	const struct wlan_ring_adaptee_ops *adaptee_ops;
	struct wlan_ring_wrapper adaptee;
#if defined(__cplusplus)
	wlan_ring_adapter()
	{
	}

	~wlan_ring_adapter()
	{
	}
#endif /* __cplusplus */
};

static inline int wlan_ring_begin_processing(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->begin_processing) {
		return self->adaptee_ops->begin_processing(&self->adaptee);
	}

	return -EINVAL;
}

static inline int wlan_ring_complete_processing(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->complete_processing) {
		return self->adaptee_ops->complete_processing(&self->adaptee);
	}

	return -EINVAL;
}

static inline int wlan_ring_read(struct wlan_ring_adapter *const self, void *data, size_t len)
{
	if (self->adaptee_ops && self->adaptee_ops->read) {
		return self->adaptee_ops->read(&self->adaptee, data, len);
	}

	return -EINVAL;
}

static inline int wlan_ring_write(struct wlan_ring_adapter *const self, void *data, size_t len)
{
	if (self->adaptee_ops && self->adaptee_ops->write) {
		return self->adaptee_ops->write(&self->adaptee, data, len);
	}

	return -EINVAL;
}

static inline int wlan_ring_tail_inc(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->tail_inc) {
		return self->adaptee_ops->tail_inc(&self->adaptee);
	}

	return -EINVAL;
}

static inline int wlan_ring_tail_rollback(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->tail_rollback) {
		return self->adaptee_ops->tail_rollback(&self->adaptee);
	}

	return -EINVAL;
}

static inline bool wlan_ring_is_empty(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->is_empty) {
		return self->adaptee_ops->is_empty(&self->adaptee);
	}

	return true;
}

static inline int wlan_ring_init(struct wlan_ring_adapter *const self, const void *params)
{
	spin_lock_init(&self->adaptee.ext.lock);

	if (self->adaptee_ops && self->adaptee_ops->init) {
		return self->adaptee_ops->init(&self->adaptee, params);
	}

	return -EINVAL;
}

static inline void wlan_ring_exit(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->exit) {
		return self->adaptee_ops->exit(&self->adaptee);
	}
}

static inline int wlan_ring_activate(struct wlan_ring_adapter *const self, bool active)
{
	if (self->adaptee_ops && self->adaptee_ops->activate) {
		return self->adaptee_ops->activate(&self->adaptee, active);
	}

	return -EINVAL;
}

static inline const char *wlan_ring_get_name(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->get_name) {
		return self->adaptee_ops->get_name(&self->adaptee);
	}

	return "N/A";
}

static inline bool wlan_ring_is_active(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->is_active) {
		return self->adaptee_ops->is_active(&self->adaptee);
	}

	return false;
}

static inline uint32_t wlan_ring_get_hw_idx(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->get_hw_idx) {
		return self->adaptee_ops->get_hw_idx(&self->adaptee);
	}

	return 0;
}

static inline uint32_t wlan_ring_get_write_idx(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->get_write_idx) {
		return self->adaptee_ops->get_write_idx(&self->adaptee);
	}

	return 0;
}

static inline uint32_t wlan_ring_get_sn(struct wlan_ring_adapter *const self)
{
	if (self->adaptee_ops && self->adaptee_ops->get_sn) {
		return self->adaptee_ops->get_sn(&self->adaptee);
	}

	return 0;
}

static inline void wlan_ring_sn_inc(struct wlan_ring_adapter *const self, uint32_t divisor)
{
	if (self->adaptee_ops && self->adaptee_ops->sn_inc) {
		return self->adaptee_ops->sn_inc(&self->adaptee, divisor);
	}
}

extern int wlan_ring_adapter_initialize(struct wlan_ring_adapter *const self, uint32_t ring_type);

#endif /* __WLAN_SERVICE_WLAN_RING_WLAN_RING_ADAPATER_H__ */
// NOLINTEND
