/*
 * LCD driver data for CRD080TI01_40NM01
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CRD080TI01_40NM01_H__
#define __CRD080TI01_40NM01_H__

/**
 * @gpio_lr: scan direction, 0: right to left, 1: left to right
 * @gpio_ud: scan direction, 0: top to bottom, 1: bottom to top
 * @gpio_selb: mode select, H: 6bit, L: 8bit
 * @gpio_stbyb: standby mode, normally pull high. 1: normal operation
 * @gpio_rest: global reset pin, active low to enter reset state

 * @left_to_right_scan: scan direction, 0: right to left, 1: left to right
 * @bottom_to_top_scan: scan direction, 0: top to bottom, 1: bottom to top
 * @six_bit_mode: 6bit/8bit mode select, 1: 6bit, 0: 8bit
 */
struct platform_crd080ti01_40nm01_data {
	int gpio_lr;
	int gpio_ud;
	int gpio_selb;
	int gpio_stbyb;
	int gpio_rest;

	int left_to_right_scan;
	int bottom_to_top_scan;
	int six_bit_mode;
};

#endif /* __CRD080TI01_40NM01_H__ */
