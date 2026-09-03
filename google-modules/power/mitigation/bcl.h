/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __BCL_DEFS_H
#define __BCL_DEFS_H

#include <linux/devfreq.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/pm_qos.h>
#include <linux/workqueue.h>
#include <trace/events/power.h>
#if IS_ENABLED(CONFIG_SOC_ZUMAPRO)
#include <dt-bindings/soc/google/zumapro-bcl.h>
#elif IS_ENABLED(CONFIG_SOC_ZUMA)
#include <dt-bindings/soc/google/zuma-bcl.h>
#elif IS_ENABLED(CONFIG_SOC_GS101)
#include <dt-bindings/soc/google/gs101-bcl.h>
#elif IS_ENABLED(CONFIG_SOC_GS201)
#include <dt-bindings/soc/google/gs201-bcl.h>
#elif IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
#include <dt-bindings/soc/google/rdo-bcl_defs.h>
#endif
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
#include <ap-pmic/da9188.h>
#else
#include <soc/google/exynos_pm_qos.h>
#include <dt-bindings/power/s2mpg1x-power.h>
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
#include <soc/google/goog_mba_cpm_iface.h>
#endif

#include "uapi/brownout_stats.h"
#include "bcl_version.h"


enum IFPMIC {
	MAX77759,
	MAX77779
};

#define DELTA_5MS			(5 * NSEC_PER_MSEC)
#define DELTA_10MS			(10 * NSEC_PER_MSEC)
#define VSHUNT_MULTIPLIER		10000
#define MILLI_TO_MICRO			1000
#define IRQ_ENABLE_DELAY_MS		50
#define NOT_USED			9999
#define INIT_CPU_MAX_COUNT		5
#define INIT_GPU_MAX_COUNT		5
#define TIMEOUT_1000MS			1000
#define TIMEOUT_5S			5000
#define TIMEOUT_10000US			10000
#define TIMEOUT_20MS			20
#define TIMEOUT_10MS			10
#define TIMEOUT_5MS			5
#define TIMEOUT_1MS			1
#define DATA_LOGGING_TIME_MS		48
#define DATA_LOGGING_NUM		50
#define DATA_LOGGING_COOL_DOWN_TIME_MS	300
#define HEAVY_MITIGATION_MODULES_NUM	3
#define MITIGATION_INPUT_DELIM		","
#define MITIGATION_PRINT_BUF_SIZE	256
#define MITIGATION_TMP_BUF_SIZE	16
#define MAXMIN_RESET_VAL		0x807F
#define DEFAULT_SMPL_WARN_LVL		3000
#define BAT_DTLS_OILO_ASSERTED		0x6
#define PWRWARN_LPF_RFFE_MMWAVE_DATA_0         0xCF
#define PWRWARN_LPF_RFFE_MMWAVE_DATA_1         0xD0
#define PWRWARN_THRESH_RFFE_MMWAVE             0x3C
#define PWRWARN_LPF_RFFE_MMWAVE_MSB_MASK       0x0F
#define LAST_CURR_RD_CNT_MAX			10
#define UVLO_DET_MAX			0x1
#define UVLO_REL_MAX			0x3
#define OILO_DET_MAX			0x1f
#define OILO_REL_MAX			0x05


enum IRQ_CONFIG {
	IRQ_NOT_EXIST,
	IRQ_EXIST,
};

#define PWRWARN_LPF_RFFE_MMWAVE_RSHIFT         4
#define DEFAULT_SYS_UVLO1_LVL 0xC /* 3.2V */
#define DEFAULT_SYS_UVLO2_LVL 0x2 /* 2.7V */
#define DEFAULT_VDROOP_INT_MASK 0xDF /* Only BATOILO is passed */
#define DEFAULT_INTB_MASK 0x0 /* All IRQs are passed */
#define DEFAULT_SMPL 0xCB /* 3.2V, 200mV HYS, 38us debounce */
#define DEFAULT_OCP_BATFET_TIMER_MS 666
#define DEFAULT_VIMON_PWR_LOOP_CNT 0
#define DEFAULT_VIMON_PWR_LOOP_THRESH 20000
#define DEFAULT_MIN_V_THRS 2000
#define DEFAULT_MIN_V_CNT_MAX 20
#define DEFAULT_MIN_V_SHDN_EN 0
#define DEFAULT_MIN_I_CLR_THRS -200
#define SYS_EVT_RD_BYTES 1
#define SYS_EVT_RD_MASK 0xFF
#define SYS_EVT_STEP 4
#define SYS_EVT_MAX_MAIN 56
#define SYS_EVT_MAX_SUB 43

#if IS_ENABLED(CONFIG_SOC_MBU)
#define SYS_EVT_MAIN 15
#define SYS_EVT_SUB 14
#else
#define SYS_EVT_MAIN 0
#define SYS_EVT_SUB 1
#endif

#define SYS_EVT_PMIC_MAX 2
#define SYS_EVT_PRE_UVLO_START 0x14
#define SYS_EVT_SHDN_START 0x1A
#define SYS_EVT_PRE_UVLO_HIT_CNT_M 0x20
#define SYS_EVT_PRE_UVLO_HIT_CNT_S 0x13
#define SYS_EVT_UVLO_DUR_CNT 6
#define SYS_EVT_BYTE_CNT 4
#define SYS_EVT_PRE_UVLO_HIT_CNT_RD_CNT 2
#define SYS_EVT_PRE_OCP_CPU1_UPPER_BITS 1
#define SYS_EVT_PRE_OCP_CPU2_UPPER_BITS 0
#define SYS_EVT_PRE_OCP_GPU_UPPER_BITS 1
#define SYS_EVT_PRE_OCP_TPU_UPPER_BITS 1

#define SYS_EVT_PRE_OCP_CPU2_CPU1 34
#define SYS_EVT_PRE_OCP_TPU_CPU0 35
#define SYS_EVT_PRE_OCP_GPU_MM 21

#define SYS_EVT_ODPM_MAIN 36
#define SYS_EVT_ODPM_SUB 23
#define SYS_EVT_ODPM_RD_CNT 1
#define SYS_EVT_ODPM_M_CH 12
#define SYS_EVT_ODPM_S_CH 12

#define QOS_PARAM_CNT 2
#define QOS_NONE 0
#define QOS_LIGHT 1
#define QOS_HEAVY 2
#define QOS_PARAM_OFFSET 1
#define QOS_LIGHT_IND 0
#define QOS_HEAVY_IND 1

#define CLK_RATIO_MASK (0x7)
#define CLK_RATIO_SHIFT (3)  /* Each clock ratio uses 3 bits */
#define MAX_DYN_CLK_DIV_RATIO (7)

static inline bool is_if_pmic_irq(int val)
{
	return val == UVLO1 || val == UVLO2 || val == BATOILO1 ||
		val == BATOILO2;
}

enum CPU_CLUSTER {
	LITTLE_CLUSTER,
	MID_CLUSTER,
	BIG_CLUSTER,
	CPU_CLUSTER_MAX,
};

enum SUBSYSTEM_SOURCE {
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
	CPU0,
	CPU1A,
	CPU1B,
	CPU2,
	GPU,
	TPU,
	AUR,
	MM,
	INFRA,
	INFRA_GPU,
	SUBSYSTEM_SOURCE_MAX,
#elif IS_ENABLED(CONFIG_SOC_MBU)
	/* SOC Sources */
	CPU1B,
	CPU1A,
	CPU2,
	GPU,
	TPU,
	AUR,
	DSU,
	CME,
	SOC_SUBSYSTEM_SOURCE_MAX,

	/* Peripheral Sources */
	MM = SOC_SUBSYSTEM_SOURCE_MAX,
	INFRA,
	INFRA_GPU,
	SUBSYSTEM_SOURCE_MAX,

	/* TODO: b/415729497 - Remove this temporary CPU0 definition,
	 * which is a workaround for compilation
	 */
	CPU0 = CPU1B
#else
	CPU0,
	CPU1,
	CPU2,
	GPU,
	TPU,
	AUR,
	MM,
	INFRA,
	INFRA_GPU,
	SUBSYSTEM_SOURCE_MAX,
#endif
};

enum CONCURRENT_PWRWARN_IRQ {
	NONE_BCL_BIN,
	MMWAVE_BCL_BIN,
	RFFE_BCL_BIN,
	MAX_CONCURRENT_PWRWARN_IRQ,
};

enum BCL_BATT_IRQ {
	UVLO1_IRQ_BIN,
	UVLO2_IRQ_BIN,
	BATOILO_IRQ_BIN,
	BATOILO2_IRQ_BIN,
	MAX_BCL_BATT_IRQ,
};

enum wlc_tx_state {
	WLC_ENABLED_TX,
	WLC_DISABLED_TX,
};

enum MITIGATION_MODE {
	START,
	LIGHT,
	MEDIUM,
	HEAVY,
	DISABLED,
	MAX_MITIGATION_MODE,
};

enum IRQ_DURATION_BIN {
	LT_5MS,
	BT_5MS_10MS,
	GT_10MS,
};

enum IRQ_TYPE {
#if IS_ENABLED(CONFIG_SOC_MBU)
	CORE_MAIN_PMIC = 15,
	CORE_SUB_PMIC = 14,
	IF_PMIC = 2,
#else
	CORE_MAIN_PMIC,
	CORE_SUB_PMIC,
	IF_PMIC,
#endif
};

struct irq_duration_stats {
	atomic_t lt_5ms_count;
	atomic_t bt_5ms_10ms_count;
	atomic_t gt_10ms_count;
	ktime_t start_time;
};

struct ocpsmpl_stats {
	ktime_t _time;
	int capacity;
	int voltage;
};

enum RATIO_SOURCE {
	heavy,
	light,
};

enum MPMM_SOURCE {
	LITTLE,
	MID,
	BIG,
	MPMMEN
};

enum cpu_qos_device_idx {
	QOS_CPU0,
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
	QOS_CPU1A,
	QOS_CPU1B,
#else
	QOS_CPU1,
#endif
	QOS_CPU2,
	CPU_CORES_MAX
};

enum non_cpu_qos_device_idx {
	QOS_GPU,
	QOS_TPU,
	QOS_GXP,
	NON_CPU_CORES_MAX
};

enum POLARITY {
	POLARITY_LOW,
	POLARITY_HIGH,
};

enum odpm_rdback_type {
	TELEM_VOLTAGE,
	TELEM_CURRENT,
	TELEM_POWER,
};

enum oplvl_tier {
	OPLVL_HIGH,
	OPLVL_MID_HIGH,
	OPLVL_MID_LOW,
	OPLVL_LOW,
	OPLVL_TIER_COUNT
};

struct dyn_clock_div_conf {
	bool enable;
	uint8_t ratio[OPLVL_TIER_COUNT];
	uint32_t thresh[OPLVL_TIER_COUNT];
};

enum THROTTLE_STATE {
	THROTTLE_STATE_NONE,
	THROTTLE_STATE_MEDIUM,
	THROTTLE_STATE_HEAVY,
	THROTTLE_STATE_CRITICAL,
	THROTTLE_STATE_MAX,
};

struct qos_throttle_limit {
	struct cpufreq_policy *cpu_policy[CPU_CORES_MAX];
	int cpu_freq[CPU_CORES_MAX];
	u32 cpu_limit[CPU_CORES_MAX][QOS_PARAM_CNT];
	u32 df_limit[NON_CPU_CORES_MAX][QOS_PARAM_CNT];
	struct devfreq *df[NON_CPU_CORES_MAX];
	int df_freq[NON_CPU_CORES_MAX];
	struct freq_qos_request cpu_qos_max[CPU_CORES_MAX];
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188) || IS_ENABLED(CONFIG_GOOGLE_MFD_DA9186)
	struct dev_pm_qos_request qos_max[NON_CPU_CORES_MAX];
#else
	struct exynos_pm_qos_request qos_max[NON_CPU_CORES_MAX];
#endif
};

struct zone_triggered_stats {
	atomic_t triggered_cnt[MAX_MITIGATION_MODE];
	ktime_t triggered_time[MAX_MITIGATION_MODE];
};

/**
 * @struct bcl_zone
 * @brief Represents a specific BCL power mitigation zone.
 */
struct bcl_zone {
	struct device *device;
	struct completion deassert;
	struct work_struct irq_triggered_work;
	struct delayed_work warn_work;
	struct delayed_work enable_irq_work;
	struct qos_throttle_limit *bcl_qos;
	struct qos_throttle_limit *userspace_bcl_qos;
	struct ocpsmpl_stats bcl_stats;
	struct zone_triggered_stats last_triggered;
	atomic_t bcl_cnt;
	struct gpio_desc *bcl_pin;
	int bcl_irq;
	int irq_type;
	int polarity;
	void *parent;
	int idx;

	/** @brief A runtime flag indicating if this zone is enable or disable */
	bool disabled;

	bool irq_reg;
	bool conf_qos;
	const char *devname;
	u32 current_state;
	bool has_irq;
	int throttle_lvl;
	int throttle_lvl_addr;
};

struct bcl_core_conf {
	unsigned int con_heavy;
	unsigned int con_light;
	unsigned int clkdivstep;
	unsigned int clkdivstep_last;
	unsigned int vdroop_flt;
	unsigned int mitigation_type;
	unsigned int clk_stats;
	unsigned int clk_out;
	void __iomem *base_mem;
};

enum CPU_BUFF_IDX {
	CPU_BUFF_IDX_MID,
	CPU_BUFF_IDX_BIG
};

enum CPU_BUFF_VALS {
	CPU_BUFF_CON_HEAVY,
	CPU_BUFF_CON_LIGHT,
	CPU_BUFF_CLKDIVSTEP,
	CPU_BUFF_VDROOP_FLT,
	CPU_BUFF_CLK_STATS,
	CPU_BUFF_VALS_MAX
};

struct bcl_cpu_buff_conf {
	unsigned int buff[CPU_BUFF_VALS_MAX];
	unsigned int addr[CPU_BUFF_VALS_MAX];
	uint8_t wr_update_rqd;
	uint8_t rd_update_rqd;
};

struct bcl_batt_irq_conf {
	int batoilo_lower_limit;
	int batoilo_upper_limit;
	u8 batoilo_trig_lvl;
	u8 batoilo_wlc_trig_lvl;
	u8 batoilo_usb_trig_lvl;
	u8 batoilo_otg_trig_lvl;
	u8 batoilo_bat_open_to;
	u8 batoilo_rel;
	u8 batoilo_det;
	u8 batoilo_int_rel;
	u8 batoilo_int_det;
	u8 uvlo_rel;
	u8 uvlo_det;
};

struct bcl_evt_count {
	unsigned int uvlo1;
	unsigned int uvlo2;
	unsigned int batoilo1;
	unsigned int batoilo2;
	u8 enable;
	u8 rate;
};

struct bcl_mitigation_conf {
	u32 module_id;
	u32 threshold;
};

struct bcl_vimon_intf {
	uint16_t data[VIMON_BUF_SIZE];
	size_t count;
};

struct bcl_debug_sys_evt {
	uint8_t addr;
	uint8_t pmic;
	uint8_t count;
};

struct bcl_sys_evt_odpm_param {
	uint8_t cpu1_ch;
	uint8_t cpu1_pmic;
	uint8_t cpu2_ch;
	uint8_t cpu2_pmic;
	uint8_t gpu_ch;
	uint8_t gpu_pmic;
	uint8_t tpu_ch;
	uint8_t tpu_pmic;
};

struct bcl_ifpmic_ops {
	int (*get_raw_sts)(struct device *ifpmic_irq_dev, int idx);
	int (*get_irq)(struct device *ifpmic_irq_dev, int *idx);
	int (*clr_irq)(struct device *ifpmic_irq_dev, int idx);
	int (*vimon_read)(struct device *ifpmic_irq_dev);
	int (*set_oilo1)(struct device *ifpmic_irq_dev, int val);
	int (*get_oilo1)(struct device *ifpmic_irq_dev, int *val);
	int (*set_oilo2)(struct device *ifpmic_irq_dev, int val);
	int (*get_oilo2)(struct device *ifpmic_irq_dev, int *val);
	int (*set_uvlo1)(struct device *ifpmic_irq_dev, int val);
	int (*get_uvlo1)(struct device *ifpmic_irq_dev, int *val);
	int (*set_uvlo2)(struct device *ifpmic_irq_dev, int val);
	int (*get_uvlo2)(struct device *ifpmic_irq_dev, int *val);
	int (*set_uvlo_vdroop)(struct device *ifpmic_irq_dev, int uvlo_type,
			       int vdroop_type, bool enable);
	int (*set_oilo_vdroop)(struct device *ifpmic_irq_dev, int oilo_type,
			       int vdroop_type, bool enable);
	int (*set_uvlo1_hyst)(struct device *ifpmic_irq_dev, int val);
	int (*get_uvlo1_hyst)(struct device *ifpmic_irq_dev, int *val);
	int (*set_uvlo2_hyst)(struct device *ifpmic_irq_dev, int val);
	int (*get_uvlo2_hyst)(struct device *ifpmic_irq_dev, int *val);
};

struct bcl_device {
	struct device *device;
	struct device *main_dev;
	struct device *sub_dev;
	struct device *mitigation_dev;
	struct odpm_info *main_odpm;
	struct odpm_info *sub_odpm;
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	struct iio_dev *indio_dev;
	struct google_odpm *odpm;
#endif
	void __iomem *sysreg_cpucl0;
	struct power_supply *batt_psy;
	struct power_supply *otg_psy;
	struct power_supply *main_charger_psy;
	struct cpm_iface_client *client;
	struct bcl_ifpmic_ops *ifpmic_ops;
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188) || IS_ENABLED(CONFIG_GOOGLE_MFD_DA9186)
	struct pmic_mfd_mbox pmic_mbox;
#endif

	u32 remote_ch;
	u32 remote_pmic_ch;

	struct notifier_block psy_nb;
	struct bcl_zone *zone[TRIGGERED_SOURCE_MAX];
	struct delayed_work mailbox_init_work;
	struct delayed_work init_qos_work;
	struct workqueue_struct *qos_update_wq;
	bool throttle;
	int throttle_state;

	struct mutex sysreg_lock;

	struct i2c_client *main_i2c;
	struct i2c_client *main_pmic_i2c;
	struct i2c_client *main_rtc_i2c;
	struct i2c_client *sub_i2c;
	struct i2c_client *sub_pmic_i2c;
	struct i2c_client *main_meter_i2c;
	struct i2c_client *sub_meter_i2c;
	struct device *intf_pmic_dev;
	struct device *irq_pmic_dev;
	struct device *fg_pmic_dev;
	struct device *vimon_dev;
	struct device *ifpmic_irq_dev;
	struct mutex cpu_ratio_lock;
	struct mutex qos_update_lock;
	struct bcl_core_conf core_conf[SUBSYSTEM_SOURCE_MAX];
	struct bcl_cpu_buff_conf cpu_buff_conf[CPU_CLUSTER_MAX];
	struct notifier_block cpu_nb;
	struct delayed_work rd_fg_work;

	bool batt_psy_initialized;
	bool initialized;
	bool sw_mitigation_enabled;
	bool hw_mitigation_enabled;
	bool ifpmic_irq_drv_en;

	unsigned int main_offsrc1;
	unsigned int main_offsrc2;
	unsigned int sub_offsrc1;
	unsigned int sub_offsrc2;
	unsigned int pwronsrc;
	unsigned int irq_delay;
	unsigned int last_current;
	unsigned int fg_rd_retry_cnt;

	unsigned int vdroop1_irq;
	unsigned int vdroop2_irq;
	struct gpio_desc *vdroop1_pin;
	struct gpio_desc *vdroop2_pin;
	unsigned int rffe_channel;

	unsigned int min_v_thrs_cnt_max;
	unsigned int min_v_thrs;
	unsigned int min_v_shdn_en;
	int min_i_clr_thrs;

	unsigned int ocp_batfet_timeout;
	bool ocp_batfet_timeout_enable;
	unsigned int ocp_bat_throttle_timeout;
	bool ocp_bat_throttle_timeout_enable;

	/* debug */
	struct dentry *debug_entry;
	unsigned int gpu_clk_out;
	unsigned int tpu_clk_out;
	unsigned int aur_clk_out;
	u8 add_perph;
	u64 add_addr;
	u64 add_data;
	void __iomem *base_add_mem[SUBSYSTEM_SOURCE_MAX];
	struct bcl_debug_sys_evt debug_sys_evt;
	struct bcl_sys_evt_odpm_param sys_evt_odpm_param;
	uint8_t sys_evt_main[SYS_EVT_MAX_MAIN];
	uint8_t sys_evt_sub[SYS_EVT_MAX_SUB];
	uint8_t cpm_cached_sys_evt_main[SYS_EVT_MAX_MAIN];
	uint8_t cpm_cached_sys_evt_sub[SYS_EVT_MAX_SUB];
	uint8_t sys_evt_sysfs_pmic;
	uint8_t sys_evt_sysfs_addr;
	uint8_t sys_evt_sysfs_data;

	int main_irq_base, sub_irq_base;
	u8 main_setting[METER_CHANNEL_MAX];
	u8 sub_setting[METER_CHANNEL_MAX];
	u64 main_limit[METER_CHANNEL_MAX];
	u64 sub_limit[METER_CHANNEL_MAX];
	int main_pwr_warn_irq[METER_CHANNEL_MAX];
	int sub_pwr_warn_irq[METER_CHANNEL_MAX];
	bool main_pwr_warn_triggered[METER_CHANNEL_MAX];
	bool sub_pwr_warn_triggered[METER_CHANNEL_MAX];
	struct delayed_work main_pwr_irq_work;
	struct delayed_work sub_pwr_irq_work;
	struct delayed_work setup_main_odpm_work;
	struct delayed_work setup_sub_odpm_work;
	struct delayed_work setup_core_pmic_work;
	struct irq_duration_stats ifpmic_irq_bins[MAX_BCL_BATT_IRQ][MAX_CONCURRENT_PWRWARN_IRQ];
	struct irq_duration_stats pwrwarn_main_irq_bins[METER_CHANNEL_MAX];
	struct irq_duration_stats pwrwarn_sub_irq_bins[METER_CHANNEL_MAX];
	const char *main_rail_names[METER_CHANNEL_MAX];
	const char *sub_rail_names[METER_CHANNEL_MAX];

	int cpu_cluster[CPU_CORES_MAX];

	bool cpu_cluster_on[CPU_CORES_MAX];

	struct bcl_batt_irq_conf batt_irq_conf1;
	struct bcl_batt_irq_conf batt_irq_conf2;
	int pmic_irq;

	enum IFPMIC ifpmic;

	struct gvotable_election *toggle_wlc;
	struct gvotable_election *toggle_usb;
	struct gvotable_election *toggle_gpu;

	struct bcl_evt_count evt_cnt;
	struct bcl_evt_count evt_cnt_latest;

	bool enabled_br_stats;
	bool data_logging_initialized;
	unsigned int triggered_idx;
	ssize_t br_stats_size;
	struct brownout_stats *br_stats;
	struct max_odpm_stats *max_odpm_stats;
	/* module id */
	struct bcl_mitigation_conf main_mitigation_conf[METER_CHANNEL_MAX];
	struct bcl_mitigation_conf sub_mitigation_conf[METER_CHANNEL_MAX];
	u32 *non_monitored_module_ids;
	u32 non_monitored_mitigation_module_ids;
	atomic_t mitigation_module_ids;

	bool config_modem;
	bool rffe_mitigation_enable;

	struct bcl_vimon_intf vimon_intf;

	u8 vdroop_int_mask;
	u8 intb_int_mask;
	u8 uvlo2_lvl;
	u8 uvlo1_lvl;
	u8 smpl_ctrl;
	u8 pre_uvlo_debounce;
	bool uvlo2_vdrp2_en;
	bool uvlo2_vdrp1_en;
	bool uvlo1_vdrp1_en;
	bool uvlo1_vdrp2_en;
	bool oilo1_vdrp1_en;
	bool oilo1_vdrp2_en;
	bool oilo2_vdrp1_en;
	bool oilo2_vdrp2_en;
	bool vimon_pwr_loop_en;
	bool vimon_pwr_loop_cnt;
	int vimon_pwr_loop_thresh;
	int vimon_trigger;
	struct brownout_stats vimon_odpm_stats;
	int odpm_cpu2_ch;
	int odpm_cpu2_max;
	int odpm_cpu1_ch;
	int odpm_cpu1_max;
	int odpm_gpu_ch;
	int odpm_gpu_max;
	int odpm_tpu_ch;
	int odpm_tpu_max;

	struct delayed_work setup_qos_work;
	u8 retry_setup_qos_count;
	struct delayed_work qos_work;
	u8 qos_init_count;
	u8 init_core_pmic_count;
	u8 init_main_odpm_count;
	bool usb_otg_conf;

	struct dyn_clock_div_conf subsys_dyn_clk_conf[SUBSYSTEM_SOURCE_MAX];
};

extern void google_bcl_irq_update_lvl(struct bcl_device *bcl_dev, int index, unsigned int lvl);
extern int google_set_db(struct bcl_device *data, unsigned int value, enum MPMM_SOURCE index);
extern unsigned int google_get_db(struct bcl_device *data, enum MPMM_SOURCE index);
extern struct bcl_device *google_retrieve_bcl_handle(void);
extern int google_init_gpu_ratio(struct bcl_device *data);
extern int google_init_tpu_ratio(struct bcl_device *data);
extern int google_init_aur_ratio(struct bcl_device *data);
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
int google_bcl_cpm_send_cmd(struct bcl_device *bcl_dev, uint8_t command, uint8_t zone,
			    uint8_t config, uint16_t value, uint32_t *response);
#endif

u64 settings_to_current(struct bcl_device *bcl_dev, int pmic, int idx, u32 setting);

void google_init_debugfs(struct bcl_device *bcl_dev);

void google_bcl_remove_thermal(struct bcl_device *bcl_dev);
int google_bcl_setup_qos(struct bcl_device *bcl_dev);
int google_bcl_setup_votable(struct bcl_device *bcl_dev);
void google_bcl_remove_votable(struct bcl_device *bcl_dev);
int google_bcl_init_data_logging(struct bcl_device *bcl_dev);
void google_bcl_start_data_logging(struct bcl_device *bcl_dev, int idx);
void google_bcl_remove_data_logging(struct bcl_device *bcl_dev);
void google_bcl_upstream_state(struct bcl_zone *zone, enum MITIGATION_MODE state);
int google_bcl_register_zone(struct bcl_device *bcl_dev, int idx, const char *devname,
			     struct gpio_desc *pin, int irq, int type, int irq_config,
			     int polarity, u32 flag);
int google_bcl_configure_modem(struct bcl_device *bcl_dev);
int google_pwr_loop_trigger_mitigation(struct bcl_device *bcl_dev);
ssize_t safe_emit_bcl_cnt(char *buf, struct bcl_zone *zone);
ssize_t safe_emit_pre_evt_cnt(char *buf, struct bcl_zone *zone);
void update_irq_end_times(struct bcl_device *bcl_dev, int id);
void update_irq_start_times(struct bcl_device *bcl_dev, int id);
void pwrwarn_update_start_time(struct bcl_device *bcl_dev,
					int id, struct irq_duration_stats *bins,
					bool *pwr_warn_triggered,
					enum CONCURRENT_PWRWARN_IRQ bin_ind);
void pwrwarn_update_end_time(struct bcl_device *bcl_dev, int id, struct irq_duration_stats *bins,
				enum CONCURRENT_PWRWARN_IRQ bin_ind);
void trace_bcl_zone_stats(struct bcl_zone *zone, int value);
int google_bcl_mitigation_trigger(int idx, void *data);
int google_bcl_register_ifpmic(struct bcl_ifpmic_ops *ops, struct device *ifpmic_irq_dev);
void google_bcl_unregister_ifpmic(void);

/**
 * google_bcl_init_vdroop_gpio
 * @bcl_dev: bcl_device private data
 *
 * This will enable any appropriate GPIO configurations
 * in order for VDROOP to function properly.
 *
 * Context: Can be called from any context.
 *
 * Return: 0 for success or reasons for failure
 */
int google_bcl_init_vdroop_gpio(struct bcl_device *bcl_dev);

inline bool is_ifpmic_dev_en_valid_ind(struct bcl_device *bcl_dev, int idx);

#endif /* __BCL_DEFS_H */
