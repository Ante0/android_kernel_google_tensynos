/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#ifndef __DPTX_DRIVER_H__
#define __DPTX_DRIVER_H__
#include <linux/notifier.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <drm/drm_device.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_bridge.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_fixed.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_edid.h>
#include <soc/google/google-usb-phy-dp.h>

#include "drm_dp_helper_additions.h"

#include "hdcp.h"
#include <linux/regmap.h>
#include <linux/notifier.h>

#define DPTX_RECEIVER_CAP_SIZE		0x10
#define DPTX_SDP_NUM		0x10
#define DPTX_SDP_LEN	0x9
#define DPTX_SDP_SIZE	(9 * 4)
#define PARSE_EST_TIMINGS_FROM_BYTE3
//#define DPTX_DEBUG_REG

// Rounding up to the nearest multiple of a number
#define ROUND_UP_TO_NEAREST(numToRound, mult) ((((numToRound + (mult) - 1) / (mult)) * (mult)))

#include "avgen.h"
#include "reg.h"
#include "dbg.h"
#include "mst.h"

struct dptx;

/* Number of Clocks */
enum {
	GPCM_PLLDP = 0,
	HSION_PIX0,
	DPU_PIX0,
	HSION_PIX1,
	DPU_PIX1,
	HSION_AUD0,
	HSION_AUD1,
	DPTX_NUM_PIXEL_CLKS
};

/* The max rate and lanes supported by the core */
#define DPTX_MAX_LINK_RATE DPTX_PHYIF_CTRL_RATE_HBR3
#define DPTX_MAX_LINK_LANES 4
#define DPTX_MAX_VS_LEVEL 3
#define DPTX_MAX_PE_LEVEL 3
#define DPTX_MAX_VS_PE_SUM 3

/* The default rate and lanes to use for link training */
#define DPTX_DEFAULT_LINK_RATE DPTX_PHYIF_CTRL_RATE_HBR
#define DPTX_DEFAULT_LINK_LANES DPTX_MAX_LINK_LANES

/* The max number of streams supported */
#define DPTX_MAX_STREAM_NUMBER 4

/**
 * struct dptx_link - The link state.
 * @status: Holds the sink status register values.
 * @trained: True if the link is successfully trained.
 * @rate: The current rate that the link is trained at.
 * @lanes: The current number of lanes that the link is trained at.
 * @preemp_level: The pre-emphasis level used for each lane.
 * @vswing_level: The vswing level used for each lane.
 */
struct dptx_link {
	u8 status[DP_LINK_STATUS_SIZE];
	bool trained;
	u8 rate;
	u8 lanes;
	bool ef;
	bool ssc;
	bool fec;
	bool dsc;
	u8 preemp_level[4];
	u8 vswing_level[4];
	u8 bpc;
	u32 output_fmt;
};

enum established_timings {
	DMT_640x480_60hz,
	DMT_800x600_60hz,
	DMT_1024x768_60hz,
	NONE
};

/**
 * struct dptx_aux - The aux state
 * @sts: The AUXSTS register contents.
 * @data: The AUX data register contents.
 * @event: Indicates an AUX event ocurred.
 * @abort: Indicates that the AUX transfer should be aborted.
 */
struct dptx_aux {
	u32 sts;
	u32 data[4];
	atomic_t abort;
	ktime_t noreply_timeout;
};

struct sdp_header {
	u8 HB0;
	u8 HB1;
	u8 HB2;
	u8 HB3;
} __packed;

struct sdp_full_data {
	u8 en;
	u32 payload[9];
	u8 blanking;
	u8 cont;
} __packed;

struct adaptive_sync_sdp_data {
	struct sdp_header header;
	u8 payload[32];
	u8 size;
};

enum ALPM_Status {
	NOT_AVAILABLE = -1,
	DISABLED = 0,
	ENABLED = 1
};

enum ALPM_State {
	POWER_ON = 0,
	POWER_OFF = 1
};

struct edp_alpm {
	enum ALPM_Status status;
	enum ALPM_State state;
};


#define DPTX_HDCP_REG_DPK_CRC_ORIG	0x331c1169
#define DPTX_HDCP_MAX_AUTH_RETRY	10

/* Memory Resources */
enum mem_res_enum {
	DPTX = 0,
	SETUPID,
	CLKMGR,
	RSTMGR,
	VG,
	AG,
	PHYIF,
	MAX_MEM_IDX
};

/* Interrupt Resources */
enum irq_res_enum {
	MAIN_IRQ = 0,
	MAX_IRQ_IDX
};

enum hotplug_state {
	HPD_UNPLUG = 0,
	HPD_PLUG,
	HPD_IRQ,
};

enum link_training_status {
	LINK_TRAINING_UNKNOWN,
	LINK_TRAINING_SUCCESS,
	LINK_TRAINING_FAILURE,
	LINK_TRAINING_FAILURE_SINK,
};

/* Getting maximum resolution for telemetry */
struct dptx_max_res {
	u16 hdisplay;
	u16 vdisplay;
};

/* Preset resolutions for telemetry logging */
enum dptx_preset_res {
	DPTX_RES_1366_768,
	DPTX_RES_1440_900,
	DPTX_RES_1600_900,
	DPTX_RES_1920_1080,
	DPTX_RES_2560_1080,
	DPTX_RES_2560_1440,
	DPTX_RES_3440_1440,
	DPTX_RES_3840_2160,
	DPTX_RES_5120_2880,
	DPTX_RES_7680_4320,
	DPTX_RES_OTHER,
	DPTX_RES_MAX,
};

struct dp_stats_counters {
	u32 link_negotiation_failures;
	u32 edid_read_failures;
	u32 dpcd_read_failures;
	u32 edid_invalid_failures;
	u32 sink_count_invalid_failures;
	u32 link_unstable_failures;
	u32 max_res_counts[DPTX_RES_MAX];
	u32 fec_dsc_supported;
	u32 fec_dsc_not_supported;
	u32 connection_success;
	u32 connection_failure;
};

struct dp_hw_config {
	/* USB Type-C */
	enum pin_assignment pin_type;
	enum plug_orientation orient_type;
};

enum dptx_pds {
	HSION_DP_PD = 0,
	DPU_DP_PD,
	SSWRP_DPU_PD,
	MAX_DP_PD,
};

#define DPTX_AUDIO_CONNECT      (1UL)
#define DPTX_AUDIO_DISCONNECT   (-1UL)

#define AUDIO_COMPLETION_TIMEOUT_MS 3000

/**
 * struct dptx - The representation of the DP TX core
 * @mutex: dptx mutex
 * @base: Base address of the registers
 * @irq: IRQ number
 * @version: Contents of the IP_VERSION register
 * @max_rate: The maximum rate that the controller supports
 * @max_lanes: The maximum lane count that the controller supports
 * @dev: The struct device
 * @root: The debugfs root
 * @regset: The debugfs regset
 * @vparams: The video params to use
 * @aparams: The audio params to use
 * @hparams: The HDCP params to use
 * @waitq: The waitq
 * @shutdown: Signals that the driver is shutting down and that all
 *	    operations should be aborted.
 * @c_connect: Signals that a HOT_PLUG or HOT_UNPLUG has occurred.
 * @sink_request: Signals the a HPD_IRQ has occurred.
 * @rx_caps: The sink's receiver capabilities.
 * @edid: The sink's EDID.
 * @sdp_list: The array of SDP elements
 * @aux: AUX channel state for performing an AUX transfer.
 * @link: The current link state.
 * @multipixel: Controls multipixel configuration. 0-Single, 1-Dual, 2-Quad.
 */
struct dptx {
	struct mutex mutex; /* generic mutex for dptx */

	struct {
		u8 multipixel;
		u8 streams;
		bool gen2phy;
		bool dsc;
	} hwparams;

	void __iomem *base[MAX_MEM_IDX];
	int irq[MAX_IRQ_IDX];

	u32 version;

	u8 max_rate;
	u8 max_lanes;
	bool ycbcr420;
	u8 streams;
	bool mst;
	int active_mst_links;
	int active_mst_vc_payload;

	struct drm_dp_mst_topology_mgr mst_mgr;  //sahakyan

	bool cr_fail;// harutk need to remove

	u8 multipixel;
	u8 bstatus;

	bool ef_en;
	bool ssc_en;
	bool fec_en;
	bool dsc_en;
	bool edp;
	bool adaptive_sync;
	bool adaptive_sync_sdp;

	bool dummy_dtds_present;
	enum established_timings selected_est_timing;

	struct device *dev;
	struct dentry *root;
	struct debugfs_regset32 *regset[MAX_MEM_IDX];

	struct video_params vparams;
	struct audio_params aparams;
	struct audio_short_desc audio_desc;
	struct hdcp_device hdcp_dev;

	wait_queue_head_t waitq;

	atomic_t shutdown;
	atomic_t c_connect;
	atomic_t sink_request;

	struct edp_alpm alpm;

	u8 rx_caps[DPTX_RECEIVER_CAP_SIZE];
	u8 fec_caps;
	u8 dsc_caps;

	u8 *edid;
	u32 edid_size;
	u8 *edid_second;
#define DPTX_DEFAULT_EDID_BUFLEN (8 * EDID_LENGTH)

	struct sdp_full_data sdp_list[DPTX_SDP_NUM];
	struct dptx_aux aux;
	struct dptx_link link;

//	DRM Structures
	struct drm_device *drm;
	struct drm_connector *connector;
	struct drm_simple_display_pipe pipe;
	struct drm_display_mode current_mode;

#define XRES_DEF	640
#define YRES_DEF	480

#define XRES_MAX	8192
#define YRES_MAX	8192

//
//	REGMAP STRUCTURE
//
	#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
	struct dw_debugfs_hwv *debugfs_hwv;
	#endif

	struct regmap *regs[MAX_MEM_IDX];
	struct ctrl_regfields *ctrl_fields;
	struct clkmng_regfields *clkmng_fields;
	struct rstmng_regfields *rstmng_fields;
	struct ag_regfields *ag_fields;
	struct vg_regfields *vg_fields;

	/* Type-C Fields */
	enum plug_orientation typec_orientation;
	enum pin_assignment typec_pin_assignment;
	enum link_training_status typec_link_training_status;
	struct mutex typec_notification_lock;

	/* HPD State */
	enum hotplug_state hpd_current_state;
	struct mutex hpd_lock;
	struct mutex hpd_state_lock;

	/* HW Config */
	struct dp_hw_config hw_config;

	/* Workqueues */
	struct workqueue_struct *dp_wq;
	struct work_struct hpd_plug_work;
	struct work_struct hpd_unplug_work;
	struct work_struct hpd_irq_work;
	bool hpd_unplug_running;
	bool hpd_irq_unplug_running;

	/* Clocks */
	struct clk_bulk_data pixel_clks[DPTX_NUM_PIXEL_CLKS];

	/* USBDP Combo PHY */
	struct phy *dp_phy;

	/* Sink count and DFP count */
	int sink_count;
	int dfp_count;
	u8 dfp_info[DP_MAX_DOWNSTREAM_PORTS];
	bool branch_dev;

	/* DRM bridge */
	struct drm_bridge bridge;
	struct completion video_disable_done;
	bool video_enabled;

	/* DP power domains */
	struct device *pd_dev[MAX_DP_PD];

	/* Audio Notifier */
	struct blocking_notifier_head audio_notifier_head;
	void *audio_notifier_data;
	struct completion audio_disable_done;
	bool audio_enabled;
	bool sink_has_pcm_audio;

	/*
	 * DP link test mode
	 * This mode is used for DP PHY physical signal testing.
	 * No DP video, DP audio, or HDCP is enabled in this mode.
	 */
	bool link_test_mode;
	bool link_test_force_cr;
	bool link_test_force_cheq;

	/* AUX debug */
	bool aux_debug_en;

	/* Allow YCbCr 4:2:0 modes */
	bool ycbcr_420_en;

	/* DP stats/error counters */
	struct dp_stats_counters stats;

	/* DP struct for managing the maximum res */
	struct dptx_max_res dptx_max_res_store;

	/* AOSS SSR Notifier */
	struct notifier_block aoss_ssr_nb;
	bool ssr_in_progress;
	bool deferred_plug;
	bool deferred_irq;
	enum hotplug_state ssr_saved_hpd;
};

/*
 * Core interface functions
 */
int dptx_core_init(struct dptx *dptx);
void dptx_init_hwparams(struct dptx *dptx);
bool dptx_check_dptx_id(struct dptx *dptx);
void dptx_core_init_phy(struct dptx *dptx);
int dptx_core_program_ssc(struct dptx *dptx, bool sink_ssc);
bool dptx_sink_enabled_ssc(struct dptx *dptx);
void dptx_enable_ssc(struct dptx *dptx);

int dptx_core_deinit(struct dptx *dptx);
void dptx_soft_reset(struct dptx *dptx, u32 bits);
void dptx_soft_reset_all(struct dptx *dptx);
void dptx_phy_soft_reset(struct dptx *dptx);

irqreturn_t dptx_irq(int irq, void *dev);
irqreturn_t dptx_threaded_irq(int irq, void *dev);

void dptx_global_intr_en(struct dptx *dp);
void dptx_global_intr_dis(struct dptx *dp);

/*
 * PHY IF Control
 */
void dptx_phy_set_lanes(struct dptx *dptx, unsigned int num);
unsigned int dptx_phy_get_lanes(struct dptx *dptx);
void dptx_phy_set_lanes_powerdown_state(struct dptx *dptx, u8 state);
void dptx_phy_shutdown_unused_lanes(struct dptx *dptx);
void dptx_phy_set_rate(struct dptx *dptx, unsigned int rate);
unsigned int dwc_phy_get_rate(struct dptx *dptx);
int dptx_phy_wait_busy(struct dptx *dptx, unsigned int lanes);
void dptx_phy_set_pre_emphasis(struct dptx *dptx,
			       unsigned int lane,
			       unsigned int level);
void dptx_phy_set_vswing(struct dptx *dptx,
			 unsigned int lane,
			 unsigned int level);
void dptx_phy_set_pattern(struct dptx *dptx,
			  unsigned int pattern);
void dptx_phy_enable_xmit(struct dptx *dptx, unsigned int lane, bool enable);

int dptx_phy_rate_to_bw(unsigned int rate);
int dptx_bw_to_phy_rate(unsigned int bw);

/*
 * AUX Channel
 */

#define DPTX_AUX_TIMEOUT 2000

int dptx_read_bytes_from_i2c(struct dptx *dptx,
			     unsigned int device_addr,
			     u8 *bytes, u32 len);

int dptx_i2c_addr_only_read(struct dptx *dptx, unsigned int device_addr);

int dptx_write_bytes_to_i2c(struct dptx *dptx,
			    unsigned int device_addr,
			    u8 *bytes, u32 len);

int __dptx_read_dpcd(struct dptx *dptx, u32 addr, u8 *byte);
int __dptx_write_dpcd(struct dptx *dptx, u32 addr, u8 byte);

int __dptx_read_bytes_from_dpcd(struct dptx *dptx,
				unsigned int reg_addr,
				u8 *bytes, u32 len);

int __dptx_write_bytes_to_dpcd(struct dptx *dptx,
			       unsigned int reg_addr,
			       u8 *bytes, u32 len);

#ifndef DPTX_DEBUG_DPCD_CMDS
static inline int dptx_read_dpcd(struct dptx *dptx, u32 addr, u8 *byte)
{
	return __dptx_read_dpcd(dptx, addr, byte);
}

static inline int dptx_write_dpcd(struct dptx *dptx, u32 addr, u8 byte)
{
	return __dptx_write_dpcd(dptx, addr, byte);
}

static inline int dptx_read_bytes_from_dpcd(struct dptx *dptx,
					    unsigned int reg_addr,
					    u8 *bytes, u32 len)
{
	return __dptx_read_bytes_from_dpcd(dptx, reg_addr, bytes, len);
}

static inline int dptx_write_bytes_to_dpcd(struct dptx *dptx,
					   unsigned int reg_addr,
					   u8 *bytes, u32 len)
{
	return __dptx_write_bytes_to_dpcd(dptx, reg_addr, bytes, len);
}

#else
#define dptx_read_dpcd(_dptx, _addr, _byteptr) ({		\
	int _ret = __dptx_read_dpcd(_dptx, _addr, _byteptr);	\
	dptx_dbg(dptx, "%s: DPCD Read %s(0x%03x) = 0x%02x\n",	\
		 __func__, #_addr, _addr, *(_byteptr));		\
	_ret;							\
})

#define dptx_write_dpcd(_dptx, _addr, _byte) ({			\
	int _ret;						\
	dptx_dbg(dptx, "%s: DPCD Write %s(0x%03x) = 0x%02x\n",	\
		 __func__, #_addr, _addr, _byte);		\
	_ret = __dptx_write_dpcd(_dptx, _addr, _byte);		\
	_ret;							\
})

char *__bytes_str(u8 *bytes, unsigned int n);

#define dptx_read_bytes_from_dpcd(_dptx, _addr, _b, _len) ({	\
	int _ret;						\
	char *_str;						\
	_ret = __dptx_read_bytes_from_dpcd(_dptx,		\
					   _addr, _b, _len);	\
	_str = __bytes_str(_b, _len);				\
	dptx_dbg(dptx, "%s: Read %llu bytes from %s(0x%02x) = [ %s ]\n", \
		 __func__, (u64)_len, #_addr, _addr, _str);		\
	_ret;							\
})

#define dptx_write_bytes_to_dpcd(_dptx, _addr, _b, _len) ({	\
	int _ret;						\
	char *_str = __bytes_str(_b, _len);			\
	dptx_dbg(dptx, "%s: Writing %llu bytes to %s(0x%02x) = [ %s ]\n", \
		 __func__, (u64)_len, #_addr, _addr, _str);		\
	_ret = __dptx_write_bytes_to_dpcd(_dptx, _addr, _b, _len); \
	_ret;							\
})

#endif

/*
 * Link training
 */
int dptx_link_training(struct dptx *dptx);
int dptx_fast_link_training(struct dptx *dptx);
int dptx_link_check_status(struct dptx *dptx, u8 *irq_vector);
int dptx_set_link_configs(struct dptx *dptx, u8 rate, u8 lanes);
void dptx_update_link_status(struct dptx *dptx, enum link_training_status link_status);

/*
 * Register read and write functions
 */

static inline u32 __dptx_read_reg(struct dptx *dp, char const *func, int line, struct regmap *regm, u32 reg)
{
	u32 val = 0;

	regmap_read(regm, reg, &val);
	#ifdef DPTX_DEBUG_REG
		dptx_dbg(dp, "%s:%d: READ: addr=0x%05x data=0x%08x\n", func, line, reg, val);
	#endif

	return val;
}

#define dptx_read_reg(_dptx, _regm, _reg) ({ \
	__dptx_read_reg(_dptx, __func__, __LINE__, _regm, _reg); \
})

static inline void __dptx_write_reg(struct dptx *dp, char const *func, int line, struct regmap *regm, u32 reg, u32 val)
{
	#ifdef DPTX_DEBUG_REG
		dptx_dbg(dp, "%s:%d: WRITE: addr=0x%05x data=0x%08x\n", func, line, reg, val);
	#endif

	regmap_write(regm, reg, val);
}

#define dptx_write_reg(_dptx, _regm, _reg, _val) ({ \
	__dptx_write_reg(_dptx, __func__, __LINE__, _regm, _reg, _val); \
})

static inline u32 __dptx_read_regfield(struct dptx *dp, char const *func, int line, struct regmap_field *reg_field)
{
	u32 val = 0;

	regmap_field_read(reg_field, &val);

	#ifdef DPTX_DEBUG_REG
		dptx_dbg(dp, "%s:%d: READ: reg=0x%05x mask=0x%05x\n", func, line,
			 reg_field->reg, reg_field->mask);
		dptx_dbg(dp, "%s:%d: READ: shift=0x%05x data=0x%08x\n", func, line,
			 reg_field->shift, val);
	#endif

	return val;
}

#define dptx_read_regfield(_dptx, _reg_field) ({ \
	__dptx_read_regfield(_dptx, __func__, __LINE__, _reg_field); \
})

static inline void __dptx_write_regfield(struct dptx *dp, char const *func, int line, struct regmap_field *reg_field, u32 val)
{
	#ifdef DPTX_DEBUG_REG
		dptx_dbg(dp, "%s:%d: WRITE: reg=0x%05x mask=0x%05x\n", func,
			 line, reg_field->reg, reg_field->mask);
		dptx_dbg(dp, "%s:%d: WRITE: shift=0x%05x data=0x%08x\n", func,
			 line, reg_field->shift, val);
	#endif

	regmap_field_write(reg_field, val);
}

#define dptx_write_regfield(_dptx, _reg_field, _val) ({ \
	__dptx_write_regfield(_dptx, __func__, __LINE__, _reg_field, _val); \
})

/*
 * Wait functions
 */
#define dptx_wait(_dptx, _cond, _timeout)				\
	({								\
		int __retval;						\
		__retval = wait_event_interruptible_timeout(		\
			_dptx->waitq,					\
			((_cond) || (atomic_read(&_dptx->shutdown))),	\
			msecs_to_jiffies(_timeout));			\
		if (atomic_read(&_dptx->shutdown)) {			\
			__retval = -ESHUTDOWN;				\
		}							\
		else if (!__retval) {					\
			__retval = -ETIMEDOUT;				\
		}							\
		__retval;						\
	})

void dptx_notify(struct dptx *dptx);
void dptx_notify_shutdown(struct dptx *dptx);

/*
 * Debugfs
 */
void dptx_debugfs_init(struct dptx *dptx);
void dptx_debugfs_exit(struct dptx *dptx);

void dptx_fill_sdp(struct dptx *dptx, struct sdp_full_data *data);

struct dptx *dptx_get_handle(void);

int dptx_aux_rw_bytes(struct dptx *dptx,
		      bool rw,
		      bool i2c,
		      u32 addr,
		      u8 *bytes,
		      unsigned int len);
int dptx_read_bytes_from_i2c(struct dptx *dptx,
			     u32 device_addr,
			     u8 *bytes,
			     u32 len);
int dptx_write_bytes_to_i2c(struct dptx *dptx,
			    u32 device_addr,
			    u8 *bytes,
			    u32 len);

// To enable audio during hotplug
void dptx_audio_sdp_en(struct dptx *dptx, int enabled);

void dptx_audio_timestamp_sdp_en(struct dptx *dptx, int enabled);

void dptx_video_disable(struct dptx *dptx);

// Stats
void dptx_connection_result_update(struct dptx *dptx, bool success);

/* EDID Audio Data Block */
#define AUDIO_TAG		1
#define VIDEO_TAG		2
#define EDID_TAG_MASK		GENMASK(7, 5)
#define EDID_TAG_SHIFT		5
#define EDID_SIZE_MASK		GENMASK(4, 0)
#define EDID_SIZE_SHIFT		0

/* EDID HF-EEODB */
#define EDID_EEODB_TAG		7
#define EDID_EEODB_EXT_TAG	0x78

/* Established timing blocks */
#define ET1_800x600_60hz	BIT(0)
#define ET1_800x600_56hz	BIT(1)
#define ET1_640x480_75hz	BIT(2)
#define ET1_640x480_72hz	BIT(3)
#define ET1_640x480_67hz	BIT(4)
#define ET1_640x480_60hz	BIT(5)
#define ET1_720x400_88hz	BIT(6)
#define ET1_720x400_70hz	BIT(7)

#define ET2_1280x1024_75hz	BIT(0)
#define ET2_1024x768_75hz	BIT(1)
#define ET2_1024x768_70hz	BIT(2)
#define ET2_1024x768_60hz	BIT(3)
#define ET2_1024x768_87hz	BIT(4)
#define ET2_832x624_75hz	BIT(5)
#define ET2_800x600_75hz	BIT(6)
#define ET2_800x600_72hz	BIT(7)

#define ET3_1152x870_75hz	BIT(7)

#endif
