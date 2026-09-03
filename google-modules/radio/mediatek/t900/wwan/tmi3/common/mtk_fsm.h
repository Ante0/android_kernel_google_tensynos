/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_FSM_H__
#define __MTK_FSM_H__
#include <linux/pm_wakeup.h>

#include "mtk_dev.h"

#define FEATURE_CNT		(64)
#define FEATURE_QUERY_PATTERN	(0x49434343)

#define FEATURE_TYPE		GENMASK(3, 0)
#define FEATURE_VER		GENMASK(7, 4)

#define EVT_HANDLER_TIMEOUT	(15 * HZ)
#define EVT_MODE_BLOCKING	(0x01)
#define EVT_MODE_TOHEAD		(0x02)
#define EVT_MODE_BLOCKING_WITHOUT_TIMEOUT		(0x04)

#define FSM_EVT_RET_FAIL	(-1)
#define FSM_EVT_RET_ONGOING	(0)
#define FSM_EVT_RET_DONE	(1)

#define FSM_NB_NAME_MAX_LEN	(32)

#define DEVICE_CFG_SHIFT	24
#define DEVICE_CFG_REGION_MASK	0x3
#define FIRMWARE_CFG_SHIFT	16

#define HS3_FAIL		1
#define HS_INIT			0

#define FSM_HS_START_MASK	(FSM_F_SAP_HS_START | FSM_F_MD_HS_START)
#define FSM_HS2_DONE_MASK	(FSM_F_SAP_HS2_DONE | FSM_F_MD_HS2_DONE)

enum mtk_fsm_flag {
	FSM_F_DFLT		= 0,
	FSM_F_DL_PORT_CREATE	= BIT(0),
	FSM_F_DL_DA		= BIT(1),
	FSM_F_DL_JUMPBL		= BIT(2),
	FSM_F_DL_TIMEOUT	= BIT(3),
	FSM_F_SAP_HS_START	= BIT(4),
	FSM_F_SAP_HS2_DONE	= BIT(5),
	FSM_F_MD_HS_START	= BIT(6),
	FSM_F_MD_HS2_DONE	= BIT(7),
	FSM_F_MDEE_INIT		= BIT(8),
	FSM_F_MDEE_INIT_DONE	= BIT(9),
	FSM_F_MDEE_CLEARQ_DONE	= BIT(10),
	FSM_F_MDEE_ALLQ_RESET	= BIT(11),
	FSM_F_MDEE_MSG		= BIT(12),
	FSM_F_MDEE_RECV_OK	= BIT(13),
	FSM_F_MDEE_PASS		= BIT(14),
	FSM_F_FULL_REINIT	= BIT(15),
	FSM_F_DL_PL		= BIT(16),
	FSM_F_DL_FB		= BIT(17),
	FSM_F_MD_HS4_DONE	= BIT(18),
	FSM_F_DL_JUMPLK		= BIT(19),
	FSM_F_MD_REBOOT		= BIT(20),
	FSM_F_EXCEPT_INT	= BIT(21),
	FSM_F_LINK_EXCEPTION = BIT(22),
	/* Add your FSM_F_XX before this line.
	 * Don't forget to update below function when you add a FSM_F_XX.
	 * static bool mtk_fsm_is_md_event(struct mtk_fsm_evt *event)
	 */
};

enum mtk_fsm_state {
	FSM_STATE_INVALID = 0,
	FSM_STATE_OFF,
	FSM_STATE_ON,
	FSM_STATE_POSTDUMP,
	FSM_STATE_DOWNLOAD,
	FSM_STATE_BOOTUP,
	FSM_STATE_READY,
	FSM_STATE_EXCEPTION,
};

enum mtk_fsm_evt_id {
	FSM_EVT_DOWNLOAD = 0,
	FSM_EVT_POSTDUMP,
	FSM_EVT_STARTUP,
	FSM_EVT_LINKDOWN,
	FSM_EVT_BUS_ERR,
	FSM_EVT_COLD_RESUME,
	FSM_EVT_REINIT,
	FSM_EVT_MDEE,
	FSM_EVT_DEV_RESET_REQ,
	FSM_EVT_DEV_RM,
	FSM_EVT_DEV_ADD,
	FSM_EVT_SOFT_OFF,
	FSM_EVT_FB_RESET,
	FSM_EVT_PWROFF,
	FSM_EVT_DUMP,
	FSM_EVT_MD_REBOOT,
	FSM_EVT_MD_POWER_OFF,
	FSM_EVT_MD_POWER_OFF_TIMEOUT,
	FSM_EVT_TRM_NOTIFY,
	FSM_EVT_GNSS_ENABLE,
	FSM_EVT_GNSS_PORT_ENUM,
	FSM_EVT_GNSS_DISABLE,
	FSM_EVT_EXCEPTION_NOTIFY,

	/* Add your FSM_EVT_XX before this line.
	 * Don't forget to update below function when you add an EVT.
	 * static bool mtk_fsm_is_md_event(struct mtk_fsm_evt *event)
	 */
	FSM_EVT_MAX
};

enum mtk_fsm_prio {
	FSM_PRIO_0 = 0,
	FSM_PRIO_1 = 1,
	FSM_PRIO_MAX
};

struct mtk_fsm_param {
	enum mtk_fsm_state from;
	enum mtk_fsm_state to;
	enum mtk_fsm_evt_id evt_id;
	/* fsm flag provided by this event */
	enum mtk_fsm_flag fsm_flag;
	/* current full fsm flags
	 * for pre notifier, it is the full fsm flags without this event @fsm_flag
	 * for post notifier, it is the full fsm flags, including this event @fsm_flag
	 */
	unsigned int full_fsm_flags;
};

enum device_cfg {
	DEV_CFG_NORMAL = 0,
	DEV_CFG_MD_ONLY,
};

#define PORT_NAME_LEN 20

enum handshake_info_id {
	HS_ID_MD = 0,
	HS_ID_SAP,
	HS_ID_GNSS,
	HS_ID_MAX
};

enum support_runtime_feature_id {
	SUPPORT_RTFT_ID_MD_SBP_ID	= 0,
	SUPPORT_RTFT_ID_MD_DSD		= 1,
	SUPPORT_RTFT_ID_MD_TIMESYNC	= 2,
	SUPPORT_RTFT_ID_SKIP_MDEE_HS	= 3,
	SUPPORT_RTFT_ID_MAX
};

struct runtime_feature_info {
	u8 feature;
};

struct fsm_hs_info {
	unsigned char id;
	void *ctrl_port;
	char port_name[PORT_NAME_LEN];
	unsigned int mhccif_ch;
	unsigned int fsm_flag_hs1;
	unsigned int fsm_flag_hs2;
	unsigned int fsm_flag_hs4;
	/* the feature that the device should support */
	struct runtime_feature_info query_ft_set[FEATURE_CNT];
	/* the feature that the host has supported */
	struct runtime_feature_info supported_ft_set[FEATURE_CNT];
	/* runtime data from device need to be parsed by host */
	void *rt_data;
	unsigned int rt_data_len;
	unsigned char hs_status;
};

enum boot_sync_mode {
	BOOT_SYNC_MODE_POLLING = 0,
	BOOT_SYNC_MODE_INTERRUPT,
	BOOT_SYNC_MODE_MAX
};

struct mtk_fsm_cfg {
	enum boot_sync_mode sync_mode;
	unsigned int (*get_hs_done_flags)(struct mtk_md_dev *mdev, u32 dev_state);
};

struct mtk_md_fsm {
	struct mtk_md_dev *mdev;
	struct task_struct *fsm_handler;
	struct fsm_hs_info hs_info[HS_ID_MAX];
	unsigned int hs_done_flag;
	unsigned long t_flag;
	/* completion for event thread paused */
	struct completion paused;
	/* monitor fsm event process */
	struct timer_list evt_timer;
	/* monitor device state change in early bootup stage */
	struct timer_list dev_state_poller;
	struct delayed_work bootup_dump_work;
	struct delayed_work md_power_off_work;
	u32 last_dev_state;
	/* fsm current state & flag */
	enum mtk_fsm_state state;
	unsigned int fsm_flag;
	struct list_head evtq;
	/* protect evtq */
	spinlock_t evtq_lock;
	/* waitq for fsm blocking submit */
	wait_queue_head_t evt_waitq;
	/* notifiers before state transition */
	struct list_head pre_notifiers;
	/* notifiers after state transition */
	struct list_head post_notifiers;
	/* fsm kernel notifier block work */
	struct work_struct nb_work;
	struct list_head nb_list;
	/* fsm kernel notifier list mutex lock */
	struct mutex nb_list_mtx;
	struct mtk_fsm_cfg *cfg;
	struct wakeup_source *fsm_ws;
	enum mtk_user_id notifier_record;
};

struct mtk_fsm_evt {
	struct list_head entry;
	struct kref kref;
	struct mtk_md_dev *mdev;
	enum mtk_fsm_evt_id id;
	unsigned int fsm_flag;
	/* event handling status
	 * -1: fail,
	 * 0: on-going,
	 * 1: successfully
	 */
	int status;
	unsigned char mode;
	unsigned int len;
	void *data;
};

struct mtk_fsm_notifier {
	struct list_head entry;
	enum mtk_user_id id;
	void (*cb)(struct mtk_fsm_param *param, void *data);
	void *data;
	enum mtk_fsm_prio prio;
};

struct notifier_fsm_state {
	unsigned int to_state;
	unsigned int fsm_flag;
};

int mtk_fsm_init(struct mtk_md_dev *mdev);
int mtk_fsm_exit(struct mtk_md_dev *mdev);
int mtk_fsm_start(struct mtk_md_dev *mdev);
int mtk_fsm_pause(struct mtk_md_dev *mdev);
int mtk_fsm_notifier_register(struct mtk_md_dev *mdev,
			      enum mtk_user_id id,
			      void (*cb)(struct mtk_fsm_param *, void *data),
			      void *data,
			      enum mtk_fsm_prio prio,
			      bool is_pre);
int mtk_fsm_notifier_unregister(struct mtk_md_dev *mdev,
				enum mtk_user_id id);
int mtk_fsm_evt_submit(struct mtk_md_dev *mdev,
		       enum mtk_fsm_evt_id id,
		       enum mtk_fsm_flag flag,
		       void *data, unsigned int len,
		       unsigned char mode);
void mtk_fsm_supported_rtft_register(enum support_runtime_feature_id rtft_id,
				     int (*cb)(struct mtk_md_dev *mdev, void *rt_data));
int mtk_fsm_kernel_notifier_register(char *uname,
				     void (*cb)(struct notifier_fsm_state *state, void *priv_data),
				     void *priv_data);
int mtk_fsm_kernel_notifier_unregister(char *uname);
void mtk_fsm_kernel_notifier_cleanup(void);
int mtk_fsm_state_get(void *state, void *flag);
void mtk_fsm_cfg_info_update(struct mtk_md_dev *mdev, struct mtk_fsm_cfg *fsm_cfg);
int mtk_fsm_trigger_mdee(struct mtk_md_dev *mdev);

#endif /* __MTK_FSM_H__ */
