/*
 *  LCD control code for CRD080TI01_40NM01
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/fb.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include "crd080ti01_40nm01.h"

struct crd080ti01_40nm01_data {
	int lcd_power;
	struct lcd_device *lcd;
	struct platform_crd080ti01_40nm01_data pdata;
	struct regulator *lcd_vcc_reg;
};

static void crd080ti01_40nm01_on(struct crd080ti01_40nm01_data *dev)
{
	int error;
	dev->lcd_power = 1;
	error = regulator_enable(dev->lcd_vcc_reg);
	if (error) {
		printk("Failed to enable LCD regulator with error %d\n", error);
	}

	if (dev->pdata.gpio_lr >= 0) {
		if (!dev->pdata.left_to_right_scan) {
			gpio_direction_output(dev->pdata.gpio_lr, 0);
		} else {
			gpio_direction_output(dev->pdata.gpio_lr, 1);
		}
	}

	if (dev->pdata.gpio_ud >= 0) {
		if (!dev->pdata.bottom_to_top_scan) {
			gpio_direction_output(dev->pdata.gpio_ud, 0);
		} else {
			gpio_direction_output(dev->pdata.gpio_ud, 1);
		}
	}

	if (dev->pdata.gpio_selb >= 0) {
		if (!dev->pdata.six_bit_mode) {
			gpio_direction_output(dev->pdata.gpio_selb, 0);
		} else {
			gpio_direction_output(dev->pdata.gpio_selb, 1);
		}
	}

	if (dev->pdata.gpio_stbyb >= 0) {
		gpio_direction_output(dev->pdata.gpio_stbyb, 1);
	}

	if (dev->pdata.gpio_rest >= 0) {
		gpio_direction_output(dev->pdata.gpio_rest, 0);
		msleep(100);
		gpio_direction_output(dev->pdata.gpio_rest, 1);
	}

	msleep(80);
}

static void crd080ti01_40nm01_off(struct crd080ti01_40nm01_data *dev)
{
	dev->lcd_power = 0;
	while (regulator_is_enabled(dev->lcd_vcc_reg)) {
		regulator_disable(dev->lcd_vcc_reg);
		printk("%s----%d\n",__func__,__LINE__);
	}

	msleep(30);
}

static int crd080ti01_40nm01_set_power(struct lcd_device *lcd, int power)
{
	struct crd080ti01_40nm01_data *dev= lcd_get_data(lcd);

	if (!power && !(dev->lcd_power)) {
		crd080ti01_40nm01_on(dev);
	} else if (power && (dev->lcd_power)) {
		crd080ti01_40nm01_off(dev);
	}
	return 0;
}

static int crd080ti01_40nm01_get_power(struct lcd_device *lcd)
{
	struct crd080ti01_40nm01_data *dev= lcd_get_data(lcd);

	return dev->lcd_power;
}

static int crd080ti01_40nm01_set_mode(struct lcd_device *lcd, struct fb_videomode *mode)
{
	return 0;
}

static struct lcd_ops crd080ti01_40nm01_ops = {
	.set_power = crd080ti01_40nm01_set_power,
	.get_power = crd080ti01_40nm01_get_power,
	.set_mode = crd080ti01_40nm01_set_mode,
};

static int crd080ti01_40nm01_probe(struct platform_device *pdev)
{
	int ret;
	struct crd080ti01_40nm01_data *dev;
	struct device_node *np = pdev->dev.of_node;

	dev = kzalloc(sizeof(struct crd080ti01_40nm01_data), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, dev);

	dev->lcd_vcc_reg = regulator_get_optional(&pdev->dev, "vccio3v0_reg");
	if (IS_ERR(dev->lcd_vcc_reg)) {
		dev_err(&pdev->dev, "failed to get regulator vlcd\n");
		return PTR_ERR(dev->lcd_vcc_reg);
	}


	dev->pdata.gpio_lr = of_get_named_gpio(np, "left-to-right", 0);
	if (dev->pdata.gpio_lr >= 0) {
		ret = gpio_request(dev->pdata.gpio_lr, "left-to-right");
		if (ret) {
			dev_err(&pdev->dev, "failed to get request gpio %d\n", dev->pdata.gpio_lr);
			return ret;
		}
	}

	dev->pdata.gpio_ud = of_get_named_gpio(np, "up-to-down", 0);
	if (dev->pdata.gpio_ud  >= 0){
		gpio_request(dev->pdata.gpio_ud, "up-to-down");
		if (ret) {
			dev_err(&pdev->dev, "failed to get request gpio %d\n", dev->pdata.gpio_ud);
			return ret;
		}
	}

	dev->pdata.gpio_selb = of_get_named_gpio(np, "mode-select", 0);
	if (dev->pdata.gpio_selb  >= 0){
		gpio_request(dev->pdata.gpio_selb, "mode-select");
		if (ret) {
			dev_err(&pdev->dev, "failed to get request gpio %d\n", dev->pdata.gpio_selb);
			return ret;
		}
	}

	dev->pdata.gpio_stbyb = of_get_named_gpio(np, "standby-mode", 0);
	if (dev->pdata.gpio_stbyb  >= 0){
		gpio_request(dev->pdata.gpio_stbyb, "standby-mode");
		if (ret) {
			dev_err(&pdev->dev, "failed to get request gpio %d\n", dev->pdata.gpio_stbyb);
			return ret;
		}
	}

	dev->pdata.gpio_rest = of_get_named_gpio(np, "reset", 0);
	if (dev->pdata.gpio_rest  >= 0){
		gpio_request(dev->pdata.gpio_rest, "reset");
		if (ret) {
			dev_err(&pdev->dev, "failed to get request gpio %d\n", dev->pdata.gpio_rest);
			return ret;
		}
	}

	if (!of_property_read_u32(np, "left-to-right-scan", &dev->pdata.left_to_right_scan)) {
		if (dev->pdata.left_to_right_scan != 0 && dev->pdata.left_to_right_scan != 1) {
			dev_err(&pdev->dev, "Irregular value set for left-to-right-scan");
			return -EINVAL;
		}
	}

	if (!of_property_read_u32(np, "bottom-to-top-scan", &dev->pdata.bottom_to_top_scan)) {
		if (dev->pdata.bottom_to_top_scan != 0 && dev->pdata.bottom_to_top_scan != 1) {
			dev_err(&pdev->dev, "Irregular value set for bottom-to-top-scan");
			return -EINVAL;
		}
	}

	if (!of_property_read_u32(np, "six-bit-mode", &dev->pdata.six_bit_mode)) {
		if (dev->pdata.six_bit_mode != 0 && dev->pdata.six_bit_mode != 1) {
			dev_err(&pdev->dev, "Irregular value set for six-bit-mode");
			return -EINVAL;
		}
	}

	crd080ti01_40nm01_on(dev);

	dev->lcd = lcd_device_register("crd080ti01_40nm01-lcd", &pdev->dev,
				       dev, &crd080ti01_40nm01_ops);

	if (IS_ERR(dev->lcd)) {
		ret = PTR_ERR(dev->lcd);
		dev->lcd = NULL;
		dev_info(&pdev->dev, "lcd device register error: %d\n", ret);
	} else {
		dev_info(&pdev->dev, "lcd device register success\n");
	}

	return 0;
}

static int crd080ti01_40nm01_remove(struct platform_device *pdev)
{
	struct crd080ti01_40nm01_data *dev = dev_get_drvdata(&pdev->dev);

	lcd_device_unregister(dev->lcd);
	crd080ti01_40nm01_off(dev);

	regulator_put(dev->lcd_vcc_reg);

	if (dev->pdata.gpio_lr  >= 0)
		gpio_free(dev->pdata.gpio_lr);
	if (dev->pdata.gpio_ud  >= 0)
		gpio_free(dev->pdata.gpio_ud);
	if (dev->pdata.gpio_selb  >= 0)
		gpio_free(dev->pdata.gpio_selb);
	if (dev->pdata.gpio_stbyb  >= 0)
		gpio_free(dev->pdata.gpio_stbyb);
	if (dev->pdata.gpio_rest  >= 0)
		gpio_free(dev->pdata.gpio_rest);

	dev_set_drvdata(&pdev->dev, NULL);
	kfree(dev);

	return 0;
}

#ifdef CONFIG_PM
static int crd080ti01_40nm01_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	return 0;
}

static int crd080ti01_40nm01_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define crd080ti01_40nm01_suspend	NULL
#define crd080ti01_40nm01_resume	NULL
#endif



static struct of_device_id jz4780_of_match[] = {
		{ .compatible = "crd080ti01_40nm01-lcd", },
		{ },
};
MODULE_DEVICE_TABLE(of, jz4780_of_match);

static struct platform_driver crd080ti01_40nm01_driver = {
	.driver		= {
		.name	= "crd080ti01_40nm01-lcd",
		.owner	= THIS_MODULE,
		.of_match_table = jz4780_of_match,
	},
	.probe		= crd080ti01_40nm01_probe,
	.remove		= crd080ti01_40nm01_remove,
	.suspend	= crd080ti01_40nm01_suspend,
	.resume		= crd080ti01_40nm01_resume,
};

static int crd080ti01_40nm01_init(void)
{
	return platform_driver_register(&crd080ti01_40nm01_driver);
}
module_init(crd080ti01_40nm01_init);

static void crd080ti01_40nm01_exit(void)
{
	platform_driver_unregister(&crd080ti01_40nm01_driver);
}
module_exit(crd080ti01_40nm01_exit);

MODULE_DESCRIPTION("CRD080TI01_40NM01 lcd panel driver");
MODULE_LICENSE("GPL");
