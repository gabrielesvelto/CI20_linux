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

#include "jz4780_lcd.h"
#include "jz4780_regs.h"

void *logo_buffer = 0;
struct jz4780_framedesc *fdsc1, *fdsc2;

void jz4780_lcd_start(void __iomem *base)
{
	uint32_t ctrl;

	iowrite32(0, base + LCDC_STATE);
	iowrite32(0, base + LCDC_OSDS);
	ctrl = ioread32(base + LCDC_CTRL);
	ctrl |= LCDC_CTRL_ENA;
	ctrl &= ~LCDC_CTRL_DIS;
	iowrite32(ctrl, base + LCDC_CTRL);

}

void jz4780_lcd_stop(void __iomem *base)
{
	int count = 5;
	uint32_t ctrl;

	ctrl = ioread32(base + LCDC_CTRL);
	ctrl |= LCDC_CTRL_DIS;
	iowrite32(ctrl, base + LCDC_CTRL);
	while (!(ioread32(base + LCDC_STATE) & LCDC_STATE_LDD)
	       && count--) {
		usleep_range(1000, 2000);
	}
	if (count >= 0) {
		ctrl = ioread32(base + LCDC_STATE);
		ctrl &= ~LCDC_STATE_LDD;
		iowrite32(ctrl, base + LCDC_STATE);
	} else {
		DRM_DEBUG_DRIVER("LCDC normal disable state wrong");
	}

}

static inline void jz4780_lcd_update_dma(void __iomem *base)
{
	iowrite32(fdsc2->next, base + LCDC_DA0);
	iowrite32(fdsc1->next, base + LCDC_DA1);
}

void jz4780_lcd_fill_dmafb(struct jz4780_framedesc *framedesc, void __iomem *base)
{
	fdsc1->next = virt_to_phys(fdsc2);
	fdsc1->databuf = framedesc[0].databuf;
	fdsc1->cmd = framedesc[0].cmd;
	fdsc1->offsize = framedesc[0].offsize;
	fdsc1->page_width = framedesc[0].page_width;
	fdsc1->cpos = framedesc[0].cpos;
	fdsc1->desc_size = framedesc[0].desc_size;

	fdsc2->next = virt_to_phys(fdsc1);
	fdsc2->databuf = framedesc[1].databuf;
	fdsc2->cmd = framedesc[1].cmd;
	fdsc2->offsize = framedesc[1].offsize;
	fdsc2->page_width = framedesc[1].page_width;
	fdsc2->cpos = framedesc[1].cpos;
	fdsc2->desc_size = framedesc[1].desc_size;

	jz4780_lcd_update_dma(base);
 }

static inline void jz4780_lcd_set_logo(void __iomem *base)
{

	fdsc1->next = virt_to_phys(fdsc2);
	fdsc1->databuf = virt_to_phys(logo_buffer);
	fdsc1->id = 0xda0;
	fdsc1->cmd = 0x440c0000;
	fdsc1->offsize = 0x0;
	fdsc1->page_width = 0x0;
	fdsc1->cpos = 0x2d000000;
	fdsc1->desc_size = 0xff2ff3ff;

	fdsc2->next = virt_to_phys(fdsc1);
	fdsc2->databuf = virt_to_phys(logo_buffer);
	fdsc2->id = 0xda1;
	fdsc2->cmd = 0x440c0000;
	fdsc2->offsize = 0x0;
	fdsc2->page_width = 0x0;
	fdsc2->cpos = 0x2f000000;
	fdsc2->desc_size = 0xff2ff3ff;

	jz4780_lcd_update_dma(base);
}

void jz4780_lcd_copy_logo(void __iomem *base)
{
	unsigned *src_addr =  (unsigned *) ioread32(base + LCDC_SA0);
	unsigned size = 0x300000;

	logo_buffer = kzalloc(size, GFP_KERNEL);
	if(!logo_buffer) {
		printk("failed to allocate logo_buffer\n");
	}

	fdsc1 = kzalloc(sizeof(struct jz4780_framedesc), GFP_KERNEL);
	if(!fdsc1) {
		printk("failed to allocate fdsc1\n");
	}

	fdsc2 = kzalloc(sizeof(struct jz4780_framedesc), GFP_KERNEL);
	if(!fdsc2) {
		printk("failed to allocate fdsc2\n");
	}

	src_addr = phys_to_virt((unsigned) src_addr);
	memcpy((void *)logo_buffer, (void *) src_addr, size);

	jz4780_lcd_set_logo(base);
}

void jz4780_lcd_release_logo(void)
{
	if (logo_buffer) {
		kfree(logo_buffer);
		logo_buffer = 0;
	}
}
