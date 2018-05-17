/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __LINUX_POWER_AC_CHARGER_H__
#define __LINUX_POWER_AC_CHARGER_H__

#include <linux/power_supply.h>
#include <linux/types.h>

/**
 * struct gpio_charger_platform_data - platform_data for gpio_charger devices
 * @name:		        Name for the chargers power_supply device
 * @type:		        Type of the charger
 * @gpio_ac:		        GPIO which is used to indicate the chargers status
 * @gpio_charging:	        GPIO which is used to indicate the charging status
 * @gpio_ac_active_low:	        Should be set to 1 if the GPIO is active low otherwise 0
 * @gpio_charging_active_low:	Should be set to 1 if the GPIO is active low otherwise 0
 */
struct jz4780_ac_charger_platform_data {
	const char *name;
	enum power_supply_type type;

	int gpio_ac;
	int gpio_ac_active_low;

	int gpio_charging;
	int gpio_charging_active_low;
};
#endif
