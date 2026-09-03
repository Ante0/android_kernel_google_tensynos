/* SPDX-License-Identifier: GPL-2.0-only */
#if !defined(__H2OMG_H__)
#define __H2OMG_H__

#include <linux/irqreturn.h>
#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

enum h2omg_sensor_id {
	SENSOR0,
	SENSOR1,
	SENSOR2,
	SENSOR_REFERENCE,
};

enum h2omg_detect_mode {
	DETECT_WET,
	DETECT_DRY
};

enum h2omg_sensor_state {
	SENSOR_DRY,
	SENSOR_WET,
	SENSOR_DIS,
	SENSOR_INV,
};

enum h2omg_fuse_state {
	FUSE_SHORT,
	FUSE_OPEN,
};

struct h2omg_state {
	bool detect_enabled;
	enum h2omg_sensor_state sensors[4];
	enum h2omg_fuse_state fuse;
	bool fault_enabled;
};

enum h2omg_trigger_state {
	SINGLE_TRIGGER_INITIAL,
	DUAL_TRIGGER_INITIAL,
	TRIGGER_WHEN_S0_GETS_WET,
	TRIGGER_WHEN_S1_GETS_WET,
	TRIGGER_NOW
};

enum h2omg_timer_id {
	TIMER_ID_SENSOR_NONE,
	TIMER_ID_SENSOR_0,
	TIMER_ID_SENSOR_1
};

#define CONTROL_SENSOR_ON         1
#define CONTROL_SENSOR_OFF        0

#define CONTROL_MODE_WET_DETECT   0
#define CONTROL_MODE_DRY_DETECT   1

struct h2omg_info {
	struct device *dev;
	unsigned int irq;
	struct regmap *regmap;
	const struct h2omg_ops *ops;
	struct h2omg_state latched_state;
	struct h2omg_state boot_state;
	struct mutex lock;
	u8 control_set;  /* intended value for control */
	unsigned int num_sensors;
	bool dual_trigger;
	unsigned int revision;

	bool vref_div;  /* does the board divide vref? */

	u32 trigger_delay_ms;       /* allowed single sensor wet time */
	int timer_id;               /* which sensor triggered this timer */
	struct timer_list wet_timer;
	struct work_struct timer_work;
};

struct h2omg_ops {
	int (*control_get)(struct h2omg_info *info, unsigned int *val);
	int (*control_set)(struct h2omg_info *info, unsigned int val);

	int (*status_get)(struct h2omg_info *info, unsigned int *val);

	int (*fuse_enable_get)(struct h2omg_info *info, bool *enable);
	int (*fuse_enable_set)(struct h2omg_info *info, bool enable);
	int (*fuse_state_get)(struct h2omg_info *info, enum h2omg_fuse_state *state);

	size_t (*sensor_count_get)(struct h2omg_info *info);
	int (*sensor_enable_get)(struct h2omg_info *info,
			enum h2omg_sensor_id id, bool *enable);
	int (*sensor_enable_set)(struct h2omg_info *info,
			enum h2omg_sensor_id id, bool enable);
	int (*sensor_mode_get)(struct h2omg_info *info,
			enum h2omg_sensor_id id, enum h2omg_detect_mode *mode);
	int (*sensor_mode_set)(struct h2omg_info *info,
			enum h2omg_sensor_id id, enum h2omg_detect_mode mode);
	int (*sensor_acmp_get)(struct h2omg_info *info,
			enum h2omg_sensor_id id, unsigned int *acmp);

	irqreturn_t (*irq_handler)(int irq, void *data);
	int (*cleanup)(struct h2omg_info *info);
	void (*timeout_handler)(struct h2omg_info *info);
};

struct h2omg_reg_val {
	unsigned int reg;
	unsigned int val;
};

extern int h2omg_update_regs(struct h2omg_info *info,
			     const struct h2omg_reg_val *reg_val, size_t len);

extern int h2omg_slg_v2_init(struct i2c_client *client);
extern int h2omg_slg_v3_init(struct i2c_client *client);
extern int h2omg_atl_v1_init(struct i2c_client *client);

extern int h2omg_timer_start(struct h2omg_info *info, enum h2omg_timer_id sensor_id);
extern int h2omg_timer_stop(struct h2omg_info *info);
extern int h2omg_timer_init(struct h2omg_info *info);
extern int h2omg_timer_cleanup(struct h2omg_info *info);

static inline int h2omg_sensor_count_get(struct h2omg_info *info)
{
	return info->ops->sensor_count_get(info);
}

static inline int h2omg_control_get(struct h2omg_info *info, unsigned int *vreg)
{
	return info->ops->control_get(info, vreg);
}

static inline int h2omg_control_set(struct h2omg_info *info, unsigned int vreg)
{
	return info->ops->control_set(info, vreg);
}

static inline int h2omg_status_get(struct h2omg_info *info, unsigned int *val)
{
	return info->ops->status_get(info, val);
}

static inline int h2omg_fuse_enable_get(struct h2omg_info *info, bool *enable)
{
	return info->ops->fuse_enable_get(info, enable);

}
static inline int h2omg_fuse_enable_set(struct h2omg_info *info, bool enable)
{
	return info->ops->fuse_enable_set(info, enable);
}

static inline int h2omg_fuse_state_get(struct h2omg_info *info,
				       enum h2omg_fuse_state *state)
{
	return info->ops->fuse_state_get(info, state);
}

static inline int h2omg_sensor_enable_get(struct h2omg_info *info,
					  enum h2omg_sensor_id id, bool *enable)
{
	return info->ops->sensor_enable_get(info, id, enable);
}

static inline int h2omg_sensor_enable_set(struct h2omg_info *info,
					  enum h2omg_sensor_id id, bool enable)
{
	return info->ops->sensor_enable_set(info, id, enable);
}

static inline int h2omg_sensor_mode_get(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					enum h2omg_detect_mode *mode)
{
	return info->ops->sensor_mode_get(info, id, mode);
}

static inline int h2omg_sensor_mode_set(struct h2omg_info *info,
					enum h2omg_sensor_id id,
					enum h2omg_detect_mode mode)
{
	return info->ops->sensor_mode_set(info, id, mode);
}

static inline bool h2omg_sensor_is_wet(unsigned int state, unsigned int mode)
{
	return (mode == CONTROL_MODE_WET_DETECT) ? !!state : !state;
}
#endif /* __H2OMG_H__ */
