/*
 * V4L2 Driver for camera sensor ov5640
 *
 * Copyright (C) 2012, Ingenic Semiconductor Inc.
 * Copyright (C) 2015, Imagination Technologies Ltd.
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
#define REG_CHIP_ID_LOW			0x300b
#define REG_CHIP_REVISION		0x302a
#define CHIP_ID_HIGH			0x56
#define CHIP_ID_LOW			0x40

/* Private v4l2 controls */
#define V4L2_CID_PRIVATE_BALANCE  (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PRIVATE_EFFECT  (V4L2_CID_PRIVATE_BASE + 1)

/* In flip, the OV5640 does not need additional settings because the ISP block
 * will auto-detect whether the pixel is in the red line or blue line and make
 * the necessary adjustments.
 */
#define REG_TC_VFLIP			0x3820
#define REG_TC_MIRROR			0x3821
#define OV5640_HFLIP			0x1
#define OV5640_VFLIP			0x2
#define OV5640_FLIP_VAL			((unsigned char)0x06)
#define OV5640_FLIP_MASK		((unsigned char)0x06)

 /* whether sensor support high resolution (> vga) preview or not */
#define SUPPORT_HIGH_RESOLUTION_PRE		1

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

/* Supported resolutions */
enum ov5640_width {
	W_QVGA	= 320,
	W_VGA	= 640,
	W_720P	= 1280,
	W_1080P	= 1920,
};

enum ov5640_height {
	H_QVGA	= 240,
	H_VGA	= 480,
	H_720P	= 720,
	H_1080P	= 1080,
};

struct ov5640_win_size {
	char *name;
	enum ov5640_width width;
	enum ov5640_height height;
	const struct regval_list *regs;
};

struct ov5640_priv {
	struct soc_camera_subdev_desc	ssdd;
	int				gpio_reset;
	int				gpio_enable;
	struct v4l2_subdev		subdev;
	struct ov5640_camera_info	*info;
	enum v4l2_mbus_pixelcode	cfmt_code;
	const struct ov5640_win_size	*win;
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
	msg.buf = (char *)buf;
	ret = i2c_transfer(adap, &msg, 1);

	if (ret != 1)
		dev_err(&client->dev, "Error writing to %s\n", client->name);
	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	   transmitted, else error code. */
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

	if (ret != 1)
		dev_err(&client->dev, "Error reading from %s\n", client->name);
	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	   transmitted, else error code. */
	return (ret == 1) ? count : ret;
}

s32 ov5640_read_reg(struct i2c_client *client, u16 reg)
{
	int ret;
	u8 retval;
	u16 r = cpu_to_be16(reg);

	ret = sensor_i2c_master_send(client, (u8 *)&r, 2);

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

s32 ov5640_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	unsigned char msg[3];
	int ret;

	reg = cpu_to_be16(reg);

	memcpy(&msg[0], &reg, 2);
	memcpy(&msg[2], &val, 1);

	ret = sensor_i2c_master_send(client, msg, 3);

	if (ret < 0)
		return ret;
	else if (ret < 3)
		return -EIO;
	return 0;
}

/*
 * Registers settings
 */

#define ENDMARKER { 0xff, 0xff }

static const struct regval_list ov5640_init_regs[] = {
	{ 0x3103, 0x11 },
	{ 0x3008, 0x82 },	/* sw reset */
	{ 0x3008, 0x42 },	/* sw powerdown */
	{ 0x3103, 0x03 },
	{ 0x3017, 0xff },
	{ 0x3018, 0xff },	/* i/o control */
	{ 0x3034, 0x18 },
	{ 0x3035, 0x11 },
	{ 0x3036, 0x46 },
	{ 0x3037, 0x13 },
	{ 0x3108, 0x01 },
	{ 0x3630, 0x36 },
	{ 0x3631, 0x0e },
	{ 0x3632, 0xe2 },
	{ 0x3633, 0x12 },
	{ 0x3621, 0xe0 },
	{ 0x3704, 0xa0 },
	{ 0x3703, 0x5a },
	{ 0x3715, 0x78 },
	{ 0x3717, 0x01 },
	{ 0x370b, 0x60 },
	{ 0x3705, 0x1a },
	{ 0x3905, 0x02 },
	{ 0x3906, 0x10 },
	{ 0x3901, 0x0a },
	{ 0x3731, 0x12 },
	{ 0x3600, 0x08 },
	{ 0x3601, 0x33 },
	{ 0x302d, 0x60 },
	{ 0x3620, 0x52 },
	{ 0x371b, 0x20 },
	{ 0x471c, 0x50 },
	{ 0x3a13, 0x43 },
	{ 0x3a18, 0x00 },
	{ 0x3a19, 0xf8 },
	{ 0x3635, 0x13 },
	{ 0x3636, 0x03 },
	{ 0x3634, 0x40 },
	{ 0x3622, 0x01 },
	{ 0x3c01, 0x34 },
	{ 0x3c04, 0x28 },
	{ 0x3c05, 0x98 },
	{ 0x3c06, 0x00 },
	{ 0x3c07, 0x08 },
	{ 0x3c08, 0x00 },
	{ 0x3c09, 0x1c },
	{ 0x3c0a, 0x9c },
	{ 0x3c0b, 0x40 },
	{ 0x3820, 0x41 },
	{ 0x3821, 0x05 },
	{ 0x3814, 0x31 },
	{ 0x3815, 0x31 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x00 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x04 },
	{ 0x3804, 0x0a },
	{ 0x3805, 0x3f },
	{ 0x3806, 0x07 },
	{ 0x3807, 0x9b },
	{ 0x3808, 0x02 },
	{ 0x3809, 0x80 },
	{ 0x380a, 0x01 },
	{ 0x380b, 0xe0 },
	{ 0x380c, 0x07 },
	{ 0x380d, 0x68 },
	{ 0x380e, 0x03 },
	{ 0x380f, 0xd8 },
	{ 0x3810, 0x00 },
	{ 0x3811, 0x10 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x06 },
	{ 0x3618, 0x00 },
	{ 0x3612, 0x29 },
	{ 0x3708, 0x62 },
	{ 0x3709, 0x52 },
	{ 0x370c, 0x03 },
	{ 0x3a02, 0x03 },
	{ 0x3a03, 0xd8 },
	{ 0x3a08, 0x01 },
	{ 0x3a09, 0x27 },
	{ 0x3a0a, 0x00 },
	{ 0x3a0b, 0xf6 },
	{ 0x3a0e, 0x03 },
	{ 0x3a0d, 0x04 },
	{ 0x3a14, 0x03 },
	{ 0x3a15, 0xd8 },
	{ 0x4001, 0x02 },
	{ 0x4004, 0x02 },
	{ 0x3000, 0x00 },
	{ 0x3002, 0x1c },
	{ 0x3004, 0xff },
	{ 0x3006, 0xc3 },
	{ 0x300e, 0x58 },
	{ 0x302e, 0x00 },
	{ 0x4300, 0x30 },	/* YUV422, sequence YUYV */
	{ 0x4740, 0x00 },	/* pclk polarity */
	{ 0x501f, 0x00 },
	{ 0x4713, 0x03 },
	{ 0x4407, 0x04 },
	{ 0x440e, 0x00 },
	{ 0x460b, 0x35 },
	{ 0x460c, 0x22 },
	{ 0x3824, 0x02 },
	{ 0x5000, 0xa7 },
	{ 0x5001, 0xa3 },
	{ 0x5180, 0xff },
	{ 0x5181, 0xf2 },
	{ 0x5182, 0x00 },
	{ 0x5183, 0x14 },
	{ 0x5184, 0x25 },
	{ 0x5185, 0x24 },
	{ 0x5186, 0x09 },
	{ 0x5187, 0x09 },
	{ 0x5188, 0x09 },
	{ 0x5189, 0x75 },
	{ 0x518a, 0x54 },
	{ 0x518b, 0xe0 },
	{ 0x518c, 0xb2 },
	{ 0x518d, 0x42 },
	{ 0x518e, 0x3d },
	{ 0x518f, 0x56 },
	{ 0x5190, 0x46 },
	{ 0x5191, 0xf8 },
	{ 0x5192, 0x04 },
	{ 0x5193, 0x70 },
	{ 0x5194, 0xf0 },
	{ 0x5195, 0xf0 },
	{ 0x5196, 0x03 },
	{ 0x5197, 0x01 },
	{ 0x5198, 0x04 },
	{ 0x5199, 0x12 },
	{ 0x519a, 0x04 },
	{ 0x519b, 0x00 },
	{ 0x519c, 0x06 },
	{ 0x519d, 0x82 },
	{ 0x519e, 0x38 },
	{ 0x5381, 0x1e },
	{ 0x5382, 0x5b },
	{ 0x5383, 0x08 },
	{ 0x5384, 0x0a },
	{ 0x5385, 0x7e },
	{ 0x5386, 0x88 },
	{ 0x5387, 0x7c },
	{ 0x5388, 0x6c },
	{ 0x5389, 0x10 },
	{ 0x538a, 0x01 },
	{ 0x538b, 0x98 },
	{ 0x5300, 0x08 },
	{ 0x5301, 0x30 },
	{ 0x5302, 0x10 },
	{ 0x5303, 0x00 },
	{ 0x5304, 0x08 },
	{ 0x5305, 0x30 },
	{ 0x5306, 0x08 },
	{ 0x5307, 0x16 },
	{ 0x5309, 0x08 },
	{ 0x530a, 0x30 },
	{ 0x530b, 0x04 },
	{ 0x530c, 0x06 },
	{ 0x5480, 0x01 },
	{ 0x5481, 0x08 },
	{ 0x5482, 0x14 },
	{ 0x5483, 0x28 },
	{ 0x5484, 0x51 },
	{ 0x5485, 0x65 },
	{ 0x5486, 0x71 },
	{ 0x5487, 0x7d },
	{ 0x5488, 0x87 },
	{ 0x5489, 0x91 },
	{ 0x548a, 0x9a },
	{ 0x548b, 0xaa },
	{ 0x548c, 0xb8 },
	{ 0x548d, 0xcd },
	{ 0x548e, 0xdd },
	{ 0x548f, 0xea },
	{ 0x5490, 0x1d },
	{ 0x5580, 0x02 },
	{ 0x5583, 0x40 },
	{ 0x5584, 0x10 },
	{ 0x5589, 0x10 },
	{ 0x558a, 0x00 },
	{ 0x558b, 0xf8 },
	{ 0x5800, 0x23 },
	{ 0x5801, 0x14 },
	{ 0x5802, 0x0f },
	{ 0x5803, 0x0f },
	{ 0x5804, 0x12 },
	{ 0x5805, 0x26 },
	{ 0x5806, 0x0c },
	{ 0x5807, 0x08 },
	{ 0x5808, 0x05 },
	{ 0x5809, 0x05 },
	{ 0x580a, 0x08 },
	{ 0x580b, 0x0d },
	{ 0x580c, 0x08 },
	{ 0x580d, 0x03 },
	{ 0x580e, 0x00 },
	{ 0x580f, 0x00 },
	{ 0x5810, 0x03 },
	{ 0x5811, 0x09 },
	{ 0x5812, 0x07 },
	{ 0x5813, 0x03 },
	{ 0x5814, 0x00 },
	{ 0x5815, 0x01 },
	{ 0x5816, 0x03 },
	{ 0x5817, 0x08 },
	{ 0x5818, 0x0d },
	{ 0x5819, 0x08 },
	{ 0x581a, 0x05 },
	{ 0x581b, 0x06 },
	{ 0x581c, 0x08 },
	{ 0x581d, 0x0e },
	{ 0x581e, 0x29 },
	{ 0x581f, 0x17 },
	{ 0x5820, 0x11 },
	{ 0x5821, 0x11 },
	{ 0x5822, 0x15 },
	{ 0x5823, 0x28 },
	{ 0x5824, 0x46 },
	{ 0x5825, 0x26 },
	{ 0x5826, 0x08 },
	{ 0x5827, 0x26 },
	{ 0x5828, 0x64 },
	{ 0x5829, 0x26 },
	{ 0x582a, 0x24 },
	{ 0x582b, 0x22 },
	{ 0x582c, 0x24 },
	{ 0x582e, 0x06 },
	{ 0x582f, 0x22 },
	{ 0x5830, 0x40 },
	{ 0x5831, 0x42 },
	{ 0x5832, 0x24 },
	{ 0x5833, 0x26 },
	{ 0x5834, 0x24 },
	{ 0x5835, 0x22 },
	{ 0x5836, 0x22 },
	{ 0x5837, 0x26 },
	{ 0x5838, 0x44 },
	{ 0x5839, 0x24 },
	{ 0x583a, 0x26 },
	{ 0x583b, 0x28 },
	{ 0x583c, 0x42 },
	{ 0x583d, 0xce },
	{ 0x5025, 0x00 },
	{ 0x3a0f, 0x30 },
	{ 0x3a10, 0x28 },
	{ 0x3a1b, 0x30 },
	{ 0x3a1e, 0x26 },
	{ 0x3a11, 0x60 },
	{ 0x3a1f, 0x14 },
	{ 0x3008, 0x02 },
	{ 0x3035, 0x21 },
	ENDMARKER,
};

static const struct regval_list ov5640_qvga_regs[] = {
	{0x4202, 0x0f},

	{0x3c07, 0x08},
	{0x5189, 0x72},

	{0x3503, 0x00},
	{0x3a00, 0x3c},
	{0x3a02, 0x09},
	{0x3a03, 0x3a},
	{0x3a14, 0x09},
	{0x3a15, 0x3a},

	{0x5302, 0x3f},
	{0x5303, 0x10},
	{0x5306, 0x04},
	{0x5307, 0x14},

	{0x3820, 0x41},
	{0x3821, 0x07},

	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0x9b},
	{0x3808, 0x01},
	{0x3809, 0x40},
	{0x380a, 0x00},
	{0x380b, 0xf0},
	{0x3810, 0x00},
	{0x3811, 0x10},
	{0x3812, 0x00},
	{0x3813, 0x06},
	{0x3814, 0x31},
	{0x3815, 0x31},

	{0x3034, 0x1a},
	{0x3035, 0x11},
	{0x3036, 0x46},
	{0x3037, 0x13},

	{0x380c, 0x07},
	{0x380d, 0x68},
	{0x380e, 0x03},
	{0x380f, 0xd8},

	{0x3a08, 0x01},
	{0x3a09, 0x27},
	{0x3a0e, 0x03},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0d, 0x04},

	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3709, 0x52},
	{0x370c, 0x03},

	{0x4004, 0x02},
	{0x3002, 0x1c},
	{0x3006, 0xc3},
	{0x3824, 0x02},
	{0x5001, 0xa3},

	{0x4713, 0x03},
	{0x4407, 0x04},
	{0x460b, 0x35},
	{0x460c, 0x22},
	{0x4837, 0x22},

	{0x4202, 0x00},

	{0x3023, 0x01},
	{0x3022, 0x04},

	ENDMARKER,
};

static const struct regval_list ov5640_vga_regs[] = {
	{0x4202, 0x0f},

	{0x3c07, 0x08},
	{0x5189, 0x72},

	{0x3503, 0x00},
	{0x3a00, 0x3c},
	{0x3a02, 0x09},
	{0x3a03, 0x3a},
	{0x3a14, 0x09},
	{0x3a15, 0x3a},

	{0x5302, 0x3f},
	{0x5303, 0x10},
	{0x5306, 0x04},
	{0x5307, 0x14},

	{0x3820, 0x41},
	{0x3821, 0x07},

	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0x9b},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x3810, 0x00},
	{0x3811, 0x10},
	{0x3812, 0x00},
	{0x3813, 0x06},
	{0x3814, 0x31},
	{0x3815, 0x31},

	{0x3034, 0x1a},
	{0x3035, 0x11},
	{0x3036, 0x46},
	{0x3037, 0x13},

	{0x380c, 0x07},
	{0x380d, 0x68},
	{0x380e, 0x03},
	{0x380f, 0xd8},

	{0x3a08, 0x01},
	{0x3a09, 0x27},
	{0x3a0e, 0x03},
	{0x3a0a, 0x00},
	{0x3a0b, 0xf6},
	{0x3a0d, 0x04},

	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3709, 0x52},
	{0x370c, 0x03},

	{0x4004, 0x02},
	{0x3002, 0x1c},
	{0x3006, 0xc3},
	{0x3824, 0x02},
	{0x5001, 0xa3},

	{0x4713, 0x03},
	{0x4407, 0x04},
	{0x460b, 0x35},
	{0x460c, 0x22},
	{0x4837, 0x22},

	{0x4202, 0x00},

	{0x3023, 0x01},
	{0x3022, 0x04},
	ENDMARKER,
};

static const struct regval_list ov5640_720p_regs[] = {
	{0x4202, 0x0f},

	{0x3c07, 0x07},
	{0x5189, 0x72},

	{0x3503, 0x00},

	{0x5302, 0x3f},
	{0x5303, 0x10},
	{0x5306, 0x04},
	{0x5307, 0x14},

	{0x3820, 0x41},
	{0x3821, 0x07},

	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0xfa},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x06},
	{0x3807, 0xa9},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x02},
	{0x380b, 0xd0},
	{0x3813, 0x04},
	{0x3814, 0x31},
	{0x3815, 0x31},

	{0x3034, 0x1a},
	{0x3035, 0x41},
	{0x3036, 0x69},
	{0x3037, 0x13},

	{0x380c, 0x07},
	{0x380d, 0x64},
	{0x380e, 0x02},
	{0x380f, 0xe4},

	{0x3a08, 0x00},
	{0x3a09, 0xde},
	{0x3a0e, 0x03},
	{0x3a0a, 0x00},
	{0x3a0b, 0xb9},
	{0x3a0d, 0x04},

	{0x3a00, 0x38},
	{0x3a02, 0x02},
	{0x3a03, 0xe4},
	{0x3a14, 0x02},
	{0x3a15, 0xe4},

	{0x3618, 0x00},
	{0x3612, 0x29},
	{0x3709, 0x52},
	{0x370c, 0x03},

	{0x4004, 0x02},
	{0x3002, 0x1c},
	{0x3006, 0xc3},
	{0x3824, 0x04},
	{0x5001, 0x83},

	{0x4713, 0x03},
	{0x4407, 0x04},
	{0x460b, 0x37},
	{0x460c, 0x20},
	{0x4837, 0x16},

	{0x4202, 0x00},

	{0x3023, 0x01},
	{0x3022, 0x04},

	ENDMARKER,
};

static const struct regval_list ov5640_1080p_regs[] = {
	{0x4202, 0x0f},

	{0x3c07, 0x07},
	{0x5189, 0x72},

	{0x3503, 0x00},

	{0x5302, 0x3f},
	{0x5303, 0x10},
	{0x5306, 0x04},
	{0x5307, 0x14},

	{0x3820, 0x40},
	{0x3821, 0x06},

	{0x3800, 0x01},
	{0x3801, 0x50},
	{0x3802, 0x01},
	{0x3803, 0xb2},
	{0x3804, 0x08},
	{0x3805, 0xef},
	{0x3806, 0x05},
	{0x3807, 0xf1},
	{0x3808, 0x07},
	{0x3809, 0x80},
	{0x380a, 0x04},
	{0x380b, 0x38},
	{0x3813, 0x04},
	{0x3814, 0x11},
	{0x3815, 0x11},

	{0x3034, 0x1a},
	{0x3035, 0x41},
	{0x3036, 0x69},
	{0x3037, 0x13},

	{0x380c, 0x09},
	{0x380d, 0xc4},
	{0x380e, 0x04},
	{0x380f, 0x60},

	{0x3a08, 0x01},
	{0x3a09, 0x50},
	{0x3a0e, 0x03},
	{0x3a0a, 0x01},
	{0x3a0b, 0x18},
	{0x3a0d, 0x04},

	{0x3a00, 0x38},
	{0x3a02, 0x04},
	{0x3a03, 0x60},
	{0x3a14, 0x04},
	{0x3a15, 0x60},

	{0x3618, 0x04},
	{0x3612, 0x2b},
	{0x3709, 0x12},
	{0x370c, 0x00},

	{0x4004, 0x06},
	{0x3002, 0x1c},
	{0x3006, 0xc3},
	{0x3824, 0x04},
	{0x5001, 0x83},

	{0x4713, 0x02},
	{0x4407, 0x04},
	{0x460b, 0x37},
	{0x460c, 0x20},
	{0x4837, 0x0a},

	{0x4202, 0x00},

	{0x3023, 0x01},
	{0x3022, 0x04},
	ENDMARKER,
};

#define OV5640_SIZE(n, w, h, r) \
	{.name = n, .width = w , .height = h, .regs = r }

static struct ov5640_win_size ov5640_supported_win_sizes[] = {
	OV5640_SIZE("1080P", W_1080P, H_1080P, ov5640_1080p_regs),
	OV5640_SIZE("720P", W_720P, H_720P, ov5640_720p_regs),
	OV5640_SIZE("VGA", W_VGA, H_VGA, ov5640_vga_regs),
	OV5640_SIZE("QVGA", W_QVGA, H_QVGA, ov5640_qvga_regs),
};

#define N_WIN_SIZES (ARRAY_SIZE(ov5640_supported_win_sizes))

static enum v4l2_mbus_pixelcode ov5640_codes[] = {
	V4L2_MBUS_FMT_YUYV8_2X8,
};

/*
 * General functions
 */
static struct ov5640_priv *to_ov5640(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ov5640_priv,
			    subdev);
}

static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov5640_priv, hdl)->subdev;
}

static int ov5640_write_array(struct i2c_client *client,
			      const struct regval_list *vals)
{
	int ret;

	while ((vals->reg_num != 0xff) || (vals->value != 0xff)) {
		dev_vdbg(&client->dev, "set reg0x%02x = 0x%02x",
			 vals->reg_num, vals->value);

		ret = ov5640_write_reg(client, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	dev_dbg(&client->dev, "%s config written\n", __func__);
	return 0;
}


static int ov5640_mask_set(struct i2c_client *client,
			   u16  reg, u16  mask, u16  set)
{
	s32 val = ov5640_read_reg(client, reg);

	if (val < 0)
		return val;

	val &= ~mask;
	val |= set & mask;

	dev_vdbg(&client->dev, "masks: 0x%02x, 0x%02x", reg, val);

	return ov5640_write_reg(client, reg, val);
}

/*
 * soc_camera_ops functions
 */
static int ov5640_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);

	if (!enable) {
		dev_info(&client->dev, "stream stop\n");
		ov5640_write_reg(client, 0x3008, 0x42);

		return 0;
	}

	dev_info(&client->dev, "stream start\n");
	ov5640_write_reg(client, 0x3008, 0x02);

	return 0;
}

static int ov5640_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	u16 value;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		dev_dbg(&client->dev, "Set VFLIP: %d\n", ctrl->val);
		value = ctrl->val ? OV5640_FLIP_VAL : 0x00;
		ret = ov5640_mask_set(client, REG_TC_VFLIP,
					OV5640_FLIP_MASK, value);
		break;

	case V4L2_CID_HFLIP:
		dev_dbg(&client->dev, "Set HFLIP: %d\n", ctrl->val);
		value = ctrl->val ? OV5640_FLIP_VAL : 0x00;
		ret = ov5640_mask_set(client, REG_TC_MIRROR,
					OV5640_FLIP_MASK, value);
		break;

	default:
		dev_err(&client->dev, "no V4L2 CID: 0x%x ", ctrl->id);
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5640_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	reg->size = 1;
	if (reg->reg > 0xff)
		return -EINVAL;

	ret = ov5640_read_reg(client, reg->reg);
	if (ret < 0)
		return ret;

	reg->val = ret;

	return 0;
}

static int ov5640_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xffff ||
	    reg->val > 0xff)
		return -EINVAL;

	return ov5640_write_reg(client, reg->reg, reg->val);
}
#endif

/* Select the nearest higher resolution for capture */
static const struct ov5640_win_size *ov5640_select_win(u32 *width, u32 *height)
{
	int i, default_size = ARRAY_SIZE(ov5640_supported_win_sizes) - 1;

	for (i = 0; i < ARRAY_SIZE(ov5640_supported_win_sizes); i++) {
		if ((*width >= ov5640_supported_win_sizes[i].width) &&
		    (*height >= ov5640_supported_win_sizes[i].height)) {
			*width = ov5640_supported_win_sizes[i].width;
			*height = ov5640_supported_win_sizes[i].height;
			return &ov5640_supported_win_sizes[i];
		}
	}

	*width = ov5640_supported_win_sizes[default_size].width;
	*height = ov5640_supported_win_sizes[default_size].height;
	return &ov5640_supported_win_sizes[default_size];
}

static int ov5640_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5640_priv *priv = to_ov5640(client);
	struct soc_camera_subdev_desc *ssdd = &priv->ssdd;
	int ret;

	dev_dbg(&client->dev, "%s(%d)\n", __func__, on);

	if (!on)
		return soc_camera_power_off(&client->dev, ssdd, NULL);

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
	return ret;
}

static int ov5640_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct ov5640_priv *priv = to_ov5640(client);

	mf->width	= priv->win->width;
	mf->height	= priv->win->height;

	mf->code	= priv->cfmt_code;
	mf->colorspace	= V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int ov5640_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	/* current do not support set format, use unify format yuv422i */
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct ov5640_priv *priv = to_ov5640(client);
	int ret;

	priv->win = ov5640_select_win(&mf->width, &mf->height);

	/* initialize the sensor with default data */
	ret = ov5640_write_array(client, ov5640_init_regs);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Error (%d)\n", __func__, ret);
		return ret;
	}

	/* set size win */
	ret = ov5640_write_array(client, priv->win->regs);
	dev_dbg(&client->dev, "%s: Set size %dx%d\n", __func__,
		mf->width, mf->height);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Error (%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

static int ov5640_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	const struct ov5640_win_size *win;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);

	/*
	 * select suitable win
	 */
	win = ov5640_select_win(&mf->width, &mf->height);

	if (mf->field == V4L2_FIELD_ANY) {
		mf->field = V4L2_FIELD_NONE;
	} else if (mf->field != V4L2_FIELD_NONE) {
		dev_err(&client->dev, "Field type invalid.\n");
		return -ENODEV;
	}

	switch (mf->code) {
	case V4L2_MBUS_FMT_YUYV8_2X8:
		mf->colorspace = V4L2_COLORSPACE_JPEG;
		break;

	default:
		mf->code = V4L2_MBUS_FMT_YUYV8_2X8;
		break;
	}

	return 0;
}


static int ov5640_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(ov5640_codes))
		return -EINVAL;

	*code = ov5640_codes[index];
	return 0;
}

static int ov5640_g_mbus_config(struct v4l2_subdev *sd,
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
 * Frame intervals.  Since frame rates are controlled with the clock
 * divider, we can only do 30/n for integer n values.  So no continuous
 * or stepwise options.  Here we just pick a handful of logical values.
 */

static int ov5640_frame_rates[] = { 30, 15, 10, 5, 1 };

static int ov5640_enum_frameintervals(struct v4l2_subdev *sd,
		struct v4l2_frmivalenum *interval)
{
	if (interval->index >= ARRAY_SIZE(ov5640_frame_rates))
		return -EINVAL;
	interval->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	interval->discrete.numerator = 1;
	interval->discrete.denominator = ov5640_frame_rates[interval->index];
	return 0;
}


/*
 * Frame size enumeration
 */
static int ov5640_enum_framesizes(struct v4l2_subdev *sd,
		struct v4l2_frmsizeenum *fsize)
{
	int i;
	int num_valid = -1;
	__u32 index = fsize->index;
	struct ov5640_win_size *win;

	for (i = 0; i < N_WIN_SIZES; i++) {
		win = &ov5640_supported_win_sizes[index];
		if (index == ++num_valid) {
			fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			fsize->discrete.width = win->width;
			fsize->discrete.height = win->height;
			return 0;
		}
	}

	return -EINVAL;
}

static int ov5640_video_probe(struct i2c_client *client)
{
	unsigned char retval = 0, retval_high = 0, retval_low = 0;

	/*
	 * check and show product ID and manufacturer ID
	 */
	/* Software reset the device first */
	retval = ov5640_write_reg(client, 0x3008, 0x80);
	if (retval)
		return -1;

	retval_high = ov5640_read_reg(client, REG_CHIP_ID_HIGH);
	retval_low = ov5640_read_reg(client, REG_CHIP_ID_LOW);
	if ((retval_high != CHIP_ID_HIGH) || (retval_low != CHIP_ID_LOW)) {
		dev_err(&client->dev, "unknown sensor chip_id %x%x\n",
			retval_high, retval_low);
		return -1;
	}

	dev_info(&client->dev, "detected sensor id 0x%x%x\n",
		 retval_high, retval_low);
	return 0;
}

static const struct v4l2_ctrl_ops  ov5640_ctrl_ops = {
	.s_ctrl = ov5640_s_ctrl,
};

static struct v4l2_subdev_core_ops ov5640_subdev_core_ops = {
	.s_power	= ov5640_s_power,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= ov5640_g_register,
	.s_register	= ov5640_s_register,
#endif
};

static struct v4l2_subdev_video_ops ov5640_subdev_video_ops = {
	.s_stream	= ov5640_s_stream,
	.g_mbus_fmt	= ov5640_g_fmt,
	.s_mbus_fmt	= ov5640_s_fmt,
	.g_mbus_config	= ov5640_g_mbus_config,
	.try_mbus_fmt	= ov5640_try_fmt,
	.enum_mbus_fmt	= ov5640_enum_fmt,
	.enum_framesizes = ov5640_enum_framesizes,
	.enum_frameintervals = ov5640_enum_frameintervals,
};

static struct v4l2_subdev_ops ov5640_subdev_ops = {
	.core	= &ov5640_subdev_core_ops,
	.video	= &ov5640_subdev_video_ops,
};

/*
 * i2c_driver functions
 */

static int ov5640_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct device_node *np = client->dev.of_node;
	struct ov5640_priv *priv;
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

	priv = devm_kzalloc(&client->dev, sizeof(struct ov5640_priv),
				GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sd_pdata = &priv->ssdd.sd_pdata;
	sd_pdata->num_regulators = 2;
	sd_pdata->regulators = devm_kzalloc(&client->dev,
					sizeof(struct regulator_bulk_data)*2,
					GFP_KERNEL);
	if (!sd_pdata->regulators) {
		dev_err(&adapter->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	/* Get and turn on the supplies */
	sd_pdata->regulators[0].supply = "core";
	sd_pdata->regulators[1].supply = "analog";
	ret = devm_regulator_bulk_get(&client->dev, sd_pdata->num_regulators,
				      sd_pdata->regulators);
	if (ret) {
		dev_err(&client->dev, "Failed to get supplies: %d\n", ret);
		kfree(priv);
		return ret;
	}

	gpio_reset = of_get_named_gpio_flags(np, "gpio-reset", 0, &flags);
	if (gpio_is_valid(gpio_reset)) {
		ret = devm_gpio_request_one(&client->dev, gpio_reset, flags,
					    "ov5640_reset");
		if (ret) {
			dev_err(&client->dev, "failed to request reset gpio %d: %d\n",
				gpio_reset, ret);
			return -ENODEV;
		}
		priv->gpio_reset = gpio_reset;
	} else {
		priv->gpio_reset = 0;
	}

	gpio_enable = of_get_named_gpio_flags(np, "gpio-enable", 0, &flags);
	if (gpio_is_valid(gpio_enable)) {
		ret = devm_gpio_request_one(&client->dev, gpio_enable, flags,
					    "ov5640_enable");
		if (ret) {
			dev_err(&client->dev, "failed to request enable gpio %d: %d\n",
				gpio_enable, ret);
			return -ENODEV;
		}
		priv->gpio_enable = gpio_enable;
	} else {
		priv->gpio_enable = 0;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ov5640_subdev_ops);

	ret = ov5640_s_power(&priv->subdev, 1);
	if (ret) {
		dev_dbg(&client->dev, "Failed to power on camera\n");
		return ret;
	}

	/* Default window size */
	priv->win = &ov5640_supported_win_sizes[0];
	priv->cfmt_code = V4L2_MBUS_FMT_YUYV8_2X8;

	/* Register controls */
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &ov5640_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ov5640_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error)
		return priv->hdl.error;

	dev_dbg(&client->dev, "Probing for ov5640\n");
	ret = ov5640_video_probe(client);
	if (ret) {
		v4l2_ctrl_handler_free(&priv->hdl);
		kfree(sd_pdata->regulators);
		kfree(priv);
		ret = -EPROBE_DEFER;
	} else {
		if (v4l2_ctrl_handler_setup(&priv->hdl) < 0)
			dev_dbg(&client->dev, "Error setting up control handler\n");

		/* Turn camera off until it's required */
		ov5640_s_power(&priv->subdev, 0);

		/* Register the subdevice */
		ret = v4l2_async_register_subdev(&priv->subdev);
	}

	return ret;
}

static int ov5640_remove(struct i2c_client *client)
{
	struct ov5640_priv       *priv = to_ov5640(client);

	kfree(priv);
	return 0;
}

static const struct of_device_id ov5640_of_match[] = {
	{ .compatible = "omnivision,ov5640", },
	{},
};
MODULE_DEVICE_TABLE(of, ov5640_of_match);

static const struct i2c_device_id ov5640_id[] = {
	{ "ov5640-front",  0 },
	{ }
};


MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_i2c_driver = {
	.driver = {
		.name = "ov5640",
		.of_match_table = of_match_ptr(ov5640_of_match),
	},
	.probe    = ov5640_probe,
	.remove   = ov5640_remove,
	.id_table = ov5640_id,
};

/*
 * Module functions
 */
static int __init ov5640_module_init(void)
{
	return i2c_add_driver(&ov5640_i2c_driver);
}

static void __exit ov5640_module_exit(void)
{
	i2c_del_driver(&ov5640_i2c_driver);
}

module_init(ov5640_module_init);
module_exit(ov5640_module_exit);

MODULE_DESCRIPTION("camera sensor ov5640 driver");
MODULE_AUTHOR("YeFei <feiye@ingenic.cn>");
MODULE_LICENSE("GPL");
