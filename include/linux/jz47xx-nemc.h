/*
 * JZ47xx NAND/external memory controller (NEMC)
 *
 * Copyright (c) 2014 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __LINUX_JZ47XX_NEMC_H__
#define __LINUX_JZ47XX_NEMC_H__

#include <linux/types.h>

struct device;

/*
 * Number of NEMC banks. Note that there are actually 6, but they are numbered
 * from 1.
 */
#define JZ47XX_NEMC_NUM_BANKS	7

/**
 * enum jz47xx_nemc_bank_type - device types which can be connected to a bank
 * @JZ47XX_NEMC_BANK_SRAM: SRAM
 * @JZ47XX_NEMC_BANK_NAND: NAND
 */
enum jz47xx_nemc_bank_type {
	JZ47XX_NEMC_BANK_SRAM,
	JZ47XX_NEMC_BANK_NAND,
};

extern unsigned int jz47xx_nemc_num_banks(struct device *dev);

extern void jz47xx_nemc_set_type(struct device *dev, unsigned int bank,
				 enum jz47xx_nemc_bank_type type);
extern void jz47xx_nemc_assert(struct device *dev, unsigned int bank,
			       bool assert);

#endif /* __LINUX_JZ47XX_NEMC_H__ */
