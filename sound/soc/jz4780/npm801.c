/*
 * NPM801 ASoC driver
 *
 * Copyright (c) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <sound/jack.h>
#include <sound/soc.h>

#define GPIO_HP_MUTE 109
#define GPIO_MIC_SW_EN 57
#define GPIO_SPEAKER_EN 134

#ifdef CONFIG_DRM_JZ4780_HDMI_AUDIO
static struct snd_soc_jack npm801_hdmi_jack;
int dw_hdmi_jack_detect(struct snd_soc_codec *codec_dai,
			struct snd_soc_jack *jack);
#endif

static struct snd_soc_jack_pin npm801_hdmi_jack_pins[] = {
	{
		.pin = "HDMI Jack",
		.mask = SND_JACK_LINEOUT,
	},
};

int jz4780_codec_check_hp_jack_status(struct snd_soc_codec *codec);

struct detection_thread_control {
	struct mutex lock;
	int hp_detection_enabled;
	struct snd_soc_codec *codec;
};

struct detection_thread_control detection_thread_ctl;

static void npm801_check_hp_jack_status(struct work_struct *unused)
{
	int run;
	int val;

	do {
		val = !jz4780_codec_check_hp_jack_status(detection_thread_ctl.codec);

		/*
		 * When headphones are muted, speakers are enabled
		 * and vice versa.
		 */
		gpio_set_value(GPIO_HP_MUTE, val);
		gpio_set_value(GPIO_SPEAKER_EN, val);
		msleep(250);

		mutex_lock(&detection_thread_ctl.lock);
		run = detection_thread_ctl.hp_detection_enabled;
		mutex_unlock(&detection_thread_ctl.lock);
	} while (run);
}

static DECLARE_WORK(npm801_check_hp_jack_status_work, npm801_check_hp_jack_status);


static int npm801_hp_event(struct snd_soc_dapm_widget *widget,
	struct snd_kcontrol *ctrl, int event)
{
	if(SND_SOC_DAPM_EVENT_ON(event)) {
		mutex_lock(&detection_thread_ctl.lock);
		detection_thread_ctl.hp_detection_enabled = 1;
		mutex_unlock(&detection_thread_ctl.lock);

		schedule_work(&npm801_check_hp_jack_status_work);
	} else {
		mutex_lock(&detection_thread_ctl.lock);
		detection_thread_ctl.hp_detection_enabled = 0;
		mutex_unlock(&detection_thread_ctl.lock);

		cancel_work_sync(&npm801_check_hp_jack_status_work);
		gpio_set_value(GPIO_HP_MUTE, 1);
		gpio_set_value(GPIO_SPEAKER_EN, 0);
	}
	return 0;
}

static const struct snd_soc_dapm_widget npm801_widgets[] = {
	SND_SOC_DAPM_MIC("Mic", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", npm801_hp_event),
	SND_SOC_DAPM_LINE("HDMI", NULL),
};

static const struct snd_soc_dapm_route npm801_routes[] = {
	{"Mic", NULL, "AIP1"},
	{"Headphone Jack", NULL, "AOHPL"},
	{"Headphone Jack", NULL, "AOHPR"},
	{"HDMI", NULL, "TX"},
};

static int npm801_audio_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int dai_fmt = rtd->dai_link->dai_fmt;
	int mclk, ret;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "failed to set cpu_dai fmt.\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "failed to set cpu_dai sysclk.\n");
		return ret;
	}

	return 0;
}

static int npm801_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);

	detection_thread_ctl.codec = codec;
	mutex_init(&detection_thread_ctl.lock);

	snd_soc_dapm_nc_pin(dapm, "AIP2");
	snd_soc_dapm_nc_pin(dapm, "AIP3");
	snd_soc_dapm_force_enable_pin(dapm, "Mic Bias");
	snd_soc_dapm_sync(dapm);

	return 0;
}

static int npm801_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);

	snd_soc_dapm_enable_pin(dapm, "HDMI");
#ifdef CONFIG_DRM_JZ4780_HDMI_AUDIO
	snd_soc_card_jack_new(rtd->card, "HDMI Jack", SND_JACK_LINEOUT,
			 &npm801_hdmi_jack, npm801_hdmi_jack_pins,
			 ARRAY_SIZE(npm801_hdmi_jack_pins));
	dw_hdmi_jack_detect(codec, &npm801_hdmi_jack);
#endif
	return 0;
}

static struct snd_soc_ops npm801_audio_dai_ops = {
	.hw_params = npm801_audio_hw_params,
};

#define npm801_DAIFMT (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF \
					| SND_SOC_DAIFMT_CBM_CFM)

static struct snd_soc_dai_link npm801_dai_link[] = {
	{
		.name = "npm801",
		.stream_name = "headphones",
		.cpu_dai_name = "jz4780-i2s",
		.platform_name = "jz4780-i2s",
		.codec_dai_name = "jz4780-hifi",
		.codec_name = "jz4780-codec",
		.init = npm801_init,
		.ops = &npm801_audio_dai_ops,
		.dai_fmt = npm801_DAIFMT,
	},
	{
		.name = "npm801 HDMI",
		.stream_name = "hdmi",
		.cpu_dai_name = "jz4780-i2s",
		.platform_name = "jz4780-i2s",
		.codec_dai_name = "dw-hdmi-hifi",
		.codec_name = "dw-hdmi-audio",
		.init = npm801_hdmi_init,
		.ops = &npm801_audio_dai_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS,
	}
};

static struct snd_soc_card npm801_audio_card = {
	.name = "npm801",
	.dai_link = npm801_dai_link,
	.num_links = ARRAY_SIZE(npm801_dai_link),

	.dapm_widgets = npm801_widgets,
	.num_dapm_widgets = ARRAY_SIZE(npm801_widgets),
	.dapm_routes = npm801_routes,
	.num_dapm_routes = ARRAY_SIZE(npm801_routes),
};

static const struct of_device_id ingenic_asoc_npm801_dt_ids[] = {
	{ .compatible = "ingenic,npm801-audio", },
	{ }
};

static int ingenic_asoc_npm801_probe(struct platform_device *pdev)
{
	int ret;

	struct snd_soc_card *card = &npm801_audio_card;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec, *i2s;

	card->dev = &pdev->dev;

	i2s = of_parse_phandle(np, "ingenic,i2s-controller", 0);
	codec = of_parse_phandle(np, "ingenic,codec", 0);

	if (!i2s || !codec) {
		dev_warn(&pdev->dev,
			 "Phandle not found for i2s/codecs, using defaults\n");
	} else {
		dev_dbg(&pdev->dev, "Setting dai_link parameters\n");
		npm801_dai_link[0].cpu_of_node = i2s;
		npm801_dai_link[0].cpu_dai_name = NULL;
		npm801_dai_link[1].cpu_of_node = i2s;
		npm801_dai_link[1].cpu_dai_name = NULL;
		npm801_dai_link[0].platform_of_node = i2s;
		npm801_dai_link[0].platform_name = NULL;
		npm801_dai_link[1].platform_of_node = i2s;
		npm801_dai_link[1].platform_name = NULL;
		npm801_dai_link[0].codec_of_node = codec;
		npm801_dai_link[0].codec_name = NULL;
	}

	ret = devm_gpio_request(&pdev->dev, GPIO_HP_MUTE, "Headphone Mute");
	if (ret < 0)
		dev_warn(&pdev->dev, "Failed to request mute GPIO: %d\n",
			 ret);

	gpio_direction_output(GPIO_HP_MUTE, 1);

	ret = devm_gpio_request(&pdev->dev, GPIO_MIC_SW_EN, "Mic Switch Enable");
	if (ret < 0)
		dev_warn(&pdev->dev,
			 "Failed to request mic switch enable GPIO: %d\n",
			 ret);

	gpio_direction_output(GPIO_MIC_SW_EN, 1);

	ret = devm_gpio_request(&pdev->dev, GPIO_SPEAKER_EN, "Speakers Enable");
	if (ret < 0)
		printk("Failed to request speakers enable GPIO: %d\n",
			 ret);

	gpio_direction_output(GPIO_SPEAKER_EN, 0);

	ret = snd_soc_register_card(card);
	if ((ret) && (ret != -EPROBE_DEFER))
		dev_err(&pdev->dev, "snd_soc_register_card() failed:%d\n", ret);

	return ret;
}

static int ingenic_asoc_npm801_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver ingenic_npm801_audio_driver = {
	.driver = {
		.name = "ingenic-npm801-audio",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ingenic_asoc_npm801_dt_ids),
	},
	.probe = ingenic_asoc_npm801_probe,
	.remove = ingenic_asoc_npm801_remove,
};

module_platform_driver(ingenic_npm801_audio_driver);

MODULE_AUTHOR("Paul Burton <paul.burton@imgtec.com>");
MODULE_DESCRIPTION("npm801/JZ4780 ASoC driver");
MODULE_LICENSE("GPL");
