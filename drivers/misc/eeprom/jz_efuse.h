#ifndef __JZ_EFUSE_H__
#define __JZ_EFUSE_H__

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/bitops.h>

#define JZ_EFUCTRL			(0x0)	/* Control Register */
#define JZ_EFUCFG			(0x4)	/* Configure Register*/
#define JZ_EFUSTATE			(0x8)	/* Status Register */
#define JZ_EFUDATA(n)			(0xC + (n)*4)

#define JZ_EFUSE_START_ADDR		0x200
#define JZ_EFUSE_SEG1_OFF		0x00	/* 64 bit Random Number */
#define JZ_EFUSE_SEG2_OFF		0x08	/* 128 bit Ingenic Chip ID */
#define JZ_EFUSE_SEG3_OFF		0x18	/* 128 bit Customer ID */
#define JZ_EFUSE_SEG4_OFF		0x28	/* 3520 bit Reserved */
#define JZ_EFUSE_SEG5_OFF		0x1E0	/* 8 bit Protect Segment */
#define JZ_EFUSE_SEG6_OFF		0x1E1	/* 2296 bit HDMI Key */
#define JZ_EFUSE_SEG7_OFF		0x300	/* 2048 bit Security boot key */
#define JZ_EFUSE_END_ADDR		0x5FF

#define JZ_EFUSE_EFUCTRL_CS		BIT(30)
#define JZ_EFUSE_EFUCTRL_ADDR_MASK	0x1FF
#define JZ_EFUSE_EFUCTRL_ADDR_SHIFT	21
#define JZ_EFUSE_EFUCTRL_LEN_MASK	0x1F
#define JZ_EFUSE_EFUCTRL_LEN_SHIFT	16
#define JZ_EFUSE_EFUCTRL_PG_EN		BIT(15)
#define JZ_EFUSE_EFUCTRL_WR_EN		BIT(1)
#define JZ_EFUSE_EFUCTRL_RD_EN		BIT(0)

#define JZ_EFUSE_EFUCFG_INT_EN		BIT(31)
#define JZ_EFUSE_EFUCFG_RD_ADJ_MASK	0xF
#define JZ_EFUSE_EFUCFG_RD_ADJ_SHIFT	20
#define JZ_EFUSE_EFUCFG_RD_STR_MASK	0xF
#define JZ_EFUSE_EFUCFG_RD_STR_SHIFT	16
#define JZ_EFUSE_EFUCFG_WR_ADJ_MASK	0xF
#define JZ_EFUSE_EFUCFG_WR_ADJ_SHIFT	12
#define JZ_EFUSE_EFUCFG_WR_STR_MASK	0xFFF
#define JZ_EFUSE_EFUCFG_WR_STR_SHIFT	0

#define JZ_EFUSE_EFUSTATE_WR_DONE	BIT(1)
#define JZ_EFUSE_EFUSTATE_RD_DONE	BIT(0)

struct jz_efuse {
	struct device		*dev;
	void __iomem		*iomem;
	struct miscdevice	mdev;
	struct clk		*clk;
	unsigned int		rd_adj;
	unsigned int		rd_strobe;
};

#endif
