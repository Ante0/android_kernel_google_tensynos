/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS Declarations of Platform-specific Functions
 *
 * Copyright (c) 2018 Google, LLC
 */

#ifndef LWIS_PLATFORM_H_
#define LWIS_PLATFORM_H_

#include "lwis_device.h"

/*
 *  lwis_platform_probe: handles platform-specific parts of
 *  device init
 */
int lwis_platform_probe(struct lwis_device *lwis_dev);

/*
 *  lwis_platform_unprobe: handles platform-specific parts of
 *  device de-init
 */
int lwis_platform_unprobe(struct lwis_device *lwis_dev);

/*
 *  lwis_platform_device_enable: handles platform-specific parts of
 *  device enable
 */
int lwis_platform_device_enable(struct lwis_device *lwis_dev);

/*
 *  lwis_platform_device_disable: handles platform-specific parts of
 *  device disable
 */
int lwis_platform_device_disable(struct lwis_device *lwis_dev);

/*
 *  lwis_platform_set_device_state: handles platform-specific parts of
 *  dump device states.
 */
void lwis_platform_set_device_state(struct lwis_device *lwis_dev, bool camera_up);

/*
 *  lwis_platform_update_qos: handles platform-specific parts of
 *  updating qos requirements. "value" is in KHz.
 */
int lwis_platform_update_qos(struct lwis_device *lwis_dev, int value, int32_t clock_family);

/*
 *  lwis_platform_remove_qos: handles platform-specific parts of
 *  removing qos requirements.
 */
int lwis_platform_remove_qos(struct lwis_device *lwis_dev);

/*
 *  lwis_platform_update_bts: handles platform-specific parts of
 *  updating bts requirement.
 */
int lwis_platform_update_bts(struct lwis_device *lwis_dev, int block, unsigned int bw_kb_peak,
			     unsigned int bw_kb_read, unsigned int bw_kb_write,
			     unsigned int bw_kb_rt);

/*
 *  lwis_platform_set_default_irq_affinity: handles platform-specific parts of
 *  setting default irq affinity.
 */
int lwis_platform_set_default_irq_affinity(unsigned int irq);

/*
 *  lwis_platform_dpm_update_qos: handles platform-specific parts of
 *  updating qos requirements either qos_family_name or clock_family is valid.
 */
int lwis_platform_dpm_update_qos(struct lwis_device *lwis_dev, struct lwis_device *target_dev,
				 struct lwis_qos_setting *qos_setting);

/*
 *  lwis_platform_get_default_pt_id: Get the default partition id
 */
int lwis_platform_get_default_pt_id(void);

/*
 * lwis_platform_dpm_sync_update_qos: Sync voting constraints across
 * subdevice IPs.
 */
int lwis_platform_dpm_sync_update_qos(struct lwis_device *lwis_dev, int sync_update);

/*
 * lwis_platform_dpm_devfreq_sync_update_qos: Sync voting constraints across
 * subdevice IPs.
 */
int lwis_platform_dpm_devfreq_sync_update_qos(struct lwis_device *lwis_dev,
					      int devfreq_sync_update);

/*
 * lwis_platform_query_irm_register_verify: Verify the irm register set correctly
 */
int lwis_platform_query_irm_register_verify(struct lwis_device *lwis_dev, int sync_update);

/*
 * lwis_platform_query_devfreq_verify
 */
int lwis_platform_query_devfreq_verify(struct lwis_device *lwis_dev, int devfreq_sync_update);
/*
 * lwis_get_sync_update_device_mask: Generates a mask for all devices
 * that need to sync constraints for a bandwidth QOS settings array.
 */
void lwis_get_sync_update_device_mask(struct lwis_device *lwis_dev,
				      struct lwis_qos_setting *qos_setting, int *sync_update);

/*
 * lwis_get_devfreq_sync_update_device_mask: Generates a mask for all devices
 * that need to sync constraints for a closk QOS settings array.
 */
void lwis_get_devfreq_sync_update_device_mask(struct lwis_device *lwis_dev,
					      struct lwis_qos_setting *qos_setting,
					      int *devfreq_sync_update);

/*
 * lwis_platform_refresh_expected_qos_settings: Generates the expected qos settings from user
 * space to irm register. Needed by irm register verification.
 */
void lwis_platform_refresh_expected_qos_settings(struct lwis_device *lwis_dev,
						 struct lwis_qos_setting *qos_setting);

/*
 * lwis_platform_check_qos_box_probed: Need defer probe lwis device probe if qos_box device
 * not probed yet
 */
int lwis_platform_check_qos_box_probed(struct device *dev, const char *lwis_device_name);

/*
 * lwis_platform_update_top_dev: Populate the platform_top_dev info via
 * devices probed before.
 */
int lwis_platform_update_top_dev(struct lwis_device *top_dev, struct lwis_device *lwis_dev,
				 bool *qos_box_probed);

/*
 * lwis_platform_dpm_read_clock: Get lwis_dev clock rate
 */
uint32_t lwis_platform_dpm_read_clock(struct lwis_device *lwis_dev);

/*
 * Checks if batch register IO is supported.
 * True if batch register IO is supported, false otherwise.
 */
bool lwis_platform_is_batch_register_io_supported(void);

/*
 *  lwis_platform_dpm_op_level_get: gets the operating level for the requesting entity.
 */
int lwis_platform_dpm_op_level_get(struct lwis_device *lwis_dev,
				   struct lwis_dpm_op_level *op_level);

#endif /* LWIS_PLATFORM_H_ */
