/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _GOOGLE_MBA_INTERNAL_H
#define _GOOGLE_MBA_INTERNAL_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/types.h>

#define MBA_NUM_CLIENT 6

struct google_mba_host_raw_irq_state {
	u32 msg;
	u32 ack;
};

struct google_mba_host_irq_state {
	// The `lock` protects `content`.
	spinlock_t lock;
	struct google_mba_host_raw_irq_state content;
};

struct google_mba_host {
	// Field(s) initialize by platform driver.
	int irq;

	// Internal field(s) below.
	struct google_mba_host_irq_state irq_state;

	struct dentry *debugfs;
	u32 debug_client;
};

struct google_mba_client_irq_state {
	// The `lock` protects `content`.
	spinlock_t lock;
	u32 content;
};

struct google_mba_client {
	// Field(s) initialize by platform driver.
	int irq;

	// Field(s) initialized by google_mba driver.
	int idx;

	// Internal field(s) below.
	char *name;
	struct google_mba_client_irq_state irq_state;

	struct dentry *debugfs;
};

struct google_mba {
	// Field(s) initialize by platform driver.
	struct device *dev;
	void __iomem *base;

	// Internal field(s) below.
	struct dentry *debugfs;

	struct google_mba_host host;
	struct google_mba_client clients[MBA_NUM_CLIENT];
};

#define host_to_google_mba(h)                                                  \
	({                                                                     \
		struct google_mba_host *__host = (h);                          \
		container_of(__host, struct google_mba, host);                 \
	})
#define client_to_google_mba(c)                                                \
	({                                                                     \
		struct google_mba_client *__client = (c);                      \
		container_of(__client, struct google_mba,                      \
			     clients[__client->idx]);                          \
	})

#define MBA_HOST_BASE 0x00000
#define MBA_HOST_ENABLE (MBA_HOST_BASE + 0x0000)
#define MBA_HOST_CONFIG (MBA_HOST_BASE + 0x0010)
#define MBA_HOST_IRQ_TRIG_BASE (MBA_HOST_BASE + 0x0020)
#define MBA_HOST_IRQ_TRIG(n) (MBA_HOST_IRQ_TRIG_BASE + 0x04 * (n))
#define MBA_HOST_IRQ_TRIG_SET_CLIENT_IRQ BIT(0)
#define MBA_HOST_IRQ_TRIG_ENABLE_CLIENT_AUTOACK BIT(8)
#define MBA_HOST_IRQ_STATUS_MSG (MBA_HOST_BASE + 0x0220)
#define MBA_HOST_IRQ_STATUS_ACK (MBA_HOST_BASE + 0x0230)
#define MBA_HOST_IRQ_MASK_MSG (MBA_HOST_BASE + 0x0240)
#define MBA_HOST_IRQ_MASK_ACK (MBA_HOST_BASE + 0x0250)

#define MBA_TOP_BASE 0x10000
#define MBA_ID_PART_NUM (MBA_TOP_BASE + 0x00)
#define MBA_ID_CONFIG (MBA_TOP_BASE + 0x04)
#define MBA_ID_VERSION (MBA_TOP_BASE + 0x08)
#define MBA_MBA_ID (MBA_TOP_BASE + 0x0C)
#define MBA_NUM_MSG_REG_BASE (MBA_TOP_BASE + 0x10)
#define MBA_NUM_MSG_REG(n) (MBA_NUM_MSG_REG_BASE + 0x04 * (n))

#define MBA_CLIENT_BASE(n) (0x20000 + 0x1000 * (n))
#define MBA_CLIENT_IRQ_CONFIG(n) (MBA_CLIENT_BASE(n) + 0x00)
#define MBA_CLIENT_IRQ_CONFIG_SET_HOST_IRQ BIT(0)
#define MBA_CLIENT_IRQ_CONFIG_ENABLE_HOST_AUTOACK BIT(8)
#define MBA_CLIENT_IRQ_CONFIG_MASK_MSG_INT BIT(16)
#define MBA_CLIENT_IRQ_CONFIG_MASK_ACK_INT BIT(24)
#define MBA_CLIENT_IRQ_STATUS(n) (MBA_CLIENT_BASE(n) + 0x04)
#define MBA_CLIENT_MBOX_SHADOW(n) (MBA_CLIENT_BASE(n) + 0x08)
#define MBA_CLIENT_COMMON_MSG_BASE(n) (MBA_CLIENT_BASE(n) + 0x10)

static inline u32 google_mba_readl(struct google_mba *mba, ptrdiff_t offset)
{
	return readl(mba->base + offset);
}

static inline void google_mba_writel(struct google_mba *mba, u32 val,
				     ptrdiff_t offset)
{
	writel(val, mba->base + offset);
}

int google_mba_debugfs_init(void);
void google_mba_debugfs_exit(void);

int google_mba_init(struct google_mba *mba);
void google_mba_exit(struct google_mba *mba);

#endif /* _GOOGLE_MBA_INTERNAL_H */
