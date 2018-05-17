/* *  mma8452.c - Linux kernel modules for 3-Axis Orientation/Motion
 *  Detection Sensor
 *
 *  Copyright (C) 2009-2010 Freescale Semiconductor Hong Kong Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/miscdevice.h>
#include <linux/input-polldev.h>
#include <linux/input.h>
#include <linux/of_gpio.h>

#include "jz4780_mma8452.h"

#define assert(expr)\
	if (!(expr)) {\
		printk( "Assertion failed! %s,%d,%s,%s\n",\
		__FILE__,__LINE__,__func__,#expr);\
	}

#define MMA8452_DRV_NAME	"jz4780_mma8452"
#define MMA8452_ID		0x2A
#define INPUT_FUZZ		32
#define INPUT_FLAT		0
#define MODE_CHANGE_DELAY_MS	100

struct mma8452_status {
	u8 mode;
	u8 ctl_reg1;
};

static struct mma8452_status mma_status = {
	.mode 		= 0,
	.ctl_reg1	= 0
};
static struct {
	unsigned int cutoff;
	unsigned int mask;
} odr_table[] = {
	{3,	MMA8452_ODR400},
	{10,	MMA8452_ODR200},
	{20,	MMA8452_ODR100},
	{100,	MMA8452_ODR50 },
	{300,	MMA8452_ODR12},
	{500,	MMA8452_ODR6},
	{1000,	MMA8452_ODR1},
};

struct mma8452_data {
	struct i2c_client *client;
	struct gsensor_platform_data *pdata;
	struct mutex lock;
	struct mutex lock_rw;
	atomic_t enabled;
	atomic_t regulator_enabled;
	int enabled_save;
	int is_suspend;
	int delay_save;
	struct delayed_work mma_work;
	struct input_dev *input_dev;
	struct work_struct pen_event_work;
	struct workqueue_struct *mma_wqueue;
	struct miscdevice mma8452_misc_device;
	struct regulator *power;
};

static int mma8452_i2c_read(struct mma8452_data *mma,u8* reg,u8 *buf, int len)
{
	int err;
	struct i2c_msg msgs[] = {
		{
		 .addr = mma->client->addr,
		 .flags = 0,
		 .len = 1,
		 .buf = reg,
		 },
		{
		 .addr = mma->client->addr,
		 .flags = I2C_M_RD,
		 .len = len,
		 .buf = buf,
		 },
	};

	err = i2c_transfer(mma->client->adapter, msgs, 2);
	if (err < 0) {
		printk("Read msg error\n");
	}
	return 0;
}

static int mma8452_i2c_write(struct mma8452_data *mma,u8 *buf, int len)
{
	int err;
	struct i2c_msg msgs[] = {
		{
		 .addr = mma->client->addr,
		 .flags = 0,
		 .len = len,
		 .buf = buf,
		 },
	};

	err = i2c_transfer(mma->client->adapter, msgs, 1);
	if (err < 0) {
		printk("Write msg error\n");
	}
	return 0;
}

static int mma8452_i2c_read_data(struct mma8452_data *mma,u8 reg,u8* rxData, int length)
{
	int ret;
	mutex_lock(&mma->lock_rw);
	ret = mma8452_i2c_read(mma, &reg, rxData, length);
	mutex_unlock(&mma->lock_rw);
	if (ret < 0)
		return ret;
	else
		return 0;
}
static int mma8452_i2c_write_data(struct mma8452_data *mma,u8 reg, char *txData, int length)
{
	char buf[80];
	int ret;

	mutex_lock(&mma->lock_rw);
	buf[0] = reg;
	memcpy(buf + 1, txData, length);
	ret = mma8452_i2c_write(mma, buf, length + 1);
	mutex_unlock(&mma->lock_rw);
	if (ret < 0)
		return ret;
	else
		return 0;
}

static int mma8452_init_client(struct mma8452_data *mma)
{
	u8 buf[3]={0,0,0};
	buf[0] = 0x0;
	mma8452_i2c_write_data(mma, MMA8452_CTRL_REG1, buf, 1);
	buf[0] = 0x0;
	mma8452_i2c_write_data(mma, MMA8452_XYZ_DATA_CFG, buf, 1);
	buf[0] = 0xff;
	mma8452_i2c_write_data(mma, MMA8452_CTRL_REG5, buf, 1);
	buf[0] = 0x01;
	mma8452_i2c_write_data(mma, MMA8452_CTRL_REG4, buf, 1);
	buf[0] = 0x01;
	mma8452_i2c_write_data(mma, MMA8452_CTRL_REG3, buf, 1);
	buf[0] = 0x00;
	mma8452_i2c_write_data(mma, MMA8452_CTRL_REG2, buf, 1);
	buf[0] = 0x21;
	mma8452_i2c_write_data(mma, MMA8452_CTRL_REG1, buf, 1);

	mdelay(MODE_CHANGE_DELAY_MS);
	return 0;
}

static int mma8452_read_data(struct mma8452_data *mma, short *x, short *y, short *z) {
	u8	tmp_data[7];
	int hw_d[3] = {0};

	if (mma8452_i2c_read_data(mma, MMA8452_OUT_X_MSB, tmp_data, 7) < 0) {
		printk("i2c block read failed\n");
			return -3;
	}

	hw_d[0] = ((tmp_data[0] << 8) & 0xff00) | tmp_data[1];
	hw_d[1] = ((tmp_data[2] << 8) & 0xff00) | tmp_data[3];
	hw_d[2] = ((tmp_data[4] << 8) & 0xff00) | tmp_data[5];

	hw_d[0] = (short)(hw_d[0]) >> 4;
	hw_d[1] = (short)(hw_d[1]) >> 4;
	hw_d[2] = (short)(hw_d[2]) >> 4;

	if (mma_status.mode==GSENSOR_4G) {
		hw_d[0] = (short)(hw_d[0]) << 1;
		hw_d[1] = (short)(hw_d[1]) << 1;
		hw_d[2] = (short)(hw_d[2]) << 1;
	}
	else if (mma_status.mode==GSENSOR_8G) {
		hw_d[0] = (short)(hw_d[0]) << 2;
		hw_d[1] = (short)(hw_d[1]) << 2;
		hw_d[2] = (short)(hw_d[2]) << 2;
	}
	*x = ((mma->pdata->negate_x) ? (-hw_d[mma->pdata->axis_map_x])
			: (hw_d[mma->pdata->axis_map_x]));
	*y = ((mma->pdata->negate_y) ? (-hw_d[mma->pdata->axis_map_y])
			: (hw_d[mma->pdata->axis_map_y]));
	*z = ((mma->pdata->negate_z) ? (-hw_d[mma->pdata->axis_map_z])
			: (hw_d[mma->pdata->axis_map_z]));

	return 0;
}

static void report_abs(struct mma8452_data *data)
{
	short x, y, z;
	u8 buf[3] = {0, 0, 0};

	mma8452_i2c_read_data(data, MMA8452_STATUS, buf, 1);
	if (!(buf[0] & 0x01)) {
		return;
	}

	if (mma8452_read_data(data, &x, &y, &z) != 0) {
		return;
	}

	input_report_abs(data->input_dev, ABS_X, x);
	input_report_abs(data->input_dev, ABS_Y, y);
	input_report_abs(data->input_dev, ABS_Z, z);

	input_sync(data->input_dev);
}
struct linux_sensor_t hardware_data = {
	"mma8452 3-axis Accelerometer",
	"ST sensor",
	SENSOR_TYPE_ACCELEROMETER, 1, 1024, 1, 1, {} /* Modify version from 0 to 1 for CTS */
};

static int mma8452_device_power_off(struct mma8452_data *mma)
{
	int result;
	u8 buf[3] = {0, 0, 0};
	mma8452_i2c_read_data(mma, MMA8452_CTRL_REG1, buf, 1);
	mma_status.ctl_reg1 = buf[0];
	mma_status.ctl_reg1 &= 0xFE;

	buf[0] = 0x0;
	mma8452_i2c_write_data(mma,MMA8452_CTRL_REG1, buf, 1);
	buf[0] = mma_status.ctl_reg1;
	result = mma8452_i2c_write_data(mma, MMA8452_CTRL_REG1, buf, 1);
	if (result < 0) {
		printk("POWER OFF ERROR\n");
		return result;
	}
	if (!IS_ERR(mma->power) && atomic_cmpxchg(&mma->regulator_enabled, 1, 0)) {
		if (regulator_is_enabled(mma->power)) {
			result = regulator_disable(mma->power);
			if (result < 0) {
				dev_err(&mma->client->dev,
						"power_off regulator failed: %d\n", result);
				return result;
			}
		}
	}
	return 0;
}

static int mma8452_device_power_on(struct mma8452_data *mma)
{
	int result;
	u8 buf[3] = {0, 0, 0};
	if (!IS_ERR(mma->power) && (atomic_cmpxchg(&mma->regulator_enabled, 0, 1) == 0)) {
		if (!regulator_is_enabled(mma->power)) {
			result = regulator_enable(mma->power);
			if (result < 0) {
				dev_err(&mma->client->dev,
						"power_off regulator failed: %d\n", result);
				return result;
			}
		}
	}
	udelay(100);

	mma8452_i2c_read_data(mma, MMA8452_CTRL_REG1, buf, 1);
	mma_status.ctl_reg1 = buf[0];
	mma_status.ctl_reg1 |= 0x01;

	buf[0] = 0x0;
	mma8452_i2c_write_data(mma, MMA8452_CTRL_REG1, buf, 1);
	udelay(100);
	buf[0] = mma_status.ctl_reg1;
	result = mma8452_i2c_write_data(mma, MMA8452_CTRL_REG1, buf, 1);
	if (result < 0) {
		printk("POWER ON ERROR\n");
		return result;
	}
	return 0;
}

static int mma8452_reset(struct mma8452_data *mma)
{
	int err = 0;

	err = mma8452_device_power_off(mma);
	if (err) {
		dev_err(&mma->client->dev,
				"reset failed: %d\n", err);
		return err;
	}
	mdelay(2);
	err = mma8452_device_power_on(mma);
	if (err) {
		dev_err(&mma->client->dev,
				"reset failed: %d\n", err);
		return err;
	}
	mdelay(2);
	atomic_set(&mma->enabled, 1);

	return 0;
}

static int mma8452_enable(struct mma8452_data *mma)
{
	int result;
	if (mma->is_suspend == 0 && !atomic_cmpxchg(&mma->enabled, 0, 1)) {
		result = mma8452_device_power_on(mma);
		if (result < 0) {
			printk("ENABLE ERROR\n");
			atomic_set(&mma->enabled, 0);
			return result;
		}
		enable_irq(mma->client->irq);
	}
	return 0;
}

static int mma8452_disable(struct mma8452_data *mma)
{
	int result;
	if (atomic_cmpxchg(&mma->enabled, 1, 0)) {
		disable_irq_nosync(mma->client->irq);
		flush_workqueue(mma->mma_wqueue);
		cancel_delayed_work_sync(&mma->mma_work);
		result=mma8452_device_power_off(mma);
		if (result < 0) {
			printk("DISABLE ERROR\n");
			atomic_set(&mma->enabled, 1);
			return result;
		}
	}
	return 0;
}
static int mma8452_set_delay(struct mma8452_data *mma, int delay)
{
	int result;
	int i;
	u8 buf[3] = {0, 0, 0};
	mma->delay_save = delay;
	mma_status.ctl_reg1 &= 0x0;

	for(i = 0;i < ARRAY_SIZE(odr_table); i++) {
		mma8452_i2c_read_data(mma, MMA8452_CTRL_REG1, buf, 1);
		mma_status.ctl_reg1 = buf[0];
		mma_status.ctl_reg1 &= 0xc7;
		mma_status.ctl_reg1 |= 0x1;
		mma_status.ctl_reg1 |= odr_table[i].mask;
		if (delay < odr_table[i].cutoff) {
			break;
		}
	}

	buf[0] = 0x0;
	mma8452_i2c_write_data(mma, MMA8452_CTRL_REG1, buf, 1);
	buf[0] = mma_status.ctl_reg1;

	result = mma8452_i2c_write_data(mma, MMA8452_CTRL_REG1, buf, 1);
	assert(result == 0);
	return result;
}
static int mma8452_misc_open(struct inode *inode, struct file *file)
{
	int err;
	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;
	return 0;
}

long mma8452_misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int interval;

	struct miscdevice *dev = file->private_data;
	struct mma8452_data *mma = container_of(dev, struct mma8452_data, mma8452_misc_device);

	switch (cmd) {
	case SENSOR_IOCTL_GET_DELAY:
		interval = mma->pdata->poll_interval;
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EFAULT;
		break;
	case SENSOR_IOCTL_SET_DELAY:
		mutex_lock(&mma->lock);
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		interval *= 10;
		if (interval < mma->pdata->min_interval )
			interval = mma->pdata->min_interval;
		else if (interval > mma->pdata->max_interval)
			interval = mma->pdata->max_interval;
		mma->pdata->poll_interval = interval;
		mma8452_set_delay(mma, mma->pdata->poll_interval);
		mutex_unlock(&mma->lock);
		break;
	case SENSOR_IOCTL_SET_ACTIVE:
		mutex_lock(&mma->lock);
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval > 1)
			return -EINVAL;
		if (interval) {
			mma8452_enable(mma);
		} else {
			mma8452_disable(mma);
		}
		mutex_unlock(&mma->lock);
		break;
	case SENSOR_IOCTL_GET_ACTIVE:
		interval = atomic_read(&mma->enabled);
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EINVAL;

		break;
	case SENSOR_IOCTL_GET_DATA:
		if (copy_to_user(argp, &hardware_data, sizeof(hardware_data)))
			return -EINVAL;
		break;
	case SENSOR_IOCTL_GET_DATA_MAXRANGE:
		if (copy_to_user(argp, &mma->pdata->g_range, sizeof(mma->pdata->g_range)))
			return -EFAULT;
		break;

	case SENSOR_IOCTL_WAKE:
		input_event(mma->input_dev, EV_SYN,SYN_CONFIG, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
static void mma8452_work(struct work_struct *work)
{
	struct mma8452_data *mma;
	mma = container_of(work, struct mma8452_data, pen_event_work);
	report_abs(mma);
	enable_irq(mma->client->irq);
}

static irqreturn_t mma8452_interrupt(int irq, void *dev_id)
{
	struct mma8452_data *mma = dev_id;
	if (mma->is_suspend == 1 || atomic_read(&mma->enabled) == 0)
		return IRQ_HANDLED;
	disable_irq_nosync(mma->client->irq);
	if (!work_pending(&mma->pen_event_work)) {
		queue_work(mma->mma_wqueue, &mma->pen_event_work);
	}
	return IRQ_HANDLED;
}

static const struct file_operations mma8452_misc_fops = {
	.owner = THIS_MODULE,
	.open = mma8452_misc_open,
	.unlocked_ioctl = mma8452_misc_ioctl,
};

static int mma8452_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct mma8452_data *mma;
	u8 buf[3] = {0, 0, 0};
	int result = -1;
	struct device_node *np = client->dev.of_node;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "client not i2c capable\n");
		result = -ENODEV;
		goto err0;
	}

	mma = kzalloc(sizeof(*mma),GFP_KERNEL);
	if (mma == NULL) {
		dev_err(&client->dev,"failed to allocate memory for module data\n");
		result = -ENOMEM;
		goto err0;
	}

	mutex_init(&mma->lock_rw);
	mutex_init(&mma->lock);

	mma->pdata = kmalloc(sizeof(*mma->pdata),GFP_KERNEL);
	if (mma->pdata == NULL)
		goto err1;

	mma->pdata->gpio_int = of_get_named_gpio(np, "gpio-int", 0);
	if (!gpio_is_valid(mma->pdata->gpio_int)) {
		dev_err(&client->dev, "Failed to get gpio-int property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u32(np, "poll-interval", &mma->pdata->poll_interval)) {
		dev_err(&client->dev, "Failed to get poll-interval property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u32(np, "min-interval", &mma->pdata->min_interval)) {
		dev_err(&client->dev, "Failed to get min-interval property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u32(np, "max-interval", &mma->pdata->max_interval)) {
		dev_err(&client->dev, "Failed to get max-interval property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u8(np, "g-range", &mma->pdata->g_range)) {
		dev_err(&client->dev, "Failed to get g-range property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u8(np, "axis-map-x", &mma->pdata->axis_map_x)) {
		dev_err(&client->dev, "Failed to get axis-map-x property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u8(np, "axis-map-y", &mma->pdata->axis_map_y)) {
		dev_err(&client->dev, "Failed to get axis-map-y property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u8(np, "axis-map-z", &mma->pdata->axis_map_z)) {
		dev_err(&client->dev, "Failed to get axis-map-z property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u8(np, "negate-x", &mma->pdata->negate_x)) {
		dev_err(&client->dev, "Failed to get negate-x property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u8(np, "negate-y", &mma->pdata->negate_y)) {
		dev_err(&client->dev, "Failed to get negate-y property\n");
		result = -EINVAL;
		goto err2;
	}
	if (of_property_read_u8(np, "negate-z", &mma->pdata->negate_z)) {
		dev_err(&client->dev, "Failed to get negate-z property\n");
		result = -EINVAL;
		goto err2;
	}

	mma->client = client;
	client->dev.init_name = client->name;

	mma->power = regulator_get(&client->dev, "mma8452_power");
	if (!IS_ERR(mma->power)) {
		if (!regulator_is_enabled(mma->power)) {
			result = regulator_enable(mma->power);
			if (result < 0) {
				dev_err(&mma->client->dev,
						"power_on regulator failed: %d\n", result);
				goto err1;
			}
			atomic_set(&mma->regulator_enabled, 1);
		}
	} else {
		dev_warn(&client->dev, "get regulator failed\n");
	}
	udelay(10);
	mma8452_reset(mma);

	/*--read id must add to load mma8452 or lis3dh--*/
	mma8452_i2c_read_data(mma, MMA8452_WHO_AM_I, buf, 1);
	if (MMA8452_ID != buf[0]) {
		printk("read mma8452 chip ID failed ,may use lis3dh driver\n");
		return -EINVAL;
	}

	if (gpio_request_one(mma->pdata->gpio_int,
				GPIOF_DIR_IN, "gsensor mma8452 irq")) {
		dev_err(&client->dev, "no irq pin available\n");
		mma->pdata->gpio_int = -EBUSY;
	}

	i2c_set_clientdata(client,mma);

	/* Initialize the MMA8452 chip */
	result = mma8452_init_client(mma);
	assert(result == 0);

	INIT_WORK(&mma->pen_event_work, mma8452_work);
	mma->mma_wqueue = create_singlethread_workqueue("mma8452");
	if (!mma->mma_wqueue) {
		result = -ESRCH;
		goto err2;
	}

	/*input poll device register */
	mma->input_dev = input_allocate_device();
	if (!mma->input_dev) {
		dev_err(&client->dev, "alloc poll device failed!\n");
		result = -ENOMEM;
		goto err3;
	}

	input_set_drvdata(mma->input_dev,mma);
	set_bit(EV_ABS, mma->input_dev->evbit);
	input_set_abs_params(mma->input_dev, ABS_X, -8192, 8191,
			INPUT_FUZZ, INPUT_FLAT);
	input_set_abs_params(mma->input_dev, ABS_Y, -8192, 8191,
			INPUT_FUZZ, INPUT_FLAT);
	input_set_abs_params(mma->input_dev, ABS_Z, -8192, 8191,
			INPUT_FUZZ, INPUT_FLAT);
	mma->input_dev->name="g_sensor";
	result = input_register_device(mma->input_dev);
	if (result) {
		dev_err(&client->dev, "register poll device failed!\n");
		goto err4;
	}

	mma->mma8452_misc_device.minor = MISC_DYNAMIC_MINOR,
	mma->mma8452_misc_device.name = "g_sensor",
	mma->mma8452_misc_device.fops = &mma8452_misc_fops,

	result = misc_register(&mma->mma8452_misc_device);
	if (result) {
		dev_err(&client->dev, "register misc device failed!\n");
		goto err5;
	}

	client->irq = gpio_to_irq(mma->pdata->gpio_int);
	result = request_irq(client->irq, mma8452_interrupt,
			IRQF_TRIGGER_LOW,
			"mma8452", mma);
	if (result < 0) {
		dev_err(&client->dev, "%s rquest irq failed\n", __func__);
		return result;
	}
	mma8452_device_power_off(mma);
	udelay(100);
	atomic_set(&mma->enabled, 0);
	mma->is_suspend = 0;
	disable_irq_nosync(mma->client->irq);
	mma8452_enable(mma);

	printk("jz4780_mma8452 successfully probed\n");
	return 0;
err5:
	input_unregister_device(mma->input_dev);
err4:
	input_free_device(mma->input_dev);
err3:
	mma8452_device_power_off(mma);
err2:
	mutex_unlock(&mma->lock);
	kfree(mma->pdata);
err1:
	kfree(mma);
err0:
	return result;
}

static int mma8452_remove(struct i2c_client *client)
{
	int result;
	struct mma8452_data *mma = i2c_get_clientdata(client);
	mma_status.ctl_reg1 = i2c_smbus_read_byte_data(client, MMA8452_CTRL_REG1);
	result = i2c_smbus_write_byte_data(client, MMA8452_CTRL_REG1,
			mma_status.ctl_reg1 & 0xFE);
	assert(result == 0);
	input_unregister_device(mma->input_dev);
	misc_deregister(&mma->mma8452_misc_device);

	return result;
}

static const struct i2c_device_id mma8452_id[] = {
	{ MMA8452_DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mma8452_id);

static const struct of_device_id mma8452_ts_match[] = {
	{ .compatible = "ingenic,mma8452", },
	{ },
};
MODULE_DEVICE_TABLE(of, mma8452_ts_match);

static struct i2c_driver mma8452_driver = {
	.driver = {
		.name	= MMA8452_DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(mma8452_ts_match),
	},

	.probe	= mma8452_probe,
	.remove	= mma8452_remove,
	.id_table = mma8452_id,
};

static int mma8452_init(void)
{
	int res;

	printk(KERN_INFO "Add mma8452 i2c driver\n");
	res = i2c_add_driver(&mma8452_driver);
	if (res < 0) {
		printk(KERN_INFO "add mma8452 i2c driver failed\n");
		return -ENODEV;
	}

	return (res);
}

static void mma8452_exit(void)
{
	printk(KERN_INFO "Remove mma8452 i2c driver.\n");
	i2c_del_driver(&mma8452_driver);
}

late_initcall(mma8452_init);
module_exit(mma8452_exit);

MODULE_AUTHOR("bcjia <bcjia@ingenic.cn>");
MODULE_DESCRIPTION("MMA8452 3-Axis Orientation/Motion Detection Sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");
