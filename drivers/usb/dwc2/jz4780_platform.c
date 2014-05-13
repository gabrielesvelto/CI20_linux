/*
 * jz4780_platform.c - JZ4780 DWC2 controller platform driver
 *
 * Copyright (C) Matthijs Kooijman <matthijs@stdin.nl>
 * Copyright (C) 2014 Imagination Technologies Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <asm/mach-jz4780/jz4780-cgu.h>

#include "core.h"
#include "hcd.h"

#define USBRESET_DETECT_TIME 0x96

static const char dwc2_driver_name[] = "dwc2-jz4780";

static const struct dwc2_core_params params_jz4780 = {
	.otg_cap			= -1,
	.otg_ver			= -1,
	.dma_enable			=  1,	/* DMA Enabled */
	.dma_desc_enable		=  0,	/* Buffer Mode */
	.speed				= -1,
	.enable_dynamic_fifo		= -1,
	.en_multiple_tx_fifo		= -1,
	.host_rx_fifo_size		=  1024, /* 1024 DWORDs */
	.host_nperio_tx_fifo_size	=  1024, /* 1024 DWORDs */
	.host_perio_tx_fifo_size	=  1024, /* 1024 DWORDs */
	.max_transfer_size		= -1,
	.max_packet_count		= -1,
	.host_channels			= -1,
	.phy_type			= -1,
	.phy_utmi_width			= -1,
	.phy_ulpi_ddr			= -1,
	.phy_ulpi_ext_vbus		= -1,
	.i2c_enable			= -1,
	.ulpi_fs_ls			= -1,
	.host_support_fs_ls_low_power	= -1,
	.host_ls_low_power_phy_clk	= -1,
	.ts_dline			= -1,
	.reload_ctl			= -1,
	.ahbcfg				= -1,
	.uframe_sched			=  0,
};

struct jz4780_otg_info {
	struct dwc2_hsotg	hsotg;
	struct clk		*otg_clk;
	struct clk		*phy_clk;
};

/**
 * dwc2_driver_remove() - Called when the DWC_otg core is unregistered with the
 * DWC_otg driver
 *
 * @dev: Platform device
 *
 * This routine is called, for example, when the rmmod command is executed. The
 * device may or may not be electrically present. If it is present, the driver
 * stops device processing. Any resources used on behalf of this device are
 * freed.
 */
static int dwc2_driver_remove(struct platform_device *dev)
{
	struct jz4780_otg_info *jz4780_otg = platform_get_drvdata(dev);

	clk_disable_unprepare(jz4780_otg->otg_clk);
	clk_disable_unprepare(jz4780_otg->phy_clk);
	dwc2_hcd_remove(&jz4780_otg->hsotg);

	return 0;
}

static const struct of_device_id dwc2_of_match_table[] = {
	{ .compatible = "ingenic,jz4780-otg", .data = &params_jz4780 },
	{},
};
MODULE_DEVICE_TABLE(of, dwc2_of_match_table);

/**
 * dwc2_driver_probe() - Called when the DWC_otg core is bound to the DWC_otg
 * driver
 *
 * @dev: Platform device
 *
 * This routine creates the driver components required to control the device
 * (core, HCD, and PCD) and initializes the device. The driver components are
 * stored in a dwc2_hsotg structure. A reference to the dwc2_hsotg is saved
 * in the device private data. This allows the driver to access the dwc2_hsotg
 * structure on subsequent calls to driver methods for this device.
 */
static int dwc2_driver_probe(struct platform_device *dev)
{
	const struct of_device_id *match;
	const struct dwc2_core_params *params;
	struct dwc2_core_params defparams;
	struct jz4780_otg_info *jz4780_otg;
	struct dwc2_hsotg *hsotg;
	struct resource *res;
	int retval;
	int irq;
	struct clk *clk_otg_phy, *clk_otg1;

	if (usb_disabled())
		return -ENODEV;

	match = of_match_device(dwc2_of_match_table, &dev->dev);
	if (match && match->data) {
		params = match->data;
	} else {
		/* Default all params to autodetect */
		dwc2_set_all_params(&defparams, -1);
		params = &defparams;
	}

	jz4780_otg = devm_kzalloc(&dev->dev, sizeof(*jz4780_otg), GFP_KERNEL);
	if (!jz4780_otg)
		return -ENOMEM;

	hsotg = &jz4780_otg->hsotg;
	hsotg->dev = &dev->dev;

	/*
	 * Use reasonable defaults so platforms don't have to provide these.
	 */
	if (!dev->dev.dma_mask)
		dev->dev.dma_mask = &dev->dev.coherent_dma_mask;
	retval = dma_set_coherent_mask(&dev->dev, DMA_BIT_MASK(32));
	if (retval)
		return retval;

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		dev_err(&dev->dev, "missing IRQ resource\n");
		return irq;
	}

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	hsotg->regs = devm_ioremap_resource(&dev->dev, res);
	if (IS_ERR(hsotg->regs))
		return PTR_ERR(hsotg->regs);

	dev_dbg(&dev->dev, "mapped PA %08lx to VA %p\n",
		(unsigned long)res->start, hsotg->regs);

	clk_otg_phy = devm_clk_get(&dev->dev, "otg_phy");
	if (IS_ERR(clk_otg_phy)) {
		retval = PTR_ERR(clk_otg_phy);
		dev_err(&dev->dev, "Failed to get otgphy clock: %d\n", retval);
		return retval;
	}

	jz4780_otg->phy_clk = clk_otg_phy;

	retval = clk_prepare_enable(clk_otg_phy);
	if (retval) {
		dev_err(&dev->dev, "Failed to enable otgphy clk:%d\n", retval);
		return retval;
	}

	retval = clk_set_rate(clk_otg_phy, 48000000);
	if (retval) {
		dev_err(&dev->dev, "Failed to set usb otg phy clk rate: %d\n",
			retval);
		goto err_clk_otg_phy;
	}

	clk_otg1 = devm_clk_get(&dev->dev, "otg1");
	if (IS_ERR(clk_otg1)) {
		retval = PTR_ERR(clk_otg1);
		dev_err(&dev->dev, "Failed to get clock:%d\n", retval);
		goto err_clk_otg_phy;
	}

	jz4780_otg->otg_clk = clk_otg1;

	retval = clk_prepare_enable(clk_otg1);
	if (retval) {
		dev_err(&dev->dev, "Failed to enable otg1 clock:%d\n", retval);
		goto err_clk_otg_phy;
	}

	retval = jz4780_cgu_set_usb_otg_mode(USB_OTG_MODE_SYNOPSYS);
	if (retval) {
		dev_err(&dev->dev, "Failed to set usb otg mode: %d\n",
			retval);
		goto err;
	}

	retval = jz4780_cgu_set_usb_utmi_bus_width
			(USB_PORT_OTG, USB_PORT_UTMI_BUS_WIDTH_16);
	if (retval) {
		dev_err(&dev->dev, "Failed to set port utmi bus width: %d\n",
			retval);
		goto err;
	}

	jz4780_cgu_set_usb_iddigfil(0x00);
	jz4780_cgu_set_usb_usbvbfil(0x00);
	jz4780_cgu_set_usb_usbrdt(USBRESET_DETECT_TIME);
	jz4780_cgu_set_usb_vbfil_ld_en(1);

	retval = jz4780_cgu_set_usbpcr_param(USBPCR_USB_MODE, 1);
	if (retval) {
		dev_err(&dev->dev, "Failed to set usbpcr mode: %d\n", retval);
		goto err;
	}

	retval = jz4780_cgu_set_usbpcr_param(USBPCR_VBUSVLDEXT, 1);
	if (retval) {
		dev_err(&dev->dev, "Failed to set usbpcr vbusvldext: %d\n",
			retval);
		goto err;
	}

	retval = jz4780_cgu_set_usbpcr_param(USBPCR_VBUSVLDEXTSEL, 1);
	if (retval) {
		dev_err(&dev->dev, "Failed to set usbpcr vbusvldextsel: %d\n",
			retval);
		goto err;
	}

	retval = jz4780_cgu_set_usbpcr_param(USBPCR_COMMONONN, 1);
	if (retval) {
		dev_err(&dev->dev, "Failed to set usbpcr commononn: %d\n",
			retval);
		goto err;
	}

	retval = jz4780_cgu_set_usbpcr_param(USBPCR_OTG_DISABLE, 0);
	if (retval) {
		dev_err(&dev->dev, "Failed to enable usbpcr OTG: %d\n",
			retval);
		goto err;
	}

	jz4780_cgu_usb_reset();

	retval = jz4780_cgu_set_usb_suspend(USB_PORT_OTG, 0);
	if (retval) {
		dev_err(&dev->dev, "Failed to unsuspend usb port: %d\n",
			retval);
		goto err;
	}

	retval = dwc2_hcd_init(hsotg, irq, params);
	if (retval)
		goto err;

	platform_set_drvdata(dev, jz4780_otg);

	return retval;

err:
	clk_disable_unprepare(clk_otg1);
err_clk_otg_phy:
	clk_disable_unprepare(clk_otg_phy);

	return retval;
}

static struct platform_driver dwc2_platform_driver = {
	.driver = {
		.name = dwc2_driver_name,
		.of_match_table = dwc2_of_match_table,
	},
	.probe = dwc2_driver_probe,
	.remove = dwc2_driver_remove,
};

module_platform_driver(dwc2_platform_driver);

MODULE_DESCRIPTION("JZ4780 DWC2 controller platform driver");
MODULE_AUTHOR("Zubair Lutfullah Kakakhel <Zubair.Kakakhel@imgtec.com>");
MODULE_LICENSE("Dual BSD/GPL");
