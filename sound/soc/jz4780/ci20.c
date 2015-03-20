/*
 * CI20/JZ4780 ASoC driver
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

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

int ci20_hp_jack_status_check(void)
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

static int ci20_hdmi_switch;

static int ci20_hdmi_switch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = ci20_hdmi_switch;
	return 0;
}

static int ci20_hdmi_switch_set(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_card *card = codec->card;
	struct snd_soc_dai *cpu_dai = card->rtd->cpu_dai;
	unsigned int fmt, clk_freq;
	int err, clk_dir;

	if (ci20_hdmi_switch == ucontrol->value.integer.value[0])
		return 0;

	ci20_hdmi_switch = ucontrol->value.integer.value[0];

	fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF;
	if (ci20_hdmi_switch) {
		fmt |= SND_SOC_DAIFMT_CBS_CFS;
		clk_freq = 11333335;
		clk_dir = SND_SOC_CLOCK_OUT;
	} else {
		fmt |= SND_SOC_DAIFMT_CBM_CFM;
		clk_freq = 12000000;
		clk_dir = SND_SOC_CLOCK_IN;
	}

	err = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (err) {
		dev_err(card->dev, "Failed to set cpu_dai format\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(cpu_dai, 0, clk_freq, clk_dir);
	if (err < 0) {
		dev_err(card->dev, "Failed to set cpu_dai clock\n");
		return err;
	}

	return 0;
}

static const struct snd_kcontrol_new ci20_controls[] = {
	SOC_SINGLE_BOOL_EXT("HDMI Switch", (unsigned long)&ci20_hdmi_switch,
		ci20_hdmi_switch_get, ci20_hdmi_switch_set),
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
	{"Headphone Jack", NULL, "AOHPL"},
};

static int ci20_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE,
			&ci20_hp_jack);
	snd_soc_jack_add_pins(&ci20_hp_jack, ARRAY_SIZE(ci20_hp_jack_pins),
			ci20_hp_jack_pins);

#ifndef CONFIG_SOUND_XBURST_DETECT
	snd_soc_jack_add_gpios(&ci20_hp_jack, 1, &ci20_hp_jack_gpio);
#endif
	snd_soc_dapm_nc_pin(dapm, "AIP1");
	snd_soc_dapm_nc_pin(dapm, "AIP3");
	snd_soc_dapm_force_enable_pin(dapm, "Mic Bias");
	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct snd_soc_dai_link ci20_dai_link = {
	.name = "ci20",
	.stream_name = "jz4780",
	.cpu_dai_name = "jz4780-i2s",
	.platform_name = "jz4780-pcm",
	.codec_dai_name = "jz4780-hifi",
	.codec_name = "jz4780-codec",
	.init = ci20_init,
};

static struct snd_soc_card ci20_audio_card = {
	.name = "ci20",
	.dai_link = &ci20_dai_link,
	.num_links = 1,

	.dapm_widgets = ci20_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ci20_widgets),
	.dapm_routes = ci20_routes,
	.num_dapm_routes = ARRAY_SIZE(ci20_routes),
	.controls = ci20_controls,
	.num_controls = ARRAY_SIZE(ci20_controls),
};

static struct platform_device *ci20_audio_device;

static int __init ci20_audio_init(void)
{
	int ret;

	ci20_audio_device = platform_device_alloc("soc-audio", -1);
	if (!ci20_audio_device)
		return -ENOMEM;

	platform_set_drvdata(ci20_audio_device, &ci20_audio_card);

	ret = gpio_request(GPIO_HP_MUTE, "Headphone Mute");
	if (ret < 0)
		pr_warn("ci20 audio: Failed to request mute GPIO: %d\n", ret);

	gpio_direction_output(GPIO_HP_MUTE, 1);

	ret = gpio_request(GPIO_MIC_SW_EN, "Mic Switch Enable");
	if (ret < 0)
		pr_warn("ci20 audio: Failed to request mic switch enable GPIO: %d\n",
			ret);

	gpio_direction_output(GPIO_MIC_SW_EN, 0);

	ret = platform_device_add(ci20_audio_device);
	if (ret) {
		pr_err("ci20 audio: Failed to add device: %d\n", ret);
		platform_device_put(ci20_audio_device);
	}

	ci20_hdmi_switch = 0;

	return ret;
}
module_init(ci20_audio_init);

static void __exit ci20_audio_exit(void)
{
#ifndef CONFIG_SOUND_XBURST_DETECT
	snd_soc_jack_free_gpios(&ci20_hp_jack, 1, &ci20_hp_jack_gpio);
#endif
	platform_device_unregister(ci20_audio_device);
	gpio_free(GPIO_MIC_SW_EN);
	gpio_free(GPIO_HP_MUTE);
}
module_exit(ci20_audio_exit);

MODULE_AUTHOR("Paul Burton <paul.burton@imgtec.com>");
MODULE_DESCRIPTION("CI20/JZ4780 ASoC driver");
MODULE_LICENSE("GPL");
