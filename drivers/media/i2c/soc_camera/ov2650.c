/*
 * V4L2 Driver for camera sensor ov2650
 *
 * Copyright (c) 2009 Intel Corporation.
 * Copyright (C) 2012, Ingenic Semiconductor Inc.
 * Copyright (C) 2015, Imagination Technologies Ltd.
 * Copyright (C) 2017, Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <media/v4l2-subdev.h>
#include <linux/clk.h>
#include <media/soc_camera.h>
#include <media/soc_mediabus.h>

#define REG_CHIP_ID_HIGH		0x300a
#define CHIP_ID_HIGH			0x26

#define SENSOR_WRITE_DELAY 0xffff
#define ENDMARKER { 0xff, 0xff }

/*
 * Struct
 */
struct regval_list {
	u16 reg_num;
	u16 value;
};

struct mode_list {
	u16 index;
	const struct regval_list *mode_regs;
};

/*
 * Supported resolutions
 */
enum ov2650_width {
	W_SXGA	= 1280,
	W_VGA	= 640,
	W_QVGA	= 320,
};

enum ov2650_height {
	H_SXGA	= 1024,
	H_VGA	= 480,
	H_QVGA	= 240,
};

struct ov2650_win_size {
	char *name;
	enum ov2650_width width;
	enum ov2650_height height;
	const struct regval_list *regs;
};

struct ov2650_priv {
	struct soc_camera_subdev_desc	ssdd;
	int				gpio_enable;
	int				gpio_reset;
	struct v4l2_subdev		subdev;
	struct ov2650_camera_info	*info;
	u32	cfmt_code;
	const struct ov2650_win_size	*win;
	struct v4l2_ctrl_handler	hdl;
	int				model;
};

static inline int sensor_i2c_master_send(struct i2c_client *client,
		const char *buf, int count)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (char *) buf;

	ret = i2c_transfer(adap, &msg, 1);

	/*
	 * If everything went ok (i.e. 1 msg transmitted), return #bytes
	 * transmitted, else error code.
	 */
	return (ret == 1) ? count : ret;
}

static inline int sensor_i2c_master_recv(struct i2c_client *client,
		char *buf, int count)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = buf;
	ret = i2c_transfer(adap, &msg, 1);

	/*
	 * If everything went ok (i.e. 1 msg transmitted), return #bytes
	 * transmitted, else error code.
	 */
	return (ret == 1) ? count : ret;
}

unsigned char ov2650_read_reg(struct i2c_client *client, unsigned short reg)
{
	int ret;
	unsigned char retval;
	u16 r = cpu_to_be16(reg);

	ret = sensor_i2c_master_send(client, (unsigned char*) &r, 2);

	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	ret = sensor_i2c_master_recv(client, &retval, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;
	return retval;
}

int ov2650_write_reg(struct i2c_client *client,unsigned short reg,
		unsigned char val)
{
	unsigned char msg[3];
	int ret;

	reg = cpu_to_be16(reg);

	memcpy(&msg[0], &reg, 2);
	memcpy(&msg[2], &val, 1);

	ret = sensor_i2c_master_send(client, msg, 3);

	if(reg == SENSOR_WRITE_DELAY)
		msleep(val);

	if (ret < 0)
	{
		dev_err(&client->dev, "RET<0\n");
		return ret;
	}
	if (ret < 3)
	{
		dev_err(&client->dev, "RET<3\n");
		return -EIO;
	}

	return 0;
}

/*
 * Registers settings
 */

static const struct regval_list ov2650_init_regs[] = {
	{0x3012, 0x80},
	{SENSOR_WRITE_DELAY, 0x0a},
	{0x308c, 0x80},
	{0x308d, 0x0e},
	{0x360b, 0x00},
	{0x30b0, 0xff},
	{0x30b1, 0xff},
	{0x30b2, 0x27},

	{0x300e, 0x34},
	{0x300f, 0xa6},
	{0x3010, 0x81},
	{0x3082, 0x01},
	{0x30f4, 0x01},
	{0x3090, 0x3b},
	{0x3091, 0xc0},
	{0x30ac, 0x42},

	{0x30d1, 0x08},
	{0x30a8, 0x56},
	{0x3015, 0x03},
	{0x3093, 0x00},
	{0x307e, 0xe5},
	{0x3079, 0x00},
	{0x30aa, 0x42},
	{0x3017, 0x40},
	{0x30f3, 0x82},
	{0x306a, 0x0c},
	{0x306d, 0x00},
	{0x336a, 0x3c},
	{0x3076, 0x6a},
	{0x30d9, 0x8c},
	{0x3016, 0x82},
	{0x3601, 0x30},
	{0x304e, 0x88},
	{0x30f1, 0x82},
	{0x3011, 0x02},

	{0x3013, 0xf7},
	{0x301c, 0x13},
	{0x301d, 0x17},
	{0x3070, 0x3e},
	{0x3072, 0x34},

	{0x30af, 0x00},
	{0x3048, 0x1f},
	{0x3049, 0x4e},
	{0x304a, 0x20},
	{0x304f, 0x20},
	{0x304b, 0x02},
	{0x304c, 0x00},
	{0x304d, 0x02},
	{0x304f, 0x20},
	{0x30a3, 0x10},
	{0x3013, 0xf7},
	{0x3014, 0x44},
	{0x3071, 0x00},
	{0x3070, 0x3e},
	{0x3073, 0x00},
	{0x3072, 0x34},
	{0x301c, 0x12},
	{0x301d, 0x16},
	{0x304d, 0x42},
	{0x304a, 0x40},
	{0x304f, 0x40},
	{0x3095, 0x07},
	{0x3096, 0x16},
	{0x3097, 0x1d},

	{0x3020, 0x01},
	{0x3021, 0x18},
	{0x3022, 0x00},
	{0x3023, 0x0a},
	{0x3024, 0x06},
	{0x3025, 0x58},
	{0x3026, 0x04},
	{0x3027, 0xbc},
	{0x3088, 0x06},
	{0x3089, 0x40},
	{0x308a, 0x04},
	{0x308b, 0xb0},
	{0x3316, 0x64},
	{0x3317, 0x4b},
	{0x3318, 0x00},
	{0x331a, 0x64},
	{0x331b, 0x4b},
	{0x331c, 0x00},
	{0x3100, 0x00},

	{0x3320, 0xfa},
	{0x3321, 0x11},
	{0x3322, 0x92},
	{0x3323, 0x01},
	{0x3324, 0x97},
	{0x3325, 0x02},
	{0x3326, 0xff},
	{0x3327, 0x0c},
	{0x3328, 0x10},
	{0x3329, 0x10},
	{0x332a, 0x58},
	{0x332b, 0x50},
	{0x332c, 0xbe},
	{0x332d, 0xe1},
	{0x332e, 0x43},
	{0x332f, 0x36},
	{0x3330, 0x4d},
	{0x3331, 0x44},
	{0x3332, 0xf8},
	{0x3333, 0x0a},
	{0x3334, 0xf0},
	{0x3335, 0xf0},
	{0x3336, 0xf0},
	{0x3337, 0x40},
	{0x3338, 0x40},
	{0x3339, 0x40},
	{0x333a, 0x00},
	{0x333b, 0x00},

	{0x3380, 0x28},
	{0x3381, 0x48},
	{0x3382, 0x10},
	{0x3383, 0x23},
	{0x3384, 0xc0},
	{0x3385, 0xe5},
	{0x3386, 0xc2},
	{0x3387, 0xb3},
	{0x3388, 0x0e},
	{0x3389, 0x98},
	{0x338a, 0x01},

	{0x3340, 0x0e},
	{0x3341, 0x1a},
	{0x3342, 0x31},
	{0x3343, 0x45},
	{0x3344, 0x5a},
	{0x3345, 0x69},
	{0x3346, 0x75},
	{0x3347, 0x7e},
	{0x3348, 0x88},
	{0x3349, 0x96},
	{0x334a, 0xa3},
	{0x334b, 0xaf},
	{0x334c, 0xc4},
	{0x334d, 0xd7},
	{0x334e, 0xe8},
	{0x334f, 0x20},

	{0x3350, 0x32},
	{0x3351, 0x25},
	{0x3352, 0x80},
	{0x3353, 0x1e},
	{0x3354, 0x00},
	{0x3355, 0x85},
	{0x3356, 0x32},
	{0x3357, 0x25},
	{0x3358, 0x80},
	{0x3359, 0x1b},
	{0x335a, 0x00},
	{0x335b, 0x85},
	{0x335c, 0x32},
	{0x335d, 0x25},
	{0x335e, 0x80},
	{0x335f, 0x1b},
	{0x3360, 0x00},
	{0x3361, 0x85},
	{0x3363, 0x70},
	{0x3364, 0x7f},
	{0x3365, 0x00},
	{0x3366, 0x00},

	{0x3301, 0xff},
	{0x338B, 0x11},
	{0x338c, 0x10},
	{0x338d, 0x40},

	{0x3370, 0xd0},
	{0x3371, 0x00},
	{0x3372, 0x00},
	{0x3373, 0x40},
	{0x3374, 0x10},
	{0x3375, 0x10},
	{0x3376, 0x04},
	{0x3377, 0x00},
	{0x3378, 0x04},
	{0x3379, 0x80},

	{0x3069, 0x84},
	{0x307c, 0x10},
	{0x3087, 0x02},

	{0x3300, 0xfc},
	{0x3302, 0x01},
	{0x3400, 0x00},
	{0x3606, 0x20},
	{0x3601, 0x30},
	{0x30f3, 0x83},
	{0x304e, 0x88},

	{0x3086, 0x0f},
	{0x3086, 0x00},

	{SENSOR_WRITE_DELAY, 0xff},
	ENDMARKER,
};

static const struct regval_list ov2650_res_sxga[] = {
	{0x3011, 0x02},

	{0x3020, 0x01},
	{0x3021, 0x18},
	{0x3022, 0x00},
	{0x3023, 0x0a},
	{0x3024, 0x06},
	{0x3025, 0x58},
	{0x3026, 0x04},
	{0x3027, 0xbc},
	{0x3088, 0x05},
	{0x3089, 0x00},
	{0x308a, 0x04},
	{0x308b, 0x00},
	{0x3316, 0x64},
	{0x3317, 0x4b},
	{0x3318, 0x00},
	{0x331a, 0x50},
	{0x331b, 0x40},
	{0x331c, 0x00},

	{0x3302, 0x11},

	{0x3014, 0x84},
	{0x301c, 0x13},
	{0x301d, 0x17},
	{0x3070, 0x40},
	{0x3071, 0x00},
	{0x3072, 0x36},
	{0x3073, 0x00},

	{0x3086, 0x0f},
	{0x3086, 0x00},

	{SENSOR_WRITE_DELAY, 0xff},
	ENDMARKER,
};

static const struct regval_list ov2650_res_vga_vario[] = {
	{0x306f, 0x14},
	{0x302a, 0x02},
	{0x302b, 0x6a},
	{0x3012, 0x10},
	{0x3011, 0x01},

	{0x3070, 0x5d},
	{0x3072, 0x4d},

	{0x301c, 0x05},
	{0x301d, 0x06},

	{0x3020, 0x01},
	{0x3021, 0x18},
	{0x3022, 0x00},
	{0x3023, 0x06},
	{0x3024, 0x06},
	{0x3025, 0x58},
	{0x3026, 0x02},
	{0x3027, 0x61},
	{0x3088, 0x02},
	{0x3089, 0x80},
	{0x308a, 0x01},
	{0x308b, 0xe0},
	{0x3316, 0x64},
	{0x3317, 0x25},
	{0x3318, 0x80},
	{0x3319, 0x08},
	{0x331a, 0x28},
	{0x331b, 0x1e},
	{0x331c, 0x00},
	{0x331d, 0x38},
	{0x3100, 0x00},

	{0x3302, 0x11},
	{0x3011, 0x00},

	{0x3014, 0x84},
	{0x3086, 0x0f},
	{0x3086, 0x00},

	{SENSOR_WRITE_DELAY, 0xff},
	ENDMARKER,
};

static const struct regval_list ov2650_res_qvga[] = {
	{0x306f, 0x14},
	{0x302a, 0x02},
	{0x302b, 0x6a},

	{0x3012, 0x10},
	{0x3011, 0x01},

	{0x3070, 0x5d},
	{0x3072, 0x4d},
	{0x301c, 0x05},
	{0x301d, 0x06},

	{0x3023, 0x06},
	{0x3026, 0x02},
	{0x3027, 0x61},
	{0x3088, 0x01},
	{0x3089, 0x40},
	{0x308a, 0x00},
	{0x308b, 0xf0},
	{0x3316, 0x64},
	{0x3317, 0x25},
	{0x3318, 0x80},
	{0x3319, 0x08},
	{0x331a, 0x14},
	{0x331b, 0x0f},
	{0x331c, 0x00},
	{0x331d, 0x38},
	{0x3100, 0x00},

	{0x3015, 0x02},
	{0x3014, 0x84},
	{0x3302, 0x11},
	{0x3086, 0x0f},
	{0x3086, 0x00},

	{SENSOR_WRITE_DELAY, 0xff},
	ENDMARKER,
};

#define OV2650_SIZE(n, w, h, r) \
	{.name = n, .width = w , .height = h, .regs = r }

static struct ov2650_win_size ov2650_supported_win_sizes[] = {
	OV2650_SIZE("SXGA", W_SXGA, H_SXGA, ov2650_res_sxga),
	OV2650_SIZE("VGA", W_VGA, H_VGA, ov2650_res_vga_vario),
	OV2650_SIZE("QVGA", W_QVGA, H_QVGA, ov2650_res_qvga),
};

#define N_WIN_SIZES (ARRAY_SIZE(ov2650_supported_win_sizes))

static u32 ov2650_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
};

/*
 * General functions
 */
static struct ov2650_priv *to_ov2650(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov2650_priv,
			subdev);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov2650_priv, hdl)->subdev;
}

static int ov2650_write_array(struct i2c_client *client,
		const struct regval_list *vals)
{
	int ret;

	while ((vals->reg_num != 0xff) || (vals->value != 0xff)) {
		dev_vdbg(&client->dev, "set reg0x%02x = 0x%02x",
			 vals->reg_num, vals->value);

		ret = ov2650_write_reg(client, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	dev_dbg(&client->dev, "%s config written\n", __func__);
	return 0;
}

/*
 * soc_camera_ops functions
 */

static int ov2650_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	if (enable) {
		ret = ov2650_write_reg(client, 0x3086, 0x00);
	} else {
		ret = ov2650_write_reg(client, 0x3086, 0x0f);
	}

	msleep(20);

	return ret;
}

 /*
 * Select the nearest higher resolution for capture
 */
static const struct ov2650_win_size *ov2650_select_win(u32 *width, u32 *height)
{
	int i, default_size = ARRAY_SIZE(ov2650_supported_win_sizes) - 1;

	for (i = 0; i < ARRAY_SIZE(ov2650_supported_win_sizes); i++) {
		if ((*width >= ov2650_supported_win_sizes[i].width) &&
			(*height >= ov2650_supported_win_sizes[i].height)) {
			*width = ov2650_supported_win_sizes[i].width;
			*height = ov2650_supported_win_sizes[i].height;
			return &ov2650_supported_win_sizes[i];
		}
	}

	*width = ov2650_supported_win_sizes[default_size].width;
	*height = ov2650_supported_win_sizes[default_size].height;
	return &ov2650_supported_win_sizes[default_size];
}

static int ov2650_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2650_priv *priv = to_ov2650(client);
	struct soc_camera_subdev_desc *ssdd = &priv->ssdd;
	int ret;

	dev_dbg(&client->dev, "%s(%d)\n", __func__, on);

	if (!on) {
		gpio_direction_output(priv->gpio_enable, 1);
		ret = soc_camera_power_off(&client->dev, ssdd, NULL);
		msleep(50);
		return ret;
	}

	ret = soc_camera_power_on(&client->dev, ssdd, NULL);
	if (ret < 0)
		return ret;

	if (priv->gpio_enable) {
		gpio_direction_output(priv->gpio_enable, 0);
		dev_dbg(&client->dev, "Enabled power GPIO\n");
	}
	if (priv->gpio_reset) {
		gpio_direction_output(priv->gpio_reset, 1);
		dev_dbg(&client->dev, "Enabled reset GPIO\n");
	}

	msleep(100);

	return ret;
}

static int ov2650_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2650_priv *priv = to_ov2650(client);

	if (format->pad)
		return -EINVAL;

	mf->width	= priv->win->width;
	mf->height	= priv->win->height;

	mf->code	= priv->cfmt_code;
	mf->colorspace	= V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov2650_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	/*
	 * Current do not support set format, use unify format yuv422i
	 */
	const struct ov2650_win_size *win;
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov2650_priv *priv = to_ov2650(client);
	int ret;

	/*
	 * select suitable win
	 */
	win = ov2650_select_win(&mf->width, &mf->height);

	if (mf->field == V4L2_FIELD_ANY) {
		mf->field = V4L2_FIELD_NONE;
	} else if (mf->field != V4L2_FIELD_NONE) {
		dev_err(&client->dev, "Field type invalid.\n");
		return -ENODEV;
	}

	switch (mf->code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
		mf->colorspace = V4L2_COLORSPACE_JPEG;
		break;

	default:
		mf->code = MEDIA_BUS_FMT_YUYV8_2X8;
		break;
	}

	priv->win = ov2650_select_win(&mf->width, &mf->height);

	/*
	 * Initialize the sensor with default data
	 */
	ret = ov2650_write_array(client, ov2650_init_regs);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Error (%d)\n", __func__, ret);
		return ret;
	}

	/*
	 * Set size win
	 */
	if(priv->win->regs) {
		ret = ov2650_write_array(client, priv->win->regs);
		dev_dbg(&client->dev, "%s: Set size %dx%d\n", __func__,
			mf->width, mf->height);
		if (ret < 0) {
			dev_err(&client->dev, "%s: Error (%d)\n", __func__, ret);
			return ret;
		}
	}

	return 0;
}

static int ov2650_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov2650_codes))
		return -EINVAL;

	code->code = ov2650_codes[code->index];
	return 0;
}

static int ov2650_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_FALLING |
			V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_HIGH |
			V4L2_MBUS_DATA_ACTIVE_HIGH |
			V4L2_MBUS_MASTER;

	return 0;
}

/*
 * Frame intervals. Since frame rates are controlled with the clock
 * divider, we can only do 30/n for integer n values. So no continuous
 * or stepwise options. Here we just pick a handful of logical values.
 */

static int ov2650_frame_rates[] = { 30, 15, 10, 5, 1 };

static int ov2650_enum_frameinterval(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->pad || fie->index >= ARRAY_SIZE(ov2650_frame_rates))
		return -EINVAL;
	fie->interval.numerator = 1;
	fie->interval.denominator = ov2650_frame_rates[fie->index];
	return 0;
}


/*
 * Frame size enumeration
 */
static int ov2650_enum_framesize(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_frame_size_enum *fse)
{
	int i;
	int num_valid = -1;
	__u32 index = fse->index;
	struct ov2650_win_size *win;

	if (fse->pad)
		return -EINVAL;

	for (i = 0; i < N_WIN_SIZES; i++) {
		win = &ov2650_supported_win_sizes[index];
		if (index == ++num_valid) {
			fse->min_width = fse->max_width = win->width;
			fse->min_height = fse->max_height = win->height;
			return 0;
		}
	}

	return -EINVAL;
}

static int ov2650_video_probe(struct i2c_client *client)
{
	unsigned char retval_high = 0;
	struct ov2650_priv *priv = to_ov2650(client);

	/*
	 * check and show product ID and manufacturer ID
	 */
	ov2650_s_power(&priv->subdev, 1);
	retval_high = ov2650_read_reg(client, REG_CHIP_ID_HIGH);
	if (retval_high != CHIP_ID_HIGH) {
		dev_err(&client->dev, "unknown sensor chip_id %xxx\n",
			retval_high);
		return -1;
	}

	ov2650_s_power(&priv->subdev, 0);
	dev_info(&client->dev, "detected sensor id 0x%xxx\n",retval_high);

	return 0;
}

static struct v4l2_subdev_core_ops ov2650_subdev_core_ops = {
	.s_power	= ov2650_s_power,
};

static struct v4l2_subdev_video_ops ov2650_subdev_video_ops = {
	.g_mbus_config	= ov2650_g_mbus_config,
	.s_stream	= ov2650_s_stream,
};

static const struct v4l2_subdev_pad_ops ov2650_pad_ops = {
	.enum_mbus_code	= ov2650_enum_mbus_code,
	.set_fmt	= ov2650_set_fmt,
	.get_fmt	= ov2650_get_fmt,
	.enum_frame_interval	= ov2650_enum_frameinterval,
	.enum_frame_size	= ov2650_enum_framesize,
 };

static struct v4l2_subdev_ops ov2650_subdev_ops = {
	.core	= &ov2650_subdev_core_ops,
	.video	= &ov2650_subdev_video_ops,
	.pad	= &ov2650_pad_ops,
};

/*
 * i2c_driver functions
 */

static int ov2650_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct device_node *np = client->dev.of_node;
	struct ov2650_priv *priv;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct v4l2_subdev_platform_data *sd_pdata;
	int gpio_reset;
	int gpio_enable;
	enum of_gpio_flags flags;
	int ret = 0;

	if (!np) {
		dev_err(&client->dev, "No devicetree data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE
			| I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "client not i2c capable\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct ov2650_priv),
				GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sd_pdata = &priv->ssdd.sd_pdata;
	sd_pdata->num_regulators = 1;
	sd_pdata->regulators = devm_kzalloc(&client->dev,
					sizeof(struct regulator_bulk_data),
					GFP_KERNEL);
	if (!sd_pdata->regulators) {
		dev_err(&adapter->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	/*
	 * Get and turn on the supplies
	 */
	sd_pdata->regulators[0].supply = "core";
	ret = devm_regulator_bulk_get(&client->dev, sd_pdata->num_regulators,
			sd_pdata->regulators);
	if (ret) {
		dev_err(&client->dev, "Failed to get supplies: %d\n", ret);
		devm_kfree(&client->dev, priv);
		return ret;
	}

	gpio_reset = of_get_named_gpio_flags(np, "gpio-reset", 0, &flags);
	if (gpio_is_valid(gpio_reset)) {
		ret = devm_gpio_request_one(&client->dev, gpio_reset, flags,
					"ov2650_reset");
		if (ret) {
			dev_err(&client->dev, "failed to request reset gpio %d: %d\n",
				gpio_reset, ret);
			return -ENODEV;
		}
		priv->gpio_reset = gpio_reset;
	}

	gpio_enable = of_get_named_gpio_flags(np, "gpio-enable", 0, &flags);
	if (gpio_is_valid(gpio_enable)) {
		ret = devm_gpio_request_one(&client->dev, gpio_enable, flags,
					"ov2650_enable");
		if (ret) {
			dev_err(&client->dev, "failed to request enable gpio %d: %d\n",
				gpio_enable, ret);
			return -ENODEV;
		}
		priv->gpio_enable = gpio_enable;
	} else {
		priv->gpio_enable = 0;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov2650_subdev_ops);

	ret = ov2650_s_power(&priv->subdev, 1);
	if (ret) {
		dev_err(&client->dev, "Failed to power on camera\n");
		return ret;
	}

	/*
	 * Default window size
	 */
	priv->win = &ov2650_supported_win_sizes[0];
	priv->cfmt_code = MEDIA_BUS_FMT_YUYV8_2X8;

	/*
	 * Register controls
	 */
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error)
		return priv->hdl.error;

	dev_dbg(&client->dev, "Probing for ov2650\n");
	ret = ov2650_video_probe(client);

	if (ret) {
		v4l2_ctrl_handler_free(&priv->hdl);
		devm_kfree(&client->dev, sd_pdata->regulators);
		devm_kfree(&client->dev, priv);
		ret = -EPROBE_DEFER;
	} else {
		if (v4l2_ctrl_handler_setup(&priv->hdl) < 0)
			dev_err(&client->dev, "Error setting up control handler\n");

		/*
		 * Turn camera off until it's required
		 */
		ov2650_s_power(&priv->subdev, 0);

		/*
		 * Register the subdevice
		 */
		ret = v4l2_async_register_subdev(&priv->subdev);
	}

	return ret;
}

static int ov2650_remove(struct i2c_client *client)
{
	struct ov2650_priv *priv = to_ov2650(client);

	devm_kfree(&client->dev, priv);
	return 0;
}

static const struct of_device_id ov2650_of_match[] = {
	{ .compatible = "omnivision,ov2650", },
	{},
};
MODULE_DEVICE_TABLE(of, ov2650_of_match);

static const struct i2c_device_id ov2650_id[] = {
	{ "ov2650", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov2650_id);

static struct i2c_driver ov2650_i2c_driver = {
	.driver = {
		.name = "ov2650",
		.of_match_table = of_match_ptr(ov2650_of_match),
	},
	.probe		= ov2650_probe,
	.remove		= ov2650_remove,
	.id_table	= ov2650_id,
};

/*
 * Module functions
 */
static int __init ov2650_module_init(void)
{
	return i2c_add_driver(&ov2650_i2c_driver);
}

static void __exit ov2650_module_exit(void)
{
	i2c_del_driver(&ov2650_i2c_driver);
}

module_init(ov2650_module_init);
module_exit(ov2650_module_exit);

MODULE_DESCRIPTION("camera sensor ov2650 driver");
MODULE_AUTHOR("YeFei <feiye@ingenic.cn>");
MODULE_LICENSE("GPL");
