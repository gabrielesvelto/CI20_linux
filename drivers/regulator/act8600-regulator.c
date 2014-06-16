/*
 * act8600-regulator.c - Voltage regulation for the active-semi ACT8600
 * http://www.active-semi.com/sheets/ACT8600_Datasheet.pdf
 *
 * Based on act8600-regulator driver
 * Copyright (C) 2014 Imagination Technologies
 *
 * TODO:
 *	- Implement charging support
 *	- Test VBUS
 *	- Test SUDCDC voltage scaling more thoroughly
 *
 * Working:
 *	- LDO - Voltage scaling tested
 *	- DCDC - Voltage scaling tested
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/act8600.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>

struct act8600 {
	struct device *dev;
	struct regmap *regmap;
};

static const struct regmap_config act8600_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct regulator_linear_range act8600_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 23, 25000),
	REGULATOR_LINEAR_RANGE(1200000, 24, 47, 50000),
	REGULATOR_LINEAR_RANGE(2400000, 48, 63, 100000),
};

/* Despite the datasheet stating 3.3v for reg9, reg9 outputs 1.8v */
static const struct regulator_linear_range act8600_voltage_ranges_reg9[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 0, 0),
};

static const struct regulator_linear_range act8600_voltage_ranges_reg10[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0, 0, 0),
};

static const struct regulator_linear_range act8600_sudcdc_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(3000000, 0, 63, 0),
	REGULATOR_LINEAR_RANGE(3000000, 64, 159, 100000),
	REGULATOR_LINEAR_RANGE(12600000, 160, 191, 200000),
	REGULATOR_LINEAR_RANGE(19000000, 191, 255, 400000),
};

static int act8600_usb_charger_set_current_limit(struct regulator_dev *rdev,
						int min_uA, int max_uA)
{
	struct act8600 *act = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret;

	ret = regmap_read(rdev->regmap, ACT8600_OTG0, &data);
	if (ret < 0) {
		dev_err(act->dev, "%s: register %d read failed with err %d\n",
		__func__, ACT8600_OTG0, ret);
		return ret;
	}

	if (max_uA <= 0 || max_uA > 800000)
		return -EINVAL;
	else if (max_uA <= 400000)
		data &= ~ACT8600_DBILIMQ3;
	else if (max_uA <= 800000)
		data |= ACT8600_DBILIMQ3;

	ret = regmap_write(rdev->regmap, ACT8600_OTG0, data);
	if (ret < 0) {
		dev_err(act->dev, "%s: register %d write failed with err %d\n",
			__func__, ACT8600_OTG0, ret);
		return ret;
	}

	return 0;
}

static int act8600_usb_charger_get_current_limit(struct regulator_dev *rdev)
{
	struct act8600 *act = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret;

	ret = regmap_read(rdev->regmap, ACT8600_OTG0, &data);
	if (ret < 0) {
		dev_err(act->dev, "%s(): register %d read failed with err %d\n",
				__func__, ACT8600_OTG0, ret);
		return ret;
	}

	return (data & ACT8600_DBILIMQ3) ? 800000 : 400000;
}

static struct regulator_ops act8600_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static struct regulator_ops act8600_vbus_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static struct regulator_ops act8600_usb_charger_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.get_current_limit	= act8600_usb_charger_get_current_limit,
	.set_current_limit	= act8600_usb_charger_set_current_limit,
};

static const struct regulator_desc act8600_reg[] = {
	{
		.name = "DCDC_REG1",
		.id = ACT8600_ID_DCDC1,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_VOLTAGE_NUM,
		.linear_ranges = act8600_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges),
		.vsel_reg = ACT8600_DCDC1_VSET,
		.vsel_mask = ACT8600_VSEL_MASK,
		.enable_reg = ACT8600_DCDC1_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC_REG2",
		.id = ACT8600_ID_DCDC2,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_VOLTAGE_NUM,
		.linear_ranges = act8600_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges),
		.vsel_reg = ACT8600_DCDC2_VSET,
		.vsel_mask = ACT8600_VSEL_MASK,
		.enable_reg = ACT8600_DCDC2_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC_REG3",
		.id = ACT8600_ID_DCDC3,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_VOLTAGE_NUM,
		.linear_ranges = act8600_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges),
		.vsel_reg = ACT8600_DCDC3_VSET,
		.vsel_mask = ACT8600_VSEL_MASK,
		.enable_reg = ACT8600_DCDC3_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "SUDCDC_REG4",
		.id = ACT8600_ID_SUDCDC4,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_SUDCDC_VOLTAGE_NUM,
		.linear_ranges = act8600_sudcdc_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_sudcdc_voltage_ranges),
		.vsel_reg = ACT8600_SUDCDC4_VSET,
		.vsel_mask = ACT8600_SUDCDC_VSEL_MASK,
		.enable_reg = ACT8600_SUDCDC4_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG5",
		.id = ACT8600_ID_LDO5,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_VOLTAGE_NUM,
		.linear_ranges = act8600_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges),
		.vsel_reg = ACT8600_LDO5_VSET,
		.vsel_mask = ACT8600_VSEL_MASK,
		.enable_reg = ACT8600_LDO5_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG6",
		.id = ACT8600_ID_LDO6,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_VOLTAGE_NUM,
		.linear_ranges = act8600_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges),
		.vsel_reg = ACT8600_LDO6_VSET,
		.vsel_mask = ACT8600_VSEL_MASK,
		.enable_reg = ACT8600_LDO6_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG7",
		.id = ACT8600_ID_LDO7,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_VOLTAGE_NUM,
		.linear_ranges = act8600_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges),
		.vsel_reg = ACT8600_LDO7_VSET,
		.vsel_mask = ACT8600_VSEL_MASK,
		.enable_reg = ACT8600_LDO7_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG8",
		.id = ACT8600_ID_LDO8,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = ACT8600_VOLTAGE_NUM,
		.linear_ranges = act8600_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges),
		.vsel_reg = ACT8600_LDO8_VSET,
		.vsel_mask = ACT8600_VSEL_MASK,
		.enable_reg = ACT8600_LDO8_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG9",
		.id = ACT8600_ID_LDO9,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.linear_ranges = act8600_voltage_ranges_reg9,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges_reg9),
		.enable_reg = ACT8600_LDO910_CTRL,
		.enable_mask = ACT8600_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO_REG10",
		.id = ACT8600_ID_LDO10,
		.ops = &act8600_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.linear_ranges = act8600_voltage_ranges_reg10,
		.n_linear_ranges = ARRAY_SIZE(act8600_voltage_ranges_reg10),
		.enable_reg = ACT8600_LDO910_CTRL,
		.enable_mask = ACT8600_LDO10_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "VBUS",
		.id = ACT8600_ID_VBUS,
		.ops = &act8600_vbus_ops,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = ACT8600_OTG0,
		.enable_mask = ACT8600_ONQ1,
		.owner = THIS_MODULE,
	},
	{
		.name = "USB_CHARGER",
		.id = ACT8600_ID_USB_CHARGER,
		.ops = &act8600_usb_charger_ops,
		.enable_reg = ACT8600_APCH0,
		.enable_mask = ACT8600_SUSCHG,
		.enable_is_inverted = true,
		.type = REGULATOR_CURRENT,
		.owner = THIS_MODULE,
	},
};

static const struct of_device_id act8600_dt_ids[] = {
	{ .compatible = "active-semi,act8600" },
	{ }
};
MODULE_DEVICE_TABLE(of, act8600_dt_ids);

static struct of_regulator_match act8600_matches[] = {
	[ACT8600_ID_DCDC1]		= { .name = "DCDC_REG1"},
	[ACT8600_ID_DCDC2]		= { .name = "DCDC_REG2"},
	[ACT8600_ID_DCDC3]		= { .name = "DCDC_REG3"},
	[ACT8600_ID_SUDCDC4]		= { .name = "SUDCDC_REG4"},
	[ACT8600_ID_LDO5]		= { .name = "LDO_REG5"},
	[ACT8600_ID_LDO6]		= { .name = "LDO_REG6"},
	[ACT8600_ID_LDO7]		= { .name = "LDO_REG7"},
	[ACT8600_ID_LDO8]		= { .name = "LDO_REG8"},
	[ACT8600_ID_LDO9]		= { .name = "LDO_REG9"},
	[ACT8600_ID_LDO10]		= { .name = "LDO_REG10"},
	[ACT8600_ID_VBUS]		= { .name = "VBUS"},
	[ACT8600_ID_USB_CHARGER]	= { .name = "USB_CHARGER"},
};

static int act8600_pmic_probe(struct i2c_client *client,
			   const struct i2c_device_id *i2c_id)
{
	struct regulator_dev *rdev;
	struct device *dev = &client->dev;
	struct regulator_config config;
	struct act8600 *act8600;
	struct device_node *of_node[ACT8600_REG_NUM];
	struct device_node *np;
	struct act8600_regulator_data *regulators;
	int i, id, matched, num_regulators;
	int error;

	np = of_get_child_by_name(dev->of_node, "regulators");
	if (!np) {
		dev_err(dev, "missing 'regulators' subnode in DT\n");
		return -EINVAL;
	}

	matched = of_regulator_match(dev, np,
				act8600_matches, ARRAY_SIZE(act8600_matches));
	of_node_put(np);
	if (matched <= 0)
		return matched;

	regulators = devm_kzalloc(dev, sizeof(struct act8600_regulator_data) *
				ARRAY_SIZE(act8600_matches), GFP_KERNEL);
	if (!regulators)
		return -ENOMEM;

	num_regulators = matched;

	for (i = 0; i < ARRAY_SIZE(act8600_matches); i++) {
		regulators[i].id = i;
		regulators[i].name = act8600_matches[i].name;
		regulators[i].platform_data = act8600_matches[i].init_data;
		of_node[i] = act8600_matches[i].of_node;
	}

	if (num_regulators > ACT8600_REG_NUM) {
		dev_err(dev, "Too many regulators found!\n");
		return -EINVAL;
	}

	act8600 = devm_kzalloc(dev, sizeof(struct act8600), GFP_KERNEL);
	if (!act8600)
		return -ENOMEM;

	act8600->regmap = devm_regmap_init_i2c(client, &act8600_regmap_config);
	if (IS_ERR(act8600->regmap)) {
		error = PTR_ERR(act8600->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	act8600->dev = dev;

	/* Finally register devices */
	for (i = 0; i < ACT8600_REG_NUM; i++) {

		id = regulators[i].id;

		config.dev = dev;
		config.init_data = regulators[i].platform_data;
		config.of_node = of_node[i];
		config.driver_data = act8600;
		config.regmap = act8600->regmap;

		rdev = devm_regulator_register(&client->dev, &act8600_reg[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "Failed to register %s\n",
				act8600_reg[id].name);
			return PTR_ERR(rdev);
		}
	}

	i2c_set_clientdata(client, act8600);

	return 0;
}

static const struct i2c_device_id act8600_ids[] = {
	{ "act8600", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, act8600_ids);

static struct i2c_driver act8600_pmic_driver = {
	.driver	= {
		.name	= "act8600",
		.owner	= THIS_MODULE,
		.of_match_table = act8600_dt_ids,
	},
	.probe		= act8600_pmic_probe,
	.id_table	= act8600_ids,
};

module_i2c_driver(act8600_pmic_driver);

MODULE_DESCRIPTION("Active-Semi act8600 voltage regulator driver");
MODULE_AUTHOR("Zubair Lutfullah Kakakhel <Zubair.Kakakhel@imgtec.com>");
MODULE_LICENSE("GPL v2");
