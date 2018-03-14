/*
 * Copyright (C) 2017 Imagination Technologies
 * Author: Dragan Cecavac <Dragan.Cecavac@imgtec.com>
 *
 * LCD DRM driver support for Ingenic JZ4780
 * Driver heavily relies on earlier configurations set by the bootloader.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __JZ4780_LCD_H__
#define __JZ4780_LCD_H__

#include <linux/types.h>

struct jz4780_framedesc;

/*
 * jz4780_lcd_fill_dmafb Initializes LCDC's DMA to display
 * graphic data located on the specified address.
 */
void jz4780_lcd_fill_dmafb(struct jz4780_framedesc *framedesc, void __iomem *base);

/*
 * jz4780_lcd_copy_logo copies the logo set by the bootloader
 * and saves it for later use. Frame descriptor passed
 * to LCDC's DMA will eventually be overwritten and
 * unknown data will be passed to LCD display, which will
 * most likely show only the black screen.
 *
 */
void jz4780_lcd_copy_logo(void __iomem *base);
void jz4780_lcd_start(void __iomem *base);
void jz4780_lcd_stop(void __iomem *base);

/*
 * jz4780_lcd_release_log frees the memory used for storing logo.
 */
void jz4780_lcd_release_logo(void);

#endif /* __JZ4780_LCD_H__ */
