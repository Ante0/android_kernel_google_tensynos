// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pixel-specific UFS inline encryption support using FMP (Flash Memory
 * Protector) and the KDN (Key Distribution Network)
 *
 * Copyright 2020 Google LLC
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <trace/hooks/ufshcd.h>
#include <ufs/ufshcd.h>

#include <linux/gsa/gsa_kdn.h>

#include <drivers/ufs/core/ufshcd-crypto.h>

#include "ufs-pixel-crypto.h"
#include "ufs-pixel.h"

/*
 * Determine the number of pending commands by counting the bits in the SCSI
 * device budget maps.
 */
static u32 ufshcd_pending_cmds(struct ufs_hba *hba)
{
	struct scsi_device *sdev;
	u32 pending = 0;

	shost_for_each_device(sdev, hba->host)
		pending += sbitmap_weight(&sdev->budget_map);

	return pending;
}

/*
 * Block new UFS requests from being issued, and wait for any outstanding UFS
 * requests to complete. Must be paired with ufshcd_resume_io().
 */
static void ufshcd_block_io(struct ufs_hba *hba)
{
	ktime_t deadline = ktime_add_ms(ktime_get(), 5 * 1000);

	/*
	 * If ufshcd_block_io() is called before the tag set has been
	 * initialized then we know that no I/O is ongoing.
	 */
	if (!hba->host->tag_set.tags)
		return;

	blk_mq_quiesce_tagset(&hba->host->tag_set);

	while (ufshcd_pending_cmds(hba)) {
		if (ktime_after(ktime_get(), deadline)) {
			dev_err(hba->dev, "%s waited too long\n", __func__);
			return;
		}
		io_schedule_timeout(msecs_to_jiffies(20));
	}
}

static void ufshcd_resume_io(struct ufs_hba *hba)
{
	if (!hba->host->tag_set.tags)
		return;

	blk_mq_unquiesce_tagset(&hba->host->tag_set);
}

/* Program crypto CSR per spec. TODO: (b/380945847) move logic to GSA */
static void pixel_ufs_set_crypto_csr(struct ufs_hba *hba, unsigned int slot)
{
	#define CRYPTO_CFG_0		0x500
	#define CRYPTO_CFG_SIZE		0x80
	u32 cryptocfg_offset = CRYPTO_CFG_0 + slot * CRYPTO_CFG_SIZE;

	/* Configure CRYPTO_CFG on DWORD 16 and DWORD 17 */
	ufshcd_writel(hba, 0x80000008, cryptocfg_offset + 0x40);
	ufshcd_writel(hba, 0, cryptocfg_offset + 0x44);
}

static int pixel_ufs_keyslot_program(struct blk_crypto_profile *profile,
				     const struct blk_crypto_key *key,
				     unsigned int slot)
{
	struct ufs_hba *hba = container_of(profile,
					struct ufs_hba, crypto_profile);
	struct pixel_ufs *ufs = to_pixel_ufs(hba);
	int err;

	dev_info(ufs->dev,
		 "kdn: programming keyslot %u with %u-byte wrapped key\n",
		 slot, key->size);

	/*
	 * Keyslot operations require the HSIO_S to be out of deep hibernate.
	 * We use pm_runtime_get_sync() to wake the HSIO_S from Deep H8.
	 */
	pm_runtime_get_sync(hba->dev);

	/*
	 * This hardware doesn't allow any encrypted I/O at all while a keyslot
	 * is being modified.
	 */
	ufshcd_block_io(hba);

	err = gsa_kdn_program_key(ufs->gsa_dev, slot, key->raw, key->size);
	if (err)
		dev_err(ufs->dev, "kdn: failed to program key; err=%d\n", err);

	/* program and evict calls are synchronized by blk_crypto_profile::lock */
	ufs->crypto_slot_in_use[slot] = true;
	pixel_ufs_set_crypto_csr(hba, slot);

	ufshcd_resume_io(hba);

	pm_runtime_put_sync(hba->dev);

	return err;
}

static int pixel_ufs_keyslot_evict(struct blk_crypto_profile *profile,
				   const struct blk_crypto_key *key,
				   unsigned int slot)
{
	struct ufs_hba *hba = container_of(profile,
					struct ufs_hba, crypto_profile);
	struct pixel_ufs *ufs = to_pixel_ufs(hba);
	int err;

	dev_info(ufs->dev, "kdn: evicting keyslot %u\n", slot);

	/*
	 * TODO: Initialize PM framework prior to KDN ops (b/461905110)
	 * We don't call pm_runtime_get_sync() here to prevent a system crash
	 * during the driver's probe sequence, as the device is already active
	 * but the PM framework is not yet fully initialized.
	 */

	/*
	 * This hardware doesn't allow any encrypted I/O at all while a keyslot
	 * is being modified.
	 */
	ufshcd_block_io(hba);

	err = gsa_kdn_program_key(ufs->gsa_dev, slot, NULL, 0);
	if (err)
		dev_err(ufs->dev, "kdn: failed to evict key; err=%d\n", err);

	ufs->crypto_slot_in_use[slot] = false;

	ufshcd_resume_io(hba);

	return err;
}

static int pixel_ufs_derive_sw_secret(struct blk_crypto_profile *profile,
				       const u8 *eph_key, size_t eph_key_size,
				       u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE])
{
	struct ufs_hba *hba = container_of(profile,
					struct ufs_hba, crypto_profile);
	struct pixel_ufs *ufs = to_pixel_ufs(hba);
	int ret;

	dev_info(ufs->dev,
		 "kdn: deriving %u-byte raw secret from %zu-byte wrapped key\n",
		 BLK_CRYPTO_SW_SECRET_SIZE, eph_key_size);

	/*
	 * Keyslot operations require the HSIO_S to be out of deep hibernate.
	 * We use pm_runtime_get_sync() to wake the HSIO_S from Deep H8.
	 */
	pm_runtime_get_sync(hba->dev);
	ret = gsa_kdn_derive_raw_secret(ufs->gsa_dev, sw_secret,
					BLK_CRYPTO_SW_SECRET_SIZE,
					eph_key, eph_key_size);
	pm_runtime_put_sync(hba->dev);

	if (ret != BLK_CRYPTO_SW_SECRET_SIZE) {
		dev_err(ufs->dev, "kdn: failed to derive raw secret; ret=%d\n",
			ret);
		/*
		 * gsa_kdn_derive_raw_secret() returns -EIO on "bad key" but
		 * upper layers expect -EINVAL.  Just always return -EINVAL.
		 */
		return -EINVAL;
	}
	return 0;
}

static const struct blk_crypto_ll_ops pixel_ufs_crypto_ops = {
	.keyslot_program	= pixel_ufs_keyslot_program,
	.keyslot_evict		= pixel_ufs_keyslot_evict,
	.derive_sw_secret	= pixel_ufs_derive_sw_secret,
};

static void pixel_ufs_release_gsa_device(void *_ufs)
{
	struct pixel_ufs *ufs = _ufs;

	put_device(ufs->gsa_dev);
}

/*
 * Get the GSA device from the device tree and save a pointer to it in the UFS
 * host struct.
 */
static int pixel_ufs_find_gsa_device(struct pixel_ufs *ufs)
{
	struct device_node *np;
	struct platform_device *gsa_pdev;

	np = of_parse_phandle(ufs->dev->of_node, "gsa-device", 0);
	if (!np) {
		dev_warn(ufs->dev,
			 "gsa-device phandle not found in UFS device tree node\n");
		return -ENODEV;
	}
	gsa_pdev = of_find_device_by_node(np);
	of_node_put(np);

	if (!gsa_pdev) {
		dev_err(ufs->dev,
			"gsa-device phandle doesn't refer to a device\n");
		return -ENODEV;
	}
	ufs->gsa_dev = &gsa_pdev->dev;
	return devm_add_action_or_reset(ufs->dev, pixel_ufs_release_gsa_device,
					ufs);
}

static void pixel_ufs_crypto_restore_keys(void *unused, struct ufs_hba *hba,
					  int *err)
{
	*err = 0;
}

/* Initialize UFS inline encryption support. */
int pixel_ufs_crypto_init(struct ufs_hba *hba)
{
	struct pixel_ufs *ufs = to_pixel_ufs(hba);
	int err;

	err = pixel_ufs_find_gsa_device(ufs);
	if (err == -ENODEV)
		goto disable;
	if (err)
		return err;

	if (ufs->pixel_ops->crypto_init)
		err = ufs->pixel_ops->crypto_init(hba);
	if (err == -ENODEV)
		goto disable;
	if (err)
		return err;

	err = register_trace_android_rvh_ufs_reprogram_all_keys(
				pixel_ufs_crypto_restore_keys, NULL);
	if (err)
		return err;

	/* Advertise crypto support to ufshcd-core. */
	hba->caps |= UFSHCD_CAP_CRYPTO;

	/* Advertise crypto quirks to ufshcd-core. */

	/*
	 * We need to override the blk_keyslot_manager, firstly in order to
	 * override the UFSHCI standand blk_crypto_ll_ops with operations that
	 * program/evict wrapped keys via the KDN, and secondly in order to
	 * declare wrapped key support rather than standard key support.
	 */
	hba->quirks |= UFSHCD_QUIRK_CUSTOM_CRYPTO_PROFILE;

	/*
	 * This host controller doesn't support the standard
	 * CRYPTO_GENERAL_ENABLE bit in REG_CONTROLLER_ENABLE.  Instead it just
	 * always has crypto support enabled.
	 */
	hba->quirks |= UFSHCD_QUIRK_BROKEN_CRYPTO_ENABLE;

	/* Advertise crypto capabilities to the block layer. */
	err = devm_blk_crypto_profile_init(hba->dev, &hba->crypto_profile,
								KDN_SLOT_NUM);
	if (err)
		return err;
	hba->crypto_profile.ll_ops = pixel_ufs_crypto_ops;
	/*
	 * The PRDT entries accept 16-byte IVs, but currently the driver passes
	 * the DUN through ufshcd_lrb::data_unit_num which is 8-byte.  8 bytes
	 * is enough for upper layers, so for now just use that as the limit.
	 */
	hba->crypto_profile.max_dun_bytes_supported = 8;
	hba->crypto_profile.key_types_supported = BLK_CRYPTO_KEY_TYPE_HW_WRAPPED;
	hba->crypto_profile.dev = ufs->dev;
	hba->crypto_profile.modes_supported[BLK_ENCRYPTION_MODE_AES_256_XTS] =
		CRYPTO_DATA_UNIT_SIZE;

	dev_info(ufs->dev,
		 "enabled inline encryption support with wrapped keys\n");
	return 0;

disable:
	/*
	 * If the GSA support for wrapped keys seems to be missing, then fall
	 * back to disabling crypto support and continuing with driver probe.
	 * Attempts to use wrapped keys will fail, but any other use of UFS will
	 * continue to work.
	 */
	dev_warn(hba->dev, "disabling inline encryption support\n");
	hba->caps &= ~UFSHCD_CAP_CRYPTO;
	return 0;
}

int pixel_ufs_crypto_resume(struct ufs_hba *hba)
{
	struct pixel_ufs *ufs = to_pixel_ufs(hba);
	int ret;
	unsigned int i;

	/*
	 * Keyslot operations require the HSIO_S to be out of deep hibernate.
	 * We use pm_runtime_get_sync() to wake the HSIO_S from Deep H8.
	 */
	pm_runtime_get_sync(hba->dev);

	ret = gsa_kdn_restore_keys(ufs->gsa_dev);

	if (ret) {
		dev_err(hba->dev, "gsa_kdn_restore_keys failed, ret=%d\n", ret);
		goto release_resources;
	}

	for (i = 0; i < KDN_SLOT_NUM; i++) {
		if (ufs->crypto_slot_in_use[i])
			pixel_ufs_set_crypto_csr(hba, i);
	}

release_resources:
	pm_runtime_put_sync(hba->dev);

	return ret;
}
