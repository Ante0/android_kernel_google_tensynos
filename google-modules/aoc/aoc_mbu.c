// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google Whitechapel AoC RDO library
 *
 * Copyright (c) 2024 Google LLC
 */

#include "aoc.h"
#include "aoc_firmware.h"
#include "aoss_ssr.h"
#include "aoc-interface.h"
#include <linux/debugfs.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <soc/google/google_gtc.h>
#include <aoss-ssr-notifier/aoss_ssr_notifier.h>
#include <soc/google/goog-mba-gdmc-iface.h>

#define STATE_TABLE_0_ENABLE_STATE_OFFSET 0x4060
#define STATE_TABLE_1_ENABLE_STATE_OFFSET 0x40c4
#define LPCM_CFG_OFFSET 0x4100
#define LPCM_START_OFFSET 0x4104
#define LPCM_STATUS_OFFSET 0x4108

#define NON_WAKING_CELLS_NUM 2

#define DEBUGFS_AOC_DRAM_ARENA "aoc_dram_arena"
#define DEBUGFS_AOC_DRAM_ARENA_OFFSET "aoc_dram_arena_offset"
#define DEBUGFS_AOC_DRAM_ARENA_SIZE "aoc_dram_arena_size"

#define SC_LIVENESS_CHECK_INTERVAL_MS 500
#define SC_LIVENESS_CHECK_TIMEOUT_MS 100

static bool aoc_disable_ssr_sequence;
module_param(aoc_disable_ssr_sequence, bool, 0644);
MODULE_PARM_DESC(aoc_disable_ssr_sequence, "Disable AOC SSR sequence.");

static bool aoc_run_partial_ssr_sequence;
module_param(aoc_run_partial_ssr_sequence, bool, 0644);
MODULE_PARM_DESC(aoc_run_partial_ssr_sequence, "Run partial AOC SSR sequence.");

static bool aoc_abort_ssr_if_cores_on;
module_param(aoc_abort_ssr_if_cores_on, bool, 0644);
MODULE_PARM_DESC(aoc_abort_ssr_if_cores_on, "Abort SSR if any core is not in WFI.");

static bool aoc_enable_sc_liveness_check;
module_param(aoc_enable_sc_liveness_check, bool, 0644);
MODULE_PARM_DESC(aoc_enable_sc_liveness_check, "Enable SC liveness check every 500ms.");

const char * const control_channels[] = {
	"control",
	"f1_control",
	"sc_control",
};
const int control_channels_size = ARRAY_SIZE(control_channels);

const long long dvfs_freqs[] = {
	720000000,
	960000000,
	1440000000,
	1920000000,
	2880000000,
	3840000000,
	5760000000,
	8070000000,
};

static const char * const core_names[] = {
	"NA",
	"A32",
	"F1",
	"HF0_1",
	"HF0_2",
	"SC",
	"NA",
	"NA",
};

static const int core_names_size = ARRAY_SIZE(core_names);

enum mbu_power_domain {
	PD_AMBSS = 0,
	PD_SSWRP_PG,
	PD_TOT
};

struct non_waking_reg {
	void __iomem *mbox_vaddr;
	size_t offset;
	uint32_t mask;
};

struct non_waking_data {
	struct non_waking_reg *non_waking_reg;
	size_t count;
};

struct aoc_mbu_prvdata {
	struct device *aoc_dev;
	struct resource *aoc_lpcm_resource;
	void *lpcm_virt;
	struct ssr_data_s *ssr_data;
	struct gdmc_iface *gdmc_iface;
	struct non_waking_data non_waking_data;
	struct delayed_work sc_liveness_work;
};

/* Register dump structure for aarch32 targets. */
struct gdmc_mba_aarch32_register_dump {
	/* bit 0 -> boolean value indicating whether the dump is valid */
	/* bit 1 -> boolean value indicating whether the core power is on */
	/* bit 2 -> boolean value indicating whether the core is halted */
	/* bits 31:3 -> reserved */
	uint32_t flags;
	/* R0-R12 */
	uint32_t gprs[13];
	/* stack pointer */
	uint32_t sp;
	/* link register */
	uint32_t lr;
	/* program counter */
	uint32_t pc;
	/* current program status register */
	uint32_t cpsr;
	/* saved program status register */
	/* (invalid if PE is in User/System mode) */
	uint32_t spsr;
	/* data fault status register */
	uint32_t dfsr;
	/* data fault address register */
	uint32_t dfar;
	/* instruction fault status register */
	uint32_t ifsr;
	/* instruction fault address register */
	uint32_t ifar;
};

struct aoc_mbu_prvdata *mbu_prvdata;

static bool pg_torn_down;

static void sc_liveness_check_work(struct work_struct *work)
{
	struct aoc_mbu_prvdata *mbu_prv =
		container_of(work, struct aoc_mbu_prvdata, sc_liveness_work.work);
	struct aoc_prvdata *prvdata = dev_get_drvdata(mbu_prv->aoc_dev);
	struct aoc_service_dev *sc_service_dev = NULL;
	struct CMD_SYS_VERSION_GET version_get = { 0 };
	int ret, i;

	if (aoc_state != AOC_STATE_ONLINE || !aoc_enable_sc_liveness_check)
		return;

	/* Find sc_control service */
	for (i = 0; i < prvdata->total_services; i++) {
		struct aoc_service_dev *sdev = service_dev_at_index(prvdata, i);
		const char *name = aoc_service_name(sdev->service);

		if (name && !strcmp(name, "sc_control")) {
			sc_service_dev = sdev;
			break;
		}
	}

	if (!sc_service_dev)
		goto reschedule;

	AocCmdHdrSet(&version_get.parent.parent, CMD_SYS_VERSION_GET_ID, sizeof(version_get));
	version_get.parent.core = 0;

	ret = aoc_service_write_timeout(sc_service_dev,
		(void *)&version_get, sizeof(version_get),
		msecs_to_jiffies(SC_LIVENESS_CHECK_TIMEOUT_MS));
	if (ret != sizeof(version_get)) {
		dev_err(mbu_prv->aoc_dev,
			"SC liveness check: failed writing to 'sc_control': %d\n", ret);
		trigger_aoc_ssr(true, "SC unresponsive (write timeout)");
		return;
	}

	ret = aoc_service_read_timeout(sc_service_dev,
		(void *)&version_get, sizeof(version_get),
		msecs_to_jiffies(SC_LIVENESS_CHECK_TIMEOUT_MS));
	if (ret != sizeof(version_get)) {
		dev_err(mbu_prv->aoc_dev,
			"SC liveness check: failed reading from 'sc_control': %d\n", ret);
		trigger_aoc_ssr(true, "SC unresponsive (read timeout)");
		return;
	}

reschedule:
	schedule_delayed_work(&mbu_prv->sc_liveness_work,
		msecs_to_jiffies(SC_LIVENESS_CHECK_INTERVAL_MS));
}

static inline void *aoc_lpcm_translate(u32 offset)
{
	if (mbu_prvdata->lpcm_virt == NULL)
		return NULL;

	if (offset > resource_size(mbu_prvdata->aoc_lpcm_resource))
		return NULL;

	return mbu_prvdata->lpcm_virt + offset;
}

bool aoc_release_from_reset(struct aoc_prvdata *prvdata)
{
	u32 lpm_status_value;
	unsigned long timeout;

	iowrite32(0x1, aoc_lpcm_translate(STATE_TABLE_0_ENABLE_STATE_OFFSET));
	iowrite32(0x0, aoc_lpcm_translate(STATE_TABLE_1_ENABLE_STATE_OFFSET));
	iowrite32(0x1, aoc_lpcm_translate(LPCM_CFG_OFFSET));
	iowrite32(0x1, aoc_lpcm_translate(LPCM_START_OFFSET));

	timeout = jiffies + (2 * HZ);
	lpm_status_value = ioread32(aoc_lpcm_translate(LPCM_STATUS_OFFSET));
	while (time_before(jiffies, timeout)) {
		lpm_status_value = ioread32(aoc_lpcm_translate(LPCM_STATUS_OFFSET));
		if ((lpm_status_value & 0x9F) == 0x90)
			return true;
		msleep(100);
	}
	return false;
}

static ssize_t aoc_dram_write(struct file *file, const char __user *buf, size_t count,
				  loff_t *ppos)
{

	struct aoc_prvdata *prvdata = file_inode(file)->i_private;
	struct device *dev = prvdata->dev;
	size_t available_sz = prvdata->dram_size - prvdata->dram_arena_debugfs_offset;
	void *arena;

	if (count > available_sz) {
		dev_err(dev, "debugfs invalid size, requested: %zu available: %zu\n",
			count, available_sz);
		return -ENOMEM;
	}

	arena = aoc_dram_translate(prvdata, prvdata->dram_arena_debugfs_offset);
	if (!arena)
		return -EINVAL;

	if (copy_from_user(arena, buf, count))
		return -EFAULT;

	return count;
}

static ssize_t aoc_dram_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{

	struct aoc_prvdata *prvdata = file_inode(file)->i_private;
	struct device *dev = prvdata->dev;
	size_t available_sz = prvdata->dram_size - prvdata->dram_arena_debugfs_offset;
	void *arena;

	if (count > available_sz) {
		dev_err(dev, "debugfs invalid size, requested: %zu available: %zu\n",
			count, available_sz);
		return -ENOMEM;
	}

	arena = aoc_dram_translate(prvdata, prvdata->dram_arena_debugfs_offset);
	if (!arena)
		return -EINVAL;

	if (copy_to_user(buf, arena, count))
		return -EFAULT;

	return count;
}

static const struct file_operations aoc_dram_arena_fops = {
	.owner = THIS_MODULE,
	.write = aoc_dram_write,
	.read = aoc_dram_read,
};

static int aoc_dram_offset_set(void *data, u64 val)
{
	struct aoc_prvdata *prvdata = data;

	if (prvdata->dram_size < val)
		return -EINVAL;

	prvdata->dram_arena_debugfs_offset = val;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(aoc_dram_offset_fops, NULL, aoc_dram_offset_set, "%llx\n");

static int aoc_dram_arena_size_get(void *data, u64 *val)
{
	struct aoc_prvdata *prvdata = data;

	*val = prvdata->dram_size - prvdata->dram_arena_debugfs_offset;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(aoc_dram_arena_size_fops, aoc_dram_arena_size_get, NULL, "%llx\n");

static long ssr_notification_event;

static ssize_t trigger_ssr_notification_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	if (ssr_notification_event < 0 ||
			ssr_notification_event >= AOSS_SSR_NOTIFICATION_EVENT_TOT) {
		dev_err(dev, "No event exists for id [%ld], use 0 to %d\n",
			ssr_notification_event, AOSS_SSR_NOTIFICATION_EVENT_TOT - 1);
		return sysfs_emit(buf, "0\n");
	}

	aoss_ssr_notify(ssr_notification_event);
	return sysfs_emit(buf, "1\n");
}

static ssize_t trigger_ssr_notification_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;

	rc = kstrtol(buf, 0, &ssr_notification_event);
	if (rc != 0)
		return -EINVAL;

	return count;
}

static DEVICE_ATTR_RW(trigger_ssr_notification);

static struct attribute *aoc_mbu_attrs[] = {
	&dev_attr_trigger_ssr_notification.attr,
	NULL
};

ATTRIBUTE_GROUPS(aoc_mbu);

static void gdmc_callback(void *reg_dump, unsigned int reg_dump_len, void *priv_data)
{
	struct aoc_prvdata *prvdata = priv_data;
	struct device *dev = prvdata->dev;

	dev_notice(dev, "AoC received interrupt from GDMC\n");
	if (reg_dump) {
		int i;
		struct gdmc_mba_aarch32_register_dump *gdmc_dump;

		gdmc_dump = (struct gdmc_mba_aarch32_register_dump *)reg_dump;
		dev_info(dev, "sp:   %#x lr:   %#x pc:   %#x\n",
			gdmc_dump->sp, gdmc_dump->lr, gdmc_dump->pc);
		dev_info(dev, "cpsr: %#x spsr: %#x dfsr: %#x\n",
			gdmc_dump->cpsr, gdmc_dump->spsr, gdmc_dump->dfsr);
		dev_info(dev, "dfar: %#x ifsr: %#x ifar: %#x\n",
			gdmc_dump->dfar, gdmc_dump->ifsr, gdmc_dump->ifar);
		for (i = 0; i < 12; i++)
			dev_info(dev, "r%d:   %#x\n", i, gdmc_dump->gprs[i]);
	}
	else
		dev_info(dev, "reg_dump from GDMC is null");

	trigger_aoc_ssr(false, "AOC watchdog interrupt from GDMC");
}

int platform_specific_probe(struct platform_device *pdev, struct aoc_prvdata *prvdata)
{
	struct device *dev = &pdev->dev;
	struct device_node *aoc_node;
	int rc = 0;
	int num_elems = 0;
	int i;

	mbu_prvdata = devm_kzalloc(dev, sizeof(*mbu_prvdata), GFP_KERNEL);
	if (!mbu_prvdata)
		return -ENOMEM;

	mbu_prvdata->aoc_dev = dev;
	mbu_prvdata->aoc_lpcm_resource =
		platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpcm");

	INIT_DELAYED_WORK(&mbu_prvdata->sc_liveness_work, sc_liveness_check_work);

	if (!mbu_prvdata->aoc_lpcm_resource) {
		dev_err(dev,
			"failed to get memory resources for lpcm\n");
		return -ENOMEM;
	}

	mbu_prvdata->lpcm_virt = devm_ioremap_resource(dev, mbu_prvdata->aoc_lpcm_resource);
	if (IS_ERR(mbu_prvdata->lpcm_virt))
		return -ENOMEM;

	prvdata->dram_arena_debugfs_offset = 0;

	debugfs_create_file(DEBUGFS_AOC_DRAM_ARENA, 0644, NULL, prvdata,
			    &aoc_dram_arena_fops);
	debugfs_create_file(DEBUGFS_AOC_DRAM_ARENA_OFFSET, 0x644, NULL, prvdata,
			    &aoc_dram_offset_fops);
	debugfs_create_file(DEBUGFS_AOC_DRAM_ARENA_SIZE, 0x444, NULL, prvdata,
			    &aoc_dram_arena_size_fops);

	mbu_prvdata->ssr_data = aoss_ssr_init(prvdata);
	if (IS_ERR(mbu_prvdata->ssr_data)) {
		dev_err(dev, "Error initializing AOSS SSR");
		rc = PTR_ERR(mbu_prvdata->ssr_data);
	}

	mbu_prvdata->gdmc_iface = gdmc_iface_get(dev);
	if (IS_ERR(mbu_prvdata->gdmc_iface))
		dev_err(dev, "failed to get gdmc interface %ld\n",
			PTR_ERR(mbu_prvdata->gdmc_iface));
	else
		gdmc_register_aoc_reset_notifier(mbu_prvdata->gdmc_iface, gdmc_callback, prvdata);

	rc = sysfs_create_groups(&dev->kobj, aoc_mbu_groups);
	if (rc)
		return rc;
	aoc_node = dev->of_node;
	num_elems = of_count_phandle_with_args(aoc_node, "mbox-non-waking",
		"#non-waking-cells");
	if (num_elems <= 0)
		goto non_waking_err;
	mbu_prvdata->non_waking_data.non_waking_reg = devm_kcalloc(dev, num_elems,
		sizeof(struct non_waking_reg), GFP_KERNEL);
	if (!mbu_prvdata->non_waking_data.non_waking_reg)
		return -ENOMEM;
	for (i = 0; i < num_elems; i++) {
		struct of_phandle_args args;
		struct resource mbox_resource;
		int ret = of_parse_phandle_with_args(aoc_node,
			"mbox-non-waking", "#non-waking-cells", i, &args);
		if (ret) {
			dev_err(dev,
				"failed to find mbox-non-waking in the device tree\n");
				goto non_waking_err;
		} else if (args.args_count != NON_WAKING_CELLS_NUM) {
			of_node_put(args.np);
			dev_err(dev, "non-waking cells mismatch\n");
			goto non_waking_err;
		}
		rc = of_address_to_resource(args.np, 0, &mbox_resource);
		of_node_put(args.np);
		if (rc != 0) {
			dev_err(dev, "Failed to get mbox resource");
			goto non_waking_err;
		} else {
			mbu_prvdata->non_waking_data.non_waking_reg[i].offset =
				args.args[0];
			mbu_prvdata->non_waking_data.non_waking_reg[i].mask =
				args.args[1];
			mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr =
				devm_ioremap(dev, mbox_resource.start,
				resource_size(&mbox_resource));
			if (IS_ERR(mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr)) {
				dev_err(dev, "Failed to ioremap mailbox controller at %#x\n",
					(unsigned int)mbox_resource.start);
				mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr = NULL;
				goto non_waking_err;
			}
			mbu_prvdata->non_waking_data.count++;
		}
	}
	prvdata->print_wakeup_irq = true;
	return rc;
non_waking_err:
	mbu_prvdata->non_waking_data.count = 0;
	return rc;
}

void request_aoc_on(struct aoc_prvdata *p, bool status)
{
	/* Nothing needed as AOSS_AMBSS and SSWRP_AOSS_PG are on in mission mode */
}

int wait_for_aoc_status(struct aoc_prvdata *p, bool status)
{
	return 0;
}

static void notify_pcie_ssr_entry(void)
{
	/* TODO(alexiacobucci): implement notification */
}

static void notify_pcie_ssr_exit(void)
{
	/* TODO(alexiacobucci): implement notification */
}

static bool aoss_cores_in_wfi(struct aoc_prvdata *prvdata)
{
	int i;
	void __iomem *vaddr;
	bool cores_in_wfi = true;
	u32 psm_status;
	static const u32 psm_base_addr[] = {
		0xb824000,	/* AON_DSP */
		0xb825000,	/* AON_SC */
		0xd124000,	/* AMB_A32 */
		0xd125000,	/* AMB_DSP0 */
		0xd126000,	/* AMB_DSP1 */
		0xd127000	/* L2C */
	};
	static const u32 psm_expected_status[] = {
		0x91,
		0x91,
		0x91,
		0x91,
		0x91,
		0x11
	};
	static const u32 psm_status_offset[] = {
		0x108,
		0x108,
		0x108,
		0x108,
		0x108,
		0x128
	};

	for (i = 0; i < ARRAY_SIZE(psm_base_addr); i++) {
		vaddr = ioremap(psm_base_addr[i], 0x1000);
		psm_status = ioread32(vaddr + psm_status_offset[i]);
		iounmap(vaddr);
		if (psm_status != psm_expected_status[i]) {
			dev_err(prvdata->dev, "Core %d in unexpected power state: got %#x, expected %#x",
				i, psm_status, psm_expected_status[i]);
			cores_in_wfi = false;
		}
	}

	return cores_in_wfi;
}

int aoc_watchdog_restart(struct aoc_prvdata *prvdata,
		struct aoc_module_parameters *aoc_module_params)
{
	int ret = 0;
	int max_wfi_attempts = 10;

	if (aoc_disable_ssr_sequence)
		return -1;

	if (!aoss_cores_in_wfi(prvdata) && aoc_abort_ssr_if_cores_on) {
		dev_err(prvdata->dev, "Cores not in WFI, aborting SSR");
		return -1;
	}

	/* Notify PCIe driver to switch to SOC reserved memory */
	notify_pcie_ssr_entry();

	/* Notify AMBSS users to release power domain vote */
	aoss_ssr_notify(AOSS_SSR_AMBSS_DOWN);

	/* Instruct CPM to prepare for SSR */
	ret = aoss_ssr_send_command(mbu_prvdata->ssr_data,
		SSR_SERVICE_CMD_PREPARE, AOSS_SSR_STAGE_AP);
	if (ret < 0) {
		dev_err(prvdata->dev, "Failed AOSS SSR prepare, ret=%d\n", ret);
		return ret;
	}

	/* We give some time for AOC cores to quiesce */
	while (!aoss_cores_in_wfi(prvdata) && (max_wfi_attempts-- > 0))
		msleep(200);

	if (aoc_run_partial_ssr_sequence) {
		ret = aoss_ssr_send_command(mbu_prvdata->ssr_data,
			SSR_SERVICE_CMD_TRIGGER, AOSS_SSR_TYPE_PARTIAL);
		if (ret < 0)
			dev_info(prvdata->dev,
				"Failed partial SSR sequence, triggering full sequence\n");
		else
			goto ssr_complete;
	}

	pg_torn_down = true;

	aoss_ssr_notify(AOSS_SSR_PG_DOWN);

	ret = aoss_ssr_send_command(mbu_prvdata->ssr_data,
		SSR_SERVICE_CMD_TRIGGER, AOSS_SSR_TYPE_FULL);
	if (ret < 0) {
		dev_err(prvdata->dev, "Failed extended AOSS SSR sequence\n");
		if (*(aoc_module_params->aoc_panic_on_ssr_failure)) {
			// Add a delay to allow any coredump files to dump to disk (b/513594924)
			msleep(5000);
			panic("AOSS SSR failed");
		}

		return ret;
	}

ssr_complete:
	dev_info(prvdata->dev, "AOSS SSR completed\n");
	aoss_ssr_send_command(mbu_prvdata->ssr_data, SSR_SERVICE_CMD_CLEANUP, 0 /* unused */);

	if (pg_torn_down)
		aoss_ssr_notify(AOSS_SSR_PG_UP);
	aoss_ssr_notify(AOSS_SSR_AMBSS_UP);
	notify_pcie_ssr_exit();
	pg_torn_down = false;

	return 0;
}

void trigger_aoc_ramdump(struct aoc_prvdata *prvdata)
{
	struct mbox_chan *channel = prvdata->mbox_channels[prvdata->aoc_coredump_mbox].channel;

	dev_notice(prvdata->dev, "Attempting to force AoC coredump\n");

	mbox_send_message(channel, NULL);
}
EXPORT_SYMBOL_GPL(trigger_aoc_ramdump);

void aoc_configure_ssmt(struct platform_device *pdev) {}

int configure_iommu_interrupts(struct device *dev, struct device_node *iommu_node,
		struct aoc_prvdata *prvdata)
{
	return 0;
}

int configure_watchdog_interrupt(struct platform_device *pdev, struct aoc_prvdata *prvdata)
{
	return 0;
}

void platform_specific_remove(struct platform_device *pdev, struct aoc_prvdata *prvdata)
{
	cancel_delayed_work_sync(&mbu_prvdata->sc_liveness_work);
	debugfs_remove(debugfs_lookup(DEBUGFS_AOC_DRAM_ARENA_SIZE, NULL));
	debugfs_remove(debugfs_lookup(DEBUGFS_AOC_DRAM_ARENA_OFFSET, NULL));
	debugfs_remove(debugfs_lookup(DEBUGFS_AOC_DRAM_ARENA, NULL));

	aoss_ssr_cleanup(mbu_prvdata->ssr_data);

	sysfs_remove_groups(&pdev->dev.kobj, aoc_mbu_groups);
}

void configure_crash_interrupts(struct aoc_prvdata *prvdata, bool enable)
{
}

u32 aoc_chip_get_revision(void)
{
	u32 product_id;
	u32 major;
	u32 minor;

	if (aoc_read_soc_compatible(mbu_prvdata->aoc_dev, &product_id, &major, &minor))
		return 0;

	return ((major & 0xF) << 4) | (minor & 0xF);
}

u32 aoc_chip_get_type(void)
{
	return 0;
}

size_t platform_specific_get_dvfs_freq(char *buff)
{
	size_t ret = 0;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(dvfs_freqs); i++)
		ret += sysfs_emit_at(buff, ret, "%lld ", dvfs_freqs[i]);
	ret += sysfs_emit_at(buff, ret, "\n");
	return ret;
}
EXPORT_SYMBOL_GPL(platform_specific_get_dvfs_freq);

u32 aoc_chip_get_product_id(void)
{
	u32 product_id;
	u32 major;
	u32 minor;

	if (aoc_read_soc_compatible(mbu_prvdata->aoc_dev, &product_id, &major, &minor))
		return 0;

	return product_id;
}

int platform_specific_aoc_online(void)
{
	aoss_ssr_notify(AOSS_SSR_ONLINE);

	if (aoc_enable_sc_liveness_check)
		schedule_delayed_work(&mbu_prvdata->sc_liveness_work,
			msecs_to_jiffies(SC_LIVENESS_CHECK_INTERVAL_MS));
	return 0;
}

int platform_specific_aoc_offline(void)
{
	return 0;
}

void platform_specific_aoc_core_suspend(void)
{
	int i;

	if (!mbu_prvdata)
		return;
	for (i = 0; i < mbu_prvdata->non_waking_data.count; i++) {
		u32 current_mask, new_mask;

		if (mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr == NULL)
			continue;

		current_mask = ioread32(mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr +
			mbu_prvdata->non_waking_data.non_waking_reg[i].offset);
		new_mask = current_mask & ~(mbu_prvdata->non_waking_data.non_waking_reg[i].mask);
		iowrite32(new_mask, mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr +
			mbu_prvdata->non_waking_data.non_waking_reg[i].offset);
	}

	cancel_delayed_work_sync(&mbu_prvdata->sc_liveness_work);
}

void platform_specific_aoc_core_resume(void)
{
	int i;

	if (!mbu_prvdata)
		return;
	for (i = 0; i < mbu_prvdata->non_waking_data.count; i++) {
		u32 current_mask, new_mask;

		if (mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr == NULL)
			continue;

		current_mask = ioread32(mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr +
			mbu_prvdata->non_waking_data.non_waking_reg[i].offset);
		new_mask = current_mask | mbu_prvdata->non_waking_data.non_waking_reg[i].mask;
		iowrite32(new_mask, mbu_prvdata->non_waking_data.non_waking_reg[i].mbox_vaddr +
			mbu_prvdata->non_waking_data.non_waking_reg[i].offset);
	}

	if (aoc_enable_sc_liveness_check)
		schedule_delayed_work(&mbu_prvdata->sc_liveness_work,
			msecs_to_jiffies(SC_LIVENESS_CHECK_INTERVAL_MS));
}

u64 aoc_get_timer_ticks(void)
{
	return goog_gtc_get_counter();
}

void aoc_print_core_boot_breadcrumbs(struct aoc_prvdata *prvdata)
{
	int i;

	for (i = 0; i < core_names_size; i++) {
		if (strcmp(core_names[i], "NA") != 0) {
			uint8_t breadcrumb = _aoc_fw_core_boot_breadcrumbs(prvdata->dram_virt, i);

			dev_info(prvdata->dev, "%s boot breadcrumb: %#x, FIQ set: %d\n",
				core_names[i], breadcrumb, breadcrumb >= 0x80);
		}
	}
}

void aoc_init_core_boot_breadcrumbs(struct aoc_prvdata *prvdata)
{
	int i;

	for (i = 0; i < core_names_size; i++) {
		_aoc_init_core_boot_breadcrumbs(prvdata->dram_virt, i);
	}
}
