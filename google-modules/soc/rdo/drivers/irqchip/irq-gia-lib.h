/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024-2025 Google LLC
 */

#ifndef IRQ_GIA_LIB_H
#define IRQ_GIA_LIB_H

#include <linux/compiler_types.h>
#include <linux/irqdomain.h>

#define IRQS_PER_REG 32

/* Interleaved pattern of multiple registers have one fixed offset, 0x4. */
#define OFFSET_FOR_INTERLEAVED_PATTERN 0x4


/*
 * Anticipation is, GIA will have 1 or 2 concurrent interrupts at any given time. More than that,
 * is a serious situation for performance reasons. More than 4, we don't really need to know, how
 * much more. It's high enough. So, record all instances of 4+ concurrent IRQs in one variable. This
 * will help save some memory as these variables are per-GIA and be part of per GIA structs.
 */
#define MAX_CONCURRENCY		4

/* struct gia_irq_latency - Holds latency stats for per IRQ behind GIA.
 *
 * TODO: This struct will be allocated per incoming line into GIA, irrespective of whether this
 *       line is claimed or not. Converting to list would optimize for size but finding right index
 *       would be linear operation in IRQ context. Need to find memory and time efficient approach.
 *
 * @sw_wait_time* : Time gone waiting for IRQ handler to run inside GIA handler. This could be
 *                  time taken by GIA handler before invoking IRQ handler, plus time spent by other
 *                  IRQ handlers from the same GIA. min/max/avg helps getting full picture of best
 *                  and worst case sw_wait_time.
 * @handler_time* : Time taken by downstream IRQ handlers.
 * @count: Number of time downstream IRQ handler invoked
 * @max_freq: Max number of IRQs occurred in 1 window of time (x ms)
 * @curr_freq: Counter to hold number of IRQs in a given window time
 * @last_window_index: Time window index of last recorded event
 * @irq_kobj: sysfs handle for per IRQ telemetry
 * @hwirq: Index number within given GIA
 */
struct gia_irq_latency {
	u32 sw_wait_time_min;
	u32 sw_wait_time_max;
	u64 sw_wait_time_total;
	u32 handler_time_min;
	u32 handler_time_max;
	u64 handler_time_total;
	u32 count;
	u32 max_freq;
	u32 curr_freq;
	u32 last_window_index;
	struct kobject irq_kobj;
	u32 hwirq;
};

/* struct gia_telemetry - Holds telemetry stats for GIA and its incoming IRQs
 *
 * @concurrency: Array to store concurrency data
 * @irq_latencies: Array to store latency data
 * @start_time: Just a placeholder to store start time of the GIA handler
 * @handler_count: GIA handler invocation count
 * @gia_handler_latency_*: GIA handler latency min/max/count
 */
struct gia_telemetry {
	u32 *concurrency;
	struct gia_irq_latency **irq_latencies;
	ktime_t start_time;
	u32 handler_count;
	u32 gia_handler_latency_min;
	u32 gia_handler_latency_max;
	u64 gia_handler_latency_total;
};

/* struct gia_type_data	- Holds GIA type specific information
 * @status_offset: Offset of status register
 * @status_overflow_offset: Offset of overflow status register
 * @enable_offset: Offset of interrupt enable register
 * @mask_offset: Offset of interrupt mask/unmask register
 * @trigger_offset: Offset of interrupt trigger register for testing
 * @hybrid_offset: Offset of hybrid register. Hybrid register
 *   contains info and determines if the GIA is for handler PE (level aggr) or observer PE (pulse
 *   aggr)
 * @clear_status_reg: Bool stating is status register clearing is required or not. Some level aggr's
 *   status bit simply reflects incoming line's assert/deassert state, and doesn't require explicit
 *   clearing
 * @check_overflow_reg: Bool stating if there is an interrupt overflow register and requires
 *   checking pending interrupts there
 * @wide_gia: Bool stating if the GIA needs multiple 32-bit register to cover all input sources.
 *   Such GIAs give only 1 summary line o/p
 * @non_aggr_gia: Bool stating if the GIA is N:N type and doesn't really aggregates all input
 *   sources into single summary line interrupt
 * @edge_detection: Bool stating if the GIA is edge/pulse sensitive or level sensitive
 */
struct gia_type_data {
	/* GIA register layout */
	int status_offset;
	int status_overflow_offset;
	int enable_offset;
	int mask_offset;
	int trigger_offset;
	int hybrid_offset;

	/* GIA Features */
	bool clear_status_reg;
	bool check_overflow_reg;
	bool wide_gia;
	bool non_aggr_gia;
	bool edge_detection;
};

/*
 * struct gia_device_data - Per GIA device data
 * @pdev: Platform device for the GIA
 * @dev: Device for the GIA
 * @type_data: Pointer to GIA type specific data
 * @irq_domain: IRQ domain for the GIA
 * @power_domain: GenPD handle for the GIA
 * @lock: Spinlock to synchronize register access
 * @mask_cache: Which bits are unmasked by enable_irq for this GIA
 * @wake_cache: Which bits are unmasked by enable_irq_wake for this GIA
 * @io_base: IOREMAP'ed address of the base of GIA
 * @virqs: Pointer to array of virqs claimed by the GIA
 * @nr_out_irq: Size of the virqs
 * @nr_irq_chips: Wide GIAs have more than 1 set of registers (referred here as irq chips)
 * @next_bank_base_offset: In case of wide GIAs, the gap between any register across 2 irq chips
 * @telemetry: Placeholder for storing all telemetry data
 * @list: Node in GIA devices' linked list
 * @gia_kobj: Handle for per GIA sysfs dir
 */
struct gia_device_data {
	struct platform_device *pdev;
	struct device *dev;
	const struct gia_type_data *type_data;
	struct irq_domain *irq_domain;
	raw_spinlock_t lock;
	u32 *mask_cache;
	u32 *wake_cache;
	bool has_wake_irqs;

	/* Values coming from device tree */
	void __iomem *io_base;
	int *virqs;
	int nr_out_irq;
	u32 nr_irq_chips;
	u32 next_bank_base_offset;

	/* Telemetry */
	struct gia_telemetry telemetry;

	struct list_head list;

	/* sysfs handle */
	struct kobject gia_kobj;
};

int gia_set_clear_trigger_reg(struct platform_device *pdev, u32 hwirq, bool set);

int gia_init(struct gia_device_data *gdd);
void gia_exit(struct gia_device_data *gdd);

#endif /* IRQ_GIA_LIB_H */
