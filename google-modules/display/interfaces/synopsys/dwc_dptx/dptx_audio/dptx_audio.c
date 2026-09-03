// SPDX-License-Identifier: GPL-2.0
/*
 * dptx audio driver
 *
 * Copyright 2024 Google LLC
 */
#define DEBUG

#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "dptx.h"
#include "dptx_audio.h"
#include "api.h"
#include "extcon.h"
#include "avgen.h"

static const unsigned int extcon_id[] = {
	EXTCON_DISP_HDMI,
	EXTCON_NONE,
};

enum {
	DPTX_AUDIO_RATE,
	DPTX_AUDIO_WIDTH,
	DPTX_AUDIO_CHANNEL,
	DPTX_MAX,
};

struct dptx_audio_data {
	spinlock_t lock;
	int state;
	struct device *dev;
	struct dptx *handle;
	struct extcon_dev *dptx_audio_extcon;
};

static void dptx_audio_switch_set_state(struct dptx_audio_data *pdata, int state)
{
	int i;

	dev_info(pdata->dev, "dptx audio switch event = %x\n", state);

	extcon_set_state_sync(pdata->dptx_audio_extcon, EXTCON_DISP_HDMI, (state  < 0) ? 0 : 1);
}

static bool dptx_audio_switch_is_connected(struct dptx_audio_data *pdata)
{
	int ret;

	ret = extcon_get_state(pdata->dptx_audio_extcon, EXTCON_DISP_HDMI);
	if (ret <= 0) {
		dev_info(pdata->dev, "dp audio is disconnected\n");
		return false;
	}

	return true;
}

static int dptx_audio_notifier(struct notifier_block *nb,
				unsigned long event, void *data)
{
	if (data == NULL) {
		pr_err("invalid data\n");
		return 0;
	}

	dptx_audio_switch_set_state((struct dptx_audio_data *)data, (int)event);
	return 0;
}

static struct notifier_block dptx_audio_nb = {
	.notifier_call = dptx_audio_notifier,
};

static int dptx_audio_hw_params_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct device *dev = snd_soc_kcontrol_component(kcontrol)->dev;
	struct dptx_audio_data *pdata = dev_get_drvdata(dev);
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kcontrol->private_value;
	int param = mc->reg;
	long value = 0;

	switch (param) {
	case DPTX_AUDIO_RATE:
		value = pdata->handle->aparams.iec_samp_freq;
		break;
	case DPTX_AUDIO_WIDTH:
		value = pdata->handle->aparams.data_width;
		break;
	case DPTX_AUDIO_CHANNEL:
		value = pdata->handle->aparams.num_channels;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(dev, "param(%d), value(%ld)\n", param, value);
	ucontrol->value.integer.value[0] = value;

	return 0;
}

static int dptx_audio_hw_params_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	/* Nothing to do */
	return 0;
}

static const struct snd_kcontrol_new dptx_audio_controls[] = {
	SOC_SINGLE_EXT("DPTX Audio Rate", DPTX_AUDIO_RATE, 0, 384000, 0,
			dptx_audio_hw_params_get, dptx_audio_hw_params_put),
	SOC_SINGLE_EXT("DPTX Audio Width", DPTX_AUDIO_WIDTH, 0, 24, 0,
			dptx_audio_hw_params_get, dptx_audio_hw_params_put),
	SOC_SINGLE_EXT("DPTX Audio Channel", DPTX_AUDIO_CHANNEL, 0, 8, 0,
			dptx_audio_hw_params_get, dptx_audio_hw_params_put),
};


static int dptx_audio_ioctl(struct snd_soc_component *component,
		struct snd_pcm_substream *substream, unsigned int cmd, void *arg)
{
	int ret;

	ret = snd_pcm_lib_ioctl(substream, cmd, arg);

	return ret;
}

static int dptx_audio_open(struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct dptx_audio_data *pdata = snd_soc_component_get_drvdata(component);

	dptx_set_audio_is_active(pdata->handle, true);
	return 0;
}

static int dptx_audio_close(struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	struct dptx_audio_data *pdata = snd_soc_component_get_drvdata(component);

	dev_info(component->dev, "notify audio complete\n");
	dptx_set_audio_is_active(pdata->handle, false);
	return 0;
}

static int dptx_audio_hw_params(struct snd_soc_component *component,
		struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	struct dptx_audio_data *pdata = snd_soc_component_get_drvdata(component);

	// Fix format
	pdata->handle->aparams.data_width = 16;
	pdata->handle->aparams.iec_samp_freq = SAMPLE_FREQ_48;
	pdata->handle->aparams.num_channels = 2;
	return 0;
}

static int dptx_audio_hw_free(struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	return 0;
}

static int dptx_audio_prepare(struct snd_soc_component *component,
		struct snd_pcm_substream *substream)
{
	return 0;
}

static int dptx_audio_start(struct dptx *handle)
{
	dptx_audio_config(handle);
	return 0;
}

static int dptx_audio_stop(struct dptx *handle)
{
	dptx_audio_deconfig(handle);
	return 0;
}

static int dptx_audio_trigger(struct snd_soc_component *component,
		struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct dptx_audio_data *pdata = snd_soc_component_get_drvdata(component);
	struct device *dev = pdata->dev;

	if (!pm_runtime_get_if_in_use(pdata->handle->pd_dev[HSION_DP_PD])) {
		return -EIO;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dev_info(dev, "%s: SNDRV_PCM_TRIGGER_START\n", __func__);
		ret = dptx_audio_start(pdata->handle);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
		dev_info(dev, "%s: SNDRV_PCM_TRIGGER_STOP\n", __func__);
		ret = dptx_audio_stop(pdata->handle);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(pdata->handle->pd_dev[HSION_DP_PD]);
	return ret;
}

static const struct snd_soc_component_driver dptx_audio_component_drv = {
	.controls		= dptx_audio_controls,
	.num_controls		= ARRAY_SIZE(dptx_audio_controls),
	.open			= dptx_audio_open,
	.close			= dptx_audio_close,
	.ioctl			= dptx_audio_ioctl,
	.hw_params		= dptx_audio_hw_params,
	.hw_free		= dptx_audio_hw_free,
	.prepare		= dptx_audio_prepare,
	.trigger		= dptx_audio_trigger,
	//.pointer		= dptx_audio_pointer,
	//.mmap			= dptx_audio_mmap,
};

static int dptx_audio_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct dptx_audio_data *pdata = snd_soc_component_get_drvdata(dai->component);

	dev_dbg(pdata->dev, "%s, stream %s mute %d\n", __func__,
		(stream == SNDRV_PCM_STREAM_PLAYBACK) ? ("Playback") : ("Capture"),
		mute);

	if (stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EINVAL;

	if (!pm_runtime_get_if_in_use(pdata->handle->pd_dev[HSION_DP_PD]))
		return -EIO;

	pdata->handle->aparams.mute = mute;
	dptx_audio_mute(pdata->handle);

	pm_runtime_put(pdata->handle->pd_dev[HSION_DP_PD]);
	return 0;
}

static const struct snd_soc_dai_ops dptx_audio_dai_ops = {
	.mute_stream = dptx_audio_mute_stream,
};

static struct snd_soc_dai_driver dptx_audio_dai_drv[] = {
	{
		.name = "dptx_audio_pcm",
		.id = 0,
		.playback = {
			.stream_name = "DPTX Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_KNOT,
			.rate_min = 8000,
			.rate_max = 384000,
			.formats = (SNDRV_PCM_FMTBIT_S16
				| SNDRV_PCM_FMTBIT_S24
				| SNDRV_PCM_FMTBIT_S32),
		},
		.ops = &dptx_audio_dai_ops,
	},
};

static int dptx_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct dptx_audio_data *pdata;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->dev = dev;
	pdata->handle = dptx_get_device_handle();
	if (!pdata->handle) {
		dev_err(dev, "Failed to get aptx handle\n");
		return -EINVAL;
	}

	pdata->dptx_audio_extcon = devm_extcon_dev_allocate(dev, extcon_id);
	if (IS_ERR(pdata->dptx_audio_extcon)) {
		dev_err(dev, "Failed to allocate dptx audio extcon\n");
		return -EINVAL;
	}

	pdata->dptx_audio_extcon->dev.init_name = "hdmi_audio";
	ret = devm_extcon_dev_register(dev, pdata->dptx_audio_extcon);
	if (ret) {
		dev_err(dev, "Failed to register dptx audio extcon\n");
		goto error1;
	}

	ret = devm_snd_soc_register_component(dev, &dptx_audio_component_drv,
				dptx_audio_dai_drv, ARRAY_SIZE(dptx_audio_dai_drv));
	if (ret < 0) {
		dev_err(dev, "Failed to register ASoC component\n");
		goto error2;
	}

	pdata->handle->audio_notifier_data = pdata;
	blocking_notifier_chain_register(&pdata->handle->audio_notifier_head, &dptx_audio_nb);

	platform_set_drvdata(pdev, pdata);
	dev_set_drvdata(dev, pdata);

	dev_info(dev, "dptx audio init\n");
	return ret;
error2:
	devm_extcon_dev_unregister(dev, pdata->dptx_audio_extcon);
error1:
	devm_extcon_dev_free(dev, pdata->dptx_audio_extcon);
	dev_err(dev, "dptx audio init failed ret = %d\n", ret);
	return ret;
}

static void dptx_audio_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dptx_audio_data *pdata = dev_get_drvdata(dev);

	blocking_notifier_chain_unregister(&pdata->handle->audio_notifier_head, &dptx_audio_nb);
	devm_extcon_dev_unregister(dev, pdata->dptx_audio_extcon);
	devm_extcon_dev_free(dev, pdata->dptx_audio_extcon);
}

static const struct of_device_id dptx_audio_driver_dt_match[] = {
	{ .compatible = "google,dwc_dptx_audio" },
	{},
};

MODULE_DEVICE_TABLE(of, dptx_audio_driver_dt_match);

static struct platform_driver dptx_audio_driver = {
	.probe	= dptx_audio_probe,
	.remove	= dptx_audio_remove,
	.driver = {
		.name = "dptx_audio",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(dptx_audio_driver_dt_match),
	},
};

int dptx_audio_register(void)
{
	int err;

	err = platform_driver_register(&dptx_audio_driver);
	if (err)
		pr_err("registering dptx audio driver failed err = %d\n", err);
	return err;
}

void dptx_audio_unregister(void)
{
	platform_driver_unregister(&dptx_audio_driver);
}
