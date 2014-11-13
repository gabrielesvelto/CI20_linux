/*
 * jz_efuse.c - Driver to read from the Ingenic one time programmable
 * efuse memory
 *
 * Currently supports JZ4780 which has 8K efuse .
 *
 * The rom itself is accessed using a 9 bit address line and an 8 word wide bus
 * which reads/writes based on strobes. The strobe is configured in the config
 * register and is based on number of cycles of the bus clock.
 *
 * Driver supports read only as the writes are done in the Factory
 *
 * Copyright (C) 2014 Imagination Technologies
 *
 * Based on work by Ingenic Semiconductor.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "jz_efuse.h"

static int jz_init_efuse_cfginfo(struct jz_efuse *efuse, unsigned long clk_rate)
{
	/*
	   rd_adj and rd_strobe are 4 bit values
	   bus clk period * (rd_adj + 1) > 6.5ns
	   bus clk period * (rd_adj + 5 + rd_strobe) > 35ns
	*/
	efuse->rd_adj = (((6500 * (clk_rate / 1000000)) / 1000000) + 1) - 1;
	if (efuse->rd_adj > 0x1F)
		return -EINVAL;

	efuse->rd_strobe = ((((35000 * (clk_rate / 1000000)) / 1000000) + 1)
			      - 5 - efuse->rd_adj);
	if (efuse->rd_strobe > 0x1F)
		return -EINVAL;

	return 0;
}

/* We read 32 byte chunks to avoid complexity in the driver. */
static ssize_t jz_efuse_read_32bytes(struct jz_efuse *efuse, char *buf,
		unsigned int addr)
{
	unsigned int tmp = 0;
	int i = 0;
	int timeout = 1000;
	int size = 32;

	/* 1. Set config register */
	tmp = readl(efuse->iomem + JZ_EFUCFG);
	tmp &= ~((JZ_EFUSE_EFUCFG_RD_ADJ_MASK << JZ_EFUSE_EFUCFG_RD_ADJ_SHIFT)
	       | (JZ_EFUSE_EFUCFG_RD_STR_MASK << JZ_EFUSE_EFUCFG_RD_STR_SHIFT));
	tmp |= (efuse->rd_adj << JZ_EFUSE_EFUCFG_RD_ADJ_SHIFT)
	       | (efuse->rd_strobe << JZ_EFUSE_EFUCFG_RD_STR_SHIFT);
	writel(tmp, efuse->iomem + JZ_EFUCFG);

	/*
	 * 2. Set control register to indicate what to read data address,
	 * read data numbers and read enable.
	 */
	tmp = readl(efuse->iomem + JZ_EFUCTRL);
	tmp &= ~(JZ_EFUSE_EFUCFG_RD_STR_SHIFT
		| (JZ_EFUSE_EFUCTRL_ADDR_MASK << JZ_EFUSE_EFUCTRL_ADDR_SHIFT)
		| JZ_EFUSE_EFUCTRL_PG_EN | JZ_EFUSE_EFUCTRL_WR_EN
		| JZ_EFUSE_EFUCTRL_WR_EN);

	/* Need to select CS bit if address accesses upper 4Kbits memory */
	if (addr >= (JZ_EFUSE_START_ADDR + 512))
		tmp |= JZ_EFUSE_EFUCTRL_CS;

	tmp |= (addr << JZ_EFUSE_EFUCTRL_ADDR_SHIFT)
		| ((size - 1) << JZ_EFUSE_EFUCTRL_LEN_SHIFT)
		| JZ_EFUSE_EFUCTRL_RD_EN;
	writel(tmp, efuse->iomem + JZ_EFUCTRL);

	/*
	 * 3. Wait status register RD_DONE set to 1 or EFUSE interrupted,
	 * software can read EFUSE data buffer 0 – 8 registers.
	 */
	do {
		tmp = readl(efuse->iomem + JZ_EFUSTATE);
		usleep_range(1000, 2000);
		if (timeout--)
			break;
	} while (!(tmp & JZ_EFUSE_EFUSTATE_RD_DONE));

	if (timeout <= 0) {
		dev_err(efuse->dev, "Timed out while reading\n");
		return -EAGAIN;
	}

	for (i = 0; i < (size / 4); i++)
		*((unsigned int *)(buf + i * 4))
			 = readl(efuse->iomem + JZ_EFUDATA(i));

	return 0;
}

static ssize_t jz_efuse_id_show(struct device *dev,
		struct device_attribute *attr, char *buf, loff_t lpos)
{
	struct jz_efuse *efuse = dev_get_drvdata(dev);
	unsigned int *data = (unsigned int *) buf;

	jz_efuse_read_32bytes(efuse, buf, lpos);

	return snprintf(buf, PAGE_SIZE, "%08x %08x %08x %08x\n",
				data[0], data[1], data[2], data[3]);
}

static ssize_t jz_efuse_chip_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return jz_efuse_id_show(dev, attr, buf,	JZ_EFUSE_SEG2_OFF);
}

static ssize_t jz_efuse_user_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return jz_efuse_id_show(dev, attr, buf,	JZ_EFUSE_SEG3_OFF);
}

static struct device_attribute jz_efuse_sysfs_attrs[] = {
	__ATTR(chip_id, S_IRUGO, jz_efuse_chip_id_show, NULL),
	__ATTR(user_id, S_IRUGO, jz_efuse_user_id_show, NULL),
};

static struct of_device_id jz_efuse_of_match[] = {
{	.compatible = "ingenic,jz4780-efuse", },
	{ },
};

static int jz_efuse_probe(struct platform_device *pdev)
{
	int ret, i;
	struct resource	*regs;
	struct jz_efuse	*efuse = NULL;
	unsigned long	clk_rate;
	struct device *dev = &pdev->dev;

	efuse = devm_kzalloc(dev, sizeof(struct jz_efuse), GFP_KERNEL);
	if (!efuse)
		return -ENOMEM;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	efuse->iomem = devm_ioremap(dev, regs->start, resource_size(regs));
	if (IS_ERR(efuse->iomem))
		return PTR_ERR(efuse->iomem);

	efuse->clk = devm_clk_get(dev, "bus_clk");
	if (IS_ERR(efuse->clk))
		return PTR_ERR(efuse->clk);

	clk_rate = clk_get_rate(efuse->clk);

	if (jz_init_efuse_cfginfo(efuse, clk_rate) < 0) {
		dev_err(dev, "Cannot set clock configuration\n");
		return -EINVAL;
	}

	efuse->dev = dev;
	efuse->mdev.minor = MISC_DYNAMIC_MINOR;
	efuse->mdev.name =  "jz-efuse";

	for (i = 0; i < ARRAY_SIZE(jz_efuse_sysfs_attrs); i++) {
		ret = device_create_file(&pdev->dev, &jz_efuse_sysfs_attrs[i]);
		if (ret) {
			dev_err(dev, "Cannot make sysfs device files\n");
			return -ENODEV;
		}
	}

	ret = misc_register(&efuse->mdev);
	if (ret < 0) {
		dev_err(dev, "misc_register failed\n");
		return ret;
	}

	platform_set_drvdata(pdev, efuse);

	return 0;
}

static int jz_efuse_remove(struct platform_device *pdev)
{
	struct jz_efuse *efuse = platform_get_drvdata(pdev);

	misc_deregister(&efuse->mdev);

	return 0;
}

static struct platform_driver jz_efuse_driver = {
	.probe		= jz_efuse_probe,
	.remove		= jz_efuse_remove,
	.driver		= {
		.name	= "jz-efuse",
		.of_match_table = jz_efuse_of_match,
	}
};

module_platform_driver(jz_efuse_driver);

MODULE_AUTHOR("Zubair Lutfullah Kakakhel <Zubair.Kakakhel@imgtec.com>");
MODULE_LICENSE("GPL");
