// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include <linux/debugfs.h>
#include <asm/byteorder.h>
#include <linux/uaccess.h>
#include "dptx.h"
#include "api/api.h"
#include "video_bridge.h"
#include "clock_mng.h"
#include "rst_mng.h"
#include "debugfs/audio_bridge.h"
#include "debugfs/clock_mng.h"
#include "debugfs/video_bridge.h"
#include "regmaps/audio_bridge.h"
#include "regmaps/rst_mng.h"


static const struct debugfs_reg32 dptx_regs[];
static const struct debugfs_reg32 clkmng_regs[];
static const struct debugfs_reg32 audiogen_regs[];
static const struct debugfs_reg32 videogen_regs[];
static const struct debugfs_reg32 setupid_regs[];
static const struct debugfs_reg32 phyif_regs[];
static const int dptx_regs_size;
static const int clkmng_regs_size;
static const int audiogen_regs_size;
static const int videogen_regs_size;
static const int setupid_regs_size;
static const int phyif_regs_size;
static int aux_addr;
static u32 dpcd_addr;
static u8 aux_type;
static int aux_size;
/**
 * DOC: DEBUGFS Interface
 *
 * Top level:
 *
 * pixel_mode_sel [rw] - Pixel mode selection. Write to this to set pixel mode.
 *
 * regdump [r] - Read this to see the values of all the core
 * registers.
 *
 * Link attributes:
 *
 * link/lane_count [r] - The current lanes in use. This will be 1, 2,
 * or 4.
 *
 * link/rate [r] - The current rate. 0 - RBR, 1 - HBR, 2 - HBR2, 3 -
 * HBR3.
 *
 * link/retrain [w] - Write to this to retrain the link. The value to
 * write is the desired rate and lanes separated by a space. For
 * example to retrain the link at 4 lanes at RBR write "0 4" to this
 * file.
 *
 * link/status [r] - Shows the status of the link.
 *
 * link/trained [r] - True if the link training was successful.
 *
 * audio/inf_type [rw] - The audio interface type. 0 - I2S, 1 - SPDIF
 *
 * audio/num_ch [rw] - Number of audio channels. 1 -8
 *
 * audio/data_width [rw] - The audio input data width. 16 - 24
 *
 * video/pix_enc [rw] - The video pixel encoding
 * 0 - RGB, 1 - YCBCR420, 2 - YCBCR422, 3 - YCBCR444, 4 - YONLY, 5 - RAW
 *
 * video/bpc [rw] - The video bits per component.  6, 8, 10, 12, 16
 *
 * video/pattern [rw] - Video pattern.
 * 0 - TILE, 1 - RAMP, 2 - COLRAMP, 3 - CHESS
 *
 * video/dynamic_range [rw] - Video dynamic range. 0 - CEA, 1 - VESA
 *
 * video/colorimetry [rw] - Video colorimetry.
 * This parametr is necassary in case of pixel encoding
 * YCBCR4:2:2, YCBCR4:4:4. 1 - YCBCR4:2:2, 2 - ITU-R BT.709
 *		   *
 * video/refresh_rate [rw] - Video mode refresh rate.
 * This parametr is neccassary in case of
 * CEA video modes 8, 9, 12, 13, 23, 24, 27, 28
 *
 * video/video_format [rw] - Video format. 0 - CEA, 1 - CVT, 2 - DMT
 *
 * video/vic - Video mode number, should also specify video format
 *
 * aux_type [rw] - Type of AUX transaction. 0 - Native AUX Native Read, 1 - I2C
 * over AUX read, 2 - AUX Native Write, 3 - I2C over AUx write.
 *
 * aux_addr [rw] - Address used for AUX transaction data.
 *
 * aux_size [rw] - Size of aux transaction in bytes.
 *
 * aux [rw] - Data for AUX transaction.
 *
 * edid [rw] - EDID data.
 *
 * edid_size [rw] - Number of EDID blocks to read.
 *
 * mute [rw] - Mute the Audio. 1- mute, 0 unmute.
 *
 * sdp [rw] - SDP data payload to be transferred during blanking period.
 *
 */

//-----------------------------------------------------------------------------------------------
// Global Reset Test
//-----------------------------------------------------------------------------------------------
static int dptx_global_reset_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	struct rstmng_regfields *rstmng_fields;

	rstmng_fields = dptx->rstmng_fields;

	mutex_lock(&dptx->mutex);
	seq_printf(s, "Global Reset Field: %u",
			dptx_read_regfield(dptx, rstmng_fields->field_global_rst));

	mutex_unlock(&dptx->mutex);
	return 0;
}

static int dptx_global_reset_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_global_reset_show, inode->i_private);
}

static ssize_t dptx_global_reset_write(struct file *file,
				     const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}

	rst_avp(dptx);

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static const struct file_operations dptx_global_reset_fops = {
	.open	   = dptx_global_reset_open,
	.write	  = dptx_global_reset_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

//--------------------------------------------------------------------------------------------



/*
 * Link Status
 */
static int dptx_link_status_show(struct seq_file *s, void *unused)
{
	int i;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);

	seq_printf(s, "trained = %d\n", dptx->link.trained);
	seq_printf(s, "rate = %d\n", dptx->link.rate);
	seq_printf(s, "lanes = %d\n", dptx->link.lanes);

	if (!dptx->link.trained)
		goto done;

	for (i = 0; i < dptx->link.lanes; i++) {
		seq_printf(s, "preemp and vswing level [%d] = %d, %d\n",
			   i, dptx->link.preemp_level[i],
			   dptx->link.vswing_level[i]);
	}

done:
	mutex_unlock(&dptx->mutex);
	return 0;
}

static int dptx_link_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_link_status_show, inode->i_private);
}

static const struct file_operations dptx_link_status_fops = {
	.open		= dptx_link_status_open,
	.write		= NULL,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * bstatus
 */
static int dptx_bstatus_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;

	seq_printf(s, "%d", dptx->bstatus);

	return 0;
}

static int dptx_bstatus_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_bstatus_show, inode->i_private);
}

static const struct file_operations dptx_bstatus_fops = {
	.open = dptx_bstatus_open,
	.write = NULL,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * Link Retrain
 */
static int dptx_link_retrain_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	seq_printf(s, "trained = %d\n", dptx->link.trained);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_link_retrain_write(struct file *file,
				       const char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	int retval = 0;
	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;
	u8 buf[32];
	u8 rate;
	u8 lanes;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	rate = buf[0] - '0';
	lanes = buf[1] - '0';

	retval = dptx_link_retrain(dptx, rate, lanes);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_link_retrain_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_link_retrain_show, inode->i_private);
}

static const struct file_operations dptx_link_retrain_fops = {
	.open		= dptx_link_retrain_open,
	.write		= dptx_link_retrain_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Aux
 */

static ssize_t dptx_aux_write(struct file *file,
			      const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	int retval = 0;
	u8 *buf;
	struct drm_dp_aux_msg *aux_msg = NULL;
	int i = 0;
	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	buf =  kmalloc(sizeof(count), GFP_KERNEL);
	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}

	for (i = 0; i < count; i++)
		buf[i] = buf[i] - '0';

	aux_msg = kmalloc(sizeof(*aux_msg), GFP_KERNEL);
	if (!aux_msg) {
		retval = -EFAULT;
		goto done;
	}
	memset(aux_msg, 0, sizeof(*aux_msg));
	switch (aux_type) {
	case 2:
		aux_msg->request = DP_AUX_NATIVE_WRITE;
		break;
	case 3:
		aux_msg->request = DP_AUX_I2C_WRITE;
		break;
	}
	aux_msg->address = aux_addr;
	aux_msg->buffer = buf;
	aux_msg->size = count;

	retval = dptx_aux_transfer(dptx, aux_msg);
	if (retval)
		goto done;

	retval = count;
done:
	kfree(buf);
	kfree(aux_msg);
	mutex_unlock(&dptx->mutex);
	return retval;
}

static ssize_t dptx_aux_read(struct file *file,
			     char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	int retval = 0;
	u8 *aux_buf = NULL;
	struct drm_dp_aux_msg *aux_msg = NULL;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	if (aux_size <= 0)
		return -EINVAL;

	mutex_lock(&dptx->mutex);
	aux_buf = kmalloc(aux_size, GFP_KERNEL);

	if (!aux_buf) {
		retval = -EFAULT;
		goto done;
	}
	aux_msg =  kmalloc(sizeof(*aux_msg), GFP_KERNEL);
	if (!aux_msg) {
		retval = -EFAULT;
		goto done;
	}

	memset(aux_buf, 0, sizeof(*aux_buf));
	memset(aux_msg, 0, sizeof(*aux_msg));

	switch (aux_type) {
	case 0:
		aux_msg->request = DP_AUX_NATIVE_READ;
		break;
	case 1:
		aux_msg->request = DP_AUX_I2C_READ;
		break;
	}
	aux_msg->address = aux_addr;
	aux_msg->buffer = aux_buf;
	aux_msg->size = aux_size;

	retval = dptx_aux_transfer(dptx, aux_msg);
	if (retval)
		goto done;

	if (copy_to_user(ubuf, aux_msg->buffer, aux_size) != 0) {
		retval = -EFAULT;
		goto done;
	}
	retval = count;
done:
	kfree(aux_buf);
	kfree(aux_msg);
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_aux_show(struct seq_file *s, void *unused)
{
	return 0;
}

static int dptx_aux_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_aux_show, inode->i_private);
}

static const struct file_operations dptx_aux_fops = {
	.open		= dptx_aux_open,
	.write	  = dptx_aux_write,
	.read	   = dptx_aux_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

/*
 * Audio sdp
 */

static ssize_t dptx_audio_sdp_write(struct file *file,
				    const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	int retval = 0;
	u8 buf[40];
	struct sdp_full_data sdp_full_data;
	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;
	int i;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));
	memset(&sdp_full_data, 0, sizeof(sdp_full_data));

	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	for (i = 0; i < 39; i++)
		buf[i] = buf[i] - '0';

	memcpy(&sdp_full_data, (struct sdp_full_data *)buf,
	       min_t(size_t, sizeof(buf) - 1, count));
	if (sdp_full_data.en)
		dptx_sdp_enable(dptx, sdp_full_data.payload,
				sdp_full_data.blanking,
				sdp_full_data.cont);
	else
		dptx_sdp_disable(dptx, sdp_full_data.payload);
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_audio_sdp_show(struct seq_file *s, void *unused)
{
	return 0;
}

static int dptx_audio_sdp_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_audio_sdp_show, inode->i_private);
}

static const struct file_operations dptx_audio_sdp_fops = {
	.open	   = dptx_audio_sdp_open,
	.write	  = dptx_audio_sdp_write,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

/*
 * Audio interface type
 */
static int dptx_audio_inf_type_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 inf_type;

	mutex_lock(&dptx->mutex);
	inf_type = dptx_get_audio_inf_type(dptx);
	seq_printf(s, "%d\n", inf_type);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_audio_inf_type_write(struct file *file,
					 const char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 inf_type;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;
	struct audio_params *aparams;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &inf_type) < 0) {
		retval = -EINVAL;
		goto done;
	}
	aparams = &dptx->aparams;
	aparams->inf_type = inf_type;
	dptx_audio_inf_type_change(dptx);
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_audio_inf_type_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_audio_inf_type_show, inode->i_private);
}

static const struct file_operations dptx_audio_inf_type_fops = {
	.open	   = dptx_audio_inf_type_open,
	.write	  = dptx_audio_inf_type_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Audio input data width
 */
static int dptx_audio_data_width_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 data_width;

	mutex_lock(&dptx->mutex);
	data_width = dptx_get_audio_data_width(dptx);
	seq_printf(s, "%d\n", data_width);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_audio_data_width_write(struct file *file,
					   const char __user *ubuf,
					   size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 data_width;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;
	struct audio_params *aparams;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &data_width) < 0) {
		retval = -EINVAL;
		goto done;
	}
	aparams = &dptx->aparams;
	aparams->data_width = data_width;
	dptx_audio_data_width_change(dptx);
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_audio_data_width_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_audio_data_width_show, inode->i_private);
}

static const struct file_operations dptx_audio_data_width_fops = {
	.open	   = dptx_audio_data_width_open,
	.write	  = dptx_audio_data_width_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Audio number channels
 */
static int dptx_audio_num_ch_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 audio_num_ch;

	mutex_lock(&dptx->mutex);
	audio_num_ch = dptx_get_audio_num_ch(dptx);
	seq_printf(s, "%d\n", audio_num_ch);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_audio_num_ch_write(struct file *file,
				       const char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 audio_num_ch;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;
	struct audio_params *aparams;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &audio_num_ch) < 0) {
		retval = -EINVAL;
		goto done;
	}
	aparams = &dptx->aparams;
	aparams->num_channels = audio_num_ch;
	dptx_audio_num_ch_change(dptx);
	dptx_en_audio_channel(dptx, audio_num_ch, 1);

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_audio_num_ch_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_audio_num_ch_show, inode->i_private);
}

static const struct file_operations dptx_audio_num_ch_fops = {
	.open	   = dptx_audio_num_ch_open,
	.write	  = dptx_audio_num_ch_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Audio mute
 */
static int dptx_audio_mute_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	struct audio_params *aparams;

	aparams = &dptx->aparams;

	mutex_lock(&dptx->mutex);
	seq_printf(s, "%d\n", aparams->mute);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_audio_mute_write(struct file *file,
				     const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 audio_mute;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;
	struct audio_params *aparams;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}

	if (kstrtou8(buf, 10, &audio_mute) < 0) {
		retval = -EINVAL;
		goto done;
	}
	aparams = &dptx->aparams;
	aparams->mute = audio_mute;
	dptx_audio_mute(dptx);
	if (audio_mute == 1)
		dptx_en_audio_channel(dptx, aparams->num_channels, 0);
	else
		dptx_en_audio_channel(dptx, aparams->num_channels, 1);

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_audio_mute_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_audio_mute_show, inode->i_private);
}

static const struct file_operations dptx_audio_mute_fops = {
	.open	   = dptx_audio_mute_open,
	.write	  = dptx_audio_mute_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Audio Generator On/Off
 */
static int dptx_audio_generator_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint32_t ag_config0, enabled_value;

	mutex_lock(&dptx->mutex);
	ag_config0 = ag_read(dptx, EXT_AG_CONFIG0);
	enabled_value = get(ag_config0, AG_CONFIG0_ENABLE_MASK);

	seq_printf(s, "%d\n", enabled_value);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_audio_generator_write(struct file *file,
				     const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 enable;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}

	if (kstrtou8(buf, 10, &enable) < 0) {
		retval = -EINVAL;
		goto done;
	}

	if (enable)
		dptx_enable_audio_generator(dptx);
	else
		dptx_disable_audio_generator(dptx);

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_audio_generator_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_audio_generator_show, inode->i_private);
}
static const struct file_operations dptx_audio_generator_fops = {
	.open	   = dptx_audio_generator_open,
	.write	  = dptx_audio_generator_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * VIC
 */
static int dptx_vic_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 vic;

	mutex_lock(&dptx->mutex);
	vic = dptx_get_video_mode(dptx);
	seq_printf(s, "%d\n", vic);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_vic_write(struct file *file,
			      const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[4];
	u8 vic;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &vic) < 0) {
		retval = -EINVAL;
		goto done;
	}
	retval = dptx_set_video_mode(dptx, vic);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_vic_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vic_show, inode->i_private);
}

static const struct file_operations dptx_vic_fops = {
	.open	   = dptx_vic_open,
	.write	  = dptx_vic_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * edid
 */

static ssize_t dptx_edid_read(struct file *file,
			      char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	int retval = 0;
	int edid_size;
	u8 *edid_buf = NULL;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);

	edid_size = dptx_get_edid_size(dptx);
	if (edid_size < 0) {
		retval = edid_size;
		goto done;
	}
	edid_buf = kmalloc(edid_size, GFP_KERNEL);
	if (!edid_buf) {
		retval = -EFAULT;
		goto done;
	}
	memset(edid_buf, 0, sizeof(*edid_buf));
	retval = dptx_get_edid(dptx, edid_buf, edid_size);
	if (retval)
		goto done;
	if (copy_to_user(ubuf, edid_buf, edid_size) != 0) {
		retval = -EFAULT;
		goto done;
	}
	retval = count;
done:
	kfree(edid_buf);
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_edid_show(struct seq_file *s, void *unused)
{
	return 0;
}

static int dptx_edid_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_edid_show, inode->i_private);
}

static const struct file_operations dptx_edid_fops = {
	.open		= dptx_edid_open,
	.write	  = NULL,
	.read	   = dptx_edid_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

/*
 * edid_size
 */

static int dptx_edid_size_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	int buf_size = 0;
	int retval = 0;

	mutex_lock(&dptx->mutex);
	buf_size = dptx_get_edid_size(dptx);
	if (buf_size < 0) {
		retval = buf_size;
		goto done;
	}
	seq_printf(s, "%d\n", buf_size);
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_edid_size_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_edid_size_show, inode->i_private);
}

static const struct file_operations dptx_edid_size_fops = {
	.open	   = dptx_edid_size_open,
	.write	  = NULL,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

/*
 * adaptive_sync
 */

static int dptx_adaptive_sync_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	int buf_size = 0;
	int retval = 0;

	mutex_lock(&dptx->mutex);
	buf_size = dptx_check_adaptive_sync_status(dptx);
	if (buf_size < 0) {
		retval = buf_size;
		goto done;
	}
	seq_printf(s, "Adaptive Sync Status: %d\n", buf_size);
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static ssize_t dptx_adaptive_sync_write(struct file *file,
			      const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	int i, retval = 0;
	char buf[4];
	char enable_char;
	u8 enable;
	u8 adaptive_sync_mode;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}

	for (i = 0; i < sizeof(buf); i++) {
		if (buf[i] == ' ') {
			if (kstrtou8(&buf[i + 1], 10, &adaptive_sync_mode) < 0) {
				dptx_dbg(dptx, "Inside Sync Mode convert\n");
				retval = -EINVAL;
				goto done;
			}
			break;
		}
	}

	memcpy(&enable_char, buf, 1);
	if (kstrtou8(&enable_char, 10, &enable) < 0) {
		dptx_dbg(dptx, "Inside Enable convert\n");
		retval = -EINVAL;
		goto done;
	}
	if (enable)
		retval = dptx_enable_adaptive_sync(dptx, adaptive_sync_mode);
	else
		retval = dptx_disable_adaptive_sync(dptx);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_adaptive_sync_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_adaptive_sync_show, inode->i_private);
}
static const struct file_operations dptx_adaptive_sync_fops = {
	.open	   = dptx_adaptive_sync_open,
	.write	  = dptx_adaptive_sync_write,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

static int dptx_rx_caps_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	int retval = 0;

	mutex_lock(&dptx->mutex);
	seq_printf(s, "%x\n", dptx->rx_caps[0]);
	mutex_unlock(&dptx->mutex);

	return retval;
}

static int dptx_rx_caps_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_rx_caps_show, inode->i_private);
}

static const struct file_operations dptx_rx_caps_fops = {
	.open		= dptx_rx_caps_open,
	.write	  = NULL,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

static int dptx_dpcd_read_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	int retval = 0;
	u8 byte;

	mutex_lock(&dptx->mutex);
	dptx_read_dpcd(dptx, dpcd_addr, &byte);
	seq_printf(s, "0x%02x\n", byte);
	mutex_unlock(&dptx->mutex);

	return retval;
}

static int dptx_dpcd_read_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_dpcd_read_show, inode->i_private);
}

static const struct file_operations dptx_dpcd_read_fops = {
	.open		= dptx_dpcd_read_open,
	.write	  = NULL,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

/*
 * colorimetry
 */

static int dptx_video_col_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 video_col;

	mutex_lock(&dptx->mutex);
	video_col = dptx_get_video_colorimetry(dptx);
	seq_printf(s, "%d\n", video_col);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_video_col_write(struct file *file,
				    const char  __user *ubuf,
				    size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 video_col;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));
	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &video_col) < 0) {
		retval = -EINVAL;
		goto done;
	}
	retval = dptx_set_video_colorimetry(dptx, video_col);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_video_col_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_video_col_show, inode->i_private);
}

static const struct file_operations dptx_video_col_fops = {
	.open	   = dptx_video_col_open,
	.write	  = dptx_video_col_write,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

/*
 * dynamic_range
 */

static int dptx_video_range_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 dynamic_range;

	mutex_lock(&dptx->mutex);
	dynamic_range = dptx_get_video_dynamic_range(dptx);
	seq_printf(s, "%d\n", dynamic_range);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_video_range_write(struct file *file,
				      const char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 dynamic_range;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));
	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &dynamic_range) < 0) {
		retval = -EINVAL;
		goto done;
	}
	retval = dptx_set_video_dynamic_range(dptx, dynamic_range);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_video_range_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_video_range_show, inode->i_private);
}

static const struct file_operations dptx_video_range_fops = {
	.open	   = dptx_video_range_open,
	.write	  = dptx_video_range_write,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

/*
 * video_format
 */
static int dptx_video_format_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 video_format;

	mutex_lock(&dptx->mutex);
	video_format = dptx_get_video_format(dptx);
	seq_printf(s, "%d\n", video_format);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_video_format_write(struct file *file,
				       const char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 video_format;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));
	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &video_format) < 0) {
		retval = -EINVAL;
		goto done;
	}
	retval = dptx_set_video_format(dptx, video_format);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_video_format_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_video_format_show, inode->i_private);
}

static const struct file_operations dptx_video_format_fops = {
	.open	   = dptx_video_format_open,
	.write	  = dptx_video_format_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * pattern
 */
static int dptx_pattern_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	struct video_params *vparams;
	u8 pattern;

	mutex_lock(&dptx->mutex);
	vparams = &dptx->vparams;
	pattern = dptx_get_pattern(dptx);
	seq_printf(s, "%d\n", pattern);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_pattern_write(struct file *file,
				  const char __user *ubuf,
				  size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 pattern;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));
	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &pattern) < 0) {
		retval = -EINVAL;
		goto done;
	}
	retval = dptx_set_pattern(dptx, pattern);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_pattern_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_pattern_show, inode->i_private);
}

static const struct file_operations dptx_pattern_fops = {
	.open	   = dptx_pattern_open,
	.write	  = dptx_pattern_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * pix_enc
 */
static int dptx_pixel_enc_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 pixel_enc;

	mutex_lock(&dptx->mutex);
	pixel_enc = dptx_get_pixel_enc(dptx);
	seq_printf(s, "%d\n", pixel_enc);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_pixel_enc_write(struct file *file,
				    const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 pixel_enc;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));
	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &pixel_enc) < 0) {
		retval = -EINVAL;
		goto done;
	}
	retval = dptx_set_pixel_enc(dptx, pixel_enc);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_pixel_enc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_pixel_enc_show, inode->i_private);
}

static const struct file_operations dptx_pix_enc_fops = {
	.open	   = dptx_pixel_enc_open,
	.write	  = dptx_pixel_enc_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * BPC
 */
static int dptx_bpc_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 bpc;

	mutex_lock(&dptx->mutex);
	bpc = dptx_get_bpc(dptx);
	seq_printf(s, "%d\n", bpc);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static ssize_t dptx_bpc_write(struct file *file,
			      const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 bpc;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &bpc) < 0) {
		retval = -EINVAL;
		goto done;
	}
	retval = dptx_set_bpc(dptx, bpc);
	if (retval)
		goto done;
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_bpc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_bpc_show, inode->i_private);
}

static const struct file_operations dptx_bpc_fops = {
	.open	   = dptx_bpc_open,
	.write	  = dptx_bpc_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * MST
 */

static ssize_t dptx_mst_status_write(struct file *file,
				     const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 mst;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &mst) < 0) {
		retval = -EINVAL;
		goto done;
	}

	dptx->mst = mst;
	retval = dptx_update_stream_mode(dptx);
	if (retval)
		goto done;

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_mst_status_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	u8 mst;

	mutex_lock(&dptx->mutex);
	mst = dptx->mst;
	seq_printf(s, "%d\n", mst);
	mutex_unlock(&dptx->mutex);

	return 0;
}

static int dptx_mst_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_mst_status_show, inode->i_private);
}

static const struct file_operations dptx_mst_status_fops = {
	.open	   = dptx_mst_status_open,
	.write	  = dptx_mst_status_write,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

static ssize_t dptx_mst_add_stream(struct file *file,
				   const char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 trigger;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &trigger) < 0) {
		retval = -EINVAL;
		goto done;
	}

	retval = dptx_add_stream(dptx);
	if (retval)
		goto done;

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_add_stream_show(struct seq_file *s, void *unused)
{
	return 0;
}

static int dptx_mst_add_stream_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_add_stream_show, inode->i_private);
}

static const struct file_operations dptx_mst_add_stream_fops = {
	.open	   = dptx_mst_add_stream_open,
	.write	  = dptx_mst_add_stream,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};


//Remove stream
static ssize_t dptx_mst_remove_stream(struct file *file,
				      const char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	u8 stream;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtou8(buf, 10, &stream) < 0) {
		retval = -EINVAL;
		goto done;
	}

	retval = dptx_remove_stream(dptx, stream);
	if (retval)
		goto done;

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static int dptx_remove_stream_show(struct seq_file *s, void *unused)
{
	return 0;
}

static int dptx_mst_remove_stream_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_remove_stream_show, inode->i_private);
}

static const struct file_operations dptx_mst_remove_stream_fops = {
	.open	   = dptx_mst_remove_stream_open,
	.write	  = dptx_mst_remove_stream,
	.read	   = seq_read,
	.llseek	 = seq_lseek,
	.release	= single_release,
};

/*
 * eDP
 */

//Advanced Link Power Management - Status
static int dptx_alpm_status_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	int alpm_status;

	mutex_lock(&dptx->mutex);
	alpm_status = alpm_get_status(dptx);
	switch (alpm_status) {
	case NOT_AVAILABLE:
		seq_puts(s, "ALPM Status: NOT AVAILABLE\n");
		break;
	case DISABLED:
		seq_puts(s, "ALPM Status: DISABLED\n");
		break;
	case ENABLED:
		seq_puts(s, "ALPM Status: ENABLED\n");
		break;
	}
	mutex_unlock(&dptx->mutex);

	return 0;
}

static int dptx_alpm_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_alpm_status_show, inode->i_private);
}

static ssize_t dptx_alpm_status_write(struct file *file,
				      const char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	int retval = 0;
	char buf[3];
	int status, old_status;
	struct edp_alpm *alpm;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}
	if (kstrtouint(buf, 10, &status) < 0) {
		retval = -EINVAL;
		goto done;
	}

	alpm = &dptx->alpm;
	old_status = alpm->status;
	dptx_dbg(dptx, "Status now is: %d", old_status);
	retval = alpm_set_status(dptx, status);
	alpm->status = status;
	if (retval)
		goto done;

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static const struct file_operations dptx_alpm_status_fops = {
	.open	= dptx_alpm_status_open,
	.write	= dptx_alpm_status_write,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

//Advanced Link Power Management - State
static int dptx_alpm_state_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	int alpm_state;

	mutex_lock(&dptx->mutex);
	alpm_state = alpm_get_state(dptx);
	switch (alpm_state) {
	case POWER_ON:
		seq_puts(s, "Main-Link POWERED-ON\n");
		break;
	case POWER_OFF:
		seq_puts(s, "Main-Link POWERED-OFF\n");
		break;
	}
	mutex_unlock(&dptx->mutex);

	return 0;
}

static int dptx_alpm_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_alpm_state_show, inode->i_private);
}

static ssize_t dptx_alpm_state_write(struct file *file,
				     const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0, i = 0;
	char buf[5];
	char state_char;
	int state, off_state = 0;
	struct edp_alpm *alpm;
	u32 reg = 0;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);
	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		retval = -EFAULT;
		goto done;
	}

	for (i = 0; i < sizeof(buf); i++) {
		if (buf[i] == ' ') {
			if (kstrtouint(&buf[i + 1], 10, &off_state) < 0) {
				retval = -EINVAL;
				goto done;
			}
			break;
		}
	}

	memcpy(&state_char, buf, 1);
	if (kstrtouint(&state_char, 10, &state) < 0) {
		retval = -EINVAL;
		goto done;
	}

	alpm = &dptx->alpm;
	if (alpm->status == DISABLED) {
		dptx_err(dptx, "ERROR: ALPM disabled, not possible to update state!\n");
		retval = -EINVAL;
		goto done;
	}

	if (state) {
		reg = dptx_read_reg(dptx, dptx->regs[DPTX], PM_CONFIG1);

		if (off_state == FW_SLEEP) {
			reg |= BIT(28);
			dptx_dbg(dptx, "OFF_STATE: FW_SLEEP");
		} else if (off_state == FW_STANDBY) {
			reg &= BIT(28);
			dptx_dbg(dptx, "OFF_STATE: FW_STANDBY");
		}

		dptx_write_reg(dptx, dptx->regs[DPTX], PM_CONFIG1, reg);
	}

	retval = alpm_set_state(dptx, state);
	alpm->state = state;
	if (retval)
		goto done;

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

static const struct file_operations dptx_alpm_state_fops = {
	.open	= dptx_alpm_state_open,
	.write	= dptx_alpm_state_write,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void dptx_debugfs_init(struct dptx *dptx)
{
	struct dentry *root;
	struct dentry *video;
	struct dentry *audio;
	struct dentry *link;
	struct dentry *file;
	struct dentry *mst;
	struct dentry *edp;
	struct dentry *clk_mng;
	struct dentry *audio_gen;
	struct dentry *video_gen;
	struct dentry *setup_id;
	struct dentry *phy;

	root = debugfs_create_dir(dev_name(dptx->dev), NULL);
	if (IS_ERR_OR_NULL(root)) {
		dptx_err(dptx, "Can't create debugfs root\n");
		return;
	}

	link = debugfs_create_dir("link", root);
	if (IS_ERR_OR_NULL(link)) {
		dptx_err(dptx, "Can't create debugfs link\n");
		debugfs_remove_recursive(root);
		return;
	}

	video = debugfs_create_dir("video", root);
	if (IS_ERR_OR_NULL(video)) {
		dptx_err(dptx, "Can't create debugfs video\n");
		debugfs_remove_recursive(root);
		return;
	}

	audio = debugfs_create_dir("audio", root);
	if (IS_ERR_OR_NULL(audio)) {
		dptx_err(dptx, "Can't create debugfs audio\n");
		debugfs_remove_recursive(root);
		return;
	}

	mst = debugfs_create_dir("mst", root);
	if (IS_ERR_OR_NULL(mst)) {
		dptx_err(dptx, "Can't create debugfs mst\n");
		debugfs_remove_recursive(root);
		return;
	}

	edp = debugfs_create_dir("eDP", root);
	if (IS_ERR_OR_NULL(edp)) {
		dptx_err(dptx, "Can't create debugfs eDP\n");
		debugfs_remove_recursive(root);
		return;
	}

	clk_mng = debugfs_create_dir("clk_manager", root);
	if (IS_ERR_OR_NULL(clk_mng)) {
		dptx_err(dptx, "Can't create debugfs clk_manager\n");
		debugfs_remove_recursive(root);
		return;
	}

	audio_gen = debugfs_create_dir("audio_generator", root);
	if (IS_ERR_OR_NULL(audio_gen)) {
		dptx_err(dptx, "Can't create debugfs audio_gen\n");
		debugfs_remove_recursive(root);
		return;
	}

	video_gen = debugfs_create_dir("video_generator", root);
	if (IS_ERR_OR_NULL(video_gen)) {
		dptx_err(dptx, "Can't create debugfs video_gen\n");
		debugfs_remove_recursive(root);
		return;
	}

	setup_id = debugfs_create_dir("setup_id", root);
	if (IS_ERR_OR_NULL(setup_id)) {
		dptx_err(dptx, "Can't create debugfs setup_id\n");
		debugfs_remove_recursive(root);
		return;
	}

	phy = debugfs_create_dir("phy", root);
	if (IS_ERR_OR_NULL(phy)) {
		dptx_err(dptx, "Can't create debugfs phy\n");
		debugfs_remove_recursive(root);
		return;
	}

	/* Registers */
	dptx->regset[DPTX] = kzalloc(sizeof(*dptx->regset[DPTX]), GFP_KERNEL);
	if (!dptx->regset[DPTX]) {
		debugfs_remove_recursive(root);
		return;
	}

	dptx->regset[DPTX]->regs = dptx_regs;
	dptx->regset[DPTX]->nregs = dptx_regs_size;
	dptx->regset[DPTX]->base = dptx->base[DPTX];

	debugfs_create_regset32("regdump", 0444, root, dptx->regset[DPTX]);

	/* Global Reset */
	file = debugfs_create_file("Global_Reset", 0644, root, dptx,
				&dptx_global_reset_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs global reset\n");

	/* Core driver */
	debugfs_create_u8("pixel_mode_sel", 0644, root, &dptx->multipixel);
	debugfs_create_bool("aux_debug_en", 0644, root, &dptx->aux_debug_en);
	debugfs_create_bool("ycbcr_420_en", 0644, root, &dptx->ycbcr_420_en);

	/* MST */
	file = debugfs_create_file("status", 0644, mst, dptx,
				   &dptx_mst_status_fops);
		if (!file)
			dev_dbg(dptx->dev, "Can't create debugfs mst status\n");

	debugfs_create_u8("nr_streams", 0644, mst,
			  &dptx->streams);

	file = debugfs_create_file("add_stream", 0644, mst, dptx,
				   &dptx_mst_add_stream_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs mst add_stream\n");

	file = debugfs_create_file("remove_stream", 0644, mst, dptx,
				   &dptx_mst_remove_stream_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs mst remove_stream\n");

	/* DPCD */
	file = debugfs_create_file("rx_caps", 0644,
				   root, dptx, &dptx_rx_caps_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video rx_caps\n");

	debugfs_create_u32("dpcd_addr", 0644, root,
			   &dpcd_addr);

	file = debugfs_create_file("dpcd_read", 0644,
				   root, dptx, &dptx_dpcd_read_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video dpcd_read\n");

	/* Link */
	debugfs_create_u8("rate", 0644, link,
			  &dptx->link.rate);
	debugfs_create_u8("lane_count", 0444, link,
			  &dptx->link.lanes);
	debugfs_create_u8("aux_type", 0644, link,
			  &aux_type);
	debugfs_create_u32("aux_addr", 0644, link,
			   &aux_addr);
	debugfs_create_u32("aux_size", 0644, link,
			   &aux_size);

	debugfs_create_bool("trained", 0444, link,
			    &dptx->link.trained);

	file = debugfs_create_file("status", 0444,
				   link, dptx, &dptx_link_status_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs link status\n");

	file = debugfs_create_file("bstatus", 0444,
				   root, dptx, &dptx_bstatus_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs bstatus\n");

	file = debugfs_create_file("retrain", 0644,
				   link, dptx, &dptx_link_retrain_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs link retrain\n");

	file = debugfs_create_file("aux", 0644,
				   link, dptx, &dptx_aux_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs aux\n");

	/* Audio */

	file = debugfs_create_file("inf_type", 0644, audio, dptx,
				   &dptx_audio_inf_type_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs audio inf_type\n");

	file = debugfs_create_file("num_ch", 0644, audio, dptx,
				   &dptx_audio_num_ch_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs audio num_ch\n");

	file = debugfs_create_file("data_width", 0644, audio, dptx,
				   &dptx_audio_data_width_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs audio data_width\n");

	file = debugfs_create_file("sdp", 0644, audio, dptx,
				   &dptx_audio_sdp_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs audio sdp\n");

	file = debugfs_create_file("mute", 0644, audio, dptx,
				   &dptx_audio_mute_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs audio mute\n");

	file = debugfs_create_file("audio_generator", 0644, audio, dptx,
				   &dptx_audio_generator_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs audio mute\n");


	/* Video */

	debugfs_create_u32("refresh_rate", 0644, video,
			   &dptx->vparams.refresh_rate);

	file = debugfs_create_file("vic", 0644, video, dptx,
				   &dptx_vic_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video vic\n");

	file = debugfs_create_file("colorimetry", 0644,
				   video, dptx, &dptx_video_col_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video colorimetry\n");

	file = debugfs_create_file("dynamic_range", 0644,
				   video, dptx, &dptx_video_range_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs dynamic_range\n");

	file = debugfs_create_file("bpc", 0644, video, dptx,
				   &dptx_bpc_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video bpc\n");

	file = debugfs_create_file("pix_enc", 0644, video, dptx,
				   &dptx_pix_enc_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video pix_enc\n");

	file = debugfs_create_file("video_format", 0644, video,
				   dptx, &dptx_video_format_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video video_format\n");

	file = debugfs_create_file("pattern", 0644, video, dptx,
				   &dptx_pattern_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video pattern\n");

	file = debugfs_create_file("edid", 0644, video, dptx,
				   &dptx_edid_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video edid\n");

	file = debugfs_create_file("edid_size", 0644, video,
				   dptx, &dptx_edid_size_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video edid size\n");

	file = debugfs_create_file("adaptive-sync", 0644, video,
				   dptx, &dptx_adaptive_sync_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs video adaptive-sync\n");

	/* eDP */
	file = debugfs_create_file("ALPM_status", 0644, edp, dptx,
				   &dptx_alpm_status_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs ALPM Status\n");

	file = debugfs_create_file("ALPM_state", 0644, edp, dptx,
				   &dptx_alpm_state_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs ALPM State\n");


	/* Clock Manager */
	file = debugfs_create_file("video_clock_state", 0644, clk_mng, dptx,
			   &dptx_clkmng_video_state_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Clk_Mng Video State\n");

	file = debugfs_create_file("enable_video_clock", 0644, clk_mng, dptx,
			   &dptx_clkmng_video_enable_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Clk_Mng Video Enable\n");

	file = debugfs_create_file("video_clock_locked", 0644, clk_mng, dptx,
			   &dptx_clkmng_video_locked_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Clk_Mng Video Locked\n");

	file = debugfs_create_file("audio_clock_state", 0644, clk_mng, dptx,
			   &dptx_clkmng_audio_state_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Clk_Mng Audio State\n");

	file = debugfs_create_file("enable_audio_clock", 0644, clk_mng, dptx,
			   &dptx_clkmng_audio_enable_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Clk_Mng Audio Enable\n");

	file = debugfs_create_file("audio_clock_locked", 0644, clk_mng, dptx,
			   &dptx_clkmng_audio_locked_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Clk_Mng Audio Locked\n");

	file = debugfs_create_file("reset", 0644, clk_mng, dptx,
			   &dptx_clkmng_rst_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Clk_Mng Reset\n");

	/* Audio Generator */
	file = debugfs_create_file("enable", 0644, audio_gen, dptx,
			   &dptx_ag_enable_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Audio Gen Enable\n");

	file = debugfs_create_file("sample_freq", 0644, audio_gen, dptx,
			   &dptx_ag_freq_fs_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Audio Gen Sample Frequency\n");

	/* Video Generator */
	file = debugfs_create_file("enable", 0644, video_gen, dptx,
			   &dptx_vg_enable_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Video Gen Enable\n");

	file = debugfs_create_file("colorimetry", 0644, video_gen, dptx,
			   &dptx_vg_colorimetry_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Video Gen Colorimetry\n");

	file = debugfs_create_file("color_depth", 0644, video_gen, dptx,
			   &dptx_vg_colordepth_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Video Gen Color Depth\n");

	file = debugfs_create_file("pattern", 0644, video_gen, dptx,
			   &dptx_vg_patt_mode_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Video Gen Pattern Mode\n");

	file = debugfs_create_file("hactive", 0644, video_gen, dptx,
			   &dptx_vg_hactive_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Video Gen HActive\n");

	file = debugfs_create_file("hblank", 0644, video_gen, dptx,
			   &dptx_vg_hblank_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Video Gen HBlank\n");

	file = debugfs_create_file("vactive", 0644, video_gen, dptx,
			   &dptx_vg_vactive_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Video Gen VActive\n");

	file = debugfs_create_file("vblank", 0644, video_gen, dptx,
			   &dptx_vg_vblank_fops);
	if (!file)
		dev_dbg(dptx->dev, "Can't create debugfs Video Gen VBlank\n");

	dptx->root = root;
}

void dptx_debugfs_exit(struct dptx *dptx)
{
	int i;

	debugfs_remove_recursive(dptx->root);

	for (i = 0; i < MAX_MEM_IDX; i++)
		kfree(dptx->regset[i]);
}

#define DEBUGFS_REG32(_name)				\
{							\
	.name	= #_name,				\
	.offset	= DPTX_##_name,				\
}

static const struct debugfs_reg32 dptx_regs[] = {

	{ .name = "DPTX_VERSION_NUMBER", .offset = DPTX_VERSION_NUMBER, },
	{ .name = "DPTX_VERSION_TYPE", .offset = DPTX_VERSION_TYPE, },
	{ .name = "DPTX_ID", .offset = DPTX_ID, },
	{ .name = "DPTX_CONFIG_REG1", .offset = DPTX_CONFIG_REG1, },
	{ .name = "DPTX_CONFIG_REG3", .offset = DPTX_CONFIG_REG3, },
	{ .name = "CCTL", .offset = CCTL, },
	{ .name = "SOFT_RESET_CTRL", .offset = SOFT_RESET_CTRL, },

	{ .name = "VSAMPLE_CTRL", .offset = VSAMPLE_CTRL, },
	{ .name = "VINPUT_POLARITY_CTRL", .offset = VINPUT_POLARITY_CTRL, },
	{ .name = "VIDEO_CONFIG1_STREAM_0", .offset = DPTX_VIDEO_CONFIG1_N(0), },
	{ .name = "VIDEO_CONFIG2_STREAM_0", .offset = DPTX_VIDEO_CONFIG2_N(0), },
	{ .name = "VIDEO_CONFIG3_STREAM_0", .offset = DPTX_VIDEO_CONFIG3_N(0), },
	{ .name = "VIDEO_CONFIG4_STREAM_0", .offset = DPTX_VIDEO_CONFIG4_N(0), },
	{ .name = "VIDEO_CONFIG5_STREAM_0", .offset = DPTX_VIDEO_CONFIG5_N(0), },

	{ .name = "VIDEO_MSA1_STREAM_0", .offset = DPTX_VIDEO_MSA1_N(0), },
	{ .name = "VIDEO_MSA2_STREAM_0", .offset = DPTX_VIDEO_MSA2_N(0), },
	{ .name = "VIDEO_MSA3_STREAM_0", .offset = DPTX_VIDEO_MSA3_N(0), },
	{ .name = "VIDEO_HBLANK_INTERVAL", .offset = VIDEO_HBLANK_INTERVAL, },

	{ .name = "MVID_CONFIG1", .offset = MVID_CONFIG1, },
	{ .name = "MVID_CONFIG2", .offset = MVID_CONFIG2, },
	{ .name = "AUD_CONFIG1", .offset = AUD_CONFIG1, },

	{ .name = "PHYIF_CTRL", .offset = PHYIF_CTRL, },
	{ .name = "PHY_TX_EQ", .offset = PHY_TX_EQ, },
	{ .name = "GENERAL_INTERRUPT", .offset = GENERAL_INTERRUPT, },
	{ .name = "GENERAL_INTERRUPT_ENABLE", .offset = GENERAL_INTERRUPT_ENABLE, },
	{ .name = "HPD_STATUS", .offset = HPD_STATUS, },
	{ .name = "HPD_INTERRUPT_ENABLE", .offset = HPD_INTERRUPT_ENABLE, },
};

static const int dptx_regs_size = ARRAY_SIZE(dptx_regs);

static const struct debugfs_reg32 clkmng_regs[] = {

	{ .name = "MMCM_AUTO_VID_SADDR", .offset = MMCM_VIDEO_OFFSET + MMCM_SADDR, },
	{ .name = "MMCM_AUTO_VID_SEN", .offset = MMCM_VIDEO_OFFSET + MMCM_SEN, },
	{ .name = "MMCM_AUTO_VID_SRDY", .offset = MMCM_VIDEO_OFFSET + MMCM_SRDY, },
	{ .name = "MMCM_AUTO_VID_LOCKED", .offset = MMCM_VIDEO_OFFSET + MMCM_LOCKED, },
	{ .name = "MMCM_AUTO_AUD_SADDR", .offset = MMCM_AUDIO_OFFSET + MMCM_SADDR, },
	{ .name = "MMCM_AUTO_AUD_SEN", .offset = MMCM_AUDIO_OFFSET + MMCM_SEN, },
	{ .name = "MMCM_AUTO_AUD_SRDY", .offset = MMCM_AUDIO_OFFSET + MMCM_SRDY, },
	{ .name = "MMCM_AUTO_AUD_LOCKED", .offset = MMCM_AUDIO_OFFSET + MMCM_LOCKED, },
};

static const int clkmng_regs_size = ARRAY_SIZE(clkmng_regs);

static const struct debugfs_reg32 audiogen_regs[] = {

	{ .name = "AB_SRC_SEL_STREAM0", .offset = STREAM_OFFSET(0) + AB_SRC_SEL, },
	{ .name = "AB_CLK_DOMAIN_EN_STREAM0", .offset = STREAM_OFFSET(0) + AB_CLK_DOMAIN_EN, },
	{ .name = "AUDIO_GEN_CONFIG0_STREAM0", .offset = STREAM_OFFSET(0) + EXT_AG_CONFIG0, },
	{ .name = "AUDIO_GEN_CONFIG1_STREAM0", .offset = STREAM_OFFSET(0) + EXT_AG_CONFIG1, },
	{ .name = "AUDIO_GEN_CONFIG2_STREAM0", .offset = STREAM_OFFSET(0) + EXT_AG_CONFIG2, },
	{ .name = "AUDIO_PROC_CONFIG0_STREAM0", .offset = STREAM_OFFSET(0) + AP_CONFIG0, },
	{ .name = "AUDIO_PROC_CONFIG1_STREAM0", .offset = STREAM_OFFSET(0) + AP_CONFIG1, },
	{ .name = "AUDIO_PROC_CONFIG2_STREAM0", .offset = STREAM_OFFSET(0) + AP_CONFIG2, },
	{ .name = "AUDIO_PROC_CONFIG3_STREAM0", .offset = STREAM_OFFSET(0) + AP_CONFIG3, },
	{ .name = "AUDIO_PROC_STATUS1_STREAM0", .offset = STREAM_OFFSET(0) + AP_STATUS1, },
	{ .name = "AUDIO_PROC_STATUS2_STREAM0", .offset = STREAM_OFFSET(0) + AP_STATUS2, },
	{ .name = "TIMER_BASE_1S_STREAM0", .offset = STREAM_OFFSET(0) + AG_TIMER_BASE, },
};

static const int audiogen_regs_size = ARRAY_SIZE(audiogen_regs);

static const struct debugfs_reg32 videogen_regs[] = {

	{ .name = "VB_SRC_SEL_STREAM0", .offset = STREAM_OFFSET(0) + VB_SRC_SEL, },
	{ .name = "VPG_CONFIG0_STREAM0", .offset = STREAM_OFFSET(0) + VPG_CONF0, },
	{ .name = "VPG_CONFIG1_STREAM0", .offset = STREAM_OFFSET(0) + VPG_CONF1, },
	{ .name = "VPG_HAHB_CONFIG_STREAM0", .offset = STREAM_OFFSET(0) + VPG_HAHB_CONFIG, },
	{ .name = "VPG_HDHW_CONFIG_STREAM0", .offset = STREAM_OFFSET(0) + VPG_HDHW_CONFIG, },
	{ .name = "VPG_VAVB_CONFIG_STREAM0", .offset = STREAM_OFFSET(0) + VPG_VAVB_CONFIG, },
	{ .name = "VPG_VDVW_CONFIG_STREAM0", .offset = STREAM_OFFSET(0) + VPG_VDVW_CONFIG, },
	{ .name = "VPG_CB_LENGTH_CONFIG_STREAM0", .offset = STREAM_OFFSET(0) + VPG_CB_LENGTH_CONFIG, },
	{ .name = "VPG_CB_COLORA_L_STREAM0", .offset = STREAM_OFFSET(0) + VPG_CB_COLORA_L, },
	{ .name = "VPG_CB_COLORA_H_STREAM0", .offset = STREAM_OFFSET(0) + VPG_CB_COLORA_H, },
	{ .name = "VPG_CB_COLORB_L_STREAM0", .offset = STREAM_OFFSET(0) + VPG_CB_COLORB_L, },
	{ .name = "VPG_CB_COLORB_H_STREAM0", .offset = STREAM_OFFSET(0) + VPG_CB_COLORB_H, },
};

static const int videogen_regs_size = ARRAY_SIZE(videogen_regs);

static const struct debugfs_reg32 setupid_regs[] = {

	{ .name = "BITFILE_ID_0", .offset = 0x000, },
	{ .name = "BITFILE_ID_1", .offset = 0x004, },
	{ .name = "PROJECT_ID_0", .offset = 0x010, },
	{ .name = "PROJECT_ID_1", .offset = 0x014, },
	{ .name = "PERFORCE_ID_0", .offset = 0x020, },
	{ .name = "PERFORCE_ID_1", .offset = 0x024, },
	{ .name = "DWC_IIP_ID", .offset = 0x030, },
	{ .name = "DWC_IIP_VERSION", .offset = 0x034, },
	{ .name = "DWC_IIP2_ID", .offset = 0x038, },
	{ .name = "DWC_IIP2_VERSION", .offset = 0x03C, },
	{ .name = "PHY_ID", .offset = 0x040, },
	{ .name = "PLATFORM_ID", .offset = 0x050, },
	{ .name = "HOST_INFO", .offset = 0x054, },
	{ .name = "SUPP_DEBUG_REG1", .offset = 0x100, },
	{ .name = "SUPP_DEBUG_REG2", .offset = 0x104, },

};

static const int setupid_regs_size = ARRAY_SIZE(setupid_regs);

static const struct debugfs_reg32 phyif_regs[] = {

	{ .name = "PHY_TX_READY", .offset = 0x000, },
	{ .name = "PHY_RESET", .offset = 0x004, },
	{ .name = "PHY_ELECIDLE", .offset = 0x008, },
	{ .name = "PHY_TX_DETECT", .offset = 0x00C, },
	{ .name = "PHY_MAXPCLKREQ", .offset = 0x010, },
	{ .name = "PHY_MAXPCLKACK", .offset = 0x014, },
	{ .name = "PHY_DPTX_M2P_MESSAGE_BUS", .offset = 0x018, },
	{ .name = "PHY_DPTX_STATUS", .offset = 0x01C, },
	{ .name = "PHY_DPTX_LANE_RESET_N", .offset = 0x020, },
	{ .name = "PHY_DPTX_P2M_MESSAGE_BUS", .offset = 0x040, },
};

static const int phyif_regs_size = ARRAY_SIZE(phyif_regs);
