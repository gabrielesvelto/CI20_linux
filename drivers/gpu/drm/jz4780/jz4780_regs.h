/*
 * Copyright (c) 2014 Imagination Technologies
 * Author: Zubair Lutfullah Kakakhel <Zubair.Kakakhel@imgtec.com>
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

#ifndef __JZ4780_REGS_H__
#define __JZ4780_REGS_H__

/* LCDC register definitions */

#include <linux/bitops.h>

#include "jz4780_drv.h"

/* Register Map Of LCDC */
#define LCDC_CFG			0x00
#define LCDC_CTRL			0x30
#define LCDC_STATE			0x34
#define LCDC_OSDC			0x100
#define LCDC_OSDCTRL			0x104
#define LCDC_OSDS			0x108
#define LCDC_BGC0			0x10c
#define LCDC_BGC1			0x2c4
#define LCDC_KEY0			0x110
#define LCDC_KEY1			0x114
#define LCDC_ALPHA			0x118
#define LCDC_IPUR			0x11c
#define LCDC_RGBC			0x90
#define LCDC_VAT			0x0c
#define LCDC_DAH			0x10
#define LCDC_DAV			0x14
#define LCDC_XYP0			0x120
#define LCDC_XYP1			0x124
#define LCDC_SIZE0			0x128
#define LCDC_SIZE1			0x12c
#define LCDC_VSYNC			0x04
#define LCDC_HSYNC			0x08
#define LCDC_PS				0x18
#define LCDC_CLS			0x1c
#define LCDC_SPL			0x20
#define LCDC_REV			0x24
#define LCDC_IID			0x38
#define LCDC_DA0			0x40
#define LCDC_SA0			0x44
#define LCDC_FID0			0x48
#define LCDC_CMD0			0x4c
#define LCDC_OFFS0			0x60
#define LCDC_PW0			0x64
#define LCDC_CNUM0			0x68
#define LCDC_CPOS0			0x68
#define LCDC_DESSIZE0			0x6c
#define LCDC_DA1			0x50
#define LCDC_SA1			0x54
#define LCDC_FID1			0x58
#define LCDC_CMD1			0x5c
#define LCDC_OFFS1			0x70
#define LCDC_PW1			0x74
#define LCDC_CNUM1			0x78
#define LCDC_CPOS1			0x78
#define LCDC_DESSIZE1			0x7c
#define LCDC_PCFG			0x2c0
#define LCDC_DUAL_CTRL			0x2c8
#define LCDC_ENH_CFG			0x400
#define LCDC_ENH_CSCCFG			0x404
#define LCDC_ENH_LUMACFG		0x408
#define LCDC_ENH_CHROCFG0		0x40c
#define LCDC_ENH_CHROCFG1		0x410
#define LCDC_ENH_DITHERCFG		0x414
#define LCDC_ENH_STATUS			0x418
#define LCDC_ENH_GAMMA			0x800
#define LCDC_ENH_VEE			0x1000

/* LCD Configure Register */
#define LCDC_CFG_LCDPIN_BIT		31
#define LCDC_CFG_LCDPIN_MASK		(0x1 << LCDC_CFG_LCDPIN_BIT)
#define LCDC_CFG_LCDPIN_LCD		(0x0 << LCDC_CFG_LCDPIN_BIT)
#define LCDC_CFG_LCDPIN_SLCD		(0x1 << LCDC_CFG_LCDPIN_BIT)
#define LCDC_CFG_TVEPEH			BIT(30)
#define LCDC_CFG_NEWDES			BIT(28)
#define LCDC_CFG_PALBP			BIT(27)
#define LCDC_CFG_TVEN			BIT(26)
#define LCDC_CFG_RECOVER		BIT(25)

#define LCDC_CFG_PSM			BIT(23)
#define LCDC_CFG_CLSM			BIT(22)
#define LCDC_CFG_SPLM			BIT(21)
#define LCDC_CFG_REVM			BIT(20)
#define LCDC_CFG_HSYNM			BIT(19)
#define LCDC_CFG_PCLKM			BIT(18)
#define LCDC_CFG_INVDAT			BIT(17)
#define LCDC_CFG_SYNDIR_IN		BIT(16)
#define LCDC_CFG_PSP			BIT(15)
#define LCDC_CFG_CLSP			BIT(14)
#define LCDC_CFG_SPLP			BIT(13)
#define LCDC_CFG_REVP			BIT(12)
#define LCDC_CFG_HSP			BIT(11)
#define LCDC_CFG_PCP			BIT(10)
#define LCDC_CFG_DEP			BIT(9)
#define LCDC_CFG_VSP			BIT(8)
#define LCDC_CFG_MODE_TFT_18BIT		BIT(7)
#define LCDC_CFG_MODE_TFT_16BIT		(0 << 7)
#define LCDC_CFG_MODE_TFT_24BIT		BIT(6)

#define LCDC_CFG_MODE_BIT		0
#define LCDC_CFG_MODE_MASK		(0x0f << LCDC_CFG_MODE_BIT)
#define LCDC_CFG_MODE_GENERIC_TFT	(0 << LCDC_CFG_MODE_BIT)
#define LCDC_CFG_MODE_SPECIAL_TFT_1	(1 << LCDC_CFG_MODE_BIT)
#define LCDC_CFG_MODE_SPECIAL_TFT_2	(2 << LCDC_CFG_MODE_BIT)
#define LCDC_CFG_MODE_SPECIAL_TFT_3	(3 << LCDC_CFG_MODE_BIT)
#define LCDC_CFG_MODE_NONINTER_CCIR656	(4 << LCDC_CFG_MODE_BIT)
#define LCDC_CFG_MODE_INTER_CCIR656	(6 << LCDC_CFG_MODE_BIT)
#define LCDC_CFG_MODE_SERIAL_TFT	(12 << LCDC_CFG_MODE_BIT)
#define LCDC_CFG_MODE_LCM		(13 << LCDC_CFG_MODE_BIT)

/* LCD Control Register */
#define LCDC_CTRL_PINMD			BIT(31)
#define LCDC_CTRL_BST_BIT		28
#define LCDC_CTRL_BST_MASK		(0x7 << LCDC_CTRL_BST_BIT)
#define LCDC_CTRL_BST_4			(0 << LCDC_CTRL_BST_BIT)
#define LCDC_CTRL_BST_8			(1 << LCDC_CTRL_BST_BIT)
#define LCDC_CTRL_BST_16		(2 << LCDC_CTRL_BST_BIT)
#define LCDC_CTRL_BST_32		(3 << LCDC_CTRL_BST_BIT)
#define LCDC_CTRL_BST_64		(4 << LCDC_CTRL_BST_BIT)
#define LCDC_CTRL_RGB565		(0 << 27)
#define LCDC_CTRL_RGB555		BIT(27)
#define LCDC_CTRL_OFUP			BIT(26)
#define LCDC_CTRL_PDD_BIT		16
#define LCDC_CTRL_PDD_MASK		(0xff << LCDC_CTRL_PDD_BIT)

#define LCDC_CTRL_DACTE			BIT(14)
#define LCDC_CTRL_EOFM			BIT(13)
#define LCDC_CTRL_SOFM			BIT(12)
#define LCDC_CTRL_OFUM			BIT(11)
#define LCDC_CTRL_IFUM0			BIT(10)
#define LCDC_CTRL_IFUM1			BIT(9)
#define LCDC_CTRL_LDDM			BIT(8)
#define LCDC_CTRL_QDM			BIT(7)
#define LCDC_CTRL_BEDN			BIT(6)
#define LCDC_CTRL_PEDN			BIT(5)
#define LCDC_CTRL_DIS			BIT(4)
#define LCDC_CTRL_ENA			BIT(3)
#define LCDC_CTRL_BPP_BIT		0
#define LCDC_CTRL_BPP_MASK		(0x07 << LCDC_CTRL_BPP_BIT)
#define LCDC_CTRL_BPP_1			(0 << LCDC_CTRL_BPP_BIT)
#define LCDC_CTRL_BPP_2			(1 << LCDC_CTRL_BPP_BIT)
#define LCDC_CTRL_BPP_4			(2 << LCDC_CTRL_BPP_BIT)
#define LCDC_CTRL_BPP_8			(3 << LCDC_CTRL_BPP_BIT)
#define LCDC_CTRL_BPP_16		(4 << LCDC_CTRL_BPP_BIT)
#define LCDC_CTRL_BPP_18_24		(5 << LCDC_CTRL_BPP_BIT)
#define LCDC_CTRL_BPP_CMPS_24		(6 << LCDC_CTRL_BPP_BIT)
#define LCDC_CTRL_BPP_30		(7 << LCDC_CTRL_BPP_BIT)

#define LCD_TYPE_GENERIC_24_BIT		(0 | 1 << 6)

/* LCD Status Register */
#define LCDC_STATE_QD			BIT(7)
#define LCDC_STATE_EOF			BIT(5)
#define LCDC_STATE_SOF			BIT(4)
#define LCDC_STATE_OFU			BIT(3)
#define LCDC_STATE_IFU0			BIT(2)
#define LCDC_STATE_IFU1			BIT(1)
#define LCDC_STATE_LDD			BIT(0)

/* OSD Configure Register */
#define LCDC_OSDC_PREMULTI1		BIT(23)
#define LCDC_OSDC_COEF_SLE1_BIT		21
#define LCDC_OSDC_COEF_SLE1_MASK	(0x03 << LCDC_OSDC_COEF_SLE1_BIT)
#define LCDC_OSDC_COEF_SLE1_0		(0 << LCDC_OSDC_COEF_SLE1_BIT)
#define LCDC_OSDC_COEF_SLE1_1		(1 << LCDC_OSDC_COEF_SLE1_BIT)
#define LCDC_OSDC_COEF_SLE1_2		(2 << LCDC_OSDC_COEF_SLE1_BIT)
#define LCDC_OSDC_COEF_SLE1_3		(3 << LCDC_OSDC_COEF_SLE1_BIT)

#define LCDC_OSDC_PREMULTI0		BIT(20)
#define LCDC_OSDC_COEF_SLE0_BIT		18
#define LCDC_OSDC_COEF_SLE0_MASK	(0x03 << LCDC_OSDC_COEF_SLE0_BIT)
#define LCDC_OSDC_COEF_SLE0_0		(0 << LCDC_OSDC_COEF_SLE0_BIT)
#define LCDC_OSDC_COEF_SLE0_1		(1 << LCDC_OSDC_COEF_SLE0_BIT)
#define LCDC_OSDC_COEF_SLE0_2		(2 << LCDC_OSDC_COEF_SLE0_BIT)
#define LCDC_OSDC_COEF_SLE0_3		(3 << LCDC_OSDC_COEF_SLE0_BIT)
#define LCDC_OSDC_ALPHAMD1		BIT(17)

#define LCDC_OSDC_SOFM1			BIT(15)
#define LCDC_OSDC_EOFM1			BIT(14)
#define LCDC_OSDC_SOFM0			BIT(11)
#define LCDC_OSDC_EOFM0			BIT(10)
#define LCDC_OSDC_DENDM			BIT(9)
#define LCDC_OSDC_F1EN			BIT(4)
#define LCDC_OSDC_F0EN			BIT(3)
#define LCDC_OSDC_ALPHAEN		BIT(2)
#define LCDC_OSDC_ALPHAMD0		BIT(1)
#define LCDC_OSDC_OSDEN			BIT(0)

/* OSD Controll Register */
#define LCDC_OSDCTRL_IPU_CLKEN		BIT(15)
#define LCDC_OSDCTRL_RGB0_RGB565	(0 << 5)
#define LCDC_OSDCTRL_RGB0_RGB555	BIT(5)
#define LCDC_OSDCTRL_RGB1_RGB565	(0 << 4)
#define LCDC_OSDCTRL_RGB1_RGB555	BIT(4)

#define LCDC_OSDCTRL_BPP_BIT		0
#define LCDC_OSDCTRL_BPP_MASK		(0x7 << LCDC_OSDCTRL_BPP_BIT)
#define LCDC_OSDCTRL_BPP_15_16		(4 << LCDC_OSDCTRL_BPP_BIT)
#define LCDC_OSDCTRL_BPP_18_24		(5 << LCDC_OSDCTRL_BPP_BIT)
#define LCDC_OSDCTRL_BPP_CMPS_24	(6 << LCDC_OSDCTRL_BPP_BIT)
#define LCDC_OSDCTRL_BPP_30		(7 << LCDC_OSDCTRL_BPP_BIT)

/* OSD State Register */
#define LCDC_OSDS_SOF1			BIT(15)
#define LCDC_OSDS_EOF1			BIT(14)
#define LCDC_OSDS_SOF0			BIT(11)
#define LCDC_OSDS_EOF0			BIT(10)
#define LCDC_OSDS_DEND			BIT(8)

/* Background 0 or Background 1 Color Register */
#define LCDC_BGC_RED_OFFSET		16
#define LCDC_BGC_RED_MASK		(0xFF << LCDC_BGC_RED_OFFSET)
#define LCDC_BGC_GREEN_OFFSET		8
#define LCDC_BGC_GREEN_MASK		(0xFF << LCDC_BGC_GREEN_OFFSET)
#define LCDC_BGC_BLUE_OFFSET		0
#define LCDC_BGC_BLUE_MASK		(0xFF << LCDC_BGC_BLUE_OFFSET)

/* Foreground 0 or Foreground 1 Color Key Register */
#define LCDC_KEY_KEYEN			BIT(31)
#define LCDC_KEY_KEYMD			BIT(30)
#define LCDC_KEY_RED_OFFSET		16
#define LCDC_KEY_RED_MASK		(0xFF << LCDC_KEY_RED_OFFSET)
#define LCDC_KEY_GREEN_OFFSET		8
#define LCDC_KEY_GREEN_MASK		(0xFF << LCDC_KEY_GREEN_OFFSET)
#define LCDC_KEY_BLUE_OFFSET		0
#define LCDC_KEY_BLUE_MASK		(0xFF << LCDC_KEY_BLUE_OFFSET)
#define LCDC_KEY_MASK			(LCDC_KEY_RED_MASK | \
					 | LCDC_KEY_GREEN_MASK \
					 | LCDC_KEY_BLUE_MASK)

/* ALPHA Register */
#define LCDC_ALPHA1_OFFSET		8
#define LCDC_ALPHA1_MASK		(0xFF << LCDC_ALPHA1_OFFSET)
#define LCDC_ALPHA0_OFFSET		0
#define LCDC_ALPHA0_MASK		(0xFF << LCDC_ALPHA0_OFFSET)

/* IPU Restart Register */
#define LCDC_IPUR_IPUREN		BIT(31)
#define LCDC_IPUR_IPURMASK		0xFFFFFF

/* RGB Control Register */
#define LCDC_RGBC_RGBDM			BIT(15)
#define LCDC_RGBC_DMM			BIT(14)
#define LCDC_RGBC_422			BIT(8)
#define LCDC_RGBC_RGBFMT		BIT(7)
#define LCDC_RGBC_ODDRGB_BIT		4
#define LCDC_RGBC_ODDRGB_MASK		(0x7 << LCDC_RGBC_ODDRGB_BIT)
#define LCDC_RGBC_ODD_RGB		(0 << LCDC_RGBC_ODDRGB_BIT) /* RGB */
#define LCDC_RGBC_ODD_RBG		(1 << LCDC_RGBC_ODDRGB_BIT) /* RBG */
#define LCDC_RGBC_ODD_GRB		(2 << LCDC_RGBC_ODDRGB_BIT) /* GRB */
#define LCDC_RGBC_ODD_GBR		(3 << LCDC_RGBC_ODDRGB_BIT) /* GBR */
#define LCDC_RGBC_ODD_BRG		(4 << LCDC_RGBC_ODDRGB_BIT) /* BRG */
#define LCDC_RGBC_ODD_BGR		(5 << LCDC_RGBC_ODDRGB_BIT) /* BGR */

#define LCDC_RGBC_EVENRGB_BIT		0
#define LCDC_RGBC_EVENRGB_MASK		(0x7 << LCDC_RGBC_EVENRGB_BIT)
#define LCDC_RGBC_EVEN_RGB		0 /* RGB */
#define LCDC_RGBC_EVEN_RBG		1 /* RBG */
#define LCDC_RGBC_EVEN_GRB		2 /* GRB */
#define LCDC_RGBC_EVEN_GBR		3 /* GBR */
#define LCDC_RGBC_EVEN_BRG		4 /* BRG */
#define LCDC_RGBC_EVEN_BGR		5 /* BGR */

/* Vertical Synchronize Register */
#define LCDC_VSYNC_VPS_BIT		16
#define LCDC_VSYNC_VPS_MASK		(0xfff << LCDC_VSYNC_VPS_BIT)
#define LCDC_VSYNC_VPE_BIT		0
#define LCDC_VSYNC_VPE_MASK		(0xfff << LCDC_VSYNC_VPE_BIT)

/* Horizontal Synchronize Register */
#define LCDC_HSYNC_HPS_BIT		16
#define LCDC_HSYNC_HPS_MASK		(0xfff << LCDC_HSYNC_HPS_BIT)
#define LCDC_HSYNC_HPE_BIT		0
#define LCDC_HSYNC_HPE_MASK		(0xfff << LCDC_HSYNC_HPE_BIT)

/* Virtual Area Setting Register */
#define LCDC_VAT_HT_BIT			16
#define LCDC_VAT_HT_MASK		(0xfff << LCDC_VAT_HT_BIT)
#define LCDC_VAT_VT_BIT			0
#define LCDC_VAT_VT_MASK		(0xfff << LCDC_VAT_VT_BIT)

/* Display Area Horizontal Start/End Point Register */
#define LCDC_DAH_HDS_BIT		16
#define LCDC_DAH_HDS_MASK		(0xfff << LCDC_DAH_HDS_BIT)
#define LCDC_DAH_HDE_BIT		0
#define LCDC_DAH_HDE_MASK		(0xfff << LCDC_DAH_HDE_BIT)

/* Display Area Vertical Start/End Point Register */
#define LCDC_DAV_VDS_BIT		16
#define LCDC_DAV_VDS_MASK		(0xfff << LCDC_DAV_VDS_BIT)
#define LCDC_DAV_VDE_BIT		0
#define LCDC_DAV_VDE_MASK		(0xfff << LCDC_DAV_VDE_BIT)

/* Foreground 0 or Foreground 1 XY Position Register */
#define LCDC_XYP_YPOS_BIT		16
#define LCDC_XYP_YPOS_MASK		(0xfff << LCDC_XYP_YPOS_BIT)
#define LCDC_XYP_XPOS_BIT		0
#define LCDC_XYP_XPOS_MASK		(0xfff << LCDC_XYP_XPOS_BIT)

/* Foreground 0 or Foreground 1 Size Register */
#define LCDC_SIZE_HEIGHT_BIT		16
#define LCDC_SIZE_HEIGHT_MASK		(0xfff << LCDC_SIZE_HEIGHT_BIT)
#define LCDC_SIZE_WIDTH_BIT		0
#define LCDC_SIZE_WIDTH_MASK		(0xfff << LCDC_SIZE_WIDTH_BIT)

/* PS Signal Setting */
#define LCDC_PS_PSS_BIT			16
#define LCDC_PS_PSS_MASK		(0xfff << LCDC_PS_PSS_BIT)
#define LCDC_PS_PSE_BIT			0
#define LCDC_PS_PSE_MASK		(0xfff << LCDC_PS_PSE_BIT)

/* CLS Signal Setting */
#define LCDC_CLS_CLSS_BIT		16
#define LCDC_CLS_CLSS_MASK		(0xfff << LCDC_CLS_CLSS_BIT)
#define LCDC_CLS_CLSE_BIT		0
#define LCDC_CLS_CLSE_MASK		(0xfff << LCDC_CLS_CLSE_BIT)

/* SPL Signal Setting */
#define LCDC_SPL_SPLS_BIT		16
#define LCDC_SPL_SPLS_MASK		(0xfff << LCDC_SPL_SPLS_BIT)
#define LCDC_SPL_SPLE_BIT		0
#define LCDC_SPL_SPLE_MASK		(0xfff << LCDC_SPL_SPLE_BIT)

/* REV Signal Setting */
#define LCDC_REV_REVS_BIT		16
#define LCDC_REV_REVS_MASK		(0xfff << LCDC_REV_REVS_BIT)

/* DMA Command 0 or 1 Register */
#define LCDC_CMD_SOFINT			BIT(31)
#define LCDC_CMD_EOFINT			BIT(30)
#define LCDC_CMD_CMD			BIT(29)
#define LCDC_CMD_PAL			BIT(28)
#define LCDC_CMD_COMPEN			BIT(27)
#define LCDC_CMD_FRM_EN			BIT(26)
#define LCDC_CMD_FIELD_SEL		BIT(25)
#define LCDC_CMD_16X16BLOCK		BIT(24)
#define LCDC_CMD_LEN_BIT		0
#define LCDC_CMD_LEN_MASK		(0xffffff << LCDC_CMD_LEN_BIT)

/* DMA Offsize Register 0,1 */
#define LCDC_OFFS_BIT			0
#define LCDC_OFFS_OFFSIZE_MASK		(0xffffff << LCDC_OFFS_BIT)

/* DMA Page Width Register 0,1 */
#define LCDC_PW_BIT			0
#define LCDC_PW_PAGEWIDTH_MASK		(0xffffff << LCDC_PW_BIT)

/* DMA Command Counter Register 0,1 */
#define LCDC_CNUM_BIT			0
#define LCDC_CNUM_CNUM_MASK		(0xff << LCDC_CNUM_BIT)

/* DMA Command Counter Register */
#define LCDC_CPOS_ALPHAMD1		BIT(31)
#define LCDC_CPOS_RGB_RGB565		(0 << 30)
#define LCDC_CPOS_RGB_RGB555		BIT(30)

#define LCDC_CPOS_BPP_BIT		27
#define LCDC_CPOS_BPP_MASK		(0x07 << LCDC_CPOS_BPP_BIT)
#define LCDC_CPOS_BPP_16		(4 << LCDC_CPOS_BPP_BIT)
#define LCDC_CPOS_BPP_18_24		(5 << LCDC_CPOS_BPP_BIT)
#define LCDC_CPOS_BPP_CMPS_24		(6 << LCDC_CPOS_BPP_BIT)
#define LCDC_CPOS_BPP_30		(7 << LCDC_CPOS_BPP_BIT)

#define LCDC_CPOS_PREMULTI		BIT(26)
#define LCDC_CPOS_COEF_SLE_BIT		24
#define LCDC_CPOS_COEF_SLE_MASK		(0x3 << LCDC_CPOS_COEF_SLE_BIT)
#define LCDC_CPOS_COEF_SLE_0		(0 << LCDC_CPOS_COEF_SLE_BIT)
#define LCDC_CPOS_COEF_SLE_1		(1 << LCDC_CPOS_COEF_SLE_BIT)
#define LCDC_CPOS_COEF_SLE_2		(2 << LCDC_CPOS_COEF_SLE_BIT)
#define LCDC_CPOS_COEF_SLE_3		(3 << LCDC_CPOS_COEF_SLE_BIT)

#define LCDC_CPOS_YPOS_BIT		12
#define LCDC_CPOS_YPOS_MASK		(0xfff << LCDC_CPOS_YPOS_BIT)
#define LCDC_CPOS_XPOS_BIT		0
#define LCDC_CPOS_XPOS_MASK		(0xfff << LCDC_CPOS_XPOS_BIT)

/* Foreground 0,1 Size Register */
#define LCDC_DESSIZE_ALPHA_BIT		24
#define LCDC_DESSIZE_ALPHA_MASK		(0xff << LCDC_DESSIZE_ALPHA_BIT)
#define LCDC_DESSIZE_HEIGHT_BIT		12
#define LCDC_DESSIZE_HEIGHT_MASK	(0xfff << LCDC_DESSIZE_HEIGHT_BIT)
#define LCDC_DESSIZE_WIDTH_BIT		0
#define LCDC_DESSIZE_WIDTH_MASK		(0xfff << LCDC_DESSIZE_WIDTH_BIT)

/* Priority level threshold configure Register */
#define LCDC_PCFG_LCDC_PRI_MD		BIT(31)

#define LCDC_PCFG_HP_BST_BIT		28
#define LCDC_PCFG_HP_BST_MASK		(0x7 << LCDC_PCFG_HP_BST_BIT)
#define LCDC_PCFG_HP_BST_4		(0 << LCDC_PCFG_HP_BST_BIT)
#define LCDC_PCFG_HP_BST_8		(1 << LCDC_PCFG_HP_BST_BIT)
#define LCDC_PCFG_HP_BST_16		(2 << LCDC_PCFG_HP_BST_BIT)
#define LCDC_PCFG_HP_BST_32		(3 << LCDC_PCFG_HP_BST_BIT)
#define LCDC_PCFG_HP_BST_C16		(5 << LCDC_PCFG_HP_BST_BIT)
#define LCDC_PCFG_HP_BST_64		(4 << LCDC_PCFG_HP_BST_BIT)
#define LCDC_PCFG_HP_BST_DIS		(7 << LCDC_PCFG_HP_BST_BIT)

#define LCDC_PCFG_PCFG2_BIT		18
#define LCDC_PCFG_PCFG2_MASK		(0x1ff << LCDC_PCFG_PCFG2_BIT)
#define LCDC_PCFG_PCFG1_BIT		9
#define LCDC_PCFG_PCFG1_MASK		(0x1ff << LCDC_PCFG_PCFG1_BIT)
#define LCDC_PCFG_PCFG0_BIT		0
#define LCDC_PCFG_PCFG0_MASK		(0x1ff << LCDC_PCFG_PCFG0_BIT)

/* Dual LCDC Channel Control register */
/*
 * Select which IPU is able to write back, this field is just
 * available in lcdc1. 0:ipu1; 1:ipu0
 */
#define LCDC_DUAL_CTRL_IPU_WR_SEL	BIT(8)
/*
 * Select which controller output to the tft/slcd panel, this field is just
 * available in lcdc1. 0:lcdc1; 1:lcdc0
 */
#define LCDC_DUAL_CTRL_TFT_SEL		BIT(6)
/*
 * 1: fix the priority of ipu0/1 in lcd internal arbiter;
 * 0: use priority of ipu0/1 generated by lcd in lcd internal arbiter
 */
#define LCDC_DUAL_CTRL_PRI_IPU_EN	BIT(5)
#define LCDC_DUAL_CTRL_PRI_IPU_BIT	3
#define LCDC_DUAL_CTRL_PRI_IPU_MASK	(0x3 << LCDC_DUAL_CTRL_PRI_IPU_BIT)
/*
 * 1: fix the priority of lcd0/1 in lcd internal arbiter;
 * 0: use priority of lcd0/1 generated by lcd in lcd internal arbiter
 */
#define LCDC_DUAL_CTRL_PRI_LCD_EN	BIT(2)
#define LCDC_DUAL_CTRL_PRI_LCD_BIT	0
#define LCDC_DUAL_CTRL_PRI_LCD_MASK	(0x3 << LCDC_DUAL_CTRL_PRI_LCD_BIT)

/* Image Enhancement CFG Register */
#define LCDC_ENH_CFG_DITHER_EN		BIT(9)
#define LCDC_ENH_CFG_YCC2RGB_EN		BIT(8)
#define LCDC_ENH_CFG_SATURATION_EN	BIT(7)
#define LCDC_ENH_CFG_VEE_EN		BIT(6)
#define LCDC_ENH_CFG_HUE_EN		BIT(5)
#define LCDC_ENH_CFG_BRIGHTNESS_EN	BIT(4)
#define LCDC_ENH_CFG_CONTRAST_EN	BIT(3)
#define LCDC_ENH_CFG_RGB2YCC_EN		BIT(2)
#define LCDC_ENH_CFG_GAMMA_EN		BIT(1)
#define LCDC_ENH_CFG_ENH_EN		BIT(0)

/* Color Space Conversion CFG Register */
#define LCDC_ENH_CSCCFG_YCC2RGBMD_BIT	2 /* YCbCr to RGB */
#define LCDC_ENH_CSCCFG_YCC2RGBMD_MASK	(0x03 << LCDC_ENH_CSCCFG_YCC2RGBMD_BIT)
#define LCDC_ENH_CSCCFG_YCC2RGBMD_0	(0 << LCDC_ENH_CSCCFG_YCC2RGBMD_BIT)
#define LCDC_ENH_CSCCFG_YCC2RGBMD_1	(1 << LCDC_ENH_CSCCFG_YCC2RGBMD_BIT)
#define LCDC_ENH_CSCCFG_YCC2RGBMD_2	(2 << LCDC_ENH_CSCCFG_YCC2RGBMD_BIT)
#define LCDC_ENH_CSCCFG_YCC2RGBMD_3	(3 << LCDC_ENH_CSCCFG_YCC2RGBMD_BIT)
/*
 * 00:601WIDE; 01:601NARROW
 * 10:709WIDE; 11:709NARROW
 * WIDE:RGB range 16-235
 * NARROW:RGB range 0-255
*/
#define LCDC_ENH_CSCCFG_RGB2YCCMD_BIT	0 /* RGB to YCbCr*/
#define LCDC_ENH_CSCCFG_RGB2YCCMD_MASK	(0x03 << LCDC_ENH_CSCCFG_RGB2YCCMD_BIT)
#define LCDC_ENH_CSCCFG_RGB2YCCMD_0	(0 << LCDC_ENH_CSCCFG_RGB2YCCMD_BIT)
#define LCDC_ENH_CSCCFG_RGB2YCCMD_1	(1 << LCDC_ENH_CSCCFG_RGB2YCCMD_BIT)
#define LCDC_ENH_CSCCFG_RGB2YCCMD_2	(2 << LCDC_ENH_CSCCFG_RGB2YCCMD_BIT)
#define LCDC_ENH_CSCCFG_RGB2YCCMD_3	(3 << LCDC_ENH_CSCCFG_RGB2YCCMD_BIT)

/* LUMA CFG Register */
#define LCDC_ENH_LUMACFG_BRIGHT_BIT	16	/*
						* Brightness value :0x0-0x7ff
						* means 0.9999~-0.9999
						*/
#define LCDC_ENH_LUMACFG_BRIGHT_MASK	(0x7ff << LCDC_ENH_LUMACFG_BRIGHT_BIT)

#define LCDC_ENH_LUMACFG_CONTRAST_BIT	0	/*
						* Contrast value :0x0-0x7ff
						* means 0~1.9999
						*/
#define LCDC_ENH_LUMACFG_CONTRAST_MASK	(0x7ff << LCDC_ENH_LUMACFG_CONTRAST_BIT)

/* CHROMA0 CFG Register */
#define LCDC_ENH_CHROCFG0_HUE_SIN_BIT	16
#define LCDC_ENH_CHROCFG0_HUE_SIN_MASK	(0xfff << LCDC_ENH_CHROCFG0_HUE_SIN_BIT)
#define LCDC_ENH_CHROCFG0_HUE_COS_BIT	0
#define LCDC_ENH_CHROCFG0_HUE_COS_MASK	(0xfff << LCDC_ENH_CHROCFG0_HUE_COS_BIT)

/* CHROMA1 CFG Register */
#define LCDC_ENH_CHROCFG1_SATUR_BIT	0
#define LCDC_ENH_CHROCFG1_SATUR_MASK	(0x7ff << LCDC_ENH_CHROCFG1_SATUR_BIT)

/* DITHER CFG Register */
/*
 * 00:8bit dither
 * 01:6bit dither
 * 10:5bit dither
 * 11:4bit dither
*/
#define LCDC_ENH_DITHERCFG_RED_BIT	4
#define LCDC_ENH_DITHERCFG_RED_MASK	(0x03 << LCDC_ENH_DITHERCFG_RED_BIT)
#define LCDC_ENH_DITHERCFG_GREEN_BIT	2
#define LCDC_ENH_DITHERCFG_GREEN_MASK	(0x03 << LCDC_ENH_DITHERCFG_GREEN_BIT)
#define LCDC_ENH_DITHERCFG_BLUE_BIT	0
#define LCDC_ENH_DITHERCFG_BLUE_MASK	(0x03 << LCDC_ENH_DITHERCFG_BLUE_BIT)

/* Enhance Status Register */
#define LCDC_ENH_STATUS_DITHER_DIS	BIT(9)
#define LCDC_ENH_STATUS_YCC2RGB_DIS	BIT(8)
#define LCDC_ENH_STATUS_SATURATION_DIS	BIT(7)
#define LCDC_ENH_STATUS_VEE_DIS		BIT(6)
#define LCDC_ENH_STATUS_HUE_DIS		BIT(5)
#define LCDC_ENH_STATUS_BRIGHTNESS_DIS	BIT(4)
#define LCDC_ENH_STATUS_CONTRAST_DIS	BIT(3)
#define LCDC_ENH_STATUS_RGB2YCC_DIS	BIT(2)
#define LCDC_ENH_STATUS_GAMMA_DIS	BIT(1)

/* GAMMA CFG Register */
#define LCDC_ENH_GAMMA_DATA1_BIT	16
#define LCDC_ENH_GAMMA_DATA1_MASK	(0x3ff << LCDC_ENH_GAMMA_DATA1_BIT)
#define LCDC_ENH_GAMMA_DATA0_BIT	0
#define LCDC_ENH_GAMMA_DATA0_MASK	(0x3ff << LCDC_ENH_GAMMA_DATA0_BIT)
#define LCDC_ENH_GAMMA_LEN		0x800

/* VEE CFG Register */
#define LCDC_ENH_VEE_VEE_DATA1_BIT	16
#define LCDC_ENH_VEE_VEE_DATA1_MASK	(0x3ff << LCDC_ENH_VEE_VEE_DATA1_BIT)
#define LCDC_ENH_VEE_VEE_DATA0_BIT	0
#define LCDC_ENH_VEE_VEE_DATA0_MASK	(0x3ff << LCDC_ENH_VEE_VEE_DATA0_BIT)
#define LCDC_ENH_VEE_LEN		0x800

/* Register Map Of SLCD (Smart LCD Controller) */
#define SLCDC_CFG			0xA0
#define SLCDC_CTRL			0xA4
#define SLCDC_STATE			0xA8
#define SLCDC_DATA			0xAc

/* SLCD Configure Register */
#define SLCDC_CFG_DWIDTH_BIT		10
#define SLCDC_CFG_DWIDTH_MASK		(0x7 << SLCDC_CFG_DWIDTH_BIT)
#define SLCDC_CFG_DWIDTH_18BIT		(0 << SLCDC_CFG_DWIDTH_BIT)
#define SLCDC_CFG_DWIDTH_16BIT		(1 << SLCDC_CFG_DWIDTH_BIT)
#define SLCDC_CFG_DWIDTH_8BIT_x3	(2 << SLCDC_CFG_DWIDTH_BIT)
#define SLCDC_CFG_DWIDTH_8BIT_x2	(3 << SLCDC_CFG_DWIDTH_BIT)
#define SLCDC_CFG_DWIDTH_8BIT_x1	(4 << SLCDC_CFG_DWIDTH_BIT)
#define SLCDC_CFG_DWIDTH_24BIT		(5 << SLCDC_CFG_DWIDTH_BIT)
#define SLCDC_CFG_DWIDTH_9BIT_x2	(7 << SLCDC_CFG_DWIDTH_BIT)
#define SLCDC_CFG_CWIDTH_BIT		8
#define SLCDC_CFG_CWIDTH_MASK		(0x3 << SLCDC_CFG_CWIDTH_BIT)
#define SLCDC_CFG_CWIDTH_16BIT		(0 << SLCDC_CFG_CWIDTH_BIT)
#define SLCDC_CFG_CWIDTH_8BIT		(1 << SLCDC_CFG_CWIDTH_BIT)
#define SLCDC_CFG_CWIDTH_18BIT		(2 << SLCDC_CFG_CWIDTH_BIT)
#define SLCDC_CFG_CWIDTH_24BIT		(3 << SLCDC_CFG_CWIDTH_BIT)
#define SLCDC_CFG_CS_ACTIVE_LOW		(0 << 4)
#define SLCDC_CFG_CS_ACTIVE_HIGH	BIT(4)
#define SLCDC_CFG_RS_CMD_LOW		(0 << 3)
#define SLCDC_CFG_RS_CMD_HIGH		BIT(3)
#define SLCDC_CFG_CLK_ACTIVE_FALLING	(0 << 1)
#define SLCDC_CFG_CLK_ACTIVE_RISING	BIT(1)
#define SLCDC_CFG_TYPE_PARALLEL		(0 << 0)
#define SLCDC_CFG_TYPE_SERIAL		BIT(0)

/* SLCD Control Register */
#define SLCDC_CTRL_DMA_MODE		BIT(2)
#define SLCDC_CTRL_DMA_START		BIT(1)
#define SLCDC_CTRL_DMA_EN		BIT(0)

/* SLCD Status Register */
#define SLCDC_STATE_BUSY		BIT(0)

/* SLCD Data Register */
#define SLCDC_DATA_RS_DATA		(0 << 31)
#define SLCDC_DATA_RS_COMMAND		BIT(31)

/* Register Map Of LVDSC (LVDS Controller) */
#define LVDS_TXCTRL			0x3c0
#define LVDS_TXPLL0			0x3c4
#define LVDS_TXPLL1			0x3c8
#define LVDS_TXECTRL			0x3cc

/* TXCTRL (LVDS Transmitter Control Register) */
#define LVDS_MODEL_SEL			BIT(31)
#define LVDS_TX_PDB			BIT(30)
#define LVDS_TX_PDB_CK			BIT(29)
#define LVDS_RESERVE(n)			(1 << 20 + (n))
#define LVDS_TX_RSTB			BIT(18)
#define LVDS_TX_CKBIT_PHA_SEL		BIT(17)
#define LVDS_TX_CKBYTE_PHA_SEL		BIT(16)

#define LVDS_TX_CKOUT_PHA_S_BIT		13
#define LVDS_TX_CKOUT_PHA_S_MASK	(0x07 << LVDS_TX_CKOUT_PHA_S_BIT)

#define LVDS_TX_CKOUT_SET		BIT(12)
#define LVDS_TX_OUT_SEL			BIT(11)
#define LVDS_TX_DLY_SEL_BIT		8
#define LVDS_TX_DLY_SEL_MASK		(0x07 << LVDS_TX_DLY_SEL_BIT)
#define LVDS_TX_AMP_ADJ			BIT(7)
#define LVDS_TX_LVDS			BIT(6)
#define LVDS_TX_CR_BIT			3
#define LVDS_TX_CR_MASK			(0x07 << LVDS_TX_CR_BIT)
#define LVDS_TX_CR_CK			BIT(2)
#define LVDS_TX_OD_S			BIT(1)
#define LVDS_TX_OD_EN			BIT(0)

/* TXPLL0 (LVDS Transmitter's PLL Control Register 0 */

#define LVDS_PLL_LOCK			BIT(31)
#define LVDS_PLL_EN			BIT(30)
#define LVDS_BG_PWD			BIT(29)
#define LVDS_PLL_SSC_EN			BIT(27)
#define LVDS_PLL_SSC_MODE		BIT(26)
#define LVDS_PLL_TEST			BIT(25)
#define LVDS_PLL_POST_DIVA_BIT		21
#define LVDS_PLL_POST_DIVA_MASK		(0x03 << LVDS_PLL_POST_DIVA_BIT)
#define LVDS_PLL_POST_DIVB_BIT		16
#define LVDS_PLL_POST_DIVB_MASK		(0x1f << LVDS_PLL_POST_DIVB_BIT)
#define LVDS_PLL_PLLN_BIT		8
#define LVDS_PLL_PLLN_MASK		(0x7f << LVDS_PLL_PLLN_BIT)
#define LVDS_PLL_TEST_DIV_BIT		6
#define LVDS_PLL_TEST_DIV_MASK		(0x03 << LVDS_PLL_TEST_DIV_BIT)
#define LVDS_PLL_TEST_DIV_2		(0 << LVDS_PLL_TEST_DIV_BIT)
#define LVDS_PLL_TEST_DIV_4		(1 << LVDS_PLL_TEST_DIV_BIT)
#define LVDS_PLL_TEST_DIV_8		(2 << LVDS_PLL_TEST_DIV_BIT)
#define LVDS_PLL_TEST_DIV_16		(3 << LVDS_PLL_TEST_DIV_BIT)
#define LVDS_PLL_IN_BYPASS		(1 << 5)
#define LVDS_PLL_INDIV_BIT		0
#define LVDS_PLL_INDIV_MASK		(0x1f << LVDS_PLL_INDIV_BIT)

/* TXPLL1 (LVDS Transmitter's PLL Control Register 1 */

#define LVDS_PLL_ICP_SEL_BIT		29
#define LVDS_PLL_ICP_SEL_MASK		(0x07 << LVDS_PLL_ICP_SEL_BIT)
#define LVDS_PLL_KVCO_BIT		26
#define LVDS_PLL_KVCO_MASK		(0x03 << LVDS_PLL_KVCO_BIT)
#define LVDS_PLL_IVCO_SEL_BIT		24
#define LVDS_PLL_IVCO_SEL_MASK		(0x03 << LVDS_PLL_IVCO_SEL_BIT)
#define LVDS_PLL_SSCN_BIT		17
#define LVDS_PLL_SSCN_MASK		(0x7f << LVDS_PLL_SSCN_BIT)
#define LVDS_PLL_COUNT_BIT		4
#define LVDS_PLL_COUNT_MASK		(0x1fff << LVDS_PLL_COUNT_BIT)
#define LVDS_PLL_GAIN_BIT		0
#define LVDS_PLL_GAIN_MASK		(0x0f << LVDS_PLL_GAIN_BIT)

/* TXECTRL (LVDS Transmitter's Enhance Control */

#define LVDS_TX_EM_S_BIT		9
#define LVDS_TX_EM_S_MASK		(0x03 <<  LVDS_TX_EM_S_BIT)
#define LVDS_TX_EM_EN			BIT(8)
#define LVDS_TX_LDO_VO_S_BIT		5
#define LVDS_TX_LDO_VO_S_MASK		(0x03 << LVDS_TX_LDO_VO_S_BIT)
#define LVDS_TX_LDO_VO_S_0		(0x00 << LVDS_TX_LDO_VO_S_BIT)
#define LVDS_TX_LDO_VO_S_1		(0x01 << LVDS_TX_LDO_VO_S_BIT)
#define LVDS_TX_LDO_VO_S_2		(0x02 << LVDS_TX_LDO_VO_S_BIT)
#define LVDS_TX_LDO_VO_S_3		(0x03 << LVDS_TX_LDO_VO_S_BIT)
#define LVDS_PLL_PL_BP			BIT(4)

/*
 * Internal 7x clock phase fine tuning for data
 * setup/hold time optimization
 */
#define LVDS_TX_CK_PHA_FINE_BIT		2
#define LVDS_TX_CK_PHA_FINE_MASK	(0x03 << LVDS_TX_CK_PHA_FINE_BIT)
/*
 * Internal 7x clock phase coarse tuning for data
 * setup/hold time optimization
 */
#define LVDS_TX_CK_PHA_COAR_BIT		0
#define LVDS_TX_CK_PHA_COAR_MASK	(0x03 << LVDS_TX_CK_PHA_COAR_BIT)

/*
 * Helpers:
 */

static inline unsigned long jz4780_read(struct drm_device *dev, u32 reg)
{
	struct jz4780_drm_private *priv = dev->dev_private;
	return ioread32(priv->mmio + reg);
}

static inline void jz4780_write(struct drm_device *dev, u32 reg, u32 data)
{
	struct jz4780_drm_private *priv = dev->dev_private;
	iowrite32(data, priv->mmio + reg);
}
#endif /* __JZ4780_REGS_H__ */
