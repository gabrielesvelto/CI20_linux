/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _DRIVERS_CLOCKSOURCE_INGENIC_TCU_H_
#define _DRIVERS_CLOCKSOURCE_INGENIC_TCU_H_

#include <linux/of.h>
#include <linux/types.h>

struct ingenic_tcu_channel_desc {
	u8 irq;
	u8 present: 1;
	unsigned flags;
#define INGENIC_TCU_CHANNEL_FIFO	(1 << 0)
#define INGENIC_TCU_CHANNEL_OST	(1 << 1)
};

#define INGENIC_TCU_CHANNEL(_irq, _flags) {	\
	.irq = _irq,				\
	.present = 1,				\
	.flags = _flags,			\
}

struct ingenic_tcu_desc {
	struct ingenic_tcu_channel_desc *channels;
	unsigned num_channels;
};

struct ingenic_tcu;
struct ingenic_tcu_channel;

typedef void (ingenic_tcu_irq_callback)(struct ingenic_tcu_channel *channel,
				       void *data);

enum ingenic_tcu_source {
	INGENIC_TCU_SRC_PCLK,
	INGENIC_TCU_SRC_RTCCLK,
	INGENIC_TCU_SRC_EXTAL,
};

/**
 * ingenic_tcu_init - initialise Ingenic jz47xx timer/counter unit (TCU)
 * desc: a description of the TCU in this SoC
 * np: the DT node representing the TCU
 */
extern struct ingenic_tcu *ingenic_tcu_init(const struct ingenic_tcu_desc *desc,
					  struct device_node *np);

/**
 * ingenic_tcu_req_channel - request a channel
 * tcu: the TCU reference returned by ingenic_tcu_init
 * idx: the channel to request, or -1 for any available channel
 */
extern struct ingenic_tcu_channel *ingenic_tcu_req_channel(struct ingenic_tcu *tcu,
							 int idx);

/**
 * ingenic_tcu_release_channel - release a channel
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 */
extern void ingenic_tcu_release_channel(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_setup_cevt - setup a clock event device using a channel
 * tcu: the TCU reference returned by ingenic_tcu_init
 * idx: the channel to request, or -1 for any available channel
 */
extern int ingenic_tcu_setup_cevt(struct ingenic_tcu *tcu, int idx);

/**
 * ingenic_tcu_set_channel_rate - set the count rate of a channel
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 * source: the source clock to be used
 * rate: the target count frequency in Hz
 *
 * Sets the channel up to use the specified source clock and count at a
 * frequency as close as possible to rate. Returns the actual frequency that
 * was set, or 0 on error.
 */
extern unsigned ingenic_tcu_set_channel_rate(struct ingenic_tcu_channel *channel,
					    enum ingenic_tcu_source source,
					    unsigned rate);

/**
 * ingenic_tcu_set_channel_rate - set the count rate of a channel
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 *
 * Returns the rate at which the channel is configured to count, or 0 on error.
 */
extern unsigned ingenic_tcu_get_channel_rate(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_enable_channel - start a channel counting
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 */
extern void ingenic_tcu_enable_channel(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_disable_channel - stop a channel counting
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 */
extern void ingenic_tcu_disable_channel(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_mask_channel_full - mask a channels full interrupt
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 */
extern void ingenic_tcu_mask_channel_full(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_unmask_channel_full - unmask a channels full interrupt
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 */
extern void ingenic_tcu_unmask_channel_full(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_mask_channel_full - mask a channels half full interrupt
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 */
extern void ingenic_tcu_mask_channel_half(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_unmask_channel_full - unmask a channels half full interrupt
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 */
extern void ingenic_tcu_unmask_channel_half(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_read_channel_count - read the current channel count
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 */
extern u64 ingenic_tcu_read_channel_count(struct ingenic_tcu_channel *channel);

/**
 * ingenic_tcu_set_channel_count - set the current channel count
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 * count: the count to set
 *
 * Returns zero on success, else -errno.
 */
extern int ingenic_tcu_set_channel_count(struct ingenic_tcu_channel *channel,
					u64 count);

/**
 * ingenic_tcu_set_channel_full - set value at which the full interrupt triggers
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 * data: the value at which the full interrupt will trigger
 *
 * Returns zero on success, else -errno.
 */
extern int ingenic_tcu_set_channel_full(struct ingenic_tcu_channel *channel,
				       unsigned data);

/**
 * ingenic_tcu_set_channel_half - set value at which the half interrupt triggers
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 * data: the value at which the half full interrupt will trigger
 *
 * Returns zero on success, else -errno.
 */
extern int ingenic_tcu_set_channel_half(struct ingenic_tcu_channel *channel,
				       unsigned data);

/**
 * ingenic_tcu_set_channel_full_cb - set function to call upon a full interrupt
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 * cb: pointer to the function to be called
 * data: user-provided data to be passed to cb
 */
extern void ingenic_tcu_set_channel_full_cb(struct ingenic_tcu_channel *channel,
					   ingenic_tcu_irq_callback *cb,
					   void *data);

/**
 * ingenic_tcu_set_channel_half_cb - set function to call upon a half interrupt
 * channel: the TCU channel reference returned by ingenic_tcu_req_channel
 * cb: pointer to the function to be called
 * data: user-provided data to be passed to cb
 */
extern void ingenic_tcu_set_channel_half_cb(struct ingenic_tcu_channel *channel,
					   ingenic_tcu_irq_callback *cb,
					   void *data);

/**
 * ingenic_tcu_reregister_cevt - register previously used clock event
 * devices for the running CPU. Only clock event devices registered for this
 * particular CPU will be assigned. This function was implemented for
 * re-registering clock event devices for the second CPU core, after waking up
 * from suspend.
 *
 * Returns number of re-registered clock event devices.
 */
extern int ingenic_tcu_reregister_cevt(void);

#endif /* _DRIVERS_CLOCKSOURCE_INGENIC_TCU_H_ */
