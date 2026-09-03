/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Google Whitechapel AoC Core Driver
 *
 * Copyright (c) 2019 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef AOC_H_
#define AOC_H_

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/timer.h>
#include <soc/google/debug-snapshot.h>
#include "aoc_ipc_core.h"
#include "aoc_common.h"

/* TODO: Remove internal calls, or promote to "public" */
#include "aoc_ipc_core_internal.h"

#include "uapi/aoc.h"

#ifdef __KERNEL__

#define AOC_S2MPU_CTRL0 0x0
#define AOC_S2MPU_CTRL_PROTECTION_ENABLE_PER_VID_CLR 0x54
#define AOC_S2MPU_CTRL_PROTECTION_ENABLE_VID_MASK_ALL 0xFF

#define AOC_RESTART_DISABLED_RC (0xD15AB1ED)

#define SENSOR_DIRECT_HEAP_SIZE SZ_4M
#define PLAYBACK_HEAP_SIZE SZ_16K
#define CAPTURE_HEAP_SIZE SZ_64K
#if IS_ENABLED(CONFIG_SOC_GS101) || IS_ENABLED(CONFIG_SOC_GS201) || IS_ENABLED(CONFIG_SOC_ZUMA) || \
	IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
#define CONTEXTHUB_SHARED_DATA_HEAP_SIZE 0
#else
#define CONTEXTHUB_SHARED_DATA_HEAP_SIZE SZ_1M
#endif
#define TPU_OFFLOAD_HEAP_SIZE SZ_128K

/* mmap pcm offload buffer size */
#define OFFLOAD_HEAP_SIZE SZ_8M
#define AOC_MMAP_PLAYBACK_SERVICE         "audio_playback0"
#define AOC_MMAP_OFFLOAD_PLAYBACK_SERVICE "audio_playback6"
#define AOC_MMAP_CAPTURE_SERVICE          "audio_capture1"

#define DT_PROPERTY_NOT_FOUND 0xffffffff

struct aoc_service_dev;
typedef void (*aoc_service_dev_handler)(struct aoc_service_dev *d);

enum AOC_FW_STATE {
	AOC_STATE_OFFLINE,
	AOC_STATE_FIRMWARE_LOADED,
	AOC_STATE_STARTING,
	AOC_STATE_ONLINE,
	AOC_STATE_SSR
};

struct service_work_data {
	struct work_struct service_work;
	int offset;
};

struct mbox_slot {
	struct mbox_client client;
	struct mbox_chan *channel;
	struct service_work_data work;
	void *prvdata;
	int index;
};

struct aoc_service_dev {
	struct device dev;
	wait_queue_head_t read_queue;
	wait_queue_head_t write_queue;

	aoc_service *service;
	void *ipc_base;
	aoc_service_dev_handler handler;
	void *prvdata;
	uint64_t suspend_rx_count;

	uint8_t mbox_index;
	uint8_t phys_index;
	uint8_t service_index;

	bool dead;
	bool wake_capable;

	int irq;
};

struct aoc_module_parameters {
	bool *aoc_autoload_firmware;
	bool *aoc_disable_restart;
	bool *aoc_panic_on_req_timeout;
	bool *aoc_enable_gsa_boot;
	bool *aoc_panic_on_coredump_timeout;
	int *aoc_monitor_online_timeout;
	bool *aoc_panic_on_monitor_timeout;
	bool *aoc_panic_on_ssr_failure;
	int *aoc_ssr_hysteresis_threshold_ms;
	int *aoc_coredump_reset_delay_ms;
};

#define AOC_DEVICE(_d) container_of((_d), struct aoc_service_dev, dev)

phys_addr_t aoc_service_ring_base_phys_addr(struct aoc_service_dev *dev, aoc_direction dir,
					    size_t *out_size);
phys_addr_t aoc_get_heap_base_phys_addr(struct aoc_service_dev *dev, aoc_direction dir,
					    size_t *out_size);
ssize_t aoc_service_read(struct aoc_service_dev *dev, uint8_t *buffer,
			 size_t count, bool block);
ssize_t aoc_service_read_timeout(struct aoc_service_dev *dev, uint8_t *buffer,
				 size_t count, long timeout);
ssize_t aoc_service_write(struct aoc_service_dev *dev, const uint8_t *buffer,
			  size_t count, bool block);
ssize_t aoc_service_write_timeout(struct aoc_service_dev *dev, const uint8_t *buffer,
				  size_t count, long timeout);
int aoc_service_can_read(struct aoc_service_dev *dev);
int aoc_service_can_write(struct aoc_service_dev *dev);
void aoc_service_set_read_blocked(struct aoc_service_dev *dev);
void aoc_service_set_write_blocked(struct aoc_service_dev *dev);
wait_queue_head_t *aoc_service_get_read_queue(struct aoc_service_dev *dev);
wait_queue_head_t *aoc_service_get_write_queue(struct aoc_service_dev *dev);

/*
 * Returns true if data was flushed, false if no data was flushed
 */
bool aoc_service_flush_read_data(struct aoc_service_dev *dev);

bool aoc_online_state(struct aoc_service_dev *dev);

struct aoc_driver {
	struct device_driver drv;

	/* Array of service names to match against.  Last entry must be NULL */
	const char * const *service_names;
	int (*probe)(struct aoc_service_dev *dev);
	int (*remove)(struct aoc_service_dev *dev);
};
#define AOC_DRIVER(_d) container_of((_d), struct aoc_driver, drv)

int aoc_driver_register(struct aoc_driver *driver);
void aoc_driver_unregister(struct aoc_driver *driver);

void aoc_set_map_handler(struct aoc_service_dev *dev, aoc_map_handler handler,
			 void *ctx);
void aoc_remove_map_handler(struct aoc_service_dev *dev);
void aoc_trigger_watchdog(const char *reason);

u32 aoc_chip_get_revision(void);
u32 aoc_chip_get_type(void);
u32 aoc_chip_get_product_id(void);
size_t platform_specific_get_dvfs_freq(char *buff);

bool aoc_release_from_reset(struct aoc_prvdata *prvdata);

void *aoc_sram_translate(u32 offset);
void *aoc_dram_translate(struct aoc_prvdata *p, u32 offset);

void request_aoc_on(struct aoc_prvdata *p, bool status);
int wait_for_aoc_status(struct aoc_prvdata *p, bool status);

int aoc_watchdog_restart(struct aoc_prvdata *prvdata,
	struct aoc_module_parameters *aoc_module_params);

int platform_specific_probe(struct platform_device *pdev, struct aoc_prvdata *prvdata);

void platform_specific_remove(struct platform_device *pdev, struct aoc_prvdata *prvdata);

int start_firmware_load(struct device *dev);

void configure_sensor_power(struct aoc_prvdata *prvdata, bool enable);

void trigger_aoc_ramdump(struct aoc_prvdata *prvdata);

bool aoc_create_dma_buf_heaps(struct aoc_prvdata *prvdata);
void aoc_set_dma_buf_as_ring(struct aoc_prvdata *prvdata);
phys_addr_t aoc_dram_translate_to_aoc(struct aoc_prvdata *p, phys_addr_t addr);

long aoc_unlocked_ioctl_handle_ion_fd(unsigned int cmd, unsigned long arg);

int configure_watchdog_interrupt(struct platform_device *pdev, struct aoc_prvdata *prvdata);

int configure_iommu_interrupts(struct device *dev, struct device_node *iommu_node,
		struct aoc_prvdata *prvdata);

void aoc_configure_ssmt(struct platform_device *pdev);

int aoc_num_services(void);

aoc_service *service_at_index(struct aoc_prvdata *prvdata,
					    unsigned int index);

struct aoc_service_dev *service_dev_at_index(struct aoc_prvdata *prvdata,
							unsigned int index);

bool validate_service(struct aoc_prvdata *prv, int i);

bool aoc_is_valid_dram_address(struct aoc_prvdata *prv, void *addr);

bool aoc_fw_ready(void);

u32 dt_property(struct device_node *node, const char *key);

void configure_crash_interrupts(struct aoc_prvdata *prvdata, bool enable);

void notify_timeout_aoc_status(void);

void trigger_aoc_ssr(bool ap_triggered_reset, char* reset_reason);

int platform_specific_aoc_online(void);

int platform_specific_aoc_offline(void);

void platform_specific_aoc_core_suspend(void);

void platform_specific_aoc_core_resume(void);

u64 aoc_get_timer_ticks(void);

int aoc_read_soc_compatible(struct device *dev, u32 *product_id, u32 *major, u32 *minor);

void schedule_service_work(int channel);

struct aoc_prvdata *get_aoc_prvdata(void);

void aoc_print_core_boot_breadcrumbs(struct aoc_prvdata *prvdata);

void aoc_init_core_boot_breadcrumbs(struct aoc_prvdata *prvdata);

#define AOC_SERVICE_NAME_LENGTH 32

/* Rings should have the ring flag set, slots = 1, size = ring size
 * tx/rx stats for rings are measured in bytes, otherwise msg sends
 */
#define AOC_MAX_ENDPOINTS 200
#define AOC_ENDPOINT_NONE 0xffffffff

/* Offset from the beginning of the DRAM region for the firmware to be stored */
#define AOC_CHARDEV_NAME "aoc"

#define AOC_DOWNCALL_DOORBELL 12

#define AOC_PCU_REVISION_OFFSET 0xF000
#define AOC_PCU_RESET_CONTROL_OFFSET 0x0
#define AOC_PCU_RESET_CONTROL_RESET_VALUE 0x0
#define AOC_PCU_WATCHDOG_CONTROL_OFFSET 0x3000
#define AOC_PCU_WATCHDOG_KEY_OFFSET 0x3004
#define AOC_PCU_WATCHDOG_VALUE_OFFSET 0x3008

#define AOC_PCU_WATCHDOG_KEY_UNLOCK 0xA55AA55A
#define AOC_PCU_WATCHDOG_CONTROL_KEY_ENABLED_MASK 0x4

#define AOC_PARAMETER_MAGIC 0x0a0cda7a

enum AOC_FIRMWARE_INFORMATION {
	kAOCBoardID = 0x1001,
	kAOCBoardRevision = 0x1002,
	kAOCSRAMRepaired = 0x1003,
	kAOCASVTableVersion = 0x1004,
	kAOCCarveoutAddress = 0x1005,
	kAOCCarveoutSize = 0x1006,
	kAOCSensorDirectHeapAddress = 0x1007,
	kAOCSensorDirectHeapSize = 0x1008,
	kAOCForceVNOM = 0x1009,
	kAOCDisableMM = 0x100A,
	kAOCEnableUART = 0x100B,
	kAOCPlaybackHeapAddress = 0x100C,
	kAOCPlaybackHeapSize = 0x100D,
	kAOCCaptureHeapAddress = 0x100E,
	kAOCCaptureHeapSize = 0x100F,
	kAOCForceSpeakerUltrasonic = 0x1010,
	kAOCRandSeed = 0x1011,
	kAOCChipRevision = 0x1012,
	kAOCChipType =  0x1013,
	kAOCGnssType =  0x1014,
	kAOCVolteReleaseMif = 0x1015,
	kAOCChipProductId = 0x1016,
	kAOCWifiChip = 0x1017,
	kAOCBtChip = 0x1018,
	kAOCContexthubSharedDataHeapAddress = 0x1019,
	kAOCContexthubSharedDataHeapSize = 0x101A,
	kAOCTpuOffloadHeapAddress = 0x101B,
	kAOCTpuOffloadHeapSize = 0x101C,
	kAOCGsaEnabled = 0x101D,
};

extern enum AOC_FW_STATE aoc_state;
extern const char * const control_channels[];
extern const int control_channels_size;

#define module_aoc_driver(__aoc_driver)                                        \
	module_driver(__aoc_driver, aoc_driver_register, aoc_driver_unregister)

#endif /* __KERNEL__ */
#endif /* AOC_H_ */
