/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Google AOC common header
 *
 * Copyright (c) 2026 Google LLC
 */
#ifndef AOC_COMMON_H_
#define AOC_COMMON_H_

#include <linux/cdev.h>
#include <linux/workqueue.h>

#define MAX_SENSOR_POWER_NUM 5
#define MAX_DMIC_POWER_NUM 4
#define AP_RESET_REASON_LENGTH 32
#define MAX_FIRMWARE_LENGTH 128

typedef int (*aoc_map_handler)(u32 handle, phys_addr_t p, size_t size,
				bool mapped, void *ctx);

struct aoc_prvdata {
	struct mbox_slot *mbox_channels;
	struct aoc_service_dev **services;

	unsigned long *read_blocked_mask;
	unsigned long *write_blocked_mask;

	struct work_struct online_work;
	struct workqueue_struct *aoc_service_wq;
	struct resource dram_resource;
	aoc_map_handler map_handler;
	void *map_handler_ctx;

	struct delayed_work monitor_work;
	atomic_t aoc_process_active;

	struct device *dev;
	struct iommu_domain *domain;
	void *ipc_base;

	void *sram_virt;
	void *dram_virt;
	void *lcpm_virt;
	void *aoc_req_virt;
	size_t sram_size;
	size_t dram_size;
	size_t aoc_req_size;

	struct dma_heap *sensor_heap;
	struct dma_heap *audio_playback_heap;
	struct dma_heap *audio_capture_heap;
	struct dma_heap *audio_offload_heap;
	struct dma_heap *contexthub_shared_data_heap;
	struct dma_heap *tpu_offload_heap;

	phys_addr_t sensor_heap_base;
	phys_addr_t audio_playback_heap_base;
	phys_addr_t audio_capture_heap_base;
	phys_addr_t audio_offload_heap_base;
	phys_addr_t contexthub_shared_data_heap_base;
	phys_addr_t tpu_offload_heap_base;
	bool skip_mmap_offload;
	bool mmap_use_kernel_heap;

	int watchdog_irq;
	struct work_struct watchdog_work;
	struct delayed_work watchdog_delayed_work;
	bool first_fw_load;
	bool aoc_reset_done;
	bool ap_triggered_reset;
	bool force_release_aoc;
	atomic_t ssr_requested_flag;
	char ap_reset_reason[AP_RESET_REASON_LENGTH];
	wait_queue_head_t aoc_reset_wait_queue;
	unsigned int acpm_async_id;
	int total_services;
	u8 *service_map;

	char firmware_name[MAX_FIRMWARE_LENGTH];
	char *firmware_version;

	struct cdev cdev;
	dev_t aoc_devt;
	struct class *_class;
	struct device *_device;

	u32 disable_monitor_mode;
	u32 enable_uart_tx;
	u32 force_voltage_nominal;
	u32 no_ap_resets;
	u32 force_speaker_ultrasonic;
	u32 volte_release_mif;

	u32 total_coredumps;
	u32 total_restarts;
	unsigned int iommu_nonsecure_irq;
	unsigned int iommu_secure_irq;

#if IS_ENABLED(CONFIG_EXYNOS_ITMON)
	struct notifier_block itmon_nb;
#endif
	struct device *gsa_dev;
	bool protected_by_gsa;

	int sensor_power_count;
	const char *sensor_power_list[MAX_SENSOR_POWER_NUM];
	struct regulator *sensor_regulator[MAX_SENSOR_POWER_NUM];

	int dmic_power_count;
	const char *dmic_power_list[MAX_DMIC_POWER_NUM];
	struct regulator *dmic_regulator[MAX_DMIC_POWER_NUM];

	int reset_hysteresis_trigger_ms;
	u64 last_reset_time_ns;
	int reset_wait_time_index;

	u32 aoc_clock_divider;
	u32 aoc_mbox_channels;
	u32 aoc_coredump_mbox;
	u32 ramdump_header_offset;
	u64 carveout_paddr_from_aoc;
	u64 carveout_vaddr_from_aoc;

	u16 iommu_size;
	struct iommu_entry *iommu;

	bool aoc_log_mbox_tx_done;

	struct wakeup_source *wakelock;

	u64 dram_arena_debugfs_offset;

	bool boot_breadcrumbs_enabled;
	bool suspended;
	bool print_wakeup_irq;
};

#endif /* AOC_COMMON_H_ */
