/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clocksource.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/sched_clock.h>

#include "ingenic-tcu.h"

static cycle_t jz4780_tcu_clocksource_read(struct clocksource *cs);

static struct ingenic_tcu_channel_desc jz4780_tcu_channels[16] = {
	[0] = INGENIC_TCU_CHANNEL(2, INGENIC_TCU_CHANNEL_FIFO),
	[1] = INGENIC_TCU_CHANNEL(2, 0),
	[2] = INGENIC_TCU_CHANNEL(2, 0),
	[3] = INGENIC_TCU_CHANNEL(2, INGENIC_TCU_CHANNEL_FIFO),
	[4] = INGENIC_TCU_CHANNEL(2, INGENIC_TCU_CHANNEL_FIFO),
	[5] = INGENIC_TCU_CHANNEL(1, INGENIC_TCU_CHANNEL_FIFO),
	[6] = INGENIC_TCU_CHANNEL(2, 0),
	[7] = INGENIC_TCU_CHANNEL(2, 0),

	[15] = INGENIC_TCU_CHANNEL(0, INGENIC_TCU_CHANNEL_OST),
};

static struct ingenic_tcu_desc jz4780_tcu_desc = {
	.channels = jz4780_tcu_channels,
	.num_channels = ARRAY_SIZE(jz4780_tcu_channels),
};

static struct {
	struct clocksource cs;
	struct ingenic_tcu_channel *channel;
} jz4780_tcu_clocksource = {
	.cs = {
		.name	= "jz4780-tcu-clocksource",
		.rating	= 200,
		.read	= jz4780_tcu_clocksource_read,
		.mask	= CLOCKSOURCE_MASK(64),
		.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
	},
};

static cycle_t jz4780_tcu_clocksource_read(struct clocksource *cs)
{
	return ingenic_tcu_read_channel_count(jz4780_tcu_clocksource.channel);
}

static u64 notrace jz4780_tcu_sched_read(void)
{
	return ingenic_tcu_read_channel_count(jz4780_tcu_clocksource.channel);
}

#ifdef CONFIG_SMP
struct jz4780_tcu_notifier_block {
	struct notifier_block nb;
	struct ingenic_tcu *tcu;
	int channel_number;
};

static int jz4780_setup_cevt_notify(struct notifier_block *self,
				      unsigned long action, void *hcpu)
{
	struct jz4780_tcu_notifier_block *jz4780_nb;
	struct ingenic_tcu *tcu;
	int channel_number;
	int err;

	if ((action & ~CPU_TASKS_FROZEN) == CPU_STARTING) {
		/*
		 * Re-register existing clock device when
		 * waking up from suspend.
		 */

		if (ingenic_tcu_reregister_cevt()) {
			return NOTIFY_OK;
		}

		/*
		 * Register new clock event device if none is existing.
		 */
		jz4780_nb = container_of(self, struct jz4780_tcu_notifier_block, nb);
		tcu = jz4780_nb->tcu;
		channel_number = jz4780_nb->channel_number;

		err = ingenic_tcu_setup_cevt(tcu, channel_number);
		BUG_ON(err);
	}

	return NOTIFY_OK;
}

struct jz4780_tcu_notifier_block jz4780_nb_channel7 = {
	.nb = {
		.notifier_call = jz4780_setup_cevt_notify,
	},
	.channel_number = 7,
};

void jz4780_schedule_setup_cevt_notify(struct notifier_block *nb) {
	cpu_notifier_register_begin();
	 __register_cpu_notifier(nb);
	cpu_notifier_register_done();
}
#endif /* CONFIG_SMP */

static void __init jz4780_tcu_init(struct device_node *np)
{
	struct ingenic_tcu *tcu;
	unsigned rate;
	int err;

	tcu = ingenic_tcu_init(&jz4780_tcu_desc, np);
	BUG_ON(IS_ERR(tcu));

	jz4780_tcu_clocksource.channel = ingenic_tcu_req_channel(tcu, 15);
	BUG_ON(IS_ERR(jz4780_tcu_clocksource.channel));

	rate = ingenic_tcu_set_channel_rate(jz4780_tcu_clocksource.channel,
					   INGENIC_TCU_SRC_EXTAL, 1000000);
	pr_info("jz4780-tcu-clocksource: OST rate is %uHz\n", rate);
	BUG_ON(!rate);

	ingenic_tcu_enable_channel(jz4780_tcu_clocksource.channel);

	err = clocksource_register_hz(&jz4780_tcu_clocksource.cs, rate);
	BUG_ON(err);
	sched_clock_register(jz4780_tcu_sched_read, 64, rate);

	/* For local clock events */
	err = ingenic_tcu_setup_cevt(tcu, 5);
	BUG_ON(err);

	/* For tick broadcasts */
	err = ingenic_tcu_setup_cevt(tcu, 6);
	BUG_ON(err);

#ifdef CONFIG_SMP
	/* Channel 7 clock events setup on the second CPU core */
	jz4780_nb_channel7.tcu = tcu;
	jz4780_schedule_setup_cevt_notify(&jz4780_nb_channel7.nb);
#endif /* CONFIG_SMP */
}

CLOCKSOURCE_OF_DECLARE(jz4780_tcu, "ingenic,jz4780-tcu", jz4780_tcu_init);
