/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022 Google LLC
 */
#ifndef _PINCTRL_GOOGLE_COMMON_H_
#define _PINCTRL_GOOGLE_COMMON_H_

#include <linux/interrupt.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>

struct pinctrl_pin_desc;
struct google_pinctrl_irqinfo;

enum PIN_TYPE_ENUM {
	PIN_TYPE_STANDARD = 0,
	PIN_DRV_3_BITS_WITH_SLEW,
	PIN_DRV_4_BITS_NO_SLEW,
	PIN_TYPE_RESERVED,	// SW should not touch these pins
	PIN_TYPE_MAX
};

enum REG_TYPE_ENUM {
	MAIN_CONTROLLER_REGS = 0,
	SECONDARY_CONTROLLER_REGS,
	REG_TYPE_MAX
};

/* GPIO registers start offset */
#define GPIO_REGS_OFFSET			0x08

#define GPIO_FUNC_BIT_POS			0
#define GPIO_FUNC_BIT_POS_FOR_SHARED_PAD	6

#define RPM_STATS_SIZE				10

enum google_pinctrl_register_idx {
	PARAM_ID,
	DMUX_ID,
	TXDATA_ID,
	RXGATE_ID,
	RXDATA_ID,
	ISR_ID,
	ISROVF_ID,
	IER_ID,
	IMR_ID,
	ITR_ID,
	PIP_ID,
	FIQTS_ID,
	REGS_NUM,
};

/* Members in layout should be in the same order as members of google_pinctrl_register_idx enum */
struct reg_layout {
	u16 param : 1;
	u16 dmux : 1;
	u16 txdata : 1;
	u16 rxgate : 1;
	u16 rxdata : 1;
	u16 isr : 1;
	u16 isrovf : 1;
	u16 ier : 1;
	u16 imr : 1;
	u16 itr : 1;
	u16 pip: 1;
	u16 fiqts: 1;
	u16 reserved : 4;
};

struct google_pinctrl_registers_flags {
	u8 pin_id;
	union {
		struct reg_layout layout;
		u16 val;
	};
};

struct param_t {
	union {
		struct {
			/* Bitfields for Type-A pins */
			u32 drv : 3;
			u32 ie : 1;
			u32 rxmode : 1;
			u32 slew : 1;
			u32 debounce_sel : 1;
			u32 reserved : 25;
		} type_a;
		struct {
			/* Bitfields for Type-B pins */
			u32 drv : 4;
			u32 ie : 1;
			u32 rxmode : 1;
			u32 debounce_sel : 1;
			u32 reserved : 24;
			u32 slew : 1;	/* Un-used but for compile safety */
		} type_b;
		struct {
			/* Bitfields for standard pins */
			u32 drv : 4;
			u32 reserved_4_7 : 4;
			u32 ie : 1;
			u32 reserved_9_11 : 3;
			u32 rxmode : 1;
			u32 reserved_13_15 : 3;
			u32 slew : 1;
			u32 reserved_17_19 : 3;
			u32 debounce_sel : 1;
			u32 reserved_21_31 : 11;
		} standard;
		u32 val;
	};
};

static inline u32 max_drv_per_pin_type(unsigned long param_format_type)
{
	switch (param_format_type) {
	case PIN_DRV_3_BITS_WITH_SLEW:
		return 7;
	case PIN_DRV_4_BITS_NO_SLEW:
		return 15;
	default:
		return 15;
	}
}

#define SET_PARAM_FIELD(params, pin_type, field, value) \
	do { \
		switch (pin_type) { \
		case PIN_DRV_3_BITS_WITH_SLEW: \
			((params).type_a.field) = (value); \
			break; \
		case PIN_DRV_4_BITS_NO_SLEW: \
			((params).type_b.field) = (value); \
			break; \
		case PIN_TYPE_STANDARD: \
			((params).standard.field) = (value); \
			break; \
		default: \
			break; \
		} \
	} while (0)

#define GET_PARAM_FIELD(params, pin_type, field, value) \
	do { \
		switch (pin_type) { \
		case PIN_DRV_3_BITS_WITH_SLEW: \
			(value) = (params).type_a.field; \
			break; \
		case PIN_DRV_4_BITS_NO_SLEW: \
			(value) = (params).type_b.field; \
			break; \
		case PIN_TYPE_STANDARD: \
			(value) = (params).standard.field; \
			break; \
		default: \
			break; \
		} \
	} while (0)

#define PINCTRL_PIN_TYPE(a, b, c) { .number = a, .name = b, .drv_data = (void *) c }

#define DRV_STRENGTH_2 ((u32)0x01)
#define DRV_STRENGTH_3 ((u32)0x02)
#define DRV_STRENGTH_4 ((u32)0x03)
#define DRV_STRENGTH_6 ((u32)0x04)
#define DRV_STRENGTH_8 ((u32)0x05)
#define DRV_STRENGTH_18 ((u32)0x06)

#define CASE_REG_TO_DRV(x)                                                     \
	case DRV_STRENGTH_##x:                                                 \
		return x;

#define CASE_DRV_TO_REG(x)                                                     \
	case x:                                                                \
		return DRV_STRENGTH_##x;

/* Register layout of txdata */
struct txdata_t {
	union {
		struct {
			u32 pupd : 2;
			u32 dout : 1;
			u32 oe : 1;
			u32 reserved : 28;
		} layout;
		u32 val;
	};
};

#define PUPD_OPEN ((u32)0x00)
#define PUPD_PU ((u32)0x02)
#define PUPD_PD ((u32)0x01)
#define PUPD_SPU ((u32)0x03)

#define PUPD_OPEN_SETTLING_INTERVAL_IN_US	10

/* Start bit of registers inside rxdata */
#define GPIO_PAD_BIT 0
#define GPIO_IRQ_TRIG_BIT 1
#define GPIO_TRIG_SIZE 3

#define INT_TRIGGER_NONE		0x0
#define INT_TRIGGER_RISING_EDGE		0x1
#define INT_TRIGGER_FALLING_EDGE	0x2
#define INT_TRIGGER_BOTH_EDGE		0x3
#define INT_TRIGGER_LEVEL_LOW		0x4
#define INT_TRIGGER_LEVEL_HIGH		0x5

struct rxdata_t {
	union {
		struct {
			u32 pad_val : 1;
			u32 trigger_type : 3;
		} layout;
		u32 val;
	};
};

/*
 * bit-positions of isr, isr_ovf, ier, imr & itr is exactly same. So, avoid
 * defining multiple similar looking struct and use generic_ixr_t for all
 * these regs.
 */
struct generic_ixr_t {
	union {
		struct {
			u32 irq : 1;
			u32 reserved : 31;
		} layout;
		u32 val;
	};
};

/**
 * struct google_pingroup - Represent a group of pins and supported pinmux functions.
 * @num: Index of the pingroup.
 * @name: Name of the pingroup.
 * @pins: List of pins that are part of the pingroup.
 * @npins: Numbers of pins included in @pins.
 * @funcs: List of functions that can be selected for this pingroup.
 * @nfuncs: Number of functions that can be selected for this pingroup.
 */
struct google_pingroup {
	const char *name;
	unsigned int num;

	const unsigned int *pins;
	unsigned int npins;

	unsigned int *funcs;
	unsigned int nfuncs;
};

#define GOOGLE_PINS(num)                                                       \
	static const unsigned int google##num##_pins[] = { num }

#define MAX_NR_FUNCS 9

#define PIN_GROUP(gnum, gname, f0, f1, f2, f3, f4, f5, f6, f7, f8)             \
	[gnum] = {                                                             \
		.num = gnum,                                                   \
		.name = gname,                                                 \
		.pins = google##gnum##_pins,                                   \
		.npins = ARRAY_SIZE(google##gnum##_pins),                      \
		.funcs =                                                       \
			(int[]){                                               \
				google_pinmux_##f0,                            \
				google_pinmux_##f1,                            \
				google_pinmux_##f2,                            \
				google_pinmux_##f3,                            \
				google_pinmux_##f4,                            \
				google_pinmux_##f5,                            \
				google_pinmux_##f6,                            \
				google_pinmux_##f7,                            \
				google_pinmux_##f8,                            \
			},                                                     \
		.nfuncs = MAX_NR_FUNCS,                                        \
	}

/**
 * struct google_pin_function - a pinmux function.
 * @name: name of the pin function that is used for lookup.
 * @groups: name(s) of pingroups that provide this function.
 * @ngroups: number of pingroups included in @groups.
 */
struct google_pin_function {
	const char *name;
	const char *const *groups;
	unsigned int ngroups;
};

#define FUNCTION_GROUPS(func, ...)                                             \
	static const char *func##_groups[] = { __VA_ARGS__ }

#define FUNCTION(func)                                                         \
	[google_pinmux_##func] = {                                             \
		.name = #func,                                                 \
		.groups = func##_groups,                                       \
		.ngroups = ARRAY_SIZE(func##_groups),                          \
	}

/**
 * struct google_pinctrl_soc_sswrp_info - Google SOC specific pinctrl configuration for a SSWRP.
 * @pins: array describing all the pins of this SSWRP.
 * @num_pins: number of entries in @pins.
 * @groups: array describing all the pingroups of this SSWRP.
 * @num_groups:number of entries in @groups.
 * @funcs: array describing all the pinmux functions of this SSWRP.
 * @num_funcs: number of entries in @funcs.
 * @num_gpios: number of pingroups exposed as gpios
 * @gpio_func: which function number is GPIO (usually 0).
 * @label: gpio chip label.
 * @common: data that is common for all sswrps in a platform
 * @pins_excl_regs: the list of pins which don't have the full set of registers.
 * @npins_excl_regs: number of entries in @pins_excl_regs.
 */
struct google_pinctrl_soc_sswrp_info {
	const struct pinctrl_pin_desc *pins;
	unsigned int num_pins;

	const struct google_pingroup *groups;
	unsigned int num_groups;

	const struct google_pin_function *funcs;
	unsigned int num_funcs;

	unsigned int num_gpios;
	unsigned int gpio_func;

	const char   *label;
	const struct platform_common_info *common;

	bool (*should_toggle_irq)(struct google_pinctrl_irqinfo *info);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	const struct google_pinctrl_registers_flags *pins_excl_regs;
	unsigned int npins_excl_regs;
#endif
};

/**
 * struct platform_common_info - Platform specific configuration for a SSWRP
 * @regs: array describing which pins are used in a given platform
 * @num_regs: number of pins in @regs
 * pin_stride: size of memory reserved for every pin
 * @dump_regs_header: header used in `dump-registers`
 */
struct platform_common_info {
	const u8     *regs;
	unsigned int num_regs;
	size_t       pin_stride;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	const char   *dump_regs_header;
#endif
};

#define DUMP_REGS_BUF_SIZE 190

extern const struct dev_pm_ops google_pinctrl_pm_ops;

struct google_pinctrl_irqinfo {
	struct google_pinctrl *gctl;
	unsigned int pad_number;
	unsigned int virq;
	int wakeup_capable;
	bool is_pad_as_irq;
	bool requested_by_client;
	bool enabled_by_client;
	bool shared_via_irq_routing;
	struct irq_affinity_notify notifier;
};

/**
 * struct google_pinctrl - state for a pinctrl-google device
 * @dev: A device handler.
 * @pctrl: A pinctrl handler.
 * @chip: A gpiochip handle.
 * @desc: A pin control descriptor.
 * @csr_base: Base address of the csr registers.
 * @secondary_csr_base: Base address of the secondary csr registers
 *                      for shared pads by 2 controllers
 * @secondary_pad_index: Array of all the PADs and their respective index
 *                       in secondary controller
 * @info: Reference to soc's platform specific data for this SSWRP.
 * @lock: Spinlock to protect register resources as well
 *        as google_pinctrl data structures.
 * @clks: Array of all the clocks required
 * @nr_clks: Count of total clocks required
 * @num_irqs: Number of irq out-going from the device
 * @google_pinctrl_irqinfo: Array of struct google_pinctrl_irqinfo. Each
 *                          item in the array holding critical data for
 *                          interrupt handling and that will be passed
 *                          as 'data' while registering each interrupt
 * @wakeup_capable_eint: Confirm whether SSWRP has wakeup capable external
 *                       interrupts
 * @csr_access_pd: Virtual device for PD ensures full power
 * @pad_function_pd: Virtual device for PD ensures min power
 * @aoc_ssr_active: AOC subsystem restart flag
 * @rpm_get_count: RPM Get counter
 * @rpm_put_count: RPM Set counter
 * @rpm_suspend_count: RPM Suspend counter
 * @rpm_resume_count: RPM Resume counter
 * @system_suspend_count: System suspend counter
 * @system_resume_count: System resume counter
 * @g_pingroups_fops_stats: Contains counters for GPIO API function calls for each pin group
 * @pins_rpm_stats: Contains runtime power management calls statistics for each pin
 * @de: DebugFs root directory
 */
struct google_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct gpio_chip chip;
	struct pinctrl_desc desc;
	void __iomem *csr_base;
	void __iomem *secondary_csr_base;
	int *secondary_pad_index;
	const struct google_pinctrl_soc_sswrp_info *info;
	raw_spinlock_t lock;
	struct clk_bulk_data *clks;
	int nr_clks;

	int num_irqs;
	struct google_pinctrl_irqinfo *irqinfo;
	int wakeup_capable_eint;

	struct device *csr_access_pd;
	struct device *pad_function_pd;
	bool aoc_ssr_active;
	bool suspended;
	bool rpm_capable;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	atomic_t rpm_get_count;
	atomic_t rpm_put_count;
	atomic_t rpm_suspend_count;
	atomic_t rpm_resume_count;
	atomic_t system_suspend_count;
	atomic_t system_resume_count;

	ktime_t rpm_suspend_resume_times[RPM_STATS_SIZE];
	ktime_t rpm_max_suspend_time;
	ktime_t rpm_last_suspend_time;
	int rpm_suspend_resume_idx;

	bool suspend_dump_enabled;
	bool resume_dump_enabled;
	bool set_check_enabled;

	struct fops_stats *g_pingroups_fops_stats;

	struct pin_rpm_stats *pins_rpm_stats;
	bool pins_rpm_stats_alloc;
	raw_spinlock_t pins_rpm_stats_lock;

	struct dentry *de;
#endif
};

/**
 * google_pinctrl_probe - register soc's pins with the pinctrl and GPIO frameworks.
 *
 * @pdev: platform device to use for memory resource lookup and resource management.
 * @google_soc_pinctrl_of_match: array describing the soc specific details for all the SSWRPs in the SOC.
 *
 * Returns zero on success else returns a negative error code.
 */
int google_pinctrl_probe(struct platform_device *pdev,
			 const struct of_device_id *google_soc_pinctrl_of_match);

/**
 * google_pinctrl_remove - unregister soc's pins that were registered using google_pinctrl_probe().
 *
 * @pdev: platform device that was passed with google_pinctrl_probe().
 * Returns zero on success else returns a negative error code.
 */
void google_pinctrl_remove(struct platform_device *pdev);

int google_pinctrl_trylock(struct google_pinctrl *gctl, unsigned long *flags, bool check_aoc_ssr);

void google_pinctrl_unlock(struct google_pinctrl *gctl, unsigned long *flags);

void google_writel(unsigned int reg_offset, u32 val, struct google_pinctrl *gctl,
		   const struct google_pingroup *g);
u32 google_readl(unsigned int reg_offset, struct google_pinctrl *gctl,
		 const struct google_pingroup *g);

int google_pinctrl_get_csr_pd(struct google_pinctrl *gctl, int pin);
void google_pinctrl_put_csr_pd(struct google_pinctrl *gctl, int pin);
int get_reg2offset(u8 reg_num);
bool google_pinctrl_should_toggle_irq(struct google_pinctrl_irqinfo *info);

/**
 * google_pinctrl_pin_valid - checks if pin can be accessed from the OS
 *
 * @gctl: pin control structure for the platform device.
 * @pin_nr: pin number to check.
 *
 * Returns true if the pin can be accessed or false otherwise.
 */
static inline bool google_pinctrl_pin_valid(struct google_pinctrl *gctl, unsigned int pin_nr)
{
	return !(gctl->chip.valid_mask && !test_bit(pin_nr, gctl->chip.valid_mask));
}

void google_pinctrl_aoc_ssr_request(bool start);

void google_pinctrl_trace_init(struct platform_device *pdev);

#endif /* _PINCTRL_GOOGLE_COMMON_H_ */
