/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#include "ingenic-tcu.h"

#define NUM_TCU_IRQS 3

enum ingenic_tcu_reg {
	REG_TER		= 0x10,
	REG_TESR	= 0x14,
	REG_TECR	= 0x18,
	REG_TSR		= 0x1c,
	REG_TFR		= 0x20,
	REG_TFSR	= 0x24,
	REG_TFCR	= 0x28,
	REG_TSSR	= 0x2c,
	REG_TMR		= 0x30,
	REG_TMSR	= 0x34,
	REG_TMCR	= 0x38,
	REG_TSCR	= 0x3c,
	REG_TDFR0	= 0x40,
	REG_TDHR0	= 0x44,
	REG_TCNT0	= 0x48,
	REG_TCSR0	= 0x4c,
	REG_OSTDR	= 0xe0,
	REG_OSTCNTL	= 0xe4,
	REG_OSTCNTH	= 0xe8,
	REG_OSTCSR	= 0xec,
	REG_TSTR	= 0xf0,
	REG_TSTSR	= 0xf4,
	REG_TSTCR	= 0xf8,
	REG_OSTCNTHBUF	= 0xfc,
};

#define CHANNEL_STRIDE		0x10
#define REG_TDFRc(c)		(REG_TDFR0 + (c * CHANNEL_STRIDE))
#define REG_TDHRc(c)		(REG_TDHR0 + (c * CHANNEL_STRIDE))
#define REG_TCNTc(c)		(REG_TCNT0 + (c * CHANNEL_STRIDE))
#define REG_TCSRc(c)		(REG_TCSR0 + (c * CHANNEL_STRIDE))

#define TFR_HMASK_SHIFT		16

#define TMR_HMASK_SHIFT		16

#define TCSR_PRESCALE_SHIFT	3
#define TCSR_PRESCALE_MASK	(0x7 << TCSR_PRESCALE_SHIFT)
#define TCSR_EXT_EN		(1 << 2)
#define TCSR_RTC_EN		(1 << 1)
#define TCSR_PCK_EN		(1 << 0)
#define TCSR_SRC_MASK		(TCSR_EXT_EN | TCSR_RTC_EN | TCSR_PCK_EN)

#define OSTCSR_CNT_MD		(1 << 15)

struct ingenic_tcu_channel {
	struct ingenic_tcu *tcu;
	unsigned idx;
	ingenic_tcu_irq_callback *full_cb, *half_cb;
	void *full_cb_data, *half_cb_data;
	unsigned stopped: 1;
	unsigned enabled: 1;
	int cpu;
};

struct ingenic_tcu_irq {
	struct ingenic_tcu *tcu;
	unsigned long channel_map;
	unsigned channel;
	int virq;
};

struct ingenic_tcu {
	const struct ingenic_tcu_desc *desc;
	void __iomem *base;
	struct ingenic_tcu_channel *channels;
	struct ingenic_tcu_irq irqs[NUM_TCU_IRQS];
};

struct ingenic_tcu_channel_list {
	struct list_head list;
	struct ingenic_tcu_channel *channel;
};

static struct ingenic_tcu_channel_list req_channels;

static void ingenic_per_cpu_event_handle(void *info)
{
	struct clock_event_device *cevt = (struct clock_event_device *) info;

	if (cevt->event_handler)
		cevt->event_handler(cevt);
}

static void ingenic_tcu_per_cpu_cb(struct ingenic_tcu_channel *channel)
{
	int cpu = channel->cpu;
	struct clock_event_device *cevt = channel->full_cb_data;

	if (smp_processor_id() == cpu) {
		if (cevt->event_handler)
			cevt->event_handler(cevt);
	} else {
		arch_local_irq_enable();
		smp_call_function_single(cpu, ingenic_per_cpu_event_handle, (void*) cevt, 0);
		arch_local_irq_disable();
	}
}

static inline u32 notrace tcu_readl(struct ingenic_tcu *tcu, enum ingenic_tcu_reg reg)
{
	return readl(tcu->base + reg);
}

static inline void tcu_writel(struct ingenic_tcu *tcu, u32 val,
			      enum ingenic_tcu_reg reg)
{
	writel(val, tcu->base + reg);
}

static enum ingenic_tcu_reg tcu_csr(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;

	if (tcu->desc->channels[channel->idx].flags & INGENIC_TCU_CHANNEL_OST)
		return REG_OSTCSR;

	return REG_TCSRc(channel->idx);
}

static void ingenic_tcu_stop_channel(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	tcu_writel(tcu, 1 << channel->idx, REG_TSSR);
	channel->stopped = true;
}

static void ingenic_tcu_start_channel(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	tcu_writel(tcu, 1 << channel->idx, REG_TSCR);
	channel->stopped = false;
}

void ingenic_tcu_enable_channel(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	tcu_writel(tcu, 1 << channel->idx, REG_TESR);
	channel->enabled = true;
}

void ingenic_tcu_disable_channel(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	tcu_writel(tcu, 1 << channel->idx, REG_TECR);
	channel->enabled = false;
}

void ingenic_tcu_mask_channel_full(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	tcu_writel(tcu, 1 << channel->idx, REG_TMSR);
}

void ingenic_tcu_unmask_channel_full(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	tcu_writel(tcu, 1 << channel->idx, REG_TMCR);
}

void ingenic_tcu_mask_channel_half(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	tcu_writel(tcu, 1 << (TMR_HMASK_SHIFT + channel->idx), REG_TMSR);
}

void ingenic_tcu_unmask_channel_half(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	tcu_writel(tcu, 1 << (TMR_HMASK_SHIFT + channel->idx), REG_TMCR);
}

static irqreturn_t ingenic_tcu_single_channel_irq(int irq, void *dev_id)
{
	struct ingenic_tcu_irq *tcu_irq = dev_id;
	struct ingenic_tcu *tcu = tcu_irq->tcu;
	struct ingenic_tcu_channel *chan;
	unsigned c = tcu_irq->channel;
	unsigned pending, ack;

	pending = tcu_readl(tcu, REG_TFR);
	chan = &tcu->channels[c];
	ack = 0;

	/* Callback for half full interrupt */
	if (pending & (1 << (TFR_HMASK_SHIFT + c))) {
		ack |= 1 << (TFR_HMASK_SHIFT + c);
		if (chan->half_cb)
			chan->half_cb(chan, chan->half_cb_data);
	}

	/* Callback for full interrupt */
	if (pending & (1 << c)) {
		ack |= 1 << c;
		if (chan->full_cb)
			chan->full_cb(chan, chan->full_cb_data);
	}

	/* Clear the match flags */
	tcu_writel(tcu, ack, REG_TFCR);

	return IRQ_HANDLED;
}

static irqreturn_t ingenic_tcu_irq(int irq, void *dev_id)
{
	struct ingenic_tcu_irq *tcu_irq = dev_id;
	struct ingenic_tcu *tcu = tcu_irq->tcu;
	struct ingenic_tcu_channel *chan;
	unsigned c, pending, ack;
	unsigned long pending_f, pending_h;

	pending = tcu_readl(tcu, REG_TFR);
	pending_f = pending & tcu_irq->channel_map;
	pending_h = (pending >> TFR_HMASK_SHIFT) & tcu_irq->channel_map;
	ack = 0;

	/* Callbacks for any pending half full interrupts */
	for_each_set_bit(c, &pending_h, tcu->desc->num_channels) {
		WARN_ON_ONCE(!tcu->desc->channels[c].present);

		chan = &tcu->channels[c];
		ack |= 1 << (TFR_HMASK_SHIFT + c);

		if (chan->half_cb)
			chan->half_cb(chan, chan->half_cb_data);
	}

	/* Callbacks for any pending full interrupts */
	for_each_set_bit(c, &pending_f, tcu->desc->num_channels) {
		WARN_ON_ONCE(!tcu->desc->channels[c].present);

		chan = &tcu->channels[c];
		ack |= 1 << c;

		if (chan->full_cb)
			chan->full_cb(chan, chan->full_cb_data);
	}

	/* Clear the match flags */
	tcu_writel(tcu, ack, REG_TFCR);

	return IRQ_HANDLED;
}

static struct irqaction ingenic_tcu_irqaction[NUM_TCU_IRQS] = {
	{
		.flags = IRQF_TIMER,
		.name = "ingenic-tcu-irq0",
	},
	{
		.flags = IRQF_TIMER,
		.name = "ingenic-tcu-irq1",
	},
	{
		.flags = IRQF_TIMER,
		.name = "ingenic-tcu-irq2",
	},
};

static int setup_tcu_irq(struct ingenic_tcu *tcu, unsigned i)
{
	unsigned channel_count;

	channel_count = bitmap_weight(&tcu->irqs[i].channel_map,
				      tcu->desc->num_channels);
	if (channel_count == 1) {
		tcu->irqs[i].channel = find_first_bit(&tcu->irqs[i].channel_map,
						      tcu->desc->num_channels);

		ingenic_tcu_irqaction[i].handler = ingenic_tcu_single_channel_irq;
	} else {
		ingenic_tcu_irqaction[i].handler = ingenic_tcu_irq;
	}

	ingenic_tcu_irqaction[i].dev_id = &tcu->irqs[i];

	return setup_irq(tcu->irqs[i].virq, &ingenic_tcu_irqaction[i]);
}

struct ingenic_tcu *ingenic_tcu_init(const struct ingenic_tcu_desc *desc,
				   struct device_node *np)
{
	struct ingenic_tcu *tcu;
	unsigned i;
	int err;

	tcu = kzalloc(sizeof(*tcu), GFP_KERNEL);
	if (!tcu) {
		err = -ENOMEM;
		goto out;
	}

	tcu->channels = kzalloc(sizeof(*tcu->channels) * desc->num_channels,
				GFP_KERNEL);
	if (!tcu->channels) {
		err = -ENOMEM;
		goto out_free;
	}

	tcu->desc = desc;

	/* Map TCU registers */
	tcu->base = of_iomap(np, 0);
	if (!tcu->base) {
		err = -EINVAL;
		goto out_free;
	}

	INIT_LIST_HEAD(&req_channels.list);

	/* Initialise all channels as stopped & calculate IRQ maps */
	for (i = 0; i < tcu->desc->num_channels; i++) {
		tcu->channels[i].tcu = tcu;
		tcu->channels[i].idx = i;
		ingenic_tcu_stop_channel(&tcu->channels[i]);
		set_bit(i, &tcu->irqs[tcu->desc->channels[i].irq].channel_map);
	}

	/* Map IRQs */
	for (i = 0; i < NUM_TCU_IRQS; i++) {
		tcu->irqs[i].tcu = tcu;

		tcu->irqs[i].virq = irq_of_parse_and_map(np, i);
		if (!tcu->irqs[i].virq) {
			err = -EINVAL;
			goto out_irq_dispose;
		}

		err = setup_tcu_irq(tcu, i);
		if (err)
			goto out_irq_dispose;
	}

	return tcu;
out_irq_dispose:
	for (i = 0; i < ARRAY_SIZE(tcu->irqs); i++) {
		remove_irq(tcu->irqs[i].virq, &ingenic_tcu_irqaction[i]);
		irq_dispose_mapping(tcu->irqs[i].virq);
	}
	iounmap(tcu->base);
out_free:
	kfree(tcu->channels);
	kfree(tcu);
out:
	return ERR_PTR(err);
}

struct ingenic_tcu_channel *ingenic_tcu_req_channel(struct ingenic_tcu *tcu,
						  int idx)
{
	struct ingenic_tcu_channel *channel;
	unsigned c;

	if (idx == -1) {
		for (c = 0; c < tcu->desc->num_channels; c++) {
			if (!tcu->desc->channels[c].present)
				continue;
			if (!tcu->channels[c].stopped)
				continue;
			idx = c;
			break;
		}
		if (idx == -1)
			return ERR_PTR(-ENODEV);
	}

	channel = &tcu->channels[idx];

	if (!channel->stopped)
		return ERR_PTR(-EBUSY);

	ingenic_tcu_mask_channel_half(channel);
	ingenic_tcu_mask_channel_full(channel);
	ingenic_tcu_start_channel(channel);
	ingenic_tcu_disable_channel(channel);

	if (tcu->desc->channels[channel->idx].flags & INGENIC_TCU_CHANNEL_OST)
		tcu_writel(tcu, OSTCSR_CNT_MD, tcu_csr(channel));
	else
		tcu_writel(tcu, 0, tcu_csr(channel));

	return channel;
}


static void ingenic_tcu_remove_req_channel(struct ingenic_tcu_channel *channel)
{
	struct list_head *iterator;
	struct ingenic_tcu_channel_list *list_element;

	list_for_each(iterator, &req_channels.list) {
		list_element = list_entry(iterator,
			    struct ingenic_tcu_channel_list, list);
		if (list_element->channel == channel) {
			list_del(list_element);
			break;
		}
	}
}

void ingenic_tcu_release_channel(struct ingenic_tcu_channel *channel)
{
	ingenic_tcu_stop_channel(channel);
	ingenic_tcu_remove_req_channel(channel);
}

unsigned ingenic_tcu_set_channel_rate(struct ingenic_tcu_channel *channel,
				     enum ingenic_tcu_source source,
				     unsigned rate)
{
	struct ingenic_tcu *tcu = channel->tcu;
	enum ingenic_tcu_reg csr_reg;
	struct clk *src_clk;
	unsigned long src_rate;
	u32 csr, div, div_log2;

	/*
	 * Source & prescale can only be changed whilst the counter isn't
	 * counting.
	 */
	if (channel->enabled)
		return 0;

	csr_reg = tcu_csr(channel);
	csr = tcu_readl(tcu, csr_reg);
	csr &= ~(TCSR_PRESCALE_MASK | TCSR_SRC_MASK);

	switch (source) {
	case INGENIC_TCU_SRC_PCLK:
		src_clk = clk_get(NULL, "pclk");
		csr |= TCSR_PCK_EN;
		break;
	case INGENIC_TCU_SRC_RTCCLK:
		src_clk = clk_get(NULL, "rtc");
		csr |= TCSR_RTC_EN;
		break;
	case INGENIC_TCU_SRC_EXTAL:
		src_clk = clk_get(NULL, "ext");
		csr |= TCSR_EXT_EN;
		break;
	default:
		return 0;
	}

	if (IS_ERR(src_clk)) {
		pr_info("%s failed to get src_clk: %ld\n", __func__,
			PTR_ERR(src_clk));
		return 0;
	}

	src_rate = clk_get_rate(src_clk);
	clk_put(src_clk);

	div = DIV_ROUND_CLOSEST(src_rate, rate);
	div_log2 = min(ilog2(div) & ~0x1, 10);
	div = 1 << div_log2;
	csr |= (div_log2 >> 1) << TCSR_PRESCALE_SHIFT;

	tcu_writel(tcu, csr, csr_reg);

	return src_rate / div;
}

unsigned ingenic_tcu_get_channel_rate(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	struct clk *src_clk;
	unsigned long src_rate;
	u32 csr, prescale, div;

	csr = tcu_readl(tcu, tcu_csr(channel));

	if (csr & TCSR_PCK_EN)
		src_clk = clk_get(NULL, "pclk");
	else if (csr & TCSR_RTC_EN)
		src_clk = clk_get(NULL, "rtc");
	else if (csr & TCSR_EXT_EN)
		src_clk = clk_get(NULL, "ext");
	else
		return 0;

	if (IS_ERR(src_clk)) {
		pr_info("%s failed to get src_clk: %ld\n", __func__,
			PTR_ERR(src_clk));
		return 0;
	}

	src_rate = clk_get_rate(src_clk);
	clk_put(src_clk);

	prescale = (csr & TCSR_PRESCALE_MASK) >> TCSR_PRESCALE_SHIFT;
	div = 1 << (prescale * 2);

	return src_rate / div;
}

u64 notrace ingenic_tcu_read_channel_count(struct ingenic_tcu_channel *channel)
{
	struct ingenic_tcu *tcu = channel->tcu;
	u64 count;

	if (tcu->desc->channels[channel->idx].flags & INGENIC_TCU_CHANNEL_OST) {
		count = tcu_readl(tcu, REG_OSTCNTL);
		count |= (u64)tcu_readl(tcu, REG_OSTCNTHBUF) << 32;
	} else {
		count = tcu_readl(tcu, REG_TCNTc(channel->idx));
	}

	return count;
}

int ingenic_tcu_set_channel_count(struct ingenic_tcu_channel *channel,
				 u64 count)
{
	struct ingenic_tcu *tcu = channel->tcu;

	if (tcu->desc->channels[channel->idx].flags & INGENIC_TCU_CHANNEL_OST) {
		tcu_writel(tcu, count, REG_OSTCNTL);
		tcu_writel(tcu, count >> 32, REG_OSTCNTH);
		return 0;
	}

	if (count > 0xffff)
		return -EINVAL;

	tcu_writel(tcu, count, REG_TCNTc(channel->idx));
	return 0;
}

int ingenic_tcu_set_channel_full(struct ingenic_tcu_channel *channel,
				unsigned data)
{
	struct ingenic_tcu *tcu = channel->tcu;

	if (tcu->desc->channels[channel->idx].flags & INGENIC_TCU_CHANNEL_OST) {
		tcu_writel(tcu, data, REG_OSTDR);
		return 0;
	}

	if (data > 0xffff)
		return -EINVAL;

	tcu_writel(tcu, data, REG_TDFRc(channel->idx));
	return 0;
}

int ingenic_tcu_set_channel_half(struct ingenic_tcu_channel *channel,
				unsigned data)
{
	struct ingenic_tcu *tcu = channel->tcu;

	if (tcu->desc->channels[channel->idx].flags & INGENIC_TCU_CHANNEL_OST)
		return -EINVAL;

	if (data > 0xffff)
		return -EINVAL;

	tcu_writel(tcu, data, REG_TDFRc(channel->idx));
	return 0;
}

void ingenic_tcu_set_channel_full_cb(struct ingenic_tcu_channel *channel,
				    ingenic_tcu_irq_callback *cb, void *data)
{
	channel->full_cb = cb;
	channel->full_cb_data = data;
}

void ingenic_tcu_set_channel_half_cb(struct ingenic_tcu_channel *channel,
				    ingenic_tcu_irq_callback *cb, void *data)
{
	channel->half_cb = cb;
	channel->half_cb_data = data;
}

struct ingenic_clock_event_device {
	struct clock_event_device cevt;
	struct ingenic_tcu_channel *channel;
	char name[32];
};

#define ingenic_cevt(_evt) \
	container_of(_evt, struct ingenic_clock_event_device, cevt)

static void ingenic_tcu_cevt_cb(struct ingenic_tcu_channel *channel, void *data)
{
	struct clock_event_device *cevt = data;

	ingenic_tcu_disable_channel(channel);
	ingenic_tcu_per_cpu_cb(channel);
}

static int ingenic_tcu_cevt_set_next(unsigned long next,
				    struct clock_event_device *evt)
{
	struct ingenic_clock_event_device *jzcevt = ingenic_cevt(evt);
	struct ingenic_tcu_channel *channel = jzcevt->channel;

	ingenic_tcu_set_channel_full(channel, next);
	ingenic_tcu_set_channel_count(channel, 0);
	ingenic_tcu_enable_channel(channel);

	return 0;
}

void ingenic_tcu_enable_clocks(void) {
	struct list_head *iterator;
	struct ingenic_tcu_channel_list *list_element;

	list_for_each(iterator, &req_channels.list) {
		list_element = list_entry(iterator,
			    struct ingenic_tcu_channel_list, list);
		ingenic_tcu_enable_channel(list_element->channel);
	}
}

void ingenic_tcu_disable_clocks(void) {
	struct list_head *iterator;
	struct ingenic_tcu_channel_list *list_element;

	list_for_each(iterator, &req_channels.list) {
		list_element = list_entry(iterator,
			    struct ingenic_tcu_channel_list, list);
		ingenic_tcu_disable_channel(list_element->channel);
	}
}

int ingenic_tcu_setup_cevt(struct ingenic_tcu *tcu, int idx)
{
	struct ingenic_tcu_channel *channel;
	struct ingenic_clock_event_device *jzcevt;
	struct ingenic_tcu_channel_list *list_element;
	unsigned rate;
	int err;

	channel = ingenic_tcu_req_channel(tcu, idx);
	if (IS_ERR(channel)) {
		err = PTR_ERR(channel);
		goto err_out;
	}

	channel->cpu = smp_processor_id();

	list_element = (struct ingenic_tcu_channel_list*)
	    kmalloc(sizeof(struct ingenic_tcu_channel_list),
	    GFP_KERNEL);

	if (!list_element) {
		err= -ENOMEM;
		goto err_out_release;
	}

	list_element->channel = channel;
	list_add_tail(&list_element->list, &req_channels.list);

	rate = ingenic_tcu_set_channel_rate(channel, INGENIC_TCU_SRC_EXTAL,
					   1000000);
	if (!rate) {
		err = -EINVAL;
		goto err_out_free_list_element;
	}

	jzcevt = kzalloc(sizeof(*jzcevt), GFP_KERNEL);
	if (!jzcevt) {
		err = -ENOMEM;
		goto err_out_free_list_element;
	}

	jzcevt->channel = channel;
	snprintf(jzcevt->name, sizeof(jzcevt->name), "jz478-tcu-chan%u",
		 channel->idx);

	jzcevt->cevt.cpumask = cpumask_of(smp_processor_id());
	jzcevt->cevt.features = CLOCK_EVT_FEAT_ONESHOT;
	jzcevt->cevt.name = jzcevt->name;
	jzcevt->cevt.rating = 200;
	jzcevt->cevt.set_next_event = ingenic_tcu_cevt_set_next;

	ingenic_tcu_set_channel_full_cb(channel, ingenic_tcu_cevt_cb,
				       &jzcevt->cevt);
	ingenic_tcu_unmask_channel_full(channel);
	clockevents_config_and_register(&jzcevt->cevt, rate, 10, (1 << 16) - 1);

	return 0;

err_out_free_list_element:
	kfree(list_element);
err_out_release:
	ingenic_tcu_release_channel(channel);
err_out:
	return err;
}

int ingenic_tcu_reregister_cevt(void)
{
	struct list_head *iterator;
	struct clock_event_device *cevt;
	struct ingenic_tcu_channel *channel;
	struct ingenic_tcu_channel_list *list_element;
	unsigned rate;
	int cpu = smp_processor_id();
	int cevt_num = 0;

	list_for_each(iterator, &req_channels.list) {
		list_element = list_entry(iterator,
			    struct ingenic_tcu_channel_list, list);
		if (list_element->channel->cpu == cpu) {
			channel = list_element->channel;
			rate = ingenic_tcu_get_channel_rate(channel);
			cevt = list_element->channel->full_cb_data;

			clockevents_config_and_register(cevt, rate,
				    10, (1 << 16) - 1);

			cevt_num++;
		}
	}

	return cevt_num;
}
