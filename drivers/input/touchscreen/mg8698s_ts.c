/* drivers/input/touchscreen/mg8698s_ts.c
 *
 * FocalTech mg8698s TouchScreen driver.
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/i2c.h>
#include <linux/input.h>
#include "mg8698s_ts.h"
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/tsc.h>
#include <linux/of_gpio.h>

#include <linux/interrupt.h>
struct ts_event {
	u16 au16_x[CFG_MAX_TOUCH_POINTS];	/*x coordinate */
	u16 au16_y[CFG_MAX_TOUCH_POINTS];	/*y coordinate */
	u8 au8_touch_event[CFG_MAX_TOUCH_POINTS];	/*touch event:
					0 -- down; 1-- contact; 2 -- contact */
	u8 au8_finger_id[CFG_MAX_TOUCH_POINTS];	/*touch ID */
	u8 weight[CFG_MAX_TOUCH_POINTS];	/*touch weight */
	u16 pressure;
	u8 touch_point;
	u8 touch_num;
};

struct mg8698s_gpio {
	struct jztsc_pin *irq;
	struct jztsc_pin *wake;
};

struct mg8698s_ts_data {
	unsigned int irq;
    unsigned int cmd;
	u8 cmd_buf[COMMAND_BYTES];
    struct completion cmd_complete;
	u32 x_max;
	u32 y_max;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct ts_event event;
	struct mg8698s_gpio gpio;
	struct regulator *power;
	struct work_struct  work;
	struct delayed_work release_work;
	struct workqueue_struct *workqueue;
};

/*
*mg8698s_i2c_Read-read data and write data by i2c
*@client: handle of i2c
*@readbuf: Where to store data read from slave
*@readlen: How many bytes to read
*
*Returns negative errno, else the number of messages executed
*
*
*/
static void mg8698s_ts_release(struct mg8698s_ts_data *data);
static void mg8698s_ts_reset(struct mg8698s_ts_data *ts);
static int mg8698s_i2c_Read(struct i2c_client *client,
        char *readbuf, int readlen)
{
    int ret;
    struct mg8698s_ts_data *mg8698s_ts = i2c_get_clientdata(client);
    struct i2c_msg msgs[] = {
        {
            .addr = client->addr,
            .flags = I2C_M_RD,
            .len = readlen,
            .buf = readbuf,
        },
    };
    ret = i2c_transfer(client->adapter, msgs, 1);
    if (ret < 0) {
        dev_err(&client->dev, "%s:i2c read error.\n", __func__);
        mg8698s_ts_reset(mg8698s_ts);
    }
    return ret;
}

/*release the point*/
static void mg8698s_ts_release(struct mg8698s_ts_data *data)
{
	/* input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, 0); */
	input_mt_sync(data->input_dev);
	input_sync(data->input_dev);
}


static void tsc_release(struct work_struct *work)
{
    struct mg8698s_ts_data *ts =
        container_of(work, struct mg8698s_ts_data, release_work.work);
    mg8698s_ts_release(ts);
}

/*Read touch point information when the interrupt  is asserted.*/
static int mg8698s_read_touchdata(struct mg8698s_ts_data *data)
{
	struct ts_event *event = &data->event;
	u8 buf[POINT_READ_BUF] = { 0 };
	int ret = -1;

    if (delayed_work_pending(&data->release_work)) {
        cancel_delayed_work_sync(&data->release_work);
    }

	memset(event, 0, sizeof(struct ts_event));
	event->touch_point = 0;
	ret = mg8698s_i2c_Read(data->client, buf, POINT_READ_BUF);
	if (ret < 0) {
        schedule_delayed_work(&data->release_work, HZ / 2);
		return ret;
	}

    if (buf[MG_MODE] == MG_MODE_KEY) {
        return 1;
    }

	event->touch_num = buf[ACTUAL_TOUCH_POINTS]&0x0f;
	if (event->touch_num == 0) {
		mg8698s_ts_release(data);
		return 1;
	}

     event->touch_point = event->touch_num;

    if (event->touch_num >= 1) {
        event->au16_x[0] = COORD_INTERPRET(buf[MG_POS_X1_HI], buf[MG_POS_X1_LOW]);
        event->au16_y[0] = COORD_INTERPRET(buf[MG_POS_Y1_HI], buf[MG_POS_Y1_LOW]);
        event->weight[0] = buf[MG_STATUS1];
        event->au8_finger_id[0] = buf[MG_CONTACT_ID1];
    }
    if (event->touch_num >= 2) {
        event->au16_x[1] = COORD_INTERPRET(buf[MG_POS_X2_HI], buf[MG_POS_X2_LOW]);
        event->au16_y[1] = COORD_INTERPRET(buf[MG_POS_Y2_HI], buf[MG_POS_Y2_LOW]);
        event->weight[1] = buf[MG_STATUS2];
        event->au8_finger_id[1] = buf[MG_CONTACT_ID2];
    }
    if (event->touch_num >= 3) {
        event->au16_x[2] = COORD_INTERPRET(buf[MG_POS_X3_HI], buf[MG_POS_X3_LOW]);
        event->au16_y[2] = COORD_INTERPRET(buf[MG_POS_Y3_HI], buf[MG_POS_Y3_LOW]);
        event->weight[2] = buf[MG_CONTACT_IDS3] & 0x0F;
        event->au8_finger_id[2] = (buf[MG_CONTACT_IDS3]>>4)&0x0F;
    }
    if (event->touch_num >= 4) {
        event->au16_x[3] = COORD_INTERPRET(buf[MG_POS_X4_HI], buf[MG_POS_X4_LOW]);
        event->au16_y[3] = COORD_INTERPRET(buf[MG_POS_Y4_HI], buf[MG_POS_Y4_LOW]);
        event->weight[3] = buf[MG_CONTACT_IDS4] & 0x0F;
        event->au8_finger_id[3] = (buf[MG_CONTACT_IDS4]>>4)&0x0F;
    }
    if (event->touch_num == 5) {
        event->au16_x[4] = COORD_INTERPRET(buf[MG_POS_X5_HI], buf[MG_POS_X5_LOW]);
        event->au16_y[4] = COORD_INTERPRET(buf[MG_POS_Y5_HI], buf[MG_POS_Y5_LOW]);
        event->weight[4] = buf[MG_CONTACT_IDS5] & 0x0F;
        event->au8_finger_id[4] = (buf[MG_CONTACT_IDS5]>>4)&0x0F;
    }
	event->pressure = FT_PRESS;

	return 0;
}
/*
 *report the point information
 */
static int mg8698s_report_value(struct mg8698s_ts_data *data)
{
	struct ts_event *event = &data->event;
	int i = 0;
	for (i = 0; i < event->touch_point; i++) {
		if(event->au16_x[i] > data->x_max || event->au16_y[i] > data->y_max)
			continue;

#ifdef CONFIG_TSC_SWAP_XY
		tsc_swap_xy(&(event->au16_x[i]),&(event->au16_y[i]));
#endif

#ifdef CONFIG_TSC_SWAP_X
		tsc_swap_x(&(event->au16_x[i]),data->x_max);
#endif

#ifdef CONFIG_TSC_SWAP_Y
		tsc_swap_y(&(event->au16_y[i]),data->y_max);
#endif
		input_report_abs(data->input_dev, ABS_MT_POSITION_X,
				event->au16_x[i]);
		input_report_abs(data->input_dev, ABS_MT_POSITION_Y,
				event->au16_y[i]);
		 input_report_abs(data->input_dev, ABS_MT_TRACKING_ID,
				event->au8_finger_id[i]);
		input_report_abs(data->input_dev,ABS_MT_TOUCH_MAJOR,
				event->weight[i]);
		input_report_abs(data->input_dev,ABS_MT_WIDTH_MAJOR,
				event->weight[i]);
		input_mt_sync(data->input_dev);
	}

    input_sync(data->input_dev);

	return 0;
}

static void mg8698s_read_version(struct mg8698s_ts_data *ts)
{
    u8 *buf = ts->cmd_buf;

    memset(buf, 0, COMMAND_BYTES);
	if (COMMAND_BYTES == mg8698s_i2c_Read(
                ts->client, buf, COMMAND_BYTES)) {
        dev_info(&ts->client->dev,
                "mg8698s Fw Version : %4x, %4x, %4x, %4x, %4x\n",
                buf[0], buf[1], buf[2], buf[3], buf[4]);
    } else {
        buf[0] = -1;
    }
}

static void mg8698s_read_device_id(struct mg8698s_ts_data *ts)
{
    u8 *buf = ts->cmd_buf;

    memset(buf, 0, COMMAND_BYTES);
	if (COMMAND_BYTES == mg8698s_i2c_Read(
                ts->client, buf, COMMAND_BYTES)) {
        dev_info(&ts->client->dev,
                "mg8698s device id : %4x, %4x, %4x, %4x, %4x\n",
                buf[0], buf[1], buf[2], buf[3], buf[4]);
    } else {
        buf[0] = -1;
    }
}

static void tsc_work_handler(struct work_struct *work)
{
	struct mg8698s_ts_data *mg8698s_ts = container_of(work, struct mg8698s_ts_data,work);
    int ret = 0;
    switch(mg8698s_ts->cmd) {
    case MG_NONE:
        ret = mg8698s_read_touchdata(mg8698s_ts);
        if (ret == 0)
            mg8698s_report_value(mg8698s_ts);

        break;

    case MG_DEVICE_VERSION:
        mg8698s_read_version(mg8698s_ts);
        mg8698s_ts->cmd = MG_NONE;
        complete(&mg8698s_ts->cmd_complete);

        break;

    case MG_DEVICE_ID:
        mg8698s_read_device_id(mg8698s_ts);
        mg8698s_ts->cmd = MG_NONE;
        complete(&mg8698s_ts->cmd_complete);

        break;

    default:

        break;
    }
    enable_irq(mg8698s_ts->irq);
}
/*The mg8698s device will signal the host about TRIGGER_FALLING.
*Processed when the interrupt is asserted.
*/
static irqreturn_t mg8698s_ts_interrupt(int irq, void *dev_id)
{
	struct mg8698s_ts_data *mg8698s_ts = dev_id;

	disable_irq_nosync(mg8698s_ts->irq);
    queue_work(mg8698s_ts->workqueue, &mg8698s_ts->work);

	return IRQ_HANDLED;
}

static void mg8698s_gpio_init(struct mg8698s_ts_data *ts, struct i2c_client *client)
{
	struct device *dev = &client->dev;

	if (gpio_request_one(ts->gpio.irq->num,
			     GPIOF_DIR_IN, "mg8698s_irq")) {
		dev_err(dev, "no irq pin available\n");
		ts->gpio.irq->num = -EBUSY;
	}
	if (gpio_request_one(ts->gpio.wake->num,
			     ts->gpio.wake->enable_level
			     ? GPIOF_OUT_INIT_LOW
			     : GPIOF_OUT_INIT_HIGH,
			     "mg8698s_shutdown")) {
		dev_err(dev, "no shutdown pin available\n");
		ts->gpio.wake->num = -EBUSY;
	}
}


static int mg8698s_ts_power_on(struct mg8698s_ts_data *ts)
{
	if (ts->power)
		return regulator_enable(ts->power);

	return 0;
}

static int mg8698s_ts_power_off(struct mg8698s_ts_data *ts)
{
	if (ts->power)
		return regulator_disable(ts->power);

	return 0;
}

static void mg8698s_ts_reset(struct mg8698s_ts_data *ts)
{
	set_pin_status(ts->gpio.wake, 1);
    msleep(10);
	set_pin_status(ts->gpio.wake, 0);
	msleep(50);
	set_pin_status(ts->gpio.wake, 1);
}

static int mg8698s_ts_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct mg8698s_ts_data *mg8698s_ts;
	struct input_dev *input_dev;
	int err = 0;
	enum of_gpio_flags flags;
	struct device_node *np = client->dev.of_node;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	mg8698s_ts = kzalloc(sizeof(struct mg8698s_ts_data), GFP_KERNEL);
	if (!mg8698s_ts) {
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	i2c_set_clientdata(client, mg8698s_ts);

	mg8698s_ts->gpio.irq = kzalloc(sizeof(struct jztsc_pin), GFP_KERNEL);
	if (!mg8698s_ts->gpio.irq) {
		err = -ENOMEM;
		goto exit_alloc_gpio1_failed;
	}

	mg8698s_ts->gpio.wake = kzalloc(sizeof(struct jztsc_pin), GFP_KERNEL);
	if (!mg8698s_ts->gpio.wake) {
		err = -ENOMEM;
		goto exit_alloc_gpio2_failed;
	}

	mg8698s_ts->gpio.wake->num = of_get_named_gpio_flags(np, "wake-gpios", 0, &flags);
	if (gpio_is_valid(mg8698s_ts->gpio.wake->num)) {
		mg8698s_ts->gpio.wake->enable_level = flags & OF_GPIO_ACTIVE_LOW;
	} else {
		dev_err(&client->dev, "Failed to get wake-gpios property\n");
		err = -EINVAL;
		goto exit_device_tree_read_failed;
	}

	mg8698s_ts->gpio.irq->num = of_get_named_gpio_flags(np, "interrupt", 0, &flags);
	if (gpio_is_valid(mg8698s_ts->gpio.irq->num)) {
		mg8698s_ts->gpio.irq->enable_level = flags & OF_GPIO_ACTIVE_LOW;
	} else {
		dev_err(&client->dev, "Failed to get interrupt property\n");
		err = -EINVAL;
		goto exit_device_tree_read_failed;
	}

	if (of_property_read_u32(np, "x-size", &mg8698s_ts->x_max)) {
		dev_err(&client->dev, "Failed to get x-size property\n");
		err = -EINVAL;
		goto exit_device_tree_read_failed;
	}
	mg8698s_ts->x_max -= 1;

	if (of_property_read_u32(np, "y-size", &mg8698s_ts->y_max)) {
		dev_err(&client->dev, "Failed to get y-size property\n");
		err = -EINVAL;
		goto exit_device_tree_read_failed;
	}
	mg8698s_ts->y_max -= 1;

	mg8698s_gpio_init(mg8698s_ts, client);

	client->dev.init_name=client->name;
	mg8698s_ts->power = regulator_get(&client->dev, "mg8698s_2v8");
	if (IS_ERR(mg8698s_ts->power)) {
		dev_warn(&client->dev, "get regulator failed\n");
	}
	mg8698s_ts_power_on(mg8698s_ts);

	INIT_WORK(&mg8698s_ts->work, tsc_work_handler);
	INIT_DELAYED_WORK(&mg8698s_ts->release_work, tsc_release);
	mg8698s_ts->workqueue = create_singlethread_workqueue("mg8698s_tsc");

	client->irq = gpio_to_irq(mg8698s_ts->gpio.irq->num);
	err = request_irq(client->irq, mg8698s_ts_interrupt,
			    IRQF_TRIGGER_FALLING,
			  "mg8698s_ts", mg8698s_ts);
	if (err < 0) {
		dev_err(&client->dev, "mg8698s_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	mg8698s_ts->irq = client->irq;
	mg8698s_ts->client = client;

	disable_irq(client->irq);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_dev_alloc_failed;
	}

	mg8698s_ts->input_dev = input_dev;

	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	set_bit(ABS_MT_TRACKING_ID,input_dev->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);

	input_set_abs_params(input_dev,
			ABS_MT_POSITION_X, 0, mg8698s_ts->x_max, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_POSITION_Y, 0, mg8698s_ts->y_max, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_TOUCH_MAJOR, 0, 250, 0, 0);
	input_set_abs_params(input_dev,
			ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
    input_set_abs_params(input_dev,
            ABS_MT_TRACKING_ID, 0, 4, 0, 0);
    input_set_abs_params(input_dev,
            ABS_PRESSURE, 0, 256, 0, 0);


	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);

	input_dev->name = MG8698S_NAME;
	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
			"mg8698s_ts_probe: failed to register input device: %s\n",
			dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

	/*make sure CTP already finish startup process */
	mg8698s_ts_reset(mg8698s_ts);
	msleep(10);

    init_completion(&mg8698s_ts->cmd_complete);
    mg8698s_ts->cmd = MG_NONE;
	enable_irq(client->irq);

	return 0;

exit_input_register_device_failed:
	input_free_device(input_dev);

exit_input_dev_alloc_failed:
	free_irq(client->irq, mg8698s_ts);
	gpio_free(mg8698s_ts->gpio.wake->num);

exit_device_tree_read_failed:
	kfree(mg8698s_ts->gpio.wake);

exit_alloc_gpio2_failed:
	kfree(mg8698s_ts->gpio.irq);

exit_alloc_gpio1_failed:
exit_irq_request_failed:
	i2c_set_clientdata(client, NULL);
	kfree(mg8698s_ts);
	mg8698s_ts_power_off(mg8698s_ts);

exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int mg8698s_ts_remove(struct i2c_client *client)
{
	struct mg8698s_ts_data *mg8698s_ts;
	mg8698s_ts = i2c_get_clientdata(client);
	input_unregister_device(mg8698s_ts->input_dev);
	gpio_free(mg8698s_ts->gpio.wake->num);
	free_irq(client->irq, mg8698s_ts);
	i2c_set_clientdata(client, NULL);
	mg8698s_ts_power_off(mg8698s_ts);
	regulator_put(mg8698s_ts->power);
	kfree(mg8698s_ts->gpio.wake);
	kfree(mg8698s_ts->gpio.irq);
	kfree(mg8698s_ts);
	return 0;
}

static const struct i2c_device_id mg8698s_ts_id[] = {
	{MG8698S_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, mg8698s_ts_id);

static const struct of_device_id mg8698_ts_match[] = {
	{ .compatible = "tcs,mg8698_tcs", },
	{ },
};
MODULE_DEVICE_TABLE(of, mg8698_ts_match);

static struct i2c_driver mg8698s_ts_driver = {
	.probe = mg8698s_ts_probe,
	.remove = mg8698s_ts_remove,
	.id_table = mg8698s_ts_id,
	.driver = {
		   .name = MG8698S_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(mg8698_ts_match),
		   },
};

module_i2c_driver(mg8698s_ts_driver);

MODULE_AUTHOR("<hlguo>");
MODULE_DESCRIPTION("FocalTech mg8698s TouchScreen driver");
MODULE_LICENSE("GPL");
