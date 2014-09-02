/*
 * CI20 ASoC driver
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
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <sound/jack.h>
#include <sound/soc.h>

#define GPIO_HP_MUTE 109
#define GPIO_HP_DETECT 135
#define GPIO_MIC_SW_EN 174

static struct snd_soc_jack ci20_hp_jack;

static struct snd_soc_jack_pin ci20_hp_jack_pins[] = {
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};

static int ci20_hp_jack_status_check(void *data)
{
	int enable;

	enable = !gpio_get_value_cansleep(GPIO_HP_DETECT);

	/*
	 * The headset type detection switch requires a rising edge on its
	 * enable pin to trigger the detection sequence.
	 */
	if (enable) {
		gpio_set_value_cansleep(GPIO_MIC_SW_EN, 1);
		return SND_JACK_HEADPHONE;
	} else {
		gpio_set_value_cansleep(GPIO_MIC_SW_EN, 0);
		return 0;
	}
}

static struct snd_soc_jack_gpio ci20_hp_jack_gpio = {
	.name = "headphone detect",
	.report = SND_JACK_HEADPHONE,
	.gpio = GPIO_HP_DETECT,
	.debounce_time = 200,
	.invert = 1,
	.jack_status_check = ci20_hp_jack_status_check,
};

static int ci20_hp_event(struct snd_soc_dapm_widget *widget,
	struct snd_kcontrol *ctrl, int event)
{
	gpio_set_value(GPIO_HP_MUTE, !!SND_SOC_DAPM_EVENT_OFF(event));
	return 0;
}

static const struct snd_soc_dapm_widget ci20_widgets[] = {
	SND_SOC_DAPM_MIC("Mic", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", ci20_hp_event),
};

static const struct snd_soc_dapm_route ci20_routes[] = {
	{"Mic", NULL, "AIP2"},
	{"Headphone Jack", NULL, "AOHPL"},
	{"Headphone Jack", NULL, "AOHPR"},
};

static int ci20_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE,
			&ci20_hp_jack);
	snd_soc_jack_add_pins(&ci20_hp_jack,
			ARRAY_SIZE(ci20_hp_jack_pins), ci20_hp_jack_pins);
	snd_soc_jack_add_gpios(&ci20_hp_jack, 1, &ci20_hp_jack_gpio);

	snd_soc_dapm_nc_pin(dapm, "AIP1");
	snd_soc_dapm_nc_pin(dapm, "AIP3");
	snd_soc_dapm_force_enable_pin(dapm, "Mic Bias");
	snd_soc_dapm_sync(dapm);

	return 0;
}

#define CI20_DAIFMT (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF \
					| SND_SOC_DAIFMT_CBM_CFM)

static struct snd_soc_dai_link ci20_dai_link = {
	.name = "ci20",
	.stream_name = "jz4780",
	.cpu_dai_name = "jz4780-i2s",
	.platform_name = "jz4780-i2s",
	.codec_dai_name = "jz4780-hifi",
	.codec_name = "jz4780-codec",
	.init = ci20_init,
	.dai_fmt = CI20_DAIFMT,
};

static struct snd_soc_card ci20_audio_card = {
	.name = "ci20",
	.dai_link = &ci20_dai_link,
	.num_links = 1,

	.dapm_widgets = ci20_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ci20_widgets),
	.dapm_routes = ci20_routes,
	.num_dapm_routes = ARRAY_SIZE(ci20_routes),
};

static const struct of_device_id ingenic_asoc_ci20_dt_ids[] = {
	{ .compatible = "ingenic,ci20-audio", },
	{ }
};

static int ingenic_asoc_ci20_probe(struct platform_device *pdev)
{
	int ret;

	struct snd_soc_card *card = &ci20_audio_card;
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
		ci20_dai_link.cpu_of_node = i2s;
		ci20_dai_link.cpu_dai_name = NULL;
		ci20_dai_link.platform_of_node = i2s;
		ci20_dai_link.platform_name = NULL;
		ci20_dai_link.codec_of_node = codec;
		ci20_dai_link.codec_name = NULL;
	}

	ret = gpio_request(GPIO_HP_MUTE, "Headphone Mute");
	if (ret < 0)
		dev_warn(&pdev->dev, "Failed to request mute GPIO: %d\n",
			 ret);

	gpio_direction_output(GPIO_HP_MUTE, 1);

	ret = gpio_request(GPIO_MIC_SW_EN, "Mic Switch Enable");
	if (ret < 0)
		dev_warn(&pdev->dev,
			 "Failed to request mic switch enable GPIO: %d\n",
			 ret);

	gpio_direction_output(GPIO_MIC_SW_EN, 0);

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed:%d\n", ret);

	return ret;
}

static int ingenic_asoc_ci20_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	snd_soc_jack_free_gpios(&ci20_hp_jack, 1, &ci20_hp_jack_gpio);
	gpio_free(GPIO_MIC_SW_EN);
	gpio_free(GPIO_HP_MUTE);

	return 0;
}

static struct platform_driver ingenic_ci20_audio_driver = {
	.driver = {
		.name = "ingenic-ci20-audio",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ingenic_asoc_ci20_dt_ids),
	},
	.probe = ingenic_asoc_ci20_probe,
	.remove = ingenic_asoc_ci20_remove,
};

module_platform_driver(ingenic_ci20_audio_driver);

MODULE_AUTHOR("Paul Burton <paul.burton@imgtec.com>");
MODULE_DESCRIPTION("ci20/JZ4780 ASoC driver");
MODULE_LICENSE("GPL");
