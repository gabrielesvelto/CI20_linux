/*
 * JZ4780 Low power Control Register regulator driver
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Matt Redfearn <matt.redfearn@imgtec.com>
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
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

/* Maximum loops to wait for a device to power up */
#define JZ4780_LCR_TIMEOUT (100000)

#define SET_BIT(bit, address) \
	iowrite32(ioread32(address) | (1 << bit), address)

#define CLEAR_BIT(bit, address) \
	iowrite32(ioread32(address) & ~(1 << bit), address)

#define TEST_BIT(bit, address) \
	((ioread32(address) & (1 << bit)) != 0)
	
#define JZ4780_LCR_REGULATOR_ID_SCPU	0
#define JZ4780_LCR_REGULATOR_CTRL_SCPU	31
#define JZ4780_LCR_REGULATOR_STAT_SCPU	27

#define JZ4780_LCR_REGULATOR_ID_VPU	1
#define JZ4780_LCR_REGULATOR_CTRL_VPU	30
#define JZ4780_LCR_REGULATOR_STAT_VPU	26

#define JZ4780_LCR_REGULATOR_ID_GPU	2
#define JZ4780_LCR_REGULATOR_CTRL_GPU	29
#define JZ4780_LCR_REGULATOR_STAT_GPU	25

#define JZ4780_LCR_REGULATOR_ID_GPS	3
#define JZ4780_LCR_REGULATOR_CTRL_GPS	28
#define JZ4780_LCR_REGULATOR_STAT_GPS	24

struct jz4780_lcr_regulator_info {
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_dev *rdev;
	void __iomem *io_base;
	u32 ctrl_bit;
	u32 state_bit;
};

static DEFINE_SPINLOCK(jz4780_lcr_lock);

static int jz4780_lcr_regulator_enable(struct regulator_dev *rdev)
{
	struct jz4780_lcr_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned long flags;
	int count = JZ4780_LCR_TIMEOUT;

	dev_dbg(rdev_get_dev(info->rdev), "Enable %s\n", info->desc.name);

	spin_lock_irqsave(&jz4780_lcr_lock, flags);
	CLEAR_BIT(info->ctrl_bit, info->io_base);
	while (TEST_BIT(info->state_bit, info->io_base) && (--count > 0));
	spin_unlock_irqrestore(&jz4780_lcr_lock, flags);

	if (count <= 0) {
		dev_err(rdev_get_dev(info->rdev),
			"Timeout waiting for %s to power up\n",
			info->desc.name);
		return -ETIMEDOUT;
	}
	return 0;
}

static int jz4780_lcr_regulator_disable(struct regulator_dev *rdev)
{
	struct jz4780_lcr_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned long flags;
	int count = JZ4780_LCR_TIMEOUT;

	dev_dbg(rdev_get_dev(info->rdev), "Disable %s\n", info->desc.name);

	spin_lock_irqsave(&jz4780_lcr_lock, flags);
	SET_BIT(info->ctrl_bit, info->io_base);
	while (!TEST_BIT(info->state_bit, info->io_base) && (--count > 0));
	spin_unlock_irqrestore(&jz4780_lcr_lock, flags);

	if (count <= 0) {
		dev_err(rdev_get_dev(info->rdev),
			"Timeout waiting for %s to power down\n",
			info->desc.name);
		return -ETIMEDOUT;
	}
	return 0;
}

static int jz4780_lcr_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct jz4780_lcr_regulator_info *info = rdev_get_drvdata(rdev);

	dev_dbg(rdev_get_dev(info->rdev), "%s: %s (0x%08x)\n", info->desc.name,
		TEST_BIT(info->state_bit, info->io_base) ?
		"disabled" : "enabled",
		readl(info->io_base));
	/* A true state bit indicates the device is disabled */
	return !TEST_BIT(info->state_bit, info->io_base);
}


static const struct regulator_ops jz4780_lcr_regulator_ops = {
	.enable		= jz4780_lcr_regulator_enable,
	.disable	= jz4780_lcr_regulator_disable,
	.is_enabled	= jz4780_lcr_regulator_is_enabled,
};

#define JZ4780_LCR_REGULATOR(_id) \
	{ \
		.desc = { \
			.name	= #_id, \
			.ops	= &jz4780_lcr_regulator_ops, \
			.type	= REGULATOR_VOLTAGE, \
			.id	= JZ4780_LCR_REGULATOR_ID_##_id, \
			.owner	= THIS_MODULE, \
		}, \
		.ctrl_bit	= JZ4780_LCR_REGULATOR_CTRL_##_id, \
		.state_bit	= JZ4780_LCR_REGULATOR_STAT_##_id, \
	}

static struct jz4780_lcr_regulator_info jz4780_lcr_regulator_info[] = {
	JZ4780_LCR_REGULATOR(SCPU),
	JZ4780_LCR_REGULATOR(VPU),
	JZ4780_LCR_REGULATOR(GPU),
	JZ4780_LCR_REGULATOR(GPS),
};

#define JZ4780_LCR_REGULATOR_OF_MATCH(_id) \
	{ \
		.name = #_id, \
		.driver_data = \
		&jz4780_lcr_regulator_info[JZ4780_LCR_REGULATOR_ID_##_id], \
	}

static struct of_regulator_match jz4780_lcr_regulator_matches[] = {
	JZ4780_LCR_REGULATOR_OF_MATCH(SCPU),
	JZ4780_LCR_REGULATOR_OF_MATCH(VPU),
	JZ4780_LCR_REGULATOR_OF_MATCH(GPU),
	JZ4780_LCR_REGULATOR_OF_MATCH(GPS),
};


static int jz4780_lcr_regulator_probe(struct platform_device *pdev)
{
	struct device_node *np, *regulators;
	void __iomem *io_base;
	int i, err;

	dev_dbg(&pdev->dev, "jz4780 LCR driver probe\n");

	np = of_node_get(pdev->dev.of_node);
	if (!np) {
		dev_err(&pdev->dev, "device tree node not found\n");
		return -ENODEV;
	}

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_warn(&pdev->dev, "regulators node not found\n");
	} else {
		err = of_regulator_match(&pdev->dev, regulators,
			jz4780_lcr_regulator_matches,
			ARRAY_SIZE(jz4780_lcr_regulator_matches));
		if (err < 0) {
			dev_err(&pdev->dev, "failed to match\n");
			return err;
		}
	}

	/* Get the IO base address from DT */
	io_base = of_iomap(pdev->dev.of_node, 0);
	if (!io_base) {
		dev_err(&pdev->dev, "failed to map IO memory\n");
		return -ENXIO;
	}

	/* register all regulators */
	for (i = 0; i < ARRAY_SIZE(jz4780_lcr_regulator_info); i++) {
		struct jz4780_lcr_regulator_info *info = NULL;
		struct regulator_init_data *init_data;
		struct regulator_config config = { };

		/* assign per-regulator data */
		info = &jz4780_lcr_regulator_info[i];
		info->dev = &pdev->dev;
		info->io_base = io_base;

		init_data = jz4780_lcr_regulator_matches[i].init_data;

		config.dev = &pdev->dev;
		config.init_data = init_data;
		config.driver_data = info;
		config.of_node = jz4780_lcr_regulator_matches[i].of_node;

		/* register regulator with framework */
		info->rdev = devm_regulator_register(&pdev->dev, &info->desc,
						     &config);
		if (IS_ERR(info->rdev)) {
			err = PTR_ERR(info->rdev);
			dev_err(&pdev->dev,
				"jz4780 LCR failed to register regulator %s\n",
				info->desc.name);
			return err;
		}

		dev_dbg(rdev_get_dev(info->rdev),
			"jz4780 LCR %s probed\n", info->desc.name);
	}

	return 0;
}


static const struct of_device_id jz4780_lcr_regulator_of_match[] = {
	{ .compatible = "ingenic,jz4780-lcr", },
	{ /* Sentinel */ }
};

static struct platform_driver jz4780_lcr_regulator_driver = {
	.probe = jz4780_lcr_regulator_probe,
	.driver	= {
		.name = "jz4780-lcr",
		.owner = THIS_MODULE,
		.of_match_table = jz4780_lcr_regulator_of_match,
	},
};

module_platform_driver(jz4780_lcr_regulator_driver);

MODULE_DESCRIPTION("Ingenic jz4780 LCR driver");
MODULE_AUTHOR("Matt Redfearn <matt.redfearn@imgtec.com>");
MODULE_LICENSE("GPL");
