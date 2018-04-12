/*
 * V4L2 Driver for camera sensor ov7675
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
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

/*
 * Registers
 */
#define REG_GAIN	0x00	/* Gain lower 8 bits (rest in vref) */
#define REG_BLUE	0x01	/* blue gain */
#define REG_RED		0x02	/* red gain */
#define REG_VREF	0x03	/* Pieces of GAIN, VSTART, VSTOP */
#define REG_COM1	0x04	/* Control 1 */
#define  COM1_CCIR656	  0x40  /* CCIR656 enable */
#define REG_BAVE	0x05	/* U/B Average level */
#define REG_GbAVE	0x06	/* Y/Gb Average level */
#define REG_AECHH	0x07	/* AEC MS 5 bits */
#define REG_RAVE	0x08	/* V/R Average level */
#define REG_COM2	0x09	/* Control 2 */
#define  COM2_SSLEEP	  0x10	/* Soft sleep mode */
#define REG_PID		0x0a	/* Product ID MSB */
#define REG_VER		0x0b	/* Product ID LSB */
#define REG_COM3	0x0c	/* Control 3 */
#define  COM3_SWAP	  0x40	  /* Byte swap */
#define  COM3_SCALEEN	  0x08	  /* Enable scaling */
#define  COM3_DCWEN	  0x04	  /* Enable downsamp/crop/window */
#define REG_COM4	0x0d	/* Control 4 */
#define REG_COM5	0x0e	/* All "reserved" */
#define REG_COM6	0x0f	/* Control 6 */
#define REG_AECH	0x10	/* More bits of AEC value */
#define REG_CLKRC	0x11	/* Clocl control */
#define   CLK_EXT	  0x40	  /* Use external clock directly */
#define   CLK_SCALE	  0x3f	  /* Mask for internal clock scale */
#define REG_COM7	0x12	/* Control 7 */
#define   COM7_RESET	  0x80	  /* Register reset */
#define   COM7_FMT_MASK	  0x38
#define   COM7_FMT_VGA	  0x00
#define	  COM7_FMT_CIF	  0x20	  /* CIF format */
#define   COM7_FMT_QVGA	  0x10	  /* QVGA format */
#define   COM7_FMT_QCIF	  0x08	  /* QCIF format */
#define	  COM7_RGB	  0x04	  /* bits 0 and 2 - RGB format */
#define	  COM7_YUV	  0x00	  /* YUV */
#define	  COM7_BAYER	  0x01	  /* Bayer format */
#define	  COM7_PBAYER	  0x05	  /* "Processed bayer" */
#define REG_COM8	0x13	/* Control 8 */
#define   COM8_FASTAEC	  0x80	  /* Enable fast AGC/AEC */
#define   COM8_AECSTEP	  0x40	  /* Unlimited AEC step size */
#define   COM8_BFILT	  0x20	  /* Band filter enable */
#define   COM8_AGC	  0x04	  /* Auto gain enable */
#define   COM8_AWB	  0x02	  /* White balance enable */
#define   COM8_AEC	  0x01	  /* Auto exposure enable */
#define REG_COM9	0x14	/* Control 9  - gain ceiling */
#define REG_COM10	0x15	/* Control 10 */
#define   COM10_HSYNC	  0x40	  /* HSYNC instead of HREF */
#define   COM10_PCLK_HB	  0x20	  /* Suppress PCLK on horiz blank */
#define   COM10_HREF_REV  0x08	  /* Reverse HREF */
#define   COM10_VS_LEAD	  0x04	  /* VSYNC on clock leading edge */
#define   COM10_VS_NEG	  0x02	  /* VSYNC negative */
#define   COM10_HS_NEG	  0x01	  /* HSYNC negative */
#define REG_HSTART	0x17	/* Horiz start high bits */
#define REG_HSTOP	0x18	/* Horiz stop high bits */
#define REG_VSTART	0x19	/* Vert start high bits */
#define REG_VSTOP	0x1a	/* Vert stop high bits */
#define REG_PSHFT	0x1b	/* Pixel delay after HREF */
#define REG_MIDH	0x1c	/* Manuf. ID high */
#define REG_MIDL	0x1d	/* Manuf. ID low */
#define REG_MVFP	0x1e	/* Mirror / vflip */
#define   MVFP_MIRROR	  0x20	  /* Mirror image */
#define   MVFP_FLIP	  0x10	  /* Vertical flip */

#define REG_AEW		0x24	/* AGC upper limit */
#define REG_AEB		0x25	/* AGC lower limit */
#define REG_VPT		0x26	/* AGC/AEC fast mode op region */
#define REG_HSYST	0x30	/* HSYNC rising edge delay */
#define REG_HSYEN	0x31	/* HSYNC falling edge delay */
#define REG_HREF	0x32	/* HREF pieces */
#define REG_TSLB	0x3a	/* lots of stuff */
#define   TSLB_YLAST	  0x04	  /* UYVY or VYUY - see com13 */
#define REG_COM11	0x3b	/* Control 11 */
#define   COM11_NIGHT	  0x80	  /* NIght mode enable */
#define   COM11_NMFR	  0x60	  /* Two bit NM frame rate */
#define   COM11_HZAUTO	  0x10	  /* Auto detect 50/60 Hz */
#define	  COM11_50HZ	  0x08	  /* Manual 50Hz select */
#define   COM11_EXP	  0x02
#define REG_COM12	0x3c	/* Control 12 */
#define   COM12_HREF	  0x80	  /* HREF always */
#define REG_COM13	0x3d	/* Control 13 */
#define   COM13_GAMMA	  0x80	  /* Gamma enable */
#define	  COM13_UVSAT	  0x40	  /* UV saturation auto adjustment */
#define   COM13_UVSWAP	  0x01	  /* V before U - w/TSLB */
#define REG_COM14	0x3e	/* Control 14 */
#define   COM14_DCWEN	  0x10	  /* DCW/PCLK-scale enable */
#define REG_EDGE	0x3f	/* Edge enhancement factor */
#define REG_COM15	0x40	/* Control 15 */
#define   COM15_R10F0	  0x00	  /* Data range 10 to F0 */
#define	  COM15_R01FE	  0x80	  /*            01 to FE */
#define   COM15_R00FF	  0xc0	  /*            00 to FF */
#define   COM15_RGB565	  0x10	  /* RGB565 output */
#define   COM15_RGB555	  0x30	  /* RGB555 output */
#define REG_COM16	0x41	/* Control 16 */
#define   COM16_AWBGAIN   0x08	  /* AWB gain enable */
#define REG_COM17	0x42	/* Control 17 */
#define   COM17_AECWIN	  0xc0	  /* AEC window - must match COM4 */
#define   COM17_CBAR	  0x08	  /* DSP Color bar */

/*
 * This matrix defines how the colors are generated, must be
 * tweaked to adjust hue and saturation.
 *
 * Order: v-red, v-green, v-blue, u-red, u-green, u-blue
 *
 * They are nine-bit signed quantities, with the sign bit
 * stored in 0x58.  Sign for v-red is bit 0, and up from there.
 */
#define	REG_CMATRIX_BASE 0x4f
#define   CMATRIX_LEN 6
#define REG_CMATRIX_SIGN 0x58


#define REG_BRIGHT	0x55	/* Brightness */
#define REG_CONTRAS	0x56	/* Contrast control */

#define REG_GFIX	0x69	/* Fix gain control */

#define REG_DBLV	0x6b	/* PLL control an debugging */
#define   DBLV_BYPASS	  0x00	  /* Bypass PLL */
#define   DBLV_X4	  0x01	  /* clock x4 */
#define   DBLV_X6	  0x10	  /* clock x6 */
#define   DBLV_X8	  0x11	  /* clock x8 */

#define REG_REG76	0x76	/* OV's name */
#define   R76_BLKPCOR	  0x80	  /* Black pixel correction enable */
#define   R76_WHTPCOR	  0x40	  /* White pixel correction enable */

#define REG_RGB444	0x8c	/* RGB 444 control */
#define   R444_ENABLE	  0x02	  /* Turn on RGB444, overrides 5x5 */
#define   R444_RGBX	  0x01	  /* Empty nibble at end */

#define REG_HAECC1	0x9f	/* Hist AEC/AGC control 1 */
#define REG_HAECC2	0xa0	/* Hist AEC/AGC control 2 */

#define REG_BD50MAX	0xa5	/* 50hz banding step limit */
#define REG_HAECC3	0xa6	/* Hist AEC/AGC control 3 */
#define REG_HAECC4	0xa7	/* Hist AEC/AGC control 4 */
#define REG_HAECC5	0xa8	/* Hist AEC/AGC control 5 */
#define REG_HAECC6	0xa9	/* Hist AEC/AGC control 6 */
#define REG_HAECC7	0xaa	/* Hist AEC/AGC control 7 */
#define REG_BD60MAX	0xab	/* 60hz banding step limit */

#define REG_CHIP_ID_HIGH               0x0a
#define CHIP_ID_HIGH                   0x76

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
enum ov7675_width {
	W_VGA	= 640,
	W_QVGA	= 320,
};

enum ov7675_height {
	H_VGA	= 480,
	H_QVGA	= 240,
};

struct ov7675_win_size {
	char *name;
	enum ov7675_width width;
	enum ov7675_height height;
	const struct regval_list *regs;
};

struct ov7675_priv {
	struct soc_camera_subdev_desc	ssdd;
	int				gpio_enable;
	struct v4l2_subdev		subdev;
	struct ov7675_camera_info	*info;
	u32	cfmt_code;
	const struct ov7675_win_size	*win;
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

unsigned char ov7675_read_reg(struct i2c_client *client, unsigned char reg)
{
	int ret;
	unsigned char retval;

	ret = sensor_i2c_master_send(client, &reg, 1);

	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	ret = sensor_i2c_master_recv(client, &retval, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;
	return retval;
}

int ov7675_write_reg(struct i2c_client *client, unsigned char reg,
		unsigned char val)
{
	unsigned char msg[2];
	int ret;

	memcpy(&msg[0], &reg, 1);
	memcpy(&msg[1], &val, 1);

	ret = sensor_i2c_master_send(client, msg, 2);

	if(reg == REG_COM7 && val == COM7_RESET)
		msleep(10);

	if (ret < 0)
	{
		dev_err(&client->dev, "RET<0\n");
		return ret;
	}
	if (ret < 2)
	{
		dev_err(&client->dev, "RET<2\n");
		return -EIO;
	}

	return 0;
}

/*
 * Registers settings
 */

#define ENDMARKER { 0xff, 0xff }

static const struct regval_list ov7675_init_regs[] = {
{ REG_COM7, COM7_RESET },
/*
 * Clock scale: 3 = 15fps
 *		2 = 20fps
 *		1 = 30fps
 */
	{ REG_CLKRC, 0x1 },	/* OV: clock scale (30 fps) */
	{ REG_TSLB, 0x04 },	/* OV */
	{ REG_COM7, 0 },	/* VGA */
	/*
	 * Set the hardware window. These values from OV don't entirely
	 * make sense - hstop is less than hstart. But they work...
	 */
	{ REG_HSTART, 0x13 },	{ REG_HSTOP, 0x01 },
	{ REG_HREF, 0xb6 },	{ REG_VSTART, 0x02 },
	{ REG_VSTOP, 0x7a },	{ REG_VREF, 0x0a },

	{ REG_COM3, 0 },	{ REG_COM14, 0 },
	/* Mystery scaling numbers */
	{ 0x70, 0x3a },		{ 0x71, 0x35 },
	{ 0x72, 0x11 },		{ 0x73, 0xf0 },
	{ 0xa2, 0x02 },		{ REG_COM10, 0x0 },

	/* Gamma curve values */
	{ 0x7a, 0x20 },		{ 0x7b, 0x10 },
	{ 0x7c, 0x1e },		{ 0x7d, 0x35 },
	{ 0x7e, 0x5a },		{ 0x7f, 0x69 },
	{ 0x80, 0x76 },		{ 0x81, 0x80 },
	{ 0x82, 0x88 },		{ 0x83, 0x8f },
	{ 0x84, 0x96 },		{ 0x85, 0xa3 },
	{ 0x86, 0xaf },		{ 0x87, 0xc4 },
	{ 0x88, 0xd7 },		{ 0x89, 0xe8 },

	/*
	 * AGC and AEC parameters. Note we start by disabling those features,
	 * then turn them only after tweaking the values.
	 */
	{ REG_COM8, COM8_FASTAEC | COM8_AECSTEP | COM8_BFILT },
	{ REG_GAIN, 0 },	{ REG_AECH, 0 },
	{ REG_COM4, 0x40 }, /* magic reserved bit */
	{ REG_COM9, 0x18 }, /* 4x gain + magic rsvd bit */
	{ REG_BD50MAX, 0x05 },	{ REG_BD60MAX, 0x07 },
	{ REG_AEW, 0x95 },	{ REG_AEB, 0x33 },
	{ REG_VPT, 0xe3 },	{ REG_HAECC1, 0x78 },
	{ REG_HAECC2, 0x68 },	{ 0xa1, 0x03 }, /* magic */
	{ REG_HAECC3, 0xd8 },	{ REG_HAECC4, 0xd8 },
	{ REG_HAECC5, 0xf0 },	{ REG_HAECC6, 0x90 },
	{ REG_HAECC7, 0x94 },
	{ REG_COM8, COM8_FASTAEC|COM8_AECSTEP|COM8_BFILT|COM8_AGC|COM8_AEC },

	/* Almost all of these are magic "reserved" values. */
	{ REG_COM5, 0x61 },	{ REG_COM6, 0x4b },
	{ 0x16, 0x02 },		{ REG_MVFP, 0x07 },
	{ 0x21, 0x02 },		{ 0x22, 0x91 },
	{ 0x29, 0x07 },		{ 0x33, 0x0b },
	{ 0x35, 0x0b },		{ 0x37, 0x1d },
	{ 0x38, 0x71 },		{ 0x39, 0x2a },
	{ REG_COM12, 0x78 },	{ 0x4d, 0x40 },
	{ 0x4e, 0x20 },		{ REG_GFIX, 0 },
	{ 0x6b, 0x4a },		{ 0x74, 0x10 },
	{ 0x8d, 0x4f },		{ 0x8e, 0 },
	{ 0x8f, 0 },		{ 0x90, 0 },
	{ 0x91, 0 },		{ 0x96, 0 },
	{ 0x9a, 0 },		{ 0xb0, 0x84 },
	{ 0xb1, 0x0c },		{ 0xb2, 0x0e },
	{ 0xb3, 0x82 },		{ 0xb8, 0x0a },

	/* More reserved magic, some of which tweaks white balance */
	{ 0x43, 0x0a },		{ 0x44, 0xf0 },
	{ 0x45, 0x34 },		{ 0x46, 0x58 },
	{ 0x47, 0x28 },		{ 0x48, 0x3a },
	{ 0x59, 0x88 },		{ 0x5a, 0x88 },
	{ 0x5b, 0x44 },		{ 0x5c, 0x67 },
	{ 0x5d, 0x49 },		{ 0x5e, 0x0e },
	{ 0x6c, 0x0a },		{ 0x6d, 0x55 },
	{ 0x6e, 0x11 },		{ 0x6f, 0x9f }, /* "9e for advance AWB" */
	{ 0x6a, 0x40 },		{ REG_BLUE, 0x40 },
	{ REG_RED, 0x60 },
	{ REG_COM8, COM8_FASTAEC|COM8_AECSTEP|COM8_BFILT|COM8_AGC|COM8_AEC|COM8_AWB },

	/* Matrix coefficients */
	{ 0x4f, 0x80 },		{ 0x50, 0x80 },
	{ 0x51, 0 },		{ 0x52, 0x22 },
	{ 0x53, 0x5e },		{ 0x54, 0x80 },
	{ 0x58, 0x9e },

	{ REG_COM16, COM16_AWBGAIN },	{ REG_EDGE, 0 },
	{ 0x75, 0x05 },		{ 0x76, 0xe1 },
	{ 0x4c, 0 },		{ 0x77, 0x01 },
	{ REG_COM13, 0xc3 },	{ 0x4b, 0x09 },
	{ 0xc9, 0x60 },		{ REG_COM16, 0x38 },
	{ 0x56, 0x40 },

	{ 0x34, 0x11 },		{ REG_COM11, COM11_EXP|COM11_HZAUTO },
	{ 0xa4, 0x88 },		{ 0x96, 0 },
	{ 0x97, 0x30 },		{ 0x98, 0x20 },
	{ 0x99, 0x30 },		{ 0x9a, 0x84 },
	{ 0x9b, 0x29 },		{ 0x9c, 0x03 },
	{ 0x9d, 0x4c },		{ 0x9e, 0x3f },
	{ 0x78, 0x04 },

	/* Extra-weird stuff. Some sort of multiplexor register */
	{ 0x79, 0x01 },		{ 0xc8, 0xf0 },
	{ 0x79, 0x0f },		{ 0xc8, 0x00 },
	{ 0x79, 0x10 },		{ 0xc8, 0x7e },
	{ 0x79, 0x0a },		{ 0xc8, 0x80 },
	{ 0x79, 0x0b },		{ 0xc8, 0x01 },
	{ 0x79, 0x0c },		{ 0xc8, 0x0f },
	{ 0x79, 0x0d },		{ 0xc8, 0x20 },
	{ 0x79, 0x09 },		{ 0xc8, 0x80 },
	{ 0x79, 0x02 },		{ 0xc8, 0xc0 },
	{ 0x79, 0x03 },		{ 0xc8, 0x40 },
	{ 0x79, 0x05 },		{ 0xc8, 0x30 },
	{ 0x79, 0x26 },
	ENDMARKER,
};

static struct regval_list ov7675_fmt_yuv422[] = {
	{ REG_COM7, 0x0 },	/* Selects YUV mode */
	{ REG_RGB444, 0 },	/* No RGB444 please */
	{ REG_COM1, 0 },	/* CCIR601 */
	{ REG_COM15, COM15_R00FF },
	{ REG_COM9, 0x48 },	/* 32x gain ceiling; 0x8 is reserved bit */
	{ 0x4f, 0x80 }, 	/* "matrix coefficient 1" */
	{ 0x50, 0x80 }, 	/* "matrix coefficient 2" */
	{ 0x51, 0 },		/* vb */
	{ 0x52, 0x22 }, 	/* "matrix coefficient 4" */
	{ 0x53, 0x5e }, 	/* "matrix coefficient 5" */
	{ 0x54, 0x80 }, 	/* "matrix coefficient 6" */
	{ REG_COM13, COM13_GAMMA|COM13_UVSAT },
	ENDMARKER,
};


static struct regval_list ov7675_res_vga[] = {
	{0x92, 0x00},
	{0x93, 0x00},
	{0xb9, 0x00},
	{0x19, 0x03},
	{0x1a, 0x7b},
	{0x17, 0x13},
	{0x18, 0x01},
	{0x03, 0x0a},
	{0xe6, 0x00},
	{0xd2, 0x1c},
	ENDMARKER,
};

static struct regval_list ov7675_res_qvga[] = {
	{0x92, 0x88},
	{0x93, 0x00},
	{0xb9, 0x30},
	{0x19, 0x02},
	{0x1a, 0x3e},
	{0x17, 0x13},
	{0x18, 0x3b},
	{0x03, 0x0a},
	{0xe6, 0x05},
	{0xd2, 0x1c},
	ENDMARKER,
};

#define OV7675_SIZE(n, w, h, r) \
	{.name = n, .width = w , .height = h, .regs = r }

static struct ov7675_win_size ov7675_supported_win_sizes[] = {
	OV7675_SIZE("VGA", W_VGA, H_VGA, ov7675_res_vga),
	OV7675_SIZE("QVGA", W_QVGA, H_QVGA, ov7675_res_qvga),
};

#define N_WIN_SIZES (ARRAY_SIZE(ov7675_supported_win_sizes))

static u32 ov7675_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
};

/*
 * General functions
 */
static struct ov7675_priv *to_ov7675(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov7675_priv,
			subdev);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov7675_priv, hdl)->subdev;
}

static int ov7675_write_array(struct i2c_client *client,
		const struct regval_list *vals)
{
	int ret;

	while ((vals->reg_num != 0xff) || (vals->value != 0xff)) {
		dev_vdbg(&client->dev, "set reg0x%02x = 0x%02x",
			 vals->reg_num, vals->value);

		ret = ov7675_write_reg(client, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	dev_dbg(&client->dev, "%s config written\n", __func__);
	return 0;
}

static int ov7675_s_hflip(struct v4l2_subdev *sd, int value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned char v = 0;
	int ret;

	v = ov7675_read_reg(client, REG_MVFP);
	if(v < 0)
		return v;

	if (value)
		v |= MVFP_MIRROR;
	else
		v &= ~MVFP_MIRROR;

	ret = ov7675_write_reg(client, REG_MVFP, v);
	return ret;
}

 /*
 * Select the nearest higher resolution for capture
 */
static const struct ov7675_win_size *ov7675_select_win(u32 *width, u32 *height)
{
	int i, default_size = ARRAY_SIZE(ov7675_supported_win_sizes) - 1;

	for (i = 0; i < ARRAY_SIZE(ov7675_supported_win_sizes); i++) {
		if ((*width >= ov7675_supported_win_sizes[i].width) &&
			(*height >= ov7675_supported_win_sizes[i].height)) {
			*width = ov7675_supported_win_sizes[i].width;
			*height = ov7675_supported_win_sizes[i].height;
			return &ov7675_supported_win_sizes[i];
		}
	}

	*width = ov7675_supported_win_sizes[default_size].width;
	*height = ov7675_supported_win_sizes[default_size].height;
	return &ov7675_supported_win_sizes[default_size];
}

static int ov7675_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov7675_priv *priv = to_ov7675(client);
	struct soc_camera_subdev_desc *ssdd = &priv->ssdd;
	int ret;

	dev_dbg(&client->dev, "%s(%d)\n", __func__, on);

	if (!on) {
		gpio_direction_output(priv->gpio_enable, 1);
		msleep(50);
		ret = soc_camera_power_off(&client->dev, ssdd, NULL);
		return ret;
	}

	ret = soc_camera_power_on(&client->dev, ssdd, NULL);
	if (ret < 0)
		return ret;

	if (priv->gpio_enable) {
		gpio_direction_output(priv->gpio_enable, 0);
		dev_dbg(&client->dev, "Enabled power GPIO\n");
	}

	msleep(50);

	return ret;
}

static int ov7675_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov7675_priv *priv = to_ov7675(client);

	if (format->pad)
		return -EINVAL;

	mf->width       = priv->win->width;
	mf->height      = priv->win->height;

	mf->code        = priv->cfmt_code;
	mf->colorspace  = V4L2_COLORSPACE_JPEG;
	mf->field       = V4L2_FIELD_NONE;

	return 0;
}

static int ov7675_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	/*
	 * Current do not support set format, use unify format yuv422i
	 */
	const struct ov7675_win_size *win;
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov7675_priv *priv = to_ov7675(client);
	int ret;

	/*
	 * select suitable win
	 */
	win = ov7675_select_win(&mf->width, &mf->height);

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

	priv->win = ov7675_select_win(&mf->width, &mf->height);

	/*
	 * Initialize the sensor with default data
	 */
	ret = ov7675_write_array(client, ov7675_init_regs);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Error (%d)\n", __func__, ret);
		return ret;
	}

	ret = ov7675_write_array(client, ov7675_fmt_yuv422);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Error (%d)\n", __func__, ret);
		return ret;
	}

	/*
	 * set size win
	 */
	if(priv->win->regs) {
		ret = ov7675_write_array(client, priv->win->regs);
		dev_dbg(&client->dev, "%s: Set size %dx%d\n", __func__,
			mf->width, mf->height);
		if (ret < 0) {
			dev_err(&client->dev, "%s: Error (%d)\n", __func__, ret);
			return ret;
		}
	}

	ov7675_s_hflip(sd, 1);

	return 0;
}

static int ov7675_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ov7675_codes))
		return -EINVAL;

	code->code = ov7675_codes[code->index];
	return 0;
}

static int ov7675_g_mbus_config(struct v4l2_subdev *sd,
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

static int ov7675_frame_rates[] = { 30, 15, 10, 5, 1 };

static int ov7675_enum_frameinterval(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->pad || fie->index >= ARRAY_SIZE(ov7675_frame_rates))
		return -EINVAL;
	fie->interval.numerator = 1;
	fie->interval.denominator = ov7675_frame_rates[fie->index];
	return 0;
}


/*
 * Frame size enumeration
 */
static int ov7675_enum_framesize(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_frame_size_enum *fse)
{
	int i;
	int num_valid = -1;
	__u32 index = fse->index;
	struct ov7675_win_size *win;

	if (fse->pad)
		return -EINVAL;

	for (i = 0; i < N_WIN_SIZES; i++) {
		win = &ov7675_supported_win_sizes[index];
		if (index == ++num_valid) {
			fse->min_width = fse->max_width = win->width;
			fse->min_height = fse->max_height = win->height;
			return 0;
		}
	}

	return -EINVAL;
}

static int ov7675_video_probe(struct i2c_client *client)
{
	unsigned char retval_high = 0;
	struct ov7675_priv *priv = to_ov7675(client);

	/*
	 * check and show product ID and manufacturer ID
	 */
	ov7675_s_power(&priv->subdev, 1);
	retval_high = ov7675_read_reg(client, REG_CHIP_ID_HIGH);
	if (retval_high != CHIP_ID_HIGH) {
		dev_err(&client->dev, "unknown sensor chip_id %xxx\n",
			retval_high);
		return -1;
	}

	ov7675_s_power(&priv->subdev, 0);
	dev_info(&client->dev, "detected sensor id 0x%xxx\n",retval_high);

	return 0;
}

static struct v4l2_subdev_core_ops ov7675_subdev_core_ops = {
	.s_power	= ov7675_s_power,
};

static struct v4l2_subdev_video_ops ov7675_subdev_video_ops = {
	.g_mbus_config	= ov7675_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops ov7675_pad_ops = {
	.enum_mbus_code	= ov7675_enum_mbus_code,
	.set_fmt	= ov7675_set_fmt,
	.get_fmt	= ov7675_get_fmt,
	.enum_frame_interval	= ov7675_enum_frameinterval,
	.enum_frame_size	= ov7675_enum_framesize,
 };

static struct v4l2_subdev_ops ov7675_subdev_ops = {
	.core	= &ov7675_subdev_core_ops,
	.video	= &ov7675_subdev_video_ops,
	.pad	= &ov7675_pad_ops,
};

/*
 * i2c_driver functions
 */

static int ov7675_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct device_node *np = client->dev.of_node;
	struct ov7675_priv *priv;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct v4l2_subdev_platform_data *sd_pdata;
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

	priv = devm_kzalloc(&client->dev, sizeof(struct ov7675_priv),
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

	gpio_enable = of_get_named_gpio_flags(np, "gpio-enable", 0, &flags);
	if (gpio_is_valid(gpio_enable)) {
		ret = devm_gpio_request_one(&client->dev, gpio_enable, flags,
					"ov7675_enable");
		if (ret) {
			dev_err(&client->dev, "failed to request enable gpio %d: %d\n",
				gpio_enable, ret);
			return -ENODEV;
		}
		priv->gpio_enable = gpio_enable;
	} else {
		priv->gpio_enable = 0;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov7675_subdev_ops);

	ret = ov7675_s_power(&priv->subdev, 1);
	if (ret) {
		dev_err(&client->dev, "Failed to power on camera\n");
		return ret;
	}

	/*
	 * Default window size
	 */
	priv->win = &ov7675_supported_win_sizes[0];
	priv->cfmt_code = MEDIA_BUS_FMT_YUYV8_2X8;

	/*
	 * Register controls
	 */
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error)
		return priv->hdl.error;

	dev_dbg(&client->dev, "Probing for ov7675\n");
	ret = ov7675_video_probe(client);

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
		ov7675_s_power(&priv->subdev, 0);

		/*
		 * Register the subdevice
		 */
		ret = v4l2_async_register_subdev(&priv->subdev);
	}

	return ret;
}

static int ov7675_remove(struct i2c_client *client)
{
	struct ov7675_priv *priv = to_ov7675(client);

	devm_kfree(&client->dev, priv);
	return 0;
}

static const struct of_device_id ov7675_of_match[] = {
	{ .compatible = "omnivision,ov7675", },
	{},
};
MODULE_DEVICE_TABLE(of, ov7675_of_match);

static const struct i2c_device_id ov7675_id[] = {
	{ "ov7675", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov7675_id);

static struct i2c_driver ov7675_i2c_driver = {
	.driver = {
		.name = "ov7675",
		.of_match_table = of_match_ptr(ov7675_of_match),
	},
	.probe		= ov7675_probe,
	.remove		= ov7675_remove,
	.id_table	= ov7675_id,
};

/*
 * Module functions
 */
static int __init ov7675_module_init(void)
{
	return i2c_add_driver(&ov7675_i2c_driver);
}

static void __exit ov7675_module_exit(void)
{
	i2c_del_driver(&ov7675_i2c_driver);
}

module_init(ov7675_module_init);
module_exit(ov7675_module_exit);

MODULE_DESCRIPTION("camera sensor ov7675 driver");
MODULE_AUTHOR("YeFei <feiye@ingenic.cn>");
MODULE_LICENSE("GPL");
