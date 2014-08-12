/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Copyright (C) 2013, Imagination Technologies
 *  JZ47xx SD/MMC controller driver
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>

#include <linux/platform_data/mmc-jz47xx.h>

#include <asm/cacheflush.h>

#ifdef CONFIG_MACH_JZ4740
# include <asm/mach-jz4740/gpio.h>
#endif

#define JZ_REG_MMC_STRPCL		0x00
#define JZ_REG_MMC_STATUS		0x04
#define JZ_REG_MMC_CLKRT		0x08
#define JZ_REG_MMC_CMDAT		0x0C
#define JZ_REG_MMC_RESTO		0x10
#define JZ_REG_MMC_RDTO			0x14
#define JZ_REG_MMC_BLKLEN		0x18
#define JZ_REG_MMC_NOB			0x1C
#define JZ_REG_MMC_SNOB			0x20
#define JZ_REG_MMC_IMASK		0x24
#define JZ_REG_MMC_IREG			0x28
#define JZ_REG_MMC_CMD			0x2C
#define JZ_REG_MMC_ARG			0x30
#define JZ_REG_MMC_RESP_FIFO		0x34
#define JZ_REG_MMC_RXFIFO		0x38
#define JZ_REG_MMC_TXFIFO		0x3C
#define JZ_REG_MMC_DMAC			0x44

#define JZ_MMC_STRPCL_EXIT_MULTIPLE	BIT(7)
#define JZ_MMC_STRPCL_EXIT_TRANSFER	BIT(6)
#define JZ_MMC_STRPCL_START_READWAIT	BIT(5)
#define JZ_MMC_STRPCL_STOP_READWAIT	BIT(4)
#define JZ_MMC_STRPCL_RESET		BIT(3)
#define JZ_MMC_STRPCL_START_OP		BIT(2)
#define JZ_MMC_STRPCL_CLOCK_CONTROL	(BIT(1) | BIT(0))
#define JZ_MMC_STRPCL_CLOCK_STOP	BIT(0)
#define JZ_MMC_STRPCL_CLOCK_START	BIT(1)

#define JZ_MMC_STATUS_IS_RESETTING	BIT(15)
#define JZ_MMC_STATUS_SDIO_INT_ACTIVE	BIT(14)
#define JZ_MMC_STATUS_PRG_DONE		BIT(13)
#define JZ_MMC_STATUS_DATA_TRAN_DONE	BIT(12)
#define JZ_MMC_STATUS_END_CMD_RES	BIT(11)
#define JZ_MMC_STATUS_DATA_FIFO_AFULL	BIT(10)
#define JZ_MMC_STATUS_IS_READWAIT	BIT(9)
#define JZ_MMC_STATUS_CLK_EN		BIT(8)
#define JZ_MMC_STATUS_DATA_FIFO_FULL	BIT(7)
#define JZ_MMC_STATUS_DATA_FIFO_EMPTY	BIT(6)
#define JZ_MMC_STATUS_CRC_RES_ERR	BIT(5)
#define JZ_MMC_STATUS_CRC_READ_ERROR	BIT(4)
#define JZ_MMC_STATUS_TIMEOUT_WRITE	BIT(3)
#define JZ_MMC_STATUS_CRC_WRITE_ERROR	BIT(2)
#define JZ_MMC_STATUS_TIMEOUT_RES	BIT(1)
#define JZ_MMC_STATUS_TIMEOUT_READ	BIT(0)

#define JZ_MMC_STATUS_READ_ERROR_MASK	\
	(JZ_MMC_STATUS_CRC_READ_ERROR | JZ_MMC_STATUS_TIMEOUT_READ)
#define JZ_MMC_STATUS_WRITE_ERROR_MASK	\
	(JZ_MMC_STATUS_CRC_WRITE_ERROR | JZ_MMC_STATUS_TIMEOUT_WRITE)

#define JZ_MMC_CMDAT_IO_ABORT		BIT(11)
#define JZ_MMC_CMDAT_BUS_WIDTH		(BIT(9) | BIT(10))
#define JZ_MMC_CMDAT_BUS_WIDTH_1BIT	0
#define JZ_MMC_CMDAT_BUS_WIDTH_4BIT	BIT(10)
#define JZ_MMC_CMDAT_BUS_WIDTH_8BIT	(BIT(9) | BIT(10))
#define JZ_MMC_CMDAT_DMA_EN		BIT(8)
#define JZ_MMC_CMDAT_INIT		BIT(7)
#define JZ_MMC_CMDAT_BUSY		BIT(6)
#define JZ_MMC_CMDAT_STREAM		BIT(5)
#define JZ_MMC_CMDAT_WRITE		BIT(4)
#define JZ_MMC_CMDAT_DATA_EN		BIT(3)
#define JZ_MMC_CMDAT_RESPONSE_FORMAT	(BIT(2) | BIT(1) | BIT(0))
#define JZ_MMC_CMDAT_RSP_R1		1
#define JZ_MMC_CMDAT_RSP_R2		2
#define JZ_MMC_CMDAT_RSP_R3		3

#define JZ_MMC_IRQ_SDIO			BIT(7)
#define JZ_MMC_IRQ_TXFIFO_WR_REQ	BIT(6)
#define JZ_MMC_IRQ_RXFIFO_RD_REQ	BIT(5)
#define JZ_MMC_IRQ_END_CMD_RES		BIT(2)
#define JZ_MMC_IRQ_PRG_DONE		BIT(1)
#define JZ_MMC_IRQ_DATA_TRAN_DONE	BIT(0)

#define JZ_MMC_DMAC_DMA_SEL		BIT(1)
#define JZ_MMC_DMAC_DMA_EN		BIT(0)

#define JZ_MMC_CLK_RATE			24000000

enum jz47xx_mmc_version {
	JZ_MMC_JZ4740,
	JZ_MMC_JZ4750,
	JZ_MMC_JZ4780,
};

enum jz47xx_mmc_state {
	JZ_MMC_STATE_READ_RESPONSE,
	JZ_MMC_STATE_TRANSFER_DATA,
	JZ_MMC_STATE_SEND_STOP,
	JZ_MMC_STATE_DONE,
};

struct jz47xx_mmc_host {
	struct mmc_host *mmc;
	struct platform_device *pdev;
	struct clk *clk;

	enum jz47xx_mmc_version version;
	int gpio_power;
	bool power_active_low;
	int irq;
	struct dma_chan *dmac;

	struct completion comp;

	void __iomem *base;
	dma_addr_t phys_base;
	struct mmc_request *req;
	struct mmc_command *cmd;
	struct dma_async_tx_descriptor *desc;

	unsigned long waiting;

	uint32_t cmdat;

	uint32_t irq_mask;

	spinlock_t lock;

	struct timer_list timeout_timer;
	struct sg_mapping_iter miter;
	enum jz47xx_mmc_state state;
};

static uint32_t jz47xx_mmc_read_irq_reg(struct jz47xx_mmc_host *host)
{
	/* In the 4780 onwards, IREG is expanded to 32 bits. */
	if (host->version >= JZ_MMC_JZ4780)
		return readl(host->base + JZ_REG_MMC_IREG);
	else
		return readw(host->base + JZ_REG_MMC_IREG);
}

static void jz47xx_mmc_write_irq_reg(struct jz47xx_mmc_host *host, uint32_t val)
{
	if (host->version >= JZ_MMC_JZ4780)
		return writel(val, host->base + JZ_REG_MMC_IREG);
	else
		return writew(val, host->base + JZ_REG_MMC_IREG);
}

static void jz47xx_mmc_set_irq_enabled(struct jz47xx_mmc_host *host,
	unsigned int irq, bool enabled)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	if (enabled)
		host->irq_mask &= ~irq;
	else
		host->irq_mask |= irq;
	spin_unlock_irqrestore(&host->lock, flags);

	/* In the 4750 onwards, IMASK is expanded to 32 bits. */
	if (host->version >= JZ_MMC_JZ4750)
		writel(host->irq_mask, host->base + JZ_REG_MMC_IMASK);
	else
		writew(host->irq_mask, host->base + JZ_REG_MMC_IMASK);
}

static void jz47xx_mmc_clock_enable(struct jz47xx_mmc_host *host,
	bool start_transfer)
{
	uint16_t val = JZ_MMC_STRPCL_CLOCK_START;

	if (start_transfer)
		val |= JZ_MMC_STRPCL_START_OP;

	writew(val, host->base + JZ_REG_MMC_STRPCL);
}

static void jz47xx_mmc_clock_disable(struct jz47xx_mmc_host *host)
{
	uint32_t status;
	unsigned int timeout = 1000;

	writew(JZ_MMC_STRPCL_CLOCK_STOP, host->base + JZ_REG_MMC_STRPCL);
	do {
		status = readl(host->base + JZ_REG_MMC_STATUS);
	} while (status & JZ_MMC_STATUS_CLK_EN && --timeout);
}

static void jz47xx_mmc_reset(struct jz47xx_mmc_host *host)
{
	uint32_t status;
	unsigned int timeout = 1000;

	writew(JZ_MMC_STRPCL_RESET, host->base + JZ_REG_MMC_STRPCL);
	udelay(10);
	do {
		status = readl(host->base + JZ_REG_MMC_STATUS);
	} while (status & JZ_MMC_STATUS_IS_RESETTING && --timeout);
}

static void jz47xx_mmc_request_done(struct jz47xx_mmc_host *host)
{
	struct mmc_request *req = host->req;
	struct mmc_data *data = req->cmd->data;

	host->req = NULL;

	if (data && host->desc) {
		if (!data->error)
			data->bytes_xfered += data->blocks * data->blksz;

		host->desc = NULL;

		dmaengine_terminate_all(host->dmac);
		dma_unmap_sg(host->dmac->device->dev, data->sg, data->sg_len,
			     ((data->flags & MMC_DATA_READ) ?
			     DMA_FROM_DEVICE : DMA_TO_DEVICE));
	}

	mmc_request_done(host->mmc, req);
}

static unsigned int jz47xx_mmc_poll_irq(struct jz47xx_mmc_host *host,
	unsigned int irq)
{
	unsigned int timeout = 0x800;
	uint32_t status;

	do {
		status = jz47xx_mmc_read_irq_reg(host);
	} while (!(status & irq) && --timeout);

	if (timeout == 0) {
		set_bit(0, &host->waiting);
		mod_timer(&host->timeout_timer, jiffies + 5*HZ);
		jz47xx_mmc_set_irq_enabled(host, irq, true);
		return true;
	}

	return false;
}

static void jz47xx_mmc_transfer_check_state(struct jz47xx_mmc_host *host,
	struct mmc_data *data)
{
	int status;

	status = readl(host->base + JZ_REG_MMC_STATUS);
	if (status & JZ_MMC_STATUS_WRITE_ERROR_MASK) {
		if (status & (JZ_MMC_STATUS_TIMEOUT_WRITE)) {
			host->req->cmd->error = -ETIMEDOUT;
			data->error = -ETIMEDOUT;
		} else {
			host->req->cmd->error = -EIO;
			data->error = -EIO;
		}
	} else if (status & JZ_MMC_STATUS_READ_ERROR_MASK) {
		if (status & (JZ_MMC_STATUS_TIMEOUT_READ)) {
			host->req->cmd->error = -ETIMEDOUT;
			data->error = -ETIMEDOUT;
		} else {
			host->req->cmd->error = -EIO;
			data->error = -EIO;
		}
	}
}

static bool jz47xx_mmc_write_pio(struct jz47xx_mmc_host *host,
	struct mmc_data *data)
{
	struct sg_mapping_iter *miter = &host->miter;
	void __iomem *fifo_addr = host->base + JZ_REG_MMC_TXFIFO;
	uint32_t *buf;
	bool timeout;
	size_t i, j;

	while (sg_miter_next(miter)) {
		buf = miter->addr;
		i = miter->length / 4;
		j = i / 8;
		i = i & 0x7;
		while (j) {
			timeout = jz47xx_mmc_poll_irq(host,
						JZ_MMC_IRQ_TXFIFO_WR_REQ);
			if (unlikely(timeout))
				goto poll_timeout;

			writel(buf[0], fifo_addr);
			writel(buf[1], fifo_addr);
			writel(buf[2], fifo_addr);
			writel(buf[3], fifo_addr);
			writel(buf[4], fifo_addr);
			writel(buf[5], fifo_addr);
			writel(buf[6], fifo_addr);
			writel(buf[7], fifo_addr);
			buf += 8;
			--j;
		}
		if (unlikely(i)) {
			timeout = jz47xx_mmc_poll_irq(host,
						JZ_MMC_IRQ_TXFIFO_WR_REQ);
			if (unlikely(timeout))
				goto poll_timeout;

			while (i) {
				writel(*buf, fifo_addr);
				++buf;
				--i;
			}
		}
		data->bytes_xfered += miter->length;
	}
	sg_miter_stop(miter);

	return false;

poll_timeout:
	miter->consumed = (void *)buf - miter->addr;
	data->bytes_xfered += miter->consumed;
	sg_miter_stop(miter);

	return true;
}

static bool jz47xx_mmc_read_pio(struct jz47xx_mmc_host *host,
	struct mmc_data *data)
{
	struct sg_mapping_iter *miter = &host->miter;
	void __iomem *fifo_addr = host->base + JZ_REG_MMC_RXFIFO;
	uint32_t *buf;
	uint32_t d;
	uint32_t status;
	size_t i, j;
	unsigned int timeout;

	while (sg_miter_next(miter)) {
		buf = miter->addr;
		i = miter->length;
		j = i / 32;
		i = i & 0x1f;
		while (j) {
			timeout = jz47xx_mmc_poll_irq(host,
						JZ_MMC_IRQ_RXFIFO_RD_REQ);
			if (unlikely(timeout))
				goto poll_timeout;

			buf[0] = readl(fifo_addr);
			buf[1] = readl(fifo_addr);
			buf[2] = readl(fifo_addr);
			buf[3] = readl(fifo_addr);
			buf[4] = readl(fifo_addr);
			buf[5] = readl(fifo_addr);
			buf[6] = readl(fifo_addr);
			buf[7] = readl(fifo_addr);

			buf += 8;
			--j;
		}

		if (unlikely(i)) {
			timeout = jz47xx_mmc_poll_irq(host,
						JZ_MMC_IRQ_RXFIFO_RD_REQ);
			if (unlikely(timeout))
				goto poll_timeout;

			while (i >= 4) {
				*buf++ = readl(fifo_addr);
				i -= 4;
			}
			if (unlikely(i > 0)) {
				d = readl(fifo_addr);
				memcpy(buf, &d, i);
			}
		}
		data->bytes_xfered += miter->length;

		/* This can go away once MIPS implements
		 * flush_kernel_dcache_page */
		flush_dcache_page(miter->page);
	}
	sg_miter_stop(miter);

	/* For whatever reason there is sometime one word more in the fifo then
	 * requested */
	timeout = 1000;
	status = readl(host->base + JZ_REG_MMC_STATUS);
	while (!(status & JZ_MMC_STATUS_DATA_FIFO_EMPTY) && --timeout) {
		d = readl(fifo_addr);
		status = readl(host->base + JZ_REG_MMC_STATUS);
	}

	return false;

poll_timeout:
	miter->consumed = (void *)buf - miter->addr;
	data->bytes_xfered += miter->consumed;
	sg_miter_stop(miter);

	return true;
}

static void jz47xx_mmc_prepare_pio_transfer(struct jz47xx_mmc_host *host)
{
	struct mmc_command *cmd = host->req->cmd;
	struct mmc_data *data = cmd->data;
	int direction;

	if (data->flags & MMC_DATA_READ)
		direction = SG_MITER_TO_SG;
	else
		direction = SG_MITER_FROM_SG;

	sg_miter_start(&host->miter, data->sg, data->sg_len, direction);
}

static struct dma_async_tx_descriptor *jz47xx_mmc_prepare_dma_transfer(
	struct jz47xx_mmc_host *host)
{
	struct mmc_command *cmd = host->req->cmd;
	struct mmc_data *data = cmd->data;
	struct dma_slave_config config;
	struct dma_async_tx_descriptor *desc;
	int ret, sg_len;

	if (!host->dmac)
		return NULL;

	sg_len = dma_map_sg(host->dmac->device->dev, data->sg, data->sg_len,
			    ((data->flags & MMC_DATA_READ) ?
			    DMA_FROM_DEVICE : DMA_TO_DEVICE));
	if (sg_len == 0)
		return NULL;

	if (data->flags & MMC_DATA_READ) {
		config.direction = DMA_DEV_TO_MEM;
		config.src_addr = host->phys_base + JZ_REG_MMC_RXFIFO;
	} else {
		config.direction = DMA_MEM_TO_DEV;
		config.dst_addr = host->phys_base + JZ_REG_MMC_TXFIFO;
	}

	config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.src_maxburst = 8;
	config.dst_maxburst = 8;

	ret = dmaengine_slave_config(host->dmac, &config);
	if (ret) {
		dev_err(&host->pdev->dev, "DMA slave config failed: %d\n", ret);
		goto err_unmap_sg;
	}

	desc = dmaengine_prep_slave_sg(host->dmac, data->sg, sg_len,
				       config.direction, 0);
	if (!desc) {
		dev_err(&host->pdev->dev, "DMA slave prepare failed\n");
		goto err_unmap_sg;
	}

	return desc;

err_unmap_sg:
	dma_unmap_sg(host->dmac->device->dev, data->sg, data->sg_len,
		     ((data->flags & MMC_DATA_READ) ?
		     DMA_FROM_DEVICE : DMA_TO_DEVICE));
	return NULL;
}

static void jz47xx_mmc_timeout(unsigned long data)
{
	struct jz47xx_mmc_host *host = (struct jz47xx_mmc_host *)data;

	if (!test_and_clear_bit(0, &host->waiting))
		return;

	jz47xx_mmc_set_irq_enabled(host, JZ_MMC_IRQ_END_CMD_RES, false);

	host->req->cmd->error = -ETIMEDOUT;
	jz47xx_mmc_request_done(host);
}

static void jz47xx_mmc_read_response(struct jz47xx_mmc_host *host,
	struct mmc_command *cmd)
{
	int i;
	uint16_t tmp;
	void __iomem *fifo_addr = host->base + JZ_REG_MMC_RESP_FIFO;

	if (cmd->flags & MMC_RSP_136) {
		tmp = readw(fifo_addr);
		for (i = 0; i < 4; ++i) {
			cmd->resp[i] = tmp << 24;
			tmp = readw(fifo_addr);
			cmd->resp[i] |= tmp << 8;
			tmp = readw(fifo_addr);
			cmd->resp[i] |= tmp >> 8;
		}
	} else {
		cmd->resp[0] = readw(fifo_addr) << 24;
		cmd->resp[0] |= readw(fifo_addr) << 8;
		cmd->resp[0] |= readw(fifo_addr) & 0xff;
	}
}

static void jz47xx_mmc_send_command(struct jz47xx_mmc_host *host,
	struct mmc_command *cmd)
{
	uint32_t cmdat = host->cmdat;

	host->cmdat &= ~JZ_MMC_CMDAT_INIT;
	jz47xx_mmc_clock_disable(host);

	host->cmd = cmd;

	if (cmd->flags & MMC_RSP_BUSY)
		cmdat |= JZ_MMC_CMDAT_BUSY;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1B:
	case MMC_RSP_R1:
		cmdat |= JZ_MMC_CMDAT_RSP_R1;
		break;
	case MMC_RSP_R2:
		cmdat |= JZ_MMC_CMDAT_RSP_R2;
		break;
	case MMC_RSP_R3:
		cmdat |= JZ_MMC_CMDAT_RSP_R3;
		break;
	default:
		break;
	}

	if (cmd->data) {
		cmdat |= JZ_MMC_CMDAT_DATA_EN;
		if (cmd->data->flags & MMC_DATA_WRITE)
			cmdat |= JZ_MMC_CMDAT_WRITE;
		if (cmd->data->flags & MMC_DATA_STREAM)
			cmdat |= JZ_MMC_CMDAT_STREAM;

		writew(cmd->data->blksz, host->base + JZ_REG_MMC_BLKLEN);
		writew(cmd->data->blocks, host->base + JZ_REG_MMC_NOB);

		/* Fall back to PIO if we have no DMA channel or setup fails. */
		host->desc = jz47xx_mmc_prepare_dma_transfer(host);
		if (host->desc) {
			/*
			 * The 4780's MMC controller has integrated DMA ability
			 * in addition to being able to use the external DMA
			 * controller. It moves DMA control bits to a separate
			 * register. The DMA_SEL bit chooses the external
			 * controller over the integrated one. Earlier SoCs
			 * can only use the external controller, and have a
			 * single DMA enable bit in CMDAT.
			 */
			if (host->version >= JZ_MMC_JZ4780) {
				writel(JZ_MMC_DMAC_DMA_EN | JZ_MMC_DMAC_DMA_SEL,
				       host->base + JZ_REG_MMC_DMAC);
			} else {
				cmdat |= JZ_MMC_CMDAT_DMA_EN;
			}
		} else {
			jz47xx_mmc_prepare_pio_transfer(host);

			if (host->version >= JZ_MMC_JZ4780)
				writel(0, host->base + JZ_REG_MMC_DMAC);
		}
	}

	writeb(cmd->opcode, host->base + JZ_REG_MMC_CMD);
	writel(cmd->arg, host->base + JZ_REG_MMC_ARG);
	writel(cmdat, host->base + JZ_REG_MMC_CMDAT);

	jz47xx_mmc_clock_enable(host, 1);
}

static void jz47xx_mmc_dma_complete_func(void *completion)
{
	complete(completion);
}

static irqreturn_t jz47xx_mmc_irq_worker(int irq, void *devid)
{
	struct jz47xx_mmc_host *host = (struct jz47xx_mmc_host *)devid;
	struct mmc_command *cmd = host->req->cmd;
	struct mmc_request *req = host->req;
	bool timeout = false;

	if (cmd->error)
		host->state = JZ_MMC_STATE_DONE;

	switch (host->state) {
	case JZ_MMC_STATE_READ_RESPONSE:
		if (cmd->flags & MMC_RSP_PRESENT)
			jz47xx_mmc_read_response(host, cmd);

		if (!cmd->data)
			break;

	case JZ_MMC_STATE_TRANSFER_DATA:
		if (host->desc) {
			dmaengine_submit(host->desc);
			init_completion(&host->comp);
			host->desc->callback = jz47xx_mmc_dma_complete_func;
			host->desc->callback_param = &host->comp;
			dma_async_issue_pending(host->dmac);
			timeout = wait_for_completion_timeout(&host->comp,
							msecs_to_jiffies(1000));
			if (timeout == 0) {
				cmd->error = -ETIMEDOUT;
				break;
			}
		} else {
			if (cmd->data->flags & MMC_DATA_READ)
				timeout = jz47xx_mmc_read_pio(host, cmd->data);
			else
				timeout = jz47xx_mmc_write_pio(host, cmd->data);

			if (unlikely(timeout)) {
				host->state = JZ_MMC_STATE_TRANSFER_DATA;
				break;
			}
		}

		jz47xx_mmc_transfer_check_state(host, cmd->data);

		timeout = jz47xx_mmc_poll_irq(host, JZ_MMC_IRQ_DATA_TRAN_DONE);
		if (unlikely(timeout)) {
			host->state = JZ_MMC_STATE_SEND_STOP;
			break;
		}
		jz47xx_mmc_write_irq_reg(host, JZ_MMC_IRQ_DATA_TRAN_DONE);

	case JZ_MMC_STATE_SEND_STOP:
		if (!req->stop)
			break;

		jz47xx_mmc_send_command(host, req->stop);

		if (mmc_resp_type(req->stop) & MMC_RSP_BUSY) {
			timeout = jz47xx_mmc_poll_irq(host,
						      JZ_MMC_IRQ_PRG_DONE);
			if (timeout) {
				host->state = JZ_MMC_STATE_DONE;
				break;
			}
		}

	case JZ_MMC_STATE_DONE:
		break;
	}

	if (!timeout)
		jz47xx_mmc_request_done(host);

	return IRQ_HANDLED;
}

static irqreturn_t jz47xx_mmc_irq(int irq, void *devid)
{
	struct jz47xx_mmc_host *host = devid;
	struct mmc_command *cmd = host->cmd;
	uint32_t irq_reg, status, tmp;

	status = readl(host->base + JZ_REG_MMC_STATUS);
	irq_reg = jz47xx_mmc_read_irq_reg(host);
	tmp = irq_reg;
	irq_reg &= ~host->irq_mask;

	tmp &= ~(JZ_MMC_IRQ_TXFIFO_WR_REQ | JZ_MMC_IRQ_RXFIFO_RD_REQ |
		JZ_MMC_IRQ_PRG_DONE | JZ_MMC_IRQ_DATA_TRAN_DONE);

	if (tmp != irq_reg)
		jz47xx_mmc_write_irq_reg(host, tmp & ~irq_reg);

	if (irq_reg & JZ_MMC_IRQ_SDIO) {
		jz47xx_mmc_write_irq_reg(host, JZ_MMC_IRQ_SDIO);
		mmc_signal_sdio_irq(host->mmc);
		irq_reg &= ~JZ_MMC_IRQ_SDIO;
	}

	if (host->req && cmd && irq_reg) {
		if (test_and_clear_bit(0, &host->waiting)) {
			del_timer(&host->timeout_timer);

			if (status & JZ_MMC_STATUS_TIMEOUT_RES) {
				cmd->error = -ETIMEDOUT;
			} else if (status & JZ_MMC_STATUS_CRC_RES_ERR) {
				cmd->error = -EIO;
			} else if (status & (JZ_MMC_STATUS_CRC_READ_ERROR |
					JZ_MMC_STATUS_CRC_WRITE_ERROR)) {
				if (cmd->data)
					cmd->data->error = -EIO;
				cmd->error = -EIO;
			}

			jz47xx_mmc_set_irq_enabled(host, irq_reg, false);
			jz47xx_mmc_write_irq_reg(host, irq_reg);

			return IRQ_WAKE_THREAD;
		}
	}

	return IRQ_HANDLED;
}

static int jz47xx_mmc_set_clock_rate(struct jz47xx_mmc_host *host, int rate)
{
	int div = 0;
	int real_rate, ret;

	jz47xx_mmc_clock_disable(host);
	ret = clk_set_rate(host->clk, host->mmc->f_max);
	if (ret) {
		dev_warn(&host->pdev->dev,
			"Unable to set clock rate to %u (%d)\n",
			host->mmc->f_max, ret);
	}

	real_rate = clk_get_rate(host->clk);

	while (real_rate > rate && div < 7) {
		++div;
		real_rate >>= 1;
	}

	writew(div, host->base + JZ_REG_MMC_CLKRT);
	return real_rate;
}

static void jz47xx_mmc_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct jz47xx_mmc_host *host = mmc_priv(mmc);

	host->req = req;

	jz47xx_mmc_write_irq_reg(host, 0xffffffff);
	jz47xx_mmc_set_irq_enabled(host, JZ_MMC_IRQ_END_CMD_RES, true);

	host->state = JZ_MMC_STATE_READ_RESPONSE;
	set_bit(0, &host->waiting);
	mod_timer(&host->timeout_timer, jiffies + 5*HZ);
	jz47xx_mmc_send_command(host, req->cmd);
}

static void jz47xx_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct jz47xx_mmc_host *host = mmc_priv(mmc);
	if (ios->clock)
		jz47xx_mmc_set_clock_rate(host, ios->clock);

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		jz47xx_mmc_reset(host);
		if (gpio_is_valid(host->gpio_power))
			gpio_set_value(host->gpio_power,
					!host->power_active_low);
		host->cmdat |= JZ_MMC_CMDAT_INIT;
		clk_prepare_enable(host->clk);
		break;
	case MMC_POWER_ON:
		break;
	default:
		if (gpio_is_valid(host->gpio_power))
			gpio_set_value(host->gpio_power,
					host->power_active_low);
		clk_disable_unprepare(host->clk);
		break;
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		host->cmdat &= ~JZ_MMC_CMDAT_BUS_WIDTH;
		host->cmdat |= JZ_MMC_CMDAT_BUS_WIDTH_1BIT;
		break;
	case MMC_BUS_WIDTH_4:
		host->cmdat &= ~JZ_MMC_CMDAT_BUS_WIDTH;
		host->cmdat |= JZ_MMC_CMDAT_BUS_WIDTH_4BIT;
		break;
	case MMC_BUS_WIDTH_8:
		host->cmdat &= ~JZ_MMC_CMDAT_BUS_WIDTH;
		host->cmdat |= JZ_MMC_CMDAT_BUS_WIDTH_8BIT;
		break;
	default:
		break;
	}
}

static void jz47xx_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct jz47xx_mmc_host *host = mmc_priv(mmc);
	jz47xx_mmc_set_irq_enabled(host, JZ_MMC_IRQ_SDIO, enable);
}

static const struct mmc_host_ops jz47xx_mmc_ops = {
	.request	= jz47xx_mmc_request,
	.set_ios	= jz47xx_mmc_set_ios,
	.get_ro		= mmc_gpio_get_ro,
	.get_cd		= mmc_gpio_get_cd,
	.enable_sdio_irq = jz47xx_mmc_enable_sdio_irq,
};

#ifdef CONFIG_MACH_JZ4740

static const struct jz_gpio_bulk_request jz4740_mmc_pins[] = {
	JZ_GPIO_BULK_PIN(MSC_CMD),
	JZ_GPIO_BULK_PIN(MSC_CLK),
	JZ_GPIO_BULK_PIN(MSC_DATA0),
	JZ_GPIO_BULK_PIN(MSC_DATA1),
	JZ_GPIO_BULK_PIN(MSC_DATA2),
	JZ_GPIO_BULK_PIN(MSC_DATA3),
};

static inline size_t jz4740_mmc_num_pins(struct jz47xx_mmc_host *host)
{
	size_t num_pins = ARRAY_SIZE(jz4740_mmc_pins);
	if (!(host->mmc->caps & MMC_CAP_4_BIT_DATA))
		num_pins -= 3;

	return num_pins;
}

#endif /* CONFIG_MACH_JZ4740 */

static struct platform_device_id jz47xx_mmc_id_table[] = {
	{
		.name	= "jz4740-mmc",
		.driver_data = JZ_MMC_JZ4740,
	}, {
		.name	= "jz4780-mmc",
		.driver_data = JZ_MMC_JZ4780,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, jz47xx_mmc_id_table);

static const struct of_device_id jz47xx_mmc_of_match[] = {
	{ .compatible = "ingenic,jz4740-mmc", .data = (void *)JZ_MMC_JZ4740 },
	{ .compatible = "ingenic,jz4780-mmc", .data = (void *)JZ_MMC_JZ4780 },
	{},
};
MODULE_DEVICE_TABLE(of, jz47xx_mmc_of_match);

static int jz47xx_mmc_probe(struct platform_device *pdev)
{
	int ret;
	struct mmc_host *mmc;
	struct jz47xx_mmc_host *host;
	struct jz47xx_mmc_platform_data *pdata;
	struct resource *res;
	const struct of_device_id *match;
	enum of_gpio_flags flags;

	mmc = mmc_alloc_host(sizeof(struct jz47xx_mmc_host), &pdev->dev);
	if (!mmc) {
		dev_err(&pdev->dev, "Failed to alloc mmc host structure\n");
		return -ENOMEM;
	}

	host = mmc_priv(mmc);

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		ret = host->irq;
		dev_err(&pdev->dev, "Failed to get platform irq: %d\n", ret);
		goto err_free_host;
	}

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		dev_err(&pdev->dev, "Failed to get mmc clock\n");
		goto err_free_host;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->phys_base = res->start;
	host->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		goto err_free_host;
	}

	/* If we fail to get a DMA channel we will fall back on PIO mode. */
	host->dmac = dma_request_slave_channel(&pdev->dev, "rx-tx");

	pdata = pdev->dev.platform_data;
	match = of_match_device(of_match_ptr(jz47xx_mmc_of_match), &pdev->dev);
	if (match) {
		host->version = (enum jz47xx_mmc_version)match->data;

		ret = mmc_of_parse(mmc);
		if (ret)
			goto err_free_host;

		host->gpio_power = of_get_named_gpio_flags(pdev->dev.of_node,
					"ingenic,power-gpio", 0, &flags);
		host->power_active_low = flags & OF_GPIO_ACTIVE_LOW;
	} else {
		host->version = platform_get_device_id(pdev)->driver_data;

		switch (pdata->bus_width) {
		case 8:
			mmc->caps |= MMC_CAP_8_BIT_DATA;
		case 4:
			mmc->caps |= MMC_CAP_4_BIT_DATA;
			break;
		case 1:
		case 0:
			/* Default to 1 if not specified. */
			break;
		default:
			dev_err(&pdev->dev, "Invalid bus_width value %u\n",
				pdata->bus_width);
			ret = -EINVAL;
			goto err_free_host;
		}

		mmc->f_max = pdata->max_freq;

		if (!pdata->card_detect_active_low)
			mmc->caps2 |= MMC_CAP2_CD_ACTIVE_HIGH;
		if (!pdata->read_only_active_low)
			mmc->caps2 |= MMC_CAP2_RO_ACTIVE_HIGH;

		if (gpio_is_valid(pdata->gpio_card_detect)) {
			ret = mmc_gpio_request_cd(mmc, pdata->gpio_card_detect,
						  0);
			if (ret)
				goto err_free_host;
		}

		if (gpio_is_valid(pdata->gpio_read_only)) {
			ret = mmc_gpio_request_ro(mmc, pdata->gpio_read_only);
			if (ret)
				goto err_free_host;
		}

		host->gpio_power = pdata->gpio_power;
		host->power_active_low = pdata->power_active_low;
	}

	/*
	 * Check that the bus width specified in the DT or platform data is
	 * supported. 8 bit bus width is only supported from the 4750 onward.
	 */
	if (mmc->caps & MMC_CAP_8_BIT_DATA && host->version < JZ_MMC_JZ4750) {
		dev_err(&pdev->dev, "8 bit bus width is unsupported\n");
		ret = -EINVAL;
		goto err_free_host;
	}

	if (gpio_is_valid(host->gpio_power)) {
		ret = devm_gpio_request_one(&pdev->dev, host->gpio_power,
				(host->power_active_low) ? GPIOF_INIT_HIGH : 0,
				"MMC power");
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to request power gpio: %d\n", ret);
			goto err_free_host;
		}
	}

#ifdef CONFIG_MACH_JZ4740
	ret = jz_gpio_bulk_request(jz4740_mmc_pins, jz4740_mmc_num_pins(host));
	if (ret) {
		dev_err(&pdev->dev, "Failed to request mmc pins: %d\n", ret);
		goto err_free_host;
	}
#endif

	mmc->ops = &jz47xx_mmc_ops;
	if (!mmc->f_max)
		mmc->f_max = JZ_MMC_CLK_RATE;
	mmc->f_min = mmc->f_max / 128;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps |= MMC_CAP_SDIO_IRQ;

	mmc->max_blk_size = (1 << 10) - 1;
	mmc->max_blk_count = (1 << 15) - 1;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;

	mmc->max_segs = 128;
	mmc->max_seg_size = mmc->max_req_size;

	host->mmc = mmc;
	host->pdev = pdev;
	spin_lock_init(&host->lock);
	host->irq_mask = 0xffffffff;

	ret = devm_request_threaded_irq(&pdev->dev, host->irq, jz47xx_mmc_irq,
			jz47xx_mmc_irq_worker, 0, dev_name(&pdev->dev), host);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", ret);
		goto err_gpio_bulk_free;
	}

	jz47xx_mmc_reset(host);
	jz47xx_mmc_clock_disable(host);
	setup_timer(&host->timeout_timer, jz47xx_mmc_timeout,
			(unsigned long)host);
	/* It is not important when it times out, it just needs to timeout. */
	set_timer_slack(&host->timeout_timer, HZ);

	platform_set_drvdata(pdev, host);

	ret = mmc_add_host(mmc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add mmc host: %d\n", ret);
		goto err_gpio_bulk_free;
	}

	dev_info(&pdev->dev, "JZ SD/MMC card driver registered\n");
	return 0;

err_gpio_bulk_free:
#ifdef CONFIG_MACH_JZ4740
	jz_gpio_bulk_free(jz4740_mmc_pins, jz4740_mmc_num_pins(host));
#endif
err_free_host:
	mmc_free_host(mmc);

	return ret;
}

static int jz47xx_mmc_remove(struct platform_device *pdev)
{
	struct jz47xx_mmc_host *host = platform_get_drvdata(pdev);

	del_timer_sync(&host->timeout_timer);
	jz47xx_mmc_set_irq_enabled(host, 0xff, false);
	jz47xx_mmc_reset(host);

	mmc_remove_host(host->mmc);

#ifdef CONFIG_MACH_JZ4740
	jz_gpio_bulk_free(jz4740_mmc_pins, jz4740_mmc_num_pins(host));
#endif

	if (host->dmac)
		dma_release_channel(host->dmac);

	mmc_free_host(host->mmc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int jz47xx_mmc_suspend(struct device *dev)
{
#ifdef CONFIG_MACH_JZ4740
	struct jz47xx_mmc_host *host = dev_get_drvdata(dev);

	jz_gpio_bulk_suspend(jz4740_mmc_pins, jz4740_mmc_num_pins(host));
#endif
	return 0;
}

static int jz47xx_mmc_resume(struct device *dev)
{
#ifdef CONFIG_MACH_JZ4740
	struct jz47xx_mmc_host *host = dev_get_drvdata(dev);

	jz_gpio_bulk_resume(jz4740_mmc_pins, jz4740_mmc_num_pins(host));
#endif
	return 0;
}

static SIMPLE_DEV_PM_OPS(jz47xx_mmc_pm_ops, jz47xx_mmc_suspend,
	jz47xx_mmc_resume);
#define JZ47XX_MMC_PM_OPS (&jz47xx_mmc_pm_ops)
#else
#define JZ47XX_MMC_PM_OPS NULL
#endif

static struct platform_driver jz47xx_mmc_driver = {
	.probe = jz47xx_mmc_probe,
	.remove = jz47xx_mmc_remove,
	.id_table = jz47xx_mmc_id_table,
	.driver = {
		.name = "jz47xx-mmc",
		.owner = THIS_MODULE,
		.pm = JZ47XX_MMC_PM_OPS,
		.of_match_table = of_match_ptr(jz47xx_mmc_of_match),
	},
};

module_platform_driver(jz47xx_mmc_driver);

MODULE_DESCRIPTION("JZ47xx SD/MMC controller driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");

