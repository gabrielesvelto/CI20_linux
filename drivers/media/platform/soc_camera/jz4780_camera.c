/*
 * V4L2 Driver for Ingenic JZ4780 camera (CIM) host
 *
 * Copyright (C) 2012, Ingenic Semiconductor Inc.
 * Copyright (C) 2015, Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <media/v4l2-ioctl.h>

#include <media/soc_camera.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-dma-contig.h>
#include <media/videobuf-dma-sg.h>
#include <media/soc_mediabus.h>
#include <media/v4l2-of.h>

#include <asm/dma.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

/* define the maximum number of camera sensor that attach to cim controller */
#define MAX_SOC_CAM_NUM			2

/*
 * CIM registers
 */
#define CIM_CFG				(0x00)
#define CIM_CTRL			(0x04)
#define CIM_STATE			(0x08)
#define CIM_IID				(0x0c)
#define CIM_DA				(0x20)
#define CIM_FA				(0x24)
#define CIM_FID				(0x28)
#define CIM_CMD				(0x2c)
#define CIM_SIZE			(0x30)
#define CIM_OFFSET			(0x34)
#define CIM_YFA				(0x38)
#define CIM_YCMD			(0x3c)
#define CIM_CBFA			(0x40)
#define CIM_CBCMD			(0x44)
#define CIM_CRFA			(0x48)
#define CIM_CRCMD			(0x4c)
#define CIM_CTRL2			(0x50)
#define CIM_FS				(0x54)
#define CIM_IMR				(0x58)
#define CIM_TC				(0x5c)
#define CIM_TINX			(0x60)
#define CIM_TCNT			(0x64)

/*CIM Configuration Register (CIMCFG)*/
#define CIM_CFG_SEP			(1<<20)
#define CIM_CFG_ORDER			18

#define CIM_CFG_ORDER_YUYV		(0 << CIM_CFG_ORDER)
#define CIM_CFG_ORDER_YVYU		(1 << CIM_CFG_ORDER)
#define CIM_CFG_ORDER_UYVY		(2 << CIM_CFG_ORDER)
#define CIM_CFG_ORDER_VYUY		(3 << CIM_CFG_ORDER)

#define CIM_CFG_DF_BIT			16
#define CIM_CFG_DF_MASK			(0x3 << CIM_CFG_DF_BIT)
/* YCbCr444 */
#define CIM_CFG_DF_YUV444		(0x1 << CIM_CFG_DF_BIT)
/* YCbCr422 */
#define CIM_CFG_DF_YUV422		(0x2 << CIM_CFG_DF_BIT)
/* ITU656 YCbCr422 */
#define CIM_CFG_DF_ITU656		(0x3 << CIM_CFG_DF_BIT)
/* VSYNC Polarity:0-rising edge active,1-falling edge active */
#define CIM_CFG_VSP			(1 << 14)
/* HSYNC Polarity:0-rising edge active,1-falling edge active */
#define CIM_CFG_HSP			(1 << 13)
/* PCLK working edge: 0-rising, 1-falling */
#define CIM_CFG_PCP			(1 << 12)
#define CIM_CFG_DMA_BURST_TYPE_BIT	10
#define CIM_CFG_DMA_BURST_TYPE_MASK	(0x3 << CIM_CFG_DMA_BURST_TYPE_BIT)
#define CIM_CFG_DMA_BURST_INCR4		(0 << CIM_CFG_DMA_BURST_TYPE_BIT)
#define CIM_CFG_DMA_BURST_INCR8		(1 << CIM_CFG_DMA_BURST_TYPE_BIT)
#define CIM_CFG_DMA_BURST_INCR16	(2 << CIM_CFG_DMA_BURST_TYPE_BIT)
#define CIM_CFG_DMA_BURST_INCR32	(3 << CIM_CFG_DMA_BURST_TYPE_BIT)
#define CIM_CFG_PACK_BIT		4
#define CIM_CFG_PACK_MASK		(0x7 << CIM_CFG_PACK_BIT)
#define CIM_CFG_PACK_VY1UY0		(0 << CIM_CFG_PACK_BIT)
#define CIM_CFG_PACK_Y0VY1U		(1 << CIM_CFG_PACK_BIT)
#define CIM_CFG_PACK_UY0VY1		(2 << CIM_CFG_PACK_BIT)
#define CIM_CFG_PACK_Y1UY0V		(3 << CIM_CFG_PACK_BIT)
#define CIM_CFG_PACK_Y0UY1V		(4 << CIM_CFG_PACK_BIT)
#define CIM_CFG_PACK_UY1VY0		(5 << CIM_CFG_PACK_BIT)
#define CIM_CFG_PACK_Y1VY0U		(6 << CIM_CFG_PACK_BIT)
#define CIM_CFG_PACK_VY0UY1		(7 << CIM_CFG_PACK_BIT)
#define CIM_CFG_DSM_BIT			0
#define CIM_CFG_DSM_MASK		(0x3 << CIM_CFG_DSM_BIT)
/* CCIR656 Progressive Mode */
#define CIM_CFG_DSM_CPM			(0 << CIM_CFG_DSM_BIT)
/* CCIR656 Interlace Mode */
#define CIM_CFG_DSM_CIM			(1 << CIM_CFG_DSM_BIT)
/* Gated Clock Mode */
#define CIM_CFG_DSM_GCM			(2 << CIM_CFG_DSM_BIT)

/* CIM State Register  (CIM_STATE) */
#define CIM_STATE_RXRF_OF		(1 << 21) /* Cr RXFIFO overflow */
#define CIM_STATE_RXBF_OF		(1 << 19) /* Cb RXFIFO overflow */
#define CIM_STATE_RXYF_OF		(1 << 17) /* Cb RXFIFO overflow */

#define CIM_STATE_DMA_EEOF		(1 << 11) /* DMA Line EEOf irq */
#define CIM_STATE_DMA_STOP		(1 << 10) /* DMA stop irq */
#define CIM_STATE_DMA_SOF		(1 << 8) /* DMA start irq */
#define CIM_STATE_DMA_EOF		(1 << 9) /* DMA end irq */
#define CIM_STATE_TLB_ERR		(1 << 4) /* TLB error */
#define CIM_STATE_SIZE_ERR		(1 << 3) /* Frame size check error */
#define CIM_STATE_RXF_OF		(1 << 2) /* RXFIFO over flow irq */
#define CIM_STATE_RXF_EMPTY		(1 << 1) /* RXFIFO empty irq */
#define CIM_STATE_VDD			(1 << 0) /* CIM disabled irq */

/* CIM_CMD_OFRCV enables all of the following FIFO overflow irq's */
#define CIM_STATE_FIFO_OVERFLOW ( \
	CIM_STATE_RXRF_OF | CIM_STATE_RXBF_OF | \
	CIM_STATE_RXYF_OF | CIM_STATE_RXF_OF)

/* CIM DMA Command Register (CIM_CMD) */
#define CIM_CMD_SOFINT			(1 << 31) /* enable DMA start irq */
#define CIM_CMD_EOFINT			(1 << 30) /* enable DMA end irq */
#define CIM_CMD_EEOFINT			(1 << 29) /* enable DMA EEOF irq */
#define CIM_CMD_STOP			(1 << 28) /* enable DMA stop irq */
#define CIM_CMD_OFRCV			(1 << 27) /* enable FIFO overlow irq */

/*CIM Control Register (CIMCR)*/
#define CIM_CTRL_FRC_BIT		16
#define CIM_CTRL_FRC_MASK		(0xf << CIM_CTRL_FRC_BIT)
#define CIM_CTRL_FRC_1			(0x0 << CIM_CTRL_FRC_BIT)
#define CIM_CTRL_FRC_10			(10 << CIM_CTRL_FRC_BIT)

/* when change DA, do frame sync */
#define CIM_CTRL_DMA_SYNC		(1 << 7)

#define CIM_CTRL_CIM_RST		(1 << 3)
#define CIM_CTRL_DMA_EN			(1 << 2) /* Enable DMA */
#define CIM_CTRL_RXF_RST		(1 << 1) /* RxFIFO reset */
#define CIM_CTRL_ENA			(1 << 0) /* Enable CIM */

/* cim control2 */
/* horizontal size remainder ignore */
#define CIM_CTRL2_FRAGHE		(1 << 25)
/* vertical size remainder ignore */
#define CIM_CTRL2_FRAGVE		(1 << 24)
/* enable frame size check */
#define CIM_CTRL2_FSC			(1 << 23)
/* enable auto-recovery for incomplete frame */
#define CIM_CTRL2_ARIF			(1 << 22)

#define CIM_CTRL2_CSC_BIT		16 /* CSC Mode Select */
#define CIM_CTRL2_CSC_MASK		(0x3 << CIM_CTRL2_CSC_BIT)
/* Bypass mode */
#define CIM_CTRL2_CSC_BYPASS		(0x0 << CIM_CTRL2_CSC_BIT)
/* CSC to YCbCr422 */
#define CIM_CTRL2_CSC_YUV422		(0x2 << CIM_CTRL2_CSC_BIT)
/* CSC to YCbCr420 */
#define CIM_CTRL2_CSC_YUV420		(0x3 << CIM_CTRL2_CSC_BIT)

#define CIM_CTRL2_OPG_BIT		4 /* option priority configuration */
#define CIM_CTRL2_OPG_MASK		(0x3 << CIM_CTRL2_OPG_BIT)
/* optional priority mode enable */
#define CIM_CTRL2_OPE			(1 << 2)
/* emergency priority enable */
#define CIM_CTRL2_EME			(1 << 1)
/* auto priority mode enable*/
#define CIM_CTRL2_APM			(1 << 0)


/* CIM Interrupt Mask Register (CIMIMR) */
#define CIM_IMR_EOFM			(1 << 9)
#define CIM_IMR_SOFM			(1 << 8)
#define CIM_IMR_TLBEM			(1 << 4)
#define CIM_IMR_FSEM			(1 << 3)
#define CIM_IMR_RFIFO_OFM		(1 << 2)

/* CIM Frame Size Register (CIM_FS) */
#define CIM_FS_FVS_BIT			16 /* vertical size of the frame */
#define CIM_FS_FVS_MASK			(0x1fff << CIM_FS_FVS_BIT)
#define CIM_FS_BPP_BIT			14 /* bytes per pixel */
#define CIM_FS_BPP_MASK			(0x3 << CIM_FS_BPP_BIT)
#define CIM_FS_FHS_BIT			0 /* horizontal size of the frame */
#define CIM_FS_FHS_MASK			(0x1fff << CIM_FS_FHS_BIT)

/* CIM TLB Control Register (CIMTC) */
#define CIM_TC_RBA			(1 << 2)
#define CIM_TC_RST			(1 << 1)
#define CIM_TC_ENA			(1 << 0)

#define SOCAM_BUS_FLAGS	(V4L2_MBUS_MASTER | \
	V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_VSYNC_ACTIVE_LOW | \
	V4L2_MBUS_HSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_LOW | \
	V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_PCLK_SAMPLE_FALLING | \
	V4L2_MBUS_DATA_ACTIVE_HIGH)

#define VERSION_CODE KERNEL_VERSION(0, 0, 1)
#define DRIVER_NAME "jz4780-cim"
#define MAX_VIDEO_MEM			16 /* Video memory limit in megabytes */

/*
 * Structures
 */

struct jz4780_camera_dma_desc {
	dma_addr_t next;
	unsigned int id;
	unsigned int buf;
	unsigned int cmd;
	/* only used when SEP = 1 */
	unsigned int cb_frame;
	unsigned int cb_len;
	unsigned int cr_frame;
	unsigned int cr_len;
} __aligned(32);

/* buffer for one video frame */
struct jz4780_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;
	enum v4l2_mbus_pixelcode code;
	int	inwork;
};

struct jz4780_camera_dev {
	struct soc_camera_host soc_host;
	struct soc_camera_device *icd[2];
	struct jz4780_buffer *active;
	struct resource	*res;
	struct clk *clk;
	struct clk *mclk;
	struct list_head capture;

	void __iomem *base;
	unsigned int irq;

	u32 mclk_freq;
	unsigned long platform_flags;

	spinlock_t lock;

	struct jz4780_camera_dma_desc *dma_desc;
	void __iomem *dma_desc_paddr;
};

static void cim_dump_reg(struct jz4780_camera_dev *pcdev)
{
	struct device *dev = pcdev->soc_host.v4l2_dev.dev;
#define FMT "\t=\t0x%08x\n"
	dev_vdbg(dev, "REG_CIM_CFG" FMT, readl(pcdev->base + CIM_CFG));
	dev_vdbg(dev, "REG_CIM_CTRL" FMT, readl(pcdev->base + CIM_CTRL));
	dev_vdbg(dev, "REG_CIM_CTRL2" FMT, readl(pcdev->base + CIM_CTRL2));
	dev_vdbg(dev, "REG_CIM_STATE" FMT, readl(pcdev->base + CIM_STATE));

	dev_vdbg(dev, "REG_CIM_IMR" FMT, readl(pcdev->base + CIM_IMR));
	dev_vdbg(dev, "REG_CIM_IID" FMT, readl(pcdev->base + CIM_IID));
	dev_vdbg(dev, "REG_CIM_DA" FMT, readl(pcdev->base + CIM_DA));
	dev_vdbg(dev, "REG_CIM_FA" FMT, readl(pcdev->base + CIM_FA));

	dev_vdbg(dev, "REG_CIM_FID" FMT, readl(pcdev->base + CIM_FID));
	dev_vdbg(dev, "REG_CIM_CMD" FMT, readl(pcdev->base + CIM_CMD));
	dev_vdbg(dev, "REG_CIM_WSIZE" FMT, readl(pcdev->base + CIM_SIZE));
	dev_vdbg(dev, "REG_CIM_WOFFSET" FMT, readl(pcdev->base + CIM_OFFSET));

	dev_vdbg(dev, "REG_CIM_FS" FMT, readl(pcdev->base + CIM_FS));
	dev_vdbg(dev, "REG_CIM_YFA" FMT, readl(pcdev->base + CIM_YFA));
	dev_vdbg(dev, "REG_CIM_YCMD" FMT, readl(pcdev->base + CIM_YCMD));
	dev_vdbg(dev, "REG_CIM_CBFA" FMT, readl(pcdev->base + CIM_CBFA));

	dev_vdbg(dev, "REG_CIM_CBCMD" FMT, readl(pcdev->base + CIM_CBCMD));
	dev_vdbg(dev, "REG_CIM_CRFA" FMT, readl(pcdev->base + CIM_CRFA));
	dev_vdbg(dev, "REG_CIM_CRCMD" FMT, readl(pcdev->base + CIM_CRCMD));
	dev_vdbg(dev, "REG_CIM_TC" FMT, readl(pcdev->base + CIM_TC));

	dev_vdbg(dev, "REG_CIM_TINX" FMT, readl(pcdev->base + CIM_TINX));
	dev_vdbg(dev, "REG_CIM_TCNT" FMT, readl(pcdev->base + CIM_TCNT));
}

/*
 *  Videobuf operations
 */
static int jz4780_videobuf_setup(struct videobuf_queue *vq, unsigned int *count,
			      unsigned int *size)
{
	struct soc_camera_device *icd = vq->priv_data;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);

	dev_dbg(icd->parent, "%s count=%d, size=%d\n", __func__, *count, *size);

	if (bytes_per_line < 0)
		return bytes_per_line;

	*size = bytes_per_line * icd->user_height;

	if (!*count)
		*count = 32;

	if (*size * *count > MAX_VIDEO_MEM * 1024 * 1024)
		*count = (MAX_VIDEO_MEM * 1024 * 1024) / *size;

	dev_dbg(icd->parent, "%s count=%d, size=%d\n", __func__, *count, *size);
	return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct jz4780_buffer *buf)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct jz4780_camera_dev *pcdev = ici->priv;
	struct videobuf_buffer *vb = &buf->vb;

	BUG_ON(in_interrupt());

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		&buf->vb, buf->vb.baddr, buf->vb.bsize);

	if (buf == pcdev->active) {
		dev_dbg(icd->parent, "freeing active buffer\n");
		/* Wait for DMA to this buffer to complete */
		wait_event(buf->vb.done, buf->vb.state == VIDEOBUF_DONE);
		pcdev->active = NULL;
	}

#ifdef DEBUG
	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	{
		int i;

		for (i = 0; i < 64; i += 4)
			dev_dbg(icd->parent, "%s 0x%08x\n", __func__,
				((u32 *)(vb->baddr))[i]);
	}
#endif
	/*
	 * This waits until this buffer is out of danger, i.e., until it is no
	 * longer in STATE_QUEUED or STATE_ACTIVE
	 */
	videobuf_dma_contig_free(vq, vb);

	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int jz4780_videobuf_prepare(struct videobuf_queue *vq,
		struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct soc_camera_device *icd = vq->priv_data;
	struct jz4780_buffer *buf = container_of(vb, struct jz4780_buffer, vb);
	int ret;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	if (bytes_per_line < 0)
		return bytes_per_line;

	/* Added list head initialization on alloc */
	WARN_ON(!list_empty(&vb->queue));

#ifdef DEBUG
	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	memset((void *)vb->baddr, 0xaa, vb->bsize);
#endif
	BUG_ON(NULL == icd->current_fmt);

	/*
	 * I think, in buf_prepare you only have to protect global data,
	 * the actual buffer is yours
	 */
	buf->inwork = 1;

	if (buf->code	!= icd->current_fmt->code ||
	    vb->width	!= icd->user_width ||
	    vb->height	!= icd->user_height ||
	    vb->field	!= field) {
		buf->code	= icd->current_fmt->code;
		vb->width	= icd->user_width;
		vb->height	= icd->user_height;
		vb->field	= field;
		vb->state	= VIDEOBUF_NEEDS_INIT;
	}

	vb->size = bytes_per_line * vb->height;
	if (0 != vb->baddr && vb->bsize < vb->size) {
		ret = -EINVAL;
		goto out;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret) {
			dev_err(icd->parent, "%s error!\n", __func__);
			goto fail;
		}
		vb->state = VIDEOBUF_PREPARED;
	}
	buf->inwork = 0;
	return 0;

fail:
	free_buffer(vq, buf);
out:
	buf->inwork = 0;
	return ret;
}

static int jz4780_camera_setup_dma(struct jz4780_camera_dev *pcdev,
		unsigned char dev_num)
{
	struct device *dev = pcdev->soc_host.v4l2_dev.dev;
	struct videobuf_buffer *vbuf = &pcdev->active->vb;
	struct soc_camera_device *icd = pcdev->icd[dev_num];
	struct jz4780_camera_dma_desc *dma_desc;
	dma_addr_t dma_address;
	unsigned int regval;

	dev_dbg(dev, "%s\n", __func__);

	/* disable dma and cim */
	regval = readl(pcdev->base + CIM_CTRL);
	regval &= ~(CIM_CTRL_ENA | CIM_CTRL_DMA_EN);
	writel(regval, pcdev->base + CIM_CTRL);

	writel(0, pcdev->base + CIM_STATE);

	dma_desc = pcdev->dma_desc;

	dma_address = videobuf_to_dma_contig(vbuf);
	if (!dma_address) {
		dev_err(dev, "Failed to setup DMA address\n");
		return -ENOMEM;
	}

	/* Store physical address to CIM_DA */
	regval = (unsigned int) (pcdev->dma_desc_paddr);
	writel(regval, pcdev->base + CIM_DA);

	dma_desc->id = 0;
	dma_desc->buf = dma_address;

	/* Next descriptor points to itself */
	dma_desc->next = (dma_addr_t) (pcdev->dma_desc);

	if (icd->current_fmt->host_fmt->fourcc == V4L2_PIX_FMT_YUYV) {
		dma_desc->cmd = icd->sizeimage >> 2 | CIM_CMD_EOFINT |
				CIM_CMD_OFRCV;
	} else {
		dma_desc->cmd = (icd->sizeimage * 8 / 12) >> 2 |
				CIM_CMD_EOFINT | CIM_CMD_OFRCV;

		dma_desc->cb_len = (icd->user_width >> 1) *
				(icd->user_height >> 1) >> 2;

		dma_desc->cr_len = (icd->user_width >> 1) *
				(icd->user_height >> 1) >> 2;
	}

	if (icd->current_fmt->host_fmt->fourcc == V4L2_PIX_FMT_YUV420) {
		dma_desc->cb_frame = dma_desc->buf + icd->sizeimage * 8 / 12;
		dma_desc->cr_frame = dma_desc->cb_frame + (icd->sizeimage / 6);

	}

	dev_dbg(dev, "%s desc address : 0x%p\n",   __func__, dma_desc);
	dev_dbg(dev, "%s frame address: 0x%08x\n", __func__, dma_desc->buf);
	dev_dbg(dev, "%s frame cmd    : 0x%08x\n", __func__, dma_desc->cmd);

	/* enable end of frame/rx overflow / frame size interrupt */
	regval = readl(pcdev->base + CIM_IMR);
	regval &= ~(CIM_IMR_EOFM | CIM_IMR_RFIFO_OFM | CIM_IMR_FSEM);
	writel(regval, pcdev->base + CIM_IMR);

	/* disable tlb error interrupt */
	regval = readl(pcdev->base + CIM_IMR);
	regval |= CIM_IMR_TLBEM;
	writel(regval, pcdev->base + CIM_IMR);

	/* disable tlb */
	regval = readl(pcdev->base + CIM_TC);
	regval &= ~CIM_TC_ENA;
	writel(regval, pcdev->base + CIM_TC);

	dev_vdbg(dev, "%s Registers before enable\n", __func__);
	cim_dump_reg(pcdev);

	/* enable dma and cim */
	regval = readl(pcdev->base + CIM_CTRL);
	regval |= CIM_CTRL_DMA_EN | CIM_CTRL_ENA;
	writel(regval, pcdev->base + CIM_CTRL);

	dev_vdbg(dev, "%s Registers after enable\n", __func__);
	cim_dump_reg(pcdev);
	return 0;
}

/* Called under spinlock_irqsave(&pcdev->lock, ...) */
static void jz4780_videobuf_queue(struct videobuf_queue *vq,
						struct videobuf_buffer *vb)
{

	struct soc_camera_device *icd = vq->priv_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct jz4780_camera_dev *pcdev = ici->priv;
	struct jz4780_buffer *buf = container_of(vb, struct jz4780_buffer, vb);

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%08lx %d active=%p\n",
		__func__, vb, vb->baddr, vb->bsize, pcdev->active);

	list_add_tail(&vb->queue, &pcdev->capture);

	vb->state = VIDEOBUF_ACTIVE;

	if ((!pcdev->active) && (buf != NULL)) {
		pcdev->active = buf;
		jz4780_camera_setup_dma(pcdev, icd->devnum);
	}
}

static void jz4780_videobuf_release(struct videobuf_queue *vq,
				 struct videobuf_buffer *vb) {

	struct jz4780_buffer *buf = container_of(vb, struct jz4780_buffer, vb);
#ifdef DEBUG
	struct soc_camera_device *icd = vq->priv_data;
	struct device *dev = icd->parent;

	dev_dbg(dev, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
		vb, vb->baddr, vb->bsize);

	switch (vb->state) {
	case VIDEOBUF_ACTIVE:
		dev_dbg(dev, "%s (active)\n", __func__);
		break;
	case VIDEOBUF_QUEUED:
		dev_dbg(dev, "%s (queued)\n", __func__);
		break;
	case VIDEOBUF_PREPARED:
		dev_dbg(dev, "%s (prepared)\n", __func__);
		break;
	default:
		dev_dbg(dev, "%s (unknown)\n", __func__);
		break;
	}
#endif

	free_buffer(vq, buf);
}

static struct videobuf_queue_ops jz4780_videobuf_ops = {
	.buf_setup	= jz4780_videobuf_setup,
	.buf_prepare	= jz4780_videobuf_prepare,
	.buf_queue	= jz4780_videobuf_queue,
	.buf_release	= jz4780_videobuf_release,
};

static void jz4780_camera_init_videobuf(struct videobuf_queue *q,
				     struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct jz4780_camera_dev *pcdev = ici->priv;

	videobuf_queue_dma_contig_init(q, &jz4780_videobuf_ops, icd->parent,
		&pcdev->lock, V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_FIELD_NONE,
		sizeof(struct jz4780_buffer), icd, &ici->host_lock);

}

static int jz4780_camera_clock_start(struct soc_camera_host *ici)
{
	struct jz4780_camera_dev *pcdev = ici->priv;
	int ret = -1;

	dev_dbg(ici->v4l2_dev.dev, "Start clock\n");

	if (pcdev->clk) {
		ret = clk_prepare_enable(pcdev->clk);
		if (ret) {
			dev_err(ici->v4l2_dev.dev, "enable clock failed!\n");
			return ret;
		}
	}

	if (pcdev->mclk) {
		ret = clk_set_rate(pcdev->mclk, pcdev->mclk_freq);
		if (ret) {
			dev_err(ici->v4l2_dev.dev, "set clock rate failed!\n");
			return ret;
		}
		ret = clk_prepare_enable(pcdev->mclk);
		if (ret) {
			dev_err(ici->v4l2_dev.dev, "enable clock failed!\n");
			return ret;
		}
	}
	return ret;
}

static void jz4780_camera_clock_stop(struct soc_camera_host *ici)
{
	struct jz4780_camera_dev *pcdev = ici->priv;
	unsigned long temp = 0;

	dev_dbg(ici->v4l2_dev.dev, "Stop clock\n");

	writel(0, pcdev->base + CIM_STATE);

	/* disable end of frame interrupt */
	temp = readl(pcdev->base + CIM_IMR);
	temp |= CIM_IMR_EOFM;
	writel(temp, pcdev->base + CIM_IMR);

	/* disable rx overflow interrupt */
	temp = readl(pcdev->base + CIM_IMR);
	temp |= CIM_IMR_RFIFO_OFM;
	writel(temp, pcdev->base + CIM_IMR);

	/* disable dma */
	temp = readl(pcdev->base + CIM_CTRL);
	temp &= ~CIM_CTRL_DMA_EN;
	writel(temp, pcdev->base + CIM_CTRL);

	/* clear rx fifo */
	temp = readl(pcdev->base + CIM_CTRL);
	temp |= CIM_CTRL_RXF_RST;
	writel(temp, pcdev->base + CIM_CTRL);

	temp = readl(pcdev->base + CIM_CTRL);
	temp &= ~CIM_CTRL_RXF_RST;
	writel(temp, pcdev->base + CIM_CTRL);

	/* disable cim */
	temp = readl(pcdev->base + CIM_CTRL);
	temp &= ~CIM_CTRL_ENA;
	writel(temp, pcdev->base + CIM_CTRL);

	if (pcdev->clk)
		clk_disable(pcdev->clk);

	if (pcdev->mclk)
		clk_disable(pcdev->mclk);
}

/*
 * The following two functions absolutely depend on the fact, that
 * there can be one or two camera on JZ4780 CIM camera sensor interface
 */
static int jz4780_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct jz4780_camera_dev *pcdev = ici->priv;
	int icd_index = icd->devnum;

	if (pcdev->icd[icd_index])
		return -EBUSY;

	dev_info(icd->parent, "JZ4780 Camera driver attached to camera %d\n",
		 icd->devnum);

	pcdev->icd[icd_index] = icd;

	return 0;
}

static void jz4780_camera_remove_device(struct soc_camera_device *icd)
{

	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct jz4780_camera_dev *pcdev = ici->priv;
	int icd_index = icd->devnum;

	BUG_ON(icd != pcdev->icd[icd_index]);

	dev_info(icd->parent, "JZ4780 Camera driver detached from camera %d\n",
		 icd->devnum);

	pcdev->icd[icd_index] = NULL;
}

static int jz4780_camera_set_crop(struct soc_camera_device *icd,
			       const struct v4l2_crop *a)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);

	return v4l2_subdev_call(sd, video, s_crop, a);
}

static int jz4780_camera_set_bus_param(struct soc_camera_device *icd)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct jz4780_camera_dev *pcdev = ici->priv;
	struct v4l2_mbus_config cfg = {.type = V4L2_MBUS_PARALLEL,};
	u32 pixfmt = icd->current_fmt->host_fmt->fourcc;
	unsigned long common_flags;
	unsigned long cfg_reg = 0;
	unsigned long ctrl_reg = 0;
	unsigned long ctrl2_reg = 0;
	unsigned long fs_reg = 0;
	int ret;

	ret = v4l2_subdev_call(sd, video, g_mbus_config, &cfg);
	if (!ret) {
		common_flags = soc_mbus_config_compatible(&cfg,
							  SOCAM_BUS_FLAGS);
		if (!common_flags) {
			dev_warn(icd->parent,
				 "Flags incompatible: camera 0x%x, host 0x%x\n",
				 cfg.flags, SOCAM_BUS_FLAGS);
			return -EINVAL;
		}
	} else if (ret != -ENOIOCTLCMD) {
		return ret;
	} else {
		common_flags = SOCAM_BUS_FLAGS;
	}

	/* Make choices, based on platform choice, if not selected by subdev */

	if ((common_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) &&
		(common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)) {
		if (pcdev->platform_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_VSYNC_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) &&
		(common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)) {
		if (pcdev->platform_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_LOW;
		else
			common_flags &= ~V4L2_MBUS_HSYNC_ACTIVE_HIGH;
	}

	if ((common_flags & V4L2_MBUS_PCLK_SAMPLE_RISING) &&
		(common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)) {
		if (pcdev->platform_flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_FALLING;
		else
			common_flags &= ~V4L2_MBUS_PCLK_SAMPLE_RISING;
	}

	cfg.flags = common_flags;
	ret = v4l2_subdev_call(sd, video, s_mbus_config, &cfg);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		dev_dbg(icd->parent, "camera s_mbus_config(0x%lx) error %d\n",
			common_flags, ret);
		return ret;
	}

	cfg_reg = (common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING) ?
			cfg_reg | CIM_CFG_PCP : cfg_reg & (~CIM_CFG_PCP);
	dev_dbg(icd->parent, "V4L2_MBUS_PCLK_SAMPLE_FALLING: %d\n",
		!!(common_flags & V4L2_MBUS_PCLK_SAMPLE_FALLING));

	cfg_reg = (common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW) ?
			cfg_reg | CIM_CFG_VSP : cfg_reg & (~CIM_CFG_VSP);
	dev_dbg(icd->parent, "V4L2_MBUS_VSYNC_ACTIVE_LOW: %d\n",
		!!(common_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW));

	cfg_reg = (common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW) ?
			cfg_reg | CIM_CFG_HSP : cfg_reg & (~CIM_CFG_HSP);
	dev_dbg(icd->parent, "V4L2_MBUS_HSYNC_ACTIVE_LOW: %d\n",
		!!(common_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW));

	cfg_reg |= CIM_CFG_DMA_BURST_INCR32 | CIM_CFG_DF_YUV422
			| CIM_CFG_DSM_GCM | CIM_CFG_PACK_Y0UY1V;

	ctrl_reg |= CIM_CTRL_DMA_SYNC | CIM_CTRL_FRC_1;

	ctrl2_reg |= CIM_CTRL2_APM | CIM_CTRL2_EME | CIM_CTRL2_OPE |
			CIM_CTRL2_FSC | CIM_CTRL2_ARIF |
			(1 << CIM_CTRL2_OPG_BIT);

	fs_reg = ((icd->user_width  - 1) << CIM_FS_FHS_BIT) |
		 ((icd->user_height - 1) << CIM_FS_FVS_BIT) |
		 (1 << CIM_FS_BPP_BIT);

	if (pixfmt == V4L2_PIX_FMT_YUV420) {
		ctrl2_reg |= CIM_CTRL2_CSC_YUV420;
		cfg_reg |= CIM_CFG_SEP | CIM_CFG_ORDER_YUYV;
	}

	writel(cfg_reg, pcdev->base + CIM_CFG);
	writel(ctrl_reg, pcdev->base + CIM_CTRL);
	writel(ctrl2_reg, pcdev->base + CIM_CTRL2);
	writel(fs_reg, pcdev->base + CIM_FS);

	return 0;
}

static int jz4780_camera_get_formats(struct soc_camera_device *icd,
					unsigned int idx,
					struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_mbus_pixelfmt *fmt;
	struct device *dev = icd->parent;
	enum v4l2_mbus_pixelcode code;
	int ret, formats = 0;

	ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
	if (ret < 0)
		/* no more formats */
		return 0;

	fmt = soc_mbus_get_fmtdesc(code);
	if (!fmt) {
		dev_err(dev, "Invalid format code #%u: %d\n", idx, code);
		return 0;
	}

	/* Generic pass-trough */
	formats++;
	if (xlate) {
		xlate->host_fmt = fmt;
		xlate->code	= code;
		xlate++;
	}
	return formats;
}

static int jz4780_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret, buswidth;

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_err(icd->parent, "Pixel format %x not found\n",
			 pix->pixelformat);
		return -EINVAL;
	}

	buswidth = xlate->host_fmt->bits_per_sample;
	if (buswidth > 8) {
		dev_err(icd->parent, "bits-per-sample %d for format %x unsupported\n",
			 buswidth, pix->pixelformat);
		return -EINVAL;
	}

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	if (mf.code != xlate->code)
		return -EINVAL;

	pix->width		= mf.width;
	pix->height		= mf.height;
	pix->field		= mf.field;
	pix->colorspace		= mf.colorspace;
	icd->current_fmt	= xlate;

	return ret;
}

static int jz4780_camera_try_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret;
	/* TODO: limit to JZ4780 hardware capabilities */

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_err(icd->parent, "Pixel format %x not found\n",
			 pix->pixelformat);
		return -EINVAL;
	}

	mf.width = pix->width;
	mf.height = pix->height;
	mf.field = pix->field;
	mf.colorspace = pix->colorspace;
	mf.code	= xlate->code;

	/* limit to sensor capabilities */
	ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
	if (ret < 0)
		return ret;

	pix->width = mf.width;
	pix->height	= mf.height;
	pix->field = mf.field;
	pix->colorspace	= mf.colorspace;

	dev_dbg(icd->parent, "Select pixel format %x\n", pix->pixelformat);

	return 0;
}

static int jz4780_camera_reqbufs(struct soc_camera_device *icd,
			      struct v4l2_requestbuffers *p)
{
	int i;
	/*
	 * This is for locking debugging only. I removed spinlocks and now I
	 * check whether .prepare is ever called on a linked buffer, or whether
	 * a dma IRQ can occur for an in-work or unlinked buffer. Until now
	 * it hadn't triggered
	 */
	for (i = 0; i < p->count; i++) {
		struct jz4780_buffer *buf = container_of(icd->vb_vidq.bufs[i],
						      struct jz4780_buffer, vb);
		buf->inwork = 0;
		INIT_LIST_HEAD(&buf->vb.queue);
	}

	return 0;
}

static unsigned int jz4780_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct jz4780_camera_dev *pcdev = ici->priv;
	struct jz4780_buffer *buf;

	buf = list_entry(icd->vb_vidq.stream.next, struct jz4780_buffer,
			 vb.stream);
	poll_wait(file, &buf->vb.done, pt);

	dev_dbg(icd->parent, "%s (vb=0x%p) 0x%08lx %d\n",
		__func__, &buf->vb, buf->vb.baddr, buf->vb.bsize);
	cim_dump_reg(pcdev);

	if (buf->vb.state == VIDEOBUF_DONE ||
	    buf->vb.state == VIDEOBUF_ERROR) {
		dev_dbg(icd->parent, "buf->vb.baddr = %lx, dma_address = %x\n",
			buf->vb.baddr, videobuf_to_dma_contig(&buf->vb));
		return POLLIN | POLLRDNORM;
	}

	return 0;
}

static int jz4780_camera_querycap(struct soc_camera_host *ici,
			       struct v4l2_capability *cap)
{
	/* cap->name is set by the friendly caller:-> */
	strlcpy(cap->card, "JZ4780-Camera", sizeof(cap->card));
	cap->version = VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static void jz4780_camera_wakeup(struct jz4780_camera_dev *pcdev,
			      struct soc_camera_device *icd,
			      struct videobuf_buffer *vb,
			      struct jz4780_buffer *buf)
{
	unsigned long temp;

	list_del_init(&vb->queue);
	vb->state = VIDEOBUF_DONE;
	do_gettimeofday(&vb->ts);
	vb->field_count++;
	wake_up(&vb->done);

	if (list_empty(&pcdev->capture)) {
		/* disable cim */
		temp = readl(pcdev->base + CIM_CTRL);
		temp &= ~(CIM_CTRL_ENA | CIM_CTRL_DMA_EN);
		writel(temp, pcdev->base + CIM_CTRL);

		dev_dbg(icd->parent, "No more buffers - disable CIM\n");

		pcdev->active = NULL;
		return;
	}

	pcdev->active = list_entry(pcdev->capture.next,
				   struct jz4780_buffer, vb.queue);
	jz4780_camera_setup_dma(pcdev, icd->devnum);
}


static irqreturn_t jz4780_camera_irq_handler(int irq, void *data)
{
	struct jz4780_camera_dev *pcdev = (struct jz4780_camera_dev *)data;
	struct soc_camera_device *icd = NULL;
	struct jz4780_buffer *buf;
	struct videobuf_buffer *vb;
	unsigned int status = 0, temp = 0;
	unsigned long flags = 0;
	unsigned int cam_dev_index;

	for (cam_dev_index = 0; cam_dev_index < 2; cam_dev_index++) {
		icd = pcdev->icd[cam_dev_index];
		if (icd)
			break;
	}

	if (unlikely(!icd))
		return IRQ_HANDLED;

	/* read interrupt status register */
	status = readl(pcdev->base + CIM_STATE);

	dev_dbg(icd->parent, "%s (status=0x%08x)\n", __func__, status);

	if (status & CIM_STATE_FIFO_OVERFLOW) {
		dev_warn(icd->parent, "Rx FIFO OverFlow interrupt!\n");

		cim_dump_reg(pcdev);

		/* clear rx overflow interrupt */
		temp = readl(pcdev->base + CIM_STATE);
		temp &= ~CIM_STATE_RXF_OF;
		writel(temp, pcdev->base + CIM_STATE);

		/* disable cim */
		temp = readl(pcdev->base + CIM_CTRL);
		temp &= ~CIM_CTRL_ENA;
		writel(temp, pcdev->base + CIM_CTRL);

		/* clear rx fifo */
		temp = readl(pcdev->base + CIM_CTRL);
		temp |= CIM_CTRL_RXF_RST;
		writel(temp, pcdev->base + CIM_CTRL);

		temp = readl(pcdev->base + CIM_CTRL);
		temp &= ~CIM_CTRL_RXF_RST;
		writel(temp, pcdev->base + CIM_CTRL);

		/* clear status register */
		writel(0, pcdev->base + CIM_STATE);

		/* enable cim */
		temp = readl(pcdev->base + CIM_CTRL);
		temp |= CIM_CTRL_ENA;
		writel(temp, pcdev->base + CIM_CTRL);

		return IRQ_HANDLED;
	}

	if (status & CIM_STATE_DMA_EOF) {
		spin_lock_irqsave(&pcdev->lock, flags);

		if (unlikely(!pcdev->active)) {
			dev_warn(icd->parent, "DMA End IRQ with no active buffer\n");
			/* clear dma interrupt status */
			temp = readl(pcdev->base + CIM_STATE);
			temp &= ~CIM_STATE_DMA_EOF;
			writel(temp, pcdev->base + CIM_STATE);

			spin_unlock_irqrestore(&pcdev->lock, flags);
			return IRQ_HANDLED;
		}

		vb = &pcdev->active->vb;
		buf = container_of(vb, struct jz4780_buffer, vb);

		WARN_ON(buf->inwork || list_empty(&vb->queue));
		dev_dbg(icd->parent, "%s (vb=0x%p) 0x%08lx %d\n", __func__,
			vb, vb->baddr, vb->bsize);

		/* clear dma interrupt status */
		temp = readl(pcdev->base + CIM_STATE);
		temp &= ~CIM_STATE_DMA_EOF;
		writel(temp, pcdev->base + CIM_STATE);

		spin_unlock_irqrestore(&pcdev->lock, flags);
		jz4780_camera_wakeup(pcdev, icd, vb, buf);

		return IRQ_HANDLED;
	}

	if (status & CIM_STATE_SIZE_ERR) {
		dev_warn(icd->parent, "Frame size error!\n");

		/* disable cim */
		temp = readl(pcdev->base + CIM_CTRL);
		temp &= ~CIM_CTRL_ENA;
		writel(temp, pcdev->base + CIM_CTRL);

		/* clear frame size error interrupt status */
		temp = readl(pcdev->base + CIM_STATE);
		temp &= ~CIM_STATE_SIZE_ERR;
		writel(temp, pcdev->base + CIM_STATE);

		/* enable cim */
		temp = readl(pcdev->base + CIM_CTRL);
		temp |= CIM_CTRL_ENA;
		writel(temp, pcdev->base + CIM_CTRL);

		return IRQ_HANDLED;
	}

	return IRQ_HANDLED;
}

static struct soc_camera_host_ops jz4780_soc_camera_host_ops = {
	.owner = THIS_MODULE,
	.add = jz4780_camera_add_device,
	.remove	= jz4780_camera_remove_device,
	.clock_start	= jz4780_camera_clock_start,
	.clock_stop	= jz4780_camera_clock_stop,
	.set_bus_param = jz4780_camera_set_bus_param,
	.set_crop = jz4780_camera_set_crop,
	.get_formats	= jz4780_camera_get_formats,
	.set_fmt = jz4780_camera_set_fmt,
	.try_fmt = jz4780_camera_try_fmt,
	.init_videobuf = jz4780_camera_init_videobuf,
	.reqbufs = jz4780_camera_reqbufs,
	.poll = jz4780_camera_poll,
	.querycap = jz4780_camera_querycap,
};

static int jz4780_camera_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct jz4780_camera_dev *pcdev;
	struct resource *regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	struct v4l2_of_endpoint ep;
	void __iomem *base;
	int err = 0;

	if (!regs || !irq)
		return -ENODEV;

	pcdev = devm_kzalloc(&pdev->dev, sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev)
		return -ENOMEM;

	pcdev->clk = devm_clk_get(&pdev->dev, "cim");
	if (IS_ERR(pcdev->clk))
		return PTR_ERR(pcdev->clk);

	pcdev->mclk = devm_clk_get(&pdev->dev, "module");
	if (IS_ERR(pcdev->mclk))
		return PTR_ERR(pcdev->mclk);

	err = of_property_read_u32(np, "clock-frequency",
				       &pcdev->mclk_freq);
	if (err) {
		dev_warn(&pdev->dev, "Using default mclk 24MHz\n");
		pcdev->mclk_freq = 24000000;
	}

	np = of_graph_get_next_endpoint(np, NULL);
	if (!np) {
		dev_err(&pdev->dev, "could not find endpoint\n");
		return -EINVAL;
	}

	err = v4l2_of_parse_endpoint(np, &ep);
	if (err) {
		dev_err(&pdev->dev, "could not parse endpoint\n");
		return -EINVAL;
	}

	pcdev->platform_flags = ep.bus.parallel.flags;

	INIT_LIST_HEAD(&pcdev->capture);
	spin_lock_init(&pcdev->lock);

	/* Request the region */
	base = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "failed to remap memory region\n");
		return PTR_ERR(base);
	}
	pcdev->irq = irq->start;
	pcdev->base = base;

	/* request irq */
	err = devm_request_irq(&pdev->dev, pcdev->irq,
				jz4780_camera_irq_handler, IRQF_DISABLED,
				dev_name(&pdev->dev), pcdev);
	if (err) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		return err;
	}

	pcdev->soc_host.drv_name	= DRIVER_NAME;
	pcdev->soc_host.ops		= &jz4780_soc_camera_host_ops;
	pcdev->soc_host.priv		= pcdev;
	pcdev->soc_host.v4l2_dev.dev	= &pdev->dev;
	pcdev->soc_host.nr		= pdev->id;

	pcdev->dma_desc = dmam_alloc_coherent(&pdev->dev,
				sizeof(*pcdev->dma_desc),
				(dma_addr_t *)&pcdev->dma_desc_paddr,
				GFP_KERNEL);
	if (!pcdev->dma_desc) {
		dev_err(&pdev->dev, "failed to allocate DMA memory\n");
		return -ENOMEM;
	}
	dev_dbg(&pdev->dev, "DMA descriptor virtual:  %p\n", pcdev->dma_desc);
	dev_dbg(&pdev->dev, "DMA descriptor physical: %p\n",
		pcdev->dma_desc_paddr);

	err = soc_camera_host_register(&pcdev->soc_host);
	if (err) {
		dev_err(&pdev->dev, "failed to register camera\n");
		return err;
	}

	/* Start the clock so that the I2C sensor may be probed */
	jz4780_camera_clock_start(&pcdev->soc_host);

	dev_info(&pdev->dev, "JZ4780 Camera driver loaded\n");
	return 0;
}

static int __exit jz4780_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *soc_host = to_soc_camera_host(&pdev->dev);

	soc_camera_host_unregister(soc_host);

	dev_info(&pdev->dev, "JZ4780 Camera driver unloaded\n");

	return 0;
}

static const struct of_device_id jz4780_camera_of_match[] = {
	{ .compatible = "ingenic,jz4780-cim", },
	{},
};
MODULE_DEVICE_TABLE(of, jz4780_camera_of_match);

static struct platform_driver jz4780_camera_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = jz4780_camera_of_match,
		.owner	= THIS_MODULE,
	},
	.probe		= jz4780_camera_probe,
	.remove		= jz4780_camera_remove,
};

static int __init jz4780_camera_init(void)
{
	return platform_driver_probe(&jz4780_camera_driver,
					jz4780_camera_probe);
}

static void __exit jz4780_camera_exit(void)
{
	return platform_driver_unregister(&jz4780_camera_driver);
}

late_initcall(jz4780_camera_init);
module_exit(jz4780_camera_exit);

MODULE_DESCRIPTION("JZ4780 SoC Camera Host driver");
MODULE_AUTHOR("FeiYe <feiye@ingenic.cn>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
