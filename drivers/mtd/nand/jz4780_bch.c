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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "jz4780_bch.h"

#define BCH_BHCR			0x0
#define BCH_BHCCR			0x8
#define BCH_BHCNT			0xc
#define BCH_BHDR			0x10
#define BCH_BHPAR0			0x14
#define BCH_BHERR0			0x84
#define BCH_BHINT			0x184
#define BCH_BHINTES			0x188
#define BCH_BHINTEC			0x18c
#define BCH_BHINTE			0x190

#define BCH_BHCR_BSEL_SHIFT		4
#define BCH_BHCR_BSEL_MASK		(0x7f << BCH_BHCR_BSEL_SHIFT)
#define BCH_BHCR_ENCE			BIT(2)
#define BCH_BHCR_INIT			BIT(1)
#define BCH_BHCR_BCHE			BIT(0)

#define BCH_BHCNT_PARITYSIZE_SHIFT	16
#define BCH_BHCNT_PARITYSIZE_MASK	(0x7f << BCH_BHCNT_PARITYSIZE_SHIFT)
#define BCH_BHCNT_BLOCKSIZE_SHIFT	0
#define BCH_BHCNT_BLOCKSIZE_MASK	(0x7ff << BCH_BHCNT_BLOCKSIZE_SHIFT)

#define BCH_BHERR_MASK_SHIFT		16
#define BCH_BHERR_MASK_MASK		(0xffff << BCH_BHERR_MASK_SHIFT)
#define BCH_BHERR_INDEX_SHIFT		0
#define BCH_BHERR_INDEX_MASK		(0x7ff << BCH_BHERR_INDEX_SHIFT)

#define BCH_BHINT_ERRC_SHIFT		24
#define BCH_BHINT_ERRC_MASK		(0x7f << BCH_BHINT_ERRC_SHIFT)
#define BCH_BHINT_TERRC_SHIFT		16
#define BCH_BHINT_TERRC_MASK		(0x7f << BCH_BHINT_TERRC_SHIFT)
#define BCH_BHINT_DECF			BIT(3)
#define BCH_BHINT_ENCF			BIT(2)
#define BCH_BHINT_UNCOR			BIT(1)
#define BCH_BHINT_ERR			BIT(0)

#define BCH_CLK_RATE			(200 * 1000 * 1000)

/* Timeout for BCH calculation/correction in milliseconds. */
#define BCH_TIMEOUT			100

struct jz4780_bch {
	void __iomem *base;
	struct clk *clk;
};

static void jz4780_bch_init(struct jz4780_bch *bch,
			    struct jz4780_bch_params *params, bool encode)
{
	uint32_t reg;

	/* Clear interrupt status. */
	writel(readl(bch->base + BCH_BHINT), bch->base + BCH_BHINT);

	/* Set up BCH count register. */
	reg = params->size << BCH_BHCNT_BLOCKSIZE_SHIFT;
	reg |= params->bytes << BCH_BHCNT_PARITYSIZE_SHIFT;
	writel(reg, bch->base + BCH_BHCNT);

	/* Initialise and enable BCH. */
	reg = BCH_BHCR_BCHE | BCH_BHCR_INIT;
	reg |= params->strength << BCH_BHCR_BSEL_SHIFT;
	if (encode)
		reg |= BCH_BHCR_ENCE;
	writel(reg, bch->base + BCH_BHCR);
}

static void jz4780_bch_disable(struct jz4780_bch *bch)
{
	writel(readl(bch->base + BCH_BHINT), bch->base + BCH_BHINT);
	writel(BCH_BHCR_BCHE, bch->base + BCH_BHCCR);
}

static void jz4780_bch_write_data(struct jz4780_bch *bch, const void *buf,
				  size_t size)
{
	size_t size32 = size / sizeof(uint32_t);
	size_t size8 = size & (sizeof(uint32_t) - 1);
	const uint32_t *src32;
	const uint8_t *src8;

	src32 = (const uint32_t *)buf;
	while (size32--)
		writel(*src32++, bch->base + BCH_BHDR);

	src8 = (const uint8_t *)src32;
	while (size8--)
		writeb(*src8++, bch->base + BCH_BHDR);
}

static void jz4780_bch_read_parity(struct jz4780_bch *bch, void *buf,
				   size_t size)
{
	size_t size32 = size / sizeof(uint32_t);
	size_t size8 = size & (sizeof(uint32_t) - 1);
	uint32_t *dest32;
	uint8_t *dest8;
	uint32_t val, offset = 0;

	dest32 = (uint32_t *)buf;
	while (size32--) {
		*dest32++ = readl(bch->base + BCH_BHPAR0 + offset);
		offset += sizeof(uint32_t);
	}

	dest8 = (uint8_t *)dest32;
	val = readl(bch->base + BCH_BHPAR0 + offset);
	switch (size8) {
	case 3:
		dest8[2] = (val >> 16) & 0xff;
	case 2:
		dest8[1] = (val >> 8) & 0xff;
	case 1:
		dest8[0] = val & 0xff;
		break;
	}
}

static bool jz4780_bch_wait_complete(struct jz4780_bch *bch, unsigned int irq,
				     uint32_t *status)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(BCH_TIMEOUT);
	uint32_t reg;

	/*
	 * While we could use use interrupts here and sleep until the operation
	 * completes, the controller works fairly quickly (usually a few
	 * microseconds), so the overhead of sleeping until we get an interrupt
	 * actually quite noticably decreases performance.
	 */
	do {
		reg = readl(bch->base + BCH_BHINT);
		if ((reg & irq) == irq) {
			if (status)
				*status = reg;

			writel(reg, bch->base + BCH_BHINT);
			return true;
		}

		cond_resched();
	} while (time_before(jiffies, timeout));

	return false;
}

/**
 * jz4780_bch_calculate() - calculate ECC for a data buffer
 * @dev: BCH device.
 * @params: BCH parameters.
 * @buf: input buffer with raw data.
 * @ecc_code: output buffer with ECC.
 *
 * Return: 0 on success, -ETIMEDOUT if timed out while waiting for BCH
 * controller.
 */
int jz4780_bch_calculate(struct device *dev, struct jz4780_bch_params *params,
			 const uint8_t *buf, uint8_t *ecc_code)
{
	struct jz4780_bch *bch = dev_get_drvdata(dev);
	int ret = 0;

	jz4780_bch_init(bch, params, true);
	jz4780_bch_write_data(bch, buf, params->size);

	if (jz4780_bch_wait_complete(bch, BCH_BHINT_ENCF, NULL)) {
		jz4780_bch_read_parity(bch, ecc_code, params->bytes);
	} else {
		dev_err(dev, "timed out while calculating ECC\n");
		ret = -ETIMEDOUT;
	}

	jz4780_bch_disable(bch);
	return ret;
}
EXPORT_SYMBOL(jz4780_bch_calculate);

/**
 * jz4780_bch_correct() - detect and correct bit errors
 * @dev: BCH device.
 * @params: BCH parameters.
 * @buf: raw data read from the chip.
 * @ecc_code: ECC read from the chip.
 *
 * Given the raw data and the ECC read from the NAND device, detects and
 * corrects errors in the data.
 *
 * Return: the number of bit errors corrected, or -1 if there are too many
 * errors to correct or we timed out waiting for the controller.
 */
int jz4780_bch_correct(struct device *dev, struct jz4780_bch_params *params,
		       uint8_t *buf, uint8_t *ecc_code)
{
	struct jz4780_bch *bch = dev_get_drvdata(dev);
	uint32_t reg, mask, index;
	int i, ret, count;

	jz4780_bch_init(bch, params, false);
	jz4780_bch_write_data(bch, buf, params->size);
	jz4780_bch_write_data(bch, ecc_code, params->bytes);

	if (!jz4780_bch_wait_complete(bch, BCH_BHINT_DECF, &reg)) {
		dev_err(dev, "timed out while correcting data\n");
		ret = -1;
		goto out;
	}

	if (reg & BCH_BHINT_UNCOR) {
		dev_warn(dev, "uncorrectable ECC error\n");
		ret = -1;
		goto out;
	}

	/* Correct any detected errors. */
	if (reg & BCH_BHINT_ERR) {
		count = (reg & BCH_BHINT_ERRC_MASK) >> BCH_BHINT_ERRC_SHIFT;
		ret = (reg & BCH_BHINT_TERRC_MASK) >> BCH_BHINT_TERRC_SHIFT;

		for (i = 0; i < count; i++) {
			reg = readl(bch->base + BCH_BHERR0 + (i * 4));
			mask = (reg & BCH_BHERR_MASK_MASK) >>
						BCH_BHERR_MASK_SHIFT;
			index = (reg & BCH_BHERR_INDEX_MASK) >>
						BCH_BHERR_INDEX_SHIFT;
			buf[(index * 2) + 0] ^= mask;
			buf[(index * 2) + 1] ^= mask >> 8;
		}
	} else {
		ret = 0;
	}

out:
	jz4780_bch_disable(bch);
	return ret;
}
EXPORT_SYMBOL(jz4780_bch_correct);

/**
 * jz4780_bch_get() - get the BCH controller device
 * @np: BCH device tree node.
 * @dev: where to store pointer to BCH controller device.
 *
 * Gets the BCH controller device from the specified device tree node. The
 * device must be released with jz4780_bch_release() when it is no longer being
 * used.
 *
 * Return: 0 on success, -EPROBE_DEFER if the controller has not yet been
 * initialised.
 */
int jz4780_bch_get(struct device_node *np, struct device **dev)
{
	struct platform_device *pdev;
	struct jz4780_bch *bch;

	pdev = of_find_device_by_node(np);
	if (!pdev || !platform_get_drvdata(pdev))
		return -EPROBE_DEFER;

	bch = platform_get_drvdata(pdev);
	clk_prepare_enable(bch->clk);

	*dev = &pdev->dev;
	return 0;
}
EXPORT_SYMBOL(jz4780_bch_get);

/**
 * jz4780_bch_release() - release the BCH controller device
 * @dev: BCH device.
 */
void jz4780_bch_release(struct device *dev)
{
	struct jz4780_bch *bch = dev_get_drvdata(dev);

	clk_disable_unprepare(bch->clk);
}
EXPORT_SYMBOL(jz4780_bch_release);

static int jz4780_bch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct jz4780_bch *bch;
	struct resource *res;

	bch = devm_kzalloc(dev, sizeof(*bch), GFP_KERNEL);
	if (!bch)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bch->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(bch->base)) {
		dev_err(dev, "failed to get I/O memory\n");
		return PTR_ERR(bch->base);
	}

	jz4780_bch_disable(bch);

	bch->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(bch->clk)) {
		dev_err(dev, "failed to get clock: %ld\n", PTR_ERR(bch->clk));
		return PTR_ERR(bch->clk);
	}

	clk_set_rate(bch->clk, BCH_CLK_RATE);

	platform_set_drvdata(pdev, bch);
	dev_info(dev, "JZ4780 BCH initialised\n");
	return 0;
}

static int jz4780_bch_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id jz4780_bch_dt_match[] = {
	{ .compatible = "ingenic,jz4780-bch" },
	{},
};
MODULE_DEVICE_TABLE(of, jz4780_bch_dt_match);

static struct platform_driver jz4780_bch_driver = {
	.probe		= jz4780_bch_probe,
	.remove		= jz4780_bch_remove,
	.driver	= {
		.name	= "jz4780-bch",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(jz4780_bch_dt_match),
	},
};
module_platform_driver(jz4780_bch_driver);

MODULE_AUTHOR("Alex Smith <alex.smith@imgtec.com>");
MODULE_DESCRIPTION("Ingenic JZ4780 BCH error correction driver");
MODULE_LICENSE("GPL");
