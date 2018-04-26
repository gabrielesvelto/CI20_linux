/*
 * This is a simplified version of cache flushing interface
 * adapted from arch/mips/include/asm/r4kcache.h
 *
 * Copyright (C) 2016 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */
#include "cache.h"

#if defined(CONFIG_MACH_JZ4780)
struct cpuinfo_mips cpu_data = {
	.dcache = {
		.waysize = 4096,
		.ways 	 = 8,
		.waybit  = 12,
		.linesz  = 32,
	},
	.icache = {
		.waysize = 4096,
		.ways 	 = 8,
		.waybit  = 12,
		.linesz  = 32,
	},
};

BUILD_BLAST_CACHE(icache, IndexInvalidate_I, 32)
BUILD_BLAST_CACHE(dcache, IndexWriteBack_D, 32)

void cache_flush_all(void) {
	blast_dcache32();
	blast_icache32();
	SYNC;
}
#else
#warning Empty cache_flush_all() implementation used!!

struct cpuinfo_mips cpu_data;

void cache_flush_all(void) {
	;
}

#endif
