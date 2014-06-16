/*
 * act8600.h  --  Voltage regulation for the active-semi act8600
 *
 * Copyright (C) 2014 Imagination Technologies
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_REGULATOR_ACT8600_H
#define __LINUX_REGULATOR_ACT8600_H

#include <linux/regulator/machine.h>
#include <linux/bitops.h>

/*
 * ACT8600 Global Register Map.
 */
#define ACT8600_SYS_MODE	0x00
#define ACT8600_SYS_CTRL	0x01
#define ACT8600_DCDC1_VSET	0x10
#define ACT8600_DCDC1_CTRL	0x12
#define ACT8600_DCDC2_VSET	0x20
#define ACT8600_DCDC2_CTRL	0x22
#define ACT8600_DCDC3_VSET	0x30
#define ACT8600_DCDC3_CTRL	0x32
#define ACT8600_SUDCDC4_VSET	0x40
#define ACT8600_SUDCDC4_CTRL	0x41
#define ACT8600_LDO5_VSET	0x50
#define ACT8600_LDO5_CTRL	0x51
#define ACT8600_LDO6_VSET	0x60
#define ACT8600_LDO6_CTRL	0x61
#define ACT8600_LDO7_VSET	0x70
#define ACT8600_LDO7_CTRL	0x71
#define ACT8600_LDO8_VSET	0x80
#define ACT8600_LDO8_CTRL	0x81
#define ACT8600_LDO910_CTRL	0x91

#define ACT8600_APCH0		0xA1
#define ACT8600_SUSCHG		BIT(7)

#define ACT8600_APCH1		0xA8
#define ACT8600_CHGDAT		BIT(0)
#define ACT8600_INDAT		BIT(1)
#define ACT8600_TEMPDAT		BIT(2)
#define ACT8600_TIMRDAT		BIT(3)
#define ACT8600_CHGSTAT		BIT(4)
#define ACT8600_INSTAT		BIT(5)
#define ACT8600_TEMPSTAT	BIT(6)
#define ACT8600_TIMRSTAT	BIT(7)

#define ACT8600_APCH2		0xA9
#define ACT8600_CHGEOCOUT	BIT(0)
#define ACT8600_INDIS		BIT(1)
#define ACT8600_TEMPOUT		BIT(2)
#define ACT8600_TIMRPRE		BIT(3)
#define ACT8600_CHGEOCIN	BIT(4)
#define ACT8600_INCON		BIT(5)
#define ACT8600_TEMPIN		BIT(6)
#define ACT8600_TIMRTOT		BIT(7)

#define ACT8600_APCH_STAT	0xAA
#define ACT8600_CSTATE_MASK	0x30
#define ACT8600_CSTATE_PRE	0x30
#define ACT8600_CSTATE_CHAGE	0x20
#define ACT8600_CSTATE_EOC	0x10
#define ACT8600_CSTATE_SUSPEND	0x00

#define ACT8600_OTG0		0xB0
#define ACT8600_VBUSDAT		BIT(0)
#define ACT8600_DBILIMQ3	BIT(1)
#define ACT8600_VBUSSTAT	BIT(2)
#define ACT8600_Q1OK		BIT(4)
#define ACT8600_ONQ3		BIT(5)
#define ACT8600_ONQ2		BIT(6)
#define ACT8600_ONQ1		BIT(7)

#define ACT8600_OTG1		0xB2
#define ACT8600_INVBUSR		BIT(7)
#define ACT8600_INVBUSF		BIT(6)
#define ACT8600_VBUSMSK		BIT(1)

/*
 * Field Definitions.
 */
#define ACT8600_ENA			0x80	/* ON - [7] */
#define ACT8600_DIS			0x04	/* DIS - [2] */
#define ACT8600_LDO10_ENA		0x40	/* ON - [6] */
#define ACT8600_VSEL_MASK		0x3F	/* VSET - [5:0] */
#define ACT8600_SUDCDC_VSEL_MASK	0xFF	/* SUDCDC VSET - [7:0] */

/*
 * ACT8600 voltage number
 */
#define ACT8600_VOLTAGE_NUM		64
#define ACT8600_SUDCDC_VOLTAGE_NUM	255

enum {
	ACT8600_ID_DCDC1,
	ACT8600_ID_DCDC2,
	ACT8600_ID_DCDC3,
	ACT8600_ID_SUDCDC4,
	ACT8600_ID_LDO5,
	ACT8600_ID_LDO6,
	ACT8600_ID_LDO7,
	ACT8600_ID_LDO8,
	ACT8600_ID_LDO9,
	ACT8600_ID_LDO10,
	ACT8600_ID_VBUS,
	ACT8600_ID_USB_CHARGER,
	ACT8600_REG_NUM,
};

/**
 * struct act8600_regulator_data - regulator data
 * @id: regulator id
 * @name: regulator name
 * @platform_data: regulator init data
 */
struct act8600_regulator_data {
	int id;
	const char *name;
	struct regulator_init_data *platform_data;
};
#endif
