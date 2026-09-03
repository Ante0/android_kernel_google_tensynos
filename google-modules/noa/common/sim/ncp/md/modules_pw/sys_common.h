#ifndef SYS_COMMON_H
#define SYS_COMMON_H

// This header includes the required APIs with same
// interfaces from different path depending on the
// target platform (driver mode or pigweed).

#ifdef linux
// driver mode
#include <linux/ip.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/taskstats.h>
#include <linux/device.h>
#include <net/ipv6.h>
#include "md/mediatek/noa_md.h"
#include "md/mediatek/noa_md_tx_data.h"
#include "md/mediatek/noa_md_rx_data.h"
#include "md/mediatek/noa_md_wrapper_dpmaif.h"
#include "md/mediatek/t900/noa_md_mtk_priv.h"
#include "mediatek/ncp_md.h"
#include "mediatek/ncp_md_tx_data.h"
#include "mediatek/ncp_md_rx_data.h"
#else
// pigweed
#include <cstdint>
#include "linux_port/bitops.h"
#include "linux_port/container_of.h"
#include "linux_port/device.h"
#include "linux_port/dma_mapping.h"
#include "linux_port/jiffies.h"
#include "linux_port/list.h"
#include "linux_port/mutex.h"
#include "linux_port/spinlock.h"
#include "linux_port/tasklet.h"
#include "linux_port/timer.h"
#include "linux_port/workqueue.h"
#include "md/mediatek/noa_md_hif_interrupt.h"
#include "md/mediatek/noa_md_hif_ring.h"
#include "pw_chrono/system_clock.h"
#include "pw_thread/sleep.h"
#include "sys_log.h"
#endif

#include "common/compiler.h"
#include "common/core.h"
#include "common/md/mediatek/noa_md_mtk_common.h"
#include "common/modem_ring_id.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "ring_mgmt/ring_manager.h"

#ifdef linux
#define ATOMIC_ADD(send_drb_cnt, to_submit_cnt) \
		atomic_add(send_drb_cnt, to_submit_cnt);
#define ATOMIC_READ(to_submit_cnt) \
		atomic_read(to_submit_cnt);
#define ATOMIC_SUB(sub_drb_cnt, to_submit_cnt) \
		atomic_sub(sub_drb_cnt, to_submit_cnt)

#define NOA_MD_INIT_DELAYED_WORK(work_, func_, data_)  \
	INIT_DELAYED_WORK(work_, func_);
#else
#define ATOMIC_ADD(send_drb_cnt, to_submit_cnt) \
		std::atomic_fetch_add(to_submit_cnt, send_drb_cnt);
#define ATOMIC_READ(to_submit_cnt) \
		std::atomic_load(to_submit_cnt);
#define ATOMIC_SUB(sub_drb_cnt, to_submit_cnt) \
		std::atomic_fetch_sub(to_submit_cnt, sub_drb_cnt);
#define NOA_MD_INIT_DELAYED_WORK(work_, func_, data_)  \
	INIT_DELAYED_WORK(work_, func_, data_);

#define jiffies jiffies()
#endif

#endif /* SYS_COMMON_H */
