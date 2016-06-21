/*
 * IMG jz4780 USB PHY driver
 *
 * Copyright (C) 2016 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#include <asm/mach-jz4740/jz4780-cgu.h>

#define USBRESET_DETECT_TIME	0x96

struct jz4780_usb_phy {
	struct device *dev;
	struct clk *phy_clk;
};

static int jz4780_usb_phy_power_on(struct phy *phy)
{
	struct jz4780_usb_phy *p_phy = phy_get_drvdata(phy);
	int ret;

	ret = clk_prepare_enable(p_phy->phy_clk);
	if (ret < 0) {
		dev_err(p_phy->dev, "Failed to enable PHY clock: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(p_phy->phy_clk, 48000000);
	if (ret) {
		dev_err(p_phy->dev, "Failed to set usb phy clk rate: %d\n", ret);
		return ret;
	}

	/* select dwc otg */
	ret = jz4780_cgu_set_usb_otg_mode(USB_OTG_MODE_SYNOPSYS);
	if (ret) {
		dev_err(p_phy->dev, "Failed to set usb otg mode: %d\n", ret);
		goto err;
	}

	/* select utmi data bus width of port0 to 16bit/30M */
	ret = jz4780_cgu_set_usb_utmi_bus_width(
		USB_PORT_OTG, USB_PORT_UTMI_BUS_WIDTH_16);
	if (ret) {
		dev_err(p_phy->dev, "Failed to set port utmi bus width: %d\n", ret);
		goto err;
	}

	jz4780_cgu_set_usb_usbvbfil(0x00);
	jz4780_cgu_set_usb_usbrdt(USBRESET_DETECT_TIME);
	jz4780_cgu_set_usb_vbfil_ld_en(1);

	ret = jz4780_cgu_set_usbpcr_param(USBPCR_USB_MODE, 1);
	if (ret) {
		dev_err(p_phy->dev, "Failed to set usbpcr mode: %d\n", ret);
		goto err;
	}

	ret = jz4780_cgu_set_usbpcr_param(USBPCR_VBUSVLDEXT, 1);
	if (ret) {
		dev_err(p_phy->dev, "Failed to set usbpcr vbusvldext: %d\n", ret);
		goto err;
	}

	ret = jz4780_cgu_set_usbpcr_param(USBPCR_VBUSVLDEXTSEL, 1);
	if (ret) {
		dev_err(p_phy->dev, "Failed to set usbpcr vbusvldextsel: %d\n", ret);
		goto err;
	}

	ret = jz4780_cgu_set_usbpcr_param(USBPCR_COMMONONN, 1);
	if (ret) {
		dev_err(p_phy->dev, "Failed to set usbpcr commononn: %d\n",	ret);
		goto err;
	}

	ret = jz4780_cgu_set_usbpcr_param(USBPCR_OTG_DISABLE, 0);
	if (ret) {
		dev_err(p_phy->dev, "Failed to enable usbpcr OTG: %d\n", ret);
		goto err;
	}

	jz4780_cgu_usb_reset();

	ret = jz4780_cgu_set_usb_suspend(USB_PORT_OTG, 0);
	if (ret) {
		dev_err(p_phy->dev, "Failed to unsuspend usb port: %d\n", ret);
		goto err;
	}

	dev_info(p_phy->dev, "initialized\n");
	return 0;

err:
	dev_err(p_phy->dev, "PHY init failed: %d\n", ret);
	clk_disable_unprepare(p_phy->phy_clk);

	return ret;
}

static int jz4780_usb_phy_power_off(struct phy *phy)
{
	struct jz4780_usb_phy *p_phy = phy_get_drvdata(phy);

	clk_disable_unprepare(p_phy->phy_clk);
	return 0;
}

static const struct phy_ops jz4780_usb_phy_ops = {
	.power_on = jz4780_usb_phy_power_on,
	.power_off = jz4780_usb_phy_power_off,
	.owner = THIS_MODULE,
};

static int jz4780_usb_phy_probe(struct platform_device *pdev)
{
	struct jz4780_usb_phy *p_phy;
	struct phy_provider *provider;
	struct phy *phy;

	p_phy = devm_kzalloc(&pdev->dev, sizeof(*p_phy), GFP_KERNEL);
	if (!p_phy)
		return -ENOMEM;
	p_phy->dev = &pdev->dev;
	platform_set_drvdata(pdev, p_phy);

	p_phy->phy_clk = devm_clk_get(p_phy->dev, "usb_phy");
	if (IS_ERR(p_phy->phy_clk)) {
		dev_err(p_phy->dev, "Failed to get usb_phy clock: %ld\n",
			PTR_ERR(p_phy->phy_clk));
		return PTR_ERR(p_phy->phy_clk);
	}

	phy = devm_phy_create(p_phy->dev, NULL, &jz4780_usb_phy_ops, NULL);
	if (IS_ERR(phy)) {
		dev_err(p_phy->dev, "Failed to create PHY: %ld\n",
			PTR_ERR(phy));
		return PTR_ERR(phy);
	}
	phy_set_drvdata(phy, p_phy);

	provider = devm_of_phy_provider_register(p_phy->dev,
						 of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(p_phy->dev, "Failed to register PHY provider: %ld\n",
			PTR_ERR(provider));
		return PTR_ERR(provider);
	}

	return 0;
}

static const struct of_device_id jz4780_usb_phy_of_match[] = {
	{ .compatible = "img,jz4780-usb-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, jz4780_usb_phy_of_match);

static struct platform_driver jz4780_usb_phy_driver = {
	.probe		= jz4780_usb_phy_probe,
	.driver		= {
		.name	= "jz4780-usb-phy",
		.of_match_table = jz4780_usb_phy_of_match,
	},
};
module_platform_driver(jz4780_usb_phy_driver);

MODULE_AUTHOR("Miodrag Dinic <miodrag.dinic@imgtec.com>");
MODULE_DESCRIPTION("IMG jz4780 USB2.0 PHY driver");
MODULE_LICENSE("GPL v2");
