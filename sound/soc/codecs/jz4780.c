/*
 * Ingenic JZ4780 ASoC codec driver
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

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define	REG_SR			0x00
#define	REG_SR2			0x01
#define	REG_MR			0x07
#define	REG_AICR_DAC		0x08
#define	REG_AICR_ADC		0x09
#define	REG_CR_LO		0x0B
#define	REG_CR_HP		0x0D
#define	REG_CR_DMIC		0x10
#define	REG_CR_MIC1		0x11
#define	REG_CR_MIC2		0x12
#define	REG_CR_LI1		0x13
#define	REG_CR_LI2		0x14
#define	REG_CR_DAC		0x17
#define	REG_CR_ADC		0x18
#define	REG_CR_MIX		0x19
#define	REG_DR_MIX		0x1A
#define	REG_CR_VIC		0x1B
#define	REG_CR_CK		0x1C
#define	REG_FCR_DAC		0x1D
#define	REG_FCR_ADC		0x20
#define	REG_CR_TIMER_MSB	0x21
#define	REG_CR_TIMER_LSB	0x22
#define	REG_ICR			0x23
#define	REG_IMR			0x24
#define	REG_IFR			0x25
#define	REG_IMR2		0x26
#define	REG_IFR2		0x27
#define	REG_GCR_HPL		0x28
#define	REG_GCR_HPR		0x29
#define	REG_GCR_LIBYL		0x2A
#define	REG_GCR_LIBYR		0x2B
#define	REG_GCR_DACL		0x2C
#define	REG_GCR_DACR		0x2D
#define	REG_GCR_MIC1		0x2E
#define	REG_GCR_MIC2		0x2F
#define	REG_GCR_ADCL		0x30
#define	REG_GCR_ADCR		0x31
#define	REG_GCR_MIXDACL		0x34
#define	REG_GCR_MIXDACR		0x35
#define	REG_GCR_MIXADCL		0x36
#define	REG_GCR_MIXADCR		0x37
#define	REG_CR_ADC_AGC		0x3A
#define	REG_DR_ADC_AGC		0x3B

#define	REG_DMIXER		0x100
#define	REG_DMIX_0		REG_DMIXER | 0x0
#define	REG_DMIX_1		REG_DMIXER | 0x1
#define	REG_DMIX_2		REG_DMIXER | 0x2
#define	REG_DMIX_3		REG_DMIXER | 0x3

#define REG_RGDATA_OFF				4
#define REG_RGADW_RGADDR_SHIFT			8
#define REG_RGADW_RGADDR_MASK			0x7F
#define REG_RGADW_RGDIN_MASK			0xFF
#define REG_RGADW_RGWR				BIT(16)
#define REG_RGADW_ICRST				BIT(31)

#define REG_CR_MIC1_MICSTEREO_MASK		BIT(7)
#define REG_CR_MIC1_MICSTEREO_MONO		0
#define REG_CR_MIC1_MICSTEREO_STEREO		1
#define REG_CR_MIC1_MIC1_SEL_MASK		BIT(1)
#define REG_CR_MIC1_MIC1_SEL_AIP2		1

#define REG_CR_ADC_ADC_LEFT_ONLY_MASK		BIT(5)
#define REG_CR_ADC_ADC_LEFT_ONLY_RCH_ACTIVE	0
#define REG_CR_ADC_ADC_LEFT_ONLY_RCH_INACTIVE	1
#define REG_CR_ADC_ADC_IN_SEL_MASK		0x3
#define REG_CR_ADC_ADC_IN_SEL_AIN1		0

#define REG_CR_DAC_DAC_MUTE_MASK		BIT(7)
#define REG_CR_DAC_DAC_MUTE_SHIFT		7
#define REG_CR_DAC_DAC_MUTE_INACTIVE		0
#define REG_CR_DAC_DAC_MUTE_SOFT_MUTE		1

#define REG_AICR_DAC_DAC_ADWL_MASK		0xC0
#define REG_AICR_DAC_DAC_ADWL_SHIFT		6
#define REG_AICR_DAC_AUDIOIF_MASK		0x3
#define REG_AICR_DAC_AUDIOIF_I2S		0x3

#define REG_AICR_ADC_ADC_ADWL_MASK		0xC0
#define REG_AICR_ADC_ADC_ADWL_SHIFT		6
#define REG_AICR_ADC_AUDIOIF_MASK		0x3
#define REG_AICR_ADC_AUDIOIF_I2S		0x3

#define REG_FCR_DAC_MASK			0xF

#define REG_CR_VIC_SB_MASK			BIT(0)
#define REG_CR_VIC_SB_NORMAL_MODE		0
#define REG_CR_VIC_SB_POWERDOWN_MODE		1
#define REG_CR_VIC_SB_SLEEP_MASK		BIT(1)
#define REG_CR_VIC_SB_SLEEP_NORMAL_MODE		0
#define REG_CR_VIC_SB_SLEEP_SLEEP_MODE		0x2

#define REG_CR_MIX_MIX_EN_MASK			BIT(7)
#define REG_CR_MIX_MIX_EN_OFF			0
#define REG_CR_MIX_MIX_EN_ENABLED		1
#define REG_CR_MIX_MIX_LOAD_MASK		BIT(6)
#define REG_CR_MIX_MIX_LOAD_READ		0
#define REG_CR_MIX_MIX_LOAD_WRITE		0x40
#define REG_CR_MIX_DAC_MIX_MASK			0x3


static struct reg_default jz4780_codec_reg_defaults[] = {
	{ REG_AICR_DAC,		0xd3 },
	{ REG_AICR_ADC,		0xd3 },
	{ REG_CR_LO,		0x90 },
	{ REG_CR_HP,		0x90 },
	{ REG_CR_MIC1,		0xb0 },
	{ REG_CR_MIC2,		0x30 },
	{ REG_CR_LI1,		0x10 },
	{ REG_CR_LI2,		0x10 },
	{ REG_CR_DAC,		0x90 },
	{ REG_CR_ADC,		0x90 },
	{ REG_CR_VIC,		0x03 },
	{ REG_IMR,		0xff },
	{ REG_IMR2,		0xff },
	{ REG_GCR_HPL,		0x06 },
	{ REG_GCR_HPR,		0x06 },
	{ REG_GCR_LIBYL,	0x06 },
	{ REG_GCR_LIBYR,	0x06 },
};

struct jz4780_codec {
	void __iomem *base;
	struct resource *mem;
	struct clk *clk;
	struct regmap *regmap;
};

static int jz4780_codec_read(void *context, unsigned int reg, unsigned int *val)
{
	struct jz4780_codec *jzc = context;

	clk_enable(jzc->clk);

	writel((reg & REG_RGADW_RGADDR_MASK) << REG_RGADW_RGADDR_SHIFT,
		jzc->base);
	*val = readl(jzc->base + REG_RGDATA_OFF) & REG_RGADW_RGDIN_MASK;

	clk_disable(jzc->clk);
	return 0;
}

static int jz4780_codec_write(void *context, unsigned int reg, unsigned int val)
{
	struct jz4780_codec *jzc = context;

	clk_enable(jzc->clk);

	writel(REG_RGADW_RGWR | ((reg & REG_RGADW_RGADDR_MASK)
		<< REG_RGADW_RGADDR_SHIFT)
		| (val & REG_RGADW_RGDIN_MASK), jzc->base);
	while (readl(jzc->base) & (REG_RGADW_RGWR))
		/* Fixme: Add timeout */;

	clk_disable(jzc->clk);
	return 0;
}

static const char * const mic1_input_mux_text[] = {
	"AIP1", "AIP2",
};

static const struct soc_enum mic1_input_enum =
	SOC_ENUM_SINGLE(REG_CR_MIC1, 0, 2, mic1_input_mux_text);

static const struct snd_kcontrol_new mic1_input_mux =
	SOC_DAPM_ENUM("Mic Input Mux", mic1_input_enum);

static const struct snd_kcontrol_new jz4780_codec_controls[] = {
	SOC_DOUBLE_R("Master Capture Volume", REG_GCR_ADCL, REG_GCR_ADCR,
		0, 63, 0),

	SOC_SINGLE("Mic Volume", REG_GCR_MIC1, 0, 7, 0),
	SOC_SINGLE("Mic Mute", REG_CR_ADC, 7, 1, 0),

	SOC_DOUBLE_R("Master Playback Volume", REG_GCR_DACL, REG_GCR_DACR,
		0, 31, 1),

	SOC_DOUBLE_R("Headphone Volume", REG_GCR_HPL, REG_GCR_HPR, 0, 31, 1),
	SOC_SINGLE("Headphone Mute", REG_CR_HP, 7, 1, 0),
};

static const struct snd_kcontrol_new jz4780_codec_input_controls[] = {
	SOC_DAPM_SINGLE("Mic Capture Switch", REG_CR_MIC1, 4, 1, 1),
};

static const struct snd_kcontrol_new jz4780_codec_output_controls[] = {
	SOC_DAPM_SINGLE("DAC Switch", REG_CR_DAC, 4, 1, 1),
};

static const struct snd_soc_dapm_widget jz4780_codec_dapm_widgets[] = {
	SND_SOC_DAPM_ADC("ADC", "Capture", REG_AICR_ADC, 4, 1),
	SND_SOC_DAPM_DAC("DAC", "Playback", REG_AICR_DAC, 4, 1),

	SND_SOC_DAPM_MUX("Mic Input Mux", SND_SOC_NOPM, 0, 0, &mic1_input_mux),

	SND_SOC_DAPM_MIXER_NAMED_CTL("Input Mixer", REG_CR_ADC, 4, 1,
			jz4780_codec_input_controls,
			ARRAY_SIZE(jz4780_codec_input_controls)),

	SND_SOC_DAPM_MIXER("Output Mixer", REG_CR_HP, 4, 1,
			jz4780_codec_output_controls,
			ARRAY_SIZE(jz4780_codec_output_controls)),

	SND_SOC_DAPM_MICBIAS("Mic Bias", REG_CR_MIC1, 5, 1),

	SND_SOC_DAPM_INPUT("AIP1"),
	SND_SOC_DAPM_INPUT("AIP2"),
	SND_SOC_DAPM_INPUT("AIP3"),

	SND_SOC_DAPM_OUTPUT("AOHPL"),
	SND_SOC_DAPM_OUTPUT("AOHPR"),
};

static const struct snd_soc_dapm_route jz4780_codec_dapm_routes[] = {
	{"ADC", NULL, "Input Mixer"},

	{"Input Mixer", "Mic Capture Switch", "Mic Input Mux"},

	{"Mic Input Mux", "AIP1", "AIP1"},
	{"Mic Input Mux", "AIP2", "AIP2"},

	{"Output Mixer", "DAC Switch", "DAC"},

	{"AOHPL", NULL, "Output Mixer"},
	{"AOHPR", NULL, "Output Mixer"},
};

static int jz4780_codec_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	uint32_t val;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;

	switch (params_rate(params)) {
	case 8000:
		val = 0;
		break;
	case 11025:
		val = 1;
		break;
	case 12000:
		val = 2;
		break;
	case 16000:
		val = 3;
		break;
	case 22050:
		val = 4;
		break;
	case 24000:
		val = 5;
		break;
	case 32000:
		val = 6;
		break;
	case 44100:
		val = 7;
		break;
	case 48000:
		val = 8;
		break;
	case 88200:
		val = 9;
		break;
	case 96000:
		val = 10;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_update_bits(codec, REG_FCR_DAC, REG_FCR_DAC_MASK, val);
	else
		snd_soc_update_bits(codec, REG_FCR_ADC, REG_FCR_DAC_MASK, val);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16:
		val = 0;
		break;
	case SNDRV_PCM_FORMAT_S18_3LE:
		val = 1;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val = 2;
		break;
	case SNDRV_PCM_FORMAT_S24:
		val = 3;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_update_bits(codec, REG_AICR_DAC,
				    REG_AICR_DAC_DAC_ADWL_MASK,
				    val << REG_AICR_DAC_DAC_ADWL_SHIFT);
	} else {
		snd_soc_update_bits(codec, REG_AICR_ADC,
				    REG_AICR_DAC_DAC_ADWL_MASK,
				    val << REG_AICR_DAC_DAC_ADWL_SHIFT);

		snd_soc_update_bits(codec, REG_CR_MIC1,
				    REG_CR_MIC1_MICSTEREO_MASK,
				    REG_CR_MIC1_MICSTEREO_MONO);
		snd_soc_update_bits(codec, REG_CR_ADC,
				    REG_CR_ADC_ADC_LEFT_ONLY_MASK,
				    REG_CR_ADC_ADC_LEFT_ONLY_RCH_INACTIVE);
	}

	return 0;
}

static int jz4780_codec_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	snd_soc_update_bits(codec, REG_CR_DAC, REG_CR_DAC_DAC_MUTE_MASK,
			    (!!mute) << REG_CR_DAC_DAC_MUTE_SHIFT);
	return 0;
}

static struct snd_soc_dai_ops jz4780_codec_dai_ops = {
	.hw_params = jz4780_codec_hw_params,
	.digital_mute = jz4780_codec_mute,
};

static struct snd_soc_dai_driver jz4780_codec_dai = {
	.name = "jz4780-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S18_3LE |
			SNDRV_PCM_FMTBIT_S20_3LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S18_3LE |
			SNDRV_PCM_FMTBIT_S20_3LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &jz4780_codec_dai_ops,
};

static int jz4780_codec_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_update_bits(codec, REG_CR_VIC, REG_CR_VIC_SB_SLEEP_MASK,
				    REG_CR_VIC_SB_SLEEP_NORMAL_MODE);
		/* From manual, max time to power up after sleep */
		msleep(900);
		break;
	case SND_SOC_BIAS_PREPARE:
		snd_soc_update_bits(codec, REG_CR_VIC, REG_CR_VIC_SB_MASK,
				    REG_CR_VIC_SB_NORMAL_MODE);
		snd_soc_update_bits(codec, REG_CR_VIC, REG_CR_VIC_SB_SLEEP_MASK,
				    REG_CR_VIC_SB_SLEEP_NORMAL_MODE);
		/* From manual, max time to power up after sleep */
		msleep(900);
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_update_bits(codec, REG_CR_VIC, REG_CR_VIC_SB_SLEEP_MASK,
				    REG_CR_VIC_SB_SLEEP_SLEEP_MODE);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, REG_CR_VIC, REG_CR_VIC_SB_MASK,
				    REG_CR_VIC_SB_POWERDOWN_MODE);
		break;
	default:
		break;
	}

	codec->dapm.bias_level = level;

	return 0;
}

static int jz4780_codec_dev_probe(struct snd_soc_codec *codec)
{
	struct jz4780_codec *jzc = snd_soc_codec_get_drvdata(codec);

	/* reset */
	writel(REG_RGADW_ICRST, jzc->base);
	udelay(2);
	writel(0, jzc->base);

	jz4780_codec_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* select I2S */
	snd_soc_update_bits(codec, REG_AICR_DAC, REG_AICR_DAC_AUDIOIF_MASK,
			    REG_AICR_DAC_AUDIOIF_I2S);
	snd_soc_update_bits(codec, REG_AICR_ADC, REG_AICR_ADC_AUDIOIF_MASK,
			    REG_AICR_ADC_AUDIOIF_I2S);

	/* select AIP2 input */
	snd_soc_update_bits(codec, REG_CR_MIC1, REG_CR_MIC1_MIC1_SEL_MASK,
			    REG_CR_MIC1_MIC1_SEL_AIP2);
	snd_soc_update_bits(codec, REG_CR_ADC, REG_CR_ADC_ADC_IN_SEL_MASK,
			    REG_CR_ADC_ADC_IN_SEL_AIN1);

	/* disable mixer */
	snd_soc_update_bits(codec, REG_CR_MIX, REG_CR_MIX_MIX_EN_MASK,
			    REG_CR_MIX_MIX_EN_OFF);

	return 0;
}

static int jz4780_codec_dev_remove(struct snd_soc_codec *codec)
{
	jz4780_codec_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int jz4780_codec_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	return jz4780_codec_set_bias_level(codec, SND_SOC_BIAS_OFF);
}

static int jz4780_codec_resume(struct snd_soc_codec *codec)
{
	return jz4780_codec_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
}

#else
#define jz4780_codec_suspend NULL
#define jz4780_codec_resume NULL
#endif

static struct snd_soc_codec_driver soc_codec_dev_jz4780_codec = {
	.probe = jz4780_codec_dev_probe,
	.remove = jz4780_codec_dev_remove,
	.suspend = jz4780_codec_suspend,
	.resume = jz4780_codec_resume,
	.set_bias_level = jz4780_codec_set_bias_level,
	.reg_cache_default = jz4780_codec_reg_defaults,
	.reg_word_size = sizeof(uint8_t),
	.reg_cache_size	= 0x40,

	.controls = jz4780_codec_controls,
	.num_controls = ARRAY_SIZE(jz4780_codec_controls),
	.dapm_widgets = jz4780_codec_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(jz4780_codec_dapm_widgets),
	.dapm_routes = jz4780_codec_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(jz4780_codec_dapm_routes),
};

static const struct of_device_id jz4780_of_matches[] = {
	{ .compatible = "ingenic,jz4780-codec", },
	{ /* sentinel */ }
};

static struct regmap_config jz4780_codec_regmap_config = {
	.reg_bits = 7,
	.val_bits = 8,

	.reg_read = jz4780_codec_read,
	.reg_write = jz4780_codec_write,

	.max_register = REG_DR_ADC_AGC,
	.reg_defaults = jz4780_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(jz4780_codec_reg_defaults),
	.cache_type = REGCACHE_NONE,
};

static int jz4780_codec_probe(struct platform_device *pdev)
{
	int ret;
	struct jz4780_codec *jz4780_codec;
	struct resource *mem;

	jz4780_codec = devm_kzalloc(&pdev->dev, sizeof(*jz4780_codec),
				    GFP_KERNEL);
	if (!jz4780_codec)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	jz4780_codec->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(jz4780_codec->base))
		return -PTR_ERR(jz4780_codec->base);

	jz4780_codec->mem = mem;

	jz4780_codec->clk = devm_clk_get(&pdev->dev, "i2s");
	if (IS_ERR(jz4780_codec->clk))
		return PTR_ERR(jz4780_codec->clk);

	clk_prepare(jz4780_codec->clk);

	jz4780_codec->regmap = devm_regmap_init(&pdev->dev,NULL,jz4780_codec,
		&jz4780_codec_regmap_config);
	if (IS_ERR(jz4780_codec->regmap))
		return PTR_ERR(jz4780_codec->regmap);

	platform_set_drvdata(pdev, jz4780_codec);

	ret = snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_jz4780_codec, &jz4780_codec_dai, 1);
	if (ret)
		goto out_err_register;

	return 0;

out_err_register:
	clk_unprepare(jz4780_codec->clk);

	return ret;
}

static int jz4780_codec_remove(struct platform_device *pdev)
{
	struct jz4780_codec *jz4780_codec = platform_get_drvdata(pdev);

	snd_soc_unregister_codec(&pdev->dev);

	clk_unprepare(jz4780_codec->clk);

	platform_set_drvdata(pdev, NULL);
	kfree(jz4780_codec);

	return 0;
}

static struct platform_driver jz4780_codec_driver = {
	.probe = jz4780_codec_probe,
	.remove = jz4780_codec_remove,
	.driver = {
		.name = "jz4780-codec",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(jz4780_of_matches)
	},
};

module_platform_driver(jz4780_codec_driver);

MODULE_AUTHOR("Paul Burton <paul.burton@imgtec.com>");
MODULE_DESCRIPTION("Ingenic JZ4780 ASoC codec driver");
MODULE_LICENSE("GPL");
