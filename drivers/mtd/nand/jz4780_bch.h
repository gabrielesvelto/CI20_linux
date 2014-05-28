/*
 * JZ4780 BCH controller
 *
 * Copyright (c) 2014 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __DRIVERS_MTD_NAND_JZ4780_BCH_H__
#define __DRIVERS_MTD_NAND_JZ4780_BCH_H__

#include <linux/types.h>

struct device;
struct device_node;

/**
 * struct jz4780_bch_params - BCH parameters
 * @size: data bytes per ECC step.
 * @bytes: ECC bytes per step.
 * @strength: number of correctable bits per ECC step.
 */
struct jz4780_bch_params {
	int size;
	int bytes;
	int strength;
};

extern int jz4780_bch_calculate(struct device *dev,
				struct jz4780_bch_params *params,
				const uint8_t *buf, uint8_t *ecc_code);
extern int jz4780_bch_correct(struct device *dev,
			      struct jz4780_bch_params *params, uint8_t *buf,
			      uint8_t *ecc_code);

extern int jz4780_bch_get(struct device_node *np, struct device **dev);
extern void jz4780_bch_release(struct device *dev);

#endif /* __DRIVERS_MTD_NAND_JZ4780_BCH_H__ */
