/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Dynamic data path switching for the NOA WiFi Driver.
 *
 * Copyright 2025 Google LLC.
 *
 */
#ifndef __NOA_WLAN_DYNAMIC_SWITCH_H__
#define __NOA_WLAN_DYNAMIC_SWITCH_H__

enum noa_wlan_data_path_mode {
	NOA_WLAN_DATA_PATH_MODE_START = 0,
	NOA_WLAN_DATA_PATH_BYPASS_MODE = NOA_WLAN_DATA_PATH_MODE_START,
	NOA_WLAN_DATA_PATH_OFFLOAD_MODE,
	NOA_WLAN_DATA_PATH_MODE_END,
	NOA_WLAN_DATA_PATH_MODE_NUM = NOA_WLAN_DATA_PATH_MODE_END,
};

enum noa_wlan_dynamic_switch_event {
	NOA_WLAN_SERVICE_PRE_SWITCH,
	NOA_WLAN_SERVICE_POST_SWITCH,
	NOA_WLAN_DEVICE_PRE_SWITCH,
	NOA_WLAN_DEVICE_POST_SWITCH,
};

enum noa_wlan_dynamic_switch_state {
	NOA_WLAN_DATA_PATH_OPERATING,
	NOA_WLAN_SERVICE_PRE_SWITCHED,
	NOA_WLAN_SERVICE_POST_SWITCHED,
	NOA_WLAN_DEVICE_PRE_SWITCHED,
	NOA_WLAN_DEVICE_POST_SWITCHED,
};

struct noa_wlan_switch_manager {
	void *vendor_plat_ops;
	void *dpa_plat_ops;
	void **current_plat_ops;
	void *vendor_bus;
	void *vendor_dev;
	void *noa_wlan_client;
	enum noa_wlan_data_path_mode dpa_ssr_policy;
	enum noa_wlan_dynamic_switch_state state;
	enum noa_wlan_data_path_mode dp_mode;
};

struct noa_wlan_dynamic_switch_init_params {
	void *vendor_plat_ops;
	void *dpa_plat_ops;
	void **current_plat_ops;
	void *vendor_bus;
	void *vendor_dev;
	enum noa_wlan_data_path_mode dp_mode;
};

static inline enum noa_wlan_data_path_mode
get_noa_wlan_dp_mode(const struct noa_wlan_switch_manager *manager)
{
	return manager->dp_mode;
}

static inline const char *get_data_path_mode_name(enum noa_wlan_data_path_mode mode)
{
#define CASE_MODE_NAME(mode)                                                                       \
	case NOA_WLAN_DATA_PATH_##mode:                                                            \
		return #mode
	switch (mode) {
		CASE_MODE_NAME(BYPASS_MODE);
		CASE_MODE_NAME(OFFLOAD_MODE);
	default:
		break;
	}

	return "Unknown";
}

/**
 * @brief Initializes the noa_wlan dynamic switch manager.
 *
 * @param[in] manager A pointer to the `struct noa_wlan_switch_manager`
 * to initialize.
 * @param[in] params A pointer to the `struct noa_wlan_dynamic_switch_init_params`
 * containing initialization parameters.
 * @return 0 on successful initialization, or a negative error code on failure.
 */
extern int noa_wlan_dynamic_switch_init(struct noa_wlan_switch_manager *manager,
					struct noa_wlan_dynamic_switch_init_params *params);

/**
 * @brief Deinitializes the noa_wlan dynamic switch manager.
 *
 * @param[in] manager A pointer to the `struct noa_wlan_switch_manager`
 * to deinitialize.
 */
extern void noa_wlan_dynamic_switch_deinit(struct noa_wlan_switch_manager *manager);

/**
 * @brief Handles events related to the noa_wlan dynamic switch.
 *
 * @param[in] manager A pointer to the `struct noa_wlan_switch_manager`
 * that the event pertains to.
 * @param[in] event An `enum noa_wlan_dynamic_switch_event` indicating the
 * type of event.
 * @return An integer indicating the success or failure of handling the event.
 */
extern int noa_wlan_dynamic_switch_event_handler(struct noa_wlan_switch_manager *manager,
						 enum noa_wlan_dynamic_switch_event event);
/**
 * @brief Pre-reset the DPA with DPA platform operations
 *
 * If the NOA WiFi was not enabled, then it does not need any operation here.
 * Otherwise, the NOA WiFi should be stopped and exit.
 *
 * @param[in] manager A pointer to the `struct noa_wlan_switch_manager`
 * that the event pertains to.
 */
extern void noa_wlan_pre_reset_dpa(struct noa_wlan_switch_manager *manager);

/**
 * @brief Post-reset the DPA with vendor platform operations
 *
 * If the NOA WiFi was not enabled, then it does not need any operation here.
 * However, if the NOA WiFi is enabled and encountered crashes, the NOA should
 * still not be re-enabled. Instead, the vendor platform should be re-initialized
 * and started so that to prevent from continuous trap.
 *
 * @param[in] manager A pointer to the `struct noa_wlan_switch_manager`
 * that the event pertains to.
 */
extern void noa_wlan_post_reset_dpa(struct noa_wlan_switch_manager *manager);

#endif // __NOA_WLAN_DYNAMIC_SWITCH_H__
